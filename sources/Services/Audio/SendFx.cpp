#include "SendFx.h"
#include "System/System/System.h"
#include <string.h>

namespace SendFx {

// ---- the accumulators -------------------------------------------
// Interleaved stereo, one block long. Buses add into these; the
// return drains them.
static int    accSamples_ = 0 ;

/* Who runs the bank, and when.
 *
 * On one core the answer is "the audio thread, now", and the wet
 * signal is written straight into the master's return slot.
 *
 * With a second core it cannot be now: the point of the Media Engine
 * is that it works on this block while this core gets on with the
 * next one, so its result is not ready until the block after the one
 * that fed it. So the return hands out what the worker finished last
 * time and posts the current block behind it.
 *
 * That is one block of latency on the WET path only -- 5.8ms at 256
 * frames. On a reverb it is inaudible by construction, and on a delay
 * it moves every echo by less than the jitter of a finger. The dry
 * path is untouched, so nothing about the timing of the notes moves.
 *
 * Off by default, and the single core path below is byte for byte
 * what it always was: no copy, no extra buffer, no branch in the
 * per-sample loop.                                                 */
/* The wet signal on its way back, as a ring rather than one buffer.
 *
 * The first version held one finished block and handed it over on the
 * next call, which works only if every call is the same size. The
 * device does not do that: the player renders in chunks between
 * ticks, so the return is called many times per audio block with
 * whatever count the tick boundary left. When the size changed, the
 * finished block was dropped and silence went out instead -- the wet
 * chopped into fragments with hard zero edges at both ends, which is
 * a click, and a click on most calls is a haze of distortion over
 * everything. Measured at 81% of the signal wrong.
 *
 * A ring makes the deferral a number of SAMPLES rather than a number
 * of calls, so how the samples are chunked stops mattering. */
#define WET_RING_FRAMES 2048
static fixed  wetRing_[WET_RING_FRAMES * 2] ;
static int    wetRead_ = 0 ;
static int    wetFill_ = 0 ;

static void wetReset() {
	wetRead_ = 0 ; wetFill_ = 0 ;
	memset(wetRing_, 0, sizeof(wetRing_)) ;
}

static bool   deferred_ = false ;
/* True only once a second core is actually running the bank. Separate
   from deferred_ on purpose: the timing can be exercised without the
   hardware, and is, but the cache handling below must not cost
   anything while the work is still happening on this core. */
static bool   meDriving_ = false ;
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
static int dlyLen_ = 11025 ;
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

/* The comb and allpass lines live in ONE allocation.
 *
 * They were twenty four separate mallocs, so where they landed
 * relative to each other was whatever the heap felt like that run.
 * Twenty four independent streams walking twenty four arbitrary
 * addresses through a 16KB data cache can map onto the same sets and
 * evict each other every sample, and whether they do is luck rather
 * than design -- which is a bad thing for the most expensive item in
 * the mixer to be resting on.
 *
 * One block, cache line aligned, with a one line skew per buffer so
 * consecutive lines do not all begin at the same offset within a set.
 * The lengths are the Freeverb primes, which are mutually prime by
 * construction, so their live positions stay spread through the block
 * rather than marching in step.
 *
 * This costs nothing: same bytes, laid out on purpose instead of by
 * accident. */
#define REV_LINE      32        /* cache line, bytes */
#define REV_SKEW      1         /* lines of skew between buffers */
static short *revBlock_ = 0 ;
/* The reverb bank runs at HALF the sample rate.
   A reverb tail has essentially nothing above 8kHz in it -- the
   damping filter in each comb loop takes the top off by design -- so
   running the bank every other sample and holding between costs a
   band nobody can hear in a diffuse tail, and halves the most
   expensive thing in the mixer. The line lengths are halved with it
   so the room stays the same size; what changes is the resolution of
   the diffusion, not its timing. */

/* Everything the bank touches, in one place.
 *
 * This is the set that has to cross to the Media Engine: a second
 * Allegrex with its own cache and no coherency with this one. What
 * goes over cannot be "whatever the function happens to reach" -- it
 * has to be an enumerable block at a known address, so that making it
 * visible to the other core is one decision rather than twenty.
 *
 * Three kinds of thing live here and the difference matters:
 *
 *   the snapshot -- the knob positions as they were when the block
 *   started. Taken once by the audio thread and read by the bank, so
 *   that turning the reverb size during a block cannot change the
 *   coefficients underneath a core that is halfway through it.
 *
 *   the lines and their positions -- the actual delay memory and
 *   where each circular buffer is up to. Owned by whichever core is
 *   running the bank, and by nobody else while it runs.
 *
 *   the two input accumulators -- what the mixer put in this block.
 *
 * What is deliberately NOT here: the knobs themselves, the fed and
 * idle flags, and the ready flag. Those belong to the audio thread,
 * are read and written between blocks, and the bank never sees them.
 */
struct Bank {
	// snapshot, taken per block by the audio thread
	int    fb_, rfb_, damp_, keep_ ;
	int    dlyLenS_ ;
	bool   runDly_, runRev_ ;

	// the delay
	short *dlyL_, *dlyR_ ;
	int    dlyPos_ ;

	// the reverb
	short *combBuf_[2][NCOMB] ;
	int    combPos_[2][NCOMB] ;
	int    combStore_[2][NCOMB] ;    // damping state, 64ths of a sample
	short *apBuf_[2][NAP] ;
	int    apPos_[2][NAP] ;
	int    combLenA_[2][NCOMB], apLenA_[2][NAP] ;
	int    revPhase_ ;
	fixed  revHeld_[2], revIn_[2] ;

	// what the mixer put in this block
	fixed *dlyAcc_, *revAcc_ ;

	// Where the wet goes when somebody else is running the bank.
	fixed *wet_ ;
} ;

static Bank  bankStore_ ;
static Bank *bank_ = &bankStore_ ;
#define BK (*bank_)

static int revSize_ = 160 ;        // 0..255 -> feedback
static int revDamp_ = 110 ;        // 0..255 -> hf loss per pass

static bool ready_ = false ;

// ------------------------------------------------------------------

static short *alloc(int n) {
	short *p = (short *)malloc(n * sizeof(short)) ;
	if (p) memset(p, 0, n * sizeof(short)) ;
	return p ;
}

/* Round a length in shorts up to a whole cache line, then add the
   skew, so the next buffer starts a line further into the set than
   this one did. */
static int lineRound(int shorts, int skewLines) {
	int bytes = shorts * (int)sizeof(short) ;
	bytes = ((bytes + REV_LINE - 1) / REV_LINE) * REV_LINE ;
	bytes += skewLines * REV_LINE ;
	return bytes / (int)sizeof(short) ;
}

void Init(int sampleRate) {
	if (ready_) return ;
	(void)sampleRate ;
	BK.dlyL_ = alloc(SENDFX_MAX_DELAY) ;
	BK.dlyR_ = alloc(SENDFX_MAX_DELAY) ;

	// lengths first, so the block can be sized before anything is
	// handed an address inside it
	for (int s = 0 ; s < 2 ; s++) {
		int off = s ? STEREO_SPREAD : 0 ;
		for (int i = 0 ; i < NCOMB ; i++)
			BK.combLenA_[s][i] = (combLen_[i] + off) / 2 ;
		for (int i = 0 ; i < NAP ; i++)
			BK.apLenA_[s][i] = (apLen_[i] + off) / 2 ;
	}

	int total = REV_LINE / (int)sizeof(short) ;    // room to align the head
	for (int s = 0 ; s < 2 ; s++) {
		for (int i = 0 ; i < NCOMB ; i++)
			total += lineRound(BK.combLenA_[s][i], REV_SKEW) ;
		for (int i = 0 ; i < NAP ; i++)
			total += lineRound(BK.apLenA_[s][i], REV_SKEW) ;
	}
	revBlock_ = alloc(total) ;

	if (revBlock_) {
		// align the head, then walk the block handing out lines
		unsigned long a = (unsigned long)revBlock_ ;
		short *p = (short *)((a + REV_LINE - 1) & ~(unsigned long)(REV_LINE - 1)) ;
		for (int s = 0 ; s < 2 ; s++) {
			for (int i = 0 ; i < NCOMB ; i++) {
				BK.combBuf_[s][i] = p ;
				p += lineRound(BK.combLenA_[s][i], REV_SKEW) ;
				BK.combPos_[s][i] = 0 ;
				BK.combStore_[s][i] = 0 ;
			}
			for (int i = 0 ; i < NAP ; i++) {
				BK.apBuf_[s][i] = p ;
				p += lineRound(BK.apLenA_[s][i], REV_SKEW) ;
				BK.apPos_[s][i] = 0 ;
			}
		}
	} else {
		for (int s = 0 ; s < 2 ; s++) {
			for (int i = 0 ; i < NCOMB ; i++) {
				BK.combBuf_[s][i] = 0 ; BK.combPos_[s][i] = 0 ;
				BK.combStore_[s][i] = 0 ;
			}
			for (int i = 0 ; i < NAP ; i++) { BK.apBuf_[s][i] = 0 ; BK.apPos_[s][i] = 0 ; }
		}
	}

	ready_ = (BK.dlyL_ && BK.dlyR_ && revBlock_) ;

	SetDelayDivision(division_) ;
}

void Close() {
	SAFE_FREE(BK.dlyL_) ; SAFE_FREE(BK.dlyR_) ;
	// one block, so one free: the per line pointers are interior
	SAFE_FREE(revBlock_) ;
	for (int s = 0 ; s < 2 ; s++) {
		for (int i = 0 ; i < NCOMB ; i++) BK.combBuf_[s][i] = 0 ;
		for (int i = 0 ; i < NAP ; i++) BK.apBuf_[s][i] = 0 ;
	}
	SAFE_FREE(BK.dlyAcc_) ; SAFE_FREE(BK.revAcc_) ; SAFE_FREE(BK.wet_) ;
	accSamples_ = 0 ;
	ready_ = false ;
}

/* Who runs the bank. See deferred_ above.

   Nothing calls this yet: the Media Engine start-up is the piece that
   only real hardware can verify, so it stays off until there is
   something to defer TO. The protocol underneath it is exercised
   either way -- flipping this makes the return run a block behind
   while still processing on this core, which is exactly the timing
   the ME will impose, and the test asks for that. */
void SetDeferred(bool on) {
	if (on == deferred_) return ;
	deferred_ = on ;
	wetReset() ;
}

bool Deferred() { return deferred_ ; }

void Flush() {
	if (BK.dlyL_) memset(BK.dlyL_, 0, SENDFX_MAX_DELAY * sizeof(short)) ;
	if (BK.dlyR_) memset(BK.dlyR_, 0, SENDFX_MAX_DELAY * sizeof(short)) ;
	for (int s = 0 ; s < 2 ; s++) {
		for (int i = 0 ; i < NCOMB ; i++) {
			if (BK.combBuf_[s][i])
				memset(BK.combBuf_[s][i], 0,
				       BK.combLenA_[s][i] * sizeof(short)) ;
			BK.combStore_[s][i] = 0 ;
		}
		for (int i = 0 ; i < NAP ; i++)
			if (BK.apBuf_[s][i])
				memset(BK.apBuf_[s][i], 0, BK.apLenA_[s][i] * sizeof(short)) ;
	}
	dlyFed_ = revFed_ = false ;
	wetReset() ;
	dlyIdle_ = revIdle_ = 1 << 30 ;
	BK.revPhase_ = 0 ;
	BK.revHeld_[0] = BK.revHeld_[1] = 0 ;
	BK.revIn_[0] = BK.revIn_[1] = 0 ;
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
	if (BK.dlyPos_ >= dlyLen_) BK.dlyPos_ = 0 ;
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
	if (accSamples_ >= samplecount && BK.dlyAcc_ && BK.revAcc_ && BK.wet_)
		return true ;
	SAFE_FREE(BK.dlyAcc_) ; SAFE_FREE(BK.revAcc_) ; SAFE_FREE(BK.wet_) ;
	int bytes = samplecount * 2 * sizeof(fixed) ;
	BK.dlyAcc_ = (fixed *)malloc(bytes) ;
	BK.revAcc_ = (fixed *)malloc(bytes) ;
	BK.wet_    = (fixed *)malloc(bytes) ;
	if (!BK.dlyAcc_ || !BK.revAcc_ || !BK.wet_) { accSamples_ = 0 ; return false ; }
	accSamples_ = samplecount ;
	memset(BK.dlyAcc_, 0, bytes) ;
	memset(BK.revAcc_, 0, bytes) ;
	memset(BK.wet_, 0, bytes) ;
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
		if (!dlyFed_) { memset(BK.dlyAcc_, 0, n * sizeof(fixed)) ;
		                dlyFed_ = true ; }
		dlyIdle_ = 0 ;
		for (int i = 0 ; i < n ; i++)
			BK.dlyAcc_[i] = clampfx((long long)BK.dlyAcc_[i] +
			                     ((buffer[i] >> 8) * delaySend)) ;
	}
	if (reverbSend > 0) {
		if (!revFed_) { memset(BK.revAcc_, 0, n * sizeof(fixed)) ;
		                revFed_ = true ; }
		revIdle_ = 0 ;
		for (int i = 0 ; i < n ; i++)
			BK.revAcc_[i] = clampfx((long long)BK.revAcc_[i] +
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

/* The bank itself: input accumulators and line state in, wet out.
 *
 * Split out of Return::Render, which was gating policy and signal
 * processing in one function. Everything above the split decides
 * WHETHER to run -- what has been fed, what is still ringing, what
 * needs zeroing -- and touches globals the audio thread owns.
 * Everything below is arithmetic over two input buffers and the delay
 * and comb state, and depends on nothing else.
 *
 * That line is where the work has to be cut to move it to the Media
 * Engine, which is a second Allegrex with its own cache and no
 * coherency with this one. What crosses to it has to be an explicit,
 * enumerable set of buffers rather than "whatever the function
 * happens to reach", and this is that set: two inputs, one output,
 * the line memory, and the four coefficients snapshotted below so the
 * other core cannot see them change underneath it.
 *
 * Same arithmetic, same order, same output -- this commit moves no
 * work and changes no sound.                                       */
/* Handing the block over.
 *
 * The Media Engine is a second Allegrex with its own data cache and
 * no coherency with this one. Anything this core wrote that the other
 * must see has to be pushed out of this cache first, and anything the
 * other wrote that this core must read has to be dropped from this
 * cache before reading, or it reads its own stale copy.
 *
 * The surface is small and known, which is the whole point of having
 * gathered the bank into one struct: the two input accumulators, the
 * wet buffer coming back, and the snapshot at the head of the Bank.
 * The line memory does NOT cross -- while the other core is driving,
 * it is the only thing that touches it, and it wants that memory
 * cached, because each line advances one sample at a time and a cache
 * line serves sixteen of them. Making the lines uncached to avoid
 * thinking about coherency would throw that away and cost far more
 * than it saved.
 */
#ifdef __PSP__
#include <pspkernel.h>
static inline void pushOut(void *p, int bytes) {
	if (p) sceKernelDcacheWritebackRange(p, bytes) ;
}
static inline void pullIn(void *p, int bytes) {
	if (p) sceKernelDcacheInvalidateRange(p, bytes) ;
}
#else
static inline void pushOut(void *, int) {}
static inline void pullIn(void *, int) {}
#endif

static void processBank(fixed *buffer, int samplecount) {

	// From the snapshot, never from the knobs. The knobs belong to
	// the audio thread and can move while this is running; on one
	// core that is merely a coefficient changing mid block, and on
	// two it is a coefficient changing underneath a core that is
	// halfway through the block.
	const int fb   = BK.fb_ ;
	const int rfb  = BK.rfb_ ;
	const int damp = BK.damp_ ;
	const int keep = BK.keep_ ;
	const bool runDly = BK.runDly_ ;
	const bool runRev = BK.runRev_ ;

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
		int outL = BK.dlyL_[BK.dlyPos_] ;
		int outR = BK.dlyR_[BK.dlyPos_] ;
		dOutL = i2fp(outL) ;
		dOutR = i2fp(outR) ;
		int inL = fp2i(BK.dlyAcc_[i * 2]) ;
		int inR = fp2i(BK.dlyAcc_[i * 2 + 1]) ;
		int fbL = inL + ((outR * fb) >> 8) ;
		int fbR = inR + ((outL * fb) >> 8) ;
		if (fbL > 32767) fbL = 32767 ; if (fbL < -32768) fbL = -32768 ;
		if (fbR > 32767) fbR = 32767 ; if (fbR < -32768) fbR = -32768 ;
		BK.dlyL_[BK.dlyPos_] = (short)fbL ;
		BK.dlyR_[BK.dlyPos_] = (short)fbR ;
		if (++BK.dlyPos_ >= BK.dlyLenS_) BK.dlyPos_ = 0 ;
		}

		// --- reverb: the delay's output goes in too, so a long echo
		// smears into the tail instead of sitting on top of it
		fixed rIn[2] ;
		rIn[0] = clampfx((long long)BK.revAcc_[i * 2] + (dOutL >> 2)) ;
		rIn[1] = clampfx((long long)BK.revAcc_[i * 2 + 1] + (dOutR >> 2)) ;

		fixed wet[2] = { 0, 0 } ;
		if (runRev) {
		// collect both samples, run the bank on every second one
		BK.revIn_[0] += rIn[0] >> 1 ;
		BK.revIn_[1] += rIn[1] >> 1 ;
		if (++BK.revPhase_ < 2) {
			wet[0] = BK.revHeld_[0] ;
			wet[1] = BK.revHeld_[1] ;
		} else {
		BK.revPhase_ = 0 ;
		rIn[0] = BK.revIn_[0] ; rIn[1] = BK.revIn_[1] ;
		BK.revIn_[0] = BK.revIn_[1] = 0 ;
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
				short *b = BK.combBuf_[s][c] ;
				int p = BK.combPos_[s][c] ;
				int out = b[p] ;                  /* +/-32767 */
				acc += out ;
				// one-pole damping inside the loop: the tail loses
				// its top end as it decays, which is what stops a
				// digital reverb sounding like a metal pipe.
				// store is in 64ths of a sample: out*keep is at most
				// 32767*256, and store*damp at most 2.1M*256, both
				// well inside 32 bits.
				int store = ((out * keep) >> 2) +
				            ((BK.combStore_[s][c] * damp) >> 8) ;
				BK.combStore_[s][c] = store ;
				int v = in + (((store * rfb) >> 8) >> 6) ;
				if (v > 32767) v = 32767 ;
				else if (v < -32768) v = -32768 ;
				b[p] = (short)v ;
				if (++p >= BK.combLenA_[s][c]) p = 0 ;
				BK.combPos_[s][c] = p ;
			}
			// eight combs at 640/256 is at most 655k, so this is the
			// one place the bank still has to be held to full scale
			int dif = (acc * REVERB_OUT_NUM) >> 8 ;
			if (dif > 32767) dif = 32767 ;
			else if (dif < -32768) dif = -32768 ;
			for (int a = 0 ; a < NAP ; a++) {
				short *b = BK.apBuf_[s][a] ;
				int p = BK.apPos_[s][a] ;
				int bufout = b[p] ;
				int o = bufout - dif ;
				int w = dif + (bufout >> 1) ;
				if (w > 32767) w = 32767 ;
				else if (w < -32768) w = -32768 ;
				b[p] = (short)w ;
				if (++p >= BK.apLenA_[s][a]) p = 0 ;
				BK.apPos_[s][a] = p ;
				if (o > 32767) o = 32767 ;
				else if (o < -32768) o = -32768 ;
				dif = o ;
			}
			wet[s] = i2fp(dif) ;
			BK.revHeld_[s] = wet[s] ;
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

}

/* Give the block to whoever is running the bank.
 *
 * One place, so that starting the Media Engine is a change here and
 * nowhere else. Today the worker is this core, called straight
 * through, and the cache handling is skipped because there is nobody
 * to be incoherent with. */
/* Hand out n frames of wet, oldest first.
 *
 * If there are not enough banked yet -- the start of a stream, or the
 * first time a chunk arrives bigger than any seen so far -- the lead
 * is extended with silence rather than the shortfall being taken out
 * of the signal. That costs a little more latency once and then
 * settles; taking it out of the signal is what made the noise. */
static void wetEmit(fixed *out, int n) {
	if (wetFill_ < n) {
		int need = n - wetFill_ ;
		wetRead_ = (wetRead_ - need + WET_RING_FRAMES) % WET_RING_FRAMES ;
		for (int i = 0 ; i < need ; i++) {
			int k = (wetRead_ + i) % WET_RING_FRAMES ;
			wetRing_[k * 2] = 0 ;
			wetRing_[k * 2 + 1] = 0 ;
		}
		wetFill_ += need ;
	}
	for (int i = 0 ; i < n ; i++) {
		int k = (wetRead_ + i) % WET_RING_FRAMES ;
		out[i * 2]     = wetRing_[k * 2] ;
		out[i * 2 + 1] = wetRing_[k * 2 + 1] ;
	}
	wetRead_ = (wetRead_ + n) % WET_RING_FRAMES ;
	wetFill_ -= n ;
}

// what the worker finished goes behind whatever is already queued
static void wetAppend(const fixed *src, int n) {
	if (wetFill_ + n > WET_RING_FRAMES) return ;   // cannot happen; do not corrupt
	int w = (wetRead_ + wetFill_) % WET_RING_FRAMES ;
	for (int i = 0 ; i < n ; i++) {
		int k = (w + i) % WET_RING_FRAMES ;
		wetRing_[k * 2]     = src[i * 2] ;
		wetRing_[k * 2 + 1] = src[i * 2 + 1] ;
	}
	wetFill_ += n ;
}

static void postBlock(int samplecount) {

	if (!meDriving_) {
		processBank(BK.wet_, samplecount) ;
		return ;
	}

	// The other core is about to read these, so they have to be out
	// of this cache first.
	int bytes = samplecount * 2 * sizeof(fixed) ;
	pushOut(BK.dlyAcc_, bytes) ;
	pushOut(BK.revAcc_, bytes) ;
	pushOut(&BK, sizeof(Bank)) ;

	// -- the Media Engine goes here. It is the one piece that cannot
	// -- be verified anywhere but on the device, so it is the last
	// -- thing added rather than the first.
	processBank(BK.wet_, samplecount) ;

	// and its result has to be dropped from this cache before this
	// core reads it, or it reads what was there before.
	pullIn(BK.wet_, bytes) ;
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
	if (!runDly && !runRev && wetFill_ == 0) return false ;
	if (!ensureAcc(samplecount)) return false ;
	if (!dlyFed_) memset(BK.dlyAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	if (!revFed_) memset(BK.revAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	if (!runDly) memset(BK.dlyAcc_, 0, samplecount * 2 * sizeof(fixed)) ;
	if (!runRev) memset(BK.revAcc_, 0, samplecount * 2 * sizeof(fixed)) ;

	// take the snapshot, then run the bank against it
	BK.fb_   = dlyFb_ ;
	// 0.70 .. 0.98 of a pass, the useful span: below 0.7 the tail is
	// gone before you hear it, above 0.98 it stops being a room and
	// becomes a drone.
	BK.rfb_  = 179 + (revSize_ * 72) / 255 ;    // 179..251 of 256
	BK.damp_ = revDamp_ ;
	BK.keep_ = 256 - BK.damp_ ;
	BK.dlyLenS_ = dlyLen_ ;
	BK.runDly_  = runDly ;
	BK.runRev_  = runRev ;

	if (!deferred_) {
		// one core: straight into the return slot, as ever
		processBank(buffer, samplecount) ;
	} else {
		// Hand out what the worker finished last time. A size change
		// mid stream, or the first block after a start, has nothing
		// to hand out yet -- that is the one block of latency.
		wetEmit(buffer, samplecount) ;
		// then post this one behind it
		if (runDly || runRev) {
			postBlock(samplecount) ;
			wetAppend(BK.wet_, samplecount) ;
		}
	}

	dlyFed_ = revFed_ = false ;
	return true ;
}

}
