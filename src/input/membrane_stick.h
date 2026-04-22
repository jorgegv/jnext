#pragma once
#include <cstdint>
#include "input/joystick.h"

/// Sinclair 1 / Sinclair 2 / Cursor / User-defined joystick-to-keyboard
/// adapter.
///
/// Mirrors VHDL `input/membrane/membrane_stick.vhd:117-198` — this module
/// sits BETWEEN the physical membrane keyboard and the CPU's port 0xFE
/// row read, AND-ing joystick direction / fire events into the matrix
/// according to the current NR 0x05 mode. The default per-mode keymap
/// table is initialised from `ram/init/keyjoy_64_6.coe:1-66`.
///
/// Wave 2 (Agent C) implements the three default modes (Sinclair 1,
/// Sinclair 2, Cursor) per the COE oracle. The User-defined mode (NR 0x05
/// = "111") and runtime keymap reprogramming via NR 0x40/0x41 remain
/// out-of-scope for this wave.
///
/// Per critic Issue #7 of the Input SKIP-reduction plan: this class
/// is SEPARATE from Keyboard (do NOT add a joy-row inject method on
/// Keyboard; the VHDL decomposition puts the joystick→key adapter in
/// its own module upstream of the matrix read).
///
/// Wiring strategy (Wave 2 decision): the test harness calls
/// `compose_into_row()` DIRECTLY, with no Keyboard/Emulator integration
/// in this commit. The runtime path that AND-merges the membrane_stick
/// output with the membrane scan (per VHDL `keyb_col <= keyb_col_i_q
/// AND membrane_stick_col AND ps2_kbd_col` at zxnext_top_issue4.vhd:1843)
/// can be wired in a follow-up wave when joystick input actually feeds
/// keyboard rows at runtime. This keeps the Wave-2 diff minimal and
/// avoids forcing Keyboard / Emulator changes into this branch.
///
/// COE-vs-plan oracle note (Wave 2 finding): the test plan rows
/// SINC1-* and SINC2-* in INPUT-TEST-PLAN-DESIGN.md §3.7 had the
/// Sinclair 1 ↔ Sinclair 2 keymap labels SWAPPED relative to the
/// canonical COE data and FUSE's reference adapter
/// (peripherals/joystick.c sinclair1_key/sinclair2_key). The COE wins
/// per the brief; the test rows are flipped to the COE-derived expected
/// values in test/input/input_test.cpp.
class MembraneStick {
public:
    MembraneStick() { reset(); }

    /// VHDL reset: both connectors' stored state cleared, modes held
    /// at Joystick's reset defaults (K1 left / S2 right).
    void reset();

    /// Mirror the current NR 0x05 mode for connector 0 (left) or 1
    /// (right). Called by Joystick::set_nr_05() / set_mode_direct().
    /// Stores the per-connector mode used by compose_into_row() to
    /// pick the right COE-derived keymap row.
    void set_mode(int connector, Joystick::Mode m);

    /// Install the 12-bit joystick state for one connector. Layout
    /// matches Joystick (see Joystick.h class comment): bit 0 = R,
    /// bit 1 = L, bit 2 = D, bit 3 = U, bit 4 = B (FIRE), bits 5..11
    /// MD6/Kempston extras (ignored by SINC/CURS modes).
    void inject_joystick_state(int connector, uint16_t bits12);

    /// Compose into a 5-bit (active-low) keyboard row. For each
    /// connector whose mode is one of {Sinclair1, Sinclair2, Cursor},
    /// looks up the (row, col) for each pressed direction in the
    /// COE-derived keymap; if the direction's row matches the requested
    /// `row`, the col bit is cleared in the returned mask. All other
    /// modes (Kempston1/2, MD3 left/right, IoMode) leave the mask
    /// untouched — they go through ports 0x1F/0x37 not the membrane.
    /// User-defined mode (mode value 7 in this enum encoding) is
    /// out-of-scope for Wave 2 and is currently treated as a no-op
    /// pass-through.
    uint8_t compose_into_row(int row, uint8_t matrix_5bit) const;

private:
    Joystick::Mode mode_left_  = Joystick::Mode::Kempston1;
    Joystick::Mode mode_right_ = Joystick::Mode::Sinclair2;
    uint16_t state_left_  = 0;
    uint16_t state_right_ = 0;
};
