#ifndef _FX_VIEW_H_
#define _FX_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "ViewData.h"
#include "Services/Audio/SendFx.h"

class Variable ;

// The send-bus effect rack: the shared delay + reverb and the fold-in
// effects that run on the Media Engine's wet tail (freeze, duck, gate,
// drive, comp, phaser, chorus). Grouped by mental model in signal order.
// Each field edits a typed UI Variable that is pushed straight into the
// Mixer model on every change, so tweaks are heard live (the model is
// fanned out to the audio engine every tick).
class FxView: public FieldView {
public:
	FxView(GUIWindow &w,ViewData *data) ;
	virtual ~FxView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() ;

private:
	void syncToModel() ;      // UI -> Mixer model (live, on each edit)
	void syncFromModel() ;    // Mixer model -> UI (on focus / project load)
	void switchTo(ViewType vt) ;

	// reverb + its behaviours
	Variable *vSize_,*vDamp_,*vFreeze_,*vDuck_,*vGate_ ;
	Variable *vLocut_,*vWidth_,*vDtone_ ;
	// delay
	Variable *vTime_,*vFdbk_ ;
	// character
	Variable *vDrive_,*vComp_ ;
	// per-channel inserts: a channel selector + the selected channel's
	// phaser and chorus (rate+depth). Editing the selector reloads the
	// insert fields from that channel.
	Variable *vChan_ ;
	Variable *vPhR_,*vPhD_,*vChR_,*vChD_ ;
	int curCh_ ;
	void loadInserts() ;      // Mixer[curCh_] -> the four insert fields

	const char *divNames_[SendFx::DIV_COUNT] ;
} ;

#endif
