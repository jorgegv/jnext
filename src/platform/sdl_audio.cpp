#include "sdl_audio.h"
#include "core/log.h"
#include <cstring>
#include <vector>

// The audio_fill ring is sized against the push-side bounds (see the WHY at
// audio_fill::RING_CAPACITY_PAIRS): QUEUE_MAX_MS of queue plus one full mixer
// ring per push. Pin the relation so neither side can drift silently.
static_assert(audio_fill::RING_CAPACITY_PAIRS ==
                  Mixer::SAMPLE_RATE * 250 / 1000,
              "audio_fill ring is defined as 250 ms at the mixer sample rate");
static_assert(audio_fill::RING_CAPACITY_PAIRS >
                  audio_pacing::QUEUE_MAX_MS * Mixer::SAMPLE_RATE / 1000 +
                      Mixer::RING_BUFFER_SIZE,
              "one push after the QUEUE_MAX_MS check must always fit the ring");

bool SdlAudio::init()
{
    // Open the device in CALLBACK mode (GH #208): SDL's audio thread calls
    // audio_callback whenever the device needs samples, so the shortfall
    // policy runs at the device boundary no matter what the GUI thread is
    // doing. allowed_changes=0 means SDL guarantees `have` == `want` and
    // converts to the hardware's real format internally — so the callback
    // always speaks S16SYS stereo 44100 and no SDL_AudioStream is needed
    // (the old push path kept one; with have==want it was an identity
    // pass-through).
    SDL_AudioSpec want{};
    want.freq = Mixer::SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;  // Buffer size in sample frames
    want.callback = &SdlAudio::audio_callback;
    want.userdata = this;

    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        Log::platform()->error("SDL_OpenAudioDevice: {}", SDL_GetError());
        return false;
    }

    // Unpause the audio device to start playback. Callbacks begin now; until
    // the first push they fill at the audio_fill::State initial level of
    // exact 0 — genuine silence (GH #116: silence rests at exact zero).
    SDL_PauseAudioDevice(device_, 0);

    initialized_ = true;
    // Task 63 (issue #9) — name the SDL audio backend (pulseaudio / pipewire /
    // alsa / dsp ...) so a reporter's log answers the backend question without
    // an extra run. Null only if SDL audio somehow closed again — print it
    // honestly rather than crash fmt with a nullptr.
    const char* audio_driver = SDL_GetCurrentAudioDriver();
    Log::platform()->info("Audio: {}Hz {}ch format={:#06x} driver={}",
                           have.freq, have.channels, have.format,
                           audio_driver ? audio_driver : "(unknown)");
    return true;
}

void SdlAudio::audio_callback(void* userdata, Uint8* stream, int len)
{
    // SDL holds the device lock around this call (SDL_LockAudioDevice on the
    // producer side mutually excludes it), so the ring and fill state are
    // accessed directly. No logging and no allocation here: this is SDL's
    // high-priority audio thread.
    auto* self = static_cast<SdlAudio*>(userdata);
    const int pairs = len / (2 * static_cast<int>(sizeof(int16_t)));
    audio_fill::fill_request(self->ring_, self->fill_,
                             reinterpret_cast<int16_t*>(stream), pairs);
    // With have==want (S16 stereo) len is always a whole number of pairs;
    // zero any remainder bytes defensively rather than leak stack garbage to
    // the device if that ever stops holding.
    const int tail = len - pairs * 2 * static_cast<int>(sizeof(int16_t));
    if (tail > 0) std::memset(stream + (len - tail), 0, static_cast<size_t>(tail));
}

int SdlAudio::queued_pairs() const
{
    SDL_LockAudioDevice(device_);
    const int pairs = ring_.size;
    SDL_UnlockAudioDevice(device_);
    return pairs;
}

int SdlAudio::queued_ms() const
{
    if (!initialized_) return -1;
    // Scale by 1000 first: a pairs-per-ms constant would truncate (44100 /
    // 1000 = 44.1 -> 44) and over-report the depth by ~0.23%.
    return static_cast<int>(static_cast<int64_t>(queued_pairs()) * 1000 /
                            Mixer::SAMPLE_RATE);
}

