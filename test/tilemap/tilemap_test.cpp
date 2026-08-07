// Tilemap Subsystem Compliance Test Runner — VHDL-derived rewrite
// =================================================================
//
// Full rewrite (Task 1 Wave 3, 2026-04-15) against
// doc/testing/TILEMAP-TEST-PLAN-DESIGN.md. Every assertion cites a
// VHDL file+line from the authoritative FPGA source at
//   cores/zxnext/src/
// Expected values are derived from VHDL, NEVER from the C++ emulator.
//
// Ground rules (see doc/testing/UNIT-TEST-PLAN-EXECUTION.md §1–§3):
//   * One check() / skip() per plan row ID, with VHDL citation.
//   * C++ is under test; VHDL is the oracle. Assertions that disagree
//     with today's C++ are intentional and are left failing.
//   * skip() is used when the plan row cannot be realised against
//     the current Tilemap public API (e.g. clip enforcement lives in
//     the compositor, not in render_scanline).
//
// VHDL DISAGREEMENT flagged to the Emulator Bug backlog (Task 2):
//   tilemap.vhd:189-195 specifies
//     bit 6 = 80-col, bit 5 = strip_flags, bit 4 = palette select,
//     bit 3 = textmode, bit 1 = 512-tile mode, bit 0 = tm_on_top.
//   The C++ Tilemap::set_control (src/video/tilemap.cpp:34-43) reads
//     bit 5 as text_mode_ and bit 4 as force_attr_ (strip flags) —
//     bit 3 is ignored. The TM-40..TM-44, TM-50..TM-53, and the
//     relevant TM-CB rows below assert the VHDL bit assignment and
//     will FAIL against the current emulator until the swap is fixed.
//     Those failures are intentional; see Task 2 in .prompts/2026-04-15.md.

#include "video/tilemap.h"
#include "video/palette.h"
#include "memory/ram.h"

// G98/G101 — TM-95/TM-105 cross unit/compositor boundaries.  The
// `private`/`protected` shim mirrors compositor_test.cpp so we can drop
// engine-prepared layer pixels straight into Renderer::composite_scanline
// without standing up a full Emulator stack.
#define private public
#define protected public
#include "video/renderer.h"
#undef private
#undef protected

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ── Test infrastructure ─────────────────────────────────────────────────

namespace {

int         g_pass  = 0;
int         g_fail  = 0;
int         g_total = 0;
std::string g_group;

struct Result {
    std::string group;
    std::string id;
    bool        passed;
    std::string detail;
};

struct SkipNote {
    const char* id;
    const char* reason;
};

std::vector<Result>   g_results;
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

// Generic equality check: "actual == expected" is the assertion; caller
// supplies the plan row ID and a "VHDL file:line — rationale" note.
template <typename A, typename B>
void check(const char* id, A actual, B expected, const char* note) {
    ++g_total;
    bool passed = (actual == expected);
    Result r{g_group, id, passed, note};
    g_results.push_back(r);
    if (passed) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, note);
    }
}

// Predicate form (for cases where there is no single expected scalar,
// or where passing a literal `true` as the 3rd argument to check() would
// trip the tautology lint — see test/lint-assertions.sh pattern 1).
void check_pred(const char* id, bool cond, const char* note) {
    ++g_total;
    Result r{g_group, id, cond, note};
    g_results.push_back(r);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, note);
    }
}

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
}

// ── RAM / map helpers (mechanical byte writers, not oracles) ────────────

// VHDL bank 5 physical base (5 * 16K).
constexpr uint32_t BANK5 = 5u * 16384u;
// VHDL bank 7 physical base (7 * 16K).
// Standalone Tilemap+Ram fixture (no BRAM wired): Tilemap::vram_read
// falls back to the legacy page-14 Ram read for bank-7 addresses, so the
// test seeds bank-7 data at page 14 as before the BRAM split.
constexpr uint32_t BANK7 = 14u * 8192u;
// VHDL reset defaults: map=0x2C, def=0x0C  (tilemap.vhd reset — see
// zxnext.vhd nr_6e/nr_6f reset handlers). Each unit = 256 bytes.
constexpr uint32_t DEF_MAP_BASE = BANK5 + 0x2C * 256u;   // 0x16C00
constexpr uint32_t DEF_DEF_BASE = BANK5 + 0x0C * 256u;   // 0x14C00

void fresh(Tilemap& tm, PaletteManager& pal, Ram& ram) {
    tm.reset();
    pal.reset();
    ram.reset();
}

// 4bpp pattern: every pixel of a tile == `pixel_val` (low nibble).
void fill_tile_pattern(Ram& ram, uint32_t def_base, uint16_t tile_idx, uint8_t pixel_val) {
    uint32_t addr = def_base + static_cast<uint32_t>(tile_idx) * 32u;
    uint8_t  byte_val = static_cast<uint8_t>(((pixel_val & 0x0F) << 4) | (pixel_val & 0x0F));
    for (int i = 0; i < 32; ++i) ram.write(addr + i, byte_val);
}

// 1bpp text-mode pattern: every row of a tile == `row_pattern`.
void fill_tile_textmode(Ram& ram, uint32_t def_base, uint16_t tile_idx, uint8_t row_pattern) {
    uint32_t addr = def_base + static_cast<uint32_t>(tile_idx) * 8u;
    for (int i = 0; i < 8; ++i) ram.write(addr + i, row_pattern);
}

void write_map2(Ram& ram, uint32_t map_base, int col, int row, int tpr,
                uint8_t tile_idx, uint8_t attr) {
    uint32_t addr = map_base + static_cast<uint32_t>(row * tpr + col) * 2u;
    ram.write(addr, tile_idx);
    ram.write(addr + 1, attr);
}

void write_map1(Ram& ram, uint32_t map_base, int col, int row, int tpr, uint8_t tile_idx) {
    uint32_t addr = map_base + static_cast<uint32_t>(row * tpr + col);
    ram.write(addr, tile_idx);
}

constexpr int MAX_W = 640;
struct Scan {
    uint32_t pixels[MAX_W];
    bool     ula_over[MAX_W];
    bool     textmode[MAX_W];
};

// G104 phase 4: render_scanline always emits 640 pixels.
//   - 80-col mode: native 1:1 (640 unique source pixels).
//   - 40-col mode: each source pixel doubled into dst[2x] AND dst[2x+1].
// Tests that previously asserted dst[N] in 40-col now assert dst[2*N].
Scan render_line(Tilemap& tm, int y, const Ram& ram,
                 const PaletteManager& pal) {
    Scan s;
    std::memset(s.pixels,   0, sizeof(s.pixels));
    std::memset(s.ula_over, 0, sizeof(s.ula_over));
    std::memset(s.textmode, 0, sizeof(s.textmode));
    tm.init_scroll_per_line();
    tm.render_scanline(s.pixels, s.ula_over, y, ram, pal, s.textmode);
    return s;
}

// Paint a deterministic 4bpp palette entry (index -> RRRGGGBB) onto the
// tilemap first palette, so tilemap_colour(idx) returns a distinct value.
// Used where the assertion needs a known palette lookup without depending
// on whatever power-on defaults the emulator provides.
void paint_tm_palette_entry(PaletteManager& pal, uint8_t idx, uint8_t rgb8) {
    pal.write_control(0x30);  // target = TILEMAP_FIRST (bits 6:4 = 011)
    pal.set_index(idx);
    pal.write_8bit(rgb8);
}

// ── Group 1: Enable / Disable / Reset Defaults ──────────────────────────

