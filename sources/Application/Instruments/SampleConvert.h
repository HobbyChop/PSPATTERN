#ifndef _SAMPLE_CONVERT_H_
#define _SAMPLE_CONVERT_H_

class WavFile ;

/* How a wav is stored once it is in the project. The sampler plays
   any rate at the right pitch and one channel or two, so fewer
   frames and fewer channels are the only levers that save memory;
   word size is not one -- RAM is always 16-bit, and the crush row
   already does bit reduction at play time. */
struct SampleImportOptions {
	bool mono ;     // fold stereo down to one channel
	int rateDiv ;   // 1, 2 or 4: keep the rate, halve it, quarter it
	SampleImportOptions():mono(false),rateDiv(1) {} ;
} ;

enum SampleConvertResult {
	SCR_OK=0,
	SCR_CANT_READ,
	SCR_CANT_WRITE,
	SCR_NO_MEMORY
} ;

/* The one reduction, shared by the converter and the preview so what
   you hear is what gets written: div frames of srcCh channels folded
   into one frame of outCh (1 or 2), a plain average. Two frames of
   stereo into one of mono is four numbers averaged. */
static inline void SampleConvertFold(const short *src,int srcCh,int div,int outCh,
                                     long nOut,short *dst) {
	if (outCh==1) {
		int taps=div*srcCh ;
		for (long f=0;f<nOut;f++) {
			const short *frame=src+f*taps ;
			int acc=0 ;
			for (int k=0;k<taps;k++) acc+=frame[k] ;
			*dst++=(short)(acc/taps) ;
		}
	} else {
		for (long f=0;f<nOut;f++) {
			const short *frame=src+f*div*srcCh ;
			int l=0,r=0 ;
			for (int k=0;k<div;k++) {
				l+=frame[k*srcCh] ;
				r+=(srcCh>1)?frame[k*srcCh+1]:frame[k*srcCh] ;
			}
			*dst++=(short)(l/div) ;
			*dst++=(short)(r/div) ;
		}
	}
}

namespace SampleConvert {
	// true when the file has to be rewritten to come out as asked: a
	// change of channels or rate, or a source that is not 16-bit PCM
	bool NeedsConversion(WavFile *src,const SampleImportOptions &opt) ;
	// what the pool will hold afterwards
	int OutChannels(int srcChannels,const SampleImportOptions &opt) ;
	int OutRate(int srcRate,const SampleImportOptions &opt) ;
	long OutFrames(long srcFrames,const SampleImportOptions &opt) ;
	// read srcPath, write dstPath as 16-bit PCM in the shape asked for.
	// Chunked: the heap cost is bounded whatever the size of the file.
	SampleConvertResult Convert(const char *srcPath,const char *dstPath,
	                            const SampleImportOptions &opt) ;
}
#endif
