#include "Player.h"

#ifdef PLATFORM_PSP
extern "C" unsigned int PSPMidi_LastClockStampUs(void);
#ifdef PSP_ME_OFFLOAD
extern "C" void PSPME_SpectrumEnable(int on);
#endif
#endif
#include "Services/Audio/AudioStats.h"
#include "LiveQueue.h"
#include "MaybeRoll.h"
#include "MidiNoteInput.h"
#include "Application/Views/BaseClasses/ViewEvent.h"
#include "System/io/Status.h"
#include "System/System/System.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/SampleInstrument.h"   // SIP_ROOTNOTE, for the kit
#include "Application/Mixer/MixerService.h"               // IsRendering, at a stop
#include "Application/Utils/char.h"
#include "System/Console/n_assert.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Model/Groove.h"
#include <math.h>
#include <string.h>
#include "Services/Midi/MidiService.h"
#include "Services/Audio/Audio.h"
#include "Application/Model/Config.h"
#include <stdlib.h>

// Private constructor - Singleton

Player::Player() {

    isRunning_ = false;
    viewData_=0;
    memset(midiHeld_,0,sizeof(midiHeld_));
    memset(laneStamp_,0,sizeof(laneStamp_));
    laneClock_=0;
	mixer_=new PlayerMixer();

    lastSongPos_ = 0;
    mode_=PM_SONG;
    rng_=0x1234567u ;
    armed_=false ;
	sequencerMode_=SM_SONG;
	lastPercentage_=0;
	retrigAllImmediate_=false;
	startTime_=0;
	currentTime_=0;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		instrumentOnChannel_[i][0] = ' ';
		instrumentOnChannel_[i][1] = ' ';
		instrumentOnChannel_[i][2] = '\0';
    }
}

Player *Player::GetInstance() {
	if (instance_==0) {
        instance_ = new Player();
    }
    return instance_;
}

bool Player::Init(Project *project,ViewData *viewData) {

    viewData_ = viewData;
    project_ = project;

    if (!mixer_->Init(project)) {
        return false;
    }

    mixer_->AddObserver((*this));
    SyncMaster *sync = SyncMaster::GetInstance();
    sync->SetTempo(project_->GetTempo());
	return mixer_->Start();
}

void Player::Reset() {
    // stop the render thread FIRST: it reads project_ and viewData_
    // on every block, and nulling them while it runs was a window
    // onto a null dereference on every project close
	Close();
    mixer_->RemoveObserver(*this);
    viewData_ = 0;
    project_ = 0;
}

void Player::Close() {
    mixer_->Stop();
    mixer_->Close();
}

void Player::SetChannelMute(int channel,bool mute) {
    mixer_->SetChannelMute(channel, mute);
}

bool Player::IsChannelMuted(int channel) {
    return mixer_->IsChannelMuted(channel);
}

void Player::Start(PlayMode mode, bool forceSongMode) {

    mixer_->Lock();

    // the bar's underrun figure describes THIS run
    AudioStats::ResetUnderruns();
#if defined(PLATFORM_PSP) && defined(PSP_ME_OFFLOAD)
    PSPME_SpectrumEnable(1);   // the spectrum runs only while playing
#endif

    lastBeatCount_ = 0;

    // Get start time for clock

    System *system = System::GetInstance();
    now_ = startClock_ = system->GetClock();

    // Sets play mode.
    // DO I need playMode_ in view data ?
    // Seems like duplicate with mode_

    viewData_->playMode_ = (forceSongMode ? PM_SONG : mode);

    // M8-style phrase follow (off unless the config opts in)
    const char *pf = Config::GetInstance()->GetValue("PHRASE_FOLLOW");
    phraseFollow_ = (pf && !strcmp(pf, "YES"));

    // Always set position, allows for playing song with chain offset
    unsigned playPos = viewData_->songY_ + viewData_->songOffset_;
    lastSongPos_ = playPos;

    // Clear all channel based data

    // the transport takes over: end any preview or keyboard note on
    // the lanes and forget the keys that held them, or a release
    // arriving later would reach for a lane that plays something else
    for (int i = 0; i < 128; i++) midiHeld_[i] = 0;
    for (int l = AUDITION_CHANNEL; l < PLAYER_CHANNEL_COUNT; l++) mixer_->StopChannel(l);
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        mixer_->StopChannel(i);
        timeToLive_[i] = 0;
        timeToStart_[i]=0;
		TablePlayback &tpb=TablePlayback::GetTablePlayback(i);
		tpb.Stop();
    }

    // Tell the instruments we're starting

    project_->GetInstrumentBank()->OnStart();

    Groove::GetInstance()->Reset();

    // Let's get started !

    SyncMaster::GetInstance()->Start();

    firstPlayCycle_ = true;
    mode_ = viewData_->playMode_;

    mixer_->OnPlayerStart();

    MidiService *ms = MidiService::GetInstance();
    // an audition is not the transport: no MIDI Start, no clock
    ms->OnPlayerStart(viewData_->playMode_ != PM_AUDITION);

    switch (viewData_->playMode_) {
    case PM_SONG: {
        for (int i = 0; i < 8; i++) {
            mixer_->StartChannel(i);
            updateSongPos(playPos, i, viewData_->chainRow_);
        }
    } break;

    case PM_LIVE: {
        for (int i = 0; i < 8; i++) {
            if ((liveQueueingMode_[i] == QM_CHAINSTART) ||
                (liveQueueingMode_[i] == QM_PHRASESTART) ||
                (liveQueueingMode_[i] == QM_TICKSTART)) {
                mixer_->StartChannel(i);
                updateSongPos(liveQueuePosition_[i], i,
                              liveQueueChainPosition_[i]);
                liveQueueingMode_[i] = QM_NONE;
            }
        }
    } break;

    case PM_CHAIN:
    case PM_PHRASE: {
        /* These modes play what the SCREEN shows, not what the song
           grid happens to reference under the cursor. Resolution used
           to run song cell -> chain -> row -> phrase, so starting a
           phrase whose song cell was empty -- reached through the
           map, or after editing numbers -- resolved to nothing and
           the button appeared dead. The song position is still laid
           down for context (so the displays agree about where you
           are), and then the viewed chain or phrase is seeded on top,
           explicitly. */
        int currentChannel = viewData_->songX_;
        mixer_->StartChannel(currentChannel);
        int currentChainPos = viewData_->chainRow_;
        updateSongPos(playPos, currentChannel, currentChainPos);
        if (viewData_->playMode_ == PM_CHAIN) {
            viewData_->currentPlayChain_[currentChannel] =
                viewData_->currentChain_;
            mixer_->StartChannel(currentChannel);   // an empty cell above stopped it
            updateChainPos(currentChainPos, currentChannel, -1);
        } else {
            viewData_->currentPlayPhrase_[currentChannel] =
                viewData_->currentPhrase_;
            mixer_->StartChannel(currentChannel);
            updatePhrasePos(0, currentChannel);
        }
    } break;
    case PM_AUDITION: {
	    int currentChannel = viewData_->songX_;
	    mixer_->StartChannel(currentChannel);
	    int currentChainPos = viewData_->chainRow_;
	    int currentPhrasePos = viewData_->phraseCurPos_;
	    // uses hop for PhrasePos
	    updateSongPos(playPos, currentChannel, currentChainPos, currentPhrasePos);
	} break;
		default:
            NInvalid;
            break;
        }

        ProcessCommands();

        // a fresh take: nothing has reached the end of the song yet
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) songPlayed_[i] = false;
        stopAtEnd_ = false;

        AudioOut *out = mixer_->GetAudioOut();
        startTime_ = out ? out->GetStreamTime() : 0;   // audio init can have failed
        /* The queued pre-start silence goes once the first slice of
           the song is rendered, so the first note is a chunk or two
           from the speaker rather than a prebuffer's worth of slices
           -- which is what a follower heard as coming in late, and
           what the clock loop then spent bars pulling back. Under the
           lock: the request reads the slot that slice will land in. */
        if (out) out->RequestStartDrop();

        SetChanged();
        PlayerEvent pe(PET_START);
        NotifyObservers(&pe);

        isRunning_ = true; // keep last !!!!
        mixer_->Unlock();
}

