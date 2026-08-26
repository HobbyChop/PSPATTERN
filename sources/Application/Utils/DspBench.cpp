#include "DspBench.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/SendFx.h"
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
enum { B_TONE=0, B_TONEF, B_VAX, B_PDX, B_FM2, B_FM4,
       B_DELAY, B_REVERB, B_BOTH } ;

static const char *names_[DSPB_ENGINES]= {
	"tone","tone+flt","vax x2","pdx","fm 2op","fm 4op",
	"delay","reverb","dly+rev"
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
		// PDX was missing from this table entirely, which meant the
		// engine a chord or pad patch is most likely to be written on
		// was the one nobody could measure. Configured like a real
		// patch: a saw with the distortion envelope moving, because a
		// static mod amount is not what anybody plays.
		case B_PDX:
			s.FindVariable(SYP_ENGINE)->SetInt(SET_PDX) ;
			s.FindVariable(SYP_PDXWAVE)->SetInt(PWT_SAW) ;
			s.FindVariable(SYP_DCWAMT)->SetInt(0x48) ;
			s.FindVariable(SYP_DCWDEC)->SetInt(0x36) ;
			s.FindVariable(SYP_DCWSUS)->SetInt(0x28) ;
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

	for (int e=0;e<DSPB_VOICE_ROWS;e++) {

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

	// ---- the send bus -------------------------------------------
	//
	// Timed the way the mixer actually drives it: N channels each
	// accumulating a block into the sends, then one Render of the
	// bank. Fed real signal rather than silence, because a bank fed
	// nothing decays to nothing and stops running, which would time
	// the early out instead of the reverb.
	// the return is an AudioModule, so the bench needs its own
	SendFx::Return ret ;

	for (int e=B_DELAY;e<DSPB_ENGINES;e++) {

		int dSend=(e==B_REVERB)?0:200 ;
		int rSend=(e==B_DELAY)?0:200 ;

		for (int step=0;step<DSPB_STEPS;step++) {

			int senders=VoicesAt(step) ;
			if (senders>SONG_CHANNEL_COUNT) senders=SONG_CHANNEL_COUNT ;

			SendFx::Flush() ;

			// something with energy across the band, so the damping
			// filters and the feedback paths all do their work
			for (int i=0;i<BENCH_FRAMES*2;i++) {
				voice_[i]=i2fp(((i*2654435761u)>>20)&0x3FFF)-i2fp(0x2000) ;
			}

			// let the lines fill: an empty reverb is a cheap reverb
			for (int b=0;b<16;b++) {
				for (int c=0;c<senders;c++)
					SendFx::Accumulate(voice_,BENCH_FRAMES,dSend,rSend) ;
				ret.Render(mix_,BENCH_FRAMES) ;
			}

			unsigned int worst=0 ;
			for (int b=0;b<BENCH_BLOCKS;b++) {
				unsigned int t0=benchMicros() ;
				for (int c=0;c<senders;c++)
					SendFx::Accumulate(voice_,BENCH_FRAMES,dSend,rSend) ;
				ret.Render(mix_,BENCH_FRAMES) ;
				unsigned int spent=benchMicros()-t0 ;
				if (spent>worst) worst=spent ;
			}

			unsigned int permille=worst*1000u/budget ;
			if (permille>9999) permille=9999 ;
			r.load_[e][step]=(short)permille ;
		}
	}
	SendFx::Flush() ;

	r.done_=true ;
}

}
