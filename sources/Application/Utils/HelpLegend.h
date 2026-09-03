#ifndef _HELP_LEGEND_H_
#define _HELP_LEGEND_H_

#include <string>
#include <stdlib.h>
#include <string.h>

static inline std::string* getHelpLegend(FourCC command) {
	// One shared buffer, not a fresh allocation. This used to
	// `new std::string[3]` and neither caller ever deleted it, so it
	// leaked three strings on every frame the command help was on
	// screen -- which is every frame the cursor sits on a command.
	// The result is consumed immediately by the caller that asked for
	// it; nothing holds on to it.
	static std::string result[3];
	result[0].clear();
	result[1].clear();
	result[2].assign("bb at speed aa");
	switch (command) {
		case I_CMD_KILL:
			result[0].assign("KILl:--bb");
			result[1].assign("stop playing note");
			result[2].assign("after bb ticks");
			break;
		case I_CMD_LPOF:
			result[0].assign("LooP OFset: Shift both");
			result[1].assign("the loop start & loop ");
			result[2].assign("end values aaaa digits");
			break;
		case I_CMD_ARPG:
			result[0].assign("ARPeGgio:abcd Cycle");
			result[1].assign("through relative pitches");
			result[2].assign("from original pitch");
			break;
		case I_CMD_ARPS:
			result[0].assign("ARp Speed:--bb");
			result[1].assign("ticks each arp note holds,");
			result[2].assign("1 = every tick (default)");
			break;
		case I_CMD_CHRD:
			result[0].assign("CHoRD:abcd four relative");
			result[1].assign("pitches cycled every tick,");
			result[2].assign("no ARPS needed");
			break;
		case I_CMD_RTGR:
			result[0].assign("ReTriGgeR shape:aabb");
			result[1].assign("aa volume, bb pitch per");
			result[2].assign("repeat, signed. Beside RTRG");
			break;
		case I_CMD_RAND:
			result[0].assign("RANDom:aabb scatter this");
			result[1].assign("note aa semitones and");
			result[2].assign("bb of level. Down only");
			break;
		case I_CMD_TRSP:
			result[0].assign("TRanSPose:--bb shift the");
			result[1].assign("note bb semitones and");
			result[2].assign("hold it there. Signed");
			break;
		case I_CMD_MAYB:
			result[0].assign("MAYBe:aa-b odds aa out of");
			result[1].assign("FF. In a phrase the note");
			result[2].assign("plays, in a table hop to b");
			break;
		case I_CMD_VIBR:
			result[0].assign("VIBRato:aabb speed aa,");
			result[1].assign("depth bb in 1/16 semitone");
			result[2].assign("so 10 is one. 0000 off");
			break;
		case I_CMD_VOLM:
			result[0].assign("VOLuMe:aabb");
			result[1].assign("approach volume");
			break;
		case I_CMD_PTCH:
			result[0].assign("PiTCH:aabb");
			result[1].assign("approach pitch");
			break;
		case I_CMD_HOP:
			result[0].assign("HOP:aabb");
			result[1].assign("hop to bb");
			result[2].assign("aa times");
			break;
		case I_CMD_LEGA:
			result[0].assign("LEGAto: slide from");
			result[1].assign("previous note to pitch");
			break;
		case I_CMD_RTRG:
			result[0].assign("ReTRiG:aabb retrigger loop");
			result[1].assign("from current position over");
			result[2].assign("bb ticks at speed aa");
			break;
		case I_CMD_TMPO:
			result[0].assign("TeMPO:--bb");
			result[1].assign("sets the tempo to hex");
			result[2].assign("value bb");
			break;
		case I_CMD_MDCC:
			result[0].assign("MiDiCC:aabb");
			result[1].assign("CC message aa");
			result[2].assign("value bb");
			break;
		case I_CMD_DRIV:
			result[0].assign("DRIVe:--bb");
			result[1].assign("post filter drive,");
			result[2].assign("vax engine");
			break;
		case I_CMD_UNIS:
			result[0].assign("UNISon:--bb");
			result[1].assign("voices in the stack,");
			result[2].assign("1-7, vax engine");
			break;
		case I_CMD_DTUN:
			result[0].assign("DeTUNe:--bb");
			result[1].assign("spread of the unison");
			result[2].assign("stack, vax engine");
			break;
		case I_CMD_LFOD:
			result[0].assign("LFO Depth:--bb");
			result[1].assign("how far the lfo swings");
			result[2].assign("");
			break;
		case I_CMD_LFOR:
			result[0].assign("LFO Rate:--bb");
			result[1].assign("how fast the lfo runs");
			result[2].assign("");
			break;
		case I_CMD_LFO_:
			result[0].assign("LFO Free:aabb");
			result[1].assign("aa rate bb depth, on the");
			result[2].assign("channel until 0000");
			break;
		case I_CMD_FML1:
		case I_CMD_FML2:
		case I_CMD_FML3:
		case I_CMD_FML4:
			result[0].assign("FM Level 1-4:--bb");
			result[1].assign("operator output level,");
			result[2].assign("a modulator's = brightness");
			break;
		case I_CMD_FMFB:
			result[0].assign("FM FeedBack:--bb");
			result[1].assign("op4 self-modulation,");
			result[2].assign("fm engine");
			break;
		case I_CMD_MDPB:
			result[0].assign("MiDi Pitch Bend:aaaa");
			result[1].assign("raw 14 bit bend,");
			result[2].assign("2000 is centre");
			break;
		case I_CMD_MDAT:
			result[0].assign("MiDi AfterTouch:--bb");
			result[1].assign("channel pressure bb");
			result[2].assign("");
			break;
		case I_CMD_MCCA:
		case I_CMD_MCCB:
		case I_CMD_MCCC:
		case I_CMD_MCCD:
			result[0].assign("MiDi CC slot:--bb");
			result[1].assign("send value bb on the");
			result[2].assign("instrument's cc1..cc4");
			break;
		case I_CMD_MDPG:
			result[0].assign("MiDi ProGram Change");
			result[1].assign("send program change bb");
			result[2].assign("to current channel");
			break;
		case I_CMD_MVEL:
			result[0].assign("MidiVELocity:--bb");
			result[1].assign("Set velocity bb for step");
			result[2].assign("");
	    break;
		case I_CMD_PLOF:
			result[0].assign("PLay OFfset:aabb");
			result[1].assign("jump abs to aa or");
			result[2].assign("move rel bb chunks");
			break;
		case I_CMD_FLTR:
			result[0].assign("FiLTer&Resonance:aabb");
			result[1].assign("cutoff aa");
			result[2].assign("resonance bb");
			break;
		case I_CMD_TABL:
			result[0].assign("TABLe:--bb");
			result[1].assign("trigger table bb");
			result[2].assign("");
			break;
		case I_CMD_CRSH:
			result[0].assign("drive&CRuSH:aa-b");
			result[1].assign("drive aa");
			result[2].assign("crush -b");
			break;
		case I_CMD_FCUT:
			result[0].assign("FilterCUToff:aabb");
			result[1].assign("set cutoff to");
			break;
		case I_CMD_FRES:
			result[0].assign("FilterRESonance:aabb");
			result[1].assign("set resonance to");
			break;
		case I_CMD_PAN_:
			result[0].assign("PAN:aabb");
			result[1].assign("pan to value");
			break;
		case I_CMD_GROV:
			result[0].assign("GROoVe:--bb");
			result[1].assign("trigger groove bb");
			result[2].assign("");
			break;
		case I_CMD_IRTG:
			result[0].assign("InstrumentReTriG:aabb");
			result[1].assign("retrig and transpose to");
			break;
		case I_CMD_PFIN:
			result[0].assign("PitchFINetune:aabb");
			result[1].assign("fine tune to ");
			break;
		case I_CMD_DLAY:
			result[0].assign("DeLAY:--bb");
			result[1].assign("delay bb tics");
			result[2].assign("");
			break;
		case I_CMD_FBMX:
			result[0].assign("FeedBack MiX:aabb");
			result[1].assign("feedback mix to");
			break;
		case I_CMD_FBTN:
			result[0].assign("FeedBack TuNe:aabb");
			result[1].assign("feedback tune to");
			break;
		case I_CMD_STOP:
			result[0].assign("STOP playing song");
			result[1].assign("immediately");
			result[2].assign("");
			break;
		default:

			result[0].assign("");
			result[1].assign("");
			result[2].assign("");
		break;
	}
	return result;
}

#endif //_HELP_LEGEND_H_
