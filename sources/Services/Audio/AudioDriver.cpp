
#include "AudioDriver.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"

AudioDriver::AudioDriver(AudioSettings &settings) {
	settings_=settings ;
}

AudioDriver::~AudioDriver() {
}

bool AudioDriver::Init() {

  // Clear all buffers
	
   for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
     pool_[i].buffer_=0 ;
     pool_[i].size_=0 ;
     spare_[i]=0 ;
     spareSize_[i]=0 ;
   } ;
   isPlaying_=false;

   return InitDriver() ;
}

void AudioDriver::Close() {
	CloseDriver() ;
	for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
		SAFE_FREE(spare_[i]) ;
		spareSize_[i]=0 ;
	} ;
};

bool AudioDriver::Start() {

    isPlaying_=true ; 

    for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
  	  SAFE_FREE(pool_[i].buffer_) ;
    } ;
	 
    poolQueuePosition_=0 ;
    poolPlayPosition_=0 ;
	hasData_=false ;

    return StartDriver() ;
};

void AudioDriver::Stop() {
     isPlaying_=false ;
	hasData_=false ;
     StopDriver() ;
}

void AudioDriver::AddBuffer(short *buffer,int samplecount) {
  
  int len=samplecount*2*sizeof(short) ;

  if (!isPlaying_) return ;

  if (len>SOUND_BUFFER_MAX) {
      Trace::Error("Alert: buffer size exceeded") ;
  }

  if (pool_[poolQueuePosition_].buffer_!=0) {
  NInvalid ;
  Trace::Error("Audio overrun, please report") ;
  SAFE_FREE(pool_[poolQueuePosition_].buffer_) ;
  return ;
  }	

  // Reuse the block this slot handed back last time round if it is
  // still big enough. The block size only moves by a frame or two
  // between ticks, so after the first pass through the pool this stops
  // touching the heap at all.

  int q=poolQueuePosition_ ;

  if (spare_[q]&&spareSize_[q]<len) {
     SAFE_FREE(spare_[q]) ;
     spareSize_[q]=0 ;
  } ;

  if (spare_[q]) {
     pool_[q].buffer_=spare_[q] ;
     spare_[q]=0 ;
  } else {
     pool_[q].buffer_=(char*) ((short *)SYS_MALLOC(len)) ;
     spareSize_[q]=len ;
  } ;

  SYS_MEMCPY(pool_[q].buffer_,(char *)buffer,len) ;
  pool_[q].size_=len ;
  poolQueuePosition_=(q+1)%SOUND_BUFFER_COUNT ;
	hasData_=true ;
}

void AudioDriver::ReleaseBuffer(int index) {
  if (spare_[index]) {
     // Should not happen: a slot holds either a queued buffer or a
     // spare, never both. Free rather than leak if it ever does.
     SYS_FREE(spare_[index]) ;
  } ;
  spare_[index]=pool_[index].buffer_ ;
  pool_[index].buffer_=0 ;
}

void AudioDriver::OnNewBufferNeeded() {
  SetChanged() ;
  Event event(Event::ADET_BUFFERNEEDED);
  NotifyObservers(&event) ;
} ;

void AudioDriver::onAudioBufferTick()
{
  SetChanged() ;
  Event event(Event::ADET_DRIVERTICK);
  NotifyObservers(&event) ;
}

bool AudioDriver::hasData() {
	return hasData_ ;
}  ;

AudioSettings AudioDriver::GetAudioSettings() {
	return settings_ ;
} ;