void Player::Stop() {
#if defined(PLATFORM_PSP) && defined(PSP_ME_OFFLOAD)
    PSPME_SpectrumEnable(0);   // stopped: no FFTs of silence
#endif

    // Nothing that stops should still be waiting to start.
    armed_=false ;


    mixer_->Lock();

    /* STOP MEANS SILENCE. Every voice is cut where it stands, the
       keyboard lanes included, and the delay and reverb are emptied
       with them (MixerService::OnPlayerStop), so the transport stops
       the sound and not just the sequencer. It used to let releases
       and tails ring out, and a stop read as the machine still
       playing. The one exception is a take: a render that is stopped
       keeps its releases and its tail, because they are part of the
       take -- the file closes on its own once the sum is silent. */
    bool take = MixerService::GetInstance()->IsRendering();
    for (int i = 0; i < PLAYER_CHANNEL_COUNT; i++) {
        if (take) mixer_->StopChannel(i);
        else      mixer_->CutChannel(i);
    }
    // and the last of the song still queued ahead of the speaker goes
    // too, so the stop lands now rather than a prebuffer later
    if (!take) {
        AudioOut *out = mixer_->GetAudioOut();
        if (out) out->RequestStopDrop();
    }
    for (int i = 0; i < 128; i++) midiHeld_[i] = 0;
    MidiService::GetInstance()->OnPlayerStop();
    mixer_->OnPlayerStop();

    SyncMaster::GetInstance()->Stop();
    isRunning_ = false;
    SetChanged();
	PlayerEvent pe(PET_STOP);
	NotifyObservers(&pe);

	mixer_->Unlock();
}

char *Player::GetPlayedNote(int channel) {
    return mixer_->GetPlayedNote(channel);
}

char *Player::GetPlayedOctive(int channel) {
    return mixer_->GetPlayedOctive(channel);
}

InstrumentType Player::GetChannelInstrumentType(int channel) {
	if (channel<0||channel>=SONG_CHANNEL_COUNT) return IT_LAST ;
	I_Instrument *i=mixer_->GetInstrument(channel) ;
	if (!i) i=mixer_->GetLastInstrument(channel) ;
	return i?i->GetType():IT_LAST ;
}

char *Player::GetPlayedInstrument(int channel) {
	if( (mixer_->GetPlayedOctive(channel))[1] == ' ' ){
		return mixer_->GetPlayedOctive(channel);
	} else {
		if (!IsChannelMuted(channel)) {
			return (char *)(&(instrumentOnChannel_[channel][0]));
		} else {
            return "--";
        }
    }
}

char *Player::GetLiveIndicator(int channel) {

    bool blink = true;

    switch (liveQueueingMode_[channel]) {
    case QM_CHAINSTART:
    case QM_CHAINSTOP:
        blink = (now_ - startClock_) % 500 < 250;
        break;
    case QM_PHRASESTART:
    case QM_PHRASESTOP:
        blink = (now_ - startClock_) % 125 < 72;
        break;
    case QM_TICKSTART:
        blink = (now_ - startClock_) % 75 < 37;
        break;
    case QM_NONE:
        break;
    }
    if (blink) {
		switch (liveQueueingMode_[channel]) {
        case QM_CHAINSTART:
        case QM_PHRASESTART:
        case QM_TICKSTART:
            if (!IsChannelMuted(channel)) {
                return (">");
            } else {
                return ("-");
            }
            break;
        case QM_CHAINSTOP:
        case QM_PHRASESTOP:
            return "_";
            break;
        case QM_NONE:
            break;
        }
    }
    return " ";
}

/********************************************************
 GetQueueSteps:
    How many steps of the current phrase are left before a
    queued channel actually switches.

    The two boundaries are the ones the player itself uses:
    a phrase ends when its position would reach 16, and a
    chain ends when the next slot in it is empty. A tick
    queue lands on the very next step.

    This is a count of steps, not of time: the groove decides
    how long each of them lasts, and a HOP in the phrase can
    end it early. Neither is knowable from here, which is why
    the readout is a step count and not a clock.
 ********************************************************/

int Player::GetQueueSteps(int channel) {

	if (mode_ != PM_LIVE) return -1 ;

	switch (liveQueueingMode_[channel]) {
	case QM_NONE:
		return -1 ;
	case QM_TICKSTART:
		return 0 ;
	default:
		break ;
	}

	// Nothing is playing on the channel, so there is no boundary to
	// wait for -- the queue lands as soon as the player looks at it.

	if (viewData_->currentPlayPhrase_[channel] == 0xFF) return 0 ;

	bool chainBoundary = (liveQueueingMode_[channel] == QM_CHAINSTART) ||
	                     (liveQueueingMode_[channel] == QM_CHAINSTOP) ;

	int chain = viewData_->currentPlayChain_[channel] ;
	unsigned char *data =
	    (chain != 0xFF) ? viewData_->song_->chain_->data_ + 16 * chain : 0 ;

	return LiveQueueSteps(viewData_->phrasePlayPos_[channel],
	                      viewData_->chainPlayPos_[channel], data,
	                      chainBoundary) ;
}

