/* PSPATTERN Media Engine audio offload.
 *
 * The first live audio-effects bank (a feedback delay and an FDN reverb)
 * to run on the PSP's second Allegrex core inside a music application.
 * Offload design and DSP by HobbyChop: the cross-core hand-off and
 * wet ring, the FPU flush-to-zero for the Media Engine's missing denormal
 * hardware, and the uncached line handling.
 *
 * Built on mcidclan's psp-media-engine-custom-core library (MIT) for
 * Media Engine boot, kernel access, and standby handling. See
 * third_party/me-core/LICENSE.md and the repo NOTICE file.
 */
/* Second-core offload -- Phase 2: run the whole send bank on the ME.
 *
 * The Media Engine now runs the ENTIRE send bank: the delay AND the FDN
 * reverb. The main core only hands the two send accumulators across and
 * reads back the finished wet a block later; it does none of the DSP.
 *
 * Everything the ME touches lives in the uncached window (0x40000000):
 * the shared I/O buffers, the reverb's lines (RemapLines), and the delay
 * lines (passed in already aliased). The ME cannot use this core's data
 * cache, so cached main-RAM access faults -- uncached is mandatory.
 *
 * Two hardware facts the ME forces on us, both handled below:
 *  - CU1 is on from the boot stub, but the FPU has no denormal support;
 *    a subnormal raises a non-maskable trap the ME cannot service. FS
 *    (flush-to-zero) plus the integer flush in FdnReverb keep clear of it.
 *  - Uncached stores sit in a write buffer until `sync` drains them, so
 *    every main->ME flag write is bracketed with meLibSync().
 *
 * me-core.h is included ONCE in the whole program; this is that place.
 * All behind PSP_ME_OFFLOAD.
 */
#ifdef PSP_ME_OFFLOAD

#include <me-core-mapper/me-core.h>
#include <psppower.h>
#include <pspkernel.h>

#undef PSP_VFPU_REVERB          /* the ME uses the scalar FDN path */
#define FDN_FLUSH_DENORMALS      /* the ME FPU traps on subnormals */
#include "Application/Utils/fixed.h"
#include "Services/Audio/FdnReverb.h"

#define ME_MAXN 2048            /* max frames per block the buffers hold */
#ifndef ME_FM_PROBE
#define ME_FM_PROBE 0
#endif

// Control words, uncached and shared with the ME.
meLibSetSharedUncached32(32);
#define ME_EXIT_  (meLibSharedMemory[0])   // main -> ME: stop
#define ME_BUSY   (meLibSharedMemory[1])   // main sets 1 = job posted; ME clears = done
#define ME_N      (meLibSharedMemory[2])   // frames in the posted job
#define ME_READY  (meLibSharedMemory[3])   // ME set once its loop is live
#define ME_HB     (meLibSharedMemory[4])   // ME loop heartbeat (climbs = alive)
#define ME_JOBS   (meLibSharedMemory[5])   // count of bank blocks the ME finished
#define ME_FB     (meLibSharedMemory[6])   // delay feedback (0..255)
#define ME_DLYLEN (meLibSharedMemory[7])   // delay length in samples
#define ME_RUNDLY (meLibSharedMemory[8])   // delay active this block
#define ME_DLYL   (meLibSharedMemory[9])   // delay line L, uncached-aliased
#define ME_DLYR   (meLibSharedMemory[10])  // delay line R, uncached-aliased
#define ME_DLYMAX (meLibSharedMemory[11])  // delay line capacity (samples)
#define ME_SIZE   (meLibSharedMemory[12])  // reverb size knob (0..255)
#define ME_DAMP   (meLibSharedMemory[13])  // reverb damp knob (0..255)
#define ME_FMBUF  (meLibSharedMemory[14])  // FM probe working set (uncached)
#define ME_FMCYC  (meLibSharedMemory[15])  // FM probe: CPU cycles for one 8-voice block
// Send-bus fold-in effect params (wet tail). [18..31] reserved for the
// duck/gate/comp/phaser/chorus effects wired in later phases.
#define ME_FREEZE (meLibSharedMemory[16])  // reverb infinite hold (0/1)
#define ME_DRIVE  (meLibSharedMemory[17])  // wet-bus drive amount (0..255)

