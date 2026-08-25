#include "MidiNoteInput.h"
#include "Player.h"
#include "Services/Midi/MidiService.h"
#include "Services/Midi/MidiMessage.h"
#include "Application/Model/Project.h"
#include "System/System/System.h"

MidiNoteInput::MidiNoteInput() {
	attached_=false ;
	project_=0 ;
	clockCount_=0 ;
	lastQuarterMs_=0 ;
	smoothedBpm_=0 ;
} ;

void MidiNoteInput::SetProject(Project *project) {
	project_=project ;
	clockCount_=0 ;
	lastQuarterMs_=0 ;
	smoothedBpm_=0 ;
} ;

/* 24 clocks to the quarter note. Timing one byte is far too noisy to
   set a tempo from, so the interval is measured a whole quarter at a
   time and then smoothed again -- a clock that wobbles by a millisecond
   should not make the song lurch. */
void MidiNoteInput::onClock() {

	if ((!project_)||(project_->GetMidiSync()==0)) return ;

	if (++clockCount_<24) return ;
	clockCount_=0 ;

	unsigned long now=System::GetInstance()->GetClock() ;
	unsigned long last=lastQuarterMs_ ;
	lastQuarterMs_=now ;
	if (last==0) return ;               // first quarter: nothing to measure

	unsigned long elapsed=now-last ;
	if ((elapsed<60)||(elapsed>2000)) { // 30..1000bpm; anything else is a glitch
		return ;
	}

	int bpm=(int)(60000/elapsed) ;
	if (bpm<30) bpm=30 ;
	if (bpm>400) bpm=400 ;

	// weighted toward the running value so one late byte cannot lurch
	// the song, but a real tempo change still arrives within a bar
	if (smoothedBpm_==0) {
		smoothedBpm_=bpm ;
	} else {
		smoothedBpm_=(smoothedBpm_*3+bpm)/4 ;
	}

	if (smoothedBpm_!=project_->GetTempo()) {
		project_->SetTempo(smoothedBpm_) ;
	}
} ;

void MidiNoteInput::onStart(bool fromTop) {
	if ((!project_)||(project_->GetMidiSync()==0)) return ;
	clockCount_=0 ;
	lastQuarterMs_=0 ;
	Player *player=Player::GetInstance() ;
	if (player->IsRunning()) return ;
	// Continue resumes where the song sits; Start goes back to the top.
	// Start means from the beginning; Continue resumes where it sits.
	player->OnSongStartButton(0,SONG_CHANNEL_COUNT-1,false,fromTop) ;
} ;

void MidiNoteInput::onStop() {
	if ((!project_)||(project_->GetMidiSync()==0)) return ;
	clockCount_=0 ;
	lastQuarterMs_=0 ;
	Player *player=Player::GetInstance() ;
	if (player->IsRunning()) {
		player->Stop() ;
	}
} ;

void MidiNoteInput::Attach() {

	if (attached_) return ;

	MidiService *svc=MidiService::GetInstance() ;
	IteratorPtr<MidiInDevice> it(svc->GetInIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		MidiInDevice &current=it->CurrentItem() ;
		current.AddObserver(*this) ;
	}
	attached_=true ;
} ;

void MidiNoteInput::Update(Observable &o,I_ObservableData *d) {

	MidiMessage *msg=(MidiMessage *)d ;
	if (!msg) return ;

	// System realtime bytes are whole status bytes, not channel
	// messages -- they have to be checked before the channel mask.
	switch (msg->status_) {
		case 0xF8: onClock() ;      return ;   // clock
		case 0xFA: onStart(true) ;  return ;   // start, from the top
		case 0xFB: onStart(false) ; return ;   // continue
		case 0xFC: onStop() ;       return ;   // stop
		default: break ;
	}

	int status=msg->status_&0xF0 ;
	Player *player=Player::GetInstance() ;

	switch (status) {
		case 0x90:   // note on, velocity 0 means off
			player->MidiNoteOn(msg->data1_,msg->data2_) ;
			break ;
		case 0x80:
			player->MidiNoteOff(msg->data1_) ;
			break ;
		case 0xB0:
			// 123 is All Notes Off, and a keyboard sends it when it
			// panics -- honouring it is the difference between a stuck
			// drone and silence
			if (msg->data1_==123) {
				player->MidiAllNotesOff() ;
			}
			break ;
		default:
			break ;
	}
} ;
