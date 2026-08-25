#ifndef _I_INSTRUMENT_H_
#define _I_INSTRUMENT_H_

#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Observable.h"
#include "Application/Utils/fixed.h"

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
#endif
