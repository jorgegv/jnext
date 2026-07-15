#pragma once
#include <array>
#include <cstddef>
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
/// Wave 2 (Agent C) implemented the three pre-programmed modes (Sinclair
/// 1, Sinclair 2, Cursor) per the COE oracle. Tier 2 Wave 1 (Agent C)
/// closes G127: the User-Defined keymap (NR 0x05 = "111") + runtime
/// keymap reprogramming via NR 0x28 / 0x29 / 0x2B is now live, with the
/// 64-cell × 6-bit SDP-RAM analogue initialised from
/// `keyjoy_64_6.coe:1-66` and the auto-incrementing addr/data write
/// process per VHDL `zxnext.vhd:6294-6324`.
///
/// Per critic Issue #7 of the Input SKIP-reduction plan: this class
/// is SEPARATE from Keyboard (do NOT add a joy-row inject method on
/// Keyboard; the VHDL decomposition puts the joystick→key adapter in
/// its own module upstream of the matrix read).
///
/// Wiring strategy: the test harness can call `compose_into_row()`
/// directly on a bare `MembraneStick`. At runtime, `Keyboard::read_rows`
/// AND-merges `MembraneStick::compose_into_row()` into each membrane
/// row per VHDL `keyb_col <= keyb_col_i_q AND membrane_stick_col AND
/// ps2_kbd_col` (zxnext_top_issue4.vhd:1843). The NR 0x05 → mode
/// propagation wire (Joystick::set_nr_05 → MembraneStick::set_mode)
/// is owned by Agent B (gap G126); JCAL-* tests bypass that wire by
/// calling MembraneStick::set_mode() directly, in the same style as
/// the SINC-* / CURS-* tests use Joystick::set_mode_direct().
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
    /// SDP-RAM cell count (matches `keyjoy_sdpram_64_6` in the FPGA — 64
    /// entries × 6 bits per cell, mapped from `keyjoy_64_6.coe`).
    static constexpr int NUM_KEYMAP_CELLS = 64;

    MembraneStick() { reset(); }

    /// VHDL reset: both connectors' stored state cleared, modes held
    /// at Joystick's reset defaults (K1 left / S2 right). Reloads the
    /// SDP-RAM with the COE-derived defaults — this matches FPGA
    /// behaviour where the keyjoy SDP-RAM is initialised at bitstream
    /// load and never re-clocked from POR (the cell contents are
    /// retained across CPU resets unless explicitly rewritten via
    /// NR 0x28-0x2B). The C++ surface conservatively resets to defaults
    /// on every `reset()` call to mirror a fresh power-on scenario.
    void reset();

    /// Mirror the current NR 0x05 mode for connector 0 (left) or 1
    /// (right). Called by Joystick::set_nr_05() / set_mode_direct().
    /// Stores the per-connector mode used by compose_into_row() to
    /// pick the right COE-derived keymap row.
    void set_mode(int connector, Joystick::Mode m);

    /// Install the 12-bit joystick state for one connector. Layout
    /// matches Joystick (see Joystick.h class comment): bit 0 = R,
    /// bit 1 = L, bit 2 = D, bit 3 = U, bit 4 = B (FIRE), bits 5..11
    /// MD6/Kempston extras (used by the User-Defined fold; ignored by
    /// SINC/CURS modes which only walk the first 5 bits).
    void inject_joystick_state(int connector, uint16_t bits12);

    /// Compose into a 5-bit (active-low) keyboard row. For each
    /// connector whose mode is one of {Sinclair1, Sinclair2, Cursor,
    /// IoMode/User-Defined}, looks up the (row, col) for each pressed
    /// direction in the COE-derived keymap; if the direction's row
    /// matches the requested `row`, the col bit is cleared in the
    /// returned mask. Other modes (Kempston1/2, MD3 left/right) leave
    /// the mask untouched — they go through ports 0x1F/0x37 not the
    /// membrane.
    uint8_t compose_into_row(int row, uint8_t matrix_5bit) const;

    // ─────────────────────────────────────────────────────────────────────
    // NR 0x28 / 0x29 / 0x2B keymap programming API (G127 / JCAL-*).
    // VHDL: zxnext.vhd:6294-6324 — the (sel, addr, data) write process.
    //
    // The NR-handler-side pattern (registered in src/core/emulator.cpp):
    //
    //   nextreg_.set_write_handler(0x28, [&](uint8_t v) { ms.write_nr_28(v); });
    //   nextreg_.set_write_handler(0x29, [&](uint8_t v) { ms.write_nr_29(v); });
    //   nextreg_.set_write_handler(0x2B, [&](uint8_t v) { ms.write_nr_2b(v); });
    //
    // NR 0x2A is reserved/dead in the VHDL (lines 6312-6319 are commented
    // out); we deliberately do NOT expose a write_nr_2a().
    // ─────────────────────────────────────────────────────────────────────

    /// VHDL zxnext.vhd:6301-6303 — NR 0x28 write:
    ///   nr_keymap_sel    <= nr_wr_dat(7);
    ///   nr_keymap_addr(8)<= nr_wr_dat(0);
    /// keymap_sel=0 selects the PS/2 keymap RAM (out-of-scope in jnext —
    /// the PS/2 keyboard adapter is not implemented). keymap_sel=1
    /// selects the joystick keymap RAM (the SDP-RAM analogue here);
    /// subsequent NR 0x2B writes land in this module's keymap[].
    void write_nr_28(uint8_t v);

    /// VHDL zxnext.vhd:6304-6305 — NR 0x29 write:
    ///   nr_keymap_addr(7 downto 0) <= nr_wr_dat;
    /// Sets the low 8 bits of the 9-bit `nr_keymap_addr`. The high bit
    /// is set separately via NR 0x28.
    void write_nr_29(uint8_t v);

    /// VHDL zxnext.vhd:6306-6308 + 6321-6324 — NR 0x2B write:
    ///   if nr_keymap_sel = '1' then write keymap[addr] <= nr_wr_dat;
    ///   nr_keymap_addr <= nr_keymap_addr + 1;       (always)
    /// The actual SDP-RAM cell selected is
    ///   A(5 downto 0) = nr_keymap_addr(4) & '1' & nr_keymap_addr(3 downto 0)
    /// — i.e. writes always land within the User-Defined Keymap (UDK)
    /// area: cells 16-31 (left, addr(4)=0) or cells 48-63 (right,
    /// addr(4)=1). The auto-increment fires regardless of `keymap_sel`,
    /// matching VHDL line 6306-6308.
    void write_nr_2b(uint8_t v);

    // ─────────────────────────────────────────────────────────────────────
    // Test / debug accessors. Used by JCAL-01..JCAL-03 to verify the
    // SDP-RAM analogue's response to NR 0x28/0x29/0x2B writes without
    // requiring a full Emulator harness.
    // ─────────────────────────────────────────────────────────────────────

    /// Returns the latched NR 0x28 bit-7 (keymap_sel). 0 = PS/2 keymap
    /// path (writes inert here); 1 = joystick keymap path (SDP-RAM).
    uint8_t  keymap_sel()  const { return keymap_sel_; }

    /// Returns the 9-bit `nr_keymap_addr`. Bits 4..0 select the SDP-RAM
    /// cell within the UDK area; bit 4 selects left (=0) vs right (=1).
    /// Bits 8..5 are tracked for parity with VHDL but unused by joymap.
    uint16_t keymap_addr() const { return keymap_addr_; }

    /// Read SDP-RAM cell `idx` (0..63). Each cell is 6 bits packed:
    /// bits[5:3] = membrane row, bits[2:0] = membrane col.
    /// Out-of-range indices return 0.
    uint8_t  keymap_cell(int idx) const {
        if (idx < 0 || idx >= NUM_KEYMAP_CELLS) return 0;
        return keymap_[static_cast<size_t>(idx)];
    }

    // Task 60c — state serialisation. Persists the per-connector modes and
    // stored state, plus the runtime-reprogrammable SDP-RAM keymap and its
    // NR 0x28/0x29/0x2B address/select latches (a program can rewrite the
    // User-Defined keymap at runtime, so it is genuine emulated state).
    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    Joystick::Mode mode_left_  = Joystick::Mode::Kempston1;
    Joystick::Mode mode_right_ = Joystick::Mode::Sinclair2;
    uint16_t state_left_  = 0;
    uint16_t state_right_ = 0;

    // SDP-RAM analogue. Initialised from `keyjoy_64_6.coe` at reset().
    std::array<uint8_t, NUM_KEYMAP_CELLS> keymap_{};

    // NR 0x28 / 0x29 / 0x2B latched state.
    uint8_t  keymap_sel_  = 0;     // bit-7 of NR 0x28
    uint16_t keymap_addr_ = 0;     // 9-bit nr_keymap_addr
};
