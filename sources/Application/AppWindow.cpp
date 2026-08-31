#include "AppWindow.h"

#if defined(PLATFORM_PSP) && defined(PSP_ME_OFFLOAD)
extern "C" int PSPME_ReadSpectrum(int *bars32);
#endif
#include "Application/Version.h"
#include "Application/Commands/ApplicationCommandDispatcher.h"
#include "Application/Commands/EventDispatcher.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/DrumKit.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Groove.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Table.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/char.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/ModalDialogs/SelectProjectDialog.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Player/Player.h"
#include "Player/MidiNoteInput.h"
#include "Services/Audio/AudioStats.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "UIFramework/Interfaces/I_GUIWindowFactory.h"
#include "Views/UIController.h"
#include <stdlib.h>
#include <string.h>

AppWindow *instance = 0;

GUIColor AppWindow::backgroundColor_(0x1D, 0x0A, 0x1F);
GUIColor AppWindow::normalColor_(0xF5, 0xEB, 0xFF);
GUIColor AppWindow::borderColor_(0xFF, 0x00, 0x8C);
GUIColor AppWindow::songviewfeColor_(0xA5, 0x5B, 0x8F);
GUIColor AppWindow::songview00Color_(0x85, 0x3B, 0x6F);
GUIColor AppWindow::highlightColor_(0xB7, 0x50, 0xD1);
GUIColor AppWindow::highlight2Color_(0xDB, 0x33, 0xDB);
GUIColor AppWindow::consoleColor_(0x00, 0xFF, 0x00);
GUIColor AppWindow::cursorColor_(0xFF, 0x00, 0x8C);
GUIColor AppWindow::playColor_(0xFF, 0x00, 0x8C);
GUIColor AppWindow::muteColor_(0xF5, 0xEB, 0xFF);
GUIColor AppWindow::rownumberColor_(0xBA, 0x28, 0xF9);
GUIColor AppWindow::rownumber2Color_(0xFF, 0x00, 0xFF);
GUIColor AppWindow::majorbeatColor_(0xBA, 0x28, 0xF9);

int AppWindow::charWidth_ = 8;
int AppWindow::charHeight_ = 8;

// #define _FORCE_SDL_EVENT_

/* Autosave.

   AUTOSAVE_CHECK_MS is how often the project is hashed -- cheap, a few
   tens of KB. AUTOSAVE_FORCE_MS is the backstop: writing to the Memory
   Stick takes long enough to starve the audio buffer, so the write
   normally waits for a gap in playback, but a session that never stops
   playing has to be protected eventually. */
#define AUTOSAVE_PATH     "project:lgptsav.autosav"
#define AUTOSAVE_CHECK_MS 4000
#define AUTOSAVE_FORCE_MS 120000
/* How long the project has to stop changing before the write happens.
   Without this, the first edit starts a four second fuse and the write
   goes off while the editing is still going on: the Memory Stick light
   comes on and the machine stops taking input for a second or two,
   over and over, exactly while somebody is trying to work. Waiting for
   a gap costs nothing -- the recovery file is for crashes, and a crash
   during editing loses the same few seconds either way. */
#define AUTOSAVE_QUIET_MS 6000
/* And how long since a button was touched.
   Waiting for the DATA to settle is not enough on its own: somebody
   pauses to think, the data has been still for six seconds, the write
   goes off, and the next button press is the one that gets eaten. The
   write costs a second or two of dead input, so it has to wait until
   nobody is using the machine at all. Ten seconds of no buttons is not
   a pause for thought, it is somebody who has put it down. */
#define AUTOSAVE_IDLE_MS 10000
/* And how long the OUTPUT has to have been silent. Stopping the song
   is not silence: release tails and the reverb -- freeze especially --
   ring on after STOP, and the write starves the audio thread just as
   audibly then as during playback. The force backstop still wins
   eventually, so a frozen tail cannot hold the recovery file hostage
   forever. */
#define AUTOSAVE_TAIL_MS 3000
/* The gap the forced write waits for, and the point at which it stops
   waiting for anything. */
#define AUTOSAVE_GAP_MS  500
#define AUTOSAVE_HARD_MS 300000

static void RecoverCallback(View &v, ModalView &dialog) {
    instance->RecoverAutosave(dialog.GetReturnCode() == MBL_YES);
};

static void ProjectSelectCallback(View &v, ModalView &dialog) {

    SelectProjectDialog &spd = (SelectProjectDialog &)dialog;
    if (dialog.GetReturnCode() > 0) {
        Path selected = spd.GetSelection();
        instance->SaveLastProject(selected);
        instance->LoadProject(selected.GetPath().c_str());
    } else {
        System::GetInstance()->PostQuitMessage();
    }
};

/* Named palettes.
 *
 * The colour system already read every colour from config.xml, which
 * meant the app had exactly one look and changing it meant editing
 * thirteen hex strings by hand. These are the same thirteen, grouped
 * and named, so THEME picks a whole set at once.
 *
 * Order matters and is checked against themeKeys_ below: a mismatch
 * would silently paint the cursor colour on the row numbers.
 *
 * Each one has to survive a PSP-1000 screen, which is dimmer and
 * flatter than an emulator on a desk. That rules out the obvious
 * moves -- dark grey on black reads as one colour in daylight -- so
 * every palette keeps a real gap between the ground, the structure
 * lines and the type, and keeps the cursor clear of both highlights.
 */
static const char *themeKeys_[] = {
    "BACKGROUND", "FOREGROUND", "BORDER", "SONGVIEW_FE", "SONGVIEW_00",
    "HICOLOR1", "HICOLOR2", "CURSORCOLOR", "PLAYCOLOR", "MUTECOLOR",
    "ROWCOLOR1", "ROWCOLOR2", "MAJORBEAT",
};
#define THEME_COLOR_COUNT 13

struct AppTheme {
    const char *name_;
    const char *v_[THEME_COLOR_COUNT];
};

static const AppTheme themes_[] = {
    /* SUNSET -- the hobbychop signature, and what shipped before this
       existed. Violet ground, cyan for anything live, gold for values,
       hot magenta cursor. */
    {"SUNSET",
     {"0A0218", "FFFFFF", "5A1878", "8C82AA", "5A5276",
      "00FFFF", "FFD25A", "FF20C8", "00FFFF", "FF7828",
      "3C305A", "8C82AA", "6E5A96"}},

    /* ICEBOX -- deep teal and glacial highs, with one warm note. The
       values are sand rather than another blue, because a screen of
       nothing but cool colours is where numbers stop being readable. */
    {"ICEBOX",
     {"04121A", "D8ECF5", "0E4257", "6F97AB", "45677A",
      "00D9FF", "FFE9A8", "FF5470", "00D9FF", "FF8A3D",
      "2A4757", "6F97AB", "3E6579"}},

    /* EMBER -- warm dark, amber CRT. Green for mute, which is the only
       cool colour in it and therefore the one thing that cannot be
       mistaken for anything else. */
    {"EMBER",
     {"140805", "FFE9C2", "5C1F0E", "B08258", "7D5A3C",
      "FFB03A", "FFD08A", "FF4D1F", "FFB03A", "8CE06B",
      "4A2A18", "B08258", "6B3C22"}},

    /* GRAPHITE -- near monochrome, one red cursor. For anybody who
       finds the others loud, and the easiest of the four to read in
       bright light. */
    {"GRAPHITE",
     {"0E0E10", "E6E6EA", "33333A", "8A8A94", "5C5C66",
      "FFFFFF", "FFD60A", "FF3B30", "FFFFFF", "FF9500",
      "3A3A42", "8A8A94", "4E4E58"}},
};
#define THEME_COUNT ((int)(sizeof(themes_) / sizeof(themes_[0])))

/* What the selected theme says this colour should be, or 0 if no
   theme is named or it does not know the key. An unknown THEME is
   ignored rather than fatal: a typo in a config file should not stop
   the program starting. */
static const char *themeValue(const char *colorName) {
    Config *config = Config::GetInstance();
    const char *want = config->GetValue("THEME");
    if (!want)
        return 0;
    for (int t = 0; t < THEME_COUNT; t++) {
        if (strcasecmp(want, themes_[t].name_))
            continue;
        for (int k = 0; k < THEME_COLOR_COUNT; k++)
            if (!strcasecmp(colorName, themeKeys_[k]))
                return themes_[t].v_[k];
        return 0;
    }
    return 0;
}

void AppWindow::defineColor(const char *colorName, GUIColor &color) {

    Config *config = Config::GetInstance();
    /* An explicit colour in config.xml still wins, so somebody who has
       tuned one value keeps it while taking the rest of a theme. That
       only works because the shipped config names a THEME instead of
       spelling all thirteen out -- as it used to, which would have
       masked every theme completely. */
    const char *value = config->GetValue(colorName);
    if (!value)
        value = themeValue(colorName);
    if (value) {
        unsigned char r;
        char2hex(value, &r);
        unsigned char g;
        char2hex(value + 2, &g);
        unsigned char b;
        char2hex(value + 4, &b);
        color = GUIColor(r, g, b);
    }
}

// The palette, in one place so boot and a live theme change agree.
void AppWindow::loadPalette() {
    defineColor("BACKGROUND", backgroundColor_);
    defineColor("FOREGROUND", normalColor_);
    defineColor("BORDER", borderColor_);
    defineColor("SONGVIEW_FE", songviewfeColor_);
    defineColor("SONGVIEW_00", songview00Color_);
    defineColor("HICOLOR1", highlightColor_);
    defineColor("HICOLOR2", highlight2Color_);
    defineColor("CURSORCOLOR", cursorColor_);
    defineColor("PLAYCOLOR", playColor_);
    defineColor("MUTECOLOR", muteColor_);
    defineColor("ROWCOLOR1", rownumberColor_);
    defineColor("ROWCOLOR2", rownumber2Color_);
    defineColor("MAJORBEAT", majorbeatColor_);
}

void AppWindow::ApplyTheme() {
    // defineColor() reads config live, so once the THEME variable has been
    // changed in memory this repaints the whole screen in the new palette.
    // InvalidateScreen busts the glyph cache so cells whose colour changed
    // but whose character did not still repaint.
    loadPalette();
    InvalidateScreen();
    Redraw();
}

/* The screen used to repaint once per rendered audio block, and the
   block length is derived from the tempo -- 641 samples at 172bpm,
   1225 at 90, 1837 at 60. So the scope and the meters ran at 68Hz on
   a fast track and 12Hz on a slow one, for no reason anyone could
   see. This thread gives the UI its own clock instead. It only asks
   for a repaint; the repaint itself still happens on the main thread,
   and Invalidate coalesces, so a request that arrives while one is
   already queued costs nothing. */
class UiTicker : public SysThread {
  public:
    UiTicker(AppWindow *w) : w_(w) { sem_ = SysSemaphore::Create(0, 1); }
    ~UiTicker() { if (sem_) delete sem_; }
    virtual bool Execute() {
        while (!shouldTerminate()) {
            // the semaphore is only ever used as a cancellable sleep
            sem_->WaitTimeout(AppWindow::uiFrameMs_);
            if (shouldTerminate()) break;
            w_->uiTick();
        }
        return true;
    }
    virtual void RequestTermination() {
        SysThread::RequestTermination();
        if (sem_) sem_->Post();
    }
  private:
    AppWindow *w_;
    SysSemaphore *sem_;
};

static UiTicker *uiTicker_ = 0;

int AppWindow::uiFps_ = 0;
int AppWindow::animFps_ = 0;
int AppWindow::uiFrameMs_ = 16;   // 62.5Hz; the PSP LCD does ~60

/* Runs on the ticker thread at ~62Hz: ask for the repaint, and poll
   the analog stick for FLICKS. A push past the threshold is one step,
   re-armed only once the stick returns near centre; the step reaches
   the current view as ET_PADANALOGFLICK through the same thread-safe
   user-event queue every other input uses, so views handle it on the
   main thread like any keypress. Direct polling rather than SDL axis
   events: the PSP port's delivery of those proved unreliable. */
