#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// WAV file loader for the ZX Spectrum emulator.
///
/// Parses PCM WAV files (8-bit unsigned or 16-bit signed LE) and provides
/// real-time EAR-bit playback by converting audio samples to a binary
/// signal via threshold comparison.
///
/// WAV loading is always real-time (no fast-load shortcut possible since
/// we have raw audio data, not structured tape blocks).
class WavLoader {
public:
    /// Load a WAV file from disk.  Returns true on success.
    /// Only PCM format (audio_format == 1) is supported.
    bool load(const std::string& path);

    /// Apply to emulator (reserved for future use).
    bool apply(Emulator& emu);

    // -----------------------------------------------------------------------
    // Real-time playback
    // -----------------------------------------------------------------------

    /// Start playback, recording the CPU T-state at which playback begins.
    void start_playback(uint64_t start_tstates);

    /// Stop playback.
    void stop_playback();

    /// True if currently playing back.
    bool is_playing() const { return playing_; }

    /// Get the current EAR bit (0 or 1) for the given absolute T-state.
    /// Returns 0 if past end of data or not playing.
    uint8_t get_ear_bit(uint64_t current_tstates) const;

    /// True if a file has been loaded.
    bool is_loaded() const { return loaded_; }

    /// Eject the tape, freeing all data.
    void eject();

private:
    bool loaded_ = false;
    bool playing_ = false;
    uint64_t start_tstates_ = 0;

    uint32_t sample_rate_ = 44100;
    uint16_t channels_ = 1;
    uint16_t bits_per_sample_ = 8;
    std::vector<uint8_t> raw_data_;  ///< Raw PCM sample data from the WAV file.
    uint32_t data_size_ = 0;

    /// ZX Spectrum CPU clock in Hz (3.5 MHz).
    static constexpr uint32_t CPU_CLOCK_HZ = 3500000;

    /// Number of bytes per sample frame (channels * bits_per_sample / 8).
    uint32_t bytes_per_frame() const {
        return static_cast<uint32_t>(channels_) * (bits_per_sample_ / 8);
    }

    /// Total number of sample frames in the loaded data.
    uint32_t total_frames() const {
        uint32_t bpf = bytes_per_frame();
        return bpf ? data_size_ / bpf : 0;
    }

    /// Convert a sample frame at the given index to an EAR bit (0 or 1).
    /// For stereo, uses the left channel only.
    uint8_t sample_to_ear(uint32_t frame_index) const;
};
