
#ifndef _VIEW_H_
#define _VIEW_H_

#include "Application/Model/Config.h"
#include "Application/Model/Project.h"
#include "Application/Player/Player.h"
#include "Foundation/T_SimpleList.h"
#include "I_Action.h"
#include "UIFramework/Interfaces/I_GUIGraphics.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "ViewEvent.h"
#ifdef SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif

// the grids end at row 20; command help sits under them, clear of the
// live note readout (rows 26-28) and the hint bar (row 29)
#define COMMAND_HELP_ROW 21

#define VU_METER_HEIGHT 8
#define VU_METER_CLIP_LEVEL 7
#define VU_METER_WARN_LEVEL 5

enum GUIEventPadButtonMasks {
    EPBM_LEFT = 1,
    EPBM_DOWN = 2,
    EPBM_RIGHT = 4,
    EPBM_UP = 8,
    EPBM_L = 16,
    EPBM_B = 32,
    EPBM_A = 64,
    EPBM_R = 128,
    EPBM_START = 256,
    EPBM_SELECT = 512,
    EPBM_DOUBLE_A = 1024,
    EPBM_DOUBLE_B = 2048
};

enum ViewType {
    VT_SONG,
    VT_CHAIN,
    VT_PHRASE,
    VT_PROJECT,
    VT_INSTRUMENT,
    VT_TABLE,  // Table screen under phrase
    VT_TABLE2, // Table screen under instrument
    VT_GROOVE,
    VT_MIXER,
    VT_CONFIG,
    VT_FX
};
// count of real screens (VT_TABLE2 is the table under a different door)
#define VT_COUNT (VT_FX + 1)

enum ViewMode {
    VM_NORMAL,
    VM_NEW,
    VM_CLONE,
    VM_SELECTION,
    VM_MUTEON,
    VM_SOLOON
};

enum ColorDefinition {
    CD_BACKGROUND,
    CD_NORMAL,
    CD_BORDER,
    CD_HILITE1,
    CD_HILITE2,
    CD_CONSOLE,
    CD_CURSOR,
    CD_PLAY,
    CD_MUTE,
    CD_SONGVIEWFE,
    CD_SONGVIEW00,
    CD_ROW,
    CD_ROW2,
    CD_MAJORBEAT
};

enum ViewUpdateDirection { VUD_LEFT = 0, VUD_RIGHT, VUD_UP, VUD_DOWN };

class View;
class ModalView;

typedef void (*ModalViewCallback)(View &v, ModalView &d);

class View : public Observable {
  public:
    View(GUIWindow &w, ViewData *viewData);
    View(View &v);

    void SetFocus(ViewType vt) {
        viewType_ = vt;
        hasFocus_ = true;
        OnFocus();
    };

    // virtual: a view may need to act when the screen is left (the
    // settings screen commits its edits here), whatever door was used
    virtual void LooseFocus() { hasFocus_ = false; };

    // Which map cell this view is currently standing in. The table
    // editor occupies TWO cells -- under phrase and under instr --
    // and only it knows which one it was entered through.
    ViewType GetViewType() const { return viewType_; };

    void Clear();

    void ProcessButton(unsigned short mask, bool pressed);

    void Redraw();
    // a modal dialog is up: animation repaints and overlay ops must
    // yield the screen to it
    bool HasModal() { return modalView_ != 0; }

    // Override in subclasses

