#ifndef _PROJECTDATAS_H_
#define _PROJECTDATAS_H_
static const char *softclipStates[] = {"Bypass", "Subtle", "Medium", "Heavy", "Insane"};
// No brackets: the pill draws the selected option as a filled
// block already, and the two extra cells each pushed this row out
// of the MIX panel and across the FILE panel beside it.
static const char *softclipGainStates[] = {"unity", "boost"};
static const char *renderModes[] = {"Off", "Stereo", "Stems"};
// Following an external clock is opt-in: a stray clock byte from a
// device you happened to leave plugged in should not take the
// transport away from you.
static const char *midiSyncModes[] = {"Off", "Follow"};
#define MAX_MIDISYNC_MODE 2
#endif