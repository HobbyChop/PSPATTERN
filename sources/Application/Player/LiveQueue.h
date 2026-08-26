#ifndef _LIVE_QUEUE_H_
#define _LIVE_QUEUE_H_

/********************************************************
 LiveQueueSteps:
    How many steps are left before a queued channel
    switches.

    This lives on its own, away from the player, because it
    has to agree with the player exactly: moveToNextPhrase
    ends a phrase when its position would reach 16, and
    moveToNextChain ends a chain at its first empty slot.
    Get either rule wrong by one and the countdown reaches
    zero a step early, which is worse on stage than no
    countdown at all. Here it can be tested against those
    rules directly.

    phrasePos     current step within the playing phrase, 0..15
    chainPos      current slot within the playing chain, 0..15
    chainData     the playing chain's 16 slots, or 0 if none
    chainBoundary true when waiting for the chain to end
                  rather than just the phrase
 ********************************************************/

inline int LiveQueueSteps(int phrasePos, int chainPos,
                          const unsigned char *chainData,
                          bool chainBoundary) {

	int steps = 16 - phrasePos ;

	if (chainBoundary && chainData) {
		// Every phrase still standing in the chain plays out first.
		for (int pos = chainPos + 1 ; pos < 16 ; pos++) {
			if (chainData[pos] == 0xFF) break ;
			steps += 16 ;
		}
	}

	return steps ;
}

#endif
