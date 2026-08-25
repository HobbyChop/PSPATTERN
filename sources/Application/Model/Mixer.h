
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
    unsigned char fx_[4];   // division, feedback, size, damp
} ;	

#endif
