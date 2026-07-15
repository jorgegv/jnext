#include "input/membrane_stick.h"
#include "core/saveable.h"

#include <cstddef>

// =============================================================================
// Phase 2 Wave 2 (Agent C) — Sinclair 1 / Sinclair 2 / Cursor joystick→key
// adapter. The default keymap is COE-derived per the oracle table at
// ram/init/keyjoy_64_6.coe:1-66, addressed by the joy_addr_start /
// joy_bit_count_start state machine in membrane_stick.vhd:117-198.
//
// Tier 2 Wave 1 (Agent C, G127) — closes JCAL-01..JCAL-03:
//   * 64-cell × 6-bit SDP-RAM analogue, initialised from
//     keyjoy_64_6.coe:1-66 (translation table k_default_keymap[] below).
//   * NR 0x28 / 0x29 / 0x2B write-side process per zxnext.vhd:6294-6324.
//     NR 0x2A is reserved/dead in VHDL (lines 6312-6319 commented out)
//     and intentionally has no handler.
//   * User-Defined fold for joy_type "111" — iterates all 12 joystick
//     bits per joy_addr_start=10000 / joy_bit_count_end=1011, looking
//     up SDP-RAM cells 16-27 (left) or 48-59 (right) and folding the
//     decoded (row, col) into the membrane.
//
// Key direction encoding within the 12-bit joystick state vector
// (membrane_stick.vhd:37, zxnext.vhd:3441-3442):
//   bit 0 = R, bit 1 = L, bit 2 = D, bit 3 = U, bit 4 = B (FIRE)
//   bits 5..11 = MD6/Kempston extras. The pre-programmed SINC/CURS
//                modes only walk bits 0..4 (joy_bit_count_end=0100);
//                User-Defined mode walks bits 0..11 (joy_bit_count_end=1011).
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
// =============================================================================

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// COE oracle table — translated 1:1 from
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/ram/init/
//     keyjoy_64_6.coe
// (memory_initialization_radix=2, 64 entries × 6 bits each).
//
// Layout per cell index:
//     0..14  : LEFT pre-programmed SINC1, SINC2, CURSOR keymaps (5 entries each)
//        15  : LEFT sentinel (111111 — out-of-band row/col, no membrane fold)
//    16..31  : LEFT user-defined area (default 000111 — row 0 col 7, no-op
//                because col 7 is outside the 5-bit row mask)
//    32..46  : RIGHT pre-programmed SINC1, SINC2, CURSOR keymaps
//        47  : RIGHT sentinel
//    48..63  : RIGHT user-defined area (default 000111 — no-op)
//
// The membrane_stick state machine (membrane_stick.vhd:117-198) reads
// these cells via address `joy_sel & sram_addr`, where joy_sel selects
// left (0) vs right (1) and sram_addr (5 bits) is driven by joy_addr_start
// + bit_count for the active joy_type.
// ─────────────────────────────────────────────────────────────────────────────
constexpr uint8_t k_default_keymap[64] = {
    // LEFT — pre-programmed (Sinclair 1, Sinclair 2, Cursor)
    /* 00 */ 0b100011, /* 01 */ 0b100100, /* 02 */ 0b100010, /* 03 */ 0b100001, /* 04 */ 0b100000,
    /* 05 */ 0b011001, /* 06 */ 0b011000, /* 07 */ 0b011010, /* 08 */ 0b011011, /* 09 */ 0b011100,
    /* 10 */ 0b100010, /* 11 */ 0b011100, /* 12 */ 0b100100, /* 13 */ 0b100011, /* 14 */ 0b100000,
    /* 15 */ 0b111111,
    // LEFT — user-defined area (default: row 0 col 7 = no-op since col 7 ∉ [0..4])
    /* 16..31 */ 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111,
                 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111,
    // RIGHT — pre-programmed
    /* 32 */ 0b100011, /* 33 */ 0b100100, /* 34 */ 0b100010, /* 35 */ 0b100001, /* 36 */ 0b100000,
    /* 37 */ 0b011001, /* 38 */ 0b011000, /* 39 */ 0b011010, /* 40 */ 0b011011, /* 41 */ 0b011100,
    /* 42 */ 0b100010, /* 43 */ 0b011100, /* 44 */ 0b100100, /* 45 */ 0b100011, /* 46 */ 0b100000,
    /* 47 */ 0b111111,
    // RIGHT — user-defined area
    /* 48..63 */ 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111,
                 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111, 0b000111,
};
static_assert(sizeof(k_default_keymap) == MembraneStick::NUM_KEYMAP_CELLS,
              "k_default_keymap must have exactly 64 entries to match keyjoy_sdpram_64_6");

// Direction indices into our compact 5-direction keymap. These match
// the bit_count order driven by joy_bit_count_start = 0 in the VHDL
// state machine (membrane_stick.vhd:155-167) → R, L, D, U, FIRE.
enum Dir : int { DIR_R = 0, DIR_L = 1, DIR_D = 2, DIR_U = 3, DIR_F = 4, NUM_DIRS = 5 };