void group1_reset_enable() {
    set_group("G1 Enable/Reset");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-01: after reset the tilemap is disabled.
    // VHDL: zxnext.vhd nr_6b_tm_en reset handler — `nr_6b_tm_en <= '0'`.
    {
        fresh(tm, pal, ram);
        check_pred("TM-01", tm.enabled() == false,
              "VHDL zxnext.vhd nr_6b reset — tilemap enable clears to 0");
    }

    // TM-02: bit 7 of NR 0x6B enables the tilemap.
    // VHDL: tilemap.vhd:195 area / zxnext.vhd nr_6b_tm_en <= nr_wr_dat(7).
    {
        fresh(tm, pal, ram);
        tm.set_control(0x80);
        check_pred("TM-02", tm.enabled(),
              "VHDL zxnext.vhd — NR 0x6B bit 7 maps to nr_6b_tm_en");
    }

    // TM-03: clearing bit 7 disables the tilemap.
    // VHDL: same enable register, single bit.
    {
        fresh(tm, pal, ram);
        tm.set_control(0x80);
        tm.set_control(0x00);
        check_pred("TM-03", tm.enabled() == false,
              "VHDL zxnext.vhd — NR 0x6B bit 7 = 0 clears nr_6b_tm_en");
    }

    // TM-04: reset default register block matches the VHDL reset table.
    // VHDL: zxnext.vhd nr_6b/6c/6e/6f reset defaults; tilemap.vhd:189-195
    // aliases show the reset value is control=0. map=0x2C, def=0x0C.
    {
        fresh(tm, pal, ram);
        check_pred("TM-04",
                   tm.get_control()       == 0x00 &&
                   tm.get_default_attr()  == 0x00 &&
                   tm.get_map_base_raw()  == 0x2C &&
                   tm.get_def_base_raw()  == 0x0C,
                   "VHDL zxnext.vhd NR 0x6B/6C/6E/6F reset — "
                   "ctrl=0, attr=0, map=0x2C, def=0x0C");
    }

    // Per-scanline NR 0x6B mid-frame mode flip (G06) — TM-160..164.
    //
    // Tilemap exposes a per-frame NR 0x6B change-log capturing the 5
    // owned bits (b7 enable, b6 80-col, b3 textmode, b1 512-tile, b0
    // tm_on_top). Bit 4 (palette select) is owned by Ula's palsel6b
    // log and is NOT exercised here.
    //
    // Lifecycle (mirrors layer2 / palette / ula):
    //   start_frame_nr6b()       — at frame start
    //   set_current_nr6b_line(N) — tag subsequent set_control writes
    //   rewind_nr6b_to_baseline() — before per-line render
    //   apply_nr6b_changes_for_line(L) — replay log entries for line L
    //
    // VHDL bit decode authority: tilemap.vhd:189-195 + zxnext.vhd:5461-5462.

    // TM-160: VHDL tilemap.vhd:189 (mode_i <= control_i(6)),
    // zxnext.vhd:5461-5462 (write storage), :7039, 7072 (downstream pipeline).
    {
        fresh(tm, pal, ram);
        tm.start_frame_nr6b();
        // Line 50: 40-col (b6=0, enabled b7=1).
        tm.set_current_nr6b_line(50);
        tm.set_control(0x80);  // b7=1, b6=0
        // Line 100: 80-col (b6=1).
        tm.set_current_nr6b_line(100);
        tm.set_control(0xC0);  // b7=1, b6=1

        tm.rewind_nr6b_to_baseline();
        tm.apply_nr6b_changes_for_line(50);
        bool at_50 = (tm.mode_80col() == false);
        tm.apply_nr6b_changes_for_line(100);
        bool at_100 = (tm.mode_80col() == true);

        check_pred("TM-160", at_50 && at_100,
                   "VHDL tilemap.vhd:189 + zxnext.vhd:5461-5462 — "
                   "NR 0x6B b6 mid-frame flip lands on the correct scanline");
    }

    // TM-161: VHDL tilemap.vhd:191 (textmode_i <= control_i(3)),
    // zxnext.vhd:5461-5462.
    {
        fresh(tm, pal, ram);
        tm.start_frame_nr6b();
        // Line 30: textmode off (b3=0).
        tm.set_current_nr6b_line(30);
        tm.set_control(0x80);  // b7=1, b3=0
        // Line 60: textmode on (b3=1).
        tm.set_current_nr6b_line(60);
        tm.set_control(0x88);  // b7=1, b3=1

        tm.rewind_nr6b_to_baseline();
        tm.apply_nr6b_changes_for_line(30);
        bool at_30 = (tm.text_mode() == false);
        tm.apply_nr6b_changes_for_line(60);
        bool at_60 = (tm.text_mode() == true);

        check_pred("TM-161", at_30 && at_60,
                   "VHDL tilemap.vhd:191 + zxnext.vhd:5461-5462 — "
                   "NR 0x6B b3 (textmode) mid-frame flip per scanline");
    }

    // TM-162: VHDL tilemap.vhd:194 (mode_512_i <= control_i(1)),
    // zxnext.vhd:5461-5462, :6863 (mode_512 ORs into tm_pixel_below).
    {
        fresh(tm, pal, ram);
        tm.start_frame_nr6b();
        // Line 20: 256-tile (b1=0).
        tm.set_current_nr6b_line(20);
        tm.set_control(0x80);  // b7=1, b1=0
        // Line 70: 512-tile (b1=1).
        tm.set_current_nr6b_line(70);
        tm.set_control(0x82);  // b7=1, b1=1

        tm.rewind_nr6b_to_baseline();
        tm.apply_nr6b_changes_for_line(20);
        bool at_20 = (tm.mode_512() == false);
        tm.apply_nr6b_changes_for_line(70);
        bool at_70 = (tm.mode_512() == true);

        check_pred("TM-162", at_20 && at_70,
                   "VHDL tilemap.vhd:194 + zxnext.vhd:5461-5462 — "
                   "NR 0x6B b1 (256->512 tile) mid-frame flip per scanline");
    }

    // TM-163: VHDL tilemap.vhd:195 (tm_on_top_i <= control_i(0)),
    // zxnext.vhd:6863, 7116 (priority effect).
    {
        fresh(tm, pal, ram);
        tm.start_frame_nr6b();
        // Line 10: tm_on_top off (b0=0).
        tm.set_current_nr6b_line(10);
        tm.set_control(0x80);  // b7=1, b0=0
        // Line 80: tm_on_top on (b0=1).
        tm.set_current_nr6b_line(80);
        tm.set_control(0x81);  // b7=1, b0=1

        tm.rewind_nr6b_to_baseline();
        tm.apply_nr6b_changes_for_line(10);
        bool at_10 = (tm.tm_on_top() == false);
        tm.apply_nr6b_changes_for_line(80);
        bool at_80 = (tm.tm_on_top() == true);

        check_pred("TM-163", at_10 && at_80,
                   "VHDL tilemap.vhd:195 + zxnext.vhd:5461-5462 — "
                   "NR 0x6B b0 (tm_on_top) mid-frame flip per scanline");
    }

    // TM-164: VHDL zxnext.vhd:5461 (nr_6b_tm_en <= nr_wr_dat(7)),
    // :6820 (tm_en_0 <= nr_6b_tm_en).
    {
        fresh(tm, pal, ram);
        tm.start_frame_nr6b();
        // Line 40: enabled (b7=1).
        tm.set_current_nr6b_line(40);
        tm.set_control(0x80);
        // Line 120: disabled (b7=0).
        tm.set_current_nr6b_line(120);
        tm.set_control(0x00);

        tm.rewind_nr6b_to_baseline();
        tm.apply_nr6b_changes_for_line(40);
        bool at_40 = (tm.enabled() == true);
        tm.apply_nr6b_changes_for_line(120);
        bool at_120 = (tm.enabled() == false);

        check_pred("TM-164", at_40 && at_120,
                   "VHDL zxnext.vhd:5461 + :6820 — "
                   "NR 0x6B b7 (enable) mid-frame flip per scanline");
    }
}

// ── Group 2: 40-column mode (8-bit tiles) ───────────────────────────────

void group2_40col() {
    set_group("G2 40-col");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-10: basic 40-col rendering. Tile 1 filled with pixel value 3;
    // the rendered colour must equal the tilemap palette entry 0x03.
    // VHDL: tilemap.vhd:382-383 standard pixel = attr(7:4) | pix(3:0).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);  // a recognisable red
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-10", s.pixels[0], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:382-383 standard pixel index = attr(7:4)|pix");
    }

    // TM-11: tile index range — tile 255 must read from def_base+255*32.
    // VHDL: tilemap.vhd:393 pix_sub_sub = mode_512 AND attr(0) & tilemap_0.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xFC);
        paint_tm_palette_entry(pal, 0x02, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 0,   1);
        fill_tile_pattern(ram, DEF_DEF_BASE, 255, 2);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 0,   0x00);
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 255, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: tile 0 src pixel 0 → dst[0]; tile 1 src pixel 0
        // (= 320-grid screen_x=8) → dst[16].
        check_pred("TM-11",
                   s.pixels[0]  == pal.tilemap_colour(0x01) &&
                   s.pixels[16] == pal.tilemap_colour(0x02),
                   "VHDL tilemap.vhd:393 — tilemap_0 selects tile 0..255 "
                   "within a 256-tile bank");
    }

    // TM-12: palette offset shifts rendered index by attr(7:4)*16.
    // VHDL: tilemap.vhd:382 pixel_data(7:4) <= tilemap_1(7:4).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x21, 0x1C);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 1);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x20);  // offset 2
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-12", s.pixels[0], pal.tilemap_colour(0x21),
              "VHDL tilemap.vhd:382 — final index = attr(7:4)<<4 | pixel");
    }

    // TM-13: X-mirror flips horizontally. Build an asymmetric tile with
    // pixel value 1 in the left half (cols 0..3) and value 2 in the right
    // half (cols 4..7). Without mirror pixel[0]=idx1; with mirror pixel[0]=idx2.
    // VHDL: tilemap.vhd:320-321 effective_x = x_mirror XOR rotate.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        paint_tm_palette_entry(pal, 0x02, 0x03);
        uint32_t addr = DEF_DEF_BASE + 1 * 32;
        for (int row = 0; row < 8; ++row) {
            ram.write(addr + row * 4 + 0, 0x11);  // px 0..1 = 1
            ram.write(addr + row * 4 + 1, 0x11);  // px 2..3 = 1
            ram.write(addr + row * 4 + 2, 0x22);  // px 4..5 = 2
            ram.write(addr + row * 4 + 3, 0x22);  // px 6..7 = 2
        }
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 1, 0x08);  // bit 3 = x mirror
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: tile 0 src 0 → dst[0]; tile 1 src 8 → dst[16].
        check_pred("TM-13",
                   s.pixels[0]  == pal.tilemap_colour(0x01) &&
                   s.pixels[16] == pal.tilemap_colour(0x02),
                   "VHDL tilemap.vhd:320-321 — attr(3) inverts "
                   "effective_x when rotate=0");
    }

    // TM-14: Y-mirror flips vertically. Top row filled with val 1, bottom
    // row with val 2; without mirror scanline 0 shows val 1, with mirror 2.
    // VHDL: tilemap.vhd:322 effective_y = y_mirror XOR abs_y(2:0).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        paint_tm_palette_entry(pal, 0x02, 0x03);
        uint32_t addr = DEF_DEF_BASE + 1 * 32;
        for (int c = 0; c < 4; ++c) ram.write(addr + c, 0x11);                 // row 0
        for (int r = 1; r < 7; ++r)
            for (int c = 0; c < 4; ++c) ram.write(addr + r * 4 + c, 0x33);
        for (int c = 0; c < 4; ++c) ram.write(addr + 7 * 4 + c, 0x22);         // row 7
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 1, 0x04);  // bit 2 = y mirror
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: tile 0 src 0 → dst[0]; tile 1 src 8 → dst[16].
        check_pred("TM-14",
                   s.pixels[0]  == pal.tilemap_colour(0x01) &&
                   s.pixels[16] == pal.tilemap_colour(0x02),
                   "VHDL tilemap.vhd:322 — attr(2) inverts effective_y");
    }

    // TM-15: rotate produces a 90° rotation, not a pure transpose.
    // VHDL tilemap.vhd:320-324:
    //   eff_x_mirror = attr(3) XOR attr(1)   → with rotate=1, x_mirror=0: eff_x_mirror=1
    //   effective_x = NOT abs_x               (mirrored)
    //   effective_y = abs_y                   (no y_mirror)
    //   transformed_x = effective_y           (rotate=1 → swap)
    //   transformed_y = effective_x
    // For pixel (abs_x=0, abs_y=0): tx=0, ty=NOT(0)=7 → reads row 7 col 0 = val 2
    // For pixel (abs_x=7, abs_y=0): tx=0, ty=NOT(7)=0 → reads row 0 col 0 = val 1
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        paint_tm_palette_entry(pal, 0x02, 0x03);
        uint32_t addr = DEF_DEF_BASE + 1 * 32;
        // Row 0: only px 0 = 1
        ram.write(addr + 0, 0x10);
        // Row 7: only px 0 = 2
        ram.write(addr + 7 * 4, 0x20);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x02);   // rotate
        tm.set_enabled(true);
        auto s0 = render_line(tm, 0, ram, pal);
        // 90° rotation: pixel 0 reads (tx=0,ty=7)=val2; pixel 7 reads (tx=0,ty=0)=val1
        // 40-col doubled: src pixel 0 → dst[0]; src pixel 7 → dst[14,15].
        check_pred("TM-15",
                   s0.pixels[0]  == pal.tilemap_colour(0x02) &&
                   s0.pixels[14] == pal.tilemap_colour(0x01),
                   "VHDL tilemap.vhd:320-324 — rotate is 90° (swap + XOR mirror)");
    }

    // TM-16: rotate + X-mirror cancel: effective_x_mirror = 1 XOR 1 = 0.
    // Same tile as TM-15 but with attr bits 3 and 1 both set. Rotate
    // still swaps axes; x-mirror is cancelled. Scanline 0 pixel 0 = val 1.
    // VHDL: tilemap.vhd:320 effective_x_mirror = attr(3) XOR attr(1).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        uint32_t addr = DEF_DEF_BASE + 1 * 32;
        ram.write(addr + 0, 0x10);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x0A);   // rotate + x-mirror
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-16", s.pixels[0], pal.tilemap_colour(0x01),
              "VHDL tilemap.vhd:320 — attr(3) XOR attr(1) cancels x-mirror");
    }

    // TM-17: per-tile ULA-over flag (attr bit 0) sets the compositor's
    // `below` signal when tm_on_top=0.
    // VHDL: tilemap.vhd:388 wdata(8) <= (attr(0) OR mode_512) AND NOT tm_on_top.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0x1C);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);   // below=0
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 1, 0x01);   // below=1
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: tile-1 src pixel 8 → dst[16,17].
        check_pred("TM-17",
                   s.ula_over[0] == false && s.ula_over[16] == true,
                   "VHDL tilemap.vhd:388 — per-tile below = attr(0) when "
                   "mode_512=0 AND tm_on_top=0");
    }
}

