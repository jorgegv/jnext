#include "sdl_audio.h"
#include "core/log.h"
#include <vector>

bool SdlAudio::init()
{
    // Open audio device with desired format
    SDL_AudioSpec want{};
    want.freq = Mixer::SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;  // Buffer size in sample frames
    want.callback = nullptr;  // We use SDL_QueueAudio / SDL_AudioStream

    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        Log::platform()->error("SDL_OpenAudioDevice: {}", SDL_GetError());
        return false;
    }

    // Create an audio stream for format conversion if needed
    stream_ = SDL_NewAudioStream(
        AUDIO_S16SYS, 2, Mixer::SAMPLE_RATE,
        have.format, have.channels, have.freq);

    if (!stream_) {
        Log::platform()->error("SDL_NewAudioStream: {}", SDL_GetError());
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return false;
    }

    // Unpause the audio device to start playback
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

uint32_t SdlAudio::ms_to_bytes(int ms)
{
    return static_cast<uint32_t>(Mixer::SAMPLE_RATE * 2 * sizeof(int16_t) * ms / 1000);
}

int SdlAudio::queued_ms() const
{
    if (!initialized_) return -1;
    // Scale by 1000 first: a bytes-per-ms constant would truncate (44100 x 4 /
    // 1000 = 176.4 -> 176) and over-report the depth by ~0.23%.
    const uint32_t bytes_per_sec = Mixer::SAMPLE_RATE * 2 * sizeof(int16_t);
    return static_cast<int>(SDL_GetQueuedAudioSize(device_) * 1000ull / bytes_per_sec);
}

void SdlAudio::queue_pcm(const int16_t* pcm, int frames)
{
    if (frames <= 0) return;

    // Put into the audio stream (handles format conversion)
    SDL_AudioStreamPut(stream_, pcm, frames * 2 * sizeof(int16_t));

    // Pull converted data and queue it for the device
    int stream_avail = SDL_AudioStreamAvailable(stream_);
    if (stream_avail > 0) {
        std::vector<uint8_t> out(stream_avail);
        int read = SDL_AudioStreamGet(stream_, out.data(), stream_avail);
        if (read > 0) {
            SDL_QueueAudio(device_, out.data(), read);
        }
    }
}

void SdlAudio::push_from_mixer(Mixer& mixer, bool hold_on_underrun)
{
    if (!initialized_) return;

    // Upper guard: the device is already far enough ahead, so let it drain.
    // Never clear the queue — that causes clicks. Note this is checked BEFORE
    // draining the mixer: reading the ring buffer and then discarding what we
    // read would punch a hole in the sample stream, which is a click too.
    if (SDL_GetQueuedAudioSize(device_) > ms_to_bytes(audio_pacing::QUEUE_MAX_MS)) {
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

    // Underrun guard. Pacing (audio_pacing.h) keeps the queue inside its band
    // on any host that can emulate in real time, so this never fires there. On
    // a host that cannot, the missing samples simply do not exist and no amount
    // of pacing can conjure them — but letting the queue reach empty means SDL
    // pads the device buffer with ZEROS, stepping the signal to 0 and back
    // several times a second: the click train of issue #7. Hold the last level
    // instead: a DC hold has no discontinuity, so a starved host stutters
    // rather than clicks.
    //
    // `hold_on_underrun` is false whenever the caller is NOT pacing on the audio
    // clock (the Qt speed multiplier is off 1x). Padding inserts samples the
    // emulator never produced; without pacing to keep the queue in its band it
    // would fire on every tick, so it must be a rescue, never a routine.
    if (!hold_on_underrun) return;

    const int queued_samples =
        static_cast<int>(SDL_GetQueuedAudioSize(device_) / (2 * sizeof(int16_t)));
    const int pad = audio_pacing::underrun_pad_samples(queued_samples, Mixer::SAMPLE_RATE);
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
    if (stream_) {
        SDL_FreeAudioStream(stream_);
        stream_ = nullptr;
    }
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    initialized_ = false;
}