// Interleaved-stereo I/O, all uncached. Two inputs (the delay and reverb
// send accumulators) and one output (the finished wet).
volatile u32 *meDlyIn __attribute__((aligned(64))) = 0;
volatile u32 *meRevIn __attribute__((aligned(64))) = 0;
volatile u32 *meOut   __attribute__((aligned(64))) = 0;
static Uncached32 meDlyInH_ = { (volatile u32 **)&meDlyIn, 0 };
static Uncached32 meRevInH_ = { (volatile u32 **)&meRevIn, 0 };
static Uncached32 meOutH_   = { (volatile u32 **)&meOut,   0 };

static FdnReverb meRev_;                    // reverb, owned by the ME while running
static volatile unsigned int meCalls_ = 0;  // main-core: hand-off count
static int   mePrevN_ = 0;                  // frames the ME output currently holds

// Fixed-point clamps, matching SendFx's own (this object cannot see them).
static inline fixed meClampfx(long long v) {
	const long long M = 1073709056LL;       // i2fp(32767)
	if (v >  M) return (fixed) M;
	if (v < -M) return (fixed)(-M);
	return (fixed)v;
}
// Smooth saturation: unity below ~0.8 full scale, asymptotic to full
// scale above, so a reverb driven past 0dB rounds off instead of
// grain-clipping. Leaves normal levels untouched.
static inline float meSoftClip(float x) {
	const float L = 32767.0f, T = 26000.0f;
	float a = x < 0.0f ? -x : x;
	if (a <= T) return x;
	float sign = x < 0.0f ? -1.0f : 1.0f;
	float over = a - T, range = L - T;
	return sign * (T + range * over / (over + range));
}
static inline short meClip16(fixed v) {
	int s = fp2i(v);
	if (s >  32767) s =  32767;
	if (s < -32768) s = -32768;
	return (short)s;
}

#if ME_FM_PROBE
// Representative 8-voice FM block, timed on the ME with the tables UNCACHED
// (worst case -- no scratchpad). renderFm is memory-bound (~5.6 mem ops per
// multiply): 4 operators/voice each doing an interpolated sine lookup
// (fmSin + fmSinD) plus a per-voice filter tap. If 8 voices x 256 samples
// fit inside one block here, they fit anywhere.
#define FMP_VOICES 8
#define FMP_OPS    4
#define FMP_BLOCK  256
static inline unsigned int meCount(void) {
	unsigned int c; asm volatile("mfc0 %0, $9\n nop\n" : "=r"(c)); return c;
}
struct FmpVoice { unsigned int phase[FMP_OPS], inc[FMP_OPS]; int level[FMP_OPS]; int lo, bd, cut, vol; };
static void fmProbeRun(void) {
	unsigned int base = (unsigned int)ME_FMBUF;
	if (!base) { ME_FMCYC = 0; return; }
	short *sinL  = (short*)base;              // 1024
	short *sinD  = (short*)(base + 2048);     // 1024
	short *cutL  = (short*)(base + 4096);     // 256
	FmpVoice *vx = (FmpVoice*)(base + 4608);  // 8 voices
	static const int dest[FMP_OPS] = {4,0,0,0};   // op3->out, op2/1/0 -> op0 chain-ish
	volatile int sink = 0;
	unsigned int best = 0xFFFFFFFFu;
	for (int rep=0; rep<4; rep++) {
		unsigned int t0 = meCount();
		for (int i=0;i<FMP_BLOCK;i++) {
			int mix = 0;
			for (int vi=0; vi<FMP_VOICES; vi++) {
				FmpVoice *v = &vx[vi];
				int fed[5] = {0,0,0,0,0};
				for (int op=FMP_OPS-1; op>=0; op--) {
					unsigned int ph = v->phase[op] + (unsigned int)(fed[op] << 6);
					int idx  = (ph >> 22) & 1023;
					int frac = (ph >> 6) & 0xFFFF;
					int sv   = sinL[idx] + ((sinD[idx] * frac) >> 16);   // 2 uncached reads
					fed[dest[op]] += (sv * v->level[op]) >> 8;
					v->phase[op] += v->inc[op];
				}
				int f  = cutL[v->cut];                                   // 1 uncached read
				v->lo += (f * v->bd) >> 16;
				int hp = fed[4] - v->lo - v->bd;
				v->bd += (f * hp) >> 16;
				mix += (v->lo * v->vol) >> 12;
			}
			sink += mix;
		}
		unsigned int d = (meCount() - t0) * 2u;   // Count ticks every 2 cycles
		if (d < best) best = d;
	}
	ME_FMCYC = best + (unsigned int)(sink & 0);   // keep sink live
}
#endif

