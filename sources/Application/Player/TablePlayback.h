#ifndef _TABLE_PLAYBACK_H_
#define _TABLE_PLAYBACK_H_

#include "Application/Model/Table.h"
#include "Application/Model/Song.h"
#include "Application/Model/Groove.h"

class I_Instrument ;

class TablePlayerChange{
public:
	// Both callers used to set timeToLive_ by hand and one of them left
	// instrRetrigger_ uninitialised entirely.
	TablePlayerChange():timeToLive_(0),instrRetrigger_(-1) {} ;
	int timeToLive_ ;
	int instrRetrigger_ ;   // -1 = no retrigger this step
} ;

struct TablePlayback {
public:
	void Init(int i) ;
	void ProcessStep(TablePlayerChange &tpc) ;
	bool ProcessLocalCommand(int row,FourCC *commandList,ushort *paramList,TablePlayerChange &tpc) ;
	void Start(I_Instrument *,Table&,bool automated) ;
	void Stop() ;
	int GetPlaybackPosition(int channel) ;
	Table *GetTable() ;
	bool GetAutomation() ;

	static void Reset() ;
	static TablePlayback &GetTablePlayback(int channel) ;
private:
	Table *table_ ;
	int position_[3] ;
	int previous_[3] ;
	bool hopped_[3] ;
	I_Instrument *instrument_ ;
	int channel_ ;
	bool automated_ ;
	uchar hopCount_[TABLE_STEPS][3] ;
	ChannelGroove groove_ ;
	// One stream shared by every table, for the same reason the
	// player has one: two columns rolling on the same tick should
	// not agree.
	static unsigned int rng_ ;

	static TablePlayback playback_[SONG_CHANNEL_COUNT] ;
} ;

class TableSaveState {
public:
	void Reset() ;
	uchar hopCount_[TABLE_STEPS][3] ;
	int position_[3] ;
} ;

#endif
