// ULA Video Subsystem Integration Test — full-machine rows exercising the
// ULA scroll / ULAnext / ULA+ surface END-TO-END through the NR-dispatch
// and port-dispatch handlers that a real Z80 write would traverse
// (Phase 3 of the ULA Video SKIP-reduction plan, 2026-04-23).
//
// These plan rows cannot be exercised against the bare Ula class — they
// span:
//   * NR 0x26 / 0x27 / 0x42 / 0x43 / 0x68 write-handlers registered in
//     `src/core/emulator.cpp:322-329, 523-545, 762-768` which translate
//     a raw byte into the narrow Ula accessor(s), mirroring the VHDL
//     decode at zxnext.vhd:5304/5307/5386/5394/5449.
//   * Port 0xBF3B / 0xFF3B handlers registered in
//     `src/core/emulator.cpp:1549-1563` which gate the ULA+ enable latch
//     on `port_bf3b_ulap_mode = "01"` (VHDL zxnext.vhd:4548).
//
// They live on the integration tier rather than the subsystem tier: the
// subsystem-tier ula_test.cpp covers the Ula-side accessor semantics and
// encoder math, while this file pins the NR/port-dispatch handoff that
// lives one level above Ula in emulator.cpp.
//
// Reference plan: doc/design/TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md §Phase 3.
// Reference structural template: test/input/input_integration_test.cpp,
// test/ctc_interrupts/ctc_interrupts_test.cpp.
//
// Run: ./build/test/ula_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "memory/ram.h"
#include "memory/mmu.h"
#include "video/ula.h"
#include "video/palette.h"
#include "video/renderer.h"

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
std::vector<SkipNote> g_skipped;  // populated by skip() for SKIP rows

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

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Compute the CPU-space offset into the primary screen area for
// (screen_row, col) under the standard ULA zxula.vhd:218-263 layout:
//   addr_p_spc_12_5 = py(7:6) & py(2:0) & py(5:3); addr(4:0) = col(7:3).
// Returns the 14-bit offset from 0x4000.
static uint16_t emu_pixel_addr_offset(int screen_row, int col) {
    return static_cast<uint16_t>(
          ((screen_row & 0xC0) << 5)
        | ((screen_row & 0x07) << 8)
        | ((screen_row & 0x38) << 2)
        | col);
}

// Write a pixel byte into bank 5 — the dedicated 16K dual-port VRAM the
// ULA reads from (VHDL bank5_ram dpram2, zxnext.vhd:6558-6578; Task 25
// replaced the old aliased physical-page-10 model). CPU address 0x4000
// → offset 0 inside the buffer.
static void poke_bank5(Emulator& emu, uint16_t cpu_addr_4000_based,
                       uint8_t val) {
    const uint32_t offset = cpu_addr_4000_based & 0x3FFFu;  // 14-bit bank offset
    emu.mmu().bank5_vram()[offset] = val;
}

// Fill every attribute band (0x5800..0x5AFF) with `v` — keeps attr-driven
// foreground/background uniform so pixel-level assertions reflect only the
// ULA scroll / screen-address path.
static void fill_attrs(Emulator& emu, uint8_t v) {
    for (uint16_t addr = 0x5800; addr < 0x5B00; ++addr) {
        poke_bank5(emu, addr, v);
    }
}

// Fill pixels 0x4000..0x57FF with `v`.
static void fill_pixels(Emulator& emu, uint8_t v) {
    for (uint16_t addr = 0x4000; addr < 0x5800; ++addr) {
        poke_bank5(emu, addr, v);
    }
}

// Render a single ULA scanline (row in framebuffer coordinates: 32 =
// first display line). We call the Ula directly to avoid pulling in the
// full compositor; the NR/port plumbing this suite exercises lands in
// Ula state, which Ula::render_scanline consumes unchanged.
static void render_line(Emulator& emu, int fb_row,
                        std::array<uint32_t, Ula::FB_WIDTH>& line) {
    line.fill(0);
    emu.ula().render_scanline(line.data(), fb_row, emu.mmu());
}

// VHDL zxula.vhd:543-553 — std-ULA encoder produces an 8-bit ula_pixel
// that indexes the single 256-entry × 2-bank ULA palette
// (zxnext.vhd:6981).  These helpers query the live Emulator palette
// for the ARGB the renderer would emit for an N-coloured ink or paper
// cycle on the active read bank.  Boot defaults seed indices
// 0x00..0x0F (ink) and 0x10..0x1F (paper) with the canonical ZX
// colours, so callers can express assertions in terms of the encoder
// pixel address the std-ULA path actually produces.
static inline uint8_t std_ula_ink_pixel(uint8_t colour /*0..15*/) {
    return static_cast<uint8_t>(colour & 0x0F);
}
static inline uint8_t std_ula_paper_pixel(uint8_t colour /*0..15*/) {
    return static_cast<uint8_t>(0x10 | (colour & 0x0F));
}
static inline uint32_t emu_ink_argb(Emulator& emu, uint8_t colour) {
    const bool bank = emu.palette().read_control() & 0x02;  // NR 0x43 b1
    return emu.palette().ula_colour(bank, std_ula_ink_pixel(colour));
}
static inline uint32_t emu_paper_argb(Emulator& emu, uint8_t colour) {
    const bool bank = emu.palette().read_control() & 0x02;
    return emu.palette().ula_colour(bank, std_ula_paper_pixel(colour));
}

// ══════════════════════════════════════════════════════════════════════
// Group A — ULA scroll end-to-end through NR 0x26 / 0x27 / 0x68 bit 2
// VHDL: zxula.vhd:193-216 (py/px fold), zxnext.vhd:5304/5307/5449 (NR
//       write decode), src/core/emulator.cpp:523-545, 762-768 (handlers)
// ══════════════════════════════════════════════════════════════════════

