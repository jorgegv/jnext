#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include "audio/mixer.h"

/// SDL2 audio output bridge.
///
/// Opens an SDL_AudioStream at 44100 Hz, stereo, int16.
/// Each frame, the emulator calls push_samples() to send
/// mixer output to SDL's audio subsystem.
class SdlAudio {
public:
    SdlAudio() = default;
    ~SdlAudio() { shutdown(); }

    /// Initialize SDL audio device and stream.
    bool init();

    /// Push stereo samples from the mixer to SDL.
    /// Called once per frame from the main loop.
    void push_from_mixer(Mixer& mixer);

    /// Shut down SDL audio.
    void shutdown();

private:
    SDL_AudioDeviceID device_ = 0;
    SDL_AudioStream*  stream_ = nullptr;
    bool initialized_ = false;
};
