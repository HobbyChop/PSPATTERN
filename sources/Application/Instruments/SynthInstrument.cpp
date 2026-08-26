#include "SynthInstrument.h"
#include "VibratoMath.h"
#include "CommandList.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Model/Config.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// Engine sample rate: every backend runs the mixer at 44.1kHz
#define SYNTH_RATE 44100

// envelopes run in Q23 (FP_ONE<<8) for step resolution on long ramps
#define ENV_ONE (FP_ONE<<8)

// PDX knee floor: full warp compresses the fast segment to 1/32 cycle
#define PDX_KNEE_MIN 1024

static const char *engineNames[SET_LAST]= {
	"tone","pdx","vax","fm"
} ;

/* 4-op algorithms. The name IS the routing: ">" means "modulates",
   a space separates independent branches, and anything not followed
   by ">" reaches the output. Eight is enough to cover the ground --
   one chain, two shapes of stacked pair, parallel stacks, and the
   additive case -- without a diagram screen to explain them. */
static const char *fmAlgoNames[FM_ALGO_COUNT]= {
	"4>3>2>1","43>2>1","432>1","4>3>2 1",
	"4>3 2>1","43>2 1","4>3 2 1","4 3 2 1"
} ;

/* Destination of each operator under each algorithm, indexed
   [algo][op] with op 0 = OP1. Every route runs from a higher
   operator to a lower one, so rendering 4,3,2,1 in order always has
   a modulator's output ready before its carrier needs it. */
static const signed char fmAlgoDest[FM_ALGO_COUNT][FM_OPS]= {
	/* 4>3>2>1 */ { FM_OUT, 0,      1,      2      },
	/* 43>2>1  */ { FM_OUT, 0,      1,      1      },
	/* 432>1   */ { FM_OUT, 0,      0,      0      },
	/* 4>3>2 1 */ { FM_OUT, FM_OUT, 1,      2      },
	/* 4>3 2>1 */ { FM_OUT, 0,      FM_OUT, 2      },
	/* 43>2 1  */ { FM_OUT, FM_OUT, 1,      1      },
	/* 4>3 2 1 */ { FM_OUT, FM_OUT, FM_OUT, 2      },
	/* 4 3 2 1 */ { FM_OUT, FM_OUT, FM_OUT, FM_OUT }
} ;

/* Coarse ratio, DX7 style: index 0 is the half, then the integers.
   Not padded: the field right-aligns them with "%3s". Padding the
   names instead would put leading spaces in the saved project, and
   XML attribute normalisation is entitled to eat those -- a patch
   that reloads with every ratio reset to the first entry. */
static const char *fmRatioNames[32]= {
	"0.5","1","2","3","4","5","6","7",
	"8","9","10","11","12","13","14","15",
	"16","17","18","19","20","21","22","23",
	"24","25","26","27","28","29","30","31"
} ;

/* Q8 frequency multiplier for each of those. */
static const unsigned short fmRatioMul[32]= {
	128, 256, 512, 768,1024,1280,1536,1792,
	2048,2304,2560,2816,3072,3328,3584,3840,
	4096,4352,4608,4864,5120,5376,5632,5888,
	6144,6400,6656,6912,7168,7424,7680,7936
} ;

/* Ids in operator order, so the constructor and the renderer can
   both loop instead of repeating themselves four times. */
static const FourCC fmRatioId[FM_OPS]= {SYP_FMR1,SYP_FMR2,SYP_FMR3,SYP_FMR4} ;
static const FourCC fmLevelId[FM_OPS]= {SYP_FML1,SYP_FML2,SYP_FML3,SYP_FML4} ;
static const FourCC fmDetId  [FM_OPS]= {SYP_FMD1,SYP_FMD2,SYP_FMD3,SYP_FMD4} ;
static const FourCC fmAtkId  [FM_OPS]= {SYP_FMA1,SYP_FMA2,SYP_FMA3,SYP_FMA4} ;
static const FourCC fmDecId  [FM_OPS]= {SYP_FMC1,SYP_FMC2,SYP_FMC3,SYP_FMC4} ;
static const FourCC fmSusId  [FM_OPS]= {SYP_FMS1,SYP_FMS2,SYP_FMS3,SYP_FMS4} ;
static const char *vaxWaveNames[VWT_LAST]= {
	"saw","pulse"
} ;
static const char *fltModeNames[VFM_LAST]= {
	"lp","bp","hp"
} ;
static const char *lfoDestNames[SLD_LAST]= {
	"off","pit","flt","pwm"
} ;

// unison detune spread: offsets per stack voice, used -3..3
static const int unisonOff[VAX_MAX_UNISON]={0,-3,3,-2,2,-1,1} ;
static const char *waveNames[SWT_LAST]= {
	"saw","square","triangle","sine"
} ;
static const char *pdxWaveNames[PWT_LAST]= {
	"saw","square","reso saw","reso tri"
} ;

bool SynthInstrument::tablesBuilt_=false ;
unsigned int *SynthInstrument::noteInc_ = 0 ;
unsigned int *SynthInstrument::lfoInc_ = 0 ;
short *SynthInstrument::sineTable_ = 0 ;
short *SynthInstrument::cosTable_ = 0 ;
short *SynthInstrument::cutTable_ = 0 ;
short *SynthInstrument::fmSin_ = 0 ;
short *SynthInstrument::fmSinD_ = 0 ;

/* Where the tables actually sit. See the header for why.
 *
 * On the PSP they go in the scratchpad: 16KB of on chip SRAM at a
 * fixed address, outside main memory and outside the data cache.
 * Everywhere else, and on the PSP if the config key says so, they go
 * in a plain static block and nothing changes.
 *
 * The escape hatch is not decoration. Scratchpad is a fixed address
 * shared with anything else on the machine that decides to use it,
 * and this is the kind of change whose effect cannot be seen in an
 * emulator at all -- PPSSPP translates to native code and models no
 * cache. So it ships able to be turned off from config.xml, without
 * a toolchain, by anybody who finds it misbehaves.
 */
#define SYNTH_TABLE_SHORTS (128*2 + 256*2 + 1024 + 1024 + 1025 + 1 + 256 + 256)
static short synthTableRam_[SYNTH_TABLE_SHORTS] ;

#ifdef __PSP__
#define SYNTH_SCRATCHPAD_ADDR 0x00010000
#define SYNTH_SCRATCHPAD_SIZE (16*1024)
#endif

int SynthInstrument::AlgoDest(int algo,int op) {
	if (algo<0) algo=0 ;
	if (algo>=FM_ALGO_COUNT) algo=FM_ALGO_COUNT-1 ;
	if (op<0||op>=FM_OPS) return FM_OUT ;
	return fmAlgoDest[algo][op] ;
}

void SynthInstrument::placeTables() {
	if (noteInc_) return ;
	short *p = synthTableRam_ ;
#ifdef SYNTH_SCRATCHPAD_ADDR
	// off only if the key is present AND set to 0
	const char *v = Config::GetInstance()->GetValue("SCRATCHPADTABLES") ;
	bool useSpad = !(v && v[0]=='0') ;
	if (useSpad && (SYNTH_TABLE_SHORTS*(int)sizeof(short))<=SYNTH_SCRATCHPAD_SIZE) {
		p = (short *)SYNTH_SCRATCHPAD_ADDR ;
	}
#endif
	// The two int tables go first, so everything after them is still
	// four byte aligned; the odd length of the cosine table is padded
	// for the same reason.
	noteInc_   = (unsigned int *)p ; p += 128*2 ;
	lfoInc_    = (unsigned int *)p ; p += 256*2 ;
	fmSin_     = p ;                 p += 1024 ;
	fmSinD_    = p ;                 p += 1024 ;
	cosTable_  = p ;                 p += 1025 + 1 ;
	sineTable_ = p ;                 p += 256 ;
	cutTable_  = p ;                 p += 256 ;
}

// cosine LUT read, 6-bit linear interpolation
static inline int cosLookup(const short *table,unsigned int idx,unsigned int frac) {
	int c0=table[idx] ;
	int c1=table[idx+1] ;
	return c0+(((c1-c0)*(int)frac)>>6) ;
}

/* ---- PolyBLEP ------------------------------------------------------
   A naive saw or square steps from one sample to the next with no
   regard for where inside the sample the edge actually fell, which
   scatters energy across the spectrum instead of leaving it on the
   harmonics. Measured on a rendered A5 saw, 2.5% of the output was at
   frequencies that are not in the note, and it worsens 3dB an octave
   going up: the top of a lead is where it is worst and where it is
   heard.

   The correction is a two sample polynomial residual around each
   discontinuity. The divide only fires inside that window, which is
   twice a cycle, so at 440Hz it is two divides per hundred samples per
   oscillator rather than one per sample.

   The pulse is built as two phase shifted saws with the width added
   back rather than as a comparison against a threshold. That is what
   makes any width DC free: a threshold pulse has a mean of
   (2*duty-1)*amplitude, which is silent until the note ends and then
   is a click, and it is why pulse width has had to stay pinned at the
   centre on every patch written so far.
*/
// One reciprocal per pitch change instead of a divide per edge. The
// first version divided inside the residual, and a 64 bit divide on
// this machine costs tens of cycles where a multiply costs one. Both
// PSPOLY and PSPECTRA moved the divide to the retune point for the
// same reason; this does what they do.
//
// ph < inc and rcp = 2^31/inc, so the product stays under 2^31 and the
// whole thing fits in 32 bit arithmetic with no widening at all.
static inline unsigned int blepRcp(unsigned int inc) {
	return inc?(0x80000000u/inc):0 ;
}

static inline int blepResidual(unsigned int ph,unsigned int inc,
                               unsigned int rcp) {
	if (inc==0) return 0 ;
	if (ph<inc) {
		int t=(int)((ph*rcp)>>16) ;   // 0..32768
		int c=32768-t ;
		return -((c*c)>>15) ;
	}
	unsigned int r=0xFFFFFFFFu-ph ;
	if (r<inc) {
		int t=(int)((r*rcp)>>16) ;
		int c=32768-t ;
		return (c*c)>>15 ;
	}
	return 0 ;
}

// Band limited saw, same +-32768 scale as the naive ramp it replaces.
static inline int sawBlep(unsigned int ph,unsigned int inc,
                          unsigned int rcp) {
	return ((int)(ph>>16)-32768)-blepResidual(ph,inc,rcp) ;
}

// Band limited pulse, +-32768 scale. w is the width as a 32 bit phase
// offset, so w==0x80000000 is a square.
static inline int pulseBlep(unsigned int ph,unsigned int inc,
                            unsigned int rcp,unsigned int w) {
	int s=sawBlep(ph,inc,rcp)-sawBlep(ph+w,inc,rcp) ;
	return s+(int)(w>>16)-32768 ;         // the DC the two saws left
}

// the click tail is a DC offset: cap it well under full scale
#define CLICK_MAX (i2fp(8000))

// Bound for the measured declick below. It can be looser than
// CLICK_MAX because that one guards an accumulating path -- repeated
// retriggers stack into it -- while this one is set outright, once, to
// exactly the step it is cancelling. It still leaves headroom so the
// decaying tail plus the oscillator underneath can't clip the mixer.
// At the old 8000 a wave change stepped further than the cap allowed
// and a fifth of the click survived.
// The measured declick has to be able to cancel the step it measured.
// A bipolar waveform can jump from one rail to the other, which is
// twice full scale, so a bound of half full scale left a loud square
// only partly corrected: 16000 capped the correction and the arp kept
// a 2% of full scale step on every note-on that a saw did not have.
#define DECLICK_MAX (i2fp(32767))