// Bit-sliced helper: extract (row, col) from a 6-bit SDP-RAM cell.
// row = bits[5:3], col = bits[2:0]. Returns (-1, -1) sentinel for the
// FPGA's "no fold" cells (any cell whose decoded col falls outside the
// 5-bit row mask 0..4 — which covers both 111111 sentinels and the
// 000111 user-defined defaults).
struct KeyCell { int row; int col; };
inline KeyCell decode_cell(uint8_t cell6) {
    const int row = (cell6 >> 3) & 0x07;
    const int col = cell6 & 0x07;
    return {row, col};
}

// Pre-programmed mode → COE addr_start lookup for fast lookup in the
// hot compose path. The "111" (User-Defined / IoMode in our enum)
// case is handled separately below because it walks 12 bits, not 5.
//
// Layout (Joystick::Mode enum value cast to int):
//   Sinclair2 = 0  → addr_start 5
//   Kempston1 = 1  → no fold
//   Cursor    = 2  → addr_start 10
//   Sinclair1 = 3  → addr_start 0
//   Kempston2 = 4  → no fold
//   Md3Left   = 5  → no fold
//   Md3Right  = 6  → no fold
//   IoMode    = 7  → user-defined (handled separately, addr_start 16)
constexpr int k_addr_start_left[8] = {
    /* Sinclair2 */ 5, /* Kempston1 */ -1, /* Cursor */ 10, /* Sinclair1 */ 0,
    /* Kempston2 */ -1, /* Md3Left  */ -1, /* Md3Right */ -1, /* IoMode    */ 16,
};

// For the right connector, the COE replicates the LEFT layout starting
// at cell 32, so add 32 to every left start. Handled inline below.

// Walk one connector's keymap entries against the joystick state and
// fold matching (row, col) cells into the row mask. `cell_base` is the
// SDP-RAM cell index for the FIRST entry of this mode + connector
// (e.g., left Sinclair1 = 0, right Sinclair1 = 32, left UDK = 16,
// right UDK = 48). `n_bits` is the number of joystick bits to walk
// (5 for SINC/CURS, 12 for User-Defined).
inline uint8_t apply_keymap_walk(int row, uint8_t mask, uint16_t state12,
                                 const uint8_t* keymap, int cell_base, int n_bits) {
    for (int b = 0; b < n_bits; ++b) {
        if (!(state12 & (1u << b))) continue;
        const KeyCell kc = decode_cell(keymap[cell_base + b]);
        // Out-of-range col (e.g. the 111111 sentinel at cells 15/47
        // or the 000111 default UDK no-op) leaves the mask untouched.
        if (kc.col < 0 || kc.col > 4) continue;
        if (kc.row != row) continue;
        mask = static_cast<uint8_t>(mask & ~(1u << kc.col));
    }
    return mask;
}

// Apply one connector's joystick state to the row mask. mode is the
// connector's NR 0x05 mode; state12 is the 12-bit raw joystick state
// (active-high per zxnext.vhd:3441-3442 — bit set means pressed).
// `is_right` selects which half of the 64-cell SDP-RAM to address
// (left=0..31, right=32..63).
inline uint8_t apply_one(int row, uint8_t mask, Joystick::Mode mode,
                         uint16_t state12, bool is_right, const uint8_t* keymap)
{
    const int mi = static_cast<int>(mode);
    if (mi < 0 || mi >= 8) return mask;

    // User-Defined ("111") fold — 12-bit walk over UDK cells.
    if (mode == Joystick::Mode::IoMode) {
        const int base = is_right ? 48 : 16;
        return apply_keymap_walk(row, mask, state12, keymap, base, 12);
    }

    // Pre-programmed modes (SINC1, SINC2, CURS) — 5-bit walk.
    const int start_left = k_addr_start_left[mi];
    if (start_left < 0) return mask;     // Kempston / MD3 — no membrane fold.
    const int base = is_right ? (start_left + 32) : start_left;
    return apply_keymap_walk(row, mask, state12, keymap, base, NUM_DIRS);
}

} // anonymous namespace

void MembraneStick::reset()
{
    mode_left_   = Joystick::Mode::Kempston1;
    mode_right_  = Joystick::Mode::Sinclair2;
    state_left_  = 0;
    state_right_ = 0;
    // Reload SDP-RAM with COE defaults. See file-level comment for the
    // FPGA-vs-emulator semantics tradeoff (FPGA loads once at bitstream
    // time and retains across CPU resets; we conservatively reload on
    // every reset() to mirror a fresh power-on).
    for (int i = 0; i < NUM_KEYMAP_CELLS; ++i) {
        keymap_[static_cast<size_t>(i)] = k_default_keymap[i];
    }
    keymap_sel_  = 0;
    keymap_addr_ = 0;
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
    r = apply_one(row, r, mode_left_,  state_left_,  false, keymap_.data());
    r = apply_one(row, r, mode_right_, state_right_, true,  keymap_.data());
    return static_cast<uint8_t>(r & 0x1F);
}

