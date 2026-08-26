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
	virtual void OnFocus() ;

	// The routing picture under the operator columns. See the
	// definition for why it is drawn the way it is.
	void drawFmAlgo(SynthInstrument *instrument) ;

	// answer to the "replace instrument?" prompt
	void ConfirmTypeChange(bool go) ;

protected:
	void warpToNext(int offset) ;
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
	// type the user picked while the confirmation was up
	InstrumentType pendingType_ ;
} ;
#endif
