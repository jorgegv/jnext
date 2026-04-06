#include "core/wav_loader.h"
#include "core/log.h"

#include <cstring>
#include <fstream>

// Little-endian read helpers.
static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

bool WavLoader::load(const std::string& path)
{
    loaded_ = false;
    playing_ = false;
    raw_data_.clear();
    data_size_ = 0;

    // Read entire file into memory.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        Log::emulator()->error("WAV: cannot open '{}'", path);
        return false;
    }

    auto file_size = file.tellg();
    if (file_size < 44) {
        Log::emulator()->error("WAV: file too small ({}B)", static_cast<long>(file_size));
        return false;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(file_size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buf.data()), file_size);
    if (!file) {
        Log::emulator()->error("WAV: read error on '{}'", path);
        return false;
    }

    // Validate RIFF/WAVE header.
    if (std::memcmp(buf.data(), "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        Log::emulator()->error("WAV: invalid RIFF/WAVE header");
        return false;
    }

    // Walk chunks to find "fmt " and "data".
    bool found_fmt = false;
    bool found_data = false;
    size_t pos = 12;  // Skip RIFF header (12 bytes).

    while (pos + 8 <= buf.size()) {
        const uint8_t* chunk_hdr = buf.data() + pos;
        uint32_t chunk_size = read_u32(chunk_hdr + 4);

        if (std::memcmp(chunk_hdr, "fmt ", 4) == 0) {
            if (chunk_size < 16 || pos + 8 + 16 > buf.size()) {
                Log::emulator()->error("WAV: truncated fmt chunk");
                return false;
            }
            const uint8_t* fmt = chunk_hdr + 8;
            uint16_t audio_format = read_u16(fmt + 0);
            if (audio_format != 1) {
                Log::emulator()->error("WAV: unsupported audio format {} (only PCM=1)", audio_format);
                return false;
            }
            channels_        = read_u16(fmt + 2);
            sample_rate_     = read_u32(fmt + 4);
            // byte_rate at fmt+8, block_align at fmt+12 — not needed.
            bits_per_sample_ = read_u16(fmt + 14);

            if (channels_ < 1 || channels_ > 2) {
                Log::emulator()->error("WAV: unsupported channel count {}", channels_);
                return false;
            }
            if (bits_per_sample_ != 8 && bits_per_sample_ != 16) {
                Log::emulator()->error("WAV: unsupported bits/sample {} (only 8 or 16)", bits_per_sample_);
                return false;
            }
            found_fmt = true;

        } else if (std::memcmp(chunk_hdr, "data", 4) == 0) {
            size_t data_start = pos + 8;
            size_t available = buf.size() - data_start;
            data_size_ = (chunk_size <= available) ? chunk_size : static_cast<uint32_t>(available);
            raw_data_.assign(buf.begin() + static_cast<ptrdiff_t>(data_start),
                             buf.begin() + static_cast<ptrdiff_t>(data_start + data_size_));
            found_data = true;
        }

        // Advance to next chunk (chunks are 2-byte aligned).
        pos += 8 + chunk_size;
        if (chunk_size & 1) pos++;  // padding byte

        if (found_fmt && found_data) break;
    }

    if (!found_fmt) {
        Log::emulator()->error("WAV: missing 'fmt ' chunk");
        return false;
    }
    if (!found_data || data_size_ == 0) {
        Log::emulator()->error("WAV: missing or empty 'data' chunk");
        return false;
    }

    loaded_ = true;
    uint32_t frames = total_frames();
    double duration = (sample_rate_ > 0) ? static_cast<double>(frames) / sample_rate_ : 0.0;
    Log::emulator()->info("WAV: loaded '{}' — {}ch {}Hz {}bit, {} samples ({:.1f}s)",
                          path, channels_, sample_rate_, bits_per_sample_, frames, duration);
    return true;
}

bool WavLoader::apply(Emulator& /*emu*/)
{
    // WAV is always real-time; no fast-load apply step needed.
    return loaded_;
}

void WavLoader::start_playback(uint64_t start_tstates)
{
    if (!loaded_) return;
    start_tstates_ = start_tstates;
    playing_ = true;
    Log::emulator()->info("WAV: playback started at T-state {}", start_tstates);
}

void WavLoader::stop_playback()
{
    playing_ = false;
    Log::emulator()->info("WAV: playback stopped");
}

void WavLoader::eject()
{
    loaded_ = false;
    playing_ = false;
    raw_data_.clear();
    data_size_ = 0;
}

uint8_t WavLoader::get_ear_bit(uint64_t current_tstates) const
{
    if (!playing_ || !loaded_) return 0;
    if (current_tstates <= start_tstates_) return 0;

    // Convert elapsed T-states to sample frame index.
    // frame_index = (elapsed_tstates * sample_rate) / CPU_CLOCK_HZ
    uint64_t elapsed = current_tstates - start_tstates_;
    uint64_t frame_index = (elapsed * sample_rate_) / CPU_CLOCK_HZ;

    uint32_t frames = total_frames();
    if (frame_index >= frames) return 0;  // Past end of audio.

    return sample_to_ear(static_cast<uint32_t>(frame_index));
}

uint8_t WavLoader::sample_to_ear(uint32_t frame_index) const
{
    uint32_t bpf = bytes_per_frame();
    size_t byte_offset = static_cast<size_t>(frame_index) * bpf;

    if (byte_offset + (bits_per_sample_ / 8) > raw_data_.size()) return 0;

    if (bits_per_sample_ == 8) {
        // 8-bit unsigned: 0-255, center at 128.
        uint8_t sample = raw_data_[byte_offset];
        return (sample >= 128) ? 1 : 0;
    } else {
        // 16-bit signed little-endian: center at 0.
        int16_t sample = static_cast<int16_t>(read_u16(raw_data_.data() + byte_offset));
        return (sample >= 0) ? 1 : 0;
    }
}
