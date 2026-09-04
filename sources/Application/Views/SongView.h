
#ifndef _SONG_VIEW_H_
#define _SONG_VIEW_H_

#include "BaseClasses/View.h"

class SongView;

class SongView : public View {
  public:
    // the delete dialog's callback is a free function and lands here
    void doDeleteRow();
    SongView(GUIWindow &w, ViewData *viewData, const char *song);
    ~SongView();

    // View implementation
    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual bool OnNavTo(ViewType to);
    virtual int  UndoSize() ;
    virtual int  UndoContext() ;
    virtual void UndoCapture(unsigned char *dst) ;
    virtual void UndoRestore(int context,const unsigned char *src) ;
    virtual void DrawView();
    virtual void AnimationUpdate();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
    virtual void OnFocus();

  protected:
    void processNormalButtonMask(unsigned int mask);
    void processSelectionButtonMask(unsigned int mask);

    void extendSelection();
    void updateChain(int offset);
    void updateSongOffset(int offset);

    /* Rows you have told the tracker you want to come back to.
       SELECT+O marks the row under the cursor, SELECT+X clears it,
       SELECT+up/down goes to the previous or next one. */
    void toggleBookmark(bool on);
    void jumpToBookmark(int direction);
    void updateCursor(int dx, int dy);
    void setChain(unsigned char);
    void cutPosition();
    void clonePosition();
    void deepClonePosition();
    void pasteLast();
    void fillClipboardData();
    GUIRect getSelectionRect();
    void copySelection();
    void pasteClipboard();
    void cutSelection();
    // the song's rows as a list, on TRIANGLE: insert a blank row above
    // or below (all eight columns move together), or delete one after
    // a yes. See insertRow.
    void insertRow(bool below);
    void deleteRow();
    void gotoRow(int row);
    int pendingDeleteRow_;

    void unMuteAll();
    void toggleMute();
    void switchSoloMode();

    void onStart();
    void startCurrentRow();
    void startImmediate();
    void onStop();

    void jumpToNextSection(int dir);

  private:
    bool updatingChain_; // .Flag that tells we're updating chain
                         //  so we don't allocate chains while
                         //  doing multiple A+ARROWS

    int updateX_; // . Position where update is happening
    int updateY_; //

    unsigned char lastChain_; // .Last chain clipboard

    int lastPlayedPosition_[8]; // .Last position played for song
                                //  used for drawing purpose

    int lastQueuedPosition_[8]; // .Last live queued position for song
                                //  used for drawing purpose

    struct {                  // .Clipboard structure
        bool active_;         // .If currently making a selection
        unsigned char *data_; // .Null if clipboard empty
        int x_;               // .Current selection positions
        int y_;               // .
        int offset_;          // .
        int width_;           // .Size of selection
        int height_;          // .
    } clipboard_;

    int saveX_;
    int saveY_;
    int saveOffset_;
    std::string songname_;
    bool invertBatt_;
    // latched by hysteresis so a reading on the threshold cannot
    // flicker the warning on and off
    bool battWarn_;
    bool needClear_;
    bool canDeepClone_;
    void nudgeTempo(int direction);
    void DrawVuBars();   // Draw VU bars - called from both DrawView and
                         // AnimationUpdate
    void DrawSidePanel(); // scope + dsp load, right of the VU bars
    uint8_t jumpLength_; // When jumping columns with B
    int vuBarHeightsL_[1]; // Master channel stereo VU meter
    int vuBarHeightsR_[1];
};

#endif
