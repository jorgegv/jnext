#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

/// Writes the mixer's stereo 16-bit PCM output to a WAV file.
///
/// The frontend owns the recorder so captures remain open across emulator
/// cold boots, including sibling-NEX chaining.
class AudioRecorder {
public:
    AudioRecorder() = default;
    ~AudioRecorder();

    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    bool start(const std::string& path, uint32_t sample_rate = 44100);
    void capture(const int16_t* samples, int stereo_pair_count);
    bool stop();

    bool is_recording() const { return file_ != nullptr; }
    const std::string& last_error() const { return last_error_; }

private:
    bool write_header(uint32_t data_bytes);
    void set_error(const std::string& message);

    std::FILE* file_ = nullptr;
    uint32_t sample_rate_ = 44100;
    uint64_t data_bytes_ = 0;
    bool failed_ = false;
    std::string last_error_;
};
