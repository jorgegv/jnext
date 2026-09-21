// Tilemap raster-split fetch-state regression tests.
//
// TX-1696 changes NR 0x6E/0x6F during the visible frame: one map/tile pair
// draws the fixed HUD, another draws the scrolling playfield, then the HUD
// pair is restored near the bottom. The FPGA tilemap consumes these registers
// as live inputs (tilemap.vhd:57-58 tm_map_base_i / tm_tile_base_i, latched
// at fetch time at :349-350, and :44 default_flags_i at :366); rendering a
// completed frame from only the final register values paints one configuration
// across every scanline.
//
// GRANULARITY, stated exactly. The VHDL latch fires when the fetch state
// machine re-enters S_IDLE, and :264 forces that on a HORIZONTAL counter
// condition - once per tile COLUMN, ~40/80 times a scanline. So on hardware a
// mid-scanline write takes effect from the next tile column of the SAME line.
// jnext models the same latch at per-scanline granularity
// (`Tilemap::snapshot_fetch_for_line`), which is the project's declared
// accuracy model (EMULATOR-DESIGN-PLAN section 1: per-scanline compositing).
// What these rows prove is therefore the LATCHING - a change does not
// retroactively repaint what has already been fetched - not the period.
//
// All fixtures below are synthetic and generated in memory.

#include "memory/ram.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "cpu/z80_cpu.h"
#include "memory/mmu.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/tilemap.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t BANK5 = 5u * 16384u;
constexpr uint32_t MAP_A = BANK5 + 0x00u * 256u;
constexpr uint32_t MAP_B = BANK5 + 0x04u * 256u;
constexpr uint32_t DEF_A = BANK5 + 0x10u * 256u;
constexpr uint32_t DEF_B = BANK5 + 0x20u * 256u;

int g_total = 0;
int g_pass = 0;
int g_fail = 0;

void check(const char* id, bool condition, const char* detail) {
    ++g_total;
    if (condition) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, detail);
    }
}

void paint_palette(PaletteManager& palette, uint8_t index, uint8_t rgb8) {
    palette.write_control(0x30);  // tilemap first palette
    palette.set_index(index);
    palette.write_8bit(rgb8);
}

void fill_tile(Ram& ram, uint32_t base, uint8_t tile, uint8_t pixel) {
    const uint8_t packed = static_cast<uint8_t>((pixel << 4) | pixel);
    const uint32_t start = base + static_cast<uint32_t>(tile) * 32u;
    for (uint32_t i = 0; i < 32; ++i)
        ram.write(start + i, packed);
}

void write_map2(Ram& ram, uint32_t base, uint8_t tile, uint8_t attr = 0) {
    ram.write(base, tile);
    ram.write(base + 1, attr);
}

void write_map1(Ram& ram, uint32_t base, uint8_t tile) {
    ram.write(base, tile);
}

uint32_t render_first_pixel(Tilemap& tilemap, int line, const Ram& ram,
                            const PaletteManager& palette) {
    uint32_t pixels[640];
    bool below[640];
    std::memset(pixels, 0, sizeof(pixels));
    std::memset(below, 0, sizeof(below));
    tilemap.render_scanline(pixels, below, line, ram, palette);
    return pixels[0];
}

void nr_write(Emulator& emulator, uint8_t reg, uint8_t value) {
    emulator.port().out(0x243B, reg);
    emulator.port().out(0x253B, value);
}

void park_cpu(Emulator& emulator) {
    emulator.mmu().write(0x8000, 0x76);  // HALT
    auto regs = emulator.cpu().get_registers();
    regs.PC = 0x8000;
    regs.SP = 0xFFFD;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emulator.cpu().set_registers(regs);
}

void copper_word(Emulator& emulator, uint16_t word) {
    nr_write(emulator, 0x60, static_cast<uint8_t>(word >> 8));
    nr_write(emulator, 0x60, static_cast<uint8_t>(word));
}

