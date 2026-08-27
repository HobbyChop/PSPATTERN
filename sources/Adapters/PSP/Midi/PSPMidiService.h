#ifndef _PSP_MIDI_SERVICE_H_
#define _PSP_MIDI_SERVICE_H_

#include "Services/Midi/MidiService.h"

class PSPMidiService: public MidiService {
public:
	PSPMidiService() ;
	~PSPMidiService() ;
	virtual void buildDriverList() ;
	virtual MidiLinkState GetLinkState() ;
private:
	// pspUsbMidiConnected is a syscall into the kernel module and the
	// panel that asks repaints every UI frame, so the answer is held
	// for a moment rather than fetched sixty times a second.
	unsigned long lastPoll_ ;
	MidiLinkState lastState_ ;
} ;
#endif
