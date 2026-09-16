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

// the lanes' late render: a chunk is at most the audio buffer size,
// which the config caps at 4096 frames
#define LANE_MAX_FRAMES 4096
#define LANE_MAX_FIXED i2fp(32767)
#define LANE_MIN_FIXED i2fp(-32768)
// 16-byte aligned: the engines' VFPU paths load and store quads
static fixed laneSum_[LANE_MAX_FRAMES*2] __attribute__((aligned(16))) ;
static fixed laneTmp_[LANE_MAX_FRAMES*2] __attribute__((aligned(16))) ;

PlayerMixer::PlayerMixer() {

    laneSync_=SDL_CreateMutex() ;
    lanesWired_=false ;
    laneTickAcc_=0.0f ;
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

#ifdef PSP_LATE_LANES
	// the lanes render at the output callback from here on
	if (AudioOut *out=GetAudioOut()) out->SetLateRender(this) ;
#endif

	// Init states

	for (int i=0;i<PLAYER_CHANNEL_COUNT;i++) {
        lastInstrument_[i]=0 ;
	} ;

	clipped_=false ;
	return true ;
} ;

void PlayerMixer::Close()  {

	// the output thread must be out of the lanes before they are
	// reset and the instruments they point at are freed
	if (AudioOut *out=GetAudioOut()) out->SetLateRender(0) ;
	LaneLock() ;
	for (int i=0;i<PLAYER_CHANNEL_COUNT;i++) {
		channel_[i]->Reset() ;
	}
	LaneUnlock() ;
	lanesWired_=false ;


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
	// under the lane lock: the object is about to be deleted, and the
	// output thread may be inside a lane that is rendering it
	LaneLock() ;
	for (int i=0;i<PLAYER_CHANNEL_COUNT;i++) channel_[i]->CutIfPlaying(instr) ;
	LaneUnlock() ;
}

void PlayerMixer::StopChannel(int channel) {

    StopInstrument(channel) ;
	isChannelPlaying_[channel]=false ;
} ;

void PlayerMixer::CutChannel(int channel) {
	bool lane=(channel>=AUDITION_CHANNEL) ;
	if (lane) LaneLock() ;
	channel_[channel]->Cut() ;
	notes_[channel]=0xFF ;
	isChannelPlaying_[channel]=false ;
	if (lane) LaneUnlock() ;
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
      channel_[i]->SetDist(mixer->GetChannelDist(i),
                           mixer->GetChannelDistEdge(i),
                           mixer->GetChannelDistTone(i),
                           mixer->GetChannelDistGate(i));
  }

  /* The lanes: the audition preview and the MIDI keyboard. A preview
     asked for by name must be heard as the instrument actually sounds,
     so their wiring is fixed and lives outside the project mixer: full
     volume, filters off, dry, no inserts. Once, under the lane lock,
     because on the PSP the output thread renders them (RenderLate) and
     a bus insert is a list edit. Where they render with everything
     else they share one spare bus into the master sum. */
  if (!lanesWired_) {
    LaneLock() ;
    for (int l=AUDITION_CHANNEL;l<PLAYER_CHANNEL_COUNT;l++) {
      PlayerChannel *lane=channel_[l];
#ifndef PSP_LATE_LANES
      lane->SetMixBus(AUDITION_BUS);
#endif
      lane->SetVolume(fl2fp(1.0f));
      lane->SetHPFMode(0);
      lane->SetLPFFreq(0);
      lane->SetSends(0,0);
      lane->SetInserts(0,0,0,0);
      lane->SetDist(0,0,0,0);
    }
    lanesWired_=true ;
    LaneUnlock() ;
  }
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
	bool lane=(channel>=AUDITION_CHANNEL) ;
	if (lane) LaneLock() ;
	channel_[channel]->SetVelocity(v) ;
	if (lane) LaneUnlock() ;
}

void PlayerMixer::StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument)  {
	// a note played into a render tail is not part of the take
	MixerService::GetInstance()->EndRenderTail() ;
	// a lane is the output thread's while it renders -- see LaneLock
	bool lane=(channel>=AUDITION_CHANNEL) ;
	if (lane) LaneLock() ;
	channel_[channel]->StartInstrument(instrument,note,newInstrument) ;
	lastInstrument_[channel]=instrument ;
	notes_[channel]=note ;
	if (lane) LaneUnlock() ;
} ;

