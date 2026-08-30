
#include "WavFile.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"
#include "Services/Time/TimeService.h"
#include "Application/Model/Config.h"
#include <stdlib.h>

int WavFile::bufferChunkSize_=-1 ;
bool WavFile::initChunkSize_=true ;

short Swap16 (short from)
{
#ifdef __ppc__
	short result;
	((char*)&result)[0] = ((char*)&from)[1];
	((char*)&result)[1] = ((char*)&from)[0];
	return  result;
#else
	return from;
#endif	
}

int Swap32 (int from)
{
#ifdef __ppc__
	int result;
	((char*)&result)[0] = ((char*)&from)[3];
	((char*)&result)[1] = ((char*)&from)[2];
	((char*)&result)[2] = ((char*)&from)[1];
	((char*)&result)[3] = ((char*)&from)[0];			 
	return  result;
#else
	return from;
#endif 	
}


WavFile::WavFile(I_File *file) {
	if (initChunkSize_) {
		const char *size=Config::GetInstance()->GetValue("SAMPLELOADCHUNKSIZE") ;
		if (size) {
			bufferChunkSize_=atoi(size) ;
		}
		initChunkSize_=false;
	}
	samples_=0 ;
	size_=0 ;
	readBuffer_=0 ;
	readBufferSize_=0 ;
	sampleBufferSize_=0 ;
	file_=file ;
} ;

WavFile::~WavFile() {
	if (file_) {
		file_->Close() ;
		delete file_ ;
	}
	SAFE_FREE(samples_) ;
	SAFE_FREE(readBuffer_) ;
} ;

