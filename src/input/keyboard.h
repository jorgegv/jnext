#pragma once
#include <cstdint>
#include <vector>
#include <SDL2/SDL.h>

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
    // Phase 1 scaffold additions (Task 3 Input) — all stubs.
    //
    // Extended key matrix + two-scan shift hysteresis. The extended-key
    // 16-bit register is read back on NR 0xB0 (bits 15..8) and NR 0xB1
    // (bits 7..0) per zxnext.vhd:6206-6212. Shift hysteresis is the
    // two-scan confirmation VHDL uses to debounce the SHIFT columns
    // (Caps Shift = row 0 col 0, Symbol Shift = row 7 col 1); Agent F
    // fills in the hysteresis FSM.
    // -----------------------------------------------------------------------

    /// Mark an extended-key ID as pressed / released. IDs follow the
    /// VHDL i_KBD_EXTENDED_KEYS bit positions (see NR 0xB0/0xB1 read
    /// compositions at zxnext.vhd:6206-6212). Phase 1 stub: no-op.
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

    /// NR 0xB0 readback. Phase 1 stub returns 0xFF (all released).
    uint8_t nr_b0_byte() const;

    /// NR 0xB1 readback. Phase 1 stub returns 0xFF (all released).
    uint8_t nr_b1_byte() const;

private:
    /// matrix_[row]: 5-bit state; bit N = 0 means column N key is pressed.
    uint8_t matrix_[8];

    void set_matrix_bit(int row, int col, bool pressed);

    // Auto-type state
    std::vector<AutoKey> auto_queue_;
    int auto_frame_count_ = 0;
    bool auto_gap_ = false;  // true = in gap between keys

    /// Extended-key 16-bit register. Stored ACTIVE-HIGH in the C++
    /// model (Phase-1 polarity fix for NR 0xB0 / NR 0xB1 at
    /// zxnext.vhd:6206-6212): bit=1 means the extended key is pressed.
    /// Read back via NR 0xB0 (low byte) and NR 0xB1 (high byte) with
    /// the VHDL bit permutations at zxnext.vhd:6206-6212.
    ///
    /// Phase 1: field declared; reset() still initialises to 0xFFFF
    /// (legacy scaffold default). Agent G will populate the
    /// scancode→ID mapping, fix the reset default, and wire the
    /// permutation readers. Agent F owns cancel_extended_entries()
    /// which clears the register to 0x0000 per membrane.vhd:183-186.
    uint16_t ex_matrix_ = 0xFFFF;

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
