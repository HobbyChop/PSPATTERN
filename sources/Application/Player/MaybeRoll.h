#ifndef _MAYBE_ROLL_H_
#define _MAYBE_ROLL_H_

/********************************************************
 MaybeTake:
    The dice MAYB rolls, in one place because two callers ask
    the same question: the player, deciding whether a step's
    note happens, and the table engine, deciding whether a
    column hops.

    chance is out of 0xFF. 00 never, FF always, and everything
    between is chance/255 -- so 80 is a hair over half.

    The generator is the same linear congruential one the synth
    voices use for RAND. Its low bits are poor, which is why the
    result is taken from bit 16 up; taking it from the bottom
    would give a sequence that alternates, and a "maybe" that
    alternates is just a pattern with extra steps.
 ********************************************************/

inline bool MaybeTake(unsigned int &rng, int chance) {

	if (chance >= 0xFF) return true ;
	if (chance <= 0) return false ;

	rng = rng * 1664525u + 1013904223u ;

	// 0..254 against the chance, so 00 can never win and FF is
	// already handled above.
	return (int)((rng >> 16) % 255u) < chance ;
}

#endif
