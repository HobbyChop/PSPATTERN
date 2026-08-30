#include "PSPUsbMidiInDevice.h"
#include <pspthreadman.h>
#include "PSPUsbMidiLink.h"
#include "System/Console/Trace.h"

bool PSPUsbMidiPump::Execute() {

	/* Clock bytes must not queue behind UI work: this thread wakes on
	   the prx's event flag the instant a packet lands, but a wake is
	   only as prompt as the scheduler lets it be. Lift it above the
	   interface threads (PSP: lower number = higher priority) so a
	   busy repaint cannot add milliseconds of jitter to the sync. */
	sceKernelChangeThreadPriority(0, 0x14) ;

	unsigned char pkt[4] ;
	// Newer prx builds stamp each packet at the USB completion (status bit
	// 0x80). One capability check, then the stamped read for the whole
	// session; an old prx on the card falls back to the plain read.
	bool hasTs = (pspUsbMidiStatus() & 0x80) != 0 ;
	while (!shouldTerminate()) {
		// returns early when the prx flags rx data; times out otherwise
		pspUsbMidiWaitData(20000) ;
		int budget=64 ;
		if (hasTs) {
			unsigned int ts ;
			while (budget-->0 && pspUsbMidiReadTs(pkt,&ts)) {
				owner_.onPacket(pkt,ts) ;
			}
		} else {
			while (budget-->0 && pspUsbMidiRead(pkt)) {
				owner_.onPacket(pkt) ;
			}
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

/* The clock slave wants the ARRIVAL time of each 0xF8, and threading a
   timestamp through the whole MidiMessage chain for one consumer would
   be surgery on every layer. A latch is enough: the dispatch below is
   synchronous on this thread, so Player::OnMidiClock reads the stamp
   the same call chain that set it. */
static volatile unsigned int s_lastClockUs = 0 ;
extern "C" unsigned int PSPMidi_LastClockStampUs(void) {
	return s_lastClockUs ;
}

void PSPUsbMidiInDevice::onPacket(unsigned char *pkt, unsigned int tsUs) {

	int cin=pkt[0]&0x0F ;
	if (tsUs && cin==0x0F && pkt[1]==0xF8) s_lastClockUs=tsUs ;

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
