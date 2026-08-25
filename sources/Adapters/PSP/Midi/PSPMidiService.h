#ifndef _PSP_MIDI_SERVICE_H_
#define _PSP_MIDI_SERVICE_H_

#include "Services/Midi/MidiService.h"

class PSPMidiService: public MidiService {
public:
	PSPMidiService() ;
	~PSPMidiService() ;
	virtual void buildDriverList() ;
} ;
#endif