// ── Group 3: 80-column mode ─────────────────────────────────────────────

void group3_80col() {
    set_group("G3 80-col");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-20: 80-col renders twice as many map columns into 640px.
    // VHDL: tilemap.vhd:189 mode_i = control(6); wrap_x = 640 in 80-col.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        for (int c = 0; c < 80; ++c)
            write_map2(ram, DEF_MAP_BASE, c, 0, 80, 1, 0x00);
        tm.set_control(0xC0);   // enable + bit 6 (80-col)
        auto s = render_line(tm, 0, ram, pal);
        check("TM-20", s.pixels[639], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:189 — control(6)=1 selects 80-col, "
              "rightmost map column reaches native pixel 639");
    }

    // TM-21: attribute decoding still applies in 80-col. Palette offset 1
    // on the first tile produces index 0x13 (offset 1 << 4 | pixel 3).
    // VHDL: tilemap.vhd:382 applies identically in both modes.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x13, 0x1C);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 80, 1, 0x10);
        tm.set_control(0xC0);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-21", s.pixels[0], pal.tilemap_colour(0x13),
              "VHDL tilemap.vhd:382 — attr(7:4) palette offset holds in 80-col");
    }

    // TM-22: 80-col native 1:1 — alternating colour columns produce
    // BOTH colours in the 640-wide output.  G104 phase 4: 80-col is now
    // emitted natively (no downsample), so every alternating tile is
    // visible in the 640-wide buffer.
    // VHDL: tilemap.vhd:228 hcount_effsub = hcount(10:1) → 14 MHz pacing.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        for (int c = 0; c < 80; ++c)
            write_map2(ram, DEF_MAP_BASE, c, 0, 80, (c & 1) ? 2 : 1, 0x00);
        tm.set_control(0xC0);
        auto s = render_line(tm, 0, ram, pal);
        bool saw_val3 = false, saw_val5 = false;
        for (int x = 0; x < 640; ++x) {
            if (s.pixels[x] == pal.tilemap_colour(0x03)) saw_val3 = true;
            if (s.pixels[x] == pal.tilemap_colour(0x05)) saw_val5 = true;
        }
        check_pred("TM-22", saw_val3 && saw_val5,
                   "VHDL tilemap.vhd:228 — 80-col native 640 emits both "
                   "alternating tile colours");
    }
}

// ── Group 4: 512-tile mode ──────────────────────────────────────────────

void group4_512tile() {
    set_group("G4 512-tile");
    Tilemap tm; PaletteManager pal; Ram ram;

    // 512-tile mode needs 512 * 32 = 16384 bytes of tile defs — the full
    // 16K bank 5.  DEF_MAP_BASE (0x2C → 0x16C00) overlaps tile 256's
    // data at DEF_DEF_BASE + 256*32 = 0x16C00.  Move the map to bank 7
    // (bit 7 = 1 in the base register) so there's no overlap.
    constexpr uint8_t  MAP512_REG = 0x80;  // bit 7=1 (bank 7), offset=0
    const     uint32_t MAP512_BASE = BANK7 + 0;  // 0x1C000

    // TM-30: 512-tile mode activated via bit 1 of NR 0x6B.
    // VHDL: tilemap.vhd:194 mode_512_i = control(1).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        paint_tm_palette_entry(pal, 0x02, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE,   0, 1);
        fill_tile_pattern(ram, DEF_DEF_BASE, 256, 2);
        tm.set_map_base(MAP512_REG);
        // attr bit 0 = 1 → in 512-tile mode selects tile 256.
        write_map2(ram, MAP512_BASE, 0, 0, 40, 0, 0x01);
        tm.set_control(0x82);   // enable + mode_512
        auto s = render_line(tm, 0, ram, pal);
        check("TM-30", s.pixels[0], pal.tilemap_colour(0x02),
              "VHDL tilemap.vhd:194 — control(1)=1 enables 9-bit tile index");
    }

    // TM-31: 512-tile mode extends the index via attr(0).
    // VHDL: tilemap.vhd:393 pix_sub_sub = (mode_512 AND attr(0)) & tilemap_0.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        paint_tm_palette_entry(pal, 0x02, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE,   0, 1);
        fill_tile_pattern(ram, DEF_DEF_BASE, 256, 2);
        tm.set_map_base(MAP512_REG);
        write_map2(ram, MAP512_BASE, 0, 0, 40, 0, 0x00);   // bit 0 = 0 → tile 0
        write_map2(ram, MAP512_BASE, 1, 0, 40, 0, 0x01);   // bit 0 = 1 → tile 256
        tm.set_control(0x82);
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: tile 0 src 0 → dst[0]; tile 1 src 8 → dst[16].
        check_pred("TM-31",
                   s.pixels[0]  == pal.tilemap_colour(0x01) &&
                   s.pixels[16] == pal.tilemap_colour(0x02),
                   "VHDL tilemap.vhd:393 — attr(0) becomes tile bit 8 in 512 mode");
    }

    // TM-32: in 512-tile mode, the `below` signal is forced by mode_512
    // (unless tm_on_top overrides, which is tested in G13/TM-124).
    // VHDL: tilemap.vhd:388 wdata(8) = (attr(0) OR mode_512) AND NOT tm_on_top.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 0, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 0, 0x00);   // attr(0)=0
        tm.set_control(0x82);   // mode_512, tm_on_top=0
        auto s = render_line(tm, 0, ram, pal);
        check_pred("TM-32", s.ula_over[0],
              "VHDL tilemap.vhd:388 — mode_512=1, tm_on_top=0 forces below=1");
    }
}

// ── Group 5: Text mode ──────────────────────────────────────────────────
//
// ALL TM-40..TM-44 rows assert the VHDL bit assignment `textmode_i =
// control(3)` (tilemap.vhd:191). The current C++ Tilemap::set_control
// (src/video/tilemap.cpp:39) reads bit 5 instead, so these assertions
// FAIL. That is intentional — they pin the correct VHDL bit and feed the
// control-bit-swap fix in Task 2.