void Player::MidiNoteOn(unsigned char note,unsigned char velocity,bool routed) {

	if (note>127) return ;
	if (velocity==0) {            // running status note-off
		MidiNoteOff(note) ;
		return ;
	}
	if ((!viewData_)||(!viewData_->project_)) return ;
	Project *project=viewData_->project_ ;
	InstrumentBank *bank=project->GetInstrumentBank() ;
	if (!bank) return ;

	/* Which instrument, and at what pitch: the MIDI IN rows on the
	   project screen decide. cursor plays the instrument under the
	   cursor on the instrument screen at the key's pitch, which is how
	   a keyboard always worked here. keys plays the chosen instrument
	   the same way, so the song can be edited while a keyboard plays a
	   fixed synth. kit makes each key from the root pick the next slot
	   up the bank -- the root plays the chosen instrument, a semitone
	   up plays the one after it -- and every slot sounds at its own
	   root note, so sixteen pads play sixteen samples rather than one
	   sample sixteen ways. Keys below the root or past the bank play
	   nothing: GetInstrument would clamp them onto slot 0, which is
	   not a drum anyone asked for. */
	int slot=viewData_->currentInstrument_ ;
	unsigned char playNote=note ;
	switch (routed?project->GetMidiInMode():(int)MIDI_IN_CURSOR) {
		case MIDI_IN_KEYS:
			slot=project->GetMidiInInstrument() ;
			break ;
		case MIDI_IN_KIT: {
			int idx=(int)note-project->GetMidiInRoot() ;
			if (idx<0) return ;
			slot=project->GetMidiInInstrument()+idx ;
			if (slot>=MAX_INSTRUMENT_COUNT) return ;
			// a sample has a root note of its own; a synth or a MIDI
			// instrument has none and plays middle C
			I_Instrument *kitIn=bank->GetInstrument(slot) ;
			Variable *root=kitIn?kitIn->FindVariable(SIP_ROOTNOTE):0 ;
			playNote=(unsigned char)(root?root->GetInt():60) ;
		} break ;
		default:
			break ;
	}
	I_Instrument *instr=bank->GetInstrument(slot) ;
	if (!instr) return ;
	// a MIDI instrument previewed before the first play needs the
	// device up; this brings it up with no transport attached
	if (instr->GetType()==IT_MIDI) MidiService::GetInstance()->EnsureDevice() ;

	/* The lanes past the song's eight, wired straight to the master
	   sum -- no strip fader, mute, filter or send can silence or
	   colour them, and the sequencer never touches them, which is why
	   a keyboard can play over the running song: the gate that used to
	   stand here dated from when the preview borrowed the cursor's song
	   channel. Four lanes, so a pad plays a kick over a hat and a
	   keyboard holds a chord; a fifth key steals the oldest. */
	int lane=allocLane(note) ;
	if (!mixer_->IsChannelPlaying(lane)) {
		mixer_->StartChannel(lane) ;
	}
	/* The key's velocity. For the sampler and the synths it is a gain
	   on the lane, through the project's in vel: 100 follows the key,
	   0 plays every key at full level, and the default sits between,
	   because a phrase note with an empty velocity column is full
	   level and a keyboard on a linear curve sat under the song. A
	   MIDI instrument gets the key's own velocity untouched; the synth
	   on the far end has a curve of its own. */
	int sens=project->GetMidiInVelocity() ;
	float g=1.0f-(sens/100.0f)*(1.0f-velocity/127.0f) ;
	mixer_->SetVelocity(lane,fl2fp(g)) ;
	instr->SetVelocity(lane,velocity) ;
	mixer_->StartInstrument(lane,instr,playNote,true) ;
	midiHeld_[note]=(unsigned char)(lane+1) ;
	laneStamp_[lane]=++laneClock_ ;
} ;

/* A lane for a new key: the key's own lane if it is still held (a pad
   hit again before its release arrived retriggers in place); else a
   lane no held key owns, the one idle longest so a release tail is cut
   as seldom as possible; else the lane with the oldest held note, and
   the keys that were on it are forgotten, or their release would cut
   the note that took their place. */
int Player::allocLane(unsigned char note) {
	if (midiHeld_[note]) return midiHeld_[note]-1 ;
	bool held[PLAYER_CHANNEL_COUNT] ;
	memset(held,0,sizeof(held)) ;
	for (int i=0;i<128;i++) {
		if (midiHeld_[i]) held[midiHeld_[i]-1]=true ;
	}
	int best=-1 ;
	for (int pass=0;(pass<2)&&(best<0);pass++) {
		for (int l=AUDITION_CHANNEL;l<PLAYER_CHANNEL_COUNT;l++) {
			if ((pass==0)&&held[l]) continue ;
			if ((best<0)||(laneStamp_[l]<laneStamp_[best])) best=l ;
		}
	}
	for (int i=0;i<128;i++) {
		if (midiHeld_[i]==best+1) midiHeld_[i]=0 ;
	}
	return best ;
} ;

void Player::MidiNoteOff(unsigned char note) {

	if (note>127) return ;
	int held=midiHeld_[note] ;
	if (held==0) return ;
	midiHeld_[note]=0 ;
	// Tracker channels are monophonic, so a second key steals the
	// first -- and the first key's release must not cut the note that
	// replaced it.
	for (int i=0;i<128;i++) {
		if (midiHeld_[i]==held) return ;
	}
	mixer_->StopInstrument(held-1) ;
} ;

// Hard-release every voice rendering `instr` -- for when the object is
// about to be deleted (a type change). Held-note bookkeeping goes with
// it: those notes' channels no longer play what the map thinks.
void Player::CutInstrument(I_Instrument *instr) {
	mixer_->CutInstrument(instr) ;
	// the table engine held its own pointer to the instrument and
	// called through it on the next tick after the retype freed it
	TablePlayback::CutInstrument(instr) ;
	for (int i=0;i<128;i++) midiHeld_[i]=0 ;
} ;

void Player::MidiAllNotesOff() {
	for (int i=0;i<128;i++) {
		if (midiHeld_[i]) {
			mixer_->StopInstrument(midiHeld_[i]-1) ;
			midiHeld_[i]=0 ;
		}
	}
} ;

void Player::SetSequencerMode(SequencerMode mode) {
    if (isRunning_) {
        switch (mode) {
        case SM_LIVE:
            mode_ = PM_LIVE;
            break;
        case SM_SONG:
            mode_ = PM_SONG;
            break;
        }
    }
    sequencerMode_ = mode;
}

SequencerMode Player::GetSequencerMode() { return sequencerMode_; }

I_Instrument *Player::GetChannelInstrument(int channel) {
    return mixer_->GetInstrument(channel);
}

bool Player::IsChannelPlaying(int channel) {
    return mixer_->IsChannelPlaying(channel);
}

// Handles start button on any screen BUT the song screen

void Player::OnStartButton(PlayMode origin,unsigned int from,bool startFromPrevious,unsigned char chainPos) {

	switch(GetSequencerMode()) {

        case SM_SONG:

			// If sequencer not running, start otherwise stop

			if (isRunning_ && viewData_->playMode_ != PM_AUDITION) {
                Stop();
            } else {
				for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
                    liveQueueingMode_[i] = QM_NONE;
                }
				Start(origin,startFromPrevious);
            }
            break;
		case SM_LIVE: // doesn't make much sense here
            break;
        }
}

