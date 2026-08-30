#ifndef _CLOCK_SYNC_H_
#define _CLOCK_SYNC_H_

/********************************************************
 ClockSync:
    Keeps the song in step with an external MIDI clock.

    One MIDI clock is exactly one tracker tick -- both are
    twenty four to the quarter note -- so the leader's clock
    bytes and our own slices are the same unit and can simply
    be counted against each other. The difference between the
    two counts IS the phase error, in ticks, and counting is
    exact even when the timing of the bytes is not: the USB
    pump sleeps up to 20ms and hands over whatever arrived in
    bursts, which ruins a timestamp but cannot lose a count.

    That matters, because measuring the interval between
    quarters and setting a tempo from it -- which is what this
    used to do -- estimates the leader's SPEED and never its
    POSITION. A song started at the wrong moment stayed at the
    wrong moment for ever, and integer BPM meant it drifted on
    top of that: one step at 128bpm is 0.78%, which is 62ms
    over an eight second phrase.

    So this is a PI loop on phase. The proportional term pulls
    a phase error out quickly; the integral term is what
    actually learns the leader's tempo, because a standing
    error is exactly what a frequency mismatch produces. Drive
    phase to zero and frequency has to match.
 ********************************************************/

class ClockSync {
public:

	ClockSync() { leadMs_ = 0.0f ; Reset(120.0f) ; }

	/* Called on the leader's start byte: the two clocks are
	   declared equal here and the loop keeps them that way. */
	void Reset(float bpm) {
		leaderTicks_ = 0 ;
		playerTicks_ = 0 ;
		base_ = bpm ;
		tempo_ = bpm ;
		settle_ = 0 ;
		avgErr_ = 0.0f ;
		fastErr_ = 0.0f ;
		locked_ = false ;
		acqT0_ = 0 ;
		acqTick0_ = 0 ;
		acqDone_ = false ;
		grossRun_ = 0 ;
	}

	/* nowMs: the machine clock at the byte's arrival, 0 if unknown.
	   With it, the first beat's worth of ticks is timed and the base
	   tempo SNAPPED to the measurement -- a song saved at 120 under a
	   140 leader used to crawl there through the integral for tens of
	   seconds, audibly flat the whole way. One beat of listening gets
	   within a hair and the loop polishes the rest. */
	void OnLeaderTick(unsigned long nowMs = 0) {
		leaderTicks_++ ;
		if (nowMs && !acqDone_) {
			if (!acqT0_) { acqT0_ = nowMs ; acqTick0_ = leaderTicks_ ; }
			else if (leaderTicks_ - acqTick0_ >= 24) {
				unsigned long el = nowMs - acqT0_ ;
				if (el > 100) {
					float m = 2500.0f * float(leaderTicks_ - acqTick0_)
					          / float(el) ;
					if (m < 30.0f) m = 30.0f ;
					if (m > 400.0f) m = 400.0f ;
					if (m - base_ > 3.0f || base_ - m > 3.0f) base_ = m ;
				}
				acqDone_ = true ;
			}
		}
		update() ;
	}
	void OnPlayerTick() { playerTicks_++ ; }

	/* How far AHEAD of the leader's count the song should run,
	   in milliseconds.

	   Locking the two counts together is not the same as the
	   two being heard together. What we align is the moment the
	   player decides a tick; what anyone hears is that tick
	   emerging from the audio buffer some time later -- 35ms at
	   the default 256 frames and six buffers deep. The clock
	   byte that triggered it was itself late by however long it
	   spent getting through USB.

	   So the target is not zero phase, it is a lead: run the
	   song early by what the chain costs, and it comes out on
	   time. The number cannot be worked out from here -- the
	   leader's own output latency is part of it -- so what is
	   known is filled in and the rest is left to be dialled. */
	void SetLeadMs(float ms) { leadMs_ = ms ; }
	float LeadMs() const { return leadMs_ ; }

	/* Ticks the leader is ahead of where we ought to be.
	   Positive means we are late and have to hurry. */
	float PhaseError() const {
		return (float)leaderTicks_ - (float)playerTicks_ + leadTicks() ;
	}

	float Tempo() const { return tempo_ ; }

	/* True once the loop has held the error inside a tick for
	   long enough to be believed -- for the screen, not for the
	   maths. */
	bool Locked() const { return locked_ ; }

private:

