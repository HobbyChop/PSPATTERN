#ifndef _SDL_SOUND_H_
#define _SDL_SOUND_H_

#include "Services/Audio/AudioDriver.h"
#include <SDL/SDL.h>
#include "System/Process/Process.h"

class SDLAudioDriver ;

class SDLAudioDriverThread: public SysThread {
public:
	SDLAudioDriverThread(SDLAudioDriver *driver) ;
	virtual ~SDLAudioDriverThread() {} ;
	virtual bool Execute() ;
	virtual void RequestTermination() ;
	void Notify() ;
private:
	SDLAudioDriver *driver_ ;
	SysSemaphore *semaphore_ ;
} ;

class SDLAudioDriver:public AudioDriver {
public:
  SDLAudioDriver(AudioSettings &settings) ;
  virtual ~SDLAudioDriver() ;
  
   // Sound implementation
	virtual bool InitDriver() ; 
	virtual void CloseDriver();
	virtual bool StartDriver() ; 
	virtual void StopDriver();
	virtual int GetPlayedBufferPercentage() ;
	// the rate the device ACTUALLY opened at — tempo, envelopes and
	// LFO rates are all derived from this, so assuming the requested
	// rate makes the whole song run at the wrong speed if the device
	// gives us something else
	virtual int GetSampleRate() { return GetActualSampleRate() ; } ;
	static int GetActualSampleRate() {
		return (actualSampleRate_>0)?actualSampleRate_:44100 ;
	} ;
	virtual bool Interlaced() { return true ; } ;	
	virtual double GetStreamTime() ;
	// Additional
	void OnChunkDone(Uint8 *stream,int len) ;

private:
  static int actualSampleRate_ ;
  int fragSize_ ;  // Actual fragsize used by the driver
  char *unalignedMain_ ;
  char *mainBuffer_ ;
  char *miniBlank_ ;
  int bufferPos_ ;
  int bufferSize_ ;
  SDLAudioDriverThread *thread_ ;
	Uint32 startTime_ ;
} ;


#endif
