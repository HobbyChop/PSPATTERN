#ifndef _DSP_BENCH_H_
#define _DSP_BENCH_H_

/* What does a voice actually cost on THIS machine?

   Every performance figure this project has is from a build host or
   an emulator, because reading a DSP meter on a PSP means holding a
   PSP. This makes that reading one button press instead of a
   protocol: it builds its own instruments, renders a fixed workload
   through the real engines, and times it against the same block
   budget the audio path uses.

   Deterministic and self-contained -- it does not touch the project,
   the player or the mixer, so the number is the engine and nothing
   else. It blocks the main thread for a couple of seconds and the
   audio will stutter while it runs; that is the price of measuring
   the render path with the render path.                            */

#define DSPB_ENGINES 5      // tone / pdx / vax / fm / sample-ish floor
#define DSPB_STEPS   4      // 1, 2, 4, 8 voices

namespace DspBench {

	struct Result {
		// permille of one block's realtime budget, [engine][step]
		short load_[DSPB_ENGINES][DSPB_STEPS] ;
		int   blockFrames_ ;
		int   sampleRate_ ;
		int   cpuMhz_ ;         // 0 if it could not be read
		bool  done_ ;
	} ;

	const char *EngineName(int e) ;
	int VoicesAt(int step) ;

	// Blocking. Fills r; safe to call from the UI thread.
	void Run(Result &r) ;
}
#endif
