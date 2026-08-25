#include "PSPUsbMidiOutDevice.h"
#include "PSPUsbMidiLink.h"

PSPUsbMidiOutDevice::PSPUsbMidiOutDevice(const char *name):
	MidiOutDevice(name),
	running_(false)
{
} ;

bool PSPUsbMidiOutDevice::Init() {
	return PSPUsbMidiLink::Available() ;
} ;

void PSPUsbMidiOutDevice::Close() {
} ;

bool PSPUsbMidiOutDevice::Start() {
	running_=PSPUsbMidiLink::Available() ;
	return running_ ;
} ;

void PSPUsbMidiOutDevice::Stop() {
	running_=false ;
} ;

void PSPUsbMidiOutDevice::SendMessage(MidiMessage &msg) {

	if (!running_) return ;

	// Wrap the message as a 4-byte USB-MIDI event packet (cable 0).
	// The prx's tx ring drops the packet if the adapter is unplugged.

	unsigned char status=msg.status_ ;
	unsigned char cin ;
	if (status>=0xF8) {
		cin=0x0F ;                    // single-byte realtime (clock/start/stop)
	} else if (status>=0xF0) {
		switch(status) {
			case 0xF1:
			case 0xF3: cin=0x02 ; break ; // two-byte system common
			case 0xF2: cin=0x03 ; break ; // song position pointer
			default: return ;             // sysex & co: not carried, skip
		}
	} else {
		cin=status>>4 ;               // channel voice: CIN == high nibble
	}

	unsigned char pkt[4] ;
	pkt[0]=cin ;
	pkt[1]=status ;
	pkt[2]=(msg.data1_!=MidiMessage::UNUSED_BYTE)?msg.data1_:0 ;
	pkt[3]=(msg.data2_!=MidiMessage::UNUSED_BYTE)?msg.data2_:0 ;
	pspUsbMidiWrite(pkt) ;
} ;
