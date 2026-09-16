
#ifndef _APPLICATION_MIXER_H_
#define _APPLICATION_MIXER_H_

#include "Foundation/T_Singleton.h"
#include "Application/Model/Project.h"
#include "Application/Views/ViewData.h"
#include "Application/Utils/fixed.h"
#include "Application/Audio/AudioFileStreamer.h"
#include "PlayerChannel.h"
#include "Foundation/Observable.h"
#include "Services/Audio/AudioOut.h"
#include "Services/Audio/AudioDriver.h"

#define STREAM_MIX_BUS 8

/* On the PSP the lanes past the song's eight are not rendered by the
   render thread at all: they are rendered at the output callback, past
   the render-ahead queue -- see RenderLate. Elsewhere they stay in the
   audition bus and render with everything else. */
#ifdef PLATFORM_PSP
#define PSP_LATE_LANES 1
#endif

class PlayerMixer: public T_Singleton<PlayerMixer>,public Observable,public I_Observer,public AudioLateRender {
public:
	PlayerMixer() ;
	virtual ~PlayerMixer() {} ;

	bool Start() ;
	void Stop() ;
	bool Init(Project *project) ;
	void Close() ;

	void OnPlayerStart() ;
	void OnPlayerStop() ;

	void StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument) ;
	void SetVelocity(int channel,fixed v) ;
	void StopInstrument(int channel) ;
	void CutInstrument(I_Instrument *instr) ;

	int GetChannelNote(int Channel) ;

	I_Instrument *GetInstrument(int channel) ;

	I_Instrument *GetLastInstrument(int channel) ;
	
	void StartChannel(int channel) ;
	void StopChannel(int channel) ;
	// StopChannel lets a release ring out; this cuts the voice now
	void CutChannel(int channel) ;

	bool IsChannelPlaying(int channel) ;
	
	bool StartStreaming(const Path &) ;
	void StopStreaming()  ;
	bool StartStreamingBuffer(const short *frames,long frameCount,int channels,int rate) ;
	void StopStreamingNow() ;
	bool IsStreaming() ;
	void SetStreamingShape(bool mono,int div) ;

	bool Clipped() ;

	void Update(Observable &o,I_ObservableData *d) ;
	int GetPlayedBufferPercentage() ;   

	void SetChannelMute(int channel,bool mute) ;
    bool IsChannelMuted(int channel);

    char *GetPlayedNote(int channel);
    char *GetPlayedOctive(int channel) ;
	
	AudioOut *GetAudioOut() ;

	void Lock() ;
	void Unlock() ;

	// AudioLateRender: the lanes, into the chunk leaving for the device
	virtual void RenderLate(short *interleaved,int frames) ;
	/* The lanes' own lock. The output thread holds it while it renders
	   them; whoever starts, stops, cuts or rewires a lane holds it for
	   that. Never nested, and never taken by the output thread together
	   with the mixer lock, which the render thread holds for a whole
	   slice: the output thread must not wait on that. Order everywhere
	   else is mixer lock, then this. */
	void LaneLock() ;
	void LaneUnlock() ;

private:

	Project *project_ ;
	bool clipped_ ;
	
    I_Instrument *lastInstrument_[PLAYER_CHANNEL_COUNT] ;
	bool isChannelPlaying_[PLAYER_CHANNEL_COUNT] ;

	AudioFileStreamer fileStreamer_ ;
	PlayerChannel *channel_[PLAYER_CHANNEL_COUNT] ;

	// store trigger notes, 0xFF = none
	
    unsigned char notes_[PLAYER_CHANNEL_COUNT] ;

	struct SDL_mutex *laneSync_ ;
	bool lanesWired_ ;
	// the lanes' tick clock, in frames since the last tick (RenderLate)
	float laneTickAcc_ ;
} ;

#endif