// Handles start on song screen
void Player::OnSongStartButton(unsigned int from,unsigned int to,bool requestStop,bool forceImmediate,bool fromSync) {

    /* In Follow the leader owns the transport, so a local press cannot
       mean "start now" -- that would free-run beside the leader
       instead of with it. It means "wait", and the leader's start byte
       is what releases it. Pressing again while waiting gives up.

       Stopping is still immediate and still local: a song you want
       stopped should stop when you say so, leader or no leader. */
    if (!fromSync && !requestStop && !isRunning_ && GetSyncMode()==SYNC_FOLLOW) {
        armed_=!armed_ ;
        return ;
    }
    if (fromSync) {
        armed_=false ;
        /* The leader's start is the one moment the two clocks are known
           to agree, so it is where the loop is zeroed. The project's
           tempo seeds it; the loop corrects from there. */
        clockSync_.Reset((float)project_->GetTempo()) ;
        clockSync_.SetLeadMs(syncLeadMs()) ;
    }

    switch(GetSequencerMode()) {

        case SM_SONG:

			// If sequencer not running, start otherwise stop

			if (isRunning_ && viewData_->playMode_ != PM_AUDITION) {
				if (!forceImmediate) {
                    Stop();
                } else {
					// Get current song row and queue for immediate retrigger
                    retrigPos_ = viewData_->songY_ + viewData_->songOffset_;
                    retrigAllImmediate_=true;
				}
            } else {
                for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        			liveQueueingMode_[i]=QM_NONE;
                }
                Start(PM_SONG,false);
            }
            break;

		case SM_LIVE:

            // Get current song row
            unsigned char songPos=viewData_->songY_+viewData_->songOffset_;

            if (!IsRunning()) {

                // not playing; we queue the chains in the selection
                // that contain something then start the player

                for (unsigned int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                    if ((i<from)||(i>to)) {
						QueueChannel(i,QM_NONE,0);
                    } else {
                        if (isPlayable(songPos,i,0)) {
                            QueueChannel(i, QM_CHAINSTART, songPos, 0);
                        }
                    }
                }

                Start(PM_LIVE, false);

            } else { // Player already running

                // Queue all chain in the given selection

                for (unsigned int i = from; i < to + 1; i++) {

                    QueueingMode mode = QM_NONE;

                    uchar row = songPos;

                    if (!requestStop) {
                        if (findPlayable(&row, i, 0)) {
                            if (!forceImmediate) {
                                if ((liveQueueingMode_[i] != QM_CHAINSTART) ||
                                    (liveQueuePosition_[i] != row)) {
                                    mode = QM_CHAINSTART;
                                } else {
                                    mode = QM_PHRASESTART;
                                }
                            } else {
                                mode = QM_TICKSTART;
                            }
                        }
                    } else { // modifier = onStop from song screen
                        if (GetQueueingMode(i) != QM_CHAINSTOP) {
                            mode = QM_CHAINSTOP;
                        } else {
                            mode = QM_PHRASESTOP;
                        }
                    }
                    if (mode != QM_NONE) {
                        QueueChannel(i, mode, row, 0);
                    }
                }
            }
            break;
        }
}

bool Player::IsArmed() { return armed_ ; }

/* How far ahead of the received clock the song has to run to be
   heard in time with the leader.

   Two parts, and only one of them is knowable from in here.

   The audio buffer is: whatever the player decides now is heard
   bufferSize x preBuffer samples later, which is 35ms at the defaults
   and is the larger half of the problem. That much is measured.

   The rest is not. How long a clock byte takes to cross USB and get
   picked up by the pump, and what the leader's own output latency is,
   are both outside this program. MIDISYNCOFFSET is the trim for them:
   milliseconds, signed, positive to play earlier.

   Read once per start rather than per tick -- it cannot change while
   a song runs, and a config lookup per clock byte would be silly. */
float Player::syncLeadMs() {

	float lead=0.0f ;

	Audio *audio=Audio::GetInstance() ;
	if (audio) {
		int rate=audio->GetSampleRate() ;
		/* ONE fragment, not the whole prebuffer. What we have queued
		   ourselves is measured now and taken out of the phase error
		   directly (ClockSync::SetOutputLag), so counting it here as
		   well would be counting it twice. What is left is the piece
		   nobody can see from in here: the fragment the backend has
		   handed to the hardware. Measured once with a scope against
		   the headphone jack would be better; counted at all beats
		   leaving it to the trim. */
		int frames=audio->GetAudioBufferSize() ;
		if (rate>0 && frames>0) {
			lead=1000.0f*float(frames)/float(rate) ;
		}
	}

	const char *trim=Config::GetInstance()->GetValue("MIDISYNCOFFSET") ;
	if (trim) lead+=float(atoi(trim)) ;

	// Beyond a beat or so this is not a latency any more.
	if (lead<-500.0f) lead=-500.0f ;
	if (lead>500.0f) lead=500.0f ;
	return lead ;
}

void Player::OnMidiClock() {
    // Clocks before the song starts have nothing to be compared
    // against; the count is zeroed at the leader's start anyway.
    if (!isRunning_) return ;
    unsigned long clockMs = System::GetInstance()->GetClock() ;
#ifdef PLATFORM_PSP
    {
        /* The prx stamps each clock byte at the USB completion in
           kernel context -- upstream of every thread wake this side.
           Prefer that time base when it exists; deltas are all the
           loop uses, so the different epoch does not matter. */
        unsigned int us = PSPMidi_LastClockStampUs() ;
        if (us) clockMs = us / 1000u ;
    }
#endif
    clockSync_.OnLeaderTick(clockMs) ;
}

bool Player::IsClockLocked() { return clockSync_.Locked() ; }

Player::SyncMode Player::GetSyncMode() {
    const char *m = Config::GetInstance()->GetValue("MIDISYNCMODE");
    if (!m) return SYNC_LEADER;   // out-of-box: sends clock, as ever
    if (m[0] == 'F' || m[0] == 'f') return SYNC_FOLLOW;
    if (m[0] == 'O' || m[0] == 'o') return SYNC_OFF;
    return SYNC_LEADER;
}

void Player::CancelArm() { armed_=false ; }

bool Player::IsRunning() { return isRunning_; }

bool Player::Clipped() { return mixer_->Clipped(); }

bool Player::isPlayable(int row, int col, int chainPos) {

    uchar *chain = viewData_->song_->data_ + 8 * row + col;
    if (*chain != 0xFF) {
        uchar data = viewData_->song_->chain_->data_[16 * (*chain) + chainPos];
    	return (data!=0xFF);
    }
    return false;
}

bool Player::findPlayable(uchar *row, int col, uchar chainPos) {

    // first look if current is fine

    uchar *chain = viewData_->song_->data_ + 8 * (*row) + col;
    if (*chain != 0xFF) {
        uchar data=viewData_->song_->chain_->data_[16*(*chain)+chainPos];
        return (data != 0xFF);
    }

    // Find upwards the first non blank

    while (*row != 255) {
        chain = viewData_->song_->data_ + 8 * (*row) + col;
        if (*chain!=0xFF) {
			break;
		}
        *row -= 1;
    }

    if (*row == 255)
        return false;

    // Find upwards the first blank

    while (*row != 255) {
        chain=viewData_->song_->data_+8*(*row)+col;
        if (*chain == 0xFF) {
            break;
        }
        *row -= 1;
    }

    if (*row == 255) {
        *row = 0;
    } else {
        *row += 1;
    }
    chain = viewData_->song_->data_ + 8 * (*row) + col;
    uchar data=0xFF;
    if (*chain != 0xFF) {
        data=viewData_->song_->chain_->data_[16*(*chain)+chainPos];
    }
    return (data!=0xFF);
}

QueueingMode Player::GetQueueingMode(int i) { return liveQueueingMode_[i]; }

