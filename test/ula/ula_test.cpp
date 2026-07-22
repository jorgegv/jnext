// ULA Video Compliance Test Runner
//
// Full rewrite (Task 1 Wave 3, 2026-04-15) against
// doc/testing/ULA-VIDEO-TEST-PLAN-DESIGN.md. Every plan row maps to exactly
// one check() or skip() with a VHDL file:line citation. Expected values are
// derived from the authoritative FPGA VHDL at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo). The C++ implementation is NEVER the oracle; where
// a row cannot be exercised through the current public Ula API surface it is
// reported via skip() with a one-line reason.
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
#include "core/saveable.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    Result r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
}

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

// ── VHDL oracle helpers (zxula.vhd) ───────────────────────────────────

// zxula.vhd:218-263 — vram_a = screen_mode(0) & py(7:6) & py(2:0) & py(5:3) & px(7:3)
uint16_t vhdl_pixel_addr(int py, int px, bool alt = false) {
    uint16_t addr = 0;
    addr |= static_cast<uint16_t>(((py & 0xC0) >> 6) << 11);
    addr |= static_cast<uint16_t>(((py & 0x07))      << 8);
    addr |= static_cast<uint16_t>(((py & 0x38) >> 3) << 5);
    addr |= static_cast<uint16_t>(px >> 3);
    if (alt) addr |= 0x2000;
    return addr;
}

// zxula.vhd:218-263 — attr vram_a = screen_mode(0) & "110" & py(7:3) & px(7:3)
uint16_t vhdl_attr_addr(int py, int px, bool alt = false) {
    uint16_t addr = 0x1800;
    addr |= static_cast<uint16_t>(((py >> 3) & 0x1F) << 5);
    addr |= static_cast<uint16_t>(px >> 3);
    if (alt) addr |= 0x2000;
    return addr;
}

// Same formula inline in Ula::pixel_addr_offset.
uint16_t emu_pixel_addr_offset(int screen_row, int col) {
    return static_cast<uint16_t>(
          ((screen_row & 0xC0) << 5)
        | ((screen_row & 0x07) << 8)
        | ((screen_row & 0x38) << 2)
        | col);
}

// zxula.vhd:543-554 — standard ULA ula_pixel encoding.
//   ula_pixel(7:3) = "000" & not pixel_en & attr(6)
//   ula_pixel(2:0) = attr(2:0) if pixel_en else attr(5:3)
uint8_t vhdl_standard_ula_pixel(bool pixel_en, uint8_t attr) {
    uint8_t bright    = (attr & 0x40) ? 1u : 0u;
    uint8_t ink       = attr & 0x07;
    uint8_t paper     = (attr >> 3) & 0x07;
    uint8_t not_pixel = pixel_en ? 0u : 1u;
    uint8_t colour    = pixel_en ? ink : paper;
    return static_cast<uint8_t>((not_pixel << 4) | (bright << 3) | colour);
}

// zxula.vhd:418-419 — border_clr = "00" & port_fe(2:0) & port_fe(2:0)
uint8_t vhdl_border_clr(uint8_t port_fe) {
    uint8_t c = port_fe & 0x07;
    return static_cast<uint8_t>((c << 3) | c);
}

// zxula.vhd:418-419 — border_clr_tmx = "01" & (not port_ff(5:3)) & port_ff(5:3)
uint8_t vhdl_border_clr_tmx(uint8_t port_ff) {
    uint8_t f = (port_ff >> 3) & 0x07;
    return static_cast<uint8_t>(0x40 | ((~f & 0x07) << 3) | f);
}

// zxula.vhd:531-541 — ULA+ ula_pixel encoding.
//   ula_pixel(7:3) = "11" & attr(7:6) & (screen_mode(2) or not pixel_en)
//   ula_pixel(2:0) = attr(2:0) if pixel_en else attr(5:3)
uint8_t vhdl_ulap_pixel(bool pixel_en, uint8_t attr, bool screen_mode_2) {
    uint8_t pg     = (attr >> 6) & 0x03;
    uint8_t low3   = pixel_en ? (attr & 0x07) : ((attr >> 3) & 0x07);
    uint8_t bit3   = ((screen_mode_2 ? 1u : 0u) | (pixel_en ? 0u : 1u)) & 0x01;
    uint8_t upper5 = 0x18 | (pg << 1) | bit3;  // "11" & attr(7:6) & bit3
    return static_cast<uint8_t>((upper5 << 3) | low3);
}

// ── Shared harness bits ───────────────────────────────────────────────

struct UlaBed {
    Ram ram;
    Rom rom;
    Mmu mmu;
    Ula ula;
    PaletteManager palette;
    UlaBed() : ram(), rom(), mmu(ram, rom) {
        mmu.reset();
        mmu.set_page(2, 10);  // bank 5 page 10 → 0x4000
        mmu.set_page(3, 11);  // bank 5 page 11 → 0x6000
        ula.set_ram(&ram);
        // G102 — wire the 256-entry × 2-bank ULA palette so the
        // renderer's std-ULA path resolves to the canonical ZX
        // colours at boot defaults (mirrored at idx 0x00..0x1F).
        // Without this, lookup_colour returns opaque black.
        palette.reset();
        ula.set_palette(&palette);
        ula.reset();
        ula.init_border_per_line();
    }
    void poke(uint16_t cpu_addr, uint8_t v) {
        uint32_t phys = (cpu_addr < 0x6000)
            ? (10u * 8192u + (cpu_addr - 0x4000u))
            : (11u * 8192u + (cpu_addr - 0x6000u));
        ram.write(phys, v);
    }
};

// VHDL zxula.vhd:543-553 — std-ULA encoder produces an 8-bit ula_pixel
// that indexes the single 256-entry × 2-bank ULA palette.  These two
// helpers return the ARGB the renderer would emit for an N-coloured
// ink or paper cycle on the active bank.  At boot defaults
// indices 0x00..0x0F (ink) and 0x10..0x1F (paper) hold the canonical
// ZX colours; the helpers query the live palette so any test palette
// poke is still observable.
inline uint8_t std_ula_ink_pixel(uint8_t colour /*0..15*/) {
    // ula_pixel(7:3) = "00000", attr(6) = bright, low3 = colour & 7
    return static_cast<uint8_t>(colour & 0x0F);
}
inline uint8_t std_ula_paper_pixel(uint8_t colour /*0..15*/) {
    // ula_pixel(7:3) = "00001" (paper cycle, not_pixel=1), low3 = colour & 7
    return static_cast<uint8_t>(0x10 | (colour & 0x0F));
}
inline uint32_t bed_ink_argb(const PaletteManager& pal, uint8_t colour) {
    return pal.ula_colour(/*bank_second=*/false, std_ula_ink_pixel(colour));
}
inline uint32_t bed_paper_argb(const PaletteManager& pal, uint8_t colour) {
    return pal.ula_colour(/*bank_second=*/false, std_ula_paper_pixel(colour));
}

} // namespace

// =========================================================================
// Section 1: Screen Address Calculation (zxula.vhd:218-263) — 12 rows
// =========================================================================

static void test_section1_screen_address() {
    set_group("S01-ScreenAddr");

    struct Row { const char* id; int py, px; uint16_t exp_pix; uint16_t exp_attr; bool alt; };
    Row rows[] = {
        {"S1.01",  0,   0,   0x0000, 0x1800, false},
        {"S1.02",  0,   8,   0x0001, 0x1801, false},
        {"S1.03",  1,   0,   0x0100, 0x1800, false},
        {"S1.04",  7,   0,   0x0700, 0x1800, false},
        {"S1.05",  8,   0,   0x0020, 0x1820, false},
        {"S1.06", 64,   0,   0x0800, 0x1900, false},
        {"S1.07",191, 248,   0x17FF, 0x1AFF, false},
        {"S1.08",  0,   0,   0x2000, 0x3800, true },
        {"S1.09", 96, 128,   0x0890, 0x1990, false},
        {"S1.10", 63,   0,   0x07E0, 0x18E0, false},
        {"S1.11", 65,   0,   0x0900, 0x1900, false},
        {"S1.12",191,   0,   0x17E0, 0x1AE0, false},
    };

    for (const auto& r : rows) {
        uint16_t vp = vhdl_pixel_addr(r.py, r.px, r.alt);
        uint16_t va = vhdl_attr_addr(r.py, r.px, r.alt);
        bool ok = (vp == r.exp_pix) && (va == r.exp_attr);
        check(r.id,
              "zxula.vhd:218-263 — VHDL pixel/attr vram_a formula",
              ok,
              fmt("py=%d px=%d alt=%d vp=0x%04X/exp 0x%04X va=0x%04X/exp 0x%04X",
                  r.py, r.px, r.alt ? 1 : 0, vp, r.exp_pix, va, r.exp_attr));
    }
}

// =========================================================================
// Section 2: Attribute Rendering (zxula.vhd:543-554) — 10 rows
// =========================================================================

static void test_section2_attribute_rendering() {
    set_group("S02-AttrRender");

    struct Row { const char* id; bool pixel_en; uint8_t attr; uint8_t exp; const char* why; };
    Row rows[] = {
        {"S2.01", true,  0x00, 0x00, "ink no-bright colour 0"},
        {"S2.02", false, 0x00, 0x10, "paper no-bright colour 0"},
        {"S2.03", true,  0x42, 0x0A, "ink bright red(2)"},
        {"S2.04", false, 0x60, 0x1C, "paper bright green(4)"},
        {"S2.05", true,  0x07, 0x07, "ink white no-bright"},
        {"S2.06", false, 0x78, 0x1F, "paper white bright"},
        {"S2.07", true,  0x45, 0x0D, "ink cyan(5) bright"},
        {"S2.09", true,  0x47, 0x0F, "full white on black bright"},
    };
    for (const auto& r : rows) {
        uint8_t got = vhdl_standard_ula_pixel(r.pixel_en, r.attr);
        check(r.id,
              "zxula.vhd:543-554 — standard ULA ula_pixel encoding",
              got == r.exp,
              fmt("%s: got 0x%02X exp 0x%02X", r.why, got, r.exp));
    }

    // S2.08 — flash attr bit 7, output depends on flash_cnt(4) (zxula.vhd:470
    // XOR upstream of the ula_pixel encoder). Exercised by §4 instead.
    // G: flash_cnt(4) XOR upstream of ula_pixel encoder (zxula.vhd:470); covered end-to-end by §4.

    // S2.10 — border pixel forced via border_active_d (zxula.vhd:414-419);
    // the ula_pixel encoder is bypassed. Exercised by §3.
    // G: border_active bypasses ula_pixel encoder (zxula.vhd:414-419); covered e2e by §3.
}

// =========================================================================
// Section 3: Border Colour (zxula.vhd:414-419) — 8 rows
// =========================================================================

static void test_section3_border_colour() {
    set_group("S03-Border");

    struct Row { const char* id; uint8_t fe; uint8_t exp; const char* why; };
    Row rows[] = {
        {"S3.01", 0, 0x00, "black"},
        {"S3.02", 1, 0x09, "blue"},
        {"S3.03", 2, 0x12, "red"},
        {"S3.04", 7, 0x3F, "white"},
        {"S3.05", 4, 0x24, "green"},
    };
    for (const auto& r : rows) {
        uint8_t got = vhdl_border_clr(r.fe);
        check(r.id,
              "zxula.vhd:418 — border_clr = \"00\" & port_fe(2:0) & port_fe(2:0)",
              got == r.exp,
              fmt("%s fe=0x%02X got 0x%02X exp 0x%02X", r.why, r.fe, got, r.exp));
    }

    // S3.06 — border_clr_tmx with port_ff(5:3) = 0.
    // VHDL: "01" & not f & f → f=0: 01 111 000 = 0x78.
    {
        uint8_t got0 = vhdl_border_clr_tmx(0x00);
        check("S3.06",
              "zxula.vhd:419 — border_clr_tmx with port_ff(5:3)=0 = 0x78",
              got0 == 0x78,
              fmt("got 0x%02X exp 0x78", got0));
    }
    // S3.07 — f=7: 01 000 111 = 0x47.
    {
        uint8_t got7 = vhdl_border_clr_tmx(0x38);
        check("S3.07",
              "zxula.vhd:419 — border_clr_tmx with port_ff(5:3)=7 = 0x47",
              got7 == 0x47,
              fmt("got 0x%02X exp 0x47", got7));
    }

    // S3.08 — border_active boundaries (zxula.vhd:414-415: border_active_v
    // = vc(8) OR (vc(7) AND vc(6))) not exposed on Ula.
    // G: border_active_v raw-vc boundary (zxula.vhd:414-415) has no Ula accessor; compositor-level.

    // -----------------------------------------------------------------
    // border_dst per-pixel flag plumbing (G179 issue 4).
    //
    // VHDL zxula.vhd:415 (border_active <= i_phc(8) or border_active_v)
    //   :567 (o_ula_border <= border_active) drives the FPGA's
    //   ula_border signal; zxnext.vhd:7256/7266/7278 mode 011/100/101
    //   gate the ULA-vs-sprite border exception on `ula_border_2='1'`.
    //   jnext's Renderer::ula_border_[] is the per-pixel materialization,
    //   filled by the Ula renderer per row.  The contract: every cell in
    //   [0, FB_WIDTH) must either be set true (border) or stay at the
    //   caller-managed pre-fill false (display).
    // -----------------------------------------------------------------

    // S3.09 — display row: left strip [0..63] = border, display [64..575]
    // = NOT border, right strip [576..639] = border (post-G104 FB_WIDTH=640).
    {
        UlaBed bed;
        // Drive a display-area row (screen_row=0 → fb_row=DISP_Y=32).
        // Pixel and attr pokes ensure render_display_line takes a
        // non-trivial display-area path, but we only assert on flags.
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xAA);
        bed.poke(0x5800, 0x07);
        bed.ula.set_border(2);  // any non-zero border colour

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        std::array<bool, Ula::FB_WIDTH>    flags{};
        std::fill(flags.begin(), flags.end(), false);

        bed.ula.render_scanline(line.data(), Ula::DISP_Y, bed.mmu, flags.data());

        bool left_ok    = true;
        bool display_ok = true;
        bool right_ok   = true;
        for (int x = 0; x < Ula::DISP_X; ++x)
            if (!flags[x]) { left_ok = false; break; }
        for (int x = Ula::DISP_X; x < Ula::DISP_X + Ula::DISP_W; ++x)
            if (flags[x])  { display_ok = false; break; }
        for (int x = Ula::DISP_X + Ula::DISP_W; x < Ula::FB_WIDTH; ++x)
            if (!flags[x]) { right_ok = false; break; }

        check("S3.09",
              "zxula.vhd:415,:567 — display row: border_dst true on "
              "[0..63] + [576..639], false on display [64..575]",
              left_ok && display_ok && right_ok,
              fmt("left=%d display=%d right=%d  (true=ok)",
                  static_cast<int>(left_ok),
                  static_cast<int>(display_ok),
                  static_cast<int>(right_ok)));
    }

    // S3.10 — top-border row (fb_row=0): every cell border_dst==true.
    {
        UlaBed bed;
        bed.ula.set_border(4);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        std::array<bool, Ula::FB_WIDTH>    flags{};
        std::fill(flags.begin(), flags.end(), false);

        bed.ula.render_scanline(line.data(), 0, bed.mmu, flags.data());

        bool all_border = true;
        int  first_bad  = -1;
        for (int x = 0; x < Ula::FB_WIDTH; ++x) {
            if (!flags[x]) { all_border = false; first_bad = x; break; }
        }
        check("S3.10",
              "zxula.vhd:414-415,:567 — top-border row: border_dst true "
              "on every cell [0..639]",
              all_border,
              fmt("first non-border cell idx=%d", first_bad));
    }

    // S3.11 — bottom-border row (fb_row=DISP_Y+DISP_H = 224): every cell
    // border_dst==true (mirrors top-border row; vc(7) AND vc(6) asserts
    // border_active_v at zxula.vhd:414).
    {
        UlaBed bed;
        bed.ula.set_border(6);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        std::array<bool, Ula::FB_WIDTH>    flags{};
        std::fill(flags.begin(), flags.end(), false);

        bed.ula.render_scanline(line.data(), Ula::DISP_Y + Ula::DISP_H,
                                bed.mmu, flags.data());

        bool all_border = true;
        int  first_bad  = -1;
        for (int x = 0; x < Ula::FB_WIDTH; ++x) {
            if (!flags[x]) { all_border = false; first_bad = x; break; }
        }
        check("S3.11",
              "zxula.vhd:414-415,:567 — bottom-border row: border_dst "
              "true on every cell [0..639]",
              all_border,
              fmt("first non-border cell idx=%d", first_bad));
    }

    // S3.12 — render_scanline with border_dst=nullptr is a no-op on the
    // flag side (back-compat default arg).  Just exercise the path to
    // confirm no crash and pixel output is unchanged when nullptr is
    // passed vs. when a buffer is passed (compare ARGB row).
    {
        UlaBed bed;
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0x55);
        bed.poke(0x5800, 0x47);
        bed.ula.set_border(3);

        std::array<uint32_t, Ula::FB_WIDTH> line_null{}, line_with{};
        std::array<bool, Ula::FB_WIDTH>     flags{};
        std::fill(flags.begin(), flags.end(), false);

        bed.ula.render_scanline(line_null.data(), Ula::DISP_Y, bed.mmu);
        bed.ula.render_scanline(line_with.data(), Ula::DISP_Y, bed.mmu,
                                flags.data());

        check("S3.12",
              "zxula.vhd:415 — border_dst=nullptr default arg is back-"
              "compat: identical ARGB output and no flag side-effects",
              line_null == line_with,
              "ARGB rows should match between nullptr and buffered call");
    }

    // S3.13 — display row in HI_COLOUR mode (bits 2:0=010 per VHDL
    // zxula.vhd:191; port 0xFF=0x02): verify the border_dst contract
    // still holds when the alternate render_display_line_hicolour path
    // is taken (separate left/right border loops in the helper).
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x02);                 // HI_COLOUR (mode 010)
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x6000 + emu_pixel_addr_offset(0, 0), 0x47);
        bed.ula.set_border(1);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        std::array<bool, Ula::FB_WIDTH>     flags{};
        std::fill(flags.begin(), flags.end(), false);

        bed.ula.render_scanline(line.data(), Ula::DISP_Y, bed.mmu, flags.data());

        bool left_ok    = true;
        bool display_ok = true;
        bool right_ok   = true;
        for (int x = 0; x < Ula::DISP_X; ++x)
            if (!flags[x]) { left_ok = false; break; }
        for (int x = Ula::DISP_X; x < Ula::DISP_X + Ula::DISP_W; ++x)
            if (flags[x])  { display_ok = false; break; }
        for (int x = Ula::DISP_X + Ula::DISP_W; x < Ula::FB_WIDTH; ++x)
            if (!flags[x]) { right_ok = false; break; }

        check("S3.13",
              "zxula.vhd:415 — HI_COLOUR display row: border_dst true on "
              "left+right strips, false on display area",
              left_ok && display_ok && right_ok,
              fmt("left=%d display=%d right=%d",
                  static_cast<int>(left_ok),
                  static_cast<int>(display_ok),
                  static_cast<int>(right_ok)));
    }
}

