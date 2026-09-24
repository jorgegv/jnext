#include "audio/beeper.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

Beeper::Beeper() { reset(); }

void Beeper::reset()
{
    ear_ = false;
    mic_ = false;
    tape_ear_ = false;
}

// GH #27 S5 — the ONE field list (design §9.2). Declaration order IS the
// binary stream order, so it must not be disturbed: the byte-identity gate
// (§17.1) pins these 3 bytes as block 19 of the 2 292 965-byte stream.
//
// `ear_` and `mic_` are port 0xFE bits 4 and 3; `tape_ear_` is the EAR line
// driven by the tape engine, which the mixer sums in alongside them.
void Beeper::describe_state(jnext::save::StateDesc& d)
{
    d.boolean("ear", ear_);
    d.boolean("mic", mic_);
    d.boolean("tape_ear", tape_ear_);
}

void Beeper::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Beeper::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
}
