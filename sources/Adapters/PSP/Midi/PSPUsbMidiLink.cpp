#include "PSPUsbMidiLink.h"
#include "System/Console/Trace.h"

#include <pspkernel.h>
#include <pspsdk.h>
#include <pspusb.h>
#include <string.h>

#define USBMIDI_DRIVER_NAME "USBMidiDriver"
#define USBMIDI_PID 0x1D9

bool PSPUsbMidiLink::available_=false ;

bool PSPUsbMidiLink::Load(const char *argv0) {

	char prxPath[256] ;
	strncpy(prxPath,argv0,sizeof(prxPath)-1) ;
	prxPath[sizeof(prxPath)-1]=0 ;
	char *slash=strrchr(prxPath,'/') ;
	if (slash) {
		*(slash+1)=0 ;
	} else {
		prxPath[0]=0 ;
	}
	strncat(prxPath,"usbmidi.prx",sizeof(prxPath)-strlen(prxPath)-1) ;

	SceUID mod=pspSdkLoadStartModule(prxPath,PSP_MEMORY_PARTITION_KERNEL) ;
	if (mod<0) {
		// No prx = no MIDI, but the tracker still runs (samples only)
		Trace::Log("PSPUSBMIDI","usbmidi.prx not loaded (%08X) - MIDI disabled",(unsigned int)mod) ;
		available_=false ;
		return false ;
	}

	int ret=sceUsbStart(PSP_USBBUS_DRIVERNAME,0,0) ;
	if (ret) Trace::Log("PSPUSBMIDI","sceUsbStart(bus)=%08X",(unsigned int)ret) ;
	ret=sceUsbStart(USBMIDI_DRIVER_NAME,0,0) ;
	if (ret) Trace::Log("PSPUSBMIDI","sceUsbStart(midi)=%08X",(unsigned int)ret) ;
	ret=sceUsbActivate(USBMIDI_PID) ;
	if (ret) Trace::Log("PSPUSBMIDI","sceUsbActivate=%08X",(unsigned int)ret) ;

	Trace::Log("PSPUSBMIDI","usbmidi.prx loaded, USB active") ;
	available_=true ;
	return true ;
} ;

void PSPUsbMidiLink::Unload() {
	if (!available_) return ;
	sceUsbDeactivate(USBMIDI_PID) ;
	sceUsbStop(USBMIDI_DRIVER_NAME,0,0) ;
	sceUsbStop(PSP_USBBUS_DRIVERNAME,0,0) ;
	available_=false ;
} ;

bool PSPUsbMidiLink::Available() {
	return available_ ;
} ;

void PSPUsbMidiLink::OnResume() {
	if (!available_) return ;
	// re-arm the device end after suspend (the PSPECTRA reconnect move)
	sceUsbDeactivate(USBMIDI_PID) ;
	sceUsbActivate(USBMIDI_PID) ;
} ;