audio_fill::StatsWindow SdlAudio::take_fill_stats()
{
    if (!initialized_) return {};
    SDL_LockAudioDevice(device_);
    const audio_fill::StatsWindow w = audio_fill::take_stats(fill_);
    SDL_UnlockAudioDevice(device_);
    return w;
}

void SdlAudio::queue_pcm(const int16_t* pcm, int frames)
{
    if (frames <= 0) return;

    SDL_LockAudioDevice(device_);
    const int accepted = audio_fill::ring_push(ring_, pcm, frames);
    SDL_UnlockAudioDevice(device_);

    // Unreachable by construction (static_assert above: the QUEUE_MAX_MS
    // check in push_from_mixer bounds the ring depth before any push, and one
    // push adds at most the mixer ring). If it ever fires, samples were
    // DROPPED — a spliced discontinuity — so say so loudly, once.
    if (accepted < frames && !overflow_warned_) {
        overflow_warned_ = true;
        Log::platform()->warn(
            "audio ring overflow: dropped {} stereo pairs (ring {} pairs); "
            "this should be impossible — please report it",
            frames - accepted, audio_fill::RING_CAPACITY_PAIRS);
    }
}

void SdlAudio::push_from_mixer(Mixer& mixer, bool hold_on_underrun)
{
    if (!initialized_) return;

    // Upper guard: the device is already far enough ahead, so let it drain.
    // Never clear the ring — that causes clicks. Note this is checked BEFORE
    // draining the mixer: reading the ring buffer and then discarding what we
    // read would punch a hole in the sample stream, which is a click too.
    if (queued_pairs() >
        audio_pacing::ms_to_samples(audio_pacing::QUEUE_MAX_MS, Mixer::SAMPLE_RATE)) {
        return;
    }

    const int avail = mixer.available();
    if (avail > 0) {
        std::vector<int16_t> buf(avail * 2);
        const int got = mixer.read_samples(buf.data(), avail);
        if (got > 0) {
            last_l_ = buf[(got - 1) * 2];
            last_r_ = buf[(got - 1) * 2 + 1];
            queue_pcm(buf.data(), got);
        }
    }

    // Underrun guard — the FIRST of two layers (GH #208).
    //
    // Pacing (audio_pacing.h) keeps the ring inside its band on any host that
    // can emulate in real time, so this never fires there. On a host that
    // cannot, the missing samples simply do not exist and no amount of pacing
    // can conjure them. This tick-side pad tops the ring up to QUEUE_FLOOR_MS
    // by holding the last level, so a merely-LATE tick (one that still runs,
    // just behind schedule) never exposes the device to an empty ring.
    //
    // What it can never bridge is a tick that does NOT run: a GUI stall, a
    // degraded timer, a host throttled below real time (issue #208). The
    // SECOND layer covers that — the device callback holds the last real pair
    // for any shortfall (audio_fill.h), at the device boundary, on SDL's
    // audio thread, regardless of tick cadence. Between the two, the device
    // never plays content jnext did not choose: this pad keeps the callback
    // fill from becoming routine, and the callback fill catches what no
    // tick-side code possibly can.
    //
    // `hold_on_underrun` is false whenever the caller is NOT pacing on the audio
    // clock (the Qt speed multiplier is off 1x). Padding inserts samples the
    // emulator never produced; without pacing to keep the ring in its band it
    // would fire on every tick, so it must be a rescue, never a routine.
    if (!hold_on_underrun) return;

    const int pad =
        audio_pacing::underrun_pad_samples(queued_pairs(), Mixer::SAMPLE_RATE);
    if (pad > 0) {
        std::vector<int16_t> hold(static_cast<size_t>(pad) * 2);
        for (int i = 0; i < pad; i++) {
            hold[i * 2]     = last_l_;
            hold[i * 2 + 1] = last_r_;
        }
        queue_pcm(hold.data(), pad);
    }
}

void SdlAudio::shutdown()
{
    if (device_) {
        // Closing waits for a running callback to return; no callback runs
        // after this, so the ring can die with the object.
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    initialized_ = false;
}
