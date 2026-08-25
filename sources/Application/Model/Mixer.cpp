
#include "Mixer.h"
#include "Application/Persistency/Checksum.h"

#include "Application/Utils/HexBuffers.h"

Mixer::Mixer():Persistent("MIXER")  {
	Clear() ;
} ;

Mixer::~Mixer() {
} ;

void Mixer::Clear() {

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channelBus_[i]=i ;
        channelVolume_[i] = 0xFF;
        channelHPF_[i] = 0; // 0=OFF, 1=20Hz, 2=90Hz
        channelLPF_[i] = 0; // 0=OFF, else frequency in Hz (20-20000)
        channelDelaySend_[i] = 0;
        channelReverbSend_[i] = 0;
    }
    // Defaults that are audible without being a wash: a dotted eighth
    // ping-pong at moderate feedback, and a medium room with the top
    // rolled off. A user who turns a send up should hear something
    // musical immediately rather than having to build the effect
    // first.
    fx_[0] = 3;     // DIV_D8, the 3/16 dotted eighth
    fx_[1] = 140;   // delay feedback
    fx_[2] = 160;   // reverb size
    fx_[3] = 110;   // reverb damping
} ;

unsigned int Mixer::Checksum(unsigned int h) {
	h=checksumBytes(h,channelBus_,sizeof(channelBus_)) ;
	h=checksumBytes(h,channelVolume_,sizeof(channelVolume_)) ;
	h=checksumBytes(h,channelHPF_,sizeof(channelHPF_)) ;
	h=checksumBytes(h,channelLPF_,sizeof(channelLPF_)) ;
	h=checksumBytes(h,channelDelaySend_,sizeof(channelDelaySend_)) ;
	h=checksumBytes(h,channelReverbSend_,sizeof(channelReverbSend_)) ;
	h=checksumBytes(h,fx_,sizeof(fx_)) ;
	return h ;
} ;

void Mixer::SaveContent(TiXmlNode *node) {
    saveHexBuffer(node, "BUS", (unsigned char *)channelBus_,
                  SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "VOL", channelVolume_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "HPF", channelHPF_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "LPF", channelLPF_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "DLY", channelDelaySend_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "REV", channelReverbSend_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "SFX", fx_, 4);
} ;

void Mixer::RestoreContent(TiXmlElement *element) {
    TiXmlElement *current = element->FirstChildElement();
    while (current) {
        const char *value = current->Value();
        if (!strcmp("BUS", value)) {
            restoreHexBuffer(current, (unsigned char *)channelBus_, sizeof(channelBus_));
        } else if (!strcmp("VOL", value)) {
            restoreHexBuffer(current, channelVolume_, sizeof(channelVolume_));
        } else if (!strcmp("HPF", value)) {
            restoreHexBuffer(current, channelHPF_, sizeof(channelHPF_));
        } else if (!strcmp("LPF", value)) {
            restoreHexBuffer(current, (unsigned char *)channelLPF_, sizeof(channelLPF_));
        } else if (!strcmp("DLY", value)) {
            restoreHexBuffer(current, channelDelaySend_, sizeof(channelDelaySend_));
        } else if (!strcmp("REV", value)) {
            restoreHexBuffer(current, channelReverbSend_, sizeof(channelReverbSend_));
        } else if (!strcmp("SFX", value)) {
            restoreHexBuffer(current, fx_, sizeof(fx_));
        }
        current = current->NextSiblingElement();
    }
}
