#include "SoundFontPreset.h"
#include "Externals/Soundfont/ENAB.H"
#include "System/Console/Trace.h"
#ifdef SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif

/* The zone lookup walks the file's navigator, one object per preset
   with state of its own, and the note it landed on is cached beside
   it. Two threads walking it at once would corrupt it, and since the
   keyboard lanes render on the output thread (PlayerMixer::RenderLate)
   while the song renders on its own, both can play the same preset at
   different notes at the same moment. So every accessor holds one lock
   from the lookup through the read of what it found. Uncontended
   nearly always; a few microseconds when not; taken a few times per
   block per voice, outside the sample loops. */
static SDL_mutex *sfLock_=0 ;
static inline void sfLock() { if (sfLock_) SDL_LockMutex(sfLock_) ; }
static inline void sfUnlock() { if (sfLock_) SDL_UnlockMutex(sfLock_) ; }

SoundFontPreset::SoundFontPreset(int sfID,int presetID):
	sfID_(sfID),
	presetID_(presetID),
	vect_(0),
	lastNote_(-1)
{
	navigator_.SetHydraFont(GetHydraPtr(sfID));   // would be bank select
	// presets are built on the main thread while the pool loads, long
	// before either render thread can want one
	if (!sfLock_) sfLock_=SDL_CreateMutex() ;
} ;

SoundFontPreset::~SoundFontPreset() {
}

int SoundFontPreset::GetChannelCount(int note) {
	// Until I merge the L & R samples
	return 1 ;
} ;

void *SoundFontPreset::GetSampleBuffer(int note){
	sfLock() ;
	checkNote(note) ;
	void *r=vect_?(void *)vect_->dwStart:0 ;
	sfUnlock() ;
	return r ;
} ;

int SoundFontPreset::GetSampleRate(int note) {
	sfLock() ;
	checkNote(note) ;
	int r=vect_?(int)vect_->dwSampleRate:44100 ;
	sfUnlock() ;
	return r ;
} ;

int SoundFontPreset::GetSize(int note) {
	sfLock() ;
	checkNote(note) ;
	int r=vect_?(int)vect_->dwEnd:0 ;
	sfUnlock() ;
	return r ;
} ;

int SoundFontPreset::GetRootNote(int note) {
	sfLock() ;
	checkNote(note) ;
	int r=60 ;
	if (vect_) {
		twoByteUnion tbu ;
		tbu.wVal=vect_->shOrigKeyAndCorr ;
		r=tbu.byVals.by1;
	} ;
	sfUnlock() ;
	return r ;
} ;

bool SoundFontPreset::IsMulti() {
	return true ;
} ;

bool SoundFontPreset::IsLooped(int note) {
	sfLock() ;
	checkNote(note) ;
	// a note with no zone used to dereference null here
	bool r=vect_?((vect_->shSampleModes&0x1)!=0):false ;
	sfUnlock() ;
	return r ;
} ;

int SoundFontPreset::GetLoopStart(int note) {
	sfLock() ;
	checkNote(note) ;
	int r=-1 ;
	if (vect_) {
		bool looped=((vect_->shSampleModes&0x1)!=0) ;
		r=looped?(int)vect_->dwStartloop:-1 ;
	}
	sfUnlock() ;
	return r ;
} ;

int SoundFontPreset::GetLoopEnd(int note) {
	sfLock() ;
	checkNote(note) ;
	int r=vect_?(int)vect_->dwEndloop:-1 ;
	sfUnlock() ;
	return r ;
} ;

void SoundFontPreset::checkNote(int note) {
	if (note!=lastNote_) {
		navigator_.Navigate( presetID_, note, 127 ); 
		int oscCount=navigator_.GetNOsc() ;
		if (oscCount!=0) {
			vect_= navigator_.GetSFPtr();		
		}
		lastNote_=note ;
	}
} ;

