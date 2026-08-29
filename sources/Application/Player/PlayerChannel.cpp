
#include "PlayerChannel.h"
#include "Services/Audio/SendFx.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Utils/fixed.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

PlayerChannel::PlayerChannel(int index) {             
    index_=index ;
    instr_=0 ;
    releasing_=false ;
    muted_=false ;
    velocity_=i2fp(1) ;
    delaySend_=0 ;
    reverbSend_=0 ;
	mixBus_=0 ;
	busIndex_=-1 ;
    volume_ = i2fp(1);
    curGain23_ = (1<<23);
    gainSnap_ = true;
    hpfPrevInput_[0] = hpfPrevInput_[1] = i2fp(0);
    hpfPrevOutput_[0] = hpfPrevOutput_[1] = i2fp(0);
    hpfAlpha_ = i2fp(0);
    hpfMode_ = 0;
    lpfPrevOutput_[0] = lpfPrevOutput_[1] = i2fp(0);
    lpfAlpha_ = i2fp(0);
    lpfFreq_ = 0;
    phaserRate_ = 128; phaserDepth_ = 0;
    chorusRate_ = 96;  chorusDepth_ = 0;
    phLfoPh_ = 0; chLfoPh_ = 0; chPos_ = 0;
    chBuf_ = 0; chClear_ = false;
    for (int k = 0; k < 4; k++) phZ_[k][0] = phZ_[k][1] = 0;
    lastOut_[0] = lastOut_[1] = 0;
    click_[0] = click_[1] = 0;
    declickPending_ = false;
}

// a tail can never exceed the full swing of the converter
#define CHANNEL_CLICK_MAX (i2fp(32000))

PlayerChannel::~PlayerChannel() {
	if (chBuf_) { free(chBuf_) ; chBuf_=0 ; }
}

void PlayerChannel::StartInstrument(I_Instrument *instr,unsigned char note,bool trigger) {
   // Whether this note follows one of its own is about to be erased by
   // the stop below, and it's the only place that knows. Same
   // instrument only -- sliding out of some other instrument's note is
   // not what anyone means by glide.
   // A note that follows a release tail is not a legato note -- the
   // previous one already ended.
   bool followsOwnNote=(instr_==instr)&&(!releasing_) ;

   /* Arm the declick for EVERY note start, not just the ones that
      follow silence.

      This used to lean on StopInstrument to arm it, and StopInstrument
      only does so when the outgoing instrument has no release stage --
      it returns early the moment IsReleasing is true. But release 0 is
      a 128-sample fade, not an instant cut, so IsReleasing is true
      straight after Stop for anything that was still sounding. The
      flag was therefore left clear at exactly the moment there was a
      step to smooth, and set only when the channel was already idle
      and both sides of the join were zero. Precisely inverted: the
      correction ran where nothing needed correcting and stood down
      where the click was.

      Arming it unconditionally is safe because applyDeclick measures
      the real jump (lastOut_ minus the first sample of the block). A
      note that happens to start near the level the last one ended at
      gets a correction of nearly zero and costs nothing. */
   if (instr_) {
      StopInstrument() ;
   }
   declickPending_=true ;
   // A new note takes its velocity immediately. See curGain23_.
   gainSnap_=true ;
   if (followsOwnNote) {
      instr->NoteFollowsNote(index_) ;
   }
   if (instr->Start(index_,note,trigger)) { // note could be refused coz it's out of the keymap
	   instr_=instr ;
	   // The StopInstrument above flags the CHANNEL as releasing when
	   // the old note had a release stage. Start then clears the
	   // VOICE's own flag, because a new note cancels the tail -- but
	   // nothing cleared this one, and Render reads it as "the release
	   // ran out, let the instrument go". So the next block after any
	   // note that followed another note threw the new note away, about
	   // twenty milliseconds in, cutting the waveform to silence where
	   // it stood. Every note after the first on a channel, on every
	   // patch with a release.
	   releasing_=false ;
   } else {
	   instr_=0 ;
   };
} ;

void PlayerChannel::CutIfPlaying(I_Instrument *instr) {
	if (instr_!=instr) return ;
	instr_->Stop(index_) ;
	instr_=0 ;
	releasing_=false ;
	declickPending_=true ;
} ;

