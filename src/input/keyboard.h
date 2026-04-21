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
    /// from the Emulator frame loop (Agent F wires this). Phase 1 stub
    /// is a no-op.
    void tick_scan();

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

    /// Extended-key 16-bit active-low register. Default 0xFFFF = all
    /// keys released. Read via NR 0xB0 (low byte) and NR 0xB1 (high
    /// byte) with the VHDL bit permutations at zxnext.vhd:6206-6212.
    /// Phase 1: field declared but not yet populated — Agent G writes
    /// the scancode→ID mapping and calls set_extended_key().
    uint16_t ex_matrix_ = 0xFFFF;

    /// Two-scan shift hysteresis buffer. shift_hist_[0] = current scan,
    /// shift_hist_[1] = previous scan; Agent F implements the
    /// confirm-on-two-agreements logic for Caps-Shift and Symbol-Shift.
    /// Phase 1: field declared only.
    uint8_t shift_hist_[2] = { 0xFF, 0xFF };
};