static void test_scroll_integration(Emulator& emu) {
    set_group("INT-SCROLL");

    // Expected ARGB values used by the assertions below.  Sourced live
    // from the 256-entry × 2-bank ULA palette (VHDL palette_utm at
    // zxnext.vhd:6960) via the std-ULA encoder addresses the renderer
    // would emit for ink-7 (white) and paper-0 (black) at boot defaults.
    const uint32_t WHITE = emu_ink_argb(emu, 7);
    const uint32_t BLACK = emu_paper_argb(emu, 0);

    // ── INT-SCROLL-01 — NR 0x26 = 8 via nextreg().write → 8-pixel shift ──
    //
    // Path exercised (VHDL oracle → jnext):
    //   zxnext.vhd:5304  nr_26_ula_scrollx <= nr_wr_dat when nr_we_26='1'
    //   zxula.vhd:199    px <= fine & (hc(7:3) + scroll_x(7:3)) & scroll_x(2:0)
    // jnext handler: src/core/emulator.cpp:524-526 —
    //   nextreg_.set_write_handler(0x26, …→ ula().set_ula_scroll_x_coarse(v))
    //
    // Stimulus: primary screen row 0, col 0 = 0xFF (all-ink), rest = 0,
    // all attrs = 0x07 (white ink on black paper). Write NR 0x26 = 8
    // through nextreg().write — NOT through the narrow Ula accessor.
    // Expected per VHDL:199: scroll_x(7:3)=1 (1-byte/8-pixel column
    // shift), scroll_x(2:0)=0 (no bit phase), total src-offset = 8. So
    // src_x ∈ [0..7] appear at display_x = src_x − 8 (mod 256) =
    // [248..255] — white at display_x 248..255, black elsewhere.
    {
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);

        // Traverse the full NR-dispatch path — this is the integration
        // point the suite pins. After the write the handler must reach
        // down into Ula::set_ula_scroll_x_coarse.
        emu.nextreg().write(0x26, 8);

        // Sanity: the narrow accessor now observes the new value (proves
        // the NR handler fired).
        const uint8_t stored = emu.ula().get_ula_scroll_x_coarse();

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        render_line(emu, 32, line);   // fb row 32 = first display row.

        bool ok_white  = true;
        bool ok_black  = true;
        for (int x = 0; x < 256; ++x) {
            if (x >= 248 && x <= 255) {
                if (line[Ula::DISP_X + 2*x] != WHITE) { ok_white = false; break; }
            } else {
                if (line[Ula::DISP_X + 2*x] != BLACK) { ok_black = false; break; }
            }
        }
        check("INT-SCROLL-01",
              "nextreg().write(0x26,8) → 8-pixel shift end-to-end  "
              "(zxula.vhd:199; zxnext.vhd:5304; emulator.cpp:524-526)",
              stored == 8 && ok_white && ok_black,
              fmt("stored=%u ok_white=%d ok_black=%d "
                  "line[DISP_X+2*248]=0x%08X line[DISP_X+2*247]=0x%08X",
                  stored, ok_white, ok_black,
                  line[Ula::DISP_X + 2*248], line[Ula::DISP_X + 2*247]));
    }

    // ── INT-SCROLL-02 — NR 0x68 bit 2 (fine X-scroll) via nextreg().write ──
    //
    // Path exercised:
    //   zxnext.vhd:5449  nr_68_ula_fine_scroll_x <= nr_wr_dat(2)
    //   zxula.vhd:199    px(8) = fine_scroll_x; line 216 merges it into
    //                    px_1 so the effective source-x shifts by 1 pixel
    //                    when fine=1 and scroll_x=0.
    // jnext handler: src/core/emulator.cpp:762-768 —
    //   nextreg_.set_write_handler(0x68, …→ ula().set_ula_fine_scroll_x(bit 2))
    //
    // Byte 0x04 = bit 2 only. Other bits: bit 7 = 0 (ula_enabled=true =
    // default), bits 6:5 = 00 (normal blend = default), bit 3 = 0
    // (ULA+ side owned by port 0xFF3B path — untouched), bit 0 = 0
    // (stencil off = default). Writing 0x04 preserves the Compositor-
    // owned fields at their reset defaults per the plan caveat that
    // Phase 3 must not trample Compositor bits.
    //
    // With NR 0x26 still = 8 from INT-SCROLL-01 we reset it to 0 first
    // so this test observes the ISOLATED 1-pixel fine shift.
    // Marker at src (row 0, col 0) = 0xFF → white at src_x ∈ [0..7]
    // → display_x = src_x − 1 (mod 256) ⇒ display_x ∈ {255, 0..6}.
    {
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(0, 0), 0xFF);

        emu.nextreg().write(0x26, 0);   // clear coarse scroll from prior row
        emu.nextreg().write(0x68, 0x04); // bit 2 = fine, others at reset defaults

        const bool fine = emu.ula().get_ula_fine_scroll_x();

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        render_line(emu, 32, line);

        bool ok = true;
        for (int x = 0; x < 256; ++x) {
            const bool should_white = (x == 255) || (x >= 0 && x <= 6);
            const uint32_t exp = should_white ? WHITE : BLACK;
            if (line[Ula::DISP_X + 2*x] != exp) { ok = false; break; }
        }
        check("INT-SCROLL-02",
              "nextreg().write(0x68, 0x04) → fine X-scroll = 1 → 1-pixel shift  "
              "(zxula.vhd:199,216; zxnext.vhd:5449; emulator.cpp:762-768)",
              fine == true && ok,
              fmt("fine=%d line[DISP_X]=0x%08X line[DISP_X+2*255]=0x%08X line[DISP_X+2*7]=0x%08X",
                  fine, line[Ula::DISP_X], line[Ula::DISP_X + 2*255], line[Ula::DISP_X + 2*7]));
    }

    // ── INT-SCROLL-03 — NR 0x27 = 32 via nextreg().write → 32-row Y shift ──
    //
    // Path exercised:
    //   zxnext.vhd:5307  nr_27_ula_scrolly <= nr_wr_dat
    //   zxula.vhd:192    py_s = vc + ('0' & scroll_y)
    //   zxula.vhd:206    else-branch (py_s(8)=0 && py_s(7:6)!="11") →
    //                    py = py_s(7:0)
    // jnext handler: src/core/emulator.cpp:532-534 —
    //   nextreg_.set_write_handler(0x27, …→ ula().set_ula_scroll_y(v))
    //
    // Stimulus: plant the all-ink marker at src row 32 (NOT row 0), all
    // other rows clear, attrs uniform. With scroll_y=32, vc=0 →
    // py_s=32 → py=32 → renderer reads src row 32 → display row 0 is
    // all white. Cross-third arithmetic: py_s=32 (bits7:6=00) is safely
    // in the first third, no wrap branch taken (validates the else-
    // branch at line 206 exclusively — cross-third behaviour is pinned
    // separately by S9.10 in ula_test.cpp).
    //
    // We DO NOT clear NR 0x26/0x68 again here — INT-SCROLL-02 left
    // NR 0x26=0, fine=1. Reset both to 0 for a clean Y-only observation.
    {
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        for (int col = 0; col < 32; ++col) {
            poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(32, col), 0xFF);
        }

        emu.nextreg().write(0x26, 0);
        emu.nextreg().write(0x68, 0x00);   // clear fine X (also clears bit 3/bit 0)
        emu.nextreg().write(0x27, 32);

        const uint8_t stored = emu.ula().get_ula_scroll_y();

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        render_line(emu, 32, line);  // screen row 0 → should pull src row 32

        bool all_white = true;
        for (int x = 0; x < 256; ++x) {
            if (line[Ula::DISP_X + 2*x] != WHITE) { all_white = false; break; }
        }
        check("INT-SCROLL-03",
              "nextreg().write(0x27,32) → Y-scroll 32 rows end-to-end  "
              "(zxula.vhd:193-207; zxnext.vhd:5307; emulator.cpp:532-534)",
              stored == 32 && all_white,
              fmt("stored=%u all_white=%d display[0]=0x%08X exp 0x%08X",
                  stored, all_white, line[Ula::DISP_X], WHITE));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group B — ULA+ end-to-end through ports 0xBF3B / 0xFF3B
// VHDL: zxnext.vhd:4523-4554 (port decoders), zxula.vhd:531-541 (encoder)
// jnext: src/core/emulator.cpp:1549-1563 (port handlers)
// ══════════════════════════════════════════════════════════════════════

