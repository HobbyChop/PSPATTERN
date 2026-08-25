#include "MidiInstrument.h"
#include "CommandList.h"
#include "System/Console/Trace.h"
#include <string.h>
#include <stdio.h>

MidiService *MidiInstrument::svc_=0 ;

// bend is carried in 1/256 semitone
#define BEND_UNIT 256

MidiInstrument::MidiInstrument() {

	strcpy(name_,"0") ;

	if (svc_==0) {
		svc_=MidiService::GetInstance() ;
	};

	Variable *v=new Variable("channel",MIP_CHANNEL,0) ;
	Insert(v) ;
	v=new Variable("note length",MIP_NOTELENGTH,0) ;
	Insert(v) ;
	v=new Variable("volume",MIP_VOLUME,255) ;
	Insert(v) ;
	// Two semitones is what almost every synth powers up with, so PTCH
	// on a MIDI channel lands on the right pitch out of the box.
	v=new Variable("bend range",MIP_BENDRANGE,2) ;
	Insert(v) ;
	v=new Variable("cc1 num",MIP_CC1NUM,-1) ;
	Insert(v) ;
	v=new Variable("cc1 val",MIP_CC1VAL,0) ;
	Insert(v) ;
	v=new Variable("cc2 num",MIP_CC2NUM,-1) ;
	Insert(v) ;
	v=new Variable("cc2 val",MIP_CC2VAL,0) ;
	Insert(v) ;
	v=new Variable("cc3 num",MIP_CC3NUM,-1) ;
	Insert(v) ;
	v=new Variable("cc3 val",MIP_CC3VAL,0) ;
	Insert(v) ;
	v=new Variable("cc4 num",MIP_CC4NUM,-1) ;
	Insert(v) ;
	v=new Variable("cc4 val",MIP_CC4VAL,0) ;
	Insert(v) ;
	v=new Variable("table",MIP_TABLE,-1) ;
	Insert(v) ;
	v=new Variable("table automation",MIP_TABLEAUTO,false) ;
	Insert(v) ;

	memset(voice_,0,sizeof(voice_)) ;
	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		voice_[i].velocity_=127 ;
		voice_[i].remainingTicks_=-1 ;
		voice_[i].bentSent_=-1 ;
	}
}

MidiInstrument::~MidiInstrument() {
} ;

bool MidiInstrument::Init() {
	tableState_.Reset() ;
	return true ;
};

void MidiInstrument::OnStart() {
	tableState_.Reset() ;
} ;

int MidiInstrument::midiChannel() {
	Variable *v=FindVariable(MIP_CHANNEL) ;
	return v->GetInt() ;
} ;

void MidiInstrument::sendCC(int controller,int value) {
	if ((controller<0)||(controller>127)) return ;
	if (value<0) value=0 ;
	if (value>127) value=127 ;
	MidiMessage msg ;
	msg.status_=MIDI_CC+midiChannel() ;
	msg.data1_=controller ;
	msg.data2_=value ;
	svc_->QueueMessage(msg) ;
} ;

/* The four assignable controllers, sent once as the note starts. A slot
   with no number is skipped, so the default patch sends nothing extra. */
void MidiInstrument::sendPatchControllers() {
	static const FourCC numId[MIDI_CC_SLOTS]=
		{MIP_CC1NUM,MIP_CC2NUM,MIP_CC3NUM,MIP_CC4NUM} ;
	static const FourCC valId[MIDI_CC_SLOTS]=
		{MIP_CC1VAL,MIP_CC2VAL,MIP_CC3VAL,MIP_CC4VAL} ;
	for (int i=0;i<MIDI_CC_SLOTS;i++) {
		Variable *n=FindVariable(numId[i]) ;
		Variable *v=FindVariable(valId[i]) ;
		if ((n)&&(v)&&(n->GetInt()>=0)) {
			sendCC(n->GetInt(),v->GetInt()) ;
		}
	}
} ;

void MidiInstrument::sendNoteOn(MidiVoice &v) {
	MidiMessage msg ;
	msg.status_=MIDI_NOTE_ON+midiChannel() ;
	msg.data1_=v.lastNote_ ;
	msg.data2_=v.velocity_ ;
	svc_->QueueMessage(msg) ;
	v.playing_=true ;
} ;

void MidiInstrument::sendNoteOff(MidiVoice &v) {
	MidiMessage msg ;
	msg.status_=MIDI_NOTE_OFF+midiChannel() ;
	msg.data1_=v.lastNote_ ;
	msg.data2_=0x00 ;
	svc_->QueueMessage(msg) ;
	v.playing_=false ;
} ;

/* 14 bit, centre 0x2000. Only sent when the value actually moves --
   a slide otherwise floods the queue with duplicates every tick. */
