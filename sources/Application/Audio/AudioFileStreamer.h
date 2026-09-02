#ifndef _AUDIO_FILE_STREAMER_H_
#define _AUDIO_FILE_STREAMER_H_

#include "Services/Audio/AudioModule.h"
#include "System/FileSystem/FileSystem.h"
#include "Application/Instruments/WavFile.h"

enum AudioFileStreamerMode {
	AFSM_STOPPED,
	AFSM_PLAYING
} ;

/* Plays one sound straight to the master, outside the song: the
   preview in the import browser and on the samples screen. The
   source is a wav on the card, read a block at a time, or a buffer
   already in RAM (a pool entry). Either way it is resampled to the
   driver rate, so a 22050Hz file sounds at its own pitch rather than
   an octave up -- which is what a plain copy of the samples did. */
class AudioFileStreamer: public AudioModule {
public:
	AudioFileStreamer() ;
	virtual ~AudioFileStreamer() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	bool Start(const Path &) ;
	// a sound already in memory: the caller keeps the buffer alive
	// until StopNow has returned
	bool StartBuffer(const short *frames,long frameCount,int channels,int rate) ;
	void Stop() ;      // ramped: the render finishes the fade
	void StopNow() ;   // at once, under the mixer lock, before the memory goes
	bool IsPlaying() { return mode_==AFSM_PLAYING ; } ;
	// how the source is heard: folded to mono, every div-th frame
	// averaged in -- the same reduction the import writes, so the
	// preview is what the project will hold
	void SetShape(bool mono,int div) ;
protected:
	void begin(int channels,int rate,long frames) ;
	void applyShape() ;
	AudioFileStreamerMode mode_ ;
	Path path_ ;
	WavFile *wav_ ;          // the file source, or 0
	const short *mem_ ;      // the memory source, or 0
	int channels_ ;          // as heard, after the fold
	int srcChannels_ ;
	int srcRate_ ;
	long srcFrames_ ;
	bool mono_ ;
	int div_ ;
	short *fold_ ;           // one block of folded frames
	long foldFrames_ ;
	long size_ ;             // source frames
	long position_ ;         // source frame the next block starts at
	unsigned int phase_ ;    // 16.16 fraction of a source frame past position_
	unsigned int step_ ;     // 16.16 source frames per output frame
	int shift_ ;
	/* A stream that begins at full level steps the converter from
	   silence to wherever the waveform starts -- the tick before every
	   preview. A few hundred samples of ramp at each end costs
	   nothing and is inaudible as a fade. */
	int fadeIn_ ;
	int fadeOut_ ;
	volatile bool stopping_ ;
} ;

#endif
