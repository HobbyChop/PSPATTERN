#ifndef _SYNC_MASTER_H_
#define _SYNC_MASTER_H_

#include "Foundation/T_Singleton.h"

// Provide basic functionalities to compute various
// setting regarding tempo, buffer sizes, ticks

class SyncMaster: public T_Singleton<SyncMaster> {
public:
	SyncMaster() ;
	void Start() ;
	void Stop() ;
	void SetTempo(int tempo) ;
	/* Following an external clock needs finer steps than whole beats
	   per minute: one integer step at 128 is 0.78%, which is 62ms of
	   drift across an eight second phrase. The sample counts below
	   were always floats; only the way in was rounded. */
	void SetTempoFine(float tempo) ;
	int GetTempo() ;
	void NextSlice() ;
	bool MajorSlice() ;
	bool TableSlice() ;
	bool MidiSlice() ;
	float GetPlaySampleCount() ;
	float GetTickSampleCount() ;
	int GetTableRatio() ;
	void SetTableRatio(int ratio) ;
	unsigned int GetBeatCount() ;
	unsigned int GetTickCount() ;
	float GetTickTime() ;
private:
	int tempo_ ;
	float fineTempo_ ;
	int currentSlice_ ;
	int tableRatio_ ;
	unsigned int beatCount_ ;
	// ticks since Start: the song clock anything free-running is timed from
	unsigned int tickCount_ ;
	float playSampleCount_ ;
	float tickSampleCount_ ;

} ;
#endif