void AppWindow::uiTick() {

    /* THE STUCK-MASK CURE, done narrowly this time.

       Every gesture reads _mask, the buttons the EVENT STREAM says
       are held. Lose one release -- a full SDL queue is enough --
       and that button is held forever as far as the program knows.
       A phantom SELECT sends every arrow into the bookmark branch, a
       phantom R into the nav map: the d-pad goes dead while the
       screen and the song play on, which is exactly the reported
       shape. Pressing the stuck button again cures it, which is why
       it always "came back later".

       The first attempt cleared the whole mask the moment the pad
       read empty and synthesised a release; the pad reads empty for
       a moment during ordinary chords, so it broke L and was backed
       out. This one clears ONE BIT at a time, only for the buttons
       whose phantoms eat input (d-pad, L, R, START, SELECT), only
       after the hardware has said "up" for 12 consecutive ticks
       (~190ms -- longer than any bounce, shorter than a player's
       patience), and synthesises nothing: the dispatcher's own mask
       is corrected too, so phantom repeats stop with it. */
    {
        static unsigned short upTicks[10];
        unsigned short hwUp = System::GetInstance()->GetPadUpBits();
        unsigned short suspect = _mask & hwUp;
        for (int b = 0; b < 10; b++) {
            unsigned short bit = (unsigned short)(1 << b);
            if (suspect & bit) {
                if (++upTicks[b] >= 12) {
                    Trace::Log("INPUT", "clearing stuck bit %03x", bit);
                    _mask &= ~bit;
                    EventDispatcher::GetInstance()->ClearMaskBits(bit);
                    upTicks[b] = 0;
                }
            } else {
                upTicks[b] = 0;
            }
        }
    }

    /* The thumbstick no longer moves anything. It only ever spoke on
       the instrument screen, and stick drift -- near-universal on
       aging PSPs -- made that screen a lottery for affected units:
       values edited and focus walked by a stick nobody touched. The
       d-pad grammar covers everything the nub did (zones, ladder,
       O+arrows editing), so the analog is simply not read any more.
       GetAnalog stays in the system layer for anything that ever
       wants an explicitly-calibrated use. */

    Invalidate();
}

AppWindow::AppWindow(I_GUIWindowImp &imp) : GUIWindow(imp) {

    instance = this;

    // Init all members

    _statusLine[0] = 0;
    overlayOpCount_ = 0;
    prevOpCount_ = 0;
    memset(cellDirty_, 0, sizeof(cellDirty_));
    navMapVisible_ = false;

    _currentView = 0;
    _viewData = 0;
    _songView = 0;
    _chainView = 0;
    _phraseView = 0;
    _projectView = 0;
    _instrumentView = 0;
    _tableView = 0;
    _nullView = 0;
    _mixerView = 0;
    _grooveView = 0;
    _configView = 0;
    _fxView = 0;
    navSel_ = VT_SONG;
    navigating_ = false;
    _closeProject = 0;
    _loadAfterSaveAsProject = 0;
    _loadAfterResume = 0;
    _undoCount = 0;
    _undoHead = 0;
    _undoScratchSize = 0;
    _undoScratchView = 0;
    _undoScratchContext = 0;
    _savedChecksum = 0;
    _lastAutosaveCheck = 0;
    _lastSeenChecksum = 0;
    _lastChangeAt = 0;
    _lastInputAt = 0;
    _dirtySince = 0;
    _lastA = 0;
    _lastB = 0;
    _mask = 0;
    colorIndex_ = CD_NORMAL;

    EventDispatcher *ed = EventDispatcher::GetInstance();
    ed->SetWindow(this);

    Status::Install(this);

    // Init midi services
    MidiService::GetInstance()->Init();

    loadPalette();

    GUIWindow::Clear(backgroundColor_);

    _nullView = new NullView((*this), 0);
    _currentView = _nullView;
    _nullView->SetDirty(true);

    Config *config = Config::GetInstance(); // Possible to disable autoloading
    const char *autoLoadEnabled = config->GetValue("AUTO_LOAD_LAST");
    bool shouldAutoLoad =
        (!autoLoadEnabled || // Default to yes if not in config
         strcmp(autoLoadEnabled, "YES") == 0);

    SelectProjectDialog *spd = new SelectProjectDialog(*_currentView);
    Path lastProjectPath = GetLastProjectPath();
    // an empty path resolves to the root dir and "exists" — only a
    // real lgpt_ project counts as a last project
    bool haveLast =
        (lastProjectPath.GetPath().find("lgpt_") != std::string::npos) &&
        lastProjectPath.Exists();
    if (shouldAutoLoad && haveLast) {
        Trace::Log("AppWindow", "Auto-loading last project: %s",
                   lastProjectPath.GetPath().c_str());
        _newProjectToLoad = lastProjectPath.GetPath().c_str();
        _loadAfterResume = true;
        delete spd;
    } else { // Show project selection dialog
        _currentView->DoModal(spd, ProjectSelectCallback);
    }

    // UIFRAMERATE is a config escape hatch: if the panel work turns
    // out to cost too much on real silicon, dial it down without a
    // rebuild. Clamped either side of anything sensible.
    {
        const char *fps = config->GetValue("UIFRAMERATE");
        int f = fps ? atoi(fps) : 60;
        if (f < 10) f = 10;
        if (f > 60) f = 60;
        uiFrameMs_ = 1000 / f;
    }
    uiTicker_ = new UiTicker(this);
    if (!uiTicker_->Start()) {
        Trace::Error("[UI] ticker thread would not start");
        delete uiTicker_;
        uiTicker_ = 0;
    }

    memset(_charScreen, ' ', 1200);
    memset(_preScreen, ' ', 1200);
    memset(_charScreenProp, 0, 1200);
    memset(_preScreenProp, 0, 1200);

    Redraw();
};

AppWindow::~AppWindow() { MidiService::GetInstance()->Close(); }

void AppWindow::DrawString(const char *string, GUIPoint &pos,
                           GUITextProperties &props, bool force) {

    // we know we don't have mode than 40 chars

    // clip hard: an off-grid draw used to memcpy with a negative
    // (= huge unsigned) length and smash the stack
    if ((pos._x >= 40) || (pos._y < 0) || (pos._y >= 30)) {
        return;
    }

    char buffer[41];
    int len = strlen(string);
    int offset = (pos._x < 0) ? -pos._x / 8 : 0;
    len -= offset;
    int available = 40 - ((pos._x < 0) ? 0 : pos._x);
    len = MIN(len, available);
    if (len <= 0) {
        return;
    }
    memcpy(buffer, string + offset, len);
    buffer[len] = 0;

    int index = pos._x + 40 * pos._y;
    memcpy(_charScreen + index, buffer, len);
    unsigned char prop = colorIndex_ + (props.invert_ ? PROP_INVERT : 0);
    memset(_charScreenProp + index, prop, len);
};

void AppWindow::Clear(bool all) {
    memset(_charScreen, ' ', 1200);
    memset(_charScreenProp, 0, 1200);
    overlayOpCount_ = 0; // views re-register their ops as they draw
    if (all) {
        memset(_preScreen, ' ', 1200);
        memset(_preScreenProp, 0, 1200);
    };
};


/* The screen map, shown while the nav modifier is held.

   The grid mirrors how the views are actually wired to each other:

        PROJECT           GROOVE
        SONG    CHAIN     PHRASE    INSTR
                MIXER     TABLE     TABLE2

   so up/down/left/right from the highlighted cell lands where the
   picture says it will. */
// The screen map's grid -- shared by the overlay drawing and the nav
// menu's highlight movement, so what you see is what the arrows walk.
struct MapCell {
    int col, row;
    const char *name;
    ViewType type;
};
static const MapCell navCells_[] = {
        {0, 0, "project", VT_PROJECT},
        {1, 0, "config",  VT_CONFIG},
        {2, 0, "groove",  VT_GROOVE},
        {0, 1, "song",    VT_SONG},
        {1, 1, "chain",   VT_CHAIN},
        {2, 1, "phrase",  VT_PHRASE},
        {3, 1, "instr",   VT_INSTRUMENT},
        {0, 2, "fx",      VT_FX},
        {1, 2, "mixer",   VT_MIXER},
        // The same editor in both places, which is why they share a
        // name. The column is what tells them apart: under phrase it
        // is the table the phrase's instrument runs, under instr it
        // is the table that instrument owns. "table2" made it look
        // like a second feature.
        {2, 2, "table",   VT_TABLE},
        {3, 2, "table",   VT_TABLE2},
};
static const int navCellCount_ = sizeof(navCells_) / sizeof(navCells_[0]);

// Move the menu highlight one cell on the grid. Returns false when
// there is no cell in that direction (the highlight stays put).
bool AppWindow::navMove(int dx, int dy) {
    const MapCell *cur = 0;
    for (int i = 0; i < navCellCount_; i++)
        if (navCells_[i].type == navSel_) { cur = &navCells_[i]; break; }
    if (!cur) return false;
    int c = cur->col + dx, r = cur->row + dy;
    for (int i = 0; i < navCellCount_; i++)
        if (navCells_[i].col == c && navCells_[i].row == r) {
            navSel_ = navCells_[i].type;
            return true;
        }
    return false;
}

bool AppWindow::navReachable(ViewType to) {
    if (!_viewData) return true;
    ViewType from = currentViewType();
    if (to == from) return true;

    // the chain screen's cursor row, of a given chain -- the same
    // cell every real drill will read
    Song *song = _viewData->song_;

    if (from == VT_SONG) {
        if (to == VT_PHRASE || to == VT_INSTRUMENT ||
            to == VT_TABLE || to == VT_TABLE2) {
            unsigned char cell = *_viewData->GetCurrentSongPointer();
            // an empty slot's drill CREATES its chain -- born empty,
            // so nothing below it is reachable yet
            if (cell == 0xFF) return false;
            unsigned char ph =
                *(song->chain_->data_ + 16 * cell + _viewData->chainRow_);
            if (ph == 0xFF) return false;
            if (to == VT_TABLE || to == VT_TABLE2)
                return _phraseView &&
                       _phraseView->ResolveNavTable(ph) >= 0;
        }
        return true;
    }
    if (from == VT_CHAIN) {
        if (to == VT_PHRASE || to == VT_INSTRUMENT ||
            to == VT_TABLE || to == VT_TABLE2) {
            unsigned char ph = *_viewData->GetCurrentChainPointer();
            if (ph == 0xFF) return false;
            if (to == VT_TABLE || to == VT_TABLE2)
                return _phraseView && _phraseView->ResolveNavTable(ph) >= 0;
        }
        return true;
    }
    if (from == VT_PHRASE) {
        if (to == VT_TABLE || to == VT_TABLE2)
            return _phraseView &&
                   _phraseView->ResolveNavTable(_viewData->currentPhrase_) >= 0;
        return true;
    }
    if (from == VT_INSTRUMENT) {
        if (to == VT_TABLE || to == VT_TABLE2) {
            I_Instrument *instr =
                _viewData->project_->GetInstrumentBank()->GetInstrument(
                    _viewData->currentInstrument_);
            return instr && instr->GetTable() >= 0;
        }
        return true;
    }

    /* every other screen: the same song-cursor gate the jump itself
       uses, computed without touching anything */
    if (to == VT_CHAIN || to == VT_PHRASE || to == VT_INSTRUMENT ||
        to == VT_TABLE || to == VT_TABLE2) {
        int sx = _viewData->songX_;
        if (from == VT_MIXER)
            sx = _viewData->mixerCol_ > 7 ? 7 : _viewData->mixerCol_;
        unsigned char cell =
            *(song->data_ + sx + 8 * (_viewData->songY_ + _viewData->songOffset_));
        if (cell == 0xFF) return false;
        if (to == VT_CHAIN) return true;
        unsigned char ph =
            *(song->chain_->data_ + 16 * cell + _viewData->chainRow_);
        if (ph == 0xFF) return false;
        if (to == VT_TABLE || to == VT_TABLE2)
            return _phraseView && _phraseView->ResolveNavTable(ph) >= 0;
        return true;
    }
    return true;
}

void AppWindow::drawNavMap() {

    const MapCell *cells = navCells_;
    const int cellCount = navCellCount_;

    // the highlight is the menu selection while navigating, else the
    // screen we are on
    ViewType current = navigating_ ? navSel_ : currentViewType();

    // panel: 4 columns of 7 cells, 3 rows of 2, centred.
    // Coordinates here are CHARACTER CELLS for DrawString and PIXELS
    // for the overlay ops (8px per cell).
    // full width: a floating box always leaves fragments of the view
    // showing along its edges, so the map takes the whole band
    const int PX = 0, PY = 8, PW = 40, PH = 11;
    const int CX = 4;    // first column of content
    const int COLW = 8;  // column pitch

    GUITextProperties props;
    GUIPoint pos;

    // blank what is underneath: this floats over the view, so the
    // cells have to be cleared or the view's text shows through the
    // panel fill (layer 0 draws under text by design)
    SetColor(CD_BACKGROUND);
    char blank[41];
    memset(blank, ' ', PW);
    blank[PW] = 0;
    for (int r = 0; r < PH; r++) {
        pos._x = PX;
        pos._y = PY + r;
        DrawString(blank, pos, props);
    }

    OpRect(0, PX * 8, PY * 8, PW * 8, PH * 8, OC_PANEL2);
    OpFrame(PX * 8, PY * 8, PW * 8, PH * 8, OC_WHITE);

    SetColor(CD_HILITE1);
    pos._x = CX;
    pos._y = PY + 1;
    DrawString("SCREENS", pos, props);

    for (int i = 0; i < cellCount; i++) {
        const MapCell &c = cells[i];
        bool here = (c.type == current);
        /* a destination the drill would refuse -- an empty chain row,
           no table to follow -- draws dimmed, so the map says what is
           reachable before you try. The refusal itself still stands
           if you commit anyway: the grey is the warning, the refusal
           is the law. */
        bool ok = navReachable(c.type);
        props.invert_ = here;
        SetColor(here ? CD_HILITE1 : (ok ? CD_ROW2 : CD_ROW));
        pos._x = CX + c.col * COLW;
        pos._y = PY + 3 + c.row * 2;
        DrawString(c.name, pos, props);
        props.invert_ = false;
    }

    SetColor(CD_ROW2);
    pos._x = CX;
    pos._y = PY + PH - 2;
    // the map is the one place with room to advertise the chord
    DrawString("hold: direction moves, L undoes", pos, props);
    SetColor(CD_NORMAL);
}