// ─────────────────────────────────────────────────────────────────────────────
// NR 0x28 / 0x29 / 0x2B keymap programming — VHDL zxnext.vhd:6294-6324.
//
// Process semantics (the VHDL is a single clocked process selecting one
// branch per write-enable; only one of nr_28_we / nr_29_we / nr_2b_we
// fires per CPU NR write since all three target distinct registers, so
// no precedence concerns):
//
//   if nr_28_we then            -- NR 0x28 written
//     nr_keymap_sel    <= nr_wr_dat(7);
//     nr_keymap_addr(8)<= nr_wr_dat(0);
//   elsif nr_29_we then         -- NR 0x29 written
//     nr_keymap_addr(7 downto 0) <= nr_wr_dat;
//   elsif nr_2b_we then          -- NR 0x2B written
//     nr_keymap_addr <= nr_keymap_addr + 1;   -- always increments
//   end if;
//
//   nr_keymap_we <= nr_2b_we and not nr_keymap_sel;   -- PS/2 keymap RAM
//   nr_joymap_we <= nr_2b_we and     nr_keymap_sel;   -- joymap SDP-RAM
//
// The actual joymap SDP-RAM cell selected for writes is
//   A(5..0) = nr_keymap_addr(4) & '1' & nr_keymap_addr(3..0)
// (membrane_stick.vhd:181). Bit 4 is HARD-WIRED to '1', so writes never
// reach the pre-programmed area (cells 0..15 / 32..47); they always
// land in the UDK area (cells 16..31 / 48..63). This is by FPGA design
// — the pre-programmed defaults aren't supposed to be runtime-mutable.
// ─────────────────────────────────────────────────────────────────────────────

void MembraneStick::write_nr_28(uint8_t v)
{
    // VHDL zxnext.vhd:6301-6303
    keymap_sel_ = static_cast<uint8_t>((v >> 7) & 0x01);
    if (v & 0x01) {
        keymap_addr_ = static_cast<uint16_t>(keymap_addr_ | 0x0100u);
    } else {
        keymap_addr_ = static_cast<uint16_t>(keymap_addr_ & 0x00FFu);
    }
}

void MembraneStick::write_nr_29(uint8_t v)
{
    // VHDL zxnext.vhd:6304-6305 — replace bits 7..0 of the 9-bit addr.
    keymap_addr_ = static_cast<uint16_t>((keymap_addr_ & 0x0100u) | static_cast<uint16_t>(v));
}

void MembraneStick::write_nr_2b(uint8_t v)
{
    // VHDL zxnext.vhd:6306-6308 + 6321-6324.
    //
    // 1. If keymap_sel = '1', commit the data byte into the SDP-RAM at
    //    the addr derived from nr_keymap_addr(4..0). Cell address bit 4
    //    is forced to '1' per membrane_stick.vhd:181.
    if (keymap_sel_) {
        const uint8_t cell_addr = static_cast<uint8_t>(
            ((keymap_addr_ & 0x10u) << 1) |    // i_keymap_addr(4) → A(5)
            0x10u                          |    // forced '1'       → A(4)
            (keymap_addr_ & 0x0Fu));            // i_keymap_addr(3..0) → A(3..0)
        // Data is 6 bits per VHDL nr_keymap_dat <= nr_wr_dat (zxnext.vhd:6324)
        // wired to membrane_stick port `i_keymap_data` (zxnext_top_issue4.vhd:
        // 1867) which truncates `nr_keymap_dat(5 downto 0)`. So we mask the
        // CPU-supplied byte to 6 bits before storing.
        keymap_[cell_addr] = static_cast<uint8_t>(v & 0x3Fu);
    }
    // 2. Auto-increment the 9-bit addr (always, regardless of keymap_sel).
    keymap_addr_ = static_cast<uint16_t>((keymap_addr_ + 1u) & 0x01FFu);
}

// =============================================================================
// Task 60c — state serialisation. The SDP-RAM keymap is runtime-
// reprogrammable via NR 0x28/0x29/0x2B, so it (and the addr/select latches)
// is genuine emulated state that must round-trip across rewind / save-load.
// =============================================================================

void MembraneStick::save_state(StateWriter& w) const
{
    w.write_u8(static_cast<uint8_t>(mode_left_));
    w.write_u8(static_cast<uint8_t>(mode_right_));
    w.write_u16(state_left_);
    w.write_u16(state_right_);
    w.write_bytes(keymap_.data(), keymap_.size());
    w.write_u8(keymap_sel_);
    w.write_u16(keymap_addr_);
}

void MembraneStick::load_state(StateReader& r)
{
    mode_left_   = static_cast<Joystick::Mode>(r.read_u8());
    mode_right_  = static_cast<Joystick::Mode>(r.read_u8());
    state_left_  = r.read_u16();
    state_right_ = r.read_u16();
    r.read_bytes(keymap_.data(), keymap_.size());
    keymap_sel_  = r.read_u8();
    keymap_addr_ = static_cast<uint16_t>(r.read_u16() & 0x01FFu);
}
