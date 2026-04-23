#pragma once
#include <cstdint>
#include <vector>
#include <SDL2/SDL.h>

// Forward decl: MembraneStick (Task 3 Input) is wired as an optional
// upstream joystick-to-keyboard adapter. Runtime (full Emulator) installs
// a pointer via set_membrane_stick(); unit tests that construct bare
// Keyboard objects leave it null and bypass the fold.
class MembraneStick;

/// ZX Spectrum keyboard matrix (8 rows x 5 columns).
///
/// Row selection: upper byte of port address, active-low.
///   Bit N of addr_high == 0 => row N is selected.
///
/// Row layout:
///   Row 0 (A8=0):  SHIFT  Z  X  C  V
///   Row 1 (A9=0):  A  S  D  F  G
///   Row 2 (A10=0): Q  W  E  R  T
///   Row 3 (A11=0): 1  2  3  4  5
///   Row 4 (A12=0): 0  9  8  7  6
///   Row 5 (A13=0): P  O  I  U  Y
///   Row 6 (A14=0): ENTER  L  K  J  H
///   Row 7 (A15=0): SPACE  SYM  M  N  B
class Keyboard {
public:
    /// Reset all keys to released state (matrix filled with 0xFF).
    void reset();

    /// Called from the SDL input handler on each KEYDOWN/KEYUP event.
    void set_key(SDL_Scancode sc, bool pressed);

    /// Called by port 0xFE read.
    /// addr_high = upper byte of the 16-bit port address.
    /// Returns 5-bit OR of selected rows (bits 0-4), active-low.
    /// Bits 7-5 are not set here — the caller should OR in 0xE0 and the
    /// EAR/MIC bits before returning to the CPU.
    uint8_t read_rows(uint8_t addr_high) const;

    /// Auto-type: press a key by matrix position (row 0-7, col 0-4) for N frames.
    struct AutoKey {
        int row1, col1;     // primary key
        int row2, col2;     // secondary key (-1 if none, e.g. SYMBOL SHIFT)
        int frames;         // how many frames to hold
    };

    /// Queue a sequence of auto-typed keys. Each entry is pressed for
    /// `frames` video frames, with a 2-frame gap between keys.
    void queue_auto_type(const std::vector<AutoKey>& keys);

    /// Called once per frame to advance the auto-type state machine.
    void tick_auto_type();

    /// True if auto-type is currently active.
    bool auto_typing() const { return !auto_queue_.empty(); }

    // -----------------------------------------------------------------------
    // Phase 2 Agent G (Task 3 Input) — 16-key extended matrix.
    //
    // The extended-key 16-bit register is read back on NR 0xB0 (ids 0..7)
    // and NR 0xB1 (ids 8..15) per zxnext.vhd:6206-6212. ex_matrix_ is
    // stored ACTIVE-HIGH: bit=1 ⇒ key pressed, bit=0 ⇒ released, default
    // 0x0000. The ID numbering below aligns 1:1 with the VHDL
    // port_253b_dat bit position for each NR, so set_extended_key(id,..)
    // flips exactly bit `id` of ex_matrix_ and nr_b0_byte()/nr_b1_byte()
    // are plain low/high byte extractions.
    //
    // Shift hysteresis (two-scan confirm) for the composite Caps-Shift
    // and Sym-Shift effects of the extended keys is Agent F's scope:
    // cancel_extended_entries() and tick_scan() are provided there.
    // -----------------------------------------------------------------------

    /// ExtKey IDs. IDs 0..7 map to NR 0xB0 bits 0..7; IDs 8..15 map to
    /// NR 0xB1 bits 0..7 (derived from zxnext.vhd:6206-6212).
    enum class ExtKey : int {
        RIGHT      =  0,   // NR 0xB0 bit 0 — folds into (row 4, col 2)
        LEFT       =  1,   // NR 0xB0 bit 1 — folds into (row 3, col 4)
        DOWN       =  2,   // NR 0xB0 bit 2 — folds into (row 4, col 4)
        UP         =  3,   // NR 0xB0 bit 3 — folds into (row 4, col 3)
        DOT        =  4,   // NR 0xB0 bit 4 — folds into (row 7, col 2)
        COMMA      =  5,   // NR 0xB0 bit 5 — folds into (row 7, col 3)
        QUOTE      =  6,   // NR 0xB0 bit 6 — folds into (row 5, col 0)
        SEMICOLON  =  7,   // NR 0xB0 bit 7 — folds into (row 5, col 1)
        EXTEND     =  8,   // NR 0xB1 bit 0 — only via shift hysteresis (Agent F)
        CAPS_LOCK  =  9,   // NR 0xB1 bit 1 — folds into (row 3, col 1)
        GRAPH      = 10,   // NR 0xB1 bit 2 — folds into (row 4, col 1)
        TRUE_VIDEO = 11,   // NR 0xB1 bit 3 — folds into (row 3, col 2)
        INV_VIDEO  = 12,   // NR 0xB1 bit 4 — folds into (row 3, col 3)
        BREAK      = 13,   // NR 0xB1 bit 5 — folds into (row 7, col 0)
        EDIT       = 14,   // NR 0xB1 bit 6 — folds into (row 3, col 0)
        DELETE     = 15    // NR 0xB1 bit 7 — folds into (row 4, col 0)
    };

