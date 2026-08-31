#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "ViewData.h"
#include "Application/Instruments/SynthPresets.h"
#include <vector>

class SynthInstrument ;

class InstrumentView: public FieldView, public I_Observer {
public:
	void OnSavePreset(const char *name) ;   // modal save-dialog callback
	virtual void ApplyDeferred() ;
	InstrumentView(GUIWindow &w,ViewData *data) ;
	virtual ~InstrumentView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void LooseFocus() ;
	virtual void OnNubFlick(int dir, unsigned short mask) ;
	virtual bool OnNavTo(ViewType to) ;
	virtual void OnFocus() ;

	// The routing picture under the operator columns. See the
	// definition for why it is drawn the way it is.
	void drawFmAlgo(SynthInstrument *instrument) ;
	// the import callback puts the cursor back on the file row
	void FocusSampleRow() ;

	// answer to the "replace instrument?" prompt

protected:
	void warpToNext(int offset) ;
	// SELECT latch: the preview note; every slot/type/engine step
	// retriggers it while latched, so browsing is hearing
	void openImportBrowser() ;
	FourCC lastLadderID_ ;   // the ladder row to come back to
	bool importPending_ ;
	void auditionStart() ;
	void auditionStop() ;
	void auditionRetrigger() ;
	// d-pad left/right: step type, lossless via the stash
	void cycleType(int step) ;
	// d-pad up/down: step the synth engine (skips hidden FM)
	void cycleEngine(int step) ;
	// the preset row: stepping loads (deferred)
	void checkPresetStep() ;
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
	bool auditionLatch_ ;   // true while SELECT is held (note sounding)
	unsigned char auditionNote_ ;
	/* the preset row on the identity ladder. presetVar_ is rebuilt on
	   every fill (the list changes when a preset is saved); loading is
	   DEFERRED like everything else that rebuilds this screen. */
	Variable *presetVar_ ;
	std::vector<const char *> presetList_ ;
	int presetShown_ ;
	bool presetPending_ ;
	int pendingPreset_ ;
	/* "--" is a place you can come back to: the sound is snapshotted
	   when browsing leaves it, and stepping back restores it. */
	SynthPresets::ParamSnapshot presetUndo_ ;
	bool presetUndoValid_ ;
	bool presetRestorePending_ ;
	/* true while a load/restore replays its SetStrings: Update must
	   sit out the burst (no per-notify retrigger, no queued rebuild)
	   because the deferred branch does both, once, afterwards */
	bool presetApplying_ ;
	char presetSaveName_[16] ;   // pending save, written outside the locks
	bool presetSavePending_ ;
} ;
#endif
