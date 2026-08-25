#include "AudioMixer.h"
#include "System/System/System.h"
#include <math.h>

#define MAX_POSITIVE_FIXED i2fp(32767)
#define MAX_NEGATIVE_FIXED i2fp(-32768)
// Everything the accumulator can hold: two full scale samples, near
// enough. Sums are held here rather than allowed to roll over.
#define MIX_ACCUM_CEILING 2147483647LL

AudioMixer::AudioMixer(const char *name):
	T_SimpleList<AudioModule>(false),
	enableRendering_(0),
	writer_(0),
	name_(name)
{
	volume_=(i2fp(1)) ;
    mixBuffer_ = 0;
    mixBufferSamples_ = 0;
    softclip_ = -1;
    softclipGain_ = 0 ;
	masterVolume_ = 100 ;
	clipped_ = false ;
	clipLatch_ = false ;
    peakMixerLevel_ = 0;
    preMasterVolumePeakLevel_ = 0 ;
    outputPeakLevel_ = 0 ;
	
	// Precalculate constant values for softclipping algorithm
	softClipData_[0].alpha = 1.45f; // -1.5db (approx.)
	softClipData_[1].alpha = 1.07f; // -3db (approx.)
	softClipData_[2].alpha = 0.75f; // -6db (approx.)
	softClipData_[3].alpha = 0.53f; // -9db (approx.)

	for (int i = 0; i < 4; i++) {
		softClipData_[i].alpha23 = softClipData_[i].alpha * (2.0f / 3.0f);
		softClipData_[i].alphaInv = 1.0f / softClipData_[i].alpha;

		if (softClipData_[i].alpha > 1.0f) {
			/* calculates gain compensation differently for
			 * modes with alpha > 1, so there's no drop in loudness
			 * and we can still drive the hard clipper when the input
			 * goes over 1.0
			 */
			softClipData_[i].gainCmp = 1.0f / (1.0f - (pow(softClipData_[i].alphaInv, 2.0f) / 3.0f));
		} else {
			softClipData_[i].gainCmp = 1.0f / softClipData_[i].alpha23;
		}
	}
} ;

AudioMixer::~AudioMixer() {
	SAFE_FREE(mixBuffer_) ;
}

void AudioMixer::SetFileRenderer(const char *path) {
	renderPath_=path ;
} ;

void AudioMixer::EnableRendering(bool enable) {

	if (enable==enableRendering_) {
		return ;
	}

	if (enable) {
		writer_=new WavFileWriter(renderPath_.c_str()) ;
	} 

	enableRendering_=enable ;
	if (!enable) {
		writer_->Close() ;
		SAFE_DELETE(writer_) ;
	}
} ;

