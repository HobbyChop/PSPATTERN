
#ifndef _PLAYER_CHANNEL_H_
#define _PLAYER_CHANNEL_H_

#include "Services/Audio/AudioModule.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Mixer/MixBus.h"

class PlayerChannel: public AudioModule {
public:
	PlayerChannel(int index) ;
	virtual ~PlayerChannel() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	void StartInstrument(I_Instrument *instr,unsigned char note,bool cleanStart) ;
	void StopInstrument() ;
	I_Instrument *GetInstrument() ;
	void SetMute(bool muted) ;
	// 0..255 each: how much of this channel is copied into the shared
	// delay and reverb. Taken post-fader and post-declick, so pulling
	// a channel down takes its tail with it and a cut note does not
	// leave a click ringing in the reverb.
	void SetSends(int delay,int reverb) { delaySend_=delay ; reverbSend_=reverb ; }
	// The velocity of the note currently sounding, as a Q15 gain.
	// Separate from volume_, which is the channel fader: one is set
	// per note by the pattern, the other by the mixer, and they
	// multiply the way a desk and a performance do.
	void SetVelocity(fixed v) { velocity_=v ; }
	bool IsMuted() ;
	void SetMixBus(int i) ;
    void SetVolume(fixed volume);
    void SetHPFMode(unsigned char mode);
    void ApplyHPF(fixed *buffer, int samplecount);
    void applyDeclick(fixed *buffer, int samplecount);
    void SetLPFFreq(unsigned short freq);
    void Reset();

  private:
	int index_ ;
	I_Instrument *instr_ ;
	// the instrument was stopped but is still ringing out its release
	bool releasing_ ;
	bool muted_ ;
	fixed velocity_ ;
	int delaySend_ ;
	int reverbSend_ ;
    fixed volume_;
    int busIndex_ ;
	MixBus *mixBus_ ;
    fixed hpfPrevInput_[2];
    fixed hpfPrevOutput_[2];
	fixed hpfAlpha_;
	unsigned char hpfMode_;
    fixed lpfPrevOutput_[2];
    fixed lpfAlpha_;
    unsigned short lpfFreq_;
    // note-on declick: channels are monophonic, so a new note cuts
    // whatever was still ringing. The jump from the last sample the
    // DAC saw to the first sample of the new voice is measured at
    // render time and carried as a fast-decaying DC tail instead of a
    // click. Measuring the real jump (rather than assuming the new
    // voice starts at zero) means a retrigger into a similar level
    // costs nothing and repeated triggers cannot compound.
    // (Upstream has couldClick_/SHOULD_KILL_CLICKS for this, but it is
    // hardcoded false and the flag is never read anywhere — dead code.)
    fixed lastOut_[2];
    fixed click_[2];
    bool declickPending_;

    /* The gain actually being emitted, in Q23, ramped toward its
       target across one block.

       Q23 and not Q15 for a reason that bites silently otherwise: a
       one-step fader move is 1/255, which in Q15 is a delta of 128,
       and 128 spread over a 256 sample block truncates to a step of
       ZERO. The ramp would stall at the old value and never arrive.
       Eight more fractional bits fixes it, and costs nothing -- the
       multiply is the same single MIPS mult, just a different
       compile-time shift.

       This has to record what was EMITTED, not the previous target,
       because three paths leave a block without passing through the
       gain stage: the strip loop is skipped entirely at unity, a
       muted channel emits nothing, and the declick tail can be the
       only thing rendered. lastOut_ cannot stand in for it -- a
       sample of zero is consistent with any gain at all. */
    int curGain23_;
    // Snap instead of ramping: a note start must not have the
    // outgoing note's velocity decaying across its attack.
    bool gainSnap_;
};

#endif