// The second core's program: run the whole send bank each posted block.
void meLibOnProcess(void) {
	// Enable FPU flush-to-zero (FS, FCR31 bit 24); re-assert CU1 first.
	asm volatile(
		".set push\n" ".set noreorder\n"
		"mfc0   $8,  $12\n" "lui $9, 0x2000\n" "or $8,$8,$9\n"
		"mtc0   $8,  $12\n" "sync\n"
		"cfc1   $8,  $31\n" "lui $9, 0x0100\n" "or $8,$8,$9\n"
		"ctc1   $8,  $31\n" "nop\n" ".set pop\n"
		::: "$8", "$9", "memory"
	);
	meLibDcacheWritebackInvalidateAll();   // read the instance fresh from RAM
	meRev_.Flush();
#if ME_FM_PROBE
	fmProbeRun();
#endif
	ME_READY = 1;

	fixed *dlyIn = (fixed *)meDlyIn;
	fixed *revIn = (fixed *)meRevIn;
	fixed *out   = (fixed *)meOut;
	int   dlyPos = 0;
	int   lastSize = -1, lastDamp = -1, lastFreeze = -1;
	// reverb runs half-rate, like the main-core FDN: accumulate the two
	// samples' average, run the network once, hold the wet between.
	int   revPhase = 0;
	fixed revSum0 = 0, revSum1 = 0, revHold0 = 0, revHold1 = 0;

	while (ME_EXIT_ == 0) {
		ME_HB++;
		if (ME_BUSY) {
			int n = (int)ME_N;
			if (n > ME_MAXN) n = ME_MAXN;

			// apply knob changes here, on the owning core -- never let
			// the main core mutate the instance the ME is reading
			int sz = (int)ME_SIZE, dp = (int)ME_DAMP;
			int fz = (int)ME_FREEZE;
			if (fz != lastFreeze) {
				meRev_.SetFreeze(fz != 0); lastFreeze = fz;
				// while frozen, size/damp are held; remember them so the
				// next real change re-applies (unfreeze rebuilds anyway)
				if (fz) { lastSize = sz; lastDamp = dp; }
			}
			if (!fz) {
				if (sz != lastSize) { meRev_.SetSize(sz); lastSize = sz; }
				if (dp != lastDamp) { meRev_.SetDamp(dp); lastDamp = dp; }
			}
			int drive = (int)ME_DRIVE;

			short *dlyL   = (short *)ME_DLYL;
			short *dlyR   = (short *)ME_DLYR;
			int    fb     = (int)ME_FB;
			int    dlyLen = (int)ME_DLYLEN;
			int    runDly = (int)ME_RUNDLY;
			bool   haveDly = dlyL && dlyR && dlyLen > 0;
			if (haveDly && dlyPos >= dlyLen) dlyPos = 0;

			for (int k = 0; k < n; k++) {
				fixed dOutL = 0, dOutR = 0;
				if (runDly && haveDly) {
					int outL = dlyL[dlyPos], outR = dlyR[dlyPos];
					dOutL = i2fp(outL); dOutR = i2fp(outR);
					int inL = fp2i(dlyIn[k * 2]), inR = fp2i(dlyIn[k * 2 + 1]);
					int fbL = inL + ((outR * fb) >> 8);
					int fbR = inR + ((outL * fb) >> 8);
					if (fbL >  32767) fbL =  32767; if (fbL < -32768) fbL = -32768;
					if (fbR >  32767) fbR =  32767; if (fbR < -32768) fbR = -32768;
					dlyL[dlyPos] = (short)fbL; dlyR[dlyPos] = (short)fbR;
					if (++dlyPos >= dlyLen) dlyPos = 0;
				}
				fixed rInL = meClampfx((long long)revIn[k * 2]     + (dOutL >> 2));
				fixed rInR = meClampfx((long long)revIn[k * 2 + 1] + (dOutR >> 2));
				revSum0 += rInL >> 1;
				revSum1 += rInR >> 1;
				if (++revPhase >= 2) {
					revPhase = 0;
					float oL, oR;
					meRev_.ProcessOneScalar(fz ? 0.0f : fp2fl(revSum0),
					                        fz ? 0.0f : fp2fl(revSum1), oL, oR);
					// clamp in FLOAT before fl2fp -- the scalar cast wraps
					// on int32 overflow where the VFPU path saturates
					revHold0 = fl2fp(meSoftClip(oL * 0.3f));
					revHold1 = fl2fp(meSoftClip(oR * 0.3f));
					revSum0 = revSum1 = 0;
				}
				fixed wl = meClampfx((long long)dOutL + revHold0);
				fixed wr = meClampfx((long long)dOutR + revHold1);
				if (drive) {
					float g = 1.0f + drive * (3.0f / 255.0f);
					wl = fl2fp(meSoftClip(fp2fl(wl) * g));
					wr = fl2fp(meSoftClip(fp2fl(wr) * g));
				}
				out[k * 2]     = i2fp(meClip16(wl));
				out[k * 2 + 1] = i2fp(meClip16(wr));
			}
			meLibSync();       // land all out[] stores in RAM before the flag
			ME_BUSY = 0;
			ME_JOBS++;
		}
		meLibDelayPipeline();
	}
	meLibHalt();
}

