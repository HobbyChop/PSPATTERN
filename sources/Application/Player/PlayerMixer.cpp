#include "PlayerMixer.h"
#include "Services/Audio/SendFx.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Utils/char.h"
#include "Application/Utils/fixed.h"
#include "Services/Midi/MidiService.h"
#include "SyncMaster.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>
#include <stdlib.h>
#include "Services/Audio/MasterEq.h"

PlayerMixer::PlayerMixer() {

    for (int i = 0; i < PLAYER_CHANNEL_COUNT; i++) {
        lastInstrument_[i] = 0;
		channel_[i] = new PlayerChannel(i);
		isChannelPlaying_[i] = false;
    }
}

bool PlayerMixer::Init(Project *project) {

	MixerService *ms=MixerService::GetInstance() ;
	if (!ms->Init()) {
			return false ;
	}

	AudioMixer *mixer=ms->GetMixBus(STREAM_MIX_BUS) ;
	mixer->Insert(fileStreamer_) ;

	project_=project ;

	// Init states

	for (int i=0;i<PLAYER_CHANNEL_COUNT;i++) {
        lastInstrument_[i]=0 ;
	} ;

	clipped_=false ;
	return true ;
} ;

void PlayerMixer::Close()  {

	for (int i=0;i<PLAYER_CHANNEL_COUNT;i++) {
		channel_[i]->Reset() ;
	}


	MixerService *ms=MixerService::GetInstance() ;
	ms->Close() ;

}

bool PlayerMixer::Start() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->AddObserver(*this) ;

	for (int i=0;i<PLAYER_CHANNEL_COUNT;i++) {
        notes_[i]=0xFF ;
    } ;

	return ms->Start() ;
} ;

void PlayerMixer::Stop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Stop() ;
	ms->RemoveObserver(*this) ;
} ;

void PlayerMixer::StartChannel(int channel) {
	isChannelPlaying_[channel]=true ;
} ;

void PlayerMixer::CutInstrument(I_Instrument *instr) {
	for (int i=0;i<PLAYER_CHANNEL_COUNT;i++) channel_[i]->CutIfPlaying(instr) ;
}

void PlayerMixer::StopChannel(int channel) {

    StopInstrument(channel) ;
	isChannelPlaying_[channel]=false ;
} ;


bool PlayerMixer::IsChannelPlaying(int channel) {
	return isChannelPlaying_[channel] ;
} ;

I_Instrument *PlayerMixer::GetLastInstrument(int channel) {
	// This is a raw pointer to something the bank owns, and the bank
	// deletes and replaces instruments behind our back -- on project
	// load, and whenever an instrument changes type. Nothing tells us,
	// so the cached pointer can be freed memory by the time a row with
	// a note but no instrument number reaches for it. Only hand back a
	// pointer the bank still owns.
	I_Instrument *last=lastInstrument_[channel] ;
	if ((!last)||(!project_)) return 0 ;
	InstrumentBank *bank=project_->GetInstrumentBank() ;
	if (bank) {
		for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
			if (bank->GetInstrument(i)==last) return last ;
		} ;
	} ;
	lastInstrument_[channel]=0 ;
	return 0 ;
} ;


bool PlayerMixer::Clipped() {
     return clipped_ ;
}

void PlayerMixer::Update(Observable &o,I_ObservableData *d) {

  // Notifies the player so that pattern data is processed
  SetChanged();
  NotifyObservers();

  // Transfer the mixer data
  Mixer *mixer = Mixer::GetInstance();

  for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
      channel_[i]->SetMixBus(mixer->GetBus(i));
      channel_[i]->SetVolume(fl2fp(mixer->GetChannelVolume(i) / 255.0f));
      channel_[i]->SetHPFMode((unsigned char)mixer->GetChannelHPF(i));
      channel_[i]->SetLPFFreq(mixer->GetChannelLPF(i));
      channel_[i]->SetSends(mixer->GetChannelDelaySend(i),
                            mixer->GetChannelReverbSend(i));
      channel_[i]->SetInserts(mixer->GetChannelPhaserRate(i),
                              mixer->GetChannelPhaserDepth(i),
                              mixer->GetChannelChorusRate(i),
                              mixer->GetChannelChorusDepth(i));
  }

  /* The audition lane. A preview asked for by name must be heard as
     the instrument actually sounds, so its wiring is fixed and lives
     outside the project mixer: full volume, filters off, dry, no
     inserts, its own spare bus into the master sum. Every setter
     early-returns once the value holds, so this costs nothing per
     block. */
  PlayerChannel *audition=channel_[AUDITION_CHANNEL];
  audition->SetMixBus(AUDITION_BUS);
  audition->SetVolume(fl2fp(1.0f));
  audition->SetHPFMode(0);
  audition->SetLPFFreq(0);
  audition->SetSends(0,0);
  audition->SetInserts(0,0,0,0);
  MixerService *ms=MixerService::GetInstance();
  // the two effects themselves, and the tempo the delay locks to
  ms->SetSendFxParams(mixer->GetDelayDivision(),mixer->GetDelayFeedback(),
                      mixer->GetReverbSize(),mixer->GetReverbDamp());
  ms->SetSendFx2(mixer->GetReverbFreeze(),mixer->GetDrive(),
                 mixer->GetReverbDuck(),mixer->GetReverbGate(),
                 mixer->GetComp(),mixer->GetReverbLowcut(),
                 mixer->GetReverbWidth(),mixer->GetDelayTone());
  SendFx::SetTempo(project_->GetTempo());
  ms->SetPregain(project_->GetPregain());
  ms->SetSoftclip(project_->GetSoftclip(), project_->GetSoftclipGain());
  ms->SetMasterVolume(project_->GetMasterVolume());
  /* Pushed every tick like the rest of the master settings. Cheap:
     SetBand only recomputes whether ANY band is off flat, and the EQ
     itself does nothing at all until one is. */
  for (int b = 0; b < MASTER_EQ_BANDS; b++) {
      ms->SetEqBand(b, project_->GetEqBand(b));
  }
  clipped_=ms->Clipped();
} ;


