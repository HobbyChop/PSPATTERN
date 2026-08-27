#ifndef _PROJECT_H_
#define _PROJECT_H_

#include "Song.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Persistency/Persistent.h"
#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Observable.h"

#define VAR_TEMPO MAKE_FOURCC('T', 'M', 'P', 'O')
#define VAR_MASTERVOL   	MAKE_FOURCC('M', 'S', 'T', 'R')
#define VAR_WRAP        	MAKE_FOURCC('W', 'R', 'A', 'P')
#define VAR_LOOP        	MAKE_FOURCC('L', 'O', 'O', 'P')
#define VAR_MIDIDEVICE  	MAKE_FOURCC('M', 'I', 'D', 'I')
#define VAR_TRANSPOSE   	MAKE_FOURCC('T', 'R', 'S', 'P')
#define VAR_SOFTCLIP 		MAKE_FOURCC('S', 'F', 'T', 'C')
#define VAR_SOFTCLIP_GAIN 	MAKE_FOURCC('S', 'F', 'G', 'N')
#define VAR_PREGAIN   		MAKE_FOURCC('P', 'R', 'G', 'N')
#define VAR_SCALE 			MAKE_FOURCC('S', 'C', 'A', 'L')
#define VAR_RENDER MAKE_FOURCC('R', 'N', 'D', 'R')
#define VAR_MIDISYNC MAKE_FOURCC('M', 'S', 'Y', 'N')
/* The ten master EQ bands, EQ0..EQ9. Saved by name like every other
   project variable, so a song written before the EQ existed simply has
   no entry for them and loads flat. */
#define VAR_EQ0 MAKE_FOURCC('E', 'Q', '0', ' ')
#define VAR_EQ1 MAKE_FOURCC('E', 'Q', '1', ' ')
#define VAR_EQ2 MAKE_FOURCC('E', 'Q', '2', ' ')
#define VAR_EQ3 MAKE_FOURCC('E', 'Q', '3', ' ')
#define VAR_EQ4 MAKE_FOURCC('E', 'Q', '4', ' ')
#define VAR_EQ5 MAKE_FOURCC('E', 'Q', '5', ' ')
#define VAR_EQ6 MAKE_FOURCC('E', 'Q', '6', ' ')
#define VAR_EQ7 MAKE_FOURCC('E', 'Q', '7', ' ')
#define VAR_EQ8 MAKE_FOURCC('E', 'Q', '8', ' ')
#define VAR_EQ9 MAKE_FOURCC('E', 'Q', '9', ' ')

#define PROJECT_NUMBER "1"
#define PROJECT_RELEASE "6"
#define BUILD_COUNT "0-bacon17"

#define MAX_TAP 3

class Project: public Persistent,public VariableContainer,I_Observer  {
public:
  Project();
  ~Project();
  void Purge();
  void PurgeInstruments(bool removeFromDisk);

  Song *song_;

  int GetMasterVolume();
  bool Wrap();
  // Does the song start again when it runs out of chains, or stop?
  // Looping is the default because it is what a tracker does while
  // you are writing; "once" is for rendering and for playing the
  // thing to somebody.
  bool Loop();
  static const char *loopModes[2];
  void OnTempoTap();
  void NudgeTempo(int value);
  int GetScale();
  int GetTempo(); // Takes nudging into account
  // Used when following an external clock; clears any nudge, since
  // the external clock is now the authority on tempo.
  void SetTempo(int tempo);
  int GetTranspose();
  int GetSoftclip();
  int GetSoftclipGain();
  int GetPregain();
  int GetEqBand(int band);
  void SetEqBand(int band,int value);
  int GetRenderMode();
  // 0 = ignore an external clock, 1 = follow it
  int GetMidiSync();
  void Trigger();

  static const unsigned int MAX_RENDER_MODE = 3;
  // I_Observer
  virtual void Update(Observable &o, I_ObservableData *d);

  InstrumentBank *GetInstrumentBank();
  virtual unsigned int Checksum(unsigned int h);
  virtual void SaveContent(TiXmlNode *node);
  virtual void RestoreContent(TiXmlElement *element);

  void LoadFirstGen(const char *root);

protected:
  void buildMidiDeviceList();

private:
  InstrumentBank *instrumentBank_;
  char **midiDeviceList_;
  int midiDeviceListSize_;
  int tempoNudge_;
  unsigned long lastTap_[MAX_TAP];
  unsigned int tempoTapCount_;
};
#endif
