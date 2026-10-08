#pragma once

#include <cstdint>

/// The page value that means "not a RAM page": code fetched from a ROM-mapped
/// slot or from an overlay (boot ROM, Multiface, DivMMC, Layer 2 read mapping),
/// where the MMU's page number does not name what supplied the instruction.
/// The Next has 224 8K RAM pages (0..0xDF), so 0xFF never names one.
inline constexpr uint8_t NOT_RAM_PAGE = 0xFF;