void fresh(Tilemap& tilemap, PaletteManager& palette, Ram& ram) {
    tilemap.reset();
    palette.reset();
    ram.reset();
    paint_palette(palette, 0x01, 0xE0);
    paint_palette(palette, 0x02, 0x1C);
    paint_palette(palette, 0x11, 0x03);
    paint_palette(palette, 0x21, 0xFC);
}

void test_map_base_split() {
    Tilemap tilemap;
    PaletteManager palette;
    Ram ram;
    fresh(tilemap, palette, ram);

    fill_tile(ram, DEF_A, 1, 1);
    fill_tile(ram, DEF_A, 2, 2);
    write_map2(ram, MAP_A, 1);
    write_map2(ram, MAP_B, 2);

    tilemap.set_control(0x80);
    tilemap.set_map_base(0x00);
    tilemap.set_def_base(0x10);
    tilemap.init_fetch_per_line();
    tilemap.snapshot_fetch_for_line(0);
    tilemap.set_map_base(0x04);
    tilemap.snapshot_fetch_for_line(1);

    const uint32_t line0 = render_first_pixel(tilemap, 0, ram, palette);
    const uint32_t line1 = render_first_pixel(tilemap, 1, ram, palette);
    check("TM-SPLIT-01",
          line0 == palette.tilemap_colour(0x01) &&
          line1 == palette.tilemap_colour(0x02),
          "NR 0x6E map-base is latched at fetch time, so a change never "
          "repaints already-fetched cells [tilemap.vhd:264,349 - S_IDLE is "
          "forced per tile COLUMN, so hardware granularity is finer than "
          "jnext's per-scanline model; zxnext.vhd:4407]");
}

void test_definition_base_split() {
    Tilemap tilemap;
    PaletteManager palette;
    Ram ram;
    fresh(tilemap, palette, ram);

    write_map2(ram, MAP_A, 1);
    fill_tile(ram, DEF_A, 1, 1);
    fill_tile(ram, DEF_B, 1, 2);

    tilemap.set_control(0x80);
    tilemap.set_map_base(0x00);
    tilemap.set_def_base(0x10);
    tilemap.init_fetch_per_line();
    tilemap.snapshot_fetch_for_line(0);
    tilemap.set_def_base(0x20);
    tilemap.snapshot_fetch_for_line(1);

    const uint32_t line0 = render_first_pixel(tilemap, 0, ram, palette);
    const uint32_t line1 = render_first_pixel(tilemap, 1, ram, palette);
    check("TM-SPLIT-02",
          line0 == palette.tilemap_colour(0x01) &&
          line1 == palette.tilemap_colour(0x02),
          "NR 0x6F tile-definition base is latched at fetch time, so a "
          "change never repaints already-fetched cells [tilemap.vhd:264,350 "
          "- S_IDLE is forced per tile COLUMN, so hardware granularity is "
          "finer than jnext's per-scanline model; zxnext.vhd:4408]");
}

void test_default_attribute_split() {
    Tilemap tilemap;
    PaletteManager palette;
    Ram ram;
    fresh(tilemap, palette, ram);

    write_map1(ram, MAP_A, 1);
    fill_tile(ram, DEF_A, 1, 1);

    tilemap.set_control(0xA0);  // enable + stripped attributes
    tilemap.set_map_base(0x00);
    tilemap.set_def_base(0x10);
    tilemap.set_default_attr(0x10);
    tilemap.init_fetch_per_line();
    tilemap.snapshot_fetch_for_line(0);
    tilemap.set_default_attr(0x20);
    tilemap.snapshot_fetch_for_line(1);

    const uint32_t line0 = render_first_pixel(tilemap, 0, ram, palette);
    const uint32_t line1 = render_first_pixel(tilemap, 1, ram, palette);
    check("TM-SPLIT-03",
          line0 == palette.tilemap_colour(0x11) &&
          line1 == palette.tilemap_colour(0x21),
          "NR 0x6C default attribute is consumed at fetch time, so a change "
          "never repaints already-fetched cells [tilemap.vhd:264,366 - "
          "S_READ_TILE_1 recurs per tile COLUMN, so hardware granularity is "
          "finer than jnext's per-scanline model; zxnext.vhd:4394]");
}

