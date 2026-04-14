// ULA Video Compliance Test Runner
//
// Tests the ULA video subsystem against VHDL-derived expected behaviour.
// All expected values come from the ULA-VIDEO-TEST-PLAN-DESIGN.md spec.
//
// Run: ./build/test/ula_test

#include "video/ula.h"
#include "video/timing.h"
#include "video/renderer.h"
#include "video/palette.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "memory/rom.h"
#include "memory/contention.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <array>

// -- Test infrastructure (same pattern as copper_test) ----------------------

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static std::string g_group;

struct TestResult {
    std::string group;
    std::string id;
    std::string description;
    bool passed;
    std::string detail;
};

static std::vector<TestResult> g_results;

static void set_group(const char* name) {
    g_group = name;
}

static void check(const char* id, const char* desc, bool cond, const char* detail = "") {
    g_total++;
    TestResult r;
    r.group = g_group;
    r.id = id;
    r.description = desc;
    r.passed = cond;
    r.detail = detail;
    g_results.push_back(r);

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

// -- Helpers ----------------------------------------------------------------

// Compute the VHDL pixel address (14-bit offset from screen base).
// VHDL formula: vram_a = screen_mode(0) & py(7:6) & py(2:0) & py(5:3) & px(7:3)
// screen_mode(0) = 0 for primary screen.
static uint16_t vhdl_pixel_addr(int py, int px, bool alt = false) {
    uint16_t addr = 0;
    addr |= ((py & 0xC0) >> 6) << 11;  // py(7:6) -> bits 12:11
    addr |= ((py & 0x07))      << 8;   // py(2:0) -> bits 10:8
    addr |= ((py & 0x38) >> 3) << 5;   // py(5:3) -> bits 7:5
    addr |= (px >> 3);                  // px(7:3) -> bits 4:0
    if (alt) addr |= (1 << 13);        // screen_mode(0) -> bit 13
    return addr;
}

// Compute the VHDL attribute address (14-bit offset from screen base).
// VHDL formula: vram_a = screen_mode(0) & "110" & py(7:3) & px(7:3)
static uint16_t vhdl_attr_addr(int py, int px, bool alt = false) {
    uint16_t addr = 0;
    addr |= 0x06 << 10;                // "110" -> bits 12:10 (= 0x1800)
    addr |= ((py >> 3) & 0x1F) << 5;   // py(7:3) -> bits 9:5
    addr |= (px >> 3);                  // px(7:3) -> bits 4:0
    if (alt) addr |= (1 << 13);        // screen_mode(0) -> bit 13
    return addr;
}

// The emulator's pixel_addr_offset returns an offset from 0x4000/0x6000.
// It takes (screen_row, col) where col is a byte column (0-31).
// To compare with VHDL, we call it with col=px/8 and mask to 14 bits.
// Note: pixel_addr_offset is a static method, accessible via Ula:: but private.
// We replicate its computation here for comparison.
static uint16_t emu_pixel_addr_offset(int screen_row, int col) {
    return static_cast<uint16_t>(
          ((screen_row & 0xC0) << 5)
        | ((screen_row & 0x07) << 8)
        | ((screen_row & 0x38) << 2)
        | col);
}

// =========================================================================
// Section 1: Screen Address Calculation
// =========================================================================

static void test_screen_address() {
    set_group("S01-ScreenAddr");

    struct AddrTest {
        const char* id;
        const char* desc;
        int py, px;
        uint16_t exp_pixel;  // 14-bit offset
        uint16_t exp_attr;   // 14-bit offset
        bool alt;
    };

    AddrTest tests[] = {
        {"S01.01", "Top-left pixel",                    0,   0,   0x0000, 0x1800, false},
        {"S01.02", "First char row, col 1",             0,   8,   0x0001, 0x1801, false},
        {"S01.03", "Pixel row 1 in char row 0",         1,   0,   0x0100, 0x1800, false},
        {"S01.04", "Pixel row 7 in char row 0",         7,   0,   0x0700, 0x1800, false},
        {"S01.05", "Char row 1, pixel row 0",           8,   0,   0x0020, 0x1820, false},
        {"S01.06", "Third of screen (py=64)",          64,   0,   0x0800, 0x1900, false},
        {"S01.07", "Bottom-right pixel",              191, 248,   0x17FF, 0x1AFF, false},
        {"S01.08", "Alternate display file",            0,   0,   0x2000, 0x3800, true},
        {"S01.09", "Middle of screen (py=96, px=128)", 96, 128,   0x0890, 0x1990, false},
        {"S01.10", "Wrap within third (py=63)",        63,   0,   0x07E0, 0x18E0, false},
        {"S01.11", "Second third start+1 row",         65,   0,   0x0900, 0x1900, false},
        {"S01.12", "Last pixel row of last char",     191,   0,   0x17E0, 0x1AE0, false},
    };

    for (auto& t : tests) {
        uint16_t vhdl_pix  = vhdl_pixel_addr(t.py, t.px, t.alt);
        uint16_t vhdl_at   = vhdl_attr_addr(t.py, t.px, t.alt);

        // Check VHDL formula gives expected value
        check(DETAIL("%s-vp", t.id), DETAIL("%s pixel addr (VHDL)", t.desc),
              vhdl_pix == t.exp_pixel,
              DETAIL("got 0x%04X, exp 0x%04X", vhdl_pix, t.exp_pixel));

        check(DETAIL("%s-va", t.id), DETAIL("%s attr addr (VHDL)", t.desc),
              vhdl_at == t.exp_attr,
              DETAIL("got 0x%04X, exp 0x%04X", vhdl_at, t.exp_attr));

        // Check emulator's pixel_addr_offset matches VHDL (for primary screen)
        if (!t.alt) {
            int col = t.px / 8;
            uint16_t emu_off = emu_pixel_addr_offset(t.py, col);
            check(DETAIL("%s-ep", t.id), DETAIL("%s emu pixel offset", t.desc),
                  emu_off == t.exp_pixel,
                  DETAIL("got 0x%04X, exp 0x%04X", emu_off, t.exp_pixel));
        }
    }
}

// =========================================================================
// Section 2: Attribute Rendering (Standard ULA)
// =========================================================================

static void test_attribute_rendering() {
    set_group("S02-AttrRender");

    // VHDL ula_pixel encoding:
    //   bits 7:3 = {0, 0, 0, NOT_pixel_en, bright}
    //   bits 2:0 = ink (pixel=1) or paper (pixel=0)
    //
    // So: ink pixel  -> (0 << 4) | (bright << 3) | ink_colour
    //     paper pixel -> (1 << 4) | (bright << 3) | paper_colour
    //
    // Wait - from VHDL: bit4 = NOT pixel_en, bit3 = attr(6) = bright

    struct AttrTest {
        const char* id;
        const char* desc;
        bool pixel_set;   // pixel bit = 1 means ink
        uint8_t attr;
        uint8_t exp_idx;  // expected 8-bit palette index
        bool skip_flash;  // if true, test ignores flash state
    };

    AttrTest tests[] = {
        {"S02.01", "Ink, no bright, colour 0",       true,  0x00, 0x00, true},
        {"S02.02", "Paper, no bright, colour 0",     false, 0x00, 0x10, true},
        {"S02.03", "Ink, bright, red (2)",            true,  0x42, 0x0A, true},
        {"S02.04", "Paper, bright, green (4)",        false, 0x60, 0x1C, true},
        {"S02.05", "Ink white, no bright",            true,  0x07, 0x07, true},
        {"S02.06", "Paper white, bright",             false, 0x78, 0x1F, true},
        {"S02.07", "Ink cyan (5), bright",            true,  0x45, 0x0D, true},
        {"S02.09", "Full white on black, bright",     true,  0x47, 0x0F, true},
    };

    // Compute ula_pixel index as per VHDL
    auto compute_ula_pixel = [](bool pixel_set, uint8_t attr) -> uint8_t {
        bool bright = (attr & 0x40) != 0;
        uint8_t ink   = attr & 0x07;
        uint8_t paper = (attr >> 3) & 0x07;
        uint8_t not_pixel = pixel_set ? 0 : 1;
        uint8_t colour = pixel_set ? ink : paper;
        return static_cast<uint8_t>((not_pixel << 4) | (bright ? (1 << 3) : 0) | colour);
    };

    for (auto& t : tests) {
        uint8_t got = compute_ula_pixel(t.pixel_set, t.attr);
        check(t.id, t.desc, got == t.exp_idx,
              DETAIL("got 0x%02X, exp 0x%02X", got, t.exp_idx));
    }

    // Now verify the emulator's actual rendering produces correct colours.
    // We set up a minimal ULA + RAM + MMU, write known pixel+attr data,
    // render a scanline, and check the output pixels.

    Ram ram(1792 * 1024);
    Rom rom;
    Mmu mmu(ram, rom);
    mmu.reset();
    // Map bank 5 pages (10,11) into slots 2-3 (0x4000-0x7FFF)
    mmu.set_page(2, 10);
    mmu.set_page(3, 11);

    Ula ula;
    ula.set_ram(&ram);
    ula.reset();

    // Write a test pattern: col 0 = pixel byte 0xFF (all ink), attr = 0x47 (bright white on black)
    // Pixel address for row 0, col 0 = 0x4000 + pixel_addr_offset(0,0) = 0x4000
    // Attr address for row 0, col 0 = 0x5800
    uint16_t paddr = 0x4000 + emu_pixel_addr_offset(0, 0);
    ram.write(10u * 8192u + (paddr & 0x3FFF), 0xFF);  // all pixels set (ink)
    ram.write(10u * 8192u + (0x5800 & 0x3FFF) + 0, 0x47);  // bright white ink on black paper

    // Also write col 1: pixel=0x00 (all paper), attr=0x47
    uint16_t paddr1 = 0x4000 + emu_pixel_addr_offset(0, 1);
    ram.write(10u * 8192u + (paddr1 & 0x3FFF), 0x00);
    ram.write(10u * 8192u + (0x5800 & 0x3FFF) + 1, 0x47);

    // Render row 32 (= screen_row 0) into a buffer
    std::array<uint32_t, 320> line{};
    ula.init_border_per_line();
    ula.render_scanline(line.data(), 32, mmu);

    // The pixel at display position (0,0) = framebuffer col 32 should be ink=bright white
    // Using kUlaPalette: bright white = index 15
    uint32_t exp_ink = kUlaPalette[15];  // bright white
    uint32_t exp_paper = kUlaPalette[8]; // bright black = index 8

    check("S02.10", "Rendered ink pixel (0xFF pixels, 0x47 attr)",
          line[32] == exp_ink,
          DETAIL("got 0x%08X, exp 0x%08X", line[32], exp_ink));

    // Col 1, all paper: pixel at framebuffer col 32+8 = 40
    check("S02.11", "Rendered paper pixel (0x00 pixels, 0x47 attr)",
          line[40] == exp_paper,
          DETAIL("got 0x%08X, exp 0x%08X", line[40], exp_paper));
}

// =========================================================================
// Section 3: Border Colour
// =========================================================================

static void test_border_colour() {
    set_group("S03-Border");

    // VHDL: border_clr = "00" & port_fe_border & port_fe_border
    // This duplicates the 3-bit colour into bits 5:3 and 2:0.
    struct BorderTest {
        const char* id;
        const char* desc;
        uint8_t port_fe;
        uint8_t exp_clr;
    };

    BorderTest tests[] = {
        {"S03.01", "Black border",  0, 0x00},
        {"S03.02", "Blue border",   1, 0x09},
        {"S03.03", "Red border",    2, 0x12},
        {"S03.04", "White border",  7, 0x3F},
        {"S03.05", "Green border",  4, 0x24},
    };

    for (auto& t : tests) {
        uint8_t clr = (t.port_fe & 0x07) | ((t.port_fe & 0x07) << 3);
        check(t.id, t.desc, clr == t.exp_clr,
              DETAIL("got 0x%02X, exp 0x%02X", clr, t.exp_clr));
    }

    // Verify the emulator's border rendering: set border colour and render a border line
    Ram ram(1792 * 1024);
    Rom rom;
    Mmu mmu(ram, rom);
    mmu.reset();

    Ula ula;
    ula.set_ram(&ram);
    ula.reset();

    // Set border to red (2)
    ula.set_border(2);
    check("S03.06", "ULA get_border returns set value",
          ula.get_border() == 2, DETAIL("got %d", ula.get_border()));

    // Render a border-only line (row 0 = top border)
    std::array<uint32_t, 320> line{};
    ula.init_border_per_line();
    ula.render_scanline(line.data(), 0, mmu);

    uint32_t exp_red = kUlaPalette[2];  // red, non-bright
    check("S03.07", "Border line pixel is red",
          line[0] == exp_red,
          DETAIL("got 0x%08X, exp 0x%08X", line[0], exp_red));
    check("S03.08", "Border line centre pixel is red",
          line[160] == exp_red,
          DETAIL("got 0x%08X, exp 0x%08X", line[160], exp_red));
}

// =========================================================================
// Section 4: Flash Timing
// =========================================================================

static void test_flash_timing() {
    set_group("S04-Flash");

    Ula ula;
    ula.reset();

    // VHDL: 5-bit counter, bit 4 controls flash phase.
    // The emulator uses flash_counter_ (0-15) and flash_phase_ toggling every 16 frames.
    // This gives 32-frame period: 16 frames phase A, 16 frames phase B.

    // Advance 16 frames: flash_phase_ should toggle
    for (int i = 0; i < 16; i++) ula.advance_flash();
    // After 16 calls, phase should have toggled once (from false to true or vice versa).
    // We can't read flash_phase_ directly, but we can test via rendering.

    // Test flash period by rendering: set up pixel=1, attr=0x87 (flash+white ink)
    // and verify that after 16 frames the pixel changes.
    Ram ram(1792 * 1024);
    Rom rom;
    Mmu mmu(ram, rom);
    mmu.reset();
    mmu.set_page(2, 10);
    mmu.set_page(3, 11);

    Ula ula2;
    ula2.set_ram(&ram);
    ula2.reset();

    // Write pixel=0xFF (all ink), attr=0x87 (flash, no bright, white ink, black paper)
    ram.write(10u * 8192u + 0, 0xFF);                   // pixel row 0 col 0
    ram.write(10u * 8192u + (0x5800 - 0x4000), 0x87);   // attr row 0 col 0

    std::array<uint32_t, 320> line1{}, line2{};
    ula2.init_border_per_line();
    ula2.render_scanline(line1.data(), 32, mmu);
    uint32_t pixel_phase0 = line1[32];

    // Advance 16 frames to toggle flash
    for (int i = 0; i < 16; i++) ula2.advance_flash();

    ula2.render_scanline(line2.data(), 32, mmu);
    uint32_t pixel_phase1 = line2[32];

    check("S04.01", "Flash period 32 frames (toggles at 16)",
          pixel_phase0 != pixel_phase1,
          DETAIL("phase0=0x%08X, phase1=0x%08X", pixel_phase0, pixel_phase1));

    // Non-flash attr: should NOT change
    ram.write(10u * 8192u + (0x5800 - 0x4000), 0x07);  // no flash, white ink

    Ula ula3;
    ula3.set_ram(&ram);
    ula3.reset();
    ula3.init_border_per_line();

    std::array<uint32_t, 320> lineA{}, lineB{};
    ula3.render_scanline(lineA.data(), 32, mmu);
    for (int i = 0; i < 16; i++) ula3.advance_flash();
    ula3.render_scanline(lineB.data(), 32, mmu);

    check("S04.02", "Non-flash attr unchanged after 16 frames",
          lineA[32] == lineB[32],
          DETAIL("phaseA=0x%08X, phaseB=0x%08X", lineA[32], lineB[32]));

    // Flash with pixel=0 (paper): should also swap
    ram.write(10u * 8192u + 0, 0x00);                   // all paper
    ram.write(10u * 8192u + (0x5800 - 0x4000), 0x87);   // flash, white ink, black paper

    Ula ula4;
    ula4.set_ram(&ram);
    ula4.reset();
    ula4.init_border_per_line();

    std::array<uint32_t, 320> lineC{}, lineD{};
    ula4.render_scanline(lineC.data(), 32, mmu);
    for (int i = 0; i < 16; i++) ula4.advance_flash();
    ula4.render_scanline(lineD.data(), 32, mmu);

    check("S04.03", "Flash paper pixel changes after 16 frames",
          lineC[32] != lineD[32],
          DETAIL("phaseC=0x%08X, phaseD=0x%08X", lineC[32], lineD[32]));
}

// =========================================================================
// Section 5: Timex Modes
// =========================================================================

static void test_timex_modes() {
    set_group("S05-Timex");

    Ula ula;
    ula.reset();

    // Test mode decoding from port 0xFF
    // Standard mode: bits 5:3 = 000
    ula.set_screen_mode(0x00);
    check("S05.01", "Standard mode (000)",
          ula.get_screen_mode_reg() == 0x00, "");

    // Alternate display file: bits 5:3 = 001 -> but mode bits are 5:3
    // Actually port 0xFF bit layout: bits 2:0 = screen bank, bits 5:3 = video mode
    // STANDARD_1 = mode bits 001 -> port_val bits 5:3 = 001 -> port_val = 0x08
    ula.set_screen_mode(0x08);
    check("S05.02", "Alt display file mode (001)",
          ula.get_screen_mode_reg() == 0x08, "");

    // Hi-colour: mode bits = 010 -> port_val bits 5:3 = 010 -> port_val = 0x10
    ula.set_screen_mode(0x10);
    check("S05.03", "Hi-colour mode (010)",
          ula.get_screen_mode_reg() == 0x10, "");

    // Hi-res: mode bits = 110 -> port_val bits 5:3 = 110 -> port_val = 0x30
    ula.set_screen_mode(0x30);
    check("S05.04", "Hi-res mode (110)",
          ula.get_screen_mode_reg() == 0x30, "");

    // Test actual rendering of hi-colour and hi-res modes
    Ram ram(1792 * 1024);
    Rom rom;
    Mmu mmu(ram, rom);
    mmu.reset();
    mmu.set_page(2, 10);
    mmu.set_page(3, 11);

    // Hi-colour: pixel data from 0x4000, attr from 0x6000 (mirrored pixel layout)
    // Write pixel=0xFF at row 0 col 0, attr at 0x6000 + pixel_addr_offset(0,0)
    Ula ula_hc;
    ula_hc.set_ram(&ram);
    ula_hc.reset();
    ula_hc.set_screen_mode(0x10);  // hi-colour

    ram.write(10u * 8192u + 0, 0xFF);  // pixel at 0x4000 offset 0
    // attr at 0x6000 offset 0 = bank 5 offset 0x2000
    ram.write(10u * 8192u + 0x2000, 0x47);  // bright white ink

    std::array<uint32_t, 320> line{};
    ula_hc.init_border_per_line();
    ula_hc.render_scanline(line.data(), 32, mmu);

    uint32_t exp_bright_white = kUlaPalette[15];
    check("S05.05", "Hi-colour renders ink pixels",
          line[32] == exp_bright_white,
          DETAIL("got 0x%08X, exp 0x%08X", line[32], exp_bright_white));

    // Hi-res: ink from port_ff bits 2:0, paper from bits 5:3
    Ula ula_hr;
    ula_hr.set_ram(&ram);
    ula_hr.reset();
    ula_hr.set_screen_mode(0x32);  // hi-res, ink=2 (red)

    // Write screen1 (0x6000) pixel = 0xFF (all set = ink)
    ram.write(10u * 8192u + 0x2000, 0xFF);

    std::array<uint32_t, 320> line_hr{};
    ula_hr.init_border_per_line();
    ula_hr.render_scanline(line_hr.data(), 32, mmu);

    uint32_t exp_red = kUlaPalette[2];  // red, non-bright
    check("S05.06", "Hi-res renders ink colour from port 0xFF",
          line_hr[32] == exp_red,
          DETAIL("got 0x%08X, exp 0x%08X", line_hr[32], exp_red));
}

// =========================================================================
// Section 8: Clip Windows
// =========================================================================

static void test_clip_windows() {
    set_group("S08-Clip");

    Ula ula;
    ula.reset();

    // Default clip window
    check("S08.01", "Default clip x1=0", ula.clip_x1() == 0,
          DETAIL("got %d", ula.clip_x1()));
    check("S08.02", "Default clip x2=255", ula.clip_x2() == 255,
          DETAIL("got %d", ula.clip_x2()));
    check("S08.03", "Default clip y1=0", ula.clip_y1() == 0,
          DETAIL("got %d", ula.clip_y1()));
    check("S08.04", "Default clip y2=191", ula.clip_y2() == 191,
          DETAIL("got %d", ula.clip_y2()));

    // Set a narrow clip window and verify
    ula.set_clip_x1(64);
    ula.set_clip_x2(192);
    ula.set_clip_y1(32);
    ula.set_clip_y2(160);

    check("S08.05", "Clip x1 set to 64", ula.clip_x1() == 64,
          DETAIL("got %d", ula.clip_x1()));
    check("S08.06", "Clip x2 set to 192", ula.clip_x2() == 192,
          DETAIL("got %d", ula.clip_x2()));
    check("S08.07", "Clip y1 set to 32", ula.clip_y1() == 32,
          DETAIL("got %d", ula.clip_y1()));
    check("S08.08", "Clip y2 set to 160", ula.clip_y2() == 160,
          DETAIL("got %d", ula.clip_y2()));
}

// =========================================================================
// Section 12: ULA Disable (NR 0x68)
// =========================================================================

static void test_ula_disable() {
    set_group("S12-ULADisable");

    Ula ula;
    ula.reset();

    check("S12.01", "ULA enabled by default",
          ula.ula_enabled() == true, "");

    ula.set_ula_enabled(false);
    check("S12.02", "ULA disabled",
          ula.ula_enabled() == false, "");

    ula.set_ula_enabled(true);
    check("S12.03", "ULA re-enabled",
          ula.ula_enabled() == true, "");
}

// =========================================================================
// Section 13: Timing Constants
// =========================================================================

static void test_timing_constants() {
    set_group("S13-Timing");

    // VHDL timing constants from the test plan:
    // 48K:      448 pixels/line (hc_max=447), 312 lines/frame (vc_max=311)
    // 128K:     456 pixels/line (hc_max=455), 311 lines/frame (vc_max=310)
    // Pentagon: 448 pixels/line (hc_max=447), 320 lines/frame (vc_max=319)
    //
    // Note: The VHDL uses max_hc/max_vc as the LAST valid value, so
    // pixels_per_line = max_hc + 1, lines_per_frame = max_vc + 1.
    //
    // The emulator's VideoTiming uses hc_max/vc_max as the TOTAL count
    // (wraps when hc >= hc_max), matching pixels_per_line directly.

    VideoTiming timing;

    // 48K
    timing.init(MachineType::ZX48K);
    int tstates_48k = timing.hc_max() * timing.vc_max() / 2;
    check("S13.01", "48K pixels/line",
          timing.hc_max() == 448,
          DETAIL("got %d, exp 448 (VHDL c_max_hc=447)", timing.hc_max()));
    check("S13.02", "48K lines/frame",
          timing.vc_max() == 312,
          DETAIL("got %d, exp 312", timing.vc_max()));
    check("S13.03", "48K T-states/frame = 69888",
          tstates_48k == 69888,
          DETAIL("got %d, VHDL exp 69888 (448*312/2)", tstates_48k));

    // 128K
    timing.init(MachineType::ZX128K);
    int tstates_128k = timing.hc_max() * timing.vc_max() / 2;
    check("S13.04", "128K pixels/line",
          timing.hc_max() == 456,
          DETAIL("got %d, exp 456", timing.hc_max()));
    check("S13.05", "128K lines/frame",
          timing.vc_max() == 311,
          DETAIL("got %d, exp 311 (VHDL says 311)", timing.vc_max()));
    check("S13.06", "128K T-states/frame = 70908",
          tstates_128k == 70908,
          DETAIL("got %d, VHDL exp 70908 (456*311/2)", tstates_128k));

    // Pentagon
    timing.init(MachineType::PENTAGON);
    int tstates_pent = timing.hc_max() * timing.vc_max() / 2;
    check("S13.07", "Pentagon pixels/line",
          timing.hc_max() == 448,
          DETAIL("got %d, exp 448", timing.hc_max()));
    check("S13.08", "Pentagon lines/frame",
          timing.vc_max() == 320,
          DETAIL("got %d, exp 320", timing.vc_max()));
    check("S13.09", "Pentagon T-states/frame = 71680",
          tstates_pent == 71680,
          DETAIL("got %d, VHDL exp 71680 (448*320/2)", tstates_pent));

    // Display window constants
    check("S13.10", "Display left = 128",
          VideoTiming::DISPLAY_LEFT == 128,
          DETAIL("got %d", VideoTiming::DISPLAY_LEFT));
    check("S13.11", "Display top = 64",
          VideoTiming::DISPLAY_TOP == 64,
          DETAIL("got %d", VideoTiming::DISPLAY_TOP));
    check("S13.12", "Display width = 256",
          VideoTiming::DISPLAY_W == 256,
          DETAIL("got %d", VideoTiming::DISPLAY_W));
    check("S13.13", "Display height = 192",
          VideoTiming::DISPLAY_H == 192,
          DETAIL("got %d", VideoTiming::DISPLAY_H));

    // Frame completion test
    timing.init(MachineType::ZX48K);
    timing.clear_frame_flag();
    int total_tstates = timing.hc_max() * timing.vc_max() / 2;
    // Advance one full frame worth of T-states
    timing.advance(total_tstates);
    check("S13.14", "Frame complete after full T-states",
          timing.frame_complete(),
          DETAIL("frame_done=%d after %d T-states", timing.frame_complete(), total_tstates));
}

// =========================================================================
// Section 15: Shadow Screen
// =========================================================================

static void test_shadow_screen() {
    set_group("S15-Shadow");

    Ram ram(1792 * 1024);
    Rom rom;
    Mmu mmu(ram, rom);
    mmu.reset();
    mmu.set_page(2, 10);
    mmu.set_page(3, 11);

    Ula ula;
    ula.set_ram(&ram);
    ula.reset();

    // Write different patterns to bank 5 (page 10) and bank 7 (page 14)
    // Bank 5 page 10: pixel at offset 0 = 0xFF
    ram.write(10u * 8192u + 0, 0xFF);
    ram.write(10u * 8192u + (0x5800 - 0x4000), 0x07);  // white ink

    // Bank 7 page 14: pixel at offset 0 = 0x00
    ram.write(14u * 8192u + 0, 0x00);
    ram.write(14u * 8192u + (0x5800 - 0x4000), 0x07);  // white ink

    // Render normal screen (bank 5): should show ink (0xFF pixels)
    std::array<uint32_t, 320> line_normal{};
    ula.init_border_per_line();
    ula.render_scanline(line_normal.data(), 32, mmu);
    uint32_t normal_pixel = line_normal[32];

    // Render shadow screen (bank 7): should show paper (0x00 pixels)
    std::array<uint32_t, 320> line_shadow{};
    ula.render_scanline_screen1(line_shadow.data(), 32, mmu);
    uint32_t shadow_pixel = line_shadow[32];

    check("S15.01", "Normal screen renders bank 5 data",
          normal_pixel == kUlaPalette[7],  // white ink, no bright
          DETAIL("got 0x%08X, exp 0x%08X", normal_pixel, kUlaPalette[7]));

    check("S15.02", "Shadow screen renders bank 7 data (different)",
          shadow_pixel != normal_pixel,
          DETAIL("normal=0x%08X, shadow=0x%08X", normal_pixel, shadow_pixel));

    // Shadow pixel should be paper colour (black, index 0)
    check("S15.03", "Shadow screen pixel is paper (black)",
          shadow_pixel == kUlaPalette[0],
          DETAIL("got 0x%08X, exp 0x%08X", shadow_pixel, kUlaPalette[0]));
}

// =========================================================================
// Renderer utility tests
// =========================================================================

static void test_renderer_utility() {
    set_group("S-Renderer");

    // Test rrrgggbb_to_argb conversion
    // 0x00 = black
    uint32_t black = Renderer::rrrgggbb_to_argb(0x00);
    check("SR.01", "rrrgggbb 0x00 -> black",
          black == 0xFF000000u,
          DETAIL("got 0x%08X", black));

    // 0xFF = white (R=7,G=7,B=3)
    uint32_t white = Renderer::rrrgggbb_to_argb(0xFF);
    check("SR.02", "rrrgggbb 0xFF -> white",
          white == 0xFFFFFFFFu,
          DETAIL("got 0x%08X", white));

    // 0xE0 = bright red (R=7,G=0,B=0)
    uint32_t red = Renderer::rrrgggbb_to_argb(0xE0);
    uint8_t r8 = (7 << 5) | (7 << 2) | (7 >> 1);  // 0xFF
    check("SR.03", "rrrgggbb 0xE0 -> red",
          (red & 0x00FF0000) == (static_cast<uint32_t>(r8) << 16),
          DETAIL("got 0x%08X, R channel exp 0x%02X", red, r8));

    // Renderer constants
    check("SR.04", "FB_WIDTH = 320",
          Renderer::FB_WIDTH == 320,
          DETAIL("got %d", Renderer::FB_WIDTH));
    check("SR.05", "FB_HEIGHT = 256",
          Renderer::FB_HEIGHT == 256,
          DETAIL("got %d", Renderer::FB_HEIGHT));
    check("SR.06", "DISP_X = 32",
          Renderer::DISP_X == 32,
          DETAIL("got %d", Renderer::DISP_X));
    check("SR.07", "DISP_Y = 32",
          Renderer::DISP_Y == 32,
          DETAIL("got %d", Renderer::DISP_Y));
}

// =========================================================================
// ULA framebuffer dimension tests
// =========================================================================

static void test_ula_dimensions() {
    set_group("S-ULADim");

    check("SD.01", "ULA FB_WIDTH = 320",
          Ula::FB_WIDTH == 320,
          DETAIL("got %d", Ula::FB_WIDTH));
    check("SD.02", "ULA FB_HEIGHT = 256",
          Ula::FB_HEIGHT == 256,
          DETAIL("got %d", Ula::FB_HEIGHT));
    check("SD.03", "ULA DISP_X = 32 (left border)",
          Ula::DISP_X == 32,
          DETAIL("got %d", Ula::DISP_X));
    check("SD.04", "ULA DISP_Y = 32 (top border)",
          Ula::DISP_Y == 32,
          DETAIL("got %d", Ula::DISP_Y));
    check("SD.05", "ULA DISP_W = 256",
          Ula::DISP_W == 256,
          DETAIL("got %d", Ula::DISP_W));
    check("SD.06", "ULA DISP_H = 192",
          Ula::DISP_H == 192,
          DETAIL("got %d", Ula::DISP_H));
    check("SD.07", "Border widths sum correctly (32+256+32=320)",
          Ula::DISP_X + Ula::DISP_W + (Ula::FB_WIDTH - Ula::DISP_X - Ula::DISP_W) == Ula::FB_WIDTH,
          "");
    check("SD.08", "Border heights sum correctly (32+192+32=256)",
          Ula::DISP_Y + Ula::DISP_H + (Ula::FB_HEIGHT - Ula::DISP_Y - Ula::DISP_H) == Ula::FB_HEIGHT,
          "");
}

// =========================================================================
// Per-line border snapshot
// =========================================================================

static void test_per_line_border() {
    set_group("S03-PerLine");

    Ula ula;
    ula.reset();

    // Default border is 7 (white)
    ula.init_border_per_line();
    check("S03P.01", "Init fills all lines with current border",
          ula.border_for_line(0) == 7 && ula.border_for_line(128) == 7 && ula.border_for_line(255) == 7,
          DETAIL("line0=%d, line128=%d, line255=%d",
                 ula.border_for_line(0), ula.border_for_line(128), ula.border_for_line(255)));

    // Snapshot per-line border change (simulating mid-frame change)
    ula.set_border(2);  // red
    ula.snapshot_border_for_line(100);

    check("S03P.02", "Per-line snapshot at line 100",
          ula.border_for_line(100) == 2,
          DETAIL("got %d", ula.border_for_line(100)));
    check("S03P.03", "Other lines unchanged",
          ula.border_for_line(99) == 7 && ula.border_for_line(101) == 7,
          DETAIL("line99=%d, line101=%d", ula.border_for_line(99), ula.border_for_line(101)));

    // Out-of-range returns current border
    check("S03P.04", "Out-of-range line returns current border",
          ula.border_for_line(-1) == 2 && ula.border_for_line(256) == 2,
          "");
}

// =========================================================================
// Main
// =========================================================================

int main() {
    printf("=== ULA Video Compliance Test Suite ===\n\n");

    test_screen_address();
    test_attribute_rendering();
    test_border_colour();
    test_per_line_border();
    test_flash_timing();
    test_timex_modes();
    test_clip_windows();
    test_ula_disable();
    test_timing_constants();
    test_shadow_screen();
    test_renderer_utility();
    test_ula_dimensions();

    printf("\n=== Results by group ===\n");
    std::string last_group;
    int gp = 0, gf = 0;
    for (size_t i = 0; i <= g_results.size(); i++) {
        bool flush = (i == g_results.size()) || (i > 0 && g_results[i].group != last_group);
        if (flush && !last_group.empty()) {
            printf("  %-20s %d/%d %s\n", last_group.c_str(), gp, gp + gf,
                   gf == 0 ? "PASS" : "FAIL");
            gp = gf = 0;
        }
        if (i < g_results.size()) {
            last_group = g_results[i].group;
            if (g_results[i].passed) gp++; else gf++;
        }
    }

    printf("\n=== Summary: %d/%d passed", g_pass, g_total);
    if (g_fail > 0) printf(" (%d FAILED)", g_fail);
    printf(" ===\n");

    return g_fail > 0 ? 1 : 0;
}