void AppWindow::InvalidateScreen() {
    // a value no real cell can hold: every cell then differs from the
    // cache and gets repainted, and the ops repaint with them
    memset(_preScreen, 0xFF, 1200);
    memset(_preScreenProp, 0xFF, 1200);
    prevOpCount_ = 0;   // every op counts as new -> all repaint
    _isDirty = true;
    SetDirty();
};

void AppWindow::ClearRect(GUIRect &r) {

    int x = r.Left();
    int y = r.Top();
    int w = r.Width();
    int h = r.Height();

    // Clip to the grid. DrawString already does this; a rect that runs
    // off any edge writes outside _charScreen the same way, and callers
    // do pass negative origins (ModalView clears one cell outside its
    // own window to draw the border).
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 40) w = 40 - x;
    if (y + h > 30) h = 30 - y;
    if ((w <= 0) || (h <= 0)) return;

    unsigned char *st = _charScreen + x + (40 * y);
    unsigned char *pr = _charScreenProp + x + (40 * y);
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            *st++ = ' ';
            *pr++ = 0;
        }
        st += (40 - w);
        pr += (40 - w);
    }
};

//
// Redraws the screen and flush it.
//

void AppWindow::Redraw() {

    // deferred work that needs the mixer lock (preset loads, type
    // swaps, file writes) runs HERE, before drawMutex_ is taken --
    // see View::ApplyDeferred for the deadlock this prevents
    if (_currentView) _currentView->ApplyDeferred();
    ConfigView::CommitPendingToDisk();

    SysMutexLocker locker(drawMutex_);

    if (_currentView) {
        _currentView->Redraw();
        Invalidate();
    }
};

//
// Flush current screen to display
//

void AppWindow::Flush() {

    // Scoped so the mutex drops BEFORE the present. GUIWindow::Flush
    // waits for vblank -- up to a frame -- and the audio thread takes
    // this same mutex every block for its play-cursor updates. Holding
    // it across the wait was a ~16ms audio stall whenever a repaint
    // and a block render collided, which is the dropout people hit
    // while scrolling during playback. Everything that touches the
    // char grid or the overlay list stays inside; only the present of
    // already-rendered pixels happens outside. Flush only ever runs on
    // the main thread, so the present cannot interleave with itself.
    {
    SysMutexLocker locker(drawMutex_);

    if (navMapVisible_ && _currentView && !_currentView->HasModal()) {
        // the map owns the screen while it is up: drop the view's
        // panels and frames so none of them paint across the band
        overlayOpCount_ = 0;
        drawNavMap();
    }

    Lock();
    memset(cellDirty_, 0, sizeof(cellDirty_));

    GUITextProperties props;
    GUIPoint pos;

    ColorDefinition color = (ColorDefinition)-1;
    pos._x = 0;
    pos._y = 0;

    int count = 0;

    unsigned char *current = _charScreen;
    unsigned char *previous = _preScreen;
    unsigned char *currentProp = _charScreenProp;
    unsigned char *previousProp = _preScreenProp;
    for (int y = 0; y < 30; y++) {
        for (int x = 0; x < 40; x++) {
#ifndef _LGPT_NO_SCREEN_CACHE_
            if ((*current != *previous) || (*currentProp != *previousProp)) {
#endif
                props.invert_ = (*currentProp & PROP_INVERT) != 0;
                if (((*currentProp) & 0x7F) != color) {
                    color = (ColorDefinition)((*currentProp) & 0x7F);
                    GUIColor gcolor = normalColor_;
                    switch (color) {
                    case CD_BACKGROUND:
                        gcolor = backgroundColor_;
                        break;
                    case CD_NORMAL:
                        break;
                    case CD_BORDER:
                        gcolor = borderColor_;
                        break;
                    case CD_HILITE1:
                        gcolor = highlightColor_;
                        break;
                    case CD_HILITE2:
                        gcolor = highlight2Color_;
                        break;
                    case CD_CONSOLE:
                        gcolor = consoleColor_;
                        break;
                    case CD_CURSOR:
                        gcolor = cursorColor_;
                        break;
                    case CD_PLAY:
                        gcolor = playColor_;
                        break;
                    case CD_MUTE:
                        gcolor = muteColor_;
                        break;
                    case CD_SONGVIEWFE:
                        gcolor = songviewfeColor_;
                        break;
                    case CD_SONGVIEW00:
                        gcolor = songview00Color_;
                        break;
                    case CD_ROW:
                        gcolor = rownumberColor_;
                        break;
                    case CD_ROW2:
                        gcolor = rownumber2Color_;
                        break;
                    case CD_MAJORBEAT:
                        gcolor = majorbeatColor_;
                        break;
                    default:
                        NAssert(0);
                        break;
                    }
                    GUIWindow::SetColor(gcolor);
                }
                GUIWindow::DrawChar(*current, pos, props);
                cellDirty_[y * 40 + x] = true;
                count++;
#ifndef _LGPT_NO_SCREEN_CACHE_
            }
#endif
            current++;
            previous++;
            currentProp++;
            previousProp++;
            pos._x += AppWindow::charWidth_;
        }
        pos._y += AppWindow::charHeight_;
        pos._x = 0;
    }
    // a modal dialog owns the screen: drop the ops (the erase pass
    // wipes their pixels); the post-modal redraw re-registers them
    if (_currentView && _currentView->HasModal()) {
        overlayOpCount_ = 0;
    }
    flushOverlayOps();
    // the cache update pairs with the diff above, so it must stay
    // under the mutex -- a play-cursor write landing between the diff
    // and this copy would be marked painted without being painted
    memcpy(_preScreen, _charScreen, 1200);
    memcpy(_preScreenProp, _charScreenProp, 1200);
    }   // drawMutex_ released here

    GUIWindow::Flush();   // the vblank wait, now outside the mutex
    Unlock();
};

static GUIColor lerpColor(GUIColor &a, GUIColor &b, int num, int den) {
    if (den < 1) den = 1;
    return GUIColor(a._r + (b._r - a._r) * num / den,
                    a._g + (b._g - a._g) * num / den,
                    a._b + (b._b - a._b) * num / den);
}

GUIColor AppWindow::colorForProp(unsigned char prop) {
    switch (prop & 0x7F) {
    case CD_BACKGROUND: return backgroundColor_;
    case CD_BORDER:     return borderColor_;
    case CD_HILITE1:    return highlightColor_;
    case CD_HILITE2:    return highlight2Color_;
    case CD_CONSOLE:    return consoleColor_;
    case CD_CURSOR:     return cursorColor_;
    case CD_PLAY:       return playColor_;
    case CD_MUTE:       return muteColor_;
    case CD_SONGVIEWFE: return songviewfeColor_;
    case CD_SONGVIEW00: return songview00Color_;
    case CD_ROW:        return rownumberColor_;
    case CD_ROW2:       return rownumber2Color_;
    case CD_MAJORBEAT:  return majorbeatColor_;
    default:            return normalColor_;
    }
}

GUIColor AppWindow::opColor(int id) {
    switch (id) {
    case OC_PANEL:  return lerpColor(backgroundColor_, borderColor_, 12, 100);
    case OC_PANEL2: return lerpColor(backgroundColor_, borderColor_, 28, 100);
    case OC_STRIP:  return lerpColor(backgroundColor_, borderColor_, 22, 100);
    case OC_GRID:   return borderColor_;
    case OC_WHITE:  return normalColor_;
    default:        return colorForProp((unsigned char)id);
    }
}

void AppWindow::setOp(const OverlayOp &op) {
    for (int i = 0; i < overlayOpCount_; i++) {
        OverlayOp &o = overlayOps_[i];
        if (o.type_ == op.type_ && o.x_ == op.x_ && o.y_ == op.y_) {
            o = op;
            return;
        }
    }
    if (overlayOpCount_ >= MAX_OVERLAY_OPS)
        return;
    overlayOps_[overlayOpCount_++] = op;
}

void AppWindow::OpRect(int layer, int x, int y, int w, int h, int col) {
    OverlayOp op = {OOP_RECT, (unsigned char)layer, (unsigned char)col, 0,
                    (short)x, (short)y, (short)w, (short)h, 0, 0, 0, 0};
    setOp(op);
}

void AppWindow::OpRing(int x, int y, int w, int h, int col) {
    OverlayOp op = {OOP_FRAME, 1, (unsigned char)col, 0,
                    (short)x, (short)y, (short)w, (short)h,
                    0, 0, 0, 0};
    setOp(op);
}

void AppWindow::OpFrame(int x, int y, int w, int h, int col, int gapX,
                        int gapW) {
    OverlayOp op = {OOP_FRAME, 0, (unsigned char)col, 0,
                    (short)x, (short)y, (short)w, (short)h,
                    (short)gapX, (short)gapW, 0, 0};
    setOp(op);
}

void AppWindow::OpBar(int x, int y, int w, int fillPx, bool focused) {
    // Six pixels tall, not eight. The bar is drawn at y*8+1, so an
    // eight pixel bar finished one pixel inside the NEXT row and cut
    // through whatever was there. Nothing noticed while every slider
    // had a blank row under it; the first slider placed on a panel's
    // last row put a bite out of the panel's bottom rule.
    OverlayOp op = {OOP_BAR, 1, 0, (unsigned char)(focused ? 1 : 0),
                    (short)x, (short)y, (short)w, 6,
                    (short)fillPx, 0, 0, 0};
    setOp(op);
}

void AppWindow::OpGlow(int x, int y, int w, int h, int intensity) {
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    OverlayOp op = {OOP_GLOW, 1, 0, 0, (short)x, (short)y, (short)w,
                    (short)h, (short)intensity, 0, 0, 0};
    setOp(op);
}

void AppWindow::OpAdsr(int x, int y, int w, int h, int a, int d, int s) {
    OverlayOp op = {OOP_ADSR, 1, 0, 0, (short)x, (short)y, (short)w,
                    (short)h, (short)a, (short)d, (short)s, 0};
    setOp(op);
}

void AppWindow::OpVBar(int x, int y, int w, int h, int fillPx, int cap) {
    OverlayOp op = {OOP_VBAR, 1, 0, 0, (short)x, (short)y, (short)w,
                    (short)h, (short)fillPx, (short)cap, 0, 0};
    setOp(op);
}

void AppWindow::OpWave(int x, int y, int w, int h, int kind) {
    OverlayOp op = {OOP_WAVE, 1, 0, 0, (short)x, (short)y, (short)w,
                    (short)h, (short)kind, 0, 0, 0};
    setOp(op);
}

void AppWindow::OpSpectrum(int x, int y, int w, int h, int tick) {
    // tick keeps the op "changed" every animation frame, same trick as
    // the scope, or the repaint-on-diff logic freezes the bars
    OverlayOp op = {OOP_SPECT, 1, 0, 0,
                    (short)x, (short)y, (short)w,
                    (short)h, (short)tick, 0, 0, 0};
    setOp(op);
}

void AppWindow::OpScope(int x, int y, int w, int h, int tick, int channel) {
    // colA_ carries the channel: 0 left, 1 right
    OverlayOp op = {OOP_SCOPE, 1, (unsigned char)(channel & 1), 0,
                    (short)x, (short)y, (short)w,
                    (short)h, (short)tick, 0, 0, 0};
    setOp(op);
}

void AppWindow::opLine(int x0, int y0, int x1, int y1) {
    int n = abs(x1 - x0);
    if (abs(y1 - y0) > n) n = abs(y1 - y0);
    if (n < 1) n = 1;
    for (int t = 0; t <= n; t++) {
        int px = x0 + (x1 - x0) * t / n;
        int py = y0 + (y1 - y0) * t / n;
        GUIRect r(px, py, px + 1, py + 1);
        GUIWindow::DrawRect(r);
    }
}

