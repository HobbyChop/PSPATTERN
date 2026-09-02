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

/* me-core.h is NOT included: it defines meLibHandler and
   meLibOnPreProcess, and this file needs its own copies -- verbatim
   from the library (header verified identical to the toolchain's),
   with ONE insertion in the handler. See the CACHES ARE NOISE comment
   at the handler. Everything else comes through me-core-custom.h
   exactly as before. */
#include <me-core-mapper/me-core-custom.h>

__attribute__((noinline, aligned(4)))
static void meLibOnPreProcess();

/* CACHES ARE NOISE AFTER A STANDBY.

   This is the ME's reset vector code, copied to 0xbfc00000 and entered
   uncached, byte-for-byte mcidclan's handler except for the block
   marked INSERTED. The insertion is why the copy exists:

   Standby cuts power to the ME complex, and that includes the tag
   SRAM of its caches. The core then reboots with cache tags that are
   NOISE -- random lines marked valid and dirty, claiming random
   addresses. The first index-writeback the old loop ran (its opening
   meLibDcacheWritebackInvalidateAll, op 0x14, a WRITEBACK-invalidate)
   faithfully wrote every one of those noise lines to the address its
   noise tag named: a spray of 64-byte garbage writes across the
   address space, a fraction of which land in real RAM. That is the
   post-standby corruption in its entirety -- wandering, because the
   tags are different noise every time; absent at cold boot, because
   Sony's firmware leaves the tags coherent; absent with the core
   parked, because nothing runs at all. Every earlier fix in this
   saga (handler re-upload, witness check, job drain, VME hold) was
   real, and none of them touched this, because the sprayer runs
   before our first line of loop code.

   So the FIRST instructions the core executes now initialise the
   tags: TagLo zeroed, then an Index Store Tag walk over both caches
   -- invalidate everything, write back nothing. After a reset there
   is nothing in these caches worth writing back, by definition. The
   walk runs before the clock/unlock calls so even their stack
   traffic starts from an empty cache. Ops: 0x09 is Index Store Tag D
   (the D-side twin of the 0x08 the library itself uses); 16KB covers
   both caches at any plausible size, since index ops wrap. If some
   model rejects 0x09 the core takes an exception and never signs in
   -- and the resume's proof-of-life then parks it: the failure mode
   is the machine we already shipped, never corruption. */
__attribute__((section("_me_section"), used, noinline, aligned(4)))
static void meLibHandlerPatched() {
  asm volatile(
    ".set push\n"
    ".set noreorder\n"
    /* --- cache init: tags to zero, write back nothing --- */
    "mtc0    $0, $28\n"
    "sync\n"
    "move    $k0, $0\n"
    "1:\n"
    "cache   0x09, 0($k0)\n"
    "cache   0x08, 0($k0)\n"
    "addiu   $k0, $k0, 64\n"
    "sltiu   $k1, $k0, 16384\n"
    "bnez    $k1, 1b\n"
    "nop\n"
    "sync\n"
    ".set pop\n"
    ::: "$k0", "$k1", "memory"
  );

  HW_SYS_BUS_CLOCK_ENABLE      = 0x0f;
  HW_SYS_TACHYON_CONFIG_STATUS |= 0x02;
  HW_SYS_NMI_FLAGS             = 0xffffffff;
  meLibSync();

  meLibUnlockHwUserRegisters();
  meLibUnlockMemory();

  /* The AWEdram clock call that lived here is withdrawn. It was
     added on a theory that was later disproven, and it is the one
     handler ingredient the 0.16-era wake boots -- weeks of them --
     never had. At cold boot it is redundant (Sony's init already
     ran); at SYSEVENT-time wake boots it is the prime suspect for
     the core hanging before sign-in: at phase 0x10005 Sony's own
     resume has not yet restored the downstream clock chain, and a
     resident-kernel clock call against that half-woken fabric can
     spin the booting core forever. The verifier caught exactly that
     -- 'core did not verify, parked scalar' -- with cold boots
     passing. */

  meLibSetMinimalVmeConfig();

  asm volatile(
    ".set noreorder                   \n"
    "li             $k0, 0x30000000   \n"
    "mtc0           $k0, $12          \n"
    "sync                             \n"
    "li             $k0, 0x279c637c   \n"
    "lw             $k1, 0x88300018   \n"
    "beq            $k0, $k1, 1f      \n"
    "nop                              \n"
    "li             $sp, 0x80200000   \n"
    "b              2f                \n"
    "nop                              \n"
    "1:                               \n"
    "li             $sp, 0x80400000   \n"
    "2:                               \n"
    "la             $k0, %0           \n"
    "li             $k1, 0x80000000   \n"
    "or             $k0, $k0, $k1     \n"
    "cache          0x8, 0($k0)       \n"
    "sync                             \n"
    "jr             $k0               \n"
    "nop                              \n"
    ".set reorder                     \n"
    :
    : "i" (meLibOnPreProcess)
    : "k0", "k1", "memory"
  );
}

