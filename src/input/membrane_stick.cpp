#include "input/membrane_stick.h"

// =============================================================================
// Phase 1 scaffold — all methods are compile-only stubs. Agent C (Phase 2,
// Wave 2) will fill in the real joystick→key adapter per
// membrane_stick.vhd:117-198 and the default keymap in
// ram/init/keyjoy_64_6.coe:1-66.
// =============================================================================

void MembraneStick::reset()
{
    mode_left_   = Joystick::Mode::Kempston1;
    mode_right_  = Joystick::Mode::Sinclair2;
    state_left_  = 0;
    state_right_ = 0;
}

void MembraneStick::set_mode(int connector, Joystick::Mode m)
{
    // Phase 1 stub: just store. Agent C drives Sinclair1/Sinclair2/Cursor/
    // UserDef lookup tables off this value.
    if (connector == 0)      mode_left_  = m;
    else if (connector == 1) mode_right_ = m;
}

void MembraneStick::inject_joystick_state(int connector, uint16_t bits12)
{
    if (connector == 0)      state_left_  = bits12 & 0x0FFF;
    else if (connector == 1) state_right_ = bits12 & 0x0FFF;
}

uint8_t MembraneStick::compose_into_row(int /*row*/, uint8_t matrix_5bit) const
{
    // Phase 1 stub: passthrough. Agent C implements the row-by-row
    // AND-OR mixing (active-low matrix: joy-pressed AND direction → bit 0).
    return matrix_5bit;
}
