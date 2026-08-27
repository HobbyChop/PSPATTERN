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

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
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

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        lastInstrument_[i]=0 ;
	} ;

	clipped_=false ;
	return true ;
} ;

void PlayerMixer::Close()  {

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channel_[i]->Reset() ;
	}


	MixerService *ms=MixerService::GetInstance() ;
	ms->Close() ;

}

bool PlayerMixer::Start() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->AddObserver(*this) ;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
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
  }
  MixerService *ms=MixerService::GetInstance();
  // the two effects themselves, and the tempo the delay locks to
  ms->SetSendFxParams(mixer->GetDelayDivision(),mixer->GetDelayFeedback(),
                      mixer->GetReverbSize(),mixer->GetReverbDamp());
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
	if ((channel<0)||(channel>=SONG_CHANNEL_COUNT)) return ;
	channel_[channel]->SetVelocity(v) ;
}

void PlayerMixer::StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument)  {
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

void PlayerMixer::StartStreaming(const Path &path) {
	fileStreamer_.Start(path) ;
} ;

void PlayerMixer::StopStreaming() {
	fileStreamer_.Stop() ;
} ;

void PlayerMixer::OnPlayerStart() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStart();
}

void PlayerMixer::OnPlayerStop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStop();
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