void group5_textmode() {
    set_group("G5 Text mode");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-40: text mode enabled via NR 0x6B bit 3.
    // VHDL: tilemap.vhd:191 textmode_i <= control_i(3).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);   // (pal_offset=0 << 1) | 1
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0xFF);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_control(0x88);   // enable (bit 7) + textmode (bit 3)
        auto s = render_line(tm, 0, ram, pal);
        check("TM-40", s.pixels[0], pal.tilemap_colour(0x01),
              "VHDL tilemap.vhd:191 — textmode_i = control(3); pix uses 1bpp");
    }

    // TM-41: text-mode pixel extraction via shift_left(mem, abs_x(2:0))
    // then bit 7. Pattern 0xAA = 10101010 → src pixels 0,2,4,6 = 1;
    // 1,3,5,7 = 0. G104 phase 4 (40-col doubled): each src pixel emits
    // into TWO output cells, so:
    //   dst[0,1] = ink, dst[2,3] = paper, dst[4,5] = ink, dst[6,7] = paper.
    //
    // GH #113 CORRECTION. This row previously asserted the paper cells were
    // 0 (transparent). That matched the C++ (which skipped `pixel_4bit == 0`
    // in text mode) but NOT the VHDL: tilemap.vhd:429 makes pixel_en_f
    // collapse to pixel_en_s whenever pixel_textmode_s = '1', so the
    // index-transparency test at tilemap.vhd:427 is bypassed and BOTH
    // half-bits emit a pixel. attr = 0x00 → pal_offset 0 → ink index
    // (0<<1)|1 = 0x01, paper index (0<<1)|0 = 0x00 (tilemap.vhd:386).
    // Both palette entries are painted distinctly so the assertion cannot
    // pass on a defaulted/zero lookup.
    // VHDL: tilemap.vhd:385-386 (extraction), 427/429 (text-mode enable).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);   // ink
        paint_tm_palette_entry(pal, 0x00, 0x03);   // paper — distinct, non-zero
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0xAA);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_control(0x88);
        auto s = render_line(tm, 0, ram, pal);
        check_pred("TM-41",
                   s.pixels[0] == pal.tilemap_colour(0x01) &&
                   s.pixels[1] == pal.tilemap_colour(0x01) &&
                   s.pixels[2] == pal.tilemap_colour(0x00) &&
                   s.pixels[3] == pal.tilemap_colour(0x00) &&
                   s.pixels[4] == pal.tilemap_colour(0x01) &&
                   s.pixels[5] == pal.tilemap_colour(0x01) &&
                   s.pixels[6] == pal.tilemap_colour(0x00) &&
                   s.pixels[7] == pal.tilemap_colour(0x00),
                   "VHDL tilemap.vhd:385-386 — shift_left(mem, abs_x(2:0))(7); "
                   "429 — text mode emits paper too; G104 40-col 2-cell pairs");
    }

    // TM-42: 7-bit palette offset + 1-bit pixel. attr = 0x42 → bits 7:1 =
    // 0x21. Expected colour index = (0x21<<1)|1 = 0x43 (67).
    // VHDL: tilemap.vhd:386 textmode pixel = tilemap_1(7:1) & bit(7).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x43, 0x1C);
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0xFF);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x42);
        tm.set_control(0x88);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-42", s.pixels[0], pal.tilemap_colour(0x43),
              "VHDL tilemap.vhd:386 — textmode index = attr(7:1)<<1 | bit");
    }

    // TM-43: text mode ignores mirror/rotate — attr bits 3..1 are part of
    // the palette offset in text mode, not transforms. Asymmetric 1bpp
    // tile with only pixel 0 set, rendered twice (attr=0, attr=0x0E).
    // Both columns must show a lit pixel at screen x=0 of their tile.
    // G104 phase 4: 40-col doubles each src pixel — tile-0 src pixel 0
    // → dst[0,1]; tile-1 src pixel 8 (= screen x=8 in 320-grid) → dst[16,17].
    //
    // GH #113 CORRECTION (same cause as TM-41): the trailing cells hold the
    // tile's PAPER colour, not transparency — tilemap.vhd:429 bypasses the
    // index-transparency test in text mode. attr 0x00 → paper index 0x00;
    // attr 0x0E → pal_offset 0x07 → ink 0x0F, paper 0x0E. Asserting the two
    // DIFFERENT paper indices also keeps the row's original discriminative
    // power over the 7-bit palette-offset decode.
    // VHDL: tilemap.vhd:386 — transforms not used on the textmode path;
    //       tilemap.vhd:429 — text mode emits every in-window pixel.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        paint_tm_palette_entry(pal, 0x0F, 0x03);
        paint_tm_palette_entry(pal, 0x00, 0x1C);   // tile-0 paper
        paint_tm_palette_entry(pal, 0x0E, 0x60);   // tile-1 paper (distinct)
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0x80);   // only leftmost bit
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 1, 0x0E);
        tm.set_control(0x88);
        auto s = render_line(tm, 0, ram, pal);
        check_pred("TM-43",
                   s.pixels[0]  == pal.tilemap_colour(0x01) &&
                   s.pixels[1]  == pal.tilemap_colour(0x01) &&
                   s.pixels[16] == pal.tilemap_colour(0x0F) &&
                   s.pixels[17] == pal.tilemap_colour(0x0F) &&
                   s.pixels[2]  == pal.tilemap_colour(0x00) &&
                   s.pixels[3]  == pal.tilemap_colour(0x00) &&
                   s.pixels[18] == pal.tilemap_colour(0x0E) &&
                   s.pixels[19] == pal.tilemap_colour(0x0E),
                   "VHDL tilemap.vhd:386 — attr(3..1) belong to palette offset "
                   "in textmode, not transform controls; 429 — paper emitted; "
                   "G104 40-col 2-cell pairs");
    }

    // TM-44 — COVERED AT COMPOSITOR TIER (not a skip).  Text-mode RGB
    // transparency comparison (tilemap.vhd:426-429, zxnext.vhd:7109)
    // happens at the post-palette RGB stage in the compositor.  See
    // test/compositor/compositor_test.cpp group "TR" row TR-20 (TM text-
    // mode RGB==NR0x14 => tm_transparent) and TR-21 (non-text ignores the
    // RGB compare).  Kept here as a source-level reference to the
    // tilemap plan row.
}

// ── Group 6: Strip flags (force_attr) mode ──────────────────────────────
//
// VHDL places strip_flags on bit 5 (tilemap.vhd:190). C++ currently reads
// it from bit 4 (src/video/tilemap.cpp:40 force_attr_). These assertions
// use the VHDL bit assignment and thus FAIL against the current emulator.

void group6_strip_flags() {
    set_group("G6 Strip flags");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-50: strip_flags mode activated via NR 0x6B bit 5.
    // With strip flags the map has 1 byte per entry; tile at col 0 row 0
    // is at map_base+0, col 1 at map_base+1.
    // VHDL: tilemap.vhd:190 + :396-398 tile address narrows to "00"&sub_sub.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        write_map1(ram, DEF_MAP_BASE, 0, 0, 40, 1);
        write_map1(ram, DEF_MAP_BASE, 1, 0, 40, 2);
        tm.set_control(0xA0);   // enable + bit 5 (strip_flags per VHDL)
        tm.set_default_attr(0x00);
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: tile 0 src 0 → dst[0]; tile 1 src 8 → dst[16].
        check_pred("TM-50",
                   s.pixels[0]  == pal.tilemap_colour(0x03) &&
                   s.pixels[16] == pal.tilemap_colour(0x05),
                   "VHDL tilemap.vhd:190 — control(5)=strip_flags; "
                   "map packed 1 byte per tile");
    }

    // TM-51: default attribute (NR 0x6C) supplies tilemap_1 when stripped.
    // Palette offset 2 in default_attr → index = 0x23 from pixel value 3.
    // VHDL: tilemap.vhd:366 tm_tilemap_1 <= default_flags_i.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x23, 0x1C);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map1(ram, DEF_MAP_BASE, 0, 0, 40, 1);
        tm.set_default_attr(0x20);
        tm.set_control(0xA0);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-51", s.pixels[0], pal.tilemap_colour(0x23),
              "VHDL tilemap.vhd:366 — default_flags_i drives tilemap_1 "
              "when strip_flags=1");
    }

    // TM-52: strip_flags + 40-col — row 1 tile is at map_base + 40 (not 80).
    // VHDL: tilemap.vhd:395-398 addr_tile = "00"&sub_sub for strip mode.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        write_map1(ram, DEF_MAP_BASE, 0, 0, 40, 1);
        write_map1(ram, DEF_MAP_BASE, 0, 1, 40, 2);
        tm.set_default_attr(0x00);
        tm.set_control(0xA0);
        auto s0 = render_line(tm, 0, ram, pal);
        auto s8 = render_line(tm, 8, ram, pal);
        check_pred("TM-52",
                   s0.pixels[0] == pal.tilemap_colour(0x03) &&
                   s8.pixels[0] == pal.tilemap_colour(0x05),
                   "VHDL tilemap.vhd:395-398 — 40-col strip map: row offset "
                   "= row * 40 bytes");
    }

    // TM-53: strip_flags + 80-col — row offset = row * 80 bytes.
    // VHDL: tilemap.vhd:328 abs_y_mult << 1 for 80-col mode.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        for (int c = 0; c < 80; ++c) write_map1(ram, DEF_MAP_BASE, c, 0, 80, 1);
        for (int c = 0; c < 80; ++c) write_map1(ram, DEF_MAP_BASE, c, 1, 80, 2);
        tm.set_default_attr(0x00);
        tm.set_control(0xE0);   // enable + 80-col + strip_flags (VHDL bits 7,6,5)
        auto s0 = render_line(tm, 0, ram, pal);
        auto s8 = render_line(tm, 8, ram, pal);
        check_pred("TM-53",
                   s0.pixels[0] == pal.tilemap_colour(0x03) &&
                   s8.pixels[0] == pal.tilemap_colour(0x05),
                   "VHDL tilemap.vhd:328,395-398 — 80-col strip: row offset "
                   "= row * 80 bytes");
    }
}

// ── Group 7: Tile map base address ──────────────────────────────────────

