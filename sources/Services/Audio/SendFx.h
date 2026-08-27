#ifndef _SEND_FX_H_
#define _SEND_FX_H_

#include "AudioModule.h"

// A shared delay and reverb, fed by per-channel sends.
//
// Every other part of this machine is dry. A tracker whose channels
// can only ever be a dry sample and a dry oscillator sounds like eight
// things happening in a row rather than one record, no matter how good
// the oscillators are -- which is most of the distance between this
// and the trackers people compare it to.
//
// The wiring follows the mixer that already exists rather than
// fighting it: each MixBus adds a scaled copy of itself into the two
// accumulators below as it renders, and a SendReturn module sitting
// LAST in the master's child list turns those accumulators into wet
// signal. Because the return is a child of the master, its output is
// summed with the dry before the master fader and the clipper, so
// pulling the master down pulls the reverb down with it, which is what
// anyone would expect.
//
// Storage is 16 bit. The lines carry the same integers the output
// carries, so this costs nothing in resolution and halves the memory
// -- which matters on a handheld with a fixed heap.

namespace SendFx {

	// Longest line we will ever need: one second covers a half note
	// down to 120bpm, and the musical divisions clamp to it.
	#define SENDFX_MAX_DELAY 44100

	void Init(int sampleRate) ;
	void Close() ;

	// called by MixBus as it renders; levels are 0..255
	void Accumulate(const fixed *buffer,int samplecount,
	                int delaySend,int reverbSend) ;

	// tempo-synced delay time. division indexes DIV_* below.
	void SetTempo(int bpm) ;
	void SetDelayDivision(int division) ;
	void SetDelayFeedback(int f) ;      // 0..255
	void SetReverbSize(int s) ;         // 0..255
	void SetReverbDamp(int d) ;         // 0..255

	enum { DIV_16, DIV_8T, DIV_8, DIV_D8, DIV_4, DIV_D4, DIV_2,
	       DIV_COUNT } ;
	const char *DivisionName(int division) ;

	// true when either accumulator has had anything put in it since
	// the last Process -- lets the master skip the whole tail when the
	// song uses no sends at all
	bool Active() ;

	// clear the lines: a stop should not leave a tail hanging over the
	// next start
	void Flush() ;

	/* Run the bank a block behind, which is what a second core doing
	   the work imposes: its result is not ready until the block after
	   the one that fed it. One block of latency on the WET path only,
	   5.8ms at 256 frames -- inaudible on a reverb by construction,
	   and less than a finger's jitter on a delay. The dry path and
	   the timing of the notes are untouched. */
	void SetDeferred(bool on) ;
	/* WHO runs the bank, as opposed to WHEN. Never true unless the
	   Media Engine has been proved to work on this machine. */
	void SetMeDriving(bool on) ;
	bool MeDriving() ;
#ifndef __PSP__
	int FlushCountForTest() ;
	void ResetFlushLogForTest() ;
	bool WasFlushedForTest(const void *p) ;
	const void *WetAccForTest(int which) ;
	const void *BankForTest() ;
#endif
	bool Deferred() ;

	// The wet return. Owned by MixerService, inserted last into the
	// master so it is summed after every bus has had its say.
	class Return : public AudioModule {
	  public:
		virtual bool Render(fixed *buffer,int samplecount) ;
	} ;
}

#endif
