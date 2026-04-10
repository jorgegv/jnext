#pragma once

#include <cstdint>
#include <array>
#include "audio/ay_chip.h"

/// TurboSound Next: 3× YM2149/AY-3-8910 with stereo panning.
///
/// Manages three independent AY chips, with per-chip stereo/mono mode
/// and panning control. Implements the VHDL turbosound.vhd logic for
/// AY selection, register routing, and stereo mixing.
///
/// Port interface:
///   - reg_addr(data): port 0xFFFD write — selects register or changes AY/panning
///   - reg_write(data): port 0xBFFD write — writes to selected register on active AY
///   - reg_read(): port 0xBFFD read — reads from selected register on active AY
class TurboSound {
public:
    TurboSound();

    void reset();

    /// Enable/disable TurboSound (when disabled, only AY#0 is active).
    void set_enabled(bool en) { enabled_ = en; }
    bool enabled() const { return enabled_; }

    /// Set AY/YM mode for all chips.
    void set_ay_mode(bool ay);

    /// Set stereo mode: false=ABC (default), true=ACB.
    void set_stereo_mode(bool acb) { stereo_mode_ = acb; }

    /// Set per-chip mono flags (bit 0=AY#0, bit 1=AY#1, bit 2=AY#2).
    void set_mono_mode(uint8_t flags) { mono_mode_ = flags & 0x07; }

    /// Port 0xFFFD write: register select or AY/panning control.
    /// When bits 7:5 = "000", selects a register on the active AY.
    /// When bit 7 = 1 and bits 4:2 = "111", changes active AY and panning.
    void reg_addr(uint8_t data);

    /// Port 0xBFFD write: write data to selected register on active AY.
    void reg_write(uint8_t data);

    /// Port 0xBFFD read: read from selected register on active AY.
    uint8_t reg_read(bool reg_mode = false) const;

    /// Advance all active AY chips by one PSG clock-enable tick.
    void tick();

    /// Get mixed stereo output (12-bit per channel).
    uint16_t pcm_left() const { return pcm_L_; }
    uint16_t pcm_right() const { return pcm_R_; }

    /// Access individual AY chip (for debugging).
    const AyChip& ay(int idx) const { return ay_[idx]; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::array<AyChip, 3> ay_;   // AY#0 (id=3), AY#1 (id=2), AY#2 (id=1)
    uint8_t ay_select_ = 3;      // Active AY: 3=AY#0, 2=AY#1, 1=AY#2

    // Per-chip panning: bits [1:0] = {left_en, right_en}
    // Default: both channels enabled (0b11)
    std::array<uint8_t, 3> pan_{};

    bool enabled_ = false;        // TurboSound enable (false = only AY#0)
    bool stereo_mode_ = false;    // false=ABC, true=ACB
    uint8_t mono_mode_ = 0;      // Per-chip mono flags

    uint16_t pcm_L_ = 0;
    uint16_t pcm_R_ = 0;

    int active_ay_index() const;
    void compute_stereo_mix();
};
