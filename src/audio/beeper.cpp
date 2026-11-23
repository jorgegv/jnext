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
    mic_final_ = false;
}

// GH #27 S5 — the ONE field list (design §9.2). Declaration order IS the
// binary stream order, so it must not be disturbed: the byte-identity gate
// (§17.1) pins these 3 bytes as block 19 of the 2 292 965-byte stream.
//
// `ear_` and `mic_` are port 0xFE bits 4 and 3; `tape_ear_` is the EAR line
// driven by the tape engine. The mixer has no tape input: the tape reaches the
// mix only through `mic_final_` (`beep_mic_final`), which is DERIVED and so
// stays out of this declaration (the byte-identity gate pins these 3 bytes).
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
    // The no-tape value (zxnext.vhd:6503); Emulator::load_state refreshes it
    // with the full formula.
    mic_final_ = mic_;
}
