
#include "PlayerChannel.h"
#include "Services/Audio/SendFx.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Utils/fixed.h"
#include <math.h>

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
    hpfPrevInput_[0] = hpfPrevInput_[1] = i2fp(0);
    hpfPrevOutput_[0] = hpfPrevOutput_[1] = i2fp(0);
    hpfAlpha_ = i2fp(0);
    hpfMode_ = 0;
    lpfPrevOutput_[0] = lpfPrevOutput_[1] = i2fp(0);
    lpfAlpha_ = i2fp(0);
    lpfFreq_ = 0;
    lastOut_[0] = lastOut_[1] = 0;
    click_[0] = click_[1] = 0;
    declickPending_ = false;
}

// a tail can never exceed the full swing of the converter
#define CHANNEL_CLICK_MAX (i2fp(32000))

PlayerChannel::~PlayerChannel() {
}

void PlayerChannel::StartInstrument(I_Instrument *instr,unsigned char note,bool trigger) {
   // Whether this note follows one of its own is about to be erased by
   // the stop below, and it's the only place that knows. Same
   // instrument only -- sliding out of some other instrument's note is
   // not what anyone means by glide.
   // A note that follows a release tail is not a legato note -- the
   // previous one already ended.
   bool followsOwnNote=(instr_==instr)&&(!releasing_) ;

   // StopInstrument flags the cut; the jump it leaves behind is
   // measured and smoothed when the next block renders
   if (instr_) {
      StopInstrument() ;
   } else {
      declickPending_=true ;   // first note after silence still steps
   }
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

bool PlayerChannel::Render(fixed *buffer,int samplecount) {
   if (instr_) {
     bool tableSlice=SyncMaster::GetInstance()->TableSlice() ;
     bool status=instr_->Render(index_,buffer,samplecount,tableSlice) ;
     if (releasing_&&(!instr_->IsReleasing(index_))) {
         // The release ran out on its own, so let the instrument go.
         // No declick: the envelope reached zero, there is no step.
         instr_=0 ;
         releasing_=false ;
     }
     if (status && !muted_) {
         // Apply HPF if enabled
         if (hpfMode_ != 0) {
             for (int n = 0; n < samplecount; n++) {
                 int idx = n * 2;
                 fixed in_l = buffer[idx];
                 fixed in_r = buffer[idx + 1];
                 fixed out_l =
                     fp_mul(hpfAlpha_, fp_add(hpfPrevOutput_[0],
                                              fp_sub(in_l, hpfPrevInput_[0])));
                 fixed out_r =
                     fp_mul(hpfAlpha_, fp_add(hpfPrevOutput_[1],
                                              fp_sub(in_r, hpfPrevInput_[1])));
                 buffer[idx] = out_l;
                 buffer[idx + 1] = out_r;
                 hpfPrevInput_[0] = in_l;
                 hpfPrevInput_[1] = in_r;
                 hpfPrevOutput_[0] = out_l;
                 hpfPrevOutput_[1] = out_r;
             }
         }

         // Apply LPF if enabled
         if (lpfFreq_ != 0) {
             fixed one_minus_alpha = fp_sub(i2fp(1), lpfAlpha_);
             for (int n = 0; n < samplecount; n++) {
                 int idx = n * 2;
                 fixed in_l = buffer[idx];
                 fixed in_r = buffer[idx + 1];
                 fixed out_l = fp_add(fp_mul(lpfAlpha_, in_l),
                                      fp_mul(one_minus_alpha, lpfPrevOutput_[0]));
                 fixed out_r = fp_add(fp_mul(lpfAlpha_, in_r),
                                      fp_mul(one_minus_alpha, lpfPrevOutput_[1]));
                 buffer[idx] = out_l;
                 buffer[idx + 1] = out_r;
                 lpfPrevOutput_[0] = out_l;
                 lpfPrevOutput_[1] = out_r;
             }
         }

         // Apply per-channel volume, and the velocity of the note
         // that is sounding. Folded into one multiply: two passes over
         // the block to apply two gains is a pass too many on this
         // machine, and the product is exact in Q15 either way.
         fixed chanGain = (velocity_ == i2fp(1)) ? volume_
                                                 : fp_mul(volume_, velocity_);
         if (chanGain != i2fp(1)) {
             for (int i = 0; i < samplecount * 2; i++) {
                 buffer[i] = fp_mul(buffer[i], chanGain);
             }
         }

         applyDeclick(buffer, samplecount);

         // The send tap. It sits at the end of the channel strip for
         // the same reason it does on a desk: what goes to the
         // effects is what the channel actually sounds like, after
         // its filters and its fader -- not the raw instrument.
         if (!muted_ && (delaySend_ > 0 || reverbSend_ > 0)) {
             SendFx::Accumulate(buffer, samplecount, delaySend_,
                                reverbSend_);
         }
     } else {
         // nothing audible from the instrument: the tail is all there
         // is to render, and it still has to ring out
         if (declickPending_ || click_[0] || click_[1]) {
             for (int i = 0; i < samplecount * 2; i++) buffer[i] = 0;
             applyDeclick(buffer, samplecount);
             return !muted_;
         }
     }
     return (status && !muted_);
   } else {
       // no instrument at all (note killed, nothing retriggered): the
       // cut still left a step, so ring its tail out here
       if (declickPending_ || click_[0] || click_[1]) {
           for (int i = 0; i < samplecount * 2; i++) buffer[i] = 0;
           applyDeclick(buffer, samplecount);
           return !muted_;
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
};
