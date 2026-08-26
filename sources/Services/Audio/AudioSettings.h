#ifndef _AUDIO_SETTINGS_H_
#define _AUDIO_SETTINGS_H_

#include <string>
// Used to propagate audio hints & settings

struct AudioSettings {
    // Initialised, because they were not.
    //
    // PSPSystem sets bufferSize_ and preBufferCount_ and never touched
    // sampleRate_, so Audio's constructor read an uninitialised stack
    // int and logged it: "Preferred Sample Rate:145001912" is in every
    // log this program has ever written on that platform. The SDL1
    // path happens not to use it -- it asks the device what rate it
    // actually got -- but the SDL2 path feeds it straight into
    // SDL_OpenAudio as the requested frequency.
    AudioSettings()
        : bufferSize_(256), preBufferCount_(6), sampleRate_(44100) {}

    std::string audioAPI_;
    std::string audioDevice_;
    int bufferSize_;
    int preBufferCount_;
    int sampleRate_;
};

#endif
