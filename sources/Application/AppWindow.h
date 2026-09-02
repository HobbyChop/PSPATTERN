
#ifndef _APP_WINDOW_H_
#define _APP_WINDOW_H_

#include "Application/Views/ChainView.h"
#include "Application/Views/ConfigView.h"
#include "Application/Views/ConsoleView.h"
#include "Application/Views/FxView.h"
#include "Application/Views/GrooveView.h"
#include "Application/Views/InstrumentView.h"
#include "Application/Views/MixerView.h"
#include "Application/Views/NullView.h"
#include "Application/Views/PhraseView.h"
#include "Application/Views/ProjectView.h"
#include "Application/Views/SongView.h"
#include "Application/Views/TableView.h"
#include "Application/Views/ViewData.h"
#include "Foundation/Observable.h"
#include "System/Process/Process.h"
#include "System/Process/SysMutex.h"
#include "System/io/Status.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

#define PROP_INVERT 0x80

class AppWindow : public GUIWindow, I_Observer, Status {
  protected:
    AppWindow(I_GUIWindowImp &imp);
    virtual ~AppWindow();

  public:
    static AppWindow *Create(GUICreateWindowParams &);
    void LoadProject(const Path &path);
    void SaveLastProject(const Path &p);

    // Autosave. The project is hashed periodically; when the hash moves
    // away from what is on disk, a recovery file is written. See
    // Persistency/Checksum.h for why it is a hash and not a dirty flag.
    /* Undo ring. Entries are small -- the largest object any view
       edits is the 2KB song grid -- so a handful of them costs less
       than a sample. */
#define UNDO_SLOTS 12
#define UNDO_MAX_BYTES 2048
    struct UndoEntry {
        View *view ;
        int   context ;
        int   size ;
        unsigned char data[UNDO_MAX_BYTES] ;
    } ;
    UndoEntry _undo[UNDO_SLOTS] ;
    int _undoCount ;              // how many are live
    int _undoHead ;               // next slot to write
    unsigned char _undoScratch[UNDO_MAX_BYTES] ;
    int _undoScratchSize ;
    View *_undoScratchView ;
    int _undoScratchContext ;

    void undoBeforeEdit();
    void undoAfterEdit();
    void undoPerform();
    void undoClear();

    void autoSaveTick();
    void ClearAutosave();          // a real save makes recovery moot
    void RecoverAutosave(bool yes);
    unsigned int _savedChecksum;   // hash of what is currently persisted
    unsigned long _lastAutosaveCheck;
    unsigned long _dirtySince;     // 0 = in step with disk
    // The checksum seen on the PREVIOUS tick, and when it last moved.
    // Editing has to go quiet before anything is written, or the write
    // lands in the middle of the editing.
    unsigned int _lastSeenChecksum;
    unsigned long _lastChangeAt;
    // When a button was last touched. The write blocks input, so it
    // waits until nobody is using the machine.
    unsigned long _lastInputAt;
    ViewType currentViewType();
    // nav-map menu state: where the highlight sits while R is held
    bool navMove(int dx, int dy);
    ViewType navPrep(ViewType from, ViewType to);
    // read-only twin of the navPrep/OnNavTo refusals: would a jump to
    // `to` from the CURRENT view be allowed? The map greys what would
    // refuse. Must mirror the veto logic and must not mutate.
    bool navReachable(ViewType to);
    ViewType navSel_;
    bool navigating_;
    void CloseProject(bool showPicker = true);

    virtual void Clear(bool all = false);
    // throw away the diff cache so the next Flush repaints every cell
    // and every overlay op — the screen is stale after a resume
    void InvalidateScreen();
    // Re-read the palette from config (used live when the theme setting
    // changes) and force a full repaint.
    void ApplyTheme();
    // the central screen switch (also the nav menu's jump)
    void switchToView(ViewType vt);
    // the screen map shown while the nav modifier is held
    void drawNavMap();
    // What is on screen while a project opens. See LoadProject.
    void DrawBootProgress(const char *phase, const char *what,
                          int done, int total);
    void DrawQuasiMessage(int battPct, int estMinutes, int secsLeft);
    void QuasiBlank();
    void QuasiWake();
    virtual void ClearRect(GUIRect &rect);
    virtual void SetColor(ColorDefinition cd);
    void SetDirty();

    // Repaints per second, measured. The UI is driven by a ticker
    // thread rather than by the audio block rate: block rate follows
    // the tempo (60*rate*2/tempo/8/6 samples), so at 90bpm the screen
    // used to update half as often as at 172, and at 60bpm a third.
    static int uiFps_;
    static int animFps_;   // animated-panel frames per second
    static int uiFrameMs_;    // target period; UIFRAMERATE in config
    void uiTick();            // called from the ticker thread