void MidiInstrument::sendBend(MidiVoice &v) {

	Variable *r=FindVariable(MIP_BENDRANGE) ;
	int range=r?r->GetInt():2 ;
	if (range<1) range=1 ;

	// bend_ is 1/256 semitone; scale it against the device's range
	int units=v.bend_*8192/(range*BEND_UNIT) ;
	if (units>8191) units=8191 ;
	if (units<-8192) units=-8192 ;
	int value=8192+units ;
	if (value==v.bentSent_) return ;
	v.bentSent_=value ;

	MidiMessage msg ;
	msg.status_=MIDI_PITCHBEND+midiChannel() ;
	msg.data1_=value&0x7F ;
	msg.data2_=(value>>7)&0x7F ;
	svc_->QueueMessage(msg) ;
} ;

void MidiInstrument::setBendSemitones(MidiVoice &v,int semis,int rate) {
	v.bendTarget_=semis*BEND_UNIT ;
	v.bendRate_=rate ;
	if (rate==0) {
		v.bend_=v.bendTarget_ ;
	}
} ;

/* One-pole walk toward the target, once per tick. */
void MidiInstrument::stepBend(MidiVoice &v) {
	if (v.bend_==v.bendTarget_) return ;
	if (v.bendRate_==0) {
		v.bend_=v.bendTarget_ ;
		return ;
	}
	int diff=v.bendTarget_-v.bend_ ;
	int step=(diff*v.bendRate_)>>8 ;
	if (step==0) {
		v.bend_=v.bendTarget_ ;
	} else {
		v.bend_+=step ;
	}
} ;

bool MidiInstrument::Start(int c,unsigned char note,bool retrigger) {

	MidiVoice &v=voice_[c] ;

	v.first_=true ;
	v.lastNote_=note ;
	v.baseNote_=note ;

	Variable *nl=FindVariable(MIP_NOTELENGTH) ;
	v.remainingTicks_=nl->GetInt() ;
	if (v.remainingTicks_==0) {
		v.remainingTicks_=-1 ;
	}

	// A new note starts unbent, and says so, so the device is not left
	// holding the last note's slide.
	v.bend_=0 ;
	v.bendTarget_=0 ;
	v.bendRate_=0 ;
	sendBend(v) ;

	v.arpOn_=false ;
	v.arpStep_=0 ;
	v.arpTick_=0 ;
	v.retrig_=false ;

	// channel volume, then whatever the patch's controllers say
	Variable *vol=FindVariable(MIP_VOLUME) ;
	int level=(vol->GetInt()+1)/2 ;
	sendCC(MIDI_CC_VOLUME,level) ;
	v.velocity_=level ;
	sendPatchControllers() ;

	return true ;
} ;

void MidiInstrument::SetChannel(int channel) {
	Variable *v=FindVariable(MIP_CHANNEL) ;
	v->SetInt(channel) ;
} ;

void MidiInstrument::Stop(int c) {
	MidiVoice &v=voice_[c] ;
	sendNoteOff(v) ;
} ;

bool MidiInstrument::Render(int channel,fixed *buffer,int size,bool updateTick) {

	MidiVoice &v=voice_[channel] ;

	// The note-on waits until here so any command on the same row --
	// a program change, a controller, a starting bend -- reaches the
	// device before the note does.
	if (v.first_) {
		sendNoteOn(v) ;
		v.first_=false ;
	}

	stepBend(v) ;
	sendBend(v) ;

	// ARPG: walk the four nibbles, one per tick, as note off/on pairs
	if (v.arpOn_) {
		if (--v.arpTick_<=0) {
			v.arpTick_=1 ;
			int shift=(3-v.arpStep_)*4 ;
			int offset=(v.arpData_>>shift)&0xF ;
			int note=v.baseNote_+offset ;
			if (note<0) note=0 ;
			if (note>127) note=127 ;
			if (note!=v.lastNote_) {
				sendNoteOff(v) ;
				v.lastNote_=note ;
				sendNoteOn(v) ;
			}
			v.arpStep_=(v.arpStep_+1)&3 ;
		}
	}

	if (v.remainingTicks_>0) {
		v.remainingTicks_-- ;
		if (v.remainingTicks_==0) {
			if (!v.retrig_) {
				Stop(channel) ;
			} else {
				v.remainingTicks_=v.retrigLoop_ ;
				sendNoteOff(v) ;
				sendNoteOn(v) ;
			} ;
		} ;
	} ;
	return false ;
};

bool MidiInstrument::IsInitialized() {
	return true ; // Always initialised
} ;

