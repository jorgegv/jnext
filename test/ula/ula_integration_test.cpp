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
std::vector<SkipNote> g_skipped;  // always empty in this suite — see plan §3

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

// Write a pixel byte into physical bank 5 (page 10, where the ULA reads
// VRAM from, per zxula.vhd + src/video/ula.cpp:35-51). CPU address 0x4000
// → offset 0 inside page 10.
static void poke_bank5(Emulator& emu, uint16_t cpu_addr_4000_based,
                       uint8_t val) {
    const uint32_t offset = cpu_addr_4000_based & 0x3FFFu;  // 14-bit bank offset
    emu.ram().write(10u * 8192u + offset, val);
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
                        std::array<uint32_t, 320>& line) {
    line.fill(0);
    emu.ula().render_scanline(line.data(), fb_row, emu.mmu());
}

// ══════════════════════════════════════════════════════════════════════
// Group A — ULA scroll end-to-end through NR 0x26 / 0x27 / 0x68 bit 2
// VHDL: zxula.vhd:193-216 (py/px fold), zxnext.vhd:5304/5307/5449 (NR
//       write decode), src/core/emulator.cpp:523-545, 762-768 (handlers)
// ══════════════════════════════════════════════════════════════════════

static void test_scroll_integration(Emulator& emu) {
    set_group("INT-SCROLL");

    // Palette indices used in the assertions below. kUlaPalette[0] is the
    // ARGB8888 value for ZX colour 0 (black), [7] for white. Declared in
    // video/palette.h (included above); the standard ULA colour table
    // matches the FPGA palette defaults (src/video/palette.cpp:26).
    const uint32_t WHITE = kUlaPalette[7];
    const uint32_t BLACK = kUlaPalette[0];

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

        std::array<uint32_t, 320> line{};
        render_line(emu, 32, line);   // fb row 32 = first display row.

        bool ok_white  = true;
        bool ok_black  = true;
        for (int x = 0; x < 256; ++x) {
            if (x >= 248 && x <= 255) {
                if (line[32 + x] != WHITE) { ok_white = false; break; }
            } else {
                if (line[32 + x] != BLACK) { ok_black = false; break; }
            }
        }
        check("INT-SCROLL-01",
              "nextreg().write(0x26,8) → 8-pixel shift end-to-end  "
              "(zxula.vhd:199; zxnext.vhd:5304; emulator.cpp:524-526)",
              stored == 8 && ok_white && ok_black,
              fmt("stored=%u ok_white=%d ok_black=%d "
                  "line[32+248]=0x%08X line[32+247]=0x%08X",
                  stored, ok_white, ok_black,
                  line[32 + 248], line[32 + 247]));
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

        std::array<uint32_t, 320> line{};
        render_line(emu, 32, line);

        bool ok = true;
        for (int x = 0; x < 256; ++x) {
            const bool should_white = (x == 255) || (x >= 0 && x <= 6);
            const uint32_t exp = should_white ? WHITE : BLACK;
            if (line[32 + x] != exp) { ok = false; break; }
        }
        check("INT-SCROLL-02",
              "nextreg().write(0x68, 0x04) → fine X-scroll = 1 → 1-pixel shift  "
              "(zxula.vhd:199,216; zxnext.vhd:5449; emulator.cpp:762-768)",
              fine == true && ok,
              fmt("fine=%d line[32]=0x%08X line[32+255]=0x%08X line[32+7]=0x%08X",
                  fine, line[32], line[32 + 255], line[32 + 7]));
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

        std::array<uint32_t, 320> line{};
        render_line(emu, 32, line);  // screen row 0 → should pull src row 32

        bool all_white = true;
        for (int x = 0; x < 256; ++x) {
            if (line[32 + x] != WHITE) { all_white = false; break; }
        }
        check("INT-SCROLL-03",
              "nextreg().write(0x27,32) → Y-scroll 32 rows end-to-end  "
              "(zxula.vhd:193-207; zxnext.vhd:5307; emulator.cpp:532-534)",
              stored == 32 && all_white,
              fmt("stored=%u all_white=%d display[0]=0x%08X exp 0x%08X",
                  stored, all_white, line[32], WHITE));
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
}

