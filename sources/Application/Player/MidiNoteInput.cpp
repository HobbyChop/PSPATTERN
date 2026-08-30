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
/* One clock byte.

   This used to time the gap between quarter notes, turn it into a
   whole number of beats per minute and write that to the project. Two
   things were wrong with it. It measured the leader's SPEED and never
   its POSITION, so a song that came in late stayed late for ever. And
   the timestamps came from a pump that sleeps up to 20ms and hands
   over whatever arrived in a burst, so the measurement was noisy
   before it was rounded to a whole beat per minute.

   Counting is exact where timing is not, so the count goes to the
   player, which holds it against its own slices. */
void MidiNoteInput::onClock() {

	if (Player::GetSyncMode()!=Player::SYNC_FOLLOW) return ;
	Player::GetInstance()->OnMidiClock() ;
} ;

void MidiNoteInput::onStart(bool fromTop) {
	if (Player::GetSyncMode()!=Player::SYNC_FOLLOW) return ;
	clockCount_=0 ;
	lastQuarterMs_=0 ;
	Player *player=Player::GetInstance() ;
	if (player->IsRunning()) return ;
	/* fromSync: this IS the leader's start, so it starts the song
	   rather than arming it to wait for one.

	   fromTop is passed on but the two are not yet told apart -- both
	   begin at the cursor. Resuming where a stopped song sat would
	   mean remembering that position, which nothing does yet. */
	player->OnSongStartButton(0,SONG_CHANNEL_COUNT-1,false,fromTop,true) ;
} ;

void MidiNoteInput::onStop() {
	if (Player::GetSyncMode()!=Player::SYNC_FOLLOW) return ;
	clockCount_=0 ;
	lastQuarterMs_=0 ;
	Player *player=Player::GetInstance() ;
	if (player->IsRunning()) {
		player->Stop() ;
	}
	// A leader that stops while we are still waiting to start cancels
	// the wait, rather than leaving the song armed for a start that
	// already came and went.
	player->CancelArm() ;
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
