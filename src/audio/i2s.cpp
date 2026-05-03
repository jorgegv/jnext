#include "audio/i2s.h"
#include "core/saveable.h"

I2s::I2s() { reset(); }

void I2s::reset()
{
    // VHDL: signals default to all-zero (silence) at reset.
    left_       = 0;
    right_      = 0;
    // VHDL zxnext.vhd:5564 — nr_a2_pi_i2s_ctl resets to 0x00 (no Pi I2S
    // path enabled). With NR 0xA2 = 0, pi_audio_L/R are forced to the
    // 10-bit DC midpoint (0x200) per zxnext.vhd:2358-2359.
    nr_a2_ctl_  = 0;
}

void I2s::set_sample(uint16_t left_10bit, uint16_t right_10bit)
{
    // Clamp inputs to 10 bits to match `std_logic_vector(9 downto 0)`
    // in audio_mixer.vhd.
    left_  = static_cast<uint16_t>(left_10bit  & 0x3FF);
    right_ = static_cast<uint16_t>(right_10bit & 0x3FF);
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

void I2s::save_state(StateWriter& w) const
{
    w.write_u16(left_);
    w.write_u16(right_);
    // NB: nr_a2_ctl_ is intentionally NOT serialised here. Older
    // emulator save snapshots predate G113 and append further subsystem
    // bytes immediately after the I2s slot (nmi_source, etc.); appending
    // a byte inside this method would shift those bytes and break
    // forwards compatibility. Instead, Emulator::save_state() appends
    // nr_a2_ctl_ at the very end of the snapshot with eof()-tolerant
    // load, mirroring the G112 pattern for nr_2d_i2s_sample_.
}

void I2s::load_state(StateReader& r)
{
    left_  = r.read_u16();
    right_ = r.read_u16();
    // nr_a2_ctl_ restored by Emulator::load_state() — see save_state().
}
