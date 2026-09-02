#ifndef _SAMPLE_INFO_H_
#define _SAMPLE_INFO_H_

class SoundSource ;

/* One vocabulary for describing a sample, used by the import browser
   (from a file header) and the instrument screen (from a pool entry),
   so the two agree on units and wording. */
namespace SampleInfo {
	// bytes the sampler holds for this many frames -- always 16-bit in RAM
	long RamBytes(int channels,long frames) ;
	// "mono", "stereo", "multi"
	const char *Channels(int channels) ;
	// "405K", "1.2M", "812B": at most six cells
	void FormatBytes(unsigned long bytes,char *out,int size) ;
	// "2.30s", or "134s" past a hundred
	void FormatSeconds(int rate,long frames,char *out,int size) ;
	// "stereo 44100Hz 2.30s 18C36fr 405K" -- frames in hex, the
	// unit the start/loop/end fields count in
	void Describe(int channels,int rate,long frames,char *out,int size) ;
	// the same for a pool entry; kit drums and soundfont presets included
	void DescribeSource(SoundSource *src,char *out,int size) ;
}
#endif
