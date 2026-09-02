#include "AudioFileStreamer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Utils/fixed.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "Application/Model/Config.h"
#include "Application/Instruments/SampleConvert.h"
#include <string.h>
#include <stdlib.h>

#define AFS_FADE 512   /* ~12ms at 44.1k: below a click, above a bump */

AudioFileStreamer::AudioFileStreamer() {
	wav_=0 ;
	mem_=0 ;
	channels_=1 ;
	srcChannels_=1 ;
	srcRate_=44100 ;
	srcFrames_=0 ;
	mono_=false ;
	div_=1 ;
	fold_=0 ;
	foldFrames_=0 ;
	size_=0 ;
	position_=0 ;
	phase_=0 ;
	step_=0x10000 ;
	fadeIn_=0 ;
	fadeOut_=0 ;
	stopping_=false ;
	shift_=1 ;
	mode_=AFSM_STOPPED ;
} ;

AudioFileStreamer::~AudioFileStreamer() {
	SAFE_DELETE(wav_) ;
	SAFE_FREE(fold_) ;
} ;

// under the mixer lock: the render reads all of this
void AudioFileStreamer::begin(int channels,int rate,long frames) {
	const char *shift=Config::GetInstance()->GetValue("PRELISTENATTENUATION") ;
	shift_=(shift)?atoi(shift):0 ;
	srcChannels_=(channels<1)?1:channels ;
	srcRate_=(rate<1)?44100:rate ;
	srcFrames_=frames ;
	position_=0 ;
	phase_=0 ;
	applyShape() ;
	fadeIn_=AFS_FADE ;
	fadeOut_=0 ;
	stopping_=false ;
	mode_=AFSM_PLAYING ;
}

// what the source becomes once folded and divided: the shape the
// resampler and the end-of-stream arithmetic see
void AudioFileStreamer::applyShape() {
	channels_=(mono_||srcChannels_==1)?1:2 ;
	size_=srcFrames_/div_ ;
	int driver=Audio::GetInstance()->GetSampleRate() ;
	if (driver<1) driver=44100 ;
	int rate=srcRate_/div_ ;
	if (rate<1) rate=driver ;
	step_=(unsigned int)(((unsigned long long)rate<<16)/(unsigned long long)driver) ;
	if (step_==0) step_=1 ;
}

void AudioFileStreamer::SetShape(bool mono,int div) {
	if (div!=2&&div!=4) div=1 ;
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	long srcPos=position_*div_ ;   // keep the place in the source
	mono_=mono ;
	div_=div ;
	applyShape() ;
	position_=srcPos/div_ ;
	if (size_>0&&position_>=size_) position_=size_-1 ;
	phase_=0 ;
	ms->Unlock() ;
}

bool AudioFileStreamer::Start(const Path &path) {
	Trace::Debug("Starting to stream %s",path.GetPath().c_str());
	// open HERE, on the caller's thread, not lazily inside the render:
	// a FAT open is tens of ms and used to be the render thread's
	// problem, one guaranteed hiccup per file browsed. The caller runs
	// deferred and unlocked; only the swap takes the mixer lock.
	WavFile *w=WavFile::Open(path.GetPath().c_str()) ;
	if (!w) {
		Trace::Error("Failed to open %s for preview",path.GetPath().c_str()) ;
		return false ;
	}
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	SAFE_DELETE(wav_) ;
	mem_=0 ;
	wav_=w ;
	path_=path ;
	begin(w->GetChannelCount(-1),w->GetSampleRate(-1),w->GetSize(-1)) ;
	ms->Unlock() ;
	return true ;
} ;

bool AudioFileStreamer::StartBuffer(const short *frames,long frameCount,int channels,int rate) {
	if (!frames||frameCount<2) return false ;
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	SAFE_DELETE(wav_) ;
	mem_=frames ;
	begin(channels,rate,frameCount) ;
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

void AudioFileStreamer::StopNow() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	mode_=AFSM_STOPPED ;
	stopping_=false ;
	mem_=0 ;
	SAFE_DELETE(wav_) ;
	ms->Unlock() ;
} ;

