#ifndef _INSTRUMENT_BANK_H_
#define _INSTRUMENT_BANK_H_

#include "Application/Persistency/Persistent.h"
#include "Application/Model/Song.h"
#include "Application/Instruments/I_Instrument.h"
#include <string>
#include <utility>
#include <vector>

#define NO_MORE_INSTRUMENT 0x100

class InstrumentBank: public Persistent {
public:
	InstrumentBank() ;
	~InstrumentBank() ;
	void AssignDefaults() ;
	I_Instrument *GetInstrument(int i) ;
	virtual unsigned int Checksum(unsigned int h);
	virtual void SaveContent(TiXmlNode *node);
	virtual void RestoreContent(TiXmlElement *element);
	void Init() ;
	void OnStart() ;
	unsigned short GetNext() ;
	unsigned short Clone(unsigned short i) ;
	/* A copy of i in the next empty slot after it, of any type: O
	   twice on the phrase screen's instrument column. An empty slot of
	   the same type is preferred; failing that, any empty slot is
	   switched to the type -- only when retype is allowed, because
	   SetType must not run under a playing voice. */
	unsigned short CloneNext(unsigned short i,bool retype) ;
	// swap the instrument at a slot to another type (never while the
	// player runs: a live voice would keep a pointer to the old one)
	void SetType(int i,InstrumentType it) ;
private:
	I_Instrument *instrument_[MAX_INSTRUMENT_COUNT] ;
	/* Per-(slot,type) parameter stash. SetType records the leaving
	   type's name->value pairs and replays them when the slot returns
	   to that type -- flipping sample->synth->sample loses NOTHING,
	   which is what let the "settings are lost" confirmation die.
	   Session-only; the project file still saves the active type. */
	std::vector<std::pair<std::string,std::string> >
	    stash_[MAX_INSTRUMENT_COUNT][IT_LAST] ;
} ;

#endif