// Per-sample declick epilogue, shared by all three engines.
//
// declickPending_ is the measured path: something discontinuous
// happened between blocks (an engine or wave change under a held
// note), and rather than assume the new oscillator starts at zero we
// measure the actual step against the last sample we emitted and
// cancel exactly that. click_ then decays at >>5, about 0.7ms.
static inline void applyVoiceClick(SynthVoice &v,fixed &smp) {
	if (v.declickPending_) {
		v.declickPending_=false ;
		fixed jump=v.lastOut_-smp ;
		if (jump>DECLICK_MAX) jump=DECLICK_MAX ;
		if (jump<-DECLICK_MAX) jump=-DECLICK_MAX ;
		v.click_=jump ;
	}
	if (v.click_) {
		smp+=v.click_ ;
		v.click_-=(v.click_>>5) ;
		if (v.click_>-32 && v.click_<32) v.click_=0 ;
	}
	v.lastOut_=smp ;
}

// SVF state bound. Max resonance only needs ~1.05e6 of ringing headroom;
// keeping it at 1<<19 leaves the drive stage margin inside an int.
#define SVF_CLAMP (1<<19)

// samples per player tick, guarded — GetTickSampleCount() is 0 before
// the player has a tempo, and a 1-sample tick would run ARPG/RTRG at
// the sample rate
static inline int tickLength() {
	int t=(int)SyncMaster::GetInstance()->GetTickSampleCount() ;
	if (t<64) t=SYNTH_RATE/20 ;      // ~50ms fallback
	return t ;
}


SynthInstrument::SynthInstrument() {

	strcpy(name_,"SYNTH") ;
	memset(voice_,0,sizeof(voice_)) ;

	WatchedVariable *wv=new WatchedVariable("engine",SYP_ENGINE,(char**)engineNames,SET_LAST,0) ;
	Insert(wv) ;
	wv->AddObserver(*this) ;

	// Watched, like the engine: TONE's pulse width row only exists on
	// the square wave, and the page has to rebuild for it to appear.
	WatchedVariable *ww=new WatchedVariable("wave",SYP_WAVE,(char**)waveNames,
	                                        SWT_LAST,0) ;
	Insert(ww) ;
	ww->AddObserver(*this) ;
	Variable *v=0 ;
	v=new Variable("volume",SYP_VOLUME,0x80) ;
	Insert(v) ;
	v=new Variable("attack",SYP_ATTACK,0) ;
	Insert(v) ;
	v=new Variable("decay",SYP_DECAY,0) ;
	Insert(v) ;
	v=new Variable("sustain",SYP_SUSTAIN,0xFF) ;
	Insert(v) ;
	// 0 keeps the old behaviour exactly: the voice is cut by the next
	// note, as it always was. Anything above 0 lets it ring out.
	v=new Variable("release",SYP_RELEASE,0x00) ;
	Insert(v) ;
	v=new Variable("pdx wave",SYP_PDXWAVE,pdxWaveNames,PWT_LAST,0) ;
	Insert(v) ;
	v=new Variable("mod",SYP_DCWAMT,0xC0) ;
	Insert(v) ;
	v=new Variable("mod attack",SYP_DCWATK,0x00) ;
	Insert(v) ;
	v=new Variable("mod decay",SYP_DCWDEC,0x60) ;
	Insert(v) ;
	v=new Variable("mod sustain",SYP_DCWSUS,0x40) ;
	Insert(v) ;
	v=new Variable("mod release",SYP_DCWREL,0x00) ;
	Insert(v) ;
	v=new Variable("vax wave",SYP_VAXWAVE,vaxWaveNames,VWT_LAST,0) ;
	Insert(v) ;
	v=new Variable("unison",SYP_UNISON,2) ;
	Insert(v) ;
	v=new Variable("detune",SYP_DETUNE,0x40) ;
	Insert(v) ;
	v=new Variable("pwm",SYP_PWM,0x80) ;
	Insert(v) ;
	v=new Variable("sub",SYP_SUB,0x00) ;
	Insert(v) ;
	v=new Variable("noise",SYP_NOISE,0x00) ;
	Insert(v) ;
	// Both default to zero, which is bypass, so every patch written
	// before they existed loads and sounds exactly as it did.
	v=new Variable("sync",SYP_SYNC,0x00) ;
	Insert(v) ;
	v=new Variable("ring",SYP_RING,0x00) ;
	Insert(v) ;
	v=new Variable("cutoff",SYP_CUTOFF,0xFF) ;
	Insert(v) ;
	v=new Variable("reso",SYP_RESO,0x00) ;
	Insert(v) ;
	v=new Variable("flt mode",SYP_FLTMODE,fltModeNames,VFM_LAST,0) ;
	Insert(v) ;
	v=new Variable("glide",SYP_GLIDE,0x00) ;
	Insert(v) ;
	v=new Variable("drive",SYP_DRIVE,0x00) ;
	Insert(v) ;
	v=new Variable("lfo dest",SYP_LFODEST,lfoDestNames,SLD_LAST,0) ;
	Insert(v) ;
	v=new Variable("lfo rate",SYP_LFORATE,0x40) ;
	Insert(v) ;
	v=new Variable("lfo depth",SYP_LFODEPTH,0x80) ;
	Insert(v) ;
	v=new Variable("algo",SYP_FMALGO,(char**)fmAlgoNames,FM_ALGO_COUNT,0) ;
	Insert(v) ;
	v=new Variable("feedback",SYP_FMFB,0x00) ;
	Insert(v) ;
	// A fresh FM patch is op3 -> op2 -> op1 with op4 silent: a two
	// stage chain, which is the electric-piano shape and the one that
	// sounds like something rather than like a sine.
	{
		static const int defRatio[FM_OPS]={1,1,3,1} ;
		static const int defLevel[FM_OPS]={0xFF,0x60,0x40,0x00} ;
		static const int defDecay[FM_OPS]={0x40,0x50,0x40,0x40} ;
		static const int defSus  [FM_OPS]={0xFF,0x80,0x40,0x00} ;
		static const char *ratioLbl[FM_OPS]=
			{"op1 ratio","op2 ratio","op3 ratio","op4 ratio"} ;
		static const char *levelLbl[FM_OPS]=
			{"op1 level","op2 level","op3 level","op4 level"} ;
		static const char *detLbl[FM_OPS]=
			{"op1 detune","op2 detune","op3 detune","op4 detune"} ;
		static const char *atkLbl[FM_OPS]=
			{"op1 attack","op2 attack","op3 attack","op4 attack"} ;
		static const char *decLbl[FM_OPS]=
			{"op1 decay","op2 decay","op3 decay","op4 decay"} ;
		static const char *susLbl[FM_OPS]=
			{"op1 sustain","op2 sustain","op3 sustain","op4 sustain"} ;
		for (int i=0;i<FM_OPS;i++) {
			v=new Variable(ratioLbl[i],fmRatioId[i],(char**)fmRatioNames,32,
			               defRatio[i]) ;
			Insert(v) ;
			v=new Variable(levelLbl[i],fmLevelId[i],defLevel[i]) ;
			Insert(v) ;
			v=new Variable(detLbl[i],fmDetId[i],7) ;   // 7 = no detune
			Insert(v) ;
			v=new Variable(atkLbl[i],fmAtkId[i],0x00) ;
			Insert(v) ;
			v=new Variable(decLbl[i],fmDecId[i],defDecay[i]) ;
			Insert(v) ;
			v=new Variable(susLbl[i],fmSusId[i],defSus[i]) ;
			Insert(v) ;
		}
	}

	v=new Variable("table",SYP_TABLE,-1) ;
	Insert(v) ;
	v=new Variable("table automation",SYP_TABLEAUTO,false) ;
	Insert(v) ;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		voice_[i].active_=false ;
	}
} ;

SynthInstrument::~SynthInstrument() {
} ;

bool SynthInstrument::Init() {

	// float only at table-build time, and only once for the class

	if (!tablesBuilt_) {
		placeTables() ;
		for (int i=0;i<128;i++) {
			double f=440.0*pow(2.0,(i-69)/12.0) ;
			noteInc_[i]=(unsigned int)(f*4294967296.0/SYNTH_RATE) ;
		}
		for (int i=0;i<256;i++) {
			sineTable_[i]=(short)(32000.0*sin(i*2.0*3.14159265358979/256.0)) ;
		}
		for (int i=0;i<1024;i++) {
			fmSin_[i]=(short)(32000.0*sin(i*2.0*3.14159265358979/1024.0)) ;
		}
		// Step to the next entry, so the interpolation below is a
		// multiply and an add rather than a subtract as well. This is
		// what msfa does with SIN_DELTA, for the same reason.
		for (int i=0;i<1024;i++) {
			fmSinD_[i]=(short)(fmSin_[(i+1)&1023]-fmSin_[i]) ;
		}
		for (int i=0;i<1025;i++) {
			cosTable_[i]=(short)(-32000.0*cos(i*2.0*3.14159265358979/1024.0)) ;
		}
		for (int i=0;i<256;i++) {
			// Exponential 30Hz..~12.5kHz, with f=2sin(pi fc/fs)
			// computed at TWICE the sample rate because the SVF is
			// ticked twice per sample.
			//
			// A Chamberlin run at the sample rate is only stable while
			// f stays well under 2, which put the ceiling at about
			// fs/6. The old table stopped at 7.6kHz for that reason and
			// then clamped f, and a clamped f is not a corner
			// frequency: measured, cutoff 255 with resonance was not a
			// 7.6kHz lowpass at all but nearly flat to 15kHz, with the
			// warp near the limit standing in for an open filter. The
			// knob was doing almost nothing across its top third.
			//
			// Ticking twice halves the effective rate the filter sees,
			// which buys an octave of headroom and lets the whole
			// range be real. Costs one extra pass of three multiplies.
			double fc=30.0*pow(2.0,i*8.7/255.0) ;
			double f=2.0*sin(3.14159265358979*fc/(2.0*SYNTH_RATE))*32768.0 ;
			if (f>30000.0) f=30000.0 ;
			cutTable_[i]=(short)f ;
		}
		for (int i=0;i<256;i++) {
			// lfo 0.05Hz..~16Hz exponential — slow sweeps to wobble
			double f=0.05*pow(2.0,i*8.33/255.0) ;
			lfoInc_[i]=(unsigned int)(f*4294967296.0/SYNTH_RATE) ;
		}
		tablesBuilt_=true ;
	}
	tableState_.Reset() ;
	return true ;
} ;

void SynthInstrument::OnStart() {
	tableState_.Reset() ;
	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		voice_[i].active_=false ;
	}
} ;

void SynthInstrument::Update(Observable &o,I_ObservableData *d) {
	// engine switch: the instrument screen rebuilds its rows
	SetChanged() ;
	NotifyObservers() ;
} ;

void SynthInstrument::startRamp(SynthRamp &r,int attack,bool fromCurrent) {

	if (!fromCurrent) {
		r.level_=0 ;
	}
	// attack restarts from the current level so voice stealing on a
	// busy channel doesn't step
	r.target_=ENV_ONE ;
	// attack 0 still fades: a level that jumps to full in one sample
	// is an audible pop on every note
	int samples=(attack==0)?DECLICK_FADE_SAMPLES:(1+attack*attack) ;  // .. ~1.5s
	r.step_=(ENV_ONE-r.level_)/samples ;
	if (r.step_==0) r.step_=1 ;
} ;

/* Walk the ramp down to silence over the release time. Called from
   Stop, so it starts wherever the envelope happened to be. */
void SynthInstrument::releaseRamp(SynthRamp &r,int release) {
	r.target_=0 ;
	// same curve as attack and decay: 0 is the shortest fade rather
	// than a hard cut, so even an instant release is not a click
	int samples=(release==0)?DECLICK_FADE_SAMPLES:(1+release*release*8) ;   // .. ~12s
	r.step_=-(r.level_/samples) ;
	if (r.step_==0) r.step_=-1 ;
} ;

