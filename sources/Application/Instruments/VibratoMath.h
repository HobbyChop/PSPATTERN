#ifndef _VIBRATO_MATH_H_
#define _VIBRATO_MATH_H_

#include <math.h>

/********************************************************
 VibratoSemitones:
    How far VIBR has bent the note, in semitones, this tick.

    Three instruments need this and each wants the answer in
    its own units -- the sampler as a playback rate ratio, the
    synth as a Q16 multiplier on a phase increment, MIDI as
    1/256 semitone of pitch bend -- so what they share is the
    semitone figure and nothing else.

    speed is a phase advance per tick; the phase is 16 bit and
    wraps on its own, which is why there is no comparison here.
    depth is in sixteenths of a semitone, so 10 is one semitone
    and 80 is the eight the widest setting reaches.

    The result is centred on zero: a note with vibrato on it
    sits at the same average pitch as one without, which is the
    difference between vibrato and detuning.
 ********************************************************/

inline float VibratoSemitones(unsigned short &phase,
                              unsigned short speed,
                              unsigned char depth) {

	if (speed == 0 || depth == 0) return 0.0f ;

	phase = (unsigned short)(phase + (speed << 8)) ;

	return (float(depth) / 16.0f)
	     * sinf(float(phase) * (2.0f * 3.14159265f / 65536.0f)) ;
}

#endif
