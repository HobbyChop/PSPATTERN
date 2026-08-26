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

/* Six voice rows, then three for the send bus.
 *
 * The bus was the one thing this could not tell you about, which is
 * an odd gap given it is the most expensive single item in the mixer
 * -- more than seven saw voices on the build host. Every figure for
 * it came from a machine that is not a PSP, and it is about to be
 * moved onto the Media Engine, so a real before number matters more
 * than usual.
 *
 * On the send rows the step axis means channels SENDING rather than
 * voices, because that is what the accumulate scales with. The bank
 * itself costs the same whether one channel feeds it or eight, and
 * seeing those two behaviours separately is the point. */
#define DSPB_ENGINES 9      // 6 voice shapes + delay / reverb / both
#define DSPB_VOICE_ROWS 6
#define DSPB_STEPS   4      // 1, 2, 4, 8 voices, or senders

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
