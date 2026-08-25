#ifndef _AUDIO_MIXER_H_
#define _AUDIO_MIXER_H_

#include "Application/Instruments/WavFileWriter.h"
#include "AudioModule.h"
#include "Foundation/T_SimpleList.h"
#include <string>

struct SoftClipData {
    float alpha;
	float alpha23;
	float alphaInv;
	float gainCmp;
};

class AudioMixer: public AudioModule,public T_SimpleList<AudioModule> {
public:
	AudioMixer(const char *name) ;
	virtual ~AudioMixer() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	void SetFileRenderer(const char *path) ;
	void EnableRendering(bool enable) ;
	void SetVolume(fixed volume) ;
    unsigned int GetPeakLevel() const { return peakMixerLevel_; }
    unsigned int GetPreMasterVolumePeakLevel() const {
        return preMasterVolumePeakLevel_;
    }
    // What actually leaves the mixer: after the fader, after the
    // clipper. The master meter has to read THIS. It used to read
    // the raw sum of the buses, which on a normal mix runs a couple
    // of times over full scale before the fader brings it down --
    // so the master lit up as clipping while the output was clean
    // and every channel meter, correctly, showed nothing wrong.
    unsigned int GetOutputPeakLevel() const { return outputPeakLevel_; }
    virtual void SetSoftclip(int clip, int gain);
    virtual void SetMasterVolume(int volume) ;
	virtual bool Clipped() ;
	// Clipped() is reset every block, so a transient that clips for
	// one buffer is gone before a 25ms UI frame can see it. This
	// latch is set by the clipper and cleared only by whoever reads
	// it, which is what a clip light on a desk actually does.
	bool TakeClipLatch() { bool c=clipLatch_ ; clipLatch_=false ; return c ; }
	
private:
  fixed hardClip(fixed sample);
  fixed softClip(fixed sample);
  bool enableRendering_;
  std::string renderPath_;
  WavFileWriter *writer_;
  fixed volume_;
  std::string name_;
  SoftClipData softClipData_[4];
  // Scratch for summing a second module into the mix. This used to be
  // malloc'd and freed inside the render callback, on every buffer.
  // Grown only when the slice size does, which is on tempo change.
  fixed *mixBuffer_;
  int mixBufferSamples_;
  int softclip_;
  int softclipGain_;
  int masterVolume_;
  bool clipped_;
  volatile bool clipLatch_;
  unsigned int peakMixerLevel_;
  unsigned int preMasterVolumePeakLevel_;
  unsigned int outputPeakLevel_;
};
#endif
