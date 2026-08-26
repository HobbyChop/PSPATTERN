#include "AppWindow.h"
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

void AppWindow::defineColor(const char *colorName, GUIColor &color) {

    Config *config = Config::GetInstance();
    const char *value = config->GetValue(colorName);
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

void AppWindow::uiTick() { Invalidate(); }

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
void AppWindow::drawNavMap() {

    struct MapCell {
        int col, row;
        const char *name;
        ViewType type;
    };
    static const MapCell cells[] = {
        {0, 0, "project", VT_PROJECT},
        {2, 0, "groove",  VT_GROOVE},
        {0, 1, "song",    VT_SONG},
        {1, 1, "chain",   VT_CHAIN},
        {2, 1, "phrase",  VT_PHRASE},
        {3, 1, "instr",   VT_INSTRUMENT},
        {1, 2, "mixer",   VT_MIXER},
        // The same editor in both places, which is why they share a
        // name. The column is what tells them apart: under phrase it
        // is the table the phrase's instrument runs, under instr it
        // is the table that instrument owns. "table2" made it look
        // like a second feature.
        {2, 2, "table",   VT_TABLE},
        {3, 2, "table",   VT_TABLE2},
    };
    const int cellCount = sizeof(cells) / sizeof(cells[0]);

    // which cell are we on?
    ViewType current = currentViewType();

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
        props.invert_ = here;
        SetColor(here ? CD_HILITE1 : CD_ROW2);
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

    SysMutexLocker locker(drawMutex_);

    if (navMapVisible_ && _currentView && !_currentView->HasModal()) {
        // the map owns the screen while it is up: drop the view's
        // panels and frames so none of them paint across the band
        overlayOpCount_ = 0;
        drawNavMap();
    }

    Lock();
    long flushStart = System::GetInstance()->GetClock();
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
    long flushEnd = System::GetInstance()->GetClock();
    // a modal dialog owns the screen: drop the ops (the erase pass
    // wipes their pixels); the post-modal redraw re-registers them
    if (_currentView && _currentView->HasModal()) {
        overlayOpCount_ = 0;
    }
    flushOverlayOps();
    GUIWindow::Flush();
    Unlock();
    memcpy(_preScreen, _charScreen, 1200);
    memcpy(_preScreenProp, _charScreenProp, 1200);
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

void AppWindow::OpScope(int x, int y, int w, int h, int tick) {
    OverlayOp op = {OOP_SCOPE, 1, 0, 0, (short)x, (short)y, (short)w,
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
                GUIWindow::SetColor(normalColor_);
                GUIRect tick(o.x_ + fill - 1, o.y_ - 1, o.x_ + fill + 1,
                             o.y_ + 8);
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
        case OOP_SCOPE: {
            GUIColor pc = opColor(OC_PANEL);
            GUIWindow::SetColor(pc);
            GUIRect bg(o.x_, o.y_, o.x_ + o.w_, o.y_ + o.h_);
            GUIWindow::DrawRect(bg);
            if (o.w_ < 4 || o.h_ < 4) break;
            bool live = Player::GetInstance()->IsRunning();
            short lo[96], hi[96];
            int n = o.w_ > 96 ? 96 : o.w_;
            if (live) AudioStats::ReadScope(lo, hi, n);
            int amp = o.h_ / 2 - 1;
            int mid = o.y_ + o.h_ / 2;
            GUIColor lc = live ? colorForProp(CD_HILITE1) : opColor(CD_ROW);
            GUIWindow::SetColor(lc);
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

void AppWindow::DrawBootProgress(const char *phase, const char *what,
                                 int done, int total) {
    Clear(false);
    GUITextProperties props;
    GUIPoint pos;
    char line[42];

    SetColor(CD_HILITE1);
    pos._x = 15; pos._y = 11;
    DrawString(PSPATTERN_NAME, pos, props);

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

void AppWindow::CloseProject() {

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
    SAFE_DELETE(_mixerView);

    UIController *controller = UIController::GetInstance();
    controller->Reset();

    SAFE_DELETE(_viewData);

    _currentView = _nullView;
    _nullView->SetDirty(true);

    SelectProjectDialog *spd = new SelectProjectDialog(*_currentView);
    _currentView->DoModal(spd, ProjectSelectCallback);
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
            _isDirty = true;
        }
        if (navMapVisible_ && (_mask & ~(EPBM_R | EPBM_LEFT | EPBM_RIGHT |
                                         EPBM_UP | EPBM_DOWN))) {
            navMapVisible_ = false;   // a chord started: get out of the way
            _isDirty = true;
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
        }
        if (_currentView)
            _currentView->ProcessButton(_mask, false);
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
        CloseProject();
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

void AppWindow::Update(Observable &o, I_ObservableData *d) {

    ViewEvent *ve = (ViewEvent *)d;

    switch (ve->GetType()) {

    case VET_SWITCH_VIEW: {
        ViewType *vt = (ViewType *)ve->GetData();
        if (_currentView) {
            _currentView->LooseFocus();
        }
        switch (*vt) {
        case VT_SONG:
            _currentView = _songView;
            break;
        case VT_CHAIN:
            _currentView = _chainView;
            break;
        case VT_PHRASE:
            _currentView = _phraseView;
            break;
        case VT_PROJECT:
            _currentView = _projectView;
            break;
        case VT_INSTRUMENT:
            _currentView = _instrumentView;
            break;
        case VT_TABLE:
            _currentView = _tableView;
            break;
        case VT_TABLE2:
            _currentView = _tableView;
            break;
        case VT_GROOVE:
            _currentView = _grooveView;
            break;
        case VT_MIXER:
            // remember where we came from before _currentView moves on
            if (_mixerView) {
                _mixerView->SetPreviousViewType(currentViewType());
            }
            _currentView = _mixerView;
            break;
        }
        _currentView->SetFocus(*vt);
        _isDirty = true;
        GUIWindow::Clear(backgroundColor_, true);
        Clear(true);
        Redraw();
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

    // Hold off while the player is running -- a Memory Stick write can
    // take longer than the audio buffer lasts. But only for so long:
    // people leave playback running for hours.
    if (Player::GetInstance()->IsRunning() &&
        ((now - _dirtySince) < AUTOSAVE_FORCE_MS)) {
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
