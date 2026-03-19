#pragma once
#include <cstdint>

/// Standard ZX Spectrum ULA palette.
///
/// 16 entries: indices 0–7 are normal colours, 8–15 are bright versions.
/// The index corresponds directly to the ink/paper value from an attribute byte
/// with the bright flag shifted in: bright_bit * 8 + colour_bits.
///
/// Colours stored as ARGB8888 (0xFF_RR_GG_BB) for direct use with the
/// uint32_t emulator framebuffer.
extern const uint32_t kUlaPalette[16];

/// Convert a ZX RGB333 colour (3 bits each, as used internally on the ZX Next)
/// to ARGB8888.
uint32_t rgb333_to_argb8888(uint8_t r3, uint8_t g3, uint8_t b3);
