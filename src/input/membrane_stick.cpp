#include "input/membrane_stick.h"

// =============================================================================
// Phase 2 Wave 2 (Agent C) — Sinclair 1 / Sinclair 2 / Cursor joystick→key
// adapter. The default keymap is COE-derived per the oracle table at
// ram/init/keyjoy_64_6.coe:1-66, addressed by the joy_addr_start /
// joy_bit_count_start state machine in membrane_stick.vhd:117-198.
//
// Key direction encoding within the 12-bit joystick state vector
// (membrane_stick.vhd:37, zxnext.vhd:3441-3442):
//   bit 0 = R, bit 1 = L, bit 2 = D, bit 3 = U, bit 4 = B (FIRE)
//   bits 5..11 = MD6/Kempston extras (ignored by SINC/CURS modes).
//
// COE entries 0..14 (each 6-bit packed: bits 5..3 = membrane row,
// bits 2..0 = membrane col), processed in (R, L, D, U, F) order
// because joy_bit_count_start = 0 and bit_count increments lockstep
// with sram_addr (membrane_stick.vhd:155-167):
//
//   Sinclair 1 (joy_type "011", joy_addr_start = 0):
//     addr 0 : 100011 = (row 4, col 3) = key 7  ← R direction
//     addr 1 : 100100 = (row 4, col 4) = key 6  ← L
//     addr 2 : 100010 = (row 4, col 2) = key 8  ← D
//     addr 3 : 100001 = (row 4, col 1) = key 9  ← U
//     addr 4 : 100000 = (row 4, col 0) = key 0  ← FIRE
//
//   Sinclair 2 (joy_type "000", joy_addr_start = 5):
//     addr 5 : 011001 = (row 3, col 1) = key 2  ← R
//     addr 6 : 011000 = (row 3, col 0) = key 1  ← L
//     addr 7 : 011010 = (row 3, col 2) = key 3  ← D
//     addr 8 : 011011 = (row 3, col 3) = key 4  ← U
//     addr 9 : 011100 = (row 3, col 4) = key 5  ← FIRE
//
//   Cursor (joy_type "010", joy_addr_start = 10):
//     addr 10: 100010 = (row 4, col 2) = key 8  ← R
//     addr 11: 011100 = (row 3, col 4) = key 5  ← L
//     addr 12: 100100 = (row 4, col 4) = key 6  ← D
//     addr 13: 100011 = (row 4, col 3) = key 7  ← U
//     addr 14: 100000 = (row 4, col 0) = key 0  ← FIRE
//
// Cross-validated against FUSE's reference adapter
// (peripherals/joystick.c) — sinclair1_key[5] = {6, 7, 9, 8, 0} (LEFT,
// RIGHT, UP, DOWN, FIRE order); sinclair2_key[5] = {1, 2, 4, 3, 5};
// cursor_key[5] = {5, 8, 7, 6, 0}. All three match the COE 1:1 once
// the bit_count→direction order is normalised.
//
// CAVEAT: the test plan rows SINC1-* and SINC2-* in
// INPUT-TEST-PLAN-DESIGN.md §3.7 had Sinclair 1 / Sinclair 2 SWAPPED
// (followed the zxnext.vhd:3434 comment "011 = Sinclair 1 (12345)"
// rather than the COE data + FUSE convention). This adapter follows
// the COE; the test rows have been flipped accordingly to the
// COE-derived expected values.
//
// User-defined mode (joy_type "111", default keymap at addr 16..27)
// and runtime user-keymap reprogramming via NR 0x40/0x41 are
// out-of-scope for Wave 2 and treated as no-op pass-through.
// =============================================================================