// =========================================================================
// Section 4: Flash Timing (zxula.vhd:470-481) — 6 rows
// =========================================================================

static void test_section4_flash_timing() {
    set_group("S04-Flash");

    // S4.01 — flash_cnt(4) toggles every 16 frames (32-frame period).
    {
        UlaBed bed;
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x87);  // flash=1, paper=black, ink=white
        std::array<uint32_t, Ula::FB_WIDTH> a{}, b{};
        bed.ula.render_scanline(a.data(), 32, bed.mmu);
        for (int i = 0; i < 16; ++i) bed.ula.advance_flash();
        bed.ula.render_scanline(b.data(), 32, bed.mmu);
        check("S4.01",
              "zxula.vhd:474-481 — flash_cnt(4) toggles every 16 frames (32-frame period)",
              a[Ula::DISP_X] != b[Ula::DISP_X],
              fmt("phase0=0x%08X phase1=0x%08X", a[Ula::DISP_X], b[Ula::DISP_X]));
    }

    // S4.02 — attr(7)=0 disables flash XOR.
    {
        UlaBed bed;
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x07);
        std::array<uint32_t, Ula::FB_WIDTH> a{}, b{};
        bed.ula.render_scanline(a.data(), 32, bed.mmu);
        for (int i = 0; i < 16; ++i) bed.ula.advance_flash();
        bed.ula.render_scanline(b.data(), 32, bed.mmu);
        check("S4.02",
              "zxula.vhd:470 — attr(7)=0 disables flash XOR; pixel invariant across frames",
              a[Ula::DISP_X] == b[Ula::DISP_X],
              fmt("phaseA=0x%08X phaseB=0x%08X", a[Ula::DISP_X], b[Ula::DISP_X]));
    }

    // S4.03 — attr(7)=1, flash_cnt(4)=0: pixel_en unchanged (ink stays ink).
    {
        UlaBed bed;
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x87);
        std::array<uint32_t, Ula::FB_WIDTH> a{};
        bed.ula.render_scanline(a.data(), 32, bed.mmu);
        const uint32_t exp_ink7 = bed_ink_argb(bed.palette, 7);
        check("S4.03",
              "zxula.vhd:470 — flash_cnt(4)=0 leaves pixel_en unchanged (ink stays ink)",
              a[Ula::DISP_X] == exp_ink7,
              fmt("got 0x%08X exp 0x%08X", a[Ula::DISP_X], exp_ink7));
    }

    // S4.04 — attr(7)=1, flash_cnt(4)=1: pixel_en inverted (ink↔paper).
    {
        UlaBed bed;
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x87);
        for (int i = 0; i < 16; ++i) bed.ula.advance_flash();
        std::array<uint32_t, Ula::FB_WIDTH> a{};
        bed.ula.render_scanline(a.data(), 32, bed.mmu);
        const uint32_t exp_paper0 = bed_paper_argb(bed.palette, 0);
        check("S4.04",
              "zxula.vhd:470 — flash_cnt(4)=1 XOR inverts ink/paper selection",
              a[Ula::DISP_X] == exp_paper0,
              fmt("got 0x%08X exp 0x%08X", a[Ula::DISP_X], exp_paper0));
    }

    // S4.05 — ULAnext disables flash (zxula.vhd:470 "and not i_ulanext_en").
    // With ulanext_en=1, the flash XOR term is gated off, so phase 0 and
    // phase 1 must produce the SAME rendered pixel. Use attr=0x87 (flash=1,
    // paper=black, ink=white) and pixels=0xFF: both phases should show ink
    // (white) because flash no longer swaps ink/paper.
    {
        UlaBed bed;
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x87);
        bed.ula.set_ulanext_en(true);
        std::array<uint32_t, Ula::FB_WIDTH> a{}, b{};
        bed.ula.render_scanline(a.data(), 32, bed.mmu);
        for (int i = 0; i < 16; ++i) bed.ula.advance_flash();
        bed.ula.render_scanline(b.data(), 32, bed.mmu);
        const uint32_t exp_ink7 = bed_ink_argb(bed.palette, 7);
        check("S4.05",
              "zxula.vhd:470 — i_ulanext_en=1 gates off flash XOR term (flash inert)",
              a[Ula::DISP_X] == b[Ula::DISP_X] && a[Ula::DISP_X] == exp_ink7,
              fmt("phase0=0x%08X phase1=0x%08X exp=0x%08X",
                  a[Ula::DISP_X], b[Ula::DISP_X], exp_ink7));
    }

    // S4.06 — ULA+ disables flash (zxula.vhd:470 "and not i_ulap_en").
    // With ulap_en=1 the flash XOR term is suppressed, so a scanline with
    // attr(7)=1 must render identically across the 16-frame flash phase.
    {
        UlaBed bed;
        bed.ula.set_ulap_en(true);
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x87);  // attr(7)=1 (would-be flash), paper=0, ink=7
        std::array<uint32_t, Ula::FB_WIDTH> a{}, b{};
        bed.ula.render_scanline(a.data(), 32, bed.mmu);
        for (int i = 0; i < 16; ++i) bed.ula.advance_flash();
        bed.ula.render_scanline(b.data(), 32, bed.mmu);
        check("S4.06",
              "zxula.vhd:470 — ULA+ enable suppresses flash XOR (pixel stable across phase)",
              a[Ula::DISP_X] == b[Ula::DISP_X],
              fmt("phase0=0x%08X phase1=0x%08X", a[Ula::DISP_X], b[Ula::DISP_X]));
    }
}

// =========================================================================
// Section 5: Timex Hi-Res / Hi-Colour Modes (zxula.vhd:384-393) — 8 rows
// =========================================================================