    /// Mark an extended-key ID as pressed / released. `id` is in
    /// [0..15]; see ExtKey. ACTIVE-HIGH: pressed sets bit `id` of
    /// ex_matrix_, released clears it. Out-of-range ids are ignored.
    void set_extended_key(int id, bool pressed);

    /// Advance the two-scan shift hysteresis by one scan tick. Called
    /// from the Emulator frame loop. Shifts shift_hist_[1] <= shift_hist_[0]
    /// and latches the current matrix_[] shift-column state into
    /// shift_hist_[0]. Per membrane.vhd:178 ("advancing shift key state
    /// one scan and holding shift key state an extra scan") read_rows()
    /// consults these to hold a releasing CS/SYM bit for one extra scan.
    void tick_scan();

    /// Called by external logic (e.g. a DivMMC hotkey handler) to cancel
    /// all extended-key entries for this scan. Per membrane.vhd:183-186
    /// (the reset/cancel branch of the matrix_state_ex process):
    /// matrix_state_ex_{0,1} and matrix_work_ex are forced to '1' i.e.
    /// all-released in the VHDL's internal active-low representation.
    /// In our active-high C++ model (NR 0xB0/0xB1 bit=1 => pressed, per
    /// Phase-1 polarity fix) that corresponds to ex_matrix_ = 0x0000.
    void cancel_extended_entries();

    /// NR 0xB0 readback (ACTIVE-HIGH): low 8 bits of ex_matrix_ — bits
    /// ';' '"' ',' '.' UP DOWN LEFT RIGHT (7..0) per zxnext.vhd:6208.
    uint8_t nr_b0_byte() const;

    /// NR 0xB1 readback (ACTIVE-HIGH): high 8 bits of ex_matrix_ — bits
    /// DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND (7..0) per
    /// zxnext.vhd:6212.
    uint8_t nr_b1_byte() const;

    /// Install (or clear) the optional upstream joystick-to-keyboard
    /// adapter. When non-null, read_rows() AND-folds
    /// `MembraneStick::compose_into_row(row, mask)` into each selected
    /// row AFTER the extended-key fold — mirroring VHDL
    /// `zxnext_top_issue4.vhd:1843`:
    ///   keyb_col <= keyb_col_i_q AND membrane_stick_col AND ps2_kbd_col
    /// Null-safe: bare Keyboard instances (unit tests) skip the fold and
    /// behave exactly as before.
    void set_membrane_stick(MembraneStick* ms) { membrane_stick_ = ms; }

private:
    /// matrix_[row]: 5-bit state; bit N = 0 means column N key is pressed.
    uint8_t matrix_[8];

    void set_matrix_bit(int row, int col, bool pressed);

    // Auto-type state
    std::vector<AutoKey> auto_queue_;
    int auto_frame_count_ = 0;
    bool auto_gap_ = false;  // true = in gap between keys

    /// Extended-key 16-bit ACTIVE-HIGH register. Default 0x0000 = all
    /// keys released. Bit `id` set ⇔ ExtKey(id) pressed. Bit layout
    /// aligns 1:1 with NR 0xB0 bits 0..7 (ids 0..7) and NR 0xB1 bits
    /// 0..7 (ids 8..15) per zxnext.vhd:6206-6212. Populated by
    /// set_extended_key() (Agent G); consumed by read_rows() for
    /// membrane folding (membrane.vhd:236-240) and by nr_b0_byte() /
    /// nr_b1_byte() for NR readbacks.
    uint16_t ex_matrix_ = 0x0000;

    /// Non-owning back-pointer to the owning MembraneStick (installed by
    /// Emulator via set_membrane_stick()). Null in bare-Keyboard unit
    /// tests — read_rows() guards for this. See VHDL
    /// `zxnext_top_issue4.vhd:1843` for the AND-merge semantics.
    MembraneStick* membrane_stick_ = nullptr;

    /// Two-scan shift hysteresis buffer. Active-LOW, matching VHDL
    /// membrane.vhd:184-186 (`matrix_state_ex_0/1 <= (others => '1')`
    /// on reset/cancel means "all released").
    ///
    ///   shift_hist_[0] = snapshot of CS/SYM bits from the previous scan
    ///   shift_hist_[1] = snapshot from the scan before that
    ///
    /// Only bit 0 (Caps-Shift, row 0 col 0) and bit 1 (Symbol-Shift,
    /// row 7 col 1) are meaningful; other bits are preserved as 1 =
    /// released to stay harmless if ever read. Each `tick_scan()`
    /// advances the shift register; `read_rows()` consults shift_hist_[0]
    /// to hold a releasing CS or SYM bit for one extra scan
    /// (membrane.vhd:178 "holding shift key state an extra scan").
    uint8_t shift_hist_[2] = { 0xFF, 0xFF };
};
