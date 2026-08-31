#include "Phrase.h"
#include "Application/Instruments/CommandList.h"
#include "System/System/System.h"
#include <stdlib.h>
#include <string.h>

Phrase::Phrase() {

	int size=PHRASE_COUNT*16 ; // PHRASE_COUNT phrases of 0x10 steps
	note_=(unsigned char *)SYS_MALLOC(size) ;
	memset(note_,0xFF,size) ;
	instr_=(unsigned char *)SYS_MALLOC(size) ;
	memset(instr_,0xFF,size) ;
	velocity_=(unsigned char *)SYS_MALLOC(size) ;
	memset(velocity_,VELOCITY_EMPTY,size) ;

	cmd1_=(FourCC *)SYS_MALLOC(size*sizeof(FourCC)) ;
	memset(cmd1_,'-',size*sizeof(FourCC)) ;
	param1_=(unsigned short *)SYS_MALLOC(size*sizeof(short)) ;
	memset(param1_,0x00,size*sizeof(short)) ;

	cmd2_=(FourCC *)SYS_MALLOC(size*sizeof(FourCC)) ;
	memset(cmd2_,'-',size*sizeof(FourCC)) ;
	param2_=(unsigned short *)SYS_MALLOC(size*sizeof(short)) ;
	memset(param2_,0x00,size*sizeof(short)) ;

	for (int i=0;i<PHRASE_COUNT;i++) {
		isUsed_[i]=false ;
	}
} ;

Phrase::~Phrase() {
	if (note_) SYS_FREE(note_) ;
	if (instr_) SYS_FREE(instr_) ;
	if (velocity_) SYS_FREE(velocity_) ;
    /* CMDS_HERE
	if (cmd_) SYS_FREE(cmd_) ;
	if (cmdData_) SYS_FREE(cmdData_) ;
	*/
} ;

unsigned short Phrase::GetNext(int startAfter) {
	// startAfter>=0: hand back the first free slot AFTER it, wrapping --
	// so a clone lands next to its source instead of at 00. Default -1
	// keeps the plain lowest-free scan for every other caller.
	for (int j=0;j<PHRASE_COUNT;j++) {
		int i=(startAfter>=0)?((startAfter+1+j)%PHRASE_COUNT):j ;
		if (!isUsed_[i]) {
			isUsed_[i]=true ;
			// a NEW phrase is an EMPTY phrase, same rule as chains:
			// clones copy over this immediately and lose nothing
			for (int r=0;r<16;r++) {
				note_[16*i+r]=0xFF ;
				instr_[16*i+r]=0xFF ;
				velocity_[16*i+r]=VELOCITY_EMPTY ;
				cmd1_[16*i+r]=I_CMD_NONE ;
				param1_[16*i+r]=0 ;
				cmd2_[16*i+r]=I_CMD_NONE ;
				param2_[16*i+r]=0 ;
			}
			return i ;
		}
	}
	return NO_MORE_PHRASE ;
} ;

void Phrase::SetUsed(unsigned char c) {
	isUsed_[c]=true ;
}

void Phrase::ClearAllocation() {

	for (int i=0;i<PHRASE_COUNT;i++) {
		isUsed_[i]=false ;
	}
} ;
