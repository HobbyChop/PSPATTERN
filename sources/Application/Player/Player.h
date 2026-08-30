#ifndef _PLAYER_H_
#define _PLAYER_H_
#include "Foundation/T_Singleton.h"
#include "Foundation/Observable.h"
#include "System/Timer/Timer.h"
#include "Application/Views/ViewData.h"
#include "Application/Views/BaseClasses/ViewEvent.h"
#include "PlayerMixer.h"
#include "SyncMaster.h"
#include "ClockSync.h"

enum PlayerEventType {
	PET_START,
	PET_UPDATE,
	PET_STOP
} ;

enum SequencerMode {
     SM_SONG,
     SM_LIVE
} ;

enum QueueingMode {
     QM_NONE,
     QM_CHAINSTART,
     QM_PHRASESTART,
     QM_CHAINSTOP,
	 QM_PHRASESTOP,
	 QM_TICKSTART
} ;

class PlayerEvent: public ViewEvent {
public:
	PlayerEvent(PlayerEventType type,unsigned int tickCount=0) ;
	PlayerEventType GetType() ;
	unsigned int GetTickCount() ;
private:
	PlayerEventType type_ ;
	unsigned int tickCount_ ;
} ;

class Player: public I_Observer,public Observable,public T_Singleton<Player> {
private: // Singleton
	Player() ;
public:


	static Player *GetInstance() ;
	bool Init(Project*,ViewData *) ;
	void Reset() ;
	void Close() ;

    virtual void Update(Observable &o, I_ObservableData *d);

    // basic interface

    void Start(PlayMode mode, bool forceSongMode);
    void Stop() ;

    //	void Toggle(PlayMode mode,bool forceSongMode=false) ;
    //	void ChangePlayMode(PlayMode mode) ;
    //	PlayMode GetPlayMode() ;

    void SetSequencerMode(SequencerMode mode);

    /* Play the instrument you are looking at from an incoming MIDI
       keyboard. The adapter could only ever send; without this there is
       no way to play the synth from a keyboard at all, which is the
       demo that sells the hardware. Ignored while the sequencer runs,
       so a keyboard cannot fight the song for a channel. */
    void MidiNoteOn(unsigned char note,unsigned char velocity);
    void MidiNoteOff(unsigned char note);
    void MidiAllNotesOff();
    // hard-release any voice rendering this instrument (pre-delete)
    void CutInstrument(I_Instrument *instr);
    SequencerMode GetSequencerMode() ;

    void OnStartButton(PlayMode origin, unsigned int from,
                       bool startFromLastPos, unsigned char chainPos);
    /* fromSync marks a press that came from the leader's transport
       rather than from the buttons. In Follow the two mean opposite
       things: a button arms the song to wait, the leader's start is
       what releases it. */
    void OnSongStartButton(unsigned int from,unsigned int to,bool requestStop,bool forceImmediate,bool fromSync=false) ;

    /* Waiting for the leader to start. Only ever true in Follow. */
    bool IsArmed() ;
    void CancelArm() ;

    /* The leader's clock byte. Counted against our own slices;
       the difference is the phase error the loop closes. */
    void OnMidiClock() ;
    bool IsClockLocked() ;
    /* Machine-level sync role from config (MIDISYNCMODE): the rig
       decides who owns the clock, not the song. */
    enum SyncMode { SYNC_OFF = 0, SYNC_LEADER = 1, SYNC_FOLLOW = 2 };
    static SyncMode GetSyncMode();

    bool IsRunning();
    bool Clipped() ;

    void ProcessCommands();
    bool ProcessChannelCommand(int channel,FourCC cmd,ushort param) ;

	void StartStreaming(const Path &path) ;
	void StopStreaming() ;

	// Channel data

    bool IsChannelPlaying(int channel);
    void SetChannelMute(int channel,bool mute) ;
    bool IsChannelMuted(int channel);

    // Live queuing

    QueueingMode GetQueueingMode(int i);
    unsigned char GetQueuePosition(int i) ;
	unsigned char GetQueueChainPosition(int i) ;
	void QueueChannel(int i,QueueingMode mode,unsigned char position,unsigned char chainpos=0) ;

	char *GetLiveIndicator(int channel) ;
	// Steps of the current groove until a queued channel actually
	// switches. -1 when nothing is queued on that channel.
	int GetQueueSteps(int channel) ;
	double GetPlayTime() ;

	char *GetPlayedNote(int channel) ;
	char *GetPlayedOctive(int channel) ;
	char *GetPlayedInstrument(int channel) ;
	/* What KIND of instrument this channel is playing, for the
	   mixer strip. The LAST instrument rather than the currently
	   sounding one, so the indicator says what the channel is
	   rather than blinking off in the gaps between notes.
	   Returns IT_LAST when the channel has played nothing. */
	InstrumentType GetChannelInstrumentType(int channel) ;

	// info
	int GetPlayedBufferPercentage() ;

	std::string GetAudioAPI() ;
	std::string GetAudioDevice() ;
	int GetAudioBufferSize() ;
	int GetAudioRequestedBufferSize() ;
	int GetAudioPreBufferCount() ;

protected:
	void updateSongPos(int position,int channel,int chainPos=0,int hop=-1) ;
	void updateChainPos(int position,int channel,int hop=0) ;
	void updatePhrasePos(int pos,int channel) ;
	void playCursorPosition(int channel) ;
    int  getChannelHop(int channel,int pos) ;
    void applyTableRetrigger(int channel,int retrigger) ;
	void moveToNextStep() ;
	void moveToNextPhrase(int channel,int hop=-1) ;
	void moveToNextChain(int channel,int hop) ;

    void triggerLiveChains() ;

    bool isPlayable(int row,int col,int chainPos=0) ;
    bool findPlayable(uchar *row, int col, uchar chainPos = 0);

  private:
    PlayerMixer *mixer_ ;
	// which tracker channel each incoming MIDI note went to, +1 so
	// that zero means "not held"
	unsigned char midiHeld_[128] ;
	ViewData *viewData_ ;
	Project *project_ ;
	// which channels have reached the end of the song with
	// "repeat once" set, so the player knows when the last one
	// has finished
	bool songPlayed_[SONG_CHANNEL_COUNT] ;
	bool stopAtEnd_ ;

	SequencerMode sequencerMode_ ;
	PlayMode mode_ ;
	bool isRunning_ ;

	unsigned long startClock_ ;    // .Used to time display live queued chains
								   //  for blinking effect
	unsigned long now_ ;
	int lastPercentage_ ;
	int lastBeatCount_ ;
	unsigned char lastSongPos_ ;
	bool firstPlayCycle_ ;
	bool triggerLiveChains_ ;
	// PM_PHRASE follow: when set (config PHRASE_FOLLOW=YES), a looping
	// phrase jumps to whatever phrase the editor cursor is on at each
	// loop wrap -- M8-style. Off by default; read once at Start.
	bool phraseFollow_ ;

	double startTime_ ;
    double currentTime_;

    char instrumentOnChannel_[SONG_CHANNEL_COUNT][3];

    // Live queuing system

    unsigned char liveQueuePosition_[SONG_CHANNEL_COUNT] ;
    QueueingMode liveQueueingMode_[SONG_CHANNEL_COUNT] ;
	unsigned char liveQueueChainPosition_[SONG_CHANNEL_COUNT] ;
	unsigned int timeToLive_[SONG_CHANNEL_COUNT] ;
	// The dice MAYB rolls with. One stream for the whole player
	// rather than one per channel, so two channels asking the same
	// question on the same step do not get the same answer.
	unsigned int rng_ ;
	// armed: a start was asked for locally and the transport is
	// holding until the leader's start byte arrives.
	bool armed_ ;
	ClockSync clockSync_ ;
	float syncLeadMs() ;
	bool rollStepMaybe(int channel,unsigned char phrase,int pos) ;
	unsigned int timeToStart_[SONG_CHANNEL_COUNT] ;

	bool retrigAllImmediate_ ;
	unsigned char retrigPos_ ;
};

#endif
