#pragma once

#include <cstdint>
#include <array>
#include "audio/ay_chip.h"

namespace jnext { namespace save { class StateDesc; } }
#include "audio/audio_mute.h"

/// What an AY port write did, for the debugger's dropped-write counters (GH #530).
/// Each non-Applied value is one VHDL term that makes the write a no-op.
enum class AyPortStatus : uint8_t {
    Applied = 0,
    HeldInReset,          ///< audio_ay_reset: turbosound.vhd:121, ym2149.vhd:170,184
    ChipSelectTurboOff,   ///< select/pan command needs turbosound_en_i: turbosound.vhd:129
    SelectIgnored,        ///< psg_addr needs d(7:5)="000": turbosound.vhd:141
    AddressHigh,          ///< register address 16-31: ym2149.vhd:188
};

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

    /// The VHDL `reset` (hard or soft) as seen by turbosound.vhd: RESET_H on the
    /// chips (address, registers, outputs; the generators keep running) plus the
    /// NR-driven shadows, which Emulator::init re-derives. Also cleared: ay_select,
    /// panning and the cached PCM outputs.
    void reset();

    /// Audio AY reset (VHDL `audio_ay_reset` — turbosound.vhd:118-138).
    /// The synchronous reset block in VHDL clears ONLY ay_select <= "11"
    /// and psg{0,1,2}_pan <= "11"; enabled / stereo_mode / mono_mode are
    /// external NR-driven inputs (turbosound_en_i / stereo_mode_i /
    /// mono_mode_i, supplied by NR 0x08 b1/b5 and NR 0x09) and MUST NOT
    /// be cleared by this path. Used by the NR 0x06 psg_mode=11 toggle
    /// (zxnext.vhd:6379 — `audio_ay_reset <= ... or nr_06_psg_mode = "11"`).
    /// Also clears each AyChip's register file and cached PCM outputs to
    /// match the VHDL ym2149 reset that fires alongside audio_ay_reset.
    void reset_ay_only();

    /// Hard reset / construction: every chip at its declared initial values
    /// (ym2149.vhd:105-125), then reset().
    void power_on();

    /// The audio_ay_reset LEVEL (zxnext.vhd:6379: reset or nr_06_psg_mode = "11"):
    /// while held, port writes and the select/pan command are ignored, ay_select
    /// and the pans stay "11" (turbosound.vhd:120-126); the generators keep
    /// running. A plain input: the emulator applies the onset reset_ay_only() and
    /// re-derives the level from NR 0x06 after init() and after a state load.
    void set_reset_held(bool h) { reset_held_ = h; }
    bool reset_held() const { return reset_held_; }

    /// Enable/disable TurboSound (when disabled, only the selected chip reaches the mix,
    /// turbosound.vhd:197/252/307; all three keep running).
    void set_enabled(bool en) { enabled_ = en; mix_dirty_ = true; }
    bool enabled() const { return enabled_; }

    /// Set AY/YM mode for all chips.
    void set_ay_mode(bool ay);

    /// Set stereo mode: false=ABC (default), true=ACB.
    void set_stereo_mode(bool acb) { stereo_mode_ = acb; mix_dirty_ = true; }
    /// Current stereo mode. Mirrors `stereo_mode_i` from NR 0x08 bit 5
    /// (VHDL zxnext.vhd:5177, :6400).
    bool stereo_mode() const { return stereo_mode_; }

    /// Set per-chip mono flags (bit 0=AY#0, bit 1=AY#1, bit 2=AY#2).
    void set_mono_mode(uint8_t flags) { mono_mode_ = flags & 0x07; mix_dirty_ = true; }
    /// Current per-chip mono flags. Mirrors `mono_mode_i` from NR 0x09
    /// bits 7:5 (VHDL zxnext.vhd:5186, :6398).
    uint8_t mono_mode() const { return mono_mode_; }

    /// AY/YM mode flag (shared across all 3 AY chips). Mirrors
    /// `aymode_i` from NR 0x06 bit 0 (VHDL zxnext.vhd:6389).
    bool ay_mode() const { return ay_[0].is_ay_mode(); }

    /// Port 0xFFFD write: register select or AY/panning control.
    /// When bits 7:5 = "000", selects a register on the active AY.
    /// When bit 7 = 1 and bits 4:2 = "111", changes active AY and panning.
    /// Returns what the write did (GH #530); the value may be ignored.
    AyPortStatus reg_addr(uint8_t data);

    /// Port 0xBFFD write: write data to selected register on active AY.
    /// Returns what the write did (GH #530); the value may be ignored.
    AyPortStatus reg_write(uint8_t data);

    /// Port 0xBFFD read: read from selected register on active AY.
    uint8_t reg_read(bool reg_mode = false) const;

    /// Advance all three AY chips by one PSG clock-enable tick; every ym2149 runs on
    /// clock_en_i (turbosound.vhd:162/217/272).
    void tick();
    /// `count` ticks in a row, for the HALT batch's PSG replay (GH #452): a loop of its own around the
    /// same inlined body, so that tick() keeps its ONE caller (the device cluster, which the compiler
    /// inlines) and the two cannot be folded into one function with two callers.
    void tick_n(uint32_t count);

    /// Debugger-only per-chip mute (AudioMute::AY0/AY1/AY2; bit i = chip i).
    /// Gates ONLY this chip's contribution to the AUDIBLE 3-PSG sum
    /// (pcm_left()/pcm_right(), chip_pcm()); the chip keeps ticking, its
    /// register file keeps reading back, and the hardware sum that save_state()
    /// serialises is computed without the mute. Not hardware, not machine state
    /// -- see audio/audio_mute.h. Deliberately not touched by reset() /
    /// reset_ay_only() / save_state() / load_state().
    void set_chip_mute_mask(uint8_t m) {
        chip_mute_mask_ = m & AudioMute::AY_ALL;
        for (int i = 0; i < 3; ++i) chip_gate_[i] = ((chip_mute_mask_ >> i) & 1) ? 0 : 0xFFFF;
        mix_dirty_ = true;
    }
    uint8_t chip_mute_mask() const { return chip_mute_mask_; }

    /// Debugger-only per-channel mute (AudioChannelMute AY bits 0-8; bit 3*chip+channel).
    /// A set bit zeroes that channel's output BEFORE the chip's A/B/C mux, so ACB,
    /// mono and pan stay exact; like the chip mute it gates only the AUDIBLE sum,
    /// never the hardware one, and is not touched by reset() / save_state() /
    /// load_state() (audio/audio_mute.h).
    void set_channel_mute_mask(uint16_t m) { channel_mute_mask_ = m & AudioChannelMute::AY_ALL; mix_dirty_ = true; }
    uint16_t channel_mute_mask() const { return channel_mute_mask_; }

    /// Get the audible mixed stereo output (12-bit per channel): the hardware sum
    /// less the chips the debugger muted.
    uint16_t pcm_left() const { return pcm_L_; }
    uint16_t pcm_right() const { return pcm_R_; }

    /// Return one chip's post-mode/post-pan contribution to the hardware sum.
    /// Like pcm_left()/pcm_right(), these values change only on a PSG tick.
    /// This is a host-mixer observation only and does not alter guest state.
    void chip_pcm(int idx, uint16_t& left, uint16_t& right) const;
    bool chip_pcm_valid() const { return chip_pcm_valid_; }

    /// Access individual AY chip (for debugging).
    const AyChip& ay(int idx) const { return ay_[idx]; }

    /// Index (0-2) of the chip ay_select_ names, and one chip's pan bits
    /// ({left_en, right_en}, turbosound.vhd:129-136). Debugger views.
    int selected_index() const { return active_ay_index(); }
    uint8_t pan(int idx) const { return pan_[idx]; }

    /// The chip a classic AY program reaches through ports 0xFFFD/0xBFFD:
    /// the one ay_select_ names (AY#0 unless TurboSound moved it).
    const AyChip& active_ay() const { return ay_[active_ay_index()]; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// GH #27 S5 — the ONE field list (design §9.2): the three chips'
    /// declarations in `ay_` order, then TurboSound's own scalars.
    void describe_state(jnext::save::StateDesc& d);

private:
    std::array<AyChip, 3> ay_;   // AY#0 (id=3), AY#1 (id=2), AY#2 (id=1)
    uint8_t ay_select_ = 3;      // Active AY: 3=AY#0, 2=AY#1, 1=AY#2

    // Per-chip panning: bits [1:0] = {left_en, right_en}
    // Default: both channels enabled (0b11)
    std::array<uint8_t, 3> pan_{};

    bool reset_held_ = false;     // audio_ay_reset level from NR 0x06 psg_mode = 11 (not serialised)
    bool enabled_ = false;        // TurboSound enable (false = only the selected AY is output)
    bool stereo_mode_ = false;    // false=ABC, true=ACB
    uint8_t mono_mode_ = 0;      // Per-chip mono flags

    // The audible sum (chips muted by chip_mute_mask_ excluded) and the hardware
    // sum (every active chip, no mute). Only the hardware pair is machine state:
    // describe_state() serialises it, so a debugger mute never reaches a saved
    // state, a rewind slot or the Z80 (audio/audio_mute.h).
    uint16_t pcm_L_ = 0;
    uint16_t pcm_R_ = 0;
    uint16_t hw_pcm_L_ = 0;
    uint16_t hw_pcm_R_ = 0;
    std::array<uint16_t, 3> chip_pcm_L_{};
    std::array<uint16_t, 3> chip_pcm_R_{};
    bool chip_pcm_valid_ = true;

    // GH #440. The mix is a pure function of the chips' outputs, ay_select_,
    // pan_, enabled_, stereo_mode_, mono_mode_, chip_mute_mask_ and channel_mute_mask_
    // (turbosound.vhd:184-337), so tick() recomputes it only when a chip's
    // output changed or this flag is set by a writer of any other input
    // (the setters, reg_addr, reset, reset_ay_only, load_state).
    bool mix_dirty_ = true;

    // Debugger-only; NOT machine state (not reset, not serialised).
    uint8_t chip_mute_mask_ = AudioMute::NONE;
    // chip_mute_mask_ as AND masks (0xFFFF audible, 0 muted), so the per-recompute loop gates a chip with
    // one AND instead of a bit extraction and a select. Kept in step by set_chip_mute_mask() only.
    uint16_t chip_gate_[3] = {0xFFFF, 0xFFFF, 0xFFFF};
    uint16_t channel_mute_mask_ = AudioChannelMute::NONE;

    int active_ay_index() const;
    // always_inline: the hardware call passes silenced == 0, which folds away, so the hot path is the
    // pre-mute one (GH #533 bench).
    [[gnu::always_inline]] inline void compute_chip_stereo(int idx, uint8_t silenced, uint16_t& left, uint16_t& right) const;
    void compute_stereo_mix_channels_();
    [[gnu::always_inline]] inline void compute_stereo_mix();
    [[gnu::always_inline]] inline void tick_impl_();
};
