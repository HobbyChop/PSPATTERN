#ifndef _FM_SINE_H_
#define _FM_SINE_H_

/* A sine without a table.

   The VFPU has no gather: it cannot read four table entries at four
   different phases in one instruction, which is the whole shape of an
   FM operator. So any vectorised operator kernel has to compute its
   sine instead of looking it up -- which is why msfa's own NEON
   kernel, and PSPHASE's port of it, both evaluate a polynomial.

   This is that polynomial, scalar, so it can be proved correct
   against the table on a machine that exists before anybody writes
   the vector version. It is also worth measuring on its own: a table
   read can miss the D-cache and a polynomial cannot, so the answer
   is not obviously "slower".

   Same contract as fmSin_[]: Q32 phase in, roughly +/-32000 out.

   NOT wired into the engine, on purpose. Swapping it in and measuring
   is how we learned what the sine is actually worth: making it 5.9x
   dearer took a four-operator FM voice from 3.28 to 6.19 base, which
   puts the lookup at 18% of the voice. Vectorising a fifth of a
   loop is Amdahl's law with extra steps, so the vector kernel this
   was the prerequisite for is not being written. It stays because
   tools/fmsine_test.cc proves it correct to one count in 32000, and
   because that decision should be re-checked, not re-derived, if the
   operator loop is ever restructured to batch four samples end to
   end.                                                              */

// Taylor for sin(pi/2 * u) on u in [0,1], five terms, in Q28. Five
// and not four because four leaves 1.6e-4 at the peak -- five
// leaves 3.5e-6, which is well under one step of a 16-bit output.
#define FMSIN_Q 28
#define FMSIN_A1   421657428     //  pi/2
#define FMSIN_A3  (-173400976)   // -(pi/2)^3/3!
#define FMSIN_A5    21393214     //  (pi/2)^5/5!
#define FMSIN_A7   (-1256710)    // -(pi/2)^7/7!
#define FMSIN_A9      43069      //  (pi/2)^9/9!

static inline int fmSinPoly(unsigned int phase) {

	// Quadrant, scalar and branch-free: the sign is the top bit, and
	// the second and fourth quarters are the first and third read
	// backwards.
	int neg=(int)(phase>>31) ;
	unsigned int x=phase&0x7FFFFFFFu ;          // half cycle
	if (x>=0x40000000u) x=0x80000000u-x ;       // mirror at the peak

	// u is the quarter cycle as Q30, so u == 1.0 at pi/2
	int u=(int)x ;
	int u2=(int)(((long long)u*u)>>30) ;

	// Horner in u^2, Q28 throughout
	long long t=FMSIN_A9 ;
	t=FMSIN_A7+((t*u2)>>30) ;
	t=FMSIN_A5+((t*u2)>>30) ;
	t=FMSIN_A3+((t*u2)>>30) ;
	t=FMSIN_A1+((t*u2)>>30) ;
	t=(t*u)>>30 ;                                // Q28, 0..1

	// the table's amplitude, so this is a drop-in for it
	int s=(int)((t*32000)>>FMSIN_Q) ;
	return neg?-s:s ;
}
#endif