void PlayerMixer::SetVelocity(int channel,fixed v) {
	if ((channel<0)||(channel>=PLAYER_CHANNEL_COUNT)) return ;
	channel_[channel]->SetVelocity(v) ;
}

void PlayerMixer::StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument)  {
	// a note played into a render tail is not part of the take
	MixerService::GetInstance()->EndRenderTail() ;
	channel_[channel]->StartInstrument(instrument,note,newInstrument) ;
	lastInstrument_[channel]=instrument ;
	notes_[channel]=note ;

} ;

void PlayerMixer::StopInstrument(int channel) {
    channel_[channel]->StopInstrument() ;
    notes_[channel]=0xFF ;
}

I_Instrument *PlayerMixer::GetInstrument(int channel) {
    return channel_[channel]->GetInstrument();
}

int PlayerMixer::GetPlayedBufferPercentage() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetPlayedBufferPercentage() ;
};

void PlayerMixer::SetChannelMute(int channel,bool mode) {
     channel_[channel]->SetMute(mode) ;
}

bool PlayerMixer::IsChannelMuted(int channel) {
     return channel_[channel]->IsMuted() ;
}

bool PlayerMixer::StartStreaming(const Path &path) {
	return fileStreamer_.Start(path) ;
} ;

void PlayerMixer::StopStreaming() {
	fileStreamer_.Stop() ;
} ;

bool PlayerMixer::StartStreamingBuffer(const short *frames,long frameCount,int channels,int rate) {
	return fileStreamer_.StartBuffer(frames,frameCount,channels,rate) ;
} ;

void PlayerMixer::StopStreamingNow() {
	fileStreamer_.StopNow() ;
} ;

bool PlayerMixer::IsStreaming() {
	return fileStreamer_.IsPlaying() ;
} ;

void PlayerMixer::SetStreamingShape(bool mono,int div) {
	fileStreamer_.SetShape(mono,div) ;
} ;

void PlayerMixer::OnPlayerStart() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStart();
}

void PlayerMixer::OnPlayerStop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStop();
	// a stopped song ends whatever a command left running on an
	// instrument -- the free LFO -- so an audition afterwards is clean
	if (project_) {
		InstrumentBank *bank=project_->GetInstrumentBank() ;
		for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
			I_Instrument *in=bank->GetInstrument(i) ;
			if (in) in->OnStop() ;
		}
	}
}

static char noteBuffer[5] ;

int PlayerMixer::GetChannelNote(int channel) {
	return notes_[channel] ;
}

char *PlayerMixer::GetPlayedNote(int channel) {

    if (notes_[channel]!=0xFF) {
		note2visualizer(notes_[channel],noteBuffer) ; 
		return noteBuffer ;
    }
    return "  " ;
} ;

char *PlayerMixer::GetPlayedOctive(int channel) {
    if (notes_[channel]!=0xFF) {
		if (!IsChannelMuted(channel)) {
	        oct2visualizer(notes_[channel],noteBuffer) ; 
	        return noteBuffer ;
		} else {
			return "--" ;
		}
    }
    return "  " ;
} ;

AudioOut *PlayerMixer::GetAudioOut() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetAudioOut();
} ;

void PlayerMixer::Lock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
} ;

void PlayerMixer::Unlock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Unlock() ;
};