// Reverb knob changes: cross the value, let the ME apply it (above).
// Cross only on an ACTUAL change: SetSendFxParams pushes these every
// tick, and an unconditional uncached store + sync every tick drains the
// write buffer on the audio thread and contends the ME bus for nothing.
extern "C" void PSPME_ReverbSize(int s) {
	static int last = -1 ;
	if (s == last) return ;
	last = s ; ME_SIZE = (unsigned int)s ; meLibSync() ;
}
extern "C" void PSPME_ReverbDamp(int d) {
	static int last = -1 ;
	if (d == last) return ;
	last = d ; ME_DAMP = (unsigned int)d ; meLibSync() ;
}
extern "C" void PSPME_Freeze(int f) {
	static int last = -1 ;
	if (f == last) return ;
	last = f ; ME_FREEZE = (unsigned int)f ; meLibSync() ;
}
extern "C" void PSPME_Drive(int d) {
	static int last = -1 ;
	if (d == last) return ;
	last = d ; ME_DRIVE = (unsigned int)d ; meLibSync() ;
}

// Give the ME the delay lines (once, after SendFx allocates them). Passed
// the cached pointers; published to RAM and stored as uncached aliases.
extern "C" void PSPME_SetDelayLines(short *dlyL, short *dlyR, int maxLen) {
	if (dlyL) sceKernelDcacheWritebackRange(dlyL, maxLen * (int)sizeof(short));
	if (dlyR) sceKernelDcacheWritebackRange(dlyR, maxLen * (int)sizeof(short));
	ME_DLYMAX = (unsigned int)maxLen;
	ME_DLYL   = dlyL ? ((unsigned int)dlyL | 0x40000000u) : 0;
	ME_DLYR   = dlyR ? ((unsigned int)dlyR | 0x40000000u) : 0;
	meLibSync();
}

// Hand this block's send accumulators to the ME and collect the PREVIOUS
// job's finished wet. Returns the number of wet frames written to `wet`
// (0 if the ME is still busy or nothing is ready yet). Never repeats a
// block: a not-ready call simply produces nothing and the caller's wet
// ring silence-extends its lead -- a fade edge, not a mid-signal seam.
/* The old PSPME_Bank collected the previous job into a caller buffer
   and posted the next one in a single call -- which forced an extra
   2KB copy per block on the caller's side (meOut -> staging -> wet
   ring). Split in two so the caller can append the finished wet
   straight from the invalidated cached view into its ring, with the
   collect still strictly BEFORE the post: once the next job is posted
   the ME starts overwriting meOut, so the order is the correctness. */

// Returns -1 while the ME is still busy (nothing collected -- do not
// post), else the frame count of the finished job (0 on the first
// call). *out points at the cached view of the wet, already
// invalidated, valid until the next PSPME_Post.
extern "C" int PSPME_Collect(fixed **out) {
	meCalls_++;
	if (ME_BUSY) return -1;                 // last job still running
	int produced = mePrevN_;
	if (produced > 0) {
		fixed *out_c = (fixed *)((unsigned int)meOut & ~0x40000000u);
		sceKernelDcacheInvalidateRange(out_c, produced * 2 * (int)sizeof(fixed));
		*out = out_c;
	}
	return produced;
}

