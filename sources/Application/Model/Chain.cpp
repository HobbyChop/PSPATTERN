#include "Chain.h"
#include "System/System/System.h"
#include <stdlib.h>
#include <string.h>

Chain::Chain() {

	int size=CHAIN_COUNT*16 ;
	data_=(unsigned char *)SYS_MALLOC(size) ;
	memset(data_,0xFF,size) ;
	transpose_=(unsigned char *)SYS_MALLOC(size) ;
	memset(transpose_,0x00,size) ;
	for (int i=0;i<CHAIN_COUNT;i++) {
		isUsed_[i]=false ;
	}
} ;

Chain::~Chain() {
	if (data_) SYS_FREE(data_) ;
	if (transpose_) SYS_FREE(transpose_) ;
};

unsigned short Chain::GetNext(int startAfter) {
	// startAfter>=0: first free slot AFTER it, wrapping, so a clone lands
	// next to its source instead of at 00. Default -1 = lowest-free scan.
	for (int j=0;j<CHAIN_COUNT;j++) {
		int i=(startAfter>=0)?((startAfter+1+j)%CHAIN_COUNT):j ;
		if (!isUsed_[i]) {
			isUsed_[i]=true ;
			/* A NEW chain is an EMPTY chain. Allocation only ever
			   flipped the flag, so a number that had held rows -- a
			   cut chain, one freed by compacting -- came back with
			   yesterday's contents, and drilling into a fresh song
			   slot landed on someone's old phrases. Clones copy over
			   this immediately, so they lose nothing. */
			for (int r=0;r<16;r++) {
				data_[16*i+r]=0xFF ;
				transpose_[16*i+r]=0 ;
			}
			return i ;
		}
	}
	return NO_MORE_CHAIN ;
} ;

void Chain::SetUsed(unsigned char c) {
	isUsed_[c]=true ;
}

void Chain::ClearAllocation() {

	for (int i=0;i<CHAIN_COUNT;i++) {
		isUsed_[i]=false ;
	}
}
