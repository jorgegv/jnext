#include "audio/turbosound.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

namespace {

// GH #27 S5 — the per-chip key table for the §9.4 loop collapse in
// `TurboSound::describe_state`.
//
// Three chips share one field list, so the keys carry the chip number: a
// `.jns` says `ay2_env_vol`, not a third anonymous `env_vol` that
// `JsonWriteDesc` would silently drop on top of the second. The numbering is
// the `ay_` array index (AY#0/#1/#2), NOT the AY id the constructor assigns
// (3/2/1) — the array index is what the port decode selects with
// `ay_select_`.
//
// LITERALS, concatenated by the preprocessor, and NOT built at run time —
// `StateDesc::fail()` STORES the `const char*` it is handed rather than
// copying it, so a key formatted into a stack buffer would dangle in exactly
// the refusal path whose job is to name the field.
#define AY_KEYS(p)                                                          \
    { /*  0 */ p "_ay_mode",    p "_reg",                                   \
      /*  2 */ p "_addr",       p "_cnt_div",                               \
      /*  4 */ p "_noise_div",  p "_ena_div",       p "_ena_div_noise",     \
      /*  7 */ p "_tone_cnt_a", p "_tone_cnt_b",    p "_tone_cnt_c",        \
      /* 10 */ p "_tone_op_a",  p "_tone_op_b",     p "_tone_op_c",         \
      /* 13 */ p "_noise_cnt",  p "_poly17",        p "_noise_op",          \
      /* 16 */ p "_env_cnt",    p "_env_ena",       p "_env_reset",         \
      /* 19 */ p "_env_vol",    p "_env_inc",       p "_env_hold",          \
      /* 22 */ p "_out_a",      p "_out_b",         p "_out_c" }

const char* const kAyKeys[3][25] = {
    AY_KEYS("ay0"), AY_KEYS("ay1"), AY_KEYS("ay2"),
};

#undef AY_KEYS

// The per-chip stereo pan bytes (NR 0x09 / zxnext.vhd), same numbering.
const char* const kPanKeys[3] = { "ay0_pan", "ay1_pan", "ay2_pan" };

}  // namespace

TurboSound::TurboSound()
    : ay_{AyChip(3), AyChip(2), AyChip(1)}  // AY#0=id 3, AY#1=id 2, AY#2=id 1
{
    reset();
}

void TurboSound::power_on()
{
    // Hard reset = FPGA reconfiguration (zxnext_top_issue2.vhd:1195): every
    // generator takes its declared initial value (ym2149.vhd:105-125).
    for (auto& a : ay_) a.power_on();
    reset();
}

void TurboSound::reset()
{
    // The VHDL `reset` (hard or soft) as seen by turbosound.vhd: RESET_H on the
    // chips (the generators keep running, ym2149.vhd:258-519) plus the NR-driven
    // shadows, which Emulator::init re-derives. Cleans the synchronously-reset
    // selector + pan AND the NR-driven shadow fields. The latter are not
    // owned by this module per VHDL turbosound.vhd:118-138, but at
    // power-on the NR 0x08 / NR 0x09 registers are themselves reset to
    // their VHDL defaults (zxnext.vhd reset clauses), so zeroing them
    // here matches the system-wide reset behaviour.
    reset_ay_only();
    enabled_ = false;
    stereo_mode_ = false;
    mono_mode_ = 0;
    reset_held_ = false;
    mix_dirty_ = true;
}

void TurboSound::reset_ay_only()
{
    // VHDL turbosound.vhd:118-138 — synchronous reset clause clears only
    //   ay_select <= "11"; psg{0,1,2}_pan <= "11";
    // The audio_ay_reset signal also feeds each ym2149's reset port, so
    // we clear the per-chip register files + cached outputs to match (the
    // generators keep running, ym2149.vhd:258-519).
    // enabled / stereo_mode / mono_mode are external NR-driven inputs
    // and intentionally LEFT UNTOUCHED here.
    for (auto& a : ay_) a.reset();
    ay_select_ = 3;  // Default: AY#0 selected (VHDL: "11")
    pan_.fill(0x03);  // Both L and R enabled for all chips
    pcm_L_ = 0;
    pcm_R_ = 0;
    chip_pcm_L_.fill(0);
    chip_pcm_R_.fill(0);
    chip_pcm_valid_ = true;
    mix_dirty_ = true;
}

void TurboSound::set_ay_mode(bool ay)
{
    for (auto& a : ay_) a.set_ay_mode(ay);
}

int TurboSound::active_ay_index() const
{
    // VHDL: ay_select "11"=AY#0, "10"=AY#1, "01"=AY#2
    switch (ay_select_) {
        case 2: return 1;  // AY#1
        case 1: return 2;  // AY#2
        default: return 0; // AY#0
    }
}

void TurboSound::reg_addr(uint8_t data)
{
    // Held in reset (NR 0x06 psg_mode = 11, zxnext.vhd:6379): the select/pan command
    // (turbosound.vhd:121, reset branch first) and the register select
    // (ym2149.vhd:170) are both ignored.
    if (reset_held_) return;
    // Check for AY select / panning command:
    // VHDL: turbosound_en_i = '1' AND psg_reg_addr_i = '1'
    //       AND psg_d_i(7) = '1' AND psg_d_i(4:2) = "111"
    if (enabled_ && (data & 0x80) && ((data >> 2) & 0x07) == 0x07) {
        uint8_t sel = data & 0x03;
        uint8_t new_pan = (data >> 5) & 0x03;
        mix_dirty_ = true;

        switch (sel) {
            case 0x02:  // AY#1
                pan_[1] = new_pan;
                ay_select_ = 2;
                break;
            case 0x01:  // AY#2
                pan_[2] = new_pan;
                ay_select_ = 1;
                break;
            default:    // AY#0 (0x00 or 0x03)
                pan_[0] = new_pan;
                ay_select_ = 3;
                break;
        }
        return;
    }

    // Normal register select: bits 7:5 must be "000"
    if ((data & 0xE0) == 0) {
        ay_[active_ay_index()].select_register(data);
    }
}