void test_emulator_copper_wiring() {
    Emulator emulator;
    EmulatorConfig config;
    config.type = MachineType::ZXN_ISSUE2;
    config.rewind_buffer_frames = 0;
    if (!emulator.init(config)) {
        check("TM-SPLIT-04", false, "failed to initialize full Emulator fixture");
        return;
    }
    park_cpu(emulator);

    uint8_t* bank5 = emulator.mmu().bank5_vram();
    const uint32_t map_a = 0x0000;
    const uint32_t map_b = 0x2000;
    const uint32_t def_a = 0x1000;
    for (int entry = 0; entry < 40 * 32; ++entry) {
        bank5[map_a + entry * 2] = 1;
        bank5[map_a + entry * 2 + 1] = 0;
        bank5[map_b + entry * 2] = 2;
        bank5[map_b + entry * 2 + 1] = 0;
    }
    std::memset(bank5 + def_a + 1 * 32, 0x11, 32);
    std::memset(bank5 + def_a + 2 * 32, 0x22, 32);

    paint_palette(emulator.palette(), 0x01, 0xE0);
    paint_palette(emulator.palette(), 0x02, 0x1C);

    nr_write(emulator, 0x68, 0x80);  // hide ULA
    nr_write(emulator, 0x6B, 0x80);  // tilemap on, 40 columns, attributes present
    nr_write(emulator, 0x6E, 0x00);  // map A at frame start
    nr_write(emulator, 0x6F, 0x10);  // shared definitions

    // WAIT cvc=100; MOVE NR 0x6E,0x20; HALT. This exercises the real
    // Copper -> NextREG -> Emulator::on_scanline -> renderer path.
    constexpr int wait_cvc = 100;
    const uint16_t wait_100 = static_cast<uint16_t>(0x8000u | wait_cvc);
    const uint16_t move_map_b = static_cast<uint16_t>((0x6Eu << 8) | 0x20u);
    const uint16_t halt = static_cast<uint16_t>(0x8000u | 511u);
    nr_write(emulator, 0x61, 0x00);
    nr_write(emulator, 0x62, 0x00);
    copper_word(emulator, wait_100);
    copper_word(emulator, move_map_b);
    copper_word(emulator, halt);
    nr_write(emulator, 0x62, 0xC0);  // reset each frame + run

    emulator.run_frame();

    const uint32_t* framebuffer = emulator.get_framebuffer();
    const uint32_t expected_before = emulator.palette().tilemap_colour(0x01);
    const uint32_t expected_after = emulator.palette().tilemap_colour(0x02);
    // GH #257: WAIT(cvc, h=0) completes at hc_ula 12 = whc 32, the start of
    // the paper of the raw line whose cvc it names (copper.vhd:94;
    // zxula_timing.vhd:423-436 hc_ula, :457-470 cvc, :474-490 whc), and the
    // fetcher re-latches the live NR 0x6E at each character's S_IDLE
    // (tilemap.vhd:309,345-350) — so hardware switches THAT row from x~35,
    // and jnext's row model applies it to that whole row: cvc + DISP_Y
    // (vblank_top == DISP_Y == 32 on this timing). It was pinned at +1 while
    // the tilemap latched at the START of the raw line.
    const int expected_transition = wait_cvc + Renderer::DISP_Y;
    int first_transition = -1;
    int transition_count = 0;
    int unexpected_row = -1;
    uint32_t unexpected_pixel = 0;
    uint32_t previous = framebuffer[Renderer::DISP_Y * Renderer::FB_WIDTH];
    for (int row = Renderer::DISP_Y; row < Renderer::FB_HEIGHT; ++row) {
        const uint32_t pixel = framebuffer[row * Renderer::FB_WIDTH];
        const uint32_t expected =
            row < expected_transition ? expected_before : expected_after;
        if (pixel != expected && unexpected_row < 0) {
            unexpected_row = row;
            unexpected_pixel = pixel;
        }
        if (row > Renderer::DISP_Y && pixel != previous) {
            ++transition_count;
            if (first_transition < 0)
                first_transition = row;
        }
        previous = pixel;
    }

    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "full Emulator/Copper path must produce one NR 0x6E transition "
                  "at row %d "
                  "(first=%d count=%d unexpected-row=%d pixel=%08X map=%02X)",
                  expected_transition, first_transition, transition_count,
                  unexpected_row, unexpected_pixel,
                  emulator.tilemap().get_map_base_raw());
    // TM-SPLIT-04 drives the same latch through Copper -> NextREG -> renderer
    // at jnext's per-scanline granularity.
    //
    // The citation lives INSIDE the call, like every other row in this file.
    // It sat in the comment above until GH #144 review round 3: that made this
    // a `named`-tier row, and since the fixture-init guard above reuses this
    // ID and is textually first, the matrix published the guard's line beside
    // a citation that named no call at all — the two columns disagreeing,
    // which is the defect the round was about.
    check("TM-SPLIT-04",
          // VHDL tilemap.vhd:264,349 — tm_map_base_q is latched whenever the
          // fetch FSM re-enters S_IDLE, which :264 forces once per tile COLUMN;
          // copper.vhd:94 + zxula_timing.vhd:423-436,474-490 put WAIT(n,0) at
          // whc 32 of row n+DISP_Y, before that row's later S_IDLEs (GH #257).
          unexpected_row < 0 &&
          transition_count == 1 &&
          first_transition == expected_transition,
          detail);
}