unsigned char Player::GetQueuePosition(int i) { return liveQueuePosition_[i]; }

unsigned char Player::GetQueueChainPosition(int i) {
    return liveQueueChainPosition_[i];
}

void Player::QueueChannel(int i,QueueingMode mode,unsigned char position,unsigned char chainpos) {
    liveQueueingMode_[i] = mode;
    liveQueuePosition_[i] = position;
    liveQueueChainPosition_[i] = chainpos;
}

/************************************************************
 Update:
    this one gets called when the audio driver has just sent
    a block of audio and we get room to prepare the next one
 ************************************************************/

void Player::Update(Observable &o,I_ObservableData *d) {

    // Make sure sync's ok

    MidiService::GetInstance()->Trigger();
    project_->Trigger();

	if (isRunning_) {
        SyncMaster *sync = SyncMaster::GetInstance();
        /* Following: the loop owns the tempo, and it is a float,
           because whole beats per minute are too coarse a step to hold
           a phase with. Otherwise the project's own tempo stands. */
        bool following = GetSyncMode() == SYNC_FOLLOW;
        if (following) {
            /* Tell the loop how far the render is ahead of the
               speaker right now, so its comparison happens at the
               output. Four bytes a frame, stereo sixteen bit. */
            float lag = 0.0f;
            AudioOut *aout = mixer_->GetAudioOut();
            if (aout) {
                float spt = sync->GetPlaySampleCount();
                if (spt > 1.0f) {
                    lag = float(aout->QueuedBytes()) * 0.25f / spt;
                }
            }
            clockSync_.SetOutputLag(lag);
            sync->SetTempoFine(clockSync_.Tempo());
            // The screen should say what is actually being played, but
            // only settle on a whole number -- writing every wobble of
            // the loop into the project would thrash the display.
            int shown = (int)(clockSync_.Tempo() + 0.5f);
            if (clockSync_.Locked() && shown != project_->GetTempo()) {
                project_->SetTempo(shown);
            }
        } else {
            sync->SetTempo(project_->GetTempo());
        }

        if (!firstPlayCycle_) {
            Groove::GetInstance()->Trigger();
            sync->NextSlice();
            // Our half of the count the loop compares.
            if (following) clockSync_.OnPlayerTick();
            triggerLiveChains_ = false;
            if (retrigAllImmediate_) {
				for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
                    QueueChannel(i, QM_TICKSTART, retrigPos_, 0);
                }
                retrigAllImmediate_=false;
			}
			// Don't advance in audition mode
            if (viewData_->playMode_ != PM_AUDITION)
                moveToNextStep();
			if (triggerLiveChains_) {
                triggerLiveChains();
            }
        }

        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (timeToStart_[i] > 0) {
                if (--timeToStart_[i] == 0) {
                    playCursorPosition(i);
                }
            }
        }

        // Process commands in current phrase
        ProcessCommands();

        // Initialise retrigger table
        int instrRetrigger[SONG_CHANNEL_COUNT];
        memset(instrRetrigger, -1, SONG_CHANNEL_COUNT * sizeof(int));

        // Process any table commands now

        for (int channel = 0; channel < SONG_CHANNEL_COUNT; channel++) {
            TablePlayback &tpb=TablePlayback::GetTablePlayback(channel);
			if (!tpb.GetAutomation()) {
                TablePlayerChange tpc;
                tpc.timeToLive_=timeToLive_[channel];
                tpc.instrRetrigger_ = -1;
                tpb.ProcessStep(tpc);
				timeToLive_[channel]=tpc.timeToLive_;
                instrRetrigger[channel] = tpc.instrRetrigger_;
            }
        }

        // Do we need to kill a voice ?

        if (sync->TableSlice()) {
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                bool stopped = false;
                if (timeToLive_[i] > 0) {
                    if (--timeToLive_[i] == 0) {
                        mixer_->StopInstrument(i);
                        stopped = true;
                    }
                }
                if (!stopped) {
                    applyTableRetrigger(i, instrRetrigger[i]);
                }
            }
        }

        firstPlayCycle_ = false;
        System *system = System::GetInstance();
        now_ = system->GetClock();

        // the song ran out on every channel and "repeat" is "once"
        if (stopAtEnd_) {
            stopAtEnd_ = false;
            Stop();
        }
    }
    // Always notify refresh to enable VU meter dropoff after pause
    PlayerEvent pe(PET_UPDATE, 0);
    SetChanged();
    NotifyObservers(&pe);
}

/************************************************************
 ProcessCommands:
    Check if there's any command to trigger at current playing
    position for all channels
 ************************************************************/

void Player::ProcessCommands() {

    // loop on all channels

    Groove *gs = Groove::GetInstance();

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {

        if (mixer_->IsChannelPlaying(i)) {

            // check if there's any phrase playing

            uchar phrase = viewData_->currentPlayPhrase_[i];
            if (phrase!=0xFF) {
                if (gs->TriggerChannel(
                        i)) { // If groove says it is time to play
                    int pos=viewData_->phrasePlayPos_[i];
                    FourCC cc =
                        viewData_->song_->phrase_->cmd1_[phrase * 16 + pos];
                    ushort param =
                        viewData_->song_->phrase_->param1_[phrase * 16 + pos];

                    // if there's any command to trigger, first pass it on the
                    // player then pass it on to the instrument

                    if (cc != I_CMD_NONE) {
                        if (!ProcessChannelCommand(i, cc, param)) {
                            I_Instrument *instrument = mixer_->GetInstrument(i);
                            if (instrument) {
                                instrument->ProcessCommand(i, cc, param);
                            }
                        }
                    }

                    // Now process second command row

                    cc = viewData_->song_->phrase_->cmd2_[phrase * 16 + pos];
                    param =
                        viewData_->song_->phrase_->param2_[phrase * 16 + pos];

                    // if there's any command to trigger, first pass it on the
                    // player then pass it on to the instrument

                    if (cc != I_CMD_NONE) {
                        if (!ProcessChannelCommand(i,cc,param)) {
                            I_Instrument *instrument = mixer_->GetInstrument(i);
                            if (instrument) {
                                instrument->ProcessCommand(i, cc, param);
                            }
                        }
                    }
                }
            }
        }
    }
}

bool Player::ProcessChannelCommand(int channel, FourCC cmd, ushort param) {

    I_Instrument *instr = mixer_->GetInstrument(channel);

    switch (cmd) {
    case I_CMD_KILL:
        if (instr) {
            int timeToLive = (param & 0xFF);
            timeToLive_[channel] = timeToLive + 1;
        }
        return true;
    case I_CMD_TMPO:
        if ((param < 400) && (param > 40)) {
            Variable *v = project_->FindVariable(VAR_TEMPO);
            v->SetInt(param);
            SyncMaster *sync = SyncMaster::GetInstance();
            sync->SetTempo(project_->GetTempo());
        }
        return true;
        break;
    case I_CMD_TABL: {
        TableHolder *th = TableHolder::GetInstance();
        TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
        param = param & 0x7F;
        Table &table = th->GetTable(param);
        tpb.Start(instr, table, false);
        return true;
        break;
    }
    case I_CMD_GROV: {
        Groove *gr = Groove::GetInstance();
        bool all = (param & 0xFF00) != 0;
        param = param & 0xFF;
        if (all) {
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                gr->SetGroove(i, param);
            }
        } else {
            gr->SetGroove(channel, param);
        }
    } break;
        case I_CMD_STOP: {
            switch (GetSequencerMode()) {
            case SM_SONG:
                Stop();
                break;
            case SM_LIVE:
                //            QueueChannel(channel,QM_CHAINSTOP,0);

                mixer_->StopChannel(channel);
                liveQueueingMode_[channel] = QM_NONE;
                break;
            }
        } break;
        default:
			break;
        }
        return false;
}

