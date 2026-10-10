#include "audio/i2s.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

I2s::I2s() { reset(); }

void I2s::reset()
{
    // The 10-bit sample rests at 0x200 (offset-binary silence), with or
    // without a Pi attached: the receiver resets its words to 0
    // (i2s_receive.vhd:129-130), and i2s.vhd:177-180 inverts the sign bit
    // on the way out, so the value the mixer sees is 0x200, not 0.
    left_       = 0x200;
    right_      = 0x200;
    // VHDL zxnext.vhd:5564 — nr_a2_pi_i2s_ctl resets to 0x00 (no Pi I2S
    // path enabled). With NR 0xA2 = 0, pi_audio_L/R are forced to the
    // 10-bit DC midpoint (0x200) per zxnext.vhd:2358-2359.
    nr_a2_ctl_  = 0;
    fe_ear_     = false;
}

void I2s::update_fe_ear()
{
    // zxnext.vhd:2361-2373, clocked at 28 MHz on the raw received samples.
    const bool en  = (nr_a2_ctl_ & 0xC0) != 0;   // pi_i2s_en = enL or enR
    const bool ear = (nr_a2_ctl_ & 0x01) != 0;   // pi_i2s_ear
    if (!en || !ear) { fe_ear_ = false; return; }
    const unsigned t = ((left_ >> 8) | (right_ >> 8)) & 0x3;   // pi_fe_threshold
    if (fe_ear_ && t == 0x0)      fe_ear_ = false;
    else if (!fe_ear_ && t == 0x3) fe_ear_ = true;
}

void I2s::set_sample(uint16_t left_10bit, uint16_t right_10bit)
{
    // Clamp inputs to 10 bits to match `std_logic_vector(9 downto 0)`
    // in audio_mixer.vhd.
    left_  = static_cast<uint16_t>(left_10bit  & 0x3FF);
    right_ = static_cast<uint16_t>(right_10bit & 0x3FF);
    update_fe_ear();
}

uint16_t I2s::pi_audio_L() const
{
    // VHDL zxnext.vhd:2358:
    //   pi_audio_L <= ("10" & X"00")
    //                  when (pi_i2s_en='0' or pi_i2s_muteL='1'
    //                        or pi_i2s_ear='1')
    //                  else pi_i2s_audio_L when pi_i2s_enL='1'
    //                  else pi_i2s_audio_R;
    // ("10" & X"00") is 10-bit 0x200 — the unsigned DC midpoint, i.e.
    // I2S "silence". Cross-channel mux: when only the OTHER channel is
    // enabled, that channel's sample is replicated here.
    constexpr uint16_t SILENCE = 0x200;
    const bool enL   = (nr_a2_ctl_ & 0x80) != 0;   // b7
    const bool enR   = (nr_a2_ctl_ & 0x40) != 0;   // b6
    const bool en    = enL || enR;                 // pi_i2s_en
    const bool muteL = (nr_a2_ctl_ & 0x08) != 0;   // b3
    const bool ear   = (nr_a2_ctl_ & 0x01) != 0;   // b0
    if (!en || muteL || ear) return SILENCE;
    return enL ? left_ : right_;
}

uint16_t I2s::pi_audio_R() const
{
    // VHDL zxnext.vhd:2359 — mirror of pi_audio_L with R/L swapped.
    constexpr uint16_t SILENCE = 0x200;
    const bool enL   = (nr_a2_ctl_ & 0x80) != 0;   // b7
    const bool enR   = (nr_a2_ctl_ & 0x40) != 0;   // b6
    const bool en    = enL || enR;
    const bool muteR = (nr_a2_ctl_ & 0x04) != 0;   // b2
    const bool ear   = (nr_a2_ctl_ & 0x01) != 0;   // b0
    if (!en || muteR || ear) return SILENCE;
    return enR ? right_ : left_;
}

// GH #27 S5 — the ONE field list (design §9.2). Declaration order IS the
// binary stream order, so it must not be disturbed: the byte-identity gate
// (§17.1) pins these 4 bytes as the `i2s` block of the 2 292 965-byte stream.
//
// `nr_a2_ctl_` is deliberately NOT declared here (G113). `Emulator::save_state`
// appends it at the very END of the snapshot instead, because putting it in
// this block would grow a slot that older snapshots have at exactly four
// bytes and byte-shift every following block. That is the same pattern G112
// uses for `nr_2d_i2s_sample_`, and it makes the field one of §9.5's
// hand-written exceptions — an Emulator scalar rather than an I2s one.
void I2s::describe_state(jnext::save::StateDesc& d)
{
    d.u16("left", left_);
    d.u16("right", right_);
}

void I2s::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void I2s::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
    // pi_fe_ear is not in the stream: a restore starts the comparator from 0
    // (the Emulator puts the input back at rest when a Pi is attached, and the
    // hysteresis re-forms on the next sample that reaches a threshold).
    fe_ear_ = false;
}