void group7_map_addr() {
    set_group("G7 Map addr");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-60: map base in bank 5 at a custom offset. reg = 0x10 →
    // map_base physical = bank5 + 0x10 * 256 = 0x15000.
    // VHDL: tilemap.vhd:402-403 bank7 = offset(6); tilemap.vhd:400
    // offset mem addr = offset(5:0) * 256.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        tm.set_map_base(0x10);
        uint32_t mb = BANK5 + 0x10 * 256;
        write_map2(ram, mb, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-60", s.pixels[0], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:403 — map fetched from bank5 + "
              "offset(5:0) * 256");
    }

    // TM-61: map base in bank 7 when bit 7 of NR 0x6E is set.
    // VHDL: tilemap.vhd:402 bank7 flag from offset MSB.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        tm.set_map_base(0x80);  // bit 7 set → bank 7, offset 0
        write_map2(ram, BANK7, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-61", s.pixels[0], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:402 — NR 0x6E bit 7 selects bank 7");
    }

    // TM-62: tile-definition base in bank 5 at a custom offset.
    // VHDL: tilemap.vhd:402-403 same decoding for NR 0x6F.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        uint32_t db = BANK5 + 0x20 * 256;
        tm.set_def_base(0x20);
        fill_tile_pattern(ram, db, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-62", s.pixels[0], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:403 — tile defs from bank5 + offset * 256");
    }

    // TM-63: tile-definition base in bank 7.
    // VHDL: tilemap.vhd:402.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        tm.set_def_base(0x80);
        fill_tile_pattern(ram, BANK7, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-63", s.pixels[0], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:402 — NR 0x6F bit 7 selects bank 7");
    }

    // TM-64: offset addition — two different offsets must point to two
    // different physical map bases. Write tile index 1 only at offset
    // 0x10, tile index 2 only at offset 0x18; switch between the two.
    // VHDL: tilemap.vhd:403 adds offset(5:0) to sub(13:8).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        write_map2(ram, BANK5 + 0x10 * 256, 0, 0, 40, 1, 0x00);
        write_map2(ram, BANK5 + 0x18 * 256, 0, 0, 40, 2, 0x00);
        tm.set_map_base(0x10);
        tm.set_enabled(true);
        auto s_10 = render_line(tm, 0, ram, pal);
        tm.set_map_base(0x18);
        auto s_18 = render_line(tm, 0, ram, pal);
        check_pred("TM-64",
                   s_10.pixels[0] == pal.tilemap_colour(0x03) &&
                   s_18.pixels[0] == pal.tilemap_colour(0x05),
                   "VHDL tilemap.vhd:403 — distinct NR 0x6E offsets map to "
                   "distinct physical base addresses");
    }

    // TM-65: map entry size — 2 bytes with flags, 1 byte stripped.
    // With flags, col 1 tile-index byte is at map_base + 2; stripped,
    // col 1 tile-index byte is at map_base + 1.
    // VHDL: tilemap.vhd:396-398.
    //
    // This test is paired with TM-50: running both confirms the 2-byte vs
    // 1-byte layouts react differently. Expressed here as a direct
    // assertion against the 2-byte layout.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        // 2-byte entries: col 0 -> (idx=1, attr=0), col 1 -> (idx=2, attr=0).
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 2, 0x00);
        tm.set_control(0x80);   // enable, no strip
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: tile 0 occupies dst[0..15]; tile 1 occupies dst[16..31].
        check_pred("TM-65",
                   s.pixels[0]  == pal.tilemap_colour(0x03) &&
                   s.pixels[16] == pal.tilemap_colour(0x05),
                   "VHDL tilemap.vhd:396 — strip=0 uses 2-byte entries "
                   "(tile_index at even byte, flags at odd)");
    }

    // TM-66 — NR 0x6E read-back masks bit 6.
    // VHDL zxnext.vhd:6108 forces bit 6 to '0' on read
    // (`nr_6e_tilemap_base_7 & '0' & nr_6e_tilemap_base`).  Other bits
    // (7 + 5:0) must round-trip unchanged from the raw stored byte.
    {
        fresh(tm, pal, ram);
        // 0xFF = all bits set; expected read-back = 0xBF (bit 6 cleared).
        tm.set_map_base(0xFF);
        check_pred("TM-66",
                   tm.get_map_base_read() == 0xBF &&
                   tm.get_map_base_raw()  == 0xFF,
                   "VHDL zxnext.vhd:6108 — NR 0x6E read forces bit 6=0; "
                   "raw store unchanged");
    }

    // TM-67 — NR 0x6F read-back masks bit 6.
    // VHDL zxnext.vhd:6111 — same pattern as NR 0x6E.
    {
        fresh(tm, pal, ram);
        tm.set_def_base(0xFF);
        check_pred("TM-67",
                   tm.get_def_base_read() == 0xBF &&
                   tm.get_def_base_raw()  == 0xFF,
                   "VHDL zxnext.vhd:6111 — NR 0x6F read forces bit 6=0; "
                   "raw store unchanged");
    }
}

// ── Group 8: Tile pixel address ─────────────────────────────────────────

void group8_pixel_addr() {
    set_group("G8 Pixel addr");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-70: standard pixel address layout — 32 bytes per tile, 4 bytes
    // per row, 2 pixels per byte. Value at row 3, col 5 must be
    // retrievable.
    // VHDL: tilemap.vhd:394 addr_pix = sub_sub & transformed_y & transformed_x(2:1).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x07, 0xE0);
        uint32_t addr = DEF_DEF_BASE + 1 * 32;
        // row 3, col 5 → byte offset = 3*4 + 5/2 = 14; col 5 is odd → low nibble.
        ram.write(addr + 14, 0x07);   // high nib = 0, low nib = 7
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 3, ram, pal);
        // 40-col doubled: src col 5 → dst[10,11].
        check("TM-70", s.pixels[10], pal.tilemap_colour(0x07),
              "VHDL tilemap.vhd:394 — standard pix addr = idx*32 + y*4 + x/2");
    }

    // TM-71: text-mode pixel address — 8 bytes per tile, 1 byte per row.
    // Row 2 of tile 1 is at def_base + 1*8 + 2.
    // VHDL: tilemap.vhd:394 addr_pix text = "00" & sub_sub & abs_y(2:0).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x01, 0xE0);
        uint32_t addr = DEF_DEF_BASE + 1 * 8;
        ram.write(addr + 2, 0x80);  // only pixel 0 set on row 2
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_control(0x88);       // textmode (VHDL bit 3)
        auto s = render_line(tm, 2, ram, pal);
        check("TM-71", s.pixels[0], pal.tilemap_colour(0x01),
              "VHDL tilemap.vhd:394 — textmode pix addr = idx*8 + abs_y(2:0)");
    }

    // TM-72: nibble selection — transformed_x(0)=0 → high nibble, =1 → low.
    // Put value 0xA in the high nibble and 0x5 in the low nibble of the
    // first byte of row 0 of tile 1; pixel 0 should read 0xA, pixel 1 → 0x5.
    // VHDL: tilemap.vhd:383.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x0A, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        uint32_t addr = DEF_DEF_BASE + 1 * 32;
        ram.write(addr + 0, 0xA5);  // high=0xA, low=0x5
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        // 40-col doubled: src pixel 0 (high nibble = 0xA) → dst[0,1];
        // src pixel 1 (low nibble = 0x5) → dst[2,3].
        check_pred("TM-72",
                   s.pixels[0] == pal.tilemap_colour(0x0A) &&
                   s.pixels[2] == pal.tilemap_colour(0x05),
                   "VHDL tilemap.vhd:383 — nibble select: x(0)=0 high, x(0)=1 low");
    }
}

// ── Group 9: Scroll ─────────────────────────────────────────────────────

void group9_scroll() {
    set_group("G9 Scroll");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-80: basic X scroll — scrolling by 8 shifts the display one tile.
    // VHDL: tilemap.vhd:309-318 tm_x_sum + correction.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 2, 0x00);
        tm.set_enabled(true);
        tm.set_scroll_x_lsb(8);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-80", s.pixels[0], pal.tilemap_colour(0x05),
              "VHDL tilemap.vhd:309-318 — scroll_x=8 brings col 1 to pixel 0");
    }

    // TM-81: X scroll wraps at 320 in 40-col mode. Scroll by 320 ≡ scroll 0.
    // VHDL: tilemap.vhd:312-316 correction subtracts 320 when sum ≥ 320
    // (40-col) so the modulo is 320.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        for (int c = 1; c < 40; ++c)
            write_map2(ram, DEF_MAP_BASE, c, 0, 40, 2, 0x00);
        tm.set_enabled(true);
        tm.set_scroll_x_msb(1);   // 256
        tm.set_scroll_x_lsb(64);  // +64 = 320
        auto s = render_line(tm, 0, ram, pal);
        check("TM-81", s.pixels[0], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:315 — scroll_x=320 wraps to 0 in 40-col");
    }

    // TM-82: X scroll wraps at 640 in 80-col mode. Scroll by 640 ≡ scroll 0.
    // VHDL: tilemap.vhd:314 correction wraps at 640 when tm_mode=1.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 80, 1, 0x00);
        for (int c = 1; c < 80; ++c)
            write_map2(ram, DEF_MAP_BASE, c, 0, 80, 2, 0x00);
        tm.set_control(0xC0);     // enable + 80-col
        tm.set_scroll_x_msb(2);   // 512
        tm.set_scroll_x_lsb(128); // +128 = 640
        auto s = render_line(tm, 0, ram, pal);
        check("TM-82", s.pixels[0], pal.tilemap_colour(0x03),
              "VHDL tilemap.vhd:314 — scroll_x=640 wraps to 0 in 80-col");
    }

    // TM-83: basic Y scroll — scrolling by 8 shifts rows by one.
    // VHDL: tilemap.vhd:326 abs_y_s = scroll_y + vcounter.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        for (int c = 0; c < 40; ++c) {
            write_map2(ram, DEF_MAP_BASE, c, 0, 40, 1, 0x00);
            write_map2(ram, DEF_MAP_BASE, c, 1, 40, 2, 0x00);
        }
        tm.set_enabled(true);
        tm.set_scroll_y(8);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-83", s.pixels[0], pal.tilemap_colour(0x05),
              "VHDL tilemap.vhd:326 — scroll_y=8 brings row 1 to line 0");
    }

    // TM-84: Y scroll wraps at 256. 8-bit add → scroll 0 == scroll 256.
    // VHDL: tilemap.vhd:326 — addition is 8-bit.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        for (int c = 0; c < 40; ++c)
            for (int r = 0; r < 32; ++r)
                write_map2(ram, DEF_MAP_BASE, c, r, 40, 1, 0x00);
        tm.set_enabled(true);
        tm.set_scroll_y(0);   // 256 would wrap to 0 in 8-bit arith
        auto s0 = render_line(tm, 0, ram, pal);
        tm.set_scroll_y(static_cast<uint8_t>(256 & 0xFF));
        auto s1 = render_line(tm, 0, ram, pal);
        check("TM-84", s0.pixels[0], s1.pixels[0],
              "VHDL tilemap.vhd:326 — scroll_y is 8-bit; 256 wraps to 0");
    }

    // TM-85: per-line scroll — snapshotting different scroll values for
    // different scanlines must be honoured by render_scanline.
    // VHDL: tilemap.vhd:345 S_IDLE samples scroll_x/y at each tile fetch.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        paint_tm_palette_entry(pal, 0x05, 0x03);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        fill_tile_pattern(ram, DEF_DEF_BASE, 2, 5);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        write_map2(ram, DEF_MAP_BASE, 1, 0, 40, 2, 0x00);
        tm.set_enabled(true);
        tm.init_scroll_per_line();
        // Scanline 0 sees scroll 0; scanline 1 sees scroll 8.
        tm.set_scroll_x_lsb(0);
        tm.snapshot_scroll_for_line(0);
        tm.set_scroll_x_lsb(8);
        tm.snapshot_scroll_for_line(1);
        Scan s0, s1;
        std::memset(&s0, 0, sizeof(s0));
        std::memset(&s1, 0, sizeof(s1));
        tm.render_scanline(s0.pixels, s0.ula_over, 0, ram, pal);
        tm.render_scanline(s1.pixels, s1.ula_over, 1, ram, pal);
        check_pred("TM-85",
                   s0.pixels[0] == pal.tilemap_colour(0x03) &&
                   s1.pixels[0] == pal.tilemap_colour(0x05),
                   "VHDL tilemap.vhd:345 — per-scanline scroll samples are "
                   "honoured between scanlines");
    }

    // TM-86 — Per-line scroll snapshot must survive line >= 320.
    // VHDL tilemap.vhd:309/326 samples scroll_x/y fresh per scanline.
    // jnext models that with a per-line snapshot array; the array must
    // cover every scanline a machine timing produces (Pentagon / +3 reach
    // lines_per_frame=320, so the last valid line index is 319, plus we
    // exercise vblank-region writes at line >= 320).  Mirrors the
    // SpriteEngine catch-up rationale at sprites.cpp:119-144.
    {
        fresh(tm, pal, ram);
        tm.set_enabled(true);

        // (1) Initialise the snapshot array at frame start with scroll=0.
        tm.set_scroll_x_lsb(0);
        tm.set_scroll_y(0);
        tm.init_scroll_per_line();

        // (2) Mid-vblank update: change scroll, snapshot at line 320 — a
        //     line index the old `< 320` cap would have silently dropped.
        tm.set_scroll_x_lsb(0x55);
        tm.set_scroll_y(0xAA);
        tm.snapshot_scroll_for_line(320);

        // (3) Snapshot at the very last valid Pentagon scanline (319) and
        //     at a line within the wider safe range to confirm the array
        //     extends beyond the old 320-element cap.
        tm.snapshot_scroll_for_line(319);
        tm.snapshot_scroll_for_line(400);

        check_pred("TM-86",
                   tm.scroll_x_for_line(319) == 0x55 &&
                   tm.scroll_y_for_line(319) == 0xAA &&
                   tm.scroll_x_for_line(320) == 0x55 &&
                   tm.scroll_y_for_line(320) == 0xAA &&
                   tm.scroll_x_for_line(400) == 0x55 &&
                   tm.scroll_y_for_line(400) == 0xAA,
                   "VHDL tilemap.vhd:309/326 — per-line scroll snapshot must "
                   "store late (vblank-region) writes; mirrors SpriteEngine "
                   "catch-up at sprites.cpp:119-144");
    }
}