void MidiInstrument::ProcessCommand(int channel,FourCC cc,ushort value) {

	MidiVoice &v=voice_[channel] ;

	switch(cc) {

		case I_CMD_RTRG:
			{
				unsigned char loop=(value&0xFF) ; // ticks before repeat
				if (loop!=0) {
					v.retrig_=true ;
					v.retrigLoop_=loop ;
					v.remainingTicks_=loop ;
				} else {
					v.retrig_=false ;
				}
			}
			break ;

		case I_CMD_MVEL:
			v.velocity_=(value&0xFF)/2 ;
			break ;

		case I_CMD_VOLM:
			sendCC(MIDI_CC_VOLUME,(value&0xFF)/2) ;
			break ;

		// ---- the shared musical commands, on their standard CCs ------
		// These did nothing at all on a MIDI channel before, which made
		// half the command set dead the moment you pointed a track at
		// an external synth.

		case I_CMD_PAN_:
			sendCC(MIDI_CC_PAN,(value&0xFF)/2) ;
			break ;

		case I_CMD_FCUT:
			sendCC(MIDI_CC_CUTOFF,(value&0xFF)/2) ;
			break ;

		case I_CMD_FRES:
			sendCC(MIDI_CC_RESO,(value&0xFF)/2) ;
			break ;

		case I_CMD_FLTR:
			sendCC(MIDI_CC_CUTOFF,((value>>8)&0xFF)/2) ;
			sendCC(MIDI_CC_RESO,(value&0xFF)/2) ;
			break ;

		case I_CMD_PTCH:
			{
				// bb = signed semitones from the note as played,
				// aa = slide rate (00 jumps), same shape as the synth
				int semis=(char)(value&0xFF) ;
				int rate=(value>>8)&0xFF ;
				setBendSemitones(v,semis,rate) ;
			}
			break ;

		case I_CMD_LEGA:
			{
				// slide to a pitch without retriggering: on MIDI that
				// is a bend, since the note is already sounding
				int semis=(char)(value&0xFF) ;
				if (semis!=0) {
					setBendSemitones(v,semis,0x20) ;
				}
			}
			break ;

		case I_CMD_ARPG:
			v.arpData_=value ;
			v.arpStep_=0 ;
			v.arpTick_=0 ;
			v.arpOn_=(value!=0) ;
			if (!v.arpOn_&&(v.lastNote_!=v.baseNote_)) {
				sendNoteOff(v) ;
				v.lastNote_=v.baseNote_ ;
				sendNoteOn(v) ;
			}
			break ;

		// ---- the MIDI specific ones ---------------------------------

		case I_CMD_MDCC:
			sendCC((value&0x7F00)>>8,value&0x7F) ;
			break ;

		case I_CMD_MDPG:
			{
				MidiMessage msg ;
				msg.status_=MIDI_PRG+midiChannel() ;
				msg.data1_=(value&0x7F) ;
				msg.data2_=MidiMessage::UNUSED_BYTE ;
				svc_->QueueMessage(msg) ;
			};
			break ;

		case I_CMD_MDPB:
			{
				// aaaa is the raw 14 bit value, 2000 is centre --
				// for devices whose bend range you do not want to
				// guess at
				int raw=value&0x3FFF ;
				v.bentSent_=-1 ;
				v.bend_=0 ;
				v.bendTarget_=0 ;
				v.bendRate_=0 ;
				MidiMessage msg ;
				msg.status_=MIDI_PITCHBEND+midiChannel() ;
				msg.data1_=raw&0x7F ;
				msg.data2_=(raw>>7)&0x7F ;
				svc_->QueueMessage(msg) ;
			};
			break ;

		case I_CMD_MDAT:
			{
				MidiMessage msg ;
				msg.status_=MIDI_AFTERTOUCH+midiChannel() ;
				msg.data1_=(value&0xFF)/2 ;
				msg.data2_=MidiMessage::UNUSED_BYTE ;
				svc_->QueueMessage(msg) ;
			};
			break ;

		// Set one of the patch's four assignable controllers live. The
		// number comes from the instrument, so the pattern only carries
		// the value.
		case I_CMD_MCCA:
		case I_CMD_MCCB:
		case I_CMD_MCCC:
		case I_CMD_MCCD:
			{
				static const FourCC numId[MIDI_CC_SLOTS]=
					{MIP_CC1NUM,MIP_CC2NUM,MIP_CC3NUM,MIP_CC4NUM} ;
				int slot=(cc==I_CMD_MCCA)?0:
				         (cc==I_CMD_MCCB)?1:
				         (cc==I_CMD_MCCC)?2:3 ;
				Variable *n=FindVariable(numId[slot]) ;
				if ((n)&&(n->GetInt()>=0)) {
					sendCC(n->GetInt(),(value&0xFF)/2) ;
				}
			};
			break ;
	}
} ;

const char *MidiInstrument::GetName() {
	Variable *v=FindVariable(MIP_CHANNEL) ;
	sprintf(name_,"MIDI CH %2.2d",v->GetInt()+1) ;
    return name_ ;
}

int MidiInstrument::GetTable() {
	Variable *v=FindVariable(MIP_TABLE) ;
	return v->GetInt() ;
} ;

bool MidiInstrument::GetTableAutomation() {
	Variable *v=FindVariable(MIP_TABLEAUTO) ;
	return v->GetBool() ;
} ;

void MidiInstrument::GetTableState(TableSaveState &state) {
	memcpy(state.hopCount_,tableState_.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(state.position_,tableState_.position_,sizeof(int)*3) ;
} ;

void MidiInstrument::SetTableState(TableSaveState &state) {
	memcpy(tableState_.hopCount_,state.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(tableState_.position_,state.position_,sizeof(int)*3) ;
} ;


// The channel is skipped: InstrumentBank stamps one in per index
// at construction, so it differs from a fresh instrument without
// anybody having touched it.
bool MidiInstrument::IsAtDefaults() {
    MidiInstrument fresh;
    return SameParametersAs(fresh, MIP_CHANNEL);
}
