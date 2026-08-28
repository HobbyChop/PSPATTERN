#ifndef _PHRASE_H_
#define _PHRASE_H_

#include "Foundation/Types/Types.h"
#define PHRASE_COUNT 0xFF
// Velocity reads 00..99 on screen and is stored the same way -- not
// 00..7F. Two characters either way, and one of them is a number
// people already know how to think in.
#define VELOCITY_FULL  99
#define VELOCITY_EMPTY 0xFF
#define NO_MORE_PHRASE 0x100

class Phrase {
public:
	Phrase() ;
	~Phrase() ;
	unsigned short GetNext(int startAfter=-1) ;
	bool IsUsed(uchar i) { return isUsed_[i] ; } ;
	void SetUsed(uchar c) ;
	void ClearAllocation() ;

	uchar *note_ ;
	uchar *instr_ ;
	// 0..VELOCITY_FULL, or VELOCITY_EMPTY for "no dynamic written
	// here". Empty plays at full, so a song that predates this column
	// sounds exactly as it did.
	uchar *velocity_ ;
	FourCC *cmd1_ ;
	ushort *param1_ ;
	FourCC *cmd2_ ;
	ushort *param2_ ;
	
private:
	bool isUsed_[PHRASE_COUNT] ;

} ;

#endif
