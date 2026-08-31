#ifndef _MIXER_VIEW_H_
#define _MIXER_VIEW_H_

#include "BaseClasses/View.h"
#include "ViewData.h"

class MixerView: public View {
public:
	MixerView(GUIWindow &w,ViewData *viewData) ;
	~MixerView() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual bool OnNavTo(ViewType to) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType ,unsigned int tick=0) ;
    virtual void AnimationUpdate();
    virtual void OnFocus() ;
    // Where R+up goes back to. Set by AppWindow as it switches in,
    // because the mixer is reachable from song, chain and table and
    // it should return you to whichever one you left.
    void SetPreviousViewType(ViewType vt) { previousViewType_ = vt ; } ;
protected:
	void processNormalButtonMask(unsigned int mask) ;
    void processSelectionButtonMask(unsigned int mask) ;
	void onStart() ;
	void onStop() ;
	void updateCursor(int dx,int dy)  ;
    void toggleMute();
    void toggleSolo();
    void unMuteAll();

private:
	const char *song_ ;
    int soloChannel_;
    int mixerRow_; // 0=bus, 1=volume, 2=hpf, 3=lpf

    struct {                      // .Clipboard structure
        bool active_ ;            // .If currently making a selection
        unsigned char *data_ ;    // .Null if clipboard empty
        int x_ ;                  // .Current selection positions
        int y_ ;                  // .
        int offset_ ;             // .
        int width_ ;              // .Size of selection
        int height_ ;             // .
    } clipboard_;

    int saveX_;
    int saveY_ ;
	int saveOffset_ ;
	bool invertBatt_ ;
    ViewType previousViewType_; // Track which view we came from for easy
                                // navigation back
    void DrawVuBars();          // Draw VU bars - called from both DrawView and
                                // AnimationUpdate
    // How much longer each channel's clip light stays lit, in ms.
    // A clip is latched by the audio side; this is what holds it on
    // screen long enough to read.
    int clipHold_[9];
    // how much longer the "large share of the mix" light stays lit
    int hotHold_[9];
    int vuBarHeightsL_[9];  // Left channel smoothed bar heights with slew rate decay (8 channels + 1 master)
    int vuBarHeightsR_[9];  // Right channel smoothed bar heights with slew rate decay (8 channels + 1 master)
};
#endif
