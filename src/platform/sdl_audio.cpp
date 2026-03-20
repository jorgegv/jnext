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
    Log::platform()->info("Audio: {}Hz {}ch format={:#06x}",
                           have.freq, have.channels, have.format);
    return true;
}

void SdlAudio::push_from_mixer(Mixer& mixer)
{
    if (!initialized_) return;

    int avail = mixer.available();
    if (avail <= 0) return;

    // Read all available samples from the mixer
    std::vector<int16_t> buf(avail * 2);
    int got = mixer.read_samples(buf.data(), avail);
    if (got <= 0) return;

    // Put into the audio stream (handles format conversion)
    SDL_AudioStreamPut(stream_, buf.data(), got * 2 * sizeof(int16_t));

    // Pull converted data and queue it for the device
    int stream_avail = SDL_AudioStreamAvailable(stream_);
    if (stream_avail > 0) {
        std::vector<uint8_t> out(stream_avail);
        int read = SDL_AudioStreamGet(stream_, out.data(), stream_avail);
        if (read > 0) {
            SDL_QueueAudio(device_, out.data(), read);
        }
    }

    // Prevent audio buffer from growing too large (more than ~80ms)
    uint32_t queued = SDL_GetQueuedAudioSize(device_);
    if (queued > Mixer::SAMPLE_RATE * 2 * sizeof(int16_t) * 80 / 1000) {
        SDL_ClearQueuedAudio(device_);
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
