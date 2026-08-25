#include "PSPMidiService.h"
#include "PSPUsbMidiLink.h"
#include "PSPUsbMidiOutDevice.h"
#include "PSPUsbMidiInDevice.h"
#include "System/Console/Trace.h"

PSPMidiService::PSPMidiService() {
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
