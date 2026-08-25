#include "DrumKit.h"
#include "System/System/System.h"
#include <math.h>
#include <string.h>

/* Baking happens once, at boot, on the main thread, so this is the
   one place in the audio path where doubles are the sensible tool.
   The output is 16-bit PCM and nothing here runs again. */

BakedSource::~BakedSource() {
	if (buf_) SYS_FREE(buf_) ;
}

namespace DrumKit {

// ---- the roster ---------------------------------------------------
// Order matters twice: it is the order these appear in the sample
// list, and a project stores the NAME, so nothing here may be
// renamed once a song exists that uses it.
static const char *names_[DRUMKIT_TOTAL]= {
	"ANKICK","ANSNARE","ANRIM","ANCLAP","ANHAT","ANHATO",
	"ANTOMLO","ANTOMHI","ANCRASH","ANRIDE","ANCOWB","ANSHAKE",
	"HDKICK","HDSNARE","HDRIM","HDCLAP","HDHAT","HDHATO",
	"HDTOMLO","HDTOMHI","HDCRASH","HDRIDE","HDCOWB","HDSHAKE"
} ;

const char *Name(int i) {
	return ((i>=0)&&(i<DRUMKIT_TOTAL))?names_[i]:"" ;
}

enum { D_KICK=0,D_SNARE,D_RIM,D_CLAP,D_HAT,D_HATO,
       D_TOMLO,D_TOMHI,D_CRASH,D_RIDE,D_COWB,D_SHAKE } ;

// ---- primitives ---------------------------------------------------
//
// None of the inner loops below call sin, exp or tanh. That is not
// premature: this runs at boot on a machine with no hardware doubles,
// and the cymbals alone would be six sines a sample across a quarter
// of a million samples. A table, an incremental envelope and a
// rational clip cost a fraction of that and are inaudibly different.

#define TWO_PI 6.283185307179586
#define SINTAB_BITS 12
#define SINTAB_SIZE (1<<SINTAB_BITS)
#define SINTAB_MASK (SINTAB_SIZE-1)

static double sinTab_[SINTAB_SIZE] ;
static bool tabBuilt_=false ;

static void buildTables() {
	if (tabBuilt_) return ;
	for (int i=0;i<SINTAB_SIZE;i++) {
		sinTab_[i]=sin(TWO_PI*i/SINTAB_SIZE) ;
	}
	tabBuilt_=true ;
}

// phase in turns; only the fraction matters
static inline double sinT(double turns) {
	int idx=(int)(turns*SINTAB_SIZE) ;
	return sinTab_[idx&SINTAB_MASK] ;
}

/* An exponential decay stepped one sample at a time instead of
   evaluated from t. Same curve, one multiply. */
struct Env {
	double v_,k_ ;
	Env() : v_(0),k_(1) {}
	void set(double secs,double start=1.0) {
		v_=start ;
		// -60dB over secs
		k_=(secs>0)?exp(-6.907755/(secs*DRUMKIT_RATE)):0.0 ;
	}
	double step() { double o=v_ ; v_*=k_ ; return o ; }
	double peek() const { return v_ ; }
} ;

struct Noise {
	unsigned int s_ ;
	Noise(unsigned int seed) : s_(seed?seed:1) {}
	// the same LCG the VAX engine uses, so the noise floor of a baked
	// hat and a live one are the same character
	double operator()() {
		s_=s_*1664525u+1013904223u ;
		return (double)(int)(short)(s_>>16)/32768.0 ;
	}
} ;

// One-pole low pass. c is the per-sample coefficient, 0..1.
struct LP1 {
	double z_ ;
	LP1() : z_(0) {}
	double operator()(double x,double c) { z_+=c*(x-z_) ; return z_ ; }
} ;

static inline double coefFor(double hz) {
	double c=TWO_PI*hz/DRUMKIT_RATE ;
	if (c>0.99) c=0.99 ;
	if (c<0.0) c=0.0 ;
	return c ;
}

// A short raised-cosine fade so nothing starts or ends on a step.
// Every drum gets the tail one; only the ones with a hard transient
// skip the head, and even they get a couple of samples.
static void fadeEdges(double *b,int n,int headSamples) {
	if (n<8) return ;
	if (headSamples>n/4) headSamples=n/4 ;
	for (int i=0;i<headSamples;i++) {
		double w=0.5-0.5*sinT(0.25+0.5*i/headSamples) ;
		b[i]*=w ;
	}
	int tail=(int)(DRUMKIT_RATE*0.004) ;   // 4ms
	if (tail>n/4) tail=n/4 ;
	for (int i=0;i<tail;i++) {
		double w=0.5-0.5*sinT(0.25+0.5*i/tail) ;
		b[n-1-i]*=w ;
	}
}

/* Soft clip: what makes the HD kit hit harder than the AN one
   without simply being louder. A rational stand-in for tanh --
   x/(1+|x|) has the same shape and no library call. */
static inline double softClip(double x,double drive) {
	x*=drive ;
	double y=x/(1.0+(x<0?-x:x)) ;
	double norm=drive/(1.0+drive) ;
	return norm>0?y/norm:y ;
}

// Every drum is baked to the same peak.
//
// The targets used to vary from 0.72 for the shaker up to 0.97 for a
// hard kick, on the reasoning that a shaker should not be as loud as
// a kick. That is true of a MIX, and it is the volume knob's job. As
// a property of the sample it just means the quiet members arrive
// already two and a half decibels down and the only way back is the
// one control the user has, which then has nothing left in it. A
// baked drum should be delivered at full level and balanced
// afterwards.
//
// This is the ceiling, not a suggestion: a 16-bit sample has nothing
// above it. A kit that still feels quiet at maximum is telling you
// the rest of the mix is too loud, not the drums too soft.
#define DRUM_PEAK 0.97

// Normalise to a target peak, so no drum in the kit is wildly out of
// step with the others and none of them clips the converter.
static void normalise(double *b,int n,double target) {
	double peak=0 ;
	for (int i=0;i<n;i++) {
		double a=b[i]<0?-b[i]:b[i] ;
		if (a>peak) peak=a ;
	}
	if (peak<1e-9) return ;
	double g=target/peak ;
	for (int i=0;i<n;i++) b[i]*=g ;
}

/* Peak normalising is not level normalising, and the gap between them
   is the whole complaint.

   Measured across the kit (tools/run_drumkit_test.sh), every drum
   peaked at the same 31784 and their RMS ran from 1556 for a ride to
   7878 for a kick: FOURTEEN DECIBELS of spread between sounds that
   are, on paper, equally loud. A clap touching full scale for four
   milliseconds and a kick holding it for eighty are the same peak and
   nowhere near the same sound. So the quiet members arrive quiet, the
   user pins their volume at maximum to compensate, and there is
   nothing left in the control -- which is the headroom complaint,
   exactly.

   Peak really is a ceiling; nothing goes above it in a 16-bit sample.
   But the room between peak and RMS is real and this was leaving it
   unused. Lift each drum toward a target loudness and let the
   transient fold: the body of a sound sits well below the peak and
   passes through the fold untouched, so what actually gets rounded is
   the first millisecond of the attack. That is a limiter, and it is
   what a drum bus does anyway.

   The lift only ever RAISES -- the kick and snare are already at
   target and are left alone, so this changes the quiet end of the kit
   without making the loud end quieter. */
#define DRUM_RMS  5500.0    /* 16-bit units; see drumkit_test */
#define DRUM_LIFT 2.5       /* past this it is squashing, not levelling */

static double rmsOf(const double *b,int n) {
	if (n<1) return 0 ;
	double acc=0 ;
	for (int i=0;i<n;i++) acc+=b[i]*b[i] ;
	return sqrt(acc/n) ;
}

/* Monotonic, unity slope at zero, asymptotic to +-1. Quiet samples
   pass through with the full gain; only what approaches the ceiling
   is bent. */
static double fold(double x) {
	double a=x<0?-x:x ;
	return x/(1.0+a*a*0.25) ;
}

static void loudness(double *b,int n) {
	normalise(b,n,1.0) ;
	double r=rmsOf(b,n)*32767.0 ;
	if (r<1e-6) { normalise(b,n,DRUM_PEAK) ; return ; }
	double g=DRUM_RMS/r ;
	if (g>DRUM_LIFT) g=DRUM_LIFT ;
	if (g<=1.0) {
		/* Already at or above target: leave it completely alone.
		   Folding at unity gain still rounds the peak, and the
		   re-normalise afterwards would scale the whole sound back up
		   to compensate -- which quietly lifts a drum this function
		   just decided not to touch. */
		normalise(b,n,DRUM_PEAK) ;
		return ;
	}
	for (int i=0;i<n;i++) b[i]=fold(b[i]*g) ;
	normalise(b,n,DRUM_PEAK) ;
}

// ---- the recipes --------------------------------------------------
//
// Everything below is written from first principles rather than
// sampled from anything: a kick is a sine whose pitch falls, a snare
// is that plus noise, a hat is noise with the bottom taken out, a
// cymbal is a stack of inharmonic partials. The two kits differ in
// how fast the sweeps are, how much noise there is, and whether the
// body gets clipped.

static void bakeKick(double *b,int n,bool hard) {
	double ph=0 ;
	Noise nz(0x1234) ;
	LP1 clickLp ;
	double f0=hard?210.0:120.0 ;
	double f1=hard?48.0:45.0 ;
	Env sweep ; sweep.set(hard?0.028:0.055) ;
	Env amp ;   amp.set(hard?0.22:0.34) ;
	Env click ; click.set(hard?0.010:0.016) ;
	double clickC=coefFor(hard?4200:2600) ;
	double clickG=hard?0.55:0.35 ;
	for (int i=0;i<n;i++) {
		double f=f1+(f0-f1)*sweep.step() ;
		ph+=f/DRUMKIT_RATE ;
		double s=sinT(ph)*amp.step() ;
		// the beater: a few ms of filtered noise, which is what stops
		// a kick sounding like a sine and makes it audible on a phone
		double c=clickLp(nz(),clickC)*click.step()*clickG ;
		b[i]=hard?softClip(s+c,2.2):(s+c) ;
	}
	fadeEdges(b,n,4) ;
	loudness(b,n) ;
}

static void bakeSnare(double *b,int n,bool hard) {
	double p1=0,p2=0 ;
	Noise nz(0x77771) ;
	LP1 lp ; LP1 hp ;
	Env body ;   body.set(hard?0.075:0.11) ;
	Env snares ; snares.set(hard?0.13:0.17) ;
	double f1=(hard?220.0:185.0)/DRUMKIT_RATE ;
	double f2=(hard?385.0:330.0)/DRUMKIT_RATE ;
	double lpC=coefFor(hard?7800:6200) ;
	double hpC=coefFor(hard?420:340) ;
	double nGain=hard?1.25:1.0 ;
	for (int i=0;i<n;i++) {
		// two detuned tones are what gives a snare its pitch without
		// making it a tom
		p1+=f1 ; p2+=f2 ;
		double tone=(sinT(p1)*0.7+sinT(p2)*0.45)*body.step() ;
		// band the noise: low-passed, then the bottom removed, which
		// is the snare wires rather than a hiss
		double raw=nz() ;
		double lo=lp(raw,lpC) ;
		double band=lo-hp(lo,hpC) ;
		double s=tone*0.85+band*snares.step()*nGain ;
		b[i]=hard?softClip(s,1.7):s ;
	}
	fadeEdges(b,n,3) ;
	loudness(b,n) ;
}

static void bakeRim(double *b,int n,bool hard) {
	double p1=0,p2=0 ;
	Noise nz(0x5150) ;
	LP1 hp ;
	// 0.022 measured out at a six millisecond decay -- a click rather
	// than a rimshot, and twenty decibels under the rest of the kit
	Env tone ;  tone.set(0.055) ;
	Env click ; click.set(0.010) ;
	double f1=(hard?1750.0:1420.0)/DRUMKIT_RATE ;
	double f2=(hard?2630.0:2180.0)/DRUMKIT_RATE ;
	double hpC=coefFor(1500) ;
	for (int i=0;i<n;i++) {
		p1+=f1 ; p2+=f2 ;
		double t=(sinT(p1)*0.6+sinT(p2)*0.4)*tone.step() ;
		double raw=nz() ;
		double c=(raw-hp(raw,hpC))*click.step()*0.7 ;
		b[i]=t+c ;
	}
	fadeEdges(b,n,2) ;
	loudness(b,n) ;
}

static void bakeClap(double *b,int n,bool hard) {
	Noise nz(0xC1A9) ;
	LP1 lp ; LP1 hp ;
	// three bursts then the tail: a clap is several hands not quite
	// together, and the spacing is the whole sound
	static const double offs[3]={0.0,0.010,0.019} ;
	double gap=hard?0.8:1.0 ;
	int at[3] ;
	for (int k=0;k<3;k++) at[k]=(int)(offs[k]*gap*DRUMKIT_RATE) ;
	Env burst[3] ;
	Env room ;
	bool started[3]={false,false,false} ;
	bool roomOn=false ;
	double lpC=coefFor(hard?5200:4200) ;
	double hpC=coefFor(hard?900:750) ;
	for (int i=0;i<n;i++) {
		double env=0 ;
		for (int k=0;k<3;k++) {
			if (!started[k]&&(i>=at[k])) { burst[k].set(0.008) ; started[k]=true ; }
			if (started[k]) env+=burst[k].step() ;
		}
		if (!roomOn&&(i>=at[2])) { room.set(hard?0.10:0.14) ; roomOn=true ; }
		if (roomOn) env+=room.step()*0.8 ;
		double raw=nz() ;
		double lo=lp(raw,lpC) ;
		double band=lo-hp(lo,hpC) ;
		b[i]=band*env ;
	}
	fadeEdges(b,n,2) ;
	loudness(b,n) ;
}

/* A cymbal is not noise: it is a dense stack of inharmonic partials.
   Six squares is what the 808 used and it is still the cheapest thing
   that sounds like metal rather than like static -- and a square from
   a phase accumulator is a sign test, so this costs no sines at all,
   which matters because the cymbals are the longest samples here. */
static void metal(double *b,int n,double decay,double hpHz,double lpHz,
                  double base,double noiseMix,unsigned int seed) {
	static const double ratio[6]={1.0,1.4142,1.7818,2.0,2.5198,2.9945} ;
	unsigned int ph[6]={0,0,0,0,0,0} ;
	unsigned int inc[6] ;
	for (int k=0;k<6;k++) {
		inc[k]=(unsigned int)(base*ratio[k]*4294967296.0/DRUMKIT_RATE) ;
	}
	Noise nz(seed) ;
	LP1 lp ; LP1 hp ;
	Env env ; env.set(decay) ;
	double lpC=coefFor(lpHz),hpC=coefFor(hpHz) ;
	double tone=1.0-noiseMix ;
	for (int i=0;i<n;i++) {
		int sum=0 ;
		for (int k=0;k<6;k++) {
			// odd harmonics are what fill the top of a cymbal
			sum+=(ph[k]&0x80000000u)?-1:1 ;
			ph[k]+=inc[k] ;
		}
		double mixed=(sum/6.0)*tone+nz()*noiseMix ;
		double lo=lp(mixed,lpC) ;
		double band=lo-hp(lo,hpC) ;
		b[i]=band*env.step() ;
	}
}

static void bakeHat(double *b,int n,bool hard,bool open) {
	double dec=open?(hard?0.30:0.38):(hard?0.040:0.055) ;
	metal(b,n,dec,hard?7000:5800,16000,hard?320:280,0.30,0x4A75) ;
	fadeEdges(b,n,2) ;
	loudness(b,n) ;
}

static void bakeCrash(double *b,int n,bool hard) {
	metal(b,n,hard?1.10:1.35,hard?3600:2800,15000,hard?290:250,0.45,0xC7A5) ;
	fadeEdges(b,n,3) ;
	loudness(b,n) ;
}

static void bakeRide(double *b,int n,bool hard) {
	// the ping is what makes a ride a ride: a clear partial over the wash
	metal(b,n,hard?0.85:1.05,hard?4200:3400,15000,hard?400:360,0.22,0x21DE) ;
	double ph=0 ;
	double f=(hard?2900.0:2450.0)/DRUMKIT_RATE ;
	Env ping ; ping.set(0.055) ;
	for (int i=0;i<n;i++) {
		ph+=f ;
		b[i]+=sinT(ph)*ping.step()*0.5 ;
	}
	fadeEdges(b,n,2) ;
	loudness(b,n) ;
}

static void bakeTom(double *b,int n,bool hard,bool low) {
	double ph=0 ;
	Noise nz(low?0x707A:0x707B) ;
	LP1 lp ;
	double f0=low?(hard?150.0:130.0):(hard?250.0:215.0) ;
	double f1=f0*0.62 ;
	Env sweep ; sweep.set(0.10) ;
	Env amp ;   amp.set(hard?0.20:0.28) ;
	Env skin ;  skin.set(0.012) ;
	double skinC=coefFor(3000) ;
	for (int i=0;i<n;i++) {
		double f=f1+(f0-f1)*sweep.step() ;
		ph+=f/DRUMKIT_RATE ;
		double s=sinT(ph)*amp.step() ;
		double sk=lp(nz(),skinC)*skin.step()*0.28 ;
		b[i]=hard?softClip(s+sk,1.5):(s+sk) ;
	}
	fadeEdges(b,n,3) ;
	loudness(b,n) ;
}

static void bakeCowbell(double *b,int n,bool hard) {
	unsigned int p1=0,p2=0 ;
	unsigned int i1=(unsigned int)((hard?620.0:540.0)*4294967296.0/DRUMKIT_RATE) ;
	unsigned int i2=(unsigned int)((hard?920.0:800.0)*4294967296.0/DRUMKIT_RATE) ;
	LP1 lp ; LP1 hp ;
	Env env ; env.set(hard?0.16:0.22) ;
	double lpC=coefFor(4500),hpC=coefFor(450) ;
	for (int i=0;i<n;i++) {
		double sq=(((p1&0x80000000u)?-1.0:1.0)+((p2&0x80000000u)?-1.0:1.0))*0.5 ;
		p1+=i1 ; p2+=i2 ;
		double lo=lp(sq,lpC) ;
		double band=lo-hp(lo,hpC) ;
		b[i]=band*env.step() ;
	}
	fadeEdges(b,n,2) ;
	loudness(b,n) ;
}

static void bakeShaker(double *b,int n,bool hard) {
	Noise nz(0x5A4E) ;
	LP1 lp ; LP1 hp ;
	Env att ; att.set(0.004) ;          // beads have to get moving
	Env amp ; amp.set(hard?0.075:0.095) ;
	double lpC=coefFor(14000),hpC=coefFor(hard?5500:4200) ;
	for (int i=0;i<n;i++) {
		double raw=nz() ;
		double lo=lp(raw,lpC) ;
		double band=lo-hp(lo,hpC) ;
		b[i]=band*(1.0-att.step())*amp.step() ;
	}
	fadeEdges(b,n,0) ;
	loudness(b,n) ;
}

// Length per drum, in seconds. Sized to where each one has actually
// decayed rather than to a round number: this is the whole memory
// budget of the feature.
static double lengthOf(int drum,bool hard) {
	switch (drum) {
		case D_KICK:  return hard?0.30:0.42 ;
		case D_SNARE: return hard?0.22:0.28 ;
		case D_RIM:   return 0.13 ;
		case D_CLAP:  return hard?0.24:0.32 ;
		case D_HAT:   return hard?0.09:0.12 ;
		case D_HATO:  return hard?0.55:0.70 ;
		case D_TOMLO: return hard?0.36:0.50 ;
		case D_TOMHI: return hard?0.30:0.42 ;
		// the cymbals are two thirds of the kit's memory between them,
		// and past this they are decaying into nothing audible anyway
		case D_CRASH: return hard?1.25:1.45 ;
		case D_RIDE:  return hard?1.00:1.15 ;
		case D_COWB:  return hard?0.28:0.38 ;
		case D_SHAKE: return hard?0.13:0.16 ;
	}
	return 0.25 ;
}

BakedSource *Bake(int i) {

	if ((i<0)||(i>=DRUMKIT_TOTAL)) return 0 ;
	buildTables() ;
	bool hard=(i>=DRUMKIT_DRUMS) ;
	int drum=i%DRUMKIT_DRUMS ;

	int n=(int)(lengthOf(drum,hard)*DRUMKIT_RATE) ;
	if (n<64) n=64 ;

	double *work=(double *)SYS_MALLOC(sizeof(double)*n) ;
	if (!work) return 0 ;
	memset(work,0,sizeof(double)*n) ;

	switch (drum) {
		case D_KICK:  bakeKick(work,n,hard) ; break ;
		case D_SNARE: bakeSnare(work,n,hard) ; break ;
		case D_RIM:   bakeRim(work,n,hard) ; break ;
		case D_CLAP:  bakeClap(work,n,hard) ; break ;
		case D_HAT:   bakeHat(work,n,hard,false) ; break ;
		case D_HATO:  bakeHat(work,n,hard,true) ; break ;
		case D_TOMLO: bakeTom(work,n,hard,true) ; break ;
		case D_TOMHI: bakeTom(work,n,hard,false) ; break ;
		case D_CRASH: bakeCrash(work,n,hard) ; break ;
		case D_RIDE:  bakeRide(work,n,hard) ; break ;
		case D_COWB:  bakeCowbell(work,n,hard) ; break ;
		case D_SHAKE: bakeShaker(work,n,hard) ; break ;
	}

	short *pcm=(short *)SYS_MALLOC(sizeof(short)*n) ;
	if (!pcm) { SYS_FREE(work) ; return 0 ; }
	for (int k=0;k<n;k++) {
		double v=work[k]*32767.0 ;
		if (v>32767.0) v=32767.0 ;
		if (v<-32767.0) v=-32767.0 ;
		pcm[k]=(short)(v<0?v-0.5:v+0.5) ;
	}
	SYS_FREE(work) ;
	return new BakedSource(pcm,n) ;
}

}
