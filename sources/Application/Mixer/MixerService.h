#ifndef _MIXER_SERVICE_H_
#define _MIXER_SERVICE_H_

#ifdef SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#include "Application/Commands/CommandDispatcher.h" // Would be better done externally and call an API here
#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"
#include "Services/Audio/AudioMixer.h"
#include "Services/Audio/AudioOut.h"
#include "MixBus.h"
#include "Services/Audio/SendFx.h"

enum MixerServiceRenderMode {
    MSRM_PLAYBACK,
    MSRM_STEREO,
};

#define MAX_BUS_COUNT 10

class MixerService: 
      public T_Singleton<MixerService>,
      public Observable,
      public I_Observer,
      public CommandExecuter      
{

public:
	MixerService() ;
	virtual ~MixerService() ;

	bool Init() ;
	void Close() ;

	bool Start() ;
	void Stop() ;

    MixBus *GetMixBus(int i);
    // the two global effect settings, pushed down from the Mixer
    // model each audio tick. The per-channel send levels are not
    // here: they are taken at the end of each channel strip, in
    // PlayerChannel, so that routing two channels to one bus does
    // not make them share an effects send.
    void SetSendFxParams(int division,int feedback,int size,int damp);
    // The fold-in effects (freeze/drive now; more later), pushed every
    // tick alongside SetSendFxParams.
    void SetSendFx2(int freeze,int drive,int duck,int gate,int comp,
                    int locut,int width,int dtone);
    unsigned int GetMasterPeakLevel() const;
    bool TakeMasterClipLatch();
    // What share of the last block the master sum spent pinned at the
    // rail, 0..100. Zero means the mix fits; anything rising means it
    // is being squared off in the accumulator and the fader has work
    // to do. See the implementation for why this is not a "how far
    // over" figure -- that number is not recoverable here.
    unsigned int GetSaturationPercent();

    virtual void Update(Observable &o, I_ObservableData *d);

    void OnPlayerStart();
    void OnPlayerStop() ;

	bool Clipped() ;
    void SetPregain(int);
    void SetSoftclip(int, int);
    void SetMasterVolume(int);
    void SetEqBand(int band,int value);
    void SetRenderMode(int);
    bool IsRendering();
    int GetPlayedBufferPercentage() ;
	
	virtual void Execute(FourCC id,float value) ;

	AudioOut *GetAudioOut() ;

	void Lock() ;
	void Unlock() ;

protected:
	void toggleRendering(bool enable) ;
private:
  void initRendering(MixerServiceRenderMode);
  AudioOut *out_;
  MixBus master_;
  MixBus bus_[MAX_BUS_COUNT];
  // last child of the master, so the wet is summed with the dry
  // BEFORE the master fader and the clipper
  SendFx::Return sendReturn_;
  MixerServiceRenderMode mode_;
  SDL_mutex *sync_;
  bool isRendering_;
} ;
#endif
