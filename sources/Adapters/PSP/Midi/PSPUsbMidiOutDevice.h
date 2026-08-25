#ifndef _PSP_USBMIDI_OUT_DEVICE_H_
#define _PSP_USBMIDI_OUT_DEVICE_H_

#include "Services/Midi/MidiOutDevice.h"

class PSPUsbMidiOutDevice: public MidiOutDevice {
public:
	PSPUsbMidiOutDevice(const char *name) ;
	virtual bool Init() ;
	virtual void Close() ;
	virtual bool Start() ;
	virtual void Stop() ;
	virtual void SendMessage(MidiMessage &m) ;
private:
	bool running_ ;
} ;
#endif
