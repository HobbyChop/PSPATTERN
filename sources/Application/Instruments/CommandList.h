
#ifndef _COMMAND_LIST_H_
#define _COMMAND_LIST_H_

#include "Foundation/Types/Types.h"

#define I_CMD_NONE MAKE_FOURCC('-','-','-','-')
#define I_CMD_KILL MAKE_FOURCC('K','I','L','L')
#define I_CMD_LPOF MAKE_FOURCC('L','P','O','F')
#define I_CMD_ARPG MAKE_FOURCC('A','R','P','G')
#define I_CMD_ARPS MAKE_FOURCC('A','R','P','S')
#define I_CMD_VOLM MAKE_FOURCC('V','O','L','M')
#define I_CMD_PTCH MAKE_FOURCC('P','T','C','H')
#define I_CMD_HOP  MAKE_FOURCC('H','O','P',' ')
#define I_CMD_LEGA MAKE_FOURCC('L','E','G','A')
#define I_CMD_RTRG MAKE_FOURCC('R','T','R','G')
// A chord is an arpeggio you do not have to set the speed for. The
// channels are monophonic, so this is what a chord CAN be here -- and
// it is what it is on the trackers this borrows from too.
#define I_CMD_CHRD MAKE_FOURCC('C','H','R','D')
// The shape of a retrigger: how much quieter and how much higher each
// repeat is than the one before it.
#define I_CMD_RTGR MAKE_FOURCC('R','T','G','R')
// Move the note off its grid a little, differently every time.
#define I_CMD_RAND MAKE_FOURCC('R','A','N','D')
// Shift the note by whole semitones, and keep shifting it. This is
// what a table's transpose column emits, and it is useful typed into
// a step on its own.
#define I_CMD_TRSP MAKE_FOURCC('T','R','S','P')
#define I_CMD_TMPO MAKE_FOURCC('T','M','P','O')
#define I_CMD_MDCC MAKE_FOURCC('M','D','C','C')
#define I_CMD_MDPG MAKE_FOURCC('M','D','P','G')
#define I_CMD_MDPB MAKE_FOURCC('M','D','P','B')
#define I_CMD_MDAT MAKE_FOURCC('M','D','A','T')
// the four assignable controllers on a MIDI instrument
#define I_CMD_MCCA MAKE_FOURCC('M','C','C','A')
#define I_CMD_MCCB MAKE_FOURCC('M','C','C','B')
#define I_CMD_MCCC MAKE_FOURCC('M','C','C','C')
#define I_CMD_MCCD MAKE_FOURCC('M','C','C','D')
#define I_CMD_MVEL MAKE_FOURCC('M','V','E','L')
#define I_CMD_PLOF MAKE_FOURCC('P','L','O','F')
#define I_CMD_FLTR MAKE_FOURCC('F','L','T','R')
#define I_CMD_TABL MAKE_FOURCC('T','A','B','L')
#define I_CMD_CRSH MAKE_FOURCC('C','R','S','H')
#define I_CMD_FCUT MAKE_FOURCC('F','C','U','T')
#define I_CMD_FRES MAKE_FOURCC('F','R','E','S')
#define I_CMD_PAN_ MAKE_FOURCC('P','A','N',' ')
#define I_CMD_GROV MAKE_FOURCC('G','R','O','V')
#define I_CMD_IRTG MAKE_FOURCC('I','R','T','G')
#define I_CMD_PFIN MAKE_FOURCC('P','F','I','N')
#define I_CMD_DLAY MAKE_FOURCC('D','L','A','Y')
#define I_CMD_FBMX MAKE_FOURCC('F','B','M','X')
#define I_CMD_FBTN MAKE_FOURCC('F','B','T','N')
#define I_CMD_STOP MAKE_FOURCC('S','T','O','P')
// the synth's own character, which no command could reach before:
// all of these were patch settings only
#define I_CMD_DRIV MAKE_FOURCC('D','R','I','V')
#define I_CMD_UNIS MAKE_FOURCC('U','N','I','S')
#define I_CMD_DTUN MAKE_FOURCC('D','T','U','N')
#define I_CMD_LFOD MAKE_FOURCC('L','F','O','D')
#define I_CMD_LFOR MAKE_FOURCC('L','F','O','R')
// FM: an operator's level is where the automation goes -- sweeping a
// modulator is the timbral move, the way cutoff is on a filter.
#define I_CMD_FML1 MAKE_FOURCC('F','M','L','1')
#define I_CMD_FML2 MAKE_FOURCC('F','M','L','2')
#define I_CMD_FML3 MAKE_FOURCC('F','M','L','3')
#define I_CMD_FML4 MAKE_FOURCC('F','M','L','4')
#define I_CMD_FMFB MAKE_FOURCC('F','M','F','B')

// Which instrument types a command actually reaches. The picker offers
// every command on every channel and an instrument silently ignores
// what it does not implement, so without this the user has no way to
// tell a wrong parameter from a command that was never going to work
// on that channel.
#define CMD_ON_SAMPLE 1
#define CMD_ON_SYNTH  2
#define CMD_ON_MIDI   4
#define CMD_ON_ALL    (CMD_ON_SAMPLE|CMD_ON_SYNTH|CMD_ON_MIDI)

class CommandList {
public:
	// bitmask of CMD_ON_*; CMD_ON_ALL for the player and table commands
	static int AppliesTo(FourCC command) ;
	static FourCC GetNext(FourCC current) ;
	static FourCC GetPrev(FourCC current) ;
	static FourCC GetNextAlpha(FourCC current) ;
    static FourCC GetPrevAlpha(FourCC current);
    static int GetCount() ;
	static FourCC GetAt(int index) ;
	static int IndexOf(FourCC current) ;
	static FourCC GetFirst() ;
	static FourCC GetLast() ;
	static bool IsFirst(FourCC current) ;
	static bool IsLast(FourCC current) ;
};
#endif