// ---------------------------------------------------------------------------
// GH #256 — the tilemap's OUTPUT-stage inputs follow the same per-scanline
// latch as the fetch inputs above.
//
// NR 0x4C (transp_colour_i) and NR 0x1B (clip_*_i) are not fetch inputs:
// tilemap.vhd:427 compares the pixel being displayed against transp_colour_i,
// and :412-424 re-latch the clip every 7 MHz and compare it against the
// display counters. On hardware a mid-line write therefore changes the rest of
// that line, and a NR 0x6E write issued beside it takes effect one tile later
// (the fetcher runs "one character ahead", tilemap.vhd:229, and latches the
// base at S_IDLE, :349). The two land within a tile of each other, so any
// per-scanline model must switch them on the SAME row. jnext's tilemap row for
// a write is the row of the raw line it executes in (TM-SPLIT-04, GH #257), so
// these do too.
//
// Before GH #256 both were read at their END-OF-FRAME value: a Copper split
// of either collapsed to one value for the whole frame.
//
// Colours are literals computed by hand, so an accessor that ignored its row
// cannot also move the expectation:
//   palette RRRGGGBB 0xE0 -> rgb333 (7,0,0) -> ARGB 0xFFFF0000  (index 1)
//   palette RRRGGGBB 0x1C -> rgb333 (0,7,0) -> ARGB 0xFF00FF00  (index 2)
//   NR 0x4A 0x03 (fallback, register format) -> ARGB 0xFF0000FF
// ---------------------------------------------------------------------------

constexpr uint32_t SPLIT_RED      = 0xFFFF0000u;
constexpr uint32_t SPLIT_GREEN    = 0xFF00FF00u;
constexpr uint32_t SPLIT_FALLBACK = 0xFF0000FFu;

// The row a Copper WAIT(cvc, h=0) + MOVE first shows on (see TM-SPLIT-04):
// the row of the raw line where the WAIT completes, at whc 32 (copper.vhd:94,
// zxula_timing.vhd:423-436,474-490). GH #257 — was cvc + DISP_Y + 1.
constexpr int split_row(int cvc) { return cvc + Renderer::DISP_Y; }