/* The library's meLibOnPreProcess, verbatim. */
__attribute__((noinline, aligned(4)))
static void meLibOnPreProcess() {
  meLibDcacheInvalidateRange(ME_CORE_BASE_ADDR, (0x90000 + 63) & ~63);
  meLibIcacheInvalidateRange(ME_CORE_BASE_ADDR, (0x90000 + 63) & ~63);

  hw(0xbc200000) = 511 << 16 | 511;
  hw(0xBC200004) = 511 << 16 | 511;
  hw(0xBC200008) = 511 << 16 | 511;
  meLibSync();

  meLibOnProcess();
}
#include <psppower.h>
#include <pspkernel.h>
#include <math.h>

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
#define ME_DUCK   (meLibSharedMemory[18])  // input ducks the tail (0..255)
#define ME_GATE   (meLibSharedMemory[19])  // gate threshold (0..255)
#define ME_COMP   (meLibSharedMemory[20])  // wet glue compressor (0..255)
#define ME_LOCUT  (meLibSharedMemory[21])  // reverb input HP (0..255)
#define ME_WIDTH  (meLibSharedMemory[22])  // tail extra width (0..255)
#define ME_DTONE  (meLibSharedMemory[23])  // delay feedback LP (0..255)
#define ME_SPECREQ  (meLibSharedMemory[24]) // main: FFT request sequence
#define ME_SPECDONE (meLibSharedMemory[25]) // ME: last sequence finished
// The canary protocol: main asks the ME to write a known pattern
// through its CACHED view of a main-RAM buffer and write it back.
// Single uncached words provably arrive (the heartbeat is one); the
// question is whether burst WRITEBACKS arrive intact after standby.
#define ME_TESTCMD  (meLibSharedMemory[26]) // 1 = run the cached-write test
#define ME_TESTPTR  (meLibSharedMemory[27]) // buffer, plain cached address
#define ME_TESTRES  (meLibSharedMemory[28]) // ME: test finished marker

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
static void meSpectrum(void);   // idle-lane FFT, defined below

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
	// wet-tail behaviour state (same integer math as the scalar
	// mirror in SendFx.cpp -- keep them IDENTICAL, a suspend swaps
	// paths mid-song). Wiped by standby with the rest of the core;
	// all of it re-settles within a block.
	int fxEnv = 0, fxGateG = 256, fxCEnv = 0;
	unsigned int specDone = ME_SPECDONE;   // survives wake via the word
	fixed lcLpL = 0, lcLpR = 0;
	int dfL = 0, dfR = 0;

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
			int duck = (int)ME_DUCK, gate = (int)ME_GATE;
			int comp = (int)ME_COMP, locut = (int)ME_LOCUT;
			int width = (int)ME_WIDTH, dtone = (int)ME_DTONE;

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
					// tone 0 = bit-exact pass-through (coeff 256)
					{
						int a = 256 - dtone;
						dfL += ((fbL - dfL) * a) >> 8;
						dfR += ((fbR - dfR) * a) >> 8;
					}
					dlyL[dlyPos] = (short)dfL; dlyR[dlyPos] = (short)dfR;
					if (++dlyPos >= dlyLen) dlyPos = 0;
				}
				fixed rInL = meClampfx((long long)revIn[k * 2]     + (dOutL >> 2));
				fixed rInR = meClampfx((long long)revIn[k * 2 + 1] + (dOutR >> 2));
				if (locut) {
					int c = 1 + (locut >> 4);
					lcLpL += (fixed)(((long long)(rInL - lcLpL) * c) >> 8);
					lcLpR += (fixed)(((long long)(rInR - lcLpR) * c) >> 8);
					rInL = meClampfx((long long)rInL - lcLpL);
					rInR = meClampfx((long long)rInR - lcLpR);
				}
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
				fixed rvL = revHold0, rvR = revHold1;
				{
					int kl = (int)revIn[k * 2] ; if (kl < 0) kl = -kl;
					int kr = (int)revIn[k * 2 + 1] ; if (kr < 0) kr = -kr;
					int key = ((kl >> 15) + (kr >> 15)) >> 1;
					if (key > fxEnv) fxEnv += (key - fxEnv) >> 2;
					else fxEnv -= (fxEnv - key) >> 9;
					int wetG = 256;
					if (duck) {
						int dg = (fxEnv * duck) >> 14;
						if (dg > 224) dg = 224;
						wetG = 256 - dg;
					}
					if (gate) {
						int open = (fxEnv > (gate << 5)) ? 256 : 0;
						fxGateG += (open - fxGateG) >> 3;
						wetG = (wetG * fxGateG) >> 8;
					}
					if (wetG != 256) {
						rvL = (fixed)(((long long)rvL * wetG) >> 8);
						rvR = (fixed)(((long long)rvR * wetG) >> 8);
					}
					if (width) {
						fixed m = (rvL >> 1) + (rvR >> 1);
						fixed sd = (fixed)(((long long)((rvL >> 1) - (rvR >> 1))
						                    * (256 + width)) >> 8);
						rvL = meClampfx((long long)m + sd);
						rvR = meClampfx((long long)m - sd);
					}
				}
				fixed wl = meClampfx((long long)dOutL + rvL);
				fixed wr = meClampfx((long long)dOutR + rvR);
				if (comp) {
					int ml = (int)(wl < 0 ? -wl : wl) >> 15;
					int mr = (int)(wr < 0 ? -wr : wr) >> 15;
					if (mr > ml) ml = mr;
					if (ml > fxCEnv) fxCEnv += (ml - fxCEnv) >> 3;
					else fxCEnv -= (fxCEnv - ml) >> 8;
					int th = 24576 - comp * 64;
					int g = 256;
					if (fxCEnv > th) {
						int red = ((fxCEnv - th) * comp) >> 16;
						if (red > 192) red = 192;
						g = 256 - red;
					}
					int tg = (g * (256 + (comp >> 1))) >> 8;
					wl = meClampfx(((long long)wl * tg) >> 8);
					wr = meClampfx(((long long)wr * tg) >> 8);
				}
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
		} else {
			// idle lane: the spectrum FFT runs ONLY when no job is
			// posted, so send audio never waits on eye candy
			unsigned int rq = ME_SPECREQ;
			if (rq != specDone) {
				meSpectrum();
				specDone = rq;
				ME_SPECDONE = rq;
			}
			/* THE QUIET IDLE. This loop's polling was millions of
			   uncached DDR touches a second -- harmless on a
			   cold-booted bus, but after standby the arbitration
			   runs subtly degraded (only Sony's full boot repairs
			   it, and re-running that init is device-proven fatal),
			   and an idle core shouting on a damaged bus during the
			   close's traffic burst was the last freeze standing.
			   The burst below cuts the idle loop's bus presence
			   ~256-fold; a posted job is still picked up within
			   tens of microseconds. */
			for (int d = 0; d < 256; d++) meLibDelayPipeline();
			if (ME_TESTCMD == 1) {
				// the canary: pattern through the CACHED view, then
				// a burst writeback -- the exact path under suspicion
				unsigned char *pc =
				    (unsigned char *)(ME_TESTPTR | 0x80000000u);
				for (int ci = 0; ci < 4096; ci++)
					pc[ci] = (unsigned char)(ci * 13 + 7);
				meLibDcacheWritebackInvalidateRange(
				    ((u32)pc) & ~63u, 4096 + 64);
				ME_TESTCMD = 0;
				ME_TESTRES = 1;
				meLibSync();
			}
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

static unsigned int meLastPostUs_ = 0;  // job-idle detector for the meter

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
	meLastPostUs_ = sceKernelGetSystemTimeLow();
	ME_BUSY = 1;
	meLibSync();
}
static volatile int meAlive_ = 0;   // Init ran; shared words are real
static volatile int meEverStarted_ = 0;   // ever booted this run (revive gate)

