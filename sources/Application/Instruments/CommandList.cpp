
#include "CommandList.h"

static FourCC _all[]= {
	I_CMD_NONE,
	I_CMD_ARPG,
	I_CMD_ARPS,
	I_CMD_CHRD,
	I_CMD_RTGR,
	I_CMD_RAND,
	I_CMD_TRSP,
	I_CMD_CRSH,
	I_CMD_DLAY,
	I_CMD_DRIV,
	I_CMD_DTUN,
	I_CMD_FBMX,
	I_CMD_FBTN,
	I_CMD_FCUT,
	I_CMD_FMFB,
	I_CMD_FML1,
	I_CMD_FML2,
	I_CMD_FML3,
	I_CMD_FML4,
	I_CMD_FLTR,
	I_CMD_FRES,
	I_CMD_GROV,
	I_CMD_HOP,
	I_CMD_IRTG,
	I_CMD_KILL,
	I_CMD_LEGA,
	I_CMD_LFOD,
	I_CMD_LFOR,
	I_CMD_LPOF,
	I_CMD_MCCA,
	I_CMD_MCCB,
	I_CMD_MCCC,
	I_CMD_MCCD,
	I_CMD_MDAT,
	I_CMD_MDCC,
	I_CMD_MDPB,
	I_CMD_MDPG,
	I_CMD_MVEL,
	I_CMD_PAN_,
	I_CMD_PFIN,
	I_CMD_PLOF,
	I_CMD_PTCH,
	I_CMD_RTRG,
	I_CMD_STOP,
	I_CMD_TABL,
	I_CMD_TMPO,
	I_CMD_UNIS,
	I_CMD_VOLM
} ;

/* Read straight off each instrument's ProcessCommand, plus the
   commands the player and the table engine handle themselves. */
int CommandList::AppliesTo(FourCC command) {
	switch (command) {
		// handled by the player or the table engine: any channel
		case I_CMD_TMPO: case I_CMD_GROV: case I_CMD_HOP:
		case I_CMD_TABL: case I_CMD_KILL: case I_CMD_IRTG:
		case I_CMD_DLAY: case I_CMD_STOP:
			return CMD_ON_ALL ;

		// every instrument implements these
		case I_CMD_VOLM: case I_CMD_RTRG:
			return CMD_ON_ALL ;

		// the shared musical ones -- MIDI sends them as bend and CCs
		case I_CMD_TRSP:
		case I_CMD_PTCH: case I_CMD_ARPG: case I_CMD_ARPS: case I_CMD_CHRD:
		case I_CMD_LEGA:
		case I_CMD_PAN_: case I_CMD_FCUT: case I_CMD_FRES:
		case I_CMD_FLTR:
			return CMD_ON_ALL ;

		// sampler only
		case I_CMD_CRSH: case I_CMD_FBTN: case I_CMD_FBMX:
		case I_CMD_PLOF: case I_CMD_LPOF: case I_CMD_PFIN:
			return CMD_ON_SAMPLE ;

		// synth only
		case I_CMD_RTGR: case I_CMD_RAND:
		case I_CMD_DRIV: case I_CMD_UNIS: case I_CMD_DTUN:
		case I_CMD_LFOD: case I_CMD_LFOR:
		case I_CMD_FML1: case I_CMD_FML2: case I_CMD_FML3:
		case I_CMD_FML4: case I_CMD_FMFB:
			return CMD_ON_SYNTH ;

		// MIDI only
		case I_CMD_MDCC: case I_CMD_MDPG: case I_CMD_MVEL:
		case I_CMD_MDPB: case I_CMD_MDAT:
		case I_CMD_MCCA: case I_CMD_MCCB:
		case I_CMD_MCCC: case I_CMD_MCCD:
			return CMD_ON_MIDI ;
	}
	// "----" and anything new: offer it everywhere rather than hide it
	return CMD_ON_ALL ;
}

int CommandList::GetCount() { return sizeof(_all) / sizeof(FourCC); }

FourCC CommandList::GetAt(int index) {
	int count = GetCount() ;
	if (count <= 0) {
		return I_CMD_NONE ;
	}
	while (index < 0) {
		index += count;
	}
	index %= count;
	return _all[index] ;
}

int CommandList::IndexOf(FourCC current) {
	for (int i=0;i<GetCount();i++) {
		if (_all[i] == current) {
			return i;
		}
	}
	return -1;
}

FourCC CommandList::GetNext(FourCC current) {
	for (uint i=0;i<sizeof(_all)/sizeof(FourCC)-1;i++) {
		if (_all[i]==current) {
			return _all[i+1] ;
		} ;
	} ;
    // Wrap around: if current is last, return first
    if (_all[sizeof(_all)/sizeof(FourCC)-1] == current) {
        return _all[0];
    }
	return _all[0] ;
} ;

FourCC CommandList::GetPrev(FourCC current) {
    uint count=sizeof(_all)/sizeof(FourCC) ;
    for (uint i = 1; i < count; i++) {
        if (_all[i]==current) {
            return _all[i - 1];
        } ;
    };
    // Wrap around: if current is first, return last
    if (_all[0] == current) {
        return _all[count - 1];
    }
	return _all[count-1] ;
} ;

FourCC CommandList::GetNextAlpha(FourCC current) {
	char letter=((char *)&current)[0];
	bool found=false ;
	for (uint i=0;i<sizeof(_all)/sizeof(FourCC);i++) {
		char tLetter=((char *)&_all[i])[0];
		if (!found) {
			if (tLetter==letter) {
				found=true ;
			}
		} else {
			if (tLetter!=letter) {
				return _all[i] ;
			}
		} ;
	} ;
	return current ;
} ;

FourCC CommandList::GetPrevAlpha(FourCC current) {

	char letter=((char *)&current)[0];
	bool found=false ;
	FourCC tReturn=0xFFFFFFFF ;
	uint count=sizeof(_all)/sizeof(FourCC) ;

	for (uint i=count-1;i>0;i--) {
		char tLetter=((char *)&_all[i])[0];
		if (!found) {
			if (tLetter==letter) {
				found=true ;
			}
		} else {
			if (tLetter!=letter) {
				if (tReturn==0xFFFFFFFF) {
					tReturn=_all[i] ;
				} else {
					if (tLetter!=((char *)&tReturn)[0]) {
						return tReturn ;
					} else {
						tReturn=_all[i] ;
					}
				}
			}
		} ;
	} ;
	if (tReturn!=0xFFFFFFFF) {
		return tReturn ;
	} 
	return current ;
} ;

FourCC CommandList::GetFirst() { return _all[0]; }

FourCC CommandList::GetLast() {
	uint count = sizeof(_all)/sizeof(FourCC) ;
	return _all[count-1] ;
}

bool CommandList::IsFirst(FourCC current) {
	return current == _all[0] ;
}

bool CommandList::IsLast(FourCC current) {
	uint count = sizeof(_all)/sizeof(FourCC) ;
	return current == _all[count-1] ;
}