// walk one sample toward target; the end of the attack arms the decay
inline void SynthInstrument::stepRamp(SynthRamp &r,fixed sustain,int decay) {

	if (r.step_>0) {
		r.level_+=r.step_ ;
		if (r.level_>=r.target_) {
			r.level_=r.target_ ;
			r.step_=0 ;
			if (r.target_==ENV_ONE && sustain<ENV_ONE) {
				r.target_=sustain ;
				// decay 0 glides too: a one-sample drop to sustain
				// clicks just like an instant attack
				int samples=(decay==0)?DECLICK_FADE_SAMPLES:(1+decay*decay*4) ;  // .. ~6s
				r.step_=-((ENV_ONE-sustain)/samples) ;
				if (r.step_==0) r.step_=-1 ;
			}
		}
	} else if (r.step_<0) {
		r.level_+=r.step_ ;
		if (r.level_<=r.target_) {
			r.level_=r.target_ ;
			r.step_=0 ;
		}
	}
} ;

bool SynthInstrument::Start(int channel,unsigned char note,bool retrigger) {

	if (note>127) return false ;

	// LGPT calls Start for EVERY note; the bool only means "the
	// channel changed instrument" (and audition passes false) — a
	// note here is ALWAYS a fresh trigger. Legato/slides come in
	// through the LEGA command instead. (Field bug: treating false
	// as legato left envelopes unarmed = silent previews.)

	SynthVoice &v=voice_[channel] ;
	int glide=FindVariable(SYP_GLIDE)->GetInt() ;

	// The player stops the old note before starting this one, so
	// active_ is always false by the time we get here and the whole
	// "busy channel" path below was unreachable -- the glide parameter
	// only ever did anything through an explicit LEGA. followsNote_ is
	// the player handing back what it knew and then threw away.
	//
	// Gated on glide: at 0, which is the default, nothing changes and a
	// repeated note re-attacks like a drum should. With glide set,
	// consecutive notes slide from the old pitch and don't retrigger the
	// envelope -- the 303 behaviour the parameter is named for.
	// One flag was answering two different questions, and once release
	// 0 became a short fade instead of a hard cut, every note had a
	// tail and the two answers stopped agreeing.
	//
	// "Is there a waveform in the air right now" decides whether this
	// note has a step to cancel and a level to ramp down from. A tail
	// counts: it is still being heard.
	//
	// "Is the previous NOTE still playing" decides glide and the
	// filter state. A tail is not a note -- the note it belonged to
	// already ended. Reading the tail as a live note made a note after
	// silence glide up from the pitch of the note before it.
	bool wasSounding=v.active_ ;
	bool wasActive=(v.active_&&!v.releasing_)||(v.followsNote_&&(glide>0)) ;
	v.followsNote_=false ;

	// per-voice command state starts from the instrument's settings;
	// commands then bend THIS voice only, and the next note resets it
	v.volume_=FindVariable(SYP_VOLUME)->GetInt() ;
	v.cutoff_=FindVariable(SYP_CUTOFF)->GetInt() ;
	v.reso_=FindVariable(SYP_RESO)->GetInt() ;
	v.modAmt_=FindVariable(SYP_DCWAMT)->GetInt() ;
	v.drive_=FindVariable(SYP_DRIVE)->GetInt() ;
	v.unison_=FindVariable(SYP_UNISON)->GetInt() ;
	v.detune_=FindVariable(SYP_DETUNE)->GetInt() ;
	v.lfoDepth_=FindVariable(SYP_LFODEPTH)->GetInt() ;
	v.lfoRate_=FindVariable(SYP_LFORATE)->GetInt() ;
	for (int i=0;i<FM_OPS;i++) {
		v.fmLevel_[i]=FindVariable(fmLevelId[i])->GetInt() ;
	}
	v.fmFeedback_=FindVariable(SYP_FMFB)->GetInt() ;
	v.cmdLocks_=0 ;           // a new note answers to the knobs again
	v.releasing_=false ;      // a new note cancels any tail
	v.pan_=0x7F ;
	v.glideCoef_=(glide==0)?0:(32768/(1+((glide*glide)>>7))) ;
	v.baseNote_=note ;
	v.vibSpeed_=0 ;
	v.vibDepth_=0 ;
	v.vibPhase_=0 ;
	v.vibMul_=65536u ;
	v.arpOn_=false ;
	v.arpStep_=0 ;
	v.arpData_=0 ;
	v.arpTick_=0 ;
	// arpRate_ deliberately survives: it is set once for a part and a
	// note-on should not throw it away.
	if (v.arpRate_==0) v.arpRate_=1 ;
	v.rtgTicks_=0 ;
	v.rtgCount_=0 ;
	v.rtgVolStep_=0 ;
	v.rtgPitchStep_=0 ;
	v.cmdGain_=256 ;
	v.rtgPitchAcc_=0 ;
	v.transpose_=0 ;
	v.tickAcc_=0 ;
	v.tickLen_=tickLength() ;

	v.phaseInc_=noteInc_[note] ;
	// This path sets the increment directly rather than going through
	// setVoicePitch, so the note has to be recorded here too --
	// VIBR 0000 asks for it back, and without this it would restore
	// whatever the previous note left behind.
	v.pitchNote_=(signed short)note ;
	lastNote_[channel]=note ;
	v.phase_=0 ;
	// unison voices start phase-spread so the stack doesn't cancel
	// into a mono saw for the first cycle
	for (int j=0;j<VAX_MAX_UNISON;j++) {
		v.uphase_[j]=(unsigned int)j*0x24924925u ;
	}
	v.subPhase_=0 ;
	v.syncPhase_=0 ;
	v.lfoPhase_=0 ;
	if (v.rng_==0) v.rng_=0x1234567+channel ;
	if (!wasActive) {
		v.svfLow_=0 ;
		v.svfBand_=0 ;
		v.curInc_=v.phaseInc_ ;
		v.click_=0 ;
	} else if (glide==0) {
		v.curInc_=v.phaseInc_ ;
	}
	// glide>0 on a busy channel: 303 style, walk from the old pitch

	if (wasSounding) {
		// A new note over a sounding one resets the phase, so the
		// oscillator jumps from wherever the old note's wave was to
		// whatever the new one starts at.
		//
		// Adding lastOut_ does NOT cancel that. It makes the first
		// sample come out as new(phase 0) + old, which is a step of
		// new(phase 0) -- and for a square or a pulse that is very
		// nearly full scale. Measured at 18% of full scale on a
		// square, on EVERY note in a pattern.
		//
		// Arm the measured declick instead, the same one an engine
		// change uses: it reads the actual first sample and cancels
		// exactly the step that is there, whatever the waveform
		// happens to start at.
		v.declickPending_=true ;
	}

	// Only the AMPLITUDE envelope carries over from a tail, and only so
	// the level does not step. Everything that shapes the timbre --
	// the phase distortion envelope, the operator envelopes -- starts
	// again, because this is a new note and not a continuation of the
	// one that just faded out.
	startRamp(v.amp_,FindVariable(SYP_ATTACK)->GetInt(),wasSounding) ;
	startRamp(v.mod_,FindVariable(SYP_DCWATK)->GetInt(),wasActive) ;
	// operator phases reset with the voice, so a retrigger is a clean
	// attack rather than a note that starts mid-timbre
	startFmOps(v,wasActive) ;

	v.active_=true ;
	return true ;
} ;

void SynthInstrument::Stop(int channel) {
	SynthVoice &v=voice_[channel] ;

	// A voice that is not sounding has nothing to fade.
	int release=FindVariable(SYP_RELEASE)->GetInt() ;
	if (!v.active_) {
		v.active_=false ;
		v.releasing_=false ;
		return ;
	}

	// Release 0 used to cut the voice where it stood. releaseRamp
	// already treats 0 as the shortest fade rather than a stop, for the
	// same reason attack 0 is a fade, so let it: 1.5ms is far too
	// short to hear as a release and it is the difference between a
	// note that ends and a note that stops. It matters most where the
	// next note starts on the following step, because then the step is
	// heard as a click on the note coming in.

	// Otherwise both envelopes walk down and the channel keeps
	// rendering us until IsReleasing goes false.
	releaseRamp(v.amp_,release) ;
	releaseRamp(v.mod_,FindVariable(SYP_DCWREL)->GetInt()) ;
	releaseFmOps(v) ;
	v.releasing_=true ;
} ;

bool SynthInstrument::IsReleasing(int channel) {
	SynthVoice &v=voice_[channel] ;
	return v.active_&&v.releasing_ ;
} ;

// The player calls this immediately before Start when this same
// instrument already had a note sounding on the channel.
void SynthInstrument::NoteFollowsNote(int channel) {
	voice_[channel].followsNote_=true ;
} ;

// re-arm the envelopes and reset phase, carrying the step as a click
// tail — shared by note-on retrigger and the RTRG command
void SynthInstrument::retrigger(SynthVoice &v) {
	// Same correction as note-on: measure the step rather than
	// assuming the new wave starts at zero. The measured path clamps
	// itself, so repeated retriggers cannot stack into a DC blast the
	// way the additive version could (drill rolls hit that).
	v.declickPending_=true ;
	v.phase_=0 ;
	for (int j=0;j<VAX_MAX_UNISON;j++) {
		v.uphase_[j]=(unsigned int)j*0x24924925u ;
	}
	v.subPhase_=0 ;
	v.syncPhase_=0 ;
	startRamp(v.amp_,FindVariable(SYP_ATTACK)->GetInt(),false) ;
	startRamp(v.mod_,FindVariable(SYP_DCWATK)->GetInt(),false) ;
}

// set the voice's target pitch from an absolute note; glideCoef_
// decides whether the walk is instant or a slide
void SynthInstrument::setVoicePitch(SynthVoice &v,int note) {
	// Remembered before the transpose is folded in, so that calling
	// this again with it -- which VIBR does every tick -- lands on the
	// same pitch rather than transposing a second time.
	v.pitchNote_=(signed short)note ;
	// the transpose rides on top of whatever asked for this pitch
	note+=v.transpose_ ;
	if (note<0) note=0 ;
	if (note>127) note=127 ;
	v.phaseInc_=noteInc_[note] ;
	if (v.vibMul_!=65536u) {
		// Q16 multiply in 64 bits: at eight semitones of depth the
		// ratio reaches 1.59, which would overflow a 32 bit product
		// for the top octave.
		v.phaseInc_=(unsigned int)
		    (((unsigned long long)v.phaseInc_*v.vibMul_)>>16) ;
	}
	if (v.glideCoef_==0) {
		v.curInc_=v.phaseInc_ ;
	}
}

