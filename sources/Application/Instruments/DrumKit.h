#ifndef _DRUM_KIT_H_
#define _DRUM_KIT_H_

/* DRUMKIT_BAKE_HOST is the build-time baker (Resources/bake_drumkit.cpp)
   including this synthesis on a desktop, with none of the app around
   it. It wants the drum roster and the maths and nothing else. */
#ifndef DRUMKIT_BAKE_HOST
#include "SoundSource.h"
#endif

/* Drums with no files.

   A tracker with no samples on the stick has nothing to play, and the
   kit this was developed against is sliced from a break we have no
   right to redistribute. So the kit is synthesised at boot instead --
   the same move PSPOLY and PSPECTRA make, for the same reason -- and
   handed to the SamplePool as ordinary sources.

   That last part is the point of doing it this way rather than as a
   fifth synth engine: a baked drum IS a sample, so it gets the loop
   points, the filter, the bit crush, the slices and the amplitude
   envelope the sampler already has, and a project references it by
   name exactly as it would a wav. A song built on these opens on a
   machine with an empty memory stick.

   Two kits, twelve drums each. ANxxx is the analog one -- long
   sine-body kick, noisy snare, 808 lineage. HDxxx hits harder:
   shorter sweeps, soft-clipped body, brighter noise.               */

#define DRUMKIT_COUNT   2
#define DRUMKIT_DRUMS  12
#define DRUMKIT_TOTAL  (DRUMKIT_COUNT*DRUMKIT_DRUMS)

// The rate everything bakes at. Drums are the one thing that wants
// the top octave -- a cymbal at 22kHz is a hiss -- so this is not a
// place to save memory.
#define DRUMKIT_RATE 44100

#ifndef DRUMKIT_BAKE_HOST
class BakedSource : public SoundSource {
public:
	virtual bool IsBaked() { return true ; }
private:
	friend class BakedSourceInit_ ;
public:
  public:
	BakedSource(short *buf,int frames,bool owns=true)
	  : buf_(buf),frames_(frames),owns_(owns) {} ;
	virtual ~BakedSource() ;
	virtual int GetSize(int note) { return frames_ ; } ;
	virtual int GetSampleRate(int note) { return DRUMKIT_RATE ; } ;
	virtual int GetChannelCount(int note) { return 1 ; } ;
	virtual void *GetSampleBuffer(int note) { return buf_ ; } ;
	virtual bool IsMulti() { return false ; } ;
	// 60 is what WavFile reports, so a baked drum transposes exactly
	// like a wav does and C-5 is its native pitch
	virtual int GetRootNote(int note) { return 60 ; } ;
  private:
	short *buf_ ;
	int frames_ ;
	/* Whether the buffer is ours to free. An embedded kit is read
	   straight out of the executable's own data, so the source
	   points at it and owns nothing. */
	bool owns_ ;
} ;
#endif

namespace DrumKit {

	// Name as it appears in the sample list, e.g. "ANKICK". Stable:
	// projects reference these, so they are not free to change.
	const char *Name(int i) ;

	/* The synthesis itself: a fresh mono 16 bit buffer for drum i and
	   its length in frames, or 0 if it would not fit. Shared with the
	   build-time baker, which is the only reason it is separate from
	   Bake below. */
	short *BakePcm(int i,int *frames) ;

#ifndef DRUMKIT_BAKE_HOST
	// Drum i as a source. Caller owns it. Returns 0 if the kit will
	// not fit -- that is not a reason to refuse to boot.
	//
	// Where DRUMKIT_EMBEDDED is defined this reads a kit baked into
	// the executable and nothing is synthesised or allocated at all.
	BakedSource *Bake(int i) ;
#endif
	/* Give back the shared working buffer. Call once the kit is
	   baked: holding it costs half a megabyte and nothing reads it
	   between one boot's bake and the next, which there isn't. */
	void ReleaseWork() ;
}
#endif
