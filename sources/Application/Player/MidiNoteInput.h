#ifndef _MIDI_NOTE_INPUT_H_
#define _MIDI_NOTE_INPUT_H_

#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"

/* Routes incoming MIDI notes to the player.

   MidiInDevice already parsed everything arriving on the wire, but the
   only thing listening was the controller mapper -- the part that lets
   a knob act as a button. Notes went nowhere, so there was no way to
   play the instruments from a keyboard, on a device whose whole reason
   to exist is a MIDI adapter.

   This subscribes to every input and forwards note on and note off.
   Controllers, clock and everything else carry on to the mapper
   untouched. */
class MidiNoteInput: public I_Observer,public T_Singleton<MidiNoteInput> {
public:
	MidiNoteInput() ;
	// subscribe to whatever inputs the service has opened
	void Attach() ;
	void SetProject(class Project *project) ;
	virtual void Update(Observable &o,I_ObservableData *d) ;
private:
	void onClock() ;
	void onStart(bool fromTop) ;
	void onStop() ;

	bool attached_ ;
	class Project *project_ ;

	/* Clock slave.

	   MIDI sends 24 clocks per quarter note. The tracker's tempo drives
	   the audio slice size, so following an external clock means
	   measuring its rate and setting the tempo from it, rather than
	   stepping the sequencer per byte. That tracks a moving tempo
	   without letting jitter on a single byte shove the whole song. */
	int  clockCount_ ;
	unsigned long lastQuarterMs_ ;
	int  smoothedBpm_ ;
} ;

#endif