bool AudioMixer::Render(fixed *buffer,int samplecount) {
    clipped_ = false;

    bool gotData = false;
    IteratorPtr<AudioModule> it(GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        AudioModule &current = it->CurrentItem();
        if (!gotData) {
            gotData=current.Render(buffer,samplecount) ;           
         } else {
            if (mixBufferSamples_<samplecount) {
               // only on the first mix and on a tempo change, never on
               // the steady-state path
               SAFE_FREE(mixBuffer_) ;
               mixBuffer_=(fixed *)malloc(samplecount*2*sizeof(fixed)) ;
               mixBufferSamples_=mixBuffer_?samplecount:0 ;
            }
            fixed *mixBuffer=mixBuffer_ ;
            if (!mixBuffer) continue ; 
            if (current.Render(mixBuffer,samplecount)) {
               fixed *dst=buffer ;
               fixed *src=mixBuffer ;
               int count=samplecount*2 ;
               while (count--) {
                 // SATURATE, do not wrap. `fixed` is a 32 bit int and
                 // a full scale sample is 32767<<15, so this
                 // accumulator holds two of them and no more. A third
                 // used to roll the sign bit over, and a wrapped
                 // sample is not distortion -- it is a full scale
                 // step in the opposite direction, a click, on every
                 // sample it lands on. That is why the artefacts got
                 // worse the more parts were playing, and why no
                 // channel meter ever showed clipping: each bus
                 // clips itself to full scale before the master ever
                 // adds it in, so the sum was the only place it
                 // could go wrong. tools/mixhead_test.cc measures it.
                 // Both sides are already held to +/-MAX_POSITIVE_FIXED
                 // -- every child clips its own output before
                 // returning, and this loop clips its accumulator on
                 // every step -- and two of those is 2147418112,
                 // which is 65535 short of what a 32 bit int holds.
                 // So the add cannot overflow and does not need to be
                 // done in 64 bits, which on a chip without a 64 bit
                 // add was costing an extra sequence per sample per
                 // channel, ten times over, forty-four thousand times
                 // a second.
                 fixed sum=*dst+*src ;
                 if (sum>MAX_POSITIVE_FIXED) sum=MAX_POSITIVE_FIXED ;
                 else if (sum<MAX_NEGATIVE_FIXED) sum=MAX_NEGATIVE_FIXED ;
                 *dst=sum ;
                 dst++ ;
                 src++ ;
               }
            }
         }
     }

     //  Apply volume

     if (gotData) {
         fixed *c = buffer;
         float damp = pow((float)masterVolume_ / 100, 4.0f);

         // Capture pre-volume peaks (raw signal before any processing)
         fixed preVolumePeakL = i2fp(0), preVolumePeakR = i2fp(0);
         for (int i = 0; i < samplecount * 2; i += 2) {
             fixed left = c[i];
             fixed right = c[i + 1];
             if (left < 0) left = -left;
             if (right < 0) right = -right;
             if (left > preVolumePeakL) preVolumePeakL = left;
             if (right > preVolumePeakR) preVolumePeakR = right;
         }
         // Pack and store pre-volume peaks
         unsigned int prePackedL = (unsigned int)fp2i(preVolumePeakL);
         unsigned int prePackedR = (unsigned int)fp2i(preVolumePeakR);
         if (prePackedL > 0xFFFF) prePackedL = 0xFFFF;
         if (prePackedR > 0xFFFF) prePackedR = 0xFFFF;
         preMasterVolumePeakLevel_ = (prePackedL << 16) | prePackedR;

         // Track peak levels (left and right channels)
         fixed peakL = i2fp(0), peakR = i2fp(0);

         if (volume_ != i2fp(1)) {
             for (int i = 0; i < samplecount * 2; i++) {
                 fixed v = fp_mul(*c, volume_);
                 *c++ = v;
             }
         }

         // Re-point c to buffer start for peak tracking
         c = buffer;
         
         // Track peak levels for both channels
         for (int i = 0; i < samplecount * 2; i += 2) {
             fixed left = c[i];
             fixed right = c[i + 1];
             if (left < 0) left = -left;
             if (right < 0) right = -right;
             if (left > peakL) peakL = left;
             if (right > peakR) peakR = right;
         }

         // left 16 bits | right 16 bits, clamped to 16-bit range
         unsigned int packedL = (unsigned int)fp2i(peakL);
         unsigned int packedR = (unsigned int)fp2i(peakR);
         if (packedL > 0xFFFF) packedL = 0xFFFF;
         if (packedR > 0xFFFF) packedR = 0xFFFF;
         peakMixerLevel_ = (packedL << 16) | packedR;

         // Apply soft/hard clipping before recording.
         //
         // This ran every sample through fixed -> float -> fixed even
         // with soft clipping off and the master at 100, where both
         // stages are identities. Take the fixed-only path when there
         // is nothing for the float maths to do.
         c = buffer;
         fixed outL = 0, outR = 0;
         if ((softclip_ == -1) && (masterVolume_ == 100)) {
             for (int i = 0; i < samplecount * 2; i++) {
                 *c = hardClip(*c);
                 fixed a = *c < 0 ? -*c : *c;
                 if (i & 1) { if (a > outR) outR = a; }
                 else       { if (a > outL) outL = a; }
                 c++;
             }
         } else {
             for (int i = 0; i < samplecount * 2; i++) {
                 // Master volume BEFORE the hard clipper, not after.
                 // It used to be the other way round, which made the
                 // master fader useless for the one job a master
                 // fader has: a mix that was over the top got clipped
                 // first and quietly attenuated second, so turning it
                 // down changed how loud the distortion was without
                 // removing any of it. Now it buys real headroom.
                 fixed sample = softClip(*c);
                 sample = fl2fp(damp * fp2fl(sample));
                 sample = hardClip(sample);
                 *c++ = sample;
                 fixed a = sample < 0 ? -sample : sample;
                 if (i & 1) { if (a > outR) outR = a; }
                 else       { if (a > outL) outL = a; }
             }
         }
         {
             unsigned int oL = (unsigned int)fp2i(outL);
             unsigned int oR = (unsigned int)fp2i(outR);
             if (oL > 0xFFFF) oL = 0xFFFF;
             if (oR > 0xFFFF) oR = 0xFFFF;
             outputPeakLevel_ = (oL << 16) | oR;
         }
     }
    if (enableRendering_&&writer_) {
		if (!gotData) {
			memset(buffer,0,samplecount*2*sizeof(fixed)) ;
		} ;
		writer_->AddBuffer(buffer,samplecount) ;
	}
     return gotData ;
} ;

void AudioMixer::SetVolume(fixed volume) { volume_ = volume; }

void AudioMixer::SetSoftclip(int clip, int gain) {
    softclip_ = clip - 1;
	softclipGain_ = gain;
}

void AudioMixer::SetMasterVolume(int volume) {
	masterVolume_ = volume;
}

bool AudioMixer::Clipped() { return clipped_; }

fixed AudioMixer::hardClip(fixed sample) {
    if (sample > MAX_POSITIVE_FIXED || sample < MAX_NEGATIVE_FIXED) {
        clipped_ = true;
        clipLatch_ = true;
		return sample > 0 ? MAX_POSITIVE_FIXED : MAX_NEGATIVE_FIXED;
    }
    return sample;
}

/* Implements standard cubic algorithm
 * https://wiki.analog.com/resources/tools-software/sigmastudio/toolbox/nonlinearprocessors/standardcubic
 */
fixed AudioMixer::softClip(fixed sample) {
    if (softclip_ == -1 || sample == 0)
        return sample;

    float x;
    float sampleFloat = fp2fl(sample);
	float maxFloat = fp2fl(sampleFloat > 0 ? MAX_POSITIVE_FIXED : MAX_NEGATIVE_FIXED);
	SoftClipData* data = &softClipData_[softclip_];

    x = data->alphaInv * (sampleFloat / maxFloat);
    if (x > -1.0f && x < 1.0f) {
        // was pow(x, 3.0f), evaluated on every sample of every buffer
        sampleFloat = maxFloat * (data->alpha * (x - ((x * x * x) / 3.0f)));
    } else {
        sampleFloat = maxFloat * data->alpha23;
    }

    if (softclipGain_) {
        sampleFloat = sampleFloat * data->gainCmp;
    }

    return fl2fp(sampleFloat);
}