void PlayerChannel::StopInstrument() {
     if (instr_) {
       instr_->Stop(index_) ;
       // An instrument with a release stage is not finished yet: hold
       // on to it and keep rendering until its envelope runs out.
       // Render lets go once IsReleasing goes false.
       if (instr_->IsReleasing(index_)) {
         releasing_=true ;
         return ;
       }
       // no release: the waveform is cut wherever it stood, and the
       // size of the jump is only known once the next block is rendered
       declickPending_=true ;
     }
     instr_=0 ;
     releasing_=false ;
} ;


/* Smooth the jump between the last sample the converter saw and the
   first sample of this block. Measuring it here (instead of assuming
   the new voice starts at silence) keeps a retrigger into a similar
   level free, and stops fast rolls from compounding tails. */
void PlayerChannel::applyDeclick(fixed *buffer, int samplecount) {

    if (samplecount <= 0) return;

    if (declickPending_) {
        declickPending_ = false;
        for (int c = 0; c < 2; c++) {
            fixed jump = lastOut_[c] - buffer[c];
            if (jump > CHANNEL_CLICK_MAX) jump = CHANNEL_CLICK_MAX;
            if (jump < -CHANNEL_CLICK_MAX) jump = -CHANNEL_CLICK_MAX;
            click_[c] = jump;
        }
    }

    if (click_[0] || click_[1]) {
        for (int n = 0; n < samplecount; n++) {
            int idx = n * 2;
            for (int c = 0; c < 2; c++) {
                if (!click_[c]) continue;
                buffer[idx + c] += click_[c];
                click_[c] -= (click_[c] >> 5);   // ~0.7ms decay
                if (click_[c] > -32 && click_[c] < 32) click_[c] = 0;
            }
        }
    }

    lastOut_[0] = buffer[(samplecount - 1) * 2];
    lastOut_[1] = buffer[(samplecount - 1) * 2 + 1];
}

// Per-channel phaser + chorus, a serial insert on the post-fader signal.
// Fixed-point throughout: pcm ints, Q15 coefficients, Q32 LFO phase,
// the chorus tap in 24.8 -- the only float is the two per-BLOCK rate
// mappings. Skipped whole when both depths are zero, so a channel with
// no inserts pays nothing; the chorus line is allocated on first use.
void PlayerChannel::applyInserts(fixed *buffer, int samplecount) {
    bool doPh = (phaserDepth_ > 0);
    bool doCh = (chorusDepth_ > 0);
    if (!doPh && !doCh) return;

    if (doCh && !chBuf_) {
        chBuf_ = (short *)malloc(CHORUS_LEN * 2 * sizeof(short));
        if (chBuf_) chClear_ = true;
        else doCh = false;               // heap tight: chorus waits
    }
    if (doCh && chClear_) {
        memset(chBuf_, 0, CHORUS_LEN * 2 * sizeof(short));
        chClear_ = false;
    }
    if (!doPh && !doCh) return;

    // rate 0..255 -> ~0.12..3.9 Hz (phaser), ~0.10..2.7 Hz (chorus);
    // Q32 phase increments, computed once a block
    unsigned int phInc =
        (unsigned int)((0.12f + phaserRate_ * (3.8f / 255.0f)) *
                       (4294967296.0f / 44100.0f));
    unsigned int chInc =
        (unsigned int)((0.10f + chorusRate_ * (2.6f / 255.0f)) *
                       (4294967296.0f / 44100.0f));
    int phMix = phaserDepth_ << 7;       // Q15
    int chMix = chorusDepth_ << 7;

    for (int n = 0; n < samplecount; n++) {
        // triangle from the top of the phase word, 0..32767
        int pt = (int)(phLfoPh_ >> 16);
        int phTri = (pt < 32768) ? pt : (65535 - pt);
        int ct = (int)(chLfoPh_ >> 16);
        int chTri = (ct < 32768) ? ct : (65535 - ct);
        // all-pass sweep 0.15..0.85 in Q15
        int coef = 4915 + (int)(((long long)22938 * phTri) >> 15);
        // delay ~10..17.5ms in 24.8 samples
        int d8 = (441 << 8) + (int)(((long long)(330 << 8) * chTri) >> 15);

        for (int c = 0; c < 2; c++) {
            int x = fp2i(buffer[n * 2 + c]);
            if (doPh) {
                int s = x;
                for (int k = 0; k < 4; k++) {
                    int y = phZ_[k][c] - (int)(((long long)coef * s) >> 15);
                    phZ_[k][c] = s + (int)(((long long)coef * y) >> 15);
                    s = y;
                }
                x = x + (int)(((long long)(s - x) * phMix) >> 15);
            }
            if (doCh) {
                int w = x;
                if (w > 32767) w = 32767;
                if (w < -32768) w = -32768;
                chBuf_[(chPos_ << 1) + c] = (short)w;
                int rp = ((chPos_ << 8) - d8) & ((CHORUS_LEN << 8) - 1);
                int i0 = rp >> 8, frac = rp & 255;
                int i1 = (i0 + 1) & (CHORUS_LEN - 1);
                int tap = (chBuf_[(i0 << 1) + c] * (256 - frac) +
                           chBuf_[(i1 << 1) + c] * frac) >> 8;
                x = x + ((tap * chMix) >> 15);
            }
            if (x > 32767) x = 32767;
            if (x < -32768) x = -32768;
            buffer[n * 2 + c] = i2fp(x);
        }
        phLfoPh_ += phInc;
        chLfoPh_ += chInc;
        chPos_ = (chPos_ + 1) & (CHORUS_LEN - 1);
    }
}

