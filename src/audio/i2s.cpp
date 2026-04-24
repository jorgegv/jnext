#include "audio/i2s.h"
#include "core/saveable.h"

I2s::I2s() { reset(); }

void I2s::reset()
{
    // VHDL: signals default to all-zero (silence) at reset.
    left_  = 0;
    right_ = 0;
}

void I2s::set_sample(uint16_t left_10bit, uint16_t right_10bit)
{
    // Clamp inputs to 10 bits to match `std_logic_vector(9 downto 0)`
    // in audio_mixer.vhd.
    left_  = static_cast<uint16_t>(left_10bit  & 0x3FF);
    right_ = static_cast<uint16_t>(right_10bit & 0x3FF);
}

void I2s::save_state(StateWriter& w) const
{
    w.write_u16(left_);
    w.write_u16(right_);
}

void I2s::load_state(StateReader& r)
{
    left_  = r.read_u16();
    right_ = r.read_u16();
}
