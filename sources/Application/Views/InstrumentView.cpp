#include "InstrumentView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Config.h"
#include "Application/Mixer/MixerService.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UISliderField.h"
#include "BaseClasses/UIPillField.h"
#include "BaseClasses/UIStepperField.h"
#include "Foundation/Variables/Variable.h"
#include "Application/Instruments/SynthPresets.h"
#include "ModalDialogs/ImportSampleDialog.h"
#include "BaseClasses/UIActionField.h"
#include "Application/Instruments/DrumKit.h"
#include "Application/Instruments/SampleInfo.h"
#include "ModalDialogs/NewProjectDialog.h"
#include "ModalDialogs/MessageBox.h"
#include "System/System/System.h"

void ImportSampleDialogCallback(View &v, ModalView &dialog) {
    /* Coming back from the browser, the cursor belongs on the file
       row: the sample just imported is what it names, and stepping to
       a neighbour or auditioning it is the next thing anybody does.
       OnFocus rebuilds the fields and restores focus by id, so naming
       the id first is all it takes. */
    ((InstrumentView &)v).FocusSampleRow();
    ((InstrumentView &)v).OnFocus();
}

static void SavePresetCallback(View &v, ModalView &dialog) {
	NewProjectDialog &npd=(NewProjectDialog &)dialog ;
	if (dialog.GetReturnCode()>0) {
		((InstrumentView &)v).OnSavePreset(npd.GetName().c_str()) ;
	}
}

extern char *InstrumentTypeData[] ;

#define IVP_TYPE MAKE_FOURCC('T','Y','P','E')
#define IVP_SLOT MAKE_FOURCC('S','L','O','T')
#define IVP_PRESET MAKE_FOURCC('P','R','S','T')
#define IVP_IMPORT MAKE_FOURCC('I','M','P','T')

InstrumentView::InstrumentView(GUIWindow &w,ViewData *data):FieldView(w,data),
	typeVar_("type",IVP_TYPE,(char**)InstrumentTypeData,IT_LAST,0),
	slotVar_("slot",IVP_SLOT,0) {

	project_=data->project_ ;
	lastFocusID_=0 ;
	current_=0 ;
	refreshingType_=false ;
	pendingType_=IT_SAMPLE ;
	rebuildPending_=false ;
	applyTypePending_=false ;
	refreshingSlot_=false ;
	auditionLatch_=false ;
	auditionNote_=60 ;
	presetVar_=0 ;
	presetShown_=0 ;
	presetPending_=false ;
	pendingPreset_=0 ;
	presetUndoValid_=false ;
	presetRestorePending_=false ;
	presetApplying_=false ;
	presetSavePending_=false ;
	importPending_=false ;
	lastLadderID_=IVP_TYPE ;
	presetSaveName_[0]=0 ;
	SynthPresets::Scan() ;
	typeVar_.AddObserver(*this) ;
	slotVar_.AddObserver(*this) ;
	onInstrumentChange() ;
}

