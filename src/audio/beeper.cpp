#include "audio/beeper.h"

Beeper::Beeper() { reset(); }

void Beeper::reset()
{
    ear_ = false;
    mic_ = false;
    tape_ear_ = false;
}

uint16_t Beeper::current_level() const
{
    // VHDL audio_mixer.vhd scaling:
    //   EAR high → 0x100 (256 units in 13-bit PCM space)
    //   MIC high → 0x020 (32 units)
    //   Tape EAR input → same level as EAR output (AC-coupled to speaker)
    uint16_t level = 0;
    if (ear_) level += 0x100;
    if (mic_) level += 0x020;
    if (tape_ear_) level += 0x100;
    return level;
}