// tick-rate command work. Called by the renderers with the sample
// count they just produced; ARPG walks its nibbles and RTRG re-arms
// on tick boundaries.
void SynthInstrument::serviceTicks(SynthVoice &v,int channel,int samples) {

	if (!v.arpOn_ && v.rtgTicks_==0 && v.vibSpeed_==0) return ;

	v.tickAcc_+=samples ;
	while (v.tickAcc_>=v.tickLen_) {
		v.tickAcc_-=v.tickLen_ ;

		if (v.vibSpeed_ && v.vibDepth_) {
			float semis=VibratoSemitones(v.vibPhase_,v.vibSpeed_,v.vibDepth_) ;
			v.vibMul_=(unsigned int)(pow(2.0,semis/12.0)*65536.0+0.5) ;
			setVoicePitch(v,v.pitchNote_) ;
		}

		if (v.arpOn_ && (++v.arpTick_>=(v.arpRate_?v.arpRate_:1))) {
			v.arpTick_=0 ;
			// abcd = four relative semitone steps from the base note;
			// trailing zeros shorten the cycle (LSDJ style)
			int shifts[4]={(v.arpData_>>12)&0xF,(v.arpData_>>8)&0xF,
			               (v.arpData_>>4)&0xF,v.arpData_&0xF} ;
			int len=4 ;
			while (len>1 && shifts[len-1]==0) len-- ;
			v.arpStep_=(unsigned char)((v.arpStep_+1)%len) ;
			setVoicePitch(v,v.baseNote_+shifts[v.arpStep_]) ;
			lastNote_[channel]=(unsigned char)(v.baseNote_+shifts[v.arpStep_]) ;
		}

		if (v.rtgTicks_) {
			if (++v.rtgCount_>=v.rtgTicks_) {
				v.rtgCount_=0 ;
				/* Each repeat can be quieter and higher than the last,
				   which is the difference between a machine gun and a
				   drill. The accumulators are separate from the
				   voice's own volume and pitch so that the ramp is
				   undone cleanly by the next note rather than being
				   baked into the patch. */
				if (v.rtgVolStep_) {
					int g=(int)v.cmdGain_+v.rtgVolStep_ ;
					if (g<0) g=0 ;
					if (g>256) g=256 ;
					v.cmdGain_=(unsigned short)g ;
				}
				if (v.rtgPitchStep_) {
					v.rtgPitchAcc_+=v.rtgPitchStep_ ;
					int n=v.baseNote_+v.rtgPitchAcc_ ;
					if (n<0) n=0 ;
					if (n>127) n=127 ;
					setVoicePitch(v,n) ;
					lastNote_[channel]=(unsigned char)n ;
				}
				retrigger(v) ;
			}
		}
	}
}

static inline fixed sustainFromParam(int p) {
	return (p==0xFF)?ENV_ONE:p*(ENV_ONE>>8) ;
}

// PAN_ 00..FE -> Q15 gains. Linear-ish with a -3dB-ish centre so a
// centred voice isn't louder than a panned one.
static inline void panLR(int pan,int &l,int &r) {
	if (pan<0) pan=0 ;
	if (pan>0xFE) pan=0xFE ;
	int right=(pan*32768)/0xFE ;      // 0..32768
	int left=32768-right ;
	// sqrt-ish taper: gain = 2*sqrt(x*(1-x)) at centre = 1.0
	l=(int)((left*46341LL)>>15) ;     // *1.414
	r=(int)((right*46341LL)>>15) ;
	if (l>32768) l=32768 ;
	if (r>32768) r=32768 ;
}

// samples between one-pole glide steps
#define GLIDE_TICK 16

// One-pole walk of curInc_ toward the target, at the voice's glide
// rate — shared by glide, LEGA, PTCH and ARPG for every engine.
//
// 'samples' is how much of the block this call covers. TONE and PDX
// used to take exactly one step per Render call, and a Render block is
// the tempo's slice size — so the same glide setting slid at a
// different speed at 90bpm than at 170, and differently again from
// VAX, which stepped every 16 samples. Stepping per GLIDE_TICK samples
// makes the rate mean one thing everywhere.
static inline void glideStep(SynthVoice &v,int samples) {
	if (v.glideCoef_==0) {
		v.curInc_=v.phaseInc_ ;
		return ;
	}
	if (v.curInc_==v.phaseInc_) return ;
	int n=samples/GLIDE_TICK ;
	if (n<1) n=1 ;
	while (n--) {
		long long diff=(long long)v.phaseInc_-(long long)v.curInc_ ;
		long long st=(diff*v.glideCoef_)>>15 ;
		if (st==0) {
			v.curInc_=v.phaseInc_ ;
			return ;
		}
		v.curInc_=(unsigned int)((long long)v.curInc_+st) ;
	}
}

bool SynthInstrument::Render(int channel,fixed *buffer,int size,bool updateTick) {

	SynthVoice &v=voice_[channel] ;
	if (!v.active_) return false ;

	// The release has run out: retire the voice. The channel checks
	// IsReleasing straight after this and lets the instrument go.
	if (v.releasing_&&(v.amp_.level_<=0)) {
		v.active_=false ;
		v.releasing_=false ;
		return false ;
	}

	// ARPG steps and RTRG retriggers land on tick boundaries
	serviceTicks(v,channel,size) ;

	// Seeded at note-on, these used to stay put for the life of the
	// note, so moving cutoff or volume on the instrument screen did
	// nothing until you played the next one -- and cutoff is the knob
	// people reach for mid-note. Follow the instrument for anything a
	// command hasn't claimed on this voice.
	if (!(v.cmdLocks_&SVL_VOLUME)) v.volume_=FindVariable(SYP_VOLUME)->GetInt() ;
	if (!(v.cmdLocks_&SVL_CUTOFF)) v.cutoff_=FindVariable(SYP_CUTOFF)->GetInt() ;
	if (!(v.cmdLocks_&SVL_RESO))   v.reso_=FindVariable(SYP_RESO)->GetInt() ;
	if (!(v.cmdLocks_&SVL_MODAMT)) v.modAmt_=FindVariable(SYP_DCWAMT)->GetInt() ;
	if (!(v.cmdLocks_&SVL_DRIVE))  v.drive_=FindVariable(SYP_DRIVE)->GetInt() ;
	if (!(v.cmdLocks_&SVL_UNISON)) v.unison_=FindVariable(SYP_UNISON)->GetInt() ;
	if (!(v.cmdLocks_&SVL_DETUNE)) v.detune_=FindVariable(SYP_DETUNE)->GetInt() ;
	if (!(v.cmdLocks_&SVL_LFODEP)) v.lfoDepth_=FindVariable(SYP_LFODEPTH)->GetInt() ;
	if (!(v.cmdLocks_&SVL_LFORAT)) v.lfoRate_=FindVariable(SYP_LFORATE)->GetInt() ;
	if (!(v.cmdLocks_&SVL_FMLV1)) v.fmLevel_[0]=FindVariable(SYP_FML1)->GetInt() ;
	if (!(v.cmdLocks_&SVL_FMLV2)) v.fmLevel_[1]=FindVariable(SYP_FML2)->GetInt() ;
	if (!(v.cmdLocks_&SVL_FMLV3)) v.fmLevel_[2]=FindVariable(SYP_FML3)->GetInt() ;
	if (!(v.cmdLocks_&SVL_FMLV4)) v.fmLevel_[3]=FindVariable(SYP_FML4)->GetInt() ;
	if (!(v.cmdLocks_&SVL_FMFB))  v.fmFeedback_=FindVariable(SYP_FMFB)->GetInt() ;

	int engine=FindVariable(SYP_ENGINE)->GetInt() ;
	// The wave selector is per-engine, so read the one this engine
	// actually uses; otherwise switching PDX waves looks like no change.
	int wave=(engine==SET_PDX)?FindVariable(SYP_PDXWAVE)->GetInt():
	         (engine==SET_VAX)?FindVariable(SYP_VAXWAVE)->GetInt():
	         (engine==SET_FM) ?FindVariable(SYP_FMALGO)->GetInt():
	                           FindVariable(SYP_WAVE)->GetInt() ;
	// Editing engine or wave under a held note hands the next block to a
	// different oscillator, which picks up at a different amplitude --
	// measured at 6x the natural step of a sine, about 11% of full
	// scale. Arm the measured declick so the first new sample cancels
	// its own step.
	if ((engine!=v.lastEngine_)||(wave!=v.lastWave_)) {
		v.declickPending_=true ;
		v.lastEngine_=engine ;
		v.lastWave_=wave ;
	}

	switch (engine) {
		case SET_PDX:
			return renderPdx(v,buffer,size) ;
		case SET_VAX:
			return renderVax(v,buffer,size) ;
		case SET_FM:
			return renderFm(v,buffer,size) ;
		case SET_TONE:
		default:
			return renderTone(v,buffer,size) ;
	}
} ;

bool SynthInstrument::renderTone(SynthVoice &v,fixed *buffer,int size) {

	int wave=FindVariable(SYP_WAVE)->GetInt() ;
	fixed vol=(fixed)(((int)v.volume_*v.cmdGain_)>>8)<<7 ;
	fixed sustain=sustainFromParam(FindVariable(SYP_SUSTAIN)->GetInt()) ;
	int decay=FindVariable(SYP_DECAY)->GetInt() ;
	int panL,panR ;
	panLR(v.pan_,panL,panR) ;

	// TONE was a single wave selector and nothing else -- one control
	// on a page sized for eight. Sub, noise and pulse width are the
	// three the VAX engine already implements, so they cost a read of
	// a parameter that is zero on every patch written before this.
	int pwm=FindVariable(SYP_PWM)->GetInt() ;
	unsigned int pwThresh=(unsigned int)(16+((pwm*224)>>8))<<24 ;
	int subQ=FindVariable(SYP_SUB)->GetInt()<<7 ;
	int noiseQ=FindVariable(SYP_NOISE)->GetInt()<<7 ;
	unsigned int rng=v.rng_ ;

	// ...and no LFO either, so pointing lfo dest at anything on this
	// engine did nothing at all
	int lfoDest=FindVariable(SYP_LFODEST)->GetInt() ;
	unsigned int lfoIncV=lfoInc_[v.lfoRate_&0xFF] ;
	int lfoDepth=v.lfoDepth_<<7 ;

	unsigned int phase=v.phase_ ;
	// PTCH/ARPG/glide walk curInc_ toward the target for every engine,
	// stepped inside the loop so a long block glides further than a
	// short one instead of the same amount
	unsigned int inc=v.curInc_ ;
	// reciprocals for the band limiting, recomputed only when the
	// pitch moves rather than at every edge
	unsigned int rcp=blepRcp(inc) ;
	unsigned int subRcp=blepRcp(inc>>1) ;
	int refresh=0 ;

	// TONE had no filter, so FCUT, FRES and FLTR all did nothing on the
	// engine that happens to be the default -- reach for a filter sweep
	// on a fresh instrument and nothing happens. Run the same Chamberlin
	// SVF the VAX engine uses, but only when it would change something:
	// wide open with no resonance skips it entirely, so every patch
	// written before this sounds identical and costs the same.
	int cut=v.cutoff_&0xFF ;
	int resoP=v.reso_&0xFF ;
	bool filtered=(cut<0xFF)||(resoP>0)||
	              ((lfoDest==SLD_FILTER)&&lfoDepth) ;
	int fltMode=FindVariable(SYP_FLTMODE)->GetInt() ;
	int qQ=32768-(resoP<<7) ;
	if (qQ<1024) qQ=1024 ;
	int f=cutTable_[cut] ;
	int low=v.svfLow_ ;
	int band=v.svfBand_ ;

	fixed *out=buffer ;
	for (int i=0;i<size;i++) {

		if (refresh==0) {
			refresh=GLIDE_TICK ;
			glideStep(v,GLIDE_TICK) ;
			inc=v.curInc_ ;
			rcp=blepRcp(inc) ;
			subRcp=blepRcp(inc>>1) ;

			// the LFO runs at block rate like the VAX engine's: a
			// vibrato does not need a sample-rate sine of its own
			int mod=0 ;
			if (lfoDest!=SLD_OFF && lfoDepth) {
				v.lfoPhase_+=lfoIncV*GLIDE_TICK ;
				mod=(sineTable_[v.lfoPhase_>>24]*lfoDepth)>>15 ;
			}
			if (lfoDest==SLD_PITCH && mod) {
				// +-~1 semitone at full depth, same law as VAX
				inc=(unsigned int)((long long)inc+
					((long long)inc*mod)/557056) ;
			}
			if (lfoDest==SLD_PWM) {
				int pwmEff=pwm+(mod>>8) ;
				if (pwmEff<0) pwmEff=0 ;
				if (pwmEff>255) pwmEff=255 ;
				pwThresh=(unsigned int)(16+((pwmEff*224)>>8))<<24 ;
			}
			if (filtered) {
				int cutEff=cut ;
				if (lfoDest==SLD_FILTER) cutEff+=mod>>8 ;
				if (cutEff>255) cutEff=255 ;
				if (cutEff<0) cutEff=0 ;
				f=cutTable_[cutEff] ;
			}
		}
		refresh-- ;

		int s ;
		unsigned int p16=phase>>16 ;
		switch (wave) {
			case SWT_SQUARE:
				// Band limited, and built from two saws so it carries
				// no DC at any width. The old threshold form was
				// bit-identical to a naive pulse and left
				// (2*duty-1)*24576 sitting on the channel whenever the
				// width was off centre. Scaled back to the same 24576
				// peak the threshold version had, so patch levels do
				// not move.
				s=(pulseBlep(phase,inc,rcp,pwThresh)*24576)>>15 ;
				break ;
			case SWT_TRIANGLE:
				s=(p16<0x8000)?((int)p16*2-32768):(98302-(int)p16*2) ;
				break ;
			case SWT_SINE:
				s=sineTable_[phase>>24] ;
				break ;
			case SWT_SAW:
			default:
				s=sawBlep(phase,inc,rcp) ;
				break ;
		}
		phase+=inc ;

		if (subQ) {
			// band limited too: the sub is an octave down, so it
			// aliases less than the main oscillator, but it is also
			// the loudest thing in a bass patch
			int sb=(pulseBlep(v.subPhase_,inc>>1,subRcp,0x80000000u)*24576)>>15 ;
			s+=(sb*subQ)>>15 ;
		}
		v.subPhase_+=inc>>1 ;
		if (noiseQ) {
			rng=rng*1664525u+1013904223u ;
			int nz=(int)(short)(rng>>16) ;
			s+=(nz*noiseQ)>>15 ;
		}
		if (s>32700) s=32700 ;
		if (s<-32700) s=-32700 ;

		if (filtered) {
			// same 64-bit widening as the VAX path: these multiply a
			// Q15 coefficient by state clamped at 1<<19, which does not
			// fit an int
			int mix=s ;
			// two passes at the same input: the filter runs at twice
			// the sample rate so the coefficient stays in the region
			// where a Chamberlin actually behaves like the filter it
			// is meant to be
			int high=0 ;
			for (int pass=0;pass<2;pass++) {
				low+=(int)(((long long)f*band)>>15) ;
				high=mix-low-(int)(((long long)qQ*band)>>15) ;
				band+=(int)(((long long)f*high)>>15) ;
				if (band>SVF_CLAMP) band=SVF_CLAMP ;
				if (band<-SVF_CLAMP) band=-SVF_CLAMP ;
				if (low>SVF_CLAMP) low=SVF_CLAMP ;
				if (low<-SVF_CLAMP) low=-SVF_CLAMP ;
				if (high>SVF_CLAMP) high=SVF_CLAMP ;
				if (high<-SVF_CLAMP) high=-SVF_CLAMP ;
			}
			s=(fltMode==VFM_LP)?low:((fltMode==VFM_BP)?band:high) ;
			if (s>32700) s=32700 ;
			if (s<-32700) s=-32700 ;
		}

		stepRamp(v.amp_,sustain,decay) ;

		fixed smp=fp_mul(s*(v.amp_.level_>>8),vol) ;
		applyVoiceClick(v,smp) ;
		// samples are Q15 (full scale ~1.07e9) — the pan multiply
		// MUST widen or it wraps
		*out++=(fixed)(((long long)smp*panL)>>15) ;
		*out++=(fixed)(((long long)smp*panR)>>15) ;
	}

	v.phase_=phase ;
	v.rng_=rng ;
	v.svfLow_=low ;
	v.svfBand_=band ;
	return true ;
} ;

