// Layer 2 Compliance Test Runner
//
// Tests the Layer 2 subsystem against VHDL-derived expected behaviour.
// All expected values come from LAYER2-TEST-PLAN-DESIGN.md.
//
// Run: ./build/test/layer2_test

#include "video/layer2.h"
#include "video/palette.h"
#include "memory/ram.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

// -- Test infrastructure (same format as copper_test) ----------------------

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

// -- Helpers ---------------------------------------------------------------

// Fill L2 VRAM for 256x192 mode at given bank. Row-major: addr = y*256 + x
static void fill_256x192(Ram& ram, uint8_t bank, uint8_t pattern_fn_id) {
    for (int y = 0; y < 192; ++y) {
        for (int x = 0; x < 256; ++x) {
            uint32_t addr = static_cast<uint32_t>(bank) * 16384 +
                            static_cast<uint32_t>(y) * 256 + x;
            uint8_t val = 0;
            if (pattern_fn_id == 0) val = static_cast<uint8_t>(y);       // horizontal bands
            else if (pattern_fn_id == 1) val = static_cast<uint8_t>(x);  // vertical bands
            else if (pattern_fn_id == 2) val = 0x42;                     // solid fill
            ram.write(addr, val);
        }
    }
}

// Fill L2 VRAM for 320x256 mode at given bank. Column-major: addr = x*256 + y
static void fill_320x256(Ram& ram, uint8_t bank, uint8_t fill_val) {
    for (int x = 0; x < 320; ++x) {
        for (int y = 0; y < 256; ++y) {
            uint32_t addr = static_cast<uint32_t>(bank) * 16384 +
                            static_cast<uint32_t>(x) * 256 + y;
            ram.write(addr, fill_val);
        }
    }
}

// Fill L2 VRAM for 640x256 mode at given bank. Column-major, 2px/byte
static void fill_640x256(Ram& ram, uint8_t bank, uint8_t fill_byte) {
    for (int col = 0; col < 320; ++col) {
        for (int y = 0; y < 256; ++y) {
            uint32_t addr = static_cast<uint32_t>(bank) * 16384 +
                            static_cast<uint32_t>(col) * 256 + y;
            ram.write(addr, fill_byte);
        }
    }
}

// Scanline buffer dimensions
static constexpr int BUF_WIDTH = 640;
static constexpr int DISP_X_256 = 32;  // 256x192 mode display offset
static constexpr int DISP_Y_256 = 32;  // 256x192 mode display offset

// =========================================================================
// Group 10: Reset Defaults
// =========================================================================

static void test_reset_defaults() {
    set_group("10. Reset Defaults");

    Layer2 l2;
    l2.reset();

    check("10.1", "NR 0x12 default = bank 8",
          l2.active_bank() == 8,
          DETAIL("got %d", l2.active_bank()));

    check("10.2", "NR 0x13 default = bank 11",
          l2.shadow_bank() == 11,
          DETAIL("got %d", l2.shadow_bank()));

    // scroll_x and scroll_y aren't directly readable via getter, test via
    // rendering behaviour (scroll=0 means no shift).
    // But we can verify resolution and palette offset via set_control.
    check("10.3", "Resolution default = 0 (256x192)",
          l2.resolution() == 0,
          DETAIL("got %d", l2.resolution()));

    check("10.4", "Layer 2 disabled at reset",
          l2.enabled() == false,
          DETAIL("got %s", l2.enabled() ? "true" : "false"));

    check("10.5", "is_wide() = false at reset",
          l2.is_wide() == false,
          DETAIL("got %s", l2.is_wide() ? "true" : "false"));
}

// =========================================================================
// Group 1: Resolution Modes
// =========================================================================

static void test_resolution_256x192() {
    set_group("1.1 Resolution 256x192");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x00);  // 256x192, offset 0

    check("1.1.0", "Resolution = 0 after set_control(0x00)",
          l2.resolution() == 0,
          DETAIL("got %d", l2.resolution()));

    // Fill with y-value pattern
    fill_256x192(ram, 8, 0);

    // Render row 32 (display row 0) and check first pixel
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);

    // Pixel at (0,0) should be palette colour for index 0 (row 0)
    uint32_t expected_colour = pal.layer2_colour(0);
    check("1.1.1", "Row 0 pixel (0,0) = palette[0] (y=0 pattern)",
          buf[DISP_X_256] == expected_colour,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_colour));

    // Render row 32+64 (display row 64) — should be palette[64]
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256 + 64, ram, pal, 320);
    uint32_t expected_64 = pal.layer2_colour(64);
    check("1.1.2", "Row 64 pixel (0,64) = palette[64]",
          buf[DISP_X_256] == expected_64,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_64));

    // Row 192+ should produce no pixels (row 32+192 = 224)
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256 + 192, ram, pal, 320);
    bool row192_empty = true;
    for (int i = 0; i < 320; ++i) {
        if (buf[i] != 0) { row192_empty = false; break; }
    }
    check("1.1.3", "Row 192 produces no pixels (out of display area)",
          row192_empty, "");

    // Fill with x-value pattern and verify vertical bands
    fill_256x192(ram, 8, 1);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_x5 = pal.layer2_colour(5);
    check("1.1.4", "Vertical bands: pixel at x=5 = palette[5]",
          buf[DISP_X_256 + 5] == expected_x5,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256 + 5], expected_x5));
}

static void test_resolution_320x256() {
    set_group("1.2 Resolution 320x256");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x10);  // bits 5:4 = 01 -> 320x256

    check("1.2.0", "Resolution = 1 after set_control(0x10)",
          l2.resolution() == 1,
          DETAIL("got %d", l2.resolution()));

    check("1.2.0b", "is_wide() = true",
          l2.is_wide() == true, "");

    // Fill with solid 0x42
    fill_320x256(ram, 8, 0x42);

    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 320);

    uint32_t expected = pal.layer2_colour(0x42);
    check("1.2.1", "Row 0 pixel (0,0) = palette[0x42] (solid fill)",
          buf[0] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected));

    // Pixel at x=319 should also be valid
    check("1.2.2", "Row 0 pixel (319,0) = palette[0x42]",
          buf[319] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[319], expected));

    // Row 255 should still render
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 255, ram, pal, 320);
    check("1.2.3", "Row 255 pixel (0,255) = palette[0x42]",
          buf[0] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected));

    // Row 256 should produce no pixels
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 256, ram, pal, 320);
    bool row256_empty = true;
    for (int i = 0; i < 320; ++i) {
        if (buf[i] != 0) { row256_empty = false; break; }
    }
    check("1.2.4", "Row 256 produces no pixels (out of range)",
          row256_empty, "");
}

static void test_resolution_640x256() {
    set_group("1.3 Resolution 640x256");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x20);  // bits 5:4 = 10 -> 640x256

    check("1.3.0", "Resolution = 2 after set_control(0x20)",
          l2.resolution() == 2,
          DETAIL("got %d", l2.resolution()));

    // Also check resolution 3 selects 640x256
    l2.set_control(0x30);
    check("1.3.0b", "Resolution = 3 also selects 640x256 (VHDL: bit 5=1)",
          l2.resolution() == 3,
          DETAIL("got %d", l2.resolution()));
    l2.set_control(0x20);  // back to 2

    // Fill with 0xA5 (high nibble=0xA, low nibble=0x5)
    fill_640x256(ram, 8, 0xA5);

    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 640);

    // In 640x256 4bpp, high nibble is left pixel, low nibble is right.
    // palette_offset=0, so left pixel = palette[0x0A], right = palette[0x05]
    uint32_t expected_left = pal.layer2_colour(0x0A);
    uint32_t expected_right = pal.layer2_colour(0x05);
    check("1.3.1", "640x256: left pixel (high nib) = palette[0x0A]",
          buf[0] == expected_left,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected_left));

    check("1.3.2", "640x256: right pixel (low nib) = palette[0x05]",
          buf[1] == expected_right,
          DETAIL("got 0x%08X, expected 0x%08X", buf[1], expected_right));

    // Verify at 320px render_width (only left pixel output)
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 320);
    check("1.3.3", "640x256 at render_width=320: only left pixel output",
          buf[0] == expected_left,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected_left));
}

// =========================================================================
// Group 2: Scrolling
// =========================================================================

static void test_scroll_256x192() {
    set_group("2.1 Scroll 256x192");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x00);

    // Fill: each pixel = x value (vertical bands)
    fill_256x192(ram, 8, 1);

    // Scroll X=128: pixel at display x=0 should read from VRAM x=128
    l2.set_scroll_x_lsb(128);
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);

    uint32_t expected = pal.layer2_colour(128);
    check("2.1.1", "Scroll X=128: display x=0 shows VRAM x=128",
          buf[DISP_X_256] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected));

    // Scroll X=0, Y=96: display row 0 should read VRAM row 96
    l2.set_scroll_x_lsb(0);
    l2.set_scroll_y(96);
    fill_256x192(ram, 8, 0);  // y-value pattern
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);

    uint32_t expected_y96 = pal.layer2_colour(96);
    check("2.1.2", "Scroll Y=96: display row 0 shows VRAM row 96",
          buf[DISP_X_256] == expected_y96,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_y96));

    // Scroll Y wraps at 192: Y=191 should show VRAM row 191 at display row 0
    l2.set_scroll_y(191);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_y191 = pal.layer2_colour(191);
    check("2.1.3", "Scroll Y=191: display row 0 shows VRAM row 191",
          buf[DISP_X_256] == expected_y191,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_y191));

    // Y wraps: scroll Y=192 -> display row 0 shows VRAM row (0+192)%192 = 0
    l2.set_scroll_y(192);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_y0 = pal.layer2_colour(0);
    check("2.1.4", "Scroll Y=192 wraps to VRAM row 0",
          buf[DISP_X_256] == expected_y0,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_y0));

    l2.set_scroll_y(0);
}

static void test_scroll_320x256() {
    set_group("2.2 Scroll 320x256");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x10);  // 320x256

    // Fill: each column has value = (x & 0xFF) so we can detect scrolling
    for (int x = 0; x < 320; ++x) {
        for (int y = 0; y < 256; ++y) {
            uint32_t addr = static_cast<uint32_t>(8) * 16384 +
                            static_cast<uint32_t>(x) * 256 + y;
            ram.write(addr, static_cast<uint8_t>(x & 0xFF));
        }
    }

    // Scroll X=160: display x=0 reads VRAM x=160
    l2.set_scroll_x_lsb(160);
    l2.set_scroll_x_msb(0);
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 320);

    uint32_t expected = pal.layer2_colour(160);
    check("2.2.1", "Scroll X=160 in 320x256: display x=0 shows VRAM x=160",
          buf[0] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected));

    // Scroll X=319: display x=0 reads VRAM x=319
    l2.set_scroll_x_lsb(319 & 0xFF);
    l2.set_scroll_x_msb(319 >> 8);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 320);
    uint32_t expected_319 = pal.layer2_colour(319 & 0xFF);
    check("2.2.2", "Scroll X=319: display x=0 shows VRAM col 319",
          buf[0] == expected_319,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected_319));

    // Scroll Y=128
    l2.set_scroll_x_lsb(0);
    l2.set_scroll_x_msb(0);
    l2.set_scroll_y(128);
    // Fill each row with y-value
    for (int x = 0; x < 320; ++x) {
        for (int y = 0; y < 256; ++y) {
            uint32_t addr = static_cast<uint32_t>(8) * 16384 +
                            static_cast<uint32_t>(x) * 256 + y;
            ram.write(addr, static_cast<uint8_t>(y));
        }
    }
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 320);
    uint32_t expected_y128 = pal.layer2_colour(128);
    check("2.2.3", "Scroll Y=128 in 320x256: display row 0 shows VRAM row 128",
          buf[0] == expected_y128,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected_y128));

    l2.set_scroll_y(0);
}

// =========================================================================
// Group 3: Clip Window
// =========================================================================

static void test_clip_256x192() {
    set_group("3.1 Clip 256x192");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x00);

    fill_256x192(ram, 8, 2);  // solid 0x42

    // Default clip should show all pixels
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected = pal.layer2_colour(0x42);
    check("3.1.1", "Default clip: pixel at x=0 visible",
          buf[DISP_X_256] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected));

    check("3.1.2", "Default clip: pixel at x=255 visible",
          buf[DISP_X_256 + 255] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256 + 255], expected));

    // Clip to centre: x1=64, x2=191, y1=48, y2=143
    l2.set_clip_x1(64);
    l2.set_clip_x2(191);
    l2.set_clip_y1(48);
    l2.set_clip_y2(143);

    // Row 0 (display row 0) is before clip_y1=48 => no pixels
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    bool row0_empty = true;
    for (int i = 0; i < 320; ++i) {
        if (buf[i] != 0) { row0_empty = false; break; }
    }
    check("3.1.3", "Clip y1=48: display row 0 is empty",
          row0_empty, "");

    // Row 48 should have pixels only at x=64..191
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256 + 48, ram, pal, 320);
    check("3.1.4", "Clip: x=63 is clipped (empty)",
          buf[DISP_X_256 + 63] == 0,
          DETAIL("got 0x%08X", buf[DISP_X_256 + 63]));

    check("3.1.5", "Clip: x=64 is visible",
          buf[DISP_X_256 + 64] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256 + 64], expected));

    check("3.1.6", "Clip: x=191 is visible",
          buf[DISP_X_256 + 191] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256 + 191], expected));

    check("3.1.7", "Clip: x=192 is clipped (empty)",
          buf[DISP_X_256 + 192] == 0,
          DETAIL("got 0x%08X", buf[DISP_X_256 + 192]));

    // Inverted clip: x1 > x2 -> no pixels
    l2.set_clip_x1(200);
    l2.set_clip_x2(100);
    l2.set_clip_y1(0);
    l2.set_clip_y2(191);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256 + 50, ram, pal, 320);
    bool inverted_empty = true;
    for (int i = 0; i < 320; ++i) {
        if (buf[i] != 0) { inverted_empty = false; break; }
    }
    check("3.1.8", "Inverted clip (x1>x2): no pixels visible",
          inverted_empty, "");

    l2.set_clip_x1(0);
    l2.set_clip_x2(255);
    l2.set_clip_y1(0);
    l2.set_clip_y2(255);
}

static void test_clip_320x256() {
    set_group("3.2 Clip 320x256");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x10);  // 320x256

    fill_320x256(ram, 8, 0x42);

    uint32_t buf[BUF_WIDTH];
    uint32_t expected = pal.layer2_colour(0x42);

    // Default clip (x1=0, x2=255) -> effective x range = 0..511
    // This covers the full 320 columns
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 320);
    check("3.2.1", "Default clip in 320x256: pixel at x=0 visible",
          buf[0] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected));

    check("3.2.2", "Default clip in 320x256: pixel at x=319 visible",
          buf[319] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[319], expected));

    // Clip register=80 -> effective x2 = 80*2+1 = 161
    // So columns 0..161 visible, 162+ clipped
    l2.set_clip_x1(0);
    l2.set_clip_x2(80);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 320);
    check("3.2.3", "Clip x2=80 -> effective x2=161: col 161 visible",
          buf[161] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[161], expected));

    check("3.2.4", "Clip x2=80 -> effective x2=161: col 162 clipped",
          buf[162] == 0,
          DETAIL("got 0x%08X", buf[162]));

    l2.set_clip_x1(0);
    l2.set_clip_x2(255);
}

// =========================================================================
// Group 4: Palette
// =========================================================================

static void test_palette_offset() {
    set_group("4.1 Palette Offset");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);

    // Fill with solid pixel value 0x00
    fill_256x192(ram, 8, 2);
    // Override: fill with 0x00
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x)
            ram.write(static_cast<uint32_t>(8) * 16384 + y * 256 + x, 0x00);

    // Offset=0: pixel 0x00 -> colour_idx = ((0>>4)+0)<<4 | (0&0xF) = 0x00
    l2.set_control(0x00);
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_0 = pal.layer2_colour(0x00);
    check("4.1.1", "Offset=0: pixel 0x00 -> palette[0x00]",
          buf[DISP_X_256] == expected_0,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_0));

    // Offset=1: pixel 0x00 -> colour_idx = ((0>>4)+1)<<4 | 0 = 0x10
    l2.set_control(0x01);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_10 = pal.layer2_colour(0x10);
    check("4.1.2", "Offset=1: pixel 0x00 -> palette[0x10]",
          buf[DISP_X_256] == expected_10,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_10));

    // Offset=15: pixel 0x00 -> colour_idx = ((0>>4)+15)<<4 | 0 = 0xF0
    l2.set_control(0x0F);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_f0 = pal.layer2_colour(0xF0);
    check("4.1.3", "Offset=15: pixel 0x00 -> palette[0xF0]",
          buf[DISP_X_256] == expected_f0,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_f0));

    // Offset=15, pixel=0x10 -> ((1+15)&0xF)<<4 | 0 = 0x00 (wraps)
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x)
            ram.write(static_cast<uint32_t>(8) * 16384 + y * 256 + x, 0x10);

    l2.set_control(0x0F);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_wrap = pal.layer2_colour(0x00);
    check("4.1.4", "Offset=15, pixel=0x10: wraps to palette[0x00]",
          buf[DISP_X_256] == expected_wrap,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_wrap));
}

static void test_palette_offset_4bit() {
    set_group("4.2 Palette Offset 4-bit (640x256)");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);

    // Fill with 0x55 -> high nib=5, low nib=5
    fill_640x256(ram, 8, 0x55);

    // Offset=0: 4bpp pixel 5 -> palette[(0<<4)|5] = palette[0x05]
    l2.set_control(0x20);  // 640x256, offset=0
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 640);
    uint32_t expected_05 = pal.layer2_colour(0x05);
    check("4.2.1", "640x256 offset=0, nibble=5 -> palette[0x05]",
          buf[0] == expected_05,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected_05));

    // Offset=3: 4bpp pixel 5 -> palette[(3<<4)|5] = palette[0x35]
    l2.set_control(0x23);  // 640x256, offset=3
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, 0, ram, pal, 640);
    uint32_t expected_35 = pal.layer2_colour(0x35);
    check("4.2.2", "640x256 offset=3, nibble=5 -> palette[0x35]",
          buf[0] == expected_35,
          DETAIL("got 0x%08X, expected 0x%08X", buf[0], expected_35));
}

// =========================================================================
// Group 5: Transparency
// =========================================================================

static void test_transparency() {
    set_group("5. Transparency");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x00);

    // Fill with 0xE3 (default transparent colour)
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x)
            ram.write(static_cast<uint32_t>(8) * 16384 + y * 256 + x, 0xE3);

    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);

    // With default identity palette, pixel 0xE3 produces RGB 0xE3 which
    // matches the transparent colour -> pixel should be transparent (skipped)
    check("5.1", "Pixel 0xE3 with default palette is transparent (skipped)",
          buf[DISP_X_256] == 0,
          DETAIL("got 0x%08X, expected 0x00000000", buf[DISP_X_256]));

    // Fill with 0x42 (not transparent)
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x)
            ram.write(static_cast<uint32_t>(8) * 16384 + y * 256 + x, 0x42);

    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected = pal.layer2_colour(0x42);
    check("5.2", "Pixel 0x42 is opaque (not transparent)",
          buf[DISP_X_256] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected));

    // Disabled layer -> no pixels rendered at all
    l2.set_enabled(false);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    bool disabled_empty = true;
    for (int i = 0; i < 320; ++i) {
        if (buf[i] != 0) { disabled_empty = false; break; }
    }
    check("5.3", "Layer 2 disabled: no pixels rendered",
          disabled_empty, "");

    l2.set_enabled(true);
}

// =========================================================================
// Group 6: Bank Selection
// =========================================================================

static void test_bank_selection() {
    set_group("6. Bank Selection");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_enabled(true);
    l2.set_control(0x00);

    // Fill bank 8 with 0x11, bank 12 with 0x22
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x) {
            ram.write(static_cast<uint32_t>(8) * 16384 + y * 256 + x, 0x11);
            ram.write(static_cast<uint32_t>(12) * 16384 + y * 256 + x, 0x22);
        }

    // Active bank = 8 (default)
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_11 = pal.layer2_colour(0x11);
    check("6.1", "Active bank 8: shows 0x11",
          buf[DISP_X_256] == expected_11,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_11));

    // Switch to bank 12
    l2.set_active_bank(12);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected_22 = pal.layer2_colour(0x22);
    check("6.2", "Active bank 12: shows 0x22",
          buf[DISP_X_256] == expected_22,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected_22));

    // Shadow bank setter/getter
    l2.set_shadow_bank(20);
    check("6.3", "Shadow bank set/get",
          l2.shadow_bank() == 20,
          DETAIL("got %d", l2.shadow_bank()));

    // Bank mask is 7 bits
    l2.set_active_bank(0xFF);
    check("6.4", "Active bank masked to 7 bits (0xFF -> 0x7F)",
          l2.active_bank() == 0x7F,
          DETAIL("got %d", l2.active_bank()));

    l2.set_shadow_bank(0xFF);
    check("6.5", "Shadow bank masked to 7 bits",
          l2.shadow_bank() == 0x7F,
          DETAIL("got %d", l2.shadow_bank()));
}

// =========================================================================
// Group 8: Enable / Disable
// =========================================================================

static void test_enable_disable() {
    set_group("8. Enable/Disable");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_control(0x00);

    fill_256x192(ram, 8, 2);  // solid 0x42

    // Disabled: no output
    l2.set_enabled(false);
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    bool empty = true;
    for (int i = 0; i < 320; ++i) {
        if (buf[i] != 0) { empty = false; break; }
    }
    check("8.1", "Disabled: no pixels rendered", empty, "");

    // Enable: pixels appear
    l2.set_enabled(true);
    memset(buf, 0, sizeof(buf));
    l2.render_scanline(buf, DISP_Y_256, ram, pal, 320);
    uint32_t expected = pal.layer2_colour(0x42);
    check("8.2", "Enabled: pixels rendered",
          buf[DISP_X_256] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected));

    // Toggle back
    l2.set_enabled(false);
    check("8.3", "set_enabled(false) reports disabled",
          l2.enabled() == false, "");

    l2.set_enabled(true);
    check("8.4", "set_enabled(true) reports enabled",
          l2.enabled() == true, "");
}

// =========================================================================
// Group 9: render_scanline_debug
// =========================================================================

static void test_debug_render() {
    set_group("9. Debug Render");

    Ram ram;
    PaletteManager pal;
    pal.reset();
    Layer2 l2;
    l2.reset();
    l2.set_control(0x00);
    l2.set_enabled(false);  // Layer disabled

    // Fill bank 12 with 0x33
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x)
            ram.write(static_cast<uint32_t>(12) * 16384 + y * 256 + x, 0x33);

    // render_scanline_debug should render from bank 12 even though L2 is disabled
    uint32_t buf[BUF_WIDTH];
    memset(buf, 0, sizeof(buf));
    l2.render_scanline_debug(buf, DISP_Y_256, ram, pal, 12, 320);

    uint32_t expected = pal.layer2_colour(0x33);
    check("9.1", "Debug render from bank 12 while L2 disabled",
          buf[DISP_X_256] == expected,
          DETAIL("got 0x%08X, expected 0x%08X", buf[DISP_X_256], expected));

    // After debug render, enabled state should be restored
    check("9.2", "Debug render restores enabled state",
          l2.enabled() == false, "");

    // Active bank should be restored
    check("9.3", "Debug render restores active bank",
          l2.active_bank() == 8,
          DETAIL("got %d", l2.active_bank()));
}

// =========================================================================
// Main
// =========================================================================

int main() {
    printf("=== Layer 2 Compliance Test Suite ===\n\n");

    test_reset_defaults();
    test_resolution_256x192();
    test_resolution_320x256();
    test_resolution_640x256();
    test_scroll_256x192();
    test_scroll_320x256();
    test_clip_256x192();
    test_clip_320x256();
    test_palette_offset();
    test_palette_offset_4bit();
    test_transparency();
    test_bank_selection();
    test_enable_disable();
    test_debug_render();

    // Print summary
    printf("\n=== Results by Group ===\n");
    std::string cur_group;
    int gp = 0, gf = 0;
    for (size_t i = 0; i <= g_results.size(); ++i) {
        if (i == g_results.size() || (i > 0 && g_results[i].group != cur_group)) {
            if (!cur_group.empty()) {
                printf("  %-35s %d/%d %s\n", cur_group.c_str(),
                       gp, gp + gf, gf == 0 ? "PASS" : "FAIL");
            }
            if (i < g_results.size()) {
                cur_group = g_results[i].group;
                gp = gf = 0;
            }
        }
        if (i < g_results.size()) {
            if (g_results[i].passed) gp++; else gf++;
        }
    }

    printf("\n=== Total: %d/%d passed", g_pass, g_total);
    if (g_fail > 0) printf(" (%d FAILED)", g_fail);
    printf(" ===\n");

    return g_fail > 0 ? 1 : 0;
}
