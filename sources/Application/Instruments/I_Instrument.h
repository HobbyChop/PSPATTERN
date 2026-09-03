#ifndef _I_INSTRUMENT_H_
#define _I_INSTRUMENT_H_

#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Observable.h"
#include "Application/Utils/fixed.h"

#include <string.h>

#include "Application/Player/TablePlayback.h"

enum InstrumentType {
	IT_SAMPLE=0,
	IT_MIDI,
	IT_SYNTH,
	IT_LAST
} ;

class I_Instrument:public VariableContainer, public Observable {
      
public:
	I_Instrument() {} ;
	virtual ~I_Instrument() {} ;

	  // Initialisation routine

	  virtual bool Init()=0 ;

	  // True when nothing here has been changed from the values a
	  // freshly created instrument of this type starts with.
	  //
	  // Deliberately NOT IsEmpty. IsEmpty decides what gets written to
	  // the save file, which is why a synth patch reports false there
	  // unconditionally: it always has to be saved. Asking that
	  // question to decide whether to warn somebody meant every synth
	  // and every midi instrument warned about losing settings that
	  // did not exist, on a patch nobody had touched.
	  virtual bool IsAtDefaults() { return false ; } ;

	  // Compares every parameter against another instrument of the
	  // same type, which in practice is a freshly constructed one. One
	  // parameter may be skipped: the sample a new project assigns by
	  // itself, and the midi channel the bank stamps in by index, are
	  // both set for you rather than by you, and neither is worth a
	  // confirmation dialog on its own.
	  bool SameParametersAs(I_Instrument &other, FourCC skip = 0) {
		  IteratorPtr<Variable> mine(GetIterator()) ;
		  IteratorPtr<Variable> theirs(other.GetIterator()) ;
		  for (mine->Begin(), theirs->Begin() ;
		       !mine->IsDone() && !theirs->IsDone() ;
		       mine->Next(), theirs->Next()) {
			  Variable &a=mine->CurrentItem() ;
			  Variable &b=theirs->CurrentItem() ;
			  if (a.GetID()==skip) continue ;
			  // GetString formats into a buffer owned by the Variable
			  // itself, so the two calls do not tread on each other.
			  if (strcmp(a.GetString(),b.GetString())!=0) return false ;
		  }
		  return mine->IsDone() && theirs->IsDone() ;
	  } ;

	  // Start & stop the instument
      virtual bool Start(int channel,unsigned char note,bool retrigger=true)=0 ;
      virtual void Stop(int channel)=0 ;

	  // Called immediately before Start when this same instrument
	  // already had a note sounding on this channel -- i.e. this note
	  // follows another rather than beginning after silence. The player
	  // stops the old note before starting the new one, so by the time
	  // Start runs the instrument can no longer tell the difference,
	  // and sliding between notes depends on knowing. Non-pure: only
	  // instruments that slide need to care.
	  virtual void NoteFollowsNote(int channel) {} ;

	  // Is there still sound to make on this channel after Stop? An
	  // instrument with a release stage says yes, and the channel keeps
	  // rendering it until this goes false instead of cutting the
	  // waveform dead. Non-pure: everything without a release is cut,
	  // which is what all of these did before.
	  virtual bool IsReleasing(int channel) { return false ; } ;

	  // Engine playback  start callback

	  virtual void OnStart()=0 ;

	  // the song stopped: whatever a command left running on the
	  // instrument (a free LFO) ends here. Non-pure: most have nothing.
	  virtual void OnStop() {} ;

      // size refers to the number of samples
      // should always fill interleaved stereo / 16bit
      
      virtual bool Render(int channel,fixed *buffer,int size,bool updateTick)=0 ;

      virtual bool IsInitialized()=0 ;

	  virtual bool IsEmpty()=0 ;

	  virtual InstrumentType GetType()=0 ;

	  virtual const char *GetName()=0 ; 
	 
	  virtual void ProcessCommand(int channel,FourCC cc,ushort value)=0 ;

	  virtual void Purge()=0 ;

	  virtual int GetTable()=0 ;
	  virtual bool GetTableAutomation()=0 ;

	  virtual void GetTableState(TableSaveState &state)=0 ;	 
	  virtual void SetTableState(TableSaveState &state)=0 ;	 

};

/* The shortest fade either engine will do.
 *
 * A parameter of 0 means "instant", and instant used to mean one
 * sample, which is a step and therefore a click. It has been a fade
 * for a while, but at 64 samples -- 1.45ms -- it was short enough that
 * the corner at each end of the ramp still ticked on a clean part with
 * nothing to hide it. 128 samples is 2.9ms: far too short to read as
 * an attack or a release, long enough that the corner is below what
 * you can hear as a separate event.
 *
 * It is used for attack, decay and release alike, in both engines, so
 * a value of 0 anywhere means the same thing everywhere.
 */
#define DECLICK_FADE_SAMPLES 128

#endif
