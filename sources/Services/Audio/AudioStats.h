#ifndef _AUDIO_STATS_H_
#define _AUDIO_STATS_H_

// Live output stats for the UI: a small scope ring of the master mix
// and a smoothed DSP load figure (render time vs realtime budget).
// Written by the audio path, read by views on their animation tick —
// reads are deliberately lock-free/racy, the data is cosmetic.

namespace AudioStats {

#define AUDIOSTATS_SCOPE_SIZE 256

	// audio side
	void BeginBlock() ;
	void EndBlock(short *interleaved,int frames,bool interlaced) ;

	/* Time spent inside the block that was not DSP.
	 *
	 * The wav writer's flush happens inside the measured window,
	 * because the tap that feeds it sits in the mixer's render. A
	 * 32KB write to a Memory Stick takes tens of milliseconds against
	 * a block budget of about six, so the block it lands on measures
	 * several hundred per cent and the peak hold latches it -- while
	 * nothing is actually wrong, because the prebuffer covers it and
	 * the song plays through without a glitch.
	 *
	 * A meter labelled dsp should say how close the DSP is to its
	 * deadline. Card I/O is not that, and reporting it as if it were
	 * teaches people to ignore the number, which is worse than not
	 * having one. So the writer declares its own time and it comes
	 * back off the total.
	 *
	 * It is subtracted, not hidden: if a write ever gets slow enough
	 * to outlast the prebuffer, that IS audible, and it is audible in
	 * the usual way rather than as a figure nobody trusts. */
	void ExcludeMicros(unsigned int us) ;

	/* Starvation: the driver served its silent buffer because the
	   render queue was empty -- the thing the prebuffer exists to
	   prevent, one count per silent serve. Reset when the transport
	   starts, so the figure always describes THIS run. */
	void AddUnderrun() ;
	int GetUnderruns() ;
	void ResetUnderruns() ;

	// UI side
	int GetDspPercent() ;

	// The worst single block seen recently, not the average.
	//
	// A dropout is caused by ONE block missing its deadline. The
	// average is smoothed over eight blocks, so a spike to 300 per
	// cent moves it by a few points and is gone again before anybody
	// reads it: a machine can glitch steadily while the meter sits at
	// twenty. This is the number that tells you whether that is what
	// is happening.
	int GetDspPeak() ;
	void ResetDspPeak() ;
	// A scope column is the MIN and MAX of a bucket of samples, not one
	// decimated sample. Two reasons, and the second is the one that
	// matters:
	//
	//  - decimating by N without a filter aliases, so the trace was
	//    showing frequencies that are not in the music;
	//  - a column per sample meant the whole display covered 8ms. At
	//    8ms a 50Hz sub is under half a cycle, so the trace was a
	//    shallow ramp that barely differed from one frame to the next.
	//    It looked frozen no matter how fast it was redrawn -- there
	//    was simply nothing new in it.
	//
	// A bucket of SCOPE_BUCKET frames puts ~33ms across the panel:
	// several cycles of the bass, and a kick transient sweeping over.
	/* channel 0 is left, 1 is right. Both are captured: the panel
	   shows a trace each, and a stereo mix that is doing something
	   different on the two sides should look like it. */
	void ReadScope(short *outMin,short *outMax,int count,int channel) ;

}
#endif