WavFile *WavFile::Open(const char *path) {

    // open file

	FileSystem *fs=FileSystem::GetInstance() ;
	I_File *file=fs->Open(path,"r") ;
	
	if (!file) return 0 ;

	WavFile *wav=new WavFile(file) ;

        
        // Get data
        
/*        file->Seek(0,SEEK_SET) ;
        file->Read(fileBuffer,filesize,1) ;
        uchar *ptr=fileBuffer ;*/
        
//Trace::Dump("Loading sample from %s",path) ;

	long position=0 ;

	// Read 'RIFF'

	unsigned int chunk ;

	position+=wav->readBlock(position,4) ;
	memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);
		
	if (chunk!=0x46464952) {
		Trace::Error("Bad RIFF format %x",chunk) ;
		delete(wav) ;
		return 0 ;
	}


	// Read size

	unsigned int size ;
	position+=wav->readBlock(position,4) ;
	memcpy(&size,wav->readBuffer_,4) ;
	size = Swap32(size);

	// Read WAVE

	position+=wav->readBlock(position,4) ;
	memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);

	if (chunk!=0x45564157) {
		Trace::Error("Bad WAV format") ;
		delete wav ;
		return 0 ;
	}

    // Read fmt or JUNK

    position += wav->readBlock(position, 4);
    memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);

        // Read (possible) JUNK

    if (chunk == 0x4b4e554a) {
        position+=wav->readBlock(position,4) ;
        memcpy(&size, wav->readBuffer_,4) ;
        size = Swap32(size) ;
        Trace::Debug("WavFile::Open(): skipping JUNK with size=%d", size);
        position+=size+(size&1);   // JUNK pads to even too
        position += wav->readBlock(position, 4);
        memcpy(&chunk,wav->readBuffer_,4) ;
		chunk = Swap32(chunk);
    }

    // Read fmt

    if (chunk!=0x20746D66) {
		Trace::Error("Bad WAV/fmt format") ;
		delete wav ;
		return 0 ;
	}

	// Read subchunk size

	position+=wav->readBlock(position,4) ;
	memcpy(&size,wav->readBuffer_,4) ;
	size = Swap32(size);

	if (size<16) {
		Trace::Error("Bad fmt size format") ;
		delete wav ;
		return 0 ;
	}
	int offset=size-16 ;

	// Read compression -- validated AFTER the extensible unwrap below

	unsigned short comp ;
	position+=wav->readBlock(position,2) ;
	memcpy(&comp,wav->readBuffer_,2) ;
	comp = Swap16(comp);

	// Read NumChannels (mono/Stereo)

	unsigned short nChannels ;
	position+=wav->readBlock(position,2) ;
	memcpy(&nChannels,wav->readBuffer_,2) ;
	nChannels = Swap16(nChannels);

	// Read Sample rate 

	unsigned int sampleRate ;

	position+=wav->readBlock(position,4) ;
	memcpy(&sampleRate,wav->readBuffer_,4) ;
	sampleRate = Swap32(sampleRate);

	// Skip byteRate & blockalign

	position+=6 ;

	short bitPerSample ;
	position+=wav->readBlock(position,2) ;
	memcpy(&bitPerSample,wav->readBuffer_,2) ;
	bitPerSample = Swap16(bitPerSample);
		
	/* WAVE_FORMAT_EXTENSIBLE: modern exporters write this wrapper
	   around perfectly ordinary PCM -- the REAL format tag hides in
	   the first two bytes of the trailing GUID. Files like these were
	   the classic "my 16-bit wav will not load" report. */
	if (comp==0xFFFE && offset>=10) {
		position+=8 ;   // cbSize, valid bits, channel mask
		position+=wav->readBlock(position,2) ;
		memcpy(&comp,wav->readBuffer_,2) ;
		comp = Swap16(comp);
		offset-=10 ;
	}

	wav->isFloat_=false ;
	if (comp==3) {
		// IEEE float: 32-bit only, converted to 16 on load
		if (bitPerSample!=32) {
			Trace::Error("Float wav must be 32 bit") ;
			delete wav ;
			return 0 ;
		}
		wav->isFloat_=true ;
	} else if (comp!=1) {
		Trace::Error("Unsupported compression %d",comp) ;
		delete wav ;
		return 0 ;
	}

	if ((bitPerSample!=8)&&(bitPerSample!=16)&&
	    (bitPerSample!=24)&&(bitPerSample!=32)) {
		Trace::Error("Only 8/16/24/32 bit supported") ;
		delete wav ;
		return 0 ;
	} ;
	bitPerSample/=8 ;
	wav->bytePerSample_=bitPerSample ;   // SOURCE width; RAM is always 16

	// some bad files have bigger chunks

	if (offset>0) {
		position+=offset ;
	}

	// read data subchunk header
	//Trace::Dump("data subch") ;

	position+=wav->readBlock(position,4) ;
	memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);
	

	while (chunk!=0x61746164) {
		position+=wav->readBlock(position,4) ;
		memcpy(&size,wav->readBuffer_,4) ;
		size = Swap32(size);

		// RIFF pads odd chunks to even; a 7-byte LIST without the
		// pad desynced the walk one byte and 'data' was never found
		position+=size+(size&1) ;
		position+=wav->readBlock(position,4) ;
		memcpy(&chunk,wav->readBuffer_,4) ;
		chunk = Swap32(chunk);
	}

        wav->sampleRate_=sampleRate ;
       	wav->channelCount_=nChannels ;

	// Read data size in byte

	position+=wav->readBlock(position,4) ;
	memcpy(&size,wav->readBuffer_,4) ;
	size = Swap32(size);

	wav->size_=size/nChannels/bitPerSample ; // Size in samples (stereo/16bits)

	wav->dataPosition_=position ;

	return wav ;
} ; 

void *WavFile::GetSampleBuffer(int note) {
	return samples_ ;
} ;

int WavFile::GetSize(int note) {
	return size_ ;
} ;

int WavFile::GetChannelCount(int note) {
    return channelCount_ ;
} ;

int WavFile::GetSampleRate(int note) {
    return sampleRate_ ;
} ;

long WavFile::readBlock(long start,long size) {
	if (size>readBufferSize_) {
		SAFE_FREE(readBuffer_) ;
		readBuffer_=SYS_MALLOC(size) ;
		readBufferSize_=size ;
	}
  if (!readBuffer_)
  {
    Trace::Error("Failed to allocate read buffer of size %d",size);
  } 
  else 
  {
  	file_->Seek(start,SEEK_SET) ;
    file_->Read(readBuffer_,size,1) ;
  }
	return size ;
} ;


