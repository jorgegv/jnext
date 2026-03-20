#pragma once

#include <cstdint>
#include <vector>
#include "audio/beeper.h"
#include "audio/turbosound.h"
#include "audio/dac.h"

/// Audio mixer: combines all audio sources into stereo PCM output.
///
/// Matches VHDL audio_mixer.vhd scaling:
///   EAR   → 512 (13-bit)
///   MIC   → 128 (13-bit)
///   AY    → 12-bit, zero-extended to 13-bit
///   DAC   → 9-bit left-shifted by 2 (×4) to 13-bit
///   (Pi I2S not emulated — would be 10-bit, zero-extended to 13-bit)
///
/// The mixer accumulates samples at 44100 Hz into a ring buffer.
/// The SDL audio bridge pulls samples from this buffer.
class Mixer {
public:
    static constexpr int SAMPLE_RATE = 44100;

    /// Maximum ring buffer size: ~4 frames worth of samples at 50 Hz.
    static constexpr int RING_BUFFER_SIZE = 4096;

    Mixer();

    void reset();

    /// Generate one stereo sample from current source states.
    /// Adds the sample to the ring buffer.
    void generate_sample(const Beeper& beeper, const TurboSound& ts, const Dac& dac);

    /// Get the number of samples available in the ring buffer.
    int available() const;

    /// Read up to `count` stereo samples into `out` (interleaved L, R, L, R...).
    /// Returns the number of stereo samples actually read.
    int read_samples(int16_t* out, int count);

private:
    // Ring buffer: stereo int16_t pairs
    std::vector<int16_t> buffer_;
    int write_pos_ = 0;
    int read_pos_ = 0;
    int count_ = 0;
};