/* The info panel's ME figure, measured WITHOUT an ME-side clock --
   the ME has none worth the name: CP0 Count does not tick on the
   Allegrex, and the 1MHz system-time register never moved when read
   from that side either (both were tried; both froze the meter at 0).

   What provably does move is ME_HB, the heartbeat the idle loop
   bumps once per spin. The spin body is a fixed instruction sequence,
   so its full-idle rate is a constant of the machine; while a job
   renders, the heartbeat stands still. Load is therefore the DROP in
   spin rate: calibrate the idle rate once at init with the main
   core's real microsecond clock, then

       load% = 100 * (1 - observed spins/sec / idle spins/sec).

   -1 = no meter (offload off, or the core is down across a suspend)
   and the UI prints "--" rather than a stale number. */
static unsigned int meIdleHz_ = 0;      // calibrated idle spins/sec
static unsigned int meLoadHB_ = 0;      // last sample: heartbeat
static unsigned int meLoadT_ = 0;       // last sample: microseconds
static int meLoadPct_ = 0;              // cached between samples

extern "C" int PSPME_LoadPercent(void) {
	if (!meAlive_ || !ME_READY || !meIdleHz_) return -1;
	unsigned int now = sceKernelGetSystemTimeLow();
	unsigned int dt = now - meLoadT_;
	if (dt < 250000) return meLoadPct_;   // fresh enough
	unsigned int hb = ME_HB;
	unsigned int dhb = hb - meLoadHB_;
	meLoadHB_ = hb;
	meLoadT_ = now;
	unsigned long long rate = (unsigned long long)dhb * 1000000ull / dt;
	/* The boot-time calibration ran on a QUIET bus (no display DMA, no
	   render traffic); runtime contention slows the idle spin even when
	   the core does nothing, which read as a permanent ~15%% of phantom
	   load after every stop. The main core KNOWS when the ME is idle --
	   nothing has been posted for half a second -- so in that state the
	   honest figure is zero by definition, and the observed spin rate
	   IS the true contended-idle baseline: adopt it. The playing figure
	   then measures against real-world idle, not a boot-time ideal. */
	if (now - meLastPostUs_ > 500000) {
		meIdleHz_ = (unsigned int)rate;
		meLoadPct_ = 0;
		return 0;
	}
	int pct = 100 - (int)(rate * 100 / meIdleHz_);
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	meLoadPct_ = pct;
	return pct;
}

/* While the core is down the cache must remember NOTHING: recording
   last=v while refusing the write used to mark a knob 'applied' that
   never crossed, and once the wake revive brought the core back the
   skip made the staleness permanent. Forgetting instead means the
   first set after a revive always writes. */
#define PSPME_KNOB(FN, WORD) extern "C" void FN(int v) { 	static int last = -1; 	if (!meAlive_) { last = -1; return; } 	if (v == last) return; 	last = v; 	WORD = (unsigned int)v; 	meLibSync(); }
PSPME_KNOB(PSPME_Duck,  ME_DUCK)
PSPME_KNOB(PSPME_Gate,  ME_GATE)
PSPME_KNOB(PSPME_Comp,  ME_COMP)
PSPME_KNOB(PSPME_Locut, ME_LOCUT)
PSPME_KNOB(PSPME_Width, ME_WIDTH)
PSPME_KNOB(PSPME_Dtone, ME_DTONE)
#undef PSPME_KNOB

