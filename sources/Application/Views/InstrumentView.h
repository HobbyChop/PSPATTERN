#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "ViewData.h"

class SynthInstrument ;

class InstrumentView: public FieldView, public I_Observer {
public:
	InstrumentView(GUIWindow &w,ViewData *data) ;
	virtual ~InstrumentView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void LooseFocus() ;
	virtual void OnNubFlick(int dir, unsigned short mask) ;
	virtual void OnFocus() ;

	// The routing picture under the operator columns. See the
	// definition for why it is drawn the way it is.
	void drawFmAlgo(SynthInstrument *instrument) ;

	// answer to the "replace instrument?" prompt

protected:
	void warpToNext(int offset) ;
	// SELECT latch: the preview note; every slot/type/engine step
	// retriggers it while latched, so browsing is hearing
	void auditionStart() ;
	void auditionStop() ;
	void auditionRetrigger() ;
	// d-pad left/right: step type, lossless via the stash
	void cycleType(int step) ;
	// d-pad up/down: step the synth engine (skips hidden FM)
	void cycleEngine(int step) ;
	void onInstrumentChange() ;
	void fillSampleParameters() ;
	void fillMidiParameters() ;
	void fillSynthParameters() ;
	void drawSynthChrome() ;
	void drawSampleChrome() ;
	void drawMidiChrome() ;
	InstrumentType getInstrumentType() ;
	void Update(Observable &o,I_ObservableData *d) ;
	// swap the slot's instrument object for one of the requested
	// type. Everything the old one held is gone afterwards.
	void applyTypeChange() ;

private:
	Project *project_ ;
	FourCC lastFocusID_ ;
	I_Instrument *current_ ;
	// the per-slot type selector (sample/midi/synth); editing it swaps
	// the instrument object in the bank
	WatchedVariable typeVar_ ;
	bool refreshingType_ ;
	// the slot number as an ordinary editable field: focus it and
	// A+arrows step instruments (no need to hold the browse chord)
	WatchedVariable slotVar_ ;
	bool refreshingSlot_ ;
	// type the user picked while the confirmation was up
	InstrumentType pendingType_ ;
	/* Rebuilds requested from inside a notification are DEFERRED to the
	   top of the next DrawView. A change to the engine/wave/type lands
	   here from the editing field's own ProcessArrow, via the variable's
	   NotifyObservers -- rebuilding immediately deletes that field while
	   its method is still on the call stack and mutates the observer
	   vector mid-iteration. Both are the freeze-and-reboot. DrawView
	   runs outside every field call and every notify walk. */
	bool rebuildPending_ ;
	bool applyTypePending_ ;
	bool auditionLatch_ ;
	unsigned char auditionNote_ ;
	unsigned long selectDownAt_ ;
} ;
#endif
