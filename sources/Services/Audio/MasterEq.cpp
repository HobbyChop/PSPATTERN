#include "MasterEq.h"

/* PSPECTRA's coefficients, unchanged: same chip, same sample rate,
   same job. Nine octave-spaced one-pole splits from about 42Hz to
   about 12.9kHz. */
const int MasterEq::coef_[9] = { 197, 394, 776, 1529, 2989,
                                 5722, 10600, 18641, 27541 } ;

/* The same coefficients as float smoothing factors, coef/32768, for
   the float full-ladder and the VFPU kernel. Spelled out rather than
   divided at startup so they are compile-time constants. */
const float MasterEq::coeff_[9] = {
	197.0f/32768.0f,   394.0f/32768.0f,   776.0f/32768.0f,
	1529.0f/32768.0f,  2989.0f/32768.0f,  5722.0f/32768.0f,
	10600.0f/32768.0f, 18641.0f/32768.0f, 27541.0f/32768.0f } ;

#if defined(__PSP__) && defined(PSP_VFPU_EQ)
/* The float ladder on the VFPU: four bands to a lane, nine bands padded
   to twelve so the whole thing is three uniform quad groups with no
   scalar tail. UNVERIFIED OFF THE DEVICE -- assembles under psp-gcc and
   computes the algebra that RenderFloatScalar proves on the host, but a
   V register exists only on the PSP. Off by default (PSP_VFPU_EQ).

   Idioms per the PSP VFPU docs and libpspmath/vectormath-vfpu:
     - the one-pole lp += coef*(x-lp) is a lerp, done as the direct
       three-op vsub/vmul/vadd (there is no vector FMA on Allegrex);
     - the output x*g9 + SUM lp[k]*dg[k] is two-and-a-bit vdot.q;
     - x is broadcast into four lanes by the source swizzle C[x,x,x,x];
     - vi2f/vf2iz with scale 15 ARE fp2fl/fl2fp (proven on device by the
       soft-clip kernel);
     - nothing is saved or restored: the audio thread owns the VFPU
       (confirmed on hardware). The mix buffer itself is read one fixed
       at a time (lw/sw), so its alignment does not matter; only the
       local state arrays are lv.q'd, and those are aligned here.

   All constants and both channels' state stay resident in V registers
   across the whole block; the lp state is loaded once at the top and
   stored back once at the end. */
void MasterEq::RenderVfpu(fixed *buffer,int samplecount) {
	if (samplecount<=0) return ;

	float __attribute__((aligned(16))) coefA[12], dgA[12], lpL[12], lpR[12] ;
	for (int k=0;k<12;k++) {
		coefA[k]=(k<9)?coeff_[k]:0.0f ;
		dgA[k]  =(k<9)?dgf_[k]  :0.0f ;
		lpL[k]  =(k<9)?lpLf_[k] :0.0f ;
		lpR[k]  =(k<9)?lpRf_[k] :0.0f ;
	}
	int g9bits ; { float g=g9f_ ; __builtin_memcpy(&g9bits,&g,4) ; }

	__asm__ volatile (
		// ---- constants, loaded once ----
		"lv.q C100, 0(%[coef])\n"  "lv.q C110, 16(%[coef])\n" "lv.q C120, 32(%[coef])\n"
		"lv.q C200, 0(%[dg])\n"    "lv.q C210, 16(%[dg])\n"   "lv.q C220, 32(%[dg])\n"
		"mtv  %[g9], S230\n"
		// ---- state, loaded once ----
		"lv.q C300, 0(%[lpL])\n"   "lv.q C310, 16(%[lpL])\n"  "lv.q C320, 32(%[lpL])\n"
		"lv.q C400, 0(%[lpR])\n"   "lv.q C410, 16(%[lpR])\n"  "lv.q C420, 32(%[lpR])\n"
		"move $t0, %[buf]\n"
		"move $t1, %[n]\n"
		"1:\n"
		// ---- left ----
		"lw   $t2, 0($t0)\n"
		"mtv  $t2, S000\n"
		"vi2f.s S000, S000, 15\n"                 // x = L/32768
		"vsub.q C500, C000[x,x,x,x], C300\n"      // x - lpL[0..3]
		"vmul.q C500, C500, C100\n"               // * coef
		"vadd.q C300, C300, C500\n"               // lpL[0..3] +=
		"vsub.q C510, C000[x,x,x,x], C310\n"
		"vmul.q C510, C510, C110\n"
		"vadd.q C310, C310, C510\n"
		"vsub.q C520, C000[x,x,x,x], C320\n"
		"vmul.q C520, C520, C120\n"
		"vadd.q C320, C320, C520\n"
		"vdot.q S600, C300, C200\n"               // SUM lpL[0..3]*dg
		"vdot.q S601, C310, C210\n"
		"vdot.q S602, C320, C220\n"
		"vmul.s S603, S000, S230\n"               // x*g9
		"vadd.s S600, S600, S601\n"
		"vadd.s S602, S602, S603\n"
		"vadd.s S600, S600, S602\n"               // accL
		"vf2iz.s S600, S600, 15\n"                // -> fixed
		"mfv  $t2, S600\n"
		"sw   $t2, 0($t0)\n"
		// ---- right ----
		"lw   $t2, 4($t0)\n"
		"mtv  $t2, S010\n"
		"vi2f.s S010, S010, 15\n"
		"vsub.q C500, C010[x,x,x,x], C400\n"
		"vmul.q C500, C500, C100\n"
		"vadd.q C400, C400, C500\n"
		"vsub.q C510, C010[x,x,x,x], C410\n"
		"vmul.q C510, C510, C110\n"
		"vadd.q C410, C410, C510\n"
		"vsub.q C520, C010[x,x,x,x], C420\n"
		"vmul.q C520, C520, C120\n"
		"vadd.q C420, C420, C520\n"
		"vdot.q S600, C400, C200\n"
		"vdot.q S601, C410, C210\n"
		"vdot.q S602, C420, C220\n"
		"vmul.s S603, S010, S230\n"
		"vadd.s S600, S600, S601\n"
		"vadd.s S602, S602, S603\n"
		"vadd.s S600, S600, S602\n"
		"vf2iz.s S600, S600, 15\n"
		"mfv  $t2, S600\n"
		"sw   $t2, 4($t0)\n"
		// ---- next frame ----
		"addiu $t0, $t0, 8\n"
		"addiu $t1, $t1, -1\n"
		"bgtz  $t1, 1b\n"
		"nop\n"
		// ---- state back ----
		"sv.q C300, 0(%[lpL])\n"   "sv.q C310, 16(%[lpL])\n"  "sv.q C320, 32(%[lpL])\n"
		"sv.q C400, 0(%[lpR])\n"   "sv.q C410, 16(%[lpR])\n"  "sv.q C420, 32(%[lpR])\n"
		// Drain the VFPU write buffer before the C below reads lpL/lpR,
		// or the CPU can load stale state (the one non-interlocked
		// hazard here: sv.q -> CPU read).
		"vflush\n"
		:
		: [coef]"r"(coefA), [dg]"r"(dgA), [lpL]"r"(lpL), [lpR]"r"(lpR),
		  [buf]"r"(buffer), [n]"r"(samplecount), [g9]"r"(g9bits)
		: "$t0","$t1","$t2","memory") ;

	for (int k=0;k<9;k++) { lpLf_[k]=lpL[k] ; lpRf_[k]=lpR[k] ; }
}
#endif
