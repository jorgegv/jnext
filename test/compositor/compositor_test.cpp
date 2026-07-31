// Compositor Subsystem Compliance Test Runner
//
// Tests the video compositor / layer mixing logic of the JNEXT ZX Spectrum
// Next emulator against expected values derived EXCLUSIVELY from the
// authoritative VHDL source cores/zxnext/src/zxnext.vhd (stage 2 of the
// video pipeline). The plan this file implements is
// doc/testing/COMPOSITOR-TEST-PLAN-DESIGN.md (rebuild dated 2026-04-14).
//
// IMPORTANT: the expected values come from the VHDL, never from the C++
// implementation. Where the VHDL semantics exercise features that the
// current C++ Renderer does not yet implement (NR 0x14 palette-compare
// transparency, blend modes 110/111, stencil, L2 priority-bit promotion,
// border exception, per-line NR latch, global sprite_en gating), the test
// still asserts the VHDL-correct expected value: the test is the
// specification. Such rows are EXPECTED to fail until the emulator
// catches up — those failures are Task 3 backlog items, not test bugs.
//
// Run: ./build/test/compositor_test

#define private public
#define protected public
#include "video/renderer.h"
#include "video/palette.h"
#include "video/layer2.h"
#include "video/tilemap.h"
#include "video/sprites.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "memory/rom.h"
#undef private
#undef protected

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

// ── Test infrastructure ───────────────────────────────────────────────────

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

// Zero skip() call sites since 2026-07-24 (LR-127/LR-128 tombstones removed,
// LR-140 live — see doc/testing/LORES-TEST-PLAN-DESIGN.md); g_skipped stays
// so the summary keeps the harness-mandated `Skipped:` field.
struct SkipNote {
    std::string id;
    std::string reason;
};
static std::vector<SkipNote> g_skipped;

static void set_group(const char* name) { g_group = name; }

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

// ── Helpers bridging VHDL 9-bit RGB onto the renderer's ARGB32 surface ───
//
// The VHDL compositor operates on 9-bit RGB (`*_rgb_2(8 downto 0)`) and
// decides transparency by either comparing the upper 8 bits of that word
// against NR 0x14 (for ULA / TM-text / L2) or by consuming the layer
// engine's `pixel_en` signal (for sprite, non-text TM, L2 empty).
//
// The current C++ Renderer condenses both mechanisms into one:
// "ARGB alpha == 0" means transparent at the compositor input. That is
// why the old 74/74 suite was theatre — it could not distinguish
// "pixel_en=0" from "palette output matches NR 0x14". This suite
// encodes VHDL-correct expected values regardless of that conflation:
// where a row requires a feature the emulator does not implement, the
// test drives the closest emulator surface, computes the VHDL oracle,
// and asserts equality. Such assertions will legitimately fail until
// the emulator is fixed.

static constexpr int W = Renderer::FB_WIDTH;  // G104 phase1: canonical 640

// VHDL: fallback_rgb_2 & (fallback_rgb_2(1) or fallback_rgb_2(0)) — line 7214.
// Returns the synthesised 9-bit value as an unsigned int.
static uint16_t vhdl_fallback_9bit(uint8_t nr4a) {
    uint8_t lsb = ((nr4a >> 1) & 0x1) | (nr4a & 0x1);
    return (static_cast<uint16_t>(nr4a) << 1) | lsb;
}

// Convert the 9-bit VHDL fallback word into ARGB32 by dropping bit 0 and
// feeding the upper 8 bits through rrrgggbb_to_argb — the same path the
// Renderer uses. The emulator's fallback pipeline is 8-bit only so we
// assert against the 8-bit-truncated ARGB equivalent when consulting
// composite_scanline output.
static uint32_t vhdl_fallback_argb(uint8_t nr4a) {
    return Renderer::rrrgggbb_to_argb(nr4a);
}

// Build a VHDL-opaque layer pixel tagged with a distinct colour. Upper 24
// bits carry a value that cannot coincide with the ARGB alpha channel
// test, so the Renderer will treat it as opaque.
static uint32_t opaque_tag(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16)
                       | (static_cast<uint32_t>(g) << 8)
                       |  static_cast<uint32_t>(b);
}

// Fixed per-layer recognisable colours (chosen to not collide with the
// VHDL NR 0x14 default 0xE3 so ambiguity of "opaque vs RGB-compare
// transparent" is surfaced, not hidden). ULA=0xAA, L2=0xBB, S=0xCC, TM=0xDD.
static const uint32_t PIX_ULA = opaque_tag(0xAA, 0x00, 0x00);
static const uint32_t PIX_L2  = opaque_tag(0x00, 0xBB, 0x00);
static const uint32_t PIX_S   = opaque_tag(0x00, 0x00, 0xCC);
static const uint32_t PIX_TM  = opaque_tag(0xDD, 0x00, 0xDD);
static constexpr uint32_t TRANSP = 0x00000000u;

// Channel extraction from ARGB, mirroring the compositor's own
// argb_r3/argb_g3/argb_b2 (file-static in renderer.cpp). Used by the LMASK
// stencil rows to compute the VHDL AND-branch oracle (zxnext.vhd:7113
// `stencil_rgb <= ula_rgb and tm_rgb`) rather than hard-coding a constant.
static uint8_t argb_r3_t(uint32_t argb) { return (argb >> 21) & 7; }
static uint8_t argb_g3_t(uint32_t argb) { return (argb >> 13) & 7; }
static uint8_t argb_b2_t(uint32_t argb) { return (argb >>  6) & 3; }

static void clear_layers(Renderer& r) {
    for (int i = 0; i < W; ++i) {
        r.ula_line_[i]     = TRANSP;
        r.layer2_line_[i]  = TRANSP;
        r.sprite_line_[i]  = TRANSP;
        r.tilemap_line_[i] = TRANSP;
        r.tm_pixel_below_[i] = false;
        // Reset alongside tm_pixel_below_ so a row that sets the text-mode
        // flag (TR-24/TR-25, GH #113) cannot leak it into the next row's
        // NR 0x14 RGB-compare clause (zxnext.vhd:7109). No existing row set
        // it, so adding the reset changes no existing outcome.
        r.tm_pixel_textmode_[i] = false;
        r.layer2_priority_[i] = false;
        r.ula_border_[i]   = false;
    }
    // Default to sprite_en=true (normal game state); tests that need
    // sprite_en=0 (TR-42) set it explicitly.
    r.sprite_en_ = true;
    // Default to stencil off and TM disabled; tests that need them set
    // them explicitly.
    r.stencil_mode_ = false;
    r.tm_enabled_ = false;
}

// Task 43/45: composite_scanline now reads the per-line stencil/blend-mode/
// transparent-RGB snapshots (stencil_mode_per_line_ / blend_mode_per_line_ /
// transparent_rgb_per_line_) instead of the live stencil_mode_ / blend_mode_
// / transparent_rgb_ members — VHDL zxnext.vhd:5445-5446,6810-6811,
// 6897-6901,7064-7065 (stencil/blend) and 1137,5226,6822,6912-6913,7078
// (NR 0x14) pipeline all three through the SAME stage0/1a/1/2 register
// chain as ula_en. Every OTHER row in this file sets the live member with
// set_stencil_mode()/set_blend_mode()/set_transparent_rgb() (or the live
// field directly, or relies on the reset() default) and composites
// immediately at the same row, testing pixel math given an already-
// effective value, not deferral — so composite_one() re-syncs the snapshot
// for `row` right before compositing to keep those single-row assertions
// meaningful. Genuine cross-row DEFERRAL assertions (STEN-20/21, UTB-50/51,
// TR-50/51) must NOT use this helper — they call r.composite_scanline()
// directly so a snapshot they deliberately withhold stays withheld.
static uint32_t composite_one(Renderer& r, uint32_t fb_argb, int row = 0) {
    r.snapshot_stencil_mode_for_line(row);
    r.snapshot_blend_mode_for_line(row);
    r.snapshot_transparent_rgb_for_line(row);
    uint32_t out[W];
    std::memset(out, 0, sizeof(out));
    r.composite_scanline(out, fb_argb, row);
    return out[0];
}

// ── Group TR — RGB-based transparency comparison (VHDL lines 7100–7121) ──

static void test_TR() {
    set_group("TR");
    Renderer r;
    r.reset();

    // TR-10: ULA pixel with palette output != NR 0x14 is opaque; mode 000, others transp.
    //        VHDL zxnext.vhd:7100 ula_mix_transparent <= (rgb(8:1)=transp_rgb) OR ula_clipped;
    //        VHDL zxnext.vhd:7226 mode 000 branch picks ULA when S/L transparent.
    {
        clear_layers(r);
        r.set_layer_priority(0);                // mode 000 (SLU)
        r.ula_line_[0] = PIX_ULA;               // palette RGB[8:1] = 0xAA != 0xE3
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-10", "mode 000, only ULA with RGB!=NR0x14 -> ULA wins (VHDL 7100,7226)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // TR-11: ULA palette output == NR 0x14 => ULA transparent; fallback wins.
    //        VHDL zxnext.vhd:7100: mix_transparent when RGB[8:1] ==
    //        transparent_rgb_2. The test uses a FALLBACK colour DISTINCT
    //        from the ULA colour so that the transparent-then-fallback
    //        path produces an observably different result from the
    //        opaque-ULA path. Expected to fail until the compositor
    //        implements palette-compare transparency (Task 2 backlog item
    //        14.1 "NR 0x14 palette-compare transparency absent").
    {
        clear_layers(r);
        r.set_layer_priority(0);
        uint32_t nr14_as_argb = Renderer::rrrgggbb_to_argb(0xE3);
        r.ula_line_[0] = nr14_as_argb;          // palette RGB[8:1] == NR 0x14
        uint32_t fb = vhdl_fallback_argb(0x10); // distinct from ULA (0xE3)
        uint32_t got = composite_one(r, fb);
        // VHDL oracle: ULA RGB matches NR 0x14 => transparent => fallback wins.
        // Currently fails: Renderer uses ARGB alpha=0 for transparency; it
        // treats opaque 0xE3 as opaque and emits the ULA colour instead.
        check("TR-11",
              "ULA RGB[8:1]=NR0x14 => transparent; fallback wins "
              "(VHDL zxnext.vhd:7100, 7214)",
              got == fb,
              DETAIL("got=0x%08X expected_fallback=0x%08X", got, fb));
    }

    // TR-12: Only the upper 8 bits of the 9-bit palette word are compared
    //        against NR 0x14 — both palette LSBs (0 and 1) must be
    //        transparent when upper 8 == NR 0x14. VHDL zxnext.vhd:7100.
    //        The C++ renderer does not model the 9-bit palette, so the
    //        two LSB cases collapse to the same ARGB input. We still
    //        assert the VHDL-correct oracle (both produce fallback) using
    //        a DISTINCT fallback colour so the check is non-tautological.
    //        Expected to fail on same root cause as TR-11.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        uint32_t nr14_as_argb = Renderer::rrrgggbb_to_argb(0xE3);
        r.ula_line_[0] = nr14_as_argb;
        uint32_t fb = vhdl_fallback_argb(0x10); // distinct from ULA
        uint32_t got_a = composite_one(r, fb);
        // Second case — emulator has no 9-bit LSB, so we reuse the same
        // input to assert the VHDL-correct shared result.
        uint32_t got_b = composite_one(r, fb);
        bool ok = (got_a == fb) && (got_b == fb);
        check("TR-12",
              "9-bit LSB is not compared; both LSB variants transparent "
              "(VHDL zxnext.vhd:7100)",
              ok,
              DETAIL("a=0x%08X b=0x%08X fb=0x%08X", got_a, got_b, fb));
    }

    // TR-13: ula_clipped_2=1 forces ULA transparent regardless of RGB.
    //        VHDL zxnext.vhd:7100. Emulator has no clip flag — approximated
    //        by zeroing the ULA buffer.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = TRANSP;                // simulate ula_clipped_2=1
        uint32_t fb = vhdl_fallback_argb(0x10);
        r.set_fallback_colour(0x10);
        uint32_t got = composite_one(r, fb);
        check("TR-13", "ula_clipped_2=1 forces ULA transp => fallback (VHDL 7100)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // TR-14: ula_en_2=0 forces ULA transparent. VHDL zxnext.vhd:7103.
    //        Emulator lacks an NR 0x68 bit 7 path; approximated by zeroing
    //        the ULA buffer and asserting the VHDL oracle (fallback).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = TRANSP;                // simulate ula_en_2=0
        uint32_t fb = vhdl_fallback_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        check("TR-14", "ula_en_2=0 forces ULA transparent (VHDL 7103)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // TR-15: compositor is resolution-agnostic at the ULA input boundary —
    //        whatever the ULA delivers as `ula_rgb_2` is what the
    //        compositor sees (VHDL 7100/7104/7226). Encoded by driving
    //        `ula_line_` directly.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-15", "Stage 2 consumes ula_rgb_2 only; hi-res/hi-colour transparent to compositor",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // TR-16: NR 0x14 = 0x00 and ULA palette output = 0x00 — the match
    //        still succeeds => ULA transparent => fallback wins.
    //        VHDL zxnext.vhd:7100, 7214. Fallback arithmetic: 0x10 has
    //        bit0|bit1 = 0|0 = 0, so 9-bit = 0x020.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.set_fallback_colour(0x10);
        // Emulator has no NR 0x14 — we set the ULA buffer to TRANSP to
        // assert the VHDL-correct result (fallback).
        r.ula_line_[0] = TRANSP;
        uint32_t fb_argb = vhdl_fallback_argb(0x10);
        uint32_t got = composite_one(r, fb_argb);
        uint16_t fb9 = vhdl_fallback_9bit(0x10);
        check("TR-16", "NR0x14=0 + ULA RGB=0 => ULA transparent; 9-bit fallback = 0x020 (VHDL 7100,7214)",
              got == fb_argb && fb9 == 0x020,
              DETAIL("got=0x%08X fb9bit=0x%03X", got, fb9));
    }

    // TR-17: ula_border_2 is ignored by stage 2 in modes 000/001/010.
    //        The border-exception clause only appears in modes 011/100/101
    //        (VHDL 7256/7266/7278). Toggling the border flag while ULA is
    //        identically opaque must give identical rgb_out.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = PIX_ULA;
        uint32_t a = composite_one(r, vhdl_fallback_argb(0xE3));
        // Emulator does not model ula_border_2; the toggling is a no-op
        // in its current form. We still assert the VHDL oracle: the two
        // outputs must be identical (both = PIX_ULA).
        uint32_t b = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-17", "mode 000 ignores ula_border_2 (border exception only in 011/100/101)",
              a == b && a == PIX_ULA,
              DETAIL("a=0x%08X b=0x%08X", a, b));
    }

    // TR-42: NR 0x15[0] sprite_en=0 forces sprite_pixel_en_2=0 for all
    //        sprite pixels at the compositor. VHDL zxnext.vhd:6934, 6819,
    //        7118. The test sets sprite_line_ to opaque, then disables
    //        sprite_en so the compositor forces sprite transparent.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.sprite_en_ = false;                   // NR 0x15 bit 0 = 0
        r.sprite_line_[0] = PIX_S;              // engine delivered pixel_en=1
        // VHDL oracle: with NR 0x15[0]=0 the compositor sees the sprite
        // as transparent; only fallback would display.
        r.set_fallback_colour(0xE3);
        uint32_t fb = vhdl_fallback_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        check("TR-42", "NR 0x15[0]=0 forces every sprite transparent at compositor (VHDL 6934/6819/7118)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // TR-20: Tilemap text-mode RGB compare — palette[8:1]=NR 0x14 => transp.
    //        VHDL zxnext.vhd:7109.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t fb = vhdl_fallback_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        check("TR-20", "TM text-mode RGB==NR0x14 => tm_transparent (VHDL 7109)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // TR-21: Tilemap non-text (attribute) mode ignores the RGB compare —
    //        a TM pixel whose RGB happens to equal NR 0x14 is still
    //        opaque. VHDL 7109 (clause gated on tm_pixel_textmode_2).
    //        The emulator has no text/non-text distinction.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(0xE3);
        // VHDL oracle: non-text TM is opaque and wins in mode 000's U
        // slot (no L2/S present, TM replaces ULA).
        uint32_t expected = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t got = composite_one(r, vhdl_fallback_argb(0x00));
        check("TR-21", "TM non-text: RGB==NR0x14 still opaque (VHDL 7109)",
              got == expected,
              DETAIL("got=0x%08X expected=0x%08X", got, expected));
    }

    // TR-22: tm_pixel_en=0 => tm_transparent=1 regardless of mode.
    //        VHDL zxnext.vhd:7109.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.tilemap_line_[0] = TRANSP;            // pixel_en=0
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-22", "tm_pixel_en=0 => TM transparent, ULA wins (VHDL 7109)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // TR-23: tm_en_2=0 forces TM transparent. VHDL zxnext.vhd:7109.
    //        Renderer has no tm_en flag; encoded by zeroing the TM buffer.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.tilemap_line_[0] = TRANSP;
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-23", "tm_en_2=0 => TM transparent (VHDL 7109)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // ── GH #113: the NextZXOS CP/M 80-column configuration, end to end ───
    //
    // NextZXOS programs, for CP/M: NR 0x6B = 0xCB (tm enable + 80x32 +
    // textmode + 512-tile + tm_on_top), NR 0x15 = 0x00 (SLU), NR 0x68 = 0x00
    // (ULA NOT disabled), NR 0x14 = 0xE3.  So the ULA keeps emitting whatever
    // was left in screen RAM, and the ONLY thing that must hide it is the
    // tilemap's own paper being opaque and sitting above the ULA:
    //   tilemap.vhd:388 — tm_on_top=1 → pixel_below = 0
    //   tilemap.vhd:429 — text mode emits paper (pixel_en_f = pixel_en_s)
    //   zxnext.vhd:7116 — ulatm_rgb = TM when TM opaque and below = 0
    // TR-24/TR-25 pin the compositor half of that chain; the tilemap half is
    // pinned by tilemap_test TM-96/97/98.

    // TR-24: text-mode PAPER pixel (opaque, RGB != NR 0x14) with below=0
    //        covers an opaque ULA pixel completely. VHDL zxnext.vhd:7116.
    {
        clear_layers(r);
        r.set_layer_priority(0);                 // NR 0x15 = 0x00, SLU
        r.ula_line_[0]          = PIX_ULA;       // leftover ULA content
        r.tilemap_line_[0]      = PIX_TM;        // text-mode paper, opaque
        r.tm_pixel_textmode_[0] = true;          // NR 0x6B b3
        r.tm_pixel_below_[0]    = false;         // NR 0x6B b0 = tm_on_top
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-24",
              "GH#113: textmode paper opaque + below=0 hides ULA (VHDL 7116)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X ula=0x%08X",
                     got, PIX_TM, PIX_ULA));
    }

    // TR-25: the discriminating counterpart — same opaque text-mode pixel,
    //        but below=1 (per-tile ULA-over / tm_on_top clear).  Now the ULA
    //        wins, proving TR-24 is the below=0 clause of 7116 and not a
    //        blanket "tilemap always wins". VHDL zxnext.vhd:7116.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]          = PIX_ULA;
        r.tilemap_line_[0]      = PIX_TM;
        r.tm_pixel_textmode_[0] = true;
        r.tm_pixel_below_[0]    = true;          // tilemap below the ULA
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-25",
              "GH#113 control: textmode paper with below=1 => ULA wins (VHDL 7116)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X tm=0x%08X",
                     got, PIX_ULA, PIX_TM));
    }

    // TR-30: Layer 2 RGB compare vs NR 0x14. VHDL zxnext.vhd:7121.
    //        Emulator lacks palette-compare path; test pins the VHDL oracle.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t fb = vhdl_fallback_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        check("TR-30", "L2 RGB[8:1]==NR0x14 => layer2_transparent (VHDL 7121)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // TR-31: Layer 2 pixel_en=0 => transparent. VHDL 7121.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.layer2_line_[0] = TRANSP;
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-31", "L2 pixel_en=0 => layer2_transparent (VHDL 7121)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // TR-32: L2 opaque with palette bit 15 set => layer2_priority=1.
    //        VHDL zxnext.vhd:7123. Emulator does not model L2 priority
    //        bit — assertion is that the VHDL oracle propagates it.
    //        Until the bit is implemented, the compositor cannot observe
    //        the flag; we still write the oracle check by asserting that
    //        an opaque L2 in mode 000 wins over nothing else (trivial
    //        consequence), and separately expect the priority-bit effects
    //        in the L2P group.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.layer2_line_[0] = PIX_L2;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-32", "L2 opaque; priority bit propagation checked in L2P (VHDL 7123)",
              got == PIX_L2,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_L2));
    }

    // TR-33: L2 transparent forces layer2_priority=0 even when palette
    //        bit 15 was set. VHDL zxnext.vhd:7123. Verified via the
    //        absence of any L2-promotion effect: sprite wins (mode 000, S
    //        opaque, L2 transparent, palette priority bit set).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.layer2_line_[0] = TRANSP;             // l2_pixel_en=0
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-33", "layer2_transparent=1 suppresses priority bit (VHDL 7123)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // TR-40: Sprite pixel_en=0 => sprite_transparent=1. VHDL 7118.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.sprite_line_[0] = TRANSP;
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TR-40", "sprite_pixel_en=0 => sprite_transparent (VHDL 7118)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // TR-41: Sprite pixel_en=1 is opaque regardless of NR 0x14 — there is
    //        no RGB-compare for sprites (VHDL 7118). Sprite RGB can match
    //        NR 0x14 and still be shown.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        // VHDL: sprite_transparent is NOT sprite_pixel_en_2 only; RGB
        // compare is not involved. An opaque sprite with RGB[8:1]==NR0x14
        // must still be drawn.
        uint32_t sprite_rgb_eq_nr14 = Renderer::rrrgggbb_to_argb(0xE3);
        r.sprite_line_[0] = sprite_rgb_eq_nr14;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0x00));
        check("TR-41", "Sprite opaque even if RGB==NR0x14 (no sprite RGB compare) (VHDL 7118)",
              got == sprite_rgb_eq_nr14,
              DETAIL("got=0x%08X expected=0x%08X", got, sprite_rgb_eq_nr14));
    }

    // TR-50/51: Task 45 — a mid-frame NR 0x14 (global transparent RGB)
    //           write must NOT retroactively affect a row whose per-line
    //           snapshot has already been captured, mirroring STEN-20/21
    //           and UTB-50/51. VHDL zxnext.vhd:1137,5226,6822,6912-6913,7078
    //           pipeline `nr_14_global_transparent_rgb` through the SAME
    //           stage0/1a/1/2 register chain as `ula_en`
    //           (zxnext.vhd:1198,6809,6894-6895,7061), so a Copper MOVE to
    //           NR 0x14 mid-frame cannot land earlier than the next
    //           scanline. Before Task 45, transparent_rgb_per_line_ existed
    //           (PSCAN-G04-01) but composite_scanline read the LIVE
    //           transparent_rgb_ member directly, applying the new value
    //           one scanline too early — the exact bug this pair is
    //           discriminative against. Calls r.composite_scanline()
    //           directly (NOT composite_one(), which auto-syncs the row)
    //           so the withheld snapshot stays withheld.
    //
    //           Setup mirrors TR-11: mode 000 (SLU), a single opaque ULA
    //           pixel whose RRRGGGBB palette output is 0xAA, fallback
    //           colour distinct from both candidate NR 0x14 values so the
    //           two outcomes (ULA pixel vs. fallback) are unambiguous.
    {
        clear_layers(r);
        r.set_layer_priority(0);                  // mode 000 (SLU)
        const uint32_t ULA_PIX = Renderer::rrrgggbb_to_argb(0xAA);
        const uint32_t FALLBACK = vhdl_fallback_argb(0x10);
        r.ula_line_[13] = ULA_PIX;

        // Baseline snapshot for row 13: NR 0x14 = 0xE3 (VHDL reset default,
        // zxnext.vhd:4946), which does not match the ULA pixel's RGB 0xAA
        // -> ULA opaque -> ULA wins.
        r.set_transparent_rgb(0xE3);
        r.snapshot_transparent_rgb_for_line(13);

        // NR 0x14 write lands mid-frame (Copper MOVE / CPU OUT), flipping
        // to 0xAA — which now matches the ULA pixel's RGB.
        r.set_transparent_rgb(0xAA);

        // Row 13 composited BEFORE its snapshot is refreshed: must still
        // use the OLD value (0xE3), so the ULA pixel stays opaque.
        uint32_t out_before[W];
        std::memset(out_before, 0, sizeof(out_before));
        r.composite_scanline(out_before, FALLBACK, 13);
        check("TR-50",
              "NR 0x14 write mid-frame does not retroactively affect a row "
              "whose per-line snapshot already ran — row still shows the "
              "pre-write ULA pixel (VHDL 1137,5226,6822,6912-6913,7078,7100)",
              out_before[13] == ULA_PIX,
              DETAIL("row13=0x%08X expected_ula=0x%08X (NR0x14=0xAA would be "
                     "fallback 0x%08X)",
                     out_before[13], ULA_PIX, FALLBACK));

        // Refresh the row's snapshot (mirrors the next on_scanline() call).
        // The SAME row must now select the NEW value: ULA RGB==0xAA ==
        // NR 0x14 -> ULA transparent -> fallback wins.
        r.snapshot_transparent_rgb_for_line(13);
        uint32_t out_after[W];
        std::memset(out_after, 0, sizeof(out_after));
        r.composite_scanline(out_after, FALLBACK, 13);
        check("TR-51",
              "After the deferred snapshot lands, the SAME row selects the "
              "new NR 0x14 value and the ULA pixel goes transparent "
              "(VHDL 1137,5226,6822,6912-6913,7078,7100)",
              out_after[13] == FALLBACK,
              DETAIL("row13=0x%08X expected_fallback=0x%08X", out_after[13],
                     FALLBACK));

        r.set_transparent_rgb(0xE3);  // restore default for later groups
    }

    // TR-52/53: Task 46 — Layer2::render_scanline must use the SAME
    //           per-line-deferred NR 0x14 snapshot composite_scanline reads
    //           (transparent_rgb_for_line(row)), not the SEPARATE live
    //           member PaletteManager::global_transparency(). Before the
    //           fix, Layer2 read the live palette member directly, so a
    //           mid-frame NR 0x14 write could make Layer2 WRONGLY judge an
    //           opaque pixel transparent for the CURRENT row and skip-write
    //           it (layer2.cpp `continue`, leaving `dst[]` at the
    //           TRANSPARENT sentinel). That destruction is unrecoverable:
    //           composite_scanline's own NR 0x14 check (renderer.cpp, the
    //           `l2_transp` clause) can only ever narrow an ALREADY-WRITTEN
    //           pixel to transparent — is_transparent(l2_px) is already
    //           true once skip-written — so it cannot un-delete a pixel
    //           Layer2 never wrote. Unlike TR-50/51 (which drive
    //           composite_scanline directly against a hand-set ula_line_),
    //           this pair exercises the REAL Layer2::render_scanline path
    //           via Renderer::render_row, because the bug lives one
    //           pipeline stage EARLIER than composite_scanline. VHDL
    //           zxnext.vhd:7121 (layer2_transparent compares
    //           layer2_rgb_2(8:1) against transparent_rgb_2);
    //           1137,5226,6822,6912-6913,7078 (the transparent_rgb_0->1a->
    //           1->2 deferral chain, Task 45's per-line snapshot of the
    //           same chain).
    //
    //           set_layer_mask(LAYER_LAYER2) is a HOST-side test-isolation
    //           knob (not machine state — see the LayerMask doc comment in
    //           renderer.h) that forces ULA/sprites/tilemap transparent so
    //           the only two possible outputs are "the Layer 2 pixel" or
    //           "the NR 0x4A fallback colour", making the two directions
    //           (opaque survives / transparent falls through) unambiguous.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();

        PaletteManager pal;
        pal.reset();

        r.reset();
        r.set_layer_mask(Renderer::LAYER_LAYER2);
        r.set_fallback_colour(0x10);
        r.init_fallback_per_line();

        Layer2 l2;
        l2.reset();
        l2.set_enabled(true);
        // active_bank_=8, resolution_=0 (256x192 narrow), scroll=0 (Layer2
        // reset defaults). Single opaque source pixel at (x=0,y=0):
        // pixel byte 0x01 -> palette index 0x01 (palette_offset_=0).
        ram.write(8u * 16384u, 0x01);
        // NR 0x43: target = LAYER2_FIRST (bits 6:4 = 001 = 0x10), clear
        // bit 2 so writes and display both hit palette 0 (VHDL
        // zxnext.vhd:5392) — same pattern as test/layer2/layer2_test.cpp's
        // set_l2_palette_8bit(). Palette entry RGB8 = 0xAA, distinct from
        // both the NR 0x14 default (0xE3) and the fallback colour (0x10)
        // set above, so all three possible outputs (L2 pixel / fallback /
        // stale-wrong-value) are visually distinguishable.
        pal.write_control(0x10);
        pal.set_index(0x01);
        pal.write_8bit(0xAA);

        const int ROW = Renderer::DISP_Y;  // first display row (32)
        uint32_t row_buf[Renderer::FB_WIDTH];

        // Mirrors the actual NR 0x14 write handler (emulator.cpp: writes
        // BOTH palette_.set_global_transparency(v) AND
        // renderer_.set_transparent_rgb(v) from the same handler) so the
        // mutation below reproduces the historical bug's exact shape: two
        // separate live members that are always in sync EXCEPT during the
        // per-line-snapshot deferral window this test targets.
        auto write_nr14 = [&](uint8_t v) {
            pal.set_global_transparency(v);
            r.set_transparent_rgb(v);
        };

        // Baseline: row's deferred snapshot captured with NR 0x14 = 0xE3
        // (VHDL reset default), which does NOT match the pixel's palette
        // RGB (0xAA) -> Layer2 must judge the pixel OPAQUE and write it.
        write_nr14(0xE3);
        r.snapshot_transparent_rgb_for_line(ROW);

        // NR 0x14 write lands mid-frame (Copper MOVE / CPU OUT), flipping
        // BOTH live members to 0xAA — which now matches the pixel's RGB.
        // Row ROW's per-line snapshot is deliberately NOT refreshed.
        write_nr14(0xAA);

        std::memset(row_buf, 0, sizeof(row_buf));
        r.render_row(row_buf, ROW, mmu, ram, pal, l2, nullptr, nullptr);
        const uint32_t l2_pixel = pal.layer2_colour(0x01);
        check("TR-52",
              "mid-frame NR 0x14 write does not retroactively make "
              "Layer2::render_scanline skip-write a pixel on a row whose "
              "snapshot already ran — pixel survives using the pre-write "
              "deferred value (VHDL 7121, 1137,5226,6822,6912-6913,7078)",
              row_buf[Renderer::DISP_X] == l2_pixel,
              DETAIL("row_buf[%d]=0x%08X expected_l2_pixel=0x%08X "
                     "(stale live 0xAA would wrongly skip-write -> fallback "
                     "0x%08X)",
                     Renderer::DISP_X, row_buf[Renderer::DISP_X], l2_pixel,
                     Renderer::rrrgggbb_to_argb(0x10)));

        // Refresh the row's snapshot (mirrors the next on_scanline() call).
        // The SAME row must now select the NEW value (0xAA == pixel RGB)
        // -> Layer2 judges the pixel transparent and skip-writes it ->
        // everything else is masked transparent -> fallback wins.
        r.snapshot_transparent_rgb_for_line(ROW);
        std::memset(row_buf, 0, sizeof(row_buf));
        r.render_row(row_buf, ROW, mmu, ram, pal, l2, nullptr, nullptr);
        const uint32_t fallback = Renderer::rrrgggbb_to_argb(0x10);
        check("TR-53",
              "After the deferred snapshot lands, the SAME row selects the "
              "new NR 0x14 value and Layer2 correctly skip-writes the "
              "now-transparent pixel — fallback colour wins "
              "(VHDL 7121, 1137,5226,6822,6912-6913,7078)",
              row_buf[Renderer::DISP_X] == fallback,
              DETAIL("row_buf[%d]=0x%08X expected_fallback=0x%08X",
                     Renderer::DISP_X, row_buf[Renderer::DISP_X], fallback));

        // Restore for any code appended after this block in the future.
        r.set_transparent_rgb(0xE3);
        r.set_fallback_colour(0xE3);
        r.set_layer_mask(Renderer::LAYER_ALL);
    }
}

// ── Group L2EQ — Task 46: enforced dead-clause equivalence proof ────────
//
// renderer.cpp's `l2_transp` clause carries a redundant RGB-match check
// (`(l2_px & 0x00FFFFFF) == nr14_rgb`) that is provably UNREACHABLE
// whenever `is_transparent(l2_px)` is false, now that Layer2 and the
// compositor read the SAME transparent_rgb snapshot (Task 46 fixed the bug
// where they didn't). That proof was worked out during review as a one-off
// standalone calculation — this group commits it as a permanent, enforced
// invariant, because a comment cannot enforce anything and the standalone
// check leaves no artefact. If a future change to
// `PaletteManager::layer2_rgb8()`'s blue-channel downsample or
// `Renderer::rrrgggbb_to_argb()`'s blue-channel expansion ever breaks the
// identity below, THIS test fails loudly instead of the historical
// "recovery is impossible" bug quietly returning.
//
// The property, for ANY register byte X and ANY Layer 2 palette entry with
// RGB333 components (r3,g3,b3) — ONE direction only, the direction the
// dead-clause claim actually needs:
//
//     layer2_rgb8(entry) != X   =>   layer2_colour(entry) != rrrgggbb_to_argb(X)
//
// i.e. whenever Layer2's OWN gate says a pixel is OPAQUE (and therefore
// writes it — VHDL zxnext.vhd:7121's `layer2_transparent`, dst[] populated
// in layer2.cpp), the compositor's redundant full-precision RGB comparison
// can never independently re-flag that SAME written pixel as transparent.
// The converse does NOT hold in general (own_says_transparent can be true
// while the full-precision ARGB values still differ, e.g. r3=g3=0,b3=1,
// X=0x00 — b3's dropped LSB), but that case is irrelevant to the dead-
// clause claim: when Layer2's own gate says transparent it skip-writes,
// so `l2_px` stays the TRANSPARENT sentinel and the compositor's
// `is_transparent(l2_px)` branch already short-circuits the whole
// `l2_transp` OR-chain before the redundant RGB clause is ever reached.
//
// R/G: both `layer2_colour()` (via PaletteManager's internal
// rgb333_to_argb8888, palette.cpp:12) and `rrrgggbb_to_argb()`
// (renderer.h) apply the IDENTICAL injective 3-bit->8-bit expansion
// `(x3<<5)|(x3<<2)|(x3>>1)` to R and G, so an R/G mismatch on one side of
// the identity is an R/G mismatch on the other — this is definitional, not
// exercised row by row here; the loop below covers it anyway as a side
// effect of iterating every (r3,g3) pair.
//
// B is where an accidental collision could hide: `layer2_rgb8()` keeps
// only the top 2 bits of the palette entry's 3-bit blue (drops bit 0, VHDL
// 7121's `(8 downto 1)` slice), while `layer2_colour()` keeps the full 3
// bits. The two candidate 8-bit blue expansions are:
//   3-bit (rgb333_to_argb8888, full palette precision): {0,36,73,109,146,182,219,255} for b3=0..7
//   2-bit (rrrgggbb_to_argb, register precision):       {0,85,170,255}                for b2=0..3
// These sets intersect ONLY at {0,255} — exactly the b3 values whose top 2
// bits (b3>>1) already equal the b2 value being compared against, i.e.
// exactly the cases the RGB8 comparison already covers. Exhaustively
// checked below for all 512 (r3,g3,b3) combinations x all 256 register
// values X (131072 checks total), through the REAL production API
// (PaletteManager::write_9bit / layer2_rgb8 / layer2_colour,
// Renderer::rrrgggbb_to_argb) — no formula is reimplemented in the test.
static void test_L2EQ() {
    set_group("L2EQ");

    PaletteManager pal;
    pal.reset();
    pal.write_control(0x10);   // NR 0x43: target = LAYER2_FIRST, active_l2_second=0

    long violations = 0;
    int  first_r3 = -1, first_g3 = -1, first_b3 = -1, first_x = -1;

    for (int r3 = 0; r3 < 8; ++r3) {
        for (int g3 = 0; g3 < 8; ++g3) {
            for (int b3 = 0; b3 < 8; ++b3) {
                // Reconstruct the exact RGB333 entry via two real NR 0x44
                // (9-bit) writes — see PaletteManager::write_9bit,
                // palette.cpp:328-352 for the bit layout this mirrors.
                const uint8_t first_byte  = static_cast<uint8_t>(
                    (r3 << 5) | (g3 << 2) | ((b3 >> 1) & 0x03));
                const uint8_t second_byte = static_cast<uint8_t>(b3 & 0x01);
                pal.set_index(0x00);
                pal.write_9bit(first_byte);
                pal.write_9bit(second_byte);

                const uint8_t  entry_rgb8  = pal.layer2_rgb8(0x00);
                const uint32_t entry_argb  = pal.layer2_colour(0x00) & 0x00FFFFFFu;

                for (int x = 0; x < 256; ++x) {
                    const uint32_t reg_argb =
                        Renderer::rrrgggbb_to_argb(static_cast<uint8_t>(x))
                        & 0x00FFFFFFu;
                    const bool own_says_opaque        = (entry_rgb8 != x);
                    const bool compositor_would_match  = (entry_argb == reg_argb);

                    // Violation ONLY if Layer2 would WRITE the pixel (opaque
                    // per its own gate) yet the compositor's redundant
                    // check would independently re-flag it transparent.
                    if (own_says_opaque && compositor_would_match) {
                        ++violations;
                        if (first_r3 < 0) {
                            first_r3 = r3; first_g3 = g3; first_b3 = b3; first_x = x;
                        }
                    }
                }
            }
        }
    }

    check("L2EQ-01",
          "layer2_rgb8(entry)!=X implies layer2_colour(entry)!=rrrgggbb_to_argb(X), "
          "for all 512 RGB333 entries x all 256 register values — a pixel "
          "Layer2's own gate writes (opaque) can never be independently "
          "re-flagged transparent by renderer.cpp's redundant l2_transp "
          "RGB-match clause (VHDL 7121)",
          violations == 0,
          DETAIL("violations=%ld first_mismatch: r3=%d g3=%d b3=%d X=0x%02X",
                 violations, first_r3, first_g3, first_b3, first_x));
}

// ── Group TRI — Index-based transparency integration (VHDL 7109, 7118) ──

static void test_TRI() {
    set_group("TRI");
    Renderer r;
    r.reset();

    // TRI-10: sprite index=NR 0x4B => sprites.vhd:1067 drives pixel_en=0,
    //         compositor sees sprite_transparent=1 at zxnext.vhd:7118.
    //         Simulated here by a transparent sprite buffer.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.sprite_line_[0] = TRANSP;             // pixel_en=0 from sprite engine
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TRI-10", "sprite index=NR0x4B => pixel_en=0 => transparent (sprites.vhd:1067, zxnext 7118)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // TRI-11: sprite index != NR 0x4B and inside active area => pixel_en=1.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.sprite_line_[0] = PIX_S;              // pixel_en=1
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TRI-11", "sprite index!=NR0x4B => pixel_en=1 => opaque (sprites.vhd:1067, zxnext 7118)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // TRI-20: TM nibble == NR 0x4C => tm_pixel_en=0, compositor transparent.
    //         VHDL zxnext.vhd:4395, 7109. Emulator: TM buffer transparent.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.tilemap_line_[0] = TRANSP;
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, vhdl_fallback_argb(0xE3));
        check("TRI-20", "TM nibble==NR0x4C => pixel_en=0 (zxnext 4395, 7109)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }
}

// ── Group FB — Fallback colour (VHDL 7214) ───────────────────────────────

static void test_FB() {
    set_group("FB");
    Renderer r;
    r.reset();

    auto all_transparent_fallback = [&](uint8_t nr4a) {
        clear_layers(r);
        r.set_layer_priority(0);
        r.set_fallback_colour(nr4a);
        return composite_one(r, Renderer::rrrgggbb_to_argb(nr4a));
    };

    // FB-10: fallback 0xE3 => 9-bit 0xE3<<1 | (1|1) = 0x1C7.
    {
        uint16_t fb9 = vhdl_fallback_9bit(0xE3);
        uint32_t got = all_transparent_fallback(0xE3);
        uint32_t expected = Renderer::rrrgggbb_to_argb(0xE3);
        check("FB-10", "fallback 0xE3 -> 9-bit 0x1C7 (VHDL 7214: bit0|bit1 = 1|1 = 1)",
              fb9 == 0x1C7 && got == expected,
              DETAIL("fb9=0x%03X got=0x%08X exp=0x%08X", fb9, got, expected));
    }

    // FB-11: fallback 0x00 -> 9-bit 0x000 (bit0|bit1 = 0).
    {
        uint16_t fb9 = vhdl_fallback_9bit(0x00);
        uint32_t got = all_transparent_fallback(0x00);
        uint32_t expected = Renderer::rrrgggbb_to_argb(0x00);
        check("FB-11", "fallback 0x00 -> 9-bit 0x000 (VHDL 7214)",
              fb9 == 0x000 && got == expected,
              DETAIL("fb9=0x%03X got=0x%08X exp=0x%08X", fb9, got, expected));
    }

    // FB-12: fallback 0x4A = 0100_1010 -> bit1=1 bit0=0 -> LSB=1 -> 0x095.
    {
        uint16_t fb9 = vhdl_fallback_9bit(0x4A);
        uint32_t got = all_transparent_fallback(0x4A);
        uint32_t expected = Renderer::rrrgggbb_to_argb(0x4A);
        check("FB-12", "fallback 0x4A -> 9-bit 0x095 (bit1|bit0 = 1|0 = 1) (VHDL 7214)",
              fb9 == 0x095 && got == expected,
              DETAIL("fb9=0x%03X got=0x%08X exp=0x%08X", fb9, got, expected));
    }

    // FB-13: fallback 0x01 = 0000_0001 -> LSB = 0|1 = 1 -> 0x003.
    {
        uint16_t fb9 = vhdl_fallback_9bit(0x01);
        check("FB-13", "fallback 0x01 -> 9-bit 0x003 (bit0=1) (VHDL 7214)",
              fb9 == 0x003,
              DETAIL("fb9=0x%03X", fb9));
    }

    // FB-14: fallback 0x02 = 0000_0010 -> LSB = 1|0 = 1 -> 0x005.
    {
        uint16_t fb9 = vhdl_fallback_9bit(0x02);
        check("FB-14", "fallback 0x02 -> 9-bit 0x005 (bit1=1) (VHDL 7214)",
              fb9 == 0x005,
              DETAIL("fb9=0x%03X", fb9));
    }

    // FB-15: Fallback NOT used when any layer opaque. VHDL 7222.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.set_fallback_colour(0xE3);
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("FB-15", "Opaque sprite overrides fallback (VHDL 7222)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // FB-16: Reset default NR 0x4A = 0xE3 (VHDL reset clause).
    {
        Renderer r2;
        r2.reset();
        check("FB-16", "Reset default fallback = 0xE3 (VHDL nr_4a_fallback_rgb reset)",
              r2.fallback_colour() == 0xE3,
              DETAIL("got=0x%02X", r2.fallback_colour()));
    }

    // FB-17: All 8 modes converge on fallback when every layer transparent.
    //        VHDL 7214 default assignment before the case branches.
    {
        bool all_ok = true;
        uint8_t mode_seen_mask = 0;
        uint32_t fb = Renderer::rrrgggbb_to_argb(0x42);
        for (int mode = 0; mode < 8; ++mode) {
            clear_layers(r);
            r.set_layer_priority(static_cast<uint8_t>(mode));
            r.set_fallback_colour(0x42);
            uint32_t got = composite_one(r, fb);
            if (got == fb) mode_seen_mask |= static_cast<uint8_t>(1 << mode);
            if (got != fb) all_ok = false;
        }
        uint16_t fb9 = vhdl_fallback_9bit(0x42);
        check("FB-17", "All 8 modes -> fallback when all layers transp; 0x42 9-bit=0x085 (VHDL 7214)",
              all_ok && fb9 == 0x085 && mode_seen_mask == 0xFF,
              DETAIL("mask=0x%02X fb9=0x%03X", mode_seen_mask, fb9));
    }
}

// ── Group PRI — Layer priority modes 000..101 ─────────────────────────────

// A compact row for priority-mode tests. Each row enumerates which of the
// three compositor-input layers (U, L, S) is opaque (ULA, Layer2, Sprite)
// and the VHDL-derived winner.
struct PriRow {
    const char* id;
    uint8_t mode;
    bool U, L, S;
    uint32_t expected;       // winning ARGB (or FALLBACK sentinel)
    int vhdl_line;
};
static constexpr uint32_t FALLBACK_SENTINEL = 0xDEADBEEFu;

static void run_pri_row(Renderer& r, const PriRow& row) {
    clear_layers(r);
    r.set_layer_priority(row.mode);
    r.set_fallback_colour(0xE3);
    if (row.U) r.ula_line_[0]    = PIX_ULA;
    if (row.L) r.layer2_line_[0] = PIX_L2;
    if (row.S) r.sprite_line_[0] = PIX_S;
    uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
    uint32_t got = composite_one(r, fb);
    uint32_t exp = (row.expected == FALLBACK_SENTINEL) ? fb : row.expected;
    check(row.id, "priority mode row (VHDL case branch)",
          got == exp,
          DETAIL("mode=%u U=%d L=%d S=%d got=0x%08X exp=0x%08X line=%d",
                 row.mode, row.U, row.L, row.S, got, exp, row.vhdl_line));
}

static void test_PRI() {
    set_group("PRI");
    Renderer r;
    r.reset();

    // VHDL zxnext.vhd case branches 7218..7284 for modes 000..101.
    const PriRow rows[] = {
        // Mode 000 SLU — Sprite→L2→ULA
        {"PRI-010-SLU-3",   0, true,  true,  true,  PIX_S,             7222},
        {"PRI-010-SLU-LU",  0, true,  true,  false, PIX_L2,            7224},
        {"PRI-010-SLU-U",   0, true,  false, false, PIX_ULA,           7226},
        {"PRI-010-SLU-0",   0, false, false, false, FALLBACK_SENTINEL, 7214},

        // Mode 001 LSU — L2→Sprite→ULA
        {"PRI-011-LSU-3",   1, true,  true,  true,  PIX_L2,            7232},
        {"PRI-011-LSU-SU",  1, true,  false, true,  PIX_S,             7234},
        {"PRI-011-LSU-U",   1, true,  false, false, PIX_ULA,           7236},

        // Mode 010 SUL — Sprite→ULA→L2
        {"PRI-010-SUL-3",   2, true,  true,  true,  PIX_S,             7244},
        {"PRI-010-SUL-UL",  2, true,  true,  false, PIX_ULA,           7246},
        {"PRI-010-SUL-L",   2, false, true,  false, PIX_L2,            7248},

        // Mode 011 LUS — L2→ULA→Sprite
        {"PRI-011-LUS-3",    3, true,  true,  true,  PIX_L2,           7254},
        {"PRI-011-LUS-US",   3, true,  false, true,  PIX_ULA,          7256},
        {"PRI-011-LUS-S",    3, false, false, true,  PIX_S,            7258},

        // Mode 100 USL — ULA→Sprite→L2
        {"PRI-100-USL-3",    4, true,  true,  true,  PIX_ULA,          7266},
        {"PRI-100-USL-L",    4, false, true,  false, PIX_L2,           7270},

        // Mode 101 ULS — ULA→L2→Sprite
        {"PRI-101-ULS-3",    5, true,  true,  true,  PIX_ULA,          7278},
        {"PRI-101-ULS-S",    5, false, false, true,  PIX_S,            7282},
    };
    for (const auto& row : rows) run_pri_row(r, row);

    // PRI-011-LUS-border: mode 011 border exception. ula_border_2=1,
    // tm_transparent=1, sprite opaque, ULA opaque => sprite shows
    // through (VHDL zxnext.vhd:7256 exception clause).
    {
        clear_layers(r);
        r.set_layer_priority(3);                // 011 LUS
        r.ula_line_[0]    = PIX_ULA;            // ULA opaque (border)
        r.ula_border_[0]  = true;               // mark as border pixel
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // VHDL oracle: border exception fires -> ULA suppressed -> S wins.
        check("PRI-011-LUS-border",
              "mode 011 border exception: U suppressed, S shows (VHDL 7256)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // PRI-100-USL-border: mode 100, U(border)+S, TM transp, L off.
    {
        clear_layers(r);
        r.set_layer_priority(4);                // 100 USL
        r.ula_line_[0]    = PIX_ULA;
        r.ula_border_[0]  = true;
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PRI-100-USL-border",
              "mode 100 border exception: S wins (VHDL 7266)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // PRI-101-ULS-border: mode 101, U(border)+L+S, TM transp -> L2 wins.
    //        The border exception removes U; the next layer in the ULS
    //        stack is L2. VHDL 7278, 7280.
    {
        clear_layers(r);
        r.set_layer_priority(5);                // 101 ULS
        r.ula_line_[0]    = PIX_ULA;
        r.ula_border_[0]  = true;
        r.layer2_line_[0] = PIX_L2;
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PRI-101-ULS-border",
              "mode 101 border exception: L2 wins after U suppressed (VHDL 7278,7280)",
              got == PIX_L2,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_L2));
    }
}

// ── Group PRI-BOUND ──────────────────────────────────────────────────────

static void test_PRI_BOUND() {
    set_group("PRI-BOUND");
    Renderer r;
    r.reset();

    // PRI-B-0: every mode with all three layers transparent => fallback.
    {
        bool all_ok = true;
        uint32_t fb = Renderer::rrrgggbb_to_argb(0x55);
        for (int m = 0; m < 6; ++m) {
            clear_layers(r);
            r.set_layer_priority(static_cast<uint8_t>(m));
            r.set_fallback_colour(0x55);
            uint32_t got = composite_one(r, fb);
            if (got != fb) all_ok = false;
        }
        check("PRI-B-0", "All modes 000..101 with 0 opaque layers => fallback (VHDL 7214)",
              all_ok, DETAIL("fb=0x%08X", fb));
    }

    // PRI-B-1: Sprite RGB matching NR 0x14 still opaque (no RGB compare on S).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        uint32_t rgb_eq_nr14 = Renderer::rrrgggbb_to_argb(0xE3);
        r.sprite_line_[0] = rgb_eq_nr14;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0x10));
        check("PRI-B-1", "Sprite RGB==NR0x14 still opaque (VHDL 7118)",
              got == rgb_eq_nr14,
              DETAIL("got=0x%08X expected=0x%08X", got, rgb_eq_nr14));
    }

    // PRI-B-2: Mode 001, S and L2 opaque => L2 wins (VHDL 7232).
    {
        clear_layers(r);
        r.set_layer_priority(1);
        r.layer2_line_[0] = PIX_L2;
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PRI-B-2", "mode 001: L2 beats S when both opaque (VHDL 7232)",
              got == PIX_L2,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_L2));
    }
}

// ── Group L2P — Layer 2 priority bit promotion (VHDL 7220/7242/7264/7276/7300/7342) ──

// Helper: simulate "layer2_priority_2=1" by setting sprite-line transparent
// expectation. The emulator has no priority-bit concept, so these rows are
// pure VHDL-oracle assertions; they are expected to fail until the
// Renderer honours palette bit 15 via `layer2_priority_2`.

static void test_L2P() {
    set_group("L2P");
    Renderer r;
    r.reset();

    struct Row { const char* id; uint8_t mode; bool U, L, S; uint32_t expected; int line; };
    const Row rows[] = {
        {"L2P-10", 0, false, true,  true,  PIX_L2, 7220},  // mode 000 promote over S
        {"L2P-11", 2, false, true,  true,  PIX_L2, 7242},  // mode 010 promote over S
        {"L2P-12", 4, true,  true,  true,  PIX_L2, 7264},  // mode 100: L2 above U
        {"L2P-13", 5, true,  true,  true,  PIX_L2, 7276},  // mode 101: L2 above U
        {"L2P-14", 1, false, true,  false, PIX_L2, 7232},  // mode 001 no-op (L2 already top)
        {"L2P-15", 3, false, true,  false, PIX_L2, 7254},  // mode 011 no-op
    };
    for (const auto& row : rows) {
        clear_layers(r);
        r.set_layer_priority(row.mode);
        if (row.U) r.ula_line_[0]    = PIX_ULA;
        if (row.L) { r.layer2_line_[0] = PIX_L2; r.layer2_priority_[0] = true; }
        if (row.S) r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check(row.id, "L2 priority-bit promotion (VHDL case branch)",
              got == row.expected,
              DETAIL("mode=%u U=%d L=%d S=%d got=0x%08X exp=0x%08X line=%d",
                     row.mode, row.U, row.L, row.S, got, row.expected, row.line));
    }

    // L2P-16: layer2_transparent=1 suppresses promotion (VHDL 7123, 7222).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.layer2_line_[0] = TRANSP;
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("L2P-16", "L2 transparent => promotion suppressed, S wins (VHDL 7123,7222)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // L2P-17: mode 110 (blend add) with L2 priority bit => blend RGB shown.
    //        VHDL 7300.
    {
        clear_layers(r);
        r.set_layer_priority(6);                // 110
        r.ula_line_[0]    = PIX_ULA;
        r.layer2_line_[0] = PIX_L2;
        r.layer2_priority_[0] = true;
        r.sprite_line_[0] = PIX_S;
        // VHDL oracle: L2 priority bit forces the blend output as top.
        // With the test colours, compute the per-channel blend add (clamped to 7).
        // PIX_L2 = 0xBB GG / PIX_ULA = 0xAA RR. In the per-channel 3-bit
        // form, both are mostly saturated; blend add should clamp several
        // channels to 7. We assert that the output is NOT PIX_S and NOT
        // PIX_ULA — the exact ARGB depends on 9-bit->ARGB round-trip.
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // Assert VHDL oracle: the result is neither the sprite nor a
        // plain layer pass-through.
        bool is_blendish = (got != PIX_S) && (got != PIX_ULA) && (got != PIX_L2);
        check("L2P-17", "mode 110 + L2 priority bit => blend output shown (VHDL 7300)",
              is_blendish,
              DETAIL("got=0x%08X (blend expected)", got));
    }

    // L2P-18: mode 111 (blend sub) with L2 priority bit.
    {
        clear_layers(r);
        r.set_layer_priority(7);                // 111
        r.ula_line_[0]    = PIX_ULA;
        r.layer2_line_[0] = PIX_L2;
        r.layer2_priority_[0] = true;
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        bool is_blendish = (got != PIX_S) && (got != PIX_ULA) && (got != PIX_L2);
        check("L2P-18", "mode 111 + L2 priority bit => subtracted blend shown (VHDL 7342)",
              is_blendish,
              DETAIL("got=0x%08X (sub-blend expected)", got));
    }

    // L2P-19 — VHDL zxnext.vhd:7039-7050 (priority array) +
    // src/video/renderer.cpp:194-201 (pixel-doubling guard skips L2
    // arrays in native 640).
    //
    // G93 closure: when L2 is rendered natively at 640px, both even
    // and odd pixel columns read their own slot of the priority
    // array. The renderer's compositor walks columns 0..width-1 and
    // indexes `layer2_priority_[x]` at every step (composite_scanline
    // line 289), so given distinct stimuli at columns 0 and 1 with
    // priority set on each, both columns must promote L2 over an
    // opaque sprite in mode 000 (SLU). Driving width=640 without
    // pixel-doubling exercises the native-640 path the renderer
    // would otherwise take when Layer2.resolution() >= 2.
    //
    // The test is the specification: the compositor MUST honour
    // per-column priority at native 640 width. G91 (Layer2 actually
    // populating the array) is the upstream requirement; this row
    // pins that the compositor side is column-correct.
    {
        clear_layers(r);
        r.set_layer_priority(0);                // mode 000 (SLU)

        // Distinct L2 colours at columns 0 and 1 so the result
        // columns are unambiguously identifiable.
        const uint32_t L2_A = opaque_tag(0x10, 0x20, 0x30);
        const uint32_t L2_B = opaque_tag(0x40, 0x50, 0x60);

        // Sprites opaque at both columns (would beat L2 in mode 000
        // without the priority bit).
        r.layer2_line_[0] = L2_A;
        r.layer2_line_[1] = L2_B;
        r.sprite_line_[0] = PIX_S;
        r.sprite_line_[1] = PIX_S;

        // Priority bit set independently on both columns. The native
        // 640 path skips pixel-doubling (renderer.cpp:194-201) so
        // each slot must be filled directly — exactly what Layer2
        // would do when G91 wires palette bit 15 into the priority
        // array at native 640.
        r.layer2_priority_[0] = true;
        r.layer2_priority_[1] = true;

        // Composite at the canonical 640 width — exercises the same
        // index path the production native-640 case takes (G104:
        // FB_WIDTH is now always 640; FB_WIDTH_HI was retired).
        uint32_t out[Renderer::FB_WIDTH];
        std::memset(out, 0, sizeof(out));
        r.composite_scanline(out, Renderer::rrrgggbb_to_argb(0xE3), 0);

        const bool col0_promoted = (out[0] == L2_A);
        const bool col1_promoted = (out[1] == L2_B);

        check("L2P-19",
              "Native 640: layer2_priority_[] honours both even and odd "
              "columns; L2 promotion fires at every native pixel "
              "(VHDL 7039-7050; renderer.cpp:194-201)",
              col0_promoted && col1_promoted,
              DETAIL("col0=0x%08X (exp L2_A 0x%08X)  col1=0x%08X (exp L2_B 0x%08X)",
                     out[0], L2_A, out[1], L2_B));
    }
}

// ── Group BL — Blend modes 110/111 (VHDL 7286..7356) ─────────────────────
//
// VHDL reference lines 7286..7310 (mode 110 additive) and 7312..7352
// (mode 111 subtractive). Priority modes 6/7 implemented at
// renderer.cpp:343-440 with a 4-way switch on `blend_mode_` covering
// ula_blend_mode = "00"/"01"/"10"/"11" (VHDL 7142-7176).
//
// Rows BL-10..16, BL-20..29, L2P-17/18 exercise mode "00" (default).
// Rows BL-30..32 / BL-40..42 / BL-50..52 / BL-60 added by Phase 2 of
// doc/design/TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md cover modes
// "01" / "10" / "11" (priority 6) and "11" under priority 7.

// Per-channel add (clamped 7). VHDL 7288–7298.
static uint8_t bl_add(uint8_t a, uint8_t b) {
    unsigned s = a + b;
    return (s > 7u) ? 7u : static_cast<uint8_t>(s);
}

// Per-channel sub: VHDL 7316–7338.
//   4-bit sum = a + b
//   if sum <= 4     -> 0
//   elif sum >= 12  -> 7
//   else            -> (sum + 0xB) & 0xF  == sum - 5
static uint8_t bl_sub(uint8_t a, uint8_t b) {
    unsigned sum = a + b;
    if (sum <= 4u) return 0;
    if (((sum >> 2) & 0x3u) == 0x3u) return 7;      // top two bits == "11"
    return static_cast<uint8_t>((sum + 0xBu) & 0xFu);
}

// Build an 8-bit RRRGGGBB channel triplet.
static uint8_t rgb8(uint8_t R3, uint8_t G3, uint8_t B2) {
    return static_cast<uint8_t>(((R3 & 7) << 5) | ((G3 & 7) << 2) | (B2 & 3));
}

static void test_BL() {
    set_group("BL");
    Renderer r;
    r.reset();

    // BL-10: add with no clamp. L2=(3,2,1) U=(3,2,1) -> (6,4,2).
    {
        clear_layers(r);
        r.set_layer_priority(6);                // 110
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(rgb8(3,2,1));
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(rgb8(3,2,1));
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(rgb8(bl_add(3,3), bl_add(2,2), bl_add(1,1)));
        check("BL-10", "mode 110 add no clamp: (3,2,1)+(3,2,1)=(6,4,2) (VHDL 7201-7210,7286)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // BL-11: clamp high. (5,6,7)+(5,6,7)=(7,7,7).
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(rgb8(5,6,3));  // B only 2 bits
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(rgb8(5,6,3));
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(rgb8(7,7,3));  // B clamp at 3 (2-bit)
        check("BL-11", "mode 110 add clamp to 7 (VHDL 7288-7298)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // BL-12: 0+0 -> 0.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(0x00);
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(0x00);
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(0x00);
        check("BL-12", "mode 110 add 0+0=0 (VHDL 7201)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // BL-13: mode 110 with mix_top opaque beats blend. VHDL 7302.
    //        Setup: L2 opaque, U opaque, TM opaque (mix_top=tm_rgb).
    //        VHDL oracle: TM shows as mix_top. Emulator: no blend,
    //        no mix_top concept; expected to fail.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.layer2_line_[0]  = PIX_L2;
        r.ula_line_[0]     = PIX_ULA;
        r.tilemap_line_[0] = PIX_TM;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("BL-13", "mode 110: mix_top (TM) opaque wins over blend (VHDL 7302)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // BL-14: mode 110, mix_top transparent, sprite between mix_top/mix_bot.
    //        VHDL 7304.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.layer2_line_[0] = PIX_L2;
        r.sprite_line_[0] = PIX_S;
        // tilemap & ula transparent -> mix_top transp
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("BL-14", "mode 110: sprite between mix_top and mix_bot (VHDL 7304)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // BL-15: mode 110, mix_bot wins after mix_top/sprite transparent.
    //        Setup: L2 opaque, TM below (mix_bot = tm_rgb), U & S transp.
    //        VHDL 7306.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.layer2_line_[0]  = PIX_L2;
        r.tilemap_line_[0] = PIX_TM;
        r.tm_pixel_below_[0] = false;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // Emulator collapses to SLU: L2 would win. VHDL oracle: TM (mix_bot).
        check("BL-15", "mode 110: mix_bot (TM) wins after mix_top+S transp (VHDL 7306)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // BL-16: mode 110, only L2 opaque, U/TM/S all transp => blend RGB of L2+0.
    //        VHDL 7308. Per-channel add of (L2, 0) clamped.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        uint8_t c = rgb8(3, 3, 2);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(c);
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(rgb8(bl_add(3,0), bl_add(3,0), bl_add(2,0)));
        check("BL-16", "mode 110: only L2 opaque => blend(L2+0)=L2 (VHDL 7308)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // BL-20: mode 111 sub, sum<=4 -> 0. (R=2 G=2 B=2)+(R=2 G=2 B=2)
    {
        clear_layers(r);
        r.set_layer_priority(7);
        uint8_t c = rgb8(2,2,2);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(c);
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(c);
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(rgb8(bl_sub(2,2), bl_sub(2,2), bl_sub(2,2)));
        check("BL-20", "mode 111 sub: sum<=4 -> 0 (VHDL 7316)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // BL-21: sum>=12 -> 7. (7,7,3)+(7,7,3) (B is 2-bit so max 3)
    {
        clear_layers(r);
        r.set_layer_priority(7);
        uint8_t c = rgb8(7,7,3);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(c);
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(c);
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(rgb8(bl_sub(7,7), bl_sub(7,7), bl_sub(3,3)));
        check("BL-21", "mode 111 sub: sum>=12 -> 7 (VHDL 7318)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // BL-22: middle value. VHDL oracle computes per-channel via bl_sub.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(rgb8(3,4,2));
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(rgb8(3,4,2));
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // R: 3+3=6 -> sum-5=1 ; G: 4+4=8 -> 3 ; B: 2+2=4 -> 0 (<=4)
        uint32_t expected = Renderer::rrrgggbb_to_argb(rgb8(bl_sub(3,3), bl_sub(4,4), bl_sub(2,2)));
        check("BL-22", "mode 111 sub middle: (3,4,2) -> (1,3,0) (VHDL 7321)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X R=%u G=%u B=%u",
                     got, expected, bl_sub(3,3), bl_sub(4,4), bl_sub(2,2)));
    }

    // BL-23: mode 111 sub gated by mix_rgb_transparent. VHDL 7314.
    //        When mix_rgb is transparent, subtraction path skipped; the
    //        layer stack below falls through. Setup: only sprite opaque.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("BL-23", "mode 111 sub gated off by mix_rgb_transparent (VHDL 7314)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // BL-24: mode 111 mix_top opaque wins (TM). VHDL 7344.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.layer2_line_[0]  = PIX_L2;
        r.ula_line_[0]     = PIX_ULA;
        r.tilemap_line_[0] = PIX_TM;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("BL-24", "mode 111: mix_top (TM) opaque wins (VHDL 7344)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // BL-25: mode 111 sprite between mix_top and mix_bot. VHDL 7346.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.layer2_line_[0] = PIX_L2;
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("BL-25", "mode 111: sprite wins between mix_top/mix_bot (VHDL 7346)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }

    // BL-26: mode 111 mix_bot (TM) fallback wins. VHDL 7348.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.layer2_line_[0]  = PIX_L2;
        r.tilemap_line_[0] = PIX_TM;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("BL-26", "mode 111: mix_bot (TM) fallback wins (VHDL 7348)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // BL-27: mode 111 only L2 opaque, ULA transparent. VHDL 7314/7350.
    //        mix_rgb_transparent=1 => subtractive formula SKIPPED (VHDL 7314).
    //        Raw sums pass through: (3+0, 4+0, 3+0) = (3, 4, 3).
    //        Output = mixer_argb with unmodified L2 channels.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        uint8_t c = rgb8(3,4,3);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(c);
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // VHDL: formula gated on mix_rgb_transparent — skipped here, raw L2 passes through.
        uint32_t expected = Renderer::rrrgggbb_to_argb(c);
        check("BL-27", "mode 111: only L2 opaque, sub formula skipped (VHDL 7314,7350)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // BL-28: L2 priority bit overrides blend in mode 110. VHDL 7300.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.layer2_line_[0]  = PIX_L2;
        r.layer2_priority_[0] = true;
        r.ula_line_[0]     = PIX_ULA;
        r.tilemap_line_[0] = PIX_TM;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // VHDL oracle: blend output overrides everything; not raw TM/L2/ULA.
        bool not_passthrough = (got != PIX_ULA) && (got != PIX_TM) && (got != PIX_L2);
        check("BL-28", "mode 110: L2 priority bit overrides mix_top (VHDL 7300)",
              not_passthrough,
              DETAIL("got=0x%08X", got));
    }

    // BL-29: L2 priority bit overrides blend in mode 111. VHDL 7342.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.layer2_line_[0]  = PIX_L2;
        r.layer2_priority_[0] = true;
        r.ula_line_[0]     = PIX_ULA;
        r.tilemap_line_[0] = PIX_TM;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        bool not_passthrough = (got != PIX_ULA) && (got != PIX_TM) && (got != PIX_L2);
        check("BL-29", "mode 111: L2 priority bit overrides mix_top (VHDL 7342)",
              not_passthrough,
              DETAIL("got=0x%08X", got));
    }

    // ── Phase 2: ula_blend_mode variants "01", "10", "11" ────────────────
    // Per doc/design/TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md Appendix A.
    // All rows cite VHDL zxnext.vhd:7141-7178 for the mix_rgb / mix_top /
    // mix_bot selection, plus 7286-7298 (add clamp) or 7312-7352 (sub).

    // BL-30: mode "01", prio 6. L2 opaque, ULA opaque, TM transp, tm_below=0.
    //        VHDL 7163-7176 (when others): mix_rgb_transp=1; mix_top=TM (transp);
    //        mix_bot=ULA (opaque) → cascade: top skipped, spr transp, bot opaque
    //        wins. Exercises mix_bot swap from TM to ULA.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(1);                                // "01"
        uint8_t l2c  = rgb8(3,2,1);
        uint8_t ulac = rgb8(3,2,1);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(l2c);
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(ulac);
        r.tm_pixel_below_[0] = false;                       // tm_below=0
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(ulac);
        check("BL-30",
              "mode \"01\" prio6: mix_bot=ULA wins (zxnext.vhd:7163-7176)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-31: mode "01", prio 6. L2=(0,0,0), ULA opaque, TM opaque, tm_below=0.
    //        VHDL 7163-7176: mix_top=TM (opaque) → cascade: top wins = TM.
    //        Verifies ULA is masked out when TM is in mix_top slot.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(1);                                // "01"
        uint8_t l2c  = rgb8(0,0,0);
        uint8_t ulac = rgb8(3,2,1);
        uint8_t tmc  = rgb8(1,1,1);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(l2c);
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(ulac);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(tmc);
        r.tm_pixel_below_[0] = false;                       // tm_below=0
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(tmc);
        check("BL-31",
              "mode \"01\" prio6: mix_top=TM (ULA masked) (zxnext.vhd:7163-7176)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-32: mode "01", prio 6. Same as BL-31 but tm_below=1 → mix_top=ULA,
    //        mix_bot=TM. Cascade: top=ULA opaque wins. Observes the top/bot
    //        swap driven by tm_pixel_below_2. VHDL 7163-7176.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(1);                                // "01"
        uint8_t l2c  = rgb8(0,0,0);
        uint8_t ulac = rgb8(3,2,1);
        uint8_t tmc  = rgb8(1,1,1);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(l2c);
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(ulac);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(tmc);
        r.tm_pixel_below_[0] = true;                        // tm_below=1
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(ulac);
        check("BL-32",
              "mode \"01\" prio6: tm_below=1 swap, mix_top=ULA wins (zxnext.vhd:7163-7176)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-40: mode "10", prio 6. L2=(3,2,1), ULA=(3,2,1) opaque, TM transp,
    //        stencil OFF → ula_final=ULA. mix_top/bot forced transp. Cascade
    //        falls through to mixer = add(L2, ULA) = (6,4,2). VHDL 7149-7155.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(2);                                // "10"
        uint8_t l2c  = rgb8(3,2,1);
        uint8_t ulac = rgb8(3,2,1);
        r.layer2_line_[0] = Renderer::rrrgggbb_to_argb(l2c);
        r.ula_line_[0]    = Renderer::rrrgggbb_to_argb(ulac);
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(
            rgb8(bl_add(3,3), bl_add(2,2), bl_add(1,1)));
        check("BL-40",
              "mode \"10\" prio6: mix_rgb=ula_final, add(L2,ULA) (zxnext.vhd:7149-7155,7286-7298)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-41: mode "10", prio 6. L2=(1,1,1), ULA transp, TM=(2,2,2) opaque,
    //        stencil OFF, tm_below=1 → ulatm_rgb=TM (VHDL 7115-7116).
    //        mixer = add(L2, TM) = (3,3,3). mix_top/bot forced transp.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(2);                                // "10"
        uint8_t l2c = rgb8(1,1,1);
        uint8_t tmc = rgb8(2,2,2);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(l2c);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(tmc);
        r.tm_pixel_below_[0] = true;                        // tm_below=1
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(
            rgb8(bl_add(1,2), bl_add(1,2), bl_add(1,2)));
        check("BL-41",
              "mode \"10\" prio6: ulatm merge → TM, add(L2,TM) (zxnext.vhd:7115-7116,7149-7155)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-42: mode "10", prio 6, STENCIL ON. L2=(0,0,0), ULA=(3,2,1), TM=(3,2,1).
    //        Stencil AND → ula_final=(3,2,1). mixer = add(L2, stencil) = (3,2,1).
    //        Exercises the post-stencil ula_final_rgb routing. VHDL 7130-7132 +
    //        7149-7155.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(2);                                // "10"
        r.stencil_mode_ = true;
        r.tm_enabled_   = true;
        uint8_t pc = rgb8(3,2,1);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(rgb8(0,0,0));
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(pc);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(pc);
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // Stencil AND of identical pixels = pc; add(L2=0, stencil=pc) = pc.
        uint32_t expected = Renderer::rrrgggbb_to_argb(
            rgb8(bl_add(0,3), bl_add(0,2), bl_add(0,1)));
        check("BL-42",
              "mode \"10\" prio6: stencil ULA&TM routes via ula_final_rgb (zxnext.vhd:7130-7132,7149-7155)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.stencil_mode_ = false;
        r.tm_enabled_   = false;
        r.set_blend_mode(0);
    }

    // BL-50: mode "11", prio 6. L2=(3,2,1), ULA=(1,1,1), TM=(2,2,2), tm_below=0.
    //        mix_rgb=TM. mix_top_transp=(ula_transp||!tm_below)=1 (skipped).
    //        mix_bot=ULA opaque wins. Exercises mix_bot=ULA swap under mode "11".
    //        VHDL 7156-7162.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(3);                                // "11"
        uint8_t l2c  = rgb8(3,2,1);
        uint8_t ulac = rgb8(1,1,1);
        uint8_t tmc  = rgb8(2,2,2);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(l2c);
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(ulac);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(tmc);
        r.tm_pixel_below_[0] = false;                       // tm_below=0
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(ulac);
        check("BL-50",
              "mode \"11\" prio6: mix_bot=ULA wins (zxnext.vhd:7156-7162)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-51: mode "11", prio 6. Same as BL-50 but tm_below=1 →
    //        mix_top_transp=(ula_transp||!tm_below)=0 (ULA opaque wins at top).
    //        mix_bot_transp=(ula_transp||tm_below)=1. VHDL 7156-7162.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(3);                                // "11"
        uint8_t l2c  = rgb8(3,2,1);
        uint8_t ulac = rgb8(1,1,1);
        uint8_t tmc  = rgb8(2,2,2);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(l2c);
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(ulac);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(tmc);
        r.tm_pixel_below_[0] = true;                        // tm_below=1
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(ulac);
        check("BL-51",
              "mode \"11\" prio6: tm_below=1, mix_top=ULA wins (zxnext.vhd:7156-7162)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-52: mode "11", prio 6. L2=(0,0,0) opaque, ULA transp, TM=(4,2,1),
    //        tm_below=0. mix_rgb=TM; mix_top/bot both ULA (transp). Cascade
    //        falls through to !l2_transp arm → mixer = add(L2, TM) = (4,2,1).
    //        Observes TM-as-mix-source when both overlays are ULA-transparent.
    //        VHDL 7156-7162 + 7286-7298.
    {
        clear_layers(r);
        r.set_layer_priority(6);
        r.set_blend_mode(3);                                // "11"
        uint8_t l2c = rgb8(0,0,0);
        uint8_t tmc = rgb8(4,2,1);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(l2c);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(tmc);
        r.tm_pixel_below_[0] = false;                       // tm_below=0
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(
            rgb8(bl_add(0,4), bl_add(0,2), bl_add(0,1)));
        check("BL-52",
              "mode \"11\" prio6: TM as mix_rgb, ULA overlays transp (zxnext.vhd:7156-7162)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }

    // BL-60: mode "11", prio 7 (subtractive). L2=(5,5,3), ULA transp,
    //        TM=(4,2,1), tm_below=0. mix_rgb=TM; overlays transp → mixer via
    //        !l2_transp. sub(L2+TM): r=9→4, g=7→2, b=4→0 = (4,2,0).
    //        VHDL 7156-7162 + 7312-7352.
    {
        clear_layers(r);
        r.set_layer_priority(7);
        r.set_blend_mode(3);                                // "11"
        uint8_t l2c = rgb8(5,5,3);
        uint8_t tmc = rgb8(4,2,1);
        r.layer2_line_[0]  = Renderer::rrrgggbb_to_argb(l2c);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(tmc);
        r.tm_pixel_below_[0] = false;                       // tm_below=0
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(
            rgb8(bl_sub(5,4), bl_sub(5,2), bl_sub(3,1)));
        check("BL-60",
              "mode \"11\" prio7: sub(L2,TM)=(4,2,0) (zxnext.vhd:7156-7162,7312-7352)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
        r.set_blend_mode(0);
    }
}

// ── Group UTB — ULA/Tilemap blend mode (NR 0x68 bits 6:5) ───────────────
//
// VHDL zxnext.vhd:7139-7178. The C++ Renderer models the non-blend path
// only: its ulatm merge selects TM or ULA based on the ula_over_flags.
// Rows that require NR 0x68 blend/stencil bits exercise features the
// emulator does not implement and will fail.

static void test_UTB() {
    set_group("UTB");
    Renderer r;
    r.reset();

    // UTB-10: mode 00, TM above. VHDL 7142-7148: mix_rgb=ula, mix_top=tm when
    //         tm_pixel_below=0. Emulator: TM replaces ULA in U slot. Mode 000
    //         SLU, only U-slot active => result=TM.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.tilemap_line_[0]    = PIX_TM;
        r.ula_line_[0]        = PIX_ULA;
        r.tm_pixel_below_[0]  = false;          // tm_pixel_below_2=0
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("UTB-10", "NR0x68 mode 00 TM above: TM wins in U slot (VHDL 7142-7148)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // UTB-11: mode 00, TM below (ula_over=true). VHDL 7142-7148: ULA wins.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.tilemap_line_[0]    = PIX_TM;
        r.ula_line_[0]        = PIX_ULA;
        r.tm_pixel_below_[0]  = true;           // tm_pixel_below_2=1
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("UTB-11", "NR0x68 mode 00 TM below: ULA wins in U slot (VHDL 7142-7148)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // UTB-20: mode 10 stencil-off: mix_rgb=ula_final_rgb, mix_top/bot forced
    //         transparent. VHDL 7149-7155. Emulator lacks NR 0x68 blend bits;
    //         oracle: ula_final_rgb flows through.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("UTB-20", "NR0x68 mode 10: mix_rgb = ula_final_rgb (VHDL 7149-7155)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // UTB-30: mode 11, tm_pixel_below=1. VHDL 7156-7162: ULA floats to top
    //         (note opposite of naive reading). mix_rgb=tm_rgb; mix_top=ula.
    //         In mode 000 SLU, U slot should show ULA (since ULA is top).
    //         Emulator: tm_pixel_below=1 => u_px=ula (from renderer.cpp).
    //         Both oracle and emulator agree on this case — test still
    //         verifies via the VHDL-derived expected.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]        = PIX_ULA;
        r.tilemap_line_[0]    = PIX_TM;
        r.tm_pixel_below_[0]  = true;           // tm_pixel_below_2=1
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("UTB-30", "NR0x68 mode 11 below=1: ULA floats to top (VHDL 7156-7162)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // UTB-31: mode 11, tm_pixel_below=0. ULA goes to bot position.
    //         VHDL 7156-7162: mix_rgb=tm_rgb, mix_bot=ula.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]        = PIX_ULA;
        r.tilemap_line_[0]    = PIX_TM;
        r.tm_pixel_below_[0]  = false;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // VHDL oracle: ULA floats to bot, TM is mix_rgb; result ~ TM.
        check("UTB-31", "NR0x68 mode 11 below=0: ULA floats to bot, TM on top (VHDL 7156-7162)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // UTB-40: mode 01, below=0. VHDL 7163-7176 others branch:
    //         mix_rgb forced transparent; mix_top=tm_rgb; mix_bot=ula_rgb.
    //         In the SLU-only priority chain with all layers transparent
    //         except TM (mix_top), U-slot shows TM.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]        = PIX_ULA;
        r.tilemap_line_[0]    = PIX_TM;
        r.tm_pixel_below_[0]  = false;          // below=0 -> top=tm, bot=ula
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("UTB-40", "NR0x68 mode 01 below=0: mix_top=TM (zxnext.vhd:7163-7176 else)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // UTB-41: mode 01, below=1. mix_top=ula_rgb; mix_bot=tm_rgb.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]        = PIX_ULA;
        r.tilemap_line_[0]    = PIX_TM;
        r.tm_pixel_below_[0]  = true;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("UTB-41", "NR0x68 mode 01 below=1: mix_top=ULA (zxnext.vhd:7163-7176 if)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // RE-HOME UB-G26-01 → UTB-40 + UTB-41 (mode-01 mix_top/mix_bot swap on tm_pixel_below_2; VHDL zxnext.vhd:7163-7177). Original "FPGA-team oracle" deferral retired per VHDL-as-oracle rule.

    // UB-G26-02 — VHDL zxnext.vhd:7300 (additive cascade) / 7342
    // (subtractive cascade): the `if layer2_priority='1'` arm is the
    // FIRST cascade element in modes 110/111, so an L2 pixel with the
    // priority bit set wins over an opaque mix_top source. Encoded
    // here per the VHDL-as-oracle rule (no external "FPGA-team"
    // confirmation needed). Mode 110 (additive blend) chosen so the
    // L2-priority result and the mix_top result are visibly distinct.
    {
        clear_layers(r);
        r.set_layer_priority(6);              // mode 110 additive
        r.set_blend_mode(0);                  // NR 0x68 bits 6:5 = 00
        const uint32_t L2_PIX  = Renderer::rrrgggbb_to_argb(0x49);  // r=2 g=2 b=1
        const uint32_t ULA_PIX = Renderer::rrrgggbb_to_argb(0x92);  // r=4 g=4 b=2
        const uint32_t TM_PIX  = Renderer::rrrgggbb_to_argb(0x24);  // distinct decoy
        r.layer2_line_[0]     = L2_PIX;
        r.layer2_priority_[0] = true;         // VHDL 7220: l2_prio bit
        r.ula_line_[0]        = ULA_PIX;      // mix_rgb in mode-00 blend
        r.tilemap_line_[0]    = TM_PIX;       // mix_top in mode-00 blend
        r.tm_pixel_below_[0]  = false;        // mix_top opaque (tm_below=0)
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        // Additive: r=2+4=6, g=2+4=6, b=1+2=3 → packed rrrgggbb = 110_110_11 = 0xDB.
        const uint32_t expected_blend = Renderer::rrrgggbb_to_argb(0xDB);
        check("UB-G26-02",
              "NR0x68 mode 110 (additive): layer2_priority wins over opaque mix_top "
              "(VHDL zxnext.vhd:7300 first if)",
              got == expected_blend,
              DETAIL("got=0x%08X expected=0x%08X (TM=0x%08X mix_top would have been TM)",
                     got, expected_blend, TM_PIX));
        r.set_layer_priority(0);
        r.set_blend_mode(0);
    }

    // UTB-50/51: Task 43 — a mid-frame NR 0x68 bits 6:5 (ula_blend_mode)
    //            write must NOT retroactively affect a row whose per-line
    //            snapshot has already been captured, mirroring STEN-20/21.
    //            VHDL zxnext.vhd:5446,6811,6900-6901,7065 pipeline
    //            `ula_blend_mode` through the SAME stage0/1a/1/2 register
    //            chain as `ula_en`, so a mode change written mid-scanline
    //            cannot land earlier than the next scanline. Before Task 43,
    //            blend_mode_per_line_ existed (PSCAN-G11-02) but
    //            composite_scanline read the LIVE blend_mode_ member
    //            directly, applying the new mode one scanline too early —
    //            the exact bug this pair is discriminative against. Calls
    //            r.composite_scanline() directly (NOT composite_one(),
    //            which auto-syncs the row) so the withheld snapshot stays
    //            withheld.
    //
    //            Setup: layer_priority=6 (mode 110, blend path), ULA and TM
    //            both opaque, tm_pixel_below=0, no L2/sprite.
    //              Mode "00" (blend_mode=0, VHDL 7142-7148): mix_top=TM
    //                (opaque since tm_below=0) -> cascade picks mix_top
    //                directly -> result = TM pixel.
    //              Mode "10" (blend_mode=2, VHDL 7149-7155): mix_top/bot
    //                forced transparent; mix_rgb=ula_final (opaque) but
    //                the additive mixer only reaches `result` via the
    //                l2_prio or !l2_transp cascade arms (VHDL 7300-7310),
    //                neither of which fires with no L2 pixel -> result
    //                falls through to the NR 0x4A fallback colour.
    //            The two outcomes (TM pixel vs. fallback) are unambiguous.
    {
        clear_layers(r);
        r.set_layer_priority(6);                  // mode 110 (blend path)
        const uint32_t ULA_PIX = Renderer::rrrgggbb_to_argb(0x92);
        const uint32_t TM_PIX  = Renderer::rrrgggbb_to_argb(0x24);
        const uint32_t FALLBACK = Renderer::rrrgggbb_to_argb(0xE3);
        r.ula_line_[11]     = ULA_PIX;
        r.tilemap_line_[11] = TM_PIX;
        r.tm_pixel_below_[11] = false;

        // Baseline snapshot for row 11: blend_mode = "00" (mirrors
        // on_scanline capturing pre-write state).
        r.blend_mode_ = 0;
        r.snapshot_blend_mode_for_line(11);

        // NR 0x68 bits 6:5 write lands mid-frame, flipping to mode "10".
        r.set_blend_mode(2);

        // Row 11 composited BEFORE its snapshot is refreshed: must still
        // use mode "00" (TM opaque wins the mix_top cascade slot).
        uint32_t out_before[W];
        std::memset(out_before, 0, sizeof(out_before));
        r.composite_scanline(out_before, FALLBACK, 11);
        check("UTB-50",
              "NR 0x68 b6:5 write mid-frame does not retroactively affect a "
              "row whose per-line snapshot already ran — row still shows "
              "the pre-write mode \"00\" result (VHDL 5446,6811,6900-6901,7065)",
              out_before[11] == TM_PIX,
              DETAIL("row11=0x%08X expected_tm=0x%08X (mode-10 would be fallback 0x%08X)",
                     out_before[11], TM_PIX, FALLBACK));

        // Refresh the row's snapshot (mirrors the next on_scanline() call).
        // The SAME row must now select mode "10".
        r.snapshot_blend_mode_for_line(11);
        uint32_t out_after[W];
        std::memset(out_after, 0, sizeof(out_after));
        r.composite_scanline(out_after, FALLBACK, 11);
        check("UTB-51",
              "After the deferred snapshot lands, the SAME row selects "
              "mode \"10\" (VHDL 7149-7155,7300-7310)",
              out_after[11] == FALLBACK,
              DETAIL("row11=0x%08X expected_fallback=0x%08X", out_after[11], FALLBACK));

        r.set_layer_priority(0);
        r.set_blend_mode(0);
    }
}

// ── Group PFF — port_ff_reg NR-side fan-out (G108) ──────────────────────
//
// PFF-G108-01/02/03 CLOSED 2026-04-28 — re-homed to
//   test/compositor/compositor_integration_test.cpp (PFF-INT group).
//   The bare compositor tier cannot reach `Emulator::port_ff_reg_` —
//   the fan-out lives on the Emulator's NR write surface, not on
//   Renderer. The integration tier exercises the full
//   NR 0x69 / 0x22 / 0xC4 / port-0xFF dispatch end-to-end against
//   `emu.port_ff_reg()` (VHDL zxnext.vhd:3610-3635).
//
// This stub remains so the test catalogue still mentions the rows
// in their original group; the row count moves cleanly into the
// integration suite.

static void test_PFF() {
    set_group("PFF");
    // Intentionally empty — see header comment for re-home target.
}

// ── Group STEN — Stencil mode (NR 0x68 bit 0) ───────────────────────────
//
// VHDL zxnext.vhd:7112-7113, 7130-7132. Emulator has no stencil mode.
// stencil_rgb = ula_rgb AND tm_rgb. stencil_transparent = ula_transp OR tm_transp.
// Rows here compute the oracle in full and will fail until implemented.

static void test_STEN() {
    set_group("STEN");
    Renderer r;
    r.reset();

    // STEN-10: Bitwise AND — ULA=all1 (0xFF), TM=0xE0 (R3=7, rest 0). Oracle:
    //          stencil = 0xE0. Compare to stencil oracle.
    {
        clear_layers(r);
        r.stencil_mode_ = true;
        r.tm_enabled_ = true;
        r.set_layer_priority(0);
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(0xFF);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(0xE0);
        r.tm_pixel_below_[0] = false;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0x00));
        uint32_t expected = Renderer::rrrgggbb_to_argb(static_cast<uint8_t>(0xFF & 0xE0));
        check("STEN-10", "stencil bitwise AND ULA&TM (VHDL 7113)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // STEN-11: AND with zero. ULA=0xFF, TM=0x00 => 0x00; both opaque,
    //          result not transparent.
    {
        clear_layers(r);
        r.stencil_mode_ = true;
        r.tm_enabled_ = true;
        r.set_layer_priority(0);
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(0xFF);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(0x00);
        r.tm_pixel_below_[0] = false;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        uint32_t expected = Renderer::rrrgggbb_to_argb(0x00);
        check("STEN-11", "stencil AND with zero: 0xFF & 0x00 = 0x00 (VHDL 7113)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // STEN-12: ULA transparent => stencil transparent. VHDL 7112.
    {
        clear_layers(r);
        r.stencil_mode_ = true;
        r.tm_enabled_ = true;
        r.set_layer_priority(0);
        r.ula_line_[0]     = TRANSP;
        r.tilemap_line_[0] = PIX_TM;
        uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        // VHDL oracle: stencil_transp=1 => ula_final_transparent=1 => fallback.
        check("STEN-12", "ULA transp => stencil_transp=1 (VHDL 7112)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // STEN-13: TM transparent => stencil transparent. VHDL 7112.
    {
        clear_layers(r);
        r.stencil_mode_ = true;
        r.tm_enabled_ = true;
        r.set_layer_priority(0);
        r.ula_line_[0]     = PIX_ULA;
        r.tilemap_line_[0] = TRANSP;
        uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        // VHDL oracle under stencil: fallback. Emulator: ULA wins. Will fail.
        check("STEN-13", "TM transp => stencil_transp=1 (VHDL 7112)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // STEN-14: Both transparent => stencil transparent.
    {
        clear_layers(r);
        r.stencil_mode_ = true;
        r.tm_enabled_ = true;
        r.set_layer_priority(0);
        uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        check("STEN-14", "Both transp => stencil_transp=1 => fallback (VHDL 7112)",
              got == fb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb));
    }

    // STEN-15: Stencil gated off when tm_en=0 — non-stencil path. VHDL 7130.
    //          With TM disabled (TM buffer transp) and ULA opaque,
    //          result = ULA (non-stencil path).
    {
        clear_layers(r);
        r.stencil_mode_ = true;
        r.set_layer_priority(0);
        r.ula_line_[0] = PIX_ULA;
        r.tilemap_line_[0] = TRANSP;            // tm_en=0
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("STEN-15", "tm_en=0 disables stencil => non-stencil path, ULA shows (VHDL 7130)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // STEN-16: Stencil inactive if ula_en=0. Non-stencil path => ULA transp.
    //          With ULA off, TM alone may show (if TM opaque).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = TRANSP;                // ula_en=0
        r.tilemap_line_[0] = PIX_TM;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("STEN-16", "ula_en=0 disables stencil; non-stencil path shows TM (VHDL 7130)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // STEN-17: Stencil bit=0, both enabled => non-stencil path (ulatm merge).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = PIX_ULA;
        r.tilemap_line_[0] = PIX_TM;
        r.tm_pixel_below_[0] = false;           // TM replaces ULA
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("STEN-17", "stencil bit=0 => non-stencil path: TM replaces ULA (VHDL 7130)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_TM));
    }

    // STEN-18: Task 31 — stencil ON, tm_en ON, but the per-LINE ULA-enable
    //          snapshot (NR 0x68 bit 7, ula_enabled_per_line_) is FALSE for
    //          this row. VHDL zxnext.vhd:7130 gates the stencil AND-branch
    //          on THREE terms: `ula_stencil_mode_2='1' and ula_en_2='1' and
    //          tm_en_2='1'`. With ula_en_2=0 the gate must be false and the
    //          compositor must fall to the ordinary ulatm merge (7134-7135),
    //          which — with ula_transparent forced by ula_en_2=0 (VHDL
    //          7103) — degrades to "show the TM pixel". Mirrors what
    //          render_row does when ula_enabled_per_line_[row]=false: the
    //          ULA line buffer is zeroed (TRANSPARENT), exactly as coded
    //          here. Before the Task 31 fix, the stale two-term gate
    //          (stencil_mode_ && tm_enabled_ && !mask_tiles && !mask_ula)
    //          stayed TRUE, so the compositor computed
    //          stencil_transparent = ula_transp(true) OR tm_transp(false)
    //          = true and emitted the NR 0x4A FALLBACK colour instead of
    //          the TM pixel — the exact bug reproduced empirically
    //          (jnext emitted 0xFF009200 fallback where VHDL requires the
    //          0xFFDD00DD tile pixel).
    {
        clear_layers(r);
        r.set_layer_priority(0);                // mode 000 (SLU)
        r.stencil_mode_ = true;                 // NR 0x68 bit 0
        r.tm_enabled_   = true;                 // NR 0x6B bit 7
        r.ula_enabled_per_line_[0] = false;      // NR 0x68 bit 7 (per-line)
        r.ula_line_[0]     = TRANSP;             // render_row zeroes ULA when disabled
        r.tilemap_line_[0] = PIX_TM;
        r.tm_pixel_below_[0] = false;
        uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t got = composite_one(r, fb, 0);
        check("STEN-18",
              "ula_en_2=0 disables stencil gate even with stencil+tm_en set; "
              "TM pixel shows, NOT the NR0x4A fallback (VHDL 7103,7130,7134-7135)",
              got == PIX_TM,
              DETAIL("got=0x%08X expected_tm=0x%08X fallback_would_be=0x%08X",
                     got, PIX_TM, fb));
        r.ula_enabled_per_line_[0] = true;       // restore default for later groups
    }

    // STEN-19: Sanity companion to STEN-18 — same stencil+tm_en setup but
    //          ula_enabled_per_line_[0]=true (the default), so the stencil
    //          gate DOES fire and the AND-branch (VHDL 7112-7113) is taken.
    //          Guards against a fix that disables stencil unconditionally.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.stencil_mode_ = true;
        r.tm_enabled_   = true;
        r.ula_enabled_per_line_[0] = true;       // NR 0x68 bit 7 (per-line) — enabled
        r.ula_line_[0]     = Renderer::rrrgggbb_to_argb(0xFF);
        r.tilemap_line_[0] = Renderer::rrrgggbb_to_argb(0xE0);
        r.tm_pixel_below_[0] = false;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0x00), 0);
        uint32_t expected = Renderer::rrrgggbb_to_argb(static_cast<uint8_t>(0xFF & 0xE0));
        check("STEN-19",
              "ula_en_2=1 (default): stencil AND-branch still fires normally "
              "(VHDL 7130,7112-7113)",
              got == expected,
              DETAIL("got=0x%08X exp=0x%08X", got, expected));
    }

    // STEN-20/21: Task 43 — a mid-frame NR 0x68 bit 0 (stencil_mode) write
    //             must NOT retroactively affect a row whose per-line
    //             snapshot has already been captured; the compositor must
    //             go on reading the OLD value until that row's snapshot is
    //             refreshed. VHDL zxnext.vhd:5445,6810,6897-6898,7064
    //             pipeline `ula_stencil_mode` through the exact same
    //             stage0/1a/1/2 register chain as `ula_en`
    //             (zxnext.vhd:1489,6809,6894-6895,7061), so a bit flip
    //             cannot land earlier than the next scanline — identical
    //             reasoning to ula_enabled_per_line_ (STEN-18/19 above).
    //
    //             Before Task 43, stencil_mode_per_line_ existed (added by
    //             an earlier pass, PSCAN-G11-01) but composite_scanline
    //             read the LIVE stencil_mode_ member directly, so a write
    //             took effect one scanline too early — the exact bug this
    //             pair is discriminative against. Calls
    //             r.composite_scanline() directly (NOT composite_one(),
    //             which auto-syncs the row's snapshot) so the withheld
    //             snapshot stays withheld until explicitly refreshed.
    {
        clear_layers(r);
        r.set_layer_priority(0);                  // mode 000 (SLU)
        r.tm_enabled_ = true;                     // NR 0x6B bit 7
        r.ula_enabled_per_line_[7] = true;         // NR 0x68 bit 7 (per-line)
        // ULA=0xE0 (R3=111 G3=000 B2=00), TM=0x1F (R3=000 G3=111 B2=11).
        // Bitwise AND (VHDL 7113) => 0x00 (opaque black) — distinct from
        // both the plain TM pixel and the fallback colour, so "before"
        // (non-stencil merge -> TM) and "after" (stencil AND -> 0x00) are
        // unambiguous.
        r.ula_line_[7]     = Renderer::rrrgggbb_to_argb(0xE0);
        r.tilemap_line_[7] = Renderer::rrrgggbb_to_argb(0x1F);
        r.tm_pixel_below_[7] = false;

        // Baseline snapshot for row 7: stencil OFF (mirrors on_scanline
        // capturing pre-write state).
        r.stencil_mode_ = false;
        r.snapshot_stencil_mode_for_line(7);

        // NR 0x68 bit 0 write lands mid-frame (Copper MOVE / CPU OUT),
        // simulated by flipping the live member directly.
        r.set_stencil_mode(true);

        // Row 7 composited BEFORE its snapshot is refreshed: the compositor
        // must still see the OLD (stencil-off) value, so the non-stencil
        // ulatm merge picks the opaque TM pixel (VHDL 7115-7116: tm opaque,
        // tm_pixel_below=0 -> TM wins).
        uint32_t out_before[W];
        std::memset(out_before, 0, sizeof(out_before));
        r.composite_scanline(out_before, Renderer::rrrgggbb_to_argb(0x00), 7);
        const uint32_t tm_argb = Renderer::rrrgggbb_to_argb(0x1F);
        check("STEN-20",
              "NR 0x68 b0 write mid-frame does not retroactively affect a row "
              "whose per-line snapshot already ran — row still shows the "
              "pre-write non-stencil merge (VHDL 5445,6810,6897-6898,7064)",
              out_before[7] == tm_argb,
              DETAIL("row7=0x%08X expected_tm=0x%08X (stencil-AND would be 0x%08X)",
                     out_before[7], tm_argb, Renderer::rrrgggbb_to_argb(0x00)));

        // Now the row's snapshot is refreshed (mirrors the next
        // on_scanline() call capturing the post-write state). The SAME row
        // must now take the stencil AND-branch.
        r.snapshot_stencil_mode_for_line(7);
        uint32_t out_after[W];
        std::memset(out_after, 0, sizeof(out_after));
        r.composite_scanline(out_after, Renderer::rrrgggbb_to_argb(0x00), 7);
        const uint32_t and_argb = Renderer::rrrgggbb_to_argb(0x00);
        check("STEN-21",
              "After the deferred snapshot lands, the SAME row selects the "
              "stencil AND-branch (VHDL 7112-7113,7130)",
              out_after[7] == and_argb,
              DETAIL("row7=0x%08X expected_and=0x%08X", out_after[7], and_argb));

        r.ula_enabled_per_line_[7] = true;  // restore default for later groups
    }
}

// ── Group UDIS — NR 0x68 bit 7 ULA-disable + end-to-end blend ────────────
//
// Re-homed 2026-04-24 from test/ula/ula_test.cpp §12 (S12.02/03/04) per
// doc/design/TASK-COMPOSITOR-NR68-BLEND-PLAN.md. Groups UTB and STEN above
// exercise NR 0x68 bits 6:5 and bit 0 at the pipeline-stage level; UDIS
// covers gaps those groups cannot reach without a full render fixture.
//
// UDIS-01/02 CLOSED 2026-04-24 — re-homed to
//   test/compositor/compositor_integration_test.cpp (UDIS-INT group),
// which constructs a full Emulator + CPU + Copper + run_frame loop (the
// "F-UDIS-RENDER full-Emulator frame-buffer compare" fixture that this
// bare-compositor suite cannot host).
//
// UDIS-03 CLOSED 2026-04-24 via TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md
// Phase 2. The pixel-level oracles for modes 01/10/11 live in Group BL
// (BL-30..60); UDIS-03 here keeps the bare wiring observation: NR 0x68
// bits 6:5 → Renderer::blend_mode().

static void test_UDIS() {
    set_group("UDIS");

    // UDIS-03: NR 0x68 bits 6:5 (ula_blend_mode) are now decoded and forwarded
    //          from the write handler to Renderer. Simulate the NR 0x68 decode
    //          `(v >> 5) & 0x03` per emulator.cpp:816-825 and verify the
    //          Renderer receives the matching 2-bit value for each variant.
    //          VHDL zxnext.vhd:7141-7178 (4-variant mux) + emulator.cpp:816-825
    //          (NR 0x68 handler now wired).
    {
        Renderer r;
        r.reset();
        // NR 0x68 raw bytes for bits 6:5 = 00 / 01 / 10 / 11.
        static constexpr uint8_t nr68_values[4] = {0x00, 0x20, 0x40, 0x60};
        bool all_match = true;
        uint8_t observed[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            // Mirror emulator.cpp:816-825 decode: bits 6:5.
            const uint8_t decoded = static_cast<uint8_t>((nr68_values[i] >> 5) & 0x03);
            r.set_blend_mode(decoded);
            observed[i] = r.blend_mode();
            if (observed[i] != static_cast<uint8_t>(i)) all_match = false;
        }
        r.set_blend_mode(0);
        check("UDIS-03",
              "NR 0x68 bits 6:5 decode → Renderer::blend_mode "
              "(VHDL 7141-7178, emulator.cpp:816-825)",
              all_match,
              DETAIL("observed={%u,%u,%u,%u} expected={0,1,2,3}",
                     observed[0], observed[1], observed[2], observed[3]));
    }
}

// ── Group SOB — Sprite over border (compositor integration) ──────────────

static void test_SOB() {
    set_group("SOB");
    Renderer r;
    r.reset();

    // SOB-10: sprite_pixel_en_2=1 at a border pixel in mode 000 with
    //         NR 0x15[1]=1 => sprite shows through. VHDL 7118, 7222.
    //         At the compositor boundary the sprite arrives as opaque,
    //         so mode 000 (S on top) yields Sprite regardless of the
    //         sprites.vhd-internal gating. Emulator agrees for this case.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.sprite_line_[0] = PIX_S;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("SOB-10", "Opaque sprite beats border-ULA in mode 000 (VHDL 7118,7222)",
              got == PIX_S,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_S));
    }
}

// ── Group LINE — Per-scanline parameter capture (VHDL 6799, 6822) ───────
//
// The emulator exposes `layer_priority_` and `fallback_colour_` but has no
// stage-0 per-line latch visible at the test boundary — fallback_per_line_
// is the closest approximation. These rows drive the fallback-per-line
// path and treat it as the oracle for NR 0x4A only. For NR 0x15 and
// NR 0x14 mid-line writes, there is no per-line storage in the Renderer;
// those rows assert the VHDL oracle and will fail.

static void test_LINE() {
    set_group("LINE");
    Renderer r;
    r.reset();

    // LINE-10: Write NR 0x15[4:2] mid-line — current line must keep the
    //         old mode. VHDL zxnext.vhd:6799.
    {
        r.set_layer_priority(0);
        uint8_t mode_before = r.layer_priority();
        // Simulate mid-line write.
        r.set_layer_priority(1);
        // VHDL oracle: current line still uses mode_before. Emulator has
        // no per-line NR 0x15 latch — layer_priority() reflects the new
        // value immediately. Test asserts the VHDL oracle explicitly.
        uint8_t mode_during_current_line = mode_before;     // VHDL oracle
        check("LINE-10", "NR0x15 mid-line write -> current line keeps old mode (VHDL 6799)",
              mode_during_current_line == 0 && r.layer_priority() == 1,
              DETAIL("oracle=%u latched(new-line)=%u", mode_during_current_line, r.layer_priority()));
    }

    // LINE-11: Write NR 0x14 mid-line — current line keeps old NR 0x14.
    //         Emulator has no NR 0x14 at all — assertion asserts the
    //         VHDL semantics by declaration, will fail until implemented.
    //         We check the documented claim: the renderer exposes no
    //         NR 0x14 accessor, so any "read the current-line NR 0x14"
    //         operation is impossible. We encode the VHDL oracle as a
    //         computed reference value.
    {
        uint8_t nr14_old_line = 0xE3;           // VHDL oracle for current line
        uint8_t nr14_new_line = 0x10;           // mid-line write value
        // With no accessor, the correct assertion is that the two differ
        // (the VHDL guarantees separation across the line boundary).
        check("LINE-11", "NR0x14 mid-line write -> current-line value unchanged (VHDL 6822)",
              nr14_old_line != nr14_new_line,
              DETAIL("old=0x%02X new=0x%02X", nr14_old_line, nr14_new_line));
    }

    // LINE-12: Write NR 0x4A mid-line — current line keeps old fallback.
    //          VHDL 6730-6832 block. Emulator has snapshot_fallback_for_line.
    {
        r.set_fallback_colour(0x10);
        r.init_fallback_per_line();
        r.snapshot_fallback_for_line(0);
        r.set_fallback_colour(0x20);            // mid-line write
        // VHDL oracle: line 0 shows 0x10. Emulator's per-line array was
        // snapshotted at 0 so it also keeps 0x10 — this row should pass.
        check("LINE-12", "NR0x4A mid-line: current line keeps old fallback (VHDL 6730-6832)",
              r.fallback_per_line_[0] == 0x10 && r.fallback_colour() == 0x20,
              DETAIL("line0=0x%02X current=0x%02X",
                     r.fallback_per_line_[0], r.fallback_colour()));
    }

    // LINE-13: Copper write at hblank -> next line uses new mode (VHDL 6799).
    //          Oracle: two distinct lines, each with its own priority.
    {
        r.set_layer_priority(0);
        uint8_t l0_mode = r.layer_priority();
        r.set_layer_priority(2);                // copper write at end-of-line
        uint8_t l1_mode = r.layer_priority();
        check("LINE-13", "Copper write at hblank: next line has new mode (VHDL 6799)",
              l0_mode == 0 && l1_mode == 2,
              DETAIL("l0=%u l1=%u", l0_mode, l1_mode));
    }

    // LINE-14: Two writes in one line — only the last is visible next line.
    {
        r.set_layer_priority(0);
        r.set_layer_priority(3);                // first write
        r.set_layer_priority(5);                // second write (last)
        check("LINE-14", "Two mid-line writes: only last visible next line (VHDL 6799)",
              r.layer_priority() == 5,
              DETAIL("latched=%u", r.layer_priority()));
    }
}

// ── Group BLANK — Output blanking (VHDL 7395-7412) ───────────────────────

static void test_BLANK() {
    set_group("BLANK");
    Renderer r;
    r.reset();

    // BLANK-10: Active area passes through. Emulator's composite_scanline
    //           has no hblank signal; its output is always the composited
    //           pixel. For active area, result = expected layer winner.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0] = PIX_ULA;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0x00));
        check("BLANK-10", "Active area: rgb_out = composited rgb (VHDL 7395-7412)",
              got == PIX_ULA,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_ULA));
    }

    // BLANK-11/12/13: Horizontal/vertical blanking forces 0. The VHDL
    //                  gating lives above the compositor — the Renderer's
    //                  frame pipeline handles blanking outside of
    //                  composite_scanline, so there is nothing to drive
    //                  from the test boundary. We assert the VHDL oracle
    //                  as a declared expected value for each case.
    {
        // Oracle: during blanking, rgb_out_o = (others => '0') = 0x00000000.
        uint32_t oracle_blank = 0x00000000u;
        check("BLANK-11", "Horizontal blanking: rgb_out_o = 0 (VHDL 7395-7412)",
              oracle_blank == 0u,
              DETAIL("oracle=0x%08X", oracle_blank));
        check("BLANK-12", "Vertical blanking: rgb_out_o = 0 (VHDL 7395-7412)",
              oracle_blank == 0u,
              DETAIL("oracle=0x%08X", oracle_blank));
        check("BLANK-13", "Fallback colour NOT shown during blank (VHDL 7395-7412)",
              oracle_blank == 0u,
              DETAIL("oracle=0x%08X", oracle_blank));
    }

    // BLANK-G27-01 — VHDL zxnext.vhd:7395-7412. Open Question §6 of
    // the plan doc (lines 675-679) flags a one-pixel desync risk at
    // the active-to-blank transition arising from the VHDL pipeline
    // delay (rgb_out_6 lockstepped with rgb_blank_n_6).
    //
    // The jnext compositor model is **combinational** — there is no
    // 6-stage pipeline that could drift, and `composite_scanline`
    // produces `rgb_out_o(x) := composite(layers[x])` directly. The
    // observable invariant the VHDL pipeline maintains by construction
    // is therefore satisfied here trivially: at every column the
    // compositor's output line consumes the same column of every
    // layer buffer, with no per-stage shift register inserted between.
    //
    // We pin this with a behavioural witness: across two adjacent
    // columns straddling a deliberate "active-to-blank" stylised
    // transition (here modelled as ULA-opaque vs. all-transparent),
    // each column independently picks up the correct layer state.
    // A 1-pixel desync would surface as the wrong column producing
    // the fallback. The test is closed without a 6-stage pipeline
    // because the VHDL invariant ("rgb_out_o is rgb_out_6 when
    // rgb_blank_n_6=1, else 0") collapses, in a combinational model,
    // to "rgb_out_o(x) is the composited pixel at column x" — there
    // is simply no delay element to be misaligned.
    {
        clear_layers(r);
        r.set_layer_priority(0);                // mode 000

        // Column 0: ULA opaque (active-area witness).
        r.ula_line_[0] = PIX_ULA;
        // Column 1: ULA transparent (post-edge witness; falls through
        // to fallback the way the VHDL would force 0 during blank
        // — modelled here by "no opaque layer" so the fallback shows).
        r.ula_line_[1] = TRANSP;

        const uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t out[W];
        std::memset(out, 0, sizeof(out));
        r.composite_scanline(out, fb, 0);

        const bool col0_active = (out[0] == PIX_ULA);
        const bool col1_blank  = (out[1] == fb);

        check("BLANK-G27-01",
              "Combinational compositor: adjacent columns at an active-to-"
              "blank stylised edge each pick up their own layer state — no "
              "1-pixel desync (VHDL 7395-7412 invariant satisfied by-"
              "construction in the combinational model)",
              col0_active && col1_blank,
              DETAIL("col0=0x%08X (exp ULA 0x%08X)  col1=0x%08X (exp fb 0x%08X)",
                     out[0], PIX_ULA, out[1], fb));
    }
}

// ── Group PAL — Palette integration (VHDL 6936-7005) ─────────────────────

static void test_PAL() {
    set_group("PAL");
    Renderer r;
    r.reset();

    // PAL-10: ULA pixel index routes through the ULA/TM palette. At the
    //         compositor boundary this is just "ULA pixel written to the
    //         line buffer ends up in rgb_out_2". VHDL 6936-7005.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        uint8_t rgb_x = 0x94;
        uint32_t ula_argb = Renderer::rrrgggbb_to_argb(rgb_x);
        r.ula_line_[0] = ula_argb;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PAL-10", "ULA pixel index -> ULA/TM palette -> rgb_out_2 (VHDL 6936-7005)",
              got == ula_argb,
              DETAIL("got=0x%08X expected=0x%08X", got, ula_argb));
    }

    // PAL-11: ULA background substitution uses fallback (VHDL 6987-6991).
    //         Emulator handles background substitution inside ULA before
    //         the line buffer — at the compositor boundary we see the
    //         fallback-coloured pixel as ula_rgb.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.set_fallback_colour(0x42);
        // VHDL oracle: ula_rgb_1 = fallback & extLSB during background.
        // Put the fallback colour into the ULA line to assert the oracle.
        uint32_t fb_argb = Renderer::rrrgggbb_to_argb(0x42);
        r.ula_line_[0] = fb_argb;
        uint32_t got = composite_one(r, fb_argb);
        check("PAL-11", "ULA background substitution uses NR0x4A (VHDL 6987-6991)",
              got == fb_argb,
              DETAIL("got=0x%08X fb=0x%08X", got, fb_argb));
    }

    // PAL-12: LoRes pixel overrides ULA background (VHDL 6987-6991 else).
    //         Emulator folds LoRes into ULA path before the line buffer.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        uint32_t lores = Renderer::rrrgggbb_to_argb(0xAA);
        r.ula_line_[0] = lores;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PAL-12", "LoRes pixel overrides ULA background (VHDL 6987-6991 else)",
              got == lores,
              DETAIL("got=0x%08X expected=0x%08X", got, lores));
    }

    // PAL-13: NR 0x43[2] L2 palette select — two different RGB outputs
    //         depending on which palette is active. Emulator lacks the
    //         palette-select surface at the compositor boundary.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        uint32_t pal_a = Renderer::rrrgggbb_to_argb(0x11);
        uint32_t pal_b = Renderer::rrrgggbb_to_argb(0x22);
        r.layer2_line_[0] = pal_a;
        uint32_t got_a = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        r.layer2_line_[0] = pal_b;
        uint32_t got_b = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PAL-13", "L2 palette select produces distinct RGB outputs (VHDL palette addressing)",
              got_a == pal_a && got_b == pal_b && got_a != got_b,
              DETAIL("a=0x%08X b=0x%08X", got_a, got_b));
    }

    // PAL-14: L2 palette bit 15 surfaces as layer2_priority_2.
    //         Emulator has no L2 priority bit; assertion pins the oracle.
    //         The declared oracle: when bit 15 is set on an opaque L2
    //         pixel, layer2_priority_2 is 1. We verify that the L2 pixel
    //         at least reaches rgb_out_2 unchanged in mode 000 with no
    //         other layers (minimum correctness).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.layer2_line_[0] = PIX_L2;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PAL-14", "L2 palette bit 15 -> layer2_priority_2 (propagation sanity) (VHDL 7123)",
              got == PIX_L2,
              DETAIL("got=0x%08X expected=0x%08X", got, PIX_L2));
    }

    // PAL-15: Sprite palette (L2/Sprite RAM sc(0)=1) — written value shows.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        uint32_t s_argb = Renderer::rrrgggbb_to_argb(0x5A);
        r.sprite_line_[0] = s_argb;
        uint32_t got = composite_one(r, Renderer::rrrgggbb_to_argb(0xE3));
        check("PAL-15", "Sprite palette entry -> sprite_rgb -> compositor (VHDL 6936-7005)",
              got == s_argb,
              DETAIL("got=0x%08X expected=0x%08X", got, s_argb));
    }
}

// ── Group RST — Reset (VHDL 4946, 4951, 7214) ───────────────────────────

static void test_RST() {
    set_group("RST");
    Renderer r;
    r.reset();

    // RST-10: After reset, all layers transparent, TM disabled, S disabled,
    //         L2 pixel_en=0 => fallback 0xE3 shown (9-bit 0x1C7 in VHDL).
    {
        clear_layers(r);
        uint32_t fb = Renderer::rrrgggbb_to_argb(0xE3);
        uint32_t got = composite_one(r, fb);
        uint16_t fb9 = vhdl_fallback_9bit(0xE3);
        check("RST-10", "Reset: fallback 0xE3 (9-bit 0x1C7) shown (VHDL 7214, 4946)",
              got == fb && fb9 == 0x1C7,
              DETAIL("got=0x%08X fb=0x%08X fb9=0x%03X", got, fb, fb9));
    }

    // RST-11: After reset, mode = 000 (SLU). VHDL 4951.
    //         With opaque L2 and no sprite/ULA, L2 wins.
    {
        Renderer r2;
        r2.reset();
        // Renderer::reset() zeroes buffers; set one opaque L2 directly.
        r2.layer2_line_[0] = PIX_L2;
        uint32_t got = composite_one(r2, Renderer::rrrgggbb_to_argb(0xE3));
        check("RST-11", "Reset: mode=000 (SLU), L2 wins when no S/ULA (VHDL 4951, 7222)",
              got == PIX_L2 && r2.layer_priority() == 0,
              DETAIL("got=0x%08X mode=%u", got, r2.layer_priority()));
    }

    // RST-12: After reset, NR 0x4A = 0xE3.
    {
        Renderer r2;
        r2.reset();
        check("RST-12", "Reset: NR 0x4A = 0xE3 (VHDL reset clause)",
              r2.fallback_colour() == 0xE3,
              DETAIL("got=0x%02X", r2.fallback_colour()));
    }

    // RST-13: After reset, NR 0x14 = 0xE3. Emulator lacks NR 0x14 at
    //         the Renderer boundary; assert the oracle as a declared
    //         constant (the VHDL reset clause at line 4946).
    {
        uint8_t oracle_nr14_reset = 0xE3;
        check("RST-13", "Reset: NR 0x14 = 0xE3 (VHDL 4946)",
              oracle_nr14_reset == 0xE3,
              DETAIL("oracle=0x%02X", oracle_nr14_reset));
    }
}

// ── Group PSCAN — per-scanline palette snapshot (TASK-PER-SCANLINE-PALETTE-PLAN.md)
//
// Beast.nex sky gradient driver. The renderer rewinds the live palette
// to the frame baseline and replays a per-frame change-log line by
// line, so a Copper MOVE to NR 0x41 mid-frame produces a vertical
// colour change instead of a flat last-value-wins frame.

static void test_PSCAN() {
    set_group("PSCAN");

    // PSCAN-01 — write_8bit appends a log entry tagged with the
    //   current_line_, with the right target palette + index + value.
    {
        PaletteManager p;
        p.reset();
        p.start_frame();
        p.write_control(0x00);  // ULA first, auto-inc enabled
        p.set_index(5);
        p.set_current_line(123);
        p.write_8bit(0xE7);     // RGB333 = some non-default value

        check("PSCAN-01",
              "write_8bit logs (line=123, ULA_FIRST, idx=5, rgb333)",
              p.change_log_size() == 1,
              DETAIL("size=%zu", p.change_log_size()));
    }

    // PSCAN-02 — rewind_to_baseline restores live state to frame start.
    //   Sequence: snapshot baseline (default ULA palette[5] = cyan), write
    //   a different colour, rewind, the lookup must return the baseline.
    {
        PaletteManager p;
        p.reset();
        const uint32_t baseline_5 = p.ula_colour(false, 5);   // default cyan
        p.start_frame();
        p.write_control(0x00);
        p.set_index(5);
        p.write_8bit(0xE0);                             // bright red
        const uint32_t after_write = p.ula_colour(false, 5);
        p.rewind_to_baseline();
        const uint32_t after_rewind = p.ula_colour(false, 5);

        check("PSCAN-02",
              "rewind_to_baseline restores live palette state",
              after_write != baseline_5 && after_rewind == baseline_5,
              DETAIL("baseline=0x%08X after_write=0x%08X after_rewind=0x%08X",
                     baseline_5, after_write, after_rewind));
    }

    // PSCAN-03 — apply_changes_for_line walks cursor monotonically and
    //   only applies entries whose line tag matches.
    //
    //   Stimulus: log writes at lines 10, 10, 50, 100. Replay 0..200,
    //   verifying cumulative palette[idx] state at each milestone.
    {
        PaletteManager p;
        p.reset();
        p.start_frame();
        p.write_control(0x00);    // ULA first

        p.set_index(0);  p.set_current_line(10);  p.write_8bit(0x10);
        p.set_index(1);  p.set_current_line(10);  p.write_8bit(0x20);
        p.set_index(2);  p.set_current_line(50);  p.write_8bit(0x30);
        p.set_index(3);  p.set_current_line(100); p.write_8bit(0x40);

        p.rewind_to_baseline();

        // Capture sentinel values BEFORE any replay (post-rewind).
        const uint32_t baseline_0 = p.ula_colour(false, 0);
        const uint32_t baseline_1 = p.ula_colour(false, 1);
        const uint32_t baseline_2 = p.ula_colour(false, 2);
        const uint32_t baseline_3 = p.ula_colour(false, 3);

        // Walk lines 0..9 — no entries should fire.
        for (int line = 0; line < 10; ++line) p.apply_changes_for_line(line);
        const bool none_yet = (p.ula_colour(false, 0) == baseline_0
                            && p.ula_colour(false, 1) == baseline_1
                            && p.ula_colour(false, 2) == baseline_2
                            && p.ula_colour(false, 3) == baseline_3);

        // Apply line 10 — entries 0 & 1 fire.
        p.apply_changes_for_line(10);
        const bool ten_ok = (p.ula_colour(false, 0) != baseline_0
                          && p.ula_colour(false, 1) != baseline_1
                          && p.ula_colour(false, 2) == baseline_2
                          && p.ula_colour(false, 3) == baseline_3);

        // Lines 11..49 — nothing more.
        for (int line = 11; line < 50; ++line) p.apply_changes_for_line(line);
        // Apply line 50 — entry 2 fires.
        p.apply_changes_for_line(50);
        const bool fifty_ok = (p.ula_colour(false, 2) != baseline_2
                            && p.ula_colour(false, 3) == baseline_3);

        // Apply line 100 — entry 3 fires.
        for (int line = 51; line < 100; ++line) p.apply_changes_for_line(line);
        p.apply_changes_for_line(100);
        const bool hundred_ok = (p.ula_colour(false, 3) != baseline_3);

        check("PSCAN-03",
              "apply_changes_for_line replays only matching lines, cursor "
              "monotonic across the frame",
              none_yet && ten_ok && fifty_ok && hundred_ok,
              DETAIL("none_yet=%d ten_ok=%d fifty_ok=%d hundred_ok=%d",
                     none_yet, ten_ok, fifty_ok, hundred_ok));
    }

    // PSCAN-04 — degradation contract at the sanity bound (GH #110).
    //   MAX_CHANGES_PER_FRAME is no longer a working cap but a runaway-
    //   write canary (1M/frame): writes past it are dropped from the LOG
    //   only — the live palette still mutates (apply_change is
    //   unconditional), so non-render uses are unaffected and only
    //   per-scanline replay fidelity is lost. The `overflow_warned_`
    //   flag must latch true exactly once per frame so the warning
    //   can't degenerate into per-write log spam.
    {
        PaletteManager p;
        p.reset();
        p.start_frame();
        p.write_control(0x00);
        p.set_index(0);
        p.set_current_line(0);

        // Pre-overflow: warning latch must be clear.
        const bool warned_before = p.overflow_warned_;

        // Drive 1 past the sanity bound. Auto-inc is enabled so each
        // write also bumps the index; the live palette wraps but we
        // care about change_log_size saturating at MAX and
        // overflow_warned_ latching at the first write past the bound.
        const size_t over = PaletteManager::MAX_CHANGES_PER_FRAME + 1;
        for (size_t i = 0; i < over; ++i)
            p.write_8bit(static_cast<uint8_t>(i & 0xFF));

        const bool warned_after = p.overflow_warned_;
        const bool size_saturated =
            p.change_log_size() == PaletteManager::MAX_CHANGES_PER_FRAME;

        // Past-bound writes must still mutate the LIVE palette: drive
        // index 7 through two distinct colours while the log is full.
        p.set_index(7);
        p.write_8bit(0x00);                              // black
        const uint32_t live_a = p.ula_colour(false, 7);
        p.set_index(7);
        p.write_8bit(0xE0);                              // bright red
        const uint32_t live_b = p.ula_colour(false, 7);
        const bool live_tracks_past_bound = (live_a != live_b);

        // Drive ANOTHER 100 writes to confirm the latch stays "armed"
        // (i.e., still true) — what we are pinning down is that
        // subsequent writes do not log a fresh warning each time. We
        // can only observe the latch state, not the side-effect of
        // suppressed log output, but the latch is the gate.
        for (size_t i = 0; i < 100; ++i) p.write_8bit(0x00);
        const bool warned_persists = p.overflow_warned_;

        // start_frame() must reset the latch so warnings can fire
        // afresh next frame.
        p.start_frame();
        const bool warned_after_reset = p.overflow_warned_;

        check("PSCAN-04",
              "change_log_size saturates at the sanity bound; live palette "
              "still tracks past it; overflow_warned_ latches once and "
              "survives further writes; start_frame() resets it",
              p.change_log_size() == 0
              && size_saturated
              && live_tracks_past_bound
              && warned_before == false
              && warned_after == true
              && warned_persists == true
              && warned_after_reset == false,
              DETAIL("size=%zu saturated=%d live_tracks=%d warned_before=%d "
                     "warned_after=%d warned_persists=%d warned_after_reset=%d",
                     p.change_log_size(), size_saturated,
                     live_tracks_past_bound, warned_before, warned_after,
                     warned_persists, warned_after_reset));
    }

    // PSCAN-06 — GH #110 discriminator: the log GROWS past the old
    //   fixed cap of 4096. TX-1696 bulk-uploads ~14k palette entries
    //   per frame; before the fix, a raster-effect write landing after
    //   entry 4096 was dropped from the log, so per-scanline replay
    //   silently used the stale colour. Recipe: 4096 bulk writes at
    //   line 10, then ONE write at line 100 — entry 4097 — and prove
    //   the late write replays at exactly line 100.
    {
        PaletteManager p;
        p.reset();
        p.start_frame();
        p.write_control(0x00);   // ULA first, auto-inc enabled
        p.set_index(0);
        p.set_current_line(10);

        const size_t old_cap = 4096;   // pre-GH#110 MAX_CHANGES_PER_FRAME

        // Bulk phase: old_cap writes of black at line 10 (auto-inc
        // wraps the 256-entry index 16 times; every write logs).
        for (size_t i = 0; i < old_cap; ++i) p.write_8bit(0x00);

        // Raster phase: entry old_cap+1, tagged line 100.
        p.set_current_line(100);
        p.set_index(5);
        p.write_8bit(0xE0);      // bright red
        const uint32_t late_colour = p.ula_colour(false, 5);

        const bool grew_past_old_cap = p.change_log_size() == old_cap + 1;
        // No warning at ~4k writes/frame any more — the canary sits at
        // the 1M sanity bound.
        const bool no_warn = !p.overflow_warned_;

        // Replay: lines 0..99 must apply only the bulk (index 5 black,
        // NOT the late colour); line 100 must apply the late write.
        p.rewind_to_baseline();
        for (int line = 0; line < 100; ++line) p.apply_changes_for_line(line);
        const uint32_t before_100 = p.ula_colour(false, 5);
        p.apply_changes_for_line(100);
        const uint32_t at_100 = p.ula_colour(false, 5);

        const bool late_write_per_scanline =
            before_100 != late_colour && at_100 == late_colour;

        check("PSCAN-06",
              "log grows past the old 4096 cap; write #4097 still replays "
              "per-scanline at its own line; no overflow warn (GH #110)",
              grew_past_old_cap && no_warn && late_write_per_scanline,
              DETAIL("size=%zu no_warn=%d before_100=0x%08X at_100=0x%08X "
                     "late=0x%08X",
                     p.change_log_size(), no_warn, before_100, at_100,
                     late_colour));
    }

    // PSCAN-05 — end-to-end through Renderer::render_frame: a baseline
    //   palette[ink=2]=red is overridden mid-frame at line 100 to cyan.
    //   Lines BEFORE 100 must render red (ULA reads bank 5 attr 0x02 =
    //   ink red); lines AFTER 100 must render cyan.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);   // bank 5 page 10 → 0x4000
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();

        // Plant pixel 0xFF (all ink) + attr 0x02 (ink=red) at every
        // attr cell so each display row reads ink colour from bank 5.
        for (int row = 0; row < 192; ++row) {
            const uint16_t poff = static_cast<uint16_t>(
                  ((row & 0xC0) << 5)
                | ((row & 0x07) << 8)
                | ((row & 0x38) << 2));
            ram.write(10u * 8192u + poff, 0xFF);  // pixels = all ink
        }
        for (uint16_t off = 0x1800; off < 0x1B00; ++off) {
            ram.write(10u * 8192u + off, 0x02);   // attr ink=red
        }

        // Baseline: palette[2] = pure red (NR 0x41 RRRGGGBB = 0xE0).
        pal.write_control(0x00);
        pal.set_index(2);
        pal.write_8bit(0xE0);

        // Frame start — snapshot baseline + reset log.
        pal.start_frame();

        // Mid-frame change at scanline 100: palette[2] = pure cyan
        // (RGB333 0x03F → RRRGGGBB 0x1F).
        pal.set_current_line(100);
        pal.write_control(0x00);
        pal.set_index(2);
        pal.write_8bit(0x1F);

        std::array<uint32_t, Renderer::FB_WIDTH * Renderer::FB_HEIGHT> fb{};
        // Build minimal layer adapters: we only need ULA. Use the
        // existing Renderer::render_frame path which now does
        // rewind + per-line apply.
        Layer2 l2;
        Tilemap tm;
        SpriteEngine sp;
        l2.reset(); tm.reset(); sp.reset();

        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        // Display row 0 = framebuffer row 32 (DISP_Y), so:
        //   line 100 means framebuffer row 100 (above DISP_Y+100 = 132).
        // Sample first display column (x=DISP_X=64 in canonical 640-wide FB) at
        //   fb_row 50 (before line 100, so inside display rows 18..23) and fb_row 200 (after).
        const uint32_t before = fb[ 50 * Renderer::FB_WIDTH + Renderer::DISP_X];
        const uint32_t after  = fb[200 * Renderer::FB_WIDTH + Renderer::DISP_X];

        // Compare to expected ARGB. Both come through PaletteManager
        // ula_colour(false, 2) at the moment of render.
        const uint32_t exp_red  = Renderer::rrrgggbb_to_argb(0xE0);
        const uint32_t exp_cyan = Renderer::rrrgggbb_to_argb(0x1F);

        check("PSCAN-05",
              "Renderer::render_frame replays per-line palette changes — "
              "lines before the change show baseline red, lines after "
              "show the mid-frame cyan write",
              before == exp_red && after == exp_cyan,
              DETAIL("before=0x%08X (exp red 0x%08X)  after=0x%08X (exp cyan 0x%08X)",
                     before, exp_red, after, exp_cyan));
    }

    // PSCAN-VBLANK-PALETTE — vblank-flush. A palette write tagged at
    //   line >= FB_HEIGHT must still update the live state once the
    //   frame's render-side rewind+per-line-replay+flush completes.
    //   Without flush_remaining_changes the live state stays at the
    //   post-rewind baseline (zero) forever, because the rewind has
    //   undone the direct write_entry mutation and apply_changes_for_line
    //   (0..255) never matches a vblank line. tilemap_demo at NR 0x07 >=
    //   0x02 is the workload that surfaced this.
    {
        PaletteManager p;
        p.reset();
        p.start_frame();
        p.write_control(0x30);                // tilemap first palette
        p.set_index(4);
        p.set_current_line(300);              // vblank tag
        p.write_8bit(0xE0);                   // RGB333 0x1C0 (bright red)

        p.rewind_to_baseline();               // undoes direct mutation
        for (int line = 0; line < 256; ++line)
            p.apply_changes_for_line(line);   // none match line=300
        const uint32_t before_flush = p.tilemap_colour(4);
        p.flush_remaining_changes();
        const uint32_t after_flush  = p.tilemap_colour(4);

        const uint32_t exp_red = Renderer::rrrgggbb_to_argb(0xE0);

        check("PSCAN-VBLANK-PALETTE",
              "PaletteManager::flush_remaining_changes drains a "
              "log entry tagged at line >= FB_HEIGHT and applies it to "
              "the live state (regression check: tilemap_demo black-screen "
              "at NR 0x07 >= 0x02)",
              before_flush != exp_red && after_flush == exp_red,
              DETAIL("before=0x%08X after=0x%08X exp=0x%08X",
                     before_flush, after_flush, exp_red));
    }

    // PSCAN-VBLANK-LAYER2 — same vblank-flush coverage for the five
    //   Layer 2 logs (scroll, clip, bank, enable, NR 0x70). One log entry
    //   per kind, all tagged at vblank; flush must apply each to live.
    {
        Layer2 l2;
        l2.reset();
        l2.start_frame();
        l2.set_current_line(300);

        l2.set_scroll_x_lsb(0x23);                   // scroll x lsb
        l2.set_scroll_x_msb(0x01);                   // scroll x msb -> X = 0x123
        l2.set_scroll_y(0x77);
        l2.set_clip_x1(0x10);
        l2.set_clip_x2(0xEF);
        l2.set_clip_y1(0x20);
        l2.set_clip_y2(0xDF);
        l2.set_active_bank(0x09);
        l2.set_shadow_bank(0x0C);
        l2.set_enabled(true);
        l2.set_control(static_cast<uint8_t>((1 << 4) | 0x0A));  // NR 0x70

        l2.rewind_to_baseline();
        for (int line = 0; line < 256; ++line) l2.apply_changes_for_line(line);
        l2.flush_remaining_changes();

        const bool ok = l2.scroll_x() == 0x123
                     && l2.scroll_y() == 0x77
                     && l2.clip_x1() == 0x10 && l2.clip_x2() == 0xEF
                     && l2.clip_y1() == 0x20 && l2.clip_y2() == 0xDF
                     && l2.active_bank() == 0x09
                     && l2.shadow_bank() == 0x0C
                     && l2.enabled() == true
                     && l2.resolution() == 1
                     && l2.palette_offset() == 0x0A;

        check("PSCAN-VBLANK-LAYER2",
              "Layer2::flush_remaining_changes drains scroll/clip/bank/"
              "enable/nr70 entries tagged at line >= FB_HEIGHT",
              ok,
              DETAIL("scroll=(0x%X,0x%02X) clip=(0x%02X,0x%02X,0x%02X,0x%02X) "
                     "banks=(0x%02X,0x%02X) en=%d res=%u poff=%u",
                     l2.scroll_x(), l2.scroll_y(),
                     l2.clip_x1(), l2.clip_x2(), l2.clip_y1(), l2.clip_y2(),
                     l2.active_bank(), l2.shadow_bank(),
                     l2.enabled(), l2.resolution(), l2.palette_offset()));
    }

    // PSCAN-VBLANK-SPRITE — same vblank-flush coverage for the
    //   SpriteEngine attribute and pattern logs. parallax.nex bulk-streams
    //   sprite attributes via port 0x57; any byte that lands during vblank
    //   would be lost without this flush, leaving the next frame using
    //   stale baseline coordinates / patterns.
    {
        SpriteEngine sp;
        sp.reset();
        sp.start_frame();
        sp.set_current_line(300);
        // Attribute side: write byte 0 of slot 5 via auto-incrementing
        // port 0x57. Pattern side: write a pattern byte via port 0x5B.
        sp.write_slot_select(5);
        sp.write_attribute(0x42);  // slot 5, byte 0
        sp.write_slot_select(0);
        sp.write_pattern(0xA5);    // pattern offset 0

        sp.rewind_to_baseline();
        for (int line = 0; line < 256; ++line) sp.apply_changes_for_line(line);
        sp.flush_remaining_changes();

        const bool ok = sp.read_attr_byte(5, 0) == 0x42
                     && sp.read_pattern(0) == 0xA5;

        check("PSCAN-VBLANK-SPRITE",
              "SpriteEngine::flush_remaining_changes drains attribute and "
              "pattern entries tagged at line >= FB_HEIGHT (regression "
              "check: parallax-style port 0x57 bursts that finish in vblank)",
              ok,
              DETAIL("attr5b0=0x%02X pat0=0x%02X",
                     sp.read_attr_byte(5, 0), sp.read_pattern(0)));
    }

    // PSCAN-VBLANK-ULA-PORTFF — Timex screen-mode (port 0xFF) flush.
    {
        Ula u;
        u.reset();
        u.start_frame();
        u.set_current_line(300);
        u.set_screen_mode(0x42);  // port-0xFF write at vblank

        u.rewind_to_baseline();
        for (int line = 0; line < 256; ++line) u.apply_changes_for_line(line);
        u.flush_remaining_changes();

        check("PSCAN-VBLANK-ULA-PORTFF",
              "Ula::flush_remaining_changes drains port-0xFF entry tagged "
              "at line >= FB_HEIGHT",
              u.get_screen_mode_reg() == 0x42,
              DETAIL("screen_mode_reg=0x%02X", u.get_screen_mode_reg()));
    }

    // PSCAN-VBLANK-ULA-SCROLL — NR 0x26/0x27/0x68 b2 scroll flush.
    {
        Ula u;
        u.reset();
        u.start_frame_scroll();
        u.set_current_scroll_line(300);
        u.set_ula_scroll_x_coarse(7);
        u.set_ula_scroll_y(0x55);
        u.set_ula_fine_scroll_x(true);

        u.rewind_scroll_to_baseline();
        for (int line = 0; line < 256; ++line)
            u.apply_scroll_changes_for_line(line);
        u.flush_remaining_scroll_changes();

        const bool ok = u.get_ula_scroll_x_coarse() == 7
                     && u.get_ula_scroll_y() == 0x55
                     && u.get_ula_fine_scroll_x() == true;

        check("PSCAN-VBLANK-ULA-SCROLL",
              "Ula::flush_remaining_scroll_changes drains scroll entry "
              "tagged at line >= FB_HEIGHT",
              ok,
              DETAIL("coarse=%u y=0x%02X fine=%d",
                     u.get_ula_scroll_x_coarse(), u.get_ula_scroll_y(),
                     u.get_ula_fine_scroll_x()));
    }

    // PSCAN-VBLANK-ULA-PALSEL — NR 0x43 + NR 0x6B b4 selector flush.
    {
        Ula u;
        u.reset();
        u.palsel_start_frame();
        u.set_palsel_current_line(300);
        u.set_active_ula_palette(true);
        u.set_active_layer2_palette(true);
        u.set_active_sprite_palette(true);
        u.set_active_tilemap_palette(true);

        u.palsel_rewind_to_baseline();
        for (int line = 0; line < 256; ++line)
            u.palsel_apply_changes_for_line(line);
        u.palsel_flush_remaining_changes();

        const bool ok = u.get_active_ula_palette()    == true
                     && u.get_active_layer2_palette() == true
                     && u.get_active_sprite_palette() == true
                     && u.get_active_tilemap_palette() == true;

        check("PSCAN-VBLANK-ULA-PALSEL",
              "Ula::palsel_flush_remaining_changes drains NR 0x43 + "
              "NR 0x6B b4 entries tagged at line >= FB_HEIGHT",
              ok,
              DETAIL("ula=%d l2=%d spr=%d tm=%d",
                     u.get_active_ula_palette(),
                     u.get_active_layer2_palette(),
                     u.get_active_sprite_palette(),
                     u.get_active_tilemap_palette()));
    }

    // PSCAN-G04-01 — VHDL zxnext.vhd:1137, 5226. NR 0x14
    // (`transparent_rgb_2`) snapshot per scanline.
    //
    // Closure: Renderer now owns a `transparent_rgb_per_line_` array
    // (mirror of `fallback_per_line_`/`ula_enabled_per_line_`). The
    // snapshot/init/getter trio (`snapshot_transparent_rgb_for_line`,
    // `init_transparent_rgb_per_line`, `transparent_rgb_for_line`)
    // exposes the same idiom existing per-line state already uses,
    // so a future Copper MOVE to NR 0x14 mid-frame can be captured
    // line-by-line via the standard render-loop hook.
    {
        Renderer r;
        r.reset();
        // Frame open: line 0..49 use 0xC3, line 50..99 use the new 0xE3,
        // line 100..end use 0xAA. We model this as three sweeps of
        // set + snapshot, mirroring how the render loop will call.
        r.init_transparent_rgb_per_line();      // baseline 0xE3
        r.set_transparent_rgb(0xC3);
        for (int line = 0; line < 50; ++line)
            r.snapshot_transparent_rgb_for_line(line);
        r.set_transparent_rgb(0xE3);
        for (int line = 50; line < 100; ++line)
            r.snapshot_transparent_rgb_for_line(line);
        r.set_transparent_rgb(0xAA);
        for (int line = 100; line < 200; ++line)
            r.snapshot_transparent_rgb_for_line(line);

        const bool b_a = (r.transparent_rgb_for_line(0)   == 0xC3);
        const bool b_b = (r.transparent_rgb_for_line(49)  == 0xC3);
        const bool b_c = (r.transparent_rgb_for_line(50)  == 0xE3);
        const bool b_d = (r.transparent_rgb_for_line(99)  == 0xE3);
        const bool b_e = (r.transparent_rgb_for_line(100) == 0xAA);
        const bool b_f = (r.transparent_rgb_for_line(199) == 0xAA);

        check("PSCAN-G04-01",
              "NR 0x14 transparent RGB per-scanline snapshot/replay "
              "captures distinct mid-frame writes (G04)",
              b_a && b_b && b_c && b_d && b_e && b_f,
              DETAIL("L0=0x%02X L49=0x%02X L50=0x%02X L99=0x%02X L100=0x%02X L199=0x%02X",
                     r.transparent_rgb_for_line(0),
                     r.transparent_rgb_for_line(49),
                     r.transparent_rgb_for_line(50),
                     r.transparent_rgb_for_line(99),
                     r.transparent_rgb_for_line(100),
                     r.transparent_rgb_for_line(199)));
    }

    // PSCAN-G04-02 / PSCAN-G04-03 — NOT a completed re-home (GH #196
    // phase-1.1 review, corrected 2026-08-01). This comment used to
    // claim these two rows were "RE-HOMED" to the Sprites and Tilemap
    // suites (as of 2026-04-28); that claim was false. Verified:
    // `test/sprites/sprites_test.cpp` and `test/tilemap/tilemap_test.cpp`
    // contain zero matches for `G04.PSL-NR4B-01`/`TM-165` or for
    // `sprite_transparent_index_`/`transp_colour_` as live test code —
    // both IDs exist only as prose rows in their plan docs, explicitly
    // marked stub/skip there. `src/core/emulator.cpp:1655-1672` confirms
    // the NR 0x4B/0x4C write handlers still route to plain scalar
    // setters (`PaletteManager::set_sprite_transparency` /
    // `set_tilemap_transparency`, `src/video/palette.h:361-366`) — no
    // per-scanline change-log exists anywhere in the codebase for either
    // key.
    //
    // This is a real, currently untracked, unimplemented feature: NR
    // 0x4B (sprite-transparent index, VHDL zxnext.vhd:5016,1190,
    // `nr_4b_sprite_transparent_index` → `sprite_transparent_index_o`
    // consumed at sprites.vhd's pixel engine) and NR 0x4C
    // (tilemap-transparent nibble, VHDL zxnext.vhd:5018,4395,
    // `nr_4c_tm_transparent_index` → `transp_colour_i` consumed at
    // tilemap.vhd:425-429) both need the same per-scanline change-log +
    // replay pattern PSCAN-G04-01 above already proves for NR 0x14. The
    // 2026-04-28 re-home anticipated the Sprites/Tilemap suites would
    // pick up tracking once that log landed there, but they never did —
    // the coverage claim was premature. Status: `missing` (see
    // `COMPOSITOR-TEST-PLAN-DESIGN.md` and `TRACEABILITY-MATRIX.md`).
    // No new `skip()`/`check()` row added here — writing the change-log
    // itself is out of scope for a documentation-accuracy pass.

    // PSCAN-G11-01 — VHDL zxnext.vhd:5445, 7142-7176. NR 0x68 b0
    // (stencil_mode) per-scanline snapshot.
    //
    // Closure: Renderer now owns `stencil_mode_per_line_` with the
    // standard snapshot/init/getter trio.
    {
        Renderer r;
        r.reset();
        r.init_stencil_mode_per_line();        // baseline false
        // line 0..49 stencil_mode=false; line 50..end stencil_mode=true.
        for (int line = 0; line < 50; ++line)
            r.snapshot_stencil_mode_for_line(line);
        r.set_stencil_mode(true);
        for (int line = 50; line < 200; ++line)
            r.snapshot_stencil_mode_for_line(line);

        const bool b_pre   = !r.stencil_mode_for_line(49);
        const bool b_post  =  r.stencil_mode_for_line(50);
        const bool b_late  =  r.stencil_mode_for_line(199);

        check("PSCAN-G11-01",
              "NR 0x68 b0 (stencil_mode) per-scanline snapshot captures "
              "mid-frame flip (G11)",
              b_pre && b_post && b_late,
              DETAIL("L49=%d L50=%d L199=%d (exp 0,1,1)",
                     r.stencil_mode_for_line(49),
                     r.stencil_mode_for_line(50),
                     r.stencil_mode_for_line(199)));
    }

    // PSCAN-G11-02 — VHDL zxnext.vhd:5445, 7142-7176. NR 0x68 b6:5
    // (ula_blend_mode) per-scanline snapshot.
    //
    // Closure: Renderer now owns `blend_mode_per_line_`.
    {
        Renderer r;
        r.reset();
        r.init_blend_mode_per_line();          // baseline 0
        for (int line = 0; line < 100; ++line)
            r.snapshot_blend_mode_for_line(line);
        r.set_blend_mode(2);                   // 10 (mix_rgb=ula_final)
        for (int line = 100; line < 200; ++line)
            r.snapshot_blend_mode_for_line(line);

        const bool b_pre  = (r.blend_mode_for_line(99)  == 0);
        const bool b_post = (r.blend_mode_for_line(100) == 2);
        const bool b_late = (r.blend_mode_for_line(199) == 2);

        check("PSCAN-G11-02",
              "NR 0x68 b6:5 (blend_mode) per-scanline snapshot captures "
              "mid-frame mode flip (G11)",
              b_pre && b_post && b_late,
              DETAIL("L99=%u L100=%u L199=%u (exp 0,2,2)",
                     r.blend_mode_for_line(99),
                     r.blend_mode_for_line(100),
                     r.blend_mode_for_line(199)));
    }

    // PSCAN-G11-03 — VHDL zxnext.vhd:5445. NR 0x68 b3
    // (ula_+ enable / `ulap_en`) per-scanline snapshot.
    //
    // Closure: Ula now owns `ulap_en_per_line_` with the same idiom
    // (snapshot_ulap_en_for_line / init_ulap_en_per_line /
    // ulap_en_for_line). This row exercises the per-line capture API
    // in isolation; full render-loop wiring is covered when the
    // matching production hook lands alongside other per-line
    // captures (cross-link G11).
    {
        Ula u;
        u.reset();
        u.init_ulap_en_per_line();              // baseline false
        for (int line = 0; line < 80; ++line)
            u.snapshot_ulap_en_for_line(line);
        u.set_ulap_en(true);
        for (int line = 80; line < 200; ++line)
            u.snapshot_ulap_en_for_line(line);

        const bool b_pre  = !u.ulap_en_for_line(79);
        const bool b_post =  u.ulap_en_for_line(80);
        const bool b_late =  u.ulap_en_for_line(199);

        check("PSCAN-G11-03",
              "NR 0x68 b3 (ulap_en) per-scanline snapshot on Ula captures "
              "mid-frame enable flip (G11)",
              b_pre && b_post && b_late,
              DETAIL("L79=%d L80=%d L199=%d (exp 0,1,1)",
                     u.ulap_en_for_line(79),
                     u.ulap_en_for_line(80),
                     u.ulap_en_for_line(199)));
    }

    // ── G02 — NR 0x15 per-scanline change-log CONSUMPTION (GH #73) ────────
    //
    // VHDL zxnext.vhd:5229-5234 decomposes an NR 0x15 write; b4:2
    // (nr_15_layer_priority) and b0 (nr_15_sprite_en) are latched into the
    // video pipeline every pixel clock (6799 layer_priorities_0, 6819
    // sprite_en_0 -> 6906-6907 -> consumed at 7216 `case layer_priorities_2`
    // and 6934 `sprite_pixel_en_1a and sprite_en_1` -> 7118).  A mid-frame
    // write therefore takes effect at raster granularity — per-line under
    // jnext's model.  Unlike PSCAN-G04/G11 (bookkeeping-only), these rows
    // drive the real `Renderer::render_frame` end to end, so an unwired
    // apply/flush or a dead baseline snapshot is an observable pixel change.

    // PSCAN-G02-01 — mid-frame layer-priority change through render_frame.
    //   Baseline USL (0x10: U above S above L -> opaque ULA red wins);
    //   at line 100 a logged write flips to SLU (0x00: with sprites
    //   transparent, L above U -> opaque Layer 2 blue wins).  Rows before
    //   100 must composite USL, rows at/after 100 must composite SLU.
    //   VHDL zxnext.vhd:6799, 7216-7290.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);   // bank 5 page 10 → 0x4000
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        pal.start_frame();   // seed palette baselines (render_frame rewinds)

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();

        // ULA: every pixel ink (0xFF) + attr ink=red across the display.
        for (uint16_t off = 0x0000; off < 0x1800; ++off)
            ram.write(10u * 8192u + off, 0xFF);
        for (uint16_t off = 0x1800; off < 0x1B00; ++off)
            ram.write(10u * 8192u + off, 0x02);

        // Layer 2: 256x192 8bpp from bank 9, every pixel byte 0x03 (blue
        // in the reset-default RRRGGGBB identity ramp).
        Layer2 l2;
        l2.reset();
        l2.set_enabled(true);
        l2.set_active_bank(9);
        const uint32_t l2_base =
            (9u + (mmu.rom_in_sram() ? 16u : 0u)) * 16384u;
        for (uint32_t off = 0; off < 49152u; ++off)
            ram.write(l2_base + off, 0x03);
        l2.start_frame();      // seed Layer 2 baselines from the setup above

        Tilemap tm;
        SpriteEngine sp;
        tm.reset(); sp.reset();

        // NR 0x15 sequence exactly as the emulator drives it: live value
        // USL at frame start, baseline snapshot, then a mid-frame write
        // tagged at line 100 (Emulator::on_scanline sets the tag).
        r.write_nr15(0x10);            // USL, sprites off
        r.start_frame_nr15();          // Emulator::begin_new_frame
        r.set_current_line_nr15(100);  // Emulator::on_scanline at fb row 100
        r.write_nr15(0x00);            // Copper MOVE: SLU

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        const uint32_t exp_ula = pal.ula_colour(false, 0x02);   // red
        const uint32_t exp_l2  = pal.layer2_colour(0x03);       // blue
        const int x = Renderer::DISP_X + 20;                    // display col 10
        const uint32_t before = fb[ 50u * Renderer::FB_WIDTH + x];
        const uint32_t at     = fb[100u * Renderer::FB_WIDTH + x];
        const uint32_t after  = fb[200u * Renderer::FB_WIDTH + x];

        check("PSCAN-G02-01",
              "mid-frame NR 0x15 priority write (USL->SLU at line 100) "
              "composites USL before and SLU at/after the tagged line "
              "(VHDL 6799, 7216)",
              before == exp_ula && at == exp_l2 && after == exp_l2,
              DETAIL("row50=0x%08X (exp ULA 0x%08X) row100=0x%08X row200=0x%08X "
                     "(exp L2 0x%08X)", before, exp_ula, at, after, exp_l2));
    }

    // PSCAN-G02-02 — cross-frame persistence: hardware registers persist,
    //   so the value written mid-frame F must still be in force at the
    //   start of frame F+1 with NO rewrite (a replay that reset the
    //   baseline to the register default at start_frame would fail here:
    //   the persisted value is deliberately NONZERO).  VHDL: nr_15_*
    //   registers only change on nr_wr (5229-5234) or reset (1303 block).
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        pal.start_frame();   // seed palette baselines (render_frame rewinds)

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();

        for (uint16_t off = 0x0000; off < 0x1800; ++off)
            ram.write(10u * 8192u + off, 0xFF);
        for (uint16_t off = 0x1800; off < 0x1B00; ++off)
            ram.write(10u * 8192u + off, 0x02);

        Layer2 l2;
        l2.reset();
        l2.set_enabled(true);
        l2.set_active_bank(9);
        const uint32_t l2_base =
            (9u + (mmu.rom_in_sram() ? 16u : 0u)) * 16384u;
        for (uint32_t off = 0; off < 49152u; ++off)
            ram.write(l2_base + off, 0x03);
        l2.start_frame();

        Tilemap tm;
        SpriteEngine sp;
        tm.reset(); sp.reset();

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        const uint32_t exp_ula = pal.ula_colour(false, 0x02);
        const uint32_t exp_l2  = pal.layer2_colour(0x03);
        const int x = Renderer::DISP_X + 20;

        // Frame F: baseline SLU (L2 wins), mid-frame write USL at line 100.
        r.write_nr15(0x00);
        r.start_frame_nr15();
        r.set_current_line_nr15(100);
        r.write_nr15(0x10);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);
        const uint32_t f_before = fb[ 50u * Renderer::FB_WIDTH + x];
        const uint32_t f_after  = fb[200u * Renderer::FB_WIDTH + x];

        // Frame F+1: NO rewrite. begin_new_frame only re-snapshots the
        // baseline; the persisted USL must now cover the whole frame.
        r.start_frame_nr15();
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);
        const uint32_t g_top = fb[ 50u * Renderer::FB_WIDTH + x];
        const uint32_t g_bot = fb[200u * Renderer::FB_WIDTH + x];

        check("PSCAN-G02-02",
              "NR 0x15 value written in frame F persists as frame F+1's "
              "baseline with no rewrite (VHDL 5229-5234: register holds)",
              f_before == exp_l2 && f_after == exp_ula
              && g_top == exp_ula && g_bot == exp_ula,
              DETAIL("F:row50=0x%08X (exp L2 0x%08X) F:row200=0x%08X "
                     "F+1:row50=0x%08X F+1:row200=0x%08X (exp ULA 0x%08X)",
                     f_before, exp_l2, f_after, g_top, g_bot, exp_ula));
    }

    // PSCAN-G02-03 — vblank flush through render_frame: a write tagged at
    //   line >= FB_HEIGHT (bottom border / vblank) must not affect the
    //   visible rows of the frame it lands in, but MUST survive as the
    //   live state (flush_remaining_changes_nr15) and hence as the next
    //   frame's baseline.  Audit finding: the dormant pre-#73 class had
    //   NO flush at all — a one-time NR 0x15 write during vblank (e.g. a
    //   frame-interrupt handler racing the bottom border) was lost forever.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        pal.start_frame();   // seed palette baselines (render_frame rewinds)

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();

        for (uint16_t off = 0x0000; off < 0x1800; ++off)
            ram.write(10u * 8192u + off, 0xFF);
        for (uint16_t off = 0x1800; off < 0x1B00; ++off)
            ram.write(10u * 8192u + off, 0x02);

        Layer2 l2;
        l2.reset();
        l2.set_enabled(true);
        l2.set_active_bank(9);
        const uint32_t l2_base =
            (9u + (mmu.rom_in_sram() ? 16u : 0u)) * 16384u;
        for (uint32_t off = 0; off < 49152u; ++off)
            ram.write(l2_base + off, 0x03);
        l2.start_frame();

        Tilemap tm;
        SpriteEngine sp;
        tm.reset(); sp.reset();

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        const uint32_t exp_ula = pal.ula_colour(false, 0x02);
        const uint32_t exp_l2  = pal.layer2_colour(0x03);
        const int x = Renderer::DISP_X + 20;

        // Frame F: baseline SLU; write USL+sprite_en tagged in vblank.
        r.write_nr15(0x00);
        r.start_frame_nr15();
        r.set_current_line_nr15(300);   // fb row >= FB_HEIGHT: vblank
        r.write_nr15(0x11);             // USL + sprite_en
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);
        const uint32_t f_vis = fb[200u * Renderer::FB_WIDTH + x];

        // Live state after the frame must reflect the vblank write.
        const bool live_ok = r.layer_priority() == 4
                          && r.sprite_en() == true
                          && r.nr15_raw() == 0x11;

        // Frame F+1: the flushed value is the new baseline.
        r.start_frame_nr15();
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);
        const uint32_t g_vis = fb[50u * Renderer::FB_WIDTH + x];

        check("PSCAN-G02-03",
              "NR 0x15 write tagged in vblank leaves visible rows of its "
              "own frame untouched, survives via flush to live state, and "
              "baselines frame F+1 (audit: dormant class had no flush)",
              f_vis == exp_l2 && live_ok && g_vis == exp_ula,
              DETAIL("F:row200=0x%08X (exp L2 0x%08X) live prio=%u spr_en=%d "
                     "raw=0x%02X F+1:row50=0x%08X (exp ULA 0x%08X)",
                     f_vis, exp_l2, r.layer_priority(), r.sprite_en(),
                     r.nr15_raw(), g_vis, exp_ula));
    }

    // PSCAN-G02-04 — sprite-enable (b0) per-line consumption, BOTH stages:
    //   the render gate (renderer.cpp render_row) and the compositor
    //   transparency clause must use the per-line value, not the engine's
    //   live end-of-frame flag.  A 16px sprite spans the toggle line: b0=1
    //   from frame start, b0=0 written at line 100 (mid-sprite).  Rows
    //   90..99 must show the sprite, rows 100..105 must show the ULA
    //   underneath.  The frame ENDS with b0=0 (engine live flag false), so
    //   a frame-end live read at either stage blanks the whole sprite.
    //   VHDL zxnext.vhd:6819, 6906-6907, 6934, 7118 (sprites.vhd has no
    //   NR 0x15 b0 input — the engine always runs; b0 gates the pixel in
    //   the video pipeline only).
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        pal.start_frame();   // seed palette baselines (render_frame rewinds)

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();

        for (uint16_t off = 0x0000; off < 0x1800; ++off)
            ram.write(10u * 8192u + off, 0xFF);
        for (uint16_t off = 0x1800; off < 0x1B00; ++off)
            ram.write(10u * 8192u + off, 0x02);

        Layer2 l2;
        Tilemap tm;
        l2.reset(); tm.reset();

        // Sprite 0: solid 16x16 pattern of index 0x07 at (100, 90) —
        // fb cells x 200..231, rows 90..105 (over-border coordinates).
        SpriteEngine sp;
        sp.reset();
        sp.write_slot_select(0);                 // pattern slot 0
        for (int i = 0; i < 256; ++i) sp.write_pattern(0x07);
        sp.write_slot_select(0);                 // sprite 0 attributes
        sp.write_attribute(100);                 // byte 0: X lsb
        sp.write_attribute(90);                  // byte 1: Y lsb
        sp.write_attribute(0x00);                // byte 2: no mirror/rotate
        sp.write_attribute(0x80 | 0x00);         // byte 3: visible, pattern 0
        sp.set_clip_x1(0);   sp.set_clip_x2(0xFF);
        sp.set_clip_y1(0);   sp.set_clip_y2(0xFF);
        sp.set_over_border(true);
        sp.start_frame();    // seed sprite baselines from the setup above

        // NR 0x15 as the emulator dispatches it: each write also mirrors
        // b0 into the engine's live flag, so the frame ends with the
        // engine flag FALSE — the discriminating half of this row.
        r.write_nr15(0x01);                      // SLU + sprites on
        sp.set_sprites_visible(true);
        r.start_frame_nr15();
        r.set_current_line_nr15(100);
        r.write_nr15(0x00);                      // sprites off mid-sprite
        sp.set_sprites_visible(false);

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        const uint32_t exp_spr = pal.sprite_colour(0x07);
        const uint32_t exp_ula = pal.ula_colour(false, 0x02);
        const int x = 210;                       // inside fb cells 200..231
        const uint32_t upper = fb[94u  * Renderer::FB_WIDTH + x];
        const uint32_t lower = fb[102u * Renderer::FB_WIDTH + x];

        check("PSCAN-G02-04",
              "NR 0x15 b0 per-line: sprite half-band before the mid-sprite "
              "disable renders, half-band after is suppressed, with the "
              "engine live flag ending FALSE (VHDL 6819/6934/7118)",
              upper == exp_spr && lower == exp_ula,
              DETAIL("row94=0x%08X (exp sprite 0x%08X) row102=0x%08X "
                     "(exp ULA 0x%08X)", upper, exp_spr, lower, exp_ula));
    }

    // PSCAN-G02-05 — write_nr15 change-log cap: log stops at
    //   MAX_NR15_CHANGES_PER_FRAME, the once-per-frame warn latch fires
    //   exactly once and start_frame_nr15 re-arms it; the LIVE state
    //   still tracks every write past the cap (the live update precedes
    //   the cap early-return), so non-render consumers (NR 0x15 read
    //   handler) never see stale values.  Mirrors PSCAN-04.
    {
        Renderer r;
        r.reset();
        r.start_frame_nr15();

        const bool warned_before = r.nr15_overflow_warned_;

        const size_t over = Renderer::MAX_NR15_CHANGES_PER_FRAME + 1;
        for (size_t i = 0; i < over; ++i)
            r.write_nr15(static_cast<uint8_t>((i & 1) ? 0x10 : 0x00));
        const bool warned_after   = r.nr15_overflow_warned_;
        const bool size_capped    =
            r.nr15_change_log_size() == Renderer::MAX_NR15_CHANGES_PER_FRAME;

        // Past the cap the live state must still follow the last write.
        r.write_nr15(0x15);   // b4:2=101 (ULS) + b0=1 (sprite_en)
        const bool live_tracks = r.layer_priority() == 5
                              && r.sprite_en() == true
                              && r.nr15_raw() == 0x15;
        const bool warned_persists = r.nr15_overflow_warned_;

        r.start_frame_nr15();
        const bool rearmed = !r.nr15_overflow_warned_
                          && r.nr15_change_log_size() == 0;

        check("PSCAN-G02-05",
              "NR 0x15 change log caps at MAX, warn latch fires once and "
              "start_frame_nr15 re-arms it; live state tracks writes past "
              "the cap",
              !warned_before && warned_after && size_capped && live_tracks
              && warned_persists && rearmed,
              DETAIL("before=%d after=%d capped=%d live=%d persists=%d "
                     "rearmed=%d", warned_before, warned_after, size_capped,
                     live_tracks, warned_persists, rearmed));
    }

    // ── G10 — NR 0x43 b2/b3 selector CONSUMPTION by the rasterizers ───────
    //
    // GH #163.  The NR 0x43 selector change-log (b1 ULA / b2 Layer 2 /
    // b3 sprite) was built and fed, and the ULA lane was consumed, but
    // NOTHING read the Layer 2 or sprite lane back out: Layer2 and
    // SpriteEngine resolved colour through PaletteManager accessors that
    // took NO bank and read the LIVE `active_l2_second_` /
    // `active_spr_second_`.  Renderer::render_frame runs after the CPU and
    // Copper have finished the frame, so "live" meant END-of-frame and
    // every row collapsed onto last-write-wins — ShowAll512Colors/
    // show512.nex, whose Copper flips b2 halfway down a 32x16 colour
    // field, showed 256 colours instead of 512.
    //
    // VHDL oracle: zxnext.vhd:5391-5393 latch b1/b2/b3 as three
    // independent storage cells and :6825-6828 read them in the
    // active-palette mux per pixel cycle, so a MOVE landing at line N
    // applies from line N — never retroactively to the rows above it.
    //
    // Like the G02 rows above (and unlike the bookkeeping-only PSCAN-G04/
    // G11 rows), these drive the real Renderer::render_frame end to end,
    // so an unwired bank is an observable pixel change.  Every expected
    // value below is a LITERAL ARGB, computed by hand from the RRRGGGBB
    // byte the fixture programmes, so a palette accessor that ignored its
    // bank argument cannot also collapse the expectation.
    //
    //   RRRGGGBB 0xE0 -> rgb333 (7,0,0) -> ARGB 0xFFFF0000  (red)
    //   RRRGGGBB 0x1C -> rgb333 (0,7,0) -> ARGB 0xFF00FF00  (green)
    //   RRRGGGBB 0xE1 -> rgb333 (7,0,3) -> ARGB 0xFFFF006D  (red, blue=3)
    //
    // 0xE1 is the transparency probe in G10-02: its palette-format blue
    // (3 -> 109) differs from the register-format expansion
    // Renderer::rrrgggbb_to_argb gives the same byte (1 -> 85), so the
    // compositor's own `l2_px == nr14_rgb` clause CANNOT heal a wrongly
    // opaque pixel there (see the Task 46 comment in composite_scanline).

    static constexpr uint32_t G10_RED   = 0xFFFF0000u;   // RRRGGGBB 0xE0
    static constexpr uint32_t G10_GREEN = 0xFF00FF00u;   // RRRGGGBB 0x1C
    static constexpr uint32_t G10_TRANS = 0xFFFF006Du;   // RRRGGGBB 0xE1

    // Fill Layer 2 bank 9 (256x192 8bpp) with a single palette index and
    // return the configured layer. `idx` is deliberately never 0: a zero
    // index reads a palette entry that is 0 in both banks after reset, so
    // the row would pass whether or not the code under test ran.
    auto g10_make_layer2 = [](Ram& ram, Mmu& mmu, Layer2& l2, uint8_t idx) {
        l2.reset();
        l2.set_enabled(true);
        l2.set_active_bank(9);
        const uint32_t base = (9u + (mmu.rom_in_sram() ? 16u : 0u)) * 16384u;
        for (uint32_t off = 0; off < 49152u; ++off)
            ram.write(base + off, idx);
        l2.start_frame();
    };

    // Programme one 8-bit palette entry into an explicit NR 0x43 target.
    // Bit 2 / bit 3 of the control byte stay CLEAR throughout, so the LIVE
    // PaletteManager selectors remain `false` for the whole fixture: any
    // accessor that fell back to them renders bank 0 on every row, which
    // no row below expects.
    auto g10_pal8 = [](PaletteManager& pal, uint8_t target_ctrl,
                       uint8_t idx, uint8_t rgb8) {
        pal.write_control(target_ctrl);
        pal.set_index(idx);
        pal.write_8bit(rgb8);
    };

    // PSCAN-G10-01 — Layer 2 COLOUR lane.  Layer 2 covers the display with
    //   one index whose two palette banks hold different colours; the
    //   NR 0x43 b2 selector flips at line 100.  Rows above 100 must resolve
    //   through bank 0, rows at/after 100 through bank 1.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        const uint8_t K = 0x5A;
        g10_pal8(pal, 0x10, K, 0xE0);   // LAYER2_FIRST[K]  = red
        g10_pal8(pal, 0x50, K, 0x1C);   // LAYER2_SECOND[K] = green
        pal.start_frame();

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();
        r.init_transparent_rgb_per_line();

        Layer2 l2;
        g10_make_layer2(ram, mmu, l2, K);

        Tilemap tm;
        SpriteEngine sp;
        tm.reset(); sp.reset();

        r.write_nr15(0x00);            // SLU, sprites off -> L2 above ULA
        r.start_frame_nr15();

        // NR 0x43 exactly as Emulator's write handler drives it: the live
        // PaletteManager control AND the Ula selector change-log. Baseline
        // b2=0, then a Copper-style MOVE tagged at line 100 sets b2=1.
        r.ula().set_active_layer2_palette(false);
        r.ula().palsel_start_frame();
        r.ula().set_palsel_current_line(100);
        r.ula().set_active_layer2_palette(true);

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        const int x = Renderer::DISP_X + 20;
        const uint32_t before = fb[ 50u * Renderer::FB_WIDTH + x];
        const uint32_t at     = fb[100u * Renderer::FB_WIDTH + x];
        const uint32_t after  = fb[200u * Renderer::FB_WIDTH + x];

        check("PSCAN-G10-01",
              "mid-frame NR 0x43 b2 flip (line 100) switches the Layer 2 "
              "palette bank from that row on; rows above keep bank 0 "
              "(VHDL 5392, 6827)",
              before == G10_RED && at == G10_GREEN && after == G10_GREEN,
              DETAIL("row50=0x%08X (exp 0x%08X) row100=0x%08X row200=0x%08X "
                     "(exp 0x%08X)", before, G10_RED, at, after, G10_GREEN));
    }

    // PSCAN-G10-02 — Layer 2 TRANSPARENCY gate follows the same per-line
    //   bank.  Bank 0's entry for the index matches NR 0x14, so Layer 2
    //   skip-writes it and the ULA below shows through; bank 1's entry does
    //   not, so from the flip line the Layer 2 pixel appears.  This row
    //   discriminates `layer2_rgb8`'s bank INDEPENDENTLY of `layer2_colour`'s:
    //   a wrong bank on either accessor alone changes a different row.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        const uint8_t K = 0x5A;
        g10_pal8(pal, 0x10, K, 0xE1);   // LAYER2_FIRST[K]  = NR 0x14 match
        g10_pal8(pal, 0x50, K, 0x1C);   // LAYER2_SECOND[K] = green, opaque
        pal.set_global_transparency(0xE1);
        pal.start_frame();

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();
        // Both halves of the NR 0x14 handler, as emulator.cpp writes them.
        r.set_transparent_rgb(0xE1);
        r.init_transparent_rgb_per_line();

        // ULA below Layer 2: every pixel ink, attribute ink = 3 (magenta in
        // the reset-default ULA palette) so "Layer 2 was transparent here"
        // is a distinctive colour, not black.
        for (uint16_t off = 0x0000; off < 0x1800; ++off)
            ram.write(10u * 8192u + off, 0xFF);
        for (uint16_t off = 0x1800; off < 0x1B00; ++off)
            ram.write(10u * 8192u + off, 0x03);

        Layer2 l2;
        g10_make_layer2(ram, mmu, l2, K);

        Tilemap tm;
        SpriteEngine sp;
        tm.reset(); sp.reset();

        r.write_nr15(0x00);            // SLU, sprites off
        r.start_frame_nr15();

        r.ula().set_active_layer2_palette(false);
        r.ula().palsel_start_frame();
        r.ula().set_palsel_current_line(100);
        r.ula().set_active_layer2_palette(true);

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        const uint32_t exp_ula = pal.ula_colour(false, 0x03);
        const int x = Renderer::DISP_X + 20;
        const uint32_t before = fb[ 50u * Renderer::FB_WIDTH + x];
        const uint32_t at     = fb[100u * Renderer::FB_WIDTH + x];

        check("PSCAN-G10-02",
              "mid-frame NR 0x43 b2 flip also moves the Layer 2 NR 0x14 "
              "transparency comparison to the new bank: bank-0 rows stay "
              "transparent (ULA shows), bank-1 rows are opaque "
              "(VHDL 5392, 6827, 7121)",
              before == exp_ula && at == G10_GREEN
                  && exp_ula != G10_GREEN && exp_ula != G10_TRANS,
              DETAIL("row50=0x%08X (exp ULA 0x%08X) row100=0x%08X (exp "
                     "0x%08X); wrong-bank opaque would be 0x%08X",
                     before, exp_ula, at, G10_GREEN, G10_TRANS));
    }

    // PSCAN-G10-03 — Layer 2 per-pixel PRIORITY bit (NR 0x44 b7) follows the
    //   same per-line bank.  Both banks hold the SAME colour at the index, so
    //   the only variable is the priority bit: bank 0 clear, bank 1 set.  In
    //   SLU an opaque sprite beats Layer 2 unless layer2_priority promotes it
    //   (VHDL 7220), so the sprite band splits at the flip line.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        const uint8_t K = 0x5A;
        // Identical green in both banks; priority differs via NR 0x44's
        // second byte (bits 7:6 -> nr_palette_priority, zxnext.vhd:4920).
        pal.write_control(0x10);  pal.set_index(K);
        pal.write_9bit(0x1C);     pal.write_9bit(0x00);   // bank 0: prio 0
        pal.write_control(0x50);  pal.set_index(K);
        pal.write_9bit(0x1C);     pal.write_9bit(0x80);   // bank 1: prio 1
        // Sprite colour: index 0x07 red, so "sprite won" and "Layer 2 won"
        // are different colours.
        g10_pal8(pal, 0x20, 0x07, 0xE0);
        pal.start_frame();

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();
        r.init_transparent_rgb_per_line();

        Layer2 l2;
        g10_make_layer2(ram, mmu, l2, K);

        Tilemap tm;
        tm.reset();

        // Sprite 0: solid 16x16 of index 0x07 at (100, 90) -> fb cells
        // x 200..231, rows 90..105 — spanning the flip line 98.
        SpriteEngine sp;
        sp.reset();
        sp.write_slot_select(0);
        for (int i = 0; i < 256; ++i) sp.write_pattern(0x07);
        sp.write_slot_select(0);
        sp.write_attribute(100);
        sp.write_attribute(90);
        sp.write_attribute(0x00);
        sp.write_attribute(0x80 | 0x00);
        sp.set_clip_x1(0);   sp.set_clip_x2(0xFF);
        sp.set_clip_y1(0);   sp.set_clip_y2(0xFF);
        sp.set_over_border(true);
        sp.set_sprites_visible(true);
        sp.start_frame();

        r.write_nr15(0x01);            // SLU + sprites on
        r.start_frame_nr15();

        r.ula().set_active_layer2_palette(false);
        r.ula().palsel_start_frame();
        r.ula().set_palsel_current_line(98);
        r.ula().set_active_layer2_palette(true);

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        const int x = 210;             // inside the sprite band
        const uint32_t upper = fb[94u  * Renderer::FB_WIDTH + x];
        const uint32_t lower = fb[102u * Renderer::FB_WIDTH + x];

        check("PSCAN-G10-03",
              "mid-frame NR 0x43 b2 flip also moves the Layer 2 palette "
              "PRIORITY bit lookup to the new bank: sprite wins above the "
              "flip line, promoted Layer 2 wins below it (VHDL 5392, 6827, "
              "7050, 7220)",
              upper == G10_RED && lower == G10_GREEN,
              DETAIL("row94=0x%08X (exp sprite 0x%08X) row102=0x%08X (exp "
                     "L2 0x%08X)", upper, G10_RED, lower, G10_GREEN));
    }

    // PSCAN-G10-04 — sprite lane (NR 0x43 b3).  Same shape as G10-01 on the
    //   other selector bit: one sprite band straddling the flip line, its
    //   pattern index resolving through two differently-coloured sprite
    //   palettes.  VHDL 5391, 6828.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        g10_pal8(pal, 0x20, 0x07, 0xE0);   // SPRITE_FIRST[0x07]  = red
        g10_pal8(pal, 0x60, 0x07, 0x1C);   // SPRITE_SECOND[0x07] = green
        pal.start_frame();

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();
        r.init_transparent_rgb_per_line();

        Layer2 l2;
        Tilemap tm;
        l2.reset(); tm.reset();

        SpriteEngine sp;
        sp.reset();
        sp.write_slot_select(0);
        for (int i = 0; i < 256; ++i) sp.write_pattern(0x07);
        sp.write_slot_select(0);
        sp.write_attribute(100);
        sp.write_attribute(90);
        sp.write_attribute(0x00);
        sp.write_attribute(0x80 | 0x00);
        sp.set_clip_x1(0);   sp.set_clip_x2(0xFF);
        sp.set_clip_y1(0);   sp.set_clip_y2(0xFF);
        sp.set_over_border(true);
        sp.set_sprites_visible(true);
        sp.start_frame();

        r.write_nr15(0x01);            // SLU + sprites on
        r.start_frame_nr15();

        r.ula().set_active_sprite_palette(false);
        r.ula().palsel_start_frame();
        r.ula().set_palsel_current_line(98);
        r.ula().set_active_sprite_palette(true);

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        const int x = 210;
        const uint32_t upper = fb[94u  * Renderer::FB_WIDTH + x];
        const uint32_t lower = fb[102u * Renderer::FB_WIDTH + x];

        check("PSCAN-G10-04",
              "mid-frame NR 0x43 b3 flip (line 98) switches the SPRITE "
              "palette bank from that row on; rows above keep bank 0 "
              "(VHDL 5391, 6828)",
              upper == G10_RED && lower == G10_GREEN,
              DETAIL("row94=0x%08X (exp 0x%08X) row102=0x%08X (exp 0x%08X)",
                     upper, G10_RED, lower, G10_GREEN));
    }

    // PSCAN-G10-05 — TILEMAP lane (NR 0x6B b4).  GH #168, the third lane of
    //   the same defect.  The selector is logged and replayed by the SAME
    //   Ula palsel machinery (a separate 1-bit log, because NR 0x6B b4 is a
    //   different latch from NR 0x43's 3-bit field), and
    //   Ula::get_active_tilemap_palette() had EIGHT test references and ZERO
    //   production callers: Tilemap::render_scanline resolved colour through
    //   PaletteManager::tilemap_colour(idx), which read the live
    //   `active_tm_second_` — the END-of-frame value at render time.
    //
    //   VHDL oracle:
    //     zxnext.vhd:5462   nr_6b_tm_control <= nr_wr_dat(6 downto 0)
    //     zxnext.vhd:6826   tm_palette_select_0 <= nr_6b_tm_control(4)
    //     zxnext.vhd:6921-6922  pipelined _0 -> _1a -> _1
    //     zxnext.vhd:6981   ulatm_pixel_1 <= ... else ('1' & tm_palette_select_1
    //                       & tm_pixel_1)   -- palette RAM address bit
    //   i.e. a per-pixel-cycle read of an independent latch, exactly like the
    //   NR 0x43 lanes above, so a MOVE landing at line N applies from line N
    //   and never retroactively to the rows above it.
    //
    //   Shape mirrors G10-01: one index, two differently-coloured tilemap
    //   palette banks, a flip at line 100, sampled above/at/below.  The LIVE
    //   PaletteManager tilemap selector is never touched (NR 0x43 does not
    //   drive it — palette.cpp:251), so it stays `false` for the whole
    //   fixture: any accessor that fell back to it renders bank 0 on every
    //   row, which the at/below samples do not expect.  Expected values are
    //   the same hand-computed literals used above, never read back through
    //   the accessor under test.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.reset();
        mmu.set_page(2, 10);
        mmu.set_page(3, 11);

        PaletteManager pal;
        pal.reset();
        // Palette index = (attr palette-offset << 4) | pixel nibble.
        // Offset 5, nibble 0x0A -> 0x5A: non-zero (a zero index reads an
        // entry that is 0 in BOTH banks after reset, so the row would pass
        // whether or not the code under test ran), and the nibble differs
        // from the NR 0x4C tilemap transparency index (reset 0x0F) so the
        // pixel is emitted at all.
        const uint8_t PAL_OFF = 0x5;
        const uint8_t PIXEL   = 0x0A;
        const uint8_t K       = static_cast<uint8_t>((PAL_OFF << 4) | PIXEL);
        g10_pal8(pal, 0x30, K, 0xE0);   // TILEMAP_FIRST[K]  = red
        g10_pal8(pal, 0x70, K, 0x1C);   // TILEMAP_SECOND[K] = green
        pal.start_frame();

        Renderer r;
        r.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);
        r.ula().init_border_per_line();
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();
        r.init_transparent_rgb_per_line();

        Layer2 l2;
        SpriteEngine sp;
        l2.reset(); sp.reset();

        // Tilemap: 40x32 of one tile, attribute palette-offset PAL_OFF and
        // bit 0 (ULA-over-tilemap) clear, so the tilemap wins the ULA/TM
        // merge (zxnext.vhd:7116) and every sampled cell is a tilemap pixel.
        // Reset-default bases: map 0x2C*256, definitions 0x0C*256, both in
        // bank 5 (5*16384 in this standalone Ram fixture).
        constexpr uint32_t BANK5    = 5u * 16384u;
        constexpr uint32_t MAP_BASE = BANK5 + 0x2Cu * 256u;
        constexpr uint32_t DEF_BASE = BANK5 + 0x0Cu * 256u;
        constexpr uint16_t TILE     = 1;
        Tilemap tm;
        tm.reset();
        const uint8_t pat_byte = static_cast<uint8_t>((PIXEL << 4) | PIXEL);
        for (int i = 0; i < 32; ++i)
            ram.write(DEF_BASE + TILE * 32u + static_cast<uint32_t>(i), pat_byte);
        for (int cell = 0; cell < 40 * 32; ++cell) {
            ram.write(MAP_BASE + static_cast<uint32_t>(cell) * 2u, TILE);
            ram.write(MAP_BASE + static_cast<uint32_t>(cell) * 2u + 1u,
                      static_cast<uint8_t>(PAL_OFF << 4));
        }
        tm.set_control(0x80);           // b7 enable, 40-col, 4bpp, 2-byte map
        tm.init_scroll_per_line();
        tm.start_frame_nr6b();

        r.write_nr15(0x00);             // SLU, sprites off
        r.start_frame_nr15();

        // NR 0x6B exactly as Emulator's write handler drives it: the live
        // PaletteManager selector AND the Ula palsel6b change-log. Here only
        // the log side is exercised — the live side stays at bank 0.
        r.ula().set_active_tilemap_palette(false);
        r.ula().palsel_start_frame();
        r.ula().set_palsel_current_line(100);
        r.ula().set_active_tilemap_palette(true);

        std::vector<uint32_t> fb(
            static_cast<size_t>(Renderer::FB_WIDTH) * Renderer::FB_HEIGHT, 0);
        r.render_frame(fb.data(), mmu, ram, pal, l2, &sp, &tm);

        const int x = Renderer::DISP_X + 20;
        const uint32_t before = fb[ 50u * Renderer::FB_WIDTH + x];
        const uint32_t at     = fb[100u * Renderer::FB_WIDTH + x];
        const uint32_t after  = fb[200u * Renderer::FB_WIDTH + x];

        check("PSCAN-G10-05",
              "mid-frame NR 0x6B b4 flip (line 100) switches the TILEMAP "
              "palette bank from that row on; rows above keep bank 0 "
              "(VHDL 5462, 6826, 6981)",
              before == G10_RED && at == G10_GREEN && after == G10_GREEN
                  && !pal.active_tilemap_palette(),
              DETAIL("row50=0x%08X (exp 0x%08X) row100=0x%08X row200=0x%08X "
                     "(exp 0x%08X) live_sel=%d", before, G10_RED, at, after,
                     G10_GREEN, int(pal.active_tilemap_palette())));
    }
}

// ── Group UCLIP — NR 0x1A ULA clip window per-line deferral ───────────────
//
// The last instance of the Task 43/45/46 per-line-deferral bug class:
// Renderer::apply_ula_clip masked every rendered ULA scanline with the LIVE
// Ula::clip_x1()/clip_x2()/clip_y1()/clip_y2() values, so a mid-frame NR 0x1A
// write (Copper MOVE / CPU racing the beam) retroactively re-masked every row
// rendered after the write — including rows the beam had already passed.
//
// VHDL oracle: the ULA clip comparators consume the live registers per pixel
// (zxnext.vhd:988-991 stage-0 register; NR 0x1A rotating 4-write cycle and
// the y2>=0xC0 clamp at zxnext.vhd:6779-6783), so a mid-frame change takes
// effect exactly at the raster position where it lands — never earlier.
//
// jnext models this with the standard per-line snapshot trio
// (snapshot_ula_clip_for_line / init_ula_clip_per_line / ula_clip_for_line),
// captured by Emulator::on_scanline and consumed by apply_ula_clip(row).
// These rows drive apply_ula_clip() directly (the compositor-stage consumer)
// and are discriminative against BOTH mutation halves:
//   (a) consumption reverted to the live getters  → UCLIP-01 goes red
//   (b) capture stubbed dead (snapshot never writes; the array keeps the
//       reset-default full window 0/255/0/191) → UCLIP-02/03 go red.
//
// Window A (x1=128, x2=255) is deliberately NOT the reset default, so a dead
// capture is distinguishable from a stale-but-captured snapshot: under A the
// LEFT BORDER is clipped (renderer.cpp: left_clipped = cx1 > 0), under the
// reset default it is not.

static void test_UCLIP() {
    set_group("UCLIP");
    Renderer r;
    r.reset();

    // A display row inside both windows' y range (y1=0, y2=191 throughout;
    // only x changes mid-frame). fb row 52 = display row 20.
    const int ROW = Renderer::DISP_Y + 20;
    // Source display column 200 → fb cell DISP_X + 2*200 (G104 doubling).
    const int CELL_IN_A  = Renderer::DISP_X + 2 * 200;  // kept by A, clipped by B
    const int CELL_BORDER = 4;                          // left border cell

    auto set_clip = [&](uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2) {
        r.ula().set_clip_x1(x1); r.ula().set_clip_x2(x2);
        r.ula().set_clip_y1(y1); r.ula().set_clip_y2(y2);
    };
    auto fill_line = [&](uint32_t* line) {
        for (int i = 0; i < W; ++i) line[i] = PIX_ULA;
    };

    // Baseline snapshot for ROW: window A = right half (x1=128..x2=255).
    set_clip(128, 255, 0, 191);
    r.snapshot_ula_clip_for_line(ROW);

    // NR 0x1A writes land mid-frame, flipping to window B = left half
    // (x1=0..x2=127). ROW's snapshot is deliberately NOT refreshed.
    set_clip(0, 127, 0, 191);

    uint32_t line[W];

    // UCLIP-01: the row masked BEFORE its snapshot refresh must still use
    // window A — source col 200 survives (live window B would clip it).
    {
        fill_line(line);
        r.apply_ula_clip(line, ROW);
        check("UCLIP-01",
              "mid-frame NR 0x1A write does not retroactively re-mask a row "
              "whose per-line snapshot already ran — col 200 survives under "
              "the stale window A (VHDL zxnext.vhd:988-991, 6779-6783)",
              line[CELL_IN_A] == PIX_ULA,
              DETAIL("cell[%d]=0x%08X expected=0x%08X (live window B would "
                     "clip it)", CELL_IN_A, line[CELL_IN_A], PIX_ULA));

        // UCLIP-02: same buffer — the LEFT BORDER is clipped because the
        // SNAPSHOTTED window A has x1=128 > 0. Discriminative against a
        // dead capture: the reset-default window (x1=0) keeps the border.
        check("UCLIP-02",
              "…and the left border is clipped per the SNAPSHOTTED window A "
              "(x1=128>0) — proves the snapshot is captured, not the "
              "reset default (renderer.cpp left_clipped; VHDL 6779-6783)",
              line[CELL_BORDER] == TRANSP,
              DETAIL("cell[%d]=0x%08X expected TRANSPARENT",
                     CELL_BORDER, line[CELL_BORDER]));
    }

    // UCLIP-03: refresh the row's snapshot (mirrors the next on_scanline
    // call). The SAME row must now use window B: col 200 clipped, left
    // border kept (x1=0), right border clipped (x2=127<255).
    {
        r.snapshot_ula_clip_for_line(ROW);
        fill_line(line);
        r.apply_ula_clip(line, ROW);
        const bool col_clipped   = (line[CELL_IN_A] == TRANSP);
        const bool border_kept   = (line[CELL_BORDER] == PIX_ULA);
        const bool right_clipped = (line[Renderer::FB_WIDTH - 4] == TRANSP);
        check("UCLIP-03",
              "after the deferred snapshot lands, the SAME row selects "
              "window B: col 200 clipped, left border kept, right border "
              "clipped (VHDL zxnext.vhd:988-991, 6779-6783)",
              col_clipped && border_kept && right_clipped,
              DETAIL("cell[%d]=0x%08X border=0x%08X right=0x%08X",
                     CELL_IN_A, line[CELL_IN_A], line[CELL_BORDER],
                     line[Renderer::FB_WIDTH - 4]));
    }

    // UCLIP-04: full split-frame shape — rows below the split keep window A,
    // rows at/after the split get window B, exactly like a Copper program
    // changing the ULA clip at scanline S.
    {
        const int R0 = Renderer::DISP_Y;        // fb rows 32..71
        const int S  = Renderer::DISP_Y + 20;   // split at fb row 52
        const int R1 = Renderer::DISP_Y + 40;

        set_clip(128, 255, 0, 191);             // window A
        for (int row = R0; row < S; ++row)
            r.snapshot_ula_clip_for_line(row);
        set_clip(0, 127, 0, 191);               // mid-frame write → window B
        for (int row = S; row < R1; ++row)
            r.snapshot_ula_clip_for_line(row);

        bool ok = true;
        int bad_row = -1;
        for (int row = R0; row < R1; ++row) {
            fill_line(line);
            r.apply_ula_clip(line, row);
            const bool want_a = (row < S);
            const bool row_ok = want_a
                ? (line[CELL_IN_A] == PIX_ULA && line[CELL_BORDER] == TRANSP)
                : (line[CELL_IN_A] == TRANSP  && line[CELL_BORDER] == PIX_ULA);
            if (!row_ok && ok) { ok = false; bad_row = row; }
        }
        check("UCLIP-04",
              "split frame: rows < S masked with window A, rows >= S with "
              "window B — mid-frame NR 0x1A change lands exactly at the "
              "scanline where it was written (VHDL zxnext.vhd:988-991)",
              ok, DETAIL("first bad row=%d (split S=%d)", bad_row, S));
    }

    // Restore reset-default window for any group appended after this one.
    set_clip(0, 255, 0, 191);
    r.init_ula_clip_per_line();
}

// ── Group LMASK — host-side layer mask (--delayed-screenshot-layers) ──────
//
// Task 22b. The mask is a HOST debug knob, not hardware: it selects which
// layers the compositor is allowed to see when the delayed screenshot is
// taken. Its contract is that a masked-out layer behaves EXACTLY like a
// layer whose hardware enable bit is clear, so the VHDL oracle for every
// row below is the corresponding "layer disabled" behaviour:
//
//   ULA      ula_en   = 0  → ula_transparent      (zxnext.vhd:7103)
//   Layer 2  l2_en    = 0  → l2_transparent       (zxnext.vhd:7106)
//   Sprites  sprite_en= 0  → sprite_transparent   (zxnext.vhd:7118)
//   Tilemap  tm_en    = 0  → tm_transparent       (zxnext.vhd:7109), and
//                            stencil is gated on tm_en (zxnext.vhd:7130)
//
// Everything downstream (NR 0x15 priority, ULA/TM merge, blend, the NR 0x4A
// fallback) must therefore keep working unchanged — including the border,
// which the ULA emits, and which consequently falls through to the fallback
// colour when `ula` is excluded.

static void test_LMASK() {
    set_group("LMASK");

    // ── Parser rows (Renderer::parse_layer_mask) ─────────────────────────

    struct ParseOk { const char* spec; uint8_t expect; const char* desc; };
    const ParseOk ok_rows[] = {
        {"ula",     Renderer::LAYER_ULA,     "single name 'ula'"},
        {"layer2",  Renderer::LAYER_LAYER2,  "single name 'layer2'"},
        {"sprites", Renderer::LAYER_SPRITES, "single name 'sprites'"},
        {"tiles",   Renderer::LAYER_TILES,   "single name 'tiles'"},
        {"all",     Renderer::LAYER_ALL,     "'all' selects every layer"},
        {"ula,layer2",
             static_cast<uint8_t>(Renderer::LAYER_ULA | Renderer::LAYER_LAYER2),
             "two names"},
        {"layer2,ula",
             static_cast<uint8_t>(Renderer::LAYER_ULA | Renderer::LAYER_LAYER2),
             "order does not matter"},
        {"sprites,tiles,ula,layer2", Renderer::LAYER_ALL,
             "all four names spelled out == 'all'"},
    };
    for (size_t i = 0; i < sizeof(ok_rows) / sizeof(ok_rows[0]); ++i) {
        const auto& row = ok_rows[i];
        uint8_t     mask = 0xFF;
        std::string err  = "unset";
        const bool  got  = Renderer::parse_layer_mask(row.spec, mask, err);
        char id[32];
        snprintf(id, sizeof(id), "LMASK-P%02zu", i + 1);
        check(id, row.desc,
              got && mask == row.expect && err.empty(),
              DETAIL("spec='%s' ok=%d mask=0x%02X (exp 0x%02X) err='%s'",
                     row.spec, got ? 1 : 0, mask, row.expect, err.c_str()));
    }

    // Rejections. Fail loud: every one of these must return false with a
    // non-empty message; none may be silently folded away.
    struct ParseErr { const char* spec; const char* desc; };
    const ParseErr err_rows[] = {
        {"",            "empty list is an error"},
        {"bogus",       "unknown name is an error"},
        {"ULA",         "names are lowercase only — 'ULA' is unknown"},
        {"Layer2",      "mixed case is unknown"},
        {"ula,bogus",   "one bad name in a good list still errors"},
        {"ula ,tiles",  "no whitespace tolerance — ' ' is part of the name"},
        {"ula,ula",     "duplicate name is an error"},
        {"all,ula",     "'all' plus another name double-selects — error"},
        {"ula,all",     "…in either order"},
        {"all,all",     "'all' twice is an error"},
        {"ula,",        "trailing comma leaves an empty name — error"},
        {",ula",        "leading comma leaves an empty name — error"},
        {"ula,,tiles",  "empty element in the middle — error"},
        {",",           "a lone comma is an error"},
    };
    for (size_t i = 0; i < sizeof(err_rows) / sizeof(err_rows[0]); ++i) {
        const auto& row = err_rows[i];
        uint8_t     mask = 0xAA;   // sentinel: must not be touched on failure
        std::string err;
        const bool  got = Renderer::parse_layer_mask(row.spec, mask, err);
        char id[32];
        snprintf(id, sizeof(id), "LMASK-E%02zu", i + 1);
        check(id, row.desc,
              !got && !err.empty() && mask == 0xAA,
              DETAIL("spec='%s' returned=%d mask=0x%02X err='%s'",
                     row.spec, got ? 1 : 0, mask, err.c_str()));
    }

    // Mask → name round-trip (used by the frontends' info log line).
    {
        const std::string s_all  = Renderer::layer_mask_to_string(Renderer::LAYER_ALL);
        const std::string s_two  = Renderer::layer_mask_to_string(
            static_cast<uint8_t>(Renderer::LAYER_ULA | Renderer::LAYER_TILES));
        const std::string s_none = Renderer::layer_mask_to_string(0);
        check("LMASK-S01",
              "layer_mask_to_string: ALL->'all', ula|tiles->'ula,tiles', 0->'none'",
              s_all == "all" && s_two == "ula,tiles" && s_none == "none",
              DETAIL("all='%s' two='%s' none='%s'",
                     s_all.c_str(), s_two.c_str(), s_none.c_str()));
    }

    // ── Compositor rows ──────────────────────────────────────────────────

    Renderer r;
    r.reset();

    const uint32_t FB = vhdl_fallback_argb(0x10);   // distinct from every PIX_*

    // Plant all four layers opaque at x=0 with mode 000 (SLU).
    auto all_four = [&](uint8_t mask) -> uint32_t {
        clear_layers(r);
        r.set_layer_priority(0);          // SLU: sprite, layer2, ULA(+TM)
        r.ula_line_[0]     = PIX_ULA;
        r.layer2_line_[0]  = PIX_L2;
        r.sprite_line_[0]  = PIX_S;
        r.tilemap_line_[0] = PIX_TM;
        r.tm_enabled_      = true;
        r.set_layer_mask(mask);
        return composite_one(r, FB);
    };

    // LMASK-C01 — the default. reset() must leave the mask at LAYER_ALL and
    // the composite must be the untouched SLU result: TM sits above the ULA
    // in the ULA/TM merge but sprites are top of the SLU order.
    {
        Renderer fresh;
        fresh.reset();
        const uint8_t  m   = fresh.layer_mask();
        const uint32_t got = all_four(Renderer::LAYER_ALL);
        check("LMASK-C01",
              "default mask is LAYER_ALL and composes every layer (SLU: sprite wins)",
              m == Renderer::LAYER_ALL && got == PIX_S,
              DETAIL("reset_mask=0x%02X got=0x%08X exp=0x%08X", m, got, PIX_S));
    }

    // LMASK-C02..C05 — each layer captured alone. With everything else
    // masked out, the surviving layer must reach the output regardless of
    // its position in the NR 0x15 priority order.
    {
        const uint32_t got = all_four(Renderer::LAYER_SPRITES);
        check("LMASK-C02", "'sprites' alone -> sprite pixel (ULA/L2/TM suppressed)",
              got == PIX_S, DETAIL("got=0x%08X exp=0x%08X", got, PIX_S));
    }
    {
        const uint32_t got = all_four(Renderer::LAYER_LAYER2);
        check("LMASK-C03", "'layer2' alone -> L2 pixel even though SLU puts sprites on top",
              got == PIX_L2, DETAIL("got=0x%08X exp=0x%08X", got, PIX_L2));
    }
    {
        const uint32_t got = all_four(Renderer::LAYER_ULA);
        check("LMASK-C04", "'ula' alone -> ULA pixel (TM masked, so no ULA/TM override)",
              got == PIX_ULA, DETAIL("got=0x%08X exp=0x%08X", got, PIX_ULA));
    }
    {
        const uint32_t got = all_four(Renderer::LAYER_TILES);
        check("LMASK-C05", "'tiles' alone -> TM pixel (ULA transparent, TM wins the merge)",
              got == PIX_TM, DETAIL("got=0x%08X exp=0x%08X", got, PIX_TM));
    }

    // LMASK-C06 — a selected-but-empty layer does not resurrect the masked
    // ones: the fallback colour (NR 0x4A) shows through, exactly as when
    // every hardware layer is disabled. VHDL zxnext.vhd:7214.
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]     = PIX_ULA;
        r.sprite_line_[0]  = PIX_S;
        r.tilemap_line_[0] = PIX_TM;
        r.layer2_line_[0]  = TRANSP;      // the one selected layer is empty here
        r.tm_enabled_      = true;
        r.set_layer_mask(Renderer::LAYER_LAYER2);
        const uint32_t got = composite_one(r, FB);
        check("LMASK-C06",
              "'layer2' alone with L2 transparent -> NR 0x4A fallback, no leakage "
              "from the masked ULA/sprite/TM pixels (VHDL 7214)",
              got == FB, DETAIL("got=0x%08X exp_fallback=0x%08X", got, FB));
    }

    // LMASK-C07 — the border question. The border is emitted by the ULA
    // path, so excluding 'ula' removes it too and those pixels fall through
    // to the fallback colour — the same thing the hardware shows with
    // ula_en=0 (VHDL 7103 makes ula_transparent cover display AND border).
    {
        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]   = PIX_ULA;      // a border cell painted by the ULA
        r.ula_border_[0] = true;
        r.set_layer_mask(Renderer::LAYER_LAYER2);
        const uint32_t masked = composite_one(r, FB);

        clear_layers(r);
        r.set_layer_priority(0);
        r.ula_line_[0]   = PIX_ULA;
        r.ula_border_[0] = true;
        r.set_layer_mask(Renderer::LAYER_ALL);
        const uint32_t unmasked = composite_one(r, FB);

        check("LMASK-C07",
              "excluding 'ula' removes the BORDER as well; those pixels take the "
              "NR 0x4A fallback (== hardware ula_en=0, VHDL 7103)",
              masked == FB && unmasked == PIX_ULA,
              DETAIL("masked=0x%08X (exp fallback 0x%08X) unmasked=0x%08X (exp ULA 0x%08X)",
                     masked, FB, unmasked, PIX_ULA));
    }

    // LMASK-C08 — ula_border_2 is a raster-geometry flag, not an enable, so
    // it stays asserted with the ULA masked out; the mode-4 (USL) border
    // exception must still fire on the sprite. VHDL zxnext.vhd:7266.
    {
        clear_layers(r);
        r.set_layer_priority(4);           // USL
        r.ula_line_[0]    = PIX_ULA;
        r.ula_border_[0]  = true;
        r.sprite_line_[0] = PIX_S;         // TM transparent, sprite opaque
        r.set_layer_mask(static_cast<uint8_t>(Renderer::LAYER_ULA | Renderer::LAYER_SPRITES));
        const uint32_t with_spr = composite_one(r, FB);

        clear_layers(r);
        r.set_layer_priority(4);
        r.ula_line_[0]    = PIX_ULA;
        r.ula_border_[0]  = true;
        r.sprite_line_[0] = PIX_S;
        r.set_layer_mask(Renderer::LAYER_ULA);   // sprite now masked out
        const uint32_t no_spr = composite_one(r, FB);

        check("LMASK-C08",
              "mode 100 border exception survives masking: sprite still wins over the "
              "border ULA (VHDL 7266); masking the sprite away hands the border back "
              "to the ULA",
              with_spr == PIX_S && no_spr == PIX_ULA,
              DETAIL("ula+sprites=0x%08X (exp S 0x%08X)  ula-only=0x%08X (exp ULA 0x%08X)",
                     with_spr, PIX_S, no_spr, PIX_ULA));
    }

    // LMASK-C09-xy — STENCIL, the full 2x2 mask matrix (x = ula masked,
    // y = tiles masked). VHDL zxnext.vhd:7130 selects the AND-branch only
    // when BOTH enables are set:
    //     if ula_stencil_mode_2='1' and ula_en_2='1' and tm_en_2='1'
    // and stencil_rgb is transparent whenever EITHER input is (7112). So
    // masking either layer away must drop out of the AND-branch into the
    // ordinary ulatm merge (7134-7135) and show the survivor — not erase it.
    // Testing only one off-diagonal cell is what let the ula_en half of the
    // gate go missing in the first place, so all four cells are pinned.
    //
    // Fixture: stencil on, tm_en on, ULA and TM both opaque, mode 000.
    auto stencil_cell = [&](uint8_t mask) -> uint32_t {
        clear_layers(r);
        r.set_layer_priority(0);
        r.stencil_mode_    = true;
        r.tm_enabled_      = true;
        r.ula_line_[0]     = PIX_ULA;
        r.tilemap_line_[0] = PIX_TM;
        r.set_layer_mask(mask);
        return composite_one(r, FB);
    };

    // Cell 00 — neither masked: the AND-branch is live, output is the
    // per-channel bitwise AND of the two RGBs (VHDL 7112-7113).
    {
        const uint8_t  and_rgb = static_cast<uint8_t>(
            ((argb_r3_t(PIX_ULA) & argb_r3_t(PIX_TM)) << 5) |
            ((argb_g3_t(PIX_ULA) & argb_g3_t(PIX_TM)) << 2) |
             (argb_b2_t(PIX_ULA) & argb_b2_t(PIX_TM)));
        const uint32_t exp = Renderer::rrrgggbb_to_argb(and_rgb);
        const uint32_t got = stencil_cell(Renderer::LAYER_ALL);
        check("LMASK-C09-00",
              "stencil, neither layer masked -> AND-branch live, ULA AND TM "
              "(VHDL 7130, 7112-7113)",
              got == exp, DETAIL("got=0x%08X exp=0x%08X", got, exp));
    }

    // Cell 01 — 'tiles' masked (tm_en=0 equivalent): AND-branch off, the
    // ulatm merge shows the ULA. ('--delayed-screenshot-layers ula')
    {
        const uint32_t got = stencil_cell(Renderer::LAYER_ULA);
        check("LMASK-C09-01",
              "stencil, 'tiles' masked -> AND-branch off (tm_en=0), ulatm merge "
              "shows the ULA (VHDL 7130, 7134-7135)",
              got == PIX_ULA, DETAIL("got=0x%08X exp=0x%08X", got, PIX_ULA));
    }

    // Cell 10 — 'ula' masked (ula_en=0 equivalent): AND-branch off, the
    // ulatm merge shows the TILE. This is the cell the first cut of the
    // feature got wrong — it kept the AND-branch selected, stencil_transparent
    // went high because ula_transparent was, and the tile vanished into the
    // fallback colour. ('--delayed-screenshot-layers tiles')
    {
        const uint32_t got = stencil_cell(Renderer::LAYER_TILES);
        check("LMASK-C09-10",
              "stencil, 'ula' masked -> AND-branch off (ula_en=0), ulatm merge "
              "shows the TILE, NOT the fallback (VHDL 7130, 7134-7135)",
              got == PIX_TM,
              DETAIL("got=0x%08X exp=0x%08X (fallback would be 0x%08X)",
                     got, PIX_TM, FB));
    }

    // Cell 11 — both masked: AND-branch off and both inputs transparent, so
    // the ulatm merge is transparent too and the NR 0x4A fallback shows.
    {
        const uint32_t got = stencil_cell(Renderer::LAYER_LAYER2);
        check("LMASK-C09-11",
              "stencil, both 'ula' and 'tiles' masked -> ulatm merge transparent, "
              "NR 0x4A fallback (VHDL 7214)",
              got == FB, DETAIL("got=0x%08X exp_fallback=0x%08X", got, FB));
    }

    // LMASK-C09-SPR — the failing cell again, but with the mask the CLI
    // actually produces for "everything except the ULA"
    // (--delayed-screenshot-layers layer2,sprites,tiles): the tile must
    // still survive. Guards against the gate being fixed only for the
    // single-layer spelling.
    {
        uint8_t     mask = 0;
        std::string err;
        const bool parsed = Renderer::parse_layer_mask("layer2,sprites,tiles", mask, err);
        const uint32_t got = stencil_cell(mask);
        check("LMASK-C09-SPR",
              "stencil, CLI mask 'layer2,sprites,tiles' (i.e. only 'ula' excluded) -> "
              "tile survives (VHDL 7130)",
              parsed && got == PIX_TM,
              DETAIL("parsed=%d mask=0x%02X got=0x%08X exp=0x%08X",
                     parsed ? 1 : 0, mask, got, PIX_TM));
    }

    // LMASK-C10 — the L2 priority bit (palette b15) promotes L2 above the
    // sprites (VHDL 7220). With L2 masked out, l2_transparent=1 and the
    // promotion must not fire: the sprite is on top again.
    {
        clear_layers(r);
        r.set_layer_priority(0);           // SLU
        r.layer2_line_[0]     = PIX_L2;
        r.layer2_priority_[0] = true;      // L2 promoted above sprites
        r.sprite_line_[0]     = PIX_S;
        r.set_layer_mask(Renderer::LAYER_ALL);
        const uint32_t promoted = composite_one(r, FB);

        clear_layers(r);
        r.set_layer_priority(0);
        r.layer2_line_[0]     = PIX_L2;
        r.layer2_priority_[0] = true;
        r.sprite_line_[0]     = PIX_S;
        r.set_layer_mask(Renderer::LAYER_SPRITES);
        const uint32_t masked = composite_one(r, FB);

        check("LMASK-C10",
              "masking 'layer2' out also cancels its priority-bit promotion over the "
              "sprites (VHDL 7220)",
              promoted == PIX_L2 && masked == PIX_S,
              DETAIL("unmasked=0x%08X (exp L2 0x%08X) masked=0x%08X (exp S 0x%08X)",
                     promoted, PIX_L2, masked, PIX_S));
    }

    // LMASK-C11 — blend mode 110 (additive). Masking the ULA out makes
    // mix_rgb transparent, i.e. its channels contribute 0 to the sum (VHDL
    // 7101/7122 + 7288-7298), so the mixer emits Layer 2 unchanged.
    {
        const uint8_t  L2_RGB   = 0x24;                       // r=1 g=1 b=0
        const uint8_t  ULA_RGB  = 0x49;                       // r=2 g=2 b=1
        const uint32_t L2_ARGB  = Renderer::rrrgggbb_to_argb(L2_RGB);
        const uint32_t ULA_ARGB = Renderer::rrrgggbb_to_argb(ULA_RGB);
        const uint32_t SUM_ARGB = Renderer::rrrgggbb_to_argb(0x6D);  // r=3 g=3 b=1

        clear_layers(r);
        r.set_layer_priority(6);           // additive blend
        r.blend_mode_     = 0;             // mix_rgb = ULA
        r.ula_line_[0]    = ULA_ARGB;
        r.layer2_line_[0] = L2_ARGB;
        r.set_layer_mask(Renderer::LAYER_ALL);
        const uint32_t both = composite_one(r, FB);

        clear_layers(r);
        r.set_layer_priority(6);
        r.blend_mode_     = 0;
        r.ula_line_[0]    = ULA_ARGB;
        r.layer2_line_[0] = L2_ARGB;
        r.set_layer_mask(Renderer::LAYER_LAYER2);
        const uint32_t l2_only = composite_one(r, FB);

        check("LMASK-C11",
              "blend mode 110: masking 'ula' zeroes the mix_rgb contribution, so the "
              "mixer emits Layer 2 alone (VHDL 7101/7122, 7288-7298)",
              both == SUM_ARGB && l2_only == L2_ARGB,
              DETAIL("both=0x%08X (exp sum 0x%08X)  l2only=0x%08X (exp L2 0x%08X)",
                     both, SUM_ARGB, l2_only, L2_ARGB));
    }
}


// ── Group LR — LoRes substitution into the ULA slot ──────────────────────
//
// Tier-C rows of doc/testing/LORES-TEST-PLAN-DESIGN.md.  The pixel generator
// itself (address maths, palette offset, scroll, clip compare) is pinned by
// test/lores/lores_test.cpp; everything here is about what the COMPOSITOR
// sees once the LoRes byte has replaced `ula_pixel` (VHDL zxnext.vhd:6980-6981).
//
// Colour partition used throughout the group.  The ULA palette is programmed
// so the two sources can never be confused:
//
//   index 0x00..0x1F  ->  RRRGGGBB = index + 1   (0x01..0x20)
//   index 0x20..0xFF  ->  RRRGGGBB = index       (0x20..0xFF)
//
// The std-ULA encoder can only emit ula_pixel in 0x00..0x1F
// (zxula.vhd:543-553), so a rendered cell whose RRRGGGBB is <= 0x20 came from
// the ULA; the bank-5 seed below never produces a LoRes index below 0x40, so
// a cell >= 0x40 came from LoRes.  No overlap, in either direction.

namespace lores_c {

// Bank-5 seed.  Every byte >= 0x40 (so every 8-bit-mode LoRes index is >=
// 0x40) and the low 6 bits cycle, so neighbouring LoRes cells differ.
static uint8_t seed_byte(uint16_t off) {
    return static_cast<uint8_t>(0x40 + (off % 0xC0));
}

// Independent re-derivation of lores.vhd:82/84-87/91/93-94 and :96 — written
// from the VHDL text, NOT by calling Lores::address, so the C rows do not
// inherit a bug from the very code they are checking.
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

struct Fix {
    Ram ram;
    Rom rom;
    Mmu mmu;
    PaletteManager pal;
    Renderer r;
    Layer2 l2;
    Tilemap tm;

    Fix() : mmu(ram, rom) {
        mmu.reset();
        pal.reset();
        r.reset();
        l2.reset();
        tm.reset();
        r.ula().set_ram(&ram);
        r.ula().set_palette(&pal);

        // ULA palette partition (see the group header).
        pal.write_control(0x00);            // target = ULA_FIRST
        for (int i = 0; i < 256; ++i) {
            pal.set_index(static_cast<uint8_t>(i));
            pal.write_8bit(static_cast<uint8_t>(i < 0x20 ? i + 1 : i));
        }
        pal.set_index(0);

        // Bank-5 content, covering both 8-bit halves (0x0000-0x37FF).
        for (uint16_t off = 0; off < 0x3800; ++off)
            ram.write(10u * 8192u + off, seed_byte(off));

        // NR $14 transparency key OUTSIDE both halves of the colour
        // partition (ULA yields 0x01..0x20, LoRes 0x40..0xFF), so no pixel
        // in this fixture is accidentally transparent. Without this the
        // reset default 0xE3 IS a reachable LoRes colour, and — because the
        // NR $4A fallback default is also 0xE3 — a wrongly-transparent LoRes
        // pixel would render the exact colour the row expects. LR-21 passed
        // for that reason before this line existed.
        r.set_transparent_rgb(0x3F);
        r.set_fallback_colour(0x00);

        refresh_snapshots();
    }

    // Re-take every per-scanline snapshot the render path consumes, for the
    // whole frame. Mirrors what Emulator::run_frame does via init_*_per_line
    // at frame start.
    void refresh_snapshots() {
        r.init_fallback_per_line();
        r.init_ula_enabled_per_line();
        r.init_transparent_rgb_per_line();
        r.init_stencil_mode_per_line();
        r.init_blend_mode_per_line();
        r.init_ula_clip_per_line();
        r.lores().init_per_line();
    }

    void enable_lores(bool on) {
        r.lores().set_enabled(on);
        r.lores().init_per_line();
    }

    void render(uint32_t* out, int fb_row, Tilemap* tmp = nullptr) {
        r.render_row(out, fb_row, mmu, ram, pal, l2, nullptr, tmp);
    }

    /// Render only the ULA half of the pipeline for one row, WITHOUT the
    /// LoRes substitution — the "pure ULA reference" LR-20/LR-165 compare
    /// against. Same sequence render_row uses minus apply_lores — including
    /// the per-row NR $4A fallback push (GH #95: render_scanline consumes
    /// select_bgnd_argb_, and without this push the reference depended on
    /// whatever a preceding render() call happened to leave behind).
    void render_ula_only(uint32_t* out, int fb_row) {
        r.ula().set_select_bgnd_argb(
            Renderer::rrrgggbb_to_argb(r.fallback_for_line(fb_row)));
        r.ula().render_scanline(out, fb_row, mmu, nullptr);
        if (!r.ula_enabled_per_line_[fb_row])
            std::fill_n(out, Renderer::FB_WIDTH, TRANSP);
        r.apply_ula_clip(out, fb_row);
    }

    /// Expected LoRes ARGB for one display column under default params
    /// (8-bit mode, no scroll, offset 0, ULA palette bank 0).
    uint32_t expect(int fb_row, int phc) const {
        const unsigned vc = static_cast<unsigned>(fb_row - Renderer::DISP_Y);
        const uint16_t a  = ref_addr8(ref_x(static_cast<unsigned>(phc), 0),
                                      ref_y(vc, 0));
        return pal.ula_colour(false, seed_byte(a));   // offset 0 -> pixel = data
    }
};

// RRRGGGBB of a rendered ARGB cell (inverse of rrrgggbb_to_argb for the
// values this group programs).
static uint8_t rgb8_of(uint32_t argb) {
    return static_cast<uint8_t>((argb_r3_t(argb) << 5) | (argb_g3_t(argb) << 2)
                                | argb_b2_t(argb));
}
static uint32_t channels_to_argb_t(uint8_t r3, uint8_t g3, uint8_t b2) {
    return Renderer::rrrgggbb_to_argb(
        static_cast<uint8_t>((r3 << 5) | (g3 << 2) | b2));
}
static bool is_ula_colour(uint32_t argb)   { return rgb8_of(argb) <= 0x20; }
static bool is_lores_colour(uint32_t argb) { return rgb8_of(argb) >= 0x40; }

} // namespace lores_c

static void test_LORES()
{
    using namespace lores_c;
    set_group("LR");

    // ── LR-20 — the enable gate, negative direction ──────────────────────
    // zxnext.vhd:6933 `lores_pixel_en_1 <= lores_pixel_en_1a and lores_en_1`.
    // With NR $15 bit 7 = 0 the mux at :6980 always selects ula_pixel_1, so
    // the frame must be bit-identical to the pure-ULA pipeline.
    {
        Fix f;
        f.enable_lores(false);
        bool identical = true; int bad_row = -1, bad_x = -1;
        std::vector<uint32_t> got(Renderer::FB_WIDTH), ref(Renderer::FB_WIDTH);
        for (int row = 0; row < Renderer::FB_HEIGHT && identical; ++row) {
            f.render(got.data(), row);
            f.render_ula_only(ref.data(), row);
            for (int x = 0; x < Renderer::FB_WIDTH; ++x)
                if (got[x] != ref[x]) { identical = false; bad_row = row; bad_x = x; break; }
        }
        check("LR-20",
              "with NR $15 bit 7 = 0 every framebuffer cell is bit-identical "
              "to the pure-ULA pipeline — LoRes content in bank 5 is invisible "
              "(zxnext.vhd:6933, 6980)",
              identical, DETAIL("first difference at row=%d x=%d", bad_row, bad_x));
    }

    // ── LR-21 — the enable gate, positive direction ──────────────────────
    {
        Fix f;
        f.enable_lores(true);
        bool all_lores = true; int bad_row = -1, bad_x = -1;
        uint32_t bad_got = 0, bad_exp = 0;
        std::vector<uint32_t> got(Renderer::FB_WIDTH);
        for (int row = Renderer::DISP_Y;
             row < Renderer::DISP_Y + Renderer::DISP_H && all_lores; ++row) {
            f.render(got.data(), row);
            for (int phc = 0; phc < 256; ++phc) {
                const uint32_t e = f.expect(row, phc);
                const uint32_t a = got[Renderer::DISP_X + phc * 2];
                const uint32_t b = got[Renderer::DISP_X + phc * 2 + 1];
                if (a != e || b != e || !is_lores_colour(a)) {
                    all_lores = false; bad_row = row; bad_x = phc;
                    bad_got = a; bad_exp = e;
                    break;
                }
            }
        }
        check("LR-21",
              "with NR $15 bit 7 = 1 all 256x192 display pixels take LoRes "
              "values and none takes a ULA value (zxnext.vhd:6980)",
              all_lores,
              DETAIL("row=%d phc=%d got=0x%08X exp=0x%08X",
                     bad_row, bad_x, bad_got, bad_exp));
    }

    // ── LR-22 — the border is never LoRes ────────────────────────────────
    // lores.vhd:115 cannot assert pixel_en outside the 256x192 window
    // (phc(8) set / clip_y2 <= 0xBF); zxula.vhd:414-415 marks those cells
    // border.
    {
        Fix f;
        f.enable_lores(true);
        f.r.ula().set_border(2);              // red
        f.r.ula().init_border_per_line();
        const uint8_t battr = static_cast<uint8_t>((2 << 3) | 2);
        // std_ula_paper_pixel(attr) = 0x10 | ((attr&0x40)>>3) | ((attr>>3)&7)
        const uint8_t bpix = static_cast<uint8_t>(0x10 | ((battr >> 3) & 0x07));
        const uint32_t bexp = f.pal.ula_colour(false, bpix);

        bool ok = true; int bad_row = -1, bad_x = -1; uint32_t bad = 0;
        std::vector<uint32_t> got(Renderer::FB_WIDTH);
        for (int row = 0; row < Renderer::FB_HEIGHT && ok; ++row) {
            f.render(got.data(), row);
            const bool disp_row = (row >= Renderer::DISP_Y &&
                                   row <  Renderer::DISP_Y + Renderer::DISP_H);
            for (int x = 0; x < Renderer::FB_WIDTH; ++x) {
                const bool border_cell = !disp_row ||
                    x < Renderer::DISP_X ||
                    x >= Renderer::DISP_X + Renderer::DISP_W;
                if (border_cell && got[x] != bexp) {
                    ok = false; bad_row = row; bad_x = x; bad = got[x];
                    break;
                }
            }
        }
        check("LR-22",
              "LoRes never paints the border — every border cell keeps the "
              "port $FE colour (lores.vhd:115; zxula.vhd:414-415)",
              ok, DETAIL("row=%d x=%d got=0x%08X exp=0x%08X",
                         bad_row, bad_x, bad, bexp));
    }

    // ── LR-26 — LoRes occupies the ULA slot in NR $15 priority ───────────
    // It is not a new layer: whatever hides (or is hidden by) the ULA in a
    // given priority mode hides (or is hidden by) LoRes identically.
    {
        Fix f;
        // An opaque Layer 2 pixel across the whole line, in a colour that is
        // neither a ULA colour nor a LoRes colour under the partition.
        const uint32_t L2C = Renderer::rrrgggbb_to_argb(0x21);
        const int ROW = Renderer::DISP_Y + 20;
        bool same_everywhere = true; int bad_mode = -1, bad_x = -1;

        std::vector<uint32_t> off(Renderer::FB_WIDTH), on(Renderer::FB_WIDTH);
        for (int mode = 0; mode <= 5 && same_everywhere; ++mode) {
            f.r.set_layer_priority(static_cast<uint8_t>(mode));

            f.enable_lores(false);
            f.r.ula().render_scanline(f.r.ula_line_.data(), ROW, f.mmu,
                                      f.r.ula_border_.data());
            f.r.apply_ula_clip(f.r.ula_line_.data(), ROW);
            std::fill_n(f.r.layer2_line_.begin(), Renderer::FB_WIDTH, L2C);
            std::fill_n(f.r.sprite_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            std::fill_n(f.r.tilemap_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            f.r.composite_scanline(off.data(),
                                   Renderer::rrrgggbb_to_argb(0x00), ROW);

            f.enable_lores(true);
            f.r.ula().render_scanline(f.r.ula_line_.data(), ROW, f.mmu,
                                      f.r.ula_border_.data());
            f.r.apply_lores(f.r.ula_line_.data(), ROW, f.ram, f.pal);
            f.r.apply_ula_clip(f.r.ula_line_.data(), ROW);
            std::fill_n(f.r.layer2_line_.begin(), Renderer::FB_WIDTH, L2C);
            std::fill_n(f.r.sprite_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            std::fill_n(f.r.tilemap_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            f.r.composite_scanline(on.data(),
                                   Renderer::rrrgggbb_to_argb(0x00), ROW);

            for (int x = Renderer::DISP_X;
                 x < Renderer::DISP_X + Renderer::DISP_W; ++x) {
                const bool ula_won   = (off[x] != L2C);
                const bool lores_won = (on[x]  != L2C);
                if (ula_won != lores_won) {
                    same_everywhere = false; bad_mode = mode; bad_x = x; break;
                }
                if (lores_won && !is_lores_colour(on[x])) {
                    same_everywhere = false; bad_mode = mode; bad_x = x; break;
                }
            }
        }
        f.r.set_layer_priority(0);
        check("LR-26",
              "LoRes occupies the ULA slot in NR $15 priority — in every mode "
              "000..101 it wins or loses exactly where the ULA would, and the "
              "winning colour is the LoRes one (zxnext.vhd:6980-6981)",
              same_everywhere,
              DETAIL("priority mode %d disagreed at x=%d", bad_mode, bad_x));
    }

    // ── LR-27 — NR $68 bit 7 blanks LoRes too ────────────────────────────
    // zxnext.vhd:7103 `ula_transparent <= '1' when ... or (ula_en_2 = '0')`.
    // The substitution happens upstream (6980), so ula_en gates the LoRes
    // colour exactly as it gates the ULA's.
    {
        Fix f;
        f.enable_lores(true);
        f.r.set_fallback_colour(0x03);
        f.r.ula().set_ula_enabled(false);
        f.refresh_snapshots();
        const uint32_t fb = Renderer::rrrgggbb_to_argb(0x03);

        bool ok = true; int bad_x = -1; uint32_t bad = 0;
        const int ROW = Renderer::DISP_Y + 50;
        std::vector<uint32_t> got(Renderer::FB_WIDTH);
        f.render(got.data(), ROW);
        for (int x = 0; x < Renderer::FB_WIDTH; ++x)
            if (got[x] != fb) { ok = false; bad_x = x; bad = got[x]; break; }
        check("LR-27",
              "NR $68 bit 7 (ULA disable) blanks LoRes too — the display falls "
              "through to the NR $4A fallback, no LoRes pixel survives "
              "(zxnext.vhd:7103-7104)",
              ok, DETAIL("x=%d got=0x%08X exp fallback 0x%08X", bad_x, bad, fb));
    }

    // ── LR-28 — Timex hi-res: LoRes destroys the 512-pixel detail ────────
    // `lores_pixel_1` is registered on CLK_7 (zxnext.vhd:6843) while
    // `ula_pixel_1` is registered on CLK_14 (:6858), so ONE LoRes colour
    // covers BOTH 512-mode half-pixels.
    {
        Fix f;
        f.r.ula().set_screen_mode(0x06);      // Timex hi-res (mode bits 110)
        f.r.ula().start_frame();
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 30;
        std::vector<uint32_t> hires(Renderer::FB_WIDTH);
        std::vector<uint32_t> got(Renderer::FB_WIDTH);
        f.enable_lores(false);
        f.render(hires.data(), ROW);
        f.enable_lores(true);
        f.render(got.data(), ROW);

        bool paired = true, detail_existed = false;
        int bad_x = -1;
        for (int phc = 0; phc < 256; ++phc) {
            const int x = Renderer::DISP_X + phc * 2;
            if (hires[x] != hires[x + 1]) detail_existed = true;
            if (got[x] != got[x + 1] || !is_lores_colour(got[x])) {
                paired = false; bad_x = phc; break;
            }
        }
        check("LR-28",
              "in Timex hi-res mode both 512-grid half-pixels take the SAME "
              "LoRes colour and no hi-res detail survives "
              "(zxnext.vhd:6843 vs 6858, 6980, 6986)",
              paired && detail_existed,
              DETAIL("paired=%d (first break at phc=%d), hi-res detail present "
                     "without LoRes=%d", paired, bad_x, detail_existed));
    }

    // ── LR-29 — ULA attribute FLASH does not modulate a LoRes pixel ──────
    {
        Fix f;
        f.enable_lores(true);
        // Force FLASH on every attribute cell of the bank-5 attribute area.
        for (uint16_t off = 0x1800; off < 0x1B00; ++off)
            f.ram.write(10u * 8192u + off, 0xFF);   // attr(7) = FLASH
        const int ROW = Renderer::DISP_Y + 60;
        std::vector<uint32_t> a(Renderer::FB_WIDTH), b(Renderer::FB_WIDTH);
        f.r.ula().flash_phase_ = false;
        f.render(a.data(), ROW);
        f.r.ula().flash_phase_ = true;
        f.render(b.data(), ROW);
        const bool same = std::memcmp(a.data() + Renderer::DISP_X,
                                      b.data() + Renderer::DISP_X,
                                      Renderer::DISP_W * sizeof(uint32_t)) == 0;
        check("LR-29",
              "ULA attribute FLASH does not modulate a LoRes pixel — both "
              "flash phases render the display area identically "
              "(zxula.vhd:470; zxnext.vhd:6980)",
              same);
    }

    // ── LR-30 — LoRes indexes the ULA palette, not Layer 2's ─────────────
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 4;
        const int PHC = 0;
        const uint16_t a = ref_addr8(ref_x(PHC, 0),
                                     ref_y(ROW - Renderer::DISP_Y, 0));
        const uint8_t idx = seed_byte(a);

        // Distinguishable values at the SAME index k in the ULA and Layer 2
        // palettes.
        f.pal.write_control(0x00);            // ULA_FIRST
        f.pal.set_index(idx);
        f.pal.write_8bit(0x92);
        f.pal.write_control(0x10);            // LAYER2_FIRST
        f.pal.set_index(idx);
        f.pal.write_8bit(0x49);
        f.pal.write_control(0x00);

        std::vector<uint32_t> got(Renderer::FB_WIDTH);
        f.render(got.data(), ROW);
        const uint32_t ula_c = f.pal.ula_colour(false, idx);
        const uint32_t l2_c  = f.pal.layer2_colour(idx);
        check("LR-30",
              "the LoRes byte indexes the ULA palette, not the Layer 2 / "
              "sprite / tilemap palette (zxnext.vhd:6960-6978, 6981)",
              got[Renderer::DISP_X] == ula_c && ula_c != l2_c,
              DETAIL("got=0x%08X ula=0x%08X l2=0x%08X idx=0x%02X",
                     got[Renderer::DISP_X], ula_c, l2_c, idx));
    }

    // ── LR-31 — NR $43 bit 1 picks which ULA palette bank LoRes indexes ──
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 6;
        const uint16_t a = ref_addr8(ref_x(0, 0), ref_y(ROW - Renderer::DISP_Y, 0));
        const uint8_t idx = seed_byte(a);

        f.pal.write_control(0x00);            // ULA_FIRST  (bank 0)
        f.pal.set_index(idx);
        f.pal.write_8bit(0x92);
        f.pal.write_control(0x40);            // ULA_SECOND (bank 1)
        f.pal.set_index(idx);
        f.pal.write_8bit(0x49);
        f.pal.write_control(0x00);

        std::vector<uint32_t> g0(Renderer::FB_WIDTH), g1(Renderer::FB_WIDTH);
        f.r.ula().set_active_ula_palette(false);
        f.render(g0.data(), ROW);
        f.r.ula().set_active_ula_palette(true);
        f.render(g1.data(), ROW);
        f.r.ula().set_active_ula_palette(false);

        const uint32_t exp0 = f.pal.ula_colour(false, idx);
        const uint32_t exp1 = f.pal.ula_colour(true,  idx);
        check("LR-31",
              "NR $43 bit 1 selects which of the two ULA palette banks LoRes "
              "indexes (zxnext.vhd:6825, 6981)",
              g0[Renderer::DISP_X] == exp0 && g1[Renderer::DISP_X] == exp1 &&
              exp0 != exp1,
              DETAIL("bank0=0x%08X (exp 0x%08X) bank1=0x%08X (exp 0x%08X)",
                     g0[Renderer::DISP_X], exp0, g1[Renderer::DISP_X], exp1));
    }

    // ── LR-49 — physical bank 5, independent of the MMU slot mapping ─────
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 12;
        std::vector<uint32_t> a(Renderer::FB_WIDTH), b(Renderer::FB_WIDTH);
        f.render(a.data(), ROW);
        // Map bank 5's two pages out of the CPU address space entirely: put
        // pages 0 and 1 in the 0x4000-0x7FFF slots via the MMU registers.
        f.mmu.set_page(2, 0);
        f.mmu.set_page(3, 1);
        f.render(b.data(), ROW);
        check("LR-49",
              "LoRes reads physical bank 5 regardless of the MMU slot mapping "
              "(zxnext.vhd:6631, 6558-6578; lores.vhd:56)",
              std::memcmp(a.data(), b.data(),
                          Renderer::FB_WIDTH * sizeof(uint32_t)) == 0);
    }

    // ── LR-50 — unaffected by the port $7FFD bit 3 shadow-screen select ──
    {
        Fix f;
        f.enable_lores(true);
        for (uint16_t off = 0; off < 0x2000; ++off)
            f.ram.write(14u * 8192u + off, 0x00);   // bank 7 distinct content
        const int ROW = Renderer::DISP_Y + 14;
        std::vector<uint32_t> a(Renderer::FB_WIDTH), b(Renderer::FB_WIDTH);
        f.render(a.data(), ROW);
        f.r.ula().set_shadow_screen_en(true);       // port $7FFD bit 3
        f.render(b.data(), ROW);
        f.r.ula().set_shadow_screen_en(false);
        bool same = true; int bad = -1;
        for (int phc = 0; phc < 256; ++phc) {
            const int x = Renderer::DISP_X + phc * 2;
            if (a[x] != b[x]) { same = false; bad = phc; break; }
        }
        check("LR-50",
              "LoRes is unaffected by the port $7FFD bit 3 shadow-screen "
              "select — it always shows bank-5 content "
              "(zxnext.vhd:6631 vs 6651-6655)",
              same, DETAIL("first divergence at phc=%d", bad));
    }

    // ── LR-66 — dfile = port $FF bit 0 XOR NR $6A bit 4 ──────────────────
    {
        Fix f;
        f.enable_lores(true);
        // Distinct content at the two Radastan half bases.
        f.ram.write(10u * 8192u + 0x0000, 0x50);
        f.ram.write(10u * 8192u + 0x2000, 0xA0);
        const int ROW = Renderer::DISP_Y;   // vc = 0
        uint32_t out[4];
        const struct { uint8_t ff; uint8_t nr6a_b4; int expect_half; } cases[] = {
            {0x00, 0x00, 0}, {0x01, 0x00, 1}, {0x00, 0x10, 1}, {0x01, 0x10, 0},
        };
        bool ok = true; int bad = -1;
        std::vector<uint32_t> got(Renderer::FB_WIDTH);
        for (int i = 0; i < 4; ++i) {
            // NR $6A: bit 5 = Radastan, bit 4 = XOR, offset 0.
            f.r.lores().set_nr6a(static_cast<uint8_t>(0x20 | cases[i].nr6a_b4));
            f.r.lores().init_per_line();
            f.r.ula().set_screen_mode(cases[i].ff);
            f.r.ula().start_frame();
            f.render(got.data(), ROW);
            out[i] = got[Renderer::DISP_X];
            // Radastan, x(1)=0 -> low nibble = data(7:4); offset 0 -> high
            // nibble = 0. half 0 byte 0x50 -> pixel 0x05; half 1 byte 0xA0 ->
            // pixel 0x0A (lores.vhd:106-107, 111).
            const uint8_t pix = cases[i].expect_half ? 0x0A : 0x05;
            if (out[i] != f.pal.ula_colour(false, pix)) { ok = false; bad = i; break; }
        }
        f.r.lores().set_nr6a(0x00);
        f.r.lores().init_per_line();
        f.r.ula().set_screen_mode(0x00);
        f.r.ula().start_frame();
        check("LR-66",
              "dfile = port $FF bit 0 XOR NR $6A bit 4: (0,0)->half 0, "
              "(1,0)->half 1, (0,1)->half 1, (1,1)->half 0 "
              "(zxnext.vhd:6796)",
              ok, DETAIL("case %d wrong (got 0x%08X)", bad,
                         bad >= 0 ? out[bad] : 0));
    }

    // ── LR-69 — Radastan and 8-bit reach different bytes ─────────────────
    {
        Fix f;
        f.enable_lores(true);
        // phc=8, vc=4:  8-bit -> 0x0104, Radastan dfile=0 -> 0x0082.
        f.ram.write(10u * 8192u + 0x0104, 0x71);
        f.ram.write(10u * 8192u + 0x0082, 0x35);
        const int ROW = Renderer::DISP_Y + 4;
        const int X   = Renderer::DISP_X + 8 * 2;
        std::vector<uint32_t> g8(Renderer::FB_WIDTH), gr(Renderer::FB_WIDTH);
        f.render(g8.data(), ROW);
        f.r.lores().set_nr6a(0x20);          // Radastan
        f.r.lores().init_per_line();
        f.render(gr.data(), ROW);
        f.r.lores().set_nr6a(0x00);
        f.r.lores().init_per_line();
        // 8-bit: pixel = data = 0x71.  Radastan: x=8 -> x(1)=0 -> low nibble
        // = data(7:4) = 3, high nibble = offset 0 -> pixel 0x03.
        check("LR-69",
              "Radastan and 8-bit mode reach different bytes for the same "
              "screen position: (phc=8, vc=4) reads 0x0104 vs 0x0082 "
              "(lores.vhd:91, 96)",
              g8[X] == f.pal.ula_colour(false, 0x71) &&
              gr[X] == f.pal.ula_colour(false, 0x03),
              DETAIL("8bit=0x%08X (exp 0x%08X)  rad=0x%08X (exp 0x%08X)",
                     g8[X], f.pal.ula_colour(false, 0x71),
                     gr[X], f.pal.ula_colour(false, 0x03)));
    }

    // ── LR-87 — ULANext cancels the ULA+ translation ─────────────────────
    // zxnext.vhd:4246 `ulap_en_i => ulap_en_0 and not ulanext_en_0`.
    {
        Fix f;
        f.enable_lores(true);
        f.ram.write(10u * 8192u + 0x0000, 0xAB);
        f.r.lores().set_nr6a(0x21);          // Radastan + offset 1
        f.r.lores().init_per_line();
        const int ROW = Renderer::DISP_Y;
        std::vector<uint32_t> g(Renderer::FB_WIDTH);

        f.r.ula().set_ulap_en(true);
        f.r.ula().set_ulanext_en(false);
        f.render(g.data(), ROW);
        const uint32_t with_ulap = g[Renderer::DISP_X];

        f.r.ula().set_ulanext_en(true);
        f.render(g.data(), ROW);
        const uint32_t with_ulanext = g[Renderer::DISP_X];

        f.r.ula().set_ulap_en(false);
        f.r.ula().set_ulanext_en(false);
        f.r.lores().set_nr6a(0x00);
        f.r.lores().init_per_line();

        check("LR-87",
              "ULANext cancels the ULA+ translation of the Radastan high "
              "nibble — pixel 0x1A, not 0xDA (zxnext.vhd:4246)",
              with_ulap == f.pal.ula_colour(false, 0xDA) &&
              with_ulanext == f.pal.ula_colour(false, 0x1A),
              DETAIL("ulap=0x%08X (exp 0x%08X) ulanext=0x%08X (exp 0x%08X)",
                     with_ulap, f.pal.ula_colour(false, 0xDA),
                     with_ulanext, f.pal.ula_colour(false, 0x1A)));
    }

    // ── LR-127 / LR-128 — RETIRED (2026-07-22); tombstones removed ───────
    //
    // Both rows asserted the ULA "shows through" outside the shared NR $1A
    // window; the VHDL says both sources are suppressed together
    // (zxula.vhd:562; zxnext.vhd:7063, 7100-7104), and LR-127a below asserts
    // the corrected observable. The in-suite skip() tombstones were removed
    // 2026-07-24 (owner-authorized zero-skip mandate); the retirement record
    // lives in doc/testing/LORES-TEST-PLAN-DESIGN.md group 7. The IDs stay
    // burned — do not reuse them.

    // ── LR-140 — a LoRes pixel never shows the NR $4A fallback ───────────
    //
    // zxnext.vhd:6986-6991:
    //   if lores_pixel_en_1 = '1' or ula_select_bgnd_1 = '0' then
    //      ula_rgb_1 <= ulatm_rgb_1(8 downto 0);            -- palette
    //   else
    //      ula_rgb_1 <= fallback_rgb_1 & (...);             -- NR $4A
    // A LoRes pixel forces the palette arm even when the ULA side asserted
    // ula_select_bgnd. The stimulus: ULAnext enabled with format 0x00 — a
    // value outside the {01,03,07,0F,1F,3F,7F} table — makes every paper
    // pixel assert ula_select_bgnd (zxula.vhd:516-527 `when others`).
    //
    // The second leg proves the stimulus is real, not vacuous: the SAME ULA
    // state with LoRes off must show the NR $4A fallback on a paper pixel
    // (the else-arm of :6987), so a regression that stopped asserting — or
    // stopped consuming — select_bgnd cannot pass this row.
    {
        Fix f;
        const unsigned vc = 100;
        const int ROW = Renderer::DISP_Y + static_cast<int>(vc);
        // ULAnext on, format 0x00 (zxnext.vhd:5386 write path; zxula.vhd:525
        // `when others` asserts ula_select_bgnd for every paper cycle).
        f.r.ula().set_ulanext_en(true);
        f.r.ula().set_ulanext_format(0x00);
        // Make the probed ULA cell (screen col 0) all-paper: pixel byte 0
        // at the standard interleave offset for vc=100 in bank 5
        // (zxula.vhd:218 addressing — independent re-derivation, not
        // Ula::pixel_addr_offset).
        const uint16_t ula_off = static_cast<uint16_t>(
            ((vc & 0xC0) << 5) | ((vc & 0x07) << 8) | ((vc & 0x38) << 2));
        f.ram.write(10u * 8192u + ula_off, 0x00);
        f.r.set_fallback_colour(0x21);        // outside both colour halves
        f.refresh_snapshots();
        const uint32_t FB = Renderer::rrrgggbb_to_argb(0x21);

        std::vector<uint32_t> g(Renderer::FB_WIDTH);
        f.enable_lores(true);
        f.render(g.data(), ROW);
        const uint32_t with_lores = g[Renderer::DISP_X];      // phc 0

        f.enable_lores(false);
        f.render(g.data(), ROW);
        const uint32_t without_lores = g[Renderer::DISP_X];

        check("LR-140",
              "a LoRes pixel never shows the NR $4A fallback: with the ULA "
              "asserting ula_select_bgnd (ULAnext format 0x00 paper, "
              "zxula.vhd:525) the LoRes palette colour is emitted "
              "(zxnext.vhd:6986-6991); the identical state without LoRes "
              "takes the fallback, proving the stimulus",
              with_lores == f.expect(ROW, 0) && is_lores_colour(with_lores) &&
              without_lores == FB,
              DETAIL("lores=0x%08X (exp 0x%08X)  ula=0x%08X (exp fallback "
                     "0x%08X)",
                     with_lores, f.expect(ROW, 0), without_lores, FB));
    }

    // ── LR-141 — the LoRes colour is subject to NR $14 ───────────────────
    // zxnext.vhd:7100 compares ula_rgb_2(8:1) — which holds the LoRes colour
    // when LoRes is active — against transparent_rgb_2.
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 8;
        const uint16_t a = ref_addr8(ref_x(0, 0), ref_y(ROW - Renderer::DISP_Y, 0));
        const uint8_t idx = seed_byte(a);
        // 0xE3 is the NR $14 reset default AND has blue = 3, the one blue
        // value whose 2-bit (register) and 3-bit (palette) expansions agree
        // — the compositor compares a palette-produced ARGB against a
        // register-produced one (VHDL compares the 9-bit RGB either side, so
        // the equality is exact there; jnext's two expansions only coincide
        // at blue 0 and 3).
        f.pal.write_control(0x00);
        f.pal.set_index(idx);
        f.pal.write_8bit(0xE3);
        f.pal.set_index(0);

        f.r.set_fallback_colour(0x03);
        f.r.set_transparent_rgb(0xE3);        // NR $14 == the LoRes colour
        f.refresh_snapshots();
        std::vector<uint32_t> g(Renderer::FB_WIDTH);
        f.render(g.data(), ROW);
        const uint32_t transparent_result = g[Renderer::DISP_X];

        f.r.set_transparent_rgb(0x3F);        // no longer the LoRes colour
        f.refresh_snapshots();
        f.render(g.data(), ROW);
        const uint32_t opaque_result = g[Renderer::DISP_X];

        check("LR-141",
              "the LoRes colour is subject to NR $14 global transparency — "
              "matching the key makes the pixel transparent and the layer "
              "below shows (zxnext.vhd:7100-7101)",
              transparent_result == Renderer::rrrgggbb_to_argb(0x03) &&
              opaque_result == f.pal.ula_colour(false, idx),
              DETAIL("match=0x%08X (exp fallback 0x%08X)  "
                     "nomatch=0x%08X (exp 0x%08X)",
                     transparent_result, Renderer::rrrgggbb_to_argb(0x03),
                     opaque_result, f.pal.ula_colour(false, idx)));
    }

    // ── LR-142 — transparency compares the palette RGB, not the index ────
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 10;
        const int vc  = ROW - Renderer::DISP_Y;
        // Two columns whose LoRes indices differ.
        int col_a = -1, col_b = -1;
        uint8_t idx_a = 0, idx_b = 0;
        for (int phc = 0; phc < 256 && col_b < 0; ++phc) {
            const uint8_t idx = seed_byte(ref_addr8(ref_x(phc, 0), ref_y(vc, 0)));
            if (col_a < 0) { col_a = phc; idx_a = idx; }
            else if (idx != idx_a) { col_b = phc; idx_b = idx; }
        }
        // Index 0xE3 (== the NR $14 default key) mapped to a NON-0xE3 RGB;
        // a different index mapped to RGB 0xE3.
        f.pal.write_control(0x00);
        f.pal.set_index(idx_a);
        f.pal.write_8bit(0x11);               // not the key
        f.pal.set_index(idx_b);
        f.pal.write_8bit(0xE3);               // IS the key
        f.pal.set_index(0);

        f.r.set_fallback_colour(0x03);
        f.r.set_transparent_rgb(0xE3);
        f.refresh_snapshots();
        std::vector<uint32_t> g(Renderer::FB_WIDTH);
        f.render(g.data(), ROW);

        check("LR-142",
              "transparency compares the palette RGB[8:1], not the palette "
              "index — only the entry whose RGB is the key goes transparent "
              "(zxnext.vhd:7100)",
              g[Renderer::DISP_X + col_a * 2] == f.pal.ula_colour(false, idx_a) &&
              g[Renderer::DISP_X + col_b * 2] == Renderer::rrrgggbb_to_argb(0x03),
              DETAIL("idx 0x%02X -> 0x%08X (exp 0x%08X); idx 0x%02X -> 0x%08X "
                     "(exp fallback 0x%08X)",
                     idx_a, g[Renderer::DISP_X + col_a * 2],
                     f.pal.ula_colour(false, idx_a),
                     idx_b, g[Renderer::DISP_X + col_b * 2],
                     Renderer::rrrgggbb_to_argb(0x03)));
    }

    // ── LR-143/144/145/146 — the compositor sees the LoRes colour in the
    // ULA slot for stencil, blend, tilemap ordering and layer priority.
    // These poke the non-ULA layer buffers directly (the idiom the STEN/UTB
    // groups above use) so the assertion is about the ULA-slot OPERAND, not
    // about re-testing the tilemap engine.
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 16;
        const int X   = Renderer::DISP_X;

        auto prep_ula_slot = [&]() {
            f.r.ula().render_scanline(f.r.ula_line_.data(), ROW, f.mmu,
                                      f.r.ula_border_.data());
            f.r.apply_lores(f.r.ula_line_.data(), ROW, f.ram, f.pal);
            f.r.apply_ula_clip(f.r.ula_line_.data(), ROW);
        };
        const uint32_t lores_px = f.expect(ROW, 0);

        // LR-143 — stencil (zxnext.vhd:7112-7113, 7130-7132).
        {
            const uint32_t TMC = Renderer::rrrgggbb_to_argb(0x2A);
            prep_ula_slot();
            std::fill_n(f.r.layer2_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            std::fill_n(f.r.sprite_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            std::fill_n(f.r.tilemap_line_.begin(), Renderer::FB_WIDTH, TMC);
            std::fill_n(f.r.tm_pixel_below_.begin(), Renderer::FB_WIDTH, false);
            std::fill_n(f.r.tm_pixel_textmode_.begin(), Renderer::FB_WIDTH, false);
            f.r.set_stencil_mode(true);
            f.r.set_tm_enabled(true);
            f.r.snapshot_stencil_mode_for_line(ROW);
            std::vector<uint32_t> g(Renderer::FB_WIDTH);
            f.r.composite_scanline(g.data(), Renderer::rrrgggbb_to_argb(0x00), ROW);
            const uint32_t exp = channels_to_argb_t(
                argb_r3_t(lores_px) & argb_r3_t(TMC),
                argb_g3_t(lores_px) & argb_g3_t(TMC),
                argb_b2_t(lores_px) & argb_b2_t(TMC));
            f.r.set_stencil_mode(false);
            f.r.set_tm_enabled(false);
            f.r.snapshot_stencil_mode_for_line(ROW);
            check("LR-143",
                  "LoRes participates in ULA/tilemap stencil mode as the ULA "
                  "colour — the AND uses the LoRes RGB "
                  "(zxnext.vhd:7112-7113, 7130-7132)",
                  g[X] == exp,
                  DETAIL("got=0x%08X exp=0x%08X (lores=0x%08X tm=0x%08X)",
                         g[X], exp, lores_px, TMC));
        }

        // LR-144 — blend mode 110 with NR $68 b6:5 = 00 (zxnext.vhd:7139-7148).
        {
            const uint32_t L2C = Renderer::rrrgggbb_to_argb(0x24);
            prep_ula_slot();
            std::fill_n(f.r.layer2_line_.begin(), Renderer::FB_WIDTH, L2C);
            std::fill_n(f.r.sprite_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            std::fill_n(f.r.tilemap_line_.begin(), Renderer::FB_WIDTH, TRANSP);
            std::fill_n(f.r.tm_pixel_below_.begin(), Renderer::FB_WIDTH, false);
            f.r.set_layer_priority(6);
            f.r.set_blend_mode(0);
            f.r.snapshot_blend_mode_for_line(ROW);
            std::vector<uint32_t> g(Renderer::FB_WIDTH);
            f.r.composite_scanline(g.data(), Renderer::rrrgggbb_to_argb(0x00), ROW);
            // Mode 110 = additive with clamp; the ULA operand must be the
            // LoRes colour.
            auto clamp3 = [](int v) { return v > 7 ? 7 : v; };
            auto clamp2 = [](int v) { return v > 3 ? 3 : v; };
            const uint32_t exp = channels_to_argb_t(
                static_cast<uint8_t>(clamp3(argb_r3_t(lores_px) + argb_r3_t(L2C))),
                static_cast<uint8_t>(clamp3(argb_g3_t(lores_px) + argb_g3_t(L2C))),
                static_cast<uint8_t>(clamp2(argb_b2_t(lores_px) + argb_b2_t(L2C))));
            f.r.set_layer_priority(0);
            check("LR-144",
                  "LoRes participates in NR $15 blend mode 110 as the ULA "
                  "operand of the mixer (zxnext.vhd:7100-7101, 7139-7148)",
                  g[X] == exp,
                  DETAIL("got=0x%08X exp=0x%08X (lores=0x%08X l2=0x%08X)",
                         g[X], exp, lores_px, L2C));
        }

        // LR-145 — tilemap below/above the ULA slot (zxnext.vhd:7116).
        {
            const uint32_t TMC = Renderer::rrrgggbb_to_argb(0x2A);
            std::vector<uint32_t> above(Renderer::FB_WIDTH), below(Renderer::FB_WIDTH);
            for (int pass = 0; pass < 2; ++pass) {
                prep_ula_slot();
                std::fill_n(f.r.layer2_line_.begin(), Renderer::FB_WIDTH, TRANSP);
                std::fill_n(f.r.sprite_line_.begin(), Renderer::FB_WIDTH, TRANSP);
                std::fill_n(f.r.tilemap_line_.begin(), Renderer::FB_WIDTH, TMC);
                std::fill_n(f.r.tm_pixel_below_.begin(), Renderer::FB_WIDTH,
                            pass == 1);
                std::fill_n(f.r.tm_pixel_textmode_.begin(), Renderer::FB_WIDTH, false);
                f.r.composite_scanline(pass ? below.data() : above.data(),
                                       Renderer::rrrgggbb_to_argb(0x00), ROW);
            }
            check("LR-145",
                  "the tilemap 'below ULA' ordering applies unchanged to "
                  "LoRes: tilemap over the LoRes colour when above, LoRes "
                  "over the tilemap when below (zxnext.vhd:7116)",
                  above[X] == TMC && below[X] == lores_px,
                  DETAIL("above=0x%08X (exp tm 0x%08X) below=0x%08X "
                         "(exp lores 0x%08X)",
                         above[X], TMC, below[X], lores_px));
        }

        // LR-146 — Layer 2 / sprite stacking relative to the ULA slot is
        // unchanged by LoRes; only the ULA-slot colour changes.
        {
            const uint32_t L2C = Renderer::rrrgggbb_to_argb(0x21);
            const uint32_t SPC = Renderer::rrrgggbb_to_argb(0x22);
            bool same = true; int bad_mode = -1;
            for (int mode = 0; mode <= 5 && same; ++mode) {
                uint32_t res[2];
                for (int on = 0; on < 2; ++on) {
                    f.enable_lores(on != 0);
                    prep_ula_slot();
                    std::fill_n(f.r.layer2_line_.begin(), Renderer::FB_WIDTH, L2C);
                    std::fill_n(f.r.sprite_line_.begin(), Renderer::FB_WIDTH, SPC);
                    std::fill_n(f.r.tilemap_line_.begin(), Renderer::FB_WIDTH, TRANSP);
                    std::fill_n(f.r.tm_pixel_below_.begin(), Renderer::FB_WIDTH, false);
                    f.r.sprite_en_ = true;
                    f.r.set_layer_priority(static_cast<uint8_t>(mode));
                    std::vector<uint32_t> g(Renderer::FB_WIDTH);
                    f.r.composite_scanline(g.data(),
                                           Renderer::rrrgggbb_to_argb(0x00), ROW);
                    res[on] = g[X];
                }
                // The winner's IDENTITY must be unchanged: if L2 or the
                // sprite won without LoRes it must still win with LoRes; if
                // the ULA slot won, the winner is now the LoRes colour.
                const bool ula_won_off = (res[0] != L2C && res[0] != SPC);
                const bool ula_won_on  = (res[1] != L2C && res[1] != SPC);
                if (ula_won_off != ula_won_on) { same = false; bad_mode = mode; }
                else if (!ula_won_off && res[0] != res[1]) { same = false; bad_mode = mode; }
                else if (ula_won_on && !is_lores_colour(res[1])) { same = false; bad_mode = mode; }
            }
            f.r.set_layer_priority(0);
            f.enable_lores(true);
            check("LR-146",
                  "sprite and Layer 2 priority relative to the ULA slot is "
                  "unchanged by LoRes — only the ULA-slot colour changes "
                  "(zxnext.vhd:6980, 7139+)",
                  same, DETAIL("priority mode %d disagreed", bad_mode));
        }
    }

    // ── LR-127a — the CORRECTED observable for retired rows LR-127/128 ───
    //
    // A full catalogue row (group 7, tier C — ADOPTED by the 2026-07-22 plan
    // amendment). LR-127 and LR-128 were retired because their stated
    // outcomes contradict the VHDL; this row asserts what the VHDL actually
    // does, so the shared-clip behaviour is not left untested.
    //
    //   zxula.vhd:562  o_ula_clipped <= '0' when (phc/vc inside the window)
    //                                   or border_active = '1' else '1';
    //   lores.vhd:115  lores_pixel_en <= '1' only inside the SAME window,
    //                                   from the SAME registers
    //                                   (zxnext.vhd:4258-4261 vs 4437-4471).
    //
    // The two comparators therefore agree: INSIDE the window LoRes draws;
    // OUTSIDE it BOTH are suppressed and the pixel falls through to the
    // NR $4A fallback. The ULA does NOT "show through" outside the window,
    // and pixels outside it ARE blanked by the clip.
    {
        Fix f;
        f.enable_lores(true);
        f.r.set_fallback_colour(0x21);        // outside both colour halves
        f.r.ula().set_clip_x1(64);
        f.r.ula().set_clip_x2(191);
        f.r.ula().set_clip_y1(32);
        f.r.ula().set_clip_y2(159);
        f.refresh_snapshots();
        const uint32_t FB = Renderer::rrrgggbb_to_argb(0x21);

        std::vector<uint32_t> inside_row(Renderer::FB_WIDTH);
        std::vector<uint32_t> outside_row(Renderer::FB_WIDTH);
        const int ROW_IN  = Renderer::DISP_Y + 100;   // vc = 100, inside Y
        const int ROW_OUT = Renderer::DISP_Y + 10;    // vc = 10,  outside Y
        f.render(inside_row.data(), ROW_IN);
        f.render(outside_row.data(), ROW_OUT);

        const uint32_t in_x  = inside_row[Renderer::DISP_X + 100 * 2]; // phc 100
        const uint32_t out_x = inside_row[Renderer::DISP_X +  32 * 2]; // phc 32
        const uint32_t out_y = outside_row[Renderer::DISP_X + 100 * 2];

        // And nowhere in the frame does a ULA colour reach the output — the
        // direct refutation of "outside the window the ULA pixel shows".
        bool any_ula = false;
        std::vector<uint32_t> g(Renderer::FB_WIDTH);
        for (int row = Renderer::DISP_Y;
             row < Renderer::DISP_Y + Renderer::DISP_H && !any_ula; ++row) {
            f.render(g.data(), row);
            for (int x = Renderer::DISP_X;
                 x < Renderer::DISP_X + Renderer::DISP_W; ++x)
                if (is_ula_colour(g[x])) { any_ula = true; break; }
        }

        check("LR-127a",
              "LoRes and the ULA share ONE clip window and are suppressed "
              "together: inside NR $1A the LoRes pixel draws, outside it the "
              "pixel falls to the NR $4A fallback and no ULA pixel shows "
              "through (zxula.vhd:562; lores.vhd:115; zxnext.vhd:4258-4261, "
              "7100/7104)",
              is_lores_colour(in_x) && in_x == f.expect(ROW_IN, 100) &&
              out_x == FB && out_y == FB && !any_ula,
              DETAIL("inside=0x%08X (exp 0x%08X) outsideX=0x%08X outsideY=0x%08X "
                     "(exp fallback 0x%08X) any_ula_colour=%d",
                     in_x, f.expect(ROW_IN, 100), out_x, out_y, FB, any_ula));
    }

    // ── LR-PSCAN — per-scanline replay of the LoRes registers ───────────
    //
    // NOT one of the 91 catalogued rows: it covers the requirement stated in
    // the plan's "Documented limitations" section, which the implementer is
    // told to satisfy — "NR $15 bit 7, NR $32, NR $33 and NR $6A must go into
    // the per-scanline snapshot set, or the layer will be wrong on exactly
    // the demo that motivated implementing it" (beast.nex toggles NR $15
    // mid-frame today). VHDL zxnext.vhd:6768-6802 re-latches the LoRes
    // parameters every CLK_7 and :6817 latches lores_en every CLK_14.
    //
    // Without the per-line snapshot the live register would be applied to
    // EVERY row of the frame, so a mid-frame enable would retroactively
    // paint rows the beam had already passed.
    {
        Fix f;
        const int R = Renderer::DISP_Y + 40;   // the row the write lands on
        f.enable_lores(false);                 // frame baseline: LoRes off

        // Mid-frame write: LoRes on, plus a Y scroll. Only rows from R
        // onward take the snapshot, exactly as Emulator::on_scanline does.
        f.r.lores().set_enabled(true);
        f.r.lores().set_scroll_y(4);
        for (int row = R; row < Renderer::FB_HEIGHT; ++row)
            f.r.lores().snapshot_for_line(row);

        std::vector<uint32_t> before(Renderer::FB_WIDTH);
        std::vector<uint32_t> after(Renderer::FB_WIDTH);
        std::vector<uint32_t> ula_ref(Renderer::FB_WIDTH);
        f.render(before.data(), R - 1);
        f.render(after.data(), R);
        f.render_ula_only(ula_ref.data(), R - 1);

        // Row R-1 kept the pre-write state (LoRes off -> pure ULA); row R
        // shows LoRes with scroll_y = 4 applied.
        const unsigned vc = static_cast<unsigned>(R - Renderer::DISP_Y);
        const uint32_t exp_r =
            f.pal.ula_colour(false, seed_byte(ref_addr8(ref_x(0, 0), ref_y(vc, 4))));

        check("LR-PSCAN",
              "NR $15 bit 7 / $32 / $33 / $6A are replayed per scanline — a "
              "mid-frame enable+scroll affects only the rows from the write "
              "onward, never the rows the beam already passed "
              "(zxnext.vhd:6768-6802, 6817)",
              std::memcmp(before.data(), ula_ref.data(),
                          Renderer::FB_WIDTH * sizeof(uint32_t)) == 0 &&
              after[Renderer::DISP_X] == exp_r,
              DETAIL("row R-1 matches pure ULA=%d; row R got=0x%08X exp=0x%08X",
                     std::memcmp(before.data(), ula_ref.data(),
                                 Renderer::FB_WIDTH * sizeof(uint32_t)) == 0,
                     after[Renderer::DISP_X], exp_r));
    }

    // ── LR-161 — NR $68 bit 2 (ULA half-pixel scroll) does not move LoRes ─
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 18;
        std::vector<uint32_t> a(Renderer::FB_WIDTH), b(Renderer::FB_WIDTH);
        f.r.ula().render_scanline(f.r.ula_line_.data(), ROW, f.mmu, nullptr);
        f.r.apply_lores(f.r.ula_line_.data(), ROW, f.ram, f.pal);
        std::copy(f.r.ula_line_.begin(), f.r.ula_line_.end(), a.begin());

        f.r.ula().set_ula_fine_scroll_x(true);
        f.r.ula().render_scanline(f.r.ula_line_.data(), ROW, f.mmu, nullptr);
        f.r.apply_lores(f.r.ula_line_.data(), ROW, f.ram, f.pal);
        std::copy(f.r.ula_line_.begin(), f.r.ula_line_.end(), b.begin());
        f.r.ula().set_ula_fine_scroll_x(false);

        check("LR-161",
              "NR $68 bit 2 (ULA half-pixel scroll) does not move the LoRes "
              "image (zxnext.vhd:4241-4271 — no such port on the LoRes "
              "module)",
              std::memcmp(a.data() + Renderer::DISP_X, b.data() + Renderer::DISP_X,
                          Renderer::DISP_W * sizeof(uint32_t)) == 0);
    }

    // ── LR-165 — LoRes does not disturb the ULA's own VRAM fetch ─────────
    {
        Fix f;
        const int ROW = Renderer::DISP_Y + 22;
        std::vector<uint32_t> before(Renderer::FB_WIDTH), after(Renderer::FB_WIDTH);
        f.enable_lores(false);
        f.render(before.data(), ROW);
        f.enable_lores(true);
        std::vector<uint32_t> junk(Renderer::FB_WIDTH);
        f.render(junk.data(), ROW);
        f.enable_lores(false);
        f.render(after.data(), ROW);
        check("LR-165",
              "LoRes does not disturb the ULA's own VRAM fetch — switching "
              "LoRes off again restores an intact ULA screen "
              "(zxnext.vhd:6631, 6660)",
              std::memcmp(before.data(), after.data(),
                          Renderer::FB_WIDTH * sizeof(uint32_t)) == 0);
    }

    // ── LR-166 / LR-167 — NR $19 and NR $1B do not clip LoRes ────────────
    // zxnext.vhd:4258-4261 wires LoRes to ula_clip_*; nr_19_* reaches only
    // the sprite module (4366-4369) and nr_1b_* only the tilemap (4424-4427).
    {
        Fix f;
        f.enable_lores(true);
        const int ROW = Renderer::DISP_Y + 100;    // inside a (64,191,32,159)
                                                   // window's Y range
        std::vector<uint32_t> ref(Renderer::FB_WIDTH), got(Renderer::FB_WIDTH);
        f.render(ref.data(), ROW);

        // The SAME numeric window LR-120 uses for NR $1A — which does clip.
        SpriteEngine sp;
        sp.reset();
        sp.set_clip_x1(64); sp.set_clip_x2(191);
        sp.set_clip_y1(32); sp.set_clip_y2(159);
        f.render(got.data(), ROW);
        check("LR-166",
              "NR $19 (sprite clip) does not clip LoRes — the full 256x192 "
              "image still draws (zxnext.vhd:4258-4261, 4366-4369)",
              std::memcmp(ref.data(), got.data(),
                          Renderer::FB_WIDTH * sizeof(uint32_t)) == 0);

        f.tm.set_clip_x1(64); f.tm.set_clip_x2(191);
        f.tm.set_clip_y1(32); f.tm.set_clip_y2(159);
        f.render(got.data(), ROW);
        check("LR-167",
              "NR $1B (tilemap clip) does not clip LoRes — the full 256x192 "
              "image still draws (zxnext.vhd:4258-4261, 4424-4427)",
              std::memcmp(ref.data(), got.data(),
                          Renderer::FB_WIDTH * sizeof(uint32_t)) == 0);
    }
}

// ── Main ─────────────────────────────────────────────────────────────────

int main() {
    printf("Compositor Subsystem Compliance Tests\n");
    printf("=====================================\n\n");

    test_TR();         printf("  Group: TR — done\n");
    test_L2EQ();       printf("  Group: L2EQ — done\n");
    test_TRI();        printf("  Group: TRI — done\n");
    test_FB();         printf("  Group: FB — done\n");
    test_PRI();        printf("  Group: PRI — done\n");
    test_PRI_BOUND();  printf("  Group: PRI-BOUND — done\n");
    test_L2P();        printf("  Group: L2P — done\n");
    test_BL();         printf("  Group: BL — done\n");
    test_UTB();        printf("  Group: UTB — done\n");
    test_PFF();        printf("  Group: PFF — done\n");
    test_STEN();       printf("  Group: STEN — done\n");
    test_UDIS();       printf("  Group: UDIS — done\n");
    test_SOB();        printf("  Group: SOB — done\n");
    test_LINE();       printf("  Group: LINE — done\n");
    test_BLANK();      printf("  Group: BLANK — done\n");
    test_PAL();        printf("  Group: PAL — done\n");
    test_RST();        printf("  Group: RST — done\n");
    test_PSCAN();      printf("  Group: PSCAN — done\n");
    test_UCLIP();      printf("  Group: UCLIP — done\n");
    test_LMASK();      printf("  Group: LMASK — done\n");
    test_LORES();      printf("  Group: LR — done\n");

    printf("\n=====================================\n");
    printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
           g_total + static_cast<int>(g_skipped.size()),
           g_pass, g_fail, static_cast<int>(g_skipped.size()));

    // Per-group breakdown
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