namespace {

// Direction indices into our compact 5-direction keymap. These match
// the bit_count order driven by joy_bit_count_start = 0 in the VHDL
// state machine (membrane_stick.vhd:155-167) → R, L, D, U, FIRE.
enum Dir : int { DIR_R = 0, DIR_L = 1, DIR_D = 2, DIR_U = 3, DIR_F = 4, NUM_DIRS = 5 };

struct KeyCell { int row; int col; };

// COE-derived keymap: indexed [mode][direction] → (row, col).
// Mode index uses the Joystick::Mode enum value cast to int; only the
// three modes that drive the membrane (Sinclair1, Sinclair2, Cursor)
// have meaningful entries. Other modes have row = -1 sentinel meaning
// "do not fold into membrane" (passthrough).
//
// Layout per COE (see file-level comment block above for the bit-
// sliced derivation):
//
//   Joystick::Mode::Sinclair2 = 0  → COE addrs 5..9
//   Joystick::Mode::Kempston1 = 1  → port 0x1F path, no membrane fold
//   Joystick::Mode::Cursor    = 2  → COE addrs 10..14
//   Joystick::Mode::Sinclair1 = 3  → COE addrs 0..4
//   Joystick::Mode::Kempston2 = 4  → port 0x37 path, no membrane fold
//   Joystick::Mode::Md3Left   = 5  → port 0x1F + bits 7:6, no fold
//   Joystick::Mode::Md3Right  = 6  → port 0x37 + bits 7:6, no fold
//   Joystick::Mode::IoMode    = 7  → I/O mode, no fold

constexpr int NUM_MODES = 8;
constexpr KeyCell s_keymap[NUM_MODES][NUM_DIRS] = {
    // Sinclair2 (mode 000) — COE addrs 5..9, R L D U F order
    {{3,1}, {3,0}, {3,2}, {3,3}, {3,4}},
    // Kempston1 (mode 001) — port 0x1F, no membrane fold
    {{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1}},
    // Cursor (mode 010) — COE addrs 10..14
    {{4,2}, {3,4}, {4,4}, {4,3}, {4,0}},
    // Sinclair1 (mode 011) — COE addrs 0..4
    {{4,3}, {4,4}, {4,2}, {4,1}, {4,0}},
    // Kempston2 (mode 100) — port 0x37, no fold
    {{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1}},
    // Md3Left (mode 101) — port 0x1F path, no fold
    {{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1}},
    // Md3Right (mode 110) — port 0x37 path, no fold
    {{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1}},
    // IoMode (mode 111) — both pins free for I/O, no fold (and the
    // VHDL "111" actually maps to the user-defined keymap region in
    // the membrane_stick state machine; Wave 2 treats it as no-op.)
    {{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1}},
};

// Apply one connector's joystick state to the row mask. mode is the
// connector's NR 0x05 mode; state12 is the 12-bit raw joystick state
// (active-high per zxnext.vhd:3441-3442 — bit set means pressed).
inline uint8_t apply_one(int row, uint8_t mask, Joystick::Mode mode, uint16_t state12)
{
    const int mi = static_cast<int>(mode);
    if (mi < 0 || mi >= NUM_MODES) return mask;
    const KeyCell* km = s_keymap[mi];
    // Iterate the five directions; for each that is currently pressed
    // and whose target keymap row matches the requested membrane row,
    // clear the corresponding column bit.
    for (int d = 0; d < NUM_DIRS; ++d) {
        if (km[d].row != row) continue;
        if (state12 & (1u << d)) {
            mask = static_cast<uint8_t>(mask & ~(1u << km[d].col));
        }
    }
    return mask;
}

} // anonymous namespace

void MembraneStick::reset()
{
    mode_left_   = Joystick::Mode::Kempston1;
    mode_right_  = Joystick::Mode::Sinclair2;
    state_left_  = 0;
    state_right_ = 0;
}

void MembraneStick::set_mode(int connector, Joystick::Mode m)
{
    if (connector == 0)      mode_left_  = m;
    else if (connector == 1) mode_right_ = m;
}

void MembraneStick::inject_joystick_state(int connector, uint16_t bits12)
{
    if (connector == 0)      state_left_  = bits12 & 0x0FFF;
    else if (connector == 1) state_right_ = bits12 & 0x0FFF;
}

uint8_t MembraneStick::compose_into_row(int row, uint8_t matrix_5bit) const
{
    // VHDL `keyb_col <= keyb_col_i_q AND membrane_stick_col AND
    // ps2_kbd_col` (zxnext_top_issue4.vhd:1843) — purely active-low
    // AND-merge. Both connectors contribute independently; if either
    // one drives a column bit low, the resulting bit is low.
    uint8_t r = matrix_5bit;
    r = apply_one(row, r, mode_left_,  state_left_);
    r = apply_one(row, r, mode_right_, state_right_);
    return static_cast<uint8_t>(r & 0x1F);
}
