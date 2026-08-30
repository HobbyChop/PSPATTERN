#include "AudioStats.h"

#ifdef PLATFORM_PSP
#include <pspkernel.h>
static unsigned int statsMicros() {
	return sceKernelGetSystemTimeLow() ;
}
#else
#include <time.h>
static unsigned int statsMicros() {
	// CPU time is exactly what a DSP meter wants anyway
	return (unsigned int)((unsigned long long)clock()*1000000u/CLOCKS_PER_SEC) ;
}
#endif

#define SCOPE_MASK (AUDIOSTATS_SCOPE_SIZE-1)
// 16 frames = 0.36ms, so the 92 columns of a trace span 33ms. Both
// traces are full width, so this is what it always was.
#define SCOPE_BUCKET 16

namespace AudioStats {

// one entry per bucket of SCOPE_BUCKET frames, min and max, per channel
static short scopeMin_[2][AUDIOSTATS_SCOPE_SIZE] ;
static short scopeMax_[2][AUDIOSTATS_SCOPE_SIZE] ;
static volatile int scopePos_=0 ;
// the bucket in progress, carried across blocks so the trace does not
// get a seam at every block boundary
static short curMin_[2]={32767,32767},curMax_[2]={-32768,-32768} ;
static int curCount_=0 ;
static volatile int dspPercent_=0 ;
// peak hold, and the countdown of blocks before it starts falling
static volatile int dspPeak_=0 ;
static int peakHold_=0 ;
// about a second and a half at 641 frames a block
#define DSP_PEAK_HOLD 100
static unsigned int blockStart_=0 ;
static unsigned int excluded_=0 ;

void ExcludeMicros(unsigned int us) { excluded_+=us ; }

void BeginBlock() {
	blockStart_=statsMicros() ;
	excluded_=0 ;
}

void EndBlock(short *buf,int frames,bool interlaced) {

	if (frames<=0) return ;

	// dsp: render micros vs the block's realtime budget, smoothed
	unsigned int spent=statsMicros()-blockStart_ ;
	// whatever the block spent not doing DSP -- see ExcludeMicros
	spent=(spent>excluded_)?(spent-excluded_):0 ;
	unsigned int budget=(unsigned int)frames*10000u/441u ; // us at 44.1k
	if (budget) {
		int pct=(int)(spent*100u/budget) ;
		if (pct>999) pct=999 ;
		dspPercent_=(dspPercent_*7+pct)>>3 ;
		// Held, then allowed to fall slowly. Held so a spike can be
		// read by somebody looking at the screen a moment later;
		// falling so it does not stay stuck on one bad block from a
		// minute ago and stop meaning anything.
		if (pct>=dspPeak_) {
			dspPeak_=pct ;
			peakHold_=DSP_PEAK_HOLD ;
		} else if (peakHold_>0) {
			peakHold_-- ;
		} else if (dspPeak_>dspPercent_) {
			dspPeak_-- ;
		}
	}

	// scope: peak envelope of the left channel, one bucket per column.
	// Sampling every other frame is plenty for a 44x92 pixel panel and
	// halves what this costs the render budget; the min/max still sees
	// the shape of a transient, which plain decimation did not.
	int step=(interlaced)?2:1 ;
	int pos=scopePos_ ;
	int cnt=curCount_ ;
	for (int i=0;i<frames;i+=2) {
		for (int ch=0;ch<2;ch++) {
			short v=interlaced?buf[i*step+ch]:buf[i*step] ;
			if (v<curMin_[ch]) curMin_[ch]=v ;
			if (v>curMax_[ch]) curMax_[ch]=v ;
		}
		cnt+=2 ;
		if (cnt>=SCOPE_BUCKET) {
			for (int ch=0;ch<2;ch++) {
				scopeMin_[ch][pos]=curMin_[ch] ;
				scopeMax_[ch][pos]=curMax_[ch] ;
				curMin_[ch]=32767 ; curMax_[ch]=-32768 ;
			}
			pos=(pos+1)&SCOPE_MASK ;
			cnt=0 ;
		}
	}
	curCount_=cnt ;
	scopePos_=pos ;
}

int GetDspPeak() {
	return dspPeak_ ;
}

void ResetDspPeak() {
	dspPeak_=0 ;
	peakHold_=0 ;
}

static volatile int underruns_ = 0 ;

void AddUnderrun() { underruns_++ ; }
int GetUnderruns() { return underruns_ ; }
void ResetUnderruns() { underruns_ = 0 ; }

int GetDspPercent() {
	return dspPercent_ ;
}

void ReadScope(short *outMin,short *outMax,int count,int channel) {
	int ch=(channel&1) ;
	// most recent 'count' buckets, oldest first
	int pos=(scopePos_-count)&SCOPE_MASK ;
	for (int i=0;i<count;i++) {
		short a=scopeMin_[ch][pos],b=scopeMax_[ch][pos] ;
		// a bucket the audio thread has not filled yet reads as the
		// empty sentinel; show it as silence rather than as a spike
		if (a>b) { a=0 ; b=0 ; }
		outMin[i]=a ;
		outMax[i]=b ;
		pos=(pos+1)&SCOPE_MASK ;
	}
}

}
