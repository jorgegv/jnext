#include "audio/turbosound.h"
#include "core/saveable.h"

TurboSound::TurboSound()
    : ay_{AyChip(3), AyChip(2), AyChip(1)}  // AY#0=id 3, AY#1=id 2, AY#2=id 1
{
    reset();
}

void TurboSound::reset()
{
    // Full reset (power-on / hard reset). Cleans the synchronously-reset
    // selector + pan AND the NR-driven shadow fields. The latter are not
    // owned by this module per VHDL turbosound.vhd:118-138, but at
    // power-on the NR 0x08 / NR 0x09 registers are themselves reset to
    // their VHDL defaults (zxnext.vhd reset clauses), so zeroing them
    // here matches the system-wide reset behaviour.
    reset_ay_only();
    enabled_ = false;
    stereo_mode_ = false;
    mono_mode_ = 0;
}

void TurboSound::reset_ay_only()
{
    // VHDL turbosound.vhd:118-138 — synchronous reset clause clears only
    //   ay_select <= "11"; psg{0,1,2}_pan <= "11";
    // The audio_ay_reset signal also feeds each ym2149's reset port, so
    // we clear the per-chip register files + cached outputs to match.
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
    // Check for AY select / panning command:
    // VHDL: turbosound_en_i = '1' AND psg_reg_addr_i = '1'
    //       AND psg_d_i(7) = '1' AND psg_d_i(4:2) = "111"
    if (enabled_ && (data & 0x80) && ((data >> 2) & 0x07) == 0x07) {
        uint8_t sel = data & 0x03;
        uint8_t new_pan = (data >> 5) & 0x03;

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
    ay_[active_ay_index()].write_data(data);
}

uint8_t TurboSound::reg_read(bool reg_mode) const
{
    return ay_[active_ay_index()].read_data(reg_mode);
}

void TurboSound::tick()
{
    // Always tick AY#0
    ay_[0].tick();

    // Tick AY#1 and AY#2 only if TurboSound is enabled,
    // or if that AY is currently selected
    if (enabled_ || ay_select_ == 2) {
        ay_[1].tick();
    }
    if (enabled_ || ay_select_ == 1) {
        ay_[2].tick();
    }

    compute_stereo_mix();
}

void TurboSound::compute_stereo_mix()
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

void TurboSound::save_state(StateWriter& w) const
{
    for (const auto& a : ay_) a.save_state(w);
    w.write_u8(ay_select_);
    for (int i = 0; i < 3; ++i) w.write_u8(pan_[i]);
    w.write_bool(enabled_);
    w.write_bool(stereo_mode_);
    w.write_u8(mono_mode_);
    w.write_u16(pcm_L_);
    w.write_u16(pcm_R_);
}

void TurboSound::load_state(StateReader& r)
{
    for (auto& a : ay_) a.load_state(r);
    ay_select_   = r.read_u8();
    for (int i = 0; i < 3; ++i) pan_[i] = r.read_u8();
    enabled_     = r.read_bool();
    stereo_mode_ = r.read_bool();
    mono_mode_   = r.read_u8();
    pcm_L_       = r.read_u16();
    pcm_R_       = r.read_u16();

    // Per-chip host observations were not part of the historical snapshot
    // schema, and appending them here would shift every following component.
    // Do not reconstruct them from current pan/mode: a snapshot may have been
    // taken after a write but before the PSG tick which updates pcm_L_/pcm_R_.
    // Mixer falls back to the serialized aggregate until the next PSG tick,
    // preserving old snapshots and deferring host-only balancing momentarily.
    chip_pcm_L_.fill(0);
    chip_pcm_R_.fill(0);
    chip_pcm_valid_ = false;
}
