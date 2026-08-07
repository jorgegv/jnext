#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include "audio/mixer.h"
#include "platform/audio_fill.h"
#include "platform/audio_pacing.h"

/// SDL2 audio output bridge.
///
/// Opens an SDL audio device at 44100 Hz, stereo, int16, in CALLBACK mode
/// (GH #208): SDL's high-priority audio thread pulls samples from an internal
/// ring buffer owned by this class. Each frame, the emulator calls
/// push_from_mixer() to append mixer output to that ring.
///
/// WHY a callback and not the old SDL_QueueAudio push model: when the queue
/// ran dry SDL delivered ZEROS to the device — content jnext never chose,
/// audible as the issue #7/#208 click-train over any nonzero programme. The
/// callback runs at the device boundary regardless of GUI-thread cadence, so
/// for any shortfall it can hold the last real sample pair instead (the pure
/// policy in audio_fill.h). The device can now never play content jnext did
/// not choose, however starved the host.
///
/// The frontends pace emulation against queued_ms() (see audio_pacing.h) so
/// the emulator's audio clock tracks the sound card's instead of drifting
/// behind it.
///
/// THREADING. The ring and fill state are shared between the GUI thread
/// (producer) and SDL's audio thread (consumer, inside the callback). The
/// only synchronisation primitive is SDL_LockAudioDevice, which SDL defines
/// to mutually exclude the callback — the callback itself therefore accesses
/// the shared state without taking it. Every producer-side access below locks.
class SdlAudio {
public:
    SdlAudio() = default;
    ~SdlAudio() { shutdown(); }

    /// Initialize the SDL audio device (callback mode).
    bool init();

    /// Push stereo samples from the mixer to the ring. Called once per tick
    /// from the main loop. `hold_on_underrun` enables the last-level padding
    /// that keeps the ring off the callback fill on a host too slow to emulate
    /// in real time; pass false when the caller is not pacing on the audio
    /// clock (see audio_pacing.h), because padding must never become a routine.
    void push_from_mixer(Mixer& mixer, bool hold_on_underrun);

    /// Current depth of the sample ring in milliseconds, or -1 when audio is
    /// not running. The exact semantic analogue of the old
    /// SDL_GetQueuedAudioSize reading — queued, not yet consumed — so the
    /// calibrated audio_pacing band constants carry over unchanged. Frontends
    /// feed this to audio_pacing::frames_for_tick().
    int queued_ms() const;

    /// Device-boundary fill diagnostics since the last call (GH #208 field
    /// debuggability): pairs delivered from the ring, pairs manufactured by
    /// the callback hold, and hold EVENTS (no-fill -> fill transitions).
    /// Drain-style: reading resets the window. Safe to call with no device
    /// (returns zeros).
    audio_fill::StatsWindow take_fill_stats();

    /// Shut down SDL audio.
    void shutdown();

private:
    /// SDL audio-thread entry: fill `len` bytes of `stream` from the ring,
    /// holding the last real pair for any shortfall (audio_fill.h).
    static void audio_callback(void* userdata, Uint8* stream, int len);

    /// Ring depth in stereo pairs, read under the device lock.
    int queued_pairs() const;

    /// Append `frames` stereo pairs to the ring (under the device lock).
    void queue_pcm(const int16_t* pcm, int frames);

    SDL_AudioDeviceID device_ = 0;
    bool initialized_ = false;

    /// Producer/consumer sample ring + the audio thread's fill state.
    /// Accessed under SDL_LockAudioDevice from the GUI thread; accessed
    /// lock-free inside the callback (SDL holds the device lock there).
    audio_fill::Ring  ring_;
    audio_fill::State fill_;

    /// Ring overflow warning latch (see queue_pcm — unreachable by
    /// construction, loud if construction is ever broken).
    bool overflow_warned_ = false;

    /// Last sample pair queued by the PUSH side — the level its tick-side
    /// underrun pad holds. Distinct from audio_fill::State's hold level,
    /// which the AUDIO thread tracks from pairs actually consumed; keeping
    /// the two per-thread avoids sharing anything beyond the ring.
    int16_t last_l_ = 0;
    int16_t last_r_ = 0;
};
