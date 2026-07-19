#include "audio/audio_recorder.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>

namespace {

void put_u16_le(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

void put_u32_le(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace

AudioRecorder::~AudioRecorder()
{
    stop();
}

bool AudioRecorder::start(const std::string& path, uint32_t sample_rate)
{
    if (is_recording()) {
        last_error_ = "an audio capture is already active";
        return false;
    }
    if (sample_rate == 0 || sample_rate > std::numeric_limits<uint32_t>::max() / 4u) {
        set_error("sample rate is outside the WAV PCM range");
        return false;
    }

    last_error_.clear();
    failed_ = false;
    data_bytes_ = 0;
    sample_rate_ = sample_rate;
    file_ = std::fopen(path.c_str(), "wb+");
    if (!file_) {
        set_error("cannot open '" + path + "': " + std::strerror(errno));
        return false;
    }

    if (!write_header(0)) {
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }
    return true;
}

void AudioRecorder::capture(const int16_t* samples, int stereo_pair_count)
{
    if (!file_ || failed_ || !samples || stereo_pair_count <= 0) return;

    constexpr size_t PAIRS_PER_CHUNK = 1024;
    std::array<uint8_t, PAIRS_PER_CHUNK * 4> bytes{};

    int offset = 0;
    while (offset < stereo_pair_count) {
        const size_t pairs = std::min(
            static_cast<size_t>(stereo_pair_count - offset), PAIRS_PER_CHUNK);
        for (size_t i = 0; i < pairs; ++i) {
            put_u16_le(&bytes[i * 4],
                       static_cast<uint16_t>(samples[(offset + static_cast<int>(i)) * 2]));
            put_u16_le(&bytes[i * 4 + 2],
                       static_cast<uint16_t>(samples[(offset + static_cast<int>(i)) * 2 + 1]));
        }

        const size_t byte_count = pairs * 4;
        if (std::fwrite(bytes.data(), 1, byte_count, file_) != byte_count) {
            set_error("failed while writing PCM data");
            return;
        }
        data_bytes_ += byte_count;
        offset += static_cast<int>(pairs);
    }
}

bool AudioRecorder::stop()
{
    if (!file_) return !failed_;

    if (data_bytes_ > std::numeric_limits<uint32_t>::max() - 36u) {
        set_error("capture is too large for a standard WAV file");
    } else if (std::fflush(file_) != 0) {
        set_error("failed to flush PCM data");
    } else if (std::fseek(file_, 0, SEEK_SET) != 0) {
        set_error("failed to seek while finalizing WAV header");
    } else if (!write_header(static_cast<uint32_t>(data_bytes_))) {
        // write_header records the error.
    } else if (std::fflush(file_) != 0) {
        set_error("failed to flush WAV header");
    }

    if (std::fclose(file_) != 0 && !failed_) {
        set_error("failed to close WAV file");
    }
    file_ = nullptr;
    return !failed_;
}

bool AudioRecorder::write_header(uint32_t data_bytes)
{
    std::array<uint8_t, 44> header{};
    std::memcpy(&header[0], "RIFF", 4);
    put_u32_le(&header[4], 36u + data_bytes);
    std::memcpy(&header[8], "WAVE", 4);
    std::memcpy(&header[12], "fmt ", 4);
    put_u32_le(&header[16], 16);
    put_u16_le(&header[20], 1);  // PCM
    put_u16_le(&header[22], 2);  // stereo
    put_u32_le(&header[24], sample_rate_);
    put_u32_le(&header[28], sample_rate_ * 4u);
    put_u16_le(&header[32], 4);   // stereo frame size
    put_u16_le(&header[34], 16);  // bits per sample
    std::memcpy(&header[36], "data", 4);
    put_u32_le(&header[40], data_bytes);

    if (std::fwrite(header.data(), 1, header.size(), file_) != header.size()) {
        set_error("failed to write WAV header");
        return false;
    }
    return true;
}

void AudioRecorder::set_error(const std::string& message)
{
    failed_ = true;
    if (last_error_.empty()) last_error_ = message;
}
