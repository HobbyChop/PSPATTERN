#include "DspBench.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Services/Audio/Audio.h"
#include <string.h>

#ifdef PLATFORM_PSP
#include <pspkernel.h>
#include <psppower.h>
static unsigned int benchMicros() { return sceKernelGetSystemTimeLow() ; }
static int benchMhz() { return scePowerGetCpuClockFrequency() ; }
#else
#include <time.h>
static unsigned int benchMicros() {
	return (unsigned int)((unsigned long long)clock()*1000000u/CLOCKS_PER_SEC) ;
}
static int benchMhz() { return 0 ; }
#endif

namespace DspBench {

// Chosen to be the shapes people actually use, not the cheapest
// settings each engine has: a bare saw tells you nothing about
// whether a track will fit.
enum { B_TONE=0, B_TONEF, B_VAX, B_FM2, B_FM4 } ;

static const char *names_[DSPB_ENGINES]= {
	"tone","tone+flt","vax x2","fm 2op","fm 4op"
} ;

const char *EngineName(int e) {
	return ((e>=0)&&(e<DSPB_ENGINES))?names_[e]:"?" ;
}

int VoicesAt(int step) {
	static const int v[DSPB_STEPS]={1,2,4,8} ;
	return ((step>=0)&&(step<DSPB_STEPS))?v[step]:0 ;
}

static void configure(SynthInstrument &s,int which) {
	s.FindVariable(SYP_ATTACK)->SetInt(0x10) ;
	s.FindVariable(SYP_DECAY)->SetInt(0x60) ;
	s.FindVariable(SYP_SUSTAIN)->SetInt(0xC0) ;
	s.FindVariable(SYP_RELEASE)->SetInt(0x00) ;
	s.FindVariable(SYP_LFODEST)->SetInt(SLD_OFF) ;
	s.FindVariable(SYP_SUB)->SetInt(0) ;
	s.FindVariable(SYP_NOISE)->SetInt(0) ;
	s.FindVariable(SYP_CUTOFF)->SetInt(0xFF) ;
	s.FindVariable(SYP_RESO)->SetInt(0) ;
	s.FindVariable(SYP_UNISON)->SetInt(1) ;
	s.FindVariable(SYP_FMFB)->SetInt(0) ;

	switch (which) {
		case B_TONE:
			s.FindVariable(SYP_ENGINE)->SetInt(SET_TONE) ;
			s.FindVariable(SYP_WAVE)->SetInt(SWT_SAW) ;
			break ;
		case B_TONEF:
			s.FindVariable(SYP_ENGINE)->SetInt(SET_TONE) ;
			s.FindVariable(SYP_WAVE)->SetInt(SWT_SAW) ;
			s.FindVariable(SYP_CUTOFF)->SetInt(0x80) ;
			s.FindVariable(SYP_RESO)->SetInt(0x60) ;
			break ;
		case B_VAX:
			s.FindVariable(SYP_ENGINE)->SetInt(SET_VAX) ;
			s.FindVariable(SYP_VAXWAVE)->SetInt(VWT_SAW) ;
			s.FindVariable(SYP_UNISON)->SetInt(2) ;
			s.FindVariable(SYP_CUTOFF)->SetInt(0x80) ;
			s.FindVariable(SYP_RESO)->SetInt(0x60) ;
			break ;
		case B_FM2:
		case B_FM4: {
			s.FindVariable(SYP_ENGINE)->SetInt(SET_FM) ;
			s.FindVariable(SYP_FMALGO)->SetInt(0) ;
			bool four=(which==B_FM4) ;
			s.FindVariable(SYP_FML1)->SetInt(0xFF) ;
			s.FindVariable(SYP_FML2)->SetInt(0x80) ;
			s.FindVariable(SYP_FML3)->SetInt(four?0x80:0x00) ;
			s.FindVariable(SYP_FML4)->SetInt(four?0x80:0x00) ;
			static const FourCC dId[FM_OPS]=
				{SYP_FMC1,SYP_FMC2,SYP_FMC3,SYP_FMC4} ;
			static const FourCC sId[FM_OPS]=
				{SYP_FMS1,SYP_FMS2,SYP_FMS3,SYP_FMS4} ;
			for (int i=0;i<FM_OPS;i++) {
				s.FindVariable(dId[i])->SetInt(0x50) ;
				s.FindVariable(sId[i])->SetInt(0xA0) ;
			}
			break ;
		}
	}
}

// One block's worth of stereo, the size the player actually asks for.
#define BENCH_FRAMES 641
#define BENCH_BLOCKS 40

static fixed mix_[BENCH_FRAMES*2] ;
static fixed voice_[BENCH_FRAMES*2] ;

void Run(Result &r) {

	memset(&r,0,sizeof(r)) ;
	r.blockFrames_=BENCH_FRAMES ;
	r.sampleRate_=Audio::GetInstance()->GetSampleRate() ;
	if (r.sampleRate_<=0) r.sampleRate_=44100 ;
	r.cpuMhz_=benchMhz() ;

	// microseconds of realtime one block represents -- the same
	// budget the DSP meter divides by
	unsigned int budget=(unsigned int)
		((unsigned long long)BENCH_FRAMES*1000000u/(unsigned)r.sampleRate_) ;
	if (!budget) budget=1 ;

	SynthInstrument synth ;
	synth.Init() ;

	for (int e=0;e<DSPB_ENGINES;e++) {

		configure(synth,e) ;

		for (int step=0;step<DSPB_STEPS;step++) {

			int voices=VoicesAt(step) ;
			if (voices>SONG_CHANNEL_COUNT) voices=SONG_CHANNEL_COUNT ;

			// spread the notes: a stack of unisons is not a workload,
			// and phase-identical voices are not either
			for (int c=0;c<voices;c++) {
				synth.Start(c,48+c*5,true) ;
			}

			// warm the caches and let the attacks pass, so what we
			// measure is the steady state a held chord actually costs
			for (int b=0;b<8;b++) {
				for (int c=0;c<voices;c++) {
					synth.Render(c,voice_,BENCH_FRAMES,false) ;
				}
			}

			unsigned int worst=0 ;
			for (int b=0;b<BENCH_BLOCKS;b++) {
				unsigned int t0=benchMicros() ;
				memset(mix_,0,sizeof(fixed)*BENCH_FRAMES*2) ;
				for (int c=0;c<voices;c++) {
					synth.Render(c,voice_,BENCH_FRAMES,false) ;
					// summing is part of the cost of a voice too
					for (int i=0;i<BENCH_FRAMES*2;i++) {
						mix_[i]+=voice_[i] ;
					}
				}
				unsigned int spent=benchMicros()-t0 ;
				// the worst block is what drops out, not the average
				if (spent>worst) worst=spent ;
			}

			for (int c=0;c<voices;c++) synth.Stop(c) ;

			unsigned int permille=worst*1000u/budget ;
			if (permille>9999) permille=9999 ;
			r.load_[e][step]=(short)permille ;
		}
	}
	r.done_=true ;
}

}