void TurboSound::reg_write(uint8_t data)
{
    if (reset_held_) return;   // ym2149.vhd:184, reset branch before busctrl_we
    ay_[active_ay_index()].write_data(data);
}

uint8_t TurboSound::reg_read(bool reg_mode) const
{
    return ay_[active_ay_index()].read_data(reg_mode);
}

[[gnu::always_inline]] inline void TurboSound::tick_impl_()
{
    // VHDL turbosound.vhd:162/217/272: all three ym2149 instances run on the
    // ungated clock_en_i (zxnext.vhd:6385, i_CLK_PSG_EN). TurboSound enable and
    // ay_select gate only each chip's contribution to the mix (:197/:252/:307),
    // which compute_chip_stereo() applies. GH #442.
    bool changed = ay_[0].tick();
    changed |= ay_[1].tick();
    changed |= ay_[2].tick();

    // GH #440: the mix only moves when a chip's output or a mix input did.
    if (changed || mix_dirty_) {
        compute_stereo_mix();
        mix_dirty_ = false;
    }
}

void TurboSound::tick() { tick_impl_(); }
[[gnu::noinline]] void TurboSound::tick_cold() { tick_impl_(); ++cold_hits_; }

inline void TurboSound::compute_stereo_mix()
{
    // Per-chip stereo mixing (matches VHDL turbosound.vhd exactly)
    // For each PSG:
    //   L_mux = C when (ACB stereo or mono) else B
    //   L_sum = L_mux + A  (9-bit)
    //   R_mux = L_sum when mono else C (9-bit)
    //   R_sum = R_mux + B  (10-bit)
    //   L_fin = R_sum when mono else L_sum (10-bit)
    //
    // Then panning gates L and R independently.
    // Final: sum all 3 PSGs for 12-bit output.

    uint16_t total_L = 0;
    uint16_t total_R = 0;

    for (int i = 0; i < 3; i++) {
        compute_chip_stereo(i, chip_pcm_L_[i], chip_pcm_R_[i]);

        total_L += chip_pcm_L_[i];
        total_R += chip_pcm_R_[i];
    }

    // Output: 12-bit (0-4095 max with all 3 PSGs at full volume)
    pcm_L_ = total_L & 0x0FFF;
    pcm_R_ = total_R & 0x0FFF;
    chip_pcm_valid_ = true;
}

void TurboSound::chip_pcm(int idx, uint16_t& left, uint16_t& right) const
{
    left = right = 0;
    if (idx < 0 || idx >= 3 || !chip_pcm_valid_) return;
    left = chip_pcm_L_[idx];
    right = chip_pcm_R_[idx];
}

void TurboSound::compute_chip_stereo(int idx, uint16_t& left, uint16_t& right) const
{
    left = right = 0;

    const bool active =
        idx == 0 ? (ay_select_ == 3 || enabled_) :
        idx == 1 ? (ay_select_ == 2 || enabled_) :
                   (ay_select_ == 1 || enabled_);
    if (!active || ((chip_mute_mask_ >> idx) & 1)) return;

    const uint16_t a = ay_[idx].output_a();
    const uint16_t b = ay_[idx].output_b();
    const uint16_t c = ay_[idx].output_c();
    const bool mono = (mono_mode_ >> idx) & 1;

    const uint16_t left_mux = (stereo_mode_ || mono) ? c : b;
    const uint16_t left_sum = left_mux + a;
    const uint16_t right_mux = mono ? left_sum : c;
    const uint16_t right_sum = right_mux + b;
    const uint16_t left_final = mono ? right_sum : left_sum;

    left = (pan_[idx] & 0x02) ? left_final : 0;
    right = (pan_[idx] & 0x01) ? right_sum : 0;
}

// GH #27 S5 — the ONE field list (design §9.2): three chips, then the
// selector, pans and mixer scalars. §9.4's loop-collapse class.
void TurboSound::describe_state(jnext::save::StateDesc& d)
{
    for (int i = 0; i < 3; ++i) ay_[i].describe_state(d, kAyKeys[i]);
    d.u8("ay_select", ay_select_);
    for (int i = 0; i < 3; ++i) d.u8(kPanKeys[i], pan_[i]);
    d.boolean("enabled", enabled_);
    d.boolean("stereo_mode", stereo_mode_);
    d.u8("mono_mode", mono_mode_);
    d.u16("pcm_l", pcm_L_);
    d.u16("pcm_r", pcm_R_);
}

void TurboSound::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void TurboSound::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);

    // Per-chip host observations are NOT declared — they were never in the
    // stream, and declaring them would shift every following block. They are
    // not reconstructed from the current pan/mode either: a snapshot may have
    // been taken after a write but before the PSG tick that updates
    // pcm_L_/pcm_R_. The mixer falls back to the serialised aggregate until
    // the next PSG tick. Kept OUTSIDE the declaration deliberately: this is a
    // reset of derived state (§9.5(7)), not a field.
    chip_pcm_L_.fill(0);
    chip_pcm_R_.fill(0);
    chip_pcm_valid_ = false;
    for (auto& a : ay_) a.mark_dirty();   // the loaded outputs may predate the loaded inputs
    mix_dirty_ = true;
}