    virtual void DrawView() = 0;
    /* Deferred main-thread work that must run OUTSIDE drawMutex_: the
       render thread acquires sync_ then drawMutex_ every block, so any
       code that takes the mixer lock while drawMutex_ is held (as the
       old in-DrawView deferred blocks did) is a lock-order inversion
       -- a permanent deadlock waiting to be timed. AppWindow calls
       this immediately BEFORE Redraw takes drawMutex_. The default
       forwards to an open modal. */
    virtual void ApplyDeferred();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int currentTick) = 0;
    virtual void OnFocus() = 0;
    virtual void AnimationUpdate() {}
    /* The nav map is a menu: hold R, walk the highlight with the
       arrows, release to jump. Before the jump the CURRENT view gets
       this call so a drill keeps its context prep -- song entering
       chain still follows the cursor, the mixer still hands its
       column over -- without the map layer knowing any of it. */
    virtual void OnNavTo(ViewType to) {}
    // The analog nub, delivered as single steps (0=left 1=right 2=up
    // 3=down). The instrument screen uses it as the chord-free browse
    // control for engine (left/right) and type (up/down).
    virtual void OnNubFlick(int dir, unsigned short mask) {}

    void SetDirty(bool dirty);

    // Primitive locking mechanism

    bool Lock();
    void WaitForObject();
    void Unlock();

    // Char based draw routines

    virtual void SetColor(ColorDefinition cd);
    virtual void ClearRect(int x, int y, int w, int h);
    // ---- hobbychop screen kit (pixel overlay + text) ----
    // title strip across row 0 with live info right-aligned
    void DrawTitleStrip(const char *name, const char *right);
    // hint bar across the last row
    void DrawHintBar(const char *hints);
    // framed panel with a tab label; coords in char cells, the label
    // row is cy and the body spans cy+1..cy+ch
    void DrawPanel(int cx, int cy, int cw, int ch, const char *label);
    // the three help lines for an FX command, in a panel under the
    // grid (they used to be drawn at absolute row 0 — on top of the
    // title strip and the first grid rows)
    void DrawCommandHelp(FourCC command);

    virtual void DrawString(int x, int y, const char *txt,
                            GUITextProperties &props);

    void DoModal(ModalView *view, ModalViewCallback cb = 0);

    void EnableNotification();
    // Default row 29 is the hint bar, which is the status line and
    // the one row on every screen that holds nothing a message can
    // destroy. At the old default of 2 a notification landed on the
    // channel headers of the song and mixer screens. Modals pass
    // their own offset, relative to their own window.
    /* Undo.

       A view that edits sequencer data reports the size of the object
       it is editing and can copy that object in and out of a plain
       buffer. AppWindow snapshots before every edit-capable keypress,
       compares afterwards, and keeps the snapshot only if something
       actually changed -- so a cursor move costs nothing.

       Views that return 0 have no undo and the chord passes straight
       through to them, which is why the mixer's own R+L still works.

       Scope is deliberate: this covers song, chain, phrase, table and
       groove, which is where the destructive operations live -- cut,
       paste, clone and interpolate all move a lot of data at one
       keypress. Instrument and project parameters are single values
       and are not covered. */
    virtual int  UndoSize() { return 0 ; }
    virtual int  UndoContext() { return 0 ; }
    virtual void UndoCapture(unsigned char *dst) {}
    virtual void UndoRestore(int context,const unsigned char *src) {}

    void SetNotification(const char *notification, int offset = 29);
    bool notificationLive();

  protected:
    virtual void ProcessButtonMask(unsigned short mask, bool pressed) = 0;

    // to remove once everything got to viewdata

    inline void updateData(unsigned char *c, int offset, unsigned char limit,
                           bool wrap) {
        int v = *c;
        if (v == 0xFF) { // Uninitiaized data
            v = 0;
        }
        v += offset;
        if (v < 0)
            v = (wrap ? (limit + 1 + v) : 0);
        if (v > limit)
            v = (wrap ? v - (limit + 1) : limit);
        *c = v;
    }

    GUIPoint GetAnchor();
    GUIPoint GetTitlePosition();

    // x0: left column of the 8-channel readout (-1 = classic layout)
    void drawNotes(int x0 = -1);

  public: // temp hack for modl windo constructors
    GUIWindow &w_;
    ViewData *viewData_;

  protected:
    ViewMode viewMode_;
    bool isDirty_; // .Do we need to redraw screeen
    ViewType viewType_;
    bool hasFocus_;
    uint8_t prevLeftVU_[8]; // inertia tracking for VU (8 channels)
    uint8_t prevRightVU_[8];

  private:
    unsigned short mask_;
    bool locked_;
    uint32_t notificationTime_;
    uint16_t NOTIFICATION_TIMEOUT;
    std::string displayNotification_;
    int notiDistY_;
    static bool initPrivate_;
    ModalView *modalView_;
    ModalViewCallback modalViewCallback_;

  public:
    static int margin_;
    static int songRowCount_;
    static bool miniLayout_;
    static int altRowNumber_;
};

#endif