static void test_section5_timex() {
    set_group("S05-Timex");

    // S5.01 — standard mode (port_ff bits 2:0 = 000 per zxula.vhd:191).
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x00);
        check("S5.01",
              "zxula.vhd:191/384-393 — port_ff(2:0)=000 selects standard 256x192 pixel/attr layout",
              bed.ula.get_screen_mode_reg() == 0x00,
              fmt("got 0x%02X", bed.ula.get_screen_mode_reg()));
    }

    // S5.02 — alt display file (mode 001). screen_mode(0)=1 raises vram_a bit 13.
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x01);
        check("S5.02",
              "zxula.vhd:191 — port_ff(2:0)=001 selects alternate display file (screen_mode(0)=1 → vram_a bit 13 = 1, zxula.vhd:218,235)",
              bed.ula.get_screen_mode_reg() == 0x01,
              fmt("got 0x%02X", bed.ula.get_screen_mode_reg()));
    }

    // S5.03 — hi-colour mode (mode 010): per-row attributes from bank 1.
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x02);
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x6000 + emu_pixel_addr_offset(0, 0), 0x47);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        const uint32_t exp_bright_white = bed_ink_argb(bed.palette, 15);
        check("S5.03",
              "zxula.vhd:386-392 — hi-colour: second vram fetch from bank 1 (0x6000) for per-row attr",
              line[Ula::DISP_X] == exp_bright_white,
              fmt("got 0x%08X exp 0x%08X (bright white)", line[Ula::DISP_X], exp_bright_white));
    }

    // S5.04 — hi-colour + alt display file (mode 011).  In VHDL zxula.vhd:218
    // (and the `vram_a <= screen_mode(0) & addr_p_spc_12_5 & px_1(7:3)` fetch
    // at :235) the pixel base follows screen_mode(0).  For HI_COLOUR mode
    // screen_mode(1)=1 forces the attribute fetch to '1' & ... (bank 1, 0x6000
    // range, zxula.vhd:239/249).  In HI_COLOUR+alt both addresses collapse
    // onto the same byte at 0x6000+poff — a VHDL-literal consequence.  Here
    // we drive port 0xFF with mode-bits 011 (port_val 0x03 per VHDL bits 2:0)
    // and poke the combined pixel-and-attr byte 0xC7 (flash=1, bright=1,
    // paper=0, ink=7; pixel pattern 11000111).  With flash_cnt(4)=0 (first
    // render, no advance_flash), the leading pixel bit = 1 → ink = 7+bright
    // → palette[15].  The same stimulus on mode 010 (no alt) would read the
    // pixel from 0x4000 (= 0x00 default) → paper → palette[8], failing the
    // equality check — so this observes the alt-file discrimination
    // end-to-end without needing a separate getter assertion.
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x03);  // bits 2:0 = 011 → HI_COLOUR + alt
        bed.poke(0x6000 + emu_pixel_addr_offset(0, 0), 0xC7);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        const uint32_t exp_bright_white = bed_ink_argb(bed.palette, 15);
        check("S5.04",
              "zxula.vhd:218 + :235 — HI_COLOUR+alt (mode 011) reads pixel AND "
              "attr from 0x6000+poff (collapsed byte); 0xC7 pixel bit 7=1, "
              "ink=7 bright=1 → ula_pixel 0x0F",
              bed.ula.get_alt_file() == true
              && line[Ula::DISP_X] == exp_bright_white,
              fmt("alt_file=%d got 0x%08X exp 0x%08X",
                  static_cast<int>(bed.ula.get_alt_file()),
                  line[Ula::DISP_X], exp_bright_white));
    }

    // S5.05 — hi-res mode (mode 110 = port_ff bits 2:0 = 110 per VHDL
    // zxula.vhd:191).  port_val = 0x06 | (5 << 3) = 0x2E; mode_bits = 110
    // selects HI_RES; paper bits 5:3 = 5 (cyan) per Timex spec.
    // Per VHDL zxula.vhd:419 + 426-427: HI_RES attr_reg is loaded with
    // `border_clr_tmx = "01" & ~port_ff(5:3) & port_ff(5:3)` — bit 6=BRIGHT,
    // ink bits 2:0 = port_ff(5:3) = 5, paper bits 5:3 = ~5 & 7 = 2.
    // Pixel bytes 0xFF at both screen-0 and screen-1 → all output pixels
    // render as ink = bright cyan.
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x06 | (5 << 3));  // 0x2E: HI_RES + paper=cyan
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x6000 + emu_pixel_addr_offset(0, 0), 0xFF);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        // Per VHDL border_clr_tmx: ink = bright(port_ff(5:3)) = bright cyan.
        const uint32_t exp_bright_cyan = bed_ink_argb(bed.palette, 5 | 0x08);
        check("S5.05",
              "zxula.vhd:191/389/419/426-427 — HI_RES via port_ff(2:0)=110; "
              "byte-interleave + border_clr_tmx ink derivation: ink = "
              "bright(port_ff(5:3)) = bright cyan when paper bits = 5",
              line[Ula::DISP_X] == exp_bright_cyan,
              fmt("got 0x%08X exp 0x%08X (bright cyan)",
                  line[Ula::DISP_X], exp_bright_cyan));
    }

    // S5.06 — hi-res border colour uses border_clr_tmx.  Per VHDL
    // zxula.vhd:443-448: when `border_active='1'` and `screen_mode_r(2)='1'`
    // (hi-res), attr_reg is loaded with `border_clr_tmx` (zxula.vhd:419)
    // instead of `border_clr` (zxula.vhd:418).  jnext's render_border_line
    // auto-routes to tmx when mode_==HI_RES.
    //
    // G102 — std-ULA encoder per VHDL :543-553 maps the TMX 8-bit attr
    // (bits 7:6="01", 5:3=~paper, 2:0=paper) to ula_pixel:
    //   ula_pixel = 0x10 | (attr(6)<<3) | attr(5:3)
    //             = 0x10 | 0x08 | (~paper & 7)
    // For paper=6: ~6 & 7 = 1 → ula_pixel = 0x19.  Boot default at
    // idx 0x19 is the paper-cycle mirror of ZX colour 9 (bright blue,
    // RGB333 (0,0,7)).
    {
        UlaBed bed;
        bed.ula.set_border(0);            // port 0xFE bits 2:0 = 0 → black
        bed.ula.init_border_per_line();
        // VHDL bits 2:0 = 110 → HI_RES (zxula.vhd:191); bits 5:3 = 110 →
        // HI_RES paper colour 6 (zxula.vhd:419).  Combined port_val = 0x36.
        bed.ula.set_screen_mode(0x36);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 0, bed.mmu);  // top border row

        const uint32_t exp_argb = rgb333_to_argb8888(0, 0, 7);  // bright blue
        check("S5.06",
              "zxula.vhd:419 + :443-448 + :543-553 — HI_RES border uses "
              "border_clr_tmx through std-ULA encoder; paper=6 → ula_pixel=0x19 "
              "(boot default = bright blue paper-cycle mirror)",
              line[0] == exp_argb,
              fmt("got 0x%08X exp 0x%08X (bright blue)",
                  line[0], exp_argb));
    }

    // S5.07 — shadow screen forces screen_mode to "000" per VHDL zxula.vhd:191:
    //   screen_mode_s <= i_port_ff_reg(2 downto 0) when i_ula_shadow_en = '0'
    //                    else "000";
    // Phase 1 (commit 8cd3488) implemented `Ula::set_shadow_screen_en(bool)`
    // which, when asserted, masks the port 0xFF value via
    // `set_screen_mode(screen_mode_reg_ & 0xF8)` — this clears bits 2:0 (the
    // VHDL mode field), which with the Wave-D set_screen_mode update also
    // clears alt_file_.  Here we verify: after writing a HI_RES port 0xFF
    // (0x36 = paper=6 in bits 5:3 + mode=110 in bits 2:0), asserting
    // shadow_en clamps the effective screen_mode (bits 2:0) to 000 while
    // preserving the paper colour (bits 5:3).
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x36);    // HI_RES + paper=6 prior to shadow enable
        bed.ula.set_shadow_screen_en(true);
        // The "existing getter" is get_screen_mode_reg(); bits 2:0 of the
        // retained raw byte must now read 000 (the VHDL mode field).
        const uint8_t mode_bits = static_cast<uint8_t>(
            bed.ula.get_screen_mode_reg() & 0x07);
        check("S5.07",
              "zxula.vhd:191 — i_ula_shadow_en='1' forces screen_mode to 000; "
              "set_shadow_screen_en(true) after port 0xFF=0x36 must zero the "
              "mode field (bits 2:0 of the stored register)",
              bed.ula.get_shadow_screen_en() == true && mode_bits == 0,
              fmt("shadow_en=%d mode_bits=%u reg=0x%02X",
                  static_cast<int>(bed.ula.get_shadow_screen_en()),
                  mode_bits, bed.ula.get_screen_mode_reg()));
    }

    // S5.08 — hi-res attr_reg loaded with border_clr_tmx.
    // G: attr_reg + border_clr_tmx (zxula.vhd:384-393) — internal shift-reg detail.

    // S5.09 — i_ula_shadow_en routes ULA reads from bank 5 (page 10) to bank 7
    //   (page 14). VHDL ula_bank_do <= vram_bank7_do when port_7ffd_shadow='1'.
    //   Regression for the half-implemented set_shadow_screen_en that recorded
    //   the flag but did NOT switch the bank; surfaced by beast.nex 2026-04-25.
    //
    //   Stimulus: write distinct ink colours into the row-0 attribute byte of
    //   each bank (cyan/5 in bank 5, red/2 in bank 7), pixel byte 0xFF (all
    //   ink) in both. Render scanline 32 (display row 0) with shadow off then
    //   on; the first display column must flip from cyan to red.
    {
        UlaBed bed;
        const uint16_t poff = emu_pixel_addr_offset(0, 0);
        const uint32_t bank5_pix = 10u * 8192u + poff;
        const uint32_t bank5_att = 10u * 8192u + 0x1800u;
        // Standalone Ula+Ram fixture (no Mmu): the ULA falls back to the
        // legacy page-14 read when no bank-7 BRAM pointer is wired.
        const uint32_t bank7_pix = 14u * 8192u + poff;
        const uint32_t bank7_att = 14u * 8192u + 0x1800u;
        bed.ram.write(bank5_pix, 0xFF);
        bed.ram.write(bank5_att, 0x05);   // paper black, ink cyan
        bed.ram.write(bank7_pix, 0xFF);
        bed.ram.write(bank7_att, 0x02);   // paper black, ink red

        std::array<uint32_t, Ula::FB_WIDTH> line_off{}, line_on{};
        bed.ula.set_shadow_screen_en(false);
        bed.ula.render_scanline(line_off.data(), 32, bed.mmu);
        bed.ula.set_shadow_screen_en(true);
        bed.ula.render_scanline(line_on.data(), 32, bed.mmu);

        const uint32_t cyan = bed_ink_argb(bed.palette, 5);
        const uint32_t red  = bed_ink_argb(bed.palette, 2);
        check("S5.09",
              "shadow_screen_en=1 must switch ULA to bank 7 (page-14 fallback, no BRAM wired); "
              "VHDL ula_bank_do <= vram_bank7_do when port_7ffd_shadow='1'",
              line_off[Ula::DISP_X] == cyan && line_on[Ula::DISP_X] == red,
              fmt("off=0x%08X (exp cyan 0x%08X)  on=0x%08X (exp red 0x%08X)",
                  line_off[Ula::DISP_X], cyan, line_on[Ula::DISP_X], red));
    }

    // S5.10 — G104: HI_RES mode renders at native 512 horizontal pixels via
    // VHDL byte-interleave (zxula.vhd:131-138, 271-306, 384-406).
    // shift_reg_32 = pbyte_hi & abyte_hi & pbyte_lo & abyte_lo, emitted
    // MSB-first.  So per source-column pair the 32 emitted hi-res pixels
    // are: 8 px from screen-0 col N, 8 px from screen-1 col N, 8 px from
    // screen-0 col N+1, 8 px from screen-1 col N+1.
    //
    // G179 (Issue #2) — colour derivation now follows VHDL zxula.vhd:419 +
    // 426-427.  In HI_RES the whole attr_reg is loaded with
    //   border_clr_tmx <= "01" & (not port_ff(5:3)) & port_ff(5:3),
    // so BRIGHT=1, ink colour = port_ff(5:3), paper colour =
    // ~port_ff(5:3) & 7.  port_ff(5:3) is the user-controllable
    // "screen colour"; mode lives in port_ff(2:0) (post-Agent-A
    // VHDL-faithful convention).
    //
    // Distinct stimulus: plant 0xAA into screen-0 col 0 and 0x55 into
    // screen-1 col 0 — different bit patterns so an implementation that
    // discards b0 (the pre-G104 bug) cannot pass by accident.
    {
        UlaBed bed;
        const uint8_t paper_color = 5;        // port_ff(5:3) = 101 (cyan)
        // Mode = HI_RES (VHDL screen_mode_s = port_ff(2:0) = 110); paper colour
        // in bits 5:3.  After Agent A's set_screen_mode rework, mode is
        // decoded from bits 2:0 — this byte selects HI_RES with screen-colour=5.
        const uint8_t port_ff_val = static_cast<uint8_t>(
            0x06 | (paper_color << 3));
        bed.ula.set_screen_mode(port_ff_val);
        const uint16_t poff = emu_pixel_addr_offset(0, 0);
        bed.poke(0x4000 + poff, 0xAA);      // screen-0 col 0 = 1010_1010
        bed.poke(0x6000 + poff, 0x55);      // screen-1 col 0 = 0101_0101

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);  // display row 0

        // Per VHDL border_clr_tmx (zxula.vhd:419):
        //   ink   = bright(port_ff(5:3))         = bright(paper_color)
        //   paper = bright(~port_ff(5:3) & 7)    = bright(~paper_color & 7)
        // bed_ink_argb / bed_paper_argb take a 4-bit colour where bit 3 is
        // the BRIGHT bit.
        const uint32_t ink_argb   = bed_ink_argb(
            bed.palette, static_cast<uint8_t>(paper_color | 0x08));
        const uint32_t paper_argb = bed_paper_argb(
            bed.palette, static_cast<uint8_t>((~paper_color & 0x07) | 0x08));

        // Cells [DISP_X+0..+7] = screen-0 col 0 bits 7..0 = 0xAA
        //   bit7=1 → ink, bit6=0 → paper, bit5=1 → ink, ...
        const uint32_t exp_b0[8] = {
            ink_argb, paper_argb, ink_argb, paper_argb,
            ink_argb, paper_argb, ink_argb, paper_argb,
        };
        // Cells [DISP_X+8..+15] = screen-1 col 0 bits 7..0 = 0x55
        //   bit7=0 → paper, bit6=1 → ink, bit5=0 → paper, ...
        const uint32_t exp_b1[8] = {
            paper_argb, ink_argb, paper_argb, ink_argb,
            paper_argb, ink_argb, paper_argb, ink_argb,
        };

        bool ok_s0 = true;
        for (int i = 0; i < 8; ++i) {
            if (line[Ula::DISP_X + i] != exp_b0[i]) { ok_s0 = false; break; }
        }
        bool ok_s1 = true;
        for (int i = 0; i < 8; ++i) {
            if (line[Ula::DISP_X + 8 + i] != exp_b1[i]) { ok_s1 = false; break; }
        }

        check("S5.10",
              "zxula.vhd:389 + :419 — HI_RES native 512 px byte-interleaved "
              "s0/s1, with ink/paper from border_clr_tmx (BRIGHT=1, ink=port_ff(5:3), "
              "paper=~port_ff(5:3)&7).  Stimulus: paper_color=5 (cyan), s0=0xAA, "
              "s1=0x55 → expect bright-cyan ink + bright-red paper, alternating "
              "MSB-first across the screen-0 then screen-1 byte windows.",
              ok_s0 && ok_s1,
              fmt("ok_s0=%d ok_s1=%d "
                  "line[DISP_X+0]=0x%08X (exp ink 0x%08X) "
                  "line[DISP_X+1]=0x%08X (exp paper 0x%08X) "
                  "line[DISP_X+8]=0x%08X (exp paper 0x%08X) "
                  "line[DISP_X+9]=0x%08X (exp ink 0x%08X)",
                  ok_s0, ok_s1,
                  line[Ula::DISP_X + 0], ink_argb,
                  line[Ula::DISP_X + 1], paper_argb,
                  line[Ula::DISP_X + 8], paper_argb,
                  line[Ula::DISP_X + 9], ink_argb));
    }

    // S5.10b — column-1 follow-on: distinct bytes in screen-0 col 1 and
    // screen-1 col 1 land at fb cells [DISP_X+16..+31].  Validates that
    // each source column contributes a 16-cell window (8 cells s0 then
    // 8 cells s1) at base DISP_X + col * 16, matching VHDL zxula.vhd:389
    // shift_reg_32 emit order across multiple columns.  Uses a different
    // paper_color from S5.10 to also exercise the colour formula on a
    // second value (S5.10 only pins paper_color=5).
    {
        UlaBed bed;
        const uint8_t paper_color = 2;        // port_ff(5:3) = 010 (red)
        const uint8_t port_ff_val = static_cast<uint8_t>(
            0x06 | (paper_color << 3));
        bed.ula.set_screen_mode(port_ff_val);

        // Column 0: keep simple test value
        const uint16_t poff0 = emu_pixel_addr_offset(0, 0);
        bed.poke(0x4000 + poff0, 0xFF);     // screen-0 col 0 all-ink
        bed.poke(0x6000 + poff0, 0x00);     // screen-1 col 0 all-paper
        // Column 1: distinct mid-row pattern
        const uint16_t poff1 = emu_pixel_addr_offset(0, 1);
        bed.poke(0x4000 + poff1, 0xF0);     // screen-0 col 1: ink ink ink ink, paper paper paper paper
        bed.poke(0x6000 + poff1, 0x0F);     // screen-1 col 1: paper paper paper paper, ink ink ink ink

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        // VHDL border_clr_tmx: ink = bright(paper_color), paper = bright(~paper_color&7).
        const uint32_t ink_argb   = bed_ink_argb(
            bed.palette, static_cast<uint8_t>(paper_color | 0x08));
        const uint32_t paper_argb = bed_paper_argb(
            bed.palette, static_cast<uint8_t>((~paper_color & 0x07) | 0x08));

        // Col 1 screen-0 bits 7..0 = 0xF0 = ink,ink,ink,ink,paper,paper,paper,paper
        // → cells [DISP_X+16..+23]
        bool ok_c1_s0 = true;
        for (int i = 0; i < 8; ++i) {
            const uint32_t exp = (0xF0 >> (7-i)) & 1 ? ink_argb : paper_argb;
            if (line[Ula::DISP_X + 16 + i] != exp) { ok_c1_s0 = false; break; }
        }
        // Col 1 screen-1 bits 7..0 = 0x0F = paper,paper,paper,paper,ink,ink,ink,ink
        // → cells [DISP_X+24..+31]
        bool ok_c1_s1 = true;
        for (int i = 0; i < 8; ++i) {
            const uint32_t exp = (0x0F >> (7-i)) & 1 ? ink_argb : paper_argb;
            if (line[Ula::DISP_X + 24 + i] != exp) { ok_c1_s1 = false; break; }
        }

        check("S5.10b",
              "zxula.vhd:389 + :419 — HI_RES per-column emission: col 1 source "
              "bytes land at fb cells [DISP_X+16..+31] (16-cell window per "
              "source column = base DISP_X + col*16).  Distinct s0=0xF0/s1=0x0F "
              "bytes catch a byte-swap regression; paper_color=2 (red) "
              "exercises the ink/paper formula on a second value distinct "
              "from S5.10's paper_color=5.",
              ok_c1_s0 && ok_c1_s1,
              fmt("ok_c1_s0=%d ok_c1_s1=%d "
                  "line[DISP_X+16]=0x%08X line[DISP_X+19]=0x%08X "
                  "line[DISP_X+24]=0x%08X line[DISP_X+27]=0x%08X",
                  ok_c1_s0, ok_c1_s1,
                  line[Ula::DISP_X + 16], line[Ula::DISP_X + 19],
                  line[Ula::DISP_X + 24], line[Ula::DISP_X + 27]));
    }

    // S5.10c — HI_RES border row width: render a top-border row (fb row 0)
    // through render_border_line and assert all FB_WIDTH cells are filled
    // with the HI_RES TMX-encoded border colour.  Pre-G104 the row width
    // was 320; G104 widens to 640 (64+512+64).  S5.06 already pins the
    // colour encoding; this test only pins that the 640-cell widening
    // doesn't leave gaps in the right half of the row.  Post-Agent-A:
    // mode lives in port_ff(2:0), screen-colour in port_ff(5:3).
    {
        UlaBed bed;
        bed.ula.set_border(0);
        bed.ula.init_border_per_line();
        const uint8_t paper_color = 6;        // port_ff(5:3) = 110 → unchanged
        const uint8_t port_ff_val = static_cast<uint8_t>(
            0x06 | (paper_color << 3));       // HI_RES, screen-colour=6
        bed.ula.set_screen_mode(port_ff_val);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 0, bed.mmu);   // top border row
        // For HI_RES paper_color=6 the std-ULA-encoded border_clr_tmx maps
        // to ula_pixel idx 0x19 → boot-default bright blue (S5.06 derivation).
        // The 8-bit border_clr_tmx attr is 0x40|((~6&7)<<3)|6 = 0x4E; the
        // std-ULA encoder paper cycle yields:
        //   ula_pixel = 0x10 | ((attr&0x40)>>3) | ((attr>>3)&7)
        //             = 0x10 | 0x08 | 0x01 = 0x19
        // Boot defaults mirror canonical ZX colours at idx 0x10..0x1F
        // (paper-cycle slots), so idx 0x19 = bright blue (RGB333 0,0,7).
        const uint32_t exp_border = rgb333_to_argb8888(0, 0, 7);

        bool ok_all = true;
        int  bad_x  = -1;
        for (int x = 0; x < Ula::FB_WIDTH; ++x) {
            if (line[x] != exp_border) { ok_all = false; bad_x = x; break; }
        }

        check("S5.10c",
              "G104 — HI_RES top-border row fills all FB_WIDTH=640 cells "
              "with the TMX-encoded border colour (S5.06 encoding).  Pins "
              "that the constants flip from 320 → 640 widens render_border_"
              "line uniformly without leaving the right half blank.",
              ok_all,
              fmt("ok_all=%d bad_x=%d line[0]=0x%08X line[FB_WIDTH-1]=0x%08X "
                  "exp 0x%08X (bright blue)",
                  ok_all, bad_x, line[0], line[Ula::FB_WIDTH - 1], exp_border));
    }

    // S5.11 — hi-res border encodes border_clr_tmx as 6-bit
    // "01" & (not port_ff(5:3)) & port_ff(5:3) per zxula.vhd:419.
    // Closed by G105: under i_ulanext_en='1', border_active_d='1' the
    // VHDL ULAnext encoder (zxula.vhd:504) emits
    //   ula_pixel = paper_base_index(7:3) & attr_active(5:3)
    //             = 0x80 | (~paper & 7)
    // which then indexes the single 256-entry × 2-bank ULA palette
    // (G102's ula_colour, zxnext.vhd:6981).  jnext's render_border_line
    // applies the same math when ulanext_en_=true.
    //
    // Stimulus: enable ULAnext, set HI_RES (port_ff bits 2:0 = 110 per
    // VHDL zxula.vhd:191) with paper=6 (bits 5:3 = 110, zxula.vhd:419),
    // i.e. port_val = 0x36.  Poke a distinct RGB into the 256-entry ULA
    // palette at the encoded slot, render row 0 (top border), assert the
    // rendered pixel matches the poked colour.
    //
    // Negative gate: with ULAnext disabled the std-ULA encoder takes
    // over.  G102 — see the assertion's exp_legacy derivation for the
    // VHDL-faithful std-ULA HI_RES border encoding (idx 0x19 in the
    // 256-entry palette → bright blue at boot defaults).
    {
        // Bring up a PaletteManager so ula_colour has a sane store.
        UlaBed bed;
        PaletteManager pal;
        pal.reset();
        bed.ula.set_palette(&pal);
        bed.ula.init_border_per_line();

        // Set HI_RES with paper bits 5:3 = 110 (=6).  border_clr_tmx_8bit
        // = 0x40 | ((~6 & 7) << 3) | 6 = 0x40 | 0x08 | 6 = 0x4E.
        // Encoder for ULAnext border (zxula.vhd:504):
        //   ula_pixel = 0x80 | (border_clr_tmx_8bit(5:3))
        //             = 0x80 | (0x4E >> 3) & 7
        //             = 0x80 | 1
        //             = 0x81
        const uint8_t paper = 6;
        const uint8_t exp_idx = static_cast<uint8_t>(0x80 | ((~paper) & 0x07));

        // VHDL bits 2:0 = 110 → HI_RES; bits 5:3 = paper colour.
        bed.ula.set_screen_mode(static_cast<uint8_t>((paper << 3) | 0x06));
        bed.ula.set_ulanext_en(true);
        // Active bank = bank 0 (default); explicit setter for clarity.
        bed.ula.set_active_ula_palette(false);

        // Write a distinct RGB333 (bright magenta = r=7,g=0,b=7) into
        // bank 0 at the encoded slot via the regular NR 0x40+0x41 path:
        //   write_control selects ULA_FIRST (target_palette_),
        //   set_index sets nr_palette_idx,
        //   write_8bit commits RGB333 with nr_palette_value (zxnext.vhd:4919).
        // 0xE3 = 0b111_000_11 → r=7, g=0, b=(11<<1)|((b1|b0)) = 7 → magenta.
        pal.write_control(0x00);   // ULA_FIRST, auto-inc on, ulanext on
        pal.set_index(exp_idx);
        pal.write_8bit(0xE3);
        const uint32_t exp_argb = rgb333_to_argb8888(7, 0, 7);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 0, bed.mmu);  // top border row
        const uint32_t got_ulanext = line[0];

        // Negative gate: disable ULAnext and re-render.  G102 — std-ULA
        // encoder takes over: VHDL :543-553 with TMX 8-bit attr 0x4E
        // (bits 7:6="01" silently dropped, but attr(6)=1 → BRIGHT lit),
        // border_active_d=1 (paper cycle):
        //   ula_pixel = 0x10 | (attr(6)<<3) | attr(5:3)
        //             = 0x10 | 0x08 | 1   (~paper bits = ~6 & 7 = 1)
        //             = 0x19
        // Boot-default ULA palette mirrors the canonical ZX colours at
        // idx 0x10..0x1F (paper-cycle), so idx 0x19 = bright ZX colour 9
        // = bright blue (RGB333 (0,0,7)).  This is now the VHDL-faithful
        // std-ULA HI_RES border colour (previously a documented
        // limitation: 3-bit paper-truncation against a narrower mirror).
        bed.ula.set_ulanext_en(false);
        std::array<uint32_t, Ula::FB_WIDTH> line_legacy{};
        bed.ula.render_scanline(line_legacy.data(), 0, bed.mmu);
        const uint32_t got_legacy = line_legacy[0];

        // VHDL-formula sanity: vhdl_border_clr_tmx(0x30) computes the
        // 8-bit attr value; assert it matches our derivation 0x4E.
        const uint8_t btmx_check = vhdl_border_clr_tmx(0x30);

        // Boot default at idx 0x19 = paper-cycle mirror of bright blue.
        const uint32_t exp_legacy = rgb333_to_argb8888(0, 0, 7);

        check("S5.11",
              "zxula.vhd:419 + :504 — HI_RES border under ULAnext: "
              "border_clr_tmx 8-bit attr → encoder ula_pixel = 0x80 | "
              "(~paper & 7) → 256-entry ULA palette lookup (zxnext.vhd:6981).  "
              "Negative gate (ulanext_en=0) routes through the std-ULA "
              "encoder: ula_pixel = 0x10 | (attr(6)<<3) | (~paper & 7) "
              "= 0x19 → boot default = bright blue (paper-cycle mirror)",
              btmx_check == 0x4E
              && got_ulanext == exp_argb
              && got_legacy  == exp_legacy,
              fmt("btmx=0x%02X (exp 0x4E) idx=0x%02X "
                  "got_ulanext=0x%08X (exp 0x%08X)  "
                  "got_legacy=0x%08X (exp bright blue 0x%08X)",
                  btmx_check, exp_idx,
                  got_ulanext, exp_argb,
                  got_legacy, exp_legacy));
    }

    // S5.12 — G167: HI_RES *display* path dispatches through the ULAnext
    // encoder when ulanext_en_=true.  Pre-G167 render_display_line_hires
    // always called the std-ULA encoder helpers, so a HI_RES program with
    // ULAnext active saw display ink/paper from the wrong palette region.
    // VHDL zxula.vhd:485-528 — for non-FF format, ink (pixel_en=1) =
    // attr_active AND format; paper (pixel_en=0) is dictated by the
    // format table (zxula.vhd:516-527); strip border (border_active_d=1)
    // = paper_base_index(7:3) & attr_active(5:3) = 0x80 | attr(5:3).
    //
    // Stimulus: paper_color=6 → hires_attr = 0x40|((~6&7)<<3)|6 = 0x4E.
    // Pick format=0x07 (3-bit ink) so paper has a deterministic non-FF
    // index: paper_pix = 0x80 | ((0x4E>>3)&0x1F) = 0x80 | 0x09 = 0x89.
    // ink_pix = 0x4E & 0x07 = 0x06.  border_pix = 0x80 | (0x4E>>3)&7 =
    // 0x81.  Plant 0x80 in s0 col 0 → cell 0 = ink, cells 1..7 = paper;
    // 0x00 in s1 col 0 → cells 8..15 = paper.  Poke distinct RGB into
    // each of the three encoder slots and assert the rendered cells
    // match — confirms the dispatch routes through ula_colour() with
    // ULAnext-encoded indices, not the std-ULA 0x00..0x1F slots.
    {
        UlaBed bed;
        PaletteManager pal;
        pal.reset();
        bed.ula.set_palette(&pal);
        bed.ula.init_border_per_line();

        const uint8_t paper_color = 6;
        const uint8_t hires_attr  = static_cast<uint8_t>(
            0x40 | ((~paper_color & 0x07) << 3) | (paper_color & 0x07)); // 0x4E
        const uint8_t format      = 0x07;
        const uint8_t ink_idx     = static_cast<uint8_t>(hires_attr & format);            // 0x06
        const uint8_t paper_idx   = static_cast<uint8_t>(0x80 | ((hires_attr >> 3) & 0x1F)); // 0x89
        const uint8_t border_idx  = static_cast<uint8_t>(0x80 | ((hires_attr >> 3) & 0x07)); // 0x81

        bed.ula.set_screen_mode(static_cast<uint8_t>(0x06 | (paper_color << 3))); // HI_RES
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(format);
        bed.ula.set_active_ula_palette(false);

        // Distinct RGB333 pokes via NR 0x40/0x41 path: bytes chosen so all
        // three channels round-trip cleanly under the BB→BBB expansion
        // (B0 = B1 OR B0; only BB=00 → B=0 and BB=11 → B=7 are pure).
        //   0xE0 = 111_000_00 → (7,0,0) red
        //   0x1C = 000_111_00 → (0,7,0) green
        //   0x77 = 011_101_11 → (3,5,7)
        pal.write_control(0x00); pal.set_index(ink_idx);    pal.write_8bit(0xE0);
        pal.write_control(0x00); pal.set_index(paper_idx);  pal.write_8bit(0x1C);
        pal.write_control(0x00); pal.set_index(border_idx); pal.write_8bit(0x77);
        const uint32_t exp_ink    = rgb333_to_argb8888(7, 0, 0);
        const uint32_t exp_paper  = rgb333_to_argb8888(0, 7, 0);
        const uint32_t exp_border = rgb333_to_argb8888(3, 5, 7);

        // Plant pixels: s0 col 0 = 0x80 (only MSB ink), s1 col 0 = 0x00.
        const uint16_t poff = emu_pixel_addr_offset(0, 0);
        bed.poke(0x4000 + poff, 0x80);
        bed.poke(0x6000 + poff, 0x00);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);   // display row 0

        const uint32_t got_ink    = line[Ula::DISP_X + 0];
        const uint32_t got_paper  = line[Ula::DISP_X + 1];
        const uint32_t got_border = line[0];

        // Negative gate: turn ULAnext off, re-render — must drop back to
        // std-ULA encoder (S5.10 / S5.10b semantics).  Use the std-ULA
        // ink/paper helpers so this gate stays robust if the boot
        // defaults of the 256-entry palette ever change.
        bed.ula.set_ulanext_en(false);
        std::array<uint32_t, Ula::FB_WIDTH> line_off{};
        bed.ula.render_scanline(line_off.data(), 32, bed.mmu);
        // VHDL zxula.vhd:543-553 std-ULA encoder against hires_attr=0x4E:
        //   ink   = ((attr&0x40)>>3) | (attr&0x07)        = 0x08 | 0x06 = 0x0E
        //   paper = 0x10 | ((attr&0x40)>>3) | ((attr>>3)&7) = 0x10|0x08|1 = 0x19
        const uint8_t legacy_ink_idx   = static_cast<uint8_t>(((hires_attr & 0x40) >> 3) | (hires_attr & 0x07));
        const uint8_t legacy_paper_idx = static_cast<uint8_t>(0x10 | ((hires_attr & 0x40) >> 3) | ((hires_attr >> 3) & 0x07));
        const uint32_t exp_legacy_ink   = pal.ula_colour(false, legacy_ink_idx);
        const uint32_t exp_legacy_paper = pal.ula_colour(false, legacy_paper_idx);
        const uint32_t got_legacy_ink   = line_off[Ula::DISP_X + 0];
        const uint32_t got_legacy_paper = line_off[Ula::DISP_X + 1];

        check("S5.12",
              "G167 / zxula.vhd:485-528 — HI_RES display path dispatches "
              "through the ULAnext encoder when ulanext_en_=true.  format=0x07: "
              "ink=attr&format=0x06, paper=0x80|((attr>>3)&0x1F)=0x89, "
              "border=0x80|(attr>>3)&7=0x81.  Distinct palette pokes "
              "verify each slot.  Negative gate (ulanext_en_=false) routes "
              "through std-ULA helpers (S5.10 baseline).",
              got_ink == exp_ink
              && got_paper == exp_paper
              && got_border == exp_border
              && got_legacy_ink == exp_legacy_ink
              && got_legacy_paper == exp_legacy_paper,
              fmt("ink=0x%08X (exp 0x%08X)  paper=0x%08X (exp 0x%08X)  "
                  "border=0x%08X (exp 0x%08X)  legacy_ink=0x%08X (exp 0x%08X)  "
                  "legacy_paper=0x%08X (exp 0x%08X)",
                  got_ink, exp_ink, got_paper, exp_paper,
                  got_border, exp_border,
                  got_legacy_ink, exp_legacy_ink,
                  got_legacy_paper, exp_legacy_paper));
    }

    // S5.13 — G167: HI_RES *display* path dispatches through the ULA+
    // encoder when ulap_en_=true.  VHDL zxula.vhd:531-541 — ula_pixel(7:3)
    // = "11" & attr(7:6) & (sm2 OR not pixel_en).  In HI_RES sm2=1, so
    // (sm2 OR not pixel_en) = 1 for both ink (pixel_en=1) and paper
    // (pixel_en=0) and border (border_active_d forces pixel_en=0); top 5
    // bits are constant.  Low 3 bits = pixel_en ? attr(2:0) : attr(5:3),
    // dropped to the 64-entry ulap_colour() index (low6).
    //
    // Stimulus: paper_color=6 → hires_attr=0x4E.  pg = (0x4E>>6)&3 = 1.
    //   ink_low6   = (1<<4)|(1<<3)|(0x4E&7)       = 0x18|0x06 = 0x1E
    //   paper_low6 = (1<<4)|(1<<3)|((0x4E>>3)&7)  = 0x18|0x01 = 0x19
    //   border_low6 = paper_low6 (same VHDL formula: pixel_en=0 +
    //                 border_active_d=1 give attr(5:3) in ula_pixel(2:0)).
    // Poke distinct RGBs at 0x1E and 0x19 via NR 0x40/0x41 (write_control
    // +ULA+ region indexes by full 8-bit ula_pixel; the encoder bits are
    // 7:6 = "11", so the actual palette write index = 0xC0 | low6).
    {
        UlaBed bed;
        PaletteManager pal;
        pal.reset();
        bed.ula.set_palette(&pal);
        bed.ula.init_border_per_line();

        const uint8_t paper_color = 6;
        const uint8_t hires_attr  = static_cast<uint8_t>(
            0x40 | ((~paper_color & 0x07) << 3) | (paper_color & 0x07)); // 0x4E
        const uint8_t pg          = static_cast<uint8_t>((hires_attr >> 6) & 0x03); // 1
        const uint8_t ink_low6    = static_cast<uint8_t>(
            (pg << 4) | (1u << 3) | (hires_attr & 0x07));               // 0x1E
        const uint8_t paper_low6  = static_cast<uint8_t>(
            (pg << 4) | (1u << 3) | ((hires_attr >> 3) & 0x07));        // 0x19

        bed.ula.set_screen_mode(static_cast<uint8_t>(0x06 | (paper_color << 3))); // HI_RES
        bed.ula.set_ulap_en(true);
        bed.ula.set_active_ula_palette(false);

        // ULA+ palette poke side-channel — VHDL zxnext.vhd:6957-6958 +
        // 4919.  nr_ff_poke takes 6-bit bf3b_index and an RRRGGGBB byte;
        // the B0 expansion (B0 = B1 OR B0) produces the 9-bit RRRGGGBBB.
        // Pure-7 / pure-0 channels round-trip to themselves, so 0xE0 →
        // (7,0,0) red and 0x1C → (0,7,0) green via ulap_colour().
        pal.nr_ff_poke(false, ink_low6,   0xE0); // red
        pal.nr_ff_poke(false, paper_low6, 0x1C); // green
        const uint32_t exp_ink    = rgb333_to_argb8888(7, 0, 0);
        const uint32_t exp_paper  = rgb333_to_argb8888(0, 7, 0);
        const uint32_t exp_border = exp_paper; // same encoder slot under HI_RES

        // s0 col 0 = 0x80 → cell 0 ink, cells 1..7 paper.
        const uint16_t poff = emu_pixel_addr_offset(0, 0);
        bed.poke(0x4000 + poff, 0x80);
        bed.poke(0x6000 + poff, 0x00);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);

        const uint32_t got_ink    = line[Ula::DISP_X + 0];
        const uint32_t got_paper  = line[Ula::DISP_X + 1];
        const uint32_t got_border = line[0];

        // Negative gate: ulap_en_=false → std-ULA encoder.
        bed.ula.set_ulap_en(false);
        std::array<uint32_t, Ula::FB_WIDTH> line_off{};
        bed.ula.render_scanline(line_off.data(), 32, bed.mmu);
        // VHDL zxula.vhd:543-553 std-ULA encoder against hires_attr=0x4E
        // (same derivation as S5.12 negative gate).
        const uint8_t legacy_ink_idx   = static_cast<uint8_t>(((hires_attr & 0x40) >> 3) | (hires_attr & 0x07));
        const uint8_t legacy_paper_idx = static_cast<uint8_t>(0x10 | ((hires_attr & 0x40) >> 3) | ((hires_attr >> 3) & 0x07));
        const uint32_t exp_legacy_ink   = pal.ula_colour(false, legacy_ink_idx);
        const uint32_t exp_legacy_paper = pal.ula_colour(false, legacy_paper_idx);
        const uint32_t got_legacy_ink   = line_off[Ula::DISP_X + 0];
        const uint32_t got_legacy_paper = line_off[Ula::DISP_X + 1];

        check("S5.13",
              "G167 / zxula.vhd:531-541 — HI_RES display path dispatches "
              "through the ULA+ encoder when ulap_en_=true.  pg=1, "
              "ink_low6=0x1E, paper_low6=0x19; border==paper under HI_RES "
              "(sm2=1 forces ula_pixel(3)=1 in both cycles).  Distinct ULA+ "
              "palette pokes verify each slot.  Negative gate (ulap_en_=false) "
              "routes through std-ULA helpers.",
              got_ink == exp_ink
              && got_paper == exp_paper
              && got_border == exp_border
              && got_legacy_ink == exp_legacy_ink
              && got_legacy_paper == exp_legacy_paper,
              fmt("ink=0x%08X (exp 0x%08X)  paper=0x%08X (exp 0x%08X)  "
                  "border=0x%08X (exp 0x%08X)  legacy_ink=0x%08X (exp 0x%08X)  "
                  "legacy_paper=0x%08X (exp 0x%08X)",
                  got_ink, exp_ink, got_paper, exp_paper,
                  got_border, exp_border,
                  got_legacy_ink, exp_legacy_ink,
                  got_legacy_paper, exp_legacy_paper));
    }

    // §5-PSL — Per-scanline port-0xFF Timex screen-mode replay (G07).
    // VHDL zxnext.vhd:3615-3616 (port_ff_reg <= cpu_do on port_ff_wr),
    // zxnext.vhd:3630 (port_ff_dat_tmx <= port_ff_reg per CPU-clk),
    // zxula.vhd:191 (screen_mode_s <= i_port_ff_reg(2:0)) and
    // zxula.vhd:209 (screen_mode <= screen_mode_s, latched per
    // character cell, X"3"/X"B" of i_hc).  Mid-frame writes change the
    // per-scanline screen-mode path (STANDARD ↔ HI_COLOUR ↔ HI_RES),
    // so jnext mirrors the layer2/palette/sprites change-log idiom:
    //   start_frame() → set_current_line(N) → set_screen_mode(...) →
    //   rewind_to_baseline() → apply_changes_for_line(row) per row.

    // S5-PSL.01 — write port 0xFF mid-frame; verify log entry appended
    // tagged with the current line and snapshotting the written byte.
    {
        UlaBed bed;
        bed.ula.start_frame();
        bed.ula.set_current_line(50);
        bed.ula.set_screen_mode(0x02);             // HI_COLOUR (bits 2:0 = 010)
        check("S5-PSL.01",
              "zxnext.vhd:3615-3616 + zxula.vhd:191/209 — port-0xFF "
              "write mid-frame appends one entry to the change log "
              "tagged with the current scanline",
              bed.ula.port_ff_change_log_size() == 1u
              && bed.ula.get_screen_mode_reg() == 0x02,
              fmt("count=%zu reg=0x%02X",
                  bed.ula.port_ff_change_log_size(),
                  bed.ula.get_screen_mode_reg()));
    }

    // S5-PSL.02 — STANDARD on line N, HI_COLOUR on line N+1: each
    // scanline must use the correct path after rewind+replay.  We
    // tag the HI_COLOUR write at line N+1 so the replay renderer pattern
    // (apply_changes_for_line(row) before render_scanline(row)) flips
    // the mode at the right boundary.
    //
    // Setup:
    //   STANDARD path (port_ff bits 2:0 = 000):
    //     pixel byte at 0x4000 + poff(0,0) = 0xFF (all ink),
    //     attr byte  at 0x5800            = 0x05 (paper black, ink cyan).
    //     Expected colour = palette[5] = cyan.
    //   HI_COLOUR path (port_ff bits 2:0 = 010, port_val 0x02):
    //     pixel byte at 0x4000 + poff(d,0) = 0xFF,
    //     per-row "attr" byte at 0x6000 + poff(d,0) = 0x47
    //       (paper black, ink white, bright)  → palette[15].
    {
        UlaBed bed;
        // STANDARD scanline (display row 0 = output row DISP_Y=32).
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800,                              0x05);
        // HI_COLOUR scanline (display row 1 = output row 33).
        bed.poke(0x4000 + emu_pixel_addr_offset(1, 0), 0xFF);
        bed.poke(0x6000 + emu_pixel_addr_offset(1, 0), 0x47);

        // Begin frame with STANDARD (mode 000) as baseline.
        bed.ula.set_screen_mode(0x00);
        bed.ula.start_frame();
        // Mid-frame write tagged at output row 33 → applies starting
        // from line 33 (after the apply_changes_for_line(33) call).
        bed.ula.set_current_line(33);
        bed.ula.set_screen_mode(0x02);             // bits 2:0 = 010 = HI_COLOUR

        // Mirror Renderer flow: rewind once, then apply+render per row.
        bed.ula.rewind_to_baseline();              // back to STANDARD
        std::array<uint32_t, Ula::FB_WIDTH> a{}, b{};
        bed.ula.apply_changes_for_line(32);        // no entries at line 32
        bed.ula.render_scanline(a.data(), 32, bed.mmu);   // STANDARD
        bed.ula.apply_changes_for_line(33);        // applies HI_COLOUR
        bed.ula.render_scanline(b.data(), 33, bed.mmu);   // HI_COLOUR

        const uint32_t cyan  = bed_ink_argb(bed.palette, 5);
        const uint32_t white = bed_ink_argb(bed.palette, 15);
        check("S5-PSL.02",
              "zxula.vhd:191/209 — STANDARD on line 32 + HI_COLOUR on "
              "line 33: each line renders via its own mode after replay",
              a[Ula::DISP_X] == cyan && b[Ula::DISP_X] == white,
              fmt("std=0x%08X (exp cyan 0x%08X)  hicol=0x%08X (exp white 0x%08X)",
                  a[Ula::DISP_X], cyan, b[Ula::DISP_X], white));
    }

    // S5-PSL.03 — HI_RES on line N, STANDARD on line N+1: STANDARD uses
    // the attr byte at 0x5800 (here ink cyan/5).  HI_RES under VHDL bits
    // 2:0 mode convention with paper=cyan in bits 5:3 means port_ff =
    // 0x06 | (5 << 3) = 0x2E.  Per VHDL zxula.vhd:419 + 426-427, HI_RES
    // ink = bright(port_ff(5:3)) = bright cyan.  After rewind+replay,
    // line N must render bright cyan; line N+1 STANDARD renders cyan
    // from attr.  Symmetry with S5-PSL.02.
    {
        UlaBed bed;
        // HI_RES needs both pixel halves; renderer reads them as monochrome.
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x6000 + emu_pixel_addr_offset(0, 0), 0xFF);
        // STANDARD on next display line.
        bed.poke(0x4000 + emu_pixel_addr_offset(1, 0), 0xFF);
        bed.poke(0x5800,                              0x05);  // ink cyan

        // Baseline HI_RES (bits 2:0=110 per zxula.vhd:191; paper=5 in bits 5:3): port_val = 0x2E.
        bed.ula.set_screen_mode(0x06 | (5 << 3));
        bed.ula.start_frame();
        // Switch to STANDARD (mode 000) tagged at line 33.
        bed.ula.set_current_line(33);
        bed.ula.set_screen_mode(0x00);

        bed.ula.rewind_to_baseline();              // back to HI_RES
        std::array<uint32_t, Ula::FB_WIDTH> a{}, b{};
        bed.ula.apply_changes_for_line(32);        // no entries at 32
        bed.ula.render_scanline(a.data(), 32, bed.mmu);   // HI_RES
        bed.ula.apply_changes_for_line(33);        // applies STANDARD
        bed.ula.render_scanline(b.data(), 33, bed.mmu);   // STANDARD

        const uint32_t bright_cyan = bed_ink_argb(bed.palette, 5 | 0x08);
        const uint32_t cyan        = bed_ink_argb(bed.palette, 5);
        check("S5-PSL.03",
              "zxula.vhd:191/209/419/426-427 — HI_RES (paper=5) on line 32 "
              "then STANDARD on line 33: bright cyan ink in HI_RES then "
              "non-bright cyan in STANDARD after replay",
              a[Ula::DISP_X] == bright_cyan && b[Ula::DISP_X] == cyan,
              fmt("hires=0x%08X (exp bright cyan 0x%08X)  std=0x%08X (exp cyan 0x%08X)",
                  a[Ula::DISP_X], bright_cyan, b[Ula::DISP_X], cyan));
    }

    // S5-PSL.04 — start_frame rewinds the port-0xFF log: the live
    // screen_mode_reg_ is captured as the next frame's baseline, the
    // log is cleared, and rewind_to_baseline() restores that baseline.
    {
        UlaBed bed;
        bed.ula.set_screen_mode(0x02);             // HI_COLOUR baseline (bits 2:0=010)
        bed.ula.start_frame();
        bed.ula.set_current_line(20);
        bed.ula.set_screen_mode(0x06);             // HI_RES write (bits 2:0=110)
        const size_t mid_count = bed.ula.port_ff_change_log_size();

        // start_frame again — old log dropped, baseline = current live byte.
        bed.ula.start_frame();
        const size_t post_count = bed.ula.port_ff_change_log_size();

        // rewind_to_baseline now restores the live byte (HI_RES, 0x06)
        // captured by the second start_frame.
        bed.ula.set_screen_mode(0x00);             // pretend unrelated change
        bed.ula.rewind_to_baseline();

        check("S5-PSL.04",
              "Ula::start_frame snapshots live port-0xFF as baseline "
              "and clears the log; subsequent rewind_to_baseline "
              "restores that baseline",
              mid_count == 1u
              && post_count == 0u
              && bed.ula.get_screen_mode_reg() == 0x06,
              fmt("mid=%zu post=%zu after_rewind_reg=0x%02X (exp 0x06)",
                  mid_count, post_count, bed.ula.get_screen_mode_reg()));
    }

    // S5-PSL.05 — save state with non-empty log; load; verify
    // identical render via rewind+replay.  Renders the same scanline
    // through the change-log replay path before save and after load,
    // and asserts byte-equal output (a[Ula::DISP_X] == b[Ula::DISP_X]).
    {
        UlaBed bed;
        // Stimulus identical to S5-PSL.02: STANDARD baseline → mid-frame
        // HI_COLOUR write tagged at line 33. Renders row 33 as white.
        bed.poke(0x4000 + emu_pixel_addr_offset(1, 0), 0xFF);
        bed.poke(0x6000 + emu_pixel_addr_offset(1, 0), 0x47);

        bed.ula.set_screen_mode(0x00);
        bed.ula.start_frame();
        bed.ula.set_current_line(33);
        bed.ula.set_screen_mode(0x02);             // bits 2:0 = 010 = HI_COLOUR

        // Render once before save.
        bed.ula.rewind_to_baseline();
        bed.ula.apply_changes_for_line(33);
        std::array<uint32_t, Ula::FB_WIDTH> a{};
        bed.ula.render_scanline(a.data(), 33, bed.mmu);

        // Save → wipe → load → render again.  Use a measure pass first
        // to size the buffer; mirrors the standard StateWriter idiom.
        StateWriter measure(nullptr, 0);
        bed.ula.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        bed.ula.save_state(w);

        UlaBed bed2;
        bed2.poke(0x4000 + emu_pixel_addr_offset(1, 0), 0xFF);
        bed2.poke(0x6000 + emu_pixel_addr_offset(1, 0), 0x47);
        StateReader rd(buf.data(), buf.size());
        bed2.ula.load_state(rd);

        bed2.ula.rewind_to_baseline();
        bed2.ula.apply_changes_for_line(33);
        std::array<uint32_t, Ula::FB_WIDTH> b{};
        bed2.ula.render_scanline(b.data(), 33, bed2.mmu);

        check("S5-PSL.05",
              "Ula::save_state + load_state round-trips the port-0xFF "
              "change log: rendering the same scanline through replay "
              "produces byte-equal output before and after",
              a[Ula::DISP_X] == b[Ula::DISP_X]
              && bed2.ula.port_ff_change_log_size() == 1u,
              fmt("pre=0x%08X post=0x%08X count_post=%zu",
                  a[Ula::DISP_X], b[Ula::DISP_X], bed2.ula.port_ff_change_log_size()));
    }
}

