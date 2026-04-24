#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include "audio/beeper.h"
#include "audio/turbosound.h"
#include "audio/dac.h"

class I2s;

/// Audio mixer: combines all audio sources into stereo PCM output.
///
/// Matches VHDL audio_mixer.vhd scaling:
///   EAR   → 512 (13-bit)
///   MIC   → 128 (13-bit)
///   AY    → 12-bit, zero-extended to 13-bit
///   DAC   → 9-bit left-shifted by 2 (×4) to 13-bit
///   Pi I2S → 10-bit, zero-extended to 13-bit (stub via set_i2s_source)
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

    /// Set a callback that receives every generated stereo sample pair.
    /// Used by VideoRecorder to capture audio at the source.
    /// Signature: (const int16_t* samples, int stereo_pair_count).
    using RecordCallback = std::function<void(const int16_t*, int)>;
    void set_record_callback(RecordCallback cb) { record_callback_ = std::move(cb); }

    /// Wire the I2s source (not owned). When set, generate_sample() adds
    /// the latched 10-bit L/R pair (zero-extended) into the 13-bit sum,
    /// mirroring audio_mixer.vhd:89-90,99-100.
    void set_i2s_source(I2s* i2s) { i2s_ = i2s; }

private:
    // Ring buffer: stereo int16_t pairs
    std::vector<int16_t> buffer_;
    int write_pos_ = 0;
    int read_pos_ = 0;
    int count_ = 0;

    RecordCallback record_callback_;

    I2s* i2s_{nullptr};  // Not owned; Emulator owns the I2s instance.
};