bool SynthInstrument::renderPdx(SynthVoice &v,fixed *buffer,int size) {

	int wave=FindVariable(SYP_PDXWAVE)->GetInt() ;
	fixed vol=(fixed)(((int)v.volume_*v.cmdGain_)>>8)<<7 ;
	fixed ampSus=sustainFromParam(FindVariable(SYP_SUSTAIN)->GetInt()) ;
	int ampDec=FindVariable(SYP_DECAY)->GetInt() ;
	fixed dcwSus=sustainFromParam(FindVariable(SYP_DCWSUS)->GetInt()) ;
	int dcwDec=FindVariable(SYP_DCWDEC)->GetInt() ;
	int dcwAmt=v.modAmt_<<7 ;                              // 0..FF -> ~Q15
	int panL,panR ;
	panLR(v.pan_,panL,panR) ;

	int lfoDest=FindVariable(SYP_LFODEST)->GetInt() ;
	unsigned int lfoIncV=lfoInc_[v.lfoRate_&0xFF] ;
	int lfoDepth=v.lfoDepth_<<7 ;

	unsigned int phase=v.phase_ ;
	unsigned int inc=v.curInc_ ;

	// warp state, recomputed every 32 samples from the DCW envelope
	// (two divisions per refresh, nothing per sample)
	unsigned int k=32768,slope1=65536,slope2=65536,resoMul=65536 ;
	int refresh=0 ;

	fixed *out=buffer ;
	for (int i=0;i<size;i++) {

		if (refresh==0) {
			refresh=32 ;
			int mod=0 ;
			if (lfoDest!=SLD_OFF && lfoDepth) {
				v.lfoPhase_+=lfoIncV*32 ;
				mod=(sineTable_[v.lfoPhase_>>24]*lfoDepth)>>15 ;
			}
			glideStep(v,32) ;   // this refresh covers 32 samples
			inc=v.curInc_ ;
			if (lfoDest==SLD_PITCH && mod) {
				inc=(unsigned int)((long long)inc+((long long)inc*mod)/557056) ;
			}
			int w15=(dcwAmt*(v.mod_.level_>>8))>>15 ;      // Q15 warp
			if (lfoDest==SLD_FILTER) {
				w15+=mod>>2 ;                              // the pd wobble
				if (w15<0) w15=0 ;
				if (w15>32640) w15=32640 ;
			}
			if (wave<=PWT_SQUARE) {
				k=32768-(unsigned int)(((unsigned int)w15*(32768-PDX_KNEE_MIN))>>15) ;
				slope1=(unsigned int)((32768u<<16)/k) ;
				slope2=(unsigned int)((32768u<<16)/(65536-k)) ;
			} else {
				resoMul=65536+(unsigned int)(((unsigned long long)w15*15*65536)>>15) ;
			}
		}
		refresh-- ;

		unsigned int p16=phase>>16 ;
		int s ;
		switch (wave) {

			case PWT_SQUARE: {
				// each half: fast cosine half-turn inside the knee,
				// then hold at the endpoint
				// same bound as the saw arm: the compare against k is
				// what keeps the product inside 32 bits
				unsigned int q ;
				if (p16<32768) {
					q=(p16<k)?((p16*slope1)>>16):32768 ;
				} else {
					unsigned int lp=p16-32768 ;
					if (lp<k) {
						q=32768+((lp*slope1)>>16) ;
						if (q>65535) q=65535 ;
					} else {
						q=0 ;   // cos(0)==cos(2pi): the high hold
					}
				}
				s=cosLookup(cosTable_,q>>6,q&0x3F) ;
				break ;
			}

			case PWT_RESO_SAW:
			case PWT_RESO_TRI: {
				// resonant cosine hard-synced to the carrier (derived
				// from it, so sync is free), amplitude-windowed.
				// CZ trick: window (cos-min) so the value is 0 at every
				// resonant wrap — no step when the window snaps back.
				unsigned int pr=(unsigned int)(((unsigned long long)phase*resoMul)>>16) ;
				int c=cosLookup(cosTable_,pr>>22,(pr>>16)&0x3F)+32000 ; // 0..64000, 0 at wrap
				unsigned int win=(wave==PWT_RESO_SAW)?
					(65536-p16):
					((p16<32768)?p16*2:(65536-p16)*2) ;
				// c is 0..64000 and win 0..65536, so this lands in
				// -16000..+48000: mean-centred, but half again the peak
				// of the cosine waves above, which sit at +/-32000.
				// Measured at 38% hotter, so picking a resonant wave
				// jumped the level and clipped that much sooner once
				// volume and the filter were in. Scale to the same
				// ceiling rather than leave the waves mismatched.
				s=(int)(((long long)c*win)>>16)-16000 ;
				s=(s*2)/3 ;
				break ;
			}

			case PWT_SAW:
			default: {
				// The branch already bounds the product. slope1 is
				// (32768<<16)/k, and this arm only runs when p16 < k,
				// so p16*slope1 < 32768<<16, which is 2^31 exactly. The
				// same holds on the other side with (65536-k). Neither
				// has ever needed to widen to 64 bit, and this runs
				// once a sample for every voice on the engine.
				unsigned int q ;
				if (p16<k) {
					q=(p16*slope1)>>16 ;
				} else {
					q=32768+(((p16-k)*slope2)>>16) ;
					if (q>65535) q=65535 ;
				}
				s=cosLookup(cosTable_,q>>6,q&0x3F) ;
				break ;
			}
		}
		phase+=inc ;

		stepRamp(v.amp_,ampSus,ampDec) ;
		stepRamp(v.mod_,dcwSus,dcwDec) ;

		fixed smp=fp_mul(s*(v.amp_.level_>>8),vol) ;
		applyVoiceClick(v,smp) ;
		// samples are Q15 (full scale ~1.07e9) — the pan multiply
		// MUST widen or it wraps
		*out++=(fixed)(((long long)smp*panL)>>15) ;
		*out++=(fixed)(((long long)smp*panR)>>15) ;
	}

	v.phase_=phase ;
	return true ;
} ;


/* ---- FM (4 operator) ---------------------------------------------

   Four sine operators, each with its own ratio, level, detune and
   A/D/S. What feeds what is the algorithm; operators that reach the
   output are summed and divided by how many there are, so switching
   from one carrier to four does not change the level by 12 dB.

   Everything is integer. Phase is Q32 in the operator's own
   accumulator, so a modulator's contribution is just added to the
   carrier's phase before the table lookup -- which is the whole
   trick, and why FM is cheap enough to run eight of these next to
   the sampler.

   The global amp envelope still sits on top: it is what gives the
   voice its release, and it is what VOLM and the amp panel move. The
   per-operator envelopes are for the timbre.                       */

/* The per-operator frequency multiplier, ratio and detune folded
   together, in Q16. Read once per block: FindVariable is a linear
   search that allocates an iterator, and this instrument now carries
   fifty-odd variables. Calling it eight times every sixteen samples
   -- which is what the first version of setFmPitch did through the
   glide refresh -- cost four times the entire rest of the engine.

   Q16 and not the ratio table's own Q8: detune is a few cents, and
   at Q8 a ratio of 1 is 256, so the whole detune range truncated to
   nothing. */
void SynthInstrument::fmMultipliers(int *mulQ16) {
	for (int i=0;i<FM_OPS;i++) {
		int ridx=FindVariable(fmRatioId[i])->GetInt() ;
		if (ridx<0) ridx=0 ;
		if (ridx>31) ridx=31 ;
		int m=((int)fmRatioMul[ridx])<<8 ;
		// detune is stored 0..14 with 7 as centre; a step is m/2048,
		// so the full swing is about +/-0.34% -- a few cents, which
		// is what makes two carriers at the same ratio beat instead
		// of cancel
		int det=FindVariable(fmDetId[i])->GetInt()-7 ;
		if (det) m+=(m*det)>>11 ;
		if (m<1) m=1 ;
		mulQ16[i]=m ;
	}
}

void SynthInstrument::setFmPitch(SynthVoice &v) {
	int mulQ16[FM_OPS] ;
	fmMultipliers(mulQ16) ;
	for (int i=0;i<FM_OPS;i++) {
		v.op_[i].inc_=(unsigned int)
			(((unsigned long long)v.curInc_*mulQ16[i])>>16) ;
	}
}

