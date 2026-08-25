#include "InstrumentView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Config.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UISliderField.h"
#include "BaseClasses/UIPillField.h"
#include "BaseClasses/UIStepperField.h"
#include "Foundation/Variables/Variable.h"
#include "ModalDialogs/ImportSampleDialog.h"
#include "ModalDialogs/MessageBox.h"
#include "System/System/System.h"

// The type change is destructive: the old instrument object goes and
// with it the sample, every parameter and the table binding. Nothing
// in the instrument screen is undoable, so ask first.
static void TypeChangeCallback(View &v, ModalView &dialog) {
	((InstrumentView &)v).ConfirmTypeChange(
		dialog.GetReturnCode() == MBL_YES) ;
}

void ImportSampleDialogCallback(View &v, ModalView &dialog) {
    ((InstrumentView &)v).OnFocus();
}

extern char *InstrumentTypeData[] ;

#define IVP_TYPE MAKE_FOURCC('T','Y','P','E')

InstrumentView::InstrumentView(GUIWindow &w,ViewData *data):FieldView(w,data),
	typeVar_("type",IVP_TYPE,(char**)InstrumentTypeData,IT_LAST,0) {

	project_=data->project_ ;
	lastFocusID_=0 ;
	current_=0 ;
	refreshingType_=false ;
	pendingType_=IT_SAMPLE ;
	typeVar_.AddObserver(*this) ;
	onInstrumentChange() ;
}

InstrumentView::~InstrumentView() {
}

InstrumentType InstrumentView::getInstrumentType() {
	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instrument=bank->GetInstrument(i) ;
    return instrument->GetType() ;
} ;

void InstrumentView::onInstrumentChange() {

	ClearFocus() ;

	I_Instrument *old=current_ ;

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	current_=bank->GetInstrument(i) ;

	if (current_!=old) {
		current_->RemoveObserver(*this) ;
	} ;
	T_SimpleList<UIField>::Empty() ;

	InstrumentType it=getInstrumentType() ;

	// the type selector heads every page; editing it swaps the slot
	refreshingType_=true ;
	typeVar_.SetInt(it) ;
	refreshingType_=false ;

	GUIPoint tpos(6,2) ;
	UIPillField *tf=new UIPillField(tpos,typeVar_,"type   ",IT_LAST) ;
	T_SimpleList<UIField>::Insert(tf) ;
	tf->SetFocus() ;

    switch (it) {
		case IT_MIDI:
			fillMidiParameters() ;
			break ;
		case IT_SAMPLE:
			fillSampleParameters() ;
			break ;
		case IT_SYNTH:
			fillSynthParameters() ;
			break ;
	} ;

	SetFocus(T_SimpleList<UIField>::GetFirst()) ;
	IteratorPtr<UIField> it2(T_SimpleList<UIField>::GetIterator()) ;
	for (it2->Begin();!it2->IsDone();it2->Next()) {
        UIIntVarField &field=(UIIntVarField &)it2->CurrentItem() ;
        if (field.GetVariableID()==lastFocusID_) {
            SetFocus(&field) ;
            break ;
        }
    } ;
	if (current_!=old) {
		current_->AddObserver(*this) ;
	}
} ;

