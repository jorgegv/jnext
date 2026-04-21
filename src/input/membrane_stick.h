#pragma once
#include <cstdint>
#include "input/joystick.h"

/// Sinclair 1 / Sinclair 2 / Cursor / User-defined joystick-to-keyboard
/// adapter.
///
/// Mirrors VHDL `input/membrane/membrane_stick.vhd:117-198` — this module
/// sits BETWEEN the physical membrane keyboard and the CPU's port 0xFE
/// row read, OR-ing joystick direction / fire events into the matrix
/// according to the current NR 0x05 mode:
///
///   Sinclair 2 → keys 0, 9, 8, 7, 6 on row 4
///   Sinclair 1 → keys 1, 2, 3, 4, 5 on row 3
///   Cursor     → keys 5, 6, 7, 8, 0 (cursor via Caps-Shift in BASIC)
///   User-def   → programmable keymap (VHDL default table lives in
///                `ram/init/keyjoy_64_6.coe:1-66`).
///
/// Per critic Issue #7 of the Input SKIP-reduction plan: this class
/// is SEPARATE from Keyboard (do NOT add a joy-row inject method on
/// Keyboard; the VHDL decomposition puts the joystick→key adapter in
/// its own module upstream of the matrix read).
///
/// Phase 1 scaffold: every new method is a stub. Agent C (Phase 2,
/// Wave 2) fills in the real OR-into-row composition.
class MembraneStick {
public:
    MembraneStick() { reset(); }

    /// VHDL reset: both connectors' stored state cleared, modes held
    /// at Joystick's reset defaults (K1 left / S2 right).
    void reset();

    /// Mirror the current NR 0x05 mode for connector 0 (left) or 1
    /// (right). Called by Joystick::set_nr_05() / set_mode_direct().
    /// Phase 1 stub just stores; Agent C wires the real per-mode map.
    void set_mode(int connector, Joystick::Mode m);

    /// Install the 12-bit joystick state for one connector. Layout
    /// matches Joystick (see Joystick.h class comment).
    void inject_joystick_state(int connector, uint16_t bits12);

    /// Compose into a 5-bit (active-low) keyboard row — Phase 1 stub
    /// passes the matrix byte through unchanged. Agent C implements
    /// the real OR-mask per Sinclair1/Sinclair2/Cursor/UserDef tables
    /// from membrane_stick.vhd:117-198 and keyjoy_64_6.coe.
    uint8_t compose_into_row(int row, uint8_t matrix_5bit) const;

private:
    Joystick::Mode mode_left_  = Joystick::Mode::Kempston1;
    Joystick::Mode mode_right_ = Joystick::Mode::Sinclair2;
    uint16_t state_left_  = 0;
    uint16_t state_right_ = 0;
};