void SynthInstrument::startFmOps(SynthVoice &v,bool fromCurrent) {
	for (int i=0;i<FM_OPS;i++) {
		if (!fromCurrent) v.op_[i].phase_=0 ;
		startRamp(v.op_[i].env_,FindVariable(fmAtkId[i])->GetInt(),fromCurrent) ;
	}
	if (!fromCurrent) {
		v.fbLast_[0]=0 ;
		v.fbLast_[1]=0 ;
	}
	setFmPitch(v) ;
}

void SynthInstrument::releaseFmOps(SynthVoice &v) {
	// The operator envelopes have no release of their own -- the
	// global amp envelope owns the tail. Holding them where they are
	// keeps the timbre steady while the voice fades, which is what a
	// released FM note does.
	for (int i=0;i<FM_OPS;i++) {
		v.op_[i].env_.step_=0 ;
		v.op_[i].env_.target_=v.op_[i].env_.level_ ;
	}
}

bool SynthInstrument::renderFm(SynthVoice &v,fixed *buffer,int size) {

	fixed vol=(fixed)(((int)v.volume_*v.cmdGain_)>>8)<<7 ;
	fixed sustain=sustainFromParam(FindVariable(SYP_SUSTAIN)->GetInt()) ;
	int decay=FindVariable(SYP_DECAY)->GetInt() ;
	int panL,panR ;
	panLR(v.pan_,panL,panR) ;

	int algo=FindVariable(SYP_FMALGO)->GetInt() ;
	if (algo<0) algo=0 ;
	if (algo>=FM_ALGO_COUNT) algo=FM_ALGO_COUNT-1 ;
	const signed char *dest=fmAlgoDest[algo] ;

	int level[FM_OPS],opSus[FM_OPS],opDec[FM_OPS] ;
	int carriers=0 ;
	for (int i=0;i<FM_OPS;i++) {
		level[i]=v.fmLevel_[i]&0xFF ;
		opSus[i]=sustainFromParam(FindVariable(fmSusId[i])->GetInt()) ;
		opDec[i]=FindVariable(fmDecId[i])->GetInt() ;
		if (dest[i]==FM_OUT && level[i]>0) carriers++ ;
	}
	if (carriers==0) carriers=1 ;
	/* Dividing the carrier sum by how many carriers there are was an
	   integer division PER SAMPLE. On this chip div is about thirty
	   five cycles and does not pipeline, which is the single most
	   expensive instruction in the engine and it was running 44100
	   times a second per voice.
	   
	   carriers is 1 to 4 and fixed for the whole render, so the
	   reciprocal is known before the loop starts. Exact for one, two
	   and four; for three it is 21845/65536, which is within an LSB
	   of a 16 bit sample. The shift floors where the division
	   truncated toward zero, so a negative sum can land one LSB
	   lower than it used to -- inaudible, and arguably the better
	   rounding, since truncation toward zero biases away from it. */
	const int carrierRecip=(carriers==3)?21845:(65536/carriers) ;
	// Self-modulation is a feedback loop, not open-loop PM: below a
	// critical gain it settles back to very nearly a sine, and above
	// it the tone breaks into a saw and then into noise. Measured,
	// that threshold sits at a raw 0x58 out of 0xFF, so a linear
	// control spends its bottom third doing nothing at all. Scaling
	// by 0xB0/0x100 lands the threshold at half travel: clean below,
	// the saw at the middle, chaos at the top.
	int fbAmt=((v.fmFeedback_&0xFF)*0xB0)>>8 ;

	// ratio and detune are patch knobs, so follow them under a held
	// note the same way cutoff does -- read once here, not once per
	// glide refresh
	int mulQ16[FM_OPS] ;
	fmMultipliers(mulQ16) ;
	setFmPitch(v) ;

	int cut=v.cutoff_&0xFF ;
	int resoP=v.reso_&0xFF ;
	bool filtered=(cut<0xFF)||(resoP>0) ;
	int fltMode=FindVariable(SYP_FLTMODE)->GetInt() ;
	int qQ=32768-(resoP<<7) ;
	if (qQ<1024) qQ=1024 ;
	int f=cutTable_[cut] ;
	int low=v.svfLow_ ;
	int band=v.svfBand_ ;

	/* Scalars, not arrays, and the operator loop below is written out
	   four times rather than looped.
	 *
	 * MIPS cannot index a register file, so an array subscripted by a
	 * loop variable is a load whatever the optimiser does -- and this
	 * loop ran four times per sample for every sample of every FM
	 * voice. Measured against the other three engines it showed up as
	 * 5.6 memory operations per multiply where they do 2.6 to 3.4:
	 * the most memory traffic and the fewest multiplies of any engine
	 * here, on the row that is already 85% of the budget at eight
	 * voices on hardware.
	 *
	 * The compiler will not do this itself. Its own unroll heuristic
	 * declines on a body this size, and forcing it with a pragma
	 * moved no work at all -- because fed[dest[i]] is a scatter with
	 * a runtime index, and one of those pins the whole array in
	 * memory however much the loop is unrolled around it.
	 *
	 * Written out, three other things fall away with it: the
	 * i==FM_OPS-1 feedback tests are dead in three blocks out of
	 * four, op0's destination is FM_OUT in every algorithm so it
	 * needs no routing at all, and nothing in the table ever feeds
	 * op3, so the fed slot it was reading every sample was always
	 * zero.                                                        */
	unsigned int ph0=v.op_[0].phase_,ph1=v.op_[1].phase_ ;
	unsigned int ph2=v.op_[2].phase_,ph3=v.op_[3].phase_ ;
	unsigned int inc0=v.op_[0].inc_,inc1=v.op_[1].inc_ ;
	unsigned int inc2=v.op_[2].inc_,inc3=v.op_[3].inc_ ;
	const int lvl0=level[0],lvl1=level[1],lvl2=level[2],lvl3=level[3] ;
	/* opSus and opDec are deliberately NOT scalars. They are read only
	   where the envelope is still moving, and an operator that has
	   reached its sustain never goes down that path -- which is most
	   of most notes. Held as scalars they cost eight registers all
	   the time to save a fixed offset load almost none of the time,
	   and this engine spills: 317 of its memory operations are the
	   compiler shuffling registers to the stack, which is what makes
	   it the most expensive thing in the mixer.

	   Writing the loop out took it from 85% of the budget to 75% at
	   eight voices on hardware. This is the same argument applied
	   again: fewer things live at once, not fewer things done.      */
	// dest[0] is FM_OUT in every algorithm, so op0 needs no routing
	const int d1=dest[1],d2=dest[2],d3=dest[3] ;
	int fb0=v.fbLast_[0],fb1=v.fbLast_[1] ;

	// An operator envelope that has reached its sustain has a
	// constant gain for the rest of the note, and most of a note is
	// that. Reading the ramp and recomputing the gain per sample for
	// four operators is three loads and a multiply each, every
	// sample, to arrive at the number we had last time.
	int gain0=0,gain1=0,gain2=0,gain3=0 ;
	bool mov0=false,mov1=false,mov2=false,mov3=false ;

	int glideRefresh=0 ;
	fixed *out=buffer ;

	for (int n=0;n<size;n++) {

		if (glideRefresh==0) {
			glideRefresh=GLIDE_TICK ;
			glideStep(v,GLIDE_TICK) ;
			// a ramp can only start moving at a note-on, which is
			// between blocks, so once per tick is often enough to
			// notice that one has settled
#define FM_REFRESH(N)                                                    \
			inc##N=(unsigned int)                                                 \
				(((unsigned long long)v.curInc_*mulQ16[N])>>16) ;                    \
			mov##N=(v.op_[N].env_.step_!=0) ;                                     \
			gain##N=(lvl##N*(int)(v.op_[N].env_.level_>>8))>>8 ;
			FM_REFRESH(0) FM_REFRESH(1) FM_REFRESH(2) FM_REFRESH(3)
#undef FM_REFRESH
		}
		glideRefresh-- ;

		// nothing in the table ever routes to op3, so there is no fed3
		int fed0=0,fed1=0,fed2=0 ;
		int mix=0 ;

		/* 4 down to 1: a modulator is always numbered above what it
		   feeds, so its output is ready by the time we get there.

		   One operator, given its own phase and its own incoming
		   modulation. A silent operator contributes nothing to
		   anybody, so its envelope, its lookup and its multiply are
		   all wasted -- and a default FM patch has one of the four at
		   zero. Its phase still has to run, or bringing it in with
		   FML4 mid-note would land it somewhere arbitrary.

		   Interpolated: reading the nearest of 1024 entries and
		   throwing away the other 22 bits of phase measured as
		   everything-not-the-fundamental sitting at -55dB on a bare
		   operator. Interpolating the same table takes that to -96,
		   and it matters more here than anywhere else in the synth
		   because a modulator's impurity is multiplied into the
		   carrier's spectrum rather than just added to the output. */
#define FM_OPERATOR(N,PHOFF,FEDIN)                                       \
		int o##N=0 ;                                                           \
		if (lvl##N) {                                                          \
			int g ;                                                               \
			if (mov##N) {                                                         \
				/* opSus and opDec are read only here, so they are not               \
				   held as scalars: an operator at its sustain never                 \
				   comes down this path, which is most of most notes. */             \
				stepRamp(v.op_[N].env_,opSus[N],opDec[N]) ;                          \
				g=(lvl##N*(int)(v.op_[N].env_.level_>>8))>>8 ;                       \
			} else {                                                              \
				g=gain##N ;                                                          \
			}                                                                     \
			/* cast BEFORE the shift: three modulators at full level              \
			   sum past 90000, and shifting that left 18 as a signed              \
			   int is undefined. Unsigned wrap is what a phase                    \
			   accumulator wants anyway. */                                       \
			unsigned int p=ph##N+(PHOFF)+                                         \
			               (((unsigned int)(FEDIN))<<FM_INDEX_SHIFT) ;            \
			unsigned int si=(p>>22)&1023 ;                                        \
			int sv=fmSin_[si]+((fmSinD_[si]*(int)((p>>6)&0xFFFF))>>16) ;          \
			o##N=(sv*g)>>15 ;                                                     \
		}                                                                      \
		ph##N+=inc##N ;

		// op3 is the only one that can modulate itself. Two-sample
		// average, the standard tamer: without it the loop turns into
		// noise well before the control reaches the top.
		unsigned int fbOff=0 ;
		if (fbAmt) {
			int avg=(fb0+fb1)>>1 ;
			fbOff=((unsigned int)((avg*fbAmt)>>8))<<FM_FB_SHIFT ;
		}
		FM_OPERATOR(3,fbOff,0)
		fb1=fb0 ; fb0=o3 ;          // zero when the operator is silent
		if (d3==FM_OUT)   mix+=o3 ;
		else if (d3==2)   fed2+=o3 ;
		else if (d3==1)   fed1+=o3 ;
		else              fed0+=o3 ;

		FM_OPERATOR(2,0,fed2)
		if (d2==FM_OUT)   mix+=o2 ;
		else if (d2==1)   fed1+=o2 ;
		else              fed0+=o2 ;

		FM_OPERATOR(1,0,fed1)
		if (d1==FM_OUT)   mix+=o1 ;
		else              fed0+=o1 ;

		FM_OPERATOR(0,0,fed0)
		mix+=o0 ;                   // dest[0] is FM_OUT in every algo
#undef FM_OPERATOR

		int smp=(int)(((long long)mix*carrierRecip)>>16) ;

		if (filtered) {
			// Two passes, exactly as renderTone and renderVax do.
			// cutTable_ holds coefficients computed at twice the
			// sample rate because of that, so a single pass here put
			// every FM patch's filter an octave below where its
			// cutoff said it was. The three engines share the table;
			// they have to share the rate it was built for.
			int high=0 ;
			for (int pass=0;pass<2;pass++) {
				low+=(int)(((long long)f*band)>>15) ;
				high=smp-low-(int)(((long long)qQ*band)>>15) ;
				band+=(int)(((long long)f*high)>>15) ;
				if (band>SVF_CLAMP) band=SVF_CLAMP ;
				if (band<-SVF_CLAMP) band=-SVF_CLAMP ;
				if (low>SVF_CLAMP) low=SVF_CLAMP ;
				if (low<-SVF_CLAMP) low=-SVF_CLAMP ;
				if (high>SVF_CLAMP) high=SVF_CLAMP ;
				if (high<-SVF_CLAMP) high=-SVF_CLAMP ;
			}
			smp=(fltMode==VFM_LP)?low:((fltMode==VFM_BP)?band:high) ;
			if (smp>32700) smp=32700 ;
			if (smp<-32700) smp=-32700 ;
		}

		stepRamp(v.amp_,sustain,decay) ;

		fixed sample=fp_mul(smp*(v.amp_.level_>>8),vol) ;
		applyVoiceClick(v,sample) ;
		*out++=(fixed)(((long long)sample*panL)>>15) ;
		*out++=(fixed)(((long long)sample*panR)>>15) ;
	}

	v.op_[0].phase_=ph0 ; v.op_[1].phase_=ph1 ;
	v.op_[2].phase_=ph2 ; v.op_[3].phase_=ph3 ;
	v.fbLast_[0]=fb0 ;
	v.fbLast_[1]=fb1 ;
	v.svfLow_=low ;
	v.svfBand_=band ;
	return true ;
} ;

bool SynthInstrument::renderVax(SynthVoice &v,fixed *buffer,int size) {

	int wave=FindVariable(SYP_VAXWAVE)->GetInt() ;
	int unison=v.unison_ ;
	if (unison<1) unison=1 ;
	if (unison>VAX_MAX_UNISON) unison=VAX_MAX_UNISON ;
	int detune=v.detune_ ;
	int pwm=FindVariable(SYP_PWM)->GetInt() ;
	unsigned int pwThresh=(unsigned int)(16+((pwm*224)>>8))<<24 ;
	int subQ=FindVariable(SYP_SUB)->GetInt()<<7 ;
	int noiseQ=FindVariable(SYP_NOISE)->GetInt()<<7 ;
	// sync: 0 is off. Above that the audible oscillators run from
	// 1x up to 4x the note and are restarted every time the silent
	// master crosses zero, which is what makes the formant sweep.
	int syncAmt=FindVariable(SYP_SYNC)->GetInt() ;
	// ring: crossfade between the oscillator and the oscillator times
	// a sine at the sub octave. 0 passes the oscillator through.
	int ringQ=FindVariable(SYP_RING)->GetInt()<<7 ;
	int cut=v.cutoff_ ;
	int resoP=v.reso_ ;
	int fltMode=FindVariable(SYP_FLTMODE)->GetInt() ;
	int modAmt=v.modAmt_ ;
	fixed modSus=sustainFromParam(FindVariable(SYP_DCWSUS)->GetInt()) ;
	int modDec=FindVariable(SYP_DCWDEC)->GetInt() ;
	fixed ampSus=sustainFromParam(FindVariable(SYP_SUSTAIN)->GetInt()) ;
	int ampDec=FindVariable(SYP_DECAY)->GetInt() ;
	fixed vol=(fixed)(((int)v.volume_*v.cmdGain_)>>8)<<7 ;
	int drive3=v.drive_*3 ;
	int panL,panR ;
	panLR(v.pan_,panL,panR) ;
	int lfoDest=FindVariable(SYP_LFODEST)->GetInt() ;
	unsigned int lfoIncV=lfoInc_[v.lfoRate_&0xFF] ;
	int lfoDepth=v.lfoDepth_<<7 ;

	int unisonAmp=32768/unison ;
	unsigned int urcp[VAX_MAX_UNISON]={0} ;
	unsigned int subRcp=0 ;
	int qQ=32768-(resoP<<7) ;         // reso: damping 1.0 -> 0.03
	if (qQ<1024) qQ=1024 ;
	int gcoef=v.glideCoef_ ;

	unsigned int curInc=v.curInc_ ;
	unsigned int targetInc=v.phaseInc_ ;
	unsigned int uinc[VAX_MAX_UNISON] ;
	unsigned int syncInc=0 ;
	int f=cutTable_[cut] ;
	int low=v.svfLow_ ;
	int band=v.svfBand_ ;
	unsigned int rng=v.rng_ ;
	int refresh=0 ;

	fixed *out=buffer ;
	for (int i=0;i<size;i++) {

		if (refresh==0) {
			refresh=16 ;

			// glide: one-pole walk of the base increment. Was a private
			// copy of glideStep; share the one implementation so VAX,
			// TONE and PDX all slide at the same rate.
			v.curInc_=curInc ;
			glideStep(v,16) ;   // this refresh covers 16 samples
			curInc=v.curInc_ ;

			// the LFO, block-rate: vibrato / wobble / pwm animation
			int mod=0 ;
			if (lfoDest!=SLD_OFF && lfoDepth) {
				v.lfoPhase_+=lfoIncV*16 ;
				mod=(sineTable_[v.lfoPhase_>>24]*lfoDepth)>>15 ;
			}

			unsigned int baseInc=curInc ;
			if (lfoDest==SLD_PITCH && mod) {
				// +-~1 semitone at full depth
				baseInc=(unsigned int)((long long)curInc+
					((long long)curInc*mod)/557056) ;
			}

			// detuned stack increments off the (gliding, wobbling) base
			for (int j=0;j<unison;j++) {
				uinc[j]=baseInc+(unsigned int)(((long long)baseInc*detune*unisonOff[j])>>15) ;
			}
			// ...then lifted by the sync ratio. The master keeps the
			// note, so the pitch you hear stays put while the timbre
			// climbs -- lift the stack without the reset below and it
			// would simply play sharp.
			if (syncAmt) {
				for (int j=0;j<unison;j++) {
					uinc[j]=(unsigned int)(((long long)uinc[j]*
						(255+syncAmt*3))/255) ;
				}
			}
			syncInc=baseInc ;
			// one reciprocal per stack member per pitch change, not
			// one divide per edge per member
			for (int j=0;j<unison;j++) urcp[j]=blepRcp(uinc[j]) ;
			subRcp=blepRcp(curInc>>1) ;

			// pulse width, possibly LFO-animated
			int pwmEff=pwm ;
			if (lfoDest==SLD_PWM) pwmEff+=mod>>8 ;
			if (pwmEff<0) pwmEff=0 ;
			if (pwmEff>255) pwmEff=255 ;
			pwThresh=(unsigned int)(16+((pwmEff*224)>>8))<<24 ;

			// cutoff rides the mod envelope (and the wobble)
			int cutEff=cut+((modAmt*(v.mod_.level_>>8))>>15) ;
			if (lfoDest==SLD_FILTER) cutEff+=mod>>8 ;
			if (cutEff>255) cutEff=255 ;
			if (cutEff<0) cutEff=0 ;
			f=cutTable_[cutEff] ;
		}
		refresh-- ;

		// Unison stack, band limited. Every member of the stack gets
		// the correction, not just the centre: the residual costs a
		// divide only in the two sample window around an edge, so a
		// seven strong stack at 440Hz is still under two divides per
		// hundred samples each, and a detuned stack of naive saws is
		// exactly the case where the alias is loudest.
		int mix=0 ;
		if (wave==VWT_PULSE) {
			for (int j=0;j<unison;j++) {
				mix+=(pulseBlep(v.uphase_[j],uinc[j],urcp[j],pwThresh)*24576)>>15 ;
				v.uphase_[j]+=uinc[j] ;
			}
		} else {
			for (int j=0;j<unison;j++) {
				mix+=sawBlep(v.uphase_[j],uinc[j],urcp[j]) ;
				v.uphase_[j]+=uinc[j] ;
			}
		}
		mix=(mix*unisonAmp)>>15 ;

		// hard sync: when the master wraps past the top of its 32 bit
		// phase, every audible oscillator restarts from zero. The
		// discontinuity that creates IS the sound.
		if (syncAmt) {
			unsigned int prev=v.syncPhase_ ;
			v.syncPhase_=prev+syncInc ;
			if (v.syncPhase_<prev) {
				for (int j=0;j<unison;j++) v.uphase_[j]=0 ;
			}
		}

		// ring: multiply by a sine running at the sub octave. Done
		// before the sub and the noise are added so those stay clean,
		// which is what lets a ringed lead keep a solid bottom.
		if (ringQ) {
			int rm=sineTable_[v.subPhase_>>24] ;
			int wet=(mix*rm)>>15 ;
			mix=mix+(((wet-mix)*ringQ)>>15) ;
		}

		// sub square one octave down + noise
		if (subQ) {
			int sb=(pulseBlep(v.subPhase_,curInc>>1,subRcp,0x80000000u)*24576)>>15 ;
			mix+=(sb*subQ)>>15 ;
		}
		v.subPhase_+=curInc>>1 ;
		if (noiseQ) {
			rng=rng*1664525u+1013904223u ;
			int nz=(int)(short)(rng>>16) ;
			mix+=(nz*noiseQ)>>15 ;
		}
		if (mix>32700) mix=32700 ;
		if (mix<-32700) mix=-32700 ;

		// Chamberlin SVF. These multiply a Q15 coefficient (up to 32000)
		// by filter state clamped at 1<<20, which is 33e9 — sixteen times
		// past what an int holds. Wrapping made the state run to its clamp
		// and stay there: the voice stopped being audio and became a
		// constant DC offset. They must be 64-bit.
		// two passes at the same input, as in renderTone: the filter
		// ticks at twice the sample rate, which is what keeps the
		// coefficient inside the range a Chamberlin is stable and
		// untwisted in
		int high=0 ;
		for (int pass=0;pass<2;pass++) {
			low+=(int)(((long long)f*band)>>15) ;
			high=mix-low-(int)(((long long)qQ*band)>>15) ;
			band+=(int)(((long long)f*high)>>15) ;
			// ringing headroom is real; runaway is not
			if (band>SVF_CLAMP) band=SVF_CLAMP ;
			if (band<-SVF_CLAMP) band=-SVF_CLAMP ;
			if (low>SVF_CLAMP) low=SVF_CLAMP ;
			if (low<-SVF_CLAMP) low=-SVF_CLAMP ;
			// high is derived from both, so it needs its own bound
			// before the drive stage multiplies it by up to 1021
			if (high>SVF_CLAMP) high=SVF_CLAMP ;
			if (high<-SVF_CLAMP) high=-SVF_CLAMP ;
		}

		int s=(fltMode==VFM_LP)?low:((fltMode==VFM_BP)?band:high) ;

		// drive: post-filter gain into a soft knee — grit that keeps
		// its shape instead of hard-clip fizz
		if (drive3) {
			s=(s*(256+drive3))>>8 ;
			if (s>24576) s=24576+((s-24576)>>2) ;
			else if (s<-24576) s=-24576+((s+24576)>>2) ;
		}
		if (s>32700) s=32700 ;
		if (s<-32700) s=-32700 ;

		stepRamp(v.amp_,ampSus,ampDec) ;
		stepRamp(v.mod_,modSus,modDec) ;

		fixed smp=fp_mul(s*(v.amp_.level_>>8),vol) ;
		applyVoiceClick(v,smp) ;
		// samples are Q15 (full scale ~1.07e9) — the pan multiply
		// MUST widen or it wraps
		*out++=(fixed)(((long long)smp*panL)>>15) ;
		*out++=(fixed)(((long long)smp*panR)>>15) ;
	}

	v.curInc_=curInc ;
	v.svfLow_=low ;
	v.svfBand_=band ;
	v.rng_=rng ;
	return true ;
} ;

bool SynthInstrument::IsInitialized() {
	return true ;
} ;

bool SynthInstrument::IsEmpty() {
	return false ;
} ;

InstrumentType SynthInstrument::GetType() {
	return IT_SYNTH ;
} ;

const char *SynthInstrument::GetName() {
	switch (FindVariable(SYP_ENGINE)->GetInt()) {
		case SET_PDX:
			sprintf(name_,"PDX %s",pdxWaveNames[FindVariable(SYP_PDXWAVE)->GetInt()]) ;
			break ;
		case SET_VAX:
			sprintf(name_,"VAX %s",vaxWaveNames[FindVariable(SYP_VAXWAVE)->GetInt()]) ;
			break ;
		case SET_FM:
			// the algorithm is the identity of an FM patch the way
			// the waveform is for the others
			sprintf(name_,"FM %s",fmAlgoNames[FindVariable(SYP_FMALGO)->GetInt()]) ;
			break ;
		default:
			sprintf(name_,"TONE %s",waveNames[FindVariable(SYP_WAVE)->GetInt()]) ;
			break ;
	}
	return name_ ;
} ;

void SynthInstrument::ProcessCommand(int channel,FourCC cc,ushort value) {

	int engine=FindVariable(SYP_ENGINE)->GetInt() ;
	SynthVoice &v=voice_[channel] ;
	if (!v.active_) return ;

	switch (cc) {
		// Each of these claims its parameter for the rest of the note
		// (see cmdLocks_): an explicit command outranks the knob, but
		// only for what it actually set, and only until the next note.

		case I_CMD_VOLM:
			v.volume_=value&0xFF ;
			v.cmdLocks_|=SVL_VOLUME ;
			break ;

		case I_CMD_FCUT:
			// TONE and VAX both run the SVF, so this is a real cutoff.
			// PDX has no filter — its DCW is the brightness control, so
			// FCUT sweeps that instead, which is the same gesture.
			if (engine==SET_PDX) {
				v.modAmt_=value&0xFF ;
				v.cmdLocks_|=SVL_MODAMT ;
			} else {
				v.cutoff_=value&0xFF ;
				v.cmdLocks_|=SVL_CUTOFF ;
			}
			break ;

		case I_CMD_FRES:
			if (engine!=SET_PDX) {
				v.reso_=value&0xFF ;
				v.cmdLocks_|=SVL_RESO ;
			}
			break ;

		case I_CMD_FLTR:
			// cutoff aa + resonance bb in one step
			if (engine==SET_PDX) {
				v.modAmt_=(value>>8)&0xFF ;
				v.cmdLocks_|=SVL_MODAMT ;
			} else {
				v.cutoff_=(value>>8)&0xFF ;
				v.reso_=value&0xFF ;
				v.cmdLocks_|=SVL_CUTOFF|SVL_RESO ;
			}
			break ;

		// The synth's own voice, reachable from the pattern at last.
		// DRIV, UNIS and DTUN are VAX's; LFOD and LFOR work on every
		// engine that has the LFO pointed somewhere.

		case I_CMD_DRIV:
			v.drive_=value&0xFF ;
			v.cmdLocks_|=SVL_DRIVE ;
			break ;

		// FM operator levels. Automating a modulator's level is the
		// FM equivalent of a filter sweep; automating a carrier's is
		// a level move on that branch of the algorithm.
		case I_CMD_FML1:
			v.fmLevel_[0]=value&0xFF ;
			v.cmdLocks_|=SVL_FMLV1 ;
			break ;
		case I_CMD_FML2:
			v.fmLevel_[1]=value&0xFF ;
			v.cmdLocks_|=SVL_FMLV2 ;
			break ;
		case I_CMD_FML3:
			v.fmLevel_[2]=value&0xFF ;
			v.cmdLocks_|=SVL_FMLV3 ;
			break ;
		case I_CMD_FML4:
			v.fmLevel_[3]=value&0xFF ;
			v.cmdLocks_|=SVL_FMLV4 ;
			break ;
		case I_CMD_FMFB:
			v.fmFeedback_=value&0xFF ;
			v.cmdLocks_|=SVL_FMFB ;
			break ;

		case I_CMD_UNIS:
			{
				int u=value&0xFF ;
				if (u<1) u=1 ;
				if (u>VAX_MAX_UNISON) u=VAX_MAX_UNISON ;
				v.unison_=u ;
				v.cmdLocks_|=SVL_UNISON ;
			}
			break ;

		case I_CMD_DTUN:
			v.detune_=value&0xFF ;
			v.cmdLocks_|=SVL_DETUNE ;
			break ;

		case I_CMD_LFOD:
			v.lfoDepth_=value&0xFF ;
			v.cmdLocks_|=SVL_LFODEP ;
			break ;

		case I_CMD_LFOR:
			v.lfoRate_=value&0xFF ;
			v.cmdLocks_|=SVL_LFORAT ;
			break ;

		case I_CMD_PAN_:
			// 00 hard left, 7F center, FE hard right
			v.pan_=value&0xFF ;
			if (v.pan_>0xFE) v.pan_=0xFE ;
			break ;

		case I_CMD_PTCH: {
			// bb = signed semitones from the note as played, aa =
			// slide rate (00 = jump). Re-issuing PTCH 00 returns.
			int pitch=(char)(value&0xFF) ;
			int rate=(value>>8)&0xFF ;
			v.glideCoef_=(rate==0)?0:(32768/(1+((rate*rate)>>7))) ;
			setVoicePitch(v,v.baseNote_+pitch) ;
			lastNote_[channel]=(unsigned char)(v.baseNote_+pitch) ;
			break ;
		}

		case I_CMD_ARPS:
			// bb ticks per arp position. 0 and 1 both mean every tick.
			v.arpRate_=(unsigned char)(value&0xFF) ;
			if (v.arpRate_==0) v.arpRate_=1 ;
			v.arpTick_=0 ;
			break ;

		case I_CMD_ARPG:
			// abcd: four relative semitone steps cycled at tick rate
			v.arpData_=value ;
			v.arpStep_=0 ;
			v.arpTick_=0 ;
			v.arpOn_=(value!=0) ;
			if (!v.arpOn_) {
				setVoicePitch(v,v.baseNote_) ;
			} else {
				v.tickLen_=tickLength() ;
			}
			break ;

		case I_CMD_VIBR:
			/* aabb: aa speed, bb depth in sixteenths of a semitone.
			   0000 puts the pitch back where the note left it, which
			   is what turning vibrato off has to mean. */
			v.vibSpeed_=(unsigned short)((value>>8)&0xFF) ;
			v.vibDepth_=(unsigned char)(value&0xFF) ;
			v.vibPhase_=0 ;
			v.vibMul_=65536u ;
			if (v.vibSpeed_ && v.vibDepth_) {
				v.tickLen_=tickLength() ;
			} else {
				setVoicePitch(v,v.pitchNote_) ;
			}
			break ;

		case I_CMD_RTRG:
			// bb = ticks between retriggers (00 = off). The drill.
			// RTGR shapes it; this only says how fast.
			v.rtgTicks_=value&0xFF ;
			v.rtgCount_=0 ;
			v.cmdGain_=256 ;
			v.rtgPitchAcc_=0 ;
			if (v.rtgTicks_) {
				v.tickLen_=tickLength() ;
				retrigger(v) ;
			}
			break ;

		case I_CMD_RTGR:
			/* The shape of a retrigger: aa is how much the volume
			   changes each repeat, bb how much the pitch does in
			   semitones, both signed bytes so 80..FF fall.
			   
			   A separate command rather than RTRG's spare byte,
			   because that byte is only spare on the synth -- the
			   sampler already reads it as a position offset per
			   repeat. One command meaning two different things
			   depending on what is under the cursor is the kind of
			   thing nobody ever quite remembers.
			   
			   Set it in the other command column on the same step as
			   RTRG, or on an earlier step to shape everything after. */
			v.rtgVolStep_=(signed char)((value>>8)&0xFF) ;
			v.rtgPitchStep_=(signed char)(value&0xFF) ;
			v.cmdGain_=256 ;
			v.rtgPitchAcc_=0 ;
			break ;

		case I_CMD_TRSP:
			/* Signed semitones, held until something changes it. A
			   table row carrying 0 therefore means "back to the
			   note", which is what a transpose column of zeroes
			   should mean. */
			v.transpose_=(signed char)(value&0xFF) ;
			setVoicePitch(v,v.baseNote_) ;
			break ;

		case I_CMD_RAND: {
			/* aa semitones of pitch scatter, bb of level, both
			   applied once, now, to the note already sounding --
			   commands run after the note starts on a step, so this
			   lands on the voice it is written beside.
			   
			   Level only ever goes DOWN. A command that could make a
			   step louder than the one you set would be a command
			   that clips a mix you had balanced, and "humanise" has
			   never meant "sometimes louder than full".
			   
			   Pitch goes both ways and is centred, so a spread of 1
			   is minus one, nothing, or plus one rather than a
			   consistent lift. */
			int ps=(value>>8)&0xFF ;
			int vs=value&0xFF ;
			if (ps) {
				v.rng_=v.rng_*1664525u+1013904223u ;
				int d=(int)((v.rng_>>16)%(unsigned)(2*ps+1))-ps ;
				int n=v.baseNote_+d ;
				if (n<0) n=0 ;
				if (n>127) n=127 ;
				setVoicePitch(v,n) ;
				lastNote_[channel]=(unsigned char)n ;
			}
			if (vs) {
				v.rng_=v.rng_*1664525u+1013904223u ;
				int drop=(int)((v.rng_>>16)%(unsigned)(vs+1)) ;
				int g=256-drop ;
				if (g<0) g=0 ;
				v.cmdGain_=(unsigned short)g ;
			}
			break ;
		}

		case I_CMD_CHRD:
			/* A chord, on a monophonic channel, is an arpeggio at the
			   fastest rate there is -- which is also what it is on the
			   trackers this borrows the idea from. ARPG can already do
			   it; this saves setting ARPS to 1 every time and reads as
			   what it is on the step. */
			v.arpData_=value ;
			v.arpStep_=0 ;
			v.arpTick_=0 ;
			v.arpRate_=1 ;
			v.arpOn_=(value!=0) ;
			if (!v.arpOn_) {
				setVoicePitch(v,v.baseNote_) ;
			} else {
				v.tickLen_=tickLength() ;
			}
			break ;

		case I_CMD_LEGA: {
			// slide to a new pitch without retriggering envelopes.
			// value low byte = signed semitone offset from the
			// current note (sample-instrument convention).
			int pitch=(char)(value&0xFF) ;
			if (pitch==0) break ;
			int target=lastNote_[channel]+pitch ;
			if (target<0) target=0 ;
			if (target>127) target=127 ;
			lastNote_[channel]=(unsigned char)target ;
			v.baseNote_=(unsigned char)target ;
			v.phaseInc_=noteInc_[target] ;
			// LEGA always slides at the instrument's glide rate
			break ;
		}
	}
} ;

void SynthInstrument::Purge() {
} ;

int SynthInstrument::GetTable() {
	return FindVariable(SYP_TABLE)->GetInt() ;
} ;

bool SynthInstrument::GetTableAutomation() {
	return FindVariable(SYP_TABLEAUTO)->GetBool() ;
} ;

void SynthInstrument::GetTableState(TableSaveState &state) {
	memcpy(state.hopCount_,tableState_.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(state.position_,tableState_.position_,sizeof(int)*3) ;
} ;

void SynthInstrument::SetTableState(TableSaveState &state) {
	memcpy(tableState_.hopCount_,state.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(tableState_.position_,state.position_,sizeof(int)*3) ;
} ;


// Nothing skipped: every parameter on a synth patch is one
// somebody chose.
bool SynthInstrument::IsAtDefaults() {
    SynthInstrument fresh;
    return SameParametersAs(fresh, 0);
}
