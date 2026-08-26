#ifndef _SONG_H_
#define _SONG_H_

#include "Chain.h"
#include "Phrase.h"
#include "Application/Persistency/Persistent.h"

#define SONG_CHANNEL_COUNT 8
#define SONG_ROW_COUNT 256

#define MAX_SAMPLEINSTRUMENT_COUNT 0x80
#define MAX_MIDIINSTRUMENT_COUNT 0x10
#define MAX_SYNTHINSTRUMENT_COUNT 0x10

#define MAX_INSTRUMENT_COUNT (MAX_SAMPLEINSTRUMENT_COUNT+MAX_MIDIINSTRUMENT_COUNT+MAX_SYNTHINSTRUMENT_COUNT)

class Song:Persistent {
public:
	Song() ;
	~Song() ;

	virtual unsigned int Checksum(unsigned int h) ;
	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);

	unsigned char *data_ ;
	Chain *chain_ ;
	Phrase *phrase_ ;

	/* One flag per song row: a place you have told the tracker you
	   want to come back to.
	   
	   A song is 256 rows and the screen shows sixteen, so getting from
	   the intro to the last chorus is a lot of holding down. The
	   section jump on L walks the gaps between blocks of chains,
	   which is structure rather than intention -- it cannot know that
	   this row is the drop and that one is where the vocal starts.
	   
	   One byte a row rather than a bitmap. 256 bytes is nothing and a
	   hex buffer of flags is readable in the save file, which matters
	   more here than the 224 bytes it saves. */
	unsigned char bookmark_[SONG_ROW_COUNT] ;
} ;

#endif