void InstrumentView::fillSampleParameters() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	SampleInstrument *instrument=(SampleInstrument *)instr  ;

	// ---- left column: SAMPLE (content rows 6-11) ----
	GUIPoint pos(2,6) ;
	Variable *v=instrument->FindVariable(SIP_SAMPLE) ;
	SamplePool *sp=SamplePool::GetInstance() ;
	UIIntVarField *f1=new UIIntVarField(pos,*v,"file  %s",0,sp->GetNameListSize()-1,1,0x10,0,17) ;
	T_SimpleList<UIField>::Insert(f1) ;

	pos=GUIPoint(2,7) ;
	v=instrument->FindVariable(SIP_VOLUME) ;
	UISliderField *sl=new UISliderField(pos,*v,"volume ",0,255,1,10,7,SD_AUTO,19) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(2,8) ;
	v=instrument->FindVariable(SIP_PAN) ;
	sl=new UISliderField(pos,*v,"pan    ",0,0xFE,1,0x10,7,SD_PAN,19) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(2,9) ;
	v=instrument->FindVariable(SIP_ROOTNOTE) ;
	UINoteVarField *nf=new UINoteVarField(pos,*v,"root   %s",0,0x7F,1,0x0C) ;
	T_SimpleList<UIField>::Insert(nf) ;

	pos=GUIPoint(2,10) ;
	v=instrument->FindVariable(SIP_FINETUNE) ;
	// Fine tune is stored 0..255 with 0x7F meaning "no detune", so the
	// number that matters is the distance from centre, with its sign.
	UIStepperField *st=
	    new UIStepperField(pos,*v,"det    ","%+d",0,255,-0x7F) ;
	T_SimpleList<UIField>::Insert(st) ;

	pos=GUIPoint(2,11) ;
	v=instrument->FindVariable(SIP_INTERPOLATION) ;
	UIPillField *pf=new UIPillField(pos,*v,"intp   ",2) ;
	T_SimpleList<UIField>::Insert(pf) ;

	// ---- left column: LOOP (content rows 14-18) ----
	pos=GUIPoint(2,14) ;
	v=instrument->FindVariable(SIP_LOOPMODE) ;
	st=new UIStepperField(pos,*v,"mode   ","%s",0,SILM_LAST-1) ;
	T_SimpleList<UIField>::Insert(st) ;

	pos=GUIPoint(2,15) ;
	v=instrument->FindVariable(SIP_SLICES) ;
	st=new UIStepperField(pos,*v,"slices ","%d",1,0xFF) ;
	T_SimpleList<UIField>::Insert(st) ;

	pos=GUIPoint(2,16) ;
	v=instrument->FindVariable(SIP_START) ;
	UIBigHexVarField *bh=new UIBigHexVarField(pos,*v,7,"start  %7.7X",0,instrument->GetSampleSize()-1,16) ;
	T_SimpleList<UIField>::Insert(bh) ;

	pos=GUIPoint(2,17) ;
	v=instrument->FindVariable(SIP_LOOPSTART) ;
	bh=new UIBigHexVarField(pos,*v,7,"loop   %7.7X",0,instrument->GetSampleSize()-1,16) ;
	T_SimpleList<UIField>::Insert(bh) ;

	pos=GUIPoint(2,18) ;
	v=instrument->FindVariable(SIP_END) ;
	bh=new UIBigHexVarField(pos,*v,7,"end    %7.7X",0,instrument->GetSampleSize()-1,16) ;
	T_SimpleList<UIField>::Insert(bh) ;

	// ---- right column: DSP (content rows 6-15) ----
	pos=GUIPoint(22,6) ;
	v=instrument->FindVariable(SIP_CRUSHVOL) ;
	sl=new UISliderField(pos,*v,"drive  ",0,0xFF,1,0x10,7) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(22,7) ;
	v=instrument->FindVariable(SIP_CRUSH) ;
	st=new UIStepperField(pos,*v,"crush  ","%d",1,0x10) ;
	T_SimpleList<UIField>::Insert(st) ;

	pos=GUIPoint(22,8) ;
	v=instrument->FindVariable(SIP_DOWNSMPL) ;
	st=new UIStepperField(pos,*v,"down   ","%d",0,8) ;
	T_SimpleList<UIField>::Insert(st) ;

	pos=GUIPoint(22,9) ;
	v=instrument->FindVariable(SIP_FILTCUTOFF) ;
	sl=new UISliderField(pos,*v,"cutoff ",0,0xFF,1,0x10,7) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(22,10) ;
	v=instrument->FindVariable(SIP_FILTRESO) ;
	sl=new UISliderField(pos,*v,"reso   ",0,0xFF,1,0x10,7) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(22,11) ;
	v=instrument->FindVariable(SIP_FILTMIX) ;
	sl=new UISliderField(pos,*v,"type   ",0,0xFF,1,0x10,7) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(22,12) ;
	v=instrument->FindVariable(SIP_FILTMODE) ;
	st=new UIStepperField(pos,*v,"mode   ","%s",0,2) ;
	T_SimpleList<UIField>::Insert(st) ;

	pos=GUIPoint(22,13) ;
	v=instrument->FindVariable(SIP_FBTUNE) ;
	sl=new UISliderField(pos,*v,"fbtune ",0,0xFF,1,0x10,7) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(22,14) ;
	v=instrument->FindVariable(SIP_FBMIX) ;
	sl=new UISliderField(pos,*v,"fbmix  ",0,0xFF,1,0x10,7) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(22,15) ;
	v=instrument->FindVariable(SIP_ATTENUATE) ;
	sl=new UISliderField(pos,*v,"atten  ",1,0xFF,1,0x10,7) ;
	T_SimpleList<UIField>::Insert(sl) ;

	// ---- right column: AMP (content rows 18-19) ----
	// The sampler had no amplitude envelope at all; the only way to
	// shape a sample's level over time was a table of VOLM steps.
	pos=GUIPoint(26,18) ;
	v=instrument->FindVariable(SIP_ATTACK) ;
	f1=new UIIntVarField(pos,*v,"a %3d%%",0,0xFF,1,0x10,0,0,true) ;
	T_SimpleList<UIField>::Insert(f1) ;
	pos=GUIPoint(33,18) ;
	v=instrument->FindVariable(SIP_DECAY) ;
	f1=new UIIntVarField(pos,*v,"d %3d%%",0,0xFF,1,0x10,0,0,true) ;
	T_SimpleList<UIField>::Insert(f1) ;
	pos=GUIPoint(26,19) ;
	v=instrument->FindVariable(SIP_SUSTAIN) ;
	f1=new UIIntVarField(pos,*v,"s %3d%%",0,0xFF,1,0x10,0,0,true) ;
	T_SimpleList<UIField>::Insert(f1) ;
	pos=GUIPoint(33,19) ;
	v=instrument->FindVariable(SIP_RELEASE) ;
	f1=new UIIntVarField(pos,*v,"r %3d%%",0,0xFF,1,0x10,0,0,true) ;
	T_SimpleList<UIField>::Insert(f1) ;

#ifdef FFMPEG_ENABLED
	pos=GUIPoint(22,20) ;
	v = instrument->FindVariable(SIP_PRINTFX);
	f1 = new UIIntVarField(pos, *v, "%s", 0, 3, 1, 2);
	T_SimpleList<UIField>::Insert(f1) ;
#endif

	// ---- table strip (content row 22) ----
	pos=GUIPoint(2,22) ;
	v=instrument->FindVariable(SIP_TABLEAUTO) ;
	pf=new UIPillField(pos,*v,"auto  ",2) ;
	T_SimpleList<UIField>::Insert(pf) ;
	pos=GUIPoint(16,22) ;
	v=instrument->FindVariable(SIP_TABLE) ;
	UIIntVarOffField *of=new UIIntVarOffField(pos,*v,"tbl %2.2X",0x00,0x7F,1,0x10) ;
	T_SimpleList<UIField>::Insert(of) ;

} ;

