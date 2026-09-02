#ifndef _WAV_FILE_WRITER_H_
#define _WAV_FILE_WRITER_H_

#include "System/FileSystem/FileSystem.h"
#include "Application/Utils/fixed.h"

class WavFileWriter {
public:
	// the render tap: stereo at the driver rate, fixed-point blocks
	WavFileWriter(const char *path) ;
	// any shape -- the import converter: channels and rate as given,
	// interleaved 16-bit frames
	WavFileWriter(const char *path,int channels,int rate) ;
	~WavFileWriter() ;
	bool IsOpen() { return file_!=0 ; } ;
	void AddBuffer(fixed *,int size) ; // size in stereo frames
	void AddShorts(const short *frames,int frameCount) ;
	void Close() ;
private:
	void open(const char *path,int channels,int rate) ;
	// shorts into the pending buffer, out to the card in lumps
	void queue(const short *s,int count) ;
	void flush() ;

	I_File *file_ ;
	short *buffer_ ;        // fixed -> short conversion, render path only
	int bufferSize_ ;
	unsigned int dataBytes_ ;   // what the data chunk will claim
	int channels_ ;
	// Samples go here and reach the card in big lumps. Every audio
	// block used to be its own write: a few hundred bytes at a time,
	// straight from the render, onto a Memory Stick, which blocks
	// long enough for the render to miss its deadline.
	short *pending_ ;
	int pendingUsed_ ;      // in shorts
} ;
#endif