/********************************************************
 triggerLiveChains:
        look if there's any chain needed to be started
        i.e. they're queued but on a channel that is not
        yet active
 ********************************************************/

void Player::triggerLiveChains() {

    if (mode_ == PM_LIVE) {
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (!(mixer_->IsChannelPlaying(i)) &&
                ((liveQueueingMode_[i] == QM_CHAINSTART) ||
                 (liveQueueingMode_[i] == QM_TICKSTART) ||
                 (liveQueueingMode_[i] == QM_PHRASESTART))) {
                if (findPlayable(&(liveQueuePosition_[i]), i,
                                 liveQueueChainPosition_[i])) {
                    mixer_->StartChannel(i);
                    updateSongPos(liveQueuePosition_[i], i,
                                  liveQueueChainPosition_[i]);
                }
                liveQueueingMode_[i] = QM_NONE;
            }
        }
    }
}

/********************************************************
 updateSongPos:
    sets current song position for a give channel. the
    last parameter allow to start the song position at a
    give chain position directly (for PM_CHAIN/PM_PHRASE)
 ********************************************************/

void Player::updateSongPos(int pos, int channel, int chainPos, int hop) {
    unsigned char *data = viewData_->song_->data_ + channel + 8 * pos;
    viewData_->songPlayPos_[channel]=pos;
	viewData_->currentPlayChain_[channel]=*data;
    updateChainPos(chainPos, channel, hop);
}

/********************************************************
 updateChainPos:
	sets current chain position for a give channel
 ********************************************************/

void Player::updateChainPos(int pos, int channel, int hop) {
    unsigned char chain=viewData_->currentPlayChain_[channel];
    if (chain != 0xFF) {
        viewData_->chainPlayPos_[channel]=pos;
		unsigned char *data=viewData_->song_->chain_->data_+(16*chain+pos);
		viewData_->currentPlayPhrase_[channel]=*data;
        if (*data == 0xFF) { // This could happen if starting in song mode on a
                             // row where a chain contains no phrase
            mixer_->StopChannel(channel);
        }
    } else {
        viewData_->currentPlayPhrase_[channel] = 0xFF;
        mixer_->StopChannel(channel);
    }
    updatePhrasePos((hop >= 0) ? hop : 0, channel);
}

/********************************************************
 updatePhrasePos:
    sets current phrase position for a give channel.
 ********************************************************/

void Player::updatePhrasePos(int pos, int channel) {

    viewData_->phrasePlayPos_[channel] = pos;

    // See if we need to delay the trigger
    timeToStart_[channel] = 1;

    uchar phrase = viewData_->currentPlayPhrase_[channel];
    // an empty column has no phrase: 0xFF indexed sixteen entries past
    // the phrase arrays on every start
    if (phrase == 0xFF) return;

    // Check both param colum 1 & 2

    FourCC cc = viewData_->song_->phrase_->cmd1_[phrase * 16 + pos];
    if (cc == I_CMD_DLAY) {
        ushort param=viewData_->song_->phrase_->param1_[phrase*16+pos];
        timeToStart_[channel] = (param & 0x0F) + 1;
    }

    cc = viewData_->song_->phrase_->cmd2_[phrase * 16 + pos];
    if (cc == I_CMD_DLAY) {
        // this is the SECOND command column, so it takes the second
        // parameter -- reading param1_ here delayed by an unrelated
        // command's value
        ushort param=viewData_->song_->phrase_->param2_[phrase*16+pos];
        timeToStart_[channel] = (param & 0x0F) + 1;
    }
}

/********************************************************
 rollStepMaybe:
    Does this step's note happen?

    MAYB aa-b: aa is the chance out of FF that the note plays,
    00 never and FF always. A step with no MAYB on it always
    plays, so the command is opt-in and every song written
    before it sounds the same.

    Read here rather than in ProcessCommands because commands
    run after the note has started, and a note that has already
    sounded cannot be taken back. This is the same reason DLAY
    is read up in updatePhrasePos.

    A step that loses its roll is left alone entirely -- the
    instrument is not stopped -- so a skipped step behaves like
    an empty one and whatever was ringing keeps ringing. A
    "maybe" that cut the previous note would be a gate, not a
    maybe.
 ********************************************************/

bool Player::rollStepMaybe(int channel,unsigned char phrase,int pos) {

	Phrase *p=viewData_->song_->phrase_ ;
	int idx=16*phrase+pos ;

	// Either command column can carry it; each takes its own
	// parameter, which is the bug DLAY had here once.
	int chance=-1 ;
	if (p->cmd1_[idx]==I_CMD_MAYB) chance=(p->param1_[idx]>>8)&0xFF ;
	if (p->cmd2_[idx]==I_CMD_MAYB) chance=(p->param2_[idx]>>8)&0xFF ;

	if (chance<0) return true ;     // no MAYB on this step
	return MaybeTake(rng_,chance) ;
}

