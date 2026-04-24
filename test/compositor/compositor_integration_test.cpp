// Compositor Subsystem Integration Test — full-Emulator fixture pinning the
// NR 0x68 bit-7 ULA-disable wiring at the render-pipeline output, both via
// a direct NR write (UDIS-01) and via a mid-frame Copper MOVE (UDIS-02).
//
// Re-homed 2026-04-24 from test/compositor/compositor_test.cpp §UDIS per
// doc/design/TASK-COMPOSITOR-NR68-BLEND-PLAN.md. The bare compositor tier
// (compositor_test.cpp) cannot construct the CPU + Copper + run_frame
// loop that these two rows require; UDIS-03 (blend-mode bits 6:5) stays
// a skip() in compositor_test.cpp pending its own plan doc.
//
// VHDL oracles:
//   * zxnext.vhd:7103 — ula_transparent <= '1' when … or (ula_en_2='0');
//   * zxnext.vhd:5445 — NR 0x68 bit 7 drives ula_en;
//   * zxnext.vhd:6809 — Copper MOVE scheduling writes nr_wr_dat at the
//     raster position specified by the preceding WAIT (copper.vhd:85-106).
//
// jnext wiring under test:
//   * src/core/emulator.cpp:816-825 — NR 0x68 handler forwards bit 7 to
//     Ula::set_ula_enabled (inverted).
//   * src/video/renderer.cpp:83-85 — when !ula_enabled, ula_line_ is
//     filled with TRANSPARENT, causing composite output to fall through
//     to the NR 0x4A fallback colour.
//   * src/core/emulator.cpp:2609-2616 — Copper::execute is driven per
//     raster position inside run_frame(), so a mid-frame MOVE takes
//     effect starting from the line where its WAIT condition is met.
//
// Run: ./build/test/compositor_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "cpu/z80_cpu.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "port/nextreg.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/ula.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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
    std::string id;
    std::string reason;
};
std::vector<SkipNote> g_skipped;  // always empty in this suite

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Fresh-state idiom: re-init before each scenario so NR 0x68 state,
// Copper state, and the CPU registers don't leak between rows.
static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Park the Z80 on a HALT at 0x8000 so run_frame() does not let the boot
// ROM execute arbitrary NR writes while we observe the compositor output.
// After HALT the CPU just advances T-states without touching any port or
// NR register — this keeps the frame deterministic.
static void park_cpu_at_halt(Emulator& emu) {
    emu.mmu().write(0x8000, 0x76);  // HALT
    auto regs = emu.cpu().get_registers();
    regs.PC   = 0x8000;
    regs.SP   = 0xFFFD;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
}