    // pixel overlay system: typed ops composited with the char grid
    // at flush time. Layer 0 (panels/frames/strips) draws UNDER the
    // text (intersecting text is re-drawn transparently on top);
    // layer 1 (bars/graphs/ticks) draws over. Coordinates are app
    // pixels (8px per char cell). Ops are re-registered by views and
    // fields as they draw; Clear() resets them.
    void OpRect(int layer, int x, int y, int w, int h, int col);
    // A frame drawn OVER the text instead of under it. The layer-0
    // frame is erased wherever an inverted cell repaints itself, so a
    // ring around a filled pill only survives outside the cell -- and
    // outside the cell it lands in the neighbouring row's ink,
    // because app pixels map to the PSP's 9-tall cells as psp*9/8 and
    // the row of leading is not addressable. Drawn on top, the ring
    // can sit on the block's own outer pixels and stay contained.
    void OpRing(int x, int y, int w, int h, int col);
    void OpFrame(int x, int y, int w, int h, int col, int gapX = 0,
                 int gapW = 0);
    void OpBar(int x, int y, int w, int fillPx, bool focused);
    void OpAdsr(int x, int y, int w, int h, int a, int d, int s);
    void OpWave(int x, int y, int w, int h, int kind);
    // vertical meter: track + fill from the bottom, warn colors up top
    // cap: 0 none, 1 hot (this channel is a big share of the mix),
    // 2 clipping
    void OpVBar(int x, int y, int w, int h, int fillPx, int cap = 0);
    // live oscilloscope of the master mix (reads AudioStats at flush;
    // pass a changing tick while audio runs so it repaints)
    // channel 0 is left, 1 is right
    void OpScope(int x, int y, int w, int h, int tick = 0, int channel = 0);
    void OpSpectrum(int x, int y, int w, int h, int tick = 0);

    /* A bar between the background and the play colour, at any
       intensity from 0 to 255.
       
       Text cannot do this. A character cell stores a ColorDefinition
       index rather than a colour, so text can only ever be one of the
       palette entries -- there is no way to say "this row, but a
       third as bright". Anything that fades has to be drawn, not
       written, and this is the primitive for it.
       
       Intensity 0 paints the background, which is how a fade ends
       without needing the op removed. */
    void OpGlow(int x, int y, int w, int h, int intensity);


    /* The wordmark, built from inverted spaces -- a filled cell is
       one "pixel" of it. The font has no block glyphs, and a bitmap
       drawn this way needs nothing from it. */
    void DrawWordmark(int x0, int y0);

    // overlay color ids: ColorDefinition values plus derived tones
    enum {
        OC_PANEL = 0x40,
        OC_PANEL2,
        OC_STRIP,
        OC_GRID,
        OC_WHITE
    };

  protected: // GUIWindow implementation
    virtual bool onEvent(GUIEvent &event);
    virtual void onUpdate();
    virtual void LayoutChildren();
    virtual void Flush();
    virtual void Redraw();

    // override draw string to avoid going too far off
    // the screen.
    virtual void DrawString(const char *string, GUIPoint &pos,
                            GUITextProperties &props, bool overlay = false);

    // I_Observer implementation

    virtual void Update(Observable &o, I_ObservableData *d);

    // Status implementation

    virtual void Print(char *);

    void defineColor(const char *colorName, GUIColor &color);
    // The 13 defineColor() calls, factored out so boot and a live theme
    // change share one definition of the palette.
    void loadPalette();

    void onQuitApp();

  private:
    View *_currentView;
    ViewData *_viewData;
    SongView *_songView;
    ChainView *_chainView;
    PhraseView *_phraseView;
    ProjectView *_projectView;
    InstrumentView *_instrumentView;
    TableView *_tableView;
    GrooveView *_grooveView;
    NullView *_nullView;
    MixerView *_mixerView;
    ConfigView *_configView;
    FxView *_fxView;

    Path _root;

    bool _isDirty;
    bool _closeProject;
    bool _loadAfterSaveAsProject;
    bool _loadAfterResume;
    bool _shouldQuit;
    unsigned short _mask;
    unsigned long _lastA;
    unsigned long _lastB;
    char _statusLine[80];
    std::string _newProjectToLoad;
    unsigned char _charScreen[1200];
    unsigned char _charScreenProp[1200];

    enum OverlayOpType { OOP_RECT, OOP_FRAME, OOP_BAR, OOP_ADSR, OOP_WAVE, OOP_VBAR, OOP_SCOPE, OOP_GLOW, OOP_SPECT };
    struct OverlayOp {
        unsigned char type_, layer_, colA_, focused_;
        short x_, y_, w_, h_;
        short p1_, p2_, p3_, p4_;
    };
#define MAX_OVERLAY_OPS 96
    OverlayOp overlayOps_[MAX_OVERLAY_OPS];
    int overlayOpCount_;
    // ops repaint ONLY when changed, over text the char diff redrew,
    // or over cells wiped because an op vanished (geometry-twin
    // analysis vs this list) — steady-state or blanket repaints
    // shimmer on the PSP's live framebuffer
    OverlayOp prevOps_[96];
    int prevOpCount_;
    bool cellDirty_[1200];
    bool navMapVisible_;   // nav modifier held: show the screen map
    void setOp(const OverlayOp &op);
    void flushOverlayOps();
    void drawClipped(int x0,int y0,int x1,int y1,
                     int bx0,int by0,int bx1,int by1);
    void opLine(int x0, int y0, int x1, int y1);
public:
    GUIColor opColor(int id);
    GUIColor colorForProp(unsigned char prop);
private:
    unsigned char _preScreen[1200];
    unsigned char _preScreenProp[1200];

    static GUIColor backgroundColor_;
    static GUIColor normalColor_;
    static GUIColor borderColor_;
    static GUIColor songviewfeColor_;
    static GUIColor songview00Color_;
    static GUIColor highlight2Color_;
    static GUIColor highlightColor_;
    static GUIColor consoleColor_;
    static GUIColor cursorColor_;
    static GUIColor playColor_;
    static GUIColor muteColor_;
    static GUIColor rownumberColor_;
    static GUIColor rownumber2Color_;
    static GUIColor majorbeatColor_;
#define LAST_PROJECT_NAME "bin:last_project"

    ColorDefinition colorIndex_;

    static int charWidth_;
    static int charHeight_;

    SysMutex drawMutex_;

    Path GetLastProjectPath();
};

#endif
