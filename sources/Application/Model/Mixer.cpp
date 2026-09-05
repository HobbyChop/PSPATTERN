
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
        // 20Hz by default: nothing musical lives below it, and eight
        // channels of subsonics stacking on the bus is where a mix goes
        // muddy before anyone has touched a fader. A saved project
        // keeps its own setting; this is the start for a new one.
        channelHPF_[i] = 1; // 0=OFF, 1=20Hz, 2=90Hz
        channelLPF_[i] = 0; // 0=OFF, else frequency in Hz (20-20000)
        channelDelaySend_[i] = 0;
        channelReverbSend_[i] = 0;
        channelPhaserRate_[i] = 128;    // mid rate; depth 0 = off
        channelPhaserDepth_[i] = 0;
        channelChorusRate_[i] = 96;
        channelChorusDepth_[i] = 0;
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

    // Fold-in effects all start off (0). The two modulation RATES start
    // mid, so raising a phaser/chorus depth sounds musical straight away
    // instead of frozen or too fast.
    for (int i = 0; i < 12; i++) fx2_[i] = 0;
    fx2_[6] = 128;  // phaser rate, mid
    fx2_[8] = 96;   // chorus rate, mid-slow
} ;

unsigned int Mixer::Checksum(unsigned int h) {
	h=checksumBytes(h,channelBus_,sizeof(channelBus_)) ;
	h=checksumBytes(h,channelVolume_,sizeof(channelVolume_)) ;
	h=checksumBytes(h,channelHPF_,sizeof(channelHPF_)) ;
	h=checksumBytes(h,channelLPF_,sizeof(channelLPF_)) ;
	h=checksumBytes(h,channelDelaySend_,sizeof(channelDelaySend_)) ;
	h=checksumBytes(h,channelReverbSend_,sizeof(channelReverbSend_)) ;
	h=checksumBytes(h,channelPhaserRate_,sizeof(channelPhaserRate_)) ;
	h=checksumBytes(h,channelPhaserDepth_,sizeof(channelPhaserDepth_)) ;
	h=checksumBytes(h,channelChorusRate_,sizeof(channelChorusRate_)) ;
	h=checksumBytes(h,channelChorusDepth_,sizeof(channelChorusDepth_)) ;
	h=checksumBytes(h,fx_,sizeof(fx_)) ;
	h=checksumBytes(h,fx2_,sizeof(fx2_)) ;
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
    saveHexBuffer(node, "PHR", channelPhaserRate_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "PHD", channelPhaserDepth_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "CHR", channelChorusRate_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "CHD", channelChorusDepth_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "SFX", fx_, 4);
    saveHexBuffer(node, "SFX2", fx2_, sizeof(fx2_));
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
        } else if (!strcmp("PHR", value)) {
            restoreHexBuffer(current, channelPhaserRate_, sizeof(channelPhaserRate_));
        } else if (!strcmp("PHD", value)) {
            restoreHexBuffer(current, channelPhaserDepth_, sizeof(channelPhaserDepth_));
        } else if (!strcmp("CHR", value)) {
            restoreHexBuffer(current, channelChorusRate_, sizeof(channelChorusRate_));
        } else if (!strcmp("CHD", value)) {
            restoreHexBuffer(current, channelChorusDepth_, sizeof(channelChorusDepth_));
        } else if (!strcmp("SFX", value)) {
            restoreHexBuffer(current, fx_, sizeof(fx_));
        } else if (!strcmp("SFX2", value)) {
            restoreHexBuffer(current, fx2_, sizeof(fx2_));
        }
        current = current->NextSiblingElement();
    }
}
