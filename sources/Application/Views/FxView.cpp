#include "FxView.h"
#include "Application/AppWindow.h"
#include "Application/Model/Mixer.h"
#include "Application/Version.h"
#include "BaseClasses/UISliderField.h"
#include "BaseClasses/UIPillField.h"
#include "BaseClasses/UIStepperField.h"
#include "Foundation/Variables/Variable.h"

// One slider row bound to a 0..hi Mixer param.
#define SLIDER(V,INIT,HI,COL,ROW,LABEL) \
	{ pos=GUIPoint(COL,ROW) ; V=new Variable(#V,(FourCC)0,INIT,HI) ; \
	  UISliderField *f=new UISliderField(pos,*V,LABEL,0,HI,1,16,7) ; \
	  T_SimpleList<UIField>::Insert(f) ; }

FxView::FxView(GUIWindow &w,ViewData *data):FieldView(w,data) {
	Mixer *mx=Mixer::GetInstance() ;
	for (int i=0;i<SendFx::DIV_COUNT;i++) divNames_[i]=SendFx::DivisionName(i) ;

	GUIPoint pos ;

	// ---- REVERB + its behaviours (left column) ----
	SLIDER(vSize_, mx->GetReverbSize(),  255, 2,6,  "size  ")
	SLIDER(vDamp_, mx->GetReverbDamp(),  255, 2,7,  "damp  ")
	pos=GUIPoint(2,8) ;
	vFreeze_=new Variable("freeze",(FourCC)0,mx->GetReverbFreeze(),1) ;
	{ UIPillField *f=new UIPillField(pos,*vFreeze_,"freeze ",2) ;
	  T_SimpleList<UIField>::Insert(f) ; }
	SLIDER(vDuck_, mx->GetReverbDuck(),  255, 2,9,  "duck  ")
	SLIDER(vGate_, mx->GetReverbGate(),  255, 2,10, "gate  ")
	SLIDER(vLocut_,mx->GetReverbLowcut(),255, 2,11, "locut ")
	SLIDER(vWidth_,mx->GetReverbWidth(), 255, 2,12, "width ")

	// ---- DELAY (left column) ----
	pos=GUIPoint(2,14) ;
	vTime_=new Variable("time",(FourCC)0,divNames_,SendFx::DIV_COUNT,
	                    mx->GetDelayDivision()) ;
	{ UIStepperField *f=new UIStepperField(pos,*vTime_,"time ","%s",0,
	                                       SendFx::DIV_COUNT-1) ;
	  T_SimpleList<UIField>::Insert(f) ; }
	SLIDER(vFdbk_, mx->GetDelayFeedback(),250, 2,15, "fdbk  ")
	SLIDER(vDtone_,mx->GetDelayTone(),   255, 2,16, "tone  ")

	// ---- character (right column) ----
	SLIDER(vDrive_, mx->GetDrive(),      255, 21,6,  "amt  ")
	SLIDER(vComp_,  mx->GetComp(),       255, 21,10, "amt  ")

	// ---- per-channel inserts (right column): pick a channel, set its
	//      phaser + chorus. The selector reloads the fields on change.
	curCh_ = 0 ;
	pos=GUIPoint(21,14) ;
	vChan_=new Variable("chan",(FourCC)0,0,SONG_CHANNEL_COUNT-1) ;
	{ UIStepperField *f=new UIStepperField(pos,*vChan_,"chan ","%d",0,
	                                       SONG_CHANNEL_COUNT-1,1) ; // shows 1..8
	  T_SimpleList<UIField>::Insert(f) ; }
	SLIDER(vPhR_, mx->GetChannelPhaserRate(0),  255, 21,15, "ph.rt")
	SLIDER(vPhD_, mx->GetChannelPhaserDepth(0), 255, 21,16, "ph.dp")
	SLIDER(vChR_, mx->GetChannelChorusRate(0),  255, 21,17, "ch.rt")
	SLIDER(vChD_, mx->GetChannelChorusDepth(0), 255, 21,18, "ch.dp")
}

FxView::~FxView() {
	delete vSize_ ; delete vDamp_ ; delete vFreeze_ ; delete vDuck_ ; delete vGate_ ;
	delete vLocut_ ; delete vWidth_ ; delete vDtone_ ;
	delete vTime_ ; delete vFdbk_ ;
	delete vDrive_ ; delete vComp_ ;
	delete vChan_ ;
	delete vPhD_ ; delete vPhR_ ; delete vChD_ ; delete vChR_ ;
}

void FxView::loadInserts() {
	Mixer *mx=Mixer::GetInstance() ;
	vPhR_->SetInt(mx->GetChannelPhaserRate(curCh_)) ;
	vPhD_->SetInt(mx->GetChannelPhaserDepth(curCh_)) ;
	vChR_->SetInt(mx->GetChannelChorusRate(curCh_)) ;
	vChD_->SetInt(mx->GetChannelChorusDepth(curCh_)) ;
}

// Push every control into the Mixer model. The model is fanned out to
// the audio engine each tick, so this makes edits audible immediately.
void FxView::syncToModel() {
	Mixer *mx=Mixer::GetInstance() ;
	mx->SetReverbSize(vSize_->GetInt()) ;
	mx->SetReverbDamp(vDamp_->GetInt()) ;
	mx->SetReverbFreeze(vFreeze_->GetInt()) ;
	mx->SetReverbDuck(vDuck_->GetInt()) ;
	mx->SetReverbGate(vGate_->GetInt()) ;
	mx->SetReverbLowcut(vLocut_->GetInt()) ;
	mx->SetReverbWidth(vWidth_->GetInt()) ;
	mx->SetDelayTone(vDtone_->GetInt()) ;
	mx->SetDelayDivision(vTime_->GetInt()) ;
	mx->SetDelayFeedback(vFdbk_->GetInt()) ;
	mx->SetDrive(vDrive_->GetInt()) ;
	mx->SetComp(vComp_->GetInt()) ;
	// the inserts belong to the selected channel
	int ch=vChan_->GetInt() ;
	mx->SetChannelPhaserRate(ch,vPhR_->GetInt()) ;
	mx->SetChannelPhaserDepth(ch,vPhD_->GetInt()) ;
	mx->SetChannelChorusRate(ch,vChR_->GetInt()) ;
	mx->SetChannelChorusDepth(ch,vChD_->GetInt()) ;
}

void FxView::syncFromModel() {
	Mixer *mx=Mixer::GetInstance() ;
	vSize_->SetInt(mx->GetReverbSize()) ;
	vDamp_->SetInt(mx->GetReverbDamp()) ;
	vFreeze_->SetInt(mx->GetReverbFreeze()) ;
	vDuck_->SetInt(mx->GetReverbDuck()) ;
	vGate_->SetInt(mx->GetReverbGate()) ;
	vLocut_->SetInt(mx->GetReverbLowcut()) ;
	vWidth_->SetInt(mx->GetReverbWidth()) ;
	vDtone_->SetInt(mx->GetDelayTone()) ;
	vTime_->SetInt(mx->GetDelayDivision()) ;
	vFdbk_->SetInt(mx->GetDelayFeedback()) ;
	vDrive_->SetInt(mx->GetDrive()) ;
	vComp_->SetInt(mx->GetComp()) ;
	vChan_->SetInt(curCh_) ;
	loadInserts() ;
}

void FxView::OnFocus() {
	syncFromModel() ;
}

void FxView::switchTo(ViewType vt) {
	ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
	SetChanged() ;
	NotifyObservers(&ve) ;
}

void FxView::ProcessButtonMask(unsigned short mask,bool pressed) {
	if (!pressed)
		return ;
	FieldView::ProcessButtonMask(mask) ;
	if (vChan_->GetInt() != curCh_) {
		curCh_ = vChan_->GetInt() ;
		loadInserts() ;                  // show the newly-selected channel
	} else {
		syncToModel() ;                  // live: heard on the next tick
	}
	if (mask & EPBM_R) {
		if (mask&EPBM_RIGHT) switchTo(VT_MIXER) ;
		if (mask&EPBM_UP)    switchTo(VT_SONG) ;
	}
}

void FxView::DrawView() {
	Clear() ;
	View::EnableNotification() ;
	DrawTitleStrip("FX",PSPATTERN_VERSION_STRING) ;

	// a '*' in the panel title marks an effect that is engaged, so
	// what's on reads at a glance without checking every value
	Mixer *mx=Mixer::GetInstance() ;
	bool revFx=mx->GetReverbFreeze()||mx->GetReverbDuck()||mx->GetReverbGate()
	          ||mx->GetReverbLowcut()||mx->GetReverbWidth() ;
	DrawPanel(1,5,18,7,  revFx?"REVERB *":"REVERB") ;
	DrawPanel(1,13,18,4, mx->GetDelayTone()?"DELAY *":"DELAY") ;
	DrawPanel(21,5,17,3,  mx->GetDrive()?"DRIVE *":"DRIVE") ;
	DrawPanel(21,9,17,3,  mx->GetComp()?"COMP *":"COMP") ;
	bool ins=mx->GetChannelPhaserDepth(curCh_)||mx->GetChannelChorusDepth(curCh_) ;
	DrawPanel(21,13,17,6, ins?"INSERTS *":"INSERTS") ;

	FieldView::Redraw() ;
	DrawHintBar("O change  R> mixer  R^ song") ;
}
