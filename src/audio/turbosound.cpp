#include "audio/turbosound.h"

TurboSound::TurboSound()
    : ay_{AyChip(3), AyChip(2), AyChip(1)}  // AY#0=id 3, AY#1=id 2, AY#2=id 1
{
    reset();
}

void TurboSound::reset()
{
    for (auto& a : ay_) a.reset();
    ay_select_ = 3;  // Default: AY#0 selected (VHDL: "11")
    pan_.fill(0x03);  // Both L and R enabled for all chips
    enabled_ = false;
    stereo_mode_ = false;
    mono_mode_ = 0;
    pcm_L_ = 0;
    pcm_R_ = 0;
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
        // Check if this PSG is active
        bool active;
        if (i == 0) {
            active = (ay_select_ == 3) || enabled_;
        } else if (i == 1) {
            active = (ay_select_ == 2) || enabled_;
        } else {
            active = (ay_select_ == 1) || enabled_;
        }

        if (!active) continue;

        uint16_t a = ay_[i].output_a();
        uint16_t b = ay_[i].output_b();
        uint16_t c = ay_[i].output_c();
        bool mono = (mono_mode_ >> i) & 1;

        // L_mux = C when (ACB or mono) else B
        uint16_t L_mux = (stereo_mode_ || mono) ? c : b;
        // L_sum = L_mux + A (9-bit)
        uint16_t L_sum = L_mux + a;

        // R_mux = L_sum when mono else C (9-bit: mono gives A+B+C partial)
        uint16_t R_mux = mono ? L_sum : c;
        // R_sum = R_mux + B (10-bit)
        uint16_t R_sum = R_mux + b;

        // L_fin = R_sum when mono else L_sum
        uint16_t L_fin = mono ? R_sum : L_sum;

        // Apply panning (pan bit 1 = left enable, bit 0 = right enable)
        uint16_t psg_L = (pan_[i] & 0x02) ? L_fin : 0;
        uint16_t psg_R = (pan_[i] & 0x01) ? R_sum : 0;

        total_L += psg_L;
        total_R += psg_R;
    }

    // Output: 12-bit (0-4095 max with all 3 PSGs at full volume)
    pcm_L_ = total_L & 0x0FFF;
    pcm_R_ = total_R & 0x0FFF;
}
