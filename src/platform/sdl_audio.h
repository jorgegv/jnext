#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include "audio/mixer.h"
#include "platform/audio_pacing.h"

/// SDL2 audio output bridge.
///
/// Opens an SDL_AudioStream at 44100 Hz, stereo, int16.
/// Each frame, the emulator calls push_from_mixer() to send
/// mixer output to SDL's audio subsystem.
///
/// The frontends pace emulation against queued_ms() (see audio_pacing.h) so
/// the emulator's audio clock tracks the sound card's instead of drifting
/// behind it.
class SdlAudio {
public:
    SdlAudio() = default;
    ~SdlAudio() { shutdown(); }

    /// Initialize SDL audio device and stream.
    bool init();

    /// Push stereo samples from the mixer to SDL.
    /// Called once per frame from the main loop.
    void push_from_mixer(Mixer& mixer);

    /// Current depth of the audio device queue in milliseconds, or -1 when
    /// audio is not running. Frontends feed this to audio_pacing::frames_for_tick().
    int queued_ms() const;

    /// Shut down SDL audio.
    void shutdown();

private:
    /// Bytes held in the device queue for `ms` of playback.
    static uint32_t ms_to_bytes(int ms);

    /// Convert and hand `frames` stereo pairs to the device queue.
    void queue_pcm(const int16_t* pcm, int frames);

    SDL_AudioDeviceID device_ = 0;
    SDL_AudioStream*  stream_ = nullptr;
    bool initialized_ = false;

    /// Last sample pair actually queued — the level the underrun guard holds
    /// when the host cannot produce samples fast enough.
    int16_t last_l_ = 0;
    int16_t last_r_ = 0;
};
