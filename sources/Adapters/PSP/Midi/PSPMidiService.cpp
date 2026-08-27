#include "PSPMidiService.h"
#include "PSPUsbMidiLink.h"
#include "PSPUsbMidiOutDevice.h"
#include "PSPUsbMidiInDevice.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"

PSPMidiService::PSPMidiService() {
	lastPoll_=0 ;
	lastState_=MLS_NODRIVER ;
} ;

PSPMidiService::~PSPMidiService() {
} ;

void PSPMidiService::buildDriverList() {
	if (PSPUsbMidiLink::Available()) {
		Insert(new PSPUsbMidiOutDevice("PSPMIDI")) ;
		inList_.Insert(new PSPUsbMidiInDevice("PSPMIDI")) ;
		Trace::Log("PSPUSBMIDI","registered PSPMIDI out+in devices") ;
	}
} ;

/* Three states, because the two ways this can fail need different
   answers from whoever is holding the PSP.

   No driver: usbmidi.prx did not load. The file is missing beside the
   EBOOT, or another plugin has taken the slot. Nothing will ever be
   sent and no cable will change that.

   Waiting: the driver is loaded and no adapter is answering. That is a
   cable, a hub, or an adapter that has not enumerated yet.

   Ready: something is on the other end. */
MidiLinkState PSPMidiService::GetLinkState() {

	if (!PSPUsbMidiLink::Available()) return MLS_NODRIVER ;

	unsigned long now=System::GetInstance()->GetClock() ;
	if (lastPoll_==0 || (now-lastPoll_)>=250) {
		lastPoll_=now ;
		// Strictly positive, not merely non-zero: if the call ever
		// returns a negative error code, that is not a connection.
		lastState_=(pspUsbMidiConnected()>0)?MLS_READY:MLS_WAITING ;
	}
	return lastState_ ;
} ;