	void update() {

		float err = PhaseError() ;

		/* A burst of clocks arriving at once, or a leader that
		   jumped, should not throw the tempo across the room.
		   Beyond this the error is not a phase error any more,
		   it is a different position. */
		if (err > 24.0f) err = 24.0f ;
		if (err < -24.0f) err = -24.0f ;

		/* A position that stays pinned at the clamp for four straight
		   beats is not going to be chased down: something pathological
		   happened (a stall, a leader jump). Re-anchor the counts and
		   carry on at the learned tempo -- wrong phase accepted once
		   beats a permanently skewed tempo trying to close a beat that
		   keeps receding. */
		if (err >= 24.0f || err <= -24.0f) {
			if (++grossRun_ > 96) {
				playerTicks_ = leaderTicks_ ;
				fastErr_ = 0.0f ;
				avgErr_ = 0.0f ;
				settle_ = 0 ;
				grossRun_ = 0 ;
				tempo_ = base_ ;
				return ;
			}
		} else {
			grossRun_ = 0 ;
		}

		/* The integral learns the tempo, and it has to be fed a
		   SMOOTHED error rather than the raw one.

		   The two tick counts are whole numbers but the lead is
		   not -- 35ms at 120bpm is 1.68 ticks -- so the raw error
		   can never actually reach zero. Integrating it directly
		   made the loop hunt between the two nearest whole
		   positions for ever, swinging the tempo about 1.5% and
		   never settling.

		   Averaging recovers what is below the quantiser: the
		   loop dithers across the two positions, and the mean of
		   that dither is the true fractional offset. */
		avgErr_ += (err - avgErr_) * 0.0625f ;
		base_ += 0.005f * avgErr_ ;
		if (base_ < 30.0f) base_ = 30.0f ;
		if (base_ > 400.0f) base_ = 400.0f ;

		/* The proportional term closes the current gap -- fed a FAST
		   smoothing of the error, not the raw count difference. The
		   USB pump hands clock bytes over in clumps of up to 20ms, so
		   the raw error square-waves by two or three ticks at clump
		   rate; a P term chasing that wobbled the tempo a few percent
		   fifty times a second, which is audible wow that no amount of
		   integral could remove. Smoothing at a quarter per tick keeps
		   the response inside a few ticks while the clumps average
		   away. */
		fastErr_ += (err - fastErr_) * 0.25f ;
		tempo_ = base_ * (1.0f + 0.010f * fastErr_) ;
		if (tempo_ < 30.0f) tempo_ = 30.0f ;
		if (tempo_ > 400.0f) tempo_ = 400.0f ;

		if (avgErr_ > -1.0f && avgErr_ < 1.0f) {
			if (settle_ < 255) settle_++ ;
		} else {
			settle_ = 0 ;
		}
		locked_ = (settle_ > 24) ;
	}

	/* The gains are written into update() above rather than kept
	   as named constants, so this stays a header with nothing to
	   link against.

	   They were chosen by measuring, not by feel: the thing that
	   matters is how far the audio ends up from the leader in
	   MILLISECONDS, and a faster proportional term wins on that
	   even though it makes the tempo readout wobble more. At
	   0.010 the loop holds a song saved at the wrong tempo to
	   within about 6ms of the leader and catches it in a few
	   bars; at 0.003 the readout is steadier and the timing is
	   half as good, which is the wrong trade for a sequencer. */

	/* The lead in ticks depends on the tempo, since a tick is
	   shorter at 160 than at 90. Recomputed rather than stored
	   so a tempo change does not leave it stale.

	   The +1: one full slice sits between DECIDING a tick and its
	   audio joining the queue, and a slice IS a tick long -- a
	   tempo-dependent latency the millisecond lead cannot carry
	   (it would need re-trimming at every tempo). In tick units it
	   is exactly one, at any tempo, so it lives here. */
	float leadTicks() const { return leadMs_ * tempo_ / 2500.0f + 1.0f ; }

	unsigned int leaderTicks_ ;
	unsigned int playerTicks_ ;
	float base_ ;
	float avgErr_ ;
	float fastErr_ ;
	unsigned long acqT0_ ;
	unsigned int acqTick0_ ;
	bool acqDone_ ;
	unsigned int grossRun_ ;
	float leadMs_ ;
	float tempo_ ;
	unsigned char settle_ ;
	bool locked_ ;
} ;

#endif