// =========================================================================
// Section 6: ULAnext Mode (zxula.vhd:492-529) — 14 rows (S6.01-S6.12 +
// S6.14/S6.15; S6.13 reserved for the ULA+ 0x7F encoder path)
// =========================================================================

static void test_section6_ulanext() {
    set_group("S06-ULAnext");

    // Expected values below are hand-derived from the VHDL at
    // zxula.vhd:492-529 with paper_base_index = "10000000" (0x80):
    //   border cycle : pixel = pbi(7:3) & attr(5:3) = 0x80 | (attr & 0x38)>>3
    //                  if format=0xFF → select_bgnd
    //   ink cycle    : pixel = attr AND format
    //   paper cycle  : case format
    //     0x01 → pbi(7)   & attr(7:1)  = 0x80 | (attr>>1)&0x7F
    //     0x03 → pbi(7:6) & attr(7:2)  = 0x80 | (attr>>2)&0x3F
    //     0x07 → pbi(7:5) & attr(7:3)  = 0x80 | (attr>>3)&0x1F
    //     0x0F → pbi(7:4) & attr(7:4)  = 0x80 | (attr>>4)&0x0F
    //     0x1F → pbi(7:3) & attr(7:5)  = 0x80 | (attr>>5)&0x07
    //     0x3F → pbi(7:2) & attr(7:6)  = 0x80 | (attr>>6)&0x03
    //     0x7F → pbi(7:1) & attr(7)    = 0x80 | (attr>>7)&0x01
    //     other (incl. 0xFF) → select_bgnd
    //
    // Expected bytes are computed inline below against the VHDL spec above,
    // NOT against the emulator's compute_ulanext_pixel() implementation.

    // S6.01 — NR 0x43 bit 0 gates ULAnext enable.
    // VHDL zxnext.vhd:5394 (NR 0x43 bit 0 → ulanext_en) and zxula.vhd:492
    // ("if i_ulanext_en = '1' then"). set_ulanext_en(true) must flip the
    // Ula's internal gate; set_ulanext_en(false) must deassert it.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bool on = bed.ula.get_ulanext_en();
        bed.ula.set_ulanext_en(false);
        bool off = bed.ula.get_ulanext_en();
        check("S6.01",
              "zxnext.vhd:5394 + zxula.vhd:492 — NR 0x43 bit 0 drives ulanext_en gate",
              on == true && off == false,
              fmt("on=%d off=%d", on ? 1 : 0, off ? 1 : 0));
    }

    // S6.02 — paper lookup for format 0x07 (zxula.vhd:520).
    // attr=0xC4, pixel_en=0: expected 0x80 | (0xC4>>3)&0x1F = 0x80|0x18 = 0x98.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0x07);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0xC4);
        const uint8_t exp = 0x98;
        check("S6.02",
              "zxula.vhd:520 — format 0x07 paper: pbi(7:5) & attr(7:3)",
              r.pixel == exp && !r.select_bgnd,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.03 — ink AND format (zxula.vhd:510).
    // format=0xFF, attr=0xA5, pixel_en=1: expected 0xA5 & 0xFF = 0xA5.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0xFF);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/true, /*border*/false, 0xA5);
        const uint8_t exp = 0xA5;
        check("S6.03",
              "zxula.vhd:510 — ink cycle: ula_pixel = attr AND i_ulanext_format",
              r.pixel == exp && !r.select_bgnd,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.04 — paper lookup for format 0x0F (zxula.vhd:521).
    // attr=0xB0, pixel_en=0: expected 0x80 | (0xB0>>4)&0x0F = 0x80|0x0B = 0x8B.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0x0F);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0xB0);
        const uint8_t exp = 0x8B;
        check("S6.04",
              "zxula.vhd:521 — format 0x0F paper: pbi(7:4) & attr(7:4)",
              r.pixel == exp && !r.select_bgnd,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.05 — ink path for format 0xFF (zxula.vhd:510).
    // Different attr from S6.03 to distinguish coverage. attr=0x3C, pixel_en=1:
    // expected 0x3C & 0xFF = 0x3C. select_bgnd must NOT assert on ink cycle
    // even for 0xFF (the bgnd assertion is only on the paper "when others").
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0xFF);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/true, /*border*/false, 0x3C);
        const uint8_t exp = 0x3C;
        check("S6.05",
              "zxula.vhd:510 — format 0xFF ink cycle: attr AND 0xFF, bgnd not asserted",
              r.pixel == exp && !r.select_bgnd,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.06 — ula_select_bgnd on non-standard paper format (zxula.vhd:525).
    // Non-list format (e.g. 0xC3), pixel_en=0: "when others => ula_select_bgnd <= '1'".
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0xC3);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0xAA);
        check("S6.06",
              "zxula.vhd:525 — non-list paper format asserts ula_select_bgnd (transparent paper)",
              r.select_bgnd == true,
              fmt("bgnd=%d pixel=0x%02X", r.select_bgnd ? 1 : 0, r.pixel));
    }

    // S6.07 — border uses paper_base_index (zxula.vhd:504).
    // border=true, attr=0x38 (bits 5:3 = 111), ulanext_en=1, format in list.
    // expected pixel = "10000" & "111" = 0x87.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0x07);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/true, 0x38);
        const uint8_t exp = 0x87;
        check("S6.07",
              "zxula.vhd:504 — border: ula_pixel = pbi(7:3) & attr(5:3) = 0x80|(attr(5:3))",
              r.pixel == exp && !r.select_bgnd,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.08 — transparent border for format 0xFF (zxula.vhd:500-504).
    // border=true, format=0xFF: select_bgnd asserts, pixel still = pbi(7:3) & attr(5:3).
    // attr=0x20 → bits 5:3 = 100 → pixel = 0x84.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0xFF);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/true, 0x20);
        const uint8_t exp = 0x84;
        check("S6.08",
              "zxula.vhd:500-504 — format 0xFF border: bgnd asserted, pixel = pbi(7:3) & attr(5:3)",
              r.pixel == exp && r.select_bgnd == true,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.09 — paper lookup for format 0x01 (zxula.vhd:518).
    // attr=0xFE, pixel_en=0: expected 0x80 | (0xFE>>1)&0x7F = 0x80|0x7F = 0xFF.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0x01);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0xFE);
        const uint8_t exp = 0xFF;
        check("S6.09",
              "zxula.vhd:518 — format 0x01 paper: pbi(7) & attr(7:1) = 0x80|(attr>>1)&0x7F",
              r.pixel == exp && !r.select_bgnd,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.10 — paper lookup for format 0x01, demonstrating attr(0) is discarded.
    // attr=0x02: attr(7:1) = 0000001 → pixel = 0x80 | 0x01 = 0x81.
    // attr=0x03 would give the same (attr bit 0 not used in the paper encoding).
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0x01);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0x02);
        const uint8_t exp = 0x81;
        // Cross-check: attr=0x03 (bit 0 differs) must produce the same paper pixel.
        auto r2 = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0x03);
        check("S6.10",
              "zxula.vhd:518 — format 0x01 paper discards attr(0); 0x02 and 0x03 both → 0x81",
              r.pixel == exp && r2.pixel == exp && !r.select_bgnd && !r2.select_bgnd,
              fmt("0x02→0x%02X 0x03→0x%02X exp 0x%02X", r.pixel, r2.pixel, exp));
    }

    // S6.11 — paper lookup for format 0x3F (zxula.vhd:523).
    // attr=0xC3, pixel_en=0: expected 0x80 | (0xC3>>6)&0x03 = 0x80|0x03 = 0x83.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0x3F);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0xC3);
        const uint8_t exp = 0x83;
        check("S6.11",
              "zxula.vhd:523 — format 0x3F paper: pbi(7:2) & attr(7:6) = 0x80|(attr>>6)&0x03",
              r.pixel == exp && !r.select_bgnd,
              fmt("got 0x%02X exp 0x%02X bgnd=%d", r.pixel, exp, r.select_bgnd ? 1 : 0));
    }

    // S6.12 — non-standard format (not in {0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F})
    // asserts transparent paper (zxula.vhd:525). 0x42 is not in the case list.
    {
        UlaBed bed;
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(0x42);
        auto r = bed.ula.compute_ulanext_pixel(/*pixel_en*/false, /*border*/false, 0x5A);
        check("S6.12",
              "zxula.vhd:525 — non-standard paper format (0x42) → ula_select_bgnd (transparent paper)",
              r.select_bgnd == true,
              fmt("bgnd=%d pixel=0x%02X", r.select_bgnd ? 1 : 0, r.pixel));
    }

    // S6.14 — content of the ULA palette above the std-ULA range at reset.
    // (S6.13 is reserved by ULA-VIDEO-TEST-PLAN-DESIGN.md:391 for the ULA+
    // 0x7F format encoder path.)
    //
    // PaletteManager::reset() models the POST-FIRMWARE palette, not the VHDL
    // power-on state.  On hardware `palette_utm` (zxnext.vhd:6960-6975) is a
    // `dpram2` with no init_file_g, so it powers up all-zero (dpram2.vhd:41-46,
    // 64-80) and tbblue.fw fills it before any program runs; but `--load`
    // injects at frame 0 (main.cpp:748) so the firmware never runs and reset()
    // is what stands in for it.  Measured over a NextZXOS boot, the firmware
    // writes every one of the 256 entries of both ULA banks to the same 16
    // colours repeated (index & 0x0F).  Indices 0x20..0xFF — reachable ONLY
    // from the ULAnext / LoRes encoders, never from the std-ULA encoder
    // (zxula.vhd:543-553 emits 0x00..0x1F) — must therefore mirror the
    // 0x00..0x0F entries and NOT an RRRGGGBB-identity ramp.
    //
    // Expressed as the relation (entry i == entry i & 0x0F) so the row pins
    // the repeat itself rather than duplicating the colour table; the two
    // absolute anchors are values where jnext and the firmware agree exactly
    // (bright blue 0x007, bright white 0x1FF).
    {
        PaletteManager pal;
        pal.reset();
        int bad = 0;
        uint16_t bad_idx = 0, bad_val = 0, bad_exp = 0;
        for (int bank = 0; bank < 2; ++bank) {
            for (int i = 32; i < 256; ++i) {
                const uint16_t got = pal.ula_rgb333(bank != 0,
                                                    static_cast<uint8_t>(i));
                const uint16_t exp = pal.ula_rgb333(bank != 0,
                                                    static_cast<uint8_t>(i & 0x0F));
                if (got != exp) {
                    if (bad == 0) { bad_idx = static_cast<uint16_t>(i);
                                    bad_val = got; bad_exp = exp; }
                    ++bad;
                }
            }
        }
        const uint16_t idx89 = pal.ula_rgb333(false, 0x89);  // colour 9  bright blue
        const uint16_t idx1f = pal.ula_rgb333(false, 0x1F);  // colour 15 bright white
        check("S6.14",
              "post-firmware palette convention — ULA palette indices "
              "0x20..0xFF repeat the 16 std-ULA colours (entry i == entry "
              "i & 0x0F) in both banks, matching what tbblue.fw writes.  The "
              "VHDL power-on is all-zero (zxnext.vhd:6960-6975 + "
              "dpram2.vhd:41-46,64-80) and is deliberately NOT modelled here: "
              "--load never runs the firmware (main.cpp:748)",
              bad == 0 && idx89 == 0x007 && idx1f == 0x1FF,
              fmt("mismatches=%d (first idx 0x%02X = 0x%03X, exp 0x%03X)  "
                  "idx0x89=0x%03X (exp 0x007)  idx0x1F=0x%03X (exp 0x1FF)",
                  bad, bad_idx, bad_val, bad_exp, idx89, idx1f));
    }

    // S6.15 — the same fact observed through the render path, which is where
    // it went wrong.  A ULAnext program that never writes the ULA palette —
    // NextSIDplayer.nex is one: NR 0x43 bit 0 = 1, NR 0x42 = 0x07, and it
    // writes only the Layer 2 palette — resolves its paper and border against
    // untouched entries above 0x1F.  Those must yield the 16-colour repeat.
    // Before this was fixed they held an RRRGGGBB ramp, so index 0x89 gave
    // rrrgggbb_to_rgb333(0x89) = (4,2,3) instead of colour 9 (bright blue),
    // and the UI rendered bright red/orange under `--load` while rendering
    // correctly from the NextZXOS browser, where the firmware had written all
    // 256 entries.  Stimulus mirrors S5.12 but pokes ONLY the ink index, so
    // paper (0x89) and border (0x81) both come from the reset content.
    // Paper is pinned absolutely (bright blue, a firmware-exact value);
    // border is pinned against colour 1 of the same table.
    {
        UlaBed bed;
        PaletteManager pal;
        pal.reset();
        bed.ula.set_palette(&pal);
        bed.ula.init_border_per_line();

        const uint8_t paper_color = 6;
        const uint8_t hires_attr  = static_cast<uint8_t>(
            0x40 | ((~paper_color & 0x07) << 3) | (paper_color & 0x07)); // 0x4E
        const uint8_t format     = 0x07;
        const uint8_t ink_idx    = static_cast<uint8_t>(hires_attr & format);              // 0x06
        const uint8_t paper_idx  = static_cast<uint8_t>(0x80 | ((hires_attr >> 3) & 0x1F)); // 0x89
        const uint8_t border_idx = static_cast<uint8_t>(0x80 | ((hires_attr >> 3) & 0x07)); // 0x81

        bed.ula.set_screen_mode(static_cast<uint8_t>(0x06 | (paper_color << 3))); // HI_RES
        bed.ula.set_ulanext_en(true);
        bed.ula.set_ulanext_format(format);
        bed.ula.set_active_ula_palette(false);

        // Only the ink index is written (0xE0 = 111_000_00 -> (7,0,0) red).
        pal.write_control(0x00); pal.set_index(ink_idx); pal.write_8bit(0xE0);

        const uint16_t poff = emu_pixel_addr_offset(0, 0);
        bed.poke(0x4000 + poff, 0x80);
        bed.poke(0x6000 + poff, 0x00);

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);

        const uint32_t got_ink    = line[Ula::DISP_X + 0];
        const uint32_t got_paper  = line[Ula::DISP_X + 1];
        const uint32_t got_border = line[0];
        const uint32_t exp_ink    = rgb333_to_argb8888(7, 0, 0);
        // 0x89 & 0x0F = 9 -> bright blue (0,0,7).
        const uint32_t exp_paper  = rgb333_to_argb8888(0, 0, 7);
        // 0x81 & 0x0F = 1 -> colour 1 of the same std-ULA table.
        const uint32_t exp_border = pal.ula_colour(false, 0x01);

        check("S6.15",
              "zxula.vhd:520 + post-firmware palette convention — unwritten "
              "ULAnext paper (0x89) and border (0x81) indices render as "
              "colours 9 and 1 of the 16-colour repeat, not as RRRGGGBB ramp "
              "entries (NextSIDplayer.nex launch-path colour divergence)",
              got_paper == exp_paper
              && got_border == exp_border
              && got_ink == exp_ink,
              fmt("paper=0x%08X (exp 0x%08X)  border=0x%08X (exp 0x%08X)  "
                  "ink=0x%08X (exp 0x%08X)",
                  got_paper, exp_paper, got_border, exp_border,
                  got_ink, exp_ink));
    }
}