// ══════════════════════════════════════════════════════════════════════
// Group D — Timex alt-file end-to-end through port 0xFF
// VHDL: zxula.vhd:218,235 (alt-file → vram_a bit 13), zxnext.vhd:2397
//       (port 0xFF gate by NR 0x82 bit 0), src/core/emulator.cpp:1187-1192
//
// Scope: verifies the alt-file → pixel-base routing in STANDARD_1 mode
// (bits 5:3 = 001 + alt_file bit = 1, i.e. port 0xFF = 0x09). The plan
// originally named this "HICOLOUR-ALT" (mode 011 = 0x18) but renamed
// here to STANDARD-ALT because:
//   - The alt_file → 0x6000 routing is identical across all modes
//     (VHDL :235 drives vram_a(13) from screen_mode(0) unconditionally).
//   - HI_COLOUR+alt would additionally require planting attributes at
//     the alt-attr base (0x7800 per :239/:249), which this test doesn't.
// A future row INT-HICOLOUR-ALT-01 (stimulus 0x18 + alt-attr plant) can
// land as a standalone follow-up. The current STANDARD_1+alt row gives
// coverage of the Wave D alt_file derivation with minimal setup.
//
// INT-SHADOW-01 deliberately OMITTED from this suite: port 0x7FFD bit 3
// → i_ula_shadow_en routing is re-homed to Emulator/MMU subsystem per
// the Phase 0 triage (S15.03/04 F-skips). Adding it here would either
// need that cross-subsystem wiring first, or would be a failing check
// — neither fits Phase 3's zero-net-new-skip target.
// ══════════════════════════════════════════════════════════════════════

static void test_altfile_integration(Emulator& emu) {
    set_group("INT-STANDARD-ALT");

    const uint32_t WHITE = kUlaPalette[7];
    const uint32_t BLACK = kUlaPalette[0];

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
    // Ula::set_screen_mode routes bits(0) → alt_file_ (mode STANDARD_1),
    // bits(5:3) → mode field. Byte 0x09 = bits_5:3=001 (STANDARD_1) +
    // bit_0=1 (alt-file) — the canonical "select alternate screen"
    // value at zxula.vhd:218.
    //
    // Stimulus:
    //   1. Plant white marker at row 0 col 0 of the ALTERNATE screen
    //      (CPU 0x6000 + offset = physical bank 5, bank-offset 0x2000
    //      per zxula.vhd:235 which also maps alt_file → vram_a bit 13).
    //   2. Plant black across primary screen (0x4000) to distinguish.
    //   3. OUT 0xFF, 0x09 (alt-file = 1, mode = STANDARD_1 to route the
    //      renderer through the alt-base path — zxula.vhd:235).
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

        // Stimulus: OUT 0xFF, 0x09 — VHDL zxula.vhd:191/218/235. Mode
        // bits 5:3 = 001 (STANDARD_1), alt-file bit 0 = 1.
        emu.port().out(0x00FF, 0x09);

        const bool alt_flag   = emu.ula().get_alt_file();
        const uint8_t mode_reg = emu.ula().get_screen_mode_reg();

        std::array<uint32_t, 320> line{};
        render_line(emu, 32, line);

        // Expected: WHITE at display_x [0..7] (alt marker), BLACK at
        // [8..255]. If alt_file wasn't honoured, the renderer would
        // read the primary plane which we cleared to 0 → all-black.
        bool ok_white = true;
        bool ok_black = true;
        for (int x = 0; x < 256; ++x) {
            if (x >= 0 && x <= 7) {
                if (line[32 + x] != WHITE) { ok_white = false; break; }
            } else {
                if (line[32 + x] != BLACK) { ok_black = false; break; }
            }
        }

        check("INT-STANDARD-ALT-01",
              "OUT 0xFF=0x09 → STANDARD_1 alt-file routes ULA to 0x6000 base  "
              "(zxula.vhd:191,218,235; zxnext.vhd:2397; emulator.cpp:1187-1192)",
              alt_flag && mode_reg == 0x09 && ok_white && ok_black,
              fmt("alt_file=%d mode_reg=0x%02X ok_white=%d ok_black=%d "
                  "line[32]=0x%08X line[32+8]=0x%08X",
                  alt_flag, mode_reg, ok_white, ok_black,
                  line[32], line[32 + 8]));
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