/* ---- spectrum analyzer: idle-lane FFT for the UI ------------------
   Main taps the FINISHED master (AudioStats::EndBlock), downmixes 256
   frames to mono into an uncached buffer and bumps ME_SPECREQ. The ME
   runs a 256-point real FFT ONLY on idle spins (else-if after the job
   check, so send audio is never delayed), collapses 127 bins into 32
   log-spaced bars and writes them uncached for the song screen.

   Tables are computed by the MAIN core here in Init, BEFORE the ME
   boots: the ME's boot-time dcache invalidate makes them visible, the
   same one-time handoff the reverb instance already rides. The work
   arrays are ME-private and stay in its cache. Display only: a wiped
   core (suspend) just leaves the bars at zero until wake. */
#define SPEC_N 256
static short *meSpecIn = 0;        // 256 mono samples, uncached
static int *meSpecOut = 0;         // 32 bar levels 0..255, uncached
static volatile u32 *meSpecInW_ = 0, *meSpecOutW_ = 0;
static Uncached32 meSpecInH_ = { &meSpecInW_, 0 };
static Uncached32 meSpecOutH_ = { &meSpecOutW_, 0 };
static float specWin_[SPEC_N];
static float specTwR_[SPEC_N / 2], specTwI_[SPEC_N / 2];
static unsigned char specRev_[SPEC_N];
static unsigned char specGLo_[32], specGHi_[32];
static float specRe_[SPEC_N], specIm_[SPEC_N];   // ME-private work

static void meSpectrum(void) {
	short *in = meSpecIn;
	if (!in || !meSpecOut) return;
	for (int i = 0; i < SPEC_N; i++) {
		int j = specRev_[i];
		specRe_[i] = (float)in[j] * specWin_[j];
		specIm_[i] = 0.0f;
	}
	for (int len = 2; len <= SPEC_N; len <<= 1) {
		int half = len >> 1, step = SPEC_N / len;
		for (int i = 0; i < SPEC_N; i += len) {
			for (int k = 0; k < half; k++) {
				float twr = specTwR_[k * step], twi = specTwI_[k * step];
				float ar = specRe_[i + k + half], ai = specIm_[i + k + half];
				float xr = ar * twr - ai * twi;
				float xi = ar * twi + ai * twr;
				specRe_[i + k + half] = specRe_[i + k] - xr;
				specIm_[i + k + half] = specIm_[i + k] - xi;
				specRe_[i + k] += xr;
				specIm_[i + k] += xi;
			}
		}
	}
	for (int b = 0; b < 32; b++) {
		float m = 0.0f;
		for (int k = specGLo_[b]; k <= specGHi_[b]; k++) {
			float p = specRe_[k] * specRe_[k] + specIm_[k] * specIm_[k];
			if (p > m) m = p;
		}
		// log scale: quiet floor around 2^18 power, ~26 doublings of
		// power to full scale, mapped onto 0..255
		float l = logf(m + 1.0f) * 1.4426950f;   // log2
		int v = (int)((l - 18.0f) * 10.0f);
		if (v < 0) v = 0;
		if (v > 255) v = 255;
		meSpecOut[b] = v;
	}
}

/* The transport gates the feed: a stopped song fed the ME thirty
   silent FFTs a second, which the meter dutifully reported as ~2-6%%
   of phantom load while the panel showed nothing at all. */
static volatile int meSpecOn_ = 0;
extern "C" void PSPME_SpectrumEnable(int on) { meSpecOn_ = on ? 1 : 0; }

/* main side: feed the finished master, at most ~30 times a second */
extern "C" void PSPME_SpectrumFeed(short *interleaved, int frames) {
	if (!meSpecOn_) return;
	if (!meAlive_ || !ME_READY || !meSpecIn) return;
	if (frames < SPEC_N) return;
	static unsigned int lastFeed = 0;
	unsigned int now = sceKernelGetSystemTimeLow();
	if (now - lastFeed < 33000) return;
	lastFeed = now;
	short *tail = interleaved + (frames - SPEC_N) * 2;
	for (int i = 0; i < SPEC_N; i++) {
		meSpecIn[i] = (short)(((int)tail[i * 2] + (int)tail[i * 2 + 1]) >> 1);
	}
	ME_SPECREQ = ME_SPECREQ + 1;
	meLibSync();
}

/* main side: latest bar set for the overlay. 0 = nothing to draw. */
extern "C" int PSPME_ReadSpectrum(int *bars32) {
	if (!meAlive_ || !ME_READY || !meSpecOut) return 0;
	if (ME_SPECDONE == 0) return 0;
	for (int i = 0; i < 32; i++) bars32[i] = meSpecOut[i];
	return 1;
}

