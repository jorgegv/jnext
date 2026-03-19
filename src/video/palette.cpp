#include "video/palette.h"

// ---------------------------------------------------------------------------
// RGB333 → ARGB8888 helper
// ---------------------------------------------------------------------------

uint32_t rgb333_to_argb8888(uint8_t r3, uint8_t g3, uint8_t b3)
{
    // Expand 3-bit component to 8-bit by replicating the high bits into the low
    // bits: xxx → xxx_xx_x  →  full range [0, 255].
    // Formula: c8 = (c3 << 5) | (c3 << 2) | (c3 >> 1)
    uint8_t r8 = static_cast<uint8_t>((r3 << 5) | (r3 << 2) | (r3 >> 1));
    uint8_t g8 = static_cast<uint8_t>((g3 << 5) | (g3 << 2) | (g3 >> 1));
    uint8_t b8 = static_cast<uint8_t>((b3 << 5) | (b3 << 2) | (b3 >> 1));
    return 0xFF000000u
         | (static_cast<uint32_t>(r8) << 16)
         | (static_cast<uint32_t>(g8) <<  8)
         |  static_cast<uint32_t>(b8);
}

// ---------------------------------------------------------------------------
// Standard ZX Spectrum ULA palette
// ---------------------------------------------------------------------------
//
// Normal colours (index 0–7):
//   ZX RGB333 values from hardware:
//     0 Black:    000 000 000
//     1 Blue:     000 000 110
//     2 Red:      110 000 000
//     3 Magenta:  110 000 110
//     4 Green:    000 110 000
//     5 Cyan:     000 110 110
//     6 Yellow:   110 110 000
//     7 White:    110 110 110
//
// Bright colours (index 8–15): same hues, all components → 111
//     8 Bright Black:    000 000 000  (same as normal, no bright black)
//     9 Bright Blue:     000 000 111
//    10 Bright Red:      111 000 000
//    11 Bright Magenta:  111 000 111
//    12 Bright Green:    000 111 000
//    13 Bright Cyan:     000 111 111
//    14 Bright Yellow:   111 111 000
//    15 Bright White:    111 111 111

const uint32_t kUlaPalette[16] = {
    // Normal (indices 0–7)
    rgb333_to_argb8888(0, 0, 0),   // 0 Black
    rgb333_to_argb8888(0, 0, 6),   // 1 Blue
    rgb333_to_argb8888(6, 0, 0),   // 2 Red
    rgb333_to_argb8888(6, 0, 6),   // 3 Magenta
    rgb333_to_argb8888(0, 6, 0),   // 4 Green
    rgb333_to_argb8888(0, 6, 6),   // 5 Cyan
    rgb333_to_argb8888(6, 6, 0),   // 6 Yellow
    rgb333_to_argb8888(6, 6, 6),   // 7 White

    // Bright (indices 8–15)
    rgb333_to_argb8888(0, 0, 0),   // 8  Bright Black  (identical to normal)
    rgb333_to_argb8888(0, 0, 7),   // 9  Bright Blue
    rgb333_to_argb8888(7, 0, 0),   // 10 Bright Red
    rgb333_to_argb8888(7, 0, 7),   // 11 Bright Magenta
    rgb333_to_argb8888(0, 7, 0),   // 12 Bright Green
    rgb333_to_argb8888(0, 7, 7),   // 13 Bright Cyan
    rgb333_to_argb8888(7, 7, 0),   // 14 Bright Yellow
    rgb333_to_argb8888(7, 7, 7),   // 15 Bright White
};
