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
// 16 frames = 0.36ms, so the 92 columns of the scope panel span 33ms
#define SCOPE_BUCKET 16

namespace AudioStats {

// one entry per bucket of SCOPE_BUCKET frames, min and max
static short scopeMin_[AUDIOSTATS_SCOPE_SIZE] ;
static short scopeMax_[AUDIOSTATS_SCOPE_SIZE] ;
static volatile int scopePos_=0 ;
// the bucket in progress, carried across blocks so the trace does not
// get a seam at every block boundary
static short curMin_=32767,curMax_=-32768 ;
static int curCount_=0 ;
static volatile int dspPercent_=0 ;
static unsigned int blockStart_=0 ;

void BeginBlock() {
	blockStart_=statsMicros() ;
}

void EndBlock(short *buf,int frames,bool interlaced) {

	if (frames<=0) return ;

	// dsp: render micros vs the block's realtime budget, smoothed
	unsigned int spent=statsMicros()-blockStart_ ;
	unsigned int budget=(unsigned int)frames*10000u/441u ; // us at 44.1k
	if (budget) {
		int pct=(int)(spent*100u/budget) ;
		if (pct>999) pct=999 ;
		dspPercent_=(dspPercent_*7+pct)>>3 ;
	}

	// scope: peak envelope of the left channel, one bucket per column.
	// Sampling every other frame is plenty for a 44x92 pixel panel and
	// halves what this costs the render budget; the min/max still sees
	// the shape of a transient, which plain decimation did not.
	int step=(interlaced)?2:1 ;
	int pos=scopePos_ ;
	short lo=curMin_,hi=curMax_ ;
	int cnt=curCount_ ;
	for (int i=0;i<frames;i+=2) {
		short v=buf[i*step] ;
		if (v<lo) lo=v ;
		if (v>hi) hi=v ;
		cnt+=2 ;
		if (cnt>=SCOPE_BUCKET) {
			scopeMin_[pos]=lo ;
			scopeMax_[pos]=hi ;
			pos=(pos+1)&SCOPE_MASK ;
			lo=32767 ; hi=-32768 ; cnt=0 ;
		}
	}
	curMin_=lo ; curMax_=hi ; curCount_=cnt ;
	scopePos_=pos ;
}

int GetDspPercent() {
	return dspPercent_ ;
}

void ReadScope(short *outMin,short *outMax,int count) {
	// most recent 'count' buckets, oldest first
	int pos=(scopePos_-count)&SCOPE_MASK ;
	for (int i=0;i<count;i++) {
		short a=scopeMin_[pos],b=scopeMax_[pos] ;
		// a bucket the audio thread has not filled yet reads as the
		// empty sentinel; show it as silence rather than as a spike
		if (a>b) { a=0 ; b=0 ; }
		outMin[i]=a ;
		outMax[i]=b ;
		pos=(pos+1)&SCOPE_MASK ;
	}
}

}
