#ifndef _AUDIO_FILE_STREAMER_H_
#define _AUDIO_FILE_STREAMER_H_

#include "Services/Audio/AudioModule.h"
#include "System/FileSystem/FileSystem.h"
#include "Application/Instruments/WavFile.h"

enum AudioFileStreamerMode {
	AFSM_STOPPED,
	AFSM_PLAYING
} ;

class AudioFileStreamer: public AudioModule {
public:
	AudioFileStreamer() ;
	virtual ~AudioFileStreamer() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	bool Start(const Path &) ;
	void Stop() ;
protected:
	AudioFileStreamerMode mode_ ;
	Path path_ ;
	bool newPath_ ;
	WavFile *wav_ ;
	int position_ ;
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