bool PlayerChannel::Render(fixed *buffer,int samplecount) {

   // A muted channel used to render its instrument in FULL and then
   // throw the result away: the mute was applied after the synth had
   // already done all the work. Muting saved only the channel strip,
   // and soloing saved nothing at all, so eight voices ran to hear
   // one. The synth is the expensive part of this by a wide margin,
   // which is why soloing a channel to find out what was overloading
   // the machine reported almost the same load as the whole song.
   //
   // Nothing downstream of this point can be heard while muted, the
   // declick tail and the effect sends included, so there is nothing
   // worth computing. The voice stops advancing while it is silent
   // and picks up where it left off when it comes back; the next
   // note-on resyncs it either way, because notes are started by the
   // player and not by the instrument.
   /* Muted and already silent -- the cheap path, and the reason
      soloing a channel to find what is overloading the machine costs
      nothing. A muted channel with no instrument can never ramp down
      because it never renders, so it is declared silent here rather
      than waiting for a fade that will not happen. */
   if (muted_ && (curGain23_==0 || !instr_)) {
       curGain23_=0 ;
       /* Nothing is rendered while muted, so as far as everything
          downstream is concerned this channel's last output was
          SILENCE. Record that, and arm the corrector.

          It used to just return, leaving lastOut_ holding whatever
          the channel was at when the mute landed. On the way back the
          corrector then measured against that stale level and
          "corrected" toward a value the converter had not seen for as
          long as the mute lasted -- reproducing the pre-mute sample as
          a step instead of removing one. Measured worst case over a
          full cycle of mute phases: 18.1% of full scale.

          Radium makes the general form of this point: mute and solo
          are not switches there, they are a gain ramped to zero, and
          nothing in its graph is ever allowed to change level in one
          step. We cannot ramp what we are not rendering -- the early
          return is what makes muting cheap, and soloing a channel to
          find what is overloading the machine depends on it -- but we
          can at least tell the truth about where the signal was, so
          the way back is smoothed rather than snapped. */
       lastOut_[0]=0 ;
       lastOut_[1]=0 ;
       declickPending_=true ;
       return false ;
   }

   if (instr_) {
     bool tableSlice=SyncMaster::GetInstance()->TableSlice() ;
     bool status=instr_->Render(index_,buffer,samplecount,tableSlice) ;
     if (releasing_&&(!instr_->IsReleasing(index_))) {
         // The release ran out on its own, so let the instrument go.
         // No declick: the envelope reached zero, there is no step.
         instr_=0 ;
         releasing_=false ;
     }
     if (status) {
         // One pass, not three.
         //
         // The high pass, the low pass and the fader each used to walk
         // the whole block on their own, so a channel with both
         // filters on read and wrote every sample three times over.
         // The arithmetic below is the same operations in the same
         // order as before, per sample, with the same state updates:
         // what changes is that a sample is loaded once, carried
         // through the strip in registers, and stored once. On a
         // machine with this little cache, the passes were costing
         // more than the maths in them.
         //
         // The flags are hoisted, so the branches inside are on
         // values that do not change for the length of the block.
         const bool doHpf = (hpfMode_ != 0);
         const bool doLpf = (lpfFreq_ != 0);
         /* The fader ramps; the note snaps.

            Every gain change here used to be a hard step: chanGain was
            hoisted out of the loop, so a fader move, a mute or a solo
            changed level between one block and the next -- a
            discontinuity every 5.8ms whenever a control moved.

            The ramp is exactly ONE BLOCK long, which is MilkyTracker's
            shape and it is chosen rather than inherited: a ramp that
            always completes at the block boundary needs no split loop
            and no per-sample branch, and the stale-target hazard --
            correcting toward a value that was never emitted -- cannot
            arise, because the target is always reached before the next
            one arrives. Schism's "free once the ramp is done" then
            falls out of the outer guard rather than an inner split.

            Velocity is deliberately NOT ramped. It is set immediately
            before the note starts, so ramping the combined gain would
            scale a new note's first milliseconds by the OUTGOING
            note's velocity -- softening every attack, and re-treating
            a step applyDeclick already measures and cancels. */
         const fixed velGain = (velocity_ == i2fp(1))
                                   ? volume_
                                   : fp_mul(volume_, velocity_);
         // Q15 -> Q23. See curGain23_: at Q15 a one-step fader move
         // spread over a block truncates to a step of zero.
         const int targetGain23 = muted_ ? 0 : (int)(velGain << 8);
         if (gainSnap_) {
             curGain23_ = targetGain23;
             gainSnap_ = false;
         }
         const int gainStep = (samplecount > 0)
                                  ? (targetGain23 - curGain23_) / samplecount
                                  : 0;
         const int gainUnity = (1 << 23);
         const bool doGain = (curGain23_ != gainUnity) ||
                             (targetGain23 != gainUnity);
         int g23 = curGain23_;

         if (doHpf || doLpf || doGain) {
             const fixed one_minus_alpha = fp_sub(i2fp(1), lpfAlpha_);
             for (int n = 0; n < samplecount; n++) {
                 const int idx = n * 2;
                 fixed l = buffer[idx];
                 fixed r = buffer[idx + 1];

                 if (doHpf) {
                     const fixed in_l = l, in_r = r;
                     l = fp_mul(hpfAlpha_, fp_add(hpfPrevOutput_[0],
                                                  fp_sub(in_l, hpfPrevInput_[0])));
                     r = fp_mul(hpfAlpha_, fp_add(hpfPrevOutput_[1],
                                                  fp_sub(in_r, hpfPrevInput_[1])));
                     hpfPrevInput_[0] = in_l;
                     hpfPrevInput_[1] = in_r;
                     hpfPrevOutput_[0] = l;
                     hpfPrevOutput_[1] = r;
                 }

                 if (doLpf) {
                     l = fp_add(fp_mul(lpfAlpha_, l),
                                fp_mul(one_minus_alpha, lpfPrevOutput_[0]));
                     r = fp_add(fp_mul(lpfAlpha_, r),
                                fp_mul(one_minus_alpha, lpfPrevOutput_[1]));
                     lpfPrevOutput_[0] = l;
                     lpfPrevOutput_[1] = r;
                 }

                 if (doGain) {
                     l = (fixed)(((long long)l * g23) >> 23);
                     r = (fixed)(((long long)r * g23) >> 23);
                     g23 += gainStep;
                 }

                 buffer[idx] = l;
                 buffer[idx + 1] = r;
             }
         }
         /* Whatever happened above, the block ends at the target: the
            ramp is one block long by construction. Assigning rather
            than trusting the accumulator also absorbs the truncation
            in gainStep, so the gain cannot drift away over time. */
         curGain23_ = targetGain23;

         applyDeclick(buffer, samplecount);

         // Per-channel phaser/chorus, before the send tap so the
         // modulated sound is what reaches master and the effects.
         applyInserts(buffer, samplecount);

         // The send tap. It sits at the end of the channel strip for
         // the same reason it does on a desk: what goes to the
         // effects is what the channel actually sounds like, after
         // its filters and its fader -- not the raw instrument.
         if (delaySend_ > 0 || reverbSend_ > 0) {
             SendFx::Accumulate(buffer, samplecount, delaySend_,
                                reverbSend_);
         }
     } else {
         // nothing audible from the instrument: the tail is all there
         // is to render, and it still has to ring out
         if (declickPending_ || click_[0] || click_[1]) {
             for (int i = 0; i < samplecount * 2; i++) buffer[i] = 0;
             applyDeclick(buffer, samplecount);
             return true;
         }
     }
     return status ;
   } else {
       // no instrument at all (note killed, nothing retriggered): the
       // cut still left a step, so ring its tail out here
       if (declickPending_ || click_[0] || click_[1]) {
           for (int i = 0; i < samplecount * 2; i++) buffer[i] = 0;
           applyDeclick(buffer, samplecount);
           return true;
       }
       return false;
   }
} ;

