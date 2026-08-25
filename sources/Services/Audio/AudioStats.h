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

	// UI side
	int GetDspPercent() ;
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
	void ReadScope(short *outMin,short *outMax,int count) ;
}
#endif