bool AudioFileStreamer::Render(fixed *buffer,int samplecount) {

	if (mode_==AFSM_STOPPED) {
		SAFE_DELETE(wav_) ;
		mem_=0 ;
		return false ;
	}
	if (!wav_&&!mem_) { mode_=AFSM_STOPPED ; return false ; }

	/* Source frames still ahead. Each output frame interpolates
	   between a frame and the next, so the last one is never a start
	   point: N frames of source yield output up to frame N-2. */
	long avail=size_-position_-1 ;
	if (avail<1) { mode_=AFSM_STOPPED ; return false ; }
	unsigned long long span=((unsigned long long)avail)<<16 ;
	long can=(span>phase_)?(long)((span-phase_+step_-1)/step_):0 ;
	int count=samplecount ;
	if (can<count) { count=(int)can ; mode_=AFSM_STOPPED ; }   // the last block
	if (count<=0) { mode_=AFSM_STOPPED ; return false ; }
	long need=(long)((phase_+(unsigned long long)(count-1)*step_)>>16)+2 ;
	if (need>avail+1) need=avail+1 ;

	const short *src ;
	int div=div_ ;
	int sch=srcChannels_ ;
	bool fold=(div>1)||(channels_!=sch) ;
	if (mem_) {
		src=mem_+position_*div*sch ;
	} else {
		/* This runs on the RENDER thread. A file that opens but cannot
		   be read used to fall through to the copy loop with a null
		   pointer -- the machine froze so hard the HOME overlay
		   stopped drawing. Fail the preview, not the system. */
		if (!wav_->GetBuffer(position_*div,need*div)) { mode_=AFSM_STOPPED ; return false ; }
		src=(const short *)wav_->GetSampleBuffer(-1) ;
		if (!src) { mode_=AFSM_STOPPED ; return false ; }
	}
	if (fold) {
		// the same reduction the import writes, one block at a time
		if (need>foldFrames_) {
			SAFE_FREE(fold_) ;
			fold_=(short *)malloc((size_t)need*2*sizeof(short)) ;
			foldFrames_=fold_?need:0 ;
		}
		if (!fold_) { mode_=AFSM_STOPPED ; return false ; }
		SampleConvertFold(src,sch,div,channels_,need,fold_) ;
		src=fold_ ;
	}

	fixed *dst=buffer ;
	int ch=channels_ ;
	int down=1+shift_ ;
	unsigned int ph=phase_ ;
	for (int i=0;i<count;i++) {
		int g=256 ;
		if (stopping_) {
			g=(fadeOut_*256)/AFS_FADE ;
			if (fadeOut_>0) fadeOut_-- ;
		} else if (fadeIn_>0) {
			g=256-((fadeIn_*256)/AFS_FADE) ;
			fadeIn_-- ;
		}
		const short *a=src+(long)(ph>>16)*ch ;
		const short *b=a+ch ;
		int frac=(int)(ph&0xFFFF) ;
		int l=(a[0]*(65536-frac)+b[0]*frac)>>16 ;
		int r=(ch==2)?((a[1]*(65536-frac)+b[1]*frac)>>16):l ;
		l>>=down ;
		r>>=down ;
		*dst++=i2fp((l*g)>>8) ;
		*dst++=i2fp((r*g)>>8) ;
		ph+=step_ ;
	}
	for (int i=count;i<samplecount;i++) {
		*dst++=i2fp(0) ;
		*dst++=i2fp(0) ;
	}
	position_+=(long)(ph>>16) ;
	phase_=ph&0xFFFF ;
	if (stopping_&&fadeOut_<=0) {
		stopping_=false ;
		mode_=AFSM_STOPPED ;
	}
	return true ;
}
