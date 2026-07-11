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

    /// Integrate the current source levels over `master_cycles` of emulated
    /// time. Called for every slice of emulated time, NOT once per output
    /// sample — see emit_sample().
    void accumulate(const Beeper& beeper, const TurboSound& ts, const Dac& dac,
                    uint32_t master_cycles);

    /// Emit one stereo output sample: the time-weighted average of everything
    /// accumulate() has been fed since the previous emit, then reset the
    /// accumulator. Adds the sample to the ring buffer.
    ///
    /// Averaging (a box filter) rather than point-sampling is what makes beeper
    /// music sound right. An output sample is ~635 master cycles (~79 T-states)
    /// apart; a 1-bit beeper engine toggles the speaker far faster than that, so
    /// reading "is EAR high right now?" once per sample DISCARDS most toggles and
    /// snaps the surviving edges to sample boundaries. The discarded energy does
    /// not vanish — it folds down into the audible band as an inharmonic whistle
    /// over the music. Measured against FUSE on the Cesare intro, point-sampling
    /// put 6.2% of total energy above 6 kHz (3.7% above 10 kHz) where FUSE, which
    /// integrates the speaker level across each sample period, has 0.5% / 0.0%.
    ///
    /// The average is taken at emulated-instruction granularity: the source
    /// levels are constant between accumulate() calls, so a sample straddling an
    /// instruction boundary gets each level weighted by how long it actually held.
    void emit_sample();

    /// Point-sample the current source states into one output sample.
    /// Equivalent to accumulate(..., 1) + emit_sample(). Retained for the unit
    /// tests, which assert the mixer's transfer function on steady inputs; the
    /// emulator uses accumulate()/emit_sample() so that audio is band-limited.
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

    /// Drive the speaker-exclusive gate (VHDL audio_mixer.vhd:80-81). When
    /// true, the EAR / MIC / tape-EAR contributions are forced to 0. The
    /// composite signal (`beep_spkr_excl` = NR 0x06 b6 AND NR 0x08 b4,
    /// VHDL zxnext.vhd:6504) is computed by Emulator and forwarded here.
    void set_exc_i(bool v) { exc_i_ = v; }

private:
    /// The VHDL 13-bit unsigned mix (audio_mixer.vhd:99-100) of the current
    /// source levels. Pure function of the sources — no state, no filtering.
    void mix(const Beeper& beeper, const TurboSound& ts, const Dac& dac,
             uint16_t& pcm_L, uint16_t& pcm_R) const;

    // Box-filter accumulator: sum of (13-bit mix x master cycles held), and the
    // total cycles, since the last emit_sample().
    uint64_t acc_l_ = 0;
    uint64_t acc_r_ = 0;
    uint64_t acc_cycles_ = 0;

    // Ring buffer: stereo int16_t pairs
    std::vector<int16_t> buffer_;
    int write_pos_ = 0;
    int read_pos_ = 0;
    int count_ = 0;

    RecordCallback record_callback_;

    I2s* i2s_{nullptr};  // Not owned; Emulator owns the I2s instance.

    // VHDL audio_mixer.vhd:80-81 — when '1', ear/mic muxes feed (others=>'0').
    bool exc_i_{false};
};
