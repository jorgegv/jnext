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
/// G113 / G73: NR 0xA2 control byte (`nr_a2_pi_i2s_ctl` per
/// `zxnext.vhd:5564`) is stored here; the gated 10-bit `pi_audio_L/R`
/// signals (per `zxnext.vhd:2358-2359`) are exposed via `pi_audio_L()`
/// and `pi_audio_R()` so the Mixer and the NR 0x2C/0x2E read handlers
/// consume the same VHDL-correct gated value.
///
/// No port or NextREG wiring exposed here; samples are injected
/// programmatically via `Emulator::i2s()` and the NR 0xA2 byte is
/// poked via `set_nr_a2_ctl()` from the NextREG write handler.
class I2s {
public:
    I2s();

    void reset();

    /// Latch a new stereo sample pair. Inputs are masked to 10 bits
    /// (`& 0x3FF`) to match `std_logic_vector(9 downto 0)`.
    void set_sample(uint16_t left_10bit, uint16_t right_10bit);

    /// Current latched left channel, 0..1023 (raw, ungated).
    uint16_t left() const  { return left_; }

    /// Current latched right channel, 0..1023 (raw, ungated).
    uint16_t right() const { return right_; }

    /// NR 0xA2 control byte (`nr_a2_pi_i2s_ctl`, VHDL zxnext.vhd:5564).
    /// Bit layout (per zxnext.vhd:2283-2290):
    ///   b7 = pi_i2s_enL
    ///   b6 = pi_i2s_enR
    ///   b4 = pi_i2s_inout
    ///   b3 = pi_i2s_muteL
    ///   b2 = pi_i2s_muteR
    ///   b1 = pi_i2s_slave (commented out in VHDL; reserved)
    ///   b0 = pi_i2s_ear
    void    set_nr_a2_ctl(uint8_t v) { nr_a2_ctl_ = v; }
    uint8_t nr_a2_ctl() const         { return nr_a2_ctl_; }

    /// Gated left-channel 10-bit value per VHDL zxnext.vhd:2358:
    ///   pi_audio_L <= 0x200 (10-bit DC midpoint, "silence")
    ///                 when (pi_i2s_en=0 OR pi_i2s_muteL=1 OR pi_i2s_ear=1)
    ///                 else pi_i2s_audio_L when pi_i2s_enL=1
    ///                 else pi_i2s_audio_R;   -- cross-channel mux
    /// where pi_i2s_en = NR 0xA2 b7 OR b6.
    uint16_t pi_audio_L() const;

    /// Gated right-channel 10-bit value per VHDL zxnext.vhd:2359
    /// (mirror of pi_audio_L with R/L swapped).
    uint16_t pi_audio_R() const;

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    uint16_t left_{0};
    uint16_t right_{0};
    uint8_t  nr_a2_ctl_{0};
};
