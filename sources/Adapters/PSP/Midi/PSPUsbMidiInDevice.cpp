#include "PSPUsbMidiInDevice.h"
#include "PSPUsbMidiLink.h"
#include "System/Console/Trace.h"

bool PSPUsbMidiPump::Execute() {

	unsigned char pkt[4] ;
	while (!shouldTerminate()) {
		// returns early when the prx flags rx data; times out otherwise
		pspUsbMidiWaitData(20000) ;
		int budget=64 ;
		while (budget-->0 && pspUsbMidiRead(pkt)) {
			owner_.onPacket(pkt) ;
		}
	}
	return true ;
} ;

PSPUsbMidiInDevice::PSPUsbMidiInDevice(const char *name):
	MidiInDevice(name),
	pump_(0)
{
} ;

PSPUsbMidiInDevice::~PSPUsbMidiInDevice() {
	stopDriver() ;
} ;

void PSPUsbMidiInDevice::onPacket(unsigned char *pkt) {

	int cin=pkt[0]&0x0F ;

	/* Single-byte realtime: clock, start, continue, stop.

	   These used to be dropped here, on the grounds that the mapping
	   engine has no use for them and the clock slave was not wired up.
	   The clock slave HAS been wired up on the other side the whole
	   time -- MidiNoteInput dispatches F8, FA, FB and FC and acts on
	   all four -- so this filter was the reason Follow did nothing on
	   a PSP: the bytes never got past the driver. */
	if (cin==0x0F) {
		MidiMessage rt(pkt[1],0,0) ;
		onDriverMessage(rt) ;
		return ;
	}

	// channel voice; system common and sysex are still of no use here
	if (cin<0x8||cin>0xE) return ;

	MidiMessage msg(pkt[1],pkt[2],pkt[3]) ;
	onDriverMessage(msg) ;
} ;

bool PSPUsbMidiInDevice::initDriver() {
	return PSPUsbMidiLink::Available() ;
} ;

void PSPUsbMidiInDevice::closeDriver() {
	stopDriver() ;
} ;

bool PSPUsbMidiInDevice::startDriver() {
	if (!PSPUsbMidiLink::Available()) return false ;
	if (pump_) return true ;
	pump_=new PSPUsbMidiPump(*this) ;
	SysProcessFactory::GetInstance()->BeginThread(*pump_) ;
	Trace::Log("PSPUSBMIDI","midi in pump running") ;
	return true ;
} ;

void PSPUsbMidiInDevice::stopDriver() {
	if (pump_) {
		pump_->RequestTermination() ;
		// exits within one 20ms wait; not joined (no join in SysThread)
		pump_=0 ;
	}
} ;
