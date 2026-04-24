#pragma once

#include <cstdint>

/// Pi I2S audio stub.
///
/// Mirrors `audio_mixer.vhd:89-90,99-100` where the `pi_i2s_L_i` /
/// `pi_i2s_R_i` 10-bit inputs are zero-extended to 13 bits and added
/// into the mixer sum.
///
/// This is a pure latched sample-pair register — no real I2S wire /
/// clocking / protocol emulation. The stub lets the Mixer path and
/// test harness exercise the final 13-bit sum term. Setters clamp to
/// 10 bits (matching the VHDL signal width).
///
/// No port or NextREG wiring exposed here; samples are injected
/// programmatically via `Emulator::i2s()`.
class I2s {
public:
    I2s();

    void reset();

    /// Latch a new stereo sample pair. Inputs are masked to 10 bits
    /// (`& 0x3FF`) to match `std_logic_vector(9 downto 0)`.
    void set_sample(uint16_t left_10bit, uint16_t right_10bit);

    /// Current latched left channel, 0..1023.
    uint16_t left() const  { return left_; }

    /// Current latched right channel, 0..1023.
    uint16_t right() const { return right_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    uint16_t left_{0};
    uint16_t right_{0};
};
