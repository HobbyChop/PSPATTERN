#ifndef _MIDI_INSTRUMENT_H_
#define _MIDI_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Application/Model/Song.h"
#include "Services/Midi/MidiService.h"

#define MIDI_NOTE_ON 0x90
#define MIDI_NOTE_OFF 0x80
#define MIDI_CC 0xB0
#define MIDI_PRG 0xC0
#define MIDI_AFTERTOUCH 0xD0
#define MIDI_PITCHBEND 0xE0

// The CC numbers the shared musical commands map onto. These are the
// General MIDI assignments every synth and every DAW already
// understands, so PAN_ and FCUT on a MIDI channel do the obvious thing
// without the user having to look a number up.
#define MIDI_CC_VOLUME  7
#define MIDI_CC_PAN    10
#define MIDI_CC_RESO   71
#define MIDI_CC_CUTOFF 74

#define MIP_CHANNEL			MAKE_FOURCC('C','H','N','L')
#define MIP_NOTELENGTH		MAKE_FOURCC('L','E','N','G')
#define MIP_VOLUME    		MAKE_FOURCC('V','O','L','M')
#define MIP_TABLE			MAKE_FOURCC('T','A','B','L')
#define MIP_TABLEAUTO		MAKE_FOURCC('T','B','L','A')
#define MIP_BENDRANGE		MAKE_FOURCC('B','N','D','R')

// Four assignable controllers, sent at note-on and addressable from the
// pattern with MCCA..MCCD. A device that needs a filter opened and a
// reverb send set before it plays can carry that in the patch.
#define MIDI_CC_SLOTS 4
#define MIP_CC1NUM			MAKE_FOURCC('C','1','N','U')
#define MIP_CC1VAL			MAKE_FOURCC('C','1','V','A')
#define MIP_CC2NUM			MAKE_FOURCC('C','2','N','U')
#define MIP_CC2VAL			MAKE_FOURCC('C','2','V','A')
#define MIP_CC3NUM			MAKE_FOURCC('C','3','N','U')
#define MIP_CC3VAL			MAKE_FOURCC('C','3','V','A')
#define MIP_CC4NUM			MAKE_FOURCC('C','4','N','U')
#define MIP_CC4VAL			MAKE_FOURCC('C','4','V','A')

/* One instrument can be playing on several tracker channels at once.
   Note length, retrigger, velocity, bend and arpeggio all used to be
   single members, so a second channel using the same MIDI instrument
   overwrote the first one's state. */
struct MidiVoice {
	int  lastNote_ ;        // the note currently sounding
	int  baseNote_ ;        // as written, before ARPG and PTCH
	bool first_ ;           // note-on still to be sent
	bool playing_ ;
	int  remainingTicks_ ;  // note length countdown, -1 = hold
	bool retrig_ ;
	int  retrigLoop_ ;
	int  velocity_ ;
	// pitch bend, in 1/256 semitone so a slide has somewhere to walk
	int  bend_ ;
	// VIBR rides beside the bend rather than in it, so a slide and a
	// wobble can happen at once instead of overwriting each other.
	int  vibBend_ ;          // 1/256 semitone, like bend_
	unsigned short vibSpeed_ ;
	unsigned char  vibDepth_ ;
	unsigned short vibPhase_ ;
	int  bendTarget_ ;
	int  bendRate_ ;        // 0 = jump straight there
	int  bentSent_ ;        // last 14-bit value actually sent
	bool arpOn_ ;
	unsigned short arpData_ ;
	int  arpStep_ ;
	int  arpTick_ ;
} ;

class MidiInstrument:public I_Instrument {

public:
	MidiInstrument();
	virtual ~MidiInstrument() ;

	  virtual bool Init() ;

	  // Start & stop the instument
      virtual bool Start(int channel,unsigned char note,bool retrigger=true) ;
      virtual void Stop(int channel) ;

      // size refers to the number of samples
      // should always fill interleaved stereo / 16bit
      virtual bool Render(int channel,fixed *buffer,int size,bool updateTick) ;
	  virtual void ProcessCommand(int channel,FourCC cc,ushort value) ;

      virtual bool IsInitialized() ;

	  virtual bool IsEmpty() { return false ; } ;
	  virtual bool IsAtDefaults() ;

	  virtual InstrumentType GetType() { return IT_MIDI ; } ;

	   virtual const char *GetName() ;

	  virtual void OnStart() ;

	   virtual void Purge() {} ;

	   virtual int GetTable() ;
	   virtual bool GetTableAutomation();
	   virtual void GetTableState(TableSaveState &state) ;
	   virtual void SetTableState(TableSaveState &state) ;

	  // external parameter list

	  void SetChannel(int i);

 private:
	  int midiChannel() ;
	  void sendCC(int controller,int value) ;
	  void sendNoteOn(MidiVoice &v) ;
	  void sendNoteOff(MidiVoice &v) ;
	  void sendBend(MidiVoice &v) ;
	  void stepBend(MidiVoice &v) ;
	  void setBendSemitones(MidiVoice &v,int semis,int rate) ;
	  void sendPatchControllers() ;

	  char name_[20] ;  // Instrument name
	  MidiVoice voice_[SONG_CHANNEL_COUNT] ;
	  TableSaveState tableState_ ;

	  static MidiService* svc_ ;
} ;

#endif
