#include "MixerService.h"
#include "Application/Audio/DummyAudioOut.h"
#include "Application/Model/Config.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Project.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include <math.h>

MixerService::MixerService() : out_(0), sync_(0), isRendering_(false) {
    mode_ = MSRM_PLAYBACK;
};

MixerService::~MixerService(){};

/*
 * initializes the mixer service, config changes depending if we're in sequencer or render mode
 */
bool MixerService::Init() {
    // create the output depending on rendering mode
    out_ = 0;
	switch (mode_) {
    case MSRM_STEREO:
        out_ = new DummyAudioOut();
        break;
    default:
        Audio *audio = Audio::GetInstance();
        out_ = audio->GetFirst();
        break;
	}

	for (int i=0;i<MAX_BUS_COUNT;i++) {
		master_.Insert(bus_[i]);
	}
	// Insert appends, and the master renders its children in order,
	// so the return runs after every bus has had a chance to feed it.
	SendFx::Init(44100);

	/* Run the delay and reverb a block behind, on this core, with
	   nothing else changed. That is exactly the timing a second CPU
	   doing the work would impose, without the second CPU -- so if a
	   song sounds right with this on, the protocol is not what to
	   suspect when the engine goes in, and if it sounds wrong, the
	   engine was never the problem. Two variables landing together is
	   how a week gets spent debugging the wrong one.

	   Read here rather than inside SendFx, which is a DSP module and
	   whose test should not have to link the filesystem to run. */
	const char *deferFx = Config::GetInstance()->GetValue("DEFERREDFX") ;
	if (deferFx && deferFx[0]=='1') SendFx::SetDeferred(true) ;
	master_.Insert(sendReturn_);
	// only the master counts saturation; the buses run the plain loop
	master_.SetTrackRawSum(true);

	bool result = false;
	if (out_) {
		result = out_->Init();
		if (result) {
			out_->Insert(master_);
		}

        initRendering(mode_);
        out_->AddObserver(*MidiService::GetInstance());
	}

	sync_=SDL_CreateMutex();
	NAssert(sync_);

	if (result) {
		Trace::Log("MixerService", "output initialized");
	} else {
		Trace::Log("MixerService", "failed to initialize output");
	}
	return (result);
};

void MixerService::initRendering(MixerServiceRenderMode mode) {
    switch(mode) {
    case MSRM_PLAYBACK:
        break;
    case MSRM_STEREO:
        out_->SetFileRenderer("project:mixdown.wav");
        break;
    }
}

void MixerService::Close() {
	if (out_) {
    out_->RemoveObserver(*MidiService::GetInstance());
		out_->Close() ;
		out_->Empty() ;
		master_.Empty() ;
		SendFx::Close() ;

    }
   for (int i=0;i<MAX_BUS_COUNT;i++) {
	   bus_[i].Empty() ;
   }
	out_=0 ;
	SDL_DestroyMutex(sync_) ;
	sync_=0 ;
} ;

void MixerService::SetRenderMode(int mode) {
    mode_ = MixerServiceRenderMode(mode);
}

bool MixerService::IsRendering() { return isRendering_; }

bool MixerService::Start() {
    MidiService::GetInstance()->Start();
    if (out_) {
        out_->AddObserver(*this);
        out_->Start();
     }
	return true ;
} ;

void MixerService::Stop() {
	MidiService::GetInstance()->Stop() ;
     if (out_) {
      out_->Stop() ;
      out_->RemoveObserver(*this) ;
     }
}

MixBus *MixerService::GetMixBus(int i) {
    // the index is a channel's bus setting, which is saved in the
    // project file and so is whatever the file says
    if ((i < 0) || (i >= MAX_BUS_COUNT)) return 0;
    return &(bus_[i]);
}

unsigned int MixerService::GetMasterPeakLevel() const {
    return master_.GetOutputPeakLevel();
}

/* How much of the last block the master sum spent held at the rail,
   0..100.

   This replaces a figure that claimed to be "the bus sum before the
   fader". It was wrong in two ways at once. It read a peak taken after
   the sum, so when the fader moved to a pre-sum gain it silently began
   including the fader and reported the opposite of what it promised.
   And the accumulator clamps every partial add, so the value it read
   could never exceed full scale -- the readout could only ever print
   1.00x however hot the mix was, while its own header said "a normal
   mix runs over 1.0 here".

   How far over the top a mix went is not recoverable once the sum has
   been clamped. How OFTEN it is clamped is, and it answers the same
   question honestly: 0 means the mix fits, anything climbing means it
   does not and the fader has work to do. */
unsigned int MixerService::GetSaturationPercent() {
    unsigned int hits = master_.GetSaturatedSamples();
    unsigned int total = master_.GetSaturatedTotal();
    if (!total) return 0;
    if (hits > total) hits = total;
    return (hits * 100) / total;
}