I_Instrument *PlayerChannel::GetInstrument() {
   return instr_ ;
} ;

void PlayerChannel::SetMute(bool muted) {
     muted_=muted ;
}

bool PlayerChannel::IsMuted() { return muted_; }

void PlayerChannel::SetVolume(fixed volume) { volume_ = volume; };

void PlayerChannel::SetHPFMode(unsigned char mode) {
    if (hpfMode_ == mode)
        return;
    hpfMode_ = mode;
    // reset state
    hpfPrevInput_[0] = hpfPrevInput_[1] = i2fp(0);
    hpfPrevOutput_[0] = hpfPrevOutput_[1] = i2fp(0);
    if (hpfMode_ == 0) {
        hpfAlpha_ = i2fp(0);
        return;
    }
    // compute alpha for one-pole HPF: alpha = RC/(RC+dt), RC=1/(2*pi*fc),
    // dt=1/fs
    float fc = (hpfMode_ == 1) ? 20.0f : 90.0f;
    float fs = 44100.0f;
    const float PI = 3.14159265358979323846f;
    float RC = 1.0f / (2.0f * PI * fc);
    float dt = 1.0f / fs;
    float alpha = RC / (RC + dt);
    hpfAlpha_ = fl2fp(alpha);
}

void PlayerChannel::SetLPFFreq(unsigned short freq) {
    if (lpfFreq_ == freq)
        return;
    lpfFreq_ = freq;
    lpfPrevOutput_[0] = lpfPrevOutput_[1] = i2fp(0);
    if (lpfFreq_ == 0) {
        lpfAlpha_ = i2fp(0);
        return;
    }
    // compute alpha for one-pole LPF: alpha = dt/(RC+dt), RC=1/(2*pi*fc),
    // dt=1/fs
    float fc = (float)lpfFreq_;
    float fs = 44100.0f;
    const float PI = 3.14159265358979323846f;
    float RC = 1.0f / (2.0f * PI * fc);
    float dt = 1.0f / fs;
    float alpha = dt / (RC + dt);
    lpfAlpha_ = fl2fp(alpha);
}

