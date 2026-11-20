// Debugger Graphics tab decoders (GH #371): jnext::dbg::gfx, sprite patterns and tile definitions.
//
// Oracle: sprites.vhd and tilemap.vhd on master (3.02.03) and zxnext.vhd, cited per row. Every
// expected value below is a literal derived by hand from those lines, never from the decoder's own
// formula.
//
// Run: ./build/test/gfx_decode_test

#include "debug/gfx_decode.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

using namespace jnext::dbg::gfx;

static int g_total = 0, g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char* id, const char* desc, bool ok, const std::string& detail = "") {
    report_row_id(id);
    ++g_total;
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  FAIL %s: %s %s\n", id, desc, detail.c_str()); }
}

using Pat = std::array<Pixel, 256>;
using Til = std::array<Pixel, 64>;
using Ram = std::vector<uint8_t>;

static Ram pat_ram() { return Ram(0x4000, 0); }

// Number of visible pixels; *first = the first visible one.
static int visible(const Pat& p, int* first) {
    int n = 0;
    for (int i = 0; i < 256; ++i)
        if (!p[i].transparent) { if (n++ == 0) *first = i; }
    return n;
}
// Exactly one visible pixel, at (x,y) of a 16-wide pattern, with this index.
static bool lone(const Pat& p, int x, int y, uint8_t idx) {
    int f = -1;
    return visible(p, &f) == 1 && f == y * 16 + x && p[f].index == idx;
}

