#ifndef _AUDIO_DRIVER_H_
#define _AUDIO_DRIVER_H_

#include "Foundation/Observable.h"
#include "AudioSettings.h"

#define SOUND_BUFFER_COUNT 50
#define SOUND_BUFFER_MAX 20000

struct AudioBufferData {
   char *buffer_ ;
   int size_ ;
   void *driverData_ ;
} ;

class AudioDriver: public Observable {

public:
  class Event: public I_ObservableData
  {
  public:
    enum Type 
    {
      ADET_DRIVERTICK,
      ADET_BUFFERNEEDED
    };
    
    Event(Type type)
    {
      type_=type;
    };
    Type type_;
  };
  
public:
	AudioDriver(AudioSettings &settings) ;
	virtual ~AudioDriver() ;

	virtual bool Init() ;
	virtual void Close() ;	
	virtual bool Start() ;
	virtual void Stop() ;	

	virtual bool InitDriver()=0 ;
	virtual void CloseDriver()=0 ;
	virtual bool StartDriver()=0 ;
	virtual void StopDriver()=0 ; 

	virtual bool Interlaced()=0 ;
	virtual int GetPlayedBufferPercentage()=0 ;   
 
	virtual double GetStreamTime()=0 ; // in secs

	void AddBuffer(short *buffer,int size) ; // size in samples

	AudioSettings GetAudioSettings() ;

	void OnNewBufferNeeded() ;

protected:
	void eatBuffer(void *buffer,int size) ; // size in bytes
	void onAudioBufferTick() ;
	bool hasData() ;

	/* Hand a played slot back instead of freeing it.

	   AddBuffer used to SYS_MALLOC a block and the consumer used to
	   SYS_FREE it, which is a malloc/free pair per audio block inside
	   the audio thread. It is not much time -- of the order of ten
	   microseconds against a block budget of fourteen thousand -- but
	   it is heap traffic on a path that must not stall, and it buys
	   nothing.

	   ReleaseBuffer keeps the allocation in spare_ and nulls
	   pool_[].buffer_ exactly as the free did, so the "null means the
	   slot is empty" protocol every driver relies on is unchanged.
	   AddBuffer then reuses the spare when it is big enough. Drivers
	   that still call SYS_FREE themselves keep working: they simply
	   never populate spare_, and AddBuffer allocates for them as
	   before. */
	void ReleaseBuffer(int index) ;

	AudioSettings settings_ ;

protected:
	bool isPlaying_ ;
	AudioBufferData pool_[SOUND_BUFFER_COUNT] ;
	char *spare_[SOUND_BUFFER_COUNT] ;
	int spareSize_[SOUND_BUFFER_COUNT] ;
	int poolQueuePosition_ ;
	int poolPlayPosition_ ;
	int bufferPos_ ;
	int bufferSize_ ;
	bool hasData_ ;
} ;
#endif