static int meCanaryRun(const char *tag);

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
	/* spectrum tables, filled by the MAIN core while the ME is still
	   down -- its boot-time invalidate is the handoff */
	for (int i = 0; i < SPEC_N; i++) {
		specWin_[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265f * i / SPEC_N);
		unsigned int r = 0, v = (unsigned int)i;
		for (int b = 0; b < 8; b++) { r = (r << 1) | (v & 1); v >>= 1; }
		specRev_[i] = (unsigned char)r;
	}
	for (int k = 0; k < SPEC_N / 2; k++) {
		float a = -2.0f * 3.14159265f * k / SPEC_N;
		specTwR_[k] = cosf(a);
		specTwI_[k] = sinf(a);
	}
	for (int b = 0; b < 32; b++) {
		int lo = (int)powf(127.0f, b / 32.0f);
		int hi = (int)powf(127.0f, (b + 1) / 32.0f);
		if (lo < 1) lo = 1;
		if (hi < lo) hi = lo;
		if (hi > 127) hi = 127;
		specGLo_[b] = (unsigned char)lo;
		specGHi_[b] = (unsigned char)hi;
	}
	meLibAllocUncached32(&meSpecInH_, SPEC_N / 2);   // 256 shorts
	meLibAllocUncached32(&meSpecOutH_, 32);          // 32 words
	meSpecIn = (short *)meSpecInW_;
	meSpecOut = (int *)meSpecOutW_;
	if (meSpecOut) for (int i = 0; i < 32; i++) meSpecOut[i] = 0;

	int tableId = meLibDefaultInit();
	if (tableId >= 0) { meAlive_ = 1; meEverStarted_ = 1; }
	/* calibrate the load meter: wait for the loop, then time the idle
	   spin rate against the main core's clock. Done here, before the
	   audio pump posts its first job, so the sample really is idle. */
	if (meAlive_) {
		int guard = 0;
		while (!ME_READY && guard++ < 1000000) {}
		if (ME_READY) meCanaryRun("boot");
		if (ME_READY) {
			unsigned int t0 = sceKernelGetSystemTimeLow();
			unsigned int h0 = ME_HB;
			sceKernelDelayThread(50000);
			unsigned int dt = sceKernelGetSystemTimeLow() - t0;
			unsigned int dh = ME_HB - h0;
			if (dt > 10000 && dh)
				meIdleHz_ = (unsigned int)((unsigned long long)dh *
				                           1000000ull / dt);
			meLoadHB_ = ME_HB;
			meLoadT_ = sceKernelGetSystemTimeLow();
		}
	}
	/* The power lock that used to live here is gone: the sleep/wake
	   handlers above quiesce the core across a suspend (hold reset,
	   ME_READY down, reboot from the resident image at wake), so the
	   switch is allowed to work. If hardware ever proves the wake path
	   wrong, the failure is silent sends on the scalar fallback -- the
	   old un-quiesced reboot cannot recur while meLibOnSleep links
	   strong. */
	return tableId;
}

/* STANDBY. me-lib hijacks Sony's ME suspend handler at kinit, so
   without strong handlers here the ME went down un-quiesced -- the
   crash-and-reboot that led to the power lock in PSPME_Init. These are
   the SIMPLE style: hold the core in reset while the machine sleeps,
   pulse the reset at wake so it reboots clean from its RESIDENT image
   (everything it needs -- the shared words, the uncached buffers, the
   delay-line aliases -- survives in main RAM). The DEFAULT register-
   save style is NOT safe here: the ME stack lives in eDRAM, which
   standby wipes.

   Sleep also drops ME_READY, and that is the safety keystone: if
   anything about the wake goes wrong, every render just takes the
   scalar fallback -- silent sends, never a hang. */
static volatile unsigned int meSleepCount_ = 0;
static volatile unsigned int meWakeCount_ = 0;

extern "C" __attribute__((noinline, aligned(4)))
void meLibOnSleep(void) {
	ME_READY = 0;              // renders fall back to the scalar path
	ME_BUSY = 0;               // nothing is in flight on a parked core
	meSleepCount_++;
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	meLibSync();
}

/* THE WAKE HOLDS; THE RESUME REVIVES.

   This used to pulse the reset so the ME rebooted from "the resident
   image" -- but the handler it boots through lives at ME_HANDLER_BASE
   = 0xbfc00000, ME-local memory, and standby WIPES the ME's local
   memory. A core released onto a wiped boot area runs garbage with
   write access to everything, and its damage surfaces wherever the
   program touches next: the wandering freeze after standby (project
   teardown one run, leaving the picker the next).

   So this handler -- kernel sysevent context, mid-wake, the worst
   possible place to do real work -- only parks the core and marks it
   dead. The actual revival happens later, on the main thread, in
   PSPME_OnResume: re-copy the handler, verify the resident kernel
   image, and only then release the reset. If any of that fails the
   core simply stays parked and every render takes the bit-identical
   scalar path. */
extern "C" __attribute__((noinline, aligned(4)))
void meLibOnWake(void) {
	/* THE REUNION. The 0.16-era wake did exactly this -- pulse the
	   reset in the sysevent handler and let the core reboot right
	   here -- and it carried weeks of standby use. Its one real
	   defect was the cache-tag noise spray at the rebooted core's
	   first writeback, and THAT is fixed inside the handler this
	   pulse boots (the Index-Store-Tag walk). This context is also
	   the one place the transition is interrupt-safe by
	   construction: sysevent handlers run with CPU interrupts
	   masked -- which the app-side revive kcalls spent three builds
	   rediscovering by hand. The deferred/inline revive machinery is
	   retired; the resume path merely verifies what already booted
	   here. */
	meWakeCount_++;
	ME_READY = 0;
	ME_BUSY = 0;
	HW_SYS_RESET_ENABLE = SC_HW_RESET;   // pulse: both held...
	HW_SYS_RESET_ENABLE = 0;             // ...both released, 0.16 form
	meLibSync();
}

/* The canary buffer: 4KB the ME writes through its cache, with a
   guard line either side that must come back untouched. Static, so
   its address is identical every run and every diag line about it
   is comparable across sessions. */
static unsigned char __attribute__((aligned(64))) meCanaryBuf_[4096 + 128];

extern "C" void pspDiag(const char *fmt, ...);

/* Returns 1 = the cached-write path is provably intact; 0 = it is
   not (or the core never answered), with everything written to the
   diag file either way. */