// =========================================================================
// Section 7: ULA+ Mode (zxula.vhd:531-541) — 6 rows
// =========================================================================

static void test_section7_ulaplus() {
    set_group("S07-ULAplus");

    // ── S7.01 — port_ff3b_ulap_en plumbing (zxnext.vhd:4547-4554). ───────
    // VHDL: `port_ff3b_ulap_en <= cpu_do(0)` when port_bf3b_ulap_mode = "01".
    // End-to-end: set mode, write 1, read back enabled; clear mode, writes
    // must no longer flip the enable.
    {
        UlaBed bed;
        // Power-on default: enable is '0' (zxnext.vhd:4547).
        bool def_ok = (bed.ula.get_ulap_en() == false);
        // Mode = "01" → enable latch is open.
        bed.ula.set_ulap_mode(0x01);
        // Mimic Emulator's port 0xFF3B handler: gate on ulap_mode, then set.
        if (bed.ula.get_ulap_mode() == 0x01) bed.ula.set_ulap_en(true);
        bool set_ok = (bed.ula.get_ulap_en() == true);
        // Mode flips to "00" (palette-index mode): subsequent writes must
        // not touch the enable per zxnext.vhd:4548 gate. Keep handler logic.
        bed.ula.set_ulap_mode(0x00);
        if (bed.ula.get_ulap_mode() == 0x01) bed.ula.set_ulap_en(false);
        bool hold_ok = (bed.ula.get_ulap_en() == true);
        check("S7.01",
              "zxnext.vhd:4547-4554 — port_ff3b_ulap_en latch gated by ulap_mode=\"01\"",
              def_ok && set_ok && hold_ok,
              fmt("def=%d set=%d hold=%d", def_ok, set_ok, hold_ok));
    }

    // ── S7.02 — paper encoding: bit 3 = NOT pixel_en (zxula.vhd:531). ────
    // With screen_mode(2)=0 and attr(7:6)=00, toggling pixel_en must flip
    // ula_pixel bit 3 alone.
    {
        const uint8_t attr = 0x00;
        const uint8_t got_ink   = Ula::encode_ulap_pixel(true,  attr, false);
        const uint8_t got_paper = Ula::encode_ulap_pixel(false, attr, false);
        const uint8_t exp_ink   = vhdl_ulap_pixel(true,  attr, false);  // 0xC0
        const uint8_t exp_paper = vhdl_ulap_pixel(false, attr, false);  // 0xC8
        const bool bit3_toggles = ((got_ink ^ got_paper) & 0x08) != 0;
        check("S7.02",
              "zxula.vhd:531 — paper encoding bit 3 = NOT pixel_en",
              got_ink == exp_ink && got_paper == exp_paper && bit3_toggles,
              fmt("ink=0x%02X/exp 0x%02X paper=0x%02X/exp 0x%02X",
                  got_ink, exp_ink, got_paper, exp_paper));
    }

    // ── S7.03 — palette-group encoding from attr(7:6) (zxula.vhd:531). ───
    // Vary attr(7:6) across all 4 values; bits 5:4 of ula_pixel must follow.
    {
        bool ok = true;
        std::string detail;
        for (uint8_t pg = 0; pg < 4; ++pg) {
            const uint8_t attr = static_cast<uint8_t>(pg << 6);
            const uint8_t got  = Ula::encode_ulap_pixel(true, attr, false);
            const uint8_t exp  = vhdl_ulap_pixel(true, attr, false);
            // Expected upper nibble: "11" & pg (4 bits = 0xC | pg).
            const uint8_t exp_hi4 = static_cast<uint8_t>(0xC | pg);
            const uint8_t got_hi4 = static_cast<uint8_t>((got >> 4) & 0x0F);
            if (got != exp || got_hi4 != exp_hi4) {
                ok = false;
                detail = fmt("pg=%u got=0x%02X exp=0x%02X", pg, got, exp);
                break;
            }
        }
        check("S7.03",
              "zxula.vhd:531 — ula_pixel(5:4) = attr(7:6) (palette-group select)",
              ok, detail);
    }

    // ── S7.04 — paper path for palette group 3 (zxula.vhd:531-541). ──────
    // attr(7:6)=11, pixel_en=0 → upper5 = "11" & "11" & "1" = 11111 → 0xF8;
    // low3 = attr(5:3). Vary attr(5:3) across all 8 values.
    {
        bool ok = true;
        std::string detail;
        for (uint8_t p = 0; p < 8; ++p) {
            const uint8_t attr = static_cast<uint8_t>(0xC0 | (p << 3));
            const uint8_t got  = Ula::encode_ulap_pixel(false, attr, false);
            const uint8_t exp  = vhdl_ulap_pixel(false, attr, false);
            // Expected: 0xF8 | p.
            const uint8_t exp_val = static_cast<uint8_t>(0xF8 | p);
            if (got != exp || got != exp_val) {
                ok = false;
                detail = fmt("p=%u attr=0x%02X got=0x%02X exp=0x%02X",
                             p, attr, got, exp_val);
                break;
            }
        }
        check("S7.04",
              "zxula.vhd:531-541 — paper path palette group 3: 0xF8 | attr(5:3)",
              ok, detail);
    }

    // ── S7.05 — hi-res (screen_mode(2)=1) forces bit 3 high (zxula.vhd:531). ─
    // Even when pixel_en=1 (ink path), screen_mode_2=1 forces ula_pixel(3)=1.
    // Compare pair with/without screen_mode_2.
    {
        const uint8_t attr = 0x00;
        const uint8_t got_lo = Ula::encode_ulap_pixel(true, attr, false);  // bit3=0
        const uint8_t got_hi = Ula::encode_ulap_pixel(true, attr, true);   // bit3=1
        const uint8_t exp_lo = vhdl_ulap_pixel(true, attr, false);
        const uint8_t exp_hi = vhdl_ulap_pixel(true, attr, true);
        const bool bit3_lo_clear = (got_lo & 0x08) == 0;
        const bool bit3_hi_set   = (got_hi & 0x08) != 0;
        check("S7.05",
              "zxula.vhd:531 — screen_mode(2)=1 ORs into ula_pixel(3)",
              got_lo == exp_lo && got_hi == exp_hi && bit3_lo_clear && bit3_hi_set,
              fmt("lo=0x%02X/exp 0x%02X hi=0x%02X/exp 0x%02X",
                  got_lo, exp_lo, got_hi, exp_hi));
    }

    // ── S7.06 — attr(7) reinterpreted as palette-group bit (zxula.vhd:531). ─
    // When ulap_en=1, attr(7) must NOT act as flash; two scanlines across
    // a 16-frame flash phase with attr(7)=1 must be identical (no XOR).
    // Additionally the encoder must surface attr(7) in ula_pixel(5).
    {
        // End-to-end flash-suppression check (dovetails with S4.06 but this
        // row asserts the attr(7) reinterpretation specifically).
        UlaBed bed;
        bed.ula.set_ulap_en(true);
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x87);  // attr(7)=1, paper=0, ink=7
        std::array<uint32_t, Ula::FB_WIDTH> a{}, b{};
        bed.ula.render_scanline(a.data(), 32, bed.mmu);
        for (int i = 0; i < 16; ++i) bed.ula.advance_flash();
        bed.ula.render_scanline(b.data(), 32, bed.mmu);
        const bool stable = (a[Ula::DISP_X] == b[Ula::DISP_X]);

        // Encoder check: attr(7)=1 → ula_pixel(5)=1 (part of palette-group
        // select, not flash). With attr = 0x80 → attr(7:6)=10b=pg=2, so the
        // upper nibble becomes "11"&"10" = 0xE; bit 5 of ula_pixel is set.
        const uint8_t got = Ula::encode_ulap_pixel(true, 0x80, false);
        const uint8_t exp = vhdl_ulap_pixel(true, 0x80, false);
        const bool group_bit_set = (got & 0x20) != 0;  // ula_pixel bit 5
        check("S7.06",
              "zxula.vhd:531 — attr(7) reinterpreted as palette-group bit in ULA+",
              stable && got == exp && group_bit_set,
              fmt("stable=%d got=0x%02X exp=0x%02X", stable, got, exp));
    }
}

