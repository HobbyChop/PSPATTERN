
#ifndef _PHRASE_VIEW_H_
#define _PHRASE_VIEW_H_

#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/View.h"
#include "ViewData.h"

#define FCC_EDIT MAKE_FOURCC('V', 'O', 'L', 'M')

class PhraseView : public View {

  public:
    PhraseView(GUIWindow &w, ViewData *viewData);
    ~PhraseView();
    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual bool OnNavTo(ViewType to);
    /* what a nav jump to the table screen would land on for the given
       phrase at this view's cursor row: TBL command first, then the
       governing instrument's table; -1 = nothing to follow. Pure --
       the nav map asks it to grey unreachable tiles. */
    int ResolveNavTable(int phraseNum);

    /* The fading trail behind the playhead. See the definition. */
    void drawTriggerTrail();
    virtual int  UndoSize() ;
    virtual int  UndoContext() ;
    virtual void UndoCapture(unsigned char *dst) ;
    virtual void UndoRestore(int context,const unsigned char *src) ;
    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
    virtual void OnFocus();
    void onCommandSelectorResult(ModalView &d);
    void onCommandSelectorPreview(ModalView &d);

  protected:
    void updateCursor(int dx, int dy);
    void stopAudition();
    void updateCursorValue(ViewUpdateDirection offset, int xOffset = 0,
                           int yOffset = 0);
    bool isCommandColumn() const;
    FourCC *getCurrentCommandPointer();
    void updateSelectionValue(ViewUpdateDirection direction);
    void warpToNeighbour(int offset);
    void warpInChain(int offset);
    void cutPosition();
    void pasteLast();

    void extendSelection();

    GUIRect getSelectionRect();
    void fillClipboardData();
    void interpolateSelection();
    void copySelection();
    void pairSelection();   // a command with its parameter, a note with its instrument and velocity
    void cutSelection();
    void pasteClipboard();

    void unMuteAll();
    void toggleMute();
    void switchSoloMode();

    void processNormalButtonMask(unsigned short mask);
    void processSelectionButtonMask(unsigned short mask);

    void setTextProps(GUITextProperties &props, int row, int col, bool restore);

  private:
    int row_;
    int col_;
    int lastNote_;
    // The phrase grid is three columns wider than it was before
    // velocity, and it shares its anchor with the song grid, which is
    // narrower. Left unshifted the last parameter runs off the right
    // edge of a 40-column screen. Everything positional in this view
    // goes through here, so the frame, the row numbers, the play
    // cursor and the columns cannot drift apart.
    GUIPoint gridAnchor() {
        GUIPoint a = GetAnchor();
        a._x -= 3;
        return a;
    }

    int lastInstr_;
    bool tapFilled_;      // the last O tap filled an empty cell
    // remembered so an empty velocity step fills with the last one
    // you actually typed, the way the note and instrument columns do
    uchar lastVelocity_ = VELOCITY_FULL;
    int lastCmd_;
    int lastParam_;
    bool commandSelectorModalActive_;
    Phrase *phrase_;
    int lastPlayingPos_;
    Variable cmdEdit_;
    UIBigHexVarField *cmdEditField_;
    void printHelpLegend(FourCC command, GUITextProperties props);
    void enterCommandSelector();
    int findClosestInstrumentFor(int);
    struct clipboard {
        bool active_;
        int col_;
        int row_;
        int width_;
        int height_;
        uchar note_[16];
        uchar instr_[16];
        uchar velocity_[16];
        uint cmd1_[16];
        ushort param1_[16];
        uint cmd2_[16];
        ushort param2_[16];
    } clipboard_;

    int saveCol_;
    int saveRow_;

    static short offsets_[2][4];
};

#endif
