#include "AudioFileStreamer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Utils/fixed.h"
#include "System/Console/Trace.h"
#include "Application/Model/Config.h"

#define AFS_FADE 512   /* ~12ms at 44.1k: below a click, above a bump */

AudioFileStreamer::AudioFileStreamer() {
	wav_=0 ;
	fadeIn_=0 ;
	fadeOut_=0 ;
	stopping_=false ;
	shift_=1 ;
	mode_=AFSM_STOPPED ;
	newPath_=false ;
} ;

AudioFileStreamer::~AudioFileStreamer() {
	SAFE_DELETE(wav_) ;
} ;
 
bool AudioFileStreamer::Start(const Path &path) {
  Trace::Debug("Starting to stream %s",path.GetPath().c_str());
	const char *shift=Config::GetInstance()->GetValue("PRELISTENATTENUATION") ;
	shift_=(shift)?atoi(shift):0 ;
	// open HERE, on the caller's thread, not lazily inside the render:
	// a FAT open is tens of ms and used to be the render thread's
	// problem, one guaranteed hiccup per file browsed. The caller runs
	// deferred and unlocked; only the pointer swap takes the mixer
	// lock the render cares about.
	WavFile *w=WavFile::Open(path.GetPath().c_str()) ;
	if (!w) {
		Trace::Error("Failed to open %s for preview",path.GetPath().c_str()) ;
		return false ;
	}
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	SAFE_DELETE(wav_) ;
	wav_=w ;
	position_=0 ;
	fadeIn_=AFS_FADE ;
	fadeOut_=0 ;
	stopping_=false ;
	path_=path ;
	newPath_=false ;
	mode_=AFSM_PLAYING ;
	ms->Unlock() ;
	return true ;
} ;

void AudioFileStreamer::Stop() {
	/* ramped, not switched: cutting mid-waveform is the same click as
	   starting on one. The render finishes the fade and stops itself. */
	if (mode_==AFSM_PLAYING&&!stopping_) {
		fadeOut_=AFS_FADE ;
		stopping_=true ;
	} else {
		mode_=AFSM_STOPPED ;
	}
  Trace::Debug("Streaming stopped");
} ;

bool AudioFileStreamer::Render(fixed *buffer,int samplecount) {

	// See if we're playing

	if (mode_==AFSM_STOPPED) {
		SAFE_DELETE(wav_) ;
		return false ;
	}

	// Do we need to get a new file ?

	if (newPath_) {
		SAFE_DELETE(wav_) ;
		newPath_=false ;
	}

	// new look if we need to load the file

	if (!wav_) 
  {
		wav_=WavFile::Open(path_.GetPath().c_str()) ;
		if (!wav_) 
    {
      Trace::Error("Failed to open streaming of %s",path_.GetPath().c_str());
			mode_=AFSM_STOPPED ;
			return false ;
		}
		position_=0 ;
	}

	// we are playing a valid file

	long size=wav_->GetSize(-1) ;
	int count=samplecount ;
	if (position_+samplecount>=size)
  {
    Trace::Debug("Reached the end of %d samples",size);
		count=size-position_ ;
		mode_=AFSM_STOPPED ;
		memset(buffer,0,2*samplecount*sizeof(fixed)) ;
	}
	/* This runs on the RENDER thread. A file that opens but cannot be
	   buffered (out of memory, truncated data) used to fall through to
	   the copy loop with a null sample pointer -- the machine froze so
	   hard the HOME overlay stopped drawing. Fail the preview, not the
	   system. */
	if (count<=0 || !wav_->GetBuffer(position_,count)) {
		mode_=AFSM_STOPPED ;
		return false ;
	}
	fixed *dst=buffer ;
	short *src=(short *)wav_->GetSampleBuffer(-1) ;
	if (!src) {
		mode_=AFSM_STOPPED ;
		return false ;
	}
	int channel=wav_->GetChannelCount(-1) ;

 // I might need to do sample interpolation here

	for (int i=0;i<count;i++) {
		int g=256 ;
		if (stopping_) {
			g=(fadeOut_*256)/AFS_FADE ;
			if (fadeOut_>0) fadeOut_-- ;
		} else if (fadeIn_>0) {
			g=256-((fadeIn_*256)/AFS_FADE) ;
			fadeIn_-- ;
		}
		int l=((*src++)>>(1+shift_)) ;
		fixed v=*dst++=i2fp((l*g)>>8) ;
		if (channel==2) {
			int r=((*src++)>>(1+shift_)) ;
			*dst++=i2fp((r*g)>>8) ;
		} else {
			*dst++=v ;
		}
	}
	position_+=count ;
	if (stopping_&&fadeOut_<=0) {
		stopping_=false ;
		mode_=AFSM_STOPPED ;
	}
	return true ;
}
