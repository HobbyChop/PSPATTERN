	.set noreorder

#include "pspstub.s"

	STUB_START "PSPUsbMidi",0x40090000,0x00060005
	STUB_FUNC  0x8EA764C2,pspUsbMidiRead
	STUB_FUNC  0x653524A1,pspUsbMidiWrite
	STUB_FUNC  0xC667BB4A,pspUsbMidiConnected
	STUB_FUNC  0x2A6CB801,pspUsbMidiWaitData
	STUB_FUNC  0x35F9677E,pspUsbMidiStatus
	STUB_FUNC  0x75884F91,pspUsbMidiReadTs
	STUB_END