static void test_ulaplus_integration(Emulator& emu) {
    set_group("INT-ULAPLUS");

    // ── INT-ULAPLUS-01 — port 0xBF3B mode=01 + port 0xFF3B bit 0 → enable ──
    //
    // Path exercised:
    //   zxnext.vhd:4525-4538  port 0xBF3B write: port_bf3b_ulap_mode <=
    //                         cpu_do(7:6). When mode ≠ "00" the low 6
    //                         bits are NOT latched as palette index.
    //   zxnext.vhd:4543-4554  port 0xFF3B write: port_ff3b_ulap_en <=
    //                         cpu_do(0), BUT ONLY when
    //                         port_bf3b_ulap_mode = "01".
    //   zxula.vhd:531-541     encode_ulap_pixel packs palette group via
    //                         attr(7:6) into ula_pixel(5:4); paper cycle
    //                         with attr(7:6)=11 → ula_pixel = 0xF8 | attr(5:3).
    // jnext handlers: src/core/emulator.cpp:1549-1563 — register
    //   0xBF3B (mask 0xFFFF) write → ula().set_ulap_mode((v >> 6) & 3).
    //   0xFF3B (mask 0xFFFF) write → if (ulap_mode == 0x01)
    //                                    ula().set_ulap_en(v & 0x01).
    //
    // Stimulus:
    //   1. OUT (0xBF3B), 0x40      ; ulap_mode = "01" (mode-group 1:
    //                                register-enable, per VHDL). Low 6
    //                                bits (0x00) ignored by the mode-
    //                                non-zero gate at VHDL:4533.
    //   2. OUT (0xFF3B), 0x01      ; enables ULA+ (bit 0 = 1). Gate
    //                                fires because ulap_mode=="01".
    //
    // Observable through the narrow accessors (Ula state that the ULA+
    // pixel encoder consumes at render time):
    //   ula().get_ulap_mode()  == 0x01
    //   ula().get_ulap_en()    == true
    //
    // Also pin the VHDL-specified encoder output (zxula.vhd:531-541,
    // palette group 3, paper cycle, attr(5:3)=111) — once the enable is
    // latched, the encoder MUST pack ula_pixel = 0xFF. This is a pure
    // function of (pixel_en, attr, screen_mode_2) so we just confirm
    // the encoder agrees with the VHDL formula for palette group 3,
    // linking the port-side enable path to the renderer-side consumer.
    {
        // Prior tests left ULAnext disabled (NR 0x43 never written) and
        // ulap_en at its reset default (false, per zxnext.vhd:4547).
        const bool def_ok = (emu.ula().get_ulap_en() == false);
        const uint8_t def_mode = emu.ula().get_ulap_mode();

        emu.port().out(0xBF3B, 0x40);       // ulap_mode = (0x40 >> 6) = 01
        const uint8_t mode_after_bf3b = emu.ula().get_ulap_mode();

        emu.port().out(0xFF3B, 0x01);       // enable ULA+
        const bool en_after_ff3b = emu.ula().get_ulap_en();

        // Negative gate: flip mode back to "00" and attempt to CLEAR
        // the enable via port 0xFF3B — the VHDL:4548 gate must reject
        // the write and the latch must hold.
        emu.port().out(0xBF3B, 0x00);       // ulap_mode = 00 (palette mode)
        const uint8_t mode_after_bf3b2 = emu.ula().get_ulap_mode();
        emu.port().out(0xFF3B, 0x00);       // gate closed → no effect
        const bool en_hold = emu.ula().get_ulap_en();

        // Encoder side: palette group 3 paper cycle, attr=0xFF,
        // screen_mode_2=0 → 0xF8 | (0xFF>>3)&0x07 = 0xF8 | 0x07 = 0xFF.
        // VHDL oracle zxula.vhd:531-541.
        const uint8_t enc = Ula::encode_ulap_pixel(/*pixel_en*/false,
                                                   /*attr*/0xFF,
                                                   /*screen_mode_2*/false);
        const bool enc_ok = (enc == 0xFF);

        const bool all_ok = def_ok
                         && def_mode == 0x00
                         && mode_after_bf3b == 0x01
                         && en_after_ff3b == true
                         && mode_after_bf3b2 == 0x00
                         && en_hold == true       // gate must hold enable
                         && enc_ok;

        check("INT-ULAPLUS-01",
              "OUT 0xBF3B=0x40 + OUT 0xFF3B=0x01 → ulap_mode=01, ulap_en=1; "
              "gate holds when mode≠01  "
              "(zxnext.vhd:4525-4554; zxula.vhd:531-541; emulator.cpp:1549-1563)",
              all_ok,
              fmt("def_en=%d def_mode=0x%02X mode_bf3b=0x%02X en_ff3b=%d "
                  "mode_bf3b2=0x%02X en_hold=%d enc=0x%02X (exp 0xFF)",
                  def_ok, def_mode, mode_after_bf3b, en_after_ff3b,
                  mode_after_bf3b2, en_hold, enc));
    }

    // ── INT-ULAPLUS-02 — NR 0x68 bit 3 ungated alternate enable path ──
    //
    // Path exercised:
    //   zxnext.vhd:4550-4551  elsif nr_68_we = '1' then
    //                             port_ff3b_ulap_en <= nr_wr_dat(3);
    //   Distinct from the port-0xFF3B gate at :4547-4554 (which
    //   requires port_bf3b_ulap_mode = "01"). NR 0x68 writes latch
    //   ulap_en UNCONDITIONALLY — no ulap_mode check.
    //
    // Regression guard against the pre-2026-04-23 bug where the NR
    // 0x68 handler only forwarded bits 0/2/7 to Ula, silently ignoring
    // bit 3. A Z80 program enabling ULA+ via `OUT (n),a : 0x68 0x08`
    // would have had no effect.
    //
    // Stimulus sequence:
    //   1. Set port_bf3b_ulap_mode ≠ "01" so the port-0xFF3B path is
    //      definitively gated off (this proves the enable we see
    //      comes from the NR 0x68 path, not 0xFF3B).
    //   2. Disable ULA+ via NR 0x68 bit 3 = 0.
    //   3. Enable ULA+ via NR 0x68 bit 3 = 1.
    //   4. Disable ULA+ via NR 0x68 bit 3 = 0 — must actually drop.
    //
    // Preserve NR 0x68 bits 7/2/0 at their defaults across writes;
    // the Ula-side mutators (`set_ula_enabled`, `set_ula_fine_scroll_x`,
    // `renderer_.set_stencil_mode`) are idempotent under unchanged
    // input.
    {
        // Step 1: clamp ulap_mode to 00 via 0xBF3B so port 0xFF3B is
        // gated off (zxnext.vhd:4548).
        emu.port().out(0xBF3B, 0x00);

        // Step 2: NR 0x68 = 0x00 → bit 3 = 0 → ulap_en=0.
        emu.nextreg().write(0x68, 0x00);
        const bool off_initial = (emu.ula().get_ulap_en() == false);

        // Step 3: NR 0x68 = 0x08 → bit 3 = 1 → ulap_en=1, ungated.
        emu.nextreg().write(0x68, 0x08);
        const bool on_via_nr68   = (emu.ula().get_ulap_en() == true);
        const uint8_t mode_check = emu.ula().get_ulap_mode();  // stays 00

        // Step 4: NR 0x68 = 0x00 → bit 3 = 0 → ulap_en=0 again.
        emu.nextreg().write(0x68, 0x00);
        const bool off_again = (emu.ula().get_ulap_en() == false);

        const bool all_ok = off_initial && on_via_nr68 && mode_check == 0x00
                         && off_again;

        check("INT-ULAPLUS-02",
              "NR 0x68 bit 3 ungated ulap_en latch (zxnext.vhd:4550-4551) — "
              "ulap_mode=00 so port 0xFF3B path is gated; bit 3 toggles "
              "ulap_en directly",
              all_ok,
              fmt("off_initial=%d on_via_nr68=%d ulap_mode=0x%02X off_again=%d",
                  off_initial, on_via_nr68, mode_check, off_again));
    }

    // ── INT-ULAPLUS-03 — ULA+ runtime palette path renders end-to-end ──
    //
    // Path exercised (VHDL → jnext):
    //   zxnext.vhd:4533-4535  port 0xBF3B with mode="00" latches the
    //                         6-bit ulap_index (palette slot selector
    //                         for NR 0xFF poke + ULA+ palette readback).
    //   zxnext.vhd:6957-6958  nr_ff_we → palette_utm dpram address
    //                         '0' & nr_43_palette_write_select(2) & "11"
    //                         & port_bf3b_ulap_index, value RRRGGGBBB
    //                         (B0 = B1 OR B0 per :4919).
    //   zxula.vhd:531-541     ULA+ encoder packs ula_pixel(7:0) =
    //                         "11" & attr(7:6) & (sm2 OR not pix_en)
    //                         & (pix_en ? attr(2:0) : attr(5:3)).
    //   zxnext.vhd:6981       runtime read addr =
    //                         '0' & ula_palette_select_1 & ula_pixel,
    //                         where ula_palette_select_1 =
    //                         nr_43_active_ula_palette (NR 0x43 b1).
    // jnext handlers:
    //   src/core/emulator.cpp 0xBF3B port-write — set_ulap_index when
    //                         ulap_mode=="00".
    //   src/core/emulator.cpp NR 0xFF write — palette_.nr_ff_poke(
    //                         bank=NR 0x43 b6, index=ulap_index, val).
    //   src/video/ula.cpp render_display_line — ulap_en branch reads
    //                         palette_->ulap_colour(active_ula_palette,
    //                         encoder_pixel & 0x3F).
    //
    // Stimulus:
    //   1. Enable ULA+ end-to-end (BF3B mode=01, FF3B bit 0=1).
    //   2. Pick attr=0x07 (palette group 0, ink=7, paper=0).  Encoder:
    //        ink_pixel  = 0xC0 | 0x00 | 0 | 0x07 = 0xC7  → bf3b_idx 0x07
    //        paper_pixel= 0xC0 | 0x00 | 8 | 0x00 = 0xC8  → bf3b_idx 0x08
    //   3. Write distinct ULA+ palette entries at index 0x07 and 0x08
    //      via the BF3B-latch + NR 0xFF poke side-channel.
    //      - NR 0x43 b6 = 0 (write to bank 0).
    //      - NR 0x43 b1 = 0 (active read bank 0) — both default.
    //   4. Plant pixel byte 0x80 (MSB ink, rest paper) at row 0 col 0.
    //   5. Render display row 0 (fb row 32).
    //   6. Assert pixel at fb_x=DISP_X+0 == ulap_argb(idx=7);
    //      pixels at fb_x=DISP_X+1..7 == ulap_argb(idx=8).
    //
    // Negative gate: with ULA+ disabled (set ulap_en=0 via NR 0x68 b3=0),
    // the std-ULA encoder takes over (G102 — single 256-entry palette).
    // attr=0x07 produces ula_pixel = 7 (ink) / 0x10 (paper); both slots
    // hold the canonical ZX colours 7 (white) / 0 (black) at boot.
    {
        // ── Setup: enable ULA+ via the canonical port path. ────────────
        // Reset state to a known baseline (prior INT-ULAPLUS-02 left
        // ulap_en=0 via NR 0x68=0x00; ulap_mode=0 from BF3B=0x00).
        // INT-SCROLL-03 left NR 0x27 = 32 (Y-scroll); INT-SCROLL-02
        // left NR 0x26 / NR 0x68 cleared.  Reset Y-scroll explicitly so
        // src row 0 maps to display row 0 (otherwise our pixel byte at
        // src row 0 would be invisible).
        emu.nextreg().write(0x27, 0x00);   // ula_scroll_y = 0
        emu.nextreg().write(0x26, 0x00);   // ula_scroll_x = 0

        emu.port().out(0xBF3B, 0x40);   // ulap_mode = 01
        emu.port().out(0xFF3B, 0x01);   // ulap_en = 1
        const bool en_check  = emu.ula().get_ulap_en();
        const uint8_t mode_chk = emu.ula().get_ulap_mode();

        // Drop ulap_mode back to 00 so the BF3B index latch fires for
        // subsequent writes (zxnext.vhd:4533-4535).  ulap_en holds (we
        // already verified the gate-hold in INT-ULAPLUS-01).
        emu.port().out(0xBF3B, 0x00);

        // ── Pick palette indices and concrete RGB333 values. ───────────
        // Encoder for attr=0x07, pg=0, sm2=0 (STANDARD mode):
        //   ink low6   = (0<<4) | (0<<3) | 0x07 = 0x07
        //   paper low6 = (0<<4) | (1<<3) | 0x00 = 0x08
        const uint8_t ink_idx   = 0x07;
        const uint8_t paper_idx = 0x08;

        // Write 0xE0 (RRRGGGBB = 111_000_00) → expanded RGB333:
        //   r3=7, g3=0, b3 = (B1|B0)<<1 + (B1|B0) = 0 → rgb333 = 0x1C0
        // ARGB8888 = rgb333_to_argb8888(7,0,0).
        // Write 0x03 (RRRGGGBB = 000_000_11) → r3=0, g3=0, b2=3,
        //   b3 = (3<<1) | (3>>1 | 3&1) = 6 | 1 = 7 → rgb333 = 0x007.
        // ARGB8888 = rgb333_to_argb8888(0,0,7).
        const uint8_t  ink_byte   = 0xE0;   // bright red, RRRGGGBB
        const uint8_t  paper_byte = 0x03;   // bright blue, RRRGGGBB
        const uint32_t exp_ink   = rgb333_to_argb8888(7, 0, 0);
        const uint32_t exp_paper = rgb333_to_argb8888(0, 0, 7);

        // Bank-select for NR 0xFF poke = NR 0x43 b6.  We write to bank 0
        // (b6=0), and the runtime reads from bank 0 (b1=0 — defaults).
        // Make NR 0x43 explicit so the test is self-contained.
        emu.nextreg().write(0x43, 0x00);   // all bits 0: write_select=0,
                                           // active_ula=0, ulanext_en=0
        // BF3B latch + NR 0xFF poke for ink slot (idx 0x07).
        emu.port().out(0xBF3B, ink_idx);   // mode=00, index=0x07
        emu.nextreg().write(0xFF, ink_byte);
        // BF3B latch + NR 0xFF poke for paper slot (idx 0x08).
        emu.port().out(0xBF3B, paper_idx); // mode=00, index=0x08
        emu.nextreg().write(0xFF, paper_byte);

        // ── Plant the pixel byte and attr. ─────────────────────────────
        // attr=0x07: pg=0 (no flash/bright/pg-bits), paper=000, ink=111.
        // pixel byte = 0x80: MSB ink, rest paper.
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(0, 0), 0x80);

        // ── Render and observe. ────────────────────────────────────────
        // Note: per-scanline ulap_en snapshot is consumed by the
        // compositor pipeline (renderer_.render_frame); render_scanline
        // here drives Ula::render_display_line directly with the LIVE
        // ulap_en_ flag, which is exactly what we want for an isolated
        // unit-style assertion of the runtime palette path.
        std::array<uint32_t, Ula::FB_WIDTH> line{};
        render_line(emu, 32, line);

        // G104 native 640: pixel byte 0x80 = bit7 ink, bit6..0 paper.  Source
        // bit 7 of column 0 occupies fb cells [DISP_X..+1]; source bit 6
        // (the first paper bit) occupies [DISP_X+2..+3].  Sample one cell
        // from each adjacent doubled-pixel window to preserve the test's
        // intent under VHDL-faithful pixel doubling (zxula.vhd:390-393).
        const uint32_t got_ink   = line[Ula::DISP_X + 0];   // bit 7 (ink)
        const uint32_t got_paper = line[Ula::DISP_X + 2];   // bit 6 (paper)

        // ── Negative gate: disable ULA+ via NR 0x68 b3=0 and confirm ──
        // the std-ULA encoder takes over (G102 single mirror) — attr=0x07
        // paper for attr=0x07).
        emu.nextreg().write(0x68, 0x00);   // ulap_en = 0 (ungated)
        const bool en_off = (emu.ula().get_ulap_en() == false);
        std::array<uint32_t, Ula::FB_WIDTH> line_off{};
        render_line(emu, 32, line_off);
        const uint32_t off_ink   = line_off[Ula::DISP_X + 0];
        const uint32_t off_paper = line_off[Ula::DISP_X + 2];

        // ── Bank-1 isolation: poke distinct values into bank 1 at the
        // same indices, then read with NR 0x43 b1=1.  Verifies the
        // bank-select asymmetry (write side = NR 0x43 b6, read side =
        // NR 0x43 b1) does not alias bank 0 with bank 1.  Also verifies
        // bank-0 entries survive untouched after bank-1 writes.
        // VHDL: zxnext.vhd:6957 (write addr nr_43_palette_write_select(2)
        // = NR 0x43 b6); :6981 (read addr ula_palette_select_1
        // = NR 0x43 b1).
        emu.nextreg().write(0x68, 0x08);   // ulap_en = 1 (re-enable)
        // Distinct bank-1 values, byte format RRRGGGBB (with NR 0xFF
        // expansion B0 = B1|B0):
        //   0x1C = 0b000_111_00  → r3=0, g3=7, b3=0  → bright green.
        //   0xFC = 0b111_111_00  → r3=7, g3=7, b3=0  → bright yellow.
        const uint8_t  ink_byte_b1   = 0x1C;
        const uint8_t  paper_byte_b1 = 0xFC;
        const uint32_t exp_ink_b1   = rgb333_to_argb8888(0, 7, 0);
        const uint32_t exp_paper_b1 = rgb333_to_argb8888(7, 7, 0);

        // Write to bank 1: NR 0x43 b6=1, b1 still 0 (read still bank 0).
        emu.nextreg().write(0x43, 0x40);
        emu.port().out(0xBF3B, ink_idx);
        emu.nextreg().write(0xFF, ink_byte_b1);
        emu.port().out(0xBF3B, paper_idx);
        emu.nextreg().write(0xFF, paper_byte_b1);

        // First, read with active=bank 0 — bank-0 values must be intact.
        emu.nextreg().write(0x43, 0x00);   // b6=0, b1=0 (read bank 0)
        std::array<uint32_t, Ula::FB_WIDTH> line_b0_after{};
        render_line(emu, 32, line_b0_after);
        const uint32_t b0_ink_after   = line_b0_after[Ula::DISP_X + 0];
        const uint32_t b0_paper_after = line_b0_after[Ula::DISP_X + 2];

        // Now flip read-side to bank 1 — bank-1 values must surface.
        emu.nextreg().write(0x43, 0x02);   // b6=0, b1=1 (read bank 1)
        std::array<uint32_t, Ula::FB_WIDTH> line_b1{};
        render_line(emu, 32, line_b1);
        const uint32_t b1_ink   = line_b1[Ula::DISP_X + 0];
        const uint32_t b1_paper = line_b1[Ula::DISP_X + 2];

        // Negative-gate expectations: with ulap_en=0 the std-ULA encoder
        // takes over and reads the unmodified ULA palette boot defaults
        // (NR 0xFF poke writes only the separate ulap_poke_rgb333_ store
        // per zxnext.vhd:6957-6958, so ula_pixel 0x07 / 0x10 retain their
        // canonical white / black ARGB).  Bank: NR 0x43 b1=0 above.
        const uint32_t off_exp_ink   = emu_ink_argb(emu, 7);
        const uint32_t off_exp_paper = emu_paper_argb(emu, 0);

        const bool all_ok = en_check
                         && mode_chk  == 0x01
                         && got_ink   == exp_ink
                         && got_paper == exp_paper
                         && en_off
                         && off_ink   == off_exp_ink
                         && off_paper == off_exp_paper
                         // Bank-0 entries survived the bank-1 writes.
                         && b0_ink_after   == exp_ink
                         && b0_paper_after == exp_paper
                         // Bank-1 entries surface when active read = b1.
                         && b1_ink   == exp_ink_b1
                         && b1_paper == exp_paper_b1;

        check("INT-ULAPLUS-03",
              "ULA+ runtime palette: BF3B index latch + NR 0xFF poke + "
              "encoder + active_ula_palette routes pixels to ulap_colour; "
              "bank-0/bank-1 isolation via NR 0x43 b6 (write) / b1 (read); "
              "negative gate (ulap_en=0) restores std-ULA boot defaults  "
              "(zxnext.vhd:4533-4535,6957-6958,6981; zxula.vhd:531-541; "
              "src/video/ula.cpp render_display_line ulap_en branch)",
              all_ok,
              fmt("en=%d mode=0x%02X b0_ink=0x%08X (exp 0x%08X) "
                  "b0_paper=0x%08X (exp 0x%08X) en_off=%d "
                  "off_ink=0x%08X (exp 0x%08X) off_paper=0x%08X (exp 0x%08X) "
                  "b0_after_ink=0x%08X b0_after_paper=0x%08X "
                  "b1_ink=0x%08X (exp 0x%08X) b1_paper=0x%08X (exp 0x%08X)",
                  en_check, mode_chk,
                  got_ink, exp_ink, got_paper, exp_paper,
                  en_off,
                  off_ink, off_exp_ink, off_paper, off_exp_paper,
                  b0_ink_after, b0_paper_after,
                  b1_ink, exp_ink_b1, b1_paper, exp_paper_b1));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group C — ULAnext end-to-end through NR 0x42 / NR 0x43
// VHDL: zxnext.vhd:5386 (NR 0x42), 5394 (NR 0x43 bit 0), zxula.vhd:503-515
// jnext: src/core/emulator.cpp:322-330 (NR 0x43), 540-545 (NR 0x42)
// ══════════════════════════════════════════════════════════════════════

static void test_ulanext_integration(Emulator& emu) {
    set_group("INT-ULANEXT");

    // ── INT-ULANEXT-01 — NR 0x43 bit 0 + NR 0x42 = 0x0F via nextreg().write ──
    //
    // Path exercised:
    //   zxnext.vhd:5394  nr_43_ulanext_en <= nr_wr_dat(0) (bit 0 only).
    //                    Remaining bits are palette-group controls owned
    //                    by the Compositor/PaletteManager (plan §Open Q 2).
    //   zxnext.vhd:5386  nr_42_ulanext_format <= nr_wr_dat (full 8 bits).
    //   zxula.vhd:503-515  paper-cycle lookup table:
    //                    case i_ulanext_format is
    //                      when X"0F" => ula_pixel <=
    //                        paper_base_index(7 downto 4) & attr(7 downto 4);
    //                    → ula_pixel = 0x80 | ((attr >> 4) & 0x0F).
    // jnext handlers:
    //   src/core/emulator.cpp:327-330  NR 0x43 write_handler →
    //       palette_.write_control(v)   (for Compositor palette bits)
    //       ula().set_ulanext_en((v & 0x01) != 0)   (narrow enable)
    //   src/core/emulator.cpp:540-545  NR 0x42 write_handler →
    //       ula().set_ulanext_format(v).
    //
    // Stimulus:
    //   NR 0x43 = 0x01   ; bit 0 = enable, palette-group bits = 0 (safe
    //                      no-op for Compositor — matches reset defaults)
    //   NR 0x42 = 0x0F   ; format byte for paper-cycle row zxula.vhd:521
    //
    // Observable through the narrow accessors (Ula-side state that the
    // ULAnext encoder consumes):
    //   ula().get_ulanext_en()     == true
    //   ula().get_ulanext_format() == 0x0F
    //
    // Pin the VHDL-specified encoder output with attr=0xB0 (paper cycle,
    // pixel_en=0): ula_pixel = 0x80 | ((0xB0 >> 4) & 0x0F) = 0x80 | 0x0B
    // = 0x8B.  select_bgnd must NOT assert for format 0x0F (only non-
    // list formats like "when others" assert transparency at line 525).
    {
        // Read reset-default state — helps pinpoint which hop failed if
        // the check fails.
        const bool def_en  = emu.ula().get_ulanext_en();
        const uint8_t def_fmt = emu.ula().get_ulanext_format();

        emu.nextreg().write(0x43, 0x01);     // bit 0 = ULAnext enable
        const bool en_after = emu.ula().get_ulanext_en();

        emu.nextreg().write(0x42, 0x0F);
        const uint8_t fmt_after = emu.ula().get_ulanext_format();

        // Encoder consumer — confirms the Ula-state handoff reaches the
        // pixel path (zxula.vhd:521 paper-cycle branch).
        auto r = emu.ula().compute_ulanext_pixel(/*pixel_en*/false,
                                                 /*border*/false,
                                                 /*attr*/0xB0);
        const uint8_t exp_pixel = 0x8B;

        const bool all_ok = def_en == false
                         && def_fmt == 0x07     // VHDL reset zxnext.vhd:5002
                         && en_after == true
                         && fmt_after == 0x0F
                         && r.pixel == exp_pixel
                         && r.select_bgnd == false;

        check("INT-ULANEXT-01",
              "nextreg().write(0x43,0x01) + write(0x42,0x0F) → ulanext_en=1, "
              "format=0x0F, encoder paper = 0x80|(attr>>4)  "
              "(zxula.vhd:521; zxnext.vhd:5386,5394,5002; emulator.cpp:322-330,540-545)",
              all_ok,
              fmt("def_en=%d def_fmt=0x%02X en_after=%d fmt_after=0x%02X "
                  "enc.pixel=0x%02X (exp 0x%02X) bgnd=%d (exp 0)",
                  def_en, def_fmt, en_after, fmt_after,
                  r.pixel, exp_pixel, r.select_bgnd));
    }

    // INT-ULANEXT-02 — runtime renderer integration end-to-end (G102).
    //
    // Path exercised (VHDL → jnext):
    //   zxula.vhd:485-528  ULAnext encoder produces 8-bit ula_pixel.
    //                      Paper cycle (pixel_en=0, format=0x07) emits
    //                      0x80 | ((attr>>3) & 0x1F)  (line 520).
    //   zxnext.vhd:6981    runtime palette read uses
    //                      '0' & ula_palette_select_1 & ula_pixel(7:0)
    //                      i.e. 256-entry x 2-bank ULA palette store.
    //   zxnext.vhd:6952-6953 NR 0x40+0x41 writes to ULA target populate
    //                      palette_utm at nr_palette_idx[7:0] (the FULL
    //                      8-bit index, no 4-bit mask — proven by
    //                      jnext PaletteManager::write_entry G102 fix).
    //   zxnext.vhd:5391-5394  NR 0x43 b0 = ULAnext enable;
    //                         NR 0x43 b1 = active_ula_palette select.
    // jnext: emulator.cpp NR 0x40/0x41/0x43 handlers route writes into
    //        PaletteManager + Ula::set_ulanext_en + the palsel mirror;
    //        Ula::render_display_line ulanext_en branch invokes
    //        compute_ulanext_pixel and palette_->ula_colour (G102:
    //        single 256-entry × 2-bank store).
    //
    // Stimulus uses a paper cycle so the ULAnext encoder lands at
    // idx ≥ 0x80 — a slot the std-ULA encoder cannot reach (its range
    // is 0x00..0x1F per VHDL :543-553).  This makes the negative gate
    // trustworthy: under ulanext_en=0 the std-ULA encoder for paper
    // bits 5:3=5 emits ula_pixel = 0x10 | 0 | 5 = 0x15, which holds
    // the boot-default ZX colour 5 (cyan) — the ULAnext-only write
    // at idx 0x85 cannot perturb it.
    //
    //   1. Enable ULAnext via NR 0x43 = 0x01 (b0=1, b1=0 → bank 0).
    //   2. NR 0x42 = 0x07 (default; paper encoder = 0x80 | (attr>>3 & 0x1F)).
    //   3. Plant pixel byte 0x00 (all paper) at row 0 col 0.
    //   4. Plant attr byte 0x28 (paper=5, ink=0; bits 5:3 = 101 → 5).
    //      Paper encoder: 0x80 | ((0x28 >> 3) & 0x1F) = 0x80 | 5 = 0x85.
    //   5. NR 0x43 = 0x01 (target=ULA_FIRST), NR 0x40 = 0x85 (idx),
    //      NR 0x41 = 0xC3 (bright red).
    //   6. Render display row 0 (fb row 32); assert pixel at fb_x = DISP_X
    //      matches the poked colour (idx 0x85 in the wider palette).
    //
    // Bank-1 isolation: write a distinct colour into bank 1 at the same
    // idx, switch active read bank to 1 via NR 0x43 = 0x03 (b0=1, b1=1),
    // re-render, assert the bank-1 colour now surfaces.
    //
    // Negative gate: disable ULAnext (NR 0x43 = 0x00) — the std-ULA
    // encoder takes over and emits ula_pixel = 0x15 for the paper
    // cycle of attr=0x28; idx 0x15 holds the canonical ZX colour 5
    // (cyan) at boot defaults.
    {
        // Reset baseline scroll state from prior INT rows.
        emu.nextreg().write(0x27, 0x00);
        emu.nextreg().write(0x26, 0x00);
        emu.nextreg().write(0x68, 0x00);   // ulap_en off, fine-x off
        emu.port().out(0xBF3B, 0x00);      // ulap_mode=00
        emu.port().out(0xFF3B, 0x00);      // ulap_en=0

        // Plant pixel + attr.  pixel byte 0x00 (all paper); attr 0x28
        // (paper=5, ink=0).  Paper-cycle encoder yields idx 0x85.
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x28);
        poke_bank5(emu, 0x4000 + emu_pixel_addr_offset(0, 0), 0x00);

        // Enable ULAnext, set format = 0x07 (default).
        emu.nextreg().write(0x42, 0x07);
        emu.nextreg().write(0x43, 0x01);   // ulanext_en=1, active_ula=0,
                                           // write_select=0 (ULA_FIRST)

        // VHDL encoder paper cycle (zxula.vhd:520):
        //   ula_pixel = paper_base_index(7:5) & attr(7:3)
        //             = "100" & "00101" = 0x85
        const uint8_t exp_idx_b0 = 0x85;

        // Bank-0 palette write at idx 0x85.  RRRGGGBB byte 0xC3 → bright
        // red (r=6, g=0, b3=7).
        const uint8_t  bank0_byte = 0xC3;
        const uint32_t exp_bank0  = rgb333_to_argb8888(6, 0, 7);
        emu.nextreg().write(0x40, exp_idx_b0);
        emu.nextreg().write(0x41, bank0_byte);

        std::array<uint32_t, Ula::FB_WIDTH> line_b0{};
        render_line(emu, 32, line_b0);
        const uint32_t got_b0 = line_b0[Ula::DISP_X];

        // Bank-1 palette write at the same idx — switch write target to
        // ULA_SECOND (NR 0x43 b6:4 = 100; ulanext_en stays 1; active_ula
        // stays 0 so we can verify isolation by switching read bank
        // separately).  NR 0x43 = 0b01000001 = 0x41.
        emu.nextreg().write(0x43, 0x41);   // ulanext_en=1, write_select=4
                                           // (ULA_SECOND), active_ula=0
        emu.nextreg().write(0x40, exp_idx_b0);
        const uint8_t  bank1_byte = 0x1C;  // 000_111_00 → r=0, g=7, b3=0
        const uint32_t exp_bank1  = rgb333_to_argb8888(0, 7, 0);
        emu.nextreg().write(0x41, bank1_byte);

        // Read with active bank still 0 — bank-0 colour must persist.
        emu.nextreg().write(0x43, 0x01);
        std::array<uint32_t, Ula::FB_WIDTH> line_b0_after{};
        render_line(emu, 32, line_b0_after);
        const uint32_t got_b0_after = line_b0_after[Ula::DISP_X];

        // Flip active read bank to 1 — bank-1 colour must surface.
        emu.nextreg().write(0x43, 0x03);   // ulanext_en=1, active_ula=1
        std::array<uint32_t, Ula::FB_WIDTH> line_b1{};
        render_line(emu, 32, line_b1);
        const uint32_t got_b1 = line_b1[Ula::DISP_X];

        // Negative gate: disable ULAnext.  The renderer falls back to
        // the std-ULA encoder, which emits ula_pixel = 0x15 for the
        // paper cycle of attr=0x28 (paper=5).  Boot-default ULA palette
        // at idx 0x15 holds ZX colour 5 (cyan).
        emu.nextreg().write(0x43, 0x00);
        std::array<uint32_t, Ula::FB_WIDTH> line_off{};
        render_line(emu, 32, line_off);
        const uint32_t got_off = line_off[Ula::DISP_X];
        const uint32_t exp_off = emu_paper_argb(emu, 5);

        const bool all_ok = got_b0       == exp_bank0
                         && got_b0_after == exp_bank0
                         && got_b1       == exp_bank1
                         && got_off      == exp_off;

        check("INT-ULANEXT-02",
              "ULAnext runtime palette: NR 0x43 b0 enable + NR 0x42 format "
              "+ NR 0x40/0x41 8-bit poke at full nr_palette_idx routes "
              "pixels through compute_ulanext_pixel + ula_colour; "
              "bank-0/bank-1 isolation via NR 0x43 b1 (active_ula); "
              "negative gate (ulanext_en=0) restores std-ULA boot defaults  "
              "(zxula.vhd:485-528, :520; zxnext.vhd:5394, 6952-6981; "
              "src/video/ula.cpp render_display_line ulanext_en branch)",
              all_ok,
              fmt("got_b0=0x%08X (exp 0x%08X) "
                  "got_b0_after=0x%08X "
                  "got_b1=0x%08X (exp 0x%08X) "
                  "got_off=0x%08X (exp cyan 0x%08X)",
                  got_b0, exp_bank0,
                  got_b0_after,
                  got_b1, exp_bank1,
                  got_off, exp_off));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group C2 — Active-palette selector end-to-end through NR 0x43 / NR 0x6B
// VHDL: zxnext.vhd:5391-5393 (NR 0x43 b1-3 latches),
//       zxnext.vhd:5462      (NR 0x6B control(4) latch),
//       zxnext.vhd:6825-6828 (active-palette mux per pixel cycle).
// jnext: src/core/emulator.cpp NR 0x43 handler (b1-3 → Ula selector setters
//        + palsel change-log), NR 0x6B handler (b4 → Ula tilemap selector
//        + palsel change-log).  Lifecycle: Emulator::run_frame →
//        Ula::palsel_start_frame; Emulator::on_scanline → Ula::
//        set_palsel_current_line; Renderer::render_frame → Ula::
//        palsel_rewind_to_baseline + apply_changes_for_line per row.
//
// G10 closes S17.01..04 at the bare-Ula level. This INT row pins the
// END-TO-END pipeline: a NR 0x43 / NR 0x6B write through nextreg().write
// (the production path the Z80 takes) populates Ula's per-scanline
// selector change-log, and the rewind+replay machinery restores the
// pre-write selector for prior scanlines and asserts the post-write
// selector for subsequent scanlines.  Without the wirings G10's setters
// are unreached from production and the change-log stays empty.
// ══════════════════════════════════════════════════════════════════════

static void test_palsel_integration(Emulator& emu) {
    set_group("INT-PALSEL");

    // ── INT-PALSEL-01 — NR 0x43 + NR 0x6B mid-frame writes drive the
    //                   Ula palsel change-log via nextreg().write ──
    //
    // Stimulus, all delivered in scanline order via the production path:
    //   1. Re-pin baseline:    palsel_start_frame() (mimics Emulator::
    //                          run_frame's start-of-frame snapshot).
    //   2. Tag line 50; nextreg().write(0x43, 0x02)
    //                          → Ula::set_active_ula_palette(true)
    //                          + b2/b3 cleared (single 3-tuple snapshot).
    //   3. Tag line 80; nextreg().write(0x43, 0x06)
    //                          → b1+b2 set, b3 cleared.
    //   4. Tag line 120; nextreg().write(0x6B, 0x90)
    //                          → b4 set (TM enable bit 7 also set, but
    //                          control(4) is the latch we care about).
    //
    // After replay+rewind:
    //   palsel43_change_log_size() == 6  (NR 0x43 stream — 2 writes × 3
    //                                     setters/write; each setter
    //                                     captures a 3-tuple snapshot
    //                                     per UL-C's design).
    //   palsel6b_change_log_size() == 1  (NR 0x6B b4 stream — 1 write).
    //   All 3 entries from the same write share the same `line` tag, so
    //   replay sees the post-write state (last entry on that line wins).
    //
    // After palsel_rewind_to_baseline() + apply_changes_for_line(line):
    //   line=49  → all selectors at baseline (false — VHDL reset zxnext.vhd:5006)
    //   line=50  → active_ula=true, others baseline
    //   line=80  → active_ula=true, active_layer2=true (b2 set), spr=false
    //   line=119 → tilemap selector still false (NR 0x6B latch not yet flipped)
    //   line=120 → tilemap selector true
    {
        // 1. Open frame at baseline state (post-reset = all false).
        emu.ula().palsel_start_frame();

        const size_t n43_initial = emu.ula().palsel43_change_log_size();
        const size_t n6b_initial = emu.ula().palsel6b_change_log_size();

        // 2. NR 0x43 write at line 50 — only b1 set (active_ula).
        emu.ula().set_palsel_current_line(50);
        emu.nextreg().write(0x43, 0x02);  // b1 = 1 → active_ula
        // 3. NR 0x43 write at line 80 — b1+b2 set.
        emu.ula().set_palsel_current_line(80);
        emu.nextreg().write(0x43, 0x06);  // b1+b2 → active_ula + active_l2
        // 4. NR 0x6B write at line 120 — b4 set (independent latch).
        emu.ula().set_palsel_current_line(120);
        emu.nextreg().write(0x6B, 0x90);  // b7 (TM en) + b4 (palsel) set

        const size_t n43_logged = emu.ula().palsel43_change_log_size();
        const size_t n6b_logged = emu.ula().palsel6b_change_log_size();

        // NR 0x43 handler invokes 3 separate setters (one per b1/b2/b3
        // bit-lane), each appending a 3-tuple snapshot to the log; so
        // 1 NR 0x43 write produces 3 log entries — all with the same
        // scanline tag, so replay correctly sees the post-write state.
        // NR 0x6B b4 stream is independent; its single write produces 1
        // entry. Two NR 0x43 writes + one NR 0x6B write → 43=6, 6b=1.
        const bool counts_ok =
            (n43_initial == 0) && (n6b_initial == 0)
         && (n43_logged  == 6) && (n6b_logged  == 1);

        // Verify line tags. All entries from one NR 0x43 write share that
        // write's scanline; the LAST entry per write captures the
        // post-write state (which is what replay locks in).
        bool tags_ok = false;
        bool fields_ok = false;
        if (counts_ok) {
            // 3 entries from line 50, 3 from line 80.
            const auto& e0 = emu.ula().palsel43_change_at(0);
            const auto& e2 = emu.ula().palsel43_change_at(2);  // line 50 final
            const auto& e3 = emu.ula().palsel43_change_at(3);
            const auto& e5 = emu.ula().palsel43_change_at(5);  // line 80 final
            const auto& t0 = emu.ula().palsel6b_change_at(0);

            tags_ok = (e0.line == 50)  && (e2.line == 50)
                   && (e3.line == 80)  && (e5.line == 80)
                   && (t0.line == 120);

            // Final state at end of each NR 0x43 write must match the
            // value byte: line 50 final → only b1 set; line 80 final →
            // b1+b2 set, b3 cleared.
            fields_ok =
                (e2.active_ula == true)  && (e2.active_l2 == false) && (e2.active_spr == false)
             && (e5.active_ula == true)  && (e5.active_l2 == true)  && (e5.active_spr == false)
             && (t0.active_tm  == true);
        }

        // Now exercise the rewind+replay machinery the Renderer drives.
        // Mid-frame state is "everything set" by the time we get here —
        // rewind must restore the baseline (all-false post-reset).
        emu.ula().palsel_rewind_to_baseline();
        const bool rewind_ula_b   = (emu.ula().get_active_ula_palette()     == false);
        const bool rewind_l2_b    = (emu.ula().get_active_layer2_palette()  == false);
        const bool rewind_spr_b   = (emu.ula().get_active_sprite_palette()  == false);
        const bool rewind_tm_b    = (emu.ula().get_active_tilemap_palette() == false);

        // Replay walks the change-log in scanline order. Apply each line's
        // entries and read the live selector after.
        emu.ula().palsel_apply_changes_for_line(49);
        const bool at49_ula = emu.ula().get_active_ula_palette();
        emu.ula().palsel_apply_changes_for_line(50);
        const bool at50_ula = emu.ula().get_active_ula_palette();
        emu.ula().palsel_apply_changes_for_line(79);
        const bool at79_l2  = emu.ula().get_active_layer2_palette();
        emu.ula().palsel_apply_changes_for_line(80);
        const bool at80_l2  = emu.ula().get_active_layer2_palette();
        const bool at80_ula = emu.ula().get_active_ula_palette();
        emu.ula().palsel_apply_changes_for_line(119);
        const bool at119_tm = emu.ula().get_active_tilemap_palette();
        emu.ula().palsel_apply_changes_for_line(120);
        const bool at120_tm = emu.ula().get_active_tilemap_palette();

        const bool replay_ok =
            // Rewind landed at baseline (all four false).
            rewind_ula_b && rewind_l2_b && rewind_spr_b && rewind_tm_b
            // Pre-line-50: ULA selector still baseline; line 50: flips on.
         && (at49_ula == false) && (at50_ula == true)
            // Pre-line-80: L2 selector still baseline; line 80: flips on.
         && (at79_l2  == false) && (at80_l2  == true)  && (at80_ula == true)
            // NR 0x6B b4 stream: pre-line-120 false; line 120: flips on.
         && (at119_tm == false) && (at120_tm == true);

        const bool all_ok = counts_ok && tags_ok && fields_ok && replay_ok;

        check("INT-PALSEL-01",
              "nextreg().write(0x43,…) + write(0x6B,…) end-to-end through "
              "Ula palsel change-log; rewind+replay restores per-scanline "
              "selectors  (zxnext.vhd:5391-5393,:5462,:6825-6828; "
              "emulator.cpp NR 0x43 / NR 0x6B handlers; G10)",
              all_ok,
              fmt("counts[43=%zu 6b=%zu] tags=%d fields=%d "
                  "replay[ula49=%d ula50=%d l279=%d l280=%d ula80=%d "
                  "tm119=%d tm120=%d]",
                  n43_logged, n6b_logged,
                  tags_ok ? 1 : 0, fields_ok ? 1 : 0,
                  at49_ula ? 1 : 0, at50_ula ? 1 : 0,
                  at79_l2  ? 1 : 0, at80_l2  ? 1 : 0, at80_ula ? 1 : 0,
                  at119_tm ? 1 : 0, at120_tm ? 1 : 0));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group D — Timex alt-file end-to-end through port 0xFF
// VHDL: zxula.vhd:218,235 (alt-file → vram_a bit 13), zxnext.vhd:2397
//       (port 0xFF gate by NR 0x82 bit 0), src/core/emulator.cpp:1187-1192
//
// Scope: verifies the alt-file → pixel-base routing in STANDARD_1 mode
// (port_ff(2:0) = 001 per VHDL zxula.vhd:191; alt_file = mode bit 0 = 1;
// i.e. port 0xFF = 0x01).  Note: byte 0x09 also works numerically because
// it has both bit 0 and bit 3 set — but the canonical VHDL value is 0x01.
// Historical: the plan originally named this "HICOLOUR-ALT" (mode 011 =
// VHDL byte 0x03) but renamed here to STANDARD-ALT because:
//   - The alt_file → 0x6000 routing is identical across all modes
//     (VHDL :235 drives vram_a(13) from screen_mode(0) unconditionally).
//   - HI_COLOUR+alt would additionally require planting attributes at
//     the alt-attr base (0x7800 per :239/:249), which this test doesn't.
// A future row INT-HICOLOUR-ALT-01 (stimulus 0x18 + alt-attr plant) can
// land as a standalone follow-up. The current STANDARD_1+alt row gives
// coverage of the Wave D alt_file derivation with minimal setup.
//
// INT-SHADOW-01 is now live — landed 2026-04-24 alongside the MMU
// shadow-screen wiring (doc/design/TASK-MMU-SHADOW-SCREEN-PLAN.md). See
// test_shadow_integration() below.
// ══════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════
// Group E — Shadow-screen end-to-end through port 0x7FFD bit 3
// VHDL: zxnext.vhd:3640 (port_7ffd_reg latch), :4453 (i_ula_shadow_en <=
//       port_7ffd_reg(3)), zxula.vhd:191 (shadow gates screen_mode=000).
// jnext: src/core/emulator.cpp 0x7FFD handler (forwards
//       Mmu::shadow_screen_en() into Ula::set_shadow_screen_en).
// ══════════════════════════════════════════════════════════════════════

static void test_shadow_integration(Emulator& emu) {
    set_group("INT-SHADOW");

    // ── INT-SHADOW-01 — port 0x7FFD bit 3 → Ula::shadow_screen_en ──
    //
    // Path exercised:
    //   OUT 0x7FFD, v    (port_dispatch handler in emulator.cpp)
    //     → Mmu::map_128k_bank(v)              (VHDL zxnext.vhd:3640)
    //     → Mmu::shadow_screen_en() returns bit 3
    //     → Ula::set_shadow_screen_en(bit3)    (VHDL zxnext.vhd:4453)
    //
    // Regression guard against the pre-2026-04-24 gap where the 0x7FFD
    // handler wired MMU paging + DivMMC rom3 + contention, but did NOT
    // forward the shadow bit. Any Z80 program enabling shadow via
    // `OUT (n),a : 0x7FFD, 0x08` would leave the ULA reading the primary
    // screen bank.
    //
    // Stimulus:
    //   1. Reset-default observation: bit clear → ula.shadow=0.
    //   2. OUT 0x7FFD, 0x08 — only bit 3 asserted, bits 0-2 select bank
    //      0 (reset default), bits 4-7 = 0 (ROM 0, no lock).
    //   3. OUT 0x7FFD, 0x00 — clears bit 3. Must drop shadow.
    //
    // Observable on Ula through the narrow accessor the Compositor
    // consumes (Ula::get_shadow_screen_en()). The downstream
    // screen_mode→"000" gate at zxula.vhd:191 is exercised by the
    // ula_test §15 S15.02 row on the bare Ula class; this row covers
    // only the port-dispatch → MMU → Ula handoff.
    {
        const bool boot_clear = (emu.ula().get_shadow_screen_en() == false);

        emu.port().out(0x7FFD, 0x08);
        const bool on_after_set = emu.ula().get_shadow_screen_en();

        emu.port().out(0x7FFD, 0x00);
        const bool off_after_clear = (emu.ula().get_shadow_screen_en() == false);

        const bool all_ok = boot_clear && on_after_set && off_after_clear;

        check("INT-SHADOW-01",
              "OUT 0x7FFD bit 3 → Ula::shadow_screen_en  "
              "(zxnext.vhd:3640,:4453; emulator.cpp 0x7FFD handler)",
              all_ok,
              fmt("boot_clear=%d on_after_set=%d off_after_clear=%d",
                  boot_clear, on_after_set, off_after_clear));
    }

    // ── INT-SHADOW-02 — port 0x7FFD bit 3 actually switches the ULA bank ──
    //
    // INT-SHADOW-01 only checks the flag toggle. That gap let beast.nex
    // ship with a half-implemented setter: `Ula::set_shadow_screen_en`
    // recorded the flag but did not flip `vram_use_bank7_`, so render
    // kept reading bank 5 (game data) instead of bank 7 (the shadow
    // screen pre-filled with uniform attrs by the demo).
    //
    // Stimulus: distinct ink colours into row-0 attribute byte of each
    // bank (cyan/5 in bank 5, red/2 in bank 7), pixel byte 0xFF in both.
    // OUT 0x7FFD,0x08 → render scanline 32 → first display column must
    // flip cyan → red. VHDL ula_bank_do <= vram_bank7_do when
    // port_7ffd_shadow='1'.
    {
        // Defensive cleanup of state left by earlier rows in this suite:
        //   INT-SCROLL-03 leaves NR 0x27 = 32 (Y-scroll), which would
        //   make render row 0 fold to source row 32 and miss our writes
        //   at offset 0 entirely (whence the off=white observed during
        //   bring-up). NR 0x26 + NR 0x68 also touched by scroll rows.
        //   INT-ULANEXT-01 / INT-ULAPLUS-02 leave their respective
        //   modes on, which would route through alternative paper/ink
        //   encoders.
        emu.nextreg().write(0x26, 0x00);  // ULA scroll-X coarse = 0
        emu.nextreg().write(0x27, 0x00);  // ULA scroll-Y = 0
        emu.nextreg().write(0x68, 0x00);  // bits 7/3/2 → ULA-disable / ulap_en / fine-x
        emu.nextreg().write(0x43, 0x00);  // ULAnext en = 0 (zxnext.vhd:5394)

        const uint16_t poff = emu_pixel_addr_offset(0, 0);
        // Bank 5 = dedicated 16K VRAM (Task 25; wired from
        // Mmu::bank5_vram in Emulator::init, like bank 7 below).
        emu.mmu().bank5_vram()[poff]    = 0xFF;
        emu.mmu().bank5_vram()[0x1800u] = 0x05;  // bank5 ink=cyan
        // Bank-7 lower half = dedicated BRAM (NextZXOS-boot fix; wired
        // from Mmu::bank7_bram in Emulator::init).
        emu.mmu().bank7_bram()[poff]    = 0xFF;
        emu.mmu().bank7_bram()[0x1800u] = 0x02;  // bank7 ink=red

        std::array<uint32_t, Ula::FB_WIDTH> line_off{}, line_on{};

        emu.port().out(0x7FFD, 0x00);
        render_line(emu, 32, line_off);

        emu.port().out(0x7FFD, 0x08);
        render_line(emu, 32, line_on);

        // Restore so subsequent rows in the suite see the reset state.
        emu.port().out(0x7FFD, 0x00);

        const uint32_t cyan = emu_ink_argb(emu, 5);
        const uint32_t red  = emu_ink_argb(emu, 2);
        check("INT-SHADOW-02",
              "OUT 0x7FFD bit 3 must switch ULA reads to bank 7 — render-side "
              "regression for the half-implemented set_shadow_screen_en that "
              "tripped beast.nex 2026-04-25",
              line_off[Ula::DISP_X] == cyan && line_on[Ula::DISP_X] == red,
              fmt("off=0x%08X (exp cyan 0x%08X)  on=0x%08X (exp red 0x%08X)",
                  line_off[Ula::DISP_X], cyan, line_on[Ula::DISP_X], red));
    }
}

static void test_altfile_integration(Emulator& emu) {
    set_group("INT-STANDARD-ALT");

    const uint32_t WHITE = emu_ink_argb(emu, 7);
    const uint32_t BLACK = emu_paper_argb(emu, 0);

    // ── INT-STANDARD-ALT-01 — port 0xFF bit 0 selects alt screen bank ──
    //
    // Path exercised:
    //   zxnext.vhd:2397  port_ff_io_en = NR 0x82 bit 0 (reset default:
    //                    1, per VHDL:1226 + nextreg.cpp reset 0xFF).
    //   zxula.vhd:191    screen_mode_s = port_ff(2:0) (bits 2:0 include
    //                    the alt-file bit 0 at :218).
    //   zxula.vhd:218,235 screen_mode(0) selects bit 13 of vram_a — '0'
    //                    → pixel base 0x4000; '1' → pixel base 0x6000.
    // jnext handler: src/core/emulator.cpp:1187-1192 —
    //   port_.register_handler(0xFFFF, 0x00FF, nullptr, [](v){
    //       if ((nr_82 & 1) == 0) return;
    //       renderer_.ula().set_screen_mode(val); });
    // Ula::set_screen_mode reads mode from port_ff(2:0) per VHDL
    // zxula.vhd:191; bit 0 of the 3-bit mode field is the alt-file
    // select.  Byte 0x01 = bits_2:0=001 (STANDARD_1 with alt_file=1) —
    // the canonical "select alternate screen" value at zxula.vhd:218.
    //
    // Stimulus:
    //   1. Plant white marker at row 0 col 0 of the ALTERNATE screen
    //      (CPU 0x6000 + offset = physical bank 5, bank-offset 0x2000
    //      per zxula.vhd:235 which also maps alt_file → vram_a bit 13).
    //   2. Plant black across primary screen (0x4000) to distinguish.
    //   3. OUT 0xFF, 0x01 (mode = STANDARD_1 with alt-file=1 per VHDL
    //      bits 2:0 = 001 — zxula.vhd:191/218/235).
    //   4. Render row 0. Expect WHITE at display_x ∈ [0..7] (alt marker)
    //      rather than BLACK (primary).
    //
    // Note: STANDARD_1 uses BOTH pixels AND attrs from the alt bank at
    // 0x6000/0x7800. We populate both planes at their alt-base offsets.
    {
        // Prior tests left a row-32 marker live in the primary screen.
        // Clear primary + alt comprehensively.
        fill_pixels(emu, 0x00);
        fill_attrs(emu, 0x07);
        // Clear alt pixels (0x6000..0x77FF) and alt attrs (0x7800..0x7AFF).
        for (uint16_t addr = 0x6000; addr < 0x7800; ++addr) {
            poke_bank5(emu, addr, 0x00);
        }
        for (uint16_t addr = 0x7800; addr < 0x7B00; ++addr) {
            poke_bank5(emu, addr, 0x07);
        }
        // Plant marker on ALT row 0 col 0.
        poke_bank5(emu, 0x6000 + emu_pixel_addr_offset(0, 0), 0xFF);

        // Clear scroll state surviving from prior groups.
        emu.nextreg().write(0x26, 0);
        emu.nextreg().write(0x27, 0);
        emu.nextreg().write(0x68, 0x00);   // clears bit 2 fine scroll
        emu.nextreg().write(0x43, 0x00);   // disable ULAnext (re-pin clean state)
        // ULA+ left enabled by INT-ULAPLUS-01, but it only affects the
        // compositor-side ula_pixel 8-bit encode — NOT the Ula scanline
        // output (which still emits standard ink/paper ARGB). So no
        // need to disable it here; but we do for cleanliness.
        emu.port().out(0xBF3B, 0x40);
        emu.port().out(0xFF3B, 0x00);     // disable ULA+

        // Stimulus: OUT 0xFF, 0x01 — VHDL zxula.vhd:191/218/235. Mode
        // bits 2:0 = 001 (STANDARD_1 with alt-file=1 per :218).
        emu.port().out(0x00FF, 0x01);

        const bool alt_flag   = emu.ula().get_alt_file();
        const uint8_t mode_reg = emu.ula().get_screen_mode_reg();

        std::array<uint32_t, Ula::FB_WIDTH> line{};
        render_line(emu, 32, line);

        // Expected: WHITE at display_x [0..7] (alt marker), BLACK at
        // [8..255]. If alt_file wasn't honoured, the renderer would
        // read the primary plane which we cleared to 0 → all-black.
        bool ok_white = true;
        bool ok_black = true;
        for (int x = 0; x < 256; ++x) {
            if (x >= 0 && x <= 7) {
                if (line[Ula::DISP_X + 2*x] != WHITE) { ok_white = false; break; }
            } else {
                if (line[Ula::DISP_X + 2*x] != BLACK) { ok_black = false; break; }
            }
        }

        check("INT-STANDARD-ALT-01",
              "OUT 0xFF=0x01 → STANDARD_1 alt-file routes ULA to 0x6000 base  "
              "(zxula.vhd:191,218,235; zxnext.vhd:2397; emulator.cpp:1187-1192)",
              alt_flag && mode_reg == 0x01 && ok_white && ok_black,
              fmt("alt_file=%d mode_reg=0x%02X ok_white=%d ok_black=%d "
                  "line[DISP_X]=0x%08X line[DISP_X+2*8]=0x%08X",
                  alt_flag, mode_reg, ok_white, ok_black,
                  line[Ula::DISP_X], line[Ula::DISP_X + 2*8]));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("ULA Video Subsystem Integration Tests\n");
    std::printf("=====================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_scroll_integration(emu);
    std::printf("  Group: INT-SCROLL     — done\n");

    test_ulaplus_integration(emu);
    std::printf("  Group: INT-ULAPLUS    — done\n");

    test_ulanext_integration(emu);
    std::printf("  Group: INT-ULANEXT    — done\n");

    test_palsel_integration(emu);
    std::printf("  Group: INT-PALSEL     — done\n");

    test_shadow_integration(emu);
    std::printf("  Group: INT-SHADOW     — done\n");

    test_altfile_integration(emu);
    std::printf("  Group: INT-STANDARD-ALT — done\n");

    std::printf("\n=====================================\n");
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
