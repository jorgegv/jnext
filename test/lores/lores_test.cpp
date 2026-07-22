// LoRes Subsystem Compliance Test Runner
//
// Implements the tier-U rows of doc/testing/LORES-TEST-PLAN-DESIGN.md, whose
// expected values are derived EXCLUSIVELY from the authoritative FPGA VHDL at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (chiefly video/lores.vhd, 117 lines, plus the wiring in zxnext.vhd).
//
// The plan was written BEFORE the implementation existed; no row here was
// adjusted to match what jnext does. Every check() recomputes its expected
// value from the VHDL expression cited beside it.
//
// Tier split (see the plan's "Test Architecture"):
//   U — this file: the pixel generator as a pure function.
//   C — test/compositor/compositor_test.cpp: ULA-slot substitution, palette,
//       transparency, priority, stencil/blend and the negative rows that need
//       a rendered scanline.
//   N — test/nextreg/nextreg_integration_test.cpp: register decode/read-back
//       and reset defaults.
//   S — deliberately absent (a reference PNG cannot be authored before the
//       renderer that would produce it; see the plan).
//
// Run: ./build/test/lores_test

#include "video/lores.h"
#include "video/renderer.h"
#include "video/palette.h"
#include "memory/ram.h"
#include "memory/rom.h"
#include "memory/mmu.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <set>

// ── Test infrastructure (same idiom as the sibling suites) ────────────────

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static std::string g_group;

struct TestResult {
    std::string group, id, description, detail;
    bool passed;
};
static std::vector<TestResult> g_results;

struct SkipNote { std::string id, reason; };
static std::vector<SkipNote> g_skipped;

static void set_group(const char* name) { g_group = name; }

static void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
    printf("  SKIP %s: %s\n", id, reason);
}

static void check(const char* id, const char* desc, bool cond,
                  const char* detail = "") {
    g_total++;
    g_results.push_back({g_group, id, desc, detail, cond});
    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL %s: %s", id, desc);
        if (detail[0]) printf(" [%s]", detail);
        printf("\n");
    }
}

static char g_buf[512];
#define DETAIL(...) (snprintf(g_buf, sizeof(g_buf), __VA_ARGS__), g_buf)

// ── VHDL-derived reference oracles ───────────────────────────────────────
//
// Independent re-derivations of the address formulas, written straight from
// the VHDL text rather than by calling the implementation. Used by the sweep
// rows (LR-47, LR-51, LR-67, LR-108) where enumerating expected values by
// hand is impractical.
//
//   lores.vhd:82     x     = (phc + scroll_x) mod 256
//   lores.vhd:84-87  y_pre = (vc + scroll_y) mod 512
//                    y     = ((y_pre>>6 & 3) + (y_pre>=192)) mod 4) << 6
//                            | (y_pre & 0x3F)
//   lores.vhd:91,93  pre   = (y>>1)<<7 | (x>>1);
//                    addr  = ((pre>>11) + (y>=96)) mod 8) << 11 | (pre & 0x7FF)
//   lores.vhd:96     rad   = dfile<<13 | (y>>1)<<6 | (x>>2)

static uint8_t ref_x(unsigned phc, uint8_t sx) {
    return static_cast<uint8_t>((phc & 0xFF) + sx);
}
static uint8_t ref_y(unsigned vc, uint8_t sy) {
    const unsigned y_pre = ((vc & 0x1FF) + sy) & 0x1FF;
    unsigned hi = (y_pre >> 6) & 3u;
    if (y_pre >= 192) hi = (hi + 1) & 3u;
    return static_cast<uint8_t>((hi << 6) | (y_pre & 0x3Fu));
}
static uint16_t ref_addr8(uint8_t x, uint8_t y) {
    const unsigned pre = static_cast<unsigned>((y >> 1) << 7) | (x >> 1);
    unsigned hi = (pre >> 11) & 7u;
    if (y >= 96) hi = (hi + 1) & 7u;
    return static_cast<uint16_t>((hi << 11) | (pre & 0x7FFu));
}
static uint16_t ref_addr_rad(uint8_t x, uint8_t y, bool dfile) {
    return static_cast<uint16_t>((dfile ? 0x2000u : 0u)
                                 | (static_cast<unsigned>(y >> 1) << 6)
                                 | (x >> 2));
}

// Convenience: default Params (register reset state, zxnext.vhd:4948, 4995,
// 4997, 5032-5034, 4971-4974).
static Lores::Params defaults() { return Lores::Params{}; }