bool MixerService::TakeMasterClipLatch() {
    return master_.TakeClipLatch();
}

void MixerService::Update(Observable &o, I_ObservableData *d) {
    AudioDriver::Event *event = (AudioDriver::Event *)d;
    if (event->type_ == AudioDriver::Event::ADET_BUFFERNEEDED) {
        Lock();
        SetChanged();
        NotifyObservers();

        out_->Trigger();
        Unlock();
    }
}

bool MixerService::Clipped() {
     // master_ is where the fader and the clipper live, so it is the
     // only place that knows whether the OUTPUT clipped
     return master_.Clipped() ;
} ;

void MixerService::SetPregain(int vol) {
    // i2fp(vol)/100, not fp_mul by fl2fp(0.01): the float round trip
    // lands 32700 for vol==100 instead of 32768, which defeated the
    // unity skip in AudioMixer -- every bus paid a full gain pass for
    // a -0.018dB "gain" at the default setting.
    fixed masterVolume = i2fp(vol) / 100;

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        bus_[i].SetVolume(masterVolume);
  }
};

// The fader and the clipper both belong to master_, which is where
// the buses are summed -- NOT to out_, which sees master_'s output
// after it has already been limited.
//
// With them on out_, the chain was: sum the buses (a normal mix runs
// a couple of times over full scale here), hard clip that to full
// scale, and only then apply the master fader. The distortion was
// baked in one stage before the only control that could have
// prevented it, which is why pulling the master down changed how
// loud the clipping was without removing any of it, and why turning
// individual channels down was the only thing that worked.
void MixerService::SetSoftclip(int clip, int gain) {
    master_.SetSoftclip(clip, gain);
}

void MixerService::SetEqBand(int band, int value) {
    // Only the master carries the EQ. The buses each own an inert one.
    master_.SetEqBand(band, value);
}

void MixerService::SetMasterVolume(int attn) {
    master_.SetMasterVolume(attn);
    /* Also pushed in as a PRE-sum gain, which is where a master fader
       has to act here. The buses are summed into a 32 bit accumulator
       that saturates once two full scale sources are in it, so a fader
       applied after the sum is attenuating audio that has already been
       flat-topped -- it changes how loud the damage is and removes
       none of it. Applied on the way in, it buys real headroom.

       Same fourth-power taper the fader always had, so the control
       feels exactly as it did. The send return is a child of the
       master like any bus, so it scales with them and the wet/dry
       balance holds. */
    float damp = powf((float)attn / 100.0f, 4.0f);
    master_.SetPreSumGain(fl2fp(damp));
}

int MixerService::GetPlayedBufferPercentage() {
	return out_->GetPlayedBufferPercentage() ;
}

void MixerService::toggleRendering(bool enable) {
    isRendering_ = enable;

    // Stopping has to close the writer whatever the mode says. The song
    // screen switches the mode back to playback as it stops, so by the
    // time this ran the stereo branch below was already unreachable --
    // the wav was left open, and since the header is only written when
    // the writer closes, every render came out zero bytes.
    if (!enable) {
        out_->EnableRendering(false);
        return;
    }

    switch (mode_) {
    case MSRM_PLAYBACK:
        initRendering(MSRM_PLAYBACK);
        break;
    case MSRM_STEREO:
        initRendering(MSRM_STEREO);
        out_->EnableRendering(enable);
        break;
    }
}

void MixerService::OnPlayerStart() {
	// A tail left over from the last take would play over the first
	// bar of this one, which reads as a glitch rather than as reverb.
	SendFx::Flush() ;
	toggleRendering(true) ;
} ;

void MixerService::OnPlayerStop() {
	toggleRendering(false) ;
} ;

void MixerService::SetSendFxParams(int division,int feedback,
                                   int size,int damp) {
	SendFx::SetDelayDivision(division) ;
	SendFx::SetDelayFeedback(feedback) ;
	SendFx::SetReverbSize(size) ;
	SendFx::SetReverbDamp(damp) ;
}

void MixerService::SetSendFx2(int freeze,int drive) {
	SendFx::SetReverbFreeze(freeze) ;
	SendFx::SetDrive(drive) ;
}

void MixerService::Execute(FourCC id,float value) {
     if (value>0.5) {
        Audio *audio=Audio::GetInstance() ;
        int volume=audio->GetMixerVolume() ;
        switch(id) {
           case TRIG_VOLUME_INCREASE:
                if (volume<100) volume+=1 ;
                break ;
           case TRIG_VOLUME_DECREASE:
                if (volume>0) volume-=1 ;
                break ;                       
        } ;
        audio->SetMixerVolume(volume) ;
     } ;
}

AudioOut *MixerService::GetAudioOut() {
	return out_ ;
} ;


void MixerService::Lock() {
	if (sync_) SDL_LockMutex(sync_) ;
}

void MixerService::Unlock() {
	if (sync_) SDL_UnlockMutex(sync_) ;
}
