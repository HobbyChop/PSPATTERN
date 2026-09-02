#include "SampleInfo.h"
#include "SoundSource.h"
#include <stdio.h>

long SampleInfo::RamBytes(int channels,long frames) {
	if (channels<1) channels=1 ;
	if (frames<0) frames=0 ;
	return 2L*channels*frames ;
}

const char *SampleInfo::Channels(int channels) {
	if (channels<=1) return "mono" ;
	if (channels==2) return "stereo" ;
	return "multi" ;
}

void SampleInfo::FormatBytes(unsigned long bytes,char *out,int size) {
	if (bytes<1024UL) {
		snprintf(out,size,"%luB",bytes) ;
	} else if (bytes<1024UL*1024UL) {
		snprintf(out,size,"%luK",bytes/1024UL) ;
	} else {
		// tenths of a megabyte; a sample cannot exceed the machine
		unsigned long tenths=(bytes/1024UL)*10UL/1024UL ;
		snprintf(out,size,"%lu.%luM",tenths/10,tenths%10) ;
	}
}

void SampleInfo::FormatSeconds(int rate,long frames,char *out,int size) {
	if (rate<1||frames<0) { snprintf(out,size,"?s") ; return ; }
	unsigned long long hund=((unsigned long long)frames*100ULL)/(unsigned long long)rate ;
	if (hund<100ULL*100ULL) {
		snprintf(out,size,"%lu.%02lus",(unsigned long)(hund/100),(unsigned long)(hund%100)) ;
	} else {
		snprintf(out,size,"%lus",(unsigned long)(hund/100)) ;
	}
}

void SampleInfo::Describe(int channels,int rate,long frames,char *out,int size) {
	char secs[12],ram[8] ;
	FormatSeconds(rate,frames,secs,sizeof(secs)) ;
	FormatBytes((unsigned long)RamBytes(channels,frames),ram,sizeof(ram)) ;
	snprintf(out,size,"%s %dHz %s %lXfr %s",Channels(channels),rate,secs,
	         (unsigned long)frames,ram) ;
}

void SampleInfo::DescribeSource(SoundSource *src,char *out,int size) {
	if (!src) { snprintf(out,size,"no sample") ; return ; }
	if (src->IsMulti()) {
		snprintf(out,size,"soundfont preset, a sample per note") ;
		return ;
	}
	Describe(src->GetChannelCount(-1),src->GetSampleRate(-1),src->GetSize(-1),
	         out,size) ;
}