int main() {
    // GFXD-01
    {
        struct Probe { uint8_t p; int x, y; unsigned addr; };
        const Probe probes[] = {{0, 0, 0, 0x0000}, {1, 15, 0, 0x010F}, {0x2A, 3, 9, 0x2A93}, {0x3F, 15, 15, 0x3FFF}};
        bool ok = true;
        std::string bad;
        for (const Probe& pr : probes) {
            Ram ram = pat_ram();
            ram[pr.addr] = 0x47;
            Pat out{};
            if (!decode_pattern(ram.data(), ram.size(), false, pr.p, 0, 0x00, out) || !lone(out, pr.x, pr.y, 0x47)) {
                ok = false;
                bad += "pattern " + std::to_string(pr.p) + " ";
            }
        }
        check("GFXD-01", "8-bit pattern addressing: pattern*256 + y*16 + x; (0,0,0)=0000 (1,15,0)=010F (2A,3,9)=2A93 (3F,15,15)=3FFF "
                         "- VHDL sprites.vhd:816, :962", ok, bad);
    }
    // GFXD-02
    {
        struct Case { uint8_t byte, off, want; };
        const Case cases[] = {{0x5C, 0, 0x5C}, {0x5C, 3, 0x8C}, {0x5C, 0xC, 0x1C}, {0xF7, 1, 0x07}};
        bool ok = true;
        std::string bad;
        for (const Case& c : cases) {
            Ram ram = pat_ram();
            ram[0] = c.byte;
            Pat out{};
            if (!decode_pattern(ram.data(), ram.size(), false, 0, c.off, 0x00, out) || out[0].index != c.want) {
                ok = false;
                char b[40]; std::snprintf(b, sizeof b, "byte %02X off %X ", c.byte, c.off);
                bad += b;
            }
        }
        check("GFXD-02", "8-bit palette offset: 4-bit add to the high nibble, wrapping; 5C+0/3/C -> 5C/8C/1C, F7+1 -> 07 "
                         "- VHDL sprites.vhd:968", ok, bad);
    }
    // GFXD-03
    {
        Ram ram = pat_ram();
        ram[0] = 0xE3;
        ram[1] = 0xC3;
        Pat out{};
        const bool ran = decode_pattern(ram.data(), ram.size(), false, 0, 2, 0xE3, out);
        check("GFXD-03", "8-bit transparency compares the RAW byte: E3 off 2 transparent; C3 off 2 -> index E3, visible "
                         "- VHDL sprites.vhd:971", ran && out[0].transparent && !out[1].transparent && out[1].index == 0xE3);
    }
    // GFXD-04
    {
        struct Probe { uint8_t p7; int x, y; unsigned addr; };
        const Probe probes[] = {{1, 0, 0, 0x0080}, {2, 0, 0, 0x0100}, {0x55, 6, 3, 0x2A9B}, {0x7F, 15, 15, 0x3FFF}};
        bool ok = true;
        std::string bad;
        for (const Probe& pr : probes) {
            Ram ram = pat_ram();
            ram[pr.addr] = (pr.x & 1) ? 0x09 : 0x90;
            Pat out{};
            if (!decode_pattern(ram.data(), ram.size(), true, pr.p7, 0, 0x00, out) || !lone(out, pr.x, pr.y, 0x09)) {
                ok = false;
                bad += "pattern " + std::to_string(pr.p7) + " ";
            }
        }
        check("GFXD-04", "4-bit pattern addressing: pattern7*128 + y*8 + x/2; (1,0,0)=0080 (2,0,0)=0100 (55,6,3)=2A9B (7F,15,15)=3FFF "
                         "- VHDL sprites.vhd:962-963", ok, bad);
    }
    // GFXD-05, GFXD-06
    {
        Ram ram = pat_ram();
        ram[0] = 0xA5;
        Pat a{}, b{};
        const bool r1 = decode_pattern(ram.data(), ram.size(), true, 0, 0, 0x00, a);
        const bool r2 = decode_pattern(ram.data(), ram.size(), true, 0, 0xB, 0x00, b);
        check("GFXD-05", "4-bit nibble order: byte A5 -> pixel 0 nibble A (high), pixel 1 nibble 5 (low) "
                         "- VHDL sprites.vhd:967", r1 && a[0].index == 0x0A && a[1].index == 0x05);
        check("GFXD-06", "4-bit index = offset<<4 | nibble: off B -> BA, B5 - VHDL sprites.vhd:968",
              r2 && b[0].index == 0xBA && b[1].index == 0xB5);
    }
    // GFXD-07
    {
        Ram ram = pat_ram();
        ram[0] = 0xE3;   // px0 nibble E, px1 nibble 3
        ram[1] = 0xF1;   // px2 nibble F, px3 nibble 1
        Pat a{}, b{};
        const bool r1 = decode_pattern(ram.data(), ram.size(), true, 0, 0, 0xE3, a);
        const bool r2 = decode_pattern(ram.data(), ram.size(), true, 0, 0, 0x1F, b);
        check("GFXD-07", "4-bit transparency compares the nibble with NR 0x4B(3:0): 0xE3 -> nibble 3 transparent, E visible; "
                         "0x1F -> F transparent, 1 visible - VHDL sprites.vhd:971",
              r1 && r2 && !a[0].transparent && a[1].transparent && b[2].transparent && !b[3].transparent);
    }
    // GFXD-08
    {
        Ram ram = pat_ram();
        Pat out{};
        for (Pixel& p : out) p = Pixel{0x77, true};
        const bool r1 = decode_pattern(ram.data(), ram.size(), false, 64, 0, 0, out);
        const bool r2 = decode_pattern(ram.data(), ram.size(), true, 128, 0, 0, out);
        const bool r3 = decode_pattern(ram.data(), 16383, false, 0, 0, 0, out);
        const bool r4 = decode_pattern(nullptr, 0x4000, false, 0, 0, 0, out);
        bool untouched = true;
        for (const Pixel& p : out) untouched = untouched && p.index == 0x77 && p.transparent;
        check("GFXD-08", "pattern refusals: 8-bit 64, 4-bit 128, RAM of 16383 bytes, no RAM -> false and the output untouched "
                         "- VHDL sprites.vhd:93 (6-bit pattern names)", !r1 && !r2 && !r3 && !r4 && untouched);
    }
    // GFXD-09
    {
        Ram ram = pat_ram();
        for (int i = 0; i < 256; ++i) ram[0x1000 + i] = static_cast<uint8_t>(i);
        Pat out{};
        bool ok = decode_pattern(ram.data(), ram.size(), false, 0x10, 0, 0xE3, out);
        std::string bad;
        for (int i = 0; ok && i < 256; ++i)
            if (out[i].index != i || out[i].transparent != (i == 0xE3)) { ok = false; bad = "pixel " + std::to_string(i); }
        check("GFXD-09", "whole 8-bit pattern 0x10 in row-major order: pixel i has index i, only E3 transparent "
                         "- VHDL sprites.vhd:816, :962, :971", ok, bad);
    }
    // GFXD-10
    {
        struct Case { uint8_t base; uint16_t t; int x, y; unsigned addr; };
        const Case cases[] = {{0x0C, 0, 0, 0, 0x0C00}, {0x0C, 1, 0, 0, 0x0C20}, {0x0C, 8, 0, 0, 0x0D00},
                              {0x0C, 7, 7, 7, 0x0CFF}, {0x3F, 8, 0, 0, 0x0000}, {0x00, 511, 7, 7, 0x3FFF}};
        bool ok = true;
        std::string bad;
        for (const Case& c : cases)
            if (tile_byte_addr(false, c.base, false, c.t, c.x, c.y) != c.addr) {
                ok = false;
                char b[40]; std::snprintf(b, sizeof b, "base %02X tile %u ", c.base, c.t);
                bad += b;
            }
        check("GFXD-10", "4-bit tile address: tile*32 + y*4 + x/2, base added to the 256-byte block with a 6-bit wrap; "
                         "0C: t0=0C00 t1=0C20 t8=0D00 t7(7,7)=0CFF; 3F t8=0000; t511=3FFF - VHDL tilemap.vhd:394, :403", ok, bad);
    }
    // GFXD-11
    {
        Ram bank(0x4000, 0);
        bank[0x0C00] = 0x12; bank[0x0C01] = 0x34; bank[0x0C02] = 0x56; bank[0x0C03] = 0x78;
        Til out{};
        const bool ran = decode_tile(bank.data(), bank.size(), false, 0x0C, false, 0, 4, 0xF, 0, nullptr, out);
        const uint8_t want[8] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48};
        bool ok = ran;
        for (int i = 0; ok && i < 8; ++i) ok = out[i].index == want[i] && !out[i].transparent;
        // The offset is attribute bits 7:4, all four of them: off B -> B1..B8.
        Til hi{};
        ok = ok && decode_tile(bank.data(), bank.size(), false, 0x0C, false, 0, 0xB, 0xF, 0, nullptr, hi);
        for (int i = 0; ok && i < 8; ++i) ok = hi[i].index == (0xB0 | (i + 1)) && !hi[i].transparent;
        check("GFXD-11", "4-bit tile pixels: bytes 12 34 56 78, off 4, NR 0x4C F -> indices 41..48, none transparent; off B -> B1..B8 "
                         "(all four attribute bits 7:4) - VHDL tilemap.vhd:382-383", ok);
    }
    // GFXD-12
    {
        Ram bank(0x4000, 0);
        bank[0] = 0xF0;   // px0 nibble F, px1 nibble 0
        Til a{}, b{};
        const bool r1 = decode_tile(bank.data(), bank.size(), false, 0, false, 0, 3, 0xF, 0, nullptr, a);
        const bool r2 = decode_tile(bank.data(), bank.size(), false, 0, false, 0, 3, 0x0, 0, nullptr, b);
        check("GFXD-12", "4-bit tile transparency compares the nibble with NR 0x4C, not the index: off 3 nibble F with 4C=F -> "
                         "transparent (index 3F); with 4C=0 visible; nibble 0 with 4C=0 transparent - VHDL tilemap.vhd:427",
              r1 && r2 && a[0].transparent && a[0].index == 0x3F && !a[1].transparent &&
                  !b[0].transparent && b[1].transparent);
    }
    // GFXD-13
    {
        struct Case { uint8_t base; uint16_t t; int y; unsigned addr; };
        const Case cases[] = {{0x0C, 0, 0, 0x0C00}, {0x0C, 1, 0, 0x0C08}, {0x0C, 32, 0, 0x0D00},
                              {0x00, 511, 7, 0x0FFF}, {0x3F, 32, 0, 0x0000}};
        bool ok = true;
        std::string bad;
        for (const Case& c : cases)
            if (tile_byte_addr(false, c.base, true, c.t, 0, c.y) != c.addr) {
                ok = false;
                char b[40]; std::snprintf(b, sizeof b, "base %02X tile %u ", c.base, c.t);
                bad += b;
            }
        check("GFXD-13", "text-mode tile address: tile*8 + y, same base and wrap; 0C: t0=0C00 t1=0C08 t32=0D00; t511 row 7=0FFF; "
                         "3F t32=0000 - VHDL tilemap.vhd:394, :403", ok, bad);
    }
    // GFXD-14, GFXD-15
    {
        Ram bank(0x4000, 0);
        bank[0] = 0x81;   // row 0: bits 7 and 0
        bank[1] = 0x80;   // row 1: bit 7 only (0x81 alone is its own mirror image)
        std::vector<uint16_t> pal(256, 0);
        Til a{};
        const bool r1 = decode_tile(bank.data(), bank.size(), false, 0, true, 0, 0x55, 0, 0xFF, pal.data(), a);
        const uint8_t want[8] = {0xAB, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAB};
        bool ok = r1;
        for (int i = 0; ok && i < 8; ++i) ok = a[i].index == want[i];
        ok = ok && a[8].index == 0xAB && a[15].index == 0xAA;
        check("GFXD-14", "text-mode bit order and index: byte 81, off7 55 -> pixel 0 = AB, pixels 1..6 = AA, pixel 7 = AB; byte 80 -> "
                         "pixel 0 = AB, pixel 7 = AA - VHDL tilemap.vhd:385-386", ok);

        pal[0xAA] = 0x1C6;   // >>1 = 0xE3
        pal[0xAB] = 0x1C4;   // >>1 = 0xE2
        Til b{}, c{};
        const bool r2 = decode_tile(bank.data(), bank.size(), false, 0, true, 0, 0x55, 0, 0xE3, pal.data(), b);
        pal[0xAA] = 0x000;
        const bool r3 = decode_tile(bank.data(), bank.size(), false, 0, true, 0, 0x55, 0xA, 0xE3, pal.data(), c);
        bool ok2 = r2 && r3 && !b[0].transparent && !b[7].transparent;
        for (int i = 1; i <= 6; ++i) ok2 = ok2 && b[i].transparent && !c[i].transparent;
        check("GFXD-15", "text-mode transparency is by colour against NR 0x14, never by NR 0x4C: palette[AA]=1C6 (>>1 = E3) with "
                         "NR 0x14 E3 -> pixels 1..6 transparent, palette[AB]=1C4 visible; NR 0x4C = A with palette[AA]=000 visible "
                         "- VHDL zxnext.vhd:7109, :6822; tilemap.vhd:429", ok2);
    }
    // GFXD-16
    {
        struct Case { uint8_t base; uint16_t t; unsigned addr; };
        const Case cases[] = {{0x20, 0, 0x0000}, {0x3F, 0, 0x1F00}, {0x1F, 8, 0x0000}};
        bool ok = true;
        for (const Case& c : cases)
            ok = ok && tile_byte_addr(true, c.base, false, c.t, 0, 0) == c.addr;
        Ram bank(0x2000, 0);
        bank[0x0000] = 0x12;
        bank[0x1F00] = 0x34;
        Til a{}, b{};
        const bool r1 = decode_tile(bank.data(), bank.size(), true, 0x20, false, 0, 0, 0xF, 0, nullptr, a);
        const bool r2 = decode_tile(bank.data(), bank.size(), true, 0x3F, false, 0, 0, 0xF, 0, nullptr, b);
        check("GFXD-16", "bank 7 wraps at 8 KB: (20,t0)=0000 (3F,t0)=1F00 (1F,t8)=0000; an 8 KB buffer decodes those bytes "
                         "- VHDL zxnext.vhd:6670-6684", ok && r1 && r2 && a[0].index == 0x01 && a[1].index == 0x02 &&
                                                         b[0].index == 0x03 && b[1].index == 0x04);
    }
    // GFXD-17
    {
        check("GFXD-17", "tile_count: NR 0x6B 00 -> 256, 02 -> 512, 0A -> 512, 08 -> 256, FD -> 256, 82 -> 512 "
                         "- VHDL tilemap.vhd:194, :393",
              tile_count(0x00) == 256 && tile_count(0x02) == 512 && tile_count(0x0A) == 512 &&
                  tile_count(0x08) == 256 && tile_count(0xFD) == 256 && tile_count(0x82) == 512);
    }
    // GFXD-18
    {
        Ram bank(0x4000, 0);
        const uint8_t r0[4] = {0x01, 0x23, 0x45, 0x67}, r7[4] = {0xFE, 0xDC, 0xBA, 0x98};
        for (int i = 0; i < 4; ++i) { bank[0x0C60 + i] = r0[i]; bank[0x0C7C + i] = r7[i]; }
        Til out{};
        const bool ran = decode_tile(bank.data(), bank.size(), false, 0x0C, false, 3, 0, 0x0, 0, nullptr, out);
        const uint8_t w7[8] = {0xF, 0xE, 0xD, 0xC, 0xB, 0xA, 0x9, 0x8};
        bool ok = ran;
        for (int i = 0; ok && i < 8; ++i) {
            ok = out[i].index == i && out[i].transparent == (i == 0) &&
                 out[56 + i].index == w7[i] && !out[56 + i].transparent;
        }
        check("GFXD-18", "whole 4-bit tile, base 0C tile 3: row 0 (at 0C60) indices 0..7 with pixel 0 transparent; row 7 (at 0C7C) F..8 "
                         "- VHDL tilemap.vhd:382-383, :394, :403, :427", ok);
    }
    // GFXD-19
    {
        Ram b5(0x4000, 0), b7(0x2000, 0);
        std::vector<uint16_t> pal(256, 0);
        Til out{};
        for (Pixel& p : out) p = Pixel{0x77, true};
        const bool r1 = decode_tile(b5.data(), b5.size(), false, 0, false, 512, 0, 0, 0, nullptr, out);
        const bool r2 = decode_tile(b5.data(), 16383, false, 0, false, 0, 0, 0, 0, nullptr, out);
        const bool r3 = decode_tile(b7.data(), 8191, true, 0, false, 0, 0, 0, 0, nullptr, out);
        const bool r4 = decode_tile(b5.data(), b5.size(), false, 0, true, 0, 0, 0, 0, nullptr, out);   // text mode, no palette
        bool untouched = true;
        for (const Pixel& p : out) untouched = untouched && p.index == 0x77 && p.transparent;
        check("GFXD-19", "tile refusals: tile 512, a 16383-byte bank-5 buffer, an 8191-byte bank-7 buffer, text mode without a "
                         "palette -> false and the output untouched - API contract (tile names are 9 bits, tilemap.vhd:393)",
              !r1 && !r2 && !r3 && !r4 && untouched);
    }

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail > 0 ? 1 : 0;
}