// Write NR through the real port dispatch (OUT 0x243B,reg ; OUT 0x253B,val),
// mirroring the idiom used by nextreg_integration_test.cpp + ctc_int_test.
static void nr_write_port(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// Poke a byte into physical bank 5 at the CPU-space 0x4000-relative
// offset — same as ula_integration_test's poke_bank5 (VRAM the ULA
// reads unconditionally per src/video/ula.cpp:35-51).
static void poke_bank5(Emulator& emu, uint16_t cpu_addr_4000_based,
                       uint8_t val) {
    const uint32_t offset = cpu_addr_4000_based & 0x3FFFu;
    emu.ram().write(10u * 8192u + offset, val);
}

static void fill_pixels(Emulator& emu, uint8_t v) {
    for (uint16_t addr = 0x4000; addr < 0x5800; ++addr) {
        poke_bank5(emu, addr, v);
    }
}

static void fill_attrs(Emulator& emu, uint8_t v) {
    for (uint16_t addr = 0x5800; addr < 0x5B00; ++addr) {
        poke_bank5(emu, addr, v);
    }
}

// CPU-space offset into the primary screen for (screen_row, col) per
// zxula.vhd:218-263 (same helper as ula_integration_test).
static uint16_t emu_pixel_addr_offset(int screen_row, int col) {
    return static_cast<uint16_t>(
          ((screen_row & 0xC0) << 5)
        | ((screen_row & 0x07) << 8)
        | ((screen_row & 0x38) << 2)
        | col);
}

// Pixel access into the last-rendered framebuffer. Framebuffer pitch is
// the value returned by Emulator::get_framebuffer_width(); for the
// scenarios in this suite no hi-res layer is enabled, so it stays 320.
static uint32_t fb_pixel(Emulator& emu, int fb_row, int fb_col) {
    const int pitch = emu.get_framebuffer_width();
    return emu.get_framebuffer()[fb_row * pitch + fb_col];
}

// ══════════════════════════════════════════════════════════════════════
// Group UDIS-INT — NR 0x68 bit 7 ULA-disable end-to-end (full-frame)
// VHDL: zxnext.vhd:7103 (ula_transparent when ula_en=0), :5445 (NR decode)
// jnext: src/core/emulator.cpp:816-825, src/video/renderer.cpp:83-85
// ══════════════════════════════════════════════════════════════════════

static void test_udis_integration(Emulator& emu) {
    set_group("UDIS-INT");

    const uint32_t WHITE = kUlaPalette[7];

    // ── UDIS-01 — NR 0x68 bit 7 toggles whole-ULA transparency ─────────
    //
    // Plants a white marker at screen (row 0, col 0) with all-white ink
    // + black-paper attributes so that with ULA enabled the display-area
    // pixel at framebuffer (DISP_Y, DISP_X) is WHITE (kUlaPalette[7])
    // and an off-marker pixel is BLACK (kUlaPalette[0]).
    //
    // Path A — ULA enabled (NR 0x68 bit 7 = 0):
    //   Renderer::render_frame fills ula_line_ from Ula::render_scanline
    //   and composite_scanline selects u_px (ULA) since all other layers
    //   are transparent. Marker pixel = WHITE; off-marker pixel = BLACK.
    //
    // Path B — ULA disabled (NR 0x68 bit 7 = 1):
    //   VHDL zxnext.vhd:7103 forces ula_transparent regardless of
    //   display pixels; renderer.cpp:83-85 fills ula_line_ with
    //   TRANSPARENT; composite_scanline falls through to fallback_argb
    //   (NR 0x4A). Both marker and off-marker pixels equal the fallback.
    //   NR 0x4A is set to 0xE0 = RRR=111 GGG=000 BB=00 → pure bright red
    //   so it is distinct from both WHITE and BLACK.
    //
    // Two `fresh()` rebuilds make the two paths fully independent and
    // avoid any stale-framebuffer confusion between the two run_frame()
    // calls in a single scope.

    // Path A: ULA enabled end-to-end.
    uint32_t path_a_marker = 0;
    uint32_t path_a_offmk  = 0;
    bool     ula_en_a      = false;
    const uint8_t  kFallback = 0xE0;
    const uint32_t kRed      = Renderer::rrrgggbb_to_argb(kFallback);
    {
        fresh(emu);
        park_cpu_at_halt(emu);
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        nr_write_port(emu, 0x4A, kFallback);
        nr_write_port(emu, 0x68, 0x00);   // ULA enabled
        emu.run_frame();

        path_a_marker = fb_pixel(emu, Renderer::DISP_Y, Renderer::DISP_X);
        path_a_offmk  = fb_pixel(emu, Renderer::DISP_Y, Renderer::DISP_X + 100);
        ula_en_a      = emu.ula().ula_enabled();
    }

    // Path B: ULA disabled end-to-end (independent rebuild).
    uint32_t path_b_marker = 0;
    uint32_t path_b_offmk  = 0;
    bool     ula_en_b      = true;
    {
        fresh(emu);
        park_cpu_at_halt(emu);
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);
        nr_write_port(emu, 0x4A, kFallback);
        nr_write_port(emu, 0x68, 0x80);   // ULA disabled
        emu.run_frame();

        path_b_marker = fb_pixel(emu, Renderer::DISP_Y, Renderer::DISP_X);
        path_b_offmk  = fb_pixel(emu, Renderer::DISP_Y, Renderer::DISP_X + 100);
        ula_en_b      = emu.ula().ula_enabled();
    }

    {
        const bool ok_a = (path_a_marker == WHITE)
                       && (path_a_offmk  == kUlaPalette[0])   // BLACK paper
                       && (ula_en_a      == true);
        const bool ok_b = (path_b_marker == kRed)
                       && (path_b_offmk  == kRed)
                       && (ula_en_b      == false);

        check("UDIS-01",
              "NR 0x68 bit 7 toggles ULA transparency → display pixel "
              "switches between ULA ink and NR 0x4A fallback "
              "(zxnext.vhd:7103; emulator.cpp:816-825; renderer.cpp:83-85)",
              ok_a && ok_b,
              fmt("A_marker=0x%08X exp WHITE 0x%08X A_offmk=0x%08X exp BLACK 0x%08X "
                  "B_marker=0x%08X B_offmk=0x%08X exp RED 0x%08X "
                  "ula_en_A=%d ula_en_B=%d",
                  path_a_marker, WHITE, path_a_offmk, kUlaPalette[0],
                  path_b_marker, path_b_offmk, kRed,
                  ula_en_a, ula_en_b));
    }

    // ── UDIS-02 — Copper mid-frame MOVE flips NR 0x68 bit 7 ─────────────
    //
    // Builds a 2-instruction Copper program:
    //   [0] WAIT  vpos=100, hpos=0           → stall until cvc==100
    //   [1] MOVE  NR 0x68, 0x80               → disable ULA
    //   [2] HALT  (WAIT vpos=511)              → park the copper
    //
    // copper.vhd:20-43 / src/peripheral/copper.cpp:10-43 decode:
    //   WAIT instruction = 0x8000 | (hpos & 0x3F)<<9 | (vpos & 0x1FF)
    //   MOVE instruction = (reg & 0x7F)<<8 | (val & 0xFF)        (MSB bit clear)
    // VHDL scheduling at zxnext.vhd:6809 drives copper.vhd from cvc/hcount,
    // and Emulator::run_frame ticks Copper::execute at every raster edge
    // (src/core/emulator.cpp:2609-2616).
    //
    // Upload via the NR 0x60 (write_8 = 1) path: each successive byte
    // lands at auto-incrementing write_addr (src/peripheral/copper.cpp:180-201),
    // alternating MSB then LSB of each 16-bit word. We set write_addr to 0
    // via NR 0x61 / NR 0x62 (bits 2:0 of addr high), then push 6 bytes:
    //   MSB-of-word0, LSB-of-word0, MSB-of-word1, LSB-of-word1, MSB-of-word2, LSB-of-word2.
    //
    // Copper mode is set by NR 0x62 bits 7:6 (src/peripheral/copper.cpp:209-215):
    //   11 = reset PC each vsync + run continuously. We write NR 0x62 = 0xC0
    //   (mode=11, addr-high bits 2:0 = 0) AFTER the upload so the
    //   write_addr reset-to-0 happens before any bytes land.
    //
    // Assertion: framebuffer row DISP_Y + 99 (the line BEFORE the Copper
    // toggle fires) shows ULA content (WHITE at display col 0 from the
    // planted row-99 marker); framebuffer row DISP_Y + 101 (AFTER toggle)
    // shows the fallback colour (red) at the same x. The exact line where
    // the flip lands may be 100 or 101 depending on raster timing inside
    // the line — we sample one row before and two rows after to leave a
    // safe cushion, per the plan's "acceptable simplification".
    {
        fresh(emu);
        park_cpu_at_halt(emu);

        // Plant WHITE markers at screen rows 99 and 101 so we can
        // observe the ULA contribution separately in each raster region.
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        for (int col = 0; col < 32; ++col) {
            poke_bank5(emu, 0x4000 + emu_pixel_addr_offset( 99, col), 0xFF);
            poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(101, col), 0xFF);
        }

        // Distinct fallback colour → 0xE0 (red) so disabled-ULA rows
        // cannot be mistaken for plain black ULA paper.
        const uint8_t  kFallback = 0xE0;
        const uint32_t kRed      = Renderer::rrrgggbb_to_argb(kFallback);
        nr_write_port(emu, 0x4A, kFallback);

        // Pre-condition: ULA enabled (reset default), ensure NR 0x68 = 0.
        nr_write_port(emu, 0x68, 0x00);

        // Build the three 16-bit instructions.
        const uint16_t WAIT_V100 = static_cast<uint16_t>(0x8000u | 100u);
        const uint16_t MOVE_68_80 = static_cast<uint16_t>((0x68u << 8) | 0x80u);
        const uint16_t WAIT_HALT = static_cast<uint16_t>(0x8000u | 511u);

        // Reset the copper write address to 0 via NR 0x61 (low 8) + NR 0x62
        // (high 3 bits + mode). Use NR 0x62 = 0x00 first (mode=00, stopped,
        // addr-high=0) so auto-increment starts at 0 cleanly.
        nr_write_port(emu, 0x61, 0x00);
        nr_write_port(emu, 0x62, 0x00);

        // Push 6 bytes via NR 0x60 (write_8 = 1 → even=MSB RAM, odd=LSB RAM).
        nr_write_port(emu, 0x60, static_cast<uint8_t>((WAIT_V100 >> 8) & 0xFF));
        nr_write_port(emu, 0x60, static_cast<uint8_t>( WAIT_V100       & 0xFF));
        nr_write_port(emu, 0x60, static_cast<uint8_t>((MOVE_68_80 >> 8) & 0xFF));
        nr_write_port(emu, 0x60, static_cast<uint8_t>( MOVE_68_80       & 0xFF));
        nr_write_port(emu, 0x60, static_cast<uint8_t>((WAIT_HALT >> 8) & 0xFF));
        nr_write_port(emu, 0x60, static_cast<uint8_t>( WAIT_HALT       & 0xFF));

        // Enable copper mode 11 (restart PC each vsync + run).
        nr_write_port(emu, 0x62, 0xC0);

        // Run two frames: the first arms + advances past the WAIT on
        // line 100, firing the MOVE; the second pass has copper parked
        // on the HALT so no further writes occur.
        emu.run_frame();

        // Before-toggle: row DISP_Y+99 should still carry ULA content
        // (WHITE at col DISP_X because the marker is at screen row 99).
        const uint32_t pre_white = fb_pixel(emu,
                                            Renderer::DISP_Y + 99,
                                            Renderer::DISP_X);

        // After-toggle: row DISP_Y+101 should be the fallback colour
        // (ULA disabled). Also row DISP_Y+150 — deep in the disabled
        // region — should be fallback regardless of marker placement.
        const uint32_t post_disabled_1 = fb_pixel(emu,
                                                  Renderer::DISP_Y + 101,
                                                  Renderer::DISP_X);
        const uint32_t post_disabled_2 = fb_pixel(emu,
                                                  Renderer::DISP_Y + 150,
                                                  Renderer::DISP_X + 100);

        // The ULA-enable state at end of frame should be false (copper
        // MOVE wrote 0x80 to NR 0x68, which latched bit 7 → disable).
        const bool ula_en_end = emu.ula().ula_enabled();

        const bool ok_pre  = (pre_white == WHITE);
        const bool ok_post = (post_disabled_1 == kRed)
                          && (post_disabled_2 == kRed);
        const bool ok_end  = (ula_en_end == false);

        check("UDIS-02",
              "Copper mid-frame MOVE NR 0x68,0x80 flips ULA-enable at line 100 "
              "→ pre-rows show ULA, post-rows show NR 0x4A fallback "
              "(zxnext.vhd:7103,6809; copper.cpp:75-154; emulator.cpp:2609-2616)",
              ok_pre && ok_post && ok_end,
              fmt("pre_row99_col0=0x%08X (exp WHITE 0x%08X) "
                  "post_row101_col0=0x%08X post_row150=0x%08X (exp RED 0x%08X) "
                  "ula_en_end=%d (exp 0)",
                  pre_white, WHITE,
                  post_disabled_1, post_disabled_2, kRed,
                  ula_en_end));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Compositor Subsystem Integration Tests\n");
    std::printf("=======================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_udis_integration(emu);
    std::printf("  Group: UDIS-INT — done\n");

    std::printf("\n=======================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown (live rows only):\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-22s %s\n", s.id.c_str(), s.reason.c_str());
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