void PlayerMixer::StopInstrument(int channel) {
	bool lane=(channel>=AUDITION_CHANNEL) ;
	if (lane) LaneLock() ;
    channel_[channel]->StopInstrument() ;
    notes_[channel]=0xFF ;
	if (lane) LaneUnlock() ;
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

void PlayerMixer::LaneLock() {
	if (laneSync_) SDL_LockMutex(laneSync_) ;
}

void PlayerMixer::LaneUnlock() {
	if (laneSync_) SDL_UnlockMutex(laneSync_) ;
}

/* THE LANES, LATE.

   Runs on the output thread, on every chunk, just before the chunk
   goes to the device. The render thread renders the song into a queue
   of tempo slices, prebuffer-count deep, and that depth is what keeps
   a heavy block or a busy repaint from turning into a dropout; it is
   also ninety milliseconds at 138 bpm between a key and the speaker,
   which is what a keyboard player hears as lag. The lanes skip the
   queue: rendered here, they are a chunk or two from the DAC whatever
   the prebuffer is set to, and the song keeps its whole margin.

   What it costs: up to four voices' worth of rendering on the device
   thread per chunk, a fraction of the chunk. What it changes: the
   lanes no longer pass through the master's EQ and clipper, and a
   keyboard played over a take is not in the WAV. What it must never
   do is wait on the render thread: it holds the lane lock only, and
   nothing that holds the mixer lock takes the lane lock inside a
   slice render.

   The tick clock. The render thread hands an instrument one tick per
   Render call because its block IS a tick -- a slice -- and the MIDI
   instrument counts its note length, arpeggio and vibrato per call.
   Here a call is a chunk, six or seven to a slice at 128 frames, so
   the tick is kept by counting frames and the lanes are told when it
   falls: the channel takes it as its table slice, the MIDI
   instrument as its tick. */
void PlayerMixer::RenderLate(short *out,int frames) {
	if (frames<=0) return ;
	if (frames>LANE_MAX_FRAMES) frames=LANE_MAX_FRAMES ;

	float slice=SyncMaster::GetInstance()->GetPlaySampleCount() ;
	if (slice<64.0f) slice=64.0f ;
	laneTickAcc_+=(float)frames ;
	bool tick=false ;
	if (laneTickAcc_>=slice) {
		laneTickAcc_-=slice ;
		tick=true ;
		// a tempo change mid-hold: never owe more than one tick
		if (laneTickAcc_>=slice) laneTickAcc_=0.0f ;
	}

	bool got=false ;
	LaneLock() ;
	for (int l=AUDITION_CHANNEL;l<PLAYER_CHANNEL_COUNT;l++) {
		PlayerChannel *lane=channel_[l] ;
		I_Instrument *in=lane->GetInstrument() ;
		if (in) in->SetLaneTick(l,tick) ;
		lane->SetTickOverride(tick?1:0) ;
		fixed *dst=got?laneTmp_:laneSum_ ;
		if (!lane->Render(dst,frames)) continue ;
		if (got) {
			// sum, saturating -- AudioMixer::Render says why not wrap
			for (int i=0;i<frames*2;i++) {
				fixed v=laneSum_[i]+laneTmp_[i] ;
				if (v>LANE_MAX_FIXED) v=LANE_MAX_FIXED ;
				else if (v<LANE_MIN_FIXED) v=LANE_MIN_FIXED ;
				laneSum_[i]=v ;
			}
		}
		got=true ;
	}
	LaneUnlock() ;
	if (!got) return ;

	/* Into the chunk at the level the lanes had through the master:
	   the MIX panel's drive, which every song bus carries, and the
	   fader's fourth-power taper, which the master applies to every
	   source on the way into its sum. Not the EQ and not the clipper
	   -- a preview is meant to be the instrument as it is. */
	MixerService *ms=MixerService::GetInstance() ;
	fixed g=fp_mul(ms->GetMasterPreSumGain(),ms->GetPregainGain()) ;
	for (int i=0;i<frames*2;i++) {
		fixed v=laneSum_[i] ;
		if (g!=FP_ONE) v=fp_mul(v,g) ;
		int o=(int)out[i]+fp2i(v) ;
		if (o>32767) o=32767 ; else if (o<-32768) o=-32768 ;
		out[i]=(short)o ;
	}
}

void PlayerMixer::Unlock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Unlock() ;
};
