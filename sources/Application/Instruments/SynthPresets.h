#ifndef _SYNTH_PRESETS_H_
#define _SYNTH_PRESETS_H_

#include <string>
#include <utility>
#include <vector>

class I_Instrument ;

/* Synth presets: a preset is the instrument's parameter list as plain
   name=value lines in a .ptx file under presets/ beside the EBOOT --
   the same replay-by-name idea project load and the type-flip stash
   already use, so a preset survives engine changes and unknown names
   are simply skipped. The factory sounds ship as files like any the
   user saves; there is nothing special about them. */
namespace SynthPresets {
	// rescan presets/ ; returns the preset count
	int Scan() ;
	int Count() ;
	const char *Name(int i) ;            // display name, no extension
	// apply preset i to the instrument (engine first, then the rest)
	bool Load(int i, I_Instrument *instr) ;
	// write the instrument's parameters as <name>.ptx (overwrites)
	bool Save(const char *name, I_Instrument *instr) ;

	/* In-memory snapshot of a sound, so browsing the bank can be
	   backed out of: capture before the first load, restore when the
	   row returns to "--". Same name=value replay as the files, no
	   filesystem involved. */
	typedef std::vector<std::pair<std::string,std::string> > ParamSnapshot ;
	void Capture(I_Instrument *instr, ParamSnapshot &out) ;
	void Restore(const ParamSnapshot &snap, I_Instrument *instr) ;
} ;

#endif