static int meCanaryRun(const char *tag) {
	unsigned char *guard0 = meCanaryBuf_;
	unsigned char *body   = meCanaryBuf_ + 64;
	unsigned char *guard1 = meCanaryBuf_ + 64 + 4096;
	for (int i = 0; i < 4096 + 128; i++) meCanaryBuf_[i] = 0xA5;
	sceKernelDcacheWritebackInvalidateRange(meCanaryBuf_, 4096 + 128);
	ME_TESTRES = 0;
	ME_TESTPTR = (unsigned int)body;
	ME_TESTCMD = 1;
	meLibSync();
	int w = 0;
	while (!ME_TESTRES && w++ < 100) sceKernelDelayThread(1000);
	if (!ME_TESTRES) {
		pspDiag("canary %s: NO ANSWER after %dms", tag, w);
		return 0;
	}
	sceKernelDcacheInvalidateRange(meCanaryBuf_, 4096 + 128);
	int bad = 0, firstOff = -1;
	unsigned int firstVal = 0, firstWant = 0;
	for (int i = 0; i < 4096; i++) {
		unsigned char want = (unsigned char)(i * 13 + 7);
		if (body[i] != want) {
			if (bad++ == 0) { firstOff = i; firstVal = body[i]; firstWant = want; }
		}
	}
	int gbad = 0;
	for (int i = 0; i < 64; i++) {
		if (guard0[i] != 0xA5) gbad++;
		if (guard1[i] != 0xA5) gbad++;
	}
	if (bad || gbad) {
		pspDiag("canary %s: FAIL bad=%d guard=%d first@%d got=%02x want=%02x buf=%08x",
		        tag, bad, gbad, firstOff, firstVal, firstWant,
		        (unsigned int)body);
		return 0;
	}
	pspDiag("canary %s: pass buf=%08x", tag, (unsigned int)body);
	return 1;
}

/* Kernel half of the revive, run through kcall exactly like the boot
   path: identify the resident Sony ME kernel image by its witness
   word (0x88300018, main RAM -- it survives standby, and if resume
   damaged that image the check refuses before anything boots),
   re-select the model's syscall table, then meLibReset -- which
   re-copies our handler over the wiped ME_HANDLER_BASE, writes every
   dirty main-core cache line back so the ME reads real state, and
   pulses the reset. This is byte-for-byte the half of
   meLibDefaultInit a resume needs: no prx load, no second sysevent
   registration, none of the app-side init that must run only once. */
static int meReviveKernel(void) {
	/* INTERRUPTS OFF ACROSS THE WHOLE TRANSITION. Sony brackets every
	   reset-register operation with sceKernelCpuSuspendIntr, and every
	   proven ME transition in this project was interrupt-quiet by
	   accident: the sleep and wake handlers run in sysevent context
	   with CPU interrupts masked, and cold boot's kcall runs in a
	   near-silent machine. This kcall was the one transition running
	   with interrupts LIVE -- vblank, audio DMA, the battery worker's
	   syscon traffic -- and it failed exactly like a lottery: odds
	   improving with settle time, never reaching safe. The library
	   shipped these macros unused, for exactly this. */
	unsigned int iv;
	meLibSuspendCpuIntr(iv);
	const int tableId = meCoreGetTableIdFromWitnessWord();
	if (tableId < 2) {
		meLibResumeCpuIntr(iv);
		return -1;                // resident image no longer identifies
	}
	meCoreSelectSystemTable(tableId);
	/* meLibReset, with one deliberate difference. SC_HW_RESET (0x14)
	   covers TWO processors: the ME (bit 0x4) and the VME (bit 0x10),
	   Sony's codec DSP -- a bus master with its own DMA. The library's
	   release writes 0x00, which frees them BOTH. We re-upload the
	   ME's code before releasing it; nobody re-programs the VME, and
	   standby wipes its state like everything else on that side of
	   the bus. A DMA engine released onto wiped state is the last
	   corruption source left standing: the parked build (both bits
	   held) was clean, the revive (both bits freed) wandered again.

	   This program never uses the VME -- no ATRAC, no video -- so
	   after a standby it simply stays in reset for the rest of the
	   run. The ME alone is released. Bit 0x2 is the main CPU: never
	   written, project rule. */
	{
		const unsigned int me_section_size =
		    (unsigned int)(&__stop__me_section - &__start__me_section);
		memcpy((void *)ME_HANDLER_BASE, (void *)&__start__me_section,
		       me_section_size);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		HW_SYS_RESET_ENABLE = SC_HW_RESET;   // 0x14: both held...
		HW_SYS_RESET_ENABLE = 0x10;          // ...ME runs, VME stays
		meLibSync();
	}
	meLibResumeCpuIntr(iv);
	return tableId;
}

/* Main-thread half of the wake, called from PSPHandleResume while the
   audio is still paused (so nothing posts during any of this). First
   reconcile the handshake a mid-flight suspend can leave wedged, then
   bring the second core back from a fresh copy of its code.

   The old wake pulsed the reset and trusted residency; standby wipes
   the ME-local boot area, so that reboot ran garbage and corrupted
   main memory -- the wandering post-standby freeze. The revive
   re-uploads first, releases second, and then DEMANDS PROOF: the
   loop must sign in (ME_READY) and the heartbeat must climb before
   any render is allowed to post. Anything less and the core is
   parked in reset, renders keep the bit-identical scalar path, and
   the machine is merely slower -- the next resume tries again. */
extern "C" void pspSetAudioPaused(int paused);

static void meReviveAttemptBody(const char *tag);

