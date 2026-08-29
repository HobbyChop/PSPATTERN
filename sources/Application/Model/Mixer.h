
#ifndef _MIXER_H_
#define _MIXER_H_

#include "Foundation/T_Singleton.h"
#include "Application/Persistency/Persistent.h"

#include "Song.h"
#include "Application/Utils/fixed.h"

class Mixer:public T_Singleton<Mixer>,Persistent {
public:
	Mixer() ;
	~Mixer() ;
	void Clear() ;

	inline int GetBus(int i) { return channelBus_[i]  ; } ;
    inline void SetBus(int i, int value) { channelBus_[i] = value; };

    inline int GetChannelVolume(int i) { return channelVolume_[i]; };
    inline void SetChannelVolume(int i, int value) { channelVolume_[i] = (unsigned char)value; };
    inline int GetChannelHPF(int i) { return channelHPF_[i]; };
    inline void SetChannelHPF(int i, int value) { channelHPF_[i] = (unsigned char)value; };
    inline unsigned short GetChannelLPF(int i) { return channelLPF_[i]; };
    inline void SetChannelLPF(int i, unsigned short value) { channelLPF_[i] = value; };

    // How much of each channel is copied into the shared delay and
    // reverb, 0..255. Saved as their own elements, so a project
    // written by a build without them simply comes back with the
    // sends at zero rather than failing to load.
    inline int GetChannelDelaySend(int i) { return channelDelaySend_[i]; };
    inline void SetChannelDelaySend(int i, int v) {
        channelDelaySend_[i] = (unsigned char)v; };
    inline int GetChannelReverbSend(int i) { return channelReverbSend_[i]; };
    inline void SetChannelReverbSend(int i, int v) {
        channelReverbSend_[i] = (unsigned char)v; };

    // Per-channel insert effects: a phaser and a chorus, each with its
    // own rate and depth (0..255; depth 0 = off). Processed on the main
    // core in PlayerChannel, so they cost DSP only on channels that use
    // them. Saved as their own elements, absent = 0.
    inline int GetChannelPhaserRate(int i) { return channelPhaserRate_[i]; };
    inline void SetChannelPhaserRate(int i, int v) { channelPhaserRate_[i] = (unsigned char)v; };
    inline int GetChannelPhaserDepth(int i) { return channelPhaserDepth_[i]; };
    inline void SetChannelPhaserDepth(int i, int v) { channelPhaserDepth_[i] = (unsigned char)v; };
    inline int GetChannelChorusRate(int i) { return channelChorusRate_[i]; };
    inline void SetChannelChorusRate(int i, int v) { channelChorusRate_[i] = (unsigned char)v; };
    inline int GetChannelChorusDepth(int i) { return channelChorusDepth_[i]; };
    inline void SetChannelChorusDepth(int i, int v) { channelChorusDepth_[i] = (unsigned char)v; };

    // The two effects themselves. One of each, shared by every
    // channel -- which is the point of a send.
    inline int GetDelayDivision() { return fx_[0]; };
    inline void SetDelayDivision(int v) { fx_[0] = (unsigned char)v; };
    inline int GetDelayFeedback() { return fx_[1]; };
    inline void SetDelayFeedback(int v) { fx_[1] = (unsigned char)v; };
    inline int GetReverbSize() { return fx_[2]; };
    inline void SetReverbSize(int v) { fx_[2] = (unsigned char)v; };
    inline int GetReverbDamp() { return fx_[3]; };
    inline void SetReverbDamp(int v) { fx_[3] = (unsigned char)v; };

    // Send-bus fold-in effects, run on the ME wet tail (scalar mirror
    // when the ME is off). 0 = off / bypass for everything but the two
    // modulation rates. Saved as "SFX2"; a project from a build without
    // them loads with the effects at 0.
    inline int GetReverbFreeze() { return fx2_[0]; };
    inline void SetReverbFreeze(int v) { fx2_[0] = (unsigned char)v; };
    inline int GetDrive() { return fx2_[1]; };
    inline void SetDrive(int v) { fx2_[1] = (unsigned char)v; };
    inline int GetReverbDuck() { return fx2_[2]; };
    inline void SetReverbDuck(int v) { fx2_[2] = (unsigned char)v; };
    inline int GetReverbGate() { return fx2_[3]; };
    inline void SetReverbGate(int v) { fx2_[3] = (unsigned char)v; };
    inline int GetComp() { return fx2_[4]; };
    inline void SetComp(int v) { fx2_[4] = (unsigned char)v; };
    inline int GetPhaserDepth() { return fx2_[5]; };
    inline void SetPhaserDepth(int v) { fx2_[5] = (unsigned char)v; };
    inline int GetPhaserRate() { return fx2_[6]; };
    inline void SetPhaserRate(int v) { fx2_[6] = (unsigned char)v; };
    inline int GetChorusDepth() { return fx2_[7]; };
    inline void SetChorusDepth(int v) { fx2_[7] = (unsigned char)v; };
    inline int GetChorusRate() { return fx2_[8]; };
    inline void SetChorusRate(int v) { fx2_[8] = (unsigned char)v; };

	virtual unsigned int Checksum(unsigned int h) ;
	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);
private:
	char channelBus_[SONG_CHANNEL_COUNT] ;
    unsigned char channelVolume_[SONG_CHANNEL_COUNT];
    unsigned char channelHPF_[SONG_CHANNEL_COUNT];
    unsigned short channelLPF_[SONG_CHANNEL_COUNT];
    unsigned char channelDelaySend_[SONG_CHANNEL_COUNT];
    unsigned char channelReverbSend_[SONG_CHANNEL_COUNT];
    unsigned char channelPhaserRate_[SONG_CHANNEL_COUNT];
    unsigned char channelPhaserDepth_[SONG_CHANNEL_COUNT];
    unsigned char channelChorusRate_[SONG_CHANNEL_COUNT];
    unsigned char channelChorusDepth_[SONG_CHANNEL_COUNT];
    unsigned char fx_[4];   // division, feedback, size, damp
    // freeze, drive, duck, gate, comp, phaserDepth, phaserRate,
    // chorusDepth, chorusRate, + 3 spare for growth
    unsigned char fx2_[12];
} ;

#endif