void InstrumentView::fillMidiParameters() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	MidiInstrument *instrument=(MidiInstrument *)instr  ;

	GUIPoint pos(2,6) ;
	Variable *v=instrument->FindVariable(MIP_CHANNEL) ;
	UIStepperField *st=new UIStepperField(pos,*v,"channel ","%2.2d",0,0x0F) ;
	T_SimpleList<UIField>::Insert(st) ;

	pos=GUIPoint(2,7) ;
	v=instrument->FindVariable(MIP_VOLUME) ;
	UISliderField *sl=new UISliderField(pos,*v,"volume  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(2,8) ;
	v=instrument->FindVariable(MIP_NOTELENGTH) ;
	sl=new UISliderField(pos,*v,"length  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
	T_SimpleList<UIField>::Insert(sl) ;

	// How far MDPB's full swing bends, in semitones. It has to match
	// the pitch bend range the receiving instrument is set to, or the
	// interval written in the phrase is not the interval that sounds.
	pos=GUIPoint(2,9) ;
	v=instrument->FindVariable(MIP_BENDRANGE) ;
	UIIntVarField *bf=new UIIntVarField(pos,*v,"bend    %2d",1,48,1,12) ;
	T_SimpleList<UIField>::Insert(bf) ;

	// ---- right column: CONTROL (content rows 6-9) ----
	// Four assignable controllers sent as the note starts. MCCA-MCCD
	// address these same four slots from a phrase, so without a
	// number set here those commands have nothing to send to.
	static const FourCC ccNum[MIDI_CC_SLOTS]=
		{MIP_CC1NUM,MIP_CC2NUM,MIP_CC3NUM,MIP_CC4NUM} ;
	static const FourCC ccVal[MIDI_CC_SLOTS]=
		{MIP_CC1VAL,MIP_CC2VAL,MIP_CC3VAL,MIP_CC4VAL} ;
	static const char *ccLabel[MIDI_CC_SLOTS]=
		{"cc a %3d","cc b %3d","cc c %3d","cc d %3d"} ;
	for (int slot=0;slot<MIDI_CC_SLOTS;slot++) {
		pos=GUIPoint(22,6+slot) ;
		v=instrument->FindVariable(ccNum[slot]) ;
		UIIntVarOffField *nf=
			new UIIntVarOffField(pos,*v,ccLabel[slot],0,127,1,0x10) ;
		T_SimpleList<UIField>::Insert(nf) ;
		pos=GUIPoint(31,6+slot) ;
		v=instrument->FindVariable(ccVal[slot]) ;
		UIIntVarField *vf=new UIIntVarField(pos,*v,"val %3d",0,127,1,0x10) ;
		T_SimpleList<UIField>::Insert(vf) ;
	}

	// ---- table strip (content row 22) ----
	pos=GUIPoint(2,22) ;
	v=instrument->FindVariable(MIP_TABLEAUTO) ;
	UIPillField *pf=new UIPillField(pos,*v,"auto  ",2) ;
	T_SimpleList<UIField>::Insert(pf) ;
	pos=GUIPoint(16,22) ;
	v=instrument->FindVariable(MIP_TABLE) ;
	UIIntVarOffField *of=new UIIntVarOffField(pos,*v,"tbl %2.2X",0,0x7F,1,0x10) ;
	T_SimpleList<UIField>::Insert(of) ;

} ;

void InstrumentView::fillSynthParameters() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	SynthInstrument *instrument=(SynthInstrument *)instr  ;
	int engine=instrument->FindVariable(SYP_ENGINE)->GetInt() ;
	bool pdx=(engine==SET_PDX) ;
	bool vax=(engine==SET_VAX) ;
	bool fm=(engine==SET_FM) ;

	// engine pills under the type row
	GUIPoint pos(6,3) ;
	Variable *v=instrument->FindVariable(SYP_ENGINE) ;
	UIPillField *pf=new UIPillField(pos,*v,"engine ",SET_LAST) ;
	T_SimpleList<UIField>::Insert(pf) ;

	// ---- left column: OSC (content rows 6-13) ----
	// two-option waves are pills; longer lists step. FM has no
	// oscillator waveform to pick -- every operator is a sine, and
	// the algorithm is what shapes the result.
	if (!fm) {
		pos=GUIPoint(2,6) ;
		v=instrument->FindVariable(vax?SYP_VAXWAVE:(pdx?SYP_PDXWAVE:SYP_WAVE)) ;
		if (vax) {
			pf=new UIPillField(pos,*v,"wave   ",VWT_LAST) ;
			T_SimpleList<UIField>::Insert(pf) ;
		} else {
			UIStepperField *ws=new UIStepperField(pos,*v,"wave  ","%s",0,
			                                      (pdx?PWT_LAST:SWT_LAST)-1) ;
			T_SimpleList<UIField>::Insert(ws) ;
		}
	}

	if (vax) {
		pos=GUIPoint(2,7) ;
		v=instrument->FindVariable(SYP_UNISON) ;
		UIStepperField *st=new UIStepperField(pos,*v,"unison ","%d",1,7) ;
		T_SimpleList<UIField>::Insert(st) ;

		pos=GUIPoint(2,8) ;
		v=instrument->FindVariable(SYP_DETUNE) ;
		UISliderField *sl=new UISliderField(pos,*v,"detune ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;

		pos=GUIPoint(2,9) ;
		v=instrument->FindVariable(SYP_PWM) ;
		sl=new UISliderField(pos,*v,"pwm    ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;

		pos=GUIPoint(2,10) ;
		v=instrument->FindVariable(SYP_SUB) ;
		sl=new UISliderField(pos,*v,"sub    ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;

		pos=GUIPoint(2,11) ;
		v=instrument->FindVariable(SYP_NOISE) ;
		sl=new UISliderField(pos,*v,"noise  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;

		pos=GUIPoint(2,12) ;
		v=instrument->FindVariable(SYP_SYNC) ;
		sl=new UISliderField(pos,*v,"sync   ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;

		pos=GUIPoint(2,13) ;
		v=instrument->FindVariable(SYP_RING) ;
		sl=new UISliderField(pos,*v,"ring   ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;
	}

	// TONE showed one control on a page sized for eight. It has the
	// same sub, noise and pulse width the VAX engine has -- pulse
	// width only on the wave where it means something -- and now an
	// LFO it can actually reach. Oscillator rows sit above the wave
	// graph; filter and LFO go in the right column, where VAX keeps
	// its own.
	if (!fm && !pdx && !vax) {
		int wv=instrument->FindVariable(SYP_WAVE)->GetInt() ;
		int ty=7 ;
		UISliderField *ts ;
		if (wv==SWT_SQUARE) {
			pos=GUIPoint(2,ty++) ;
			v=instrument->FindVariable(SYP_PWM) ;
			ts=new UISliderField(pos,*v,"width  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
			T_SimpleList<UIField>::Insert(ts) ;
		}
		pos=GUIPoint(2,ty++) ;
		v=instrument->FindVariable(SYP_SUB) ;
		ts=new UISliderField(pos,*v,"sub    ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(ts) ;
		pos=GUIPoint(2,ty++) ;
		v=instrument->FindVariable(SYP_NOISE) ;
		ts=new UISliderField(pos,*v,"noise  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(ts) ;
	}

	// ---- left column: AMP (content rows 16-19) ----
	pos=GUIPoint(2,16) ;
	v=instrument->FindVariable(SYP_VOLUME) ;
	UISliderField *sl=new UISliderField(pos,*v,"volume ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
	T_SimpleList<UIField>::Insert(sl) ;

	pos=GUIPoint(11,17) ;
	v=instrument->FindVariable(SYP_ATTACK) ;
	UIIntVarField *f1=new UIIntVarField(pos,*v,"a %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;
	pos=GUIPoint(11,18) ;
	v=instrument->FindVariable(SYP_DECAY) ;
	f1=new UIIntVarField(pos,*v,"d %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;
	pos=GUIPoint(11,19) ;
	v=instrument->FindVariable(SYP_SUSTAIN) ;
	f1=new UIIntVarField(pos,*v,"s %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;
	// release sits beside sustain: the panel has no spare row, and
	// these two are read together anyway
	pos=GUIPoint(16,19) ;
	v=instrument->FindVariable(SYP_RELEASE) ;
	f1=new UIIntVarField(pos,*v,"r %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	if (fm) {
		// ---- left column: FM (content rows 6-10) ----
		pos=GUIPoint(2,6) ;
		v=instrument->FindVariable(SYP_FMALGO) ;
		UIStepperField *as=new UIStepperField(pos,*v,"algo   ","%s",0,
		                                      FM_ALGO_COUNT-1) ;
		T_SimpleList<UIField>::Insert(as) ;
		pos=GUIPoint(2,7) ;
		v=instrument->FindVariable(SYP_FMFB) ;
		sl=new UISliderField(pos,*v,"fbk    ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;
		pos=GUIPoint(2,8) ;
		v=instrument->FindVariable(SYP_LFODEST) ;
		UIStepperField *ds=new UIStepperField(pos,*v,"lfo    ","%s",0,
		                                      SLD_LAST-1) ;
		T_SimpleList<UIField>::Insert(ds) ;
		pos=GUIPoint(2,9) ;
		v=instrument->FindVariable(SYP_LFORATE) ;
		sl=new UISliderField(pos,*v,"rate   ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;
		pos=GUIPoint(2,10) ;
		v=instrument->FindVariable(SYP_LFODEPTH) ;
		sl=new UISliderField(pos,*v,"depth  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(sl) ;

		// ---- right column: OPERATORS (content rows 6-12) ----
		// One column per operator. Ratio prints as the multiplier
		// itself rather than an index; detune reads +/-7 around a
		// stored 7, which is why it carries a display offset.
		static const int opCol[FM_OPS]={27,30,33,36} ;
		static const FourCC rId[FM_OPS]={SYP_FMR1,SYP_FMR2,SYP_FMR3,SYP_FMR4} ;
		static const FourCC lId[FM_OPS]={SYP_FML1,SYP_FML2,SYP_FML3,SYP_FML4} ;
		static const FourCC dId[FM_OPS]={SYP_FMD1,SYP_FMD2,SYP_FMD3,SYP_FMD4} ;
		static const FourCC aId[FM_OPS]={SYP_FMA1,SYP_FMA2,SYP_FMA3,SYP_FMA4} ;
		static const FourCC cId[FM_OPS]={SYP_FMC1,SYP_FMC2,SYP_FMC3,SYP_FMC4} ;
		static const FourCC sId[FM_OPS]={SYP_FMS1,SYP_FMS2,SYP_FMS3,SYP_FMS4} ;
		for (int op=0;op<FM_OPS;op++) {
			int c=opCol[op] ;
			pos=GUIPoint(c,7) ;
			v=instrument->FindVariable(rId[op]) ;
			f1=new UIIntVarField(pos,*v,"%3s",0,31,1,4) ;
			T_SimpleList<UIField>::Insert(f1) ;
			pos=GUIPoint(c+1,8) ;
			v=instrument->FindVariable(lId[op]) ;
			f1=new UIIntVarField(pos,*v,"%2.2X",0,0xFF,1,0x10) ;
			T_SimpleList<UIField>::Insert(f1) ;
			pos=GUIPoint(c+1,9) ;
			v=instrument->FindVariable(dId[op]) ;
			f1=new UIIntVarField(pos,*v,"%+2d",0,14,1,7,-7) ;
			T_SimpleList<UIField>::Insert(f1) ;
			pos=GUIPoint(c+1,10) ;
			v=instrument->FindVariable(aId[op]) ;
			f1=new UIIntVarField(pos,*v,"%2.2X",0,0xFF,1,0x10) ;
			T_SimpleList<UIField>::Insert(f1) ;
			pos=GUIPoint(c+1,11) ;
			v=instrument->FindVariable(cId[op]) ;
			f1=new UIIntVarField(pos,*v,"%2.2X",0,0xFF,1,0x10) ;
			T_SimpleList<UIField>::Insert(f1) ;
			pos=GUIPoint(c+1,12) ;
			v=instrument->FindVariable(sId[op]) ;
			f1=new UIIntVarField(pos,*v,"%2.2X",0,0xFF,1,0x10) ;
			T_SimpleList<UIField>::Insert(f1) ;
		}
	}

	if (!fm && !pdx && !vax) {
		// ---- right column: FILTER (6-8) then LFO (12-14) ----
		pos=GUIPoint(22,6) ;
		v=instrument->FindVariable(SYP_CUTOFF) ;
		UISliderField *fs=new UISliderField(pos,*v,"cutoff ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(fs) ;
		pos=GUIPoint(22,7) ;
		v=instrument->FindVariable(SYP_RESO) ;
		fs=new UISliderField(pos,*v,"reso   ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(fs) ;
		pos=GUIPoint(22,8) ;
		v=instrument->FindVariable(SYP_FLTMODE) ;
		UIStepperField *fms=new UIStepperField(pos,*v,"mode   ","%s",0,
		                                       VFM_LAST-1) ;
		T_SimpleList<UIField>::Insert(fms) ;
		pos=GUIPoint(22,12) ;
		v=instrument->FindVariable(SYP_LFODEST) ;
		fms=new UIStepperField(pos,*v,"lfo    ","%s",0,SLD_LAST-1) ;
		T_SimpleList<UIField>::Insert(fms) ;
		pos=GUIPoint(22,13) ;
		v=instrument->FindVariable(SYP_LFORATE) ;
		fs=new UISliderField(pos,*v,"rate   ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(fs) ;
		pos=GUIPoint(22,14) ;
		v=instrument->FindVariable(SYP_LFODEPTH) ;
		fs=new UISliderField(pos,*v,"depth  ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(fs) ;
	}

	if (pdx||vax) {
		// ---- right column: FILTER / DCW (content rows 6-13) ----
		int ry=6 ;
		if (vax) {
			pos=GUIPoint(22,ry++) ;
			v=instrument->FindVariable(SYP_CUTOFF) ;
			sl=new UISliderField(pos,*v,"cutoff ",0,0xFF,1,0x10,7) ;
			T_SimpleList<UIField>::Insert(sl) ;
			pos=GUIPoint(22,ry++) ;
			v=instrument->FindVariable(SYP_RESO) ;
			sl=new UISliderField(pos,*v,"reso   ",0,0xFF,1,0x10,7) ;
			T_SimpleList<UIField>::Insert(sl) ;
			pos=GUIPoint(22,ry++) ;
			v=instrument->FindVariable(SYP_FLTMODE) ;
			UIStepperField *ms=new UIStepperField(pos,*v,"mode   ","%s",0,
			                                      VFM_LAST-1) ;
			T_SimpleList<UIField>::Insert(ms) ;
		}
		pos=GUIPoint(22,ry) ;
		v=instrument->FindVariable(SYP_DCWAMT) ;
		sl=new UISliderField(pos,*v,vax?"env    ":"amount ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(sl) ;
		ry++ ;
		// mod envelope a/d/s beside its graph
		pos=GUIPoint(30,ry) ;
		v=instrument->FindVariable(SYP_DCWATK) ;
		f1=new UIIntVarField(pos,*v,"a %2.2X",0,0xFF,1,0x10) ;
		T_SimpleList<UIField>::Insert(f1) ;
		pos=GUIPoint(30,ry+1) ;
		v=instrument->FindVariable(SYP_DCWDEC) ;
		f1=new UIIntVarField(pos,*v,"d %2.2X",0,0xFF,1,0x10) ;
		T_SimpleList<UIField>::Insert(f1) ;
		pos=GUIPoint(30,ry+2) ;
		v=instrument->FindVariable(SYP_DCWSUS) ;
		f1=new UIIntVarField(pos,*v,"s %2.2X",0,0xFF,1,0x10) ;
		T_SimpleList<UIField>::Insert(f1) ;
		pos=GUIPoint(35,ry+2) ;
		v=instrument->FindVariable(SYP_DCWREL) ;
		f1=new UIIntVarField(pos,*v,"r %2.2X",0,0xFF,1,0x10) ;
		T_SimpleList<UIField>::Insert(f1) ;
		ry+=3 ;
		if (vax) {
			pos=GUIPoint(22,ry) ;
			v=instrument->FindVariable(SYP_GLIDE) ;
			sl=new UISliderField(pos,*v,"glide  ",0,0xFF,1,0x10,7) ;
			T_SimpleList<UIField>::Insert(sl) ;
		}

		// ---- right column: LFO + DRIVE (content rows 16-19) ----
		pos=GUIPoint(22,16) ;
		v=instrument->FindVariable(SYP_LFODEST) ;
		UIStepperField *ds=new UIStepperField(pos,*v,"lfo    ","%s",0,
		                                      SLD_LAST-1) ;
		T_SimpleList<UIField>::Insert(ds) ;
		pos=GUIPoint(22,17) ;
		v=instrument->FindVariable(SYP_LFORATE) ;
		sl=new UISliderField(pos,*v,"rate   ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(sl) ;
		pos=GUIPoint(22,18) ;
		v=instrument->FindVariable(SYP_LFODEPTH) ;
		sl=new UISliderField(pos,*v,"depth  ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(sl) ;
		if (vax) {
			pos=GUIPoint(22,19) ;
			v=instrument->FindVariable(SYP_DRIVE) ;
			sl=new UISliderField(pos,*v,"drive  ",0,0xFF,1,0x10,7) ;
			T_SimpleList<UIField>::Insert(sl) ;
		}
	}

	// ---- table strip (content row 22) ----
	pos=GUIPoint(2,22) ;
	v=instrument->FindVariable(SYP_TABLEAUTO) ;
	pf=new UIPillField(pos,*v,"auto  ",2) ;
	T_SimpleList<UIField>::Insert(pf) ;
	pos=GUIPoint(16,22) ;
	v=instrument->FindVariable(SYP_TABLE) ;
	UIIntVarOffField *of=new UIIntVarOffField(pos,*v,"tbl %2.2X",0,0x7F,1,0x10) ;
	T_SimpleList<UIField>::Insert(of) ;

} ;

void InstrumentView::applyTypeChange() {
	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	bank->SetType(i,pendingType_) ;
	current_=0 ;   // old pointer is gone; re-observe the new one
	lastFocusID_=IVP_TYPE ;
	onInstrumentChange() ;
	isDirty_=true ;
} ;

void InstrumentView::ConfirmTypeChange(bool go) {
	if (go) {
		applyTypeChange() ;
	} else {
		onInstrumentChange() ;   // redraw the selector where it was
		isDirty_=true ;
	}
} ;

void InstrumentView::warpToNext(int offset) {
	int instrument=viewData_->currentInstrument_+offset ;
	if (instrument>=MAX_INSTRUMENT_COUNT) {
		instrument=instrument-MAX_INSTRUMENT_COUNT ;
	} ;
	if (instrument<0) {
		instrument=MAX_INSTRUMENT_COUNT+instrument ;
	} ;
	viewData_->currentInstrument_=instrument ;
	onInstrumentChange() ;
	isDirty_=true ;
} ;

void InstrumentView::ProcessButtonMask(unsigned short mask,bool pressed) {

	if (!pressed) return ;

	isDirty_=false ;

	if (viewMode_==VM_NEW) {
		if (mask==EPBM_A) {
			UIIntVarField *field=(UIIntVarField *)GetFocus() ;
			Variable &v=field->GetVariable() ;
			switch(v.GetID()) {
				case SIP_SAMPLE:
				 {
                    // First check if the samplelib exists

					 Path sampleLib(SamplePool::GetInstance()->GetSampleLib()) ;
					 if (FileSystem::GetInstance()->GetFileType(sampleLib.GetPath().c_str())!=FT_DIR) {
						 MessageBox *mb=new MessageBox(*this,"Can't access the samplelib",MBBF_OK) ;
						 DoModal(mb) ;
					 } else { ;
						// Go to import sample

						 ImportSampleDialog *isd=new ImportSampleDialog(*this) ;
                         DoModal(isd, ImportSampleDialogCallback);
                     }
                    break ;
				 }
				case SIP_TABLE:
				 {
					int next=TableHolder::GetInstance()->GetNext() ;
					if (next!=NO_MORE_TABLE) {
						v.SetInt(next) ;
						isDirty_=true ;
					}
					break ;
                }
                case SIP_PRINTFX: {
                    FxPrinter printer(viewData_);
                    isDirty_ = printer.Run();
                    View::SetNotification(printer.GetNotification());
                    break;
                }
                default:
                    break ;
			}
			mask&=(0xFFFF-EPBM_A) ;
		}
	}

	if (viewMode_==VM_CLONE) {
        if ((mask&EPBM_A)&&(mask&EPBM_L)) {
			UIIntVarField *field=(UIIntVarField *)GetFocus() ;
			mask&=(0xFFFF-EPBM_A) ;
			// Only the table field holds a table number. Cloning from
			// anywhere else fed a cutoff or a volume straight into the
			// table array as an index.
			FourCC varID=field->GetVariableID() ;
			if ((varID!=SIP_TABLE)&&(varID!=MIP_TABLE)&&(varID!=SYP_TABLE)) return ;
			Variable &v=field->GetVariable() ;
			int current=v.GetInt() ;
			if (current==-1) return ;

			int next=TableHolder::GetInstance()->Clone(current) ;
			if (next!=NO_MORE_TABLE) {
				v.SetInt(next) ;
				isDirty_=true ;
			}
		}
		mask&=(0xFFFF-(EPBM_A|EPBM_L)) ;
	} ;

	if (viewMode_==VM_SELECTION) {
	} else {
		viewMode_=VM_NORMAL ;
	}

	FieldView::ProcessButtonMask(mask) ;

    Player *player=Player::GetInstance() ;
	// B Modifier

    if (mask & EPBM_B) {
        if (mask&EPBM_LEFT) warpToNext(-1) ;
		if (mask&EPBM_RIGHT) warpToNext(+1);
		if (mask&EPBM_DOWN) warpToNext(-16) ;
		if (mask&EPBM_UP) warpToNext(+16);
		if (mask&EPBM_A) { // Allow cut instrument
		   if (getInstrumentType()==IT_SAMPLE) {
                if (GetFocus()==T_SimpleList<UIField>::GetFirst()) {
	               int i=viewData_->currentInstrument_ ;
	               InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	               I_Instrument *instr=bank->GetInstrument(i) ;
					instr->Purge() ;
//                   Variable *v=instr->FindVariable(SIP_SAMPLE) ;
//                   v->SetInt(-1) ;
                   isDirty_=true ;
                }
           }

		   // Check if on table
		   if (GetFocus()==T_SimpleList<UIField>::GetLast()) {
	            int i=viewData_->currentInstrument_ ;
	            InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	            I_Instrument *instr=bank->GetInstrument(i) ;
                // the table variable has a different id per instrument type
                // (the synth uses SYP_TABLE, not SIP_TABLE) -- looking up the
                // wrong one returns null and dereferencing it kills the app
                Variable *v=instr->FindVariable(SIP_TABLE) ;
                if (!v) v=instr->FindVariable(SYP_TABLE) ;
                if (v) {
                    v->SetInt(-1) ;
                    isDirty_=true ;
                }
		   } ;
        }
        if (mask&EPBM_L) {
            viewMode_=VM_CLONE ;
        } ;
    } else {

        // A modifier

        if (mask == EPBM_A) {
            FourCC varID = ((UIIntVarField *)GetFocus())->GetVariableID();
            if ((varID == SIP_TABLE) || (varID == MIP_TABLE) ||
                (varID == SIP_SAMPLE) || (varID == SIP_PRINTFX)) {
                viewMode_ = VM_NEW;
			}
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    ViewType vt = VT_PHRASE;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {

                    // Go to table view

                    ViewType vt = VT_TABLE2;

                    int i = viewData_->currentInstrument_;
                    InstrumentBank *bank =
                        viewData_->project_->GetInstrumentBank();
                    I_Instrument *instr = bank->GetInstrument(i);
                    int table = instr->GetTable();
                    if (table != VAR_OFF) {
                        viewData_->currentTable_ = table;
                    }
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                // if (mask&EPBM_RIGHT) {

                //	// Go to import sample

                //		ViewType vt=VT_IMPORT ;
                //		ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
                //		SetChanged();
                //		NotifyObservers(&ve) ;
                //}

                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
            } else {
                // No modifier
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                                          viewData_->chainRow_);
                }
            }
        }
    }

    UIIntVarField *field = (UIIntVarField *)GetFocus();
    if (field) {
	   lastFocusID_=field->GetVariableID() ;
    }

} ;

void InstrumentView::DrawView() {

	Clear() ;
    View::EnableNotification();

    GUITextProperties props;

    // title strip: screen + the slot identity, live info right
    I_Instrument *instr = viewData_->project_->GetInstrumentBank()->GetInstrument(
        viewData_->currentInstrument_);
    char title[40];
    const char *name = instr->GetName();
    snprintf(title, 32, "INSTRUMENT %2.2X  %s", viewData_->currentInstrument_,
             (name && name[0]) ? name : "empty");
    DrawTitleStrip(title, "");

    switch (getInstrumentType()) {
        case IT_SYNTH:  drawSynthChrome();  break;
        case IT_SAMPLE: drawSampleChrome(); break;
        case IT_MIDI:   drawMidiChrome();   break;
        default: break;
    }

    // Draw fields

    FieldView::Redraw();
    DrawHintBar("O edit  X+</> instr  R+< phr  R+v tbl");
} ;

void InstrumentView::drawSampleChrome() {

	I_Instrument *instrument=
		viewData_->project_->GetInstrumentBank()->GetInstrument(
			viewData_->currentInstrument_) ;
	AppWindow &app=(AppWindow &)w_ ;

	DrawPanel(1,5,19,6,"SAMPLE") ;
	DrawPanel(1,13,19,6,"LOOP") ;
	DrawPanel(21,5,18,10,"DSP") ;
	// DSP fills rows 6-15, so its bottom rule sits on row 16: AMP's
	// label goes below that, not on it
	DrawPanel(21,17,18,2,"AMP") ;
	// 32px, not 40: the envelope readouts grew from "FF" to "100%" and
	// start one cell further left, so the graph has to end before
	// column 27 instead of running into it.
	app.OpAdsr(22*8+2,18*8+2,28,15,
		instrument->FindVariable(SIP_ATTACK)->GetInt(),
		instrument->FindVariable(SIP_DECAY)->GetInt(),
		instrument->FindVariable(SIP_SUSTAIN)->GetInt()) ;
	DrawPanel(1,21,38,2,"TABLE") ;
} ;

void InstrumentView::drawMidiChrome() {

	DrawPanel(1,5,19,4,"MIDI OUT") ;
	DrawPanel(21,5,18,4,"CONTROL") ;
	DrawPanel(1,21,38,2,"TABLE") ;
} ;

void InstrumentView::drawSynthChrome() {

	SynthInstrument *instrument=(SynthInstrument *)
		viewData_->project_->GetInstrumentBank()->GetInstrument(
			viewData_->currentInstrument_) ;
	AppWindow &app=(AppWindow &)w_ ;
	int engine=instrument->FindVariable(SYP_ENGINE)->GetInt() ;
	bool pdx=(engine==SET_PDX) ;
	bool vax=(engine==SET_VAX) ;
	bool fm=(engine==SET_FM) ;

	if (fm) {
		// FM: routing and the LFO on the left, one column per
		// operator on the right. Six rows of four values reads
		// faster than twenty-four labelled rows, and it is the shape
		// every FM panel since the DX7 has settled on.
		DrawPanel(1,5,19,5,"FM") ;
		DrawPanel(21,5,18,7,"OPERATORS") ;
		GUITextProperties hp ;
		SetColor(CD_HILITE1) ;
		// on the value columns (28/31/34/37), not the wider ratio
		// column: three-cell headers at three-cell pitch ran together
		// into one word
		DrawString(28,6,"o1",hp) ;
		DrawString(31,6,"o2",hp) ;
		DrawString(34,6,"o3",hp) ;
		DrawString(37,6,"o4",hp) ;
		SetColor(CD_ROW2) ;
		DrawString(22,7,"rat",hp) ;
		DrawString(22,8,"lvl",hp) ;
		DrawString(22,9,"det",hp) ;
		DrawString(22,10,"atk",hp) ;
		DrawString(22,11,"dec",hp) ;
		DrawString(22,12,"sus",hp) ;
		SetColor(CD_NORMAL) ;
		DrawPanel(1,15,19,4,"AMP") ;
		app.OpAdsr(2*8+2,17*8+1,52,21,
			instrument->FindVariable(SYP_ATTACK)->GetInt(),
			instrument->FindVariable(SYP_DECAY)->GetInt(),
			instrument->FindVariable(SYP_SUSTAIN)->GetInt()) ;
		DrawPanel(1,21,38,2,"TABLE") ;
		return ;
	}

	DrawPanel(1,5,19,8,"OSC") ;
	int wk=0 ;
	if (vax) wk=(instrument->FindVariable(SYP_VAXWAVE)->GetInt()==VWT_PULSE)?1:0 ;
	else if (!pdx) {
		int wv=instrument->FindVariable(SYP_WAVE)->GetInt() ;
		wk=(wv==SWT_SQUARE)?1:((wv==SWT_TRIANGLE)?2:0) ;
	}
	// wave graph: beside the lone wave stepper. VAX has no room for
	// one any more and needs it least -- sync and ring fill the last
	// two rows of the panel, and VAX's wave is a two item pill with
	// the shape written next to it, so the icon was repeating what
	// the label already said. PDX and TONE keep theirs: they have
	// four wave shapes each and the spare rows to show one.
	if (vax) {
		// no graph
	} else if (pdx) {
		app.OpWave(2*8+2,8*8+2,52,14,wk) ;
	} else {
		// TONE's oscillator rows reach row 9 now (10 on a square,
		// which has a width control), so the graph sits under them
		app.OpWave(2*8+2,11*8+2,52,18,wk) ;
	}

	DrawPanel(1,15,19,4,"AMP") ;
	app.OpAdsr(2*8+2,17*8+1,52,21,
		instrument->FindVariable(SYP_ATTACK)->GetInt(),
		instrument->FindVariable(SYP_DECAY)->GetInt(),
		instrument->FindVariable(SYP_SUSTAIN)->GetInt()) ;

	if (!pdx&&!vax) {
		// TONE runs the same SVF; it just never had the controls on
		// screen, so FCUT and FRES looked like they did nothing
		DrawPanel(21,5,18,3,"FILTER") ;
		DrawPanel(21,11,18,3,"LFO") ;
	}
	if (pdx||vax) {
		DrawPanel(21,5,18,8,vax?"FILTER":"DCW") ;
		int gy=vax?10:7 ;
		app.OpAdsr(22*8+2,gy*8+1,44,22,
			instrument->FindVariable(SYP_DCWATK)->GetInt(),
			instrument->FindVariable(SYP_DCWDEC)->GetInt(),
			instrument->FindVariable(SYP_DCWSUS)->GetInt()) ;
		DrawPanel(21,15,18,4,vax?"LFO + DRIVE":"LFO") ;
	}
	DrawPanel(1,21,38,2,"TABLE") ;
} ;

void InstrumentView::OnFocus() { onInstrumentChange(); }

void InstrumentView::Update(Observable &o,I_ObservableData *d) {

	if (&o==(Observable *)&typeVar_) {
		if (refreshingType_) return ;

		int i=viewData_->currentInstrument_ ;
		InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;

		if (Player::GetInstance()->IsRunning()) {
			// a live voice keeps a pointer to the old instrument —
			// refuse the swap while playing
			refreshingType_=true ;
			typeVar_.SetInt(bank->GetInstrument(i)->GetType()) ;
			refreshingType_=false ;
			View::SetNotification("stop playback to change type") ;
			return ;
		}

		pendingType_=(InstrumentType)typeVar_.GetInt() ;
		if (!bank->GetInstrument(i)->IsEmpty()) {
			// put the selector back until the answer comes in, so a
			// cancelled swap does not leave the wrong type on screen
			refreshingType_=true ;
			typeVar_.SetInt(bank->GetInstrument(i)->GetType()) ;
			refreshingType_=false ;
			MessageBox *mb=new MessageBox(*this,
				"replace instrument? settings are lost",
				MBBF_YES|MBBF_NO) ;
			DoModal(mb,TypeChangeCallback) ;
			isDirty_=true ;
			return ;
		}
		applyTypeChange() ;
		return ;
	}

	onInstrumentChange() ;
}
