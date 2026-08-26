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
#endif