#ifndef _SYNTH_INSTRUMENT_H_
#define _SYNTH_INSTRUMENT_H_

// PSPATTERN soft synth instrument (slots 0x90-0x9F).
// One instrument hosts selectable engines:
//   TONE — naive osc test engine (M3)
//   PDX  — CZ-style phase distortion (M4): a cosine read through a
//          warped phase, warp driven by the mod envelope (the DCW).
//          The DCW is the CZ "filter" — FCUT sweeps its amount.
//   VAX  — virtual analog mono (M5): 1-7 detuned unison saws/pulses
//          (2 detuned saws = the Reese) + sub square + noise into a
//          Chamberlin SVF (LP/BP/HP); cutoff rides the mod envelope,
//          FCUT/FRES table commands sweep cutoff/reso, glide on
//          legato notes.
// The mod envelope (amount + A/D/S) is engine-polymorphic: PDX reads
// it as DCW, VAX as filter env.
// One voice per tracker channel (channels are monophonic -> max 8
// voices). LSDJ model: no release stage, a voice is cut by the next
// note/KILL, so envelopes are attack/decay-to-sustain held until Stop.

#include "Application/Model/Song.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "I_Instrument.h"

#define SYP_ENGINE    MAKE_FOURCC('S','Y','E','N')
#define SYP_WAVE      MAKE_FOURCC('S','Y','W','V')
#define SYP_VOLUME    MAKE_FOURCC('S','Y','V','L')
#define SYP_ATTACK    MAKE_FOURCC('S','Y','A','T')
#define SYP_DECAY     MAKE_FOURCC('S','Y','D','C')
#define SYP_SUSTAIN   MAKE_FOURCC('S','Y','S','U')
#define SYP_RELEASE   MAKE_FOURCC('S','Y','R','L')
#define SYP_PDXWAVE   MAKE_FOURCC('S','Y','P','W')
#define SYP_DCWAMT    MAKE_FOURCC('S','Y','C','W')
#define SYP_DCWATK    MAKE_FOURCC('S','Y','C','A')
#define SYP_DCWDEC    MAKE_FOURCC('S','Y','C','D')
#define SYP_DCWSUS    MAKE_FOURCC('S','Y','C','S')
#define SYP_DCWREL    MAKE_FOURCC('S','Y','C','R')
#define SYP_VAXWAVE   MAKE_FOURCC('S','Y','V','W')
#define SYP_UNISON    MAKE_FOURCC('S','Y','U','N')
#define SYP_DETUNE    MAKE_FOURCC('S','Y','D','T')
#define SYP_PWM       MAKE_FOURCC('S','Y','P','M')
#define SYP_SUB       MAKE_FOURCC('S','Y','S','B')
#define SYP_NOISE     MAKE_FOURCC('S','Y','N','S')
#define SYP_SYNC      MAKE_FOURCC('S','Y','S','Y')
#define SYP_RING      MAKE_FOURCC('S','Y','R','G')
#define SYP_CUTOFF    MAKE_FOURCC('S','Y','F','C')
#define SYP_RESO      MAKE_FOURCC('S','Y','F','R')
#define SYP_FLTMODE   MAKE_FOURCC('S','Y','F','M')
#define SYP_GLIDE     MAKE_FOURCC('S','Y','G','L')
#define SYP_DRIVE     MAKE_FOURCC('S','Y','D','V')
#define SYP_LFODEST   MAKE_FOURCC('S','Y','L','T')
#define SYP_LFORATE   MAKE_FOURCC('S','Y','L','R')
#define SYP_LFODEPTH  MAKE_FOURCC('S','Y','L','D')
// ---- FM (4 operator) -------------------------------------------
// One block of six parameters per operator, laid out on screen as a
// column each. Written out rather than generated because a FourCC has
// to be a constant expression.
#define SYP_FMALGO    MAKE_FOURCC('F','M','A','G')
#define SYP_FMFB      MAKE_FOURCC('F','M','F','B')
#define SYP_FMR1      MAKE_FOURCC('F','M','R','1')
#define SYP_FMR2      MAKE_FOURCC('F','M','R','2')
#define SYP_FMR3      MAKE_FOURCC('F','M','R','3')
#define SYP_FMR4      MAKE_FOURCC('F','M','R','4')
#define SYP_FML1      MAKE_FOURCC('F','M','L','1')
#define SYP_FML2      MAKE_FOURCC('F','M','L','2')
#define SYP_FML3      MAKE_FOURCC('F','M','L','3')
#define SYP_FML4      MAKE_FOURCC('F','M','L','4')
#define SYP_FMD1      MAKE_FOURCC('F','M','D','1')
#define SYP_FMD2      MAKE_FOURCC('F','M','D','2')
#define SYP_FMD3      MAKE_FOURCC('F','M','D','3')
#define SYP_FMD4      MAKE_FOURCC('F','M','D','4')
#define SYP_FMA1      MAKE_FOURCC('F','M','A','1')
#define SYP_FMA2      MAKE_FOURCC('F','M','A','2')
#define SYP_FMA3      MAKE_FOURCC('F','M','A','3')
#define SYP_FMA4      MAKE_FOURCC('F','M','A','4')
#define SYP_FMC1      MAKE_FOURCC('F','M','C','1')
#define SYP_FMC2      MAKE_FOURCC('F','M','C','2')
#define SYP_FMC3      MAKE_FOURCC('F','M','C','3')
#define SYP_FMC4      MAKE_FOURCC('F','M','C','4')
#define SYP_FMS1      MAKE_FOURCC('F','M','S','1')
#define SYP_FMS2      MAKE_FOURCC('F','M','S','2')
#define SYP_FMS3      MAKE_FOURCC('F','M','S','3')
#define SYP_FMS4      MAKE_FOURCC('F','M','S','4')

#define SYP_TABLE     MAKE_FOURCC('S','Y','T','B')
#define SYP_TABLEAUTO MAKE_FOURCC('S','Y','T','A')

enum SynthEngineType {
	SET_TONE=0,
	SET_PDX,
	SET_VAX,
	SET_FM,
	SET_LAST
} ;

#define FM_OPS 4
#define FM_OUT 4          // "destination" of a carrier
#define FM_ALGO_COUNT 8

// A modulator at full level and full envelope moves its carrier's
// phase by ~2 cycles, so the modulation index tops out around 12 --
// the DX7's ballpark, and enough reach that a bell is actually
// possible. Feedback gets two shifts less: at the same scale it
// collapses into noise long before the control reaches the top.
#define FM_INDEX_SHIFT 18
// Feedback is scaled by the operator's own output level before it
// gets here, so at 16 a modulator set to anything but full level
// could not bend itself at all -- measured, the whole 0-255 control
// moved the tone by two zero crossings in four thousand.
#define FM_FB_SHIFT    17

enum VaxWaveType {
	VWT_SAW=0,
	VWT_PULSE,
	VWT_LAST
} ;

enum VaxFilterMode {
	VFM_LP=0,
	VFM_BP,
	VFM_HP,
	VFM_LAST
} ;

enum SynthLfoDest {
	SLD_OFF=0,
	SLD_PITCH,    // vibrato
	SLD_FILTER,   // vax cutoff / pdx dcw — the wobble
	SLD_PWM,      // vax pulse width
	SLD_LAST
} ;

#define VAX_MAX_UNISON 7

enum SynthWaveType {
	SWT_SAW=0,
	SWT_SQUARE,
	SWT_TRIANGLE,
	SWT_SINE,
	SWT_LAST
} ;

enum PdxWaveType {
	PWT_SAW=0,      // knee-warped cosine
	PWT_SQUARE,     // knee-warped with endpoint holds
	PWT_RESO_SAW,   // synced resonant cosine, saw window
	PWT_RESO_TRI,   // synced resonant cosine, triangle window
	PWT_LAST
} ;