extern "C" void PSPME_Post(const fixed *dlyAcc, const fixed *revAcc,
                           int n, int fb, int dlyLen, int runDly) {
	if (n > ME_MAXN) n = ME_MAXN;
	int bytes = n * 2 * (int)sizeof(fixed);
	fixed *dly_c = (fixed *)((unsigned int)meDlyIn & ~0x40000000u);
	fixed *rev_c = (fixed *)((unsigned int)meRevIn & ~0x40000000u);
	for (int k = 0; k < n * 2; k++) { dly_c[k] = dlyAcc[k]; rev_c[k] = revAcc[k]; }
	sceKernelDcacheWritebackRange(dly_c, bytes);
	sceKernelDcacheWritebackRange(rev_c, bytes);
	ME_FB = (unsigned int)fb; ME_DLYLEN = (unsigned int)dlyLen;
	ME_RUNDLY = (unsigned int)runDly; ME_N = (unsigned int)n;
	mePrevN_ = n;
	meLibSync();
	ME_BUSY = 1;
	meLibSync();
}
extern "C" int PSPME_Init(void) {
	ME_EXIT_ = 0; ME_BUSY = 0; ME_N = 0; ME_READY = 0; ME_HB = 0; ME_JOBS = 0;
	ME_DLYL = 0; ME_DLYR = 0; ME_DLYMAX = 0; ME_SIZE = 160; ME_DAMP = 110;

	meLibAllocUncached32(&meDlyInH_, ME_MAXN * 2);
	meLibAllocUncached32(&meRevInH_, ME_MAXN * 2);
	meLibAllocUncached32(&meOutH_,   ME_MAXN * 2);
	for (int k = 0; k < ME_MAXN * 2; k++) { ((fixed *)meOut)[k] = 0; }

	// reverb lines: allocate + zero on this core, then alias uncached so
	// the ME (no data cache into main RAM) can reach them.
	meRev_.Init();
	sceKernelDcacheWritebackInvalidateAll();
	meRev_.RemapLines(0x40000000u);
	sceKernelDcacheWritebackInvalidateAll();

#if ME_FM_PROBE
	// build the uncached FM working set: 1024 sin + 1024 sinD + 256 cut
	// shorts, then 8 voices; ~7KB in one uncached block, cross the base.
	static Uncached32 fmH_ = { 0, 0 };
	static volatile u32 *fmBuf = 0;
	fmH_.mem = &fmBuf;
	meLibAllocUncached32(&fmH_, 2048);              // 8KB
	{
		short *sinL = (short*)fmBuf;
		short *sinD = (short*)((unsigned int)fmBuf + 2048);
		short *cutL = (short*)((unsigned int)fmBuf + 4096);
		int   *vx   = (int*)  ((unsigned int)fmBuf + 4608);
		for (int i=0;i<1024;i++) sinL[i] = (short)(32000.0*__builtin_sin(i*2.0*3.14159265/1024.0));
		for (int i=0;i<1024;i++) sinD[i] = (short)(sinL[(i+1)&1023]-sinL[i]);
		for (int i=0;i<256;i++)  cutL[i] = (short)(200 + i*40);
		for (int vi=0; vi<8; vi++) {
			int *v = vx + vi*16;
			for (int op=0; op<4; op++) { v[op]=0; v[4+op]=0x00200000u + op*0x40000 + vi*0x1000; v[8+op]=(op==0)?255:96; }
			v[12]=0; v[13]=0; v[14]=(vi*13)&0xFF; v[15]=200;   // lo,bd,cut,vol
		}
	}
	ME_FMBUF = (unsigned int)fmBuf;   // already uncached
	ME_FMCYC = 0;
#endif
	int tableId = meLibDefaultInit();
	if (tableId == ME_CORE_T2_IMG_TABLE || tableId == ME_CORE_IMG_TABLE) {
		scePowerLock(0);   // suspend with the core live reboots inside the app
	}
	return tableId;
}

extern "C" void PSPME_Shutdown(void) {
	ME_EXIT_ = 1;
	int retry = 0;
	while (ME_READY && ME_BUSY && ++retry <= 5) sceKernelDelayThread(100000);
	if (meDlyIn) meLibAllocUncached32(&meDlyInH_, 0);
	if (meRevIn) meLibAllocUncached32(&meRevInH_, 0);
	if (meOut)   meLibAllocUncached32(&meOutH_,   0);
}

extern "C" unsigned int PSPME_Ready(void)     { return (unsigned int)ME_READY; }
extern "C" unsigned int PSPME_Heartbeat(void) { return (unsigned int)ME_HB; }
extern "C" unsigned int PSPME_Jobs(void)      { return (unsigned int)ME_JOBS; }
extern "C" unsigned int PSPME_Calls(void)     { return meCalls_; }
#if ME_FM_PROBE
extern "C" unsigned int PSPME_FmCycles(void) { return (unsigned int)ME_FMCYC; }
#endif

#endif /* PSP_ME_OFFLOAD */
