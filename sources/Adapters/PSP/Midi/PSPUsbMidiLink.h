#ifndef _PSP_USBMIDI_LINK_H_
#define _PSP_USBMIDI_LINK_H_

// Bridge to the PSP-MIDI adapter's usbmidi.prx kernel module
// (PSPUsbMidi syscall library: vendor-bulk USB carrying raw 4-byte
// USB-MIDI packets). The prx ships beside the EBOOT; when it is
// missing or fails to load the tracker keeps running without MIDI —
// the stub imports below stay unresolved, so every call site must be
// gated on PSPUsbMidiLink::Available().

extern "C" {
int pspUsbMidiRead(unsigned char *pkt);
int pspUsbMidiWrite(const unsigned char *pkt);
int pspUsbMidiConnected(void);
int pspUsbMidiWaitData(unsigned int timeout_us);
int pspUsbMidiStatus(void);
}

class PSPUsbMidiLink {
public:
	// argv0: the EBOOT's boot path; usbmidi.prx is looked up next to it
	static bool Load(const char *argv0) ;
	static void Unload() ;
	static bool Available() ;
	// after a suspend/resume cycle the USB connection needs re-arming
	static void OnResume() ;
private:
	static bool available_ ;
} ;
#endif
