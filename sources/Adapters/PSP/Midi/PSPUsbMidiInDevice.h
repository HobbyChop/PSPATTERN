#ifndef _PSP_USBMIDI_IN_DEVICE_H_
#define _PSP_USBMIDI_IN_DEVICE_H_

#include "Services/Midi/MidiInDevice.h"
#include "System/Process/Process.h"

class PSPUsbMidiInDevice ;

// pump thread: blocks on the prx's rx event flag, drains with a
// budget (the PSPOLY lesson: an unbounded drain starves the render)
class PSPUsbMidiPump: public SysThread {
public:
	PSPUsbMidiPump(PSPUsbMidiInDevice &owner):owner_(owner) {} ;
	virtual bool Execute() ;
private:
	PSPUsbMidiInDevice &owner_ ;
} ;

class PSPUsbMidiInDevice: public MidiInDevice {
public:
	PSPUsbMidiInDevice(const char *name) ;
	virtual ~PSPUsbMidiInDevice() ;

	// called from the pump thread
	void onPacket(unsigned char *pkt) ;

protected:
	virtual bool initDriver() ;
	virtual void closeDriver() ;
	virtual bool startDriver() ;
	virtual void stopDriver() ;

private:
	PSPUsbMidiPump *pump_ ;
} ;
#endif