void Player::playCursorPosition(int channel) {

    int pos = viewData_->phrasePlayPos_[channel];

    // Get chain content and see if instr needs to be started/Stopped
    unsigned char currentPhrase = viewData_->currentPlayPhrase_[channel];

    if (currentPhrase != 0xFF) {

        Song *song = viewData_->song_;
        Phrase *phrase = song->phrase_;
        unsigned char note=phrase->note_[16*currentPhrase+pos] ;
        unsigned char instr = phrase->instr_[16 * currentPhrase + pos];

        TableHolder *th = TableHolder::GetInstance();
        TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);

        // Asked once, before anything is started or stopped.
        bool playNote = (note != 0xFF) && rollStepMaybe(channel, currentPhrase, pos);

        if (playNote) {

            // Stop instrument if playing

            mixer_->StopInstrument(channel);
            InstrumentBank *bank = viewData_->project_->GetInstrumentBank();

            // get instrument for next note

            bool newInstrument = false;

            I_Instrument *instrument;
            if (instr != 0xFF) {
                instrument = bank->GetInstrument(instr);
                newInstrument = true;
            } else {
                instrument = mixer_->GetLastInstrument(channel);
            }

            if (instrument == 0) {
                // no instrument on the row and none remembered for the
                // channel: instrument 0 is a NEW instrument here, so it
                // gets a clean start. Without that the voice kept its
                // old position and retrigger state, which on a channel
                // that had never played was whatever the heap held.
                instrument = bank->GetInstrument(0);
                newInstrument = true;
            }

            if (instrument != 0) {

                int chain = viewData_->currentPlayChain_[channel];
                int chainPos = viewData_->chainPlayPos_[channel];
                unsigned char *trsp = viewData_->song_->chain_->transpose_ +
                                      (16 * chain + chainPos);
                note += *trsp;
                note += project_->GetTranspose();
                instrumentOnChannel_[channel][0] = (instr / 16) > 9
                                                       ? 'A' - 10 + (instr / 16)
                                                       : '0' + (instr / 16);
                instrumentOnChannel_[channel][1] = (instr % 16) > 9
                                                       ? 'A' - 10 + (instr % 16)
                                                       : '0' + (instr % 16);
                instrumentOnChannel_[channel][2] = '\0';

                // Check if note is in acceptable midi range

                if (note < 128) {
                    // Velocity belongs to the NOTE, so it is set before
                    // the note starts and left alone afterwards. An
                    // empty step means "no dynamic written here" and
                    // plays at full -- which is what every song written
                    // before this column existed says on every step.
                    uchar vel =
                        phrase->velocity_[16 * currentPhrase + pos];
                    if (vel == VELOCITY_EMPTY) {
                        mixer_->SetVelocity(channel, i2fp(1));
                        instrument->SetVelocity(channel, 127);
                    } else {
                        if (vel > VELOCITY_FULL) vel = VELOCITY_FULL;
                        mixer_->SetVelocity(
                            channel, fl2fp(float(vel) / float(VELOCITY_FULL)));
                        // the same column, as a MIDI velocity, for the
                        // instrument that makes no sound of its own
                        instrument->SetVelocity(channel,
                                                (vel * 127 + VELOCITY_FULL / 2) / VELOCITY_FULL);
                    }
                    mixer_->StartInstrument(channel, instrument, note,
                                            newInstrument);
                    int instrTable = instrument->GetTable();

                    // If an instrument number has been specified && instrument
                    // has table, we trigger the table.

                    if ((instrTable != VAR_OFF) && (newInstrument)) {
                        Table &table = th->GetTable(instrTable);
                        bool automated = instrument->GetTableAutomation();
                        tpb.Start(instrument, table, automated);
                    } else {
                        // if there was an instrument number, we stop the table
                        if (newInstrument) {
                            tpb.Stop();
                        }
                    }
                } else {
                    Trace::Error("Note outside range: %02x",
                                 (unsigned int)note);
                }
            }
        }
        if ((note != 0xFF) || (instr != 0xFF)) {
            I_Instrument *instrument = mixer_->GetInstrument(channel);
            if (instrument) {
                if (instrument->GetTableAutomation()) {
                    TablePlayerChange tpc;
					tpc.timeToLive_=timeToLive_[channel];
                    tpb.ProcessStep(tpc);
                    timeToLive_[channel]=tpc.timeToLive_;
                    // IRTG was read out of the table here and then
                    // dropped on the floor, so the command did nothing
                    // at all once an instrument had table automation on.
                    applyTableRetrigger(channel, tpc.instrRetrigger_);
                }
            }
        }
    }
}

/* IRTG: restart the sounding note, transposed by the parameter.
   Values above 0x50 read as negative, so bb walks -0x30..+0x50. */
void Player::applyTableRetrigger(int channel, int retrigger) {

    if (retrigger < 0) return;

    int note = mixer_->GetChannelNote(channel);
    I_Instrument *instr = mixer_->GetInstrument(channel);
    if ((note == 0xFF) || (instr == 0)) return;

    note += (retrigger > 80) ? retrigger - 256 : retrigger;
    while (note > 127) {
        note -= 12;
    }
    if (note < 0) return;

    mixer_->StopInstrument(channel);
    mixer_->StartInstrument(channel, instr, note, false);
}

int Player::getChannelHop(int channel, int pos) {

    int phrase = viewData_->currentPlayPhrase_[channel];
    FourCC cc = viewData_->song_->phrase_->cmd1_[phrase * 16 + pos];
    if (cc == I_CMD_HOP) {
        return (viewData_->song_->phrase_->param1_[phrase * 16 + pos]) & 0xF;
    }
    cc = viewData_->song_->phrase_->cmd2_[phrase * 16 + pos];
    if (cc == I_CMD_HOP) {
        return (viewData_->song_->phrase_->param2_[phrase * 16 + pos]) & 0xF;
    }
  return -1;
}

/********************************************************
 moveToNextStep:
    Atomic routine that triggers the next step for all
    playing channels.
 ********************************************************/

void Player::moveToNextStep() {
    // we'll need to know if any channel is playing

    bool playingChannel = false;

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        bool liveTriggered = false;

        switch (liveQueueingMode_[i]) {
        case QM_TICKSTART:
            liveQueueingMode_[i] = QM_NONE;
            if (findPlayable(&(liveQueuePosition_[i]), i,
                             liveQueueChainPosition_[i])) {
                liveTriggered = true;
                updateSongPos(liveQueuePosition_[i], i,
                              liveQueueChainPosition_[i]);
            }
            return;
            break;
        case QM_PHRASESTART:
        case QM_PHRASESTOP:
        case QM_CHAINSTART:
        case QM_CHAINSTOP:
        case QM_NONE:
            break;
        }

        Groove *gs = Groove::GetInstance();

        if (mixer_->IsChannelPlaying(i) && !liveTriggered) {
            playingChannel = true;

            if (gs->TriggerChannel(i)) { // If groove says it is time to play
                if (viewData_->currentPlayPhrase_[i] != 0xFF) {
                    int pos = (viewData_->phrasePlayPos_[i]) + 1;
                    if (pos != 16) {
                        int hop = getChannelHop(i, pos);
                        if (hop >= 0) {
                            if (mode_ != PM_PHRASE) {
                                moveToNextPhrase(i, hop);
                            } else {
                                updatePhrasePos(hop, i);
                            }
                        } else {
                            updatePhrasePos(pos, i);
                        }
                    } else { // HOP.. something should be done so that if
                             // next chain has a hop on pos zero, it is
                             // effective
                        if (mode_ != PM_PHRASE) {
                            moveToNextPhrase(i);
                        } else {
                            // phrase mode loops; optionally follow the
                            // editor cursor to whatever phrase it now
                            // sits on, so play chases you down (M8)
                            if (phraseFollow_ &&
                                viewData_->currentPhrase_ != 0xFF)
                                viewData_->currentPlayPhrase_[i] =
                                    viewData_->currentPhrase_ ;
                            updatePhrasePos(0, i);
                        }
                    }
                }
            }
        }
    }
    // if no channel is playing we allow straight
    // queueing of chains
    if (!playingChannel) {
        triggerLiveChains_ = true;
    }
}

/********************************************************
 moveToNextPhrase:
    Compute what is the next phrase to be triggered on
    the selected channel. Called when the current phrase
    has reached the end
 ********************************************************/