InstrumentView::~InstrumentView() {
	// unhook, or the instrument keeps a dangling observer pointer and
	// its next notify is a wild vtable call
	if (current_) current_->RemoveObserver(*this) ;
	if (presetVar_) delete presetVar_ ;
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
		// unhook from the instrument being LEFT -- this used to remove
		// from the new one instead, so every visited instrument kept
		// notifying this view forever
		if (old) old->RemoveObserver(*this) ;
		// defensive: drop any stale registration on the incoming one
		// before the re-add below
		current_->RemoveObserver(*this) ;
		// a different object under the deck (slot walk or type flip):
		// the preset label described the OLD sound, drop it to "--"
		// and the snapshot with it
		presetShown_=0 ;
		presetUndoValid_=false ;
		presetUndo_.clear() ;
		presetRestorePending_=false ;
	} ;
	T_SimpleList<UIField>::Empty() ;

	InstrumentType it=getInstrumentType() ;

	// the type selector heads every page; editing it swaps the slot
	refreshingType_=true ;
	typeVar_.SetInt(it) ;
	refreshingType_=false ;
	refreshingSlot_=true ;
	slotVar_.SetInt(i) ;
	refreshingSlot_=false ;

	// no slot FIELD: the title strip carries the number, and L+arrows
	// are deliberately the ONLY way to change which instrument -- a
	// focusable slot row made it editable through every value gesture
	GUIPoint tpos(2,2) ;
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

	SamplePool *sp=SamplePool::GetInstance() ;
	Variable *v=instrument->FindVariable(SIP_SAMPLE) ;
	int listEnd=sp->GetNameListSize() ;

	/* One list. It held a KIT/WAV split for a day and the split fought
	   the pool, the rebuilds and the player of the machine; the kit
	   simply lives at the top of the list and the imports after it,
	   which is what the sorting already does. Import stays in plain
	   sight below the type row. */
	GUIPoint apos(2,3) ;
	UIActionField *af=new UIActionField("import sample >",IVP_IMPORT,apos) ;
	T_SimpleList<UIField>::Insert(af) ;

	// ---- left column: SAMPLE (content rows 6-11) ----
	GUIPoint pos(2,6) ;
	UIIntVarField *f1=new UIIntVarField(pos,*v,"file  %s",0,
	                    (listEnd>0)?listEnd-1:0,1,0x10,0,17) ;
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
	bool vox=(engine==SET_VOX) ;
	bool hive=(engine==SET_HIVE) ;

	// engine pills under the type row
	GUIPoint pos(6,3) ;
	Variable *v=instrument->FindVariable(SYP_ENGINE) ;
	// FM is the CPU-hungriest engine; FM_ENGINE=NO hides it from the
	// picker for anyone who wants the guard rail back. Selectable by
	// default -- the dsp meter is on every screen now, so the cost is
	// visible instead of forbidden. Saved FM instruments always play.
	const char *fmCfg=Config::GetInstance()->GetValue("FM_ENGINE") ;
	bool fmEnabled=!(fmCfg && (fmCfg[0]=='N'||fmCfg[0]=='n')) ;
	UIPillField *pf=new UIPillField(pos,*v,"engine ",SET_LAST,
	                                fmEnabled?-1:SET_FM) ;
	T_SimpleList<UIField>::Insert(pf) ;

	// the preset row: third rung of the identity ladder. Stepping it
	// LOADS (deferred -- see DrawView), O-tap on it SAVES under a new
	// name. Entry 0 is "--", the unloaded state.
	presetList_.clear() ;
	presetList_.push_back("--") ;
	for (int pi=0;pi<SynthPresets::Count();pi++)
		presetList_.push_back(SynthPresets::Name(pi)) ;
	if (presetShown_>=(int)presetList_.size()) presetShown_=0 ;
	if (presetVar_) delete presetVar_ ;
	presetVar_=new Variable("preset",IVP_PRESET,
	                        (char**)&presetList_[0],
	                        (int)presetList_.size(),presetShown_) ;
	pos=GUIPoint(6,4) ;
	UIStepperField *prf=new UIStepperField(pos,*presetVar_,"preset ","%s",
	                                       0,(int)presetList_.size()-1) ;
	T_SimpleList<UIField>::Insert(prf) ;

	// ---- left column: OSC (content rows 6-13) ----
	// two-option waves are pills; longer lists step. FM has no
	// oscillator waveform to pick -- every operator is a sine, and
	// the algorithm is what shapes the result.
	if (!fm && !vox) {
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

	if (hive) {
		// ---- left column: the swarm (content rows 7-11) ----
		// voices first, because it is the cost knob: every voice is
		// one oscillator, about 2.5% of a block per channel
		pos=GUIPoint(2,7) ;
		v=instrument->FindVariable(SYP_HVVOICES) ;
		UIStepperField *hs=new UIStepperField(pos,*v,"voices ","%d",1,HIVE_MAX_VOICES) ;
		T_SimpleList<UIField>::Insert(hs) ;
		pos=GUIPoint(2,8) ;
		v=instrument->FindVariable(SYP_HVCHORD) ;
		hs=new UIStepperField(pos,*v,"chord  ","%s",0,HIVE_CHORD_COUNT-1) ;
		T_SimpleList<UIField>::Insert(hs) ;
		pos=GUIPoint(2,9) ;
		v=instrument->FindVariable(SYP_HVSPREAD) ;
		UISliderField *hl=new UISliderField(pos,*v,"spread ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(hl) ;
		pos=GUIPoint(2,10) ;
		v=instrument->FindVariable(SYP_HVWIDTH) ;
		hl=new UISliderField(pos,*v,"width  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(hl) ;
		pos=GUIPoint(2,11) ;
		v=instrument->FindVariable(SYP_GLIDE) ;
		hl=new UISliderField(pos,*v,"glide  ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(hl) ;
	}

	// TONE showed one control on a page sized for eight. It has the
	// same sub, noise and pulse width the VAX engine has -- pulse
	// width only on the wave where it means something -- and now an
	// LFO it can actually reach. Oscillator rows sit above the wave
	// graph; filter and LFO go in the right column, where VAX keeps
	// its own.
	if (!fm && !pdx && !vax && !vox && !hive) {
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

	if (!fm && !pdx && !vax && !vox && !hive) {
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

	if (vox) {
		// VOX (formant / vocal). The reused Variables wear vocal labels
		// here instead of the tone-page ones: wave = source, cutoff =
		// vowel, reso = formant sharpness, pwm = glottal, noise = breath.
		UIStepperField *vst ;
		UISliderField  *vfs ;
		pos=GUIPoint(2,6) ;
		v=instrument->FindVariable(SYP_WAVE) ;
		vst=new UIStepperField(pos,*v,"source ","%s",0,SWT_LAST-1) ;
		T_SimpleList<UIField>::Insert(vst) ;
		pos=GUIPoint(2,7) ;
		v=instrument->FindVariable(SYP_PWM) ;
		vfs=new UISliderField(pos,*v,"glottal",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(vfs) ;
		pos=GUIPoint(2,8) ;
		v=instrument->FindVariable(SYP_NOISE) ;
		vfs=new UISliderField(pos,*v,"breath ",0,0xFF,1,0x10,7,SD_AUTO,19) ;
		T_SimpleList<UIField>::Insert(vfs) ;

		pos=GUIPoint(22,6) ;
		v=instrument->FindVariable(SYP_CUTOFF) ;
		vfs=new UISliderField(pos,*v,"vowel  ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(vfs) ;
		pos=GUIPoint(22,7) ;
		v=instrument->FindVariable(SYP_RESO) ;
		vfs=new UISliderField(pos,*v,"formant",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(vfs) ;
		pos=GUIPoint(22,8) ;
		v=instrument->FindVariable(SYP_VOXVA) ;
		vst=new UIStepperField(pos,*v,"vow a  ","%s",0,4) ;   // 5 vowels
		T_SimpleList<UIField>::Insert(vst) ;
		pos=GUIPoint(22,9) ;
		v=instrument->FindVariable(SYP_VOXVB) ;
		vst=new UIStepperField(pos,*v,"vow b  ","%s",0,4) ;
		T_SimpleList<UIField>::Insert(vst) ;
		pos=GUIPoint(22,11) ;
		v=instrument->FindVariable(SYP_LFODEST) ;
		vst=new UIStepperField(pos,*v,"lfo    ","%s",0,SLD_LAST-1) ;
		T_SimpleList<UIField>::Insert(vst) ;
		pos=GUIPoint(22,12) ;
		v=instrument->FindVariable(SYP_LFORATE) ;
		vfs=new UISliderField(pos,*v,"rate   ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(vfs) ;
		pos=GUIPoint(22,13) ;
		v=instrument->FindVariable(SYP_LFODEPTH) ;
		vfs=new UISliderField(pos,*v,"depth  ",0,0xFF,1,0x10,7) ;
		T_SimpleList<UIField>::Insert(vfs) ;
	}

	if (pdx||vax||hive) {
		// ---- right column: FILTER / DCW (content rows 6-13) ----
		int ry=6 ;
		if (vax||hive) {
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
		sl=new UISliderField(pos,*v,(vax||hive)?"env    ":"amount ",0,0xFF,1,0x10,7) ;
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
		if (vax||hive) {
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
	/* Serialize with the audio thread and hard-release any voice still
	   rendering the object about to be deleted (a ringing audition
	   note, or its release tail) -- the deferred path runs from
	   DrawView, which is OUTSIDE the input path's mixer lock. */
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	Player::GetInstance()->CutInstrument(bank->GetInstrument(i)) ;
	bank->SetType(i,pendingType_) ;
	ms->Unlock() ;
	current_=0 ;   // old pointer is gone; re-observe the new one
	lastFocusID_=IVP_TYPE ;
	onInstrumentChange() ;
	isDirty_=true ;
} ;

// the ladder rows the d-pad owns; the nub may not focus these.
// TYPE and ENGINE only -- the wave row sits among the parameters and
// belongs to the stick like any other variable.
static bool ivIsSelectorId(FourCC id) {
	return id==IVP_TYPE||id==SYP_ENGINE||id==IVP_PRESET||id==IVP_IMPORT ;
}

void InstrumentView::auditionStart() {
	if (Player::GetInstance()->IsRunning()) {
		View::SetNotification("stop playback to audition") ;
		return ;
	}
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(viewData_->currentInstrument_) ;
	int note=60 ;
	if (instr&&instr->GetType()==IT_SAMPLE) {
		Variable *rv=instr->FindVariable(SIP_ROOTNOTE) ;
		if (rv&&rv->GetInt()>=0&&rv->GetInt()<128) note=rv->GetInt() ;
	}
	auditionNote_=(unsigned char)note ;
	Player::GetInstance()->MidiNoteOn(auditionNote_,127) ;
	auditionLatch_=true ;
} ;

void InstrumentView::auditionStop() {
	Player::GetInstance()->MidiNoteOff(auditionNote_) ;
	auditionLatch_=false ;
} ;

void InstrumentView::auditionRetrigger() {
	Player::GetInstance()->MidiNoteOff(auditionNote_) ;
	auditionStart() ;
} ;

void InstrumentView::cycleType(int step) {
	if (Player::GetInstance()->IsRunning()) {
		View::SetNotification("stop playback to change type") ;
		return ;
	}
	InstrumentType t=getInstrumentType() ;
	pendingType_=(InstrumentType)((t+step+IT_LAST)%IT_LAST) ;
	applyTypePending_=true ;   // applied at the top of the next DrawView
	isDirty_=true ;
} ;

void InstrumentView::cycleEngine(int step) {
	if (getInstrumentType()!=IT_SYNTH) return ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(viewData_->currentInstrument_) ;
	Variable *v=instr->FindVariable(SYP_ENGINE) ;
	if (!v) return ;
	const char *fmCfg=Config::GetInstance()->GetValue("FM_ENGINE") ;
	bool fmEnabled=!(fmCfg && (fmCfg[0]=='N'||fmCfg[0]=='n')) ;
	int e=v->GetInt()+step ;
	if (!fmEnabled && e==SET_FM) e+=step ;   // skip the hidden engine
	if (e<0||e>=SET_LAST) return ;           // ends stop, no wrap
	v->SetInt(e) ;   // notify -> deferred rebuild (+ audition retrigger)
	isDirty_=true ;
} ;

/* Stepping the preset row IS loading: whenever its ui variable moved
   (d-pad ladder or O+arrows), queue the deferred load. Index 0 ("--")
   is the unloaded state and loads nothing. */
void InstrumentView::checkPresetStep() {
	if (!presetVar_) return ;
	int pv=presetVar_->GetInt() ;
	if (pv==presetShown_) return ;
	// leaving "--": snapshot the sound about to be overwritten, so the
	// row is a browser, not a one-way door. Taken here, BEFORE the
	// deferred load runs, so the values are still the original ones.
	if (presetShown_==0&&pv>0) {
		InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
		I_Instrument *instr=bank->GetInstrument(viewData_->currentInstrument_) ;
		if (instr&&instr->GetType()==IT_SYNTH) {
			SynthPresets::Capture(instr,presetUndo_) ;
			presetUndoValid_=true ;
		}
	}
	presetShown_=pv ;
	if (pv>0) {
		pendingPreset_=pv-1 ;
		presetPending_=true ;   // applied at the top of the next DrawView
	} else if (presetUndoValid_) {
		presetRestorePending_=true ;   // back to "--": restore the sound
	}
	isDirty_=true ;
} ;

void InstrumentView::OnSavePreset(const char *name) {
	// the modal callback runs inside the input path's mixer lock; the
	// stick write happens in ApplyDeferred, outside every lock
	strncpy(presetSaveName_,name,sizeof(presetSaveName_)-1) ;
	presetSaveName_[sizeof(presetSaveName_)-1]=0 ;
	presetSavePending_=true ;
	isDirty_=true ;
} ;

/* The nub owns the parameters: alone it WALKS the focus between the
   fields (all four directions); with O held it TURNS the focused value
   (left/right fine, up/down coarse -- the O+arrows grammar on an
   analog feel). Walking can never edit, turning needs the O. */
void InstrumentView::OnNubFlick(int dir, unsigned short mask) {
	static const unsigned short bits[4]=
	    {EPBM_LEFT,EPBM_RIGHT,EPBM_UP,EPBM_DOWN} ;
	if (mask&EPBM_A) {
		UIField *f=GetFocus() ;
		if (!f) return ;
		f->ProcessArrow(bits[dir&3]) ;
		checkPresetStep() ;   // the nub can step the preset row too
	} else {
		// walk -- but never REST on the ladder: type/engine/wave belong
		// to the d-pad. A step that lands there keeps going in the same
		// direction (so the stick hops over the ladder into the
		// parameters); if there is no parameter that way, the focus
		// returns to where it started.
		UIField *before=GetFocus() ;
		UIField *landed=0 ;
		for (int guard=0;guard<6;guard++) {
			UIField *prev=GetFocus() ;
			FieldView::ProcessButtonMask(bits[dir&3]) ;
			UIField *now=GetFocus() ;
			if (now==prev) break ;           // hit the edge of the grid
			if (!ivIsSelectorId(((UIIntVarField *)now)->GetVariableID())) {
				landed=now ;
				break ;
			}
		}
		if (!landed) SetFocus(before) ;
	}
	isDirty_=true ;
} ;

bool InstrumentView::OnNavTo(ViewType to) {
	// the table editor owns TWO tiles on the map (VT_TABLE/VT_TABLE2);
	// arriving on either one means this instrument's table
	if (to==VT_TABLE||to==VT_TABLE2) {
		/* Jumping to the table screen from here means THIS
		   instrument's table -- the one its table row names -- not
		   whichever table was looked at last. Same coupling the song
		   screen got for its chains. Unset stays put: there is
		   nothing to follow. */
		I_Instrument *instr=viewData_->project_->GetInstrumentBank()
		    ->GetInstrument(viewData_->currentInstrument_) ;
		int t=instr?instr->GetTable():-1 ;
		if (t<0) {
			// nothing to follow, so the jump is refused and says
			// why -- the same rule as the empty chain row
			View::SetNotification("no table set - set one first") ;
			return false ;
		}
		viewData_->currentTable_=t ;
	}
	return true ;
}

void InstrumentView::LooseFocus() {
	if (auditionLatch_) auditionStop() ;
	View::LooseFocus() ;
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
	if (auditionLatch_) auditionRetrigger() ;
	isDirty_=true ;
} ;

void InstrumentView::FocusSampleRow() {
	lastFocusID_=SIP_SAMPLE ;
} ;

void InstrumentView::openImportBrowser() {
	/* the samplelib not existing used to be an error message and a
	   dead end; it is a folder, so make it */
	Path sampleLib(SamplePool::GetInstance()->GetSampleLib()) ;
	FileSystem *fs=FileSystem::GetInstance() ;
	if (fs->GetFileType(sampleLib.GetPath().c_str())!=FT_DIR) {
		fs->MakeDir(sampleLib.GetPath().c_str()) ;
	}
	if (fs->GetFileType(sampleLib.GetPath().c_str())!=FT_DIR) {
		MessageBox *mb=new MessageBox(*this,"Can't access the samplelib",MBBF_OK) ;
		DoModal(mb) ;
		return ;
	}
	ImportSampleDialog *isd=new ImportSampleDialog(*this) ;
	DoModal(isd, ImportSampleDialogCallback);
} ;

void InstrumentView::ProcessButtonMask(unsigned short mask,bool pressed) {

	if (!pressed) {
		// the preview lives exactly as long as SELECT is held: the
		// note starts on the press and dies on the release, like a key
		if (auditionLatch_&&!(mask&EPBM_SELECT)) {
			auditionStop() ;
			isDirty_=true ;
		}
		return ;
	}

	isDirty_=false ;

	if (viewMode_==VM_NEW) {
		if (mask==EPBM_A) {
			UIIntVarField *field=(UIIntVarField *)GetFocus() ;
			Variable &v=field->GetVariable() ;
			switch(v.GetID()) {
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

	// ---- the instrument deck: focus-independent controls ----
	// SELECT previews the instrument for as long as it is held: press
	// = note on, release = note off (handled above the !pressed
	// early-return). No latch, no timing.
	if (mask==EPBM_A) {
		UIField *fa=GetFocus() ;
		if (fa&&fa->GetVariableID()==IVP_IMPORT) {
			importPending_=true ;   // opened from ApplyDeferred
			isDirty_=true ;
			return ;
		}
	}
	if (mask==EPBM_SELECT) {
		if (!auditionLatch_) auditionStart() ;
		isDirty_=true ;
		return ;
	}
	// transport takes the channels: drop the latch first
	if ((mask&EPBM_START)&&auditionLatch_) auditionStop() ;
	// L+arrows step the instrument NUMBER from anywhere -- the nub
	// picks WHAT it is (type/engine), L picks WHICH one. B+L still
	// arms clone, R+L is undo, and A stays clear for the clone flow.
	if ((mask&EPBM_L)&&!(mask&(EPBM_A|EPBM_B|EPBM_R))) {
		if (mask&EPBM_LEFT)  { warpToNext(-1) ; return ; }
		if (mask&EPBM_RIGHT) { warpToNext(+1) ; return ; }
		if (mask&EPBM_DOWN)  { warpToNext(-16) ; return ; }
		if (mask&EPBM_UP)    { warpToNext(+16) ; return ; }
	}
	/* Two zones, and X+up/down crosses between them: the SELECTOR
	   LADDER (type, bank, engine, preset, import) and the PARAMETER
	   grid. Inside a zone the plain d-pad walks -- the ladder used to
	   own the d-pad outright, so reaching a parameter without the nub
	   was impossible. */
	if ((mask&EPBM_B)&&!(mask&(EPBM_A|EPBM_L|EPBM_R|EPBM_SELECT|EPBM_START))&&
	    (mask&(EPBM_UP|EPBM_DOWN))) {
		UIField *f=GetFocus() ;
		bool onLadder=f&&ivIsSelectorId(f->GetVariableID()) ;
		UIField *target=0 ;
		IteratorPtr<UIField> itz(T_SimpleList<UIField>::GetIterator()) ;
		if ((mask&EPBM_DOWN)&&onLadder) {
			// down into the parameters: the first row that is not a
			// selector, in list order (the screen's own reading order)
			for (itz->Begin();!itz->IsDone();itz->Next()) {
				UIField &fld=itz->CurrentItem() ;
				if (fld.IsStatic()) continue ;
				if (!ivIsSelectorId(fld.GetVariableID())) { target=&fld ; break ; }
			}
		} else if ((mask&EPBM_UP)&&!onLadder) {
			// back up to the ladder row we came from
			UIField *firstSel=0 ;
			for (itz->Begin();!itz->IsDone();itz->Next()) {
				UIField &fld=itz->CurrentItem() ;
				FourCC id=fld.GetVariableID() ;
				if (!ivIsSelectorId(id)) continue ;
				if (!firstSel) firstSel=&fld ;
				if (id==lastLadderID_) { target=&fld ; break ; }
			}
			if (!target) target=firstSel ;
		}
		if (target) {
			SetFocus(target) ;
			lastFocusID_=target->GetVariableID() ;
			isDirty_=true ;
		}
		return ;
	}

	/* Plain d-pad: walks whichever zone the focus is in. On the ladder
	   up/down change row and left/right step the value; in the
	   parameters it is ordinary field navigation (O+arrows edits). */
	if (!(mask&(EPBM_A|EPBM_B|EPBM_L|EPBM_R|EPBM_SELECT|EPBM_START)) &&
	    (mask&(EPBM_LEFT|EPBM_RIGHT|EPBM_UP|EPBM_DOWN)) &&
	    GetFocus()&&ivIsSelectorId(GetFocus()->GetVariableID())) {
		UIField *sel[6] ; int nSel=0, cur=-1 ;
		UIField *f=GetFocus() ;
		IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
		for (it->Begin();!it->IsDone()&&nSel<6;it->Next()) {
			UIField &fld=it->CurrentItem() ;
			FourCC id=fld.GetVariableID() ;   // virtual: safe for action rows
			if (ivIsSelectorId(id)) {
				if (&fld==f) cur=nSel ;
				sel[nSel++]=&fld ;
			}
		}
		if (nSel>0) {
			/* record the ladder row we are acting on BEFORE stepping:
			   an engine/wave step rebuilds the whole field list, and
			   the rebuild restores focus by lastFocusID_ -- left stale,
			   focus silently fell onto the TYPE row and the next press
			   changed the type instead of the engine. */
			if (cur<0) {
				SetFocus(sel[0]) ;             // back up to the ladder
				lastFocusID_=lastLadderID_=sel[0]->GetVariableID() ;
			} else if (mask&(EPBM_LEFT|EPBM_RIGHT)) {
				lastFocusID_=lastLadderID_=sel[cur]->GetVariableID() ;
				sel[cur]->ProcessArrow(mask&(EPBM_LEFT|EPBM_RIGHT)) ;
				checkPresetStep() ;
			} else if ((mask&EPBM_UP)&&cur>0) {
				SetFocus(sel[cur-1]) ;
				lastFocusID_=lastLadderID_=sel[cur-1]->GetVariableID() ;
			} else if ((mask&EPBM_DOWN)&&cur<nSel-1) {
				SetFocus(sel[cur+1]) ;
				lastFocusID_=lastLadderID_=sel[cur+1]->GetVariableID() ;
			} else if (mask&EPBM_DOWN) {
				/* Down off the ladder's last row crosses into the
				   parameters, the same landing X+down makes: the first
				   row that is not a selector, in the screen's reading
				   order. The zones meet at their edges now; the X chord
				   stays as the jump from anywhere. */
				UIField *target=0 ;
				IteratorPtr<UIField> itd(T_SimpleList<UIField>::GetIterator()) ;
				for (itd->Begin();!itd->IsDone();itd->Next()) {
					UIField &fld=itd->CurrentItem() ;
					if (fld.IsStatic()) continue ;
					if (!ivIsSelectorId(fld.GetVariableID())) { target=&fld ; break ; }
				}
				if (target) {
					SetFocus(target) ;
					lastFocusID_=target->GetVariableID() ;
				}
			}
			isDirty_=true ;
		}
		return ;
	}

	// the quiet fallback for a worn-out nub: X+d-pad walks the
	// variables exactly as the stick does, same ladder fence and all
	if ((mask&EPBM_B)&&!(mask&(EPBM_A|EPBM_L|EPBM_R|EPBM_SELECT))&&
	    (mask&(EPBM_LEFT|EPBM_RIGHT|EPBM_UP|EPBM_DOWN))) {
		int dir=(mask&EPBM_LEFT)?0:(mask&EPBM_RIGHT)?1:
		        (mask&EPBM_UP)?2:3 ;
		OnNubFlick(dir,0) ;
		return ;
	}

	if (mask==EPBM_A&&presetVar_) {
		UIField *pf2=GetFocus() ;
		if (pf2&&((UIIntVarField *)pf2)->GetVariableID()==IVP_PRESET) {
			NewProjectDialog *mb=new NewProjectDialog(*this,Path("bin:presets")) ;
			DoModal(mb,SavePresetCallback) ;
			return ;
		}
	}

	/* Plain arrows and the zone edge. The selector rows sit in the
	   same field list and the same column, so ordinary navigation
	   (and its wrap) walks onto the ladder. UP off the top parameter
	   is allowed through -- that is the edge, and the cursor should
	   cross it the way it does on every other tracker -- while any
	   other direction that lands on a selector is put back, so a wrap
	   or a sideways step never changes what the instrument is by
	   accident. X+up/down remains the jump from anywhere. */
	UIField *beforeNav=GetFocus() ;
	bool wasParam=beforeNav&&!ivIsSelectorId(beforeNav->GetVariableID()) ;
	FieldView::ProcessButtonMask(mask) ;
	if (wasParam&&!(mask&(EPBM_A|EPBM_B))) {
		UIField *nowF=GetFocus() ;
		if (nowF&&ivIsSelectorId(nowF->GetVariableID())) {
			if (mask&EPBM_UP) {
				lastFocusID_=lastLadderID_=nowF->GetVariableID() ;
			} else {
				SetFocus(beforeNav) ;
			}
		}
	}
	checkPresetStep() ;

    Player *player=Player::GetInstance() ;
	// B Modifier

    if (mask & EPBM_B) {
        // X+arrow warping removed: L+arrows are the one slot control
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
            /* The file row no longer opens the browser. O there is
               half of O+arrows -- the gesture that steps a value on
               every screen -- and opening a file browser the instant
               the O landed made choosing a sample already in the
               project impossible. Import has its own row now, in
               plain sight, which is where that job belongs. */
            if ((varID == SIP_TABLE) || (varID == MIP_TABLE) ||
                (varID == SIP_PRINTFX)) {
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
                    if (table == VAR_OFF) {
                        View::SetNotification("no table set - set one first");
                    } else {
                        viewData_->currentTable_ = table;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        SetChanged();
                        NotifyObservers(&ve);
                    }
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

/* The deferred rebuild/type-swap/preset work. Runs on the main thread
   with no field method on the stack (the original deferral reasons)
   AND outside drawMutex_ (the deadlock reason it moved out of
   DrawView: these branches take the mixer lock, and the render thread
   takes sync_ then drawMutex_ every block). */
void InstrumentView::ApplyDeferred() {

	FieldView::ApplyDeferred() ;

	if (importPending_) {
		importPending_=false ;
		openImportBrowser() ;
	}

	if (presetSavePending_) {
		presetSavePending_=false ;
		InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
		I_Instrument *instr=bank->GetInstrument(viewData_->currentInstrument_) ;
		if (instr&&instr->GetType()==IT_SYNTH) {
			if (SynthPresets::Save(presetSaveName_,instr)) {
				SynthPresets::Scan() ;
				presetShown_=0 ;
				for (int pi=0;pi<SynthPresets::Count();pi++) {
					if (!strcmp(SynthPresets::Name(pi),presetSaveName_)) {
						presetShown_=pi+1 ; break ;
					}
				}
				lastFocusID_=IVP_PRESET ;
				rebuildPending_=true ;
				char msg[40] ;
				snprintf(msg,sizeof(msg),"saved preset %s",presetSaveName_) ;
				View::SetNotification(msg) ;
			} else {
				View::SetNotification("preset save failed") ;
			}
		}
		isDirty_=true ;
	}

	if (applyTypePending_) {
		applyTypePending_=false ;
		rebuildPending_=false ;
		applyTypeChange() ;
		if (auditionLatch_) auditionRetrigger() ;
	} else if (presetPending_) {
		// preset load = a burst of SetStrings, each notifying; do it
		// here for the same reason as the type swap, then rebuild once
		presetPending_=false ;
		InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
		I_Instrument *instr=bank->GetInstrument(viewData_->currentInstrument_) ;
		if (instr&&instr->GetType()==IT_SYNTH) {
			/* The FILE read happens before the lock: a Memory Stick
			   open is tens of ms, and holding the mixer lock across it
			   starved the render thread for the whole read. Only the
			   parameter APPLY needs the lock (and the voice cut -- an
			   engine flip under a sounding voice tears its state). */
			SynthPresets::ParamSnapshot snap ;
			if (SynthPresets::ReadPreset(pendingPreset_,snap)) {
				MixerService *ms=MixerService::GetInstance() ;
				ms->Lock() ;
				Player::GetInstance()->CutInstrument(instr) ;
				presetApplying_=true ;
				SynthPresets::Restore(snap,instr) ;
				presetApplying_=false ;
				ms->Unlock() ;
			}
		}
		rebuildPending_=false ;
		lastFocusID_=IVP_PRESET ;
		onInstrumentChange() ;
		// browsing is SILENT by request: the cut above ended any
		// ringing preview, and only SELECT demos the new sound
		auditionLatch_=false ;
	} else if (presetRestorePending_) {
		// the browse backed out: replay the snapshot taken when it
		// began, then drop it -- edits made back on "--" must be the
		// base of the NEXT browse, not this stale copy
		presetRestorePending_=false ;
		InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
		I_Instrument *instr=bank->GetInstrument(viewData_->currentInstrument_) ;
		if (instr&&instr->GetType()==IT_SYNTH&&presetUndoValid_) {
			MixerService *ms=MixerService::GetInstance() ;
			ms->Lock() ;
			Player::GetInstance()->CutInstrument(instr) ;
			presetApplying_=true ;
			SynthPresets::Restore(presetUndo_,instr) ;
			presetApplying_=false ;
			ms->Unlock() ;
		}
		presetUndoValid_=false ;
		presetUndo_.clear() ;
		rebuildPending_=false ;
		lastFocusID_=IVP_PRESET ;
		onInstrumentChange() ;
		auditionLatch_=false ;   // silent here too: SELECT demos
	} else if (rebuildPending_) {
		rebuildPending_=false ;
		onInstrumentChange() ;
	}
}

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
    DrawTitleStrip(title, auditionLatch_
                              ? "AUDIT"
                              : InstrumentTypeData[getInstrumentType()]);

    switch (getInstrumentType()) {
        case IT_SYNTH:  drawSynthChrome();  break;
        case IT_SAMPLE: drawSampleChrome(); break;
        case IT_MIDI:   drawMidiChrome();   break;
        default: break;
    }

    // Draw fields

    FieldView::Redraw();
    {
        // on the ladder, left and right CHANGE the row; say so where
        // it applies
        UIField *hf=GetFocus() ;
        bool onLadder=hf&&ivIsSelectorId(hf->GetVariableID()) ;
        DrawHintBar(onLadder?"</> change  v params  L inst  SEL hear"
                            :"X+u/d zone  O+arw edit  L inst  SEL hear");
    }
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

	/* What the slot is working with. The whole name -- the file row
	   cuts it at seventeen cells -- and the shape of the sound, frames
	   in hex because start, loop and end count in hex frames. Rows
	   24-28 are free on every page; the hint bar owns 29. */
	DrawPanel(1,24,38,2,"FILE") ;
	{
		SampleInstrument *si=(SampleInstrument *)instrument ;
		SamplePool *pool=SamplePool::GetInstance() ;
		int index=si->GetSampleIndex() ;
		SoundSource *src=(index>=0)?pool->GetSource(index):0 ;
		GUITextProperties props ;
		char line[40] ;
		SetColor(CD_NORMAL) ;
		if (!src) {
			DrawString(2,25,"no sample",props) ;
		} else {
			const char *name=(index<pool->GetNameListSize())?pool->GetNameList()[index]:0 ;
			snprintf(line,37,"%s%s",name?name:"",src->IsBaked()?"  (generated kit)":"") ;
			DrawString(2,25,line,props) ;
			SampleInfo::DescribeSource(src,line,37) ;
			DrawString(2,26,line,props) ;
		}
	}
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
	bool hive=(engine==SET_HIVE) ;

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
		drawFmAlgo(instrument) ;
		DrawPanel(1,21,38,2,"TABLE") ;
		return ;
	}

	DrawPanel(1,5,19,8,hive?"HIVE":"OSC") ;
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
	if (vax||hive) {
		// no graph: the rows are full
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

	if (!pdx&&!vax&&!hive) {
		// TONE runs the same SVF; it just never had the controls on
		// screen, so FCUT and FRES looked like they did nothing
		DrawPanel(21,5,18,3,"FILTER") ;
		DrawPanel(21,11,18,3,"LFO") ;
	}
	if (pdx||vax||hive) {
		DrawPanel(21,5,18,8,(vax||hive)?"FILTER":"DCW") ;
		int gy=(vax||hive)?10:7 ;
		app.OpAdsr(22*8+2,gy*8+1,44,22,
			instrument->FindVariable(SYP_DCWATK)->GetInt(),
			instrument->FindVariable(SYP_DCWDEC)->GetInt(),
			instrument->FindVariable(SYP_DCWSUS)->GetInt()) ;
		DrawPanel(21,15,18,4,(vax||hive)?"LFO + DRIVE":"LFO") ;
	}
	DrawPanel(1,21,38,2,"TABLE") ;
} ;

void InstrumentView::OnFocus() { onInstrumentChange(); }

void InstrumentView::Update(Observable &o,I_ObservableData *d) {

	// a preset load/restore is a burst of ~55 SetStrings, each landing
	// here. Reacting to each one (a rebuild flag is cheap, but the
	// audition retrigger is a note restart RACING the audio thread)
	// wedged the preview voice. The deferred branch rebuilds and
	// retriggers exactly once when the burst is done.
	if (presetApplying_) return ;

	if (&o==(Observable *)&slotVar_) {
		if (refreshingSlot_) return ;
		int n=slotVar_.GetInt() ;
		if (n<0) n=0 ;
		if (n>=MAX_INSTRUMENT_COUNT) n=MAX_INSTRUMENT_COUNT-1 ;
		viewData_->currentInstrument_=n ;
		lastFocusID_=IVP_SLOT ;   // keep focus on the stepper across the rebuild
		rebuildPending_=true ;    // DEFERRED: we are inside the field's edit
		if (auditionLatch_) auditionRetrigger() ;
		isDirty_=true ;
		return ;
	}

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
		// No confirmation any more: SetType stashes the leaving type's
		// settings and replays them when the slot returns to it, so a
		// flip loses nothing.
		// DEFER: this notification came from the type pill's own
		// ProcessArrow; applying now would delete that field under its
		// executing method (see the flags in the header)
		applyTypePending_=true ;
		isDirty_=true ;
		return ;
	}

	// the visible import row: its press arrives here as its fourcc
	if (((unsigned int)(uintptr_t)d)==IVP_IMPORT) {
		importPending_=true ;   // ApplyDeferred opens it outside the locks
		isDirty_=true ;
		return ;
	}

	// DEFER, same reason: engine/wave notifications arrive from the
	// editing field's call stack, inside the variable's observer walk
	rebuildPending_=true ;
	if (auditionLatch_) auditionRetrigger() ;
	isDirty_=true ;
}

/* The routing picture, under the operator columns it describes.
 *
 * Four operators and eight algorithms is eight numbers and a shrug
 * until you can see where they go. This draws it: a box per operator,
 * stacked by how far it is from the output, with a line from each one
 * to whatever it feeds.
 *
 * Two things it does deliberately.
 *
 * Each operator keeps its own horizontal slot whatever the algorithm
 * does, and the slot is labelled with the operator number. A diagram
 * that rearranged the boxes to look tidy would be prettier and would
 * make you work out which box was which every time you changed the
 * algorithm.
 *
 * The box is also the level meter: the fill is that operator's level,
 * so an operator turned down is visibly an empty box and the ones
 * doing the work are the full ones. Carriers -- the ones you hear --
 * are drawn in the highlight colour, modulators in the structure
 * colour, so which is which reads without counting arrows.
 *
 * The routing comes from SynthInstrument::AlgoDest, which is the same
 * table the engine plays from. There is no second copy to drift.
 */
void InstrumentView::drawFmAlgo(SynthInstrument *instrument) {

	AppWindow &app=(AppWindow &)w_ ;

	int algo=instrument->FindVariable(SYP_FMALGO)->GetInt() ;
	if (algo<0) algo=0 ;
	if (algo>=FM_ALGO_COUNT) algo=FM_ALGO_COUNT-1 ;

	static const FourCC lvlId[FM_OPS]={SYP_FML1,SYP_FML2,SYP_FML3,SYP_FML4} ;
	int level[FM_OPS],dest[FM_OPS],depth[FM_OPS] ;
	for (int i=0;i<FM_OPS;i++) {
		level[i]=instrument->FindVariable(lvlId[i])->GetInt()&0xFF ;
		dest[i]=SynthInstrument::AlgoDest(algo,i) ;
	}

	/* How far each operator is from the output. A destination is
	   always a lower-numbered operator, so walking upwards settles in
	   one pass -- but the loop is bounded anyway, because a table
	   edited into a cycle should draw something wrong rather than
	   hang the screen. */
	for (int i=0;i<FM_OPS;i++) {
		int d=0,at=i,guard=0 ;
		while (dest[at]!=FM_OUT&&guard++<FM_OPS) { at=dest[at] ; d++ ; }
		depth[i]=(d>3)?3:d ;
	}

	DrawPanel(21,14,18,5,"ROUTING") ;

	/* Signal runs left to right: the deepest modulator on the left,
	   carriers on the right, output off the right edge.
	 *
	 * The first version stacked it vertically, by depth, which is how
	 * a DX7 manual draws it -- and there are 40 pixels of panel to do
	 * it in. Four levels left two pixels between boxes, so every
	 * connecting line came out a one pixel stub and the picture had no
	 * arrows at all. There are 144 pixels across and 40 down; the
	 * chain goes the way there is room for it.
	 *
	 * Operators at the same depth stack, which is what a diagram
	 * should do anyway: four carriers side by side reads as four
	 * voices, and that is exactly what algorithm 8 is.
	 */
	static const int colCell[4]={22,26,30,34} ;   // depth 3,2,1,0
	GUITextProperties props ;

	// where each operator sits: column by depth, row by how many
	// share that depth
	int rowOf[FM_OPS],colOf[FM_OPS],used[4]={0,0,0,0},total[4]={0,0,0,0} ;
	for (int i=0;i<FM_OPS;i++) total[depth[i]]++ ;
	for (int i=0;i<FM_OPS;i++) {
		int d=depth[i] ;
		colOf[i]=colCell[3-d] ;
		rowOf[i]=15+(4-total[d])/2+used[d] ;
		used[d]++ ;
	}

	for (int i=0;i<FM_OPS;i++) {

		int row=rowOf[i] ;
		int bx=(colOf[i]+1)*8 ;
		int by=row*8+1 ;
		bool carrier=(dest[i]==FM_OUT) ;
		bool live=(level[i]>0) ;

		// the number goes in the cell left of the box, where the
		// overlay cannot paint over it
		SetColor(live?(carrier?CD_HILITE1:CD_ROW2):CD_ROW) ;
		char lab[2]={(char)('1'+i),0} ;
		DrawString(colOf[i],row,lab,props) ;

		app.OpRing(bx,by,16,6,live?AppWindow::OC_GRID:AppWindow::OC_PANEL2) ;
		if (live) {
			int fill=level[i]*14/255 ;
			if (fill<1) fill=1 ;
			app.OpRect(1,bx+1,by+1,fill,4,carrier?CD_HILITE1:CD_HILITE2) ;
		}

		// the line to whatever it feeds, rightwards
		int fromX=bx+16 ;
		int cy=by+2 ;
		if (carrier) {
			app.OpRect(1,fromX,cy,(38*8)-fromX,2,AppWindow::OC_GRID) ;
		} else {
			int drow=rowOf[dest[i]] ;
			/* Stop at the LEFT edge of the destination's label cell,
			   not at its box. Run it to the box and the line crosses
			   the digit, which reads as a number with a line through
			   it rather than as a signal arriving. */
			int dx=colOf[dest[i]]*8 ;
			int toY=drow*8+3 ;
			int midX=(fromX+dx)/2 ;
			app.OpRect(1,fromX,cy,midX-fromX,2,AppWindow::OC_GRID) ;
			int y0=(cy<toY)?cy:toY ;
			int hseg=(cy<toY)?(toY-cy+2):(cy-toY+2) ;
			app.OpRect(1,midX,y0,2,hseg,AppWindow::OC_GRID) ;
			app.OpRect(1,midX,toY,dx-midX,2,AppWindow::OC_GRID) ;
		}
	}

	/* op4's feedback loop, when it is turned up: a small hook off the
	   right of its box.
	   
	   It used to be drawn above the box, at by-3, which is in the row
	   ABOVE -- fine while op4 happened to sit low, and straight
	   through the panel's top rule the moment an algorithm put it on
	   the first row. Everything here stays inside its own row. */
	if ((instrument->FindVariable(SYP_FMFB)->GetInt()&0xFF)>0) {
		int bx=(colOf[FM_OPS-1]+1)*8 ;
		int by=rowOf[FM_OPS-1]*8+1 ;
		app.OpRect(1,bx+16,by,6,2,CD_HILITE2) ;
		app.OpRect(1,bx+20,by,2,6,CD_HILITE2) ;
		app.OpRect(1,bx+16,by+4,6,2,CD_HILITE2) ;
	}
	SetColor(CD_NORMAL) ;
}
