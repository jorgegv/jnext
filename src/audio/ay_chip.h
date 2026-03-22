#pragma once

#include <cstdint>
#include <array>

/// YM2149 / AY-3-8910 sound chip emulation.
///
/// Implements all 16 registers, 3 tone generators, noise generator,
/// envelope generator, and channel mixing. Volume tables and envelope
/// shapes match the VHDL ym2149.vhd from the FPGA source.
///
/// The chip is clocked via tick() at the PSG clock-enable rate
/// (28 MHz / 16 = 1.75 MHz). Internal dividers produce the tone
/// and noise generator rates.
class AyChip {
public:
    /// Chip ID (2-bit, used for register readback): 0=AY#2, 1=unused, 2=AY#1, 3=AY#0
    explicit AyChip(uint8_t id = 3);

    void reset();

    /// Select a register (0-15). addr bits 4:0 are used.
    void select_register(uint8_t addr);

    /// Write to the currently selected register.
    void write_data(uint8_t val);

    /// Read the currently selected register.
    /// @param reg_mode  If true, returns AY_ID & selected register number
    ///                  instead of register contents.
    uint8_t read_data(bool reg_mode = false) const;

    /// Get the currently selected register number.
    uint8_t selected_register() const { return addr_ & 0x0F; }

    /// Read a specific register value (for debugger introspection).
    uint8_t read_register(uint8_t reg) const { return reg_[reg & 0x0F]; }

    /// Set AY mode (true) or YM mode (false).
    void set_ay_mode(bool ay) { ay_mode_ = ay; }

    /// Advance the chip by one PSG clock-enable tick (1.75 MHz rate).
    /// The internal /8 divider produces the tone generator clock;
    /// the /16 divider produces the noise generator clock.
    void tick();

    /// Get current output level for channel A/B/C (0-255).
    uint8_t output_a() const { return out_a_; }
    uint8_t output_b() const { return out_b_; }
    uint8_t output_c() const { return out_c_; }

private:
    uint8_t id_;
    bool ay_mode_ = true;  // true=AY, false=YM

    // Register file
    std::array<uint8_t, 16> reg_{};
    uint8_t addr_ = 0;

    // Internal clock divider (divides PSG clock enable by 8 with I_SEL_L=1)
    uint8_t cnt_div_ = 0;
    bool noise_div_ = false;
    bool ena_div_ = false;
    bool ena_div_noise_ = false;

    // Tone generators (3 channels, 12-bit counters)
    std::array<uint16_t, 3> tone_cnt_{};
    std::array<bool, 3> tone_op_{};

    // Noise generator (5-bit counter + 17-bit LFSR)
    uint8_t noise_cnt_ = 0;
    uint32_t poly17_ = 0;
    bool noise_op_ = false;

    // Envelope generator
    uint16_t env_cnt_ = 0;
    bool env_ena_ = false;
    bool env_reset_ = false;
    uint8_t env_vol_ = 0;   // 5-bit envelope volume
    bool env_inc_ = false;   // true = counting up
    bool env_hold_ = false;

    // Output registers
    uint8_t out_a_ = 0;
    uint8_t out_b_ = 0;
    uint8_t out_c_ = 0;

    // Volume lookup tables (from VHDL)
    static const uint8_t vol_table_ay_[16];
    static const uint8_t vol_table_ym_[32];

    // Internal helpers
    void update_divider();
    void update_tone_generators();
    void update_noise_generator();
    void update_envelope();
    void update_output();

    uint16_t tone_period(int ch) const;
    uint16_t tone_comp(int ch) const;
    uint8_t noise_period() const;
    uint8_t noise_comp() const;
    uint16_t env_period() const;
    uint16_t env_comp() const;
};