// Full-Emulator fixture shared by TM-165 and TM-SPLIT-05/06: ULA hidden, tilemap on in
// 40x32 with attribute bytes, map A (NR 0x6E=0x00) all tile 1 and map B
// (NR 0x6E=0x20) all tile 2, tile 1 = index 1 and tile 2 = index 2, fallback
// blue. The Copper program is loaded but not started.
bool split_fixture(Emulator& emulator, const char* id,
                   const uint16_t* copper, int copper_words) {
    EmulatorConfig config;
    config.type = MachineType::ZXN_ISSUE2;
    config.rewind_buffer_frames = 0;
    if (!emulator.init(config)) {
        check(id, false, "failed to initialize full Emulator fixture");
        return false;
    }
    park_cpu(emulator);

    uint8_t* bank5 = emulator.mmu().bank5_vram();
    for (int entry = 0; entry < 40 * 32; ++entry) {
        bank5[0x0000 + entry * 2]     = 1;
        bank5[0x0000 + entry * 2 + 1] = 0;
        bank5[0x2000 + entry * 2]     = 2;
        bank5[0x2000 + entry * 2 + 1] = 0;
    }
    std::memset(bank5 + 0x1000 + 1 * 32, 0x11, 32);
    std::memset(bank5 + 0x1000 + 2 * 32, 0x22, 32);

    paint_palette(emulator.palette(), 0x01, 0xE0);
    paint_palette(emulator.palette(), 0x02, 0x1C);

    nr_write(emulator, 0x4A, 0x03);  // fallback blue
    nr_write(emulator, 0x68, 0x80);  // hide ULA
    nr_write(emulator, 0x6B, 0x80);  // tilemap on, 40 columns, attributes
    nr_write(emulator, 0x6E, 0x00);  // map A at frame start
    nr_write(emulator, 0x6F, 0x10);  // shared definitions

    nr_write(emulator, 0x61, 0x00);
    nr_write(emulator, 0x62, 0x00);
    for (int i = 0; i < copper_words; ++i)
        copper_word(emulator, copper[i]);
    return true;
}

constexpr uint16_t cu_wait(int cvc) { return static_cast<uint16_t>(0x8000u | cvc); }
constexpr uint16_t cu_move(uint8_t reg, uint8_t val) {
    return static_cast<uint16_t>((reg << 8) | val);
}
constexpr uint16_t CU_HALT = static_cast<uint16_t>(0x8000u | 511u);

// Compare column `x` of every framebuffer row against `expected(row)`; report
// the first mismatch and how many rows differed.
template <typename Expected>
bool rows_match(const uint32_t* fb, int x, Expected expected,
                char* detail, size_t detail_size, const char* what) {
    int first_bad = -1, bad = 0;
    uint32_t got = 0, want = 0;
    for (int row = 0; row < Renderer::FB_HEIGHT; ++row) {
        const uint32_t pixel = fb[row * Renderer::FB_WIDTH + x];
        if (pixel != expected(row)) {
            if (first_bad < 0) {
                first_bad = row;
                got = pixel;
                want = expected(row);
            }
            ++bad;
        }
    }
    std::snprintf(detail, detail_size,
                  "%s: %d rows wrong, first row %d got %08X want %08X",
                  what, bad, first_bad, got, want);
    return bad == 0;
}

void test_transparency_index_split() {
    // Index 1 starts transparent, turns opaque at cvc 60 and transparent
    // again at cvc 140: two transitions, so neither the frame-start nor the
    // end-of-frame value alone can produce the band in the middle.
    const uint16_t copper[] = {
        cu_wait(60),  cu_move(0x4C, 0x0F),
        cu_wait(140), cu_move(0x4C, 0x01),
        CU_HALT,
    };
    Emulator emulator;
    if (!split_fixture(emulator, "TM-165", copper, 5))
        return;
    nr_write(emulator, 0x4C, 0x01);  // index 1 transparent at frame start
    nr_write(emulator, 0x62, 0xC0);  // reset each frame + run
    emulator.run_frame();

    char detail[256];
    const bool ok = rows_match(emulator.get_framebuffer(), 0,
        [](int row) {
            return (row >= split_row(60) && row < split_row(140))
                ? SPLIT_RED : SPLIT_FALLBACK;
        },
        detail, sizeof(detail), "NR 0x4C Copper split");
    check("TM-165",
          // VHDL tilemap.vhd:427 — pixel_en_standard_s compares the displayed
          // pixel against transp_colour_i (zxnext.vhd:4395 nr_4c), so the
          // index is live per pixel, never once per frame (GH #256). Row per
          // split_row(): copper.vhd:94, zxula_timing.vhd:423-436,474-490.
          ok, detail);
}