/* Convert one chunk of raw file samples to the 16-bit the whole
   program speaks. Explicit little-endian byte math, so it is correct
   on any host without the Swap macros. */
static void wavConvert(const unsigned char *src, short *dst, int n,
                       int srcBytes, bool isFloat) {
	switch (srcBytes) {
	case 1:
		for (int i = 0; i < n; i++)
			dst[i] = (short)(((int)src[i] - 128) * 256);
		break;
	case 2:
		for (int i = 0; i < n; i++)
			dst[i] = (short)(src[i*2] | (src[i*2+1] << 8));
		break;
	case 3:
		// top sixteen of the twenty four
		for (int i = 0; i < n; i++)
			dst[i] = (short)(src[i*3+1] | (src[i*3+2] << 8));
		break;
	case 4:
		if (isFloat) {
			for (int i = 0; i < n; i++) {
				unsigned int u = src[i*4] | (src[i*4+1] << 8) |
				                 (src[i*4+2] << 16) |
				                 ((unsigned int)src[i*4+3] << 24);
				float f;
				memcpy(&f, &u, 4);
				if (f > 1.0f) f = 1.0f;
				if (f < -1.0f) f = -1.0f;
				dst[i] = (short)(f * 32767.0f);
			}
		} else {
			// 32-bit int: top sixteen
			for (int i = 0; i < n; i++)
				dst[i] = (short)(src[i*4+2] | (src[i*4+3] << 8));
		}
		break;
	}
}

bool WavFile::GetBuffer(long start,long size) {

	/* One converting reader for every source width. The destination
	   is ALWAYS 16-bit in RAM; the file may be 8, 16, 24 or 32-float
	   (24/32 are what modern exporters produce, and rejecting them
	   was most of "my sample will not load"). Raw bytes go through
	   readBuffer_ a chunk at a time and convert on the way in -- a
	   source wider than 16 bits cannot be read into the destination
	   and converted in place, it would not fit. */

	int sampleBufferSize=2*channelCount_*size ;
	if (sampleBufferSize>sampleBufferSize_) {
		SAFE_FREE(samples_) ;
		samples_=(short *)SYS_MALLOC(sampleBufferSize) ;
		sampleBufferSize_=sampleBufferSize ;
	}

	if (!samples_) {
		Trace::Error("Failed to allocate %d samples",sampleBufferSize);
		return false ;
	}

	long srcFrame=(long)channelCount_*bytePerSample_ ;
	long bufferStart=dataPosition_+start*srcFrame ;
	long left=size*channelCount_ ;         // samples still to produce
	short *out=samples_ ;

	// trickle when asked (big loads breathe), 128KB strides otherwise
	int rawChunk=(bufferChunkSize_>0)?bufferChunkSize_:131072 ;
	rawChunk-=rawChunk%bytePerSample_ ;
	if (rawChunk<bytePerSample_) rawChunk=bytePerSample_ ;

	while (left>0) {
		int samplesThis=rawChunk/bytePerSample_ ;
		if (samplesThis>left) samplesThis=(int)left ;
		int rawThis=samplesThis*bytePerSample_ ;
		readBlock(bufferStart,rawThis) ;
		wavConvert((unsigned char *)readBuffer_,out,samplesThis,
		           bytePerSample_,isFloat_) ;
		bufferStart+=rawThis ;
		out+=samplesThis ;
		left-=samplesThis ;
		if (bufferChunkSize_>0) TimeService::GetInstance()->Sleep(1) ;
	}
	return true ;
} ;

void WavFile::Close() {
	file_->Close() ;
	SAFE_DELETE(file_) ;
	SAFE_FREE(readBuffer_) ;
	readBufferSize_=0 ;
} ;

int WavFile::GetRootNote(int note) {
	return 60 ;
} 
