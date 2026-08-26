#ifndef _WAV_FILE_WRITER_H_
#define _WAV_FILE_WRITER_H_

#include "System/FileSystem/FileSystem.h"
#include "Application/Utils/fixed.h"

class WavFileWriter {
public:
	WavFileWriter(const char *path) ;
	~WavFileWriter() ;
	void AddBuffer(fixed *,int size) ; // size in samples
	void Close() ;
private:
	void flush() ;

	int sampleCount_ ;
	short *buffer_ ;
	int bufferSize_ ;
	// Samples go here and reach the card in big lumps. Every audio
	// block used to be its own write: a few hundred bytes at a time,
	// straight from the render, onto a Memory Stick, which blocks
	// long enough for the render to miss its deadline.
	short *pending_ ;
	int pendingUsed_ ;      // in shorts
	I_File *file_ ;
} ;
#endif
