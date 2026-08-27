#ifndef _AUDIO_MIXER_H_
#define _AUDIO_MIXER_H_

#include "Application/Instruments/WavFileWriter.h"
#include "AudioModule.h"
#include "Foundation/T_SimpleList.h"
#include <string>
#include "MasterEq.h"

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
	/* Gain applied to every source as it is summed, not after.

	   The summing accumulator saturates -- it has to, it is a 32 bit
	   int and two full scale samples fill it -- so anything applied
	   AFTER the sum is attenuating audio that has already been
	   destroyed. That is the one job a master fader has, and it could
	   not do it: a mix eight channels deep arrived here flat-topped
	   and turning the fader down just made the flat-topping quieter.

	   Applied here instead, the fader buys real headroom in the sum.
	   The send return is a child of the master like any bus, so it is
	   scaled with them and the wet/dry balance holds. */
	void SetPreSumGain(fixed gain) { preSumGain_=gain ; }
	fixed GetPreSumGain() const { return preSumGain_ ; }

	/* Count the samples whose sum hit the rail this block.

	   How far over the top a mix went is NOT recoverable once the
	   accumulator has clamped -- the excess is gone, and keeping the
	   true sum would need a 64-bit accumulator, which is the cost the
	   summing loop was deliberately written to avoid on this chip.
	   How OFTEN the rail is hit is knowable and free, because the test
	   is already in the loop, and it answers the same question: a mix
	   that never saturates is clean, one that saturates constantly is
	   being destroyed.

	   Opt in, so the per-bus mixers pay nothing. */
	void SetTrackRawSum(bool on) { trackRawSum_=on ; }
	unsigned int GetSaturatedSamples() const { return (unsigned int)rawSumPeak_ ; }
	// how many samples were in that block, so the count above can be
	// turned into a share without the caller guessing the buffer size
	unsigned int GetSaturatedTotal() const { return rawSumTotal_ ; }

	/* The master graphic EQ. Only the master ever has bands set, so
	   on a bus this object sits inert -- MasterEq::Active() is
	   false while every band is flat and its Render returns
	   immediately. */
	void SetEqBand(int band,int value) { eq_.SetBand(band,value) ; }
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

#ifndef __PSP__
	// Two ways through the same clipper, for the equivalence test: the
	// old per-sample softClip and the new block form, both on the real
	// shipping code. clip is the UI value (1..4 = Subtle..Insane);
	// gain is softclipGain_. See tools/softclip_test.cc.
	fixed SoftClipOldForTest(fixed s,int clip,int gain) {
		softclip_=clip-1 ; softclipGain_=gain ; return softClip(s) ;
	}
	fixed SoftClipNewForTest(fixed s,int clip,int gain) {
		softclip_=clip-1 ; softclipGain_=gain ;
		fixed b=s ; softClipBlock(&b,1) ; return b ;
	}
#endif

private:
  fixed hardClip(fixed sample);
  fixed softClip(fixed sample);
  /* The cubic soft clipper over a whole block, in one branchless pass.

     The per-sample softClip() above was a function call with a branch
     inside it, run on every sample of the master buffer whenever a
     clip mode is on. This does the identical maths with the mode
     constants hoisted out of the loop and the in-range/saturated
     branch removed: the standard-cubic curve s - k*s^3 IS the
     saturated value once s is clamped to the knee, so clamping first
     makes the two cases one. See the .cpp for the algebra and
     tools/softclip_test.cc for the proof it lands on the same int16.

     This is also the shape the VFPU wants -- four samples through the
     same polynomial with no gather and no branch -- which is why the
     block form exists. The VFPU kernel itself lives behind
     PSP_VFPU_SOFTCLIP and is verified on the device, not here. */
  void softClipBlock(fixed *buf, int n);
#if defined(__PSP__) && defined(PSP_VFPU_SOFTCLIP)
  // Returns how many leading samples the VFPU handled; the scalar loop
  // finishes the rest. Zero means it did nothing (do it all in C).
  int softClipBlockVfpu(fixed *buf, int n, float knee, float k, float g);
#endif
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
  MasterEq eq_;
  fixed preSumGain_;
  bool trackRawSum_;
  fixed rawSumPeak_;
  unsigned int rawSumTotal_;
  int masterVolume_;
  bool clipped_;
  volatile bool clipLatch_;
  unsigned int peakMixerLevel_;
  unsigned int preMasterVolumePeakLevel_;
  unsigned int outputPeakLevel_;
};
#endif
