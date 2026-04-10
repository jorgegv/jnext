#include "audio/beeper.h"
#include "core/saveable.h"

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

void Beeper::save_state(StateWriter& w) const
{
    w.write_bool(ear_);
    w.write_bool(mic_);
    w.write_bool(tape_ear_);
}

void Beeper::load_state(StateReader& r)
{
    ear_      = r.read_bool();
    mic_      = r.read_bool();
    tape_ear_ = r.read_bool();
}