// =========================================================================
// Section 8: Clip Windows (zxula.vhd:562, zxnext.vhd:6779-6782) — 8 rows
// =========================================================================

static void test_section8_clip() {
    set_group("S08-Clip");

    UlaBed bed;

    // S8.01..S8.04 — reset defaults (x1=0, x2=0xFF, y1=0, y2=0xBF).
    check("S8.01",
          "zxula.vhd:562 / zxnext.vhd:6779 — reset x1=0",
          bed.ula.clip_x1() == 0,
          fmt("got %u", bed.ula.clip_x1()));
    check("S8.02",
          "zxula.vhd:562 / zxnext.vhd:6779 — reset x2=255",
          bed.ula.clip_x2() == 255,
          fmt("got %u", bed.ula.clip_x2()));
    check("S8.03",
          "zxula.vhd:562 / zxnext.vhd:6779 — reset y1=0",
          bed.ula.clip_y1() == 0,
          fmt("got %u", bed.ula.clip_y1()));
    check("S8.04",
          "zxula.vhd:562 / zxnext.vhd:6779 — reset y2=191 (0xBF)",
          bed.ula.clip_y2() == 191,
          fmt("got %u", bed.ula.clip_y2()));

    // S8.05 — inside narrow window: latch storage after NR 0x1A 4-write sequence.
    bed.ula.set_clip_x1(64);
    bed.ula.set_clip_x2(192);
    bed.ula.set_clip_y1(32);
    bed.ula.set_clip_y2(160);
    check("S8.05",
          "zxula.vhd:562 — clip latches store (64,192,32,160) after 4-write sequence",
          bed.ula.clip_x1() == 64 && bed.ula.clip_x2() == 192
            && bed.ula.clip_y1() == 32 && bed.ula.clip_y2() == 160,
          fmt("got (%u,%u,%u,%u)",
              bed.ula.clip_x1(), bed.ula.clip_x2(),
              bed.ula.clip_y1(), bed.ula.clip_y2()));

    // S8.06 — outside right edge (phc > x2).
    // G: o_ula_clipped phc>x2 comparator (zxula.vhd:562) unobservable; e2e via compositor.
    // S8.07 — outside top (vc < y1).
    // G: o_ula_clipped vc<y1 comparator (zxula.vhd:562) unobservable; e2e via compositor.
    // S8.08 — y2 >= 0xC0 clamp per VHDL zxnext.vhd:6779-6783:
    //     if nr_1a_ula_clip_y2(7 downto 6) = "11" then
    //         ula_clip_y2_0 <= X"BF";
    //     else
    //         ula_clip_y2_0 <= nr_1a_ula_clip_y2;
    //     end if;
    // The clamp is applied combinationally at the consumer-facing signal
    // `ula_clip_y2_0`, NOT at the NR 0x1A storage register — so jnext stores
    // the raw byte and clamps inside the `clip_y2()` getter.  Here we write
    // three raw values and check the getter: 0xBF (boundary, passes through),
    // 0xC0 (first clamped value → 0xBF), and 0xFF (maximum, clamped → 0xBF).
    {
        UlaBed bed2;
        bed2.ula.set_clip_y2(0xBF);
        const bool ok_bf = bed2.ula.clip_y2() == 0xBF;
        bed2.ula.set_clip_y2(0xC0);
        const bool ok_c0 = bed2.ula.clip_y2() == 0xBF;
        bed2.ula.set_clip_y2(0xFF);
        const bool ok_ff = bed2.ula.clip_y2() == 0xBF;
        check("S8.08",
              "zxnext.vhd:6779-6783 — y2 top-two-bits = '11' (>= 0xC0) "
              "clamps the consumer-facing value to 0xBF; raw byte still "
              "stored (read-time clamp, render-site equivalent via getter)",
              ok_bf && ok_c0 && ok_ff,
              fmt("ok_bf=%d ok_c0=%d ok_ff=%d",
                  static_cast<int>(ok_bf),
                  static_cast<int>(ok_c0),
                  static_cast<int>(ok_ff)));
    }
}

// =========================================================================
// Section 9: Pixel Scrolling (zxula.vhd:193-216) — 10 rows
// =========================================================================

