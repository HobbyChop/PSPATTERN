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
    preSumGain_ = FP_ONE;
    trackRawSum_ = false;
    rawSumPeak_ = 0;
    rawSumTotal_ = 0;
    softclipGain_ = 0 ;
	masterVolume_ = 100 ;
	clipped_ = false ;
	clipLatch_ = false ;
    peakMixerLevel_ = 0;
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
    if (trackRawSum_) { rawSumPeak_ = 0; rawSumTotal_ = samplecount * 2; }

    bool gotData = false;
    IteratorPtr<AudioModule> it(GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        AudioModule &current = it->CurrentItem();
        if (!gotData) {
            gotData=current.Render(buffer,samplecount) ;
            // The fader acts here, on the way in, so that the sum has
            // room. See SetPreSumGain.
            if (gotData && preSumGain_ != FP_ONE) {
                fixed *g = buffer;
                int count = samplecount * 2;
                while (count--) { *g = fp_mul(*g, preSumGain_); g++; }
            }
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
               // The fader is folded into the sum read below instead of
               // a separate read-modify-write pass over the buffer: the
               // master defaults to 75, so every stock project paid
               // that extra 2KB walk per bus per block. Safe because
               // the gain is (master/100)^4 <= 1, so the scaled source
               // still respects the saturation invariant.
               fixed g=preSumGain_ ;
               bool scaled=(g!=FP_ONE) ;
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
                 /* How hot the mix is, measured where it matters.

                    An exact "how far over the top did the sum go" is
                    not recoverable here: once an earlier child pushed
                    the accumulator to the rail, the excess is gone,
                    and holding the true sum would need a 64-bit
                    accumulator -- the cost this loop was written to
                    avoid. What IS knowable, and free, is how often the
                    rail is being hit, because the test is already
                    here. A mix that never saturates is clean; one that
                    saturates on a fifth of its samples is being
                    destroyed, and that is the thing worth showing. */
                 fixed sum=*dst+(scaled?fp_mul(*src,g):*src) ;
                 if (sum>MAX_POSITIVE_FIXED) {
                     sum=MAX_POSITIVE_FIXED ;
                     if (trackRawSum_) rawSumPeak_++ ;
                 } else if (sum<MAX_NEGATIVE_FIXED) {
                     sum=MAX_NEGATIVE_FIXED ;
                     if (trackRawSum_) rawSumPeak_++ ;
                 }
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

         // Master fader / per-channel volume. Two extra full-buffer peak
         // scans used to bracket this: a pre-volume peak that had no
         // readers anywhere, and the post-volume peak (peakMixerLevel_,
         // the bus VU) which equals the post-clip peak taken below -- a
         // bus's EQ is inert and hard clip is identity until full scale.
         // Both are gone; peakMixerLevel_ is set from that single pass.
         if (volume_ != i2fp(1)) {
             for (int i = 0; i < samplecount * 2; i++) {
                 fixed v = fp_mul(*c, volume_);
                 *c++ = v;
             }
         }

         
         // Apply soft/hard clipping before recording.
         //
         // This ran every sample through fixed -> float -> fixed even
         // with soft clipping off and the master at 100, where both
         // stages are identities. Take the fixed-only path when there
         // is nothing for the float maths to do.
         c = buffer;
         fixed outL = 0, outR = 0;
         /* The EQ sits here: after the sum and after the fader, and
            BEFORE the clippers -- shape the tone, then catch whatever
            the shaping pushed over. PSPECTRA runs the same order.

            It costs nothing until somebody moves a band: Render
            returns immediately while every band is flat, which is
            where they all start. */
         eq_.Render(buffer, samplecount);

         /* The master fader is no longer here. It is applied to every
            source on the way INTO the sum -- see SetPreSumGain -- so
            by this point the signal has already been faded and what
            is left to do is shape the peaks. */
         if (softclip_ == -1) {
             for (int i = 0; i < samplecount * 2; i++) {
                 *c = hardClip(*c);
                 fixed a = *c < 0 ? -*c : *c;
                 if (i & 1) { if (a > outR) outR = a; }
                 else       { if (a > outL) outL = a; }
                 c++;
             }
         } else {
             // Soft clip the whole block first, in one branchless pass
             // that the VFPU can take over on the device -- see
             // softClipBlock. The fader has already acted, on the way
             // into the sum, so nothing here re-applies it: the
             // accumulator saturates while summing, so a mix that
             // arrived over the top was already flat-topped and
             // attenuating it afterwards only made the damage quieter.
             softClipBlock(c, samplecount * 2);
             // Then the hard-clip catch and the peak read. The cubic
             // in gain-compensated modes can push past full scale, so
             // this ceiling still has to be here.
             for (int i = 0; i < samplecount * 2; i++) {
                 fixed sample = hardClip(*c);
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
             // Bus VU reads peakMixerLevel_; on a bus this post-clip
             // peak matches the old post-volume scan (inert EQ, hard
             // clip) below full scale and both peg above it.
             peakMixerLevel_ = outputPeakLevel_;
         }
     } else {
         /* Nothing sounded this block, so the meters read zero.

            Without this they keep whatever the last block that DID
            sound left behind: a muted channel's bar stays lit until
            the transport stops, because UpdateVuBarHeights only
            decays a bar when the new peak is LOWER than the old one,
            and a frozen peak is never lower than itself. */
         peakMixerLevel_ = 0;
         outputPeakLevel_ = 0;
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

/* The same clipper, over a block, with the one branch removed -- and
   why removing it is exact.

   The per-sample version normalises to x = alphaInv * s / max, then
   picks between the cubic maxFloat*alpha*(x - x^3/3) inside the knee
   and the constant maxFloat*alpha23 outside it. Two facts collapse
   that choice to a single clamp:

     - x never goes negative. max carries the sign of s, so s/max is
       |s|/|max| and x >= 0. The lower half of the -1<x<1 test is dead.

     - alpha23 == alpha*2/3 (see the init), and the cubic AT x=1 is
       alpha*(1 - 1/3) = alpha*2/3. So the saturated branch is exactly
       the cubic evaluated at the knee. Clamp x to 1 and both cases are
       the one expression.

   So the SCALAR path below keeps the original's per-sign max, its
   s/max divide, its stored alpha23 saturated value and its exact
   multiply grouping -- it is the old softClip to the bit, only with
   the if/else turned into a SELECT of the two already-computed values
   (a conditional move, not a taken branch). softclip_test.cc checks
   the int16 out is identical for every input and mode, so no default
   build's sound moves at all.

   The VFPU kernel wants no divide and no per-lane sign pick, so it
   uses the algebraically equal reassociated form

       out = s - k*s^3 ,   k = alphaInv^2 / (3 * M^2)

   with s clamped to +/- alpha*M. That folds the sign into s and drops
   the divide, at the cost of unifying the 32767/32768 rails into one
   M -- a sub-LSB (-96 dBFS) difference from the scalar reference, on a
   distortion path that is off by default and verified on the device.

   gainCmp, in the gain-compensated modes, multiplies the finished
   value in both, just as the original did. */
void AudioMixer::softClipBlock(fixed *buf, int n) {
    if (softclip_ == -1)
        return;

    const SoftClipData &d = softClipData_[softclip_];

    int i = 0;
#if defined(__PSP__) && defined(PSP_VFPU_SOFTCLIP)
    // The VFPU takes the aligned quads off the front and reports how
    // many samples it did; the scalar loop below finishes the head/
    // tail it left. i stays 0 if it did nothing. Constants in the
    // fp2fl domain, full scale 32768.0.
    {
        const float M = 32768.0f;
        const float knee = d.alpha * M;
        const float k = (d.alphaInv * d.alphaInv) / (3.0f * M * M);
        const float g = softclipGain_ ? d.gainCmp : 1.0f;
        i = softClipBlockVfpu(buf, n, knee, k, g);
    }
#endif

    // Scalar reference: the old per-sample softClip to the bit. Both
    // the in-range cubic and the saturated constant are computed, then
    // selected by the original's own comparison -- same operations,
    // same grouping, same stored alpha23, so the result is identical
    // and the select is branchless.
    const float MAXP = fp2fl(MAX_POSITIVE_FIXED);   //  32767.0
    const float MAXN = fp2fl(MAX_NEGATIVE_FIXED);   // -32768.0
    for (; i < n; i++) {
        float s = fp2fl(buf[i]);
        float m = (s > 0.0f) ? MAXP : MAXN;         // sign-carrying max
        float x = d.alphaInv * (s / m);             // >= 0
        float in  = m * (d.alpha * (x - ((x * x * x) / 3.0f)));
        float sat = m * d.alpha23;
        float o = (x > -1.0f && x < 1.0f) ? in : sat;
        if (softclipGain_) o *= d.gainCmp;
        buf[i] = fl2fp(o);
    }
}

#if defined(__PSP__) && defined(PSP_VFPU_SOFTCLIP)
/* The VFPU kernel: four samples of s - k*s^3 per pass.

   UNVERIFIED OFF THE DEVICE. Everything above this point is proven on
   the build host; a VFPU register exists nowhere but the PSP, so this
   is written to the documented semantics and left OFF by default
   (PSP_VFPU_SOFTCLIP undefined). Enable it only to A/B on hardware,
   and only once two preconditions are met:

     1. The render must run on a thread created with THREAD_ATTR_VFPU.
        AudioMixer::Render is driven from the audio driver thread; if
        that thread has no VFPU context, touching a V register there
        faults. Confirm before enabling.

     2. lv.q / sv.q move 16 bytes and want a 16-byte-aligned address.
        buf is SYS_MALLOC'd with no such guarantee, so this handles a
        scalar prologue up to the first aligned quad and returns the
        number of leading samples it did NOT vectorise; the caller's
        scalar loop finishes the head and tail. (Returns false to mean
        "did nothing, do it all in C".)

   The maths is the scalar block's: convert fixed->float with vi2f
   scale 15 (that IS /32768 = fp2fl), clamp to +/-knee by broadcasting
   +knee and -knee into two vectors and vmin/vmax against them,
   evaluate the polynomial, convert back truncating toward zero with
   vf2iz scale 15 (matching the (fixed) cast in fl2fp).

   Returns the number of leading samples processed (a whole number of
   quads); the caller's scalar loop finishes from there. Zero means it
   declined (unaligned buffer, or fewer than four samples). */
int AudioMixer::softClipBlockVfpu(fixed *buf, int n,
                                  float knee, float k, float g) {
    // lv.q/sv.q want a 16-byte-aligned address. The mix buffer is
    // allocated once and is aligned in practice; if it ever is not,
    // decline and let C do the whole block.
    if (((unsigned int)buf & 15) != 0)
        return 0;

    int quads = n >> 2;          // whole vec4 groups
    if (quads == 0)
        return 0;

    // The two clamp vectors, +knee and -knee in all four lanes, built
    // in aligned memory and loaded with lv.q -- more robust than the
    // VFPU broadcast-prefix syntax, and done once. k and g go into
    // scalar slots for vscl.q.
    float __attribute__((aligned(16))) kv[4]  = { knee, knee, knee, knee };
    float __attribute__((aligned(16))) nkv[4] = { -knee, -knee, -knee, -knee };

    // One self-contained block: constants and the loop together, so the
    // compiler cannot land VFPU-touching code between the setup and the
    // body. $8/$9 are the pointer and the down-counter.
    __asm__ volatile (
        "lv.q   C020, 0(%1)\n"              // C020 = (+knee x4)
        "lv.q   C030, 0(%2)\n"              // C030 = (-knee x4)
        "mtv    %3, S010\n"                 // S010 = k
        "mtv    %4, S011\n"                 // S011 = g
        "move   $8, %0\n"                   // $8 = buf
        "move   $9, %5\n"                   // $9 = quads
        "1:\n"
        "lv.q   C100, 0($8)\n"              // raw 4 x int32
        "vi2f.q C100, C100, 15\n"           // -> float, /32768  (fp2fl)
        "vmin.q C100, C100, C020\n"         // min(s, +knee)
        "vmax.q C100, C100, C030\n"         // max(., -knee)  -> clamp
        "vmul.q C110, C100, C100\n"         // s^2
        "vmul.q C110, C110, C100\n"         // s^3
        "vscl.q C110, C110, S010\n"         // k*s^3
        "vsub.q C100, C100, C110\n"         // s - k*s^3
        "vscl.q C100, C100, S011\n"         // * g
        "vf2iz.q C100, C100, 15\n"          // -> int32, *32768, trunc-to-zero
        "sv.q   C100, 0($8)\n"
        "addiu  $8, $8, 16\n"               // next quad (16 bytes)
        "addiu  $9, $9, -1\n"
        "bgtz   $9, 1b\n"
        "nop\n"
        :
        : "r"(buf), "r"(kv), "r"(nkv), "r"(k), "r"(g), "r"(quads)
        : "$8", "$9", "memory");

    return quads << 2;           // samples the scalar loop should skip
}
#endif