void Player::moveToNextPhrase(int channel, int hop) {

    // First check if we're in live mode and the channel
    // has been trigged in immediate mode. In which case we
    // do the action straight away (START/STOP)

    if (mode_==PM_LIVE) {

        switch (liveQueueingMode_[channel]) {
        case QM_TICKSTART:
        case QM_PHRASESTART:
            if (findPlayable(&(liveQueuePosition_[channel]), channel,
                             liveQueueChainPosition_[channel])) {
                updateSongPos(liveQueuePosition_[channel], channel,
                              liveQueueChainPosition_[channel], hop);
            }
            liveQueueingMode_[channel] = QM_NONE;
            return;
            break;
        case QM_PHRASESTOP:
            mixer_->StopChannel(channel);
            liveQueueingMode_[channel] = QM_NONE;
            return;
        case QM_CHAINSTART:
        case QM_CHAINSTOP:
        case QM_NONE:
            break;
        }
    }

    // If nothing has been triggered, we need to find what is
    // The next phrase to play

    int chain = viewData_->currentPlayChain_[channel];
    int pos = (viewData_->chainPlayPos_[channel]) + 1;

    // Look if there' any data at current position
    // which means we continue in the current chain

    bool canContinue = (pos < 16);
    if (canContinue) {
        unsigned char *data =
            viewData_->song_->chain_->data_ + (16 * chain + pos);
        canContinue = (*data != 0xFF);
    }

    // If so, we trigger it. Otherwise, we go to the next phrase

    if (canContinue) {
        updateChainPos(pos, channel, hop);
    } else { // Should move to next chain
        if ((mode_==PM_SONG)||(mode_==PM_LIVE)) {
            moveToNextChain(channel, hop); // HOP. here
        } else {
            updateChainPos(0, channel, hop);
        }
    }
}

/********************************************************
 moveToNextChain:
    Compute what is the next chain to be triggered on
    the selected channel. Called when the current chain
    has reached the end.
 ********************************************************/

void Player::moveToNextChain(int channel, int hop) {

    // if there's unplaying channels queue they should be started
    // once all position have been updated

    triggerLiveChains_ = true;

    bool searchNext = true;
    int nextPos=0;
    int chainPosition=0;

    // Hop here ?

    // See if current channel has been queued to play something
    // in normal mode

    if (mode_ == PM_LIVE) {
        switch (liveQueueingMode_[channel]) {

        case QM_CHAINSTART:
        case QM_PHRASESTART:

            if (findPlayable(&(liveQueuePosition_[channel]), channel,
                             liveQueueChainPosition_[channel])) {
                nextPos = liveQueuePosition_[channel];
                searchNext = false;
                liveQueueingMode_[channel] = QM_NONE;
                chainPosition = liveQueueChainPosition_[channel];
            } else {
                liveQueueingMode_[channel] = QM_NONE;
            }
            break;

        case QM_CHAINSTOP:
        case QM_PHRASESTOP:
            mixer_->StopChannel(channel);
            liveQueueingMode_[channel] = QM_NONE;
            return;

        case QM_NONE:
            break;
        }
    }

    // if live mode didn't queue anything, we find the next to play

    if (searchNext) {
        int pos = (viewData_->songPlayPos_[channel]) + 1;
        // row 256 does not exist: a chain on the last row ends the
        // song, and reading past it was reading the next heap block
        bool loopBack=(pos>=SONG_ROW_COUNT);
        // the pointer stays on row pos even when that row does not
        // exist: the loop-back walk below steps it down first, and
        // only a real row is ever read
        unsigned char *data=viewData_->song_->data_+channel+8*pos;
        if (!loopBack) loopBack=(*data==0xFF);
        // Check if first step of chain contains somethin, if not we loop back
        if (!loopBack) {
            unsigned char step = viewData_->song_->chain_->data_[*data * 16];
            loopBack = (step == 0xFF);
        }
        if (loopBack && mode_ != PM_LIVE && project_ && !project_->Loop()) {
            // "repeat once": the song has run out of chains on this
            // channel and is not going back to the top. Stop this
            // channel and let the others finish their own bars --
            // stopping the whole player on the first channel to run
            // dry would cut the end off any part that had more to
            // play. When the last one goes quiet the player stops
            // itself.
            mixer_->StopChannel(channel);
            songPlayed_[channel] = true;
            bool allDone = true;
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++)
                if (!songPlayed_[i]) { allDone = false; break; }
            // Ask for the stop, do not take it here: this runs part
            // way through the per-channel loop of a tick, and Stop()
            // clears isRunning_, which the rest of that tick is still
            // reading.
            if (allDone) stopAtEnd_ = true;
            return;
        }
        if (loopBack) {
            data -= 8;
            pos--;
            while (pos >= 0) {
                if (*data == 0xFF) { // we stop searching if there's a blank
                    break;
                } else { // Or if first phrase of chain is empty
                    if (viewData_->song_->chain_->data_[(*data) * 16] == 0xFF) {
                        break;
                    }
                }
                if (pos != 0)
                    data -= 8;
                pos--;
            }
                    pos++;
        }
        nextPos = pos;
    }
    // Do a last check in case we had only one chain and it go destroyed

    if (isPlayable(nextPos, channel, chainPosition)) {
        updateSongPos(nextPos, channel, chainPosition, hop);
    } else {
        mixer_->StopChannel(channel);
    }
}

double Player::GetPlayTime() {
    AudioOut *out = mixer_->GetAudioOut();
    double currentTime = out ? out->GetStreamTime() : 0;
    if (isRunning_) {
        currentTime_ = currentTime - startTime_;
    }
    return currentTime_;
}

int Player::GetPlayedBufferPercentage() {
    unsigned int beatCount = SyncMaster::GetInstance()->GetBeatCount();
    if (beatCount != lastBeatCount_) {
        lastBeatCount_ = beatCount;
        lastPercentage_ = mixer_->GetPlayedBufferPercentage();
    }
    return lastPercentage_;
}

PlayerEvent::PlayerEvent(PlayerEventType type, unsigned int tickCount)
    : ViewEvent(VET_PLAYER_POSITION_UPDATE) {
    type_ = type;
    tickCount_ = tickCount;
}

PlayerEventType PlayerEvent::GetType() { return type_; }

unsigned int PlayerEvent::GetTickCount() { return tickCount_; }

bool Player::StartStreaming(const Path &path) { return mixer_->StartStreaming(path); }
void Player::StopStreaming() { mixer_->StopStreaming(); }
bool Player::StartStreamingBuffer(const short *frames,long frameCount,int channels,int rate) {
	return mixer_->StartStreamingBuffer(frames,frameCount,channels,rate);
}
void Player::StopStreamingNow() { mixer_->StopStreamingNow(); }
bool Player::IsStreaming() { return mixer_->IsStreaming(); }
void Player::SetStreamingShape(bool mono,int div) { mixer_->SetStreamingShape(mono,div); }

std::string Player::GetAudioAPI() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out)?out->GetAudioAPI():"";
}

std::string Player::GetAudioDevice() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out) ? out->GetAudioDevice() : "";
}

int Player::GetAudioBufferSize() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out) ? out->GetAudioBufferSize() : 0;
}

int Player::GetAudioRequestedBufferSize() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out)?out->GetAudioRequestedBufferSize():0;
}

int Player::GetAudioPreBufferCount() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out) ? out->GetAudioPreBufferCount() : 0;
}
