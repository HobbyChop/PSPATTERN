#ifndef _WAV_FILE_WRITER_H_
#define _WAV_FILE_WRITER_H_

#include "System/FileSystem/FileSystem.h"
#include "Application/Utils/fixed.h"

class SysSemaphore ;
class WavWriteThread ;

class WavFileWriter {
public:
	// the render tap: stereo at the driver rate, fixed-point blocks.
	// Reaches the card from a thread of its own -- see the ring below.
	WavFileWriter(const char *path) ;
	// any shape -- the import converter: channels and rate as given,
	// interleaved 16-bit frames, written from the calling thread
	WavFileWriter(const char *path,int channels,int rate) ;
	~WavFileWriter() ;
	bool IsOpen() { return file_!=0 ; } ;
	void AddBuffer(fixed *,int size) ; // size in stereo frames
	void AddShorts(const short *frames,int frameCount) ;
	// Drain, write the header and close the file, here and now. Blocks
	// for as long as the card takes to swallow what is still held, so
	// never from the audio thread while the driver runs.
	void Close() ;
	// The same, done by the writer thread: returns at once, and Done()
	// goes true when the file is complete -- delete the writer then.
	// Without a thread (the import shape) it closes here.
	void Finish() ;
	bool Done() { return done_ ; }
private:
	void open(const char *path,int channels,int rate) ;
	// shorts into the pending buffer, out to the card in lumps
	void queue(const short *s,int count) ;
	void flush() ;
	void finalize() ;
	bool startThread() ;
	void ringPut(const short *s,int count) ;
	void drain(bool all) ;
	void threadMain() ;
	friend class WavWriteThread ;

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

	/* The render path goes further: the lumps themselves have left
	   the render thread. A 32KB write to a Memory Stick takes tens of
	   milliseconds -- hundreds while the card does its own
	   housekeeping -- against a block budget of about six, and the
	   audio buffer's lead did not always cover it: a render came with
	   a scatter of underruns the same song never had when it was only
	   played. So the render fills a ring, and a thread at the UI's
	   priority drains it to the card in the same lumps. The ring
	   holds a second and a half, which is what a card's worst stall
	   needs. One producer, one consumer, each owning its own index. */
	short *ring_ ;
	int ringSize_ ;                 // in shorts
	volatile int ringRead_ ;        // the thread's
	volatile int ringWrite_ ;       // the render's
	SysSemaphore *wake_ ;
	WavWriteThread *thread_ ;
	volatile bool finishing_ ;
	volatile bool done_ ;
} ;
#endif
