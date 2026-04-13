// Compositor Compliance Test Runner
//
// Tests the video compositor / layer mixing logic against VHDL-derived
// expected behaviour from COMPOSITOR-TEST-PLAN-DESIGN.md.
//
// Run: ./build/test/compositor_test

// Access private members for direct buffer manipulation (zero production changes)
#define private public
#define protected public
#include "video/renderer.h"
#undef private
#undef protected

#include "video/palette.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

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

// -- Constants --------------------------------------------------------------

static constexpr uint32_t TRANSPARENT = 0x00000000;  // alpha=0
static constexpr int TEST_WIDTH = 320;

// Make an opaque ARGB pixel with a recognisable tag value
static uint32_t make_pixel(uint8_t tag) {
    return 0xFF000000u | (static_cast<uint32_t>(tag) << 16) |
           (static_cast<uint32_t>(tag) << 8) | tag;
}

// Distinct pixel colours for each layer
static constexpr uint32_t PIX_ULA     = 0xFFAA0000;  // red-ish
static constexpr uint32_t PIX_L2      = 0xFF00BB00;  // green-ish
static constexpr uint32_t PIX_SPRITE  = 0xFF0000CC;  // blue-ish
static constexpr uint32_t PIX_TM      = 0xFFDD00DD;  // magenta-ish
static constexpr uint32_t PIX_FALLBACK = 0xFFE3E3E3; // fallback

// Helper: clear all layer buffers to transparent
static void clear_layers(Renderer& r) {
    std::fill_n(r.ula_line_.begin(), TEST_WIDTH, TRANSPARENT);
    std::fill_n(r.layer2_line_.begin(), TEST_WIDTH, TRANSPARENT);
    std::fill_n(r.sprite_line_.begin(), TEST_WIDTH, TRANSPARENT);
    std::fill_n(r.tilemap_line_.begin(), TEST_WIDTH, TRANSPARENT);
    std::fill_n(r.ula_over_flags_.begin(), TEST_WIDTH, false);
}

// Helper: composite pixel at position 0 and return result
static uint32_t composite_one(Renderer& r, uint32_t fallback_argb) {
    uint32_t out[TEST_WIDTH];
    std::memset(out, 0, sizeof(out));
    r.composite_scanline(out, fallback_argb, TEST_WIDTH);
    return out[0];
}

// -- Group 1: Transparency Detection ---------------------------------------

static void test_transparency() {
    set_group("Transparency");
    Renderer r;
    r.reset();

    // TR-01: All layers transparent -> fallback shown
    {
        clear_layers(r);
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("TR-01", "All transparent -> fallback",
              result == PIX_FALLBACK,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_FALLBACK));
    }

    // TR-02: Only ULA opaque -> ULA shown
    {
        clear_layers(r);
        r.ula_line_[0] = PIX_ULA;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("TR-02", "Only ULA opaque -> ULA colour",
              result == PIX_ULA,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_ULA));
    }

    // TR-03: Only L2 opaque -> L2 shown
    {
        clear_layers(r);
        r.layer2_line_[0] = PIX_L2;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("TR-03", "Only L2 opaque -> L2 colour",
              result == PIX_L2,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_L2));
    }

    // TR-04: Only Sprite opaque -> Sprite shown
    {
        clear_layers(r);
        r.sprite_line_[0] = PIX_SPRITE;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("TR-04", "Only Sprite opaque -> Sprite colour",
              result == PIX_SPRITE,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_SPRITE));
    }

    // TR-05: ULA transparent (alpha=0), L2 opaque -> L2 shown (SLU mode)
    {
        clear_layers(r);
        r.ula_line_[0] = TRANSPARENT;
        r.layer2_line_[0] = PIX_L2;
        r.set_layer_priority(0); // SLU
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("TR-05", "ULA transparent, L2 opaque in SLU -> L2",
              result == PIX_L2,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_L2));
    }

    // TR-06: Tilemap opaque, ULA transparent, ula_over=false -> TM replaces ULA
    {
        clear_layers(r);
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = TRANSPARENT;
        r.ula_over_flags_[0] = false;
        r.set_layer_priority(0); // SLU
        uint32_t result = composite_one(r, PIX_FALLBACK);
        // TM replaces ULA in U slot, so U=TM, and in SLU with no S/L it shows TM
        check("TR-06", "TM opaque replaces transparent ULA in U slot",
              result == PIX_TM,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_TM));
    }

    // TR-07: Tilemap opaque, ULA opaque, ula_over=true -> ULA wins in U slot
    {
        clear_layers(r);
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = PIX_ULA;
        r.ula_over_flags_[0] = true;
        r.set_layer_priority(0); // SLU
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("TR-07", "TM opaque + ULA opaque + ula_over -> ULA wins in U slot",
              result == PIX_ULA,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_ULA));
    }

    // TR-08: Tilemap transparent -> ULA shows through in U slot
    {
        clear_layers(r);
        r.tilemap_line_[0] = TRANSPARENT;
        r.ula_line_[0] = PIX_ULA;
        r.set_layer_priority(0); // SLU
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("TR-08", "TM transparent -> ULA in U slot",
              result == PIX_ULA,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_ULA));
    }
}

// -- Group 2: Fallback Colour -----------------------------------------------

static void test_fallback() {
    set_group("Fallback");
    Renderer r;
    r.reset();

    // FB-01: All transparent, custom fallback
    {
        clear_layers(r);
        uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t result = composite_one(r, fb);
        check("FB-01", "All transparent -> fallback 0xE3",
              result == fb,
              DETAIL("got 0x%08X, expected 0x%08X", result, fb));
    }

    // FB-02: All transparent, fallback=0x00
    {
        clear_layers(r);
        uint32_t fb = Renderer::rrrgggbb_to_argb(0x00);
        uint32_t result = composite_one(r, fb);
        check("FB-02", "All transparent -> fallback 0x00 (black)",
              result == fb,
              DETAIL("got 0x%08X, expected 0x%08X", result, fb));
    }

    // FB-03: All transparent, fallback=0x4A
    {
        clear_layers(r);
        uint32_t fb = Renderer::rrrgggbb_to_argb(0x4A);
        uint32_t result = composite_one(r, fb);
        check("FB-03", "All transparent -> fallback 0x4A",
              result == fb,
              DETAIL("got 0x%08X, expected 0x%08X", result, fb));
    }

    // FB-04: One layer opaque -> that layer wins, not fallback
    {
        clear_layers(r);
        r.sprite_line_[0] = PIX_SPRITE;
        uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t result = composite_one(r, fb);
        check("FB-04", "One layer opaque -> not fallback",
              result == PIX_SPRITE,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_SPRITE));
    }

    // FB-05: Fallback reset default is 0xE3
    {
        Renderer r2;
        r2.reset();
        check("FB-05", "Reset default fallback = 0xE3",
              r2.fallback_colour() == 0xE3,
              DETAIL("got 0x%02X", r2.fallback_colour()));
    }

    // FB-06: set_fallback_colour stores correctly
    {
        Renderer r2;
        r2.reset();
        r2.set_fallback_colour(0x42);
        check("FB-06", "set_fallback_colour round-trip",
              r2.fallback_colour() == 0x42,
              DETAIL("got 0x%02X", r2.fallback_colour()));
    }

    // FB-07: rrrgggbb_to_argb(0x00) -> black with full alpha
    {
        uint32_t c = Renderer::rrrgggbb_to_argb(0x00);
        check("FB-07", "rrrgggbb_to_argb(0x00) is opaque black",
              (c & 0xFF000000) == 0xFF000000 && (c & 0x00FFFFFF) == 0,
              DETAIL("got 0x%08X", c));
    }
}

// -- Group 3: Layer Priority Modes (000-101) --------------------------------

static void test_priority_modes() {
    set_group("Priority");
    Renderer r;
    r.reset();

    // Test structure: set priority mode, set layers opaque/transparent, check winner
    struct PriorityTest {
        const char* id;
        const char* desc;
        uint8_t priority;
        bool ula_opaque;
        bool l2_opaque;
        bool spr_opaque;
        uint32_t expected;
    };

    // For each mode, test which layer wins when all are opaque,
    // and when only lower-priority layers are present.
    PriorityTest tests[] = {
        // Mode 000 (SLU): S > L > U
        {"PRI-01", "SLU: all opaque -> Sprite", 0, true, true, true, PIX_SPRITE},
        {"PRI-02", "SLU: no S -> L2", 0, true, true, false, PIX_L2},
        {"PRI-03", "SLU: no S,L -> ULA", 0, true, false, false, PIX_ULA},
        {"PRI-04", "SLU: none -> fallback", 0, false, false, false, PIX_FALLBACK},

        // Mode 001 (LSU): L > S > U
        {"PRI-05", "LSU: all opaque -> L2", 1, true, true, true, PIX_L2},
        {"PRI-06", "LSU: no L -> Sprite", 1, true, false, true, PIX_SPRITE},
        {"PRI-07", "LSU: no L,S -> ULA", 1, true, false, false, PIX_ULA},

        // Mode 010 (SUL): S > U > L
        {"PRI-08", "SUL: all opaque -> Sprite", 2, true, true, true, PIX_SPRITE},
        {"PRI-09", "SUL: no S -> ULA", 2, true, true, false, PIX_ULA},
        {"PRI-10", "SUL: no S,U -> L2", 2, false, true, false, PIX_L2},

        // Mode 011 (LUS): L > U > S
        {"PRI-11", "LUS: all opaque -> L2", 3, true, true, true, PIX_L2},
        {"PRI-12", "LUS: no L -> ULA", 3, true, false, false, PIX_ULA},
        {"PRI-13", "LUS: no L,U -> Sprite", 3, false, false, true, PIX_SPRITE},

        // Mode 100 (USL): U > S > L
        {"PRI-14", "USL: all opaque -> ULA", 4, true, true, true, PIX_ULA},
        {"PRI-15", "USL: no U -> Sprite", 4, false, true, true, PIX_SPRITE},
        {"PRI-16", "USL: no U,S -> L2", 4, false, true, false, PIX_L2},

        // Mode 101 (ULS): U > L > S
        {"PRI-17", "ULS: all opaque -> ULA", 5, true, true, true, PIX_ULA},
        {"PRI-18", "ULS: no U -> L2", 5, false, true, true, PIX_L2},
        {"PRI-19", "ULS: no U,L -> Sprite", 5, false, false, true, PIX_SPRITE},
    };

    for (const auto& t : tests) {
        clear_layers(r);
        r.set_layer_priority(t.priority);
        if (t.ula_opaque)  r.ula_line_[0] = PIX_ULA;
        if (t.l2_opaque)   r.layer2_line_[0] = PIX_L2;
        if (t.spr_opaque)  r.sprite_line_[0] = PIX_SPRITE;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check(t.id, t.desc, result == t.expected,
              DETAIL("got 0x%08X, expected 0x%08X", result, t.expected));
    }
}

// -- Group 4: Blend Modes (6, 7) -- currently deferred to SLU fallback ------

static void test_blend_modes() {
    set_group("Blend");
    Renderer r;
    r.reset();

    // Mode 6 and 7 are documented as blend modes in VHDL but the emulator
    // currently falls back to SLU. We test current behaviour.

    // BL-01: Mode 6 with all opaque -> should behave like SLU (sprite wins)
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.ula_line_[0] = PIX_ULA;
        r.layer2_line_[0] = PIX_L2;
        r.sprite_line_[0] = PIX_SPRITE;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        // Current code falls back to SLU for modes 6,7
        check("BL-01", "Mode 6 (blend): current fallback to SLU -> sprite",
              result == PIX_SPRITE,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_SPRITE));
    }

    // BL-02: Mode 7 with only L2 -> L2 shown
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.layer2_line_[0] = PIX_L2;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("BL-02", "Mode 7 (blend): only L2 -> L2",
              result == PIX_L2,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_L2));
    }

    // BL-03: Mode 6, all transparent -> fallback
    {
        clear_layers(r);
        r.set_layer_priority(6);
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("BL-03", "Mode 6: all transparent -> fallback",
              result == PIX_FALLBACK,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_FALLBACK));
    }

    // BL-04: Mode 7, all transparent -> fallback
    {
        clear_layers(r);
        r.set_layer_priority(7);
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("BL-04", "Mode 7: all transparent -> fallback",
              result == PIX_FALLBACK,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_FALLBACK));
    }
}

// -- Group 5: ULA/Tilemap Combination (U slot) ------------------------------

static void test_ula_tilemap_combo() {
    set_group("ULA+TM");
    Renderer r;
    r.reset();
    r.set_layer_priority(0); // SLU

    // UTB-01: TM opaque, ULA opaque, ula_over=false -> TM wins in U slot
    {
        clear_layers(r);
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = PIX_ULA;
        r.ula_over_flags_[0] = false;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("UTB-01", "TM+ULA opaque, ula_over=false -> TM in U slot",
              result == PIX_TM,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_TM));
    }

    // UTB-02: TM opaque, ULA opaque, ula_over=true -> ULA wins in U slot
    {
        clear_layers(r);
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = PIX_ULA;
        r.ula_over_flags_[0] = true;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("UTB-02", "TM+ULA opaque, ula_over=true -> ULA in U slot",
              result == PIX_ULA,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_ULA));
    }

    // UTB-03: TM transparent, ULA opaque -> ULA shows in U slot
    {
        clear_layers(r);
        r.tilemap_line_[0] = TRANSPARENT;
        r.ula_line_[0] = PIX_ULA;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("UTB-03", "TM transparent -> ULA in U slot",
              result == PIX_ULA,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_ULA));
    }

    // UTB-04: TM opaque, ULA transparent, ula_over=false -> TM in U slot
    {
        clear_layers(r);
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = TRANSPARENT;
        r.ula_over_flags_[0] = false;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("UTB-04", "TM opaque, ULA transparent, no ula_over -> TM",
              result == PIX_TM,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_TM));
    }

    // UTB-05: TM opaque, ULA transparent, ula_over=true -> ULA (transparent) in U slot
    // ula_over means ULA has priority, but ULA is transparent, so U = ULA = transparent
    {
        clear_layers(r);
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = TRANSPARENT;
        r.ula_over_flags_[0] = true;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        // ULA wins priority but is transparent -> U slot is transparent -> fallback
        check("UTB-05", "TM opaque + ula_over + ULA transparent -> fallback",
              result == PIX_FALLBACK,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_FALLBACK));
    }

    // UTB-06: Both TM and ULA transparent -> U slot transparent -> fallback
    {
        clear_layers(r);
        r.tilemap_line_[0] = TRANSPARENT;
        r.ula_line_[0] = TRANSPARENT;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("UTB-06", "Both TM+ULA transparent -> fallback",
              result == PIX_FALLBACK,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_FALLBACK));
    }
}

// -- Group 6: Layer Priority with U slot (TM interaction) -------------------

static void test_priority_with_tilemap() {
    set_group("Priority+TM");
    Renderer r;
    r.reset();

    // In SLU mode: S > L > U. TM replaces ULA in U slot.
    // If S and L transparent, TM (in U slot) should show.
    {
        clear_layers(r);
        r.set_layer_priority(0); // SLU
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = TRANSPARENT;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("PTM-01", "SLU: S,L transparent, TM in U -> TM shown",
              result == PIX_TM,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_TM));
    }

    // Sprite opaque takes priority over TM in U slot (SLU)
    {
        clear_layers(r);
        r.set_layer_priority(0); // SLU
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = TRANSPARENT;
        r.sprite_line_[0] = PIX_SPRITE;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("PTM-02", "SLU: Sprite over TM in U -> Sprite shown",
              result == PIX_SPRITE,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_SPRITE));
    }

    // USL mode: U slot (with TM) on top
    {
        clear_layers(r);
        r.set_layer_priority(4); // USL
        r.tilemap_line_[0] = PIX_TM;
        r.ula_line_[0] = TRANSPARENT;
        r.sprite_line_[0] = PIX_SPRITE;
        r.layer2_line_[0] = PIX_L2;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("PTM-03", "USL: TM in U slot is top -> TM shown",
              result == PIX_TM,
              DETAIL("got 0x%08X, expected 0x%08X", result, PIX_TM));
    }
}

// -- Group 7: rrrgggbb_to_argb conversion -----------------------------------

static void test_colour_conversion() {
    set_group("ColourConv");
    Renderer r;

    // Test known colour conversions
    // 0xFF = R=7, G=7, B=3 -> white
    {
        uint32_t c = Renderer::rrrgggbb_to_argb(0xFF);
        uint8_t r8 = (c >> 16) & 0xFF;
        uint8_t g8 = (c >> 8) & 0xFF;
        uint8_t b8 = c & 0xFF;
        check("CC-01", "0xFF -> near white",
              r8 == 0xFF && g8 == 0xFF && b8 == 0xFF,
              DETAIL("got R=%d G=%d B=%d", r8, g8, b8));
    }

    // 0x00 = black
    {
        uint32_t c = Renderer::rrrgggbb_to_argb(0x00);
        check("CC-02", "0x00 -> black",
              (c & 0x00FFFFFF) == 0,
              DETAIL("got 0x%08X", c));
    }

    // 0xE0 = R=7, G=0, B=0 -> pure red
    {
        uint32_t c = Renderer::rrrgggbb_to_argb(0xE0);
        uint8_t r8 = (c >> 16) & 0xFF;
        uint8_t g8 = (c >> 8) & 0xFF;
        uint8_t b8 = c & 0xFF;
        check("CC-03", "0xE0 -> red",
              r8 == 0xFF && g8 == 0 && b8 == 0,
              DETAIL("got R=%d G=%d B=%d", r8, g8, b8));
    }

    // 0x1C = R=0, G=7, B=0 -> pure green
    {
        uint32_t c = Renderer::rrrgggbb_to_argb(0x1C);
        uint8_t r8 = (c >> 16) & 0xFF;
        uint8_t g8 = (c >> 8) & 0xFF;
        uint8_t b8 = c & 0xFF;
        check("CC-04", "0x1C -> green",
              r8 == 0 && g8 == 0xFF && b8 == 0,
              DETAIL("got R=%d G=%d B=%d", r8, g8, b8));
    }

    // 0x03 = R=0, G=0, B=3 -> pure blue
    {
        uint32_t c = Renderer::rrrgggbb_to_argb(0x03);
        uint8_t r8 = (c >> 16) & 0xFF;
        uint8_t g8 = (c >> 8) & 0xFF;
        uint8_t b8 = c & 0xFF;
        check("CC-05", "0x03 -> blue",
              r8 == 0 && g8 == 0 && b8 == 0xFF,
              DETAIL("got R=%d G=%d B=%d", r8, g8, b8));
    }

    // All values produce full alpha
    {
        bool all_opaque = true;
        for (int i = 0; i < 256; i++) {
            uint32_t c = Renderer::rrrgggbb_to_argb(static_cast<uint8_t>(i));
            if ((c & 0xFF000000) != 0xFF000000) { all_opaque = false; break; }
        }
        check("CC-06", "All 256 values have alpha=0xFF",
              all_opaque, "");
    }
}

// -- Group 8: is_transparent helper -----------------------------------------

static void test_is_transparent() {
    set_group("IsTransp");

    // is_transparent checks alpha=0
    check("IT-01", "alpha=0 is transparent",
          Renderer::is_transparent(0x00000000), "");
    check("IT-02", "alpha=FF is opaque",
          !Renderer::is_transparent(0xFF000000), "");
    check("IT-03", "alpha=0 with RGB data still transparent",
          Renderer::is_transparent(0x00FFFFFF), "");
    check("IT-04", "alpha=01 is opaque",
          !Renderer::is_transparent(0x01000000), "");
}

// -- Group 9: Palette Manager Basics ----------------------------------------

static void test_palette_basics() {
    set_group("Palette");
    PaletteManager pm;
    pm.reset();

    // PAL-01: default global transparency
    check("PAL-01", "Default global transparency = 0xE3",
          pm.global_transparency() == 0xE3,
          DETAIL("got 0x%02X", pm.global_transparency()));

    // PAL-02: default sprite transparency
    check("PAL-02", "Default sprite transparency = 0xE3",
          pm.sprite_transparency() == 0xE3,
          DETAIL("got 0x%02X", pm.sprite_transparency()));

    // PAL-03: default tilemap transparency
    check("PAL-03", "Default tilemap transparency = 0x0F",
          pm.tilemap_transparency() == 0x0F,
          DETAIL("got 0x%02X", pm.tilemap_transparency()));

    // PAL-04: set/get global transparency
    pm.set_global_transparency(0x42);
    check("PAL-04", "Set global transparency round-trip",
          pm.global_transparency() == 0x42,
          DETAIL("got 0x%02X", pm.global_transparency()));

    // PAL-05: set/get sprite transparency
    pm.set_sprite_transparency(0x55);
    check("PAL-05", "Set sprite transparency round-trip",
          pm.sprite_transparency() == 0x55,
          DETAIL("got 0x%02X", pm.sprite_transparency()));

    // PAL-06: tilemap transparency is 4-bit masked
    pm.set_tilemap_transparency(0xFF);
    check("PAL-06", "Tilemap transparency 4-bit mask",
          pm.tilemap_transparency() == 0x0F,
          DETAIL("got 0x%02X", pm.tilemap_transparency()));

    // PAL-07: palette write and readback via 8-bit interface
    {
        PaletteManager pm2;
        pm2.reset();
        // Select ULA first palette, index 0
        pm2.write_control(0x00);  // target=ULA_FIRST, auto-inc enabled
        pm2.set_index(0);
        pm2.write_8bit(0xE0);    // red (RRRGGGBB)
        // Read it back
        uint32_t c = pm2.ula_colour(0);
        uint8_t r8 = (c >> 16) & 0xFF;
        check("PAL-07", "ULA palette write 0xE0 -> red",
              r8 == 0xFF,
              DETAIL("got R=%d from 0x%08X", r8, c));
    }
}

// -- Group 10: Renderer Reset Defaults --------------------------------------

static void test_reset_defaults() {
    set_group("Reset");
    Renderer r;
    r.reset();

    check("RST-01", "Default layer_priority = 0 (SLU)",
          r.layer_priority() == 0,
          DETAIL("got %d", r.layer_priority()));

    check("RST-02", "Default fallback_colour = 0xE3",
          r.fallback_colour() == 0xE3,
          DETAIL("got 0x%02X", r.fallback_colour()));

    // set_layer_priority masks to 3 bits
    r.set_layer_priority(0xFF);
    check("RST-03", "set_layer_priority masks to 3 bits",
          r.layer_priority() == 0x07,
          DETAIL("got %d", r.layer_priority()));
}

// -- Group 11: Multi-pixel scanline compositing -----------------------------

static void test_scanline_multi() {
    set_group("Scanline");
    Renderer r;
    r.reset();
    r.set_layer_priority(0); // SLU

    // Fill a 4-pixel scanline with different layer configurations
    clear_layers(r);

    // Pixel 0: only ULA
    r.ula_line_[0] = PIX_ULA;

    // Pixel 1: only L2
    r.layer2_line_[1] = PIX_L2;

    // Pixel 2: only Sprite
    r.sprite_line_[2] = PIX_SPRITE;

    // Pixel 3: all transparent -> fallback

    uint32_t out[TEST_WIDTH];
    std::memset(out, 0, sizeof(out));
    r.composite_scanline(out, PIX_FALLBACK, TEST_WIDTH);

    check("SCN-01", "Pixel 0: ULA only -> ULA",
          out[0] == PIX_ULA,
          DETAIL("got 0x%08X", out[0]));
    check("SCN-02", "Pixel 1: L2 only -> L2",
          out[1] == PIX_L2,
          DETAIL("got 0x%08X", out[1]));
    check("SCN-03", "Pixel 2: Sprite only -> Sprite",
          out[2] == PIX_SPRITE,
          DETAIL("got 0x%08X", out[2]));
    check("SCN-04", "Pixel 3: all transparent -> fallback",
          out[3] == PIX_FALLBACK,
          DETAIL("got 0x%08X", out[3]));
}

// -- Group 12: Modes 6/7 default fallback behaviour -------------------------

static void test_default_modes_67() {
    set_group("Mode6/7");
    Renderer r;
    r.reset();

    // Mode 6: only ULA opaque -> ULA (SLU fallback behaviour)
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.ula_line_[0] = PIX_ULA;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("M67-01", "Mode 6: only ULA -> ULA (SLU fallback)",
              result == PIX_ULA,
              DETAIL("got 0x%08X", result));
    }

    // Mode 7: only ULA opaque -> ULA (SLU fallback behaviour)
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.ula_line_[0] = PIX_ULA;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("M67-02", "Mode 7: only ULA -> ULA (SLU fallback)",
              result == PIX_ULA,
              DETAIL("got 0x%08X", result));
    }

    // Mode 6: S > L in fallback
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.sprite_line_[0] = PIX_SPRITE;
        r.layer2_line_[0] = PIX_L2;
        uint32_t result = composite_one(r, PIX_FALLBACK);
        check("M67-03", "Mode 6: S and L -> S wins (SLU fallback)",
              result == PIX_SPRITE,
              DETAIL("got 0x%08X", result));
    }
}

// -- Main -------------------------------------------------------------------

int main() {
    printf("Compositor Compliance Test Runner\n");
    printf("====================================\n\n");

    test_transparency();
    printf("  Group: Transparency -- done\n");

    test_fallback();
    printf("  Group: Fallback -- done\n");

    test_priority_modes();
    printf("  Group: Priority -- done\n");

    test_blend_modes();
    printf("  Group: Blend -- done\n");

    test_ula_tilemap_combo();
    printf("  Group: ULA+TM -- done\n");

    test_priority_with_tilemap();
    printf("  Group: Priority+TM -- done\n");

    test_colour_conversion();
    printf("  Group: ColourConv -- done\n");

    test_is_transparent();
    printf("  Group: IsTransp -- done\n");

    test_palette_basics();
    printf("  Group: Palette -- done\n");

    test_reset_defaults();
    printf("  Group: Reset -- done\n");

    test_scanline_multi();
    printf("  Group: Scanline -- done\n");

    test_default_modes_67();
    printf("  Group: Mode6/7 -- done\n");

    printf("\n====================================\n");
    printf("Results: %d/%d passed", g_pass, g_total);
    if (g_fail > 0)
        printf(" (%d FAILED)", g_fail);
    printf("\n");

    // Per-group summary
    printf("\nPer-group breakdown:\n");
    std::string last_group;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last_group) {
            if (!last_group.empty())
                printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);
            last_group = r.group;
            gp = gf = 0;
        }
        if (r.passed) gp++; else gf++;
    }
    if (!last_group.empty())
        printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
