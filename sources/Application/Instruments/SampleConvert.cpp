#include "SampleConvert.h"
#include "WavFile.h"
#include "WavFileWriter.h"
#include "System/Console/Trace.h"
#include <stdlib.h>

/* Frames per pass. A multiple of four so every pass divides cleanly
   at any rate divider; small enough that the source chunk, its raw
   read buffer, the output block and the pending write buffer add up
   to under 200KB even on a stereo float file. */
#define CONVERT_CHUNK_FRAMES 8192

static int divider(const SampleImportOptions &opt) {
	return (opt.rateDiv==2||opt.rateDiv==4)?opt.rateDiv:1 ;
}

bool SampleConvert::NeedsConversion(WavFile *src,const SampleImportOptions &opt) {
	if (!src) return false ;
	int ch=src->GetChannelCount(-1) ;
	// RAM is 16-bit; so is the copy the project keeps
	if (src->GetBitDepth()!=16||src->IsFloat()) return true ;
	if (divider(opt)>1) return true ;
	if (opt.mono&&ch>1) return true ;
	if (ch>2) return true ;   // the sampler plays one channel or two
	return false ;
}

int SampleConvert::OutChannels(int srcChannels,const SampleImportOptions &opt) {
	if (srcChannels<=1||opt.mono) return 1 ;
	return 2 ;
}

int SampleConvert::OutRate(int srcRate,const SampleImportOptions &opt) {
	return srcRate/divider(opt) ;
}

long SampleConvert::OutFrames(long srcFrames,const SampleImportOptions &opt) {
	return srcFrames/divider(opt) ;
}

SampleConvertResult SampleConvert::Convert(const char *srcPath,const char *dstPath,
                                           const SampleImportOptions &opt) {
	WavFile *in=WavFile::Open(srcPath) ;
	if (!in) return SCR_CANT_READ ;
	int ch=in->GetChannelCount(-1) ;
	int rate=in->GetSampleRate(-1) ;
	long frames=in->GetSize(-1) ;
	int div=divider(opt) ;
	int outCh=OutChannels(ch,opt) ;
	long outFrames=frames/div ;
	if (ch<1||rate<1||outFrames<1) { delete in ; return SCR_CANT_READ ; }

	WavFileWriter out(dstPath,outCh,rate/div) ;
	if (!out.IsOpen()) { delete in ; return SCR_CANT_WRITE ; }

	short *block=(short *)malloc(CONVERT_CHUNK_FRAMES*outCh*sizeof(short)) ;
	if (!block) { delete in ; out.Close() ; return SCR_NO_MEMORY ; }

	/* Reduction is an average, not a filtered resample: two or four
	   frames folded into one, both channels folded into one. Cheap,
	   stateless across passes, and it takes the top end down rather
	   than off -- bright material can alias, which on this machine is
	   as often wanted as not. */
	long pos=0 ;
	long total=outFrames*div ;   // whole output frames only
	SampleConvertResult result=SCR_OK ;
	while (pos<total) {
		long n=total-pos ;
		if (n>CONVERT_CHUNK_FRAMES) n=CONVERT_CHUNK_FRAMES ;
		if (!in->GetBuffer(pos,n)) { result=SCR_CANT_READ ; break ; }
		const short *s=(const short *)in->GetSampleBuffer(-1) ;
		if (!s) { result=SCR_NO_MEMORY ; break ; }
		long nOut=n/div ;
		short *d=block ;
		if (outCh==1) {
			// div frames of ch channels lie contiguous: one flat average
			int taps=div*ch ;
			for (long f=0;f<nOut;f++) {
				const short *frame=s+f*taps ;
				int acc=0 ;
				for (int k=0;k<taps;k++) acc+=frame[k] ;
				*d++=(short)(acc/taps) ;
			}
		} else {
			for (long f=0;f<nOut;f++) {
				const short *frame=s+f*div*ch ;
				int l=0,r=0 ;
				for (int k=0;k<div;k++) { l+=frame[k*ch] ; r+=frame[k*ch+1] ; }
				*d++=(short)(l/div) ;
				*d++=(short)(r/div) ;
			}
		}
		out.AddShorts(block,(int)nOut) ;
		pos+=n ;
	}
	free(block) ;
	out.Close() ;
	delete in ;
	if (result==SCR_OK) {
		Trace::Log("CONVERT","%s -> %d ch, %d Hz, %ld frames",srcPath,outCh,
		           rate/div,outFrames) ;
	} else {
		Trace::Error("convert %s failed (%d)",srcPath,(int)result) ;
	}
	return result ;
}
