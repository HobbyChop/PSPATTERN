#ifndef _SONG_H_
#define _SONG_H_

#include "Chain.h"
#include "Phrase.h"
#include "Application/Persistency/Persistent.h"

#define SONG_CHANNEL_COUNT 8
/* Voice lanes beyond the song's eight: the audition preview and a MIDI
   keyboard play there, wired straight to the master sum -- so no song
   strip's mute, fader, filter or sends can silence or colour them, and
   the sequencer never touches them. Four, so a pad can play a kick over
   a hat and a keyboard can hold a chord; a fifth key steals the oldest
   (Player::allocLane). One lane used to be enough because a keyboard
   was ignored while the song ran. Everything holding per-voice state
   sizes with PLAYER_CHANNEL_COUNT; the song, the project mixer and the
   views stay eight wide. The cost is memory, not time: an idle lane
   renders nothing, and the per-voice state of every instrument grows
   by three lanes, which is under 300KB across a full bank. On the PSP
   the lanes are rendered at the output callback, past the render-ahead
   queue, so a key is a chunk or two from the speaker rather than a
   queue's worth of slices -- PlayerMixer::RenderLate. */
#define LANE_COUNT 4
#define PLAYER_CHANNEL_COUNT (SONG_CHANNEL_COUNT+LANE_COUNT)
#define AUDITION_CHANNEL SONG_CHANNEL_COUNT    // the first lane
#define SONG_ROW_COUNT 256

#define MAX_SAMPLEINSTRUMENT_COUNT 0x80
#define MAX_MIDIINSTRUMENT_COUNT 0x10
/* Thirty two synths, up from sixteen. The bank lays its slots out
   samples, then MIDI, then synths, so widening the LAST range leaves
   every existing instrument number meaning exactly what it did: a
   song saved with synth 90 still finds it at 90, and no save needs
   converting. The cost is the other direction -- a song that uses
   A0 and above will not open properly on a build older than this
   one, which is the price of any format that grows. */
#define MAX_SYNTHINSTRUMENT_COUNT 0x20

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