// one A/D/S ramp in Q23 (FP_ONE<<8: the extra 8 bits keep long ramp
// times from quantizing away — see the M3 harness)
struct SynthRamp {
	fixed level_ ;
	fixed target_ ;
	fixed step_ ;
} ;

// per-FM-operator state
struct FmOp {
	unsigned int phase_ ;
	unsigned int inc_ ;
	SynthRamp env_ ;
} ;

// bits for SynthVoice::cmdLocks_
#define SVL_VOLUME   1
#define SVL_CUTOFF   2
#define SVL_RESO     4
#define SVL_MODAMT   8
#define SVL_DRIVE   16
#define SVL_UNISON  32
#define SVL_DETUNE  64
#define SVL_LFODEP 128
#define SVL_LFORAT 256
#define SVL_FMLV1  512
#define SVL_FMLV2 1024
#define SVL_FMLV3 2048
#define SVL_FMLV4 4096
#define SVL_FMFB  8192

struct SynthVoice {
	bool active_ ;
	unsigned int phase_ ;     // Q32 phase accumulator (TONE/PDX)
	unsigned int phaseInc_ ;  // target increment (glide/PTCH target)
	SynthRamp amp_ ;
	SynthRamp mod_ ;          // PDX: DCW / VAX: filter env
	// VAX state
	unsigned int uphase_[VAX_MAX_UNISON] ;
	unsigned int subPhase_ ;
	// hard sync: the master runs at the note, is never heard, and
	// exists only to reset the audible oscillators when it wraps
	unsigned int syncPhase_ ;
	unsigned int curInc_ ;    // glide walks this toward phaseInc_
	unsigned int lfoPhase_ ;
	unsigned int rng_ ;
	int svfLow_ ;
	int svfBand_ ;
	// FM state
	FmOp op_[FM_OPS] ;
	int fbLast_[2] ;          // op4's last two outputs; the feedback
	                          // path reads their average
	int fmLevel_[FM_OPS] ;    // FML1-FML4 per-voice override
	int fmFeedback_ ;         // FMFB
	// note-on declick: the step a phase-reset retrigger makes is
	// carried as a decaying DC tail instead of a pop
	fixed lastOut_ ;
	fixed click_ ;
	// set when something discontinuous happened between blocks (an
	// engine or wave change under a held note). The next sample
	// rendered measures its own step against lastOut_ rather than
	// assuming the new oscillator starts at zero.
	bool declickPending_ ;
	int lastEngine_ ;
	int lastWave_ ;
	// set by NoteFollowsNote, consumed by the next Start: this note
	// follows another of ours rather than starting from silence
	bool followsNote_ ;
	// Stop started the release and the voice is ringing out. The
	// channel keeps rendering while this is set; the voice goes
	// inactive when the amp envelope reaches zero.
	bool releasing_ ;
	// ---- per-voice command state -----------------------------------
	// Commands write HERE, never to the instrument's Variables: a
	// Variable is shared by every channel using the instrument AND is
	// what gets saved, so writing there let one channel's FCUT bend
	// another channel and bake itself into the project file.
	int volume_ ;             // VOLM
	int cutoff_ ;             // FCUT / FLTR (VAX)
	int reso_ ;               // FRES / FLTR (VAX)
	int modAmt_ ;             // FCUT on PDX = DCW amount
	// The synth's own character. These were patch settings only, so no
	// command could open the drive on one step or sweep detune across
	// a bar -- the table could only send the nine shared commands.
	int drive_ ;              // DRIV
	int unison_ ;             // UNIS
	int detune_ ;             // DTUN
	int lfoDepth_ ;           // LFOD
	int lfoRate_ ;            // LFOR
	// Which of the four above a command has taken over for this voice.
	// Anything unlocked follows the instrument's own value every block,
	// so turning a knob is audible on a note that's already sounding;
	// anything a command has claimed stays claimed until the next note.
	unsigned short cmdLocks_ ;
	int pan_ ;                // PAN_ 0..0xFE, 0x7F center
	int glideCoef_ ;          // pitch walk rate (0 = jump)
	unsigned char baseNote_ ; // note as played, before ARPG/PTCH
	unsigned char arpStep_ ;  // ARPG cursor
	// How many ticks each arp note holds. 1 is one position per tick,
	// which is the classic chip trill and the default. Higher values
	// walk the same figure slowly enough to read as a melody. Set by
	// ARPS and kept across notes, because an arp speed is a property of
	// the part, not of one note.
	unsigned char arpRate_ ;
	unsigned char arpTick_ ;  // ticks counted toward the next position
	unsigned short arpData_ ; // ARPG abcd nibbles
	bool arpOn_ ;
	unsigned char rtgTicks_ ; // RTRG period in ticks (0 = off)
	unsigned char rtgCount_ ;
	int tickAcc_ ;            // sample counter for tick-rate commands
	int tickLen_ ;
} ;

