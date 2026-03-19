#pragma once
#include <cstdint>
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

private:
    /// matrix_[row]: 5-bit state; bit N = 0 means column N key is pressed.
    uint8_t matrix_[8];

    void set_matrix_bit(int row, int col, bool pressed);
};