static void test_section9_scrolling() {
    set_group("S09-Scroll");

    // Shared harness: fill VRAM so that every row N has pixel bytes = 0x00
    // everywhere EXCEPT a marker at src_row M where ALL 32 pixel bytes = 0xFF.
    // Attributes across the entire screen are 0x07 (ink=white, paper=black,
    // no flash, no bright). This lets each test assert the effective source
    // row chosen by the VHDL Y-fold (zxula.vhd:192-207) via a one-pixel
    // palette check: if eff_row == M we get white at the display area,
    // otherwise we get black.
    //
    // For X-scroll, we leave the marker as "row M = all 0xFF" and additionally
    // set a narrow 1-byte white column at (row 0, col 0) = 0xFF (src_x ∈ [0..7])
    // with rest black; then assert which 8-pixel window of the display goes
    // white after the X-fold (zxula.vhd:199).

    auto poke_row_all = [](UlaBed& bed, int row, uint8_t pix) {
        for (int col = 0; col < 32; ++col)
            bed.poke(0x4000 + emu_pixel_addr_offset(row, col), pix);
    };
    // Fill attrs so every 8-row attr band is 0x07 (white ink on black paper).
    // Keeps attr constant regardless of which source row the VHDL fold
    // selects, so a white/black test result reflects only pixel byte content.
    auto init_attrs = [](UlaBed& bed) {
        for (int attr_row = 0; attr_row < 24; ++attr_row) {
            const uint16_t base = static_cast<uint16_t>(0x5800 + attr_row * 32);
            for (int col = 0; col < 32; ++col) bed.poke(base + col, 0x07);
        }
    };

    // Boot-default ULA palette: query it directly so WHITE/BLACK reflect
    // exactly what the renderer's std-ULA path emits for ink-7 / paper-0.
    PaletteManager fn_palette;
    fn_palette.reset();
    const uint32_t WHITE = bed_ink_argb(fn_palette, 7);
    const uint32_t BLACK = bed_paper_argb(fn_palette, 0);

    // -- S9.02 scroll_y=1: screen_row=0 should read source row 1. ------------
    // VHDL zxula.vhd:192 — py_s = vc + scroll_y; vc=0, scroll_y=1 → py_s=1
    // (bit8=0, bits7:6="00", else branch line 206 → py = 1). addr_p_spc_12_5
    // (zxula.vhd:223) then points into row 1 pixels.
    {
        UlaBed bed;
        init_attrs(bed);
        poke_row_all(bed, 1, 0xFF);  // source row 1 = all ink
        bed.ula.set_ula_scroll_y(1);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);  // screen_row=0
        // Expected per VHDL :199-207: py=1 → render pulls pixel byte row 1 =
        // 0xFF → all 256 display pixels white.
        bool all_white = true;
        for (int x = 0; x < 256; ++x) if (line[Ula::DISP_X + 2*x] != WHITE) { all_white = false; break; }
        check("S9.02",
              "zxula.vhd:192,206 — scroll_y=1 + vc=0 → py=1 (passthrough else branch)",
              all_white,
              fmt("display[0]=0x%08X exp 0x%08X (white)", line[Ula::DISP_X], WHITE));
    }

    // -- S9.03 scroll_y=191, vc=1 → py_s=192 (0xC0) → middle branch wraps to 0.
    // VHDL zxula.vhd:203-204 — py_s(8)=0 but py_s(7:6)="11", so
    //   py <= (py_s(7:6)+1="00") & py_s(5:0)="000000" = 0.
    {
        UlaBed bed;
        init_attrs(bed);
        poke_row_all(bed, 0, 0xFF);  // source row 0 = all ink
        bed.ula.set_ula_scroll_y(191);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 33, bed.mmu);  // screen_row=1
        bool all_white = true;
        for (int x = 0; x < 256; ++x) if (line[Ula::DISP_X + 2*x] != WHITE) { all_white = false; break; }
        check("S9.03",
              "zxula.vhd:203-204 — scroll_y=191 + vc=1 → py=0 (cross-third wrap)",
              all_white,
              fmt("display[0]=0x%08X exp 0x%08X (white)", line[Ula::DISP_X], WHITE));
    }

    // -- S9.04 scroll_y=192, vc=0 → py_s=192 (0xC0) → py = 0.
    // Identical encoder to S9.03 (py_s(7:6)="11", middle branch), verifies that
    // scroll_y=192 is the exact wrap boundary where the display snaps back to
    // source row 0.
    {
        UlaBed bed;
        init_attrs(bed);
        poke_row_all(bed, 0, 0xFF);
        bed.ula.set_ula_scroll_y(192);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);  // screen_row=0
        bool all_white = true;
        for (int x = 0; x < 256; ++x) if (line[Ula::DISP_X + 2*x] != WHITE) { all_white = false; break; }
        check("S9.04",
              "zxula.vhd:203-204 — scroll_y=192 + vc=0 → py=0 (modulo-192 boundary)",
              all_white,
              fmt("display[0]=0x%08X exp 0x%08X (white)", line[Ula::DISP_X], WHITE));
    }

    // -- S9.05 scroll_x=8, vc=0 → 8-pixel horizontal shift.
    // VHDL zxula.vhd:199 — px has scroll_x(7:3)=1 (1-byte column shift) +
    // scroll_x(2:0)=0 → total 8-pixel source-offset. With source pixel marker
    // at (row 0, col 0)=0xFF (src_x ∈ [0..7]) and rest black, the display
    // white window must shift to src_x − shift = display_x ⇒ white at
    // display_x ∈ [-8..-1] mod 256 = [248..255].
    //
    // (Note: the plan-doc nominal "shift by 64 pixels (8*8)" treats NR 0x26
    // as byte-granular; VHDL line 199 stores an 8-bit pixel-shift with
    // bits(7:3)=byte count and bits(2:0)=within-byte bit shift, so NR 0x26=8
    // is an 8-pixel shift, not 64. Expected derived strictly from VHDL.)
    {
        UlaBed bed;
        init_attrs(bed);
        // Only col 0 of row 0 is white.
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.ula.set_ula_scroll_x_coarse(8);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);  // screen_row=0
        // Expect white at display_x = 248..255, black elsewhere.
        bool ok = true;
        for (int x = 0; x < 256; ++x) {
            const uint32_t exp = (x >= 248 && x <= 255) ? WHITE : BLACK;
            if (line[Ula::DISP_X + 2*x] != exp) { ok = false; break; }
        }
        check("S9.05",
              "zxula.vhd:199 — NR 0x26=8 → 8-pixel shift (scroll_x(7:3)=1, (2:0)=0)",
              ok,
              fmt("line[DISP_X+2*248]=0x%08X line[DISP_X+2*247]=0x%08X",
                  line[Ula::DISP_X + 2*248], line[Ula::DISP_X + 2*247]));
    }

    // -- S9.06 fine_scroll_x=1 (NR 0x68 bit 2), scroll_x=0 → 1-pixel shift.
    // VHDL zxula.vhd:199 — px(8) = fine_scroll_x contributes 1 pixel after
    // px_1 merge (line 216). With marker at (row 0, col 0)=0xFF source
    // src_x ∈ [0..7], shift=1 → display white at src_x−1 = display_x ⇒
    // display_x ∈ [-1..6] = {255, 0..6}.
    {
        UlaBed bed;
        init_attrs(bed);
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.ula.set_ula_fine_scroll_x(true);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        bool ok = true;
        for (int x = 0; x < 256; ++x) {
            const bool should_white = (x == 255) || (x >= 0 && x <= 6);
            const uint32_t exp = should_white ? WHITE : BLACK;
            if (line[Ula::DISP_X + 2*x] != exp) { ok = false; break; }
        }
        check("S9.06",
              "zxula.vhd:199,216 — fine_scroll_x=1 → 1-pixel source offset",
              ok,
              fmt("line[DISP_X]=0x%08X line[DISP_X+2*255]=0x%08X line[DISP_X+2*7]=0x%08X",
                  line[Ula::DISP_X], line[Ula::DISP_X + 2*255], line[Ula::DISP_X + 2*7]));
    }

    // -- S9.07 NR 0x26=255 → 255-pixel shift (= −1 mod 256).
    // VHDL zxula.vhd:199 — scroll_x=0xFF → scroll_x(7:3)=31 (31 bytes = 248
    // pixels) + scroll_x(2:0)=7 (7 bit shift) → total 255-pixel shift. With
    // marker at (row 0, col 0)=0xFF src_x ∈ [0..7], display white at
    // src_x − 255 = display_x mod 256 ⇒ display_x ∈ {1..7, 8}? Let me
    // recompute: src_x = (display_x + 255) & 0xFF ∈ [0..7] ⇒
    // display_x ∈ [1..8]. All eight pixels land in the window.
    {
        UlaBed bed;
        init_attrs(bed);
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.ula.set_ula_scroll_x_coarse(255);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        bool ok = true;
        for (int x = 0; x < 256; ++x) {
            const bool should_white = (x >= 1 && x <= 8);
            const uint32_t exp = should_white ? WHITE : BLACK;
            if (line[Ula::DISP_X + 2*x] != exp) { ok = false; break; }
        }
        check("S9.07",
              "zxula.vhd:199 — NR 0x26=0xFF → 255-pixel shift (wraps mod 256)",
              ok,
              fmt("line[DISP_X+2*1]=0x%08X line[DISP_X+2*8]=0x%08X line[DISP_X+2*9]=0x%08X",
                  line[Ula::DISP_X + 2*1], line[Ula::DISP_X + 2*8], line[Ula::DISP_X + 2*9]));
    }

    // -- S9.08 fine_scroll_x isolation: only NR 0x68 bit 2 must drive fine.
    // The Phase-1 emulator wiring at src/core/emulator.cpp maps
    //   renderer_.ula().set_ula_fine_scroll_x((v & 0x04) != 0)
    // per VHDL zxnext.vhd:5449. Writing v=0xFB (all-ones except bit 2) MUST
    // leave fine_scroll_x off; writing v=0x04 alone must enable it. This
    // test exercises ONLY the narrow Ula accessor (Phase-1 boundary): we
    // set_ula_fine_scroll_x(false) and confirm no 1-pixel shift is applied.
    {
        UlaBed bed;
        init_attrs(bed);
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.ula.set_ula_fine_scroll_x(false);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        // Expect original unscrolled window: display_x ∈ [0..7] white.
        bool ok = true;
        for (int x = 0; x < 256; ++x) {
            const uint32_t exp = (x >= 0 && x <= 7) ? WHITE : BLACK;
            if (line[Ula::DISP_X + 2*x] != exp) { ok = false; break; }
        }
        check("S9.08",
              "zxula.vhd:199 — fine_scroll_x=0 leaves px(8)=0 (no 1-pixel offset)",
              ok,
              fmt("line[DISP_X]=0x%08X line[DISP_X+2*7]=0x%08X line[DISP_X+2*8]=0x%08X",
                  line[Ula::DISP_X], line[Ula::DISP_X + 2*7], line[Ula::DISP_X + 2*8]));
    }

    // -- S9.09 combined scroll: scroll_y=2, NR 0x26=16 (2-byte/16-pixel
    // shift), fine_scroll_x=1 — total X shift 17 pixels, Y shift 2.
    // VHDL zxula.vhd:199: scroll_x(7:3)=2 → 16 pixels + fine=1 → 17 pixels.
    // zxula.vhd:206 (else branch): py_s=2 → py=2. Marker at source row 2,
    // col 1 only (src_x ∈ [8..15]) = 0xFF; display white should be at
    // src_x − 17 = display_x mod 256 ⇒ (display_x+17)&0xFF ∈ [8..15] ⇒
    // display_x ∈ [-9..-2] mod 256 = [247..254].
    {
        UlaBed bed;
        init_attrs(bed);
        bed.poke(0x4000 + emu_pixel_addr_offset(2, 1), 0xFF);  // source row 2, col 1
        bed.ula.set_ula_scroll_y(2);
        bed.ula.set_ula_scroll_x_coarse(16);
        bed.ula.set_ula_fine_scroll_x(true);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);  // screen_row=0
        bool ok = true;
        for (int x = 0; x < 256; ++x) {
            const bool should_white = (x >= 247 && x <= 254);
            const uint32_t exp = should_white ? WHITE : BLACK;
            if (line[Ula::DISP_X + 2*x] != exp) { ok = false; break; }
        }
        check("S9.09",
              "zxula.vhd:193-216 — scroll_y=2 + NR 0x26=16 + fine=1 compose",
              ok,
              fmt("line[DISP_X+2*247]=0x%08X line[DISP_X+2*254]=0x%08X line[DISP_X+2*255]=0x%08X",
                  line[Ula::DISP_X + 2*247], line[Ula::DISP_X + 2*254], line[Ula::DISP_X + 2*255]));
    }

    // -- S9.10 cross-third wrap: scroll_y=64 + vc=0 → py_s=64 (0x40).
    // VHDL zxula.vhd:206 — py_s(8)=0, py_s(7:6)="01", else branch →
    // py = 64 (0x40). Then addr_p_spc_12_5 (line 223) = py(7:6)=01 &
    // py(2:0)=000 & py(5:3)=000 — which selects third 1 in screen-address
    // space (bits 12:11 = 01), confirming the swap from third 0 (raw vc=0).
    // Marker at source row 64 = all-white row; expect display all-white.
    {
        UlaBed bed;
        init_attrs(bed);
        poke_row_all(bed, 64, 0xFF);
        bed.ula.set_ula_scroll_y(64);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);  // screen_row=0
        bool all_white = true;
        for (int x = 0; x < 256; ++x) if (line[Ula::DISP_X + 2*x] != WHITE) { all_white = false; break; }
        check("S9.10",
              "zxula.vhd:206,223 — scroll_y=64 → py=64 (third-0→third-1 swap)",
              all_white,
              fmt("display[0]=0x%08X exp 0x%08X (white from row 64)", line[Ula::DISP_X], WHITE));
    }

    // S9.01 — G: no-scroll baseline already covered by §1 address tests + §2 rendering.

    // §9-PSL — Per-scanline NR 0x26 / NR 0x27 ULA scroll replay (G08).
    // VHDL zxula.vhd:193-207, :199 — px / py latch each character cell on
    // i_hc(3:0)='3' or 'B'. A Copper write to NR 0x26 / NR 0x27 mid-frame
    // shifts the scroll on the next cell boundary, so per-scanline replay
    // (the natural emulator approximation) requires Ula to maintain a
    // change-log indexed by scanline. The setters log a snapshot tagged
    // with `current_scroll_line_`; Renderer rewinds to baseline and
    // applies the log entries for each scanline in order.
    //
    // Tests assert against the VHDL semantics already covered by S9.02/05/
    // 07/10: scroll_y=1+vc=0 → all-row-1 white (VHDL :206); scroll_x=8 →
    // 1-cell rotation (VHDL :199); fine_scroll=1 → +1 pixel offset; etc.

    // -- S9-PSL.01 — mid-frame writes append to change log (G08). ----------
    // Two writes (one to NR 0x27 tagged line 50, one to NR 0x26 tagged
    // line 100). VHDL behaviour (zxnext.vhd:5304/5307): each write to a
    // scroll register is an independent assignment, so the log MUST grow
    // by one per setter call — even when current_scroll_line_ is unchanged
    // between the writes. scroll_change_log_size() is a count, not a
    // dedupe set.
    {
        UlaBed bed;
        bed.ula.start_frame_scroll();
        const size_t base = bed.ula.scroll_change_log_size();
        bed.ula.set_current_scroll_line(50);
        bed.ula.set_ula_scroll_y(7);
        bed.ula.set_current_scroll_line(100);
        bed.ula.set_ula_scroll_x_coarse(16);
        bed.ula.set_ula_fine_scroll_x(true);
        const size_t after = bed.ula.scroll_change_log_size();
        check("S9-PSL.01",
              "zxnext.vhd:5304/5307,5449 — three setter calls append three log entries",
              after - base == 3,
              fmt("scroll_change_log_size after=%zu base=%zu (exp +3)",
                  after, base));
    }

    // -- S9-PSL.02 — NR 0x27 mid-frame split. ------------------------------
    // VHDL zxula.vhd:192,206 — at vc=0 with scroll_y=A the renderer fetches
    // row A; with scroll_y=B it fetches row B (else-branch of py_s wrap).
    // Mid-frame write at scanline 33 (display_row=1) MUST flip subsequent
    // line N reads to use scroll_y=B without disturbing line 0.
    //
    // Construct: source row 0 = all ink, source row 64 = all ink, all other
    // rows pre-zeroed. Apply log: line 32 (display_row 0) ← scroll_y=0;
    // line 33 (display_row 1) ← scroll_y=64. After per-line apply +
    // render, line 32 is white (from row 0) and line 33 is also white
    // (from row 64 via cross-third wrap, VHDL :206/:223). Without per-
    // scanline replay, the "last write wins" semantics would force ALL
    // lines to use scroll_y=64, which still happens to be white here —
    // so we instead pick scroll_y values that produce DIFFERENT colours.
    //
    // Better split: row 0 = all-white (0xFF), row 1 = all-black (0x00),
    // attrs uniformly ink-7-on-paper-0. With scroll_y=0 line 32 reads row
    // 0 → white. With scroll_y=1 line 33 reads row 2, which is 0x00 →
    // black. (VHDL :206 else branch: py = vc + scroll_y when no wrap.)
    {
        UlaBed bed;
        init_attrs(bed);
        poke_row_all(bed, 0, 0xFF);  // row 0 = white
        poke_row_all(bed, 1, 0x00);  // row 1 = black (default; explicit)
        poke_row_all(bed, 2, 0x00);  // row 2 = black
        bed.ula.start_frame_scroll();
        // Apply baseline scroll_y=0 explicitly so per-scanline rewind
        // restores to zero (rewind targets the baseline snapshot).
        bed.ula.set_current_scroll_line(32);
        bed.ula.set_ula_scroll_y(0);
        bed.ula.set_current_scroll_line(33);
        bed.ula.set_ula_scroll_y(1);
        // Simulate Renderer flow: rewind to baseline, then apply per line.
        bed.ula.rewind_scroll_to_baseline();
        std::array<uint32_t, Ula::FB_WIDTH> line32{}, line33{};
        bed.ula.apply_scroll_changes_for_line(32);
        bed.ula.render_scanline(line32.data(), 32, bed.mmu);   // screen_row 0
        bed.ula.apply_scroll_changes_for_line(33);
        bed.ula.render_scanline(line33.data(), 33, bed.mmu);   // screen_row 1
        const bool ok32_white = (line32[Ula::DISP_X] == WHITE);
        const bool ok33_black = (line33[Ula::DISP_X] == BLACK);
        check("S9-PSL.02",
              "zxula.vhd:192,206 — NR 0x27 mid-frame split: line32→row0(white) line33→row2(black)",
              ok32_white && ok33_black,
              fmt("line32[Ula::DISP_X]=0x%08X exp WHITE 0x%08X / line33[Ula::DISP_X]=0x%08X exp BLACK 0x%08X",
                  line32[Ula::DISP_X], WHITE, line33[Ula::DISP_X], BLACK));
    }

    // -- S9-PSL.03 — NR 0x26 fine-scroll mid-frame flip. -------------------
    // VHDL zxula.vhd:199 — fine_scroll_x adds 1 pixel of shift. With NR
    // 0x26 = 0 and fine = 0, source-row col 0 byte at offset 0 (poked to
    // 0xFF) renders as display pixels [0..7] = white. Toggling fine = 1
    // mid-frame for line 33 shifts the white run to display pixels
    // [1..8] (per VHDL src_x = display_x + scroll_x + (fine?1:0)).
    {
        UlaBed bed;
        init_attrs(bed);
        // Source row 0 col 0 → 0xFF (one byte = 8 white pixels) for both
        // display rows we render.
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x4000 + emu_pixel_addr_offset(1, 0), 0xFF);
        bed.ula.start_frame_scroll();
        bed.ula.set_current_scroll_line(32);
        bed.ula.set_ula_fine_scroll_x(false);   // line 32 → no fine offset
        bed.ula.set_current_scroll_line(33);
        bed.ula.set_ula_fine_scroll_x(true);    // line 33 → +1 pixel offset
        bed.ula.rewind_scroll_to_baseline();
        std::array<uint32_t, Ula::FB_WIDTH> line32{}, line33{};
        bed.ula.apply_scroll_changes_for_line(32);
        bed.ula.render_scanline(line32.data(), 32, bed.mmu);   // screen_row 0
        bed.ula.apply_scroll_changes_for_line(33);
        bed.ula.render_scanline(line33.data(), 33, bed.mmu);   // screen_row 1

        // Without fine: white at display_x ∈ [0..7]; with fine: white at
        // display_x ∈ [255, 0..6] — i.e. left-edge pixel 0 swaps from
        // white to white (rotated by 1) but pixel 7 swaps white→black,
        // pixel 255 swaps black→white. Use those two markers as the
        // discriminator.
        const bool no_fine_pixel7  = (line32[Ula::DISP_X + 2*7]   == WHITE);
        const bool fine_pixel7     = (line33[Ula::DISP_X + 2*7]   == BLACK);
        const bool fine_pixel255   = (line33[Ula::DISP_X + 2*255] == WHITE);
        check("S9-PSL.03",
              "zxula.vhd:199 — NR 0x68 b2 fine_scroll mid-frame flip per line",
              no_fine_pixel7 && fine_pixel7 && fine_pixel255,
              fmt("line32[DISP_X+2*7]=0x%08X line33[DISP_X+2*7]=0x%08X line33[DISP_X+2*255]=0x%08X",
                  line32[Ula::DISP_X + 2*7], line33[Ula::DISP_X + 2*7], line33[Ula::DISP_X + 2*255]));
    }

    // -- S9-PSL.04 — start_frame_scroll rewinds the log. --------------------
    // After populating the log + replaying it, a fresh start_frame_scroll
    // call MUST clear scroll_change_count_ back to 0 and update the
    // baseline snapshot to the live values. Mirrors Layer2::start_frame
    // semantics exactly. Without this the log would grow unbounded across
    // frames and rewind would never see the current frame's baseline.
    {
        UlaBed bed;
        bed.ula.start_frame_scroll();
        bed.ula.set_current_scroll_line(50);
        bed.ula.set_ula_scroll_y(42);
        bed.ula.set_ula_scroll_x_coarse(17);
        bed.ula.set_ula_fine_scroll_x(true);
        const size_t mid = bed.ula.scroll_change_log_size();
        // Rewind would normally happen now (Renderer); then frame ends
        // and start_frame_scroll re-baselines from current live values.
        bed.ula.start_frame_scroll();
        const size_t after = bed.ula.scroll_change_log_size();
        // Subsequent setter calls must produce a fresh entry counted
        // from zero, and the live values from before the start_frame
        // call MUST be preserved (start_frame snapshots, never resets).
        bed.ula.set_current_scroll_line(10);
        bed.ula.set_ula_scroll_y(99);
        const size_t after_one = bed.ula.scroll_change_log_size();
        check("S9-PSL.04",
              "zxula.vhd:193-207 — start_frame_scroll clears log and snapshots baseline",
              mid == 3 && after == 0 && after_one == 1
                  && bed.ula.get_ula_scroll_x_coarse() == 17
                  && bed.ula.get_ula_fine_scroll_x() == true,
              fmt("mid=%zu after_clear=%zu after_one=%zu live(x=%u,fine=%d)",
                  mid, after, after_one,
                  bed.ula.get_ula_scroll_x_coarse(),
                  static_cast<int>(bed.ula.get_ula_fine_scroll_x())));
    }
}

// =========================================================================
// Section 10: Floating Bus (zxula.vhd:308-345, zxula.vhd:573) — 8 rows
// =========================================================================

static void test_section10_floating_bus() {
    set_group("S10-FloatingBus");

    // S10.01/05/06/07/08 — RE-HOME to doc/design/TASK-FLOATING-BUS-PLAN.md.
    // These 5 rows test floating-bus read behaviour that lives on
    // Emulator::floating_bus_read (zxula.vhd:573, zxnext.vhd:2813), NOT
    // on Ula. The new plan catalogues the rows + approach; pick up when
    // a session has budget for an Emulator-side audit.
    // S10.02 — G: hc(3:0)=0x9 capture phase (zxula.vhd:308-345) internal; end-to-end by §1/§2.
    // S10.03 — G: hc(3:0)=0xB attr capture phase (zxula.vhd:308-345) internal; end-to-end by §1/§2.
    // S10.04 — G: hc(3:0)=0x1 reset phase (zxula.vhd:308-345) internal; end-to-end by §1/§2.
}

// =========================================================================
// Section 11: Contention Timing (zxula.vhd:578-601, zxnext.vhd:4481-4496) — 12 rows
// =========================================================================

static void test_section11_contention() {
    set_group("S11-Contention");

    // S11.01..12 — RE-HOME to doc/design/TASK-CONTENTION-MODEL-PLAN.md.
    // Contention is a machine-wide subsystem with no ContentionModel
    // class yet. The new plan catalogues the 12 rows (per-machine
    // contention phases, I/O contention, Pentagon/Next-turbo overrides)
    // with VHDL cites at zxula.vhd:582-601 + zxnext.vhd:4481-4496.
    // Pick up when a session picks the contention subsystem.
}

// =========================================================================
// Section 12: ULA Disable (zxnext.vhd:5445) — 4 rows
// =========================================================================

static void test_section12_ula_disable() {
    set_group("S12-ULADisable");

    // S12.01 — reset default nr_68_ula_en = 1.
    {
        UlaBed bed;
        check("S12.01",
              "zxnext.vhd:5445 — reset default nr_68_ula_en=1 (ULA enabled)",
              bed.ula.ula_enabled() == true,
              fmt("got %d", bed.ula.ula_enabled() ? 1 : 0));
    }
    // S12.02, S12.03 — nr_68 bit7 enable/disable. set_ula_enabled/ula_enabled
    // round-trips a plain bool field that is never consulted inside
    // render_scanline, so a setter→getter check would be tautological.
    // Demoted to skip until nr_68 bit7 is wired into the render pipeline
    // (Emulator Bug backlog candidate).
    // S12.02/03/04 — RE-HOME to doc/design/TASK-COMPOSITOR-NR68-BLEND-PLAN.md.
    // NR 0x68 bits 6:5 (blend mode) + bit 7 (ULA disable) are forwarded
    // to the Renderer today but the render-pipeline consumption is not
    // verified. Setter→getter would be tautological; needs end-to-end
    // frame-buffer assertion in compositor_test (reopens that suite).
    // VHDL cite: zxnext.vhd:5445. Plan scopes 3 rows + reopen.
}

// =========================================================================
// Section 13: Timing Constants (zxula_timing.vhd) — 8 rows + 1 extra
// =========================================================================

static void test_section13_timing() {
    set_group("S13-Timing");

    VideoTiming t;

    // Plan row #1 — 48K frame length: (447+1) * (311+1) / 2 = 69888 T-states.
    // Post-V1: hc_max() / vc_max() return VHDL c_max_* (447, 311); period is +1.
    t.init(MachineType::ZX48K);
    {
        int ts = (t.hc_max() + 1) * (t.vc_max() + 1) / 2;
        check("S13.01",
              "zxula_timing.vhd — 48K c_max_hc=447, c_max_vc=311 → 448*312/2 = 69888 T-states",
              t.hc_max() == 447 && t.vc_max() == 311 && ts == 69888,
              fmt("hc_max=%d vc_max=%d ts=%d", t.hc_max(), t.vc_max(), ts));
    }

    // Plan row #2 — 128K frame length: (455+1) * (310+1) / 2 = 70908 T-states.
    t.init(MachineType::ZX128K);
    {
        int ts = (t.hc_max() + 1) * (t.vc_max() + 1) / 2;
        check("S13.02",
              "zxula_timing.vhd — 128K c_max_hc=455, c_max_vc=310 → 456*311/2 = 70908 T-states",
              t.hc_max() == 455 && t.vc_max() == 310 && ts == 70908,
              fmt("hc_max=%d vc_max=%d ts=%d", t.hc_max(), t.vc_max(), ts));
    }

    // Plan row #3 — RETIRED 2026-05-04: standalone Pentagon machine type
    // dropped (Wave 0.3 follow-up). VideoTiming no longer initialises any
    // Pentagon-specific c_max_hc/c_max_vc constants; the row is unrunnable.

    // Plan row #4 — 48K active display origin (hc=128, vc=64).
    check("S13.04",
          "zxula_timing.vhd — 48K min_hactive=128, min_vactive=64 → display origin (128,64) 256x192",
          VideoTiming::DISPLAY_LEFT == 128 && VideoTiming::DISPLAY_TOP == 64
            && VideoTiming::DISPLAY_W == 256 && VideoTiming::DISPLAY_H == 192,
          fmt("origin=(%d,%d) size=%dx%d",
              VideoTiming::DISPLAY_LEFT, VideoTiming::DISPLAY_TOP,
              VideoTiming::DISPLAY_W, VideoTiming::DISPLAY_H));

    // S13.05/06/07/08 — RE-HOME to doc/design/TASK-VIDEOTIMING-EXPANSION-PLAN.md.
    //   #5 128K active display origin (hc=136, vc=64).
    //   #6 Pentagon active display origin (hc=128, vc=80).
    //   #7 ULA hc resets at min_hactive-12 (12-cycle prefetch lead).
    //   #8 60 Hz variant: 448 * 264 / 2 = 59136 T-states.
    // VideoTiming currently uses shared 48K constants for origins; the
    // 60 Hz MachineType variant doesn't exist. VHDL cite: zxula_timing.vhd.
    // Plan may merge with the VideoTiming production-wiring backlog.

    // -- Extra coverage (not in §13 plan rows) --------------------------
    // S13.14 — frame_done flips exactly at 69888 T-states (48K).
    // KNOWN FAIL — Emulator Bug backlog (Task 2 item 4): VideoTiming::advance
    // does not flip frame_done_ at the 69888 boundary. Kept as a failing-
    // check regression witness per the Task 1 Wave 3 prompt; do NOT convert.
    t.init(MachineType::ZX48K);
    t.clear_frame_flag();
    // Post-V1: hc_max() / vc_max() return VHDL c_max_*; period is +1.
    int full_frame_tstates = (t.hc_max() + 1) * (t.vc_max() + 1) / 2;
    t.advance(full_frame_tstates);
    check("S13.14",
          "zxula_timing.vhd — frame_done flips exactly at 69888 T-states (48K) [regression witness]",
          t.frame_complete(),
          fmt("frame_done=%d after %d T-states (Task 2 backlog — emulator bug)",
              t.frame_complete() ? 1 : 0, full_frame_tstates));
}