/* THE QUIET-BUS ENVELOPE. Every revive that ever succeeded on this
   device ran with audio paused: the forensic build's inline revive
   sat before the unpause by construction, and the first deferred
   revive to run against a live render thread died on the song
   screen. So the envelope is now explicit: whoever calls the
   revive, the render pipeline is paused, the in-flight block
   drains, the revive runs on a quiet bus, and audio returns. The
   cost is a ~300ms sound gap at the moment the second core comes
   back -- once per wake, and only then. */
static void meReviveAttempt(const char *tag) {
	/* The GE drain that briefly lived here is gone: at resume the GE
	   is LOST (standby wiped it, re-init comes on the next draw) and
	   sceGeDrawSync on a dead engine is undefined -- the build that
	   called it froze the UI on its first post-resume draw. The
	   envelope is audio-only; the inline slot's whole point is that
	   the GE has not drawn since wake. */
	pspSetAudioPaused(1);
	sceKernelDelayThread(60 * 1000);   // a full audio block drains
	meReviveAttemptBody(tag);
	pspSetAudioPaused(0);
}

static void meReviveAttemptBody(const char *tag) {
	ME_BUSY = 0;
	ME_READY = 0;
	meAlive_ = 0;
	mePrevN_ = 0;
	meLibSync();
	if (!meEverStarted_ || ME_EXIT_) return;   // never ran, or exiting

	if (kcall(meReviveKernel, 0) < 0) return;  // stay scalar, still parked

	// the fresh loop flushes the reverb before signing in; give it
	// time, but never hang a resume on it
	int waited = 0;
	while (!ME_READY && waited++ < 200) sceKernelDelayThread(1000);
	if (ME_READY) {
		unsigned int h0 = ME_HB;
		sceKernelDelayThread(2000);
		if (ME_HB != h0) {
			/* alive and looping -- but looping is single uncached
			   words, and the wounds this saga chased arrive by
			   burst writeback. The canary exercises exactly that
			   path and the core earns its jobs only by passing. */
			if (!meCanaryRun(tag)) {
				pspDiag("%s: canary failed, core parked", tag);
			} else {
				meLoadHB_ = ME_HB;
				meLoadT_ = sceKernelGetSystemTimeLow();
				meAlive_ = 1;
				meLibSync();
				pspDiag("%s: alive, jobs enabled", tag);
				return;
			}
		}
	}
	// signed in late or not at all: park it dead rather than wonder
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	meLibSync();
	ME_READY = 0;
	meLibSync();
}

/* THE SETTLE WINDOW. RESUME_COMPLETE means the power service thinks
   resume is done -- not that every driver's deferred re-init has
   finished. Reviving the second core inside that tail races other
   drivers' clock and bus re-initialisation, which showed up exactly
   the way races do: occasional freezes at resume that became RARE
   when the diagnostic build's two seconds of card writes were
   accidentally acting as a settle delay. So the resume path no
   longer revives inline: it reconciles the handshake, stamps a
   deadline, and the UI tick performs the revive a few seconds
   later, on a machine that has finished waking -- the same late,
   quiet-bus conditions the post-load relaunch already proved. */
static volatile unsigned int meReviveDueAt_ = 0;
static volatile int meRevivePending_ = 0;

static int meParkK(void);

/* The verifier: the loop signed in, the heartbeat climbs, the
   canary passes -- then and only then, jobs. Anything less parks
   scalar, and the next wake pulse gets another chance. Shared by
   the resume path and the post-load relaunch. */
static void meVerifyAfterBoot(const char *tag) {
	ME_BUSY = 0;
	meAlive_ = 0;
	mePrevN_ = 0;
	meLibSync();
	if (!meEverStarted_ || ME_EXIT_) return;
	int waited = 0;
	while (!ME_READY && waited++ < 200) sceKernelDelayThread(1000);
	if (ME_READY) {
		unsigned int h0 = ME_HB;
		sceKernelDelayThread(2000);
		if (ME_HB != h0 && meCanaryRun(tag)) {
			meLoadHB_ = ME_HB;
			meLoadT_ = sceKernelGetSystemTimeLow();
			meAlive_ = 1;
			meLibSync();
			pspDiag("%s: alive, jobs enabled", tag);
			return;
		}
	}
	pspDiag("%s: core did not verify, parked scalar", tag);
	kcall(meParkK, 0);
	ME_READY = 0;
	meLibSync();
}

extern "C" void PSPME_OnResume(void) {
	/* The core already rebooted at wake, in the sysevent handler,
	   interrupts masked -- the 0.16 architecture booting the fixed
	   handler. This path only verifies. */
	meVerifyAfterBoot("wakeboot");
}

/* Called from the UI tick on the main thread; runs the deferred
   revive once its settle window has passed. */
extern "C" void PSPME_TickRevive(void) {
	if (!meRevivePending_) return;
	if ((int)(sceKernelGetSystemTimeLow() - meReviveDueAt_) < 0) return;
	meRevivePending_ = 0;
	meReviveAttempt("revive");
}

/* THE SIDESTEP. Post-standby, the running core plus the close's
   heavy traffic is the one combination that wedges the bus -- and
   every attempt to re-order the wake boot sequence around it died
   on the device. So the wedge's window is removed instead of
   fought: the close PARKS the core (the sleep handler's own
   operation), the teardown and load run in the configuration weeks
   of the parked build proved clean, and the proven revive re-lights
   the core once the new project is up. Cold sessions are untouched
   -- their close has been clean for months. */
/* Kernel half of the close-time park: the sleep handler's exact
   operation, in the sleep handler's execution context (kernel mode,
   via kcall like every proven revive). The first attempt parked from
   USER mode, deep inside the teardown at quiesce time -- and the
   machine died at exactly that stage. Two things distinguish every
   park that has ever worked on this device: kernel context, and a
   quiet bus. This one has both -- it runs before the teardown
   generates any traffic at all. */
/* The wake pulse, SC-initiated: register-identical to the sysevent
   wake that two consecutive standby cycles just verified (hold 0x14,
   release 0x00, resident handler boots with its cache walk),
   interrupts masked like every transition that works. Used by the
   post-load relaunch. */
static int meWakePulseK(void) {
	unsigned int iv;
	meLibSuspendCpuIntr(iv);
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	HW_SYS_RESET_ENABLE = 0;
	meLibSync();
	meLibResumeCpuIntr(iv);
	return 0;
}

static int meParkK(void) {
	unsigned int iv;
	meLibSuspendCpuIntr(iv);
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	meLibSync();
	meLibResumeCpuIntr(iv);
	return 0;
}

extern "C" void PSPME_ParkForClose(void) {
	if (!meEverStarted_ || meSleepCount_ == 0) return;
	if (!meAlive_ && !ME_READY) return;   // already dark
	ME_READY = 0;    // renders go scalar; no new jobs post
	meLibSync();
	int w = 0;
	while (ME_BUSY && w++ < 50) sceKernelDelayThread(1000);
	kcall(meParkK, 0);
	ME_BUSY = 0;
	meAlive_ = 0;
	meLibSync();
	pspDiag("park: core held for close (busy drained in %dms)", w);
}

extern "C" void PSPME_Relaunch(void) {
	if (meSleepCount_ == 0) return;   // cold session: nothing to do
	meRevivePending_ = 0;
	pspDiag("relaunch: wake pulse after load");
	pspSetAudioPaused(1);
	sceKernelDelayThread(60 * 1000);
	ME_READY = 0;
	meLibSync();
	kcall(meWakePulseK, 0);
	meVerifyAfterBoot("relaunch");
	pspSetAudioPaused(0);
}

extern "C" unsigned int PSPME_SleepCount(void) { return meSleepCount_; }
extern "C" unsigned int PSPME_WakeCount(void)  { return meWakeCount_; }

extern "C" void PSPME_Shutdown(void) {
	/* ORDER MATTERS -- the audio thread is still rendering while this
	   runs (SDL only stops at atexit). The old sequence freed the
	   shared buffers FIRST and dropped ME_READY last, so a render
	   mid-flight could post into freed memory: the exit-through-the-
	   menu crash. Now: ready down (renders take the scalar path from
	   the next block), core out, RESET HELD, a grace delay for any
	   render already past the ready check -- and only then the frees,
	   with both cores provably unable to touch them. */
	meAlive_ = 0;
	ME_READY = 0;
	meLibSync();
	ME_EXIT_ = 1;
	meLibSync();
	int retry = 0;
	while (ME_BUSY && ++retry <= 5) sceKernelDelayThread(100000);
	// held in reset: a halted-but-unreset ME across a suspend is the
	// guaranteed hang (project rule); held in reset it is inert
	// whatever the power switch does during shutdown
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	meLibSync();
	sceKernelDelayThread(30 * 1000);
	if (meDlyIn) meLibAllocUncached32(&meDlyInH_, 0);
	if (meRevIn) meLibAllocUncached32(&meRevInH_, 0);
	if (meOut)   meLibAllocUncached32(&meOutH_,   0);
}

extern "C" unsigned int PSPME_Ready(void)     { return (unsigned int)ME_READY; }
extern "C" unsigned int PSPME_Busy(void)      { return (unsigned int)ME_BUSY; }

/* Close-time drain. The delay lines the ME writes belong to SendFx,
   and SendFx::Close is about to hand them back to the heap: a job
   still in flight would then be writing freed memory -- and what it
   lands on is the allocator's own bookkeeping, which turns the NEXT
   malloc into an infinite walk of a corrupted freelist. That is a
   freeze with no crash and no place to look.

   The caller has already stopped the audio driver, so no new job can
   arrive; this waits out the one that may be running. A healthy job
   is over in a couple of milliseconds; fifty is generosity. A core
   still busy after that is not running our loop any more, and gets
   parked in reset -- renders take the scalar path, and the next
   resume revives it fresh. */
extern "C" void PSPME_Quiesce(void) {
	if (!meEverStarted_) return;
	int w = 0;
	while (ME_BUSY && w++ < 50) sceKernelDelayThread(1000);
	if (ME_BUSY) {
		/* a job that will not drain means the core is not running
		   our loop; park it -- through the masked kernel call, never
		   a user-mode register write (the stage-4 lesson) */
		kcall(meParkK, 0);
		ME_BUSY = 0;
		ME_READY = 0;
		meAlive_ = 0;
		meLibSync();
	}
}
extern "C" unsigned int PSPME_Heartbeat(void) { return (unsigned int)ME_HB; }
extern "C" unsigned int PSPME_Jobs(void)      { return (unsigned int)ME_JOBS; }
extern "C" unsigned int PSPME_Calls(void)     { return meCalls_; }
#if ME_FM_PROBE
extern "C" unsigned int PSPME_FmCycles(void) { return (unsigned int)ME_FMCYC; }
#endif

#endif /* PSP_ME_OFFLOAD */