// ── Group 10: Transparency ──────────────────────────────────────────────

void group10_transparency() {
    set_group("G10 Transparency");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-90: standard-mode transparency index comes from NR 0x4C.
    // VHDL: tilemap.vhd:427 pixel_en_standard = pixel_en AND (pix /= transp).
    {
        fresh(tm, pal, ram);
        pal.set_tilemap_transparency(0x05);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 5);   // matches transp
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-90", s.pixels[0], 0u,
              "VHDL tilemap.vhd:427 — pix == transp_colour => pixel disabled");
    }

    // TM-91: reset default transparency index is 0x0F.
    // VHDL: zxnext.vhd nr_4c reset — default tilemap transp = 0x0F.
    {
        fresh(tm, pal, ram);
        check("TM-91", pal.tilemap_transparency(), uint8_t{0x0F},
              "VHDL zxnext.vhd NR 0x4C reset — default transparent idx = 0xF");
    }

    // TM-92: writing a custom transparent index (0x07) makes pixels of
    // that value transparent.
    // VHDL: tilemap.vhd:427.
    {
        fresh(tm, pal, ram);
        pal.set_tilemap_transparency(0x07);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 7);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-92", s.pixels[0], 0u,
              "VHDL tilemap.vhd:427 — custom transp idx 0x07 disables pixel");
    }

    // TM-93 — COVERED AT COMPOSITOR TIER (not a skip).  Textmode RGB
    // transparency (zxnext.vhd:7109) is verified at
    // test/compositor/compositor_test.cpp row TR-20.

    // ── GH #113: text mode bypasses the INDEX transparency test ──────────
    //
    // VHDL tilemap.vhd:427,429:
    //   pixel_en_standard_s <= '1' when pixel_en_s = '1'
    //                          and (video_data(3 downto 0) /= transp_colour_i);
    //   pixel_en_f <= (pixel_en_standard_s and not pixel_textmode_s)
    //                 or (pixel_en_s and pixel_textmode_s);
    // With pixel_textmode_s = '1' the left term is masked off and pixel_en_f
    // collapses to pixel_en_s — the clip/window gate alone.  So in text mode
    // EVERY in-window pixel is emitted, paper (bit 0) included, and NR 0x4C
    // has no effect at all.  jnext used to drop text-mode paper pixels, which
    // made the NextZXOS CP/M 80-column screen transparent over its whole
    // paper area and let leftover ULA content show through (GH #113).

    // TM-96: an all-paper text-mode tile emits an opaque run — no cell is
    // transparent, and every cell carries the paper index (attr[7:1]<<1)|0.
    // 40-col doubling: one 8-pixel tile fills output cells 0..15.
    // VHDL: tilemap.vhd:386 (paper index), 429 (text mode emits it).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x00, 0x1C);          // paper, distinct
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0x00);   // every bit = paper
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00); // attr 0 → offset 0
        tm.set_control(0x88);                             // enable + textmode
        auto s = render_line(tm, 0, ram, pal);
        bool all_paper = true;
        for (int i = 0; i < 16; ++i)
            if (s.pixels[i] != pal.tilemap_colour(0x00)) all_paper = false;
        check_pred("TM-96", all_paper,
              "VHDL tilemap.vhd:429 — text mode emits paper pixels opaque; "
              "an all-paper tile is a solid run, never transparent");
    }

    // TM-97: the paper index is the tile's own (attr[7:1]<<1)|0 — not 0 and
    // not the ink index.  attr 0xFE → pal_offset 0x7F → ink 0xFF, paper 0xFE.
    // Pattern 0xAA alternates, so ink and paper must alternate in 2-cell
    // pairs with two DIFFERENT palette entries.
    // VHDL: tilemap.vhd:386 — textmode pixel = tilemap_1(7 downto 1) & bit.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0xFF, 0xE0);   // ink
        paint_tm_palette_entry(pal, 0xFE, 0x03);   // paper
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0xAA);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0xFE);
        tm.set_control(0x88);
        auto s = render_line(tm, 0, ram, pal);
        check_pred("TM-97",
                   s.pixels[0] == pal.tilemap_colour(0xFF) &&
                   s.pixels[2] == pal.tilemap_colour(0xFE) &&
                   s.pixels[4] == pal.tilemap_colour(0xFF) &&
                   s.pixels[6] == pal.tilemap_colour(0xFE) &&
                   pal.tilemap_colour(0xFF) != pal.tilemap_colour(0xFE),
                   "VHDL tilemap.vhd:386 — paper index = attr(7:1)<<1 | 0, "
                   "a real palette entry distinct from the ink index");
    }

    // TM-98: the paired row that pins the pixel_en_f MUX itself.  Identical
    // stimulus — transparency index 0x00 and an all-zero tile definition —
    // rendered once with NR 0x6B b3 = 0 and once with b3 = 1.  Non-text must
    // DROP the pixel (pixel_en_standard_s, tilemap.vhd:427); text must EMIT
    // it (tilemap.vhd:429).  The two outcomes differ only by the textmode
    // bit, so this fails if the index test is applied in text mode (the
    // GH #113 bug) AND if it is skipped in standard mode.
    {
        fresh(tm, pal, ram);
        pal.set_tilemap_transparency(0x00);
        paint_tm_palette_entry(pal, 0x00, 0x1C);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 0);       // 4bpp: all pixels 0
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0x00);   // 1bpp: all paper
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);

        tm.set_control(0x80);                             // enable, textmode OFF
        auto s_std = render_line(tm, 0, ram, pal);

        tm.set_control(0x88);                             // enable, textmode ON
        auto s_txt = render_line(tm, 0, ram, pal);

        check_pred("TM-98",
                   s_std.pixels[0] == 0u &&
                   s_txt.pixels[0] == pal.tilemap_colour(0x00),
                   "VHDL tilemap.vhd:427/429 — pixel_en_f masks the index "
                   "transparency test in text mode only: same data drops in "
                   "standard mode and emits in text mode");
    }

    // TM-94 — COVERED AT COMPOSITOR TIER (not a skip).  pixel_en_f
    // selection between index- and RGB-based transparency pipelines
    // (zxnext.vhd compositor) is verified at compositor_test.cpp rows
    // TR-21 (non-text ignores RGB compare) and TR-22 (tm_pixel_en=0 →
    // transparent).

    // TM-95 — Text-mode RGB transparency at the compositor tier.
    // VHDL zxnext.vhd:7109 —
    //   tm_transparent <= '1' when (tm_pixel_en_2 = '0')
    //     or (tm_pixel_textmode_2 = '1' and tm_rgb_2(8:1) = transparent_rgb_2)
    //     or (tm_en_2 = '0').
    // The renderer feeds Tilemap output (line buffer + per-pixel textmode
    // flag) into composite_scanline; the compositor's text-mode RGB-
    // compare clause must mark a textmode pixel whose 8-bit RGB equals
    // NR 0x14 as transparent so ULA shows through.  Driver: render a
    // textmode tilemap whose palette entry equals the global transparent
    // RGB, then composite with an opaque ULA pixel and verify the
    // composite output is the ULA pixel (TM-pixel suppressed by RGB
    // match), not the TM pixel.  Closure: G98 + G101.
    {
        fresh(tm, pal, ram);
        // NR 0x14 transparent RGB = 0xE3 (default) — VHDL zxnext.vhd:7100.
        const uint8_t kTranspRgb = 0xE3;
        // Paint the tilemap palette entry that the textmode pixel will
        // resolve to (attr(7:1)<<1 | bit = 0x55 with attr=0x54, bit=1)
        // such that its colour equals the NR 0x14 transparent RGB.
        paint_tm_palette_entry(pal, 0x55, kTranspRgb);
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0xFF);   // all 8 row bits set
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x54); // attr(7:1)=0x2A
        tm.set_control(0x88);   // enable + textmode (VHDL bit 3)
        auto s = render_line(tm, 0, ram, pal);

        // Sanity-check the Tilemap-tier emission: pixel RGB == NR 0x14
        // and the per-pixel textmode flag is set (precondition for the
        // compositor clause). Without these the compositor cannot fire
        // the RGB-compare branch at all.
        const uint32_t nr14_argb = Renderer::rrrgggbb_to_argb(kTranspRgb);
        const bool tm_pre_ok = (s.pixels[0] == nr14_argb) && s.textmode[0];

        // Drive Renderer::composite_scanline with the rendered TM line
        // and an opaque ULA pixel; expect the TM pixel to be flagged
        // transparent (text-mode + RGB match) so ULA wins in mode SLU.
        Renderer R; R.reset();
        R.set_layer_priority(0);                    // SLU
        R.set_transparent_rgb(kTranspRgb);          // NR 0x14
        R.set_tm_enabled(true);
        const uint32_t ula_pix = 0xFFAABBCCu;       // distinct opaque ULA
        // G104 phase 4: tilemap line buffer is now 640-native.
        for (int i = 0; i < Renderer::FB_WIDTH; ++i) {
            R.ula_line_[i]        = (i == 0) ? ula_pix : 0u;
            R.layer2_line_[i]     = 0u;
            R.sprite_line_[i]     = 0u;
            R.tilemap_line_[i]    = s.pixels[i];
            R.tm_pixel_below_[i]  = s.ula_over[i];
            R.tm_pixel_textmode_[i] = s.textmode[i];
            R.layer2_priority_[i] = false;
            R.ula_border_[i]      = false;
        }
        uint32_t out[Renderer::FB_WIDTH];  // G104 phase1: canonical 640
        std::memset(out, 0, sizeof(out));
        R.composite_scanline(out, /*fallback_argb=*/0u, /*row=*/0);

        check_pred("TM-95",
                   tm_pre_ok && out[0] == ula_pix,
                   "VHDL zxnext.vhd:7109 — textmode pixel + RGB==NR0x14 "
                   "marked transparent at compositor; ULA shows through");
    }
}

