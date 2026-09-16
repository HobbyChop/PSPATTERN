#ifndef _PROJECTDATAS_H_
#define _PROJECTDATAS_H_
static const char *softclipStates[] = {"Bypass", "Subtle", "Medium", "Heavy", "Insane"};
// No brackets: the pill draws the selected option as a filled
// block already, and the two extra cells each pushed this row out
// of the MIX panel and across the FILE panel beside it.
static const char *softclipGainStates[] = {"unity", "boost"};
/* Stems is gone. Eight files being written at once is 1.4MB a second
   to a Memory Stick in pieces the card does not want, and it never
   worked on hardware -- the render came out glitched, which is the
   worst kind of broken for a bounce, because it looks like it worked
   until you listen to it. */
static const char *renderModes[] = {"Off", "Stereo"};
// Following an external clock is opt-in: a stray clock byte from a
// device you happened to leave plugged in should not take the
// transport away from you.
static const char *midiSyncModes[] = {"Off", "Follow"};
#define MAX_MIDISYNC_MODE 2
// MIDI IN (Project.h has the counts): saved by displayed name like
// every list variable, so these names are part of the file format
static const char *midiInChannels[] = {"omni","1","2","3","4","5","6","7","8",
                                       "9","10","11","12","13","14","15","16"};
static const char *midiInModes[] = {"cursor","keys","kit"};
#endif