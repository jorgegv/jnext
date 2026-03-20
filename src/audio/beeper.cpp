#include "audio/beeper.h"

Beeper::Beeper() { reset(); }

void Beeper::reset()
{
    ear_ = false;
    mic_ = false;
}

uint16_t Beeper::current_level() const
{
    // VHDL audio_mixer.vhd scaling:
    //   EAR high → 0x100 (256 units in 13-bit PCM space)
    //   MIC high → 0x020 (32 units)
    uint16_t level = 0;
    if (ear_) level += 0x100;
    if (mic_) level += 0x020;
    return level;
}