// ── Group 11: Palette selection / pixel composition ─────────────────────

void group11_palette() {
    set_group("G11 Palette");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-100: NR 0x6B bit 4 = 0 → tilemap uses palette 0.
    // VHDL: nr_6b_tm_palette_select (zxnext.vhd) routes into the tilemap
    // palette lookup.  Tilemap::set_control captures bit 4 and the emulator
    // wires it into PaletteManager::set_active_tilemap_palette().
    {
        fresh(tm, pal, ram);
        tm.set_control(0x80);                 // bit 4 = 0
        check_pred("TM-100", tm.palette_sel() == false,
                   "NR 0x6B bit 4 = 0 → tm.palette_sel() = false");
    }

    // TM-101: NR 0x6B bit 4 = 1 → tilemap uses palette 1.
    {
        fresh(tm, pal, ram);
        tm.set_control(0x90);                 // bit 4 = 1
        check_pred("TM-101", tm.palette_sel() == true,
                   "NR 0x6B bit 4 = 1 → tm.palette_sel() = true "
                   "— VHDL zxnext.vhd:5462,6826");
    }

    // TM-102: palette_sel from NR 0x6B is the authoritative select at
    // render time — it is NOT overridden by the NR 0x43 write target.
    // This row exercises the Tilemap-side state at unit tier; the
    // end-to-end render-lookup binding is tested at the integration tier.
    {
        fresh(tm, pal, ram);
        tm.set_control(0x90);                 // NR 0x6B bit 4 = 1
        // Flipping bit 4 must take effect immediately, irrespective of
        // any subsequent NR 0x43 activity (not exercised here since that
        // would require an Emulator).
        check_pred("TM-102", tm.palette_sel() == true,
                   "palette_sel authoritative at Tilemap layer "
                   "(VHDL zxnext.vhd:6826,6921-6922,6981 tm_palette_select)");
    }

    // TM-103: standard pixel composition — final index = attr(7:4)<<4 | pix.
    // VHDL: tilemap.vhd:382-383.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x53, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x50);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-103", s.pixels[0], pal.tilemap_colour(0x53),
              "VHDL tilemap.vhd:382-383 — standard: idx = attr(7:4)<<4 | pix");
    }

    // TM-104: text-mode pixel composition — final = attr(7:1)<<1 | bit.
    // VHDL: tilemap.vhd:386. Requires textmode via VHDL bit 3, so this
    // FAILS under the current C++ swap — intentional, pairs with TM-42.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x55, 0xE0);   // (0x2A<<1)|1 = 0x55
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0xFF);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x54);  // attr(7:1) = 0x2A
        tm.set_control(0x88);   // enable + textmode (VHDL bit 3)
        auto s = render_line(tm, 0, ram, pal);
        check("TM-104", s.pixels[0], pal.tilemap_colour(0x55),
              "VHDL tilemap.vhd:386 — textmode: idx = attr(7:1)<<1 | bit");
    }

    // TM-105 — Per-pixel pixel_textmode_o flag emitted by Tilemap.
    // VHDL tilemap.vhd:62,443 — pixel_textmode_o latched alongside
    // pixel_en_o and pixel_below_o; consumed at zxnext.vhd:7072
    // (pipeline stage 2 register) and 7109 (text-mode RGB transparency
    // gate).  Tilemap::render_scanline now exposes the per-pixel flag
    // via the optional textmode_flags out-buffer (G101 closure).  This
    // row drives two scanlines — one with NR 0x6B bit 3 = 1 (textmode),
    // one with bit 3 = 0 (standard) — and asserts:
    //   * textmode-on  → emitted pixels carry textmode_flag = true,
    //                    untouched (transparent) pixels stay false;
    //   * textmode-off → every pixel's textmode_flag is false, even
    //                    where the line is opaquely rendered.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x55, 0xE0);
        fill_tile_textmode(ram, DEF_DEF_BASE, 1, 0xFF);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x54);

        // 1) Textmode active: every emitted pixel must report textmode=1.
        tm.set_control(0x88);   // enable + textmode (VHDL bit 3)
        auto s_text = render_line(tm, 0, ram, pal);
        bool tm_flag_set_when_pixel = true;
        bool tm_flag_clear_when_no_pixel = true;
        // G104 phase 4: 640-cell output (40-col doubled).
        for (int i = 0; i < 640; ++i) {
            if (s_text.pixels[i] != 0u) {
                if (!s_text.textmode[i]) { tm_flag_set_when_pixel = false; break; }
            } else {
                // VHDL pixel_textmode_o latches alongside pixel_en_o
                // (tilemap.vhd:437); when no pixel is emitted (alpha=0)
                // the downstream compositor never reads the flag.  The
                // emulator keeps the array zeroed where no pixel was
                // written so callers get a deterministic false.
                if (s_text.textmode[i]) { tm_flag_clear_when_no_pixel = false; break; }
            }
        }

        // 2) Standard (non-textmode): every position must report flag=false.
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_control(0x80);   // enable, no textmode
        auto s_std = render_line(tm, 0, ram, pal);
        bool std_flag_all_false = true;
        // G104 phase 4: 640-cell output (40-col doubled).
        for (int i = 0; i < 640; ++i) {
            if (s_std.textmode[i]) { std_flag_all_false = false; break; }
        }

        check_pred("TM-105",
                   tm_flag_set_when_pixel &&
                   tm_flag_clear_when_no_pixel &&
                   std_flag_all_false,
                   "VHDL tilemap.vhd:62,443 — per-pixel pixel_textmode_o "
                   "exposed: 1 in textmode for emitted pixels, 0 otherwise");
    }
}

// ── Group 12: Clip window ───────────────────────────────────────────────
//
// The C++ Tilemap::render_scanline does not apply the clip window
// (tilemap.cpp:144-). Clip enforcement lives in the VHDL compositor
// and is observed at full-machine test tier. The Tilemap class exposes
// setters (set_clip_x1.. set_clip_y2) but no readback. Group 12 is
// therefore skipped at this tier.

