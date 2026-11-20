#include "debug/gfx_decode.h"

namespace jnext {
namespace dbg {
namespace gfx {

bool decode_pattern(const uint8_t* ram, size_t size, bool four_bit, uint8_t pattern,
                    uint8_t pal_offset, uint8_t transp, std::array<Pixel, 256>& out)
{
    const int limit = four_bit ? PATTERNS_4BIT : PATTERNS_8BIT;
    if (!ram || size < PATTERN_RAM_SIZE || pattern >= limit) return false;
    const unsigned off = pal_offset & 0x0F;
    for (int y = 0; y < PATTERN_SIDE; ++y) {
        for (int x = 0; x < PATTERN_SIDE; ++x) {
            Pixel& p = out[static_cast<size_t>(y * PATTERN_SIDE + x)];
            if (!four_bit) {
                // sprites.vhd:816 / :962: address = pattern(5:0) & y & x.
                const uint8_t b = ram[pattern * 256u + static_cast<unsigned>(y * 16 + x)];
                // :968 high nibble + offset, 4-bit wrap; :971 transparency on the raw byte.
                p.index = static_cast<uint8_t>((((b >> 4) + off) & 0x0F) << 4 | (b & 0x0F));
                p.transparent = (b == transp);
            } else {
                // :962-963: address = N5:N0 & N6 & y & x(3:1), pattern7 = N5:N0:N6.
                const uint8_t b = ram[pattern * 128u + static_cast<unsigned>(y * 8 + x / 2)];
                // :967 high nibble for an even x.
                const uint8_t nib = (x & 1) ? (b & 0x0F) : (b >> 4);
                p.index = static_cast<uint8_t>(off << 4 | nib);
                p.transparent = (nib == (transp & 0x0F));   // :971
            }
        }
    }
    return true;
}

int tile_count(uint8_t nr6b)
{
    return (nr6b & 0x02) ? 512 : 256;   // tilemap.vhd:393, mode_512
}

uint16_t tile_byte_addr(bool bank7, uint8_t base6, bool textmode, uint16_t tile, int x, int y)
{
    // tilemap.vhd:394: 4-bit = tile & y & x(2:1); text mode = "00" & tile & y.
    const unsigned sub = textmode ? (tile * 8u + static_cast<unsigned>(y))
                                  : (tile * 32u + static_cast<unsigned>(y * 4 + x / 2));
    // :403: 6-bit add of the base to the block number, wrapping inside the bank.
    unsigned addr = ((((sub >> 8) + base6) & 0x3Fu) << 8) | (sub & 0xFFu);
    // zxnext.vhd:6670-6684: the bank-7 BRAM is 8 KB.
    if (bank7) addr &= 0x1FFFu;
    return static_cast<uint16_t>(addr);
}

bool decode_tile(const uint8_t* bank, size_t size, bool bank7, uint8_t base6, bool textmode,
                 uint16_t tile, uint8_t pal_offset, uint8_t nr4c, uint8_t nr14,
                 const uint16_t* palette256, std::array<Pixel, 64>& out)
{
    if (!bank || tile > 511 || size < (bank7 ? BANK7_SIZE : BANK5_SIZE)) return false;
    if (textmode && !palette256) return false;
    for (int y = 0; y < TILE_SIDE; ++y) {
        for (int x = 0; x < TILE_SIDE; ++x) {
            Pixel& p = out[static_cast<size_t>(y * TILE_SIDE + x)];
            const uint8_t b = bank[tile_byte_addr(bank7, base6, textmode, tile, x, y)];
            if (!textmode) {
                // :383 high nibble for an even x; :382 index = attribute(7:4) & nibble.
                const uint8_t nib = (x & 1) ? (b & 0x0F) : (b >> 4);
                p.index = static_cast<uint8_t>((pal_offset & 0x0F) << 4 | nib);
                p.transparent = (nib == (nr4c & 0x0F));   // :427, the nibble, not the index
            } else {
                // :385-386: pixel x is bit 7-x; index = attribute(7:1) & bit.
                const unsigned bit = (b >> (7 - x)) & 1u;
                p.index = static_cast<uint8_t>(((pal_offset & 0x7F) << 1) | bit);
                // :429 always enabled here; zxnext.vhd:7109 drops it by colour against NR 0x14.
                p.transparent = ((palette256[p.index] >> 1) & 0xFF) == nr14;
            }
        }
    }
    return true;
}

}  // namespace gfx
}  // namespace dbg
}  // namespace jnext