class SynthInstrument: public I_Instrument, I_Observer {
public:
	SynthInstrument() ;
	virtual ~SynthInstrument() ;

	virtual bool Init() ;
	virtual bool Start(int channel,unsigned char note,bool retrigger=true) ;
	virtual void Stop(int channel) ;
	virtual void NoteFollowsNote(int channel) ;
	virtual bool IsReleasing(int channel) ;
	virtual void OnStart() ;
	virtual bool Render(int channel,fixed *buffer,int size,bool updateTick) ;
	virtual bool IsInitialized() ;
	virtual bool IsEmpty() ;
	virtual bool IsAtDefaults() ;
	virtual InstrumentType GetType() ;
	virtual const char *GetName() ;
	virtual void ProcessCommand(int channel,FourCC cc,ushort value) ;
	virtual void Purge() ;
	virtual int GetTable() ;
	virtual bool GetTableAutomation() ;
	virtual void GetTableState(TableSaveState &state) ;
	virtual void SetTableState(TableSaveState &state) ;

	// I_Observer: engine switch rebuilds the instrument screen
	virtual void Update(Observable &o,I_ObservableData *d) ;

private:
	void startRamp(SynthRamp &r,int attack,bool fromCurrent) ;
	void releaseRamp(SynthRamp &r,int release) ;
	void stepRamp(SynthRamp &r,fixed sustain,int decay) ;
	bool renderTone(SynthVoice &v,fixed *buffer,int size) ;
	bool renderPdx(SynthVoice &v,fixed *buffer,int size) ;
	bool renderVax(SynthVoice &v,fixed *buffer,int size) ;
	bool renderFm(SynthVoice &v,fixed *buffer,int size) ;
	void startFmOps(SynthVoice &v,bool fromCurrent) ;
	void releaseFmOps(SynthVoice &v) ;
	void setFmPitch(SynthVoice &v) ;
	void fmMultipliers(int *mulQ16) ;
	// tick-rate command work (ARPG steps, RTRG retriggers), called
	// from the renderers as the voice crosses tick boundaries
	void serviceTicks(SynthVoice &v,int channel,int samples) ;
	void retrigger(SynthVoice &v) ;
	void setVoicePitch(SynthVoice &v,int note) ;

	SynthVoice voice_[SONG_CHANNEL_COUNT] ;
	unsigned char lastNote_[SONG_CHANNEL_COUNT] ;
	TableSaveState tableState_ ;
	char name_[16] ;

	static bool tablesBuilt_ ;
	static unsigned int noteInc_[128] ;
	static unsigned int lfoInc_[256] ;
	static short sineTable_[256] ;
	static short cosTable_[1025] ;
	static short cutTable_[256] ;   // cutoff param -> SVF f coeff (Q15)
	// FM needs a finer table than the 256-entry one the other engines
	// use: at 8 bits a modulator's own quantisation lands in the
	// sidebands, which is exactly where it is audible.
	static short fmSin_[1024] ;
	static short fmSinD_[1024] ;
} ;
#endif
