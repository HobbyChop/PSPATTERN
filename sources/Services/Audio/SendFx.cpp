#include "SendFx.h"
#include "System/System/System.h"
#include <string.h>

namespace SendFx {

// ---- the accumulators -------------------------------------------
// Interleaved stereo, one block long. Buses add into these; the
// return drains them.
static fixed *dlyAcc_ = 0 ;
static fixed *revAcc_ = 0 ;
static int    accSamples_ = 0 ;
static bool   dlyFed_ = false ;
static bool   revFed_ = false ;
/* How many samples since anything was sent to each effect. Both used
   to run every block whatever the song did, so a project that used
   only the delay still paid for eight combs and four allpasses a
   side, and one that used neither paid for all of it. Measured, the
   pair cost six times a four operator FM voice -- more than every
   voice in the demo put together -- so what they cost when they have
   nothing to do matters. */
static int    dlyIdle_ = 1 << 30 ;
static int    revIdle_ = 1 << 30 ;

// ---- delay -------------------------------------------------------
static short *dlyL_ = 0, *dlyR_ = 0 ;
static int dlyLen_ = 11025 ;
static int dlyPos_ = 0 ;
static int dlyFb_ = 140 ;          // 0..255
static int division_ = DIV_D8 ;
static int bpm_ = 120 ;

// ---- reverb ------------------------------------------------------
// Eight combs and four allpasses per side. Four combs was the first
// attempt and the test caught it: with only four, the early tail is a
// train of separate echoes about 33ms apart -- 281 of the first 500
// milliseconds sat more than 40dB below the peak, which is a flutter,
// not a room. Eight fills those gaps in.
//
// The lengths are the Freeverb primes at 44.1k, with the right side
// offset by 23 samples so the two sides decorrelate -- that offset is
// the whole reason it sounds like a space rather than a mono effect
// glued to the middle of the image.
#define NCOMB 8
#define NAP   4
static const int combLen_[NCOMB] =
	{ 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 } ;
static const int apLen_[NAP]     = { 556, 441, 341, 225 } ;
#define STEREO_SPREAD 23
// A comb is a feedback loop whose gain is 1/(1-fb), which at the top
// of the size range is about fifty. Feeding it anything like full
// scale saturates it, and eight saturated combs summed is eight times
// what a fixed can hold -- which is what was wrapping. Feed them
// quietly enough that the loudest setting still behaves (this is the
// same order as Freeverb's fixed input gain) and make the level back
// up after the bank, where it can be clamped instead of wrapped.
#define REVERB_IN_SHIFT 10             /* with the (256-rfb) factor */
#define REVERB_OUT_NUM  640            /* /256 after the comb sum */

static short *combBuf_[2][NCOMB] ;
static int    combPos_[2][NCOMB] ;
static int    combStore_[2][NCOMB] ;    // damping state, 64ths of a sample
static short *apBuf_[2][NAP] ;
static int    apPos_[2][NAP] ;
static int    combLenA_[2][NCOMB], apLenA_[2][NAP] ;
/* The reverb bank runs at HALF the sample rate.
   A reverb tail has essentially nothing above 8kHz in it -- the
   damping filter in each comb loop takes the top off by design -- so
   running the bank every other sample and holding between costs a
   band nobody can hear in a diffuse tail, and halves the most
   expensive thing in the mixer. The line lengths are halved with it
   so the room stays the same size; what changes is the resolution of
   the diffusion, not its timing. */
static int    revPhase_ = 0 ;
static fixed  revHeld_[2] = { 0, 0 } ;
static fixed  revIn_[2] = { 0, 0 } ;

static int revSize_ = 160 ;        // 0..255 -> feedback
static int revDamp_ = 110 ;        // 0..255 -> hf loss per pass

static bool ready_ = false ;

// ------------------------------------------------------------------

static short *alloc(int n) {
	short *p = (short *)malloc(n * sizeof(short)) ;
	if (p) memset(p, 0, n * sizeof(short)) ;
	return p ;
}

void Init(int sampleRate) {
	if (ready_) return ;
	(void)sampleRate ;
	dlyL_ = alloc(SENDFX_MAX_DELAY) ;
	dlyR_ = alloc(SENDFX_MAX_DELAY) ;
	for (int s = 0 ; s < 2 ; s++) {
		int off = s ? STEREO_SPREAD : 0 ;
		for (int i = 0 ; i < NCOMB ; i++) {
			combLenA_[s][i] = (combLen_[i] + off) / 2 ;
			combBuf_[s][i] = alloc(combLenA_[s][i]) ;
			combPos_[s][i] = 0 ;
			combStore_[s][i] = 0 ;
		}
		for (int i = 0 ; i < NAP ; i++) {
			apLenA_[s][i] = (apLen_[i] + off) / 2 ;
			apBuf_[s][i] = alloc(apLenA_[s][i]) ;
			apPos_[s][i] = 0 ;
		}
	}
	ready_ = (dlyL_ && dlyR_) ;
	SetDelayDivision(division_) ;
}

void Close() {
	SAFE_FREE(dlyL_) ; SAFE_FREE(dlyR_) ;
	for (int s = 0 ; s < 2 ; s++) {
		for (int i = 0 ; i < NCOMB ; i++) SAFE_FREE(combBuf_[s][i]) ;
		for (int i = 0 ; i < NAP ; i++) SAFE_FREE(apBuf_[s][i]) ;
	}
	SAFE_FREE(dlyAcc_) ; SAFE_FREE(revAcc_) ;
	accSamples_ = 0 ;
	ready_ = false ;
}

void Flush() {
	if (dlyL_) memset(dlyL_, 0, SENDFX_MAX_DELAY * sizeof(short)) ;
	if (dlyR_) memset(dlyR_, 0, SENDFX_MAX_DELAY * sizeof(short)) ;
	for (int s = 0 ; s < 2 ; s++) {
		for (int i = 0 ; i < NCOMB ; i++) {
			if (combBuf_[s][i])
				memset(combBuf_[s][i], 0,
				       combLenA_[s][i] * sizeof(short)) ;
			combStore_[s][i] = 0 ;
		}
		for (int i = 0 ; i < NAP ; i++)
			if (apBuf_[s][i])
				memset(apBuf_[s][i], 0, apLenA_[s][i] * sizeof(short)) ;
	}
	dlyFed_ = revFed_ = false ;
	dlyIdle_ = revIdle_ = 1 << 30 ;
	revPhase_ = 0 ;
	revHeld_[0] = revHeld_[1] = 0 ;
	revIn_[0] = revIn_[1] = 0 ;
}

// ---- parameters --------------------------------------------------

static const char *divNames_[DIV_COUNT] = {
	"1/16", "1/8T", "1/8", "3/16", "1/4", "3/8", "1/2"
} ;
// numerator/denominator of a QUARTER note
static const int divNum_[DIV_COUNT] = { 1, 1, 1, 3, 1, 3, 2 } ;
static const int divDen_[DIV_COUNT] = { 4, 3, 2, 4, 1, 2, 1 } ;

const char *DivisionName(int d) {
	if (d < 0 || d >= DIV_COUNT) return "?" ;
	return divNames_[d] ;
}

static void recalcDelay() {
	if (bpm_ < 1) bpm_ = 1 ;
	// samples in a quarter note, then the division of it
	long long quarter = (long long)44100 * 60 / bpm_ ;
	long long n = quarter * divNum_[division_] / divDen_[division_] ;
	if (n < 32) n = 32 ;
	if (n > SENDFX_MAX_DELAY) n = SENDFX_MAX_DELAY ;
	dlyLen_ = (int)n ;
	if (dlyPos_ >= dlyLen_) dlyPos_ = 0 ;
}

void SetTempo(int bpm) { bpm_ = bpm ; recalcDelay() ; }
void SetDelayDivision(int d) {
	if (d < 0) d = 0 ;
	if (d >= DIV_COUNT) d = DIV_COUNT - 1 ;
	division_ = d ; recalcDelay() ;
}
void SetDelayFeedback(int f) { dlyFb_ = f < 0 ? 0 : (f > 250 ? 250 : f) ; }
void SetReverbSize(int s)    { revSize_ = s < 0 ? 0 : (s > 255 ? 255 : s) ; }
void SetReverbDamp(int d)    { revDamp_ = d < 0 ? 0 : (d > 255 ? 255 : d) ; }

bool Active() { return dlyFed_ || revFed_ ; }

/* Everything below adds signals together, and a full scale sample in
   Q15 is 32767<<15 -- half the range of the 32 bit int it lives in.
   Two of anything is therefore the entire budget, and this file adds
   eight channels into a send, eight comb outputs into a tail, and a
   delay tap into its own feedback. Every one of those overflowed.

   An overflowed sum is not distortion, it is a sign flip: the tail
   jumps the full height of the waveform in one sample. Driven hard,
   the reverb produced four thousand of those in three seconds, which
   is heard as clicking that sounds like dropouts, and as dirt on
   whichever channel is feeding the effect hardest.

   It got through the first round of tests because those asked only
   whether the output stayed BOUNDED -- and wrapping keeps a value
   bounded, that is the whole problem with it. */
static inline fixed clampfx(long long v) {
    const long long M = 1073709056LL;          /* i2fp(32767) */
    if (v > M) return (fixed)M;
    if (v < -M) return (fixed)(-M);
    return (fixed)v;
}

// ---- the send tap ------------------------------------------------

static bool ensureAcc(int samplecount) {
	if (accSamples_ >= samplecount && dlyAcc_ && revAcc_) return true ;
	SAFE_FREE(dlyAcc_) ; SAFE_FREE(revAcc_) ;
	dlyAcc_ = (fixed *)malloc(samplecount * 2 * sizeof(fixed)) ;
	revAcc_ = (fixed *)malloc(samplecount * 2 * sizeof(fixed)) ;
	if (!dlyAcc_ || !revAcc_) { accSamples_ = 0 ; return false ; }
	accSamples_ = samplecount ;
	memset(dlyAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	memset(revAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	return true ;
}

void Accumulate(const fixed *buffer, int samplecount,
                int delaySend, int reverbSend) {
	if (!ready_) return ;
	if (delaySend <= 0 && reverbSend <= 0) return ;
	if (!ensureAcc(samplecount)) return ;
	int n = samplecount * 2 ;

	// Shift first, then multiply.
	//
	// This is the busiest loop in the mixer: it runs for every sample
	// of every channel that sends, twice over when a channel sends to
	// both. Eight channels sending both is more than twenty thousand
	// passes through it per block.
	//
	// It used to widen to 64 bit to do it, because a Q15 sample times
	// a 0..255 send overflows an int. Shifting the sample down by the
	// eight bits the send is about to shift it back up by keeps the
	// product under 2^31, so the multiply is a single 32 bit one. What
	// that costs is the bottom eight bits of a value that still has
	// seven fractional bits left and is on its way into a send anyway.
	//
	// The add stays 64 bit: both terms are clamped to i2fp(32767) and
	// their sum can just pass what an int holds. An add is not what
	// was expensive here.
	if (delaySend > 0) {
		if (!dlyFed_) { memset(dlyAcc_, 0, n * sizeof(fixed)) ;
		                dlyFed_ = true ; }
		dlyIdle_ = 0 ;
		for (int i = 0 ; i < n ; i++)
			dlyAcc_[i] = clampfx((long long)dlyAcc_[i] +
			                     ((buffer[i] >> 8) * delaySend)) ;
	}
	if (reverbSend > 0) {
		if (!revFed_) { memset(revAcc_, 0, n * sizeof(fixed)) ;
		                revFed_ = true ; }
		revIdle_ = 0 ;
		for (int i = 0 ; i < n ; i++)
			revAcc_[i] = clampfx((long long)revAcc_[i] +
			                     ((buffer[i] >> 8) * reverbSend)) ;
	}
}

// ---- the return --------------------------------------------------

static inline short clip16(fixed v) {
	int s = fp2i(v) ;
	if (s > 32767) s = 32767 ;
	if (s < -32768) s = -32768 ;
	return (short)s ;
}

bool Return::Render(fixed *buffer, int samplecount) {

	if (!ready_) return false ;

	// Nothing was sent this block, but the lines may still be ringing.
	// Keep running until they have decayed, or a stab into a long
	// reverb would be cut off the instant the note ended.
	// A line that has had nothing put into it for longer than its own
	// tail has nothing left to say, and running it is pure cost. The
	// delay's tail is its own length times however many audible hops
	// the feedback allows; four seconds covers the reverb at any size.
	if (!dlyFed_) dlyIdle_ += samplecount ;
	if (!revFed_) revIdle_ += samplecount ;
	bool runDly = dlyFed_ || dlyIdle_ < (dlyLen_ * 12) ;
	bool runRev = revFed_ || revIdle_ < (4 * 44100) ;
	if (!runDly && !runRev) return false ;
	if (!ensureAcc(samplecount)) return false ;
	if (!dlyFed_) memset(dlyAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	if (!revFed_) memset(revAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	if (!runDly) memset(dlyAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	if (!runRev) memset(revAcc_, 0, samplecount * 2 * sizeof(fixed)) ;

	int fb = dlyFb_ ;
	// 0.70 .. 0.98 of a pass, the useful span: below 0.7 the tail is
	// gone before you hear it, above 0.98 it stops being a room and
	// becomes a drone.
	int rfb = 179 + (revSize_ * 72) / 255 ;    // 179..251 of 256
	int damp = revDamp_ ;
	int keep = 256 - damp ;

	for (int i = 0 ; i < samplecount ; i++) {

		fixed dOutL = 0, dOutR = 0 ;
		if (runDly) {
		// --- delay: a ping-pong, which is why it is stereo at all.
		// The left tap feeds the right line and vice versa, so a
		// mono source still opens out across the image.
		// Whole samples, as the comb bank below already does.
		//
		// The line holds shorts, so the fraction the Q15 version
		// carried through the feedback multiply was thrown away at the
		// store anyway. Doing it in sample units puts the products
		// inside an int -- 32767 by 255 is eight million -- and takes
		// two 64 bit multiplies and two 64 bit clamps per sample out
		// of a loop that runs at the sample rate whenever anything is
		// echoing.
		int outL = dlyL_[dlyPos_] ;
		int outR = dlyR_[dlyPos_] ;
		dOutL = i2fp(outL) ;
		dOutR = i2fp(outR) ;
		int inL = fp2i(dlyAcc_[i * 2]) ;
		int inR = fp2i(dlyAcc_[i * 2 + 1]) ;
		int fbL = inL + ((outR * fb) >> 8) ;
		int fbR = inR + ((outL * fb) >> 8) ;
		if (fbL > 32767) fbL = 32767 ; if (fbL < -32768) fbL = -32768 ;
		if (fbR > 32767) fbR = 32767 ; if (fbR < -32768) fbR = -32768 ;
		dlyL_[dlyPos_] = (short)fbL ;
		dlyR_[dlyPos_] = (short)fbR ;
		if (++dlyPos_ >= dlyLen_) dlyPos_ = 0 ;
		}

		// --- reverb: the delay's output goes in too, so a long echo
		// smears into the tail instead of sitting on top of it
		fixed rIn[2] ;
		rIn[0] = clampfx((long long)revAcc_[i * 2] + (dOutL >> 2)) ;
		rIn[1] = clampfx((long long)revAcc_[i * 2 + 1] + (dOutR >> 2)) ;

		fixed wet[2] = { 0, 0 } ;
		if (runRev) {
		// collect both samples, run the bank on every second one
		revIn_[0] += rIn[0] >> 1 ;
		revIn_[1] += rIn[1] >> 1 ;
		if (++revPhase_ < 2) {
			wet[0] = revHeld_[0] ;
			wet[1] = revHeld_[1] ;
		} else {
		revPhase_ = 0 ;
		rIn[0] = revIn_[0] ; rIn[1] = revIn_[1] ;
		revIn_[0] = revIn_[1] = 0 ;
		for (int s = 0 ; s < 2 ; s++) {
			// Each comb is a feedback loop with a gain of 1/(1-fb),
			// which at the top of the size range is about fifty. Fed
			// anything like full scale it saturates, and eight
			// saturated combs summed is eight times what a fixed can
			// hold. Feed them quietly and average the bank rather
			// than summing it: the level is made back up by the comb
			// gain itself, which is what a reverb is.
			// Scale the input by how much the loop is NOT feeding
			// back. A comb multiplies what it is given by
			// 1/(1-fb), so without this the same send is 23dB
			// louder at the top of the size range than at the
			// bottom -- "bigger room" would double as "much
			// louder", which is not what the control says.
			// WHOLE SAMPLES, NOT Q15, for this section.
			//
			// The comb buffers hold shorts, so everything here is
			// naturally +/-32767 and the only reason it was in Q15
			// was that the rest of the file is. That cost three 64
			// bit multiplies per comb per sample -- seven hundred
			// thousand comb steps a second, on a chip with no 64 bit
			// multiply -- and it bought nothing: eight whole samples
			// summed is 262136, which a 32 bit int holds with
			// fourteen bits to spare. In sample units the overflow
			// the clamps were there to prevent cannot arise, so the
			// clamps go too. Same arithmetic, same output.
			//
			// The damping state keeps 6 fractional bits. That is the
			// one place precision is lost against the Q15 version,
			// and it is a sixth of an LSB of a 16 bit sample.
			int x = fp2i(rIn[s]) ;
			// scale the input by how much the loop is NOT feeding
			// back: a comb multiplies what it is given by 1/(1-fb),
			// so without this "bigger room" would double as "23dB
			// louder", which is not what the control says
			int in = (x * (256 - rfb)) >> REVERB_IN_SHIFT ;
			int acc = 0 ;
			for (int c = 0 ; c < NCOMB ; c++) {
				short *b = combBuf_[s][c] ;
				int p = combPos_[s][c] ;
				int out = b[p] ;                  /* +/-32767 */
				acc += out ;
				// one-pole damping inside the loop: the tail loses
				// its top end as it decays, which is what stops a
				// digital reverb sounding like a metal pipe.
				// store is in 64ths of a sample: out*keep is at most
				// 32767*256, and store*damp at most 2.1M*256, both
				// well inside 32 bits.
				int store = ((out * keep) >> 2) +
				            ((combStore_[s][c] * damp) >> 8) ;
				combStore_[s][c] = store ;
				int v = in + (((store * rfb) >> 8) >> 6) ;
				if (v > 32767) v = 32767 ;
				else if (v < -32768) v = -32768 ;
				b[p] = (short)v ;
				if (++p >= combLenA_[s][c]) p = 0 ;
				combPos_[s][c] = p ;
			}
			// eight combs at 640/256 is at most 655k, so this is the
			// one place the bank still has to be held to full scale
			int dif = (acc * REVERB_OUT_NUM) >> 8 ;
			if (dif > 32767) dif = 32767 ;
			else if (dif < -32768) dif = -32768 ;
			for (int a = 0 ; a < NAP ; a++) {
				short *b = apBuf_[s][a] ;
				int p = apPos_[s][a] ;
				int bufout = b[p] ;
				int o = bufout - dif ;
				int w = dif + (bufout >> 1) ;
				if (w > 32767) w = 32767 ;
				else if (w < -32768) w = -32768 ;
				b[p] = (short)w ;
				if (++p >= apLenA_[s][a]) p = 0 ;
				apPos_[s][a] = p ;
				if (o > 32767) o = 32767 ;
				else if (o < -32768) o = -32768 ;
				dif = o ;
			}
			wet[s] = i2fp(dif) ;
			revHeld_[s] = wet[s] ;
		}
		}
		}

		// The return is just another child of the master, so it has
		// to obey the same rule every bus does: never hand upstream
		// more than full scale. Left alone the delay and the reverb
		// together measured twice that, which is a whole bus worth of
		// the master's headroom spent on the effects.
		buffer[i * 2]     = i2fp(clip16(clampfx((long long)dOutL + wet[0]))) ;
		buffer[i * 2 + 1] = i2fp(clip16(clampfx((long long)dOutR + wet[1]))) ;
	}

	dlyFed_ = revFed_ = false ;
	return true ;
}

}
