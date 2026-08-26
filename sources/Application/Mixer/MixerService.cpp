#include "MixerService.h"
#include "Application/Audio/DummyAudioOut.h"
#include "Application/Model/Config.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Project.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"

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

unsigned int MixerService::GetPreFaderSum() {
    // master_'s peak is captured after the buses are summed and before
    // the fader, which is exactly the figure wanted here
    unsigned int lv = master_.GetPeakLevel();
    unsigned int l = (lv >> 16) & 0xFFFF, r = lv & 0xFFFF;
    unsigned int m = (l > r) ? l : r;
    return (m << 8) / 32767;          // 0x100 == full scale
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
    Mixer *mixer = Mixer::GetInstance();

    fixed masterVolume = fp_mul(i2fp(vol), fl2fp(0.01f));

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

void MixerService::SetMasterVolume(int attn) {
    master_.SetMasterVolume(attn);
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