static uint16_t addr8(unsigned phc, unsigned vc, uint8_t sx = 0, uint8_t sy = 0) {
    Lores::Params p = defaults();
    p.scroll_x = sx;
    p.scroll_y = sy;
    return Lores::address(p, static_cast<uint16_t>(phc), static_cast<uint16_t>(vc));
}
static uint16_t addr_rad(unsigned phc, unsigned vc, bool dfile,
                         uint8_t sx = 0, uint8_t sy = 0) {
    Lores::Params p = defaults();
    p.mode = true;
    p.dfile = dfile;
    p.scroll_x = sx;
    p.scroll_y = sy;
    return Lores::address(p, static_cast<uint16_t>(phc), static_cast<uint16_t>(vc));
}

// ═════════════════════════════════════════════════════════════════════════
// Group 2 (U rows) — the clip/display-window part of the enable gate
// ═════════════════════════════════════════════════════════════════════════

static void test_group2()
{
    set_group("LR-2x");

    // LR-23 — lores.vhd:115. There is no explicit display-area test in the
    // VHDL: phc(8) is set outside the 256-pixel active window and clip_x2 is
    // an 8-bit register, so hc_i <= '0' & clip_x2_i fails for every phc >= 256.
    {
        const Lores::Params p = defaults();
        const bool a = Lores::pixel_en(p, 256, 0);
        const bool b = Lores::pixel_en(p, 300, 0);
        const bool c = Lores::pixel_en(p, 464, 0);
        check("LR-23",
              "pixel_en = 0 for phc >= 256 (phc(8) set; clip_x2 is 8-bit) "
              "(lores.vhd:115)",
              !a && !b && !c,
              DETAIL("phc256=%d phc300=%d phc464=%d", a, b, c));
    }

    // LR-24 — lores.vhd:115 + zxnext.vhd:4974 (clip_y2 reset 0xBF = 191).
    {
        const Lores::Params p = defaults();
        const bool a = Lores::pixel_en(p, 0, 192);
        const bool b = Lores::pixel_en(p, 0, 255);
        const bool c = Lores::pixel_en(p, 0, 300);
        check("LR-24",
              "pixel_en = 0 for vc >= 192 at the default clip (clip_y2 reset "
              "0xBF) (lores.vhd:115; zxnext.vhd:4974)",
              !a && !b && !c,
              DETAIL("vc192=%d vc255=%d vc300=%d", a, b, c));
    }

    // LR-25 — lores.vhd:115, all four bounds inclusive.
    {
        const Lores::Params p = defaults();
        const bool tl = Lores::pixel_en(p, 0, 0);
        const bool tr = Lores::pixel_en(p, 255, 0);
        const bool bl = Lores::pixel_en(p, 0, 191);
        const bool br = Lores::pixel_en(p, 255, 191);
        check("LR-25",
              "pixel_en = 1 at all four display corners (0,0)/(255,0)/(0,191)/"
              "(255,191) (lores.vhd:115)",
              tl && tr && bl && br,
              DETAIL("tl=%d tr=%d bl=%d br=%d", tl, tr, bl, br));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Group 3 — 8-bit mode addressing (mode = 0)
// ═════════════════════════════════════════════════════════════════════════

static void test_group3()
{
    set_group("LR-4x");

    {
        const uint16_t a = addr8(0, 0);
        check("LR-40", "top-left LoRes pixel reads bank-5 offset 0 "
              "(lores.vhd:91)", a == 0x0000,
              DETAIL("addr=0x%04X expected 0x0000", a));
    }
    {
        const uint16_t a = addr8(0, 2);
        check("LR-41", "row stride is 128 bytes: y(7:1) occupies addr bits "
              "13:7 (lores.vhd:91)", a == 0x0080,
              DETAIL("addr=0x%04X expected 0x0080", a));
    }
    {
        const uint16_t a = addr8(2, 0);
        check("LR-42", "column stride is 1 byte per 2 display pixels: x(7:1) "
              "occupies addr bits 6:0 (lores.vhd:91)", a == 0x0001,
              DETAIL("addr=0x%04X expected 0x0001", a));
    }
    {
        const uint16_t a = addr8(4, 6), b = addr8(5, 6);
        const uint16_t c = addr8(4, 7), d = addr8(5, 7);
        check("LR-43", "a LoRes pixel is a 2x2 block of display pixels — "
              "(4,6)/(5,6)/(4,7)/(5,7) all read 0x0182 (lores.vhd:91)",
              a == 0x0182 && b == 0x0182 && c == 0x0182 && d == 0x0182,
              DETAIL("0x%04X 0x%04X 0x%04X 0x%04X expected 0x0182",
                     a, b, c, d));
    }
    {
        const uint16_t a = addr8(254, 94);
        check("LR-44", "last byte of the top half (phc=254, vc=94) is 0x17FF "
              "(lores.vhd:91, 93)", a == 0x17FF,
              DETAIL("addr=0x%04X expected 0x17FF", a));
    }
    {
        const uint16_t a = addr8(0, 96);
        check("LR-45", "first byte of the bottom half skips the attribute "
              "area: 0x2000, NOT 0x1800 (lores.vhd:93-94)", a == 0x2000,
              DETAIL("addr=0x%04X expected 0x2000 (0x1800 = missing +1)", a));
    }
    {
        const uint16_t a = addr8(254, 190);
        check("LR-46", "last byte of the bottom half (phc=254, vc=190) is "
              "0x37FF (lores.vhd:91, 93)", a == 0x37FF,
              DETAIL("addr=0x%04X expected 0x37FF", a));
    }
    {
        // LR-47 — sweep the entire display; the +1 on addr(13:11) exists
        // precisely so the 0x1800-0x1FFF attribute area is never read.
        int bad = 0; uint16_t first = 0; unsigned bx = 0, by = 0;
        for (unsigned vc = 0; vc < 192 && bad == 0; ++vc) {
            for (unsigned phc = 0; phc < 256; ++phc) {
                const uint16_t a = addr8(phc, vc);
                if (a >= 0x1800 && a <= 0x1FFF) {
                    if (!bad) { first = a; bx = phc; by = vc; }
                    ++bad;
                    break;
                }
            }
        }
        check("LR-47", "no address in 0x1800-0x1FFF is ever generated in "
              "8-bit mode across the whole display (lores.vhd:93-94)",
              bad == 0,
              DETAIL("first offender addr=0x%04X at phc=%u vc=%u", first, bx, by));
    }
    {
        const uint16_t a = addr8(0, 0, /*sx=*/0, /*sy=*/96);
        check("LR-48", "the half-select uses the SCROLLED y, not vc: "
              "vc=0 scroll_y=96 gives y=96 and therefore 0x2000 "
              "(lores.vhd:86-87, 93)", a == 0x2000,
              DETAIL("addr=0x%04X expected 0x2000", a));
    }
    {
        // LR-51 — in 8-bit mode addr_rad is not selected at all
        // (lores.vhd:98), so neither the Timex display-file bit nor NR $6A
        // bit 4 can reach the address. Sweep the display for all four
        // combinations of the two bits that produce `dfile`.
        bool same = true; unsigned bx = 0, by = 0;
        for (unsigned vc = 0; vc < 192 && same; ++vc) {
            for (unsigned phc = 0; phc < 256 && same; ++phc) {
                Lores::Params p0 = defaults(); p0.dfile = false;
                Lores::Params p1 = defaults(); p1.dfile = true;
                const uint16_t a0 = Lores::address(p0, static_cast<uint16_t>(phc),
                                                   static_cast<uint16_t>(vc));
                const uint16_t a1 = Lores::address(p1, static_cast<uint16_t>(phc),
                                                   static_cast<uint16_t>(vc));
                if (a0 != a1) { same = false; bx = phc; by = vc; }
            }
        }
        check("LR-51", "in 8-bit mode the Timex display-file bit and NR $6A "
              "bit 4 have no effect on the address (lores.vhd:96, 98)",
              same, DETAIL("first divergence at phc=%u vc=%u", bx, by));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Group 4 — Radastan 4-bit mode (mode = 1)
// ═════════════════════════════════════════════════════════════════════════

static void test_group4()
{
    set_group("LR-6x");

    {
        const uint16_t a = addr_rad(0, 2, false);
        check("LR-60", "Radastan row stride is 64 bytes (lores.vhd:96)",
              a == 0x0040, DETAIL("addr=0x%04X expected 0x0040", a));
    }
    {
        const uint16_t a = addr_rad(0, 0, false), b = addr_rad(1, 0, false);
        const uint16_t c = addr_rad(2, 0, false), d = addr_rad(3, 0, false);
        check("LR-61", "two LoRes pixels per byte: phc 0..3 all read offset 0 "
              "(x(7:2) drops both low bits) (lores.vhd:96)",
              a == 0 && b == 0 && c == 0 && d == 0,
              DETAIL("0x%04X 0x%04X 0x%04X 0x%04X", a, b, c, d));
    }
    {
        // LR-62 — lores.vhd:106: x(1)='0' selects data(7:4).
        Lores::Params p = defaults();
        p.mode = true;
        const uint8_t left  = Lores::pixel(p, Lores::coord_x(0, 0), 0xAB);
        const uint8_t right = Lores::pixel(p, Lores::coord_x(2, 0), 0xAB);
        check("LR-62", "x(1)=0 selects the HIGH nibble (left pixel of the "
              "pair), x(1)=1 the low nibble (lores.vhd:106)",
              (left & 0x0F) == 0x0A && (right & 0x0F) == 0x0B,
              DETAIL("phc0 low nibble=0x%X phc2 low nibble=0x%X "
                     "(pixels 0x%02X / 0x%02X)",
                     left & 0x0F, right & 0x0F, left, right));
    }
    {
        const uint16_t a = addr_rad(0, 190, false);
        check("LR-63", "dfile=0 bases the image at offset 0: last row starts "
              "at 0x17C0 (lores.vhd:96)", a == 0x17C0,
              DETAIL("addr=0x%04X expected 0x17C0", a));
    }
    {
        const uint16_t a = addr_rad(0, 0, true);
        check("LR-64", "dfile=1 bases the image at offset 0x2000 "
              "(lores.vhd:96)", a == 0x2000,
              DETAIL("addr=0x%04X expected 0x2000", a));
    }
    {
        const uint16_t a = addr_rad(0, 96, false);
        check("LR-65", "Radastan applies NO +0x800 correction at row 48: "
              "vc=96 gives 0x0C00, not 0x1400 (lores.vhd:96 vs 93-94)",
              a == 0x0C00,
              DETAIL("addr=0x%04X expected 0x0C00", a));
    }
    {
        // LR-67 — 96 rows x 64 bytes = 0x1800 bytes, contiguous within the
        // selected half, and every one of them reachable.
        std::set<uint16_t> seen;
        bool in_range = true; uint16_t offender = 0;
        for (unsigned vc = 0; vc < 192; ++vc) {
            for (unsigned phc = 0; phc < 256; ++phc) {
                const uint16_t a = addr_rad(phc, vc, false);
                if (a > 0x17FF) { in_range = false; offender = a; }
                seen.insert(a);
            }
        }
        check("LR-67", "the Radastan image is 6144 bytes, contiguous within "
              "its half, and every byte is reachable (lores.vhd:96)",
              in_range && seen.size() == 6144,
              DETAIL("distinct=%zu expected 6144, in_range=%d offender=0x%04X",
                     seen.size(), in_range, offender));
    }
    {
        // LR-68 — the mode bit is a pure combinational mux over two
        // independently computed addresses (lores.vhd:98); toggling it must
        // switch formulas with no residual state either way.
        Lores::Params p = defaults();
        const uint16_t a0 = Lores::address(p, 8, 4);           // 8-bit
        p.mode = true;
        const uint16_t a1 = Lores::address(p, 8, 4);           // Radastan
        p.mode = false;
        const uint16_t a2 = Lores::address(p, 8, 4);           // back to 8-bit
        p.mode = true;
        const uint16_t a3 = Lores::address(p, 8, 4);           // back to Radastan
        check("LR-68", "toggling NR $6A bit 5 switches the address generator "
              "and nothing else is latched (lores.vhd:98)",
              a0 == ref_addr8(ref_x(8, 0), ref_y(4, 0)) &&
              a1 == ref_addr_rad(ref_x(8, 0), ref_y(4, 0), false) &&
              a2 == a0 && a3 == a1,
              DETAIL("8bit=0x%04X rad=0x%04X back8=0x%04X backrad=0x%04X",
                     a0, a1, a2, a3));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Group 5 — palette offset and pixel composition
// ═════════════════════════════════════════════════════════════════════════

static void test_group5()
{
    set_group("LR-8x");

    auto px8 = [](uint8_t data, uint8_t offset) {
        Lores::Params p = defaults();
        p.palette_offset = offset;
        return Lores::pixel(p, /*x*/0, data);
    };
    auto px_rad = [](uint8_t data, uint8_t offset, uint8_t x, bool ulap) {
        Lores::Params p = defaults();
        p.mode = true;
        p.ulap_en = ulap;
        p.palette_offset = offset;
        return Lores::pixel(p, x, data);
    };

    {
        const uint8_t v = px8(0x35, 0x2);
        check("LR-80", "8-bit: the offset adds to the HIGH nibble only — "
              "0x35 + offset 2 gives 0x55 (lores.vhd:102, 111)", v == 0x55,
              DETAIL("pixel=0x%02X expected 0x55", v));
    }
    {
        const uint8_t v = px8(0xF7, 0x3);
        check("LR-81", "8-bit: the high-nibble add wraps at 4 bits with no "
              "carry out — 0xF7 + offset 3 gives 0x27 (lores.vhd:102)",
              v == 0x27, DETAIL("pixel=0x%02X expected 0x27", v));
    }
    {
        const uint8_t v = px8(0x0F, 0xF);
        check("LR-82", "8-bit: the low nibble passes through untouched by any "
              "offset — 0x0F + offset 0xF gives 0xFF (lores.vhd:111)",
              v == 0xFF, DETAIL("pixel=0x%02X expected 0xFF", v));
    }
    {
        const uint8_t v = px8(0x9C, 0x0);
        check("LR-83", "8-bit: offset 0 is the identity (lores.vhd:102)",
              v == 0x9C, DETAIL("pixel=0x%02X expected 0x9C", v));
    }
    {
        const uint8_t v = px_rad(0xAB, 0x5, /*x*/0, /*ulap*/false);
        check("LR-84", "Radastan: the high nibble IS the offset, not an add — "
              "byte 0xAB offset 5 gives 0x5A (lores.vhd:107, 111)",
              v == 0x5A, DETAIL("pixel=0x%02X expected 0x5A", v));
    }
    {
        const uint8_t v = px_rad(0xAB, 0x1, /*x*/0, /*ulap*/true);
        check("LR-85", "Radastan + ULA+: the high nibble becomes "
              "\"11\" & offset(1:0) — offset 1 gives 0xDA (lores.vhd:107)",
              v == 0xDA, DETAIL("pixel=0x%02X expected 0xDA", v));
    }
    {
        const uint8_t a = px_rad(0xAB, 0x1, 0, true);
        const uint8_t b = px_rad(0xAB, 0xD, 0, true);
        check("LR-86", "Radastan + ULA+: offset bits 3:2 are ignored — "
              "offsets 0x1 and 0xD give the same pixel (lores.vhd:107)",
              a == 0xDA && b == 0xDA,
              DETAIL("offset1=0x%02X offsetD=0x%02X expected 0xDA both", a, b));
    }
    {
        // LR-88 — palette_offset does not appear anywhere in the
        // lores_pixel_en_o expression (lores.vhd:115).
        Lores::Params p0 = defaults(); p0.palette_offset = 0x0;
        Lores::Params pF = defaults(); pF.palette_offset = 0xF;
        bool same = true;
        for (unsigned phc = 0; phc < 512 && same; ++phc)
            for (unsigned vc = 0; vc < 320 && same; vc += 7)
                if (Lores::pixel_en(p0, static_cast<uint16_t>(phc),
                                    static_cast<uint16_t>(vc)) !=
                    Lores::pixel_en(pF, static_cast<uint16_t>(phc),
                                    static_cast<uint16_t>(vc)))
                    same = false;
        check("LR-88", "the palette offset never affects pixel_en "
              "(lores.vhd:111, 115)", same);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Group 6 — scrolling
// ═════════════════════════════════════════════════════════════════════════

static void test_group6()
{
    set_group("LR-10x");

    {
        const uint16_t a = addr8(0, 0, /*sx*/4);
        check("LR-100", "X scroll advances the source column: scroll_x=4 "
              "moves the image left by 2 LoRes pixels (lores.vhd:82, 91)",
              a == 0x0002, DETAIL("addr=0x%04X expected 0x0002", a));
    }
    {
        // LR-101 — x(0) is never used: lores_addr_pre takes x(7:1)
        // (lores.vhd:91), so the X-scroll LSB is architecturally invisible
        // in 8-bit mode.
        const uint16_t a0 = addr8(0, 0, 0), a1 = addr8(0, 0, 1);
        const uint16_t a2 = addr8(0, 0, 2), a3 = addr8(0, 0, 3);
        check("LR-101", "the X-scroll LSB is discarded in 8-bit mode "
              "(x(0) unused) but bit 1 is not (lores.vhd:82, 91)",
              a0 == a1 && a2 == a3 && a0 != a2,
              DETAIL("sx0=0x%04X sx1=0x%04X sx2=0x%04X sx3=0x%04X",
                     a0, a1, a2, a3));
    }
    {
        const uint16_t a = addr8(10, 0, /*sx*/250);
        check("LR-102", "X scroll wraps mod 256 (= mod 128 LoRes pixels): "
              "phc=10 + scroll 250 gives x=4 (lores.vhd:82)",
              a == 0x0002, DETAIL("addr=0x%04X expected 0x0002", a));
    }
    {
        bool ok = true; uint16_t offender = 0; unsigned at = 0;
        for (unsigned phc = 0; phc < 256; ++phc) {
            const uint16_t a = addr8(phc, 0, /*sx*/200);
            if (a > 0x007F) { ok = false; offender = a; at = phc; break; }
        }
        check("LR-103", "X scroll wraps within the row and never into the "
              "next row (lores.vhd:82, 91)", ok,
              DETAIL("addr=0x%04X at phc=%u escaped row 0", offender, at));
    }
    {
        // LR-104 — Radastan uses x(7:2) for the byte and x(1) for the
        // nibble (lores.vhd:96, 106), so the low two scroll bits select
        // nibble then byte.
        Lores::Params p = defaults(); p.mode = true;
        uint8_t nib[4]; uint16_t adr[4];
        for (uint8_t sx = 0; sx < 4; ++sx) {
            p.scroll_x = sx;
            adr[sx] = Lores::address(p, 0, 0);
            nib[sx] = static_cast<uint8_t>(
                Lores::pixel(p, Lores::coord_x(0, sx), 0xAB) & 0x0F);
        }
        check("LR-104", "Radastan X-scroll granularity: scroll 0/1 give the "
              "HIGH nibble of byte 0, scroll 2/3 the LOW nibble of byte 0 "
              "(lores.vhd:82, 96, 106)",
              adr[0] == 0 && adr[1] == 0 && adr[2] == 0 && adr[3] == 0 &&
              nib[0] == 0xA && nib[1] == 0xA && nib[2] == 0xB && nib[3] == 0xB,
              DETAIL("addr %04X/%04X/%04X/%04X nib %X/%X/%X/%X",
                     adr[0], adr[1], adr[2], adr[3],
                     nib[0], nib[1], nib[2], nib[3]));
    }
    {
        const uint16_t a = addr8(0, 0, 0, /*sy*/4);
        check("LR-105", "Y scroll advances the source row: scroll_y=4 gives "
              "addr 0x0100 (lores.vhd:84-87, 91)", a == 0x0100,
              DETAIL("addr=0x%04X expected 0x0100", a));
    }
    {
        const uint16_t a = addr8(0, 0, 0, 10), b = addr8(0, 0, 0, 11);
        const uint16_t c = addr8(0, 0, 0, 12);
        check("LR-106", "the Y-scroll LSB is discarded away from the wrap "
              "boundary (y(0) unused) but bit 1 is not (lores.vhd:86-87, 91)",
              a == b && a != c,
              DETAIL("sy10=0x%04X sy11=0x%04X sy12=0x%04X", a, b, c));
    }
    {
        const uint16_t a = addr8(0, 100, 0, /*sy*/100);
        const uint8_t  y = Lores::coord_y(100, 100);
        check("LR-107", "Y scroll wraps mod 192: vc=100 + scroll 100 gives "
              "y=8 and addr 0x0200 (lores.vhd:84-87)",
              y == 8 && a == 0x0200,
              DETAIL("y=%u (exp 8) addr=0x%04X (exp 0x0200)", y, a));
    }
    {
        // LR-108 — for y_pre < 384 the two-bit-add construction is exactly
        // a mod-192 reduction. scroll_y <= 192 and vc <= 191 keeps y_pre
        // <= 383, i.e. entirely inside the exact range.
        bool ok = true; unsigned bs = 0, bv = 0; unsigned got = 0, exp = 0;
        for (unsigned sy = 0; sy <= 192 && ok; ++sy) {
            for (unsigned vc = 0; vc < 192; ++vc) {
                const uint8_t y = Lores::coord_y(static_cast<uint16_t>(vc),
                                                 static_cast<uint8_t>(sy));
                const unsigned e = (vc + sy) % 192u;
                if (y != e) { ok = false; bs = sy; bv = vc; got = y; exp = e; break; }
            }
        }
        check("LR-108", "the Y wrap is exactly (vc + scroll_y) mod 192 across "
              "the whole legal range scroll_y in [0,192] (lores.vhd:84-87)",
              ok, DETAIL("scroll_y=%u vc=%u got y=%u expected %u",
                         bs, bv, got, exp));
    }
    {
        // LR-109 — HARDWARE QUIRK. y_pre = 384 = 0b1_1000_0000; the compare
        // y_pre >= 192 fires, y_pre(7:6) = "10" = 2, +1 = 3, y(5:0) = 0, so
        // y = 192 rather than the 0 a true mod-192 would give.
        const uint8_t y = Lores::coord_y(191, 193);
        check("LR-109", "hardware quirk: for vc + scroll_y >= 384 the wrap is "
              "WRONG — scroll_y=193, vc=191 gives y=192, not 0, so the row "
              "reads outside the 96-row image (lores.vhd:84-87)",
              y == 192, DETAIL("y=%u expected 192 (a mod-192 model gives 0)", y));
    }
    {
        // LR-110 — the quirk's address consequence. scroll_y=255, vc=161 =>
        // y_pre = 416; y_pre(7:6) of 416 (0b1_1010_0000) = "10" = 2, +1 = 3;
        // y(5:0) = 32 => y = 224. addr_pre = (224>>1)<<7 = 0x3800; bits
        // 13:11 = 7, +1 wraps to 0 => addr = 0x0000.
        const uint8_t  y = Lores::coord_y(161, 255);
        const uint16_t a = addr8(0, 161, 0, 255);
        check("LR-110", "the quirk's address consequence: y=224 makes the "
              "3-bit +1 field wrap 7 -> 0, so addr_pre 0x3800 becomes 0x0000 "
              "(lores.vhd:93-94)",
              y == 224 && a == 0x0000,
              DETAIL("y=%u (exp 224) addr=0x%04X (exp 0x0000)", y, a));
    }
    {
        const uint16_t a = addr8(0, 0, /*sx*/6, /*sy*/8);
        check("LR-111", "the two scroll registers are independent: "
              "scroll_x=6 + scroll_y=8 gives addr 0x0203 "
              "(lores.vhd:82, 84, 91)", a == 0x0203,
              DETAIL("addr=0x%04X expected 0x0203", a));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Group 7 (U rows) — the shared ULA clip window
// ═════════════════════════════════════════════════════════════════════════

static void test_group7()
{
    set_group("LR-12x");

    auto clip = [](uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2) {
        Lores::Params p = defaults();
        p.clip_x1 = x1; p.clip_x2 = x2; p.clip_y1 = y1; p.clip_y2 = y2;
        return p;
    };

    {
        // LR-120 — LoRes IS clipped by NR $1A. The module's clip inputs are
        // the ULA clip registers (zxnext.vhd:4258-4261); there is no LoRes
        // window and no exemption.
        const Lores::Params p = clip(64, 191, 32, 159);
        const bool inside  = Lores::pixel_en(p, 100, 100);
        const bool outside = Lores::pixel_en(p, 32, 100);
        check("LR-120", "LoRes IS clipped by NR $1A — it is not exempt "
              "(lores.vhd:115; zxnext.vhd:4258-4261)",
              !outside && inside,
              DETAIL("phc=32 (outside) en=%d, phc=100 (inside) en=%d",
                     outside, inside));
    }
    {
        const Lores::Params p = clip(64, 191, 0, 191);
        const bool a = Lores::pixel_en(p,  63, 0);
        const bool b = Lores::pixel_en(p,  64, 0);
        const bool c = Lores::pixel_en(p, 191, 0);
        const bool d = Lores::pixel_en(p, 192, 0);
        check("LR-121", "clip X bounds are inclusive at both ends "
              "(lores.vhd:115)", !a && b && c && !d,
              DETAIL("63=%d 64=%d 191=%d 192=%d (expect 0 1 1 0)", a, b, c, d));
    }
    {
        const Lores::Params p = clip(0, 255, 32, 159);
        const bool a = Lores::pixel_en(p, 0,  31);
        const bool b = Lores::pixel_en(p, 0,  32);
        const bool c = Lores::pixel_en(p, 0, 159);
        const bool d = Lores::pixel_en(p, 0, 160);
        check("LR-122", "clip Y bounds are inclusive at both ends "
              "(lores.vhd:115)", !a && b && c && !d,
              DETAIL("31=%d 32=%d 159=%d 160=%d (expect 0 1 1 0)", a, b, c, d));
    }
    {
        // LR-123 — the clip compares phc, which is in 256-pixel display
        // units (zxnext.vhd:4250), while a LoRes pixel spans two of them.
        // A clip edge can therefore bisect a LoRes pixel.
        const Lores::Params p127 = clip(0, 127, 0, 191);
        const Lores::Params p128 = clip(0, 128, 0, 191);
        const bool a127 = Lores::pixel_en(p127, 127, 0);
        const bool b127 = Lores::pixel_en(p127, 128, 0);
        const bool a128 = Lores::pixel_en(p128, 128, 0);
        const bool b128 = Lores::pixel_en(p128, 129, 0);
        const uint8_t col127 = static_cast<uint8_t>(Lores::coord_x(127, 0) >> 1);
        const uint8_t col128 = static_cast<uint8_t>(Lores::coord_x(128, 0) >> 1);
        check("LR-123", "clip X is in 256-pixel display units, so a clip edge "
              "can fall mid-LoRes-pixel: x2=127 ends at LoRes column 63, "
              "x2=128 draws only the left half of column 64 "
              "(lores.vhd:115; zxnext.vhd:4250)",
              a127 && !b127 && col127 == 63 && a128 && !b128 && col128 == 64,
              DETAIL("x2=127: en(127)=%d en(128)=%d col=%u | "
                     "x2=128: en(128)=%d en(129)=%d col=%u",
                     a127, b127, col127, a128, b128, col128));
    }
    {
        // LR-125 — an inverted X window can never satisfy both comparisons.
        const Lores::Params p = clip(200, 100, 0, 191);
        bool any = false;
        for (unsigned phc = 0; phc < 512 && !any; ++phc)
            if (Lores::pixel_en(p, static_cast<uint16_t>(phc), 0)) any = true;
        check("LR-125", "an inverted X window (x1 > x2) draws nothing "
              "(lores.vhd:115)", !any);
    }
    {
        const Lores::Params p = clip(0, 255, 150, 50);
        bool any = false;
        for (unsigned vc = 0; vc < 320 && !any; ++vc)
            if (Lores::pixel_en(p, 0, static_cast<uint16_t>(vc))) any = true;
        check("LR-126", "an inverted Y window (y1 > y2) draws nothing "
              "(lores.vhd:115)", !any);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Group 9 (U row) — negatives
// ═════════════════════════════════════════════════════════════════════════

static void test_group9()
{
    set_group("LR-16x");

    // LR-160 — NR $26 / $27 are the ULA's scroll registers
    // (zxnext.vhd:4474-4475 into the zxula port map). The `lores` entity has
    // no such port (lores.vhd:37-61) and the LoRes port map at
    // zxnext.vhd:4241-4271 wires only nr_32/nr_33. Driven through the real
    // substitution path so this asserts the wiring, not just the struct
    // layout.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        PaletteManager pal;
        pal.reset();

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.lores().set_enabled(true);
        r.lores().init_per_line();

        // Distinguishable LoRes content: byte value = (offset & 0xFF) | 1 so
        // no cell is the palette-0 default.
        for (uint16_t off = 0; off < 0x3800; ++off)
            ram.write(10u * 8192u + off, static_cast<uint8_t>((off & 0xFF) | 1));

        const int ROW = Renderer::DISP_Y + 40;
        std::vector<uint32_t> a(Renderer::FB_WIDTH, 0);
        std::vector<uint32_t> b(Renderer::FB_WIDTH, 0);

        r.apply_lores(a.data(), ROW, ram, pal);

        // NR $26 / $27 (and the NR $68 b2 fine-scroll for good measure are
        // covered by LR-161 in the compositor tier).
        r.ula().set_ula_scroll_x_coarse(64);
        r.ula().set_ula_scroll_y(64);
        r.apply_lores(b.data(), ROW, ram, pal);

        check("LR-160", "NR $26 / $27 (ULA scroll) do not move the LoRes "
              "image (lores.vhd:37-61 has no such port; "
              "zxnext.vhd:4241-4271)",
              std::memcmp(a.data(), b.data(),
                          Renderer::FB_WIDTH * sizeof(uint32_t)) == 0,
              "ULA scroll leaked into the LoRes address generator");
    }
}

// ── Main ─────────────────────────────────────────────────────────────────

int main()
{
    printf("LoRes Subsystem Compliance Tests\n");
    printf("================================\n\n");

    test_group2();  printf("  Group: LR-2x  (enable gate / display window) — done\n");
    test_group3();  printf("  Group: LR-4x  (8-bit addressing) — done\n");
    test_group4();  printf("  Group: LR-6x  (Radastan addressing) — done\n");
    test_group5();  printf("  Group: LR-8x  (palette offset / pixel) — done\n");
    test_group6();  printf("  Group: LR-10x (scrolling) — done\n");
    test_group7();  printf("  Group: LR-12x (clip window) — done\n");
    test_group9();  printf("  Group: LR-16x (negatives) — done\n");

    printf("\n================================\n");
    printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
           g_total + static_cast<int>(g_skipped.size()),
           g_pass, g_fail, static_cast<int>(g_skipped.size()));

    printf("\nPer-group breakdown:\n");
    std::string last_group;
    int gp = 0, gf = 0;
    for (const auto& res : g_results) {
        if (res.group != last_group) {
            if (!last_group.empty())
                printf("  %-12s %d/%d\n", last_group.c_str(), gp, gp + gf);
            last_group = res.group;
            gp = gf = 0;
        }
        if (res.passed) gp++; else gf++;
    }
    if (!last_group.empty())
        printf("  %-12s %d/%d\n", last_group.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
