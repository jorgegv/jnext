#include "audio/beeper.h"
#include "core/saveable.h"

Beeper::Beeper() { reset(); }

void Beeper::reset()
{
    ear_ = false;
    mic_ = false;
    tape_ear_ = false;
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