void test_map_and_transparency_coherent() {
    // The GH #256 program shape: MOVE NR 0x6E and MOVE NR 0x4C back to back.
    // Map A shows index 1 (visible while NR 0x4C = 2), map B shows index 2
    // (visible once NR 0x4C = 1). A row rendered from the new map with the
    // old index, or the reverse, is transparent and shows the fallback.
    const uint16_t copper[] = {
        cu_wait(100), cu_move(0x6E, 0x20), cu_move(0x4C, 0x01),
        CU_HALT,
    };
    Emulator emulator;
    if (!split_fixture(emulator, "TM-SPLIT-05", copper, 4))
        return;
    nr_write(emulator, 0x4C, 0x02);
    nr_write(emulator, 0x62, 0xC0);
    emulator.run_frame();

    char detail[256];
    const bool ok = rows_match(emulator.get_framebuffer(), 0,
        [](int row) { return row < split_row(100) ? SPLIT_RED : SPLIT_GREEN; },
        detail, sizeof(detail), "NR 0x6E + NR 0x4C switch together");
    check("TM-SPLIT-05",
          // VHDL tilemap.vhd:229,349,427 — the base is latched one tile ahead
          // of the pixel the index is compared against, so both writes reach
          // the display within one tile of each other: the same row. Row per
          // split_row(): copper.vhd:94, zxula_timing.vhd:423-436,474-490.
          ok, detail);
}

void test_clip_window_split() {
    // NR 0x1C b3 resets the tilemap clip index, then x1 = 0x10 moves the left
    // edge to 320-grid x = 32 (xsv = x1 & '0'): column 0 is clipped from the
    // split on, column 80 (grid x = 40) stays inside the window.
    const uint16_t copper[] = {
        cu_wait(100),
        cu_move(0x1C, 0x08),
        cu_move(0x1B, 0x10), cu_move(0x1B, 0x9F),
        cu_move(0x1B, 0x00), cu_move(0x1B, 0xFF),
        CU_HALT,
    };
    Emulator emulator;
    if (!split_fixture(emulator, "TM-SPLIT-06", copper, 7))
        return;
    nr_write(emulator, 0x62, 0xC0);
    emulator.run_frame();

    const uint32_t* fb = emulator.get_framebuffer();
    char detail[256], inside[256];
    const bool clipped = rows_match(fb, 0,
        [](int row) { return row < split_row(100) ? SPLIT_RED : SPLIT_FALLBACK; },
        detail, sizeof(detail), "column 0 (clipped from the split)");
    const bool kept = rows_match(fb, 80,
        [](int) { return SPLIT_RED; },
        inside, sizeof(inside), "column 80 (inside the window)");
    char both[512];
    std::snprintf(both, sizeof(both), "%s; %s", detail, inside);
    check("TM-SPLIT-06",
          // VHDL tilemap.vhd:412-424 — xsv/xev/ysv/yev re-latch clip_*_i every
          // 7 MHz and gate pixel_en_s against the display counters
          // (zxnext.vhd:4424-4427 nr_1b), so a mid-frame clip applies per line.
          // Row per split_row(): copper.vhd:94, zxula_timing.vhd:423-436,474-490.
          clipped && kept, both);
}

} // namespace

int main() {
    std::printf("Tilemap raster-split fetch-state regression tests\n");
    test_map_base_split();
    test_definition_base_split();
    test_default_attribute_split();
    test_emulator_copper_wiring();
    test_transparency_index_split();
    test_map_and_transparency_coherent();
    test_clip_window_split();
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
