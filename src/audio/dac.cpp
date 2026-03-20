#include "audio/dac.h"

Dac::Dac() { reset(); }

void Dac::reset()
{
    // VHDL: reset to 0x80 (midpoint for unsigned 8-bit audio = silence)
    ch_[0] = 0x80;
    ch_[1] = 0x80;
    ch_[2] = 0x80;
    ch_[3] = 0x80;
}

void Dac::write_channel(int ch, uint8_t val)
{
    if (ch >= 0 && ch < 4) ch_[ch] = val;
}