void PlayerChannel::SetMixBus(int i) {

	if (i==busIndex_) return ;

	if (mixBus_) {
		mixBus_->Remove(*this) ;
	}
    busIndex_ = i;
    mixBus_=MixerService::GetInstance()->GetMixBus(i) ;
	if (mixBus_) {
		mixBus_->Insert(*this) ;
	}
} ;

void PlayerChannel::Reset() {
    if (mixBus_) {
        mixBus_->Remove(*this) ;
    }
    muted_=false ;
  busIndex_=-1 ;
  hpfPrevInput_[0]=hpfPrevInput_[1]=i2fp(0);
  hpfPrevOutput_[0]=hpfPrevOutput_[1]=i2fp(0);
  hpfAlpha_ = i2fp(0);
  hpfMode_ = 0;
  lpfPrevOutput_[0] = lpfPrevOutput_[1] = i2fp(0);
  lpfAlpha_ = i2fp(0);
  lpfFreq_ = 0;
  /* Reset the gain and declick state too. This did not, so a channel
     carried its fader, its velocity, its last emitted sample and a
     half-decayed click correction across a project load into a song
     that had said nothing about any of them. */
  volume_ = i2fp(1);
  velocity_ = i2fp(1);
  curGain23_ = (1 << 23);
  gainSnap_ = true;
  lastOut_[0] = lastOut_[1] = 0;
  click_[0] = click_[1] = 0;
  declickPending_ = false;
};
