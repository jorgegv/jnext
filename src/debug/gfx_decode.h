#pragma once
// GH #371 - the Qt-free half of the Debugger's Graphics tab: how a sprite pattern or a tilemap tile
// definition is laid out in its RAM and which palette index (and transparency) each pixel gets.
// Std headers only, so any frontend can use it and its suite runs in every build tree. A sibling of
// mem_tools.h, not part of the four frozen facade headers. The decoders return the palette INDEX the
// layer looks up; the colour is applied by the caller through Debugger::palette().
//
// Oracle: sprites.vhd and tilemap.vhd on master (3.02.03); line numbers in gfx_decode.cpp.

#include <array>
#include <cstddef>
#include <cstdint>

namespace jnext {
namespace dbg {
namespace gfx {

struct Pixel {
    uint8_t index;       ///< the palette index the layer looks up
    bool    transparent; ///< the layer does not draw this pixel
};

constexpr int PATTERN_SIDE    = 16;
constexpr int PATTERNS_8BIT   = 64;
constexpr int PATTERNS_4BIT   = 128;
constexpr size_t PATTERN_RAM_SIZE = 0x4000;

/// One sprite pattern, untransformed, row-major (y*16+x). `pattern` is 0..63 (8-bit) or the 7-bit
/// N5:N0:N6 number 0..127 (4-bit), the numbering the Sprites table shows. `pal_offset` 0..15
/// (SpriteInfo::palette_offset), `transp` = NR 0x4B. False (out untouched) when the pattern number
/// or the RAM size (must be at least 16384) is out of range.
bool decode_pattern(const uint8_t* ram, size_t size, bool four_bit, uint8_t pattern,
                    uint8_t pal_offset, uint8_t transp, std::array<Pixel, 256>& out);

constexpr int TILE_SIDE = 8;
constexpr size_t BANK5_SIZE = 0x4000;
constexpr size_t BANK7_SIZE = 0x2000;

/// Tile definitions the map can address: 512 when NR 0x6B bit 1 is set, else 256.
int tile_count(uint8_t nr6b);

/// Byte of tile `tile` holding pixel (x,y) (4-bit) or row y (text mode; x ignored) within its bank
/// image: `base6` = NR 0x6F bits 5:0 added to the 256-byte block number with a 6-bit wrap; bank 7
/// is 8 KB so its address is masked to 13 bits.
uint16_t tile_byte_addr(bool bank7, uint8_t base6, bool textmode, uint16_t tile, int x, int y);

/// One tile definition, row-major (y*8+x). `bank` = the 16 KB bank-5 image, or the 8 KB bank-7
/// image when `bank7`. 4-bit: `pal_offset` 0..15, transparent when the nibble == NR 0x4C(3:0).
/// Text mode: `pal_offset` 0..127 (attribute bits 7:1), 1 bit per pixel, never transparent by index;
/// it is transparent when `palette256[index] >> 1 == nr14` (the global transparent colour), so
/// `palette256` (256 RGB333 entries of the tilemap palette in use) is required there. False (out
/// untouched) when tile > 511, the buffer is too small, or a text-mode palette is missing.
bool decode_tile(const uint8_t* bank, size_t size, bool bank7, uint8_t base6, bool textmode,
                 uint16_t tile, uint8_t pal_offset, uint8_t nr4c, uint8_t nr14,
                 const uint16_t* palette256, std::array<Pixel, 64>& out);

}  // namespace gfx
}  // namespace dbg
}  // namespace jnext