// Draw a rectangle, but only the part of it inside a box. Used to
// repaint just the disturbed slice of a panel instead of the whole
// panel.
void AppWindow::drawClipped(int x0, int y0, int x1, int y1,
                            int bx0, int by0, int bx1, int by1) {
    if (x0 < bx0) x0 = bx0;
    if (y0 < by0) y0 = by0;
    if (x1 > bx1) x1 = bx1;
    if (y1 > by1) y1 = by1;
    if (x1 <= x0 || y1 <= y0) return;
    GUIRect r(x0, y0, x1, y1);
    GUIWindow::DrawRect(r);
}

void AppWindow::flushOverlayOps() {

    // ---- geometry-twin analysis. A previous op whose geometry
    // (type/layer/x/y/w/h) survives in the current list keeps its
    // pixels valid — its successor repaints them whenever its content
    // changes. A previous op with no geometric successor left pixels
    // nothing will repaint: those cells are repainted as plain chars.
    // This keeps a value-edit redraw (near-identical op list) pixel-
    // quiet — blanket erase+repaint per edit reads as a screen flash
    // on the PSP's live framebuffer.
    static bool eraseMask[1200];
    memset(eraseMask, 0, sizeof(eraseMask));
    bool anyErase = false;
    for (int j = 0; j < prevOpCount_; j++) {
        OverlayOp &p = prevOps_[j];
        bool twin = false;
        for (int i = 0; i < overlayOpCount_; i++) {
            OverlayOp &o = overlayOps_[i];
            if (o.type_ == p.type_ && o.layer_ == p.layer_ &&
                o.x_ == p.x_ && o.y_ == p.y_ && o.w_ == p.w_ &&
                o.h_ == p.h_) {
                twin = true;
                break;
            }
        }
        if (twin)
            continue;
        // A bar used to claim nine pixel rows regardless of the height
        // it was given, which reaches into the character row below it.
        // Nothing noticed while every slider had a spare row beneath;
        // put one on the last row of a panel and it took a bite out of
        // the panel's bottom rule. Bars now honour their height like
        // every other op, and OpBar sizes itself to stay inside one row.
        int h = p.h_;
        int cx0 = (p.x_ - 1) / 8, cy0 = (p.y_ - 1) / 8;
        int cx1 = (p.x_ + p.w_) / 8, cy1 = (p.y_ + h) / 8;
        for (int cy = cy0; cy <= cy1 && cy < 30; cy++)
            for (int cx = cx0; cx <= cx1 && cx < 40; cx++)
                if (cx >= 0 && cy >= 0) {
                    eraseMask[cy * 40 + cx] = true;
                    anyErase = true;
                }
    }
    if (anyErase) {
        GUITextProperties props;
        GUIPoint pos;
        for (int idx = 0; idx < 1200; idx++) {
            if (!eraseMask[idx])
                continue;
            unsigned char ch = _charScreen[idx];
            unsigned char prop = _charScreenProp[idx];
            GUIColor c = colorForProp(prop);
            GUIWindow::SetColor(c);
            props.invert_ = (prop & PROP_INVERT) != 0;
            props.transparent_ = false;
            pos._x = (idx % 40) * AppWindow::charWidth_;
            pos._y = (idx / 40) * AppWindow::charHeight_;
            GUIWindow::DrawChar(ch, pos, props);
        }
    }

    // ---- which ops actually need painting this flush? Changed or
    // new ops, ops over text the char diff redrew, ops over cells the
    // erase pass just wiped. Everything else keeps its pixels.
    static bool need[MAX_OVERLAY_OPS];
    // Did the op itself change, or is it only being repainted because
    // a character underneath it changed? The two want very different
    // treatment and conflating them is what made the song title
    // flicker: the dsp figure in the title strip changes on nearly
    // every animation frame, which marked the whole strip for
    // repaint, which put every character in that row -- the song name
    // included -- through a transparent redraw thirty-nine times a
    // second to repair damage confined to five cells.
    static bool selfChanged[MAX_OVERLAY_OPS];
    for (int i = 0; i < overlayOpCount_; i++) {
        OverlayOp &o = overlayOps_[i];
        bool n = true;
        for (int j = 0; j < prevOpCount_; j++) {
            if (!memcmp(&prevOps_[j], &o, sizeof(OverlayOp))) {
                n = false;
                break;
            }
        }
        selfChanged[i] = n;
        if (!n) {
            int h = (o.type_ == OOP_BAR) ? 9 : o.h_;
            int cx0 = (o.x_ - 1) / 8, cy0 = (o.y_ - 1) / 8;
            int cx1 = (o.x_ + o.w_) / 8, cy1 = (o.y_ + h) / 8;
            for (int cy = cy0; cy <= cy1 && cy < 30 && !n; cy++)
                for (int cx = cx0; cx <= cx1 && cx < 40; cx++)
                    if (cx >= 0 && cy >= 0 &&
                        (cellDirty_[cy * 40 + cx] ||
                         eraseMask[cy * 40 + cx])) {
                        n = true;
                        break;
                    }
        }
        need[i] = n;
    }
    memcpy(prevOps_, overlayOps_, overlayOpCount_ * sizeof(OverlayOp));
    prevOpCount_ = overlayOpCount_;

    if (overlayOpCount_ == 0)
        return;

    // ---- layer 0: panels / frames / strips, under the text --------
    static bool mask[1200];
    memset(mask, 0, sizeof(mask));

    for (int i = 0; i < overlayOpCount_; i++) {
        OverlayOp &o = overlayOps_[i];
        if (!need[i])
            continue;
        if (o.type_ != OOP_RECT && o.type_ != OOP_FRAME) continue;
        if (o.layer_ != 0) continue;

        int cx0 = o.x_ / 8, cy0 = o.y_ / 8;
        int cx1 = (o.x_ + o.w_ - 1) / 8, cy1 = (o.y_ + o.h_ - 1) / 8;
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (cx1 > 39) cx1 = 39;
        if (cy1 > 29) cy1 = 29;

        // When the op is unchanged and only some characters under it
        // moved, the damage is confined to those characters' cells.
        // Repaint that box and nothing else.
        int bx0 = o.x_, by0 = o.y_;
        int bx1 = o.x_ + o.w_, by1 = o.y_ + o.h_;
        if (!selfChanged[i]) {
            int dx0 = 40, dy0 = 30, dx1 = -1, dy1 = -1;
            for (int cy = cy0; cy <= cy1; cy++)
                for (int cx = cx0; cx <= cx1; cx++)
                    if (cellDirty_[cy * 40 + cx] || eraseMask[cy * 40 + cx]) {
                        if (cx < dx0) dx0 = cx;
                        if (cx > dx1) dx1 = cx;
                        if (cy < dy0) dy0 = cy;
                        if (cy > dy1) dy1 = cy;
                    }
            if (dx1 < 0) continue;          // nothing was disturbed
            cx0 = dx0; cx1 = dx1; cy0 = dy0; cy1 = dy1;
            if (dx0 * 8 > bx0) bx0 = dx0 * 8;
            if (dy0 * 8 > by0) by0 = dy0 * 8;
            if ((dx1 + 1) * 8 < bx1) bx1 = (dx1 + 1) * 8;
            if ((dy1 + 1) * 8 < by1) by1 = (dy1 + 1) * 8;
        }

        GUIColor c = opColor(o.colA_);
        GUIWindow::SetColor(c);
        if (o.type_ == OOP_RECT) {
            drawClipped(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_,
                        bx0, by0, bx1, by1);
        } else {
            if (o.p2_) {
                drawClipped(o.x_, o.y_, o.p1_, o.y_ + 1,
                            bx0, by0, bx1, by1);
                drawClipped(o.p1_ + o.p2_, o.y_, o.x_ + o.w_, o.y_ + 1,
                            bx0, by0, bx1, by1);
            } else {
                drawClipped(o.x_, o.y_, o.x_ + o.w_, o.y_ + 1,
                            bx0, by0, bx1, by1);
            }
            drawClipped(o.x_, o.y_ + o.h_ - 1, o.x_ + o.w_, o.y_ + o.h_,
                        bx0, by0, bx1, by1);
            drawClipped(o.x_, o.y_, o.x_ + 1, o.y_ + o.h_,
                        bx0, by0, bx1, by1);
            drawClipped(o.x_ + o.w_ - 1, o.y_, o.x_ + o.w_, o.y_ + o.h_,
                        bx0, by0, bx1, by1);
        }
        // Which cells have to have their text put back on top.
        //
        // A FRAME paints four thin lines and nothing in between, so
        // only the cells its border crosses are disturbed -- but this
        // used to mark the whole bounding box. The song screen's
        // frame is 27 cells by 17: four hundred and fifty nine cells
        // of untouched interior, each getting a transparent DrawChar
        // (a per-pixel loop over a 12x9 cell) on every animation
        // frame. That is close to two million pixel operations a
        // second, on a 333MHz machine, to repair damage nothing did.
        // It is why the scope and the meters crawled on hardware
        // while measuring 39fps in an emulator.
        if (o.type_ == OOP_FRAME) {
            for (int cx = cx0; cx <= cx1; cx++) {
                mask[cy0 * 40 + cx] = true;
                mask[cy1 * 40 + cx] = true;
            }
            for (int cy = cy0; cy <= cy1; cy++) {
                mask[cy * 40 + cx0] = true;
                mask[cy * 40 + cx1] = true;
            }
        } else {
            for (int cy = cy0; cy <= cy1; cy++)
                for (int cx = cx0; cx <= cx1; cx++)
                    mask[cy * 40 + cx] = true;
        }
    }

    // ---- text over layer 0: redraw intersecting ink transparently -
    {
        GUITextProperties props;
        GUIPoint pos;
        for (int idx = 0; idx < 1200; idx++) {
            if (!mask[idx])
                continue;
            unsigned char ch = _charScreen[idx];
            if (ch == ' ')
                continue;
            unsigned char prop = _charScreenProp[idx];
            GUIColor c = colorForProp(prop);
            GUIWindow::SetColor(c);
            props.invert_ = (prop & PROP_INVERT) != 0;
            props.transparent_ = !props.invert_;
            pos._x = (idx % 40) * AppWindow::charWidth_;
            pos._y = (idx / 40) * AppWindow::charHeight_;
            GUIWindow::DrawChar(ch, pos, props);
        }
    }

    // ---- layer 1: bars / graphs / ticks, over everything ----------
    for (int i = 0; i < overlayOpCount_; i++) {
        OverlayOp &o = overlayOps_[i];
        if (!need[i])
            continue;
        switch (o.type_) {
        case OOP_RECT: {
            // The layer 0 pass above takes rects at layer 0 and skips
            // everything else; this switch had no case for a rect at
            // all. So OpRect(1, ...) was accepted, stored, counted
            // against the op budget, and then drawn by nobody -- it
            // just did not appear, with nothing to say why. The one
            // caller in the tree passes 0, which is why it went
            // unnoticed.
            if (o.layer_ != 1)
                break;
            GUIColor rc = opColor(o.colA_);
            GUIWindow::SetColor(rc);
            GUIRect r(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(r);
            break;
        }
        case OOP_GLOW: {
            GUIColor g = lerpColor(backgroundColor_, playColor_, o.p1_, 255);
            GUIWindow::SetColor(g);
            GUIRect r(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(r);
            break;
        }
        case OOP_FRAME: {
            if (o.layer_ != 1)
                break;
            GUIColor c = opColor(o.colA_);
            GUIWindow::SetColor(c);
            GUIRect t(o.x_, o.y_, o.x_ + o.w_, o.y_ + 1);
            GUIWindow::DrawRect(t);
            GUIRect b(o.x_, o.y_ + o.h_ - 1, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(b);
            GUIRect l(o.x_, o.y_, o.x_ + 1, o.y_ + o.h_);
            GUIWindow::DrawRect(l);
            GUIRect r(o.x_ + o.w_ - 1, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(r);
            break;
        }
        case OOP_BAR: {
            int w = o.w_, fill = o.p1_;
            if (fill > w) fill = w;
            // the focus tick paints 1px outside the track; clear that
            // ring so a moved or unfocused tick leaves nothing behind
            GUIWindow::SetColor(backgroundColor_);
            GUIRect ring1(o.x_ - 1, o.y_ - 1, o.x_ + w + 2, o.y_);
            GUIWindow::DrawRect(ring1);
            GUIRect ring2(o.x_ - 1, o.y_ - 1, o.x_, o.y_ + 7);
            GUIWindow::DrawRect(ring2);
            GUIRect ring3(o.x_ + w, o.y_ - 1, o.x_ + w + 2, o.y_ + 7);
            GUIWindow::DrawRect(ring3);
            GUIColor track = opColor(OC_PANEL2);
            GUIWindow::SetColor(track);
            GUIRect tr(o.x_, o.y_, o.x_ + w, o.y_ + 6);
            GUIWindow::DrawRect(tr);
            GUIColor un = opColor(CD_ROW);
            GUIWindow::SetColor(un);
            GUIRect ur(o.x_, o.y_ + 6, o.x_ + w, o.y_ + 7);
            GUIWindow::DrawRect(ur);
            int done = 0;
            for (int st = 0; st < 8 && done < fill; st++) {
                int e = fill * (st + 1) / 8;
                if (e <= done) continue;
                GUIColor c = lerpColor(cursorColor_, highlightColor_, done, w);
                GUIWindow::SetColor(c);
                GUIRect seg(o.x_ + done, o.y_, o.x_ + e, o.y_ + 6);
                GUIWindow::DrawRect(seg);
                done = e;
            }
            if (o.focused_) {
                // y+7, not y+8. The bar sits at row*8+1, so a tick
                // ending at y+8 paints rows row*8 through row*8+8 --
                // nine rows, the last of which is the FIRST row of the
                // next character row. Nothing ever repaints that one:
                // the track stops at y+6, the underline at y+7, the
                // rings above only cover the left and right margins,
                // and the erase pass works from the op's height of
                // six, which does not reach it either. So every
                // position the tick passed through kept one white
                // pixel row, and dragging a slider drew a line under
                // it.
                //
                // Ending at y+7 keeps the whole tick inside its own
                // character row, where the track and the rings repaint
                // every pixel of it on the next flush.
                GUIWindow::SetColor(normalColor_);
                GUIRect tick(o.x_ + fill - 1, o.y_ - 1, o.x_ + fill + 1,
                             o.y_ + 7);
                GUIWindow::DrawRect(tick);
            }
            break;
        }
        case OOP_ADSR: {
            GUIColor pc = opColor(OC_PANEL2);
            GUIWindow::SetColor(pc);
            GUIRect bg(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(bg);
            GUIColor lc = colorForProp(CD_HILITE1);
            GUIWindow::SetColor(lc);
            int ax = o.x_ + 2 + (o.w_ - 4) * o.p1_ / 1020;
            int dx = ax + (o.w_ - 4) * o.p2_ / 765;
            if (dx > o.x_ + o.w_ - 2) dx = o.x_ + o.w_ - 2;
            int sy = o.y_ + o.h_ - 2 - (o.h_ - 4) * o.p3_ / 255;
            opLine(o.x_ + 2, o.y_ + o.h_ - 2, ax, o.y_ + 2);
            opLine(ax, o.y_ + 2, dx, sy);
            opLine(dx, sy, o.x_ + o.w_ - 2, sy);
            break;
        }
        case OOP_VBAR: {
#if defined(PLATFORM_PSP) && defined(PSP_GU_DISPLAY)
            {
                #define GU_ABGR(c) (0xFF000000u|(((c)._b&0xFF)<<16)|(((c)._g&0xFF)<<8)|((c)._r&0xFF))
                GUIColor trk = opColor(OC_PANEL2);
                GetImpWindow()->GuQueueRect(o.x_, o.y_, o.w_, o.h_, GU_ABGR(trk));
                int fill = o.p1_; if (fill > o.h_) fill = o.h_;
                if (o.p2_) {
                    GUIColor cc = colorForProp(o.p2_ >= 2 ? CD_MAJORBEAT : CD_HILITE2);
                    GetImpWindow()->GuQueueRect(o.x_, o.y_ - 4, o.w_, 3, GU_ABGR(cc));
                }
                if (fill > 0) {
                    int warn = o.h_ * 30 / 100, clip = o.h_ * 10 / 100;
                    int zTop = o.y_ + o.h_ - fill;
                    int zWarn = o.y_ + warn, zClip = o.y_ + clip;
                    GUIColor c1 = colorForProp(CD_HILITE1);
                    GetImpWindow()->GuQueueRect(o.x_, zTop, o.w_, (o.y_ + o.h_) - zTop, GU_ABGR(c1));
                    if (zTop < zWarn) { GUIColor c2 = colorForProp(CD_HILITE2);
                        GetImpWindow()->GuQueueRect(o.x_, zTop, o.w_, zWarn - zTop, GU_ABGR(c2)); }
                    if (zTop < zClip) { GUIColor c3 = colorForProp(CD_MUTE);
                        GetImpWindow()->GuQueueRect(o.x_, zTop, o.w_, zClip - zTop, GU_ABGR(c3)); }
                }
                #undef GU_ABGR
            }
            break;
#endif
            GUIColor track = opColor(OC_PANEL2);
            GUIWindow::SetColor(track);
            GUIRect tr(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(tr);
            int fill = o.p1_;
            if (fill > o.h_) fill = o.h_;
            if (o.p2_) {
                // Two lights, both latched by the audio side and held
                // long enough to read. RED is this channel clipping.
                // GOLD is this channel being a large share of the mix
                // -- not a fault in itself, but the thing to reach for
                // when the master is working hard, and the one state
                // a per-channel meter could never show before.
                GUIColor cc = colorForProp(o.p2_ >= 2 ? CD_MAJORBEAT
                                                      : CD_HILITE2);
                GUIWindow::SetColor(cc);
                GUIRect cap(o.x_, o.y_ - 4, o.x_ + o.w_, o.y_ - 1);
                GUIWindow::DrawRect(cap);
            }
            if (fill > 0) {
                // bottom part cyan, top of tall bars runs hot
                int warn = o.h_ * 30 / 100;   // top 30% = gold
                int clip = o.h_ * 10 / 100;   // top 10% = orange
                int base = fill - warn > 0 ? fill - warn : 0;
                GUIColor c1 = colorForProp(CD_HILITE1);
                GUIWindow::SetColor(c1);
                GUIRect f1(o.x_, o.y_ + o.h_ - (fill - (fill > o.h_ - warn ? fill - (o.h_ - warn) : 0)),
                           o.x_ + o.w_, o.y_ + o.h_);
                // simpler: three stacked zones
                int zTop = o.y_ + o.h_ - fill;
                int zWarn = o.y_ + warn;
                int zClip = o.y_ + clip;
                GUIRect fc(o.x_, zTop, o.x_ + o.w_, o.y_ + o.h_);
                GUIWindow::DrawRect(fc);
                if (zTop < zWarn) {
                    GUIColor c2 = colorForProp(CD_HILITE2);
                    GUIWindow::SetColor(c2);
                    GUIRect fw(o.x_, zTop, o.x_ + o.w_, zWarn);
                    GUIWindow::DrawRect(fw);
                }
                if (zTop < zClip) {
                    GUIColor c3 = colorForProp(CD_MUTE);
                    GUIWindow::SetColor(c3);
                    GUIRect fx(o.x_, zTop, o.x_ + o.w_, zClip);
                    GUIWindow::DrawRect(fx);
                }
            }
            break;
        }
        case OOP_SPECT: {
            // 32 log-spaced bars from the ME's idle-lane FFT of the
            // finished master. Data is display-only and racy by design.
            GUIColor pc2 = opColor(OC_PANEL);
            GUIWindow::SetColor(pc2);
            GUIRect bg2(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(bg2);
            if (o.w_ < 34 || o.h_ < 4) break;
            // stopped = cleared, exactly like the scope above it: the
            // op's tick freezes at stop, so whatever was drawn last
            // would otherwise sit there looking like held audio
            if (!Player::GetInstance()->IsRunning()) break;
            int bars[32];
            int have = 0;
#if defined(PLATFORM_PSP) && defined(PSP_ME_OFFLOAD)
            have = PSPME_ReadSpectrum(bars);
#endif
            if (!have) break;   // core down: an empty panel, not junk
            GUIColor sc2 = colorForProp(CD_HILITE1);
            GUIWindow::SetColor(sc2);
            GetImpWindow()->SetBatchRects(true);
            int bw = o.w_ / 32;
            int x0 = o.x_ + (o.w_ - bw * 32) / 2;
            for (int b = 0; b < 32; b++) {
                int hh = (bars[b] * (o.h_ - 2)) / 255;
                if (hh <= 0) continue;
                if (hh > o.h_ - 2) hh = o.h_ - 2;
                GUIRect bar(x0 + b * bw, o.y_ + o.h_ - 1 - hh,
                            x0 + b * bw + (bw > 1 ? bw - 1 : 1),
                            o.y_ + o.h_ - 1);
                GUIWindow::DrawRect(bar);
            }
            GetImpWindow()->SetBatchRects(false);
            break;
        }
        case OOP_SCOPE: {
#if defined(PLATFORM_PSP) && defined(PSP_GU_DISPLAY)
            {
                // GPU path: queue the scope; the GU draws it after the
                // software present, so the CPU stops plotting the columns.
                GUIColor pcol = opColor(OC_PANEL);
                bool live = Player::GetInstance()->IsRunning();
                short lo[96], hi[96];
                int n = o.w_ > 96 ? 96 : o.w_;
                if (live) AudioStats::ReadScope(lo, hi, n, o.colA_);
                else for (int j = 0; j < n; j++) { lo[j] = 0; hi[j] = 0; }
                GUIColor lcol = live ? colorForProp(CD_HILITE1) : opColor(CD_ROW);
                unsigned int wave = 0xFF000000u|((lcol._b&0xFF)<<16)|((lcol._g&0xFF)<<8)|(lcol._r&0xFF);
                unsigned int bgc  = 0xFF000000u|((pcol._b&0xFF)<<16)|((pcol._g&0xFF)<<8)|(pcol._r&0xFF);
                GetImpWindow()->GuQueueScope(o.x_, o.y_, o.w_, o.h_, lo, hi, n, live, wave, bgc);
            }
            break;
#endif
            GUIColor pc = opColor(OC_PANEL);
            GUIWindow::SetColor(pc);
            GUIRect bg(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(bg);
            if (o.w_ < 4 || o.h_ < 4) break;
            bool live = Player::GetInstance()->IsRunning();
            short lo[96], hi[96];
            int n = o.w_ > 96 ? 96 : o.w_;
            if (live) AudioStats::ReadScope(lo, hi, n, o.colA_);
            int amp = o.h_ / 2 - 1;
            int mid = o.y_ + o.h_ / 2;
            GUIColor lc = live ? colorForProp(CD_HILITE1) : opColor(CD_ROW);
            GUIWindow::SetColor(lc);
            // the background rect above already covers the whole scope, so
            // the per-column rects only flood the dirty-rect list (and tip
            // the final blit into a full-screen copy). Batch them under it.
            GetImpWindow()->SetBatchRects(true);
            for (int j = 0; j < o.w_; j++) {
                int k = j * n / o.w_;
                // 2x display gain: a mix peaking near full scale still
                // spends most of its time well below it, and a trace
                // that never leaves the middle three pixels reads as a
                // dead scope. Loud transients flatten against the top,
                // which is what a scope is supposed to look like.
                int top = live ? -(hi[k] * amp) / 16384 : 0;
                int bot = live ? -(lo[k] * amp) / 16384 : 0;
                if (top < -amp) top = -amp;
                if (bot > amp) bot = amp;
                if (top > amp) top = amp;
                if (bot < -amp) bot = -amp;
                GUIRect sp(o.x_ + j, mid + top, o.x_ + j + 1, mid + bot + 1);
                GUIWindow::DrawRect(sp);
            }
            GetImpWindow()->SetBatchRects(false);
            break;
        }
        case OOP_WAVE: {
            GUIColor pc = opColor(OC_PANEL2);
            GUIWindow::SetColor(pc);
            GUIRect bg(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(bg);
            GUIColor lc = colorForProp(CD_HILITE1);
            GUIWindow::SetColor(lc);
            int mid = o.y_ + o.h_ / 2, amp = o.h_ / 2 - 2;
            int n = o.w_ - 4;
            if (n < 4) break;
            for (int j = 0; j < n; j++) {
                int ph = j * 512 / n;
                int t = ph & 0xFF;
                int v;
                switch (o.p1_) {
                case 1: v = (t < 128) ? amp : -amp; break;
                case 2: v = ((t < 128 ? t : 255 - t) * 2 - 128) * amp / 128;
                        break;
                default: v = (t - 128) * amp / 128; break;
                }
                GUIRect px(o.x_ + 2 + j, mid - v, o.x_ + 3 + j, mid - v + 1);
                GUIWindow::DrawRect(px);
            }
            break;
        }
        default:
            break;
        }
    }
}

/* The screen while a project opens.

   Opening is not instant, and most of it is not disk: the drum kit is
   twenty-four instruments SYNTHESISED at boot, deliberately, so that a
   project can rely on a kit being there on a machine with an empty
   memory stick. That costs a few seconds on a 333MHz handheld, and a
   few seconds of black screen is indistinguishable from a machine that
   has crashed -- which, for something people put on their own PSP, is
   the worst first impression available.

   So say what is happening and how far along it is. Drawn through the
   normal char screen and Flush(), so only the cells that change get
   repainted and the bar fills without flickering. */

static AppWindow *bootWindow_ = 0;
static const char *bootPhase_ = "";

/* Nine letters and eight gaps: 36 of the 40 columns. A filled cell is
   drawn as an inverted space, so this needs no glyphs the font might
   not have.
   
   Every letter is three cells wide except N, which is four. Three
   columns can hold two uprights or a diagonal but not both, so a
   three wide N is an H with a nick in it -- the first version of this
   read PSPATTERH on screen, which is the sort of thing that is
   obvious the moment you look and invisible while you are counting
   columns. */
void AppWindow::DrawWordmark(int x0, int y0) {

    static const char *mark[5] = {
        "### ### ### ### ### ### ### ### #..#",
        "#.# #.. #.# #.# .#. .#. #.. #.# ##.#",
        "### ### ### ### .#. .#. ### ### #.##",
        "#.. ..# #.. #.# .#. .#. #.. ##. #..#",
        "#.. ### #.. #.# .#. .#. ### #.# #..#",
    };
    const int markW = 36;

    GUITextProperties props;
    props.invert_ = true;

    // a shadow one cell down and right, so the letters have some
    // weight on a screen with no panels on it
    SetColor(CD_ROW);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < markW; c++)
            if (mark[r][c] == '#') {
                GUIPoint p(x0 + c + 1, y0 + r + 1);
                DrawString(" ", p, props);
            }

    SetColor(CD_CURSOR);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < markW; c++)
            if (mark[r][c] == '#') {
                GUIPoint p(x0 + c, y0 + r);
                DrawString(" ", p, props);
            }

    props.invert_ = false;
    SetColor(CD_NORMAL);
}

void AppWindow::DrawBootProgress(const char *phase, const char *what,
                                 int done, int total) {
    Clear(false);
    GUITextProperties props;
    GUIPoint pos;
    char line[42];

    /* The wordmark goes HERE, not on the null view behind it. The
       null view is covered by the project dialog and gone in a frame
       when a project auto-loads: this is the screen anybody actually
       looks at, for the seconds the sample bank takes to open. */
    DrawWordmark((40 - 36) / 2, 6);

    SetColor(CD_ROW2);
    pos._x = 6; pos._y = 14;
    snprintf(line, sizeof(line), "%-28s", phase ? phase : "");
    DrawString(line, pos, props);

    SetColor(CD_NORMAL);
    pos._x = 6; pos._y = 15;
    snprintf(line, sizeof(line), "%-28s", what ? what : "");
    DrawString(line, pos, props);

    // the bar, in pixels: 28 cells wide, starting at cell 6
    int barW = 28 * 8;
    int fill = (total > 0) ? (done * barW) / total : barW;
    if (fill < 0) fill = 0;
    if (fill > barW) fill = barW;
    OpBar(6 * 8, 17 * 8, barW, fill, false);

    Flush();
}

static void bootProgressCb(const char *what, int done, int total) {
    if (bootWindow_)
        bootWindow_->DrawBootProgress(bootPhase_, what, done, total);
}

void AppWindow::LoadProject(const Path &p) {
    Trace::Log("LoadProject", "%s\n", p.GetPath().c_str());
    _root = p;

    // Whatever we load is the project to come back to. This used to be
    // recorded only when you picked one from the boot dialog, so a Save
    // As switched you to the new project for this session and then
    // booted you back into the old one next time -- with no sign that
    // the edits you kept making were going to the wrong copy.
    SaveLastProject(p);

    undoClear();

    _closeProject = false;
    _loadAfterSaveAsProject = false;

    PersistencyService *persist = PersistencyService::GetInstance();

    // Mixer, Groove and TableHolder are Persistent singletons built on
    // first use. Mixer's first use was PlayerMixer::Update, on the audio
    // thread, which is long after the save is restored -- so the Mixer
    // was not in the service list when Restore walked it, and the whole
    // MIXER section of the file was skipped every single load. Per
    // channel volume, the high and low pass settings and the bus
    // routing all silently reverted to defaults on open. Touch them
    // here so they are registered before anything is restored.
    Mixer::GetInstance();
    Groove::GetInstance();
    TableHolder::GetInstance();

    TablePlayback::Reset();

    Path::SetAlias("project", _root.GetPath().c_str());
    Path::SetAlias("samples", "project:samples");

    // Load the sample pool

    SamplePool *pool = SamplePool::GetInstance();

    bootWindow_ = this;
    GUIWindow::Clear(backgroundColor_, true);
    Clear(true);
    bootPhase_ = "synthesising drums";
    SamplePool::SetProgressCallback(bootProgressCb);
    DrawBootProgress(bootPhase_, "", 0, DRUMKIT_TOTAL);

    unsigned int load_result = pool->Load();

    SamplePool::SetProgressCallback(0);
    bootPhase_ = "opening project";
    // GetName() is not const, and this Path is
    Path forName(p.GetPath().c_str());
    DrawBootProgress(bootPhase_, forName.GetName().c_str(), 0, 1);

    Project *project = new Project();

    bool succeeded = persist->Load();
    if (!succeeded) {
        project->GetInstrumentBank()->AssignDefaults();
    };

    // Project

    WatchedVariable::Disable();

    project->GetInstrumentBank()->Init();

    WatchedVariable::Enable();

    bootPhase_ = "starting up";
    DrawBootProgress(bootPhase_, "", 1, 1);
    bootWindow_ = 0;

    ApplicationCommandDispatcher::GetInstance()->Init(project);

    // Create view data

    _viewData = new ViewData(project);

    // Create & observe the player
    Player *player = Player::GetInstance();
    bool playerOK = player->Init(project, _viewData);
    player->AddObserver(*this);

    // let a MIDI keyboard play the instrument on screen
    MidiNoteInput::GetInstance()->Attach();
    MidiNoteInput::GetInstance()->SetProject(project);

    // Create the controller
    UIController *controller = UIController::GetInstance();
    controller->Init(project, _viewData);

    // Create & observe all views
    _songView = new SongView((*this), _viewData, _root.GetName().c_str());
    _songView->AddObserver((*this));

    _chainView = new ChainView((*this), _viewData);
    _chainView->AddObserver((*this));

    _phraseView = new PhraseView((*this), _viewData);
    _phraseView->AddObserver((*this));

    _projectView = new ProjectView((*this), _viewData);
    _projectView->AddObserver((*this));

    _instrumentView = new InstrumentView((*this), _viewData);
    _instrumentView->AddObserver((*this));

    _tableView = new TableView((*this), _viewData);
    _tableView->AddObserver((*this));

    _grooveView = new GrooveView((*this), _viewData);
    _grooveView->AddObserver(*this);

    _configView = new ConfigView((*this), _viewData);
    _configView->AddObserver(*this);

    _fxView = new FxView((*this), _viewData);
    _fxView->AddObserver(*this);

    _mixerView = new MixerView((*this), _viewData);
    _mixerView->AddObserver(*this);

    _currentView = _songView;
    _currentView->OnFocus();

    // Baseline: what is in memory now is what is on disk.
    _savedChecksum = persist->Checksum();
    _dirtySince = 0;
    _lastAutosaveCheck = System::GetInstance()->GetClock();

    if (!playerOK) {
        MessageBox *mb =
            new MessageBox(*_songView, "Failed to initialize audio", MBBF_OK);
        _songView->DoModal(mb);
    }

    // An autosave still sitting here means the last session ended
    // without a clean save -- battery, crash, or the power switch.
    {
        Path autoPath(AUTOSAVE_PATH);
        if (autoPath.Exists()) {
            MessageBox *mb = new MessageBox(
                *_songView, "Recover unsaved changes?", MBBF_YES | MBBF_NO);
            _songView->DoModal(mb, RecoverCallback);
        }
    }

    // Report on sample & SoundFont load fails
    if (load_result) {
      // load_result is a bitmask, so these have to be bit tests. The
      // third arm used to read `load_result == A | B`, and == binds
      // tighter than | -- it evaluated to (load_result==A)|B, never
      // zero, so it caught everything that reached it and the invalid
      // directory message below was unreachable.
      const char *err_str =
	  (load_result & SLOAD_ERR_INVALID_DIR) ? "Sample directory could not be opened"
	: ((load_result & SLOAD_ERR_MAX_SAMPLES) && (load_result & SLOAD_ERR_MAX_SOUNDFONTS)) ? "Too many samples and SoundFonts"
	: (load_result & SLOAD_ERR_MAX_SAMPLES) ? "Maximum number of samples exceeded"
	: (load_result & SLOAD_ERR_MAX_SOUNDFONTS) ? "Too many SoundFonts to load"
	: (load_result & SLOAD_ERR_INPUT_FILE) ? "Some samples could not be read"
	: "Unknown error loading sample pool";
      Trace::Error(err_str) ;
      MessageBox *mb =
            new MessageBox(*_currentView, err_str);	  
    _currentView->DoModal(mb);
    }
    
    Redraw();
}

void AppWindow::CloseProject(bool showPicker) {

    _closeProject = false;
    Player *player = Player::GetInstance();
    player->Stop();
    player->RemoveObserver(*this);

    player->Reset();

    SamplePool *pool = SamplePool::GetInstance();
    pool->Reset();

    TableHolder::GetInstance()->Reset();
    TablePlayback::Reset();

    ApplicationCommandDispatcher::GetInstance()->Close();

    SAFE_DELETE(_songView);
    SAFE_DELETE(_chainView);
    SAFE_DELETE(_phraseView);
    SAFE_DELETE(_projectView);
    SAFE_DELETE(_instrumentView);
    SAFE_DELETE(_tableView);
    // LoadProject builds these two as well; they were the only ones
    // left behind on every project switch, and the PSP has 32MB.
    SAFE_DELETE(_grooveView);
    SAFE_DELETE(_configView);
    SAFE_DELETE(_fxView);
    SAFE_DELETE(_mixerView);

    UIController *controller = UIController::GetInstance();
    controller->Reset();

    SAFE_DELETE(_viewData);

    _currentView = _nullView;
    _nullView->SetDirty(true);

    /* The picker ONLY when nothing is about to load. The save-as flow
       runs CloseProject immediately followed by LoadProject -- and the
       unconditional picker here opened a dialog that the load then
       built a whole new set of views underneath. The orphaned modal
       haunted the session: phantom picker, saves that seemed to land
       only when it was dismissed, crashes on its dead references. */
    if (showPicker) {
        SelectProjectDialog *spd = new SelectProjectDialog(*_currentView);
        _currentView->DoModal(spd, ProjectSelectCallback);
    }
};

AppWindow *AppWindow::Create(GUICreateWindowParams &params) {
    I_GUIWindowImp &imp =
        I_GUIWindowFactory::GetInstance()->CreateWindowImp(params);
    AppWindow *w = new AppWindow(imp);
    return w;
};

void AppWindow::SetDirty() { _isDirty = true; };

bool AppWindow::onEvent(GUIEvent &event) {

    // We need to tell the app to quit once we're out of the
    // mixer lock, otherwise the windows driver will never return

    _shouldQuit = false;

    _isDirty = false;

    unsigned short v = 1 << event.GetValue();

    MixerService *sm = MixerService::GetInstance();
    sm->Lock();

    switch (event.GetType()) {

    case ET_PADBUTTONDOWN:
        _lastInputAt = System::GetInstance()->GetClock();

        _mask |= v;
        // Holding the nav modifier brings up the screen map, so the arrows
        // have something to aim at. But R is also the mute/solo modifier, so
        // the map must not appear when R is part of a chord -- it used to
        // cover the mixer's channel strips for the whole gesture.
        if ((v & EPBM_R) && !navMapVisible_ && !(_mask & ~EPBM_R)) {
            navMapVisible_ = true;
            // the map is a menu: the highlight starts on this screen
            // and the arrows below walk it while R stays down
            navSel_ = currentViewType();
            navigating_ = true;
            _isDirty = true;
        }
        if (navMapVisible_ && (_mask & ~(EPBM_R | EPBM_LEFT | EPBM_RIGHT |
                                         EPBM_UP | EPBM_DOWN))) {
            navMapVisible_ = false;   // a chord started: get out of the way
            navigating_ = false;      // and the release will not jump
            _isDirty = true;
        }
        // While the menu is up, the arrows move its highlight and go no
        // further -- the views' own R+arrow chords are retired in favour
        // of one consistent walk of the drawn grid.
        if (navMapVisible_ && navigating_ && (_mask & EPBM_R) &&
            (v & (EPBM_LEFT | EPBM_RIGHT | EPBM_UP | EPBM_DOWN))) {
            int dx = (v & EPBM_RIGHT) ? 1 : ((v & EPBM_LEFT) ? -1 : 0);
            int dy = (v & EPBM_DOWN) ? 1 : ((v & EPBM_UP) ? -1 : 0);
            if (navMove(dx, dy)) _isDirty = true;
            break;
        }
        // Undo is R+L: hold the nav modifier, then press L. Order
        // matters and this is the right way round -- L on its own is
        // paste, so a chord that let L arrive first would paste before
        // it undid. R on its own only brings up the screen map.
        //
        // Views without undo never see it taken away, which is why the
        // mixer's own R+L still unmutes everything.
        if ((v & EPBM_L) && (_mask & EPBM_R) && _currentView &&
            (_currentView->UndoSize() > 0)) {
            undoPerform();
            break;
        }
        if (_currentView) {
            undoBeforeEdit();
            _currentView->ProcessButton(_mask, true);
            undoAfterEdit();
        }
        break;

    case ET_PADBUTTONUP:

        _mask &= (0xFFFF - v);
        if (navMapVisible_ && !(_mask & EPBM_R)) {
            navMapVisible_ = false;
            _isDirty = true;   // the view repaints over the map
            // releasing R commits the menu: the leaving view first gets
            // to prep the destination's context (a drill keeps
            // following the cursor), then the jump
            if (navigating_) {
                navigating_ = false;
                if (navSel_ != currentViewType() && _currentView) {
                    ViewType dest = navPrep(currentViewType(), navSel_);
                    if (dest != currentViewType()) switchToView(dest);
                }
            }
        }
        if (_currentView)
            _currentView->ProcessButton(_mask, false);
        break;

    case ET_PADANALOGFLICK:
        if (_currentView && !_currentView->HasModal()) {
            _currentView->OnNubFlick((int)event.GetValue(), _mask);
            // the deferred work a flick queues (type swap, engine
            // rebuild) runs at the top of DrawView -- without this the
            // redraw never fired and the flick looked like nothing
            _isDirty = true;
        }
        break;

    case ET_SYSQUIT:
        _shouldQuit = true;
        break;

        /*		case ET_KEYDOWN:
            if
           (event.GetValue()==EKT_ESCAPE&&!Player::GetInstance()->IsRunning()) {
                if (_currentView!=_listView) {
                    CloseProject() ;
                    _isDirty=true ;
                } else {
                    System::GetInstance()->PostQuitMessage() ;
                };
            } ;*/

    default:
        break;
    }
    sm->Unlock();

    if (_shouldQuit) {
        onQuitApp();
    }
    if (_closeProject) {
        CloseProject();
        _isDirty = true;
    }
    if (_loadAfterSaveAsProject) {
        _loadAfterSaveAsProject = false;
        CloseProject(false);   // no picker: the load below is the point
        _isDirty = true;
        LoadProject(_newProjectToLoad.c_str());
    }
#ifdef _SHOW_GP2X_
    Redraw();
#else
    if (_isDirty)
        Redraw();
#endif
    return false;
};

void AppWindow::onUpdate() {
    if (_loadAfterResume) {
        _loadAfterResume = false;
        _isDirty = true;
        LoadProject(_newProjectToLoad.c_str());
        return;
    }

    unsigned long now = System::GetInstance()->GetClock();

    // measured repaint rate, for the readout on the song screen
    {
        static unsigned int calls = 0;
        static unsigned long mark = 0;
        calls++;
        if (mark == 0) mark = now;
        if (now - mark >= 1000) {
            uiFps_ = (int)(calls * 1000 / (now - mark));
            calls = 0;
            mark = now;
        }
    }

    // The animated panels -- scope, meters, battery -- run on the
    // wall clock, not on a count of repaints. Repaints arrive from
    // both the ticker and the audio thread and neither rate is fixed,
    // so "every second one" was never a frame rate.
    static unsigned long lastAnim = 0;
    if ((now - lastAnim) >= (unsigned long)uiFrameMs_) {
        lastAnim = now;
        {   // how often the animated panels actually run: the
            // number to watch when the scope or the meters look slow
            static unsigned int acalls = 0;
            static unsigned long amark = 0;
            acalls++;
            if (amark == 0) amark = now;
            if (now - amark >= 1000) {
                animFps_ = (int)(acalls * 1000 / (now - amark));
                acalls = 0;
                amark = now;
            }
        }
        if (_currentView) {
            _currentView->AnimationUpdate();
        }
        autoSaveTick();
    }
    Flush();
}

void AppWindow::LayoutChildren() {};

// Which of the views _currentView currently points at.
ViewType AppWindow::currentViewType() {
    if (_currentView == _projectView)         return VT_PROJECT;
    else if (_currentView == _grooveView)     return VT_GROOVE;
    else if (_currentView == _configView)     return VT_CONFIG;
    else if (_currentView == _fxView)         return VT_FX;
    else if (_currentView == _chainView)      return VT_CHAIN;
    else if (_currentView == _phraseView)     return VT_PHRASE;
    else if (_currentView == _instrumentView) return VT_INSTRUMENT;
    else if (_currentView == _mixerView)      return VT_MIXER;
    // NOT a flat VT_TABLE: the table editor sits in two cells of the
    // map and returning the same one for both meant the highlight
    // never moved when you walked into the second, which reads as
    // "the cursor will not go there".
    else if (_currentView == _tableView)      return _tableView->GetViewType();
    return VT_SONG;
}

/* Context prep for a menu jump. A single hop asks the leaving view
   (its OnNavTo override). A DEEP jump -- song straight to phrase or
   instrument -- runs the preps along the hierarchy chain, so landing
   on the phrase from the song follows the cursor through the chain
   exactly as two single hops would: the cell under the song cursor
   names the chain, the chain's row names the phrase. */
/* Returns where the jump actually lands. A prep along the chain may
   REFUSE (an empty chain row does not lead to a phrase until one is
   placed -- the LSDJ rule), and then the traveller stops at the
   blockage instead of arriving past it on stale state. */
ViewType AppWindow::navPrep(ViewType from, ViewType to) {
    if (from == VT_SONG &&
        (to == VT_PHRASE || to == VT_INSTRUMENT ||
         to == VT_TABLE || to == VT_TABLE2)) {
        if (_songView && !_songView->OnNavTo(VT_CHAIN)) return VT_SONG;
        if (_chainView && !_chainView->OnNavTo(VT_PHRASE)) return VT_CHAIN;
        if (to == VT_INSTRUMENT && _phraseView)
            _phraseView->OnNavTo(VT_INSTRUMENT);
        if ((to == VT_TABLE || to == VT_TABLE2) && _phraseView &&
            !_phraseView->OnNavTo(to))
            return VT_PHRASE;   // no table to follow: stop at the phrase
        return to;
    }
    if (from == VT_CHAIN &&
        (to == VT_INSTRUMENT || to == VT_TABLE || to == VT_TABLE2)) {
        if (_chainView && !_chainView->OnNavTo(VT_PHRASE)) return VT_CHAIN;
        if (to == VT_INSTRUMENT) {
            if (_phraseView) _phraseView->OnNavTo(VT_INSTRUMENT);
            return to;
        }
        if (_phraseView && !_phraseView->OnNavTo(to)) return VT_PHRASE;
        return to;
    }
    if (from == VT_PHRASE || from == VT_INSTRUMENT) {
        if (_currentView && !_currentView->OnNavTo(to)) return from;
        return to;
    }

    /* EVERY OTHER SCREEN -- mixer, config, FX, groove, project, the
       tables -- jumps into the song hierarchy through the same gate:
       the song cursor's context, with the same refusals. These
       screens used to slide through on whatever chain/phrase/table
       was last open, which made the strict rules look optional. */
    if (to == VT_CHAIN || to == VT_PHRASE || to == VT_INSTRUMENT ||
        to == VT_TABLE || to == VT_TABLE2) {
        if (from == VT_MIXER && _viewData) {
            // the mixer's column IS its song cursor
            _viewData->songX_ =
                _viewData->mixerCol_ > 7 ? 7 : _viewData->mixerCol_;
        }
        unsigned char cell = *_viewData->GetCurrentSongPointer();
        if (cell == 0xFF) {
            if (_currentView)
                _currentView->SetNotification("empty slot - place a chain first");
            return from;
        }
        _viewData->currentChain_ = cell;
        if (to == VT_CHAIN) return to;
        if (_chainView && !_chainView->OnNavTo(VT_PHRASE)) return VT_CHAIN;
        if (to == VT_PHRASE) return to;
        if (to == VT_INSTRUMENT) {
            if (_phraseView) _phraseView->OnNavTo(VT_INSTRUMENT);
            return to;
        }
        if (_phraseView && !_phraseView->OnNavTo(to)) return VT_PHRASE;
        return to;
    }
    if (_currentView && !_currentView->OnNavTo(to)) return from;
    return to;
}

// The one place a screen switch happens: views fire VET_SWITCH_VIEW at
// it, and the nav menu jumps through it on R-release.
void AppWindow::switchToView(ViewType vt) {
    if (_currentView) {
        _currentView->LooseFocus();
    }
    switch (vt) {
    case VT_SONG:       _currentView = _songView;       break;
    case VT_CHAIN:      _currentView = _chainView;      break;
    case VT_PHRASE:     _currentView = _phraseView;     break;
    case VT_PROJECT:    _currentView = _projectView;    break;
    case VT_INSTRUMENT: _currentView = _instrumentView; break;
    case VT_TABLE:      _currentView = _tableView;      break;
    case VT_TABLE2:     _currentView = _tableView;      break;
    case VT_GROOVE:     _currentView = _grooveView;     break;
    case VT_CONFIG:     _currentView = _configView;     break;
    case VT_FX:         _currentView = _fxView;         break;
    case VT_MIXER:
        // remember where we came from before _currentView moves on
        if (_mixerView) {
            _mixerView->SetPreviousViewType(currentViewType());
        }
        _currentView = _mixerView;
        break;
    }
    _currentView->SetFocus(vt);
    _isDirty = true;
    GUIWindow::Clear(backgroundColor_, true);
    Clear(true);
    Redraw();
}

void AppWindow::Update(Observable &o, I_ObservableData *d) {

    ViewEvent *ve = (ViewEvent *)d;

    switch (ve->GetType()) {

    case VET_SWITCH_VIEW: {
        ViewType *vt = (ViewType *)ve->GetData();
        switchToView(*vt);
        break;
    }

    case VET_PLAYER_POSITION_UPDATE: {
        PlayerEvent *pt = (PlayerEvent *)ve;

        // A song whose repeat is "once" reaches its end and stops with
        // nobody pressing START, and the render was only ever torn down
        // in the START handler on the song screen. Two things went
        // wrong with that. The screen kept saying "rendering to wav"
        // over a file the mixer had already closed, and the mixer was
        // left in stereo render mode, so the next START opened a fresh
        // writer on the same path and truncated the finished render to
        // whatever the new take reached.
        //
        // It belongs here rather than in a view: only the CURRENT view
        // is handed this event, and not even that one while a modal is
        // open, so a song ending on any other screen left the flag set.
        // Stopping with START clears the flag before the player stops,
        // so this fires once either way and never notifies twice.
        if (pt->GetType() == PET_STOP && _viewData &&
            _viewData->isRendering_) {
            _viewData->isRendering_ = false;
            MixerService::GetInstance()->SetRenderMode(MSRM_PLAYBACK);
            // The notification belongs to whichever screen is up. It is
            // drawn by the view, so there has to be one to draw it; the
            // state above is cleared either way.
            //
            // Redraw() is not decoration. A notification is painted into
            // the char grid by DrawView, and once playback stops the only
            // thing still repainting is AnimationUpdate, which draws the
            // side panel and never touches the bottom row. Without a full
            // redraw the new message is set and never drawn, and the
            // "rendering to wav..." characters from the START of the take
            // stay on screen over a finished file.
            if (_currentView) {
                _currentView->SetNotification("render complete");
                Redraw();
            }
        }

        // A dialog owns the screen while it is up. OnPlayerUpdate paints
        // straight into the char grid -- play cursors, the note readout,
        // the hint bar -- so letting it run while a modal is open draws
        // the transport right through the box. Nothing is lost by
        // skipping it: closing the modal marks the view dirty and the
        // whole thing repaints.
        if (_currentView && !_currentView->HasModal()) {
            SysMutexLocker locker(drawMutex_);
            _currentView->OnPlayerUpdate(pt->GetType(), pt->GetTickCount());
            Invalidate();
        }

        break;
    }

        /*	  case VET_LIST_SELECT:
              {
                char *name=(char*)ve->GetData() ;
                LoadProject(name) ;
                break ;
              } */

    case VET_SAVEAS_PROJECT: {
        char *name = (char *)ve->GetData();
        _loadAfterSaveAsProject = true;
        _newProjectToLoad = name;
        break;
    }

    case VET_PROJECT_SAVED:
        ClearAutosave();
        break;

    case VET_QUIT_PROJECT: {
        // defer event to after we got out of the view
        _closeProject = true;
        break;
    }
    case VET_QUIT_APP:
        _shouldQuit = true;
        break;
    }
}

void AppWindow::onQuitApp() {
    Player *player = Player::GetInstance();
    player->Stop();
    player->RemoveObserver(*this);

    player->Reset();
    System::GetInstance()->PostQuitMessage();
}
void AppWindow::Print(char *line) {

    //	GUIWindow::Clear(View::backgroundColor_,true) ;
    Clear();
    // the caller's buffer is 4096 bytes and _statusLine is 80, with a live
    // std::string next to it -- "Loading <long filename>" was enough to
    // smash it
    strncpy(_statusLine, line, sizeof(_statusLine) - 1);
    _statusLine[sizeof(_statusLine) - 1] = 0;
    // unwrapped for gcc
    int position = 40;
    position -= (int)strlen(_statusLine);
    if (position < 0) position = 0;   // int - size_t used to wrap negative
    position /= 2;
    GUIPoint pos(position, 12);
    //
    GUITextProperties props;
    SetColor(CD_NORMAL);
    DrawString(_statusLine, pos, props);
    char buildString[80];
    sprintf(buildString, "%s %s", PSPATTERN_NAME, PSPATTERN_VERSION_STRING);
    pos._y = 28;
    pos._x = (40 - strlen(buildString)) / 2;
    DrawString(buildString, pos, props);
    Flush();
};

void AppWindow::SetColor(ColorDefinition cd) { colorIndex_ = cd; };

Path AppWindow::GetLastProjectPath() {
    Path lastProjectFile(LAST_PROJECT_NAME);
    FileSystem *fs = FileSystem::GetInstance();
    I_File *file = fs->Open(lastProjectFile.GetPath().c_str(), "r");

    if (!file) {
        return Path();
    }

    // Get file size
    file->Seek(0, SEEK_END);
    int length = file->Tell();
    
    if (length <= 0) {
        file->Close();
        delete file;
        return Path();
    }

    // Allocate buffer and seek back to start
    char *buffer = (char *)SYS_MALLOC(length + 1);
    memset(buffer, 0, length + 1);

    file->Seek(0, SEEK_SET); // Seek back to start
    int bytes = file->Read(buffer, 1, length); // Read full length
    file->Close();
    delete file;

    if (bytes <= 0) {
        Trace::Error("GetLastProject: Failed to read last project file");
        SYS_FREE(buffer);
        return Path();
    }

    buffer[bytes] = 0; // Null terminate

    // Remove newline if present
    int len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = 0;
    }

    Path result;
    if (strlen(buffer) > 0) {
        if (strstr(buffer, "lgpt_") != NULL) { // Ensure it's an lgpt project
            result = Path(buffer);
        } else {
            Trace::Error("GetLastProject: Invalid project path format: %s",
                         buffer);
        }
    }
    if (!result.IsDirectory()) {
        Trace::Error("GetLastProject: path does not exist: %s", result.GetPath().c_str());
    }

    SYS_FREE(buffer);
    return result;
}

/* Snapshot whatever the current view is editing, before it sees the
   keypress. Nothing is kept yet -- undoAfterEdit decides. */
void AppWindow::undoBeforeEdit() {

    _undoScratchSize = 0;
    _undoScratchView = 0;
    if (!_currentView) return;

    int size = _currentView->UndoSize();
    if ((size <= 0) || (size > UNDO_MAX_BYTES)) return;

    _currentView->UndoCapture(_undoScratch);
    _undoScratchSize = size;
    _undoScratchView = _currentView;
    _undoScratchContext = _currentView->UndoContext();
}

/* Keep the snapshot only if the keypress actually changed something.
   A cursor move, a mode toggle or a refused edit costs nothing. */
void AppWindow::undoAfterEdit() {

    if ((_undoScratchSize <= 0) || (!_undoScratchView)) return;
    if (_currentView != _undoScratchView) {
        _undoScratchSize = 0;
        return;
    }

    static unsigned char after[UNDO_MAX_BYTES];
    if (_currentView->UndoContext() != _undoScratchContext) {
        // the view moved to a different object: nothing was edited here
        _undoScratchSize = 0;
        return;
    }
    _currentView->UndoCapture(after);
    if (memcmp(after, _undoScratch, _undoScratchSize) == 0) {
        _undoScratchSize = 0;
        return;
    }

    UndoEntry &e = _undo[_undoHead];
    e.view = _undoScratchView;
    e.context = _undoScratchContext;
    e.size = _undoScratchSize;
    memcpy(e.data, _undoScratch, _undoScratchSize);
    _undoHead = (_undoHead + 1) % UNDO_SLOTS;
    if (_undoCount < UNDO_SLOTS) _undoCount++;
    _undoScratchSize = 0;
}

void AppWindow::undoPerform() {

    if (_undoCount <= 0) {
        if (_currentView) _currentView->SetNotification("nothing to undo");
        return;
    }

    _undoHead = (_undoHead + UNDO_SLOTS - 1) % UNDO_SLOTS;
    _undoCount--;
    UndoEntry &e = _undo[_undoHead];

    if (e.view != _currentView) {
        // The edit happened on another screen. Rather than silently
        // change something you cannot see, say where it was.
        if (_currentView) {
            _currentView->SetNotification("that edit was on another screen");
        }
        // put it back: the user can go there and undo it
        _undoHead = (_undoHead + 1) % UNDO_SLOTS;
        _undoCount++;
        return;
    }

    _currentView->UndoRestore(e.context, e.data);
    _currentView->SetDirty(true);
    _currentView->SetNotification("undone");
    _isDirty = true;
}

/* A new project has nothing to undo into. */
void AppWindow::undoClear() {
    _undoCount = 0;
    _undoHead = 0;
    _undoScratchSize = 0;
    _undoScratchView = 0;
}

/* Hash the project every few seconds; when it stops matching what is
   on disk, write a recovery file. */
void AppWindow::autoSaveTick() {

    if (!_viewData) return;                 // no project open
    if (_currentView && _currentView->HasModal()) return;

    unsigned long now = System::GetInstance()->GetClock();
    if ((now - _lastAutosaveCheck) < AUTOSAVE_CHECK_MS) return;
    _lastAutosaveCheck = now;

    /* A live set must never lose input or a tail to a file write.
       AUTOSAVE NO -- editable on the config screen, effective
       immediately -- disarms the whole machine for the night. Manual
       save still works; the recovery file just stops being written. */
    {
        const char *as = Config::GetInstance()->GetValue("AUTOSAVE");
        if (as && (as[0] == 'N' || as[0] == 'n')) return;
    }

    PersistencyService *persist = PersistencyService::GetInstance();
    unsigned int current = persist->Checksum();
    if (current == _savedChecksum) {
        _dirtySince = 0;
        _lastSeenChecksum = current;
        return;
    }
    if (_dirtySince == 0) {
        _dirtySince = now;
    }

    // Still being edited? Then wait. The write costs a second or two
    // of frozen input on this machine, and doing that while somebody
    // is placing chains is the worst possible moment for it.
    if (current != _lastSeenChecksum) {
        _lastSeenChecksum = current;
        _lastChangeAt = now;
        return;
    }
    if ((now - _lastChangeAt) < AUTOSAVE_QUIET_MS &&
        (now - _dirtySince) < AUTOSAVE_FORCE_MS) {
        return;
    }
    // Nobody touching it, either. Debouncing the data alone still
    // fires into the gap where somebody stopped to think and is about
    // to press the next button.
    if (_lastInputAt && (now - _lastInputAt) < AUTOSAVE_IDLE_MS &&
        (now - _dirtySince) < AUTOSAVE_FORCE_MS) {
        return;
    }

    /* THE BACKSTOP MUST STILL LAND IN A GAP.

       Every guard above ends with "unless we have been dirty for two
       minutes", and the write takes a second or more of the main
       thread. So somebody editing steadily for two minutes -- which
       is what working on a song IS -- got the write dropped into the
       middle of a keypress: the machine stops, and the button release
       that arrives during the stall is lost, so that direction stays
       held and does nothing until it is pressed again. That is the
       freeze-then-one-dead-direction the editing sessions run into.

       A backstop is allowed to interrupt the work, but not the
       gesture. Half a second without a button is a gap that occurs
       constantly while editing and never delays the write for long.
       AUTOSAVE_HARD_MS is the end of the argument: past that it
       writes whatever anyone is doing, because a recovery file that
       is never written is not a recovery file. */
    if ((now - _lastInputAt) < AUTOSAVE_GAP_MS &&
        (now - _dirtySince) < AUTOSAVE_HARD_MS) {
        return;
    }

    /* Never while the player is running. Not "not for two minutes" --
       never.

       A Memory Stick write can take longer than the audio buffer
       lasts, so an autosave during playback is an audible dropout.
       This used to hold off only until AUTOSAVE_FORCE_MS and then
       write anyway, on the reasoning that people leave playback
       running for hours and their work should not sit unsaved that
       whole time. The reasoning is sound and the conclusion was
       wrong: the answer to "they might play for hours" is not to
       glitch the audio at the two minute mark, in a music program,
       while they are listening to the thing they are making.

       Nothing is lost by waiting. The transport stops eventually, and
       the next tick after it does saves within a few seconds -- the
       quiet and idle debounces above are six and ten seconds, not
       minutes. Manual save is still there for anyone who wants it
       sooner. */
    if (Player::GetInstance()->IsRunning()) {
        return;
    }
    if (AudioStats::QuietMs() < AUTOSAVE_TAIL_MS &&
        (now - _dirtySince) < AUTOSAVE_FORCE_MS) {
        return;
    }

    if (persist->Save(AUTOSAVE_PATH)) {
        _savedChecksum = current;
        _dirtySince = 0;
        Trace::Log("AUTOSAVE", "wrote %s", AUTOSAVE_PATH);
    } else {
        Trace::Error("autosave failed");
        // leave _dirtySince alone: try again on the next tick
    }
}

/* A successful manual save means there is nothing left to recover. */
void AppWindow::ClearAutosave() {
    PersistencyService *persist = PersistencyService::GetInstance();
    _savedChecksum = persist->Checksum();
    _dirtySince = 0;
    Path autoPath(AUTOSAVE_PATH);
    if (autoPath.Exists()) {
        FileSystem::GetInstance()->Delete(autoPath.GetPath().c_str());
    }
}

/* Answer to the recovery prompt at project load. */
void AppWindow::RecoverAutosave(bool yes) {

    PersistencyService *persist = PersistencyService::GetInstance();

    if (!yes) {
        // Declining throws the recovery file away, so the prompt does
        // not come back every single boot.
        Path autoPath(AUTOSAVE_PATH);
        FileSystem::GetInstance()->Delete(autoPath.GetPath().c_str());
        _savedChecksum = persist->Checksum();
        _dirtySince = 0;
        return;
    }

    Player::GetInstance()->Stop();

    WatchedVariable::Disable();
    bool ok = persist->Load(AUTOSAVE_PATH);
    if (ok && _viewData && _viewData->project_) {
        _viewData->project_->GetInstrumentBank()->Init();
    }
    WatchedVariable::Enable();

    if (ok) {
        // The recovered project is deliberately NOT marked as being in
        // step with disk -- it isn't. _savedChecksum still holds the
        // main save's hash, so the next tick rewrites the recovery file
        // and the work stays protected until a real save.
        _currentView->SetNotification("recovered - save the song");
    } else {
        _currentView->SetNotification("could not read the recovery file");
    }
    _isDirty = true;
    Redraw();
}

void AppWindow::SaveLastProject(const Path &p) {
    Path lastProjectFile(LAST_PROJECT_NAME);
    FileSystem *fs = FileSystem::GetInstance();
    I_File *file = fs->Open(lastProjectFile.GetPath().c_str(), "w");

    if (!file) {
        Trace::Error("SaveLastProject: Failed to open %s for writing",
                     LAST_PROJECT_NAME);
        return;
    }

    std::string pathStr = p.GetPath();
    file->Write(pathStr.c_str(), 1, pathStr.length());
    file->Write("\n", 1, 1);
    file->Close();
    delete file;

    Trace::Log("SaveLastProject", "Saved last project: %s",
               p.GetPath().c_str());
}