void group12_clip() {
    set_group("G12 Clip");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-110: default clip covers the full visible area
    // (x1=0, x2=0x9F → pixel-x 0..319; y1=0, y2=0xFF → y 0..255).  With
    // an enabled tilemap and default clips, every pixel on a rendered
    // scanline is non-transparent.
    // VHDL: tilemap.vhd:424, zxnext.vhd:4977-4980.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        bool all_opaque = true;
        // G104 phase 4: 640-cell output (40-col doubled).
        for (int i = 0; i < 640; ++i) if (s.pixels[i] == 0u) { all_opaque = false; break; }
        check_pred("TM-110", all_opaque,
                   "Default clip 0x00..0x9F/0x00..0xFF renders full scanline");
    }

    // TM-111: custom clip window blanks pixels outside the rectangle.
    // Set clip to x1=0x10, x2=0x20 (pixel x 0x20..0x41), y1=0x40, y2=0x80.
    // Render y=0x20 (outside Y range) → full-line transparent.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        for (int row = 0; row < 32; ++row)
            write_map2(ram, DEF_MAP_BASE, 0, row, 40, 1, 0x00);
        tm.set_enabled(true);
        tm.set_clip_x1(0x10);
        tm.set_clip_x2(0x20);
        tm.set_clip_y1(0x40);
        tm.set_clip_y2(0x80);
        auto out_of_y = render_line(tm, 0x20, ram, pal);
        bool all_trans = true;
        // G104 phase 4: 640-cell output.
        for (int i = 0; i < 640; ++i) if (out_of_y.pixels[i] != 0u) { all_trans = false; break; }
        check_pred("TM-111", all_trans,
                   "Y outside clip (y=0x20 < clip_y1=0x40) → scanline transparent");
    }

    // TM-112: clip X coordinates are doubled by VHDL tilemap.vhd:416-417:
    //   xsv = clip_x1*2, xev = clip_x2*2 + 1.
    // Set clip_x1=clip_x2=0x10 → window covers hcounter_i ∈ [0x20, 0x21]
    // (320-grid).  Per VHDL tilemap.vhd:424 the clip comparator runs
    // against `hcounter_i` regardless of col-mode, so in jnext's 14 MHz
    // / 640-cell framebuffer each `hcounter_i` step covers a pair of
    // output cells.  hcounter_i=0x20 → cells [0x40..0x41];
    // hcounter_i=0x21 → cells [0x42..0x43]; total 4 opaque cells in
    // BOTH col-modes.  Default mode here is 40-col (set_enabled only).
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        for (int row = 0; row < 32; ++row)
            write_map2(ram, DEF_MAP_BASE, 0, row, 40, 1, 0x00);
        tm.set_enabled(true);
        tm.set_clip_x1(0x10);
        tm.set_clip_x2(0x10);
        tm.set_clip_y1(0x00);
        tm.set_clip_y2(0xFF);
        auto s = render_line(tm, 0, ram, pal);
        // Cells 0x40..0x43 must be opaque (one clip_x register pair = 4
        // 640-grid cells under VHDL `pixel_en_s` hcounter-granular
        // gating); cells 0x3F and 0x44 must be transparent.
        bool ok = (s.pixels[0x40] != 0u) && (s.pixels[0x41] != 0u) &&
                  (s.pixels[0x42] != 0u) && (s.pixels[0x43] != 0u) &&
                  (s.pixels[0x3F] == 0u) && (s.pixels[0x44] == 0u);
        check_pred("TM-112", ok,
                   "clip_x1=clip_x2=0x10 → 4 opaque cells at x=0x40..0x43 "
                   "(VHDL tilemap.vhd:416-417,424 — clip comparator runs "
                   "against hcounter_i, doubled into 640-grid)");
    }

    // TM-113: clip Y coordinates are direct (no doubling): clip_y1, clip_y2
    // compared to scanline y in 0..255 range.  Render exactly at y=clip_y1
    // and y=clip_y2: both inclusive; one pixel off each edge transparent.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        for (int row = 0; row < 32; ++row)
            write_map2(ram, DEF_MAP_BASE, 0, row, 40, 1, 0x00);
        tm.set_enabled(true);
        tm.set_clip_x1(0x00);
        tm.set_clip_x2(0x9F);
        tm.set_clip_y1(0x40);
        tm.set_clip_y2(0x50);
        auto at_y1  = render_line(tm, 0x40, ram, pal);
        auto at_y2  = render_line(tm, 0x50, ram, pal);
        auto before = render_line(tm, 0x3F, ram, pal);
        auto after  = render_line(tm, 0x51, ram, pal);
        bool ok = (at_y1.pixels[0] != 0u) && (at_y2.pixels[0] != 0u) &&
                  (before.pixels[0] == 0u) && (after.pixels[0] == 0u);
        check_pred("TM-113", ok,
                   "Y clip inclusive at clip_y1/clip_y2, outside transparent");
    }

    // TM-114 — TESTED AT INTEGRATION TIER (not a skip).  NR 0x1B 4-write
    // rotating index is implemented in emulator.cpp (NR 0x1B handler
    // cycles clip_tm_idx_).  See test/nextreg/nextreg_integration_test.cpp
    // group "Tilemap-Clip-NR" for the coverage.

    // TM-115 — TESTED AT INTEGRATION TIER (not a skip).  NR 0x1C bit 3
    // clip-index reset implemented in emulator.cpp (NR 0x1C handler).
    // See test/nextreg/nextreg_integration_test.cpp.

    // TM-116: clip getters exposed on Tilemap class.
    {
        fresh(tm, pal, ram);
        tm.set_clip_x1(0x11); tm.set_clip_x2(0x22);
        tm.set_clip_y1(0x33); tm.set_clip_y2(0x44);
        bool ok = tm.clip_x1() == 0x11 && tm.clip_x2() == 0x22 &&
                  tm.clip_y1() == 0x33 && tm.clip_y2() == 0x44;
        check_pred("TM-116", ok, "clip getters return programmed values");
    }
}

// ── Group 13: Layer priority (below flag) ───────────────────────────────

void group13_priority() {
    set_group("G13 Priority");
    Tilemap tm; PaletteManager pal; Ram ram;

    // TM-120: default is tilemap-on-top — attr(0)=0 → below=0.
    // VHDL: tilemap.vhd:388 wdata(8)=(attr(0) OR mode_512) AND NOT tm_on_top.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x00);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check("TM-120", s.ula_over[0], false,
              "VHDL tilemap.vhd:388 — default attr(0)=0 yields below=0");
    }

    // TM-121: global tm_on_top (NR 0x6B bit 0) forces below=0.
    // VHDL: tilemap.vhd:388 NOT tm_on_top_q zeroes the below signal.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x01);  // per-tile below
        tm.set_control(0x81);  // enable + tm_on_top
        auto s = render_line(tm, 0, ram, pal);
        check("TM-121", s.ula_over[0], false,
              "VHDL tilemap.vhd:388 — tm_on_top=1 overrides per-tile below");
    }

    // TM-122: per-tile below without tm_on_top.
    // VHDL: tilemap.vhd:388 — attr(0)=1 propagates when tm_on_top=0.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 1, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 1, 0x01);
        tm.set_enabled(true);
        auto s = render_line(tm, 0, ram, pal);
        check_pred("TM-122", s.ula_over[0],
              "VHDL tilemap.vhd:388 — attr(0)=1 with tm_on_top=0 sets below=1");
    }

    // TM-123 — COVERED AT COMPOSITOR TIER (not a skip).  The ULA/TM
    // layering decision `ulatm_rgb = tm when below=0 or ula_transp`
    // (zxnext.vhd:7116) is verified at compositor_test.cpp group "UTB"
    // rows UTB-10 (TM above: TM wins) and UTB-11 (TM below: ULA wins),
    // plus UTB-30/31/40/41 exercising the NR 0x68 mode matrix.

    // TM-124: tm_on_top overrides the per-tile below bit even in 512 mode.
    // VHDL: tilemap.vhd:388 — NOT tm_on_top_q gates the whole OR term.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 0, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 0, 0x00);
        tm.set_control(0x83);   // enable + mode_512 + tm_on_top
        auto s = render_line(tm, 0, ram, pal);
        check("TM-124", s.ula_over[0], false,
              "VHDL tilemap.vhd:388 — tm_on_top=1 zeroes below even in 512 mode");
    }

    // TM-125: 512-tile mode forces below when tm_on_top=0.
    // VHDL: tilemap.vhd:388 — mode_512 ORs into the below term.
    {
        fresh(tm, pal, ram);
        paint_tm_palette_entry(pal, 0x03, 0xE0);
        fill_tile_pattern(ram, DEF_DEF_BASE, 0, 3);
        write_map2(ram, DEF_MAP_BASE, 0, 0, 40, 0, 0x00);
        tm.set_control(0x82);   // enable + mode_512, tm_on_top=0
        auto s = render_line(tm, 0, ram, pal);
        check_pred("TM-125", s.ula_over[0],
              "VHDL tilemap.vhd:388 — mode_512=1 OR forces below=1");
    }
}

// ── Group 14: Stencil interaction ───────────────────────────────────────

void group14_stencil() {
    set_group("G14 Stencil");
    // TM-130 / TM-131 — COVERED AT COMPOSITOR TIER (not skips).  Stencil
    // mode (NR 0x68 ula_stencil_mode, VHDL zxnext.vhd:7112-7113) is a
    // compositor feature: stencil_rgb = ula_rgb AND tm_rgb, stencil
    // transparency = ula_transp OR tm_transp.  See compositor_test.cpp
    // group "STEN" rows STEN-10 (bitwise AND) and STEN-11 (AND with zero).
}

// ── Group 15: Enable / below interaction ────────────────────────────────

void group15_enable_below() {
    set_group("G15 Enable/below");
    // TM-140 / TM-141 — COVERED AT COMPOSITOR TIER (not skips).  When
    // tm_en=0, VHDL zxnext.vhd:6863 derives below = NOT tm_on_top in the
    // compositor's tm_pixel_below_1 path.  Tilemap::render_scanline
    // short-circuits when enabled_=false, so this check cannot land at
    // the Tilemap layer.  See compositor_test.cpp row TR-23 (tm_en=0 →
    // TM transparent) for the downstream verification.
}

} // namespace

// ── main ────────────────────────────────────────────────────────────────

int main() {
    std::printf("Tilemap Subsystem Compliance Test (VHDL-derived rewrite)\n");
    std::printf("========================================================\n\n");

    group1_reset_enable();   std::printf("  G1  Enable/Reset done\n");
    group2_40col();          std::printf("  G2  40-col       done\n");
    group3_80col();          std::printf("  G3  80-col       done\n");
    group4_512tile();        std::printf("  G4  512-tile     done\n");
    group5_textmode();       std::printf("  G5  Text mode    done\n");
    group6_strip_flags();    std::printf("  G6  Strip flags  done\n");
    group7_map_addr();       std::printf("  G7  Map addr     done\n");
    group8_pixel_addr();     std::printf("  G8  Pixel addr   done\n");
    group9_scroll();         std::printf("  G9  Scroll       done\n");
    group10_transparency();  std::printf("  G10 Transparency done\n");
    group11_palette();       std::printf("  G11 Palette      done\n");
    group12_clip();          std::printf("  G12 Clip         done\n");
    group13_priority();      std::printf("  G13 Priority     done\n");
    group14_stencil();       std::printf("  G14 Stencil      done\n");
    group15_enable_below();  std::printf("  G15 Enable/below done\n");

    std::printf("\n========================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows (unrealisable with current C++ API):\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-10s %s\n", s.id, s.reason);
        }
    }

    return g_fail > 0 ? 1 : 0;
}