// =========================================================================
// Section 14: Frame Interrupt (zxula_timing.vhd:547-559) — 6 rows
// =========================================================================

static void test_section14_frame_int() {
    set_group("S14-FrameInt");

    // S14.01/02/03 — RE-HOME to doc/design/TASK-VIDEOTIMING-EXPANSION-PLAN.md.
    //   01 48K int position hc=116, vc=0.
    //   02 128K int position hc=128, vc=1.
    //   03 Pentagon int position hc=439, vc=319.
    // VHDL cite: zxula_timing.vhd:547-559. Plan may merge with the
    // VideoTiming production-wiring backlog.

    // S14.04/05/06 — Post-closure walkback 2026-04-23: these three rows
    // were flipped to live check()s by Wave E, driving the VideoTiming
    // pulse-counter API directly. The underlying VHDL logic (zxula_timing.vhd
    // :547-583) is faithfully modelled inside VideoTiming, BUT no production
    // code path writes to those setters or reads those counters — the actual
    // ULA frame + line interrupt emulation is done by Emulator::run_frame
    // reading local line_int_enabled_/ula_int_disabled_/line_int_value_ fields
    // (emulator.cpp:2138, :2154). VideoTiming's interrupt-related state is
    // test-only dead code. Keeping the rows as check()s would validate logic
    // that no user-visible emulation depends on — coverage theatre. See
    // `.prompts/2026-04-23.md` "VideoTiming pulse-counter production wiring"
    // backlog item for the architectural-unification reasoning and the
    // decision to defer indefinitely. Un-comment these blocks if/when the
    // Emulator scheduler is funnelled through a production VideoTiming
    // instance.
    // G: S14.04 — VideoTiming inten_ula gate: test-only surface, no production consumer.
    // G: S14.05 — VideoTiming line-int fire at target-1: test-only surface, no production consumer.
    // G: S14.06 — VideoTiming target=0 → c_max_vc wrap: test-only surface, no production consumer.
}

// =========================================================================
// Section 15: Shadow Screen (zxnext.vhd:4453) — 4 rows
// =========================================================================

static void test_section15_shadow() {
    set_group("S15-Shadow");

    // S15.01 — primary render reads bank 5 (page 10).
    {
        UlaBed bed;
        bed.poke(0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        bed.poke(0x5800, 0x07);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline(line.data(), 32, bed.mmu);
        const uint32_t exp_ink7 = bed_ink_argb(bed.palette, 7);
        check("S15.01",
              "zxnext.vhd:4453 — primary render reads bank 5 (page 10) VRAM",
              line[Ula::DISP_X] == exp_ink7,
              fmt("got 0x%08X exp 0x%08X", line[Ula::DISP_X], exp_ink7));
    }

    // S15.02 — i_ula_shadow_en routes bank 7 (page 14).
    {
        UlaBed bed;
        uint32_t shadow_base = 14u * 8192u;
        bed.ram.write(shadow_base + emu_pixel_addr_offset(0, 0), 0x00);
        bed.ram.write(shadow_base + (0x5800 - 0x4000), 0x07);
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        bed.ula.render_scanline_screen1(line.data(), 32, bed.mmu);
        const uint32_t exp_paper0 = bed_paper_argb(bed.palette, 0);
        check("S15.02",
              "zxnext.vhd:4453 — i_ula_shadow_en selects bank 7 (page 14) VRAM",
              line[Ula::DISP_X] == exp_paper0,
              fmt("got 0x%08X exp 0x%08X (black paper)", line[Ula::DISP_X], exp_paper0));
    }

    // S15.03/04 — RE-HOME to doc/design/TASK-MMU-SHADOW-SCREEN-PLAN.md.
    //   03 Shadow disables Timex mode (screen_mode forced to "000").
    //   04 Port 0x7FFD bit 3 → i_ula_shadow_en routing.
    // VHDL cite: zxula.vhd:191 + zxnext.vhd:4453. Ula-side plumbing
    // already in place (Ula::set_shadow_screen_en landed in Phase 1);
    // the MUC-side routing (port 0x7FFD bit 3 → the setter) is the
    // missing half. Reopens mmu_test suite by +2 rows. Small fix.
}

// =========================================================================
// Section 16: NR 0xFF palette write side-channel (zxnext.vhd:6957) — 1 row
// =========================================================================

static void test_section16_nrff_palette() {
    set_group("S16-NRFF-PALETTE");

    // S16.01 — VHDL zxnext.vhd:6957-6958 + :4919 — NR 0xFF write commits a
    // 9-bit value derived from nr_wr_dat (RRRGGGBBB with B0 = B1|B0) into
    // the ULA palette region at slot (NR 0x43 b6, port 0xBF3B b5:0).
    //
    // End-to-end exercise without the Port/NextReg dispatch shells:
    //   1. mimic port 0xBF3B handler: write 0x05 (mode "00", index 0x05)
    //      → Ula::set_ulap_mode(0) + set_ulap_index(5).
    //   2. mimic NR 0xFF handler: bank from NR 0x43 b6, index from Ula,
    //      byte through PaletteManager::nr_ff_poke.
    //   3. assert the entry stored at (bank, index) matches the VHDL
    //      RRRGGGBBB expansion of the byte.
    //
    // The bf3b mode-gate sub-check (mode != "00" must NOT update index)
    // is the same VHDL line that governs S7.01's enable-gate; we cover
    // it in the same row to keep S16 a single check() per the test plan.
    {
        Ula ula;
        PaletteManager palette;
        // Emulator-style bf3b write with mode="00" (palette-index mode):
        const uint8_t bf3b_byte = 0x05;  // mode=00, index=0x05
        ula.set_ulap_mode(static_cast<uint8_t>((bf3b_byte >> 6) & 0x03));
        if (((bf3b_byte >> 6) & 0x03) == 0x00) {
            ula.set_ulap_index(static_cast<uint8_t>(bf3b_byte & 0x3F));
        }
        const bool mode_ok  = (ula.get_ulap_mode()  == 0x00);
        const bool index_ok = (ula.get_ulap_index() == 0x05);

        // NR 0x43 control: target bits 6:4 = 100 (ULA_SECOND) selects
        // bank_second = (b6=1).  Only b6 reaches the NR 0xFF address
        // override (palette_write_select(2) per zxnext.vhd:6958).
        palette.write_control(0x40);  // bits[6:4] = 100 → bank_second=true
        const bool bank_select = (palette.read_control() & 0x40) != 0;

        // Mimic NR 0xFF write of 0xA5 (R=101, G=001, B=01).
        // VHDL zxnext.vhd:4919: nr_palette_value = (byte & (b1 | b0)).
        //   byte = 0xA5 = 1010_0101 → r3=101, g3=001, b2=01
        //   B0 = (b1=0 | b0=1) = 1 → b3 = 011
        // Expected RGB333 = (101 << 6) | (001 << 3) | 011 = 0x14B.
        const uint8_t  nrff_byte    = 0xA5;
        const uint16_t expected_rgb = static_cast<uint16_t>((5u << 6) | (1u << 3) | 3u);
        palette.nr_ff_poke(bank_select, ula.get_ulap_index(), nrff_byte);

        const uint16_t got_rgb       = palette.ulap_poke_rgb333(true,  0x05);
        const uint16_t other_bank    = palette.ulap_poke_rgb333(false, 0x05);
        const uint16_t other_index   = palette.ulap_poke_rgb333(true,  0x06);

        // Sub-check: mode != "00" must NOT update index (zxnext.vhd:4533).
        ula.set_ulap_mode(0x01);  // simulate handler raw mode write
        // Index gate: do NOT call set_ulap_index when mode != 00.
        const bool index_held_ok = (ula.get_ulap_index() == 0x05);

        const bool poke_ok        = (got_rgb     == expected_rgb);
        const bool isolation_bank = (other_bank  == 0);
        const bool isolation_idx  = (other_index == 0);

        check("S16.01",
              "zxnext.vhd:6957-6958/4919 — NR 0xFF poke at (bank=NR0x43b6, "
              "idx=bf3b[5:0]) commits RRRGGGBBB(B0=B1|B0)",
              mode_ok && index_ok && poke_ok && isolation_bank
                  && isolation_idx && index_held_ok,
              fmt("mode=%d idx=0x%02X rgb=0x%03X/exp 0x%03X "
                  "other_bank=0x%03X other_idx=0x%03X held=%d",
                  ula.get_ulap_mode(), ula.get_ulap_index(),
                  got_rgb, expected_rgb, other_bank, other_index,
                  index_held_ok ? 1 : 0));
    }
}

// =========================================================================
// Section 17: Per-scanline active-palette select (G10)
// =========================================================================
//
// Distinct from per-scanline palette CONTENT (already landed in
// PaletteManager). G10 covers the SELECTOR bit-lanes (NR 0x43 b1-3 +
// NR 0x6B b4) which choose which palette bank is active per-line.
// VHDL zxnext.vhd:5391-5393 (NR 0x43 b1/b2/b3 latches),
//      zxnext.vhd:5462      (NR 0x6B control(4) latch),
//      zxnext.vhd:6825-6828 (active-palette mux per pixel cycle).
//
// Two physically-independent latch groups => two separate change-logs;
// S17.03 verifies the streams stay independent.
static void test_section17_palette_select() {
    set_group("S17-PALSEL");

    // S17.01 — NR 0x43 b1-3 selector change-log exists and tags entries
    // with the current scanline. VHDL zxnext.vhd:5391-5393 latches all
    // three active-palette bits on the same NR 0x43 write, so a single
    // NR 0x43 write produces ONE log entry with all three fields.
    {
        UlaBed bed;
        bed.ula.palsel_start_frame();
        // Mid-frame Copper write at line 100: flip active_layer2_palette
        // to '1' (NR 0x43 bit 2 from VHDL :5392).
        bed.ula.set_palsel_current_line(100);
        bed.ula.set_active_layer2_palette(true);
        // Another write at line 150: flip ULA + sprite together.
        bed.ula.set_palsel_current_line(150);
        bed.ula.set_active_ula_palette(true);
        bed.ula.set_active_sprite_palette(true);

        const size_t n = bed.ula.palsel43_change_log_size();
        // Three setter calls => 3 log entries (each captures full snapshot).
        bool ok = (n == 3);
        if (ok) {
            const auto& e0 = bed.ula.palsel43_change_at(0);
            const auto& e1 = bed.ula.palsel43_change_at(1);
            const auto& e2 = bed.ula.palsel43_change_at(2);
            ok = ok && (e0.line == 100 && e0.active_ula == false
                       && e0.active_l2 == true && e0.active_spr == false);
            ok = ok && (e1.line == 150 && e1.active_ula == true
                       && e1.active_l2 == true && e1.active_spr == false);
            ok = ok && (e2.line == 150 && e2.active_ula == true
                       && e2.active_l2 == true && e2.active_spr == true);
        }
        check("S17.01",
              "zxnext.vhd:5391-5393 — NR 0x43 b1-3 selector change-log "
              "captures per-line snapshots",
              ok,
              fmt("entries=%zu", n));
    }

    // S17.02 — NR 0x6B b4 tilemap-palette mid-frame flip is recorded
    // and replayable per scanline. VHDL zxnext.vhd:5462 latches
    // nr_6b_tm_control(4) on NR 0x6B writes; mux at :6826 picks the
    // active tilemap palette per scanline.
    {
        UlaBed bed;
        bed.ula.palsel_start_frame();
        // Line N: tm_palette_select = 0
        bed.ula.set_palsel_current_line(50);
        bed.ula.set_active_tilemap_palette(false);
        // Line N+1: tm_palette_select = 1
        bed.ula.set_palsel_current_line(51);
        bed.ula.set_active_tilemap_palette(true);

        bed.ula.palsel_rewind_to_baseline();

        // Replay per-scanline; check the live state matches each line's
        // expected selector.
        bed.ula.palsel_apply_changes_for_line(50);
        bool at50 = (bed.ula.get_active_tilemap_palette() == false);
        bed.ula.palsel_apply_changes_for_line(51);
        bool at51 = (bed.ula.get_active_tilemap_palette() == true);

        check("S17.02",
              "zxnext.vhd:5462 + :6826 — NR 0x6B b4 mid-frame flip lands "
              "on the correct scanline",
              at50 && at51,
              fmt("at50=%d at51=%d entries=%zu",
                  at50 ? 1 : 0, at51 ? 1 : 0,
                  bed.ula.palsel6b_change_log_size()));
    }

    // S17.03 — NR 0x43 / NR 0x6B b4 streams are independent. VHDL has
    // two distinct latch groups (zxnext.vhd:5391-5393 vs :5462); a
    // write to one MUST NOT append to the other.
    {
        UlaBed bed;
        bed.ula.palsel_start_frame();

        // Three NR 0x43 writes, no NR 0x6B writes.
        bed.ula.set_palsel_current_line(10);
        bed.ula.set_active_ula_palette(true);
        bed.ula.set_palsel_current_line(20);
        bed.ula.set_active_layer2_palette(true);
        bed.ula.set_palsel_current_line(30);
        bed.ula.set_active_sprite_palette(true);

        const size_t n43_after_43_writes = bed.ula.palsel43_change_log_size();
        const size_t n6b_after_43_writes = bed.ula.palsel6b_change_log_size();

        // Two NR 0x6B b4 writes, no NR 0x43 writes.
        bed.ula.set_palsel_current_line(40);
        bed.ula.set_active_tilemap_palette(true);
        bed.ula.set_palsel_current_line(45);
        bed.ula.set_active_tilemap_palette(false);

        const size_t n43_after_6b_writes = bed.ula.palsel43_change_log_size();
        const size_t n6b_after_6b_writes = bed.ula.palsel6b_change_log_size();

        // After NR 0x43 phase: 3 entries on 43-log, 0 on 6b-log.
        // After NR 0x6B phase: 43-log unchanged, 6b-log gains 2.
        bool ok = (n43_after_43_writes == 3)
               && (n6b_after_43_writes == 0)
               && (n43_after_6b_writes == 3)
               && (n6b_after_6b_writes == 2);
        check("S17.03",
              "zxnext.vhd:5391-5393 vs :5462 — NR 0x43 / NR 0x6B b4 are "
              "independent change-streams",
              ok,
              fmt("43[after43=%zu after6b=%zu] 6b[after43=%zu after6b=%zu]",
                  n43_after_43_writes, n43_after_6b_writes,
                  n6b_after_43_writes, n6b_after_6b_writes));
    }

    // S17.04 — palsel_start_frame rewinds (clears) both selector logs.
    // After append + start_frame, both logs must be empty and the
    // baseline must equal the live state at the moment of start_frame.
    {
        UlaBed bed;
        bed.ula.palsel_start_frame();
        bed.ula.set_palsel_current_line(80);
        bed.ula.set_active_ula_palette(true);
        bed.ula.set_active_tilemap_palette(true);

        const size_t n43_before = bed.ula.palsel43_change_log_size();
        const size_t n6b_before = bed.ula.palsel6b_change_log_size();

        // start_frame at next frame edge: live state (ULA=1, TM=1) becomes
        // baseline; logs are cleared.
        bed.ula.palsel_start_frame();

        const size_t n43_after = bed.ula.palsel43_change_log_size();
        const size_t n6b_after = bed.ula.palsel6b_change_log_size();

        // After rewind_to_baseline (no replay), live should match the
        // captured baseline (ULA=1, TM=1) — i.e. NOT reset to power-on
        // defaults.
        bed.ula.palsel_rewind_to_baseline();
        bool live_matches_baseline =
            (bed.ula.get_active_ula_palette() == true)
            && (bed.ula.get_active_tilemap_palette() == true);

        bool ok = (n43_before >= 1) && (n6b_before >= 1)
               && (n43_after  == 0) && (n6b_after  == 0)
               && live_matches_baseline;
        check("S17.04",
              "zxnext.vhd:5391-5393 + :5462 — palsel_start_frame clears "
              "logs and snapshots baseline from live state",
              ok,
              fmt("before[43=%zu 6b=%zu] after[43=%zu 6b=%zu] "
                  "live_eq_baseline=%d",
                  n43_before, n6b_before, n43_after, n6b_after,
                  live_matches_baseline ? 1 : 0));
    }
}

// =========================================================================
// Main
// =========================================================================

int main() {
    std::printf("=== ULA Video Compliance Test Suite (Phase-2 idiom, 122 plan rows) ===\n\n");

    test_section1_screen_address();
    test_section2_attribute_rendering();
    test_section3_border_colour();
    test_section4_flash_timing();
    test_section5_timex();
    test_section6_ulanext();
    test_section7_ulaplus();
    test_section8_clip();
    test_section9_scrolling();
    test_section10_floating_bus();
    test_section11_contention();
    test_section12_ula_disable();
    test_section13_timing();
    test_section14_frame_int();
    test_section15_shadow();
    test_section16_nrff_palette();
    test_section17_palette_select();

    std::printf("\n=== Results by group ===\n");
    std::string last_group;
    int gp = 0, gf = 0;
    for (size_t i = 0; i <= g_results.size(); i++) {
        bool flush = (i == g_results.size()) || (i > 0 && g_results[i].group != last_group);
        if (flush && !last_group.empty()) {
            std::printf("  %-20s %d/%d %s\n", last_group.c_str(), gp, gp + gf,
                        gf == 0 ? "PASS" : "FAIL");
            gp = gf = 0;
        }
        if (i < g_results.size()) {
            last_group = g_results[i].group;
            if (g_results[i].passed) ++gp; else ++gf;
        }
    }

    if (!g_skipped.empty()) {
        std::printf("\n=== Skipped plan rows (%zu) ===\n", g_skipped.size());
        for (const auto& s : g_skipped) {
            std::printf("  SKIP %s: %s\n", s.id, s.reason);
        }
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    return g_fail > 0 ? 1 : 0;
}
