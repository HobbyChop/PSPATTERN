
#include "SyncMaster.h"
#include "Services/Audio/Audio.h"

#ifdef WIN32
#define AUDIO_SLICES_PER_STEP 6  // needs to be a multiple of 6 !
#else
#define AUDIO_SLICES_PER_STEP 6  // needs to be a multiple of 6 !
#endif

SyncMaster::SyncMaster() {
	tableRatio_=1 ;
	// the rest were left uninitialized: SetTempo normally lands before
	// anything reads them, but "normally" isn't a guarantee and the
	// failure mode is a garbage slice size
	tempo_=138 ;
	currentSlice_=0 ;
	beatCount_=0 ;
	playSampleCount_=0.0f ;
	tickSampleCount_=0.0f ;
}

void SyncMaster::Start() {
	currentSlice_=0 ;
	beatCount_=0 ;
} ;

void SyncMaster::Stop() {
} ;

void SyncMaster::SetTempo(int tempo) {
	// every expression below divides by this
	if (tempo<1) tempo=1 ;
	tempo_=tempo ;
	int driverRate=Audio::GetInstance()->GetSampleRate() ;
    playSampleCount_=60.0f*driverRate*2.0f/tempo_/8.0f/float(AUDIO_SLICES_PER_STEP)  ;
	tickSampleCount_=60.0f*driverRate*2.0f/tempo_/8.0f/float(AUDIO_SLICES_PER_STEP)*tableRatio_  ;
}  ;

int SyncMaster::GetTempo() {
	return tempo_ ;
} ;

void SyncMaster::NextSlice() {
 	currentSlice_=(currentSlice_+1)%AUDIO_SLICES_PER_STEP ;
	if (currentSlice_==0) {
		beatCount_++ ;
	};
}  ;

bool SyncMaster::MajorSlice() {
	return	currentSlice_==0  ;
} ;

bool SyncMaster::TableSlice() {
	int tableTick=currentSlice_%(AUDIO_SLICES_PER_STEP/6*tableRatio_) ;
	return	tableTick==0  ;
} ;

bool SyncMaster::MidiSlice() {
	int midiTick=currentSlice_%(AUDIO_SLICES_PER_STEP/6) ;
	return midiTick==0  ;
} ;

// Returns the number of samples per play slice

float SyncMaster::GetPlaySampleCount() {
	return playSampleCount_ ;
} ;

// Returns the number of sample per tick
float SyncMaster::GetTickSampleCount() {
	return tickSampleCount_ ;
} ;

//xx samples per tick
//xx/driverRate seconds
//xx/driverRate*1000 msecs

float SyncMaster::GetTickTime() {
	return 60.0f*2.0f/tempo_/8.0f/AUDIO_SLICES_PER_STEP*1000.0f  ;
} ;

void SyncMaster::SetTableRatio(int ratio) {
	// TABLERATIO is read straight out of the project file, and
	// TableSlice divides by it -- a zero there is an integer divide by
	// zero on every audio slice.
	if (ratio<1) ratio=1 ;
	if (ratio>AUDIO_SLICES_PER_STEP) ratio=AUDIO_SLICES_PER_STEP ;
	tableRatio_=ratio ;
}

int SyncMaster::GetTableRatio() {
	return tableRatio_ ;
}

unsigned int SyncMaster::GetBeatCount() {
	return beatCount_ ;
} ;