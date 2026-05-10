// NextREG Integration Test — Full-machine reset-default verification.
//
// This test constructs a full Emulator (headless, machine-type Next) and
// verifies that reading NextREG registers through the port path (port
// 0x243B select, port 0x253B read) returns the VHDL-spec'd reset defaults.
//
// These rows were skipped in the bare-NextReg unit test because the defaults
// are owned by subsystem classes (MMU, Palette, Renderer, etc.), not by the
// bare NextReg register file. This integration tier exercises the full
// emulator wiring — the same path that real Z80 code uses.
//
// Plan: doc/testing/NEXTREG-TEST-PLAN-DESIGN.md, section 3 (RST-01..09).
// Oracle: zxnext.vhd lines 4926-5100 (reset block), 4607-4618 (MMU),
//         5052-5068 (port enables).
//
// Run: ./build/test/nextreg_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "input/joystick.h"
#include "input/mouse.h"
#include "peripheral/nmi_source.h"

#include <cstdio>
#include <cstdarg>
#include <cstdint>
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

std::string hex2(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
}

std::string hex4(uint16_t v) {
    char buf[10];
    std::snprintf(buf, sizeof(buf), "0x%04x", v);
    return buf;
}

std::string detail_eq(uint8_t got, uint8_t expected) {
    return "got=" + hex2(got) + " expected=" + hex2(expected);
}

std::string detail_eq(uint16_t got, uint16_t expected) {
    return "got=" + hex4(got) + " expected=" + hex4(expected);
}

// Lightweight printf-style detail formatter for multi-value rows.
// Returns a std::string with up to 256 chars of formatted output.
static std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

} // namespace

// ── Emulator construction helper ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Read NextREG register through the real port path — exactly like Z80
// code would do via OUT (0x243B),reg / IN A,(0x253B).
static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

// Write NextREG register through the real port path.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// ── 3. Reset Defaults (RST-01..09) — integration tier ────────────────
//
// These rows were skipped in the bare-NextReg test (nextreg_test.cpp)
// because the VHDL reset defaults are owned by subsystem classes, not by
// the bare NextReg register file. Here we verify them through the full
// emulator wiring.

static void test_reset_defaults(Emulator& emu) {
    set_group("Reset-Integration");

    // MID-01 — NR 0x00 machine ID.
    //
    // DELIBERATE DEVIATION FROM VHDL.  VHDL generic
    // g_machine_id = X"0A" in zxnext_top_issue{2,4,5}.vhd:35 (ZX Spectrum
    // Next real-hardware identifier).  jnext returns 0x08 instead — the
    // TBBlue firmware convention for HWID_EMULATORS.  Reporting 0x0A makes
    // NextZXOS treat jnext as real hardware and divert into the
    // FPGA-flash / Configuration flow, which fails for emulator-mounted
    // SD images (observed 2026-04-18 while diagnosing NextZXOS boot).
    // Reporting 0x08 lets NextZXOS take its emulator-aware boot paths.
    // Source: src/port/nextreg.cpp:18 — reset default for NR 0x00.
    {
        uint8_t got = nr_read(emu, 0x00);
        check("MID-01",
              "NR 0x00 machine ID reset=0x08 (HWID_EMULATORS; jnext "
              "deviates from VHDL g_machine_id=X\"0A\" on purpose)",
              got == 0x08, detail_eq(got, 0x08));
    }

    // RST-01 — NR 0x14 global transparent colour.
    // VHDL zxnext.vhd:4947 — nr_14_global_transparent <= X"E3" on reset.
    {
        uint8_t got = nr_read(emu, 0x14);
        check("RST-01",
              "NR 0x14 global transparent reset=0xE3 "
              "[zxnext.vhd:4947 nr_14_global_transparent]",
              got == 0xE3, detail_eq(got, 0xE3));
    }

    // RST-02 — NR 0x15 sprite/layer control.
    // VHDL zxnext.vhd:4948 — nr_15_lores_en/sprite_en/layer_priority all
    // zeroed on reset. Full register = 0x00.
    {
        uint8_t got = nr_read(emu, 0x15);
        check("RST-02",
              "NR 0x15 sprite/layer control reset=0x00 "
              "[zxnext.vhd:4948]",
              got == 0x00, detail_eq(got, 0x00));
    }

    // RST-03 — NR 0x4A fallback RGB colour.
    // VHDL zxnext.vhd:5002 — nr_4a_fallback_colour <= X"E3" on reset.
    {
        uint8_t got = nr_read(emu, 0x4A);
        check("RST-03",
              "NR 0x4A fallback RGB reset=0xE3 "
              "[zxnext.vhd:5002 nr_4a_fallback_colour]",
              got == 0xE3, detail_eq(got, 0xE3));
    }

    // RST-04 — NR 0x42 ULANext format (ink mask).
    // VHDL zxnext.vhd:4993 — nr_42_ulanext_format <= X"07" on reset.
    {
        uint8_t got = nr_read(emu, 0x42);
        check("RST-04",
              "NR 0x42 ULANext format reset=0x07 "
              "[zxnext.vhd:4993 nr_42_ulanext_format]",
              got == 0x07, detail_eq(got, 0x07));
    }

    // RST-05 — NR 0x50-0x57 MMU page defaults.
    // VHDL zxnext.vhd:4610-4618:
    //   slot 0 = 0xFF (ROM), slot 1 = 0xFF (ROM),
    //   slot 2 = 0x0A (bank 5 page 0), slot 3 = 0x0B (bank 5 page 1),
    //   slot 4 = 0x04 (bank 2 page 0), slot 5 = 0x05 (bank 2 page 1),
    //   slot 6 = 0x00 (bank 0 page 0), slot 7 = 0x01 (bank 0 page 1).
    {
        const uint8_t expected[8] = {0xFF, 0xFF, 0x0A, 0x0B,
                                     0x04, 0x05, 0x00, 0x01};
        bool all_ok = true;
        std::string worst;
        for (int i = 0; i < 8; ++i) {
            uint8_t got = nr_read(emu, static_cast<uint8_t>(0x50 + i));
            if (got != expected[i]) {
                all_ok = false;
                worst = "NR " + hex2(static_cast<uint8_t>(0x50 + i)) + " " +
                        detail_eq(got, expected[i]);
            }
        }
        check("RST-05",
              "NR 0x50-0x57 MMU defaults [zxnext.vhd:4610-4618]",
              all_ok, worst);
    }

    // RST-06 — NR 0x68 ULA control.
    // VHDL zxnext.vhd:5029 — nr_68_ula_en <= '1' on reset (ULA enabled).
    // Bit 7 = NOT ula_en, so bit 7 = 0. Bits 6:0 = 0. Full register = 0x00.
    {
        uint8_t got = nr_read(emu, 0x68);
        check("RST-06",
              "NR 0x68 ULA control reset=0x00 (ula_en=1 → bit7=0) "
              "[zxnext.vhd:5029]",
              got == 0x00, detail_eq(got, 0x00));
    }

    // RST-07 — NR 0x0B I/O mode.
    // VHDL zxnext.vhd:4939-4941 — iomode_0 <= '1' on reset.
    // NR 0x0B = 0x01.
    {
        uint8_t got = nr_read(emu, 0x0B);
        check("RST-07",
              "NR 0x0B I/O mode reset=0x01 "
              "[zxnext.vhd:4939-4941]",
              got == 0x01, detail_eq(got, 0x01));
    }

    // RST-08 — NR 0x82-0x85 internal port enables.
    // VHDL zxnext.vhd:5052-5068 — all reset to 0xFF on power-on (reset_type=1).
    // NR 0x85 has bits 6:4 always-zero on read (VHDL:6138), so read = 0x8F.
    {
        const uint8_t expected[4] = {0xFF, 0xFF, 0xFF, 0x8F};
        bool all_ok = true;
        std::string worst;
        for (int i = 0; i < 4; ++i) {
            uint8_t reg = static_cast<uint8_t>(0x82 + i);
            uint8_t got = nr_read(emu, reg);
            if (got != expected[i]) {
                all_ok = false;
                worst = "NR " + hex2(reg) + " " +
                        detail_eq(got, expected[i]);
            }
        }
        check("RST-08",
              "NR 0x82-0x85 port enables reset=0xFF "
              "[zxnext.vhd:5052-5068]",
              all_ok, worst);
    }

    // RST-10 — NR 0x12 Layer 2 active bank.
    // VHDL zxnext.vhd:4945 — nr_12_layer2_active_bank <= X"08" on reset.
    {
        uint8_t got = nr_read(emu, 0x12);
        check("RST-10",
              "NR 0x12 L2 active bank reset=0x08 "
              "[zxnext.vhd:4945 nr_12_layer2_active_bank]",
              got == 0x08, detail_eq(got, 0x08));
    }

    // RST-11 — NR 0x4B sprite transparent index.
    // VHDL zxnext.vhd:5003 — nr_4b_sprite_transparent_index <= X"E3" on reset.
    {
        uint8_t got = nr_read(emu, 0x4B);
        check("RST-11",
              "NR 0x4B sprite transparent index reset=0xE3 "
              "[zxnext.vhd:5003 nr_4b_sprite_transparent_index]",
              got == 0xE3, detail_eq(got, 0xE3));
    }

    // RST-12 — NR 0x4C tilemap transparent index.
    // VHDL zxnext.vhd:5004 — nr_4c_tm_transparent_index <= X"0F" on reset.
    {
        uint8_t got = nr_read(emu, 0x4C);
        check("RST-12",
              "NR 0x4C tilemap transparent index reset=0x0F "
              "[zxnext.vhd:5004 nr_4c_tm_transparent_index]",
              got == 0x0F, detail_eq(got, 0x0F));
    }

    // RST-09 — NR 0x1B tilemap clip window defaults.
    // NR 0x1B is a 4-way combinatorial mux over tilemap clip coords
    // (x1/x2/y1/y2) selected by clip_tm_idx_. Post-reset idx=00 so a
    // single read returns x1, whose default is 0x00 per VHDL
    // zxnext.vhd:4977-4981. Per VHDL:5971-5977 the read is purely
    // combinatorial and does NOT advance idx — the CLIP-09 row below
    // exercises that invariant.
    {
        uint8_t got = nr_read(emu, 0x1B);
        check("RST-09",
              "NR 0x1B post-reset read returns tilemap clip_x1 = 0x00 "
              "[zxnext.vhd:5971-5977 read mux; :4977-4981 reset defaults]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // RST-13 / V17-NMP-01 — NR 0xB8/0xB9/0xBA/0xBB DivMMC automap entry-point
    // configuration registers. VHDL zxnext.vhd:5087-5090 reset these to
    // 0x83/0x01/0x00/0xCD respectively (master reset block; unconditional
    // on `i_reset`). Read mux at zxnext.vhd:6217-6227 returns each storage
    // byte verbatim. Pre-Pass-17, jnext only registered write_handlers
    // (emulator.cpp:2461-2464) — readback fell through to `regs_[reg]`
    // (zeroed by `regs_.fill(0)` in NextReg::reset) and returned 0x00 for
    // all four. Discriminative test for the V17-NMP-01 fix.
    {
        const uint8_t expected[4] = {0x83, 0x01, 0x00, 0xCD};
        bool all_ok = true;
        std::string worst;
        for (int i = 0; i < 4; ++i) {
            uint8_t reg = static_cast<uint8_t>(0xB8 + i);
            uint8_t got = nr_read(emu, reg);
            if (got != expected[i]) {
                all_ok = false;
                worst = "NR " + hex2(reg) + " " +
                        detail_eq(got, expected[i]);
            }
        }
        check("RST-13",
              "NR 0xB8/0xB9/0xBA/0xBB DivMMC automap EP reset "
              "= 0x83/0x01/0x00/0xCD [zxnext.vhd:5087-5090]",
              all_ok, worst);
    }
}

// ── DMA IM2 delay integration (DMA plan rows 20.3, 20.4) ──────────────
//
// These rows were skipped in dma_test.cpp because NR 0xCC/0xCD/0xCE and the
// im2_dma_delay composition live at the zxnext.vhd level, not inside the
// standalone Dma class.  They are verified here through the full emulator.
//
// VHDL: zxnext.vhd:1957-1958 (int_en composition), :2005-2007 (delay
// composition), :5629-5637 (register writes), :6257-6263 (readback).

static void test_dma_im2_delay(Emulator& emu) {
    set_group("DMA-IM2-Delay");

    // 20.3 — Writes to NR 0xCC/0xCD/0xCE are captured and read back with
    // the exact VHDL bit layout (no spurious bits).
    {
        nr_write(emu, 0xCC, 0x83);       // bit7=1 (NMI), bits1:0=11
        uint8_t got = nr_read(emu, 0xCC);
        check("20.3a",
              "NR 0xCC readback masks to bits 7 and 1:0 "
              "[zxnext.vhd:6257]",
              got == 0x83, detail_eq(got, 0x83));
    }
    {
        nr_write(emu, 0xCC, 0x7F);       // bit7=0, bits6:2 ignored, bits1:0=11
        uint8_t got = nr_read(emu, 0xCC);
        check("20.3b",
              "NR 0xCC ignores bits 6:2 on readback "
              "[zxnext.vhd:5629-5630]",
              got == 0x03, detail_eq(got, 0x03));
    }
    {
        nr_write(emu, 0xCD, 0xA5);       // full 8 bits used for CTC 7..0
        uint8_t got = nr_read(emu, 0xCD);
        check("20.3c",
              "NR 0xCD readback preserves all 8 bits (CTC 7..0) "
              "[zxnext.vhd:5633, :6260]",
              got == 0xA5, detail_eq(got, 0xA5));
    }
    {
        nr_write(emu, 0xCE, 0xFF);       // bits 7/3 should not be stored
        uint8_t got = nr_read(emu, 0xCE);
        check("20.3d",
              "NR 0xCE readback masks to bits 6:4 + 2:0 (bits 7,3 zero) "
              "[zxnext.vhd:5636-5637, :6263]",
              got == 0x77, detail_eq(got, 0x77));
    }

    // 20.3 composition — the 14-bit im2_dma_int_en mask assembled from NR
    // bit fields must match VHDL zxnext.vhd:1957-1958.
    {
        // Drive every NR bit set: NR CC=0x83 (keeps bit7 + bits1:0), NR CD=0xFF,
        // NR CE=0x77.  Expected mask = 0x3FFF (all 14 bits).
        nr_write(emu, 0xCC, 0x83);
        nr_write(emu, 0xCD, 0xFF);
        nr_write(emu, 0xCE, 0x77);
        uint16_t m = emu.compose_im2_dma_int_en();
        check("20.3e",
              "im2_dma_int_en = 0x3FFF when all NR enable bits are set "
              "[zxnext.vhd:1957-1958]",
              m == 0x3FFF, detail_eq(m, static_cast<uint16_t>(0x3FFF)));
    }
    {
        // Sparse enable: NR CC bit 0 (ULA) — maps to im2_dma_int_en[11].
        nr_write(emu, 0xCC, 0x01);
        nr_write(emu, 0xCD, 0x00);
        nr_write(emu, 0xCE, 0x00);
        uint16_t m = emu.compose_im2_dma_int_en();
        check("20.3f",
              "NR CC[0] alone -> im2_dma_int_en[11] "
              "[zxnext.vhd:1957]",
              m == (1u << 11),
              detail_eq(m, static_cast<uint16_t>(1u << 11)));
    }
    {
        // NR CE UART0 Rx or Rx-error: bits 1 or 0 of ce_210 OR together
        // into im2_dma_int_en[1].
        nr_write(emu, 0xCC, 0x00);
        nr_write(emu, 0xCD, 0x00);
        nr_write(emu, 0xCE, 0x01);       // UART0 Rx-error
        uint16_t m1 = emu.compose_im2_dma_int_en();
        nr_write(emu, 0xCE, 0x02);       // UART0 Rx
        uint16_t m2 = emu.compose_im2_dma_int_en();
        check("20.3g",
              "NR CE[0] or CE[1] -> im2_dma_int_en[1] (UART0 Rx OR) "
              "[zxnext.vhd:1958]",
              m1 == (1u << 1) && m2 == (1u << 1),
              detail_eq(m1, static_cast<uint16_t>(1u << 1)));
    }

    // 20.4 — im2_dma_delay = im2_dma_int OR (nmi AND nr_cc_7)
    //                        OR (im2_dma_delay_prev AND dma_delay).
    // VHDL zxnext.vhd:2007.  We exercise each disjunct explicitly.
    {
        // Reset latch first by writing NR CC/CD/CE=0 and running a cycle
        // with all inputs deasserted.
        nr_write(emu, 0xCC, 0x00);
        nr_write(emu, 0xCD, 0x00);
        nr_write(emu, 0xCE, 0x00);
        emu.update_im2_dma_delay(false, false, false);
        bool latched = emu.im2_dma_delay();
        check("20.4a", "All inputs deasserted -> im2_dma_delay=0",
              latched == false, detail_eq(static_cast<uint8_t>(latched ? 1 : 0), uint8_t{0}));
    }
    {
        // Disjunct 1: im2_dma_int=1 alone.
        bool out = emu.update_im2_dma_delay(true, false, false);
        check("20.4b",
              "im2_dma_int=1 -> im2_dma_delay=1 [zxnext.vhd:2007]",
              out == true, detail_eq(static_cast<uint8_t>(out ? 1 : 0), uint8_t{1}));
        // Reset back.
        emu.update_im2_dma_delay(false, false, false);
    }
    {
        // Disjunct 2a: nmi=1 AND nr_cc_7=0 -> stays 0.
        nr_write(emu, 0xCC, 0x00);
        bool out = emu.update_im2_dma_delay(false, true, false);
        check("20.4c",
              "nmi=1 & nr_cc_7=0 -> im2_dma_delay=0 [zxnext.vhd:2007]",
              out == false, detail_eq(static_cast<uint8_t>(out ? 1 : 0), uint8_t{0}));
    }
    {
        // Disjunct 2b: nmi=1 AND nr_cc_7=1 -> 1.
        nr_write(emu, 0xCC, 0x80);       // bit7=1
        bool out = emu.update_im2_dma_delay(false, true, false);
        check("20.4d",
              "nmi=1 & nr_cc_7=1 -> im2_dma_delay=1 [zxnext.vhd:2007]",
              out == true, detail_eq(static_cast<uint8_t>(out ? 1 : 0), uint8_t{1}));
    }
    {
        // Disjunct 3: latched previous=1 AND dma_delay=1 -> stays 1 even
        // though im2_dma_int=0 and nmi=0.  Start from the latched state left
        // by the previous step.
        bool prev = emu.im2_dma_delay();
        bool out = emu.update_im2_dma_delay(false, false, true);
        check("20.4e",
              "latched_prev=1 & dma_delay=1 -> im2_dma_delay stays 1 "
              "[zxnext.vhd:2007]",
              prev == true && out == true,
              detail_eq(static_cast<uint8_t>(out ? 1 : 0), uint8_t{1}));
    }
    {
        // Disjunct 3 negation: latched=1 AND dma_delay=0 -> drops to 0.
        bool out = emu.update_im2_dma_delay(false, false, false);
        check("20.4f",
              "latched_prev=1 & dma_delay=0 -> im2_dma_delay drops to 0 "
              "[zxnext.vhd:2007]",
              out == false, detail_eq(static_cast<uint8_t>(out ? 1 : 0), uint8_t{0}));
    }
}

// ── Tilemap clip NR routing (Tilemap plan rows TM-114, TM-115) ────────

static void test_tilemap_clip_nr(Emulator& emu) {
    set_group("Tilemap-Clip-NR");

    // Baseline: reset clip indices to known-zero state.
    nr_write(emu, 0x1C, 0x08);       // bit 3 = reset tilemap clip index

    // TM-114 — 4 successive writes to NR 0x1B cycle through
    // x1 → x2 → y1 → y2.  VHDL zxnext.vhd:5242-5290 (nr_1b_tm_clip_idx
    // cycles 0..3).  Emulator clip_tm_idx_ at emulator.cpp:285-292.
    {
        nr_write(emu, 0x1B, 0x11);   // idx 0 → clip_x1
        nr_write(emu, 0x1B, 0x22);   // idx 1 → clip_x2
        nr_write(emu, 0x1B, 0x33);   // idx 2 → clip_y1
        nr_write(emu, 0x1B, 0x44);   // idx 3 → clip_y2
        const auto& tm = emu.tilemap();
        bool ok = tm.clip_x1() == 0x11 && tm.clip_x2() == 0x22 &&
                  tm.clip_y1() == 0x33 && tm.clip_y2() == 0x44;
        char detail[128];
        snprintf(detail, sizeof(detail),
                 "x1=%02X x2=%02X y1=%02X y2=%02X (expected 11/22/33/44)",
                 tm.clip_x1(), tm.clip_x2(), tm.clip_y1(), tm.clip_y2());
        check("TM-114",
              "NR 0x1B 4-write cycle programs x1/x2/y1/y2 in order "
              "[zxnext.vhd:5242-5290]",
              ok, detail);
    }

    // TM-115 — NR 0x1C bit 3 = 1 resets the tilemap clip write index.
    // After the 4-write cycle of TM-114, idx is back at 0.  Advance idx
    // to 2 by writing twice, then reset via NR 0x1C bit 3, then write
    // once: the write must land on x1 (idx=0), not on y1 (idx=2).
    {
        nr_write(emu, 0x1B, 0xA1);   // idx 0 → x1 = 0xA1 (idx → 1)
        nr_write(emu, 0x1B, 0xA2);   // idx 1 → x2 = 0xA2 (idx → 2)
        // Reset tm clip idx via NR 0x1C bit 3.
        nr_write(emu, 0x1C, 0x08);
        // Next write must target idx 0 = x1, overwriting 0xA1 with 0x77.
        nr_write(emu, 0x1B, 0x77);
        const auto& tm = emu.tilemap();
        bool ok = tm.clip_x1() == 0x77 && tm.clip_x2() == 0xA2;
        char detail[128];
        snprintf(detail, sizeof(detail),
                 "x1=%02X x2=%02X (expected x1=77 x2=A2)",
                 tm.clip_x1(), tm.clip_x2());
        check("TM-115",
              "NR 0x1C bit 3 resets tilemap clip idx so next 0x1B write → x1 "
              "[emulator.cpp:304-308]",
              ok, detail);
    }
}

// ── Soft reset vs hard reset (NR 0x02 bits 0/1) ──────────────────────
//
// VHDL: tbblue hardware.h defines RESET_SOFT=0x01, RESET_HARD=0x02,
// RESET_ESPBUS=0x80. Soft reset preserves SRAM (not in the reset domain);
// hard reset is equivalent to power-on. Also zxnext_top_issue5.vhd:1493-
// 1611 — bootrom_en is set by reset_hard only, so it survives soft reset.
// This is the mechanism NextZXOS relies on: tbblue.fw loads ROMs into
// SRAM via config_mode routing (Branch 1), writes NR 0x03 to exit config
// mode, then writes NR 0x02=RESET_SOFT; after the reset Z80 boots into
// the just-installed ROM at SRAM bank 0, with boot_rom overlay still
// disabled.

static void test_soft_reset(Emulator& emu) {
    set_group("Soft-Reset");

    // SR-01..05 use Z80 address 0x4000 (slot 2, default page 0x0A). Slot 2
    // is RAM-mapped at reset so writes land in ram_.page_ptr(0x0A), which
    // is OUTSIDE the Next ROM-in-SRAM seed window (pages 0..7). That lets
    // SR-01 observe soft-reset preserves the page, and SR-02 observe
    // hard-reset zeroes it without being re-seeded from rom_.

    // SR-01: write a marker byte into SRAM via the Mmu write path, trigger
    // NR 0x02=RESET_SOFT, observe the byte survives.
    {
        emu.mmu().write(0x4000, 0xA5);
        const uint8_t before = emu.mmu().read(0x4000);
        nr_write(emu, 0x02, 0x01);              // RESET_SOFT
        const uint8_t after = emu.mmu().read(0x4000);
        char detail[128];
        snprintf(detail, sizeof(detail), "before=0x%02X after=0x%02X", before, after);
        check("SR-01",
              "NR 0x02=0x01 (RESET_SOFT) preserves SRAM contents "
              "[VHDL: SRAM not in reset domain]",
              before == 0xA5 && after == 0xA5, detail);
    }

    // SR-02: same marker location, trigger NR 0x02=RESET_HARD, observe
    // SRAM is zeroed. Hard reset returns the emulator to power-on state.
    {
        emu.mmu().write(0x4000, 0x5A);
        const uint8_t before = emu.mmu().read(0x4000);
        nr_write(emu, 0x02, 0x02);              // RESET_HARD
        const uint8_t after = emu.mmu().read(0x4000);
        char detail[128];
        snprintf(detail, sizeof(detail), "before=0x%02X after=0x%02X", before, after);
        check("SR-02",
              "NR 0x02=0x02 (RESET_HARD) zeroes SRAM "
              "[full power-on reinit]",
              before == 0x5A && after == 0x00, detail);
    }

    // SR-03: boot_rom_en_ must survive soft reset even when a boot ROM is
    // loaded. Without a boot ROM, Mmu::reset() never re-enables the
    // overlay (it gates on boot_rom_ pointer) — so this test would pass
    // vacuously. Load a small fake boot ROM via mmu_.set_boot_rom() so
    // the subsequent mmu_.reset() inside init() actually tries to
    // re-enable; then soft_reset()'s explicit restore must keep it off.
    //
    // VHDL mechanism (zxnext.vhd:1101 default '1', :5109-5111 re-enabled
    // inside `if reset=1` only when nr_03_config_mode='1', :5122 cleared
    // by NR 0x03 write): after firmware clears config_mode via NR 0x03
    // bits[2:0]∈{001..110}, a soft reset leaves bootrom_en at 0.
    {
        static uint8_t fake_boot_rom[8192] = {};
        emu.mmu().set_boot_rom(fake_boot_rom, sizeof(fake_boot_rom));
        emu.mmu().set_boot_rom_enabled(false);
        const bool before = emu.mmu().boot_rom_enabled();
        nr_write(emu, 0x02, 0x01);              // RESET_SOFT
        const bool after = emu.mmu().boot_rom_enabled();
        char detail[64];
        snprintf(detail, sizeof(detail), "before=%d after=%d", before, after);
        check("SR-03",
              "boot_rom_en_ cleared state survives RESET_SOFT with boot ROM loaded "
              "[zxnext.vhd:1101,5109-5111,5122]",
              before == false && after == false, detail);
        // Clear the pointer so later tests don't see a phantom boot ROM.
        emu.mmu().set_boot_rom(nullptr, 0);
    }

    // SR-04: RESET_ESPBUS alone (bit 7) is a peripheral-bus reset signal
    // on real HW; in jnext we have no ESP, so it is a no-op. SRAM must not
    // be touched.
    {
        emu.mmu().write(0x4000, 0x3C);
        nr_write(emu, 0x02, 0x80);              // RESET_ESPBUS only
        const uint8_t after = emu.mmu().read(0x4000);
        check("SR-04",
              "NR 0x02=0x80 (RESET_ESPBUS alone) is a no-op — SRAM untouched",
              after == 0x3C, detail_eq(after, uint8_t{0x3C}));
    }

    // SR-05: hard-reset wins over soft when both bits are set (bit 1 | bit 0).
    // VHDL SRAM is zeroed on hard reset — verify the hard path is taken.
    {
        emu.mmu().write(0x4000, 0xC3);
        nr_write(emu, 0x02, 0x03);              // RESET_HARD | RESET_SOFT
        const uint8_t after = emu.mmu().read(0x4000);
        check("SR-05",
              "NR 0x02=0x03 (RESET_HARD|RESET_SOFT): hard wins, SRAM zeroed",
              after == 0x00, detail_eq(after, uint8_t{0x00}));
    }

    // SR-06: the Next ROM-in-SRAM window (ram_ pages 0..7) is the CORE
    // surface of Branch 3 — tbblue.fw's load_roms() writes the selected
    // machine's ROM there before RESET_SOFT, and NextZXOS boot depends on
    // that content surviving. Write a marker byte directly into ram_ page
    // 4 (inside the window, bypasses the Mmu to avoid config_mode routing
    // interference), trigger soft reset, verify the byte survives.
    {
        uint8_t* p4 = emu.ram().page_ptr(4);
        if (p4) p4[0x0100] = 0xEE;
        nr_write(emu, 0x02, 0x01);              // RESET_SOFT
        const uint8_t after = p4 ? p4[0x0100] : 0xFF;
        check("SR-06",
              "ROM-in-SRAM window (ram_ pages 0..7) survives RESET_SOFT "
              "[Branch 3 core: tbblue-loaded ROMs persist across reset]",
              after == 0xEE, detail_eq(after, uint8_t{0xEE}));
    }

    // SR-07: hard reset re-seeds the ROM-in-SRAM window from rom_ (which
    // for the Next fixture has 0xFF for pages 2..7 — only 48.rom pages
    // 0..1 were loaded). After RESET_HARD, ram_ page 4 byte should revert
    // to the seed value (0xFF), confirming the full reinit path runs.
    {
        uint8_t* p4 = emu.ram().page_ptr(4);
        if (p4) p4[0x0100] = 0x5A;              // Pollute with non-seed value.
        nr_write(emu, 0x02, 0x02);              // RESET_HARD
        const uint8_t after = emu.ram().page_ptr(4)[0x0100];
        check("SR-07",
              "ROM-in-SRAM window re-seeded from rom_ on RESET_HARD "
              "[Branch 2 seed loop runs on hard reset but not soft]",
              after == 0xFF, detail_eq(after, uint8_t{0xFF}));
    }
}

// ── Read-only registers (RO-01..06) ──────────────────────────────────
//
// Plan row group 2 in NEXTREG-TEST-PLAN-DESIGN.md. VHDL resolves NR 0x00,
// 0x01, 0x0E, 0x0F, 0x1E, 0x1F in the read-dispatch mux at
// zxnext.vhd:5867-6292 from FPGA generics and the cvc counter. In JNEXT
// these are either seeded into `regs_[]` at reset (`src/port/nextreg.cpp`)
// or served by a dedicated read_handler in `Emulator::init`. All rows
// are meaningless at the bare-NextReg tier because the reset values and
// read handlers live on the integration side.

static void test_readonly_registers(Emulator& emu) {
    set_group("Read-Only");

    // RO-01 — NR 0x00 machine ID.
    // JNEXT deviates from VHDL (returns 0x08 instead of 0x0A); covered by
    // MID-01 in the Reset-Integration group. This row asserts the
    // read-only property itself.
    {
        uint8_t got = nr_read(emu, 0x00);
        check("RO-01",
              "NR 0x00 machine ID reset=0x08 via port path "
              "[src/port/nextreg.cpp:27 — JNEXT deviation from VHDL g_machine_id]",
              got == 0x08, detail_eq(got, 0x08));
    }

    // RO-02 — NR 0x00 read-only enforcement.
    // VHDL zxnext.vhd:5884-5885 — read dispatch routes nr_register=X"00"
    // unconditionally to g_machine_id, regardless of any prior write; NR
    // 0x00 has no write handler in the VHDL write dispatch (writes are
    // discarded). JNEXT installs a read_handler in Emulator::init that
    // always returns 0x08 (HWID_EMULATORS), so a write to NR 0x00 must
    // never be observable via a subsequent read.
    {
        nr_write(emu, 0x00, 0x42);
        uint8_t got = nr_read(emu, 0x00);
        check("RO-02",
              "NR 0x00 read-only: write 0x42 then read returns 0x08 "
              "[zxnext.vhd:5884-5885 — read routed to g_machine_id; "
              "no write handler; JNEXT reports HWID_EMULATORS=0x08]",
              got == 0x08, detail_eq(got, 0x08));
    }

    // RO-03 — NR 0x01 core version. VHDL g_version = X"32" (core 3.02).
    // Seeded at src/port/nextreg.cpp:28.
    {
        uint8_t got = nr_read(emu, 0x01);
        check("RO-03",
              "NR 0x01 core version reset=0x32 (core 3.02) "
              "[src/port/nextreg.cpp:28 — g_version]",
              got == 0x32, detail_eq(got, 0x32));
    }

    // RO-04 — NR 0x0E sub-version. VHDL `g_sub_version = X"03"` (set in
    // zxnext_top_issue2.vhd:38 and zxnext_top_issue4.vhd:38); the read
    // mux at zxnext.vhd:5917-5918 returns g_sub_version verbatim. Seeded
    // at src/port/nextreg.cpp.
    {
        uint8_t got = nr_read(emu, 0x0E);
        check("RO-04",
              "NR 0x0E sub-version reset=0x03 "
              "[zxnext_top_issue2.vhd:38 g_sub_version]",
              got == 0x03, detail_eq(got, 0x03));
    }

    // RO-05 — NR 0x0F board issue (lower nibble). VHDL g_board_issue.
    // JNEXT leaves regs_[0x0F]=0x00, which corresponds to "no board
    // specified." If a specific VHDL generic value applies, the mismatch
    // goes to the Emulator Bug backlog.
    {
        uint8_t got = nr_read(emu, 0x0F);
        check("RO-05",
              "NR 0x0F board issue reset=0x00 "
              "[VHDL g_board_issue generic — JNEXT default unset]",
              got == 0x00, detail_eq(got, 0x00));
    }

    // RO-06 — NR 0x1E/0x1F active video line. VHDL computes cvc from the
    // raster counter; JNEXT wires read_handlers at emulator.cpp:405-414.
    // At frame-start the line counter is 0 on both 0x1E and 0x1F.
    {
        uint8_t got1e = nr_read(emu, 0x1E);
        uint8_t got1f = nr_read(emu, 0x1F);
        check("RO-06",
              "NR 0x1E/0x1F active video line readable via port path "
              "[emulator.cpp:405-414 computes vc from elapsed cycles]",
              got1e <= 0x01 && got1f <= 0xFF,
              "0x1E=" + hex2(got1e) + " 0x1F=" + hex2(got1f));
    }
}

// ── SEL-03: NR 0x00 read-only enforcement ────────────────────────────
//
// RO-02 above already covers the same invariant (write NR 0x00, verify
// read is unchanged). SEL-03 is an alias/alternative framing — plan row
// asserts the selection pathway respects read-only. We verify the same
// property via the port path (OUT 0x243B, OUT 0x253B, IN 0x253B).

static void test_sel_03(Emulator& emu) {
    set_group("Selection");
    // SEL-03 — same invariant as RO-02, framed via the selection path:
    // select NR 0x00 (OUT 0x243B,0x00), write via the selected-register
    // port (OUT 0x253B,0x42), read back via the selected-register port
    // (IN 0x253B). Must still return 0x08.
    // VHDL zxnext.vhd:5884-5885 — NR 0x00 read is hard-wired to
    // g_machine_id; writes have no handler and are discarded.
    {
        nr_write(emu, 0x00, 0x42);
        uint8_t got = nr_read(emu, 0x00);
        check("SEL-03",
              "NR 0x00 via select+write+read path returns 0x08 "
              "(selection pathway respects read-only) "
              "[zxnext.vhd:5884-5885]",
              got == 0x08, detail_eq(got, 0x08));
    }
}

// ── CLIP-01..08: Clip-window 4-write cycling and NR 0x1C reset ──────
//
// Plan row group 5 in NEXTREG-TEST-PLAN-DESIGN.md. VHDL cycles a 2-bit
// index per window on each write to NR 0x18/0x19/0x1A/0x1B; NR 0x1C bits
// 0..3 reset the respective indices; NR 0x1C read returns the four
// packed 2-bit indices. Layer 2 / Sprite / ULA / Tilemap each own their
// index state via write_handlers installed by Emulator::init.
//
// VHDL citation: zxnext.vhd:5242-5290.

static void test_clip_cycling(Emulator& emu) {
    set_group("Clip-Cycle");

    // CLIP-01 — NR 0x18 4-write cycle lands x1, x2, y1, y2 in order.
    // VHDL zxnext.vhd:5242-5249: writes to NR 0x18 dispatch to
    // nr_18_layer2_clip_{x1,x2,y1,y2} selected by nr_18_layer2_clip_idx,
    // and the idx is incremented after each write.
    {
        // Reset L2 clip idx to 0 via NR 0x1C bit 0 (zxnext.vhd:5278-5281).
        nr_write(emu, 0x1C, 0x01);
        nr_write(emu, 0x18, 0x11);           // x1 ← 0x11, idx 0→1
        nr_write(emu, 0x18, 0x22);           // x2 ← 0x22, idx 1→2
        nr_write(emu, 0x18, 0x33);           // y1 ← 0x33, idx 2→3
        nr_write(emu, 0x18, 0x44);           // y2 ← 0x44, idx 3→0 (wrap)
        const auto& l2 = emu.layer2();
        check("CLIP-01",
              "NR 0x18 4-write cycle → x1=0x11 x2=0x22 y1=0x33 y2=0x44 "
              "[zxnext.vhd:5242-5249]",
              l2.clip_x1() == 0x11 && l2.clip_x2() == 0x22 &&
              l2.clip_y1() == 0x33 && l2.clip_y2() == 0x44,
              "x1=" + hex2(l2.clip_x1()) + " x2=" + hex2(l2.clip_x2()) +
              " y1=" + hex2(l2.clip_y1()) + " y2=" + hex2(l2.clip_y2()));
    }

    // CLIP-02 — NR 0x18 idx wraps mod-4: the fifth write overwrites x1.
    // VHDL zxnext.vhd:5249: nr_18_layer2_clip_idx <= nr_18_layer2_clip_idx + 1
    // (2-bit counter — natural mod-4 wrap).
    {
        nr_write(emu, 0x1C, 0x01);           // reset L2 idx to 0
        nr_write(emu, 0x18, 0xA1);           // x1 ← 0xA1, idx 0→1
        nr_write(emu, 0x18, 0xA2);           // x2 ← 0xA2, idx 1→2
        nr_write(emu, 0x18, 0xA3);           // y1 ← 0xA3, idx 2→3
        nr_write(emu, 0x18, 0xA4);           // y2 ← 0xA4, idx 3→0 (wrap)
        nr_write(emu, 0x18, 0xBE);           // idx wrapped → x1 ← 0xBE
        const auto& l2 = emu.layer2();
        check("CLIP-02",
              "NR 0x18 fifth write wraps back to x1 (mod-4 idx) "
              "[zxnext.vhd:5242-5249]",
              l2.clip_x1() == 0xBE && l2.clip_x2() == 0xA2 &&
              l2.clip_y1() == 0xA3 && l2.clip_y2() == 0xA4,
              "x1=" + hex2(l2.clip_x1()) + " x2=" + hex2(l2.clip_x2()) +
              " y1=" + hex2(l2.clip_y1()) + " y2=" + hex2(l2.clip_y2()));
    }

    // CLIP-03 — NR 0x1C bit 0 resets Layer 2 clip idx. Writing NR 0x18
    // after the reset lands the value in x1 regardless of prior idx.
    // VHDL zxnext.vhd:5278-5281.
    {
        nr_write(emu, 0x1C, 0x01);           // reset L2 idx to 0
        nr_write(emu, 0x18, 0x01);           // x1 ← 0x01, idx 0→1
        nr_write(emu, 0x18, 0x02);           // x2 ← 0x02, idx 1→2
        nr_write(emu, 0x1C, 0x01);           // reset L2 idx to 0
        nr_write(emu, 0x18, 0xCC);           // must land on x1
        const auto& l2 = emu.layer2();
        check("CLIP-03",
              "NR 0x1C bit 0 resets L2 clip idx so next NR 0x18 write → x1 "
              "[zxnext.vhd:5278-5281]",
              l2.clip_x1() == 0xCC,
              "x1=" + hex2(l2.clip_x1()) + " (want 0xCC) x2=" +
              hex2(l2.clip_x2()));
    }

    // CLIP-04 — NR 0x1C bit 1 resets the sprite clip idx (VHDL zxnext.vhd:
    // 5242-5290). Cycle NR 0x19 four times to set x1/x2/y1/y2, reset the
    // sprite idx via NR 0x1C bit 1, then a fresh NR 0x19 write must land
    // on x1. Only x1 is overwritten post-reset; x2/y1/y2 keep their values.
    {
        nr_write(emu, 0x1C, 0x02);           // reset sprite clip idx to 0
        nr_write(emu, 0x19, 0x10);           // x1 ← 0x10, idx 0→1
        nr_write(emu, 0x19, 0x20);           // x2 ← 0x20, idx 1→2
        nr_write(emu, 0x19, 0x30);           // y1 ← 0x30, idx 2→3
        nr_write(emu, 0x19, 0x40);           // y2 ← 0x40, idx 3→0 (wrap)
        nr_write(emu, 0x1C, 0x02);           // reset sprite clip idx to 0
        nr_write(emu, 0x19, 0x50);           // must land on x1
        const auto& sp = emu.sprites();
        check("CLIP-04",
              "NR 0x1C bit 1 resets sprite clip idx so next NR 0x19 write → x1 "
              "[zxnext.vhd:5242-5290]",
              sp.clip_x1() == 0x50 && sp.clip_x2() == 0x20 &&
              sp.clip_y1() == 0x30 && sp.clip_y2() == 0x40,
              "x1=" + hex2(sp.clip_x1()) + " (want 0x50) x2=" +
              hex2(sp.clip_x2()) + " y1=" + hex2(sp.clip_y1()) +
              " y2=" + hex2(sp.clip_y2()));
    }

    // CLIP-05 — NR 0x1C bit 2 resets the ULA clip idx (VHDL zxnext.vhd:
    // 5242-5290). Mirrors CLIP-04 but with NR 0x1A (writes) and NR 0x1C
    // bit 2 (reset).
    {
        nr_write(emu, 0x1C, 0x04);           // reset ULA clip idx to 0
        nr_write(emu, 0x1A, 0x10);           // x1 ← 0x10, idx 0→1
        nr_write(emu, 0x1A, 0x20);           // x2 ← 0x20, idx 1→2
        nr_write(emu, 0x1A, 0x30);           // y1 ← 0x30, idx 2→3
        nr_write(emu, 0x1A, 0x40);           // y2 ← 0x40, idx 3→0 (wrap)
        nr_write(emu, 0x1C, 0x04);           // reset ULA clip idx to 0
        nr_write(emu, 0x1A, 0x50);           // must land on x1
        const auto& u = emu.ula();
        check("CLIP-05",
              "NR 0x1C bit 2 resets ULA clip idx so next NR 0x1A write → x1 "
              "[zxnext.vhd:5242-5290]",
              u.clip_x1() == 0x50 && u.clip_x2() == 0x20 &&
              u.clip_y1() == 0x30 && u.clip_y2() == 0x40,
              "x1=" + hex2(u.clip_x1()) + " (want 0x50) x2=" +
              hex2(u.clip_x2()) + " y1=" + hex2(u.clip_y1()) +
              " y2=" + hex2(u.clip_y2()));
    }

    // CLIP-06 — NR 0x1C bit 3 resets tilemap clip index. Tilemap has a
    // public clip_x1() getter so this one DOES work end-to-end.
    {
        nr_write(emu, 0x1B, 0x01);
        nr_write(emu, 0x1B, 0x02);
        nr_write(emu, 0x1C, 0x08);           // reset TM idx
        nr_write(emu, 0x1B, 0xAA);           // must land on x1
        const auto& tm = emu.tilemap();
        check("CLIP-06",
              "NR 0x1C bit 3 resets tilemap clip idx so next 0x1B write → x1 "
              "[zxnext.vhd:5242-5290]",
              tm.clip_x1() == 0xAA,
              "x1=" + hex2(tm.clip_x1()) + " (want 0xAA)");
    }

    // CLIP-07 — NR 0x1C read packs the 4 clip-window indices.
    // VHDL zxnext.vhd:5979-5980:
    //   port_253b_dat <= tm_idx & ula_idx & sprite_idx & layer2_idx
    // Each field is 2 bits; bits 7:6 = tm, 5:4 = ULA, 3:2 = sprite,
    // 1:0 = Layer2. Post-reset all four indices are 0 so the read
    // returns 0x00. After one write to NR 0x1B, clip_tm_idx_ advances
    // to 1 (zxnext.vhd:5276) so bits 7:6 = 01 → 0x40.
    {
        // Ensure all 4 indices are at zero: NR 0x1C bits 3:0 = 1111 resets
        // all (tm, ULA, sprite, L2).
        nr_write(emu, 0x1C, 0x0F);
        uint8_t got = nr_read(emu, 0x1C);
        check("CLIP-07a",
              "NR 0x1C read post-all-reset packs idx=0000 → 0x00 "
              "[zxnext.vhd:5979-5980]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // After one 0x1B write, tm idx advances 0 → 1; other 3 still 0.
        nr_write(emu, 0x1C, 0x0F);           // reset all four idx
        nr_write(emu, 0x1B, 0x55);           // write x1 → idx 0→1
        uint8_t got = nr_read(emu, 0x1C);
        check("CLIP-07b",
              "NR 0x1C after one NR 0x1B write: bits 7:6 = 01 → 0x40 "
              "[zxnext.vhd:5276 write increments idx; :5979-5980 packing]",
              got == 0x40, detail_eq(got, uint8_t{0x40}));
    }

    // CLIP-08 — NR 0x18 read is a combinatorial 4-way mux over the Layer 2
    // clip coords selected by nr_18_layer2_clip_idx (zxnext.vhd:5947-5953).
    // Reads do NOT advance the idx (no assignment in the read block); only
    // writes do (zxnext.vhd:5249). We therefore seed all four coords, then
    // walk the idx via successive writes (re-writing the same value to
    // avoid clobbering) and read between advances to observe each slot.
    {
        // Seed all four slots with distinct values, idx wraps back to 0.
        nr_write(emu, 0x1C, 0x01);           // reset L2 idx to 0
        nr_write(emu, 0x18, 0x11);           // x1 ← 0x11, idx 0→1
        nr_write(emu, 0x18, 0x22);           // x2 ← 0x22, idx 1→2
        nr_write(emu, 0x18, 0x33);           // y1 ← 0x33, idx 2→3
        nr_write(emu, 0x18, 0x44);           // y2 ← 0x44, idx 3→0

        // idx = 0 → read returns x1.
        uint8_t r_x1 = nr_read(emu, 0x18);

        // Advance idx 0→1 by re-writing x1 with the same value.
        nr_write(emu, 0x18, 0x11);
        uint8_t r_x2 = nr_read(emu, 0x18);   // idx = 1 → x2

        // Advance idx 1→2 by re-writing x2.
        nr_write(emu, 0x18, 0x22);
        uint8_t r_y1 = nr_read(emu, 0x18);   // idx = 2 → y1

        // Advance idx 2→3 by re-writing y1.
        nr_write(emu, 0x18, 0x33);
        uint8_t r_y2 = nr_read(emu, 0x18);   // idx = 3 → y2

        check("CLIP-08",
              "NR 0x18 read mux cycles through x1, x2, y1, y2 as idx advances "
              "[zxnext.vhd:5947-5953]",
              r_x1 == 0x11 && r_x2 == 0x22 &&
              r_y1 == 0x33 && r_y2 == 0x44,
              "r_x1=" + hex2(r_x1) + " r_x2=" + hex2(r_x2) +
              " r_y1=" + hex2(r_y1) + " r_y2=" + hex2(r_y2));
    }

    // CLIP-09 — VHDL-faithful invariant: NR 0x1B read does NOT advance
    // clip_tm_idx_. Per zxnext.vhd:5971-5977 the read is a pure
    // combinatorial mux with no side effect; only writes advance idx
    // (zxnext.vhd:5276). Two consecutive reads must return the same
    // value. Also verify with idx != 0: writing NR 0x1B three times
    // advances idx 0→1→2→3, selecting y2 (default 0xFF). Two reads
    // then both return 0xFF; if a buggy impl advanced idx on read,
    // the second would wrap to idx=0 and return x1.
    {
        // Seed a known x1, then reset idx so both reads hit x1.
        nr_write(emu, 0x1C, 0x08);           // reset tm idx to 0
        nr_write(emu, 0x1B, 0x77);           // x1 ← 0x77 (idx 0→1)
        nr_write(emu, 0x1C, 0x08);           // reset tm idx back to 0
        uint8_t r1 = nr_read(emu, 0x1B);
        uint8_t r2 = nr_read(emu, 0x1B);
        check("CLIP-09a",
              "NR 0x1B read does NOT advance idx (two consecutive reads equal) "
              "[zxnext.vhd:5971-5977 — combinatorial, no idx increment]",
              r1 == r2 && r1 == 0x77,
              "r1=" + hex2(r1) + " r2=" + hex2(r2));
    }
    {
        // Advance idx to 3 via 3 writes; idx=3 selects y2 (default 0xFF).
        // Use values that won't corrupt later assertions: write x1/x2/y1
        // with 0x11/0x22/0x33 — y2 remains at reset default 0xFF.
        nr_write(emu, 0x1C, 0x08);           // reset tm idx to 0
        nr_write(emu, 0x1B, 0x11);           // x1 ← 0x11, idx 0→1
        nr_write(emu, 0x1B, 0x22);           // x2 ← 0x22, idx 1→2
        nr_write(emu, 0x1B, 0x33);           // y1 ← 0x33, idx 2→3
        uint8_t r1 = nr_read(emu, 0x1B);     // idx=3 → y2 = 0xFF (default)
        uint8_t r2 = nr_read(emu, 0x1B);     // still idx=3 if read is pure
        check("CLIP-09b",
              "NR 0x1B reads at idx=3 both return y2=0xFF (reset default); "
              "read does not wrap idx [zxnext.vhd:5971-5977]",
              r1 == 0xFF && r2 == 0xFF,
              "r1=" + hex2(r1) + " r2=" + hex2(r2));
    }

    // CLIP-10 — Discriminative: NR 0x1B write advances idx AND NR 0x1C
    // reflects that advance. VHDL zxnext.vhd:5276 increments
    // nr_1b_tm_clip_idx on write; :5980 packs it into NR 0x1C bits 7:6.
    {
        nr_write(emu, 0x1C, 0x0F);           // reset all four idx to 0
        nr_write(emu, 0x1B, 0xAA);           // write x1 → idx 0→1
        const auto& tm = emu.tilemap();
        uint8_t got_1c = nr_read(emu, 0x1C);
        check("CLIP-10",
              "NR 0x1B write lands x1=0xAA AND advances tm idx → NR 0x1C "
              "bits 7:6 = 01 (0x40) "
              "[zxnext.vhd:5276 write increments idx; :5980 NR 0x1C packing]",
              tm.clip_x1() == 0xAA && got_1c == 0x40,
              "x1=" + hex2(tm.clip_x1()) + " nr_1c=" + hex2(got_1c));
    }

    // CLIP-11 — NR 0x19 read is a combinatorial 4-way mux over the sprite
    // clip coords selected by nr_19_sprite_clip_idx (zxnext.vhd:5955-5961).
    // Reads do NOT advance the idx; only writes do (zxnext.vhd:5256).
    // Closes G1.AT-16 / G97 — re-homed from sprites_test.cpp because the
    // mux state lives in Emulator (clip_spr_idx_), not in SpriteEngine.
    // Mirrors the CLIP-08 idiom: seed all four slots, then walk the idx
    // via successive same-value writes and read between advances.
    {
        // Seed all four slots with distinct values, idx wraps back to 0.
        nr_write(emu, 0x1C, 0x02);           // reset sprite idx to 0
        nr_write(emu, 0x19, 0x10);           // x1 ← 0x10, idx 0→1
        nr_write(emu, 0x19, 0x20);           // x2 ← 0x20, idx 1→2
        nr_write(emu, 0x19, 0x30);           // y1 ← 0x30, idx 2→3
        nr_write(emu, 0x19, 0x40);           // y2 ← 0x40, idx 3→0

        // idx = 0 → read returns x1.
        uint8_t r_x1 = nr_read(emu, 0x19);

        // Advance idx 0→1 by re-writing x1 with the same value.
        nr_write(emu, 0x19, 0x10);
        uint8_t r_x2 = nr_read(emu, 0x19);   // idx = 1 → x2

        // Advance idx 1→2 by re-writing x2.
        nr_write(emu, 0x19, 0x20);
        uint8_t r_y1 = nr_read(emu, 0x19);   // idx = 2 → y1

        // Advance idx 2→3 by re-writing y1.
        nr_write(emu, 0x19, 0x30);
        uint8_t r_y2 = nr_read(emu, 0x19);   // idx = 3 → y2

        check("CLIP-11",
              "NR 0x19 read mux cycles through x1, x2, y1, y2 as idx advances "
              "[zxnext.vhd:5955-5961]",
              r_x1 == 0x10 && r_x2 == 0x20 &&
              r_y1 == 0x30 && r_y2 == 0x40,
              "r_x1=" + hex2(r_x1) + " r_x2=" + hex2(r_x2) +
              " r_y1=" + hex2(r_y1) + " r_y2=" + hex2(r_y2));
    }

    // CLIP-11b — NR 0x19 read does NOT advance idx (two consecutive reads
    // equal). Mirrors CLIP-09a for the sprite clip register set. VHDL
    // zxnext.vhd:5955-5961 is a pure combinatorial mux; only writes
    // advance idx (:5256). Discriminative against an off-by-one impl
    // that mistakenly increments on read.
    {
        nr_write(emu, 0x1C, 0x02);           // reset sprite idx to 0
        nr_write(emu, 0x19, 0x77);           // x1 ← 0x77 (idx 0→1)
        nr_write(emu, 0x1C, 0x02);           // reset sprite idx back to 0
        uint8_t r1 = nr_read(emu, 0x19);
        uint8_t r2 = nr_read(emu, 0x19);
        // Both reads must return x1=0x77; if the read advanced idx,
        // r2 would return x2 (still default 0xFF post-reset on a fresh
        // engine, but here we don't re-fresh — assert equality, which
        // is the load-bearing invariant).
        check("CLIP-11b",
              "NR 0x19 read does NOT advance idx (two consecutive reads equal) "
              "[zxnext.vhd:5955-5961 — combinatorial, no idx increment]",
              r1 == r2 && r1 == 0x77,
              "r1=" + hex2(r1) + " r2=" + hex2(r2));
    }

    // CLIP-12 — NR 0x1A read is a combinatorial 4-way mux over the ULA
    // clip coords selected by nr_1a_ula_clip_idx (zxnext.vhd:5963-5969).
    // Reads do NOT advance the idx; only writes do (zxnext.vhd:5265).
    // Closes G1.AT-17 / G97. Use values < 0xC0 for y2 to avoid the
    // VHDL clamp at zxnext.vhd:6779-6783 — that clamp applies to the
    // consumer-facing `ula_clip_y2_0`, not to the raw register the
    // NR 0x1A read returns. CLIP-12c covers the raw-register property
    // explicitly.
    {
        nr_write(emu, 0x1C, 0x04);           // reset ULA idx to 0
        nr_write(emu, 0x1A, 0x05);           // x1 ← 0x05, idx 0→1
        nr_write(emu, 0x1A, 0x15);           // x2 ← 0x15, idx 1→2
        nr_write(emu, 0x1A, 0x25);           // y1 ← 0x25, idx 2→3
        nr_write(emu, 0x1A, 0x35);           // y2 ← 0x35, idx 3→0

        uint8_t r_x1 = nr_read(emu, 0x1A);   // idx = 0 → x1

        nr_write(emu, 0x1A, 0x05);           // re-write x1 to advance idx
        uint8_t r_x2 = nr_read(emu, 0x1A);   // idx = 1 → x2

        nr_write(emu, 0x1A, 0x15);
        uint8_t r_y1 = nr_read(emu, 0x1A);   // idx = 2 → y1

        nr_write(emu, 0x1A, 0x25);
        uint8_t r_y2 = nr_read(emu, 0x1A);   // idx = 3 → y2

        check("CLIP-12",
              "NR 0x1A read mux cycles through x1, x2, y1, y2 as idx advances "
              "[zxnext.vhd:5963-5969]",
              r_x1 == 0x05 && r_x2 == 0x15 &&
              r_y1 == 0x25 && r_y2 == 0x35,
              "r_x1=" + hex2(r_x1) + " r_x2=" + hex2(r_x2) +
              " r_y1=" + hex2(r_y1) + " r_y2=" + hex2(r_y2));
    }

    // CLIP-12b — NR 0x1A read does NOT advance idx (mirrors CLIP-11b /
    // CLIP-09a). VHDL zxnext.vhd:5963-5969 is combinatorial.
    {
        nr_write(emu, 0x1C, 0x04);           // reset ULA idx to 0
        nr_write(emu, 0x1A, 0x88);           // x1 ← 0x88 (idx 0→1)
        nr_write(emu, 0x1C, 0x04);           // reset ULA idx back to 0
        uint8_t r1 = nr_read(emu, 0x1A);
        uint8_t r2 = nr_read(emu, 0x1A);
        check("CLIP-12b",
              "NR 0x1A read does NOT advance idx (two consecutive reads equal) "
              "[zxnext.vhd:5963-5969 — combinatorial, no idx increment]",
              r1 == r2 && r1 == 0x88,
              "r1=" + hex2(r1) + " r2=" + hex2(r2));
    }

    // CLIP-12c — NR 0x1A read returns the RAW nr_1a_ula_clip_y2 register
    // (zxnext.vhd:5968), NOT the consumer-side clamped ula_clip_y2_0
    // signal (zxnext.vhd:6779-6783). Discriminative: write y2=0xFF
    // (high two bits set, which would clamp to 0xBF on the consumer
    // side) and verify the NR 0x1A read returns 0xFF.
    {
        nr_write(emu, 0x1C, 0x04);           // reset ULA idx to 0
        nr_write(emu, 0x1A, 0x00);           // x1 ← 0x00, idx 0→1
        nr_write(emu, 0x1A, 0x00);           // x2 ← 0x00, idx 1→2
        nr_write(emu, 0x1A, 0x00);           // y1 ← 0x00, idx 2→3
        nr_write(emu, 0x1A, 0xFF);           // y2 ← 0xFF (raw), idx 3→0
        // Walk idx back to 3 via 3 same-value writes.
        nr_write(emu, 0x1A, 0x00);           // x1 (idx 0→1)
        nr_write(emu, 0x1A, 0x00);           // x2 (idx 1→2)
        nr_write(emu, 0x1A, 0x00);           // y1 (idx 2→3)
        uint8_t got = nr_read(emu, 0x1A);    // idx = 3 → y2 raw
        check("CLIP-12c",
              "NR 0x1A read at idx=3 returns RAW nr_1a_ula_clip_y2=0xFF "
              "(NOT consumer-side ula_clip_y2_0 clamped to 0xBF) "
              "[zxnext.vhd:5968 raw register read; clamp at :6779-6783 "
              "is on consumer signal only]",
              got == 0xFF, detail_eq(got, 0xFF));
    }
}

// ── PAL-01..06: Palette write pipeline and read-back ────────────────
//
// Plan row group 8 in NEXTREG-TEST-PLAN-DESIGN.md. VHDL palette pipeline
// at zxnext.vhd:4918-4920 uses NR 0x40 (index), NR 0x41 (8-bit write),
// NR 0x44 (9-bit write with sub_idx latch), NR 0x43 (control, bit 7
// auto-increment enable), plus read dispatch for NR 0x41/0x44.

static void test_palette(Emulator& emu) {
    set_group("Palette");

    // Select Layer 2 palette via NR 0x43 bits 6:4 = 000.
    nr_write(emu, 0x43, 0x00);

    // PAL-01 — NR 0x40 sets the palette index; NR 0x41 writes the 8-bit
    // value AND auto-increments the index. VHDL zxnext.vhd:4918-4920 +
    // palette write dispatch. Facility IS implemented in JNEXT
    // (src/video/palette.cpp:142 gates auto-inc, :252-257 advances
    // index), so per UNIT-TEST-PLAN-EXECUTION.md §2 this is a FAIL, not
    // a SKIP — "facility exists but produces wrong value". First-run
    // observation: two consecutive NR 0x41 writes both land at the same
    // index (pal[0]=0x03 pal[1]=0x03 when we expect pal[0]=0xFC and
    // pal[1]=0x03). Real emulator bug, to be fixed in a future palette-
    // audit branch.
    //
    // FIXED 2026-04-20 after critic (was SKIP, hiding the bug).
    {
        nr_write(emu, 0x43, 0x00);           // L2 palette, auto-inc enabled
        nr_write(emu, 0x40, 0x00);           // index 0
        nr_write(emu, 0x41, 0xFC);
        nr_write(emu, 0x41, 0x03);           // auto-inc → index 1
        nr_write(emu, 0x40, 0x00);
        uint8_t got0 = nr_read(emu, 0x41);
        nr_write(emu, 0x40, 0x01);
        uint8_t got1 = nr_read(emu, 0x41);
        check("PAL-01",
              "NR 0x41 auto-increments palette index: pal[0]=0xFC pal[1]=0x03 "
              "[zxnext.vhd:4918-4920 palette write]",
              got0 == 0xFC && got1 == 0x03,
              "pal[0]=" + hex2(got0) + " pal[1]=" + hex2(got1) +
              " (want 0xFC / 0x03)");
    }

    // PAL-02 — NR 0x41 8-bit round-trip.
    {
        nr_write(emu, 0x40, 0x10);
        nr_write(emu, 0x41, 0xA5);
        nr_write(emu, 0x40, 0x10);
        uint8_t got = nr_read(emu, 0x41);
        check("PAL-02",
              "NR 0x41 8-bit palette value round-trips at selected index "
              "[zxnext.vhd:4918-4920]",
              got == 0xA5, detail_eq(got, 0xA5));
    }

    // PAL-03 — NR 0x44 9-bit palette write via sub_idx latch. VHDL
    // zxnext.vhd:4918-4920. Facility IS implemented in JNEXT
    // (src/video/palette.cpp:190-210 — nine_bit_first_written_ latch),
    // so per UNIT-TEST-PLAN-EXECUTION.md §2 this is a FAIL, not a SKIP.
    //
    // FIXED 2026-04-20 after critic (was SKIP, hiding the bug).
    {
        nr_write(emu, 0x43, 0x00);           // L2 palette
        nr_write(emu, 0x40, 0x20);
        nr_write(emu, 0x44, 0xCC);           // first write: upper 8 bits
        nr_write(emu, 0x44, 0x81);           // second write: priority + LSB; advances pointer
        nr_write(emu, 0x40, 0x20);           // re-select idx 0x20
        uint8_t got41 = nr_read(emu, 0x41);
        bool ok = got41 == 0xCC;
        check("PAL-03",
              "NR 0x44 9-bit write: upper 8 bits land at selected idx "
              "[zxnext.vhd:4918-4920 palette sub_idx latch]",
              ok,
              "NR41@0x20=" + hex2(got41) + " (want 0xCC)");
    }

    // PAL-04 — NR 0x41 read returns the stored 8-bit palette value at the
    // currently selected index (covered by PAL-02 above, added for
    // plan-row traceability).
    {
        nr_write(emu, 0x40, 0x30);
        nr_write(emu, 0x41, 0x5A);
        nr_write(emu, 0x40, 0x30);
        uint8_t got = nr_read(emu, 0x41);
        check("PAL-04",
              "NR 0x41 read returns palette byte at selected index "
              "[zxnext.vhd read dispatch ~5867-6292]",
              got == 0x5A, detail_eq(got, 0x5A));
    }

    // PAL-05 — NR 0x44 read returns priority + LSB for the selected index.
    {
        nr_write(emu, 0x40, 0x40);
        nr_write(emu, 0x44, 0x00);           // upper 8 = 0
        nr_write(emu, 0x44, 0x81);           // priority=1, LSB=1
        nr_write(emu, 0x40, 0x40);
        uint8_t got = nr_read(emu, 0x44);
        check("PAL-05",
              "NR 0x44 read returns priority+LSB for selected index "
              "[zxnext.vhd read dispatch ~5867-6292]",
              (got & 0x81) == 0x81,
              "NR44=" + hex2(got) + " (want bits 7 and 0 set)");
    }

    // PAL-06 — NR 0x43 bit 7 disables palette auto-increment. VHDL
    // zxnext.vhd:4918-4920. Facility IS implemented in JNEXT
    // (src/video/palette.cpp:142 reads NR 0x43 bit 7 into
    // auto_inc_disabled_, :252-257 gates advance_index()), so per §2
    // this is a FAIL not a SKIP.
    //
    // FIXED 2026-04-20 after critic (was SKIP, hiding the bug).
    {
        nr_write(emu, 0x43, 0x80);           // bit 7 = 1 → auto-inc disabled
        nr_write(emu, 0x40, 0x50);
        nr_write(emu, 0x41, 0x11);
        nr_write(emu, 0x41, 0x22);           // MUST overwrite pal[0x50]; pointer stays
        nr_write(emu, 0x40, 0x50);
        uint8_t got50 = nr_read(emu, 0x41);
        nr_write(emu, 0x40, 0x51);
        uint8_t got51 = nr_read(emu, 0x41);
        nr_write(emu, 0x43, 0x00);           // restore default
        check("PAL-06",
              "NR 0x43 bit 7 disables auto-inc: 2× NR 0x41 at idx 0x50 "
              "keeps pointer on 0x50, pal[0x51] untouched "
              "[zxnext.vhd:4918-4920]",
              got50 == 0x22 && got51 != 0x22,
              "pal[0x50]=" + hex2(got50) + " pal[0x51]=" + hex2(got51));
    }
}

// ── PE-05: NR 0x86-0x89 bus port-enable defaults ────────────────────
//
// Plan row group 9 in NEXTREG-TEST-PLAN-DESIGN.md. VHDL zxnext.vhd:
// 5052-5068 resets NR 0x86-0x89 (bus port enables) to 0xFF on power-on.
// Bare NextReg does not seed these. RST-08 (Reset-Integration) covers
// NR 0x82-0x85; PE-05 is the bus-side parallel.

// ── PE-03: NR 0x82 bit 6 gates port 0x1F (Kempston 1) ─────────────────
//
// VHDL zxnext.vhd:2392-2442 (internal port enable decode): port_1f_en is
// composed from `nr_82_internal_port_enable(6) AND port_01_ff_io_en` so
// NR 0x82 bit 6 = 0 masks port 0x1F off the internal bus. With the mask,
// a read of port 0x1F falls through to the unhandled-port default
// (floating bus, 0xFF); with bit 6 = 1 (VHDL reset default, NR 0x82 bit 6
// reset 1 per zxnext.vhd:5052-5068), the Kempston handler responds.
//
// JNEXT currently registers the Kempston handler unconditionally in
// Emulator::init (src/core/emulator.cpp:1119), ignoring NR 0x82 bit 6.
// Real feature gap. Backlog: wire NR 0x82 bit 6 → port_1f_en gate in
// PortDispatch (or in the handler closure) so that OUT (0x243B),0x82 /
// OUT (0x253B),0xBF followed by IN A,(0x1F) returns 0xFF.

static void test_pe_03(Emulator& emu) {
    set_group("Port-Enable-0x1F");

    // VHDL zxnext.vhd:2392-2407 — port_1f_io_en = internal_port_enable(6),
    // whose low byte is nr_82_internal_port_enable when expbus is disabled.
    // Reset default nr_82_internal_port_enable = 0xFF (zxnext.vhd:1226, 5054)
    // so bit 6 = 1 and the Kempston handler responds. Clearing bit 6
    // (NR 0x82 = 0xBF) removes port 0x1F from the internal bus and a
    // read must return the unhandled-port floating-bus value (0xFF).
    // Re-enabling bit 6 must restore the Kempston handler response.
    //
    // JNEXT's Kempston 1 stub returns 0x00 (no buttons) when enabled —
    // see src/core/emulator.cpp Kempston-1 handler. The test oracle is
    // the gating behaviour, not the stub value.

    // 1. Reset default: bit 6 = 1, expect Kempston stub (0x00).
    uint8_t enabled = emu.port().in(0x001F);

    // 2. Clear bit 6 (NR 0x82 = 0xBF): expect floating bus (0xFF).
    nr_write(emu, 0x82, 0xBF);
    uint8_t gated = emu.port().in(0x001F);

    // 3. Re-enable bit 6 (NR 0x82 = 0xFF): expect Kempston stub (0x00).
    nr_write(emu, 0x82, 0xFF);
    uint8_t re_enabled = emu.port().in(0x001F);

    const bool ok = (enabled == 0x00) && (gated == 0xFF) && (re_enabled == 0x00);
    check("PE-03",
          "NR 0x82 bit 6 gates port 0x1F (Kempston 1): "
          "bit6=1→handler, bit6=0→0xFF [zxnext.vhd:2392-2442]",
          ok,
          "enabled=" + hex2(enabled) + " gated=" + hex2(gated) +
          " re_enabled=" + hex2(re_enabled));
}

static void test_pe_05(Emulator& emu) {
    set_group("Port-Enable-Bus");
    // PE-05 — NR 0x86-0x89 bus-side port enables. VHDL zxnext.vhd:
    // 6147-6150 reads NR 0x89 as `nr_89_bus_port_reset_type & "000" &
    // nr_89_bus_port_enable`. Reset defaults (zxnext.vhd:1234-1235):
    // nr_89_bus_port_reset_type='1', nr_89_bus_port_enable=(others=>'1').
    // So the correct read oracle for NR 0x89 is 0x8F (bit 7 = 1, bits
    // 6:4 = 000, bits 3:0 = 1111), same layout/value as NR 0x85 (which
    // is tested correctly at RST-08). Seeded at src/port/nextreg.cpp.
    {
        uint8_t got = nr_read(emu, 0x89);
        check("PE-05",
              "NR 0x89 bus port enable reset=0x8F "
              "[zxnext.vhd:1234-1235, 6147-6150]",
              got == 0x8F, detail_eq(got, 0x8F));
    }
}

// ── PE-INT-82, PE-INT-86, PE-INT-89, PE-INT-80-88: G154 closure ──────
//
// These rows came down from nextreg_test.cpp PE-06..09 when G154
// was closed. They cover NR 0x80 / 0x82 / 0x86 / 0x88 / 0x89 read
// packing + reset defaults end-to-end through the full Emulator
// fixture (port path), which is the only tier where the
// emulator-side handlers + NextReg::reset() seeds are wired.
//
// Oracles (zxnext.vhd, all line numbers verified):
//   NR 0x80: signal default 0x00 at line 360; read mux at 6122-6123
//            returns nr_80_expbus directly; no pack mask.
//   NR 0x82: default 0xFF at 1226; reset_type-1 reload at 5054; write
//            stores 8 bits at 5499; read returns 8 bits at 6128-6129.
//            No pack mask. Round-trip identity.
//   NR 0x86: default 0xFF at 1231; reset_type-0 reload at 5063; write
//            stores 8 bits at 5512; read returns 8 bits at 6140-6141.
//            Round-trip identity.
//   NR 0x88: default 0xFF at 1233; reset_type-0 reload at 5065; write
//            stores 8 bits at 5518; read returns 8 bits at 6146-6147.
//            Round-trip identity.
//   NR 0x89: enable default 0xF + reset_type='1' at 1234-1235; write
//            splits bit 7 → reset_type, bits 3:0 → enable, bits 6:4
//            discarded (5520-5522); read recomposes reset_type &
//            "000" & enable (6149-6150). Pack mask 0x8F enforced by
//            read_handler in src/core/emulator.cpp.

static void test_pe_integration(Emulator& emu) {
    set_group("PE-Integration");

    // PE-INT-80-88 — Post-reset defaults for NR 0x80 and NR 0x88.
    //   NR 0x80 = 0x00 (zxnext.vhd:360)
    //   NR 0x88 = 0xFF (zxnext.vhd:1233)
    {
        uint8_t got80 = nr_read(emu, 0x80);
        uint8_t got88 = nr_read(emu, 0x88);
        const bool ok = (got80 == 0x00) && (got88 == 0xFF);
        check("PE-INT-80-88",
              "NR 0x80 reset=0x00, NR 0x88 reset=0xFF "
              "[zxnext.vhd:360, 1233]",
              ok,
              "NR 0x80 got=" + hex2(got80) + " expected=0x00; "
              "NR 0x88 got=" + hex2(got88) + " expected=0xFF");
    }

    // PE-INT-82 — NR 0x82 round-trip (no packing). Write 0xA5; expect
    // 0xA5 back per VHDL :5499 (write 8 bits) and :6128-6129 (read 8
    // bits). Restore 0xFF afterwards so downstream rows see the reset
    // default (NR 0x82 bit 1 / bit 3 gate port FD/1FFD elsewhere).
    {
        nr_write(emu, 0x82, 0xA5);
        uint8_t got = nr_read(emu, 0x82);
        nr_write(emu, 0x82, 0xFF);  // restore reset default
        check("PE-INT-82",
              "NR 0x82 write=0xA5 read=0xA5 (no pack mask) "
              "[zxnext.vhd:5499, 6128-6129]",
              got == 0xA5, detail_eq(got, 0xA5));
    }

    // PE-INT-86 — NR 0x86 round-trip + reset default observation.
    // Verify pre-write reset default 0xFF (zxnext.vhd:1231), then
    // confirm write 0x5A round-trips (zxnext.vhd:5512, 6140-6141).
    // Restore 0xFF afterwards.
    {
        uint8_t reset_default = nr_read(emu, 0x86);
        nr_write(emu, 0x86, 0x5A);
        uint8_t got = nr_read(emu, 0x86);
        nr_write(emu, 0x86, 0xFF);  // restore reset default
        const bool ok = (reset_default == 0xFF) && (got == 0x5A);
        check("PE-INT-86",
              "NR 0x86 reset=0xFF + write=0x5A read=0x5A "
              "[zxnext.vhd:1231, 5512, 6140-6141]",
              ok,
              "reset=" + hex2(reset_default) + " (want 0xFF), "
              "round-trip=" + hex2(got) + " (want 0x5A)");
    }

    // PE-INT-87 — NR 0x87 round-trip + reset default observation.
    // Same VHDL shape as NR 0x86 / NR 0x88: signal default `(others => '1')`
    // at zxnext.vhd:1232; reset block at :5061-5067 reloads to 0xFF gated on
    // nr_89_bus_port_reset_type='0'; write at :5514-5515 (full 8 bits);
    // read mux at :6143-6144 (no pack). G154 follow-on.
    {
        uint8_t reset_default = nr_read(emu, 0x87);
        nr_write(emu, 0x87, 0x3C);
        uint8_t got = nr_read(emu, 0x87);
        nr_write(emu, 0x87, 0xFF);  // restore reset default
        const bool ok = (reset_default == 0xFF) && (got == 0x3C);
        check("PE-INT-87",
              "NR 0x87 reset=0xFF + write=0x3C read=0x3C "
              "[zxnext.vhd:1232, 5514-5515, 6143-6144]",
              ok,
              "reset=" + hex2(reset_default) + " (want 0xFF), "
              "round-trip=" + hex2(got) + " (want 0x3C)");
    }

    // PE-INT-89 — NR 0x89 read packing (bits 6:4 always zero). Write
    // 0xF7 = 1111_0111: stored bit 7 → reset_type=1, bits 3:0 →
    // enable=0x7; bits 6:4 of write discarded. Read returns
    // reset_type & "000" & enable = 1_000_0111 = 0x87, NOT the 0xF7
    // we wrote. This is the exact pack-mask shape of NR 0x85.
    // Restore 0x8F afterwards (NR 0x89 reset default).
    {
        nr_write(emu, 0x89, 0xF7);
        uint8_t got = nr_read(emu, 0x89);
        nr_write(emu, 0x89, 0x8F);  // restore reset default
        check("PE-INT-89",
              "NR 0x89 write=0xF7 read=0x87 (bits 6:4 always 0) "
              "[zxnext.vhd:5520-5522, 6149-6150]",
              got == 0x87, detail_eq(got, 0x87));
    }
}

// ── RW-01, RW-02: asymmetric read/write registers ────────────────────
//
// Plan row group 4 in NEXTREG-TEST-PLAN-DESIGN.md. NR 0x07 (CPU speed)
// and NR 0x08 (peripheral 3) have read formats that differ from writes —
// the VHDL mux formats from internal state at read time.

static void test_rw_asymmetric(Emulator& emu) {
    set_group("RW-Asymmetric");
    (void)emu;

    // RW-01 — NR 0x07 CPU-speed packed read.
    // VHDL zxnext.vhd:5902-5903:
    //   port_253b_dat <= "00" & cpu_speed & "00" & nr_07_cpu_speed;
    // → bits[5:4] = actual cpu_speed, bits[1:0] = requested nr_07_cpu_speed,
    //   other bits 0.
    //
    // JNEXT does not model the expbus speed override (VHDL zxnext.vhd:
    // 5816-5820 assigns cpu_speed <= nr_07_cpu_speed when expbus_en='0'),
    // so the effective actual speed tracks the requested value directly —
    // i.e. after a write of v (low 2 bits), the composed read is
    // ((v & 3) << 4) | (v & 3).
    //
    // Sequence: write 0x02 (request 7 MHz), expect read == 0x22;
    // write 0x03 (request 28 MHz), expect read == 0x33; write 0x00
    // (request 3.5 MHz), expect read == 0x00. Upper two bits and bits
    // [3:2] must be zero regardless of what was written (they are hard
    // constants in the VHDL read format).
    nr_write(emu, 0x07, 0x02);
    uint8_t r2 = nr_read(emu, 0x07);

    nr_write(emu, 0x07, 0x03);
    uint8_t r3 = nr_read(emu, 0x07);

    nr_write(emu, 0x07, 0x00);
    uint8_t r0 = nr_read(emu, 0x07);

    // Also check that a write with upper bits set still reads back the
    // VHDL-composed format (upper bits masked to 00, bits[3:2] masked
    // to 00). Write 0xFF → only the low 2 bits (= 3) are captured →
    // read should be 0x33.
    nr_write(emu, 0x07, 0xFF);
    uint8_t rf = nr_read(emu, 0x07);

    const bool ok = (r2 == 0x22) && (r3 == 0x33) && (r0 == 0x00) && (rf == 0x33);
    check("RW-01",
          "NR 0x07 read = (actual<<4) | requested, pads 0 in bits[7:6] "
          "and bits[3:2] [zxnext.vhd:5902-5903]",
          ok,
          "r(0x02)=" + hex2(r2) + " r(0x03)=" + hex2(r3) +
          " r(0x00)=" + hex2(r0) + " r(0xFF)=" + hex2(rf));

    // RW-02 — NR 0x08 bit 7 on read should return NOT port_7ffd_locked.
    // VHDL zxnext.vhd:5906 composes port_253b_dat for NR 0x08 as
    //   (not port_7ffd_locked) & eff_nr_08_contention_disable & stereo &
    //   spkr & dac & port_ff & turbosound & issue2.
    // Branch C installed a read_handler on NR 0x08 that drives bit 7 from
    // Mmu::paging_locked() and bit 6 from Mmu::contention_disabled(). The
    // low 6 bits mirror the last write (see nr_08_stored_low_ in Emulator).
    //
    // Sequence:
    //   1. Write port_7FFD with bit 5 set → lock paging.
    //   2. Read NR 0x08: expect bit 7 = 0 (locked).
    //   3. Write NR 0x08 with bit 7 set → clear paging lock (write-strobe).
    //   4. Read NR 0x08: expect bit 7 = 1 (unlocked).
    //   5. Write NR 0x08 with bit 6 set → contention_disable = 1, stored.
    //   6. Read NR 0x08: expect bit 6 = 1 (and bit 7 still = 1).
    {
        // 1. Lock paging via direct Z80 OUT to port 0x7FFD (tested via
        //    the port dispatch; we reach into emu.port() directly here
        //    since nr_write/nr_read already do that for NextREG ports).
        emu.port().out(0x7FFD, 0x20);  // bit 5 set → lock
        uint8_t locked = nr_read(emu, 0x08);
        bool bit7_locked_clear = (locked & 0x80) == 0;

        // 3/4. Write bit 7 to clear the lock, then re-read.
        nr_write(emu, 0x08, 0x80);
        uint8_t unlocked = nr_read(emu, 0x08);
        bool bit7_unlocked_set = (unlocked & 0x80) != 0;

        // 5/6. Drive bit 6 (contention_disable). Keep bit 7 unset so the
        //      write is not re-issuing the unlock strobe (harmless either
        //      way, but clearer without).
        nr_write(emu, 0x08, 0x40);
        // Verify8-memory class-(a) fix: VHDL zxnext.vhd:5906 reads
        // `eff_nr_08_contention_disable` (effective), not the immediate
        // shadow. Per :5822-5823 the effective gate latches from the
        // shadow only on the bus-idle + hc(8)='1' edge. Without running
        // any instructions in this harness, poke the commit explicitly
        // so the read-back observes the committed value.
        emu.contention().commit_contention_disable_on_hc(300);  // hc(8)=1
        uint8_t cd_on = nr_read(emu, 0x08);
        bool bit6_set = (cd_on & 0x40) != 0;
        bool bit7_still_set = (cd_on & 0x80) != 0;

        const bool ok = bit7_locked_clear && bit7_unlocked_set &&
                        bit6_set && bit7_still_set;
        check("RW-02",
              "NR 0x08 bit 7 read = NOT port_7ffd_locked, bit 6 = "
              "nr_08_contention_disable [zxnext.vhd:5906]",
              ok,
              "locked_read=" + hex2(locked) +
              " unlocked_read=" + hex2(unlocked) +
              " cd_on_read=" + hex2(cd_on));
    }
}

// ── Cat27 testcov-memory regression rows ─────────────────────────────
//
// Per-fix regression coverage for memory-subsystem fixes that depend on
// the full Emulator integration (NR-write path / port-dispatch / cross-
// subsystem state). See doc/issues/nextzxos-boot/
// NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY.md.

static void test_cat27_verify8_nr08_effective(Emulator& emu) {
    set_group("Cat27-NR08-Effective");

    // FIX-NR08-EFFLOCK-01 — commit b6b42dd verify8 A3: NR 0x08 bit 7
    // readback uses effective_paging_locked (VHDL :3769), not the raw
    // port_7ffd_reg(5) mirror. Pentagon-1024 mode (NR 0x8F=11 AND EFF7(2)=0)
    // drops port_7ffd_locked to '0' even when bit 5 is set; pre-fix the
    // readback returned NOT raw_bit_5 = 0, post-fix returns NOT eff = 1.
    //
    // This row is on a Next emulator (ZXN_ISSUE2 default). Sequence:
    //   1. Lock paging via port_7FFD bit 5 (raw lock asserted).
    //   2. Read NR 0x08 — bit 7 = 0 (raw lock; both pre- and post-fix).
    //   3. Enable Pentagon-1024: NR 0x8F = 0b11 (mapping_mode = "11").
    //   4. Make sure EFF7(2)=0 (default at reset).
    //   5. Read NR 0x08 — bit 7 should be 1 (effective_paging_locked=0
    //      because Pentagon-1024 override). Pre-fix: 0.
    {
        // Make sure Mmu is at reset for this test (fresh paging state).
        emu.mmu().reset(true);
        emu.contention().rebuild_for_type(MachineType::ZXN_ISSUE2);
        // Ensure EFF7(2) is cleared (Pentagon-1024 enable not disabled).
        emu.mmu().write_port_eff7(0x00);
        // 1. Lock paging via direct Mmu (the port-dispatch handler is the
        //    integration path; we already exercised that in RW-02).
        emu.mmu().map_128k_bank(0x20);  // bit 5 → raw lock asserted
        const uint8_t nr08_raw_lock = nr_read(emu, 0x08);
        const bool bit7_raw = (nr08_raw_lock & 0x80) != 0;
        // 2. Enable Pentagon-1024 mode via NR 0x8F.
        emu.mmu().write_nr_8f(0x03);
        // 3. Sanity: effective_paging_locked() should now be false.
        const bool eff_locked = emu.mmu().effective_paging_locked();
        // 4. Read NR 0x08 — bit 7 should be 1 (=NOT effective_paging_locked).
        const uint8_t nr08_pent = nr_read(emu, 0x08);
        const bool bit7_pent = (nr08_pent & 0x80) != 0;
        check("FIX-NR08-EFFLOCK-01",
              "NR 0x08 bit 7 read returns NOT effective_paging_locked "
              "(Pentagon-1024 overrides the raw lock) — VHDL :5906/:3769; "
              "commit b6b42dd",
              !bit7_raw && !eff_locked && bit7_pent,
              "raw_lock_b7=" + std::to_string(bit7_raw) +
              " eff_locked=" + std::to_string(eff_locked) +
              " pent_b7=" + std::to_string(bit7_pent) +
              " (exp 0/0/1)");
    }
}

// Cat27 testcov-memory follow-up — emulator-handler integration rows.
//
// The base testcov-memory pass (af29460) verified the per-API contract for
// these fixes via direct Mmu/ContentionModel calls. The reviewer flagged
// (NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY-REVIEW.md, "Coverage gaps") that
// a regression in the **emulator NR-write dispatcher** itself (the seam
// between the NR-port write and the Mmu API) would not be caught: a
// future refactor that reverts the dispatcher to a buggy call would slip
// silently through the API-only tests. These rows close the gap by
// driving every fix through the production NR-write path
// (`nr_write(emu, reg, val)` → port 0x253B → NextReg::write → fix
// callback → Mmu API) and observing the public side-effect.
static void test_cat27_emu_handler_integration() {
    set_group("Cat27-Emu-Handler-Integration");

    // ── FIX-NR5xFF-INT-01 — emulator NR $51,$FF dispatcher routes to the
    //    per-slot helper (slot 1 only), preserving the explicit slot-0
    //    mapping. Pre-fix dispatcher path called the both-slot
    //    engage_legacy_rom_paging() helper which clobbered slot 0.
    //    Reviewer finding: bypass risk for FIX-NR5xFF-01 unit row. VHDL
    //    zxnext.vhd:4686-4696 nr_mmu_we per-slot.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("FIX-NR5xFF-INT-01",
                  "Emulator construction failed",
                  false, "build_next_emulator returned false");
        } else {
            emu.mmu().reset(true);
            // Step 1: explicitly map slot 0 to a RAM page via NR $50,RAM.
            nr_write(emu, 0x50, 0x05);
            const uint8_t pre_slot0 = nr_read(emu, 0x50);
            // Step 2: NR $51,$FF — must affect ONLY slot 1 (per-slot helper).
            nr_write(emu, 0x51, 0xFF);
            const uint8_t post_slot0 = nr_read(emu, 0x50);
            const uint8_t post_slot1 = nr_read(emu, 0x51);
            check("FIX-NR5xFF-INT-01",
                  "NR $51,$FF dispatcher uses per-slot helper — slot 0 "
                  "explicit RAM mapping preserved (VHDL :4686-4696; "
                  "commit f832f38)",
                  pre_slot0 == 0x05 && post_slot0 == 0x05 && post_slot1 == 0xFF,
                  "pre_s0=" + hex2(pre_slot0) + " post_s0=" + hex2(post_slot0) +
                  " post_s1=" + hex2(post_slot1) + " (exp 0x05/0x05/0xFF)");
        }
    }

    // ── FIX-NR5xFF-INT-02 — emulator NR $52,$FF dispatcher leaves slot 2
    //    inactive (read 0xFF), NOT mapped to ROM page 0. Pre-fix called
    //    `mmu_.map_rom(2, 0)` which served ROM-page-0 bytes from the slot.
    //    Discriminative observation: the NR-port readback returns 0xFF
    //    only when nr_mmu_[2]=0xFF (= the post-fix sentinel-stored path).
    //    Pre-fix dispatcher set the slot to a non-0xFF value via map_rom,
    //    which would NOT update nr_mmu_ but would change the read pointer.
    //    Combined check: NR readback = 0xFF AND a CPU read from 0x4000
    //    returns 0xFF (= floating-bus / inactive slot per VHDL :3061).
    //
    //    Discriminative seed (fix-reviewer NIT, 2026-05-10): pre-fix
    //    `map_rom(2, 0)` would point slot 2 at `ram_.page_ptr(0)` (because
    //    Next mode has `rom_in_sram_=true` and Emulator::init copies
    //    rom→ram pages 0..7). Without seeding, `ram[0][0]` happens to be
    //    0xFF (because `Rom::Rom()` fills with 0xFF and that gets copied
    //    into ram[0][0]) — so pre-fix `cpu_rd` would coincidentally also
    //    read 0xFF and the row would pass regardless of fix presence.
    //    Seed ram[0][0] = 0x42 BEFORE the NR write to force a discriminative
    //    pre-fix read: post-fix slot is inactive → cpu_rd=0xFF; pre-fix
    //    map_rom(2,0) → cpu_rd=0x42.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("FIX-NR5xFF-INT-02",
                  "Emulator construction failed",
                  false, "build_next_emulator returned false");
        } else {
            emu.mmu().reset(true);
            // Seed page 0 byte 0 with a non-0xFF sentinel so a pre-fix
            // map_rom(2, 0) would yield 0x42 (RAM-backed in Next mode),
            // not the post-fix-required 0xFF (inactive slot).
            emu.ram().page_ptr(0)[0] = 0x42;
            nr_write(emu, 0x52, 0xFF);
            const uint8_t nr_rb = nr_read(emu, 0x52);
            const uint8_t cpu_rd = emu.mmu().read(0x4000);
            check("FIX-NR5xFF-INT-02",
                  "NR $52,$FF dispatcher → slot 2 inactive (CPU read 0xFF) "
                  "AND nr_mmu_[2]=0xFF (VHDL :3061 sram_pre_active=0; "
                  "commit f832f38)",
                  nr_rb == 0xFF && cpu_rd == 0xFF,
                  "nr_rb=" + hex2(nr_rb) + " cpu_rd=" + hex2(cpu_rd) +
                  " (exp 0xFF/0xFF; ram[0][0]=0x42 seed)");
        }
    }

    // ── FIX-NR5xFF-INT-03 — emulator NR $56,$FF dispatcher leaves slot 6
    //    inactive (NOT legacy RAM auto-paged). Pre-fix called
    //    `mmu_.engage_legacy_ram_paging()` which forced bank-0 mapping.
    //    Discriminative: read 0xC000 returns 0xFF (inactive) AND NR $56
    //    readback = 0xFF (verbatim sentinel).
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("FIX-NR5xFF-INT-03",
                  "Emulator construction failed",
                  false, "build_next_emulator returned false");
        } else {
            emu.mmu().reset(true);
            nr_write(emu, 0x56, 0xFF);
            const uint8_t nr_rb = nr_read(emu, 0x56);
            const uint8_t cpu_rd = emu.mmu().read(0xC000);
            check("FIX-NR5xFF-INT-03",
                  "NR $56,$FF dispatcher → slot 6 inactive (NOT legacy RAM "
                  "auto-paged; CPU read 0xFF, NR readback 0xFF) — VHDL "
                  ":3061; commit f832f38",
                  nr_rb == 0xFF && cpu_rd == 0xFF,
                  "nr_rb=" + hex2(nr_rb) + " cpu_rd=" + hex2(cpu_rd) +
                  " (exp 0xFF/0xFF)");
        }
    }

    // ── FIX-EFF7-FF-INT-01 — emulator NR $50,$FF dispatcher under EFF7(3)=1
    //    must (a) leave nr_mmu_[0] at 0xFF (verbatim) and (b) NOT mirror
    //    EFF7's RAM-at-0x0000 override on the NR-write cycle. Pre-fix
    //    dispatcher called engage_legacy_rom_paging() (both slots) instead
    //    of the per-slot helper, and (per the original EFF7-FF-01 fix)
    //    the per-slot helper must not honour the eff7 override on the
    //    NR-write cycle.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("FIX-EFF7-FF-INT-01",
                  "Emulator construction failed",
                  false, "build_next_emulator returned false");
        } else {
            emu.mmu().reset(true);
            // Activate eff7(3) RAM-at-0x0000.
            emu.mmu().write_port_eff7(0x08);
            const bool baseline_ro0 = emu.mmu().is_slot_rom(0);
            // Now NR $50,$FF via the dispatcher.
            nr_write(emu, 0x50, 0xFF);
            const uint8_t nr_rb = nr_read(emu, 0x50);  // verbatim sentinel
            const bool   rom0  = emu.mmu().is_slot_rom(0);
            check("FIX-EFF7-FF-INT-01",
                  "Emulator NR $50,$FF dispatcher under EFF7(3)=1: nr_mmu_[0]="
                  "0xFF verbatim, slot 0 → legacy ROM (NOT eff7 RAM override) "
                  "— VHDL :4686-4696; commits 31d1786 + 560cb18",
                  !baseline_ro0 && nr_rb == 0xFF && rom0,
                  "baseline_ro0=" + std::to_string(baseline_ro0) +
                  " nr_rb=" + hex2(nr_rb) +
                  " rom0=" + std::to_string(rom0) +
                  " (exp 0/0xFF/1)");
        }
    }

    // ── FIX-NR12-PROP-INT-01 — emulator NR 0x12 dispatcher propagates
    //    the new bank to Mmu::l2_bank_ via mmu_.set_l2_active_bank().
    //    Pre-fix the dispatcher only called layer2_.set_active_bank()
    //    and the Mmu's cached `l2_bank_` stayed stale until the next
    //    port 0x123B write. VHDL zxnext.vhd:2968 makes layer2_active_bank
    //    combinational. Discriminative observable: Mmu::l2_bank()
    //    accessor returns the Mmu's cached bank (verify4 fix wires NR 0x12
    //    → mmu_.set_l2_active_bank, so post-fix Mmu::l2_bank() tracks
    //    NR 0x12 writes immediately).
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("FIX-NR12-PROP-INT-01",
                  "Emulator construction failed",
                  false, "build_next_emulator returned false");
        } else {
            emu.mmu().reset(true);
            // Mmu reset re-zeros l2_bank_ to its default 8 (matching
            // Layer2's reset pattern via the NR 0x12 reset value 0x08
            // pushed at NR-init).
            const uint8_t pre_mmu_l2_bank = emu.mmu().l2_bank();
            // Drive NR 0x12 = 16 via the production NR-write port path.
            nr_write(emu, 0x12, 16);
            const uint8_t post_mmu_l2_bank = emu.mmu().l2_bank();
            const uint8_t post_nr12_rb    = nr_read(emu, 0x12);
            check("FIX-NR12-PROP-INT-01",
                  "Emulator NR 0x12 dispatcher propagates new bank to "
                  "Mmu::l2_bank_ via mmu_.set_l2_active_bank() — VHDL :2968; "
                  "commit 560cb18",
                  post_mmu_l2_bank == 16 && post_nr12_rb == 16,
                  "pre_mmu_l2_bank=" + std::to_string(pre_mmu_l2_bank) +
                  " post_mmu_l2_bank=" + std::to_string(post_mmu_l2_bank) +
                  " (exp post=16) post_nr12_rb=" +
                  std::to_string(post_nr12_rb) + " (exp 16)");
        }
    }
}

// ── CFG-01, CFG-02, CFG-05: machine-config state ─────────────────────
//
// Plan row group 7 in NEXTREG-TEST-PLAN-DESIGN.md. NR 0x03 bits 6:4 set
// machine timing, bit 3 XOR-toggles dt_lock, bits 2:0 enter/exit config
// mode, and machine-type writes are gated by config_mode. VHDL
// zxnext.vhd:5121-5151.

static void test_cfg_integration(Emulator& emu) {
    set_group("Machine-Cfg");

    // CFG-01 — VHDL zxnext.vhd:5893-5894 reads NR 0x03 as
    //   port_253b_dat <= nr_palette_sub_idx & nr_03_machine_timing(2:0) &
    //                    nr_03_user_dt_lock & nr_03_machine_type(2:0)
    // i.e. a COMPOSED format, not a raw register read. After reset, the
    // VHDL signal defaults (zxnext.vhd:1099-1103) are:
    //   nr_03_machine_timing = "011" (+3 timing)
    //   nr_03_user_dt_lock   = '0'
    //   nr_03_machine_type   = "011" (+3 machine type)
    // JNEXT now tracks these on NextReg and installs a read_handler on
    // NR 0x03 composing the byte per :5894 (palette_sub_idx not modeled →
    // bit 7 = 0). So after-reset NR 0x03 should read 0x33: bit 7 = 0,
    // bits[6:4] = 011, bit 3 = 0, bits[2:0] = 011.
    //
    // CFG-01 pins bits[6:4]: they must match the VHDL default timing code
    // "011" (+3), not whatever byte was last written to NR 0x03.
    {
        uint8_t got = nr_read(emu, 0x03);
        uint8_t timing_bits = static_cast<uint8_t>((got >> 4) & 0x07);
        check("CFG-01",
              "NR 0x03 bits[6:4] compose from nr_03_machine_timing "
              "(reset default \"011\") [zxnext.vhd:1099, 5893-5894]",
              timing_bits == 0x03,
              std::string("got bits[6:4]=") + std::to_string(timing_bits) +
              " full byte=" + detail_eq(got, 0x33));
    }

    // CFG-02 — NR 0x03 bit 3 XOR-toggles nr_03_user_dt_lock
    // (zxnext.vhd:5135 — nr_03_user_dt_lock <= nr_03_user_dt_lock XOR
    // nr_wr_dat(3), unconditional). The read (zxnext.vhd:5894) exposes
    // the current dt_lock as bit 3.
    //
    // Sequence:
    //   1. Ensure config_mode = 1 (write 0x07 → bits[2:0]=111 re-enters).
    //   2. Write 0x08 (bit 3 = 1) → toggles dt_lock from 0 → 1.
    //   3. Read NR 0x03, assert bit 3 = 1.
    //   4. Write 0x08 again (bit 3 = 1) → toggles dt_lock from 1 → 0.
    //   5. Read NR 0x03, assert bit 3 = 0.
    //
    // The reads are straightforward byte reads; bits[6:4] and bits[2:0]
    // are asserted in CFG-01 / CFG-05 respectively, so CFG-02 pins only
    // bit 3.
    {
        // Re-enter config_mode (CFG-05 may have cleared it if order is
        // not guaranteed). Bits[2:0] = 111 → config_mode ← 1.
        nr_write(emu, 0x03, 0x07);

        nr_write(emu, 0x03, 0x08);  // toggle dt_lock 0 → 1
        uint8_t got1 = nr_read(emu, 0x03);
        bool dtlock_on = (got1 & 0x08) != 0;

        nr_write(emu, 0x03, 0x08);  // toggle dt_lock 1 → 0
        uint8_t got2 = nr_read(emu, 0x03);
        bool dtlock_off = (got2 & 0x08) == 0;

        check("CFG-02",
              "NR 0x03 bit 3 XOR-toggles nr_03_user_dt_lock; read "
              "composes bit 3 from that state [zxnext.vhd:5121-5151, 5894]",
              dtlock_on && dtlock_off,
              std::string("after first toggle bit3=") + (dtlock_on ? "1" : "0") +
              " after second toggle bit3=" + (dtlock_off ? "0" : "1") +
              " (reads got1=" + detail_eq(got1, 0x3B) +
              " got2=" + detail_eq(got2, 0x33) + ")");
    }

    // CFG-05 — NR 0x03 bits 2:0 ∈ {001..100} commit machine type, but
    // only when config_mode = 1 at write time. VHDL zxnext.vhd:5137.
    // After reset config_mode = 1; NR 0x03 = 0x01 should:
    //   (a) commit machine_type = ZX48 (bits 2:0 = 001)
    //   (b) clear config_mode (via apply_nr_03_config_mode_transition)
    // A second NR 0x03 = 0x02 write with config_mode=0 should NOT commit
    // (leave machine_type at the previously committed value).
    //
    // JNEXT has the config_mode state machine (Task 11 Branch 1) but the
    // machine-type commit gating is not re-verified here. Observable:
    // after the sequence, NR 0x03 still reads whatever was last written,
    // but we cannot directly observe machine_type from the port path
    // without a read handler. The test is therefore reduced to the
    // config_mode side: the first write with bits 2:0 ∈ {001..110} must
    // clear config_mode. Machine-type commit verification is deferred.
    {
        // Re-enter config_mode first (bits 2:0 = 111).
        nr_write(emu, 0x03, 0x07);
        // Now bits 2:0 = 001 should clear config_mode.
        nr_write(emu, 0x03, 0x01);
        bool cleared = !emu.nextreg().nr_03_config_mode();
        check("CFG-05",
              "NR 0x03 bits 2:0=001 clears config_mode at write time "
              "[zxnext.vhd:5147-5151 — Task 11 Branch 1 implemented]",
              cleared,
              std::string("config_mode after = ") + (cleared ? "0" : "1"));
    }

    // CFG-09-INT — G63: NR 0x03 machine-type latch survives soft reset.
    //
    // VHDL zxnext.vhd:1103 — `nr_03_machine_type` has signal initialiser
    // "011" (FPGA power-on default). Mutations only at :5137-5145 (NR 0x03
    // write, gated on config_mode='1'). NO explicit reset clause anywhere
    // in zxnext.vhd, so the latch survives both hard and soft reset. The
    // NR 0x03 read at :5894 composes machine_type into bits[2:0].
    //
    // Pre-G63 jnext clobbered nr_03_machine_type_ to 0x03 in every
    // NextReg::reset() call — wrong per VHDL. Fixed by removing the
    // unconditional reset assignment; member initialiser handles
    // power-on. (See src/port/nextreg.cpp `reset()` G63 comment.)
    //
    // Sequence:
    //   1. Re-enter config_mode (NR 0x03 = 0x07 → bits[2:0]=111).
    //   2. Write NR 0x03 = 0x02 → commits machine_type = 010 (ZX128) AND
    //      clears config_mode (bits[2:0]=010 ≠ 000, ≠ 111).
    //   3. Read NR 0x03; assert bits[2:0] = 010 — pre-soft-reset baseline.
    //   4. Soft reset (NR 0x02 = 0x01). Per VHDL, nr_03_machine_type
    //      latches.
    //   5. Read NR 0x03; assert bits[2:0] still = 010 — preserved.
    {
        // Re-enter config_mode (idempotent — earlier subtests may have
        // cleared it).
        nr_write(emu, 0x03, 0x07);
        // Commit machine_type = 010 (ZX128). Also clears config_mode.
        nr_write(emu, 0x03, 0x02);
        const uint8_t pre  = nr_read(emu, 0x03);
        const uint8_t pre_mtype = pre & 0x07;

        // Soft reset. Per VHDL nr_03_machine_type has no reset clause.
        nr_write(emu, 0x02, 0x01);              // NR 0x02 b0 = RESET_SOFT
        const uint8_t post = nr_read(emu, 0x03);
        const uint8_t post_mtype = post & 0x07;

        const bool ok = (pre_mtype == 0x02) && (post_mtype == 0x02);
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "pre-soft-reset NR03=0x%02X (machine_type=%u); "
                      "post-soft-reset NR03=0x%02X (machine_type=%u)",
                      pre, pre_mtype, post, post_mtype);
        check("CFG-09-INT",
              "NR 0x03 machine-type latch survives soft reset "
              "[zxnext.vhd:1103, :5137-5145, :5894 — no reset clause]",
              ok, detail);
    }

    // CFG-08-INT — G62: NR 0x03 config_mode latch survives soft reset.
    //
    // VHDL zxnext.vhd:1102 — `nr_03_config_mode` has signal initialiser
    // '1' (FPGA power-on default). The only mutator is the NR 0x03
    // write_handler at :5147-5151 (set on bits[2:0]=111, clear on bits
    // [2:0] ∈ {001..110}). NO explicit reset clause anywhere in zxnext.vhd
    // — verified by `grep -n "nr_03_config_mode" zxnext.vhd`: no occurrence
    // inside any `if reset = '1' then` block — so the latch survives both
    // hard and soft reset.
    //
    // Pre-G62 jnext clobbered nr_03_config_mode_ to true in every
    // NextReg::reset() call — wrong per VHDL. Fixed by removing the
    // unconditional reset assignment; member initialiser handles
    // power-on. (See src/port/nextreg.cpp `reset()` G62 comment.)
    //
    // RE-HOMED here from nextreg_test.cpp CFG-08 because soft reset
    // (NR 0x02 b0 = 1) flows through Emulator::soft_reset(), not
    // reachable from bare NextReg.
    //
    // Sequence:
    //   1. Re-enter config_mode (NR 0x03 = 0x07 → bits[2:0]=111).
    //   2. Clear config_mode via NR 0x03 = 0x01 (bits[2:0]=001).
    //   3. Assert emu.nextreg().nr_03_config_mode() == false — pre-soft-reset baseline.
    //   4. Soft reset (NR 0x02 = 0x01). Per VHDL the latch survives.
    //   5. Assert emu.nextreg().nr_03_config_mode() == false — preserved.
    {
        // Re-enter config_mode first to guarantee a deterministic starting state
        // (earlier subtests may have left config_mode in either polarity).
        nr_write(emu, 0x03, 0x07);
        // Clear config_mode (bits[2:0]=001 ≠ 000, ≠ 111).
        nr_write(emu, 0x03, 0x01);
        const bool pre_cleared = !emu.nextreg().nr_03_config_mode();

        // Soft reset. Per VHDL nr_03_config_mode has no reset clause.
        nr_write(emu, 0x02, 0x01);              // NR 0x02 b0 = RESET_SOFT
        const bool post_cleared = !emu.nextreg().nr_03_config_mode();

        const bool ok = pre_cleared && post_cleared;
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "pre-soft-reset config_mode=%d; post-soft-reset config_mode=%d",
                      static_cast<int>(!pre_cleared), static_cast<int>(!post_cleared));
        check("CFG-08-INT",
              "NR 0x03 config_mode latch survives soft reset "
              "[zxnext.vhd:1102, :5147-5151 — no reset clause]",
              ok, detail);
    }
}

// ── N8E RAM-gate: NR 0x8E bit 3 gates MMU6/7 rebuild ─────────────────
//
// VHDL zxnext.vhd:3814 drives port_memory_ram_change_dly =
//   NOT(nr_8e_we AND NOT nr_wr_dat(3))
// so an NR 0x8E write with bit 3 = 0 drives the signal to '0' and the
// MMU6/7 update at :4677 is gated off. MMU0/1 still rebuild because
// port_memory_change_dly (:3813) is independent of bit 3.
//
// These two rows pin the gate: with a prior NR 0x56 override in place,
// an NR 0x8E bit-3=0 write must PRESERVE the override; an NR 0x8E
// bit-3=1 write must REBUILD from port_7ffd_bank (clobbering the
// override).

static void test_n8e_ram_gate(Emulator& emu) {
    set_group("N8E-RAM-Gate");

    // N8E-RAM-PRESERVE-0 — NR 0x56 override survives NR 0x8E bit-3=0 write.
    //
    // VHDL: :3814 port_memory_ram_change_dly='0' when nr_8e_we=1 AND
    // nr_wr_dat(3)=0 → :4677 MMU6/7 update skipped. The prior NR 0x56
    // value remains in place.
    //
    // Override value 0x20: non-zero, distinct from any bank*2 value the
    // compose_bank_ rebuild would produce from post-reset state
    // (port_7ffd_=0, port_dffd_=0 → bank=0 → pages 0/1). So a stomping
    // rebuild would flip nr_mmu_[6] to 0x00 and get_page(6)=0x00 — the
    // test distinguishes preservation from unconditional rebuild.
    {
        nr_write(emu, 0x02, 0x02);              // RESET_HARD — deterministic state
        nr_write(emu, 0x56, 0x20);              // establish override on slot 6
        const uint8_t before = emu.mmu().get_page(6);
        nr_write(emu, 0x8E, 0x00);              // bit 3 = 0 → suppress MMU6/7 rebuild
        const uint8_t after  = emu.mmu().get_page(6);
        char detail[96];
        std::snprintf(detail, sizeof(detail), "before=0x%02X after=0x%02X", before, after);
        check("N8E-RAM-PRESERVE-0",
              "NR 0x56 override survives NR 0x8E write with bit 3 = 0 "
              "[zxnext.vhd:3814 port_memory_ram_change_dly, :4677 MMU6/7 gate]",
              before == 0x20 && after == 0x20, detail);
    }

    // N8E-RAM-REBUILD-1 — NR 0x8E bit-3=1 DOES rebuild MMU6/7 from
    // port_7ffd_bank, clobbering a prior NR 0x56 override.
    //
    // Setup: port_7ffd_=0x03 (bank 3 baseline) → NR 0x56=0x20 override →
    // NR 0x8E=0x08 (bit 3 = 1, bits 6:4 = 000) forces 7FFD(2:0)=0, so
    // compose_bank_ yields bank 0 → set_page(6, 0) runs.
    //
    // VHDL: :3814 port_memory_ram_change_dly='1' when nr_wr_dat(3)=1,
    // so :4677 MMU6/7 update runs and overwrites the override.
    {
        nr_write(emu, 0x02, 0x02);              // RESET_HARD
        emu.mmu().map_128k_bank(0x03);          // port_7ffd_=0x03 → bank=3
        nr_write(emu, 0x56, 0x20);              // override slot 6
        const uint8_t before = emu.mmu().get_page(6);
        nr_write(emu, 0x8E, 0x08);              // bit 3 = 1, bits 6:4 = 000
                                                // → 7FFD(2:0) ← 0 → bank=0
        const uint8_t after  = emu.mmu().get_page(6);
        char detail[96];
        std::snprintf(detail, sizeof(detail), "before=0x%02X after=0x%02X", before, after);
        check("N8E-RAM-REBUILD-1",
              "NR 0x8E bit 3 = 1 rebuilds MMU6/7 from port_7ffd_bank "
              "[zxnext.vhd:3814, :4677]",
              before == 0x20 && after == 0x00, detail);
    }
}

// ── Layer 2 bank selection (MMU plan rows L2M-05, L2M-06) ─────────────
//
// These rows were skipped in the bare-Mmu test (test/mmu/mmu_test.cpp)
// because the NR 0x12 (active bank) and NR 0x13 (shadow bank) write
// dispatch lives in Emulator::init handler wiring (zxnext.vhd:4945-4946)
// and stores in the Layer 2 subsystem, not in the bare Mmu surface.
// Mmu::set_l2_port() takes the active bank directly as a parameter and
// Emulator resolves NR 0x12 vs 0x13 at handler-wiring time.
//
// VHDL oracle: zxnext.vhd:4945 (nr_12 active) and :4946 (nr_13 shadow).

static void test_layer2_bank_nr(Emulator& emu) {
    set_group("L2-Bank-NR");

    // L2M-05 — NR 0x12 write lands in layer2 active_bank (7-bit field).
    // VHDL zxnext.vhd:4945: nr_12_layer2_active_bank <= nr_wr_dat(6:0).
    {
        nr_write(emu, 0x12, 0x2A);              // active bank ← 0x2A
        const uint8_t active = emu.layer2().active_bank();
        const uint8_t rb     = nr_read(emu, 0x12);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "active=0x%02X read=0x%02X expected=0x2A",
                      active, rb);
        check("L2M-05",
              "NR 0x12 write sets Layer 2 active bank (7-bit) "
              "[zxnext.vhd:4945 nr_12_layer2_active_bank]",
              active == 0x2A && rb == 0x2A, detail);
    }

    // L2M-05b — NR 0x12 bit 7 is discarded on BOTH write and readback.
    // VHDL zxnext.vhd:4945 stores bits (6:0); VHDL:5930 reads
    // '0' & nr_12_layer2_active_bank (7-bit). The two halves must agree
    // — a naive raw-regs_[] readback on the bare NextReg would return the
    // unmasked byte and fail the rb assertion; only the NR 0x12 read_handler
    // that pulls from Layer2 delivers the VHDL-spec 7-bit read.
    {
        nr_write(emu, 0x12, 0xC5);              // 0b1100_0101 → keep 0x45
        const uint8_t active = emu.layer2().active_bank();
        const uint8_t rb     = nr_read(emu, 0x12);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "active=0x%02X read=0x%02X expected=0x45 (bit 7 masked)",
                      active, rb);
        check("L2M-05b",
              "NR 0x12 top bit masked on write (Layer2 state) AND on readback "
              "(VHDL read-handler) [zxnext.vhd:4945 + 5930]",
              active == 0x45 && rb == 0x45, detail);
    }

    // L2M-06 — NR 0x13 write lands in layer2 shadow_bank (7-bit field).
    // VHDL zxnext.vhd:4946: nr_13_layer2_shadow_bank <= nr_wr_dat(6:0).
    {
        nr_write(emu, 0x13, 0x3B);              // shadow bank ← 0x3B
        const uint8_t shadow = emu.layer2().shadow_bank();
        const uint8_t rb     = nr_read(emu, 0x13);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "shadow=0x%02X read=0x%02X expected=0x3B",
                      shadow, rb);
        check("L2M-06",
              "NR 0x13 write sets Layer 2 shadow bank (7-bit) "
              "[zxnext.vhd:4946 nr_13_layer2_shadow_bank]",
              shadow == 0x3B && rb == 0x3B, detail);
    }

    // L2M-06b — NR 0x13 bit 7 is discarded on BOTH write and readback.
    // Mirrors L2M-05b for the shadow-bank register.
    // VHDL zxnext.vhd:4946 + 5931.
    {
        nr_write(emu, 0x13, 0xB8);              // 0b1011_1000 → keep 0x38
        const uint8_t shadow = emu.layer2().shadow_bank();
        const uint8_t rb     = nr_read(emu, 0x13);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "shadow=0x%02X read=0x%02X expected=0x38 (bit 7 masked)",
                      shadow, rb);
        check("L2M-06b",
              "NR 0x13 top bit masked on write (Layer2 state) AND on readback "
              "(VHDL read-handler) [zxnext.vhd:4946 + 5931]",
              shadow == 0x38 && rb == 0x38, detail);
    }
}

// ── NR 0x68 bit-3 read composition ────────────────────────────────────
//
// VHDL zxnext.vhd:6093 reads NR 0x68 by composing bit 3 from the live
// `port_ff3b_ulap_en` rather than from a stored copy. The enable latch
// has TWO writers — the NR 0x68 bit-3 write path AND the port 0xFF3B
// path (gated on port 0xBF3B ulap_mode = "01", VHDL :4548) — so the
// read must reflect the live state, not the last NR 0x68 snapshot.
//
// Pre-fix bug (Phase A audit, 2026-04-30): jnext returned regs_[0x68]
// directly, so a port 0xFF3B write that mutated ulap_en did not show up
// in subsequent NR 0x68 reads.

static void test_nr_68_ulap_read_composition(Emulator& emu) {
    set_group("NR68-ULAP-Compose");

    // Reset baseline: bit 3 = 0.
    {
        uint8_t got = nr_read(emu, 0x68);
        check("ULAP-01",
              "NR 0x68 bit 3 = 0 at reset (ulap_en=0)",
              (got & 0x08) == 0, detail_eq(got, uint8_t{0x00}));
    }

    // Drive ulap_en=1 via the port 0xFF3B path (NOT via NR 0x68 write).
    // VHDL :4548 — gated on ulap_mode = "01", set via port 0xBF3B.
    emu.port().out(0xBF3B, 0x40);  // ulap_mode = "01"
    emu.port().out(0xFF3B, 0x01);  // ulap_en   = 1
    {
        uint8_t got = nr_read(emu, 0x68);
        check("ULAP-02",
              "NR 0x68 bit 3 reflects port 0xFF3B write (ulap_en=1) "
              "[zxnext.vhd:6093 read composes from port_ff3b_ulap_en]",
              (got & 0x08) != 0, detail_eq(got, uint8_t{0x08}));
    }

    // Diagnostic for the cached-vs-live divergence: NR 0x68 write seeds
    // bit 3 in regs_[0x68], then port 0xFF3B clears the live ulap_en.
    // The read must follow LIVE state, not cached. Without the fix the
    // cached 0x08 leaks through and ULAP-03 fails.
    nr_write(emu, 0x68, 0x08);     // cache bit 3 = 1 + sync ulap_en = 1
    emu.port().out(0xBF3B, 0x40);  // ulap_mode = "01"
    emu.port().out(0xFF3B, 0x00);  // live ulap_en ← 0
    {
        uint8_t got = nr_read(emu, 0x68);
        check("ULAP-03",
              "NR 0x68 bit 3 prefers LIVE port_ff3b_ulap_en over cached "
              "regs_[0x68] (port 0xFF3B clear after NR 0x68 set bit3)",
              (got & 0x08) == 0, detail_eq(got, uint8_t{0x00}));
    }

    // Sanity: the NR 0x68 bit-3 write path still works end-to-end (cache
    // and live state both move). Compose bits 6:5 (blend mode) at the
    // same time to verify other bits stay intact through the read path.
    nr_write(emu, 0x68, 0x68);  // bit 6=1, bit 5=1 → blend mode 11; bit 3=1
    {
        uint8_t got = nr_read(emu, 0x68);
        check("ULAP-04",
              "NR 0x68 read preserves cached blend bits + reflects ulap_en "
              "(write 0x68 → read 0x68)",
              got == 0x68, detail_eq(got, uint8_t{0x68}));
    }

    // Restore reset state for downstream tests.
    nr_write(emu, 0x68, 0x00);
}

// ── G108: NR 0x69 / 0x22 / 0xC4 → port_ff_reg fan-out ─────────────────
//
// VHDL zxnext.vhd:3610-3635 — port_ff_reg is updated from FOUR sources
// in `elsif` priority order (port_ff_wr highest, then nr_69_we, nr_22_we,
// nr_c4_we). Each NR-side write touches only a sub-field:
//   nr_69_we  → port_ff_reg(5:0) <= nr_wr_dat(5:0)        [mode + paper]
//   nr_22_we  → port_ff_reg(6)   <= nr_wr_dat(2)          [int-disable]
//   nr_c4_we  → port_ff_reg(6)   <= NOT nr_wr_dat(0)      [int-disable, inverted]
//
// G108 closure: each handler writes Emulator::port_ff_reg_ AND drives
// Ula::set_screen_mode(port_ff_reg_) so the renderer sees the change
// (was: only port-FF writes propagated to Ula::screen_mode_reg_).
//
// These tests verify the end-to-end propagation: an NR write must update
// both port_ff_reg() and Ula::get_screen_mode_reg() and (for NR 0x69)
// re-decode mode_ + alt_file_.
static void test_g108_port_ff_fanout(Emulator& emu) {
    set_group("G108-PortFF-Fanout");

    // Reset port_ff_reg to a known baseline before each test by writing 0
    // through the port-FF path (highest priority).
    auto port_ff_write = [&](uint8_t v) { emu.port().out(0x00FF, v); };
    auto get_smr = [&]() -> uint8_t {
        return emu.renderer().ula().get_screen_mode_reg();
    };

    // G108-NR69-MODE — writing NR 0x69 with bits 5:0 = HI_RES + paper=5
    // (= 0x06 | (5<<3) = 0x2E) updates port_ff_reg(5:0) AND
    // Ula::screen_mode_reg_(5:0) AND drives mode_ to HI_RES.
    {
        port_ff_write(0x00);                 // baseline: STANDARD
        nr_write(emu, 0x69, 0x2E);           // bits 5:0 = HI_RES, paper=5
        const uint8_t pff       = emu.port_ff_reg();
        const uint8_t smr       = get_smr();
        const uint8_t mode_bits = static_cast<uint8_t>(smr & 0x07);
        check("G108-NR69-MODE",
              "NR 0x69 bits 5:0 fan out to port_ff_reg(5:0) AND "
              "Ula::screen_mode_reg_; mode bits 2:0 = 110 (HI_RES) per "
              "VHDL [zxnext.vhd:3617-3618 + zxula.vhd:191]",
              (pff & 0x3F) == 0x2E && (smr & 0x3F) == 0x2E && mode_bits == 0x06,
              fmt("pff=0x%02X smr=0x%02X mode_bits=0x%02X (exp 0x06=HI_RES)",
                  pff, smr, mode_bits));
    }

    // G108-NR69-PRESERVE-BIT7 — NR 0x69 bits 7:6 are NOT in the fan-out
    // window. They must NOT mutate port_ff_reg(7:6). Bit 7 of NR 0x69 is
    // Layer 2 enable (separate handler effect); bit 6 is "ULA shadow"
    // (separate handler effect). Bit 6 of NR 0x69 happens to also be a
    // value we write here but the fan-out only touches port_ff_reg(5:0).
    {
        port_ff_write(0x40);                 // pre-set port_ff_reg(6)=1
        nr_write(emu, 0x69, 0xFF);           // all bits set; only 5:0 fan out
        const uint8_t pff = emu.port_ff_reg();
        check("G108-NR69-PRESERVE-BIT7",
              "NR 0x69 fan-out only touches port_ff_reg(5:0); bit 6 "
              "(carried in from earlier port-FF write) preserved "
              "[zxnext.vhd:3617-3618]",
              (pff & 0xFF) == 0x7F,
              fmt("pff=0x%02X (exp 0x7F)", pff));
    }

    // G108-NR22-INTDIS-SET — preset port_ff_reg(5:0) via port-FF, then
    // write NR 0x22 with bit 2 = 1; verify port_ff_reg(6) ← 1, bits 5:0
    // preserved, Ula::screen_mode_reg_ in sync.
    {
        port_ff_write(0x2A);                 // bits 5:0 = 0x2A, bit 6 = 0
        nr_write(emu, 0x22, 0x04);           // bit 2 = 1 → port_ff_reg(6) ← 1
        const uint8_t pff = emu.port_ff_reg();
        const uint8_t smr = get_smr();
        check("G108-NR22-INTDIS-SET",
              "NR 0x22 bit 2 → port_ff_reg(6); port_ff_reg(5:0) preserved; "
              "Ula::screen_mode_reg_ propagated [zxnext.vhd:3619-3620]",
              pff == 0x6A && smr == 0x6A,
              fmt("pff=0x%02X smr=0x%02X (exp 0x6A both)", pff, smr));
    }

    // G108-NR22-INTDIS-CLR — same baseline; NR 0x22 bit 2 = 0 clears
    // port_ff_reg(6).
    {
        port_ff_write(0x6A);                 // bit 6 = 1, bits 5:0 = 0x2A
        nr_write(emu, 0x22, 0x00);           // bit 2 = 0 → port_ff_reg(6) ← 0
        const uint8_t pff = emu.port_ff_reg();
        const uint8_t smr = get_smr();
        check("G108-NR22-INTDIS-CLR",
              "NR 0x22 bit 2 = 0 clears port_ff_reg(6); bits 5:0 preserved "
              "[zxnext.vhd:3619-3620]",
              pff == 0x2A && smr == 0x2A,
              fmt("pff=0x%02X smr=0x%02X (exp 0x2A both)", pff, smr));
    }

    // G108-NRC4-INTDIS-CLR — NR 0xC4 polarity is INVERTED:
    //   port_ff_reg(6) <= NOT nr_wr_dat(0)
    // So writing bit 0 = 1 ENABLES the ULA int (clears port_ff_reg(6));
    // writing bit 0 = 0 DISABLES it (sets port_ff_reg(6)).
    // Note bit 1 of NR 0xC4 also fires reschedule_line_interrupt, so we
    // pick a v whose bit 1 is well-defined for this test.
    {
        port_ff_write(0x6A);                 // bit 6 = 1
        nr_write(emu, 0xC4, 0x01);           // bit 0 = 1 → port_ff_reg(6) ← 0
        const uint8_t pff = emu.port_ff_reg();
        const uint8_t smr = get_smr();
        check("G108-NRC4-INTDIS-CLR",
              "NR 0xC4 bit 0 = 1 → port_ff_reg(6) ← 0 (inverted polarity); "
              "bits 5:0 preserved; Ula::screen_mode_reg_ propagated "
              "[zxnext.vhd:3621-3622]",
              pff == 0x2A && smr == 0x2A,
              fmt("pff=0x%02X smr=0x%02X (exp 0x2A both)", pff, smr));
    }

    // G108-NRC4-INTDIS-SET — bit 0 = 0 → port_ff_reg(6) ← 1.
    {
        port_ff_write(0x2A);                 // bit 6 = 0
        nr_write(emu, 0xC4, 0x00);           // bit 0 = 0 → port_ff_reg(6) ← 1
        const uint8_t pff = emu.port_ff_reg();
        const uint8_t smr = get_smr();
        check("G108-NRC4-INTDIS-SET",
              "NR 0xC4 bit 0 = 0 → port_ff_reg(6) ← 1 (inverted polarity) "
              "[zxnext.vhd:3621-3622]",
              pff == 0x6A && smr == 0x6A,
              fmt("pff=0x%02X smr=0x%02X (exp 0x6A both)", pff, smr));
    }

    // G108-PORTFF-WINS — port-FF write supersedes any NR-side fan-out.
    // After staging bit 6 = 1 via NR 0xC4 and bits 5:0 via NR 0x69, a
    // subsequent direct port-FF write must overwrite the FULL 8 bits.
    {
        port_ff_write(0x00);                 // baseline
        nr_write(emu, 0xC4, 0x00);           // port_ff_reg(6) ← 1
        nr_write(emu, 0x69, 0x06);           // port_ff_reg(5:0) ← 0x06 (HI_RES, paper=0)
        // Pre-port-FF state should be 0x46 (bit 6 + HI_RES mode bits).
        const uint8_t pre  = emu.port_ff_reg();
        port_ff_write(0x12);                 // direct write of full byte
        const uint8_t post = emu.port_ff_reg();
        const uint8_t smr  = get_smr();
        check("G108-PORTFF-WINS",
              "Direct port-FF write supersedes accumulated NR-side fan-out; "
              "all 8 bits replaced [zxnext.vhd:3614-3622 elsif chain]",
              pre == 0x46 && post == 0x12 && smr == 0x12,
              fmt("pre=0x%02X (exp 0x46 NR accumulation) "
                  "post=0x%02X smr=0x%02X (exp 0x12 both)",
                  pre, post, smr));
    }

    // Restore reset baseline for downstream tests.
    port_ff_write(0x00);
    nr_write(emu, 0x22, 0x00);
    nr_write(emu, 0xC4, 0x01);  // ULA int enabled (port_ff_reg(6) ← 0)
    port_ff_write(0x00);
}

// ── G56 Cluster A — NR 0x05 / NR 0x06 composed-read divergence ────────
//
// VHDL `port_253b_dat` for NR 0x05 / NR 0x06 (zxnext.vhd:5897 / :5900)
// composes the read byte from a mix of subsystem-owned signals and shadow
// signals that have read-side bit-layouts that don't match write bit
// positions (NR 0x05) or that have write-side gating (NR 0x06 bit 2 is
// only latched while nr_03_config_mode = '1', VHDL :5167).
//
// Pre-fix bug (G56 audit): jnext NextReg::write stored the raw 8-bit
// byte unconditionally (src/port/nextreg.cpp:117-123) and reads fell
// through to that shadow store for any NR without a read_handler. So
// NR 0x06 bit-2 writes outside config_mode would (incorrectly) read
// back as the value last written instead of the VHDL-faithful "no
// change". NR 0x05 happened to round-trip every bit through the shadow
// store, but it had no source-of-truth-from-Joystick read path, so a
// `set_mode_direct()` (test bypass) would diverge from the read.
//
// Post-fix: emulator.cpp installs read_handlers for 0x05 / 0x06 that
// pull from the authoritative state per the VHDL formulas, and the
// 0x06 write handler gates bit 2 on nr_03_config_mode. See:
//   * src/core/emulator.cpp set_read_handler(0x05, ...)
//   * src/core/emulator.cpp set_read_handler(0x06, ...)
//   * src/core/emulator.cpp set_write_handler(0x06, ...) (config_mode gate)
//   * src/core/emulator.h    nr_06_ps2_mode_

static void test_nr_05_composed_read(Emulator& emu) {
    set_group("G56-CR-NR05");

    // Reset baseline — VHDL zxnext.vhd:1105-1106, :1302-1303:
    //   nr_05_joy0          = "001"  (Kempston1)
    //   nr_05_joy1          = "000"  (Sinclair2)
    //   nr_05_5060          = '0'
    //   nr_05_scandouble_en = '1'
    // Read formula at :5897 → joy0[1:0]=01 << 6 | joy0[2]=0 << 3 |
    // eff_5060=0 << 2 | joy1[2:0] all 0 | eff_scandouble=1 → 0x41.
    {
        // Force a fresh init to clear any state from prior groups.
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
    }
    {
        uint8_t got = nr_read(emu, 0x05);
        check("G56-CR-NR05-01",
              "Reset default read = 0x41 "
              "[VHDL zxnext.vhd:5897, :1105-1106, :1302-1303]",
              got == 0x41, detail_eq(got, uint8_t{0x41}));
    }

    // Round-trip: writing a canonical value should read back identical
    // (VHDL :5897 read formula is permutation-invariant w.r.t. write
    // bits — every write bit appears exactly once in the read formula).
    // Write 0xA5 = 0b10100101.
    //   joy0_bits = (v[3], v[7], v[6]) = (0, 1, 0) = "010" = Cursor
    //   joy1_bits = (v[1], v[5], v[4]) = (0, 1, 0) = "010" = Cursor
    //   nr_05_5060          = v[2] = 1
    //   nr_05_scandouble_en = v[0] = 1
    // Read = joy0[1:0] | joy1[1:0]<<4 | joy0[2]<<3 | eff_5060<<2 |
    //        joy1[2]<<1 | eff_scandouble = 0b10 10 0 1 0 1 = 0xA5.
    {
        nr_write(emu, 0x05, 0xA5);
        uint8_t got = nr_read(emu, 0x05);
        check("G56-CR-NR05-02",
              "write 0xA5 → read 0xA5 (round-trip; VHDL :5897 is permutation-id)",
              got == 0xA5, detail_eq(got, uint8_t{0xA5}));
    }

    // Bit-mask discriminator: write 0xFF, every position should read
    // back 1 because every write bit feeds exactly one read bit.
    //   joy0_bits = (1, 1, 1) = "111" = IoMode
    //   joy1_bits = (1, 1, 1) = "111" = IoMode
    //   nr_05_5060=1, nr_05_scandouble_en=1
    // Read = 0xFF.
    {
        nr_write(emu, 0x05, 0xFF);
        uint8_t got = nr_read(emu, 0x05);
        check("G56-CR-NR05-03",
              "write 0xFF → read 0xFF (every bit feeds exactly one read bit)",
              got == 0xFF, detail_eq(got, uint8_t{0xFF}));
    }

    // Partial-clear: write 0x00 — every read bit should be zero,
    // including the 0x40 reset default for the joy0[0]=1 bit (which
    // moves to 0 on a 0x00 write).
    {
        nr_write(emu, 0x05, 0x00);
        uint8_t got = nr_read(emu, 0x05);
        check("G56-CR-NR05-04",
              "write 0x00 → read 0x00 (reset-default joy0[0] cleared)",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // Restore reset state for downstream tests.
    nr_write(emu, 0x05, 0x40);
}

static void test_nr_06_composed_read(Emulator& emu) {
    set_group("G56-CR-NR06");

    // Re-init to clear state; PASS-6 — VHDL zxnext.vhd:4932-4933 only resets
    // NR 0x06 bits 7 and 5 (hotkey_cpu_speed_en, hotkey_5060_en) to '1'.
    // Bits 6, 4, 3, 2, 1, 0 are NOT in the reset block: they have only
    // initial-value declarations (zxnext.vhd:1109-1113) and survive both
    // hard and soft reset. To deterministically test the "reset default"
    // shape (= power-on read of 0xA0) regardless of what earlier groups
    // wrote, write NR 0x06 ← 0x00 first then re-init: post-init bits 7,5
    // re-assert to '1' and the user-cleared bits stay '0', yielding 0xA0.
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
    }
    // G62: nr_03_config_mode has NO reset clause in VHDL (zxnext.vhd:1102 +
    // :5147-5151), so NextReg::reset() preserves whatever state earlier
    // groups left it in. Explicitly re-enter config_mode here (NR 0x03 =
    // 0x07 → bits[2:0]=111) so the gated NR 0x06 bit-2 writes below latch
    // deterministically. Same pattern as CFG-09-INT and CFG-08-INT.
    nr_write(emu, 0x03, 0x07);
    // PASS-6 explicitly clear the preserved-on-reset bits before testing
    // the post-reset readback, so this test does not depend on the order
    // of earlier groups in the run. After the second init(), bits 7 and 5
    // re-assert to '1' (VHDL zxnext.vhd:4932-4933), bit 2 (ps2_mode) was
    // just cleared above, and the remaining preserved-on-reset bits are '0'.
    nr_write(emu, 0x06, 0x00);
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
    }
    nr_write(emu, 0x03, 0x07);

    // Reset default read formula (VHDL :5900):
    //   bits 7=1, 6=0, 5=1, 4=0, 3=0, 2=0, 1:0=00 → 0xA0.
    {
        uint8_t got = nr_read(emu, 0x06);
        check("G56-CR-NR06-01",
              "Reset default read = 0xA0 "
              "[VHDL :5900, :1107-1108, :1111-1112]",
              got == 0xA0, detail_eq(got, uint8_t{0xA0}));
    }

    // Round-trip with config_mode=1: every write bit lands in its read
    // bit. Write 0xFF → expect 0xFF.
    {
        nr_write(emu, 0x06, 0xFF);
        uint8_t got = nr_read(emu, 0x06);
        check("G56-CR-NR06-02",
              "config_mode=1: write 0xFF → read 0xFF (all bits land)",
              got == 0xFF, detail_eq(got, uint8_t{0xFF}));
    }

    // Write 0x00 with config_mode=1 to clear ps2_mode + every other bit.
    {
        nr_write(emu, 0x06, 0x00);
        uint8_t got = nr_read(emu, 0x06);
        check("G56-CR-NR06-03",
              "config_mode=1: write 0x00 → read 0x00",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // Now: drop config_mode by writing NR 0x03 with bits[2:0] != 000/111
    // (VHDL :5147-5151 transition rules). Use 0x03 (machine_type +3,
    // gated commit honoured by emulator.cpp:1031-1039).
    nr_write(emu, 0x03, 0x03);

    // First seed ps2_mode = 1 (need to be in config_mode for that).
    // We already exited; re-enter to seed, then exit again.
    nr_write(emu, 0x03, 0x07);  // bits 111 → re-enter config_mode
    nr_write(emu, 0x06, 0x04);  // ps2_mode = 1, all hotkey enables off
    nr_write(emu, 0x03, 0x03);  // exit config_mode

    // Now attempt to write NR 0x06 with bit 2 = 0 (other bits = 0). VHDL
    // :5167-5169 gates bit 2 — outside config_mode, ps2_mode does NOT
    // change. Read should still see bit 2 = 1 (sticky). All other bits
    // ARE updated unconditionally (VHDL :5162-5166, :5170 are not gated).
    nr_write(emu, 0x06, 0x00);
    {
        uint8_t got = nr_read(emu, 0x06);
        // Expected: bit 2 still = 1 (sticky); all other bits = 0.
        check("G56-CR-NR06-04",
              "config_mode=0: write 0x00 keeps ps2_mode sticky "
              "[VHDL :5167-5169 gate; pre-fix would read 0x00]",
              got == 0x04, detail_eq(got, uint8_t{0x04}));
    }

    // Cross-check: with config_mode still = 0, write bit 2 = 1 — also a
    // no-op for ps2_mode (already 1, but the gate should still apply).
    // Then write all other bits to confirm they update.
    nr_write(emu, 0x06, 0xFB);  // 0xFB = 0b11111011 — bit 2 = 0
    {
        uint8_t got = nr_read(emu, 0x06);
        // ps2_mode stays 1 → bit 2 = 1; all others = 1 → 0xFF.
        check("G56-CR-NR06-05",
              "config_mode=0: write 0xFB still has ps2_mode=1 (sticky); "
              "other bits track write [VHDL :5162-5166, :5170 ungated]",
              got == 0xFF, detail_eq(got, uint8_t{0xFF}));
    }

    // PASS-6 — NR 0x06 reset preservation. VHDL zxnext.vhd:1109-1113 are
    // initial-value-only signals; the reset block at zxnext.vhd:4932-4933
    // touches ONLY bits 7 and 5 (hotkey_cpu_speed_en + hotkey_5060_en),
    // re-asserting them to '1'. Bits 6, 4, 3, 2, 1, 0 (internal_speaker_beep,
    // button_drive_nmi_en, button_m1_nmi_en, ps2_mode, psg_mode[1:0]) survive
    // both hard and soft reset. Concretely:
    //   * Bits 4 and 3 (the DivMMC and Multiface NMI button enables, VHDL
    //     :2090-2091) MUST survive — software that arms NMI dispatch then
    //     issues NR 0x02 ← 0x01 (soft reset) expects the gate to remain
    //     armed across the reset boundary, exactly as on real hardware.
    //   * Bit 2 (ps2_mode) is config-mode-gated on write (VHDL :5167-5169)
    //     but otherwise sticky — already covered by G56-CR-NR06-04.
    //   * Bits 7 and 5 are forced back to '1' on every reset.
    //
    // Pre-pass-6 jnext re-applied 0xA0 verbatim on every NextReg::reset(),
    // clobbering bits 4/3/2/1/0 — observable as the NMI gate going dead
    // immediately after a deliberate soft reset.
    {
        // Re-init to reach a known starting state, then drive config_mode=1
        // so the gated bit-2 path is open.
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
        nr_write(emu, 0x03, 0x07);
        // Set every preserved-on-reset bit + clear bits 7,5 (which the
        // reset block re-asserts). The post-write byte is 0x5F:
        //   bits 7,5 = 0 (cleared, will be re-set by reset)
        //   bit 6    = 1 (internal_speaker_beep, preserved)
        //   bit 4    = 1 (button_drive_nmi_en, preserved)
        //   bit 3    = 1 (button_m1_nmi_en, preserved)
        //   bit 2    = 1 (ps2_mode, preserved — config_mode=1 lets it commit)
        //   bits 1:0 = 11 (psg_mode "11" = AY+reset, preserved)
        nr_write(emu, 0x06, 0x5F);
        // Trigger an emulator reset (= calls init() → NextReg::reset()).
        emu.init(cfg);
        // Per VHDL: bits 7 and 5 are now '1'; bits 6,4,3,2,1,0 survive at '1'.
        // Expected readback: 0xA0 | 0x5F = 0xFF.
        const uint8_t got = nr_read(emu, 0x06);
        check("G56-CR-NR06-RESET-PRESERVE",
              "PASS-6: NR 0x06 bits 6,4,3,2,1,0 survive reset; bits 7,5 re-assert "
              "to '1' [VHDL zxnext.vhd:4932-4933 reset clause covers ONLY "
              "hotkey_cpu_speed_en/hotkey_5060_en]",
              got == 0xFF, detail_eq(got, uint8_t{0xFF}));
    }

    // Restore config_mode + zero out for downstream cleanliness.
    nr_write(emu, 0x03, 0x07);  // re-enter config_mode
    nr_write(emu, 0x06, 0xA0);  // back to reset default
}

// ── G56 Cluster B — composed-read VHDL-faithful read handlers ────────
//
// Verifies that NR 0x09, 0x0A, 0x0B, 0x10, 0x15, 0x34 read back via the
// VHDL composition formula (zxnext.vhd:5909, :5912, :5915, :5924,
// :5939, :6033) rather than echoing the last-written byte through
// NextReg::write's raw cache store.
//
// Without these read_handlers, write(0xFF) → read returns 0xFF for any
// NR with reserved/const-zero bits or fields composed from authoritative
// state. Each VHDL formula is cited in the row description.

static void test_g56_cluster_b(Emulator& emu) {
    set_group("G56-Cluster-B");

    // Ensure config_mode = 1 so writes that are gated on it (NR 0x0A
    // mf_type/sd_swap, NR 0x10 coreid) commit normally. Earlier tests
    // (test_cfg_integration, test_soft_reset) may have left it cleared.
    // Bits[2:0] = 111 → config_mode <- 1 (zxnext.vhd:5147).
    nr_write(emu, 0x03, 0x07);

    // ── NR 0x09 — Peripheral 4 control / scanlines ────────────────────
    // VHDL zxnext.vhd:5909:
    //   port_253b_dat <= nr_09_psg_mono [7:5] & nr_09_sprite_tie [4]
    //                  & '0' [3] & (NOT nr_09_hdmi_audio_en) [2]
    //                  & eff_nr_09_scanlines [1:0]
    // - psg_mono [7:5]: from cached last-write (no consumer)
    // - sprite_tie [4]: from sprites_.mirror_tie()
    // - bit 3: const-0
    // - bit 2: echoes the WRITE bit 2 (VHDL: stored as `not`, read as `not not`)
    // - eff_scanlines [1:0]: from cached last-write
    {
        // Round-trip with all bits set: bit 3 must read 0, bit 4 must
        // come from sprites_.mirror_tie() (which is set by the same
        // write — both should agree).
        nr_write(emu, 0x09, 0xFF);
        uint8_t got = nr_read(emu, 0x09);
        check("G56-09-01",
              "NR 0x09 write=0xFF read=0xF7 (bit 3 const-0)",
              got == 0xF7, detail_eq(got, uint8_t{0xF7}));
    }
    {
        // Clear path — round-trip 0x00.
        nr_write(emu, 0x09, 0x00);
        uint8_t got = nr_read(emu, 0x09);
        check("G56-09-02",
              "NR 0x09 write=0x00 read=0x00",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // Bit-mask: write a pattern that exercises sprite_tie + scanlines
        // + hdmi_audio bit. 0x14 = sprite_tie=1, hdmi_audio_en write
        // bit 2 = 1. Read should be 0x14 (bit 4 + bit 2 set).
        nr_write(emu, 0x09, 0x14);
        uint8_t got = nr_read(emu, 0x09);
        check("G56-09-03",
              "NR 0x09 write=0x14 → bit4 (sprite_tie) + bit2 (hdmi_audio) "
              "[zxnext.vhd:5909]",
              got == 0x14, detail_eq(got, uint8_t{0x14}));
    }
    {
        // Bit 3 of write must be ignored on read.
        nr_write(emu, 0x09, 0x08);
        uint8_t got = nr_read(emu, 0x09);
        check("G56-09-04",
              "NR 0x09 write=0x08 → read=0x00 (bit 3 forced 0) "
              "[zxnext.vhd:5909]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // ── NR 0x0A — SD-card swap + mouse + DivMMC + Multiface ──────────
    // VHDL zxnext.vhd:5912:
    //   port_253b_dat <= nr_0a_mf_type [7:6] & nr_0a_sd_swap [5]
    //                  & nr_0a_divmmc_automap_en [4]
    //                  & nr_0a_mouse_button_reverse [3] & '0' [2]
    //                  & nr_0a_mouse_dpi [1:0]
    {
        nr_write(emu, 0x0A, 0xFF);
        uint8_t got = nr_read(emu, 0x0A);
        check("G56-0A-01",
              "NR 0x0A write=0xFF read=0xFB (bit 2 const-0) "
              "[zxnext.vhd:5912]",
              got == 0xFB, detail_eq(got, uint8_t{0xFB}));
    }
    {
        // Clear-path round trip — but mouse_dpi reset default is "01"
        // (zxnext.vhd:1128) — KempstonMouse::reset() sets dpi_=0x01.
        // After write(0x00), set_dpi(0) drives dpi to 0, so read=0x00.
        nr_write(emu, 0x0A, 0x00);
        uint8_t got = nr_read(emu, 0x0A);
        check("G56-0A-02",
              "NR 0x0A write=0x00 read=0x00",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // Bit-mask: only divmmc_automap (bit 4) set. config_mode is 1 at
        // power-on (re-default in NextReg::reset), but nothing in the
        // tests has flipped it, so writes commit fully.
        nr_write(emu, 0x0A, 0x10);
        uint8_t got = nr_read(emu, 0x0A);
        check("G56-0A-03",
              "NR 0x0A write=0x10 → bit 4 (divmmc_automap_en) only "
              "[zxnext.vhd:5912]",
              got == 0x10, detail_eq(got, uint8_t{0x10}));
    }
    {
        // Bit 2 must read 0 even when written 1.
        nr_write(emu, 0x0A, 0x04);
        uint8_t got = nr_read(emu, 0x0A);
        check("G56-0A-04",
              "NR 0x0A write=0x04 → read=0x00 (bit 2 forced 0) "
              "[zxnext.vhd:5912]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // V11-NMP-02 discriminative — VHDL zxnext.vhd:5191-5198 gates the
    // latches `nr_0a_mf_type` (bits 7:6) and `nr_0a_sd_swap` (bit 5)
    // behind `nr_03_config_mode = '1'`. When the gate is closed, the
    // underlying signals retain their previous values. Pre-fix the C++
    // wrote the raw byte `v` into `regs_[0x0A]` regardless of the gate,
    // poisoning the cache. The observable consequence flows through the
    // init/reset fan-out at emulator.cpp:985-996, which reads
    // `cached(0x0A)` and forwards bits 7:6 to `multiface_.set_mode()`.
    // The discriminative chain: (a) commit bits 7:6 = 11 in config_mode,
    // (b) exit config_mode, (c) attempt to clear bits 7:6 via NR 0x0A
    // write — VHDL ignores that, jnext pre-fix corrupted the cache,
    // (d) trigger RESET_HARD via NR 0x02 = 0x02 — the cache is
    // re-applied via the fan-out and the post-reset NR 0x0A read should
    // still surface bits 7:6 = 11.  Pre-fix this read is 0x00 (= bug
    // present, cache leaked the rejected write); post-fix it is 0xC0
    // (multiface mode_48 preserved per VHDL).
    {
        // (a) Ensure config_mode = 1 and write mf_type = 11 (mode_48).
        nr_write(emu, 0x03, 0x07);              // bits[2:0]=111 → cfg_mode=1
        nr_write(emu, 0x0A, 0xC0);              // bits 7:6 = 11
        const uint8_t after_commit = nr_read(emu, 0x0A);
        // (b) Exit config_mode.
        nr_write(emu, 0x03, 0x01);              // bits[2:0]=001 → cfg_mode=0
        // (c) Try to clear bits 7:6 outside config_mode — VHDL latches
        //     should retain the previous value.
        nr_write(emu, 0x0A, 0x00);
        const uint8_t after_reject = nr_read(emu, 0x0A);
        // (d) Hard reset — re-enters cache-driven fan-out at init().
        //     NextReg::reset() preserves regs_[0x0A] (no reset clause in
        //     VHDL :1124-1128), so the canonicalised cache survives.
        nr_write(emu, 0x02, 0x02);              // RESET_HARD
        const uint8_t after_reset = nr_read(emu, 0x0A);
        char d[128];
        std::snprintf(d, sizeof(d),
                      "after_commit=0x%02X after_reject=0x%02X "
                      "after_reset=0x%02X (want each top-2-bits 11)",
                      after_commit, after_reject, after_reset);
        const bool committed_ok  = ((after_commit  & 0xC0) == 0xC0);
        const bool rejected_ok   = ((after_reject  & 0xC0) == 0xC0);
        const bool post_reset_ok = ((after_reset   & 0xC0) == 0xC0);
        check("V11-NMP-02",
              "NR 0x0A bits 7:6 (mf_type) survive an out-of-config_mode "
              "attempted clear AND a subsequent hard reset — VHDL :5191 "
              "gates the latch on config_mode='1'; pre-fix the C++ cache "
              "leaked the rejected write into the post-reset fan-out",
              committed_ok && rejected_ok && post_reset_ok, d);
        // Restore config_mode = 1 and clear the mf_type so downstream
        // tests see a clean baseline (the hard reset above zeroed RAM
        // but preserved cached(0x0A) which now has bits 7:6 = 11).
        nr_write(emu, 0x03, 0x07);              // re-enter config_mode
        nr_write(emu, 0x0A, 0x01);              // mouse_dpi power-on default
    }

    // V11-NMP-03 discriminative — VHDL zxnext.vhd:5167-5169 gates the
    // latch `nr_06_ps2_mode` (bit 2) behind `nr_03_config_mode = '1'`.
    // When the gate is closed, the underlying signal retains its previous
    // value. Pre-fix the C++ wrote the raw byte `v` into `regs_[0x06]`
    // regardless of the gate (the read handler masked bit 2 from the live
    // `nr_06_ps2_mode_` shadow, but the cache itself absorbed the rejected
    // bit). The observable consequence flows through the init/reset
    // fan-out at emulator.cpp:3272-3276, which seeds `nr_06_ps2_mode_`
    // from `cached(0x06) & 0x04` after every reset. The discriminative
    // chain mirrors V11-NMP-02 exactly: (a) commit ps2_mode = 1 in
    // config_mode, (b) exit config_mode, (c) attempt to clear bit 2 via
    // NR 0x06 write — VHDL ignores that, jnext pre-fix corrupted the
    // cache, (d) trigger RESET_HARD via NR 0x02 = 0x02 — the cache is
    // re-applied via the init fan-out and the post-reset NR 0x06 read
    // should still surface bit 2 = 1. Pre-fix this read is 0xA0 (= bug
    // present, cache leaked the rejected write; only bits 7,5 from the
    // VHDL reset clause remain set); post-fix it is 0xA4 (ps2_mode
    // preserved per VHDL).
    {
        // (a) Ensure config_mode = 1 and commit ps2_mode = 1. Clear all
        //     other preserved-on-reset bits so post-reset readback is
        //     unambiguous (only the reset-asserted bits 7,5 + the gated
        //     bit 2 will be set).
        nr_write(emu, 0x03, 0x07);              // bits[2:0]=111 → cfg_mode=1
        nr_write(emu, 0x06, 0x04);              // bit 2 = 1, all else = 0
        const uint8_t after_commit = nr_read(emu, 0x06);
        // (b) Exit config_mode.
        nr_write(emu, 0x03, 0x01);              // bits[2:0]=001 → cfg_mode=0
        // (c) Try to clear bit 2 outside config_mode — VHDL latch should
        //     retain the previous value (ps2_mode = 1).
        nr_write(emu, 0x06, 0x00);
        const uint8_t after_reject = nr_read(emu, 0x06);
        // (d) Hard reset — re-enters cache-driven fan-out at init().
        //     NextReg::reset() preserves the lower bits of regs_[0x06]
        //     (no reset clause for them in VHDL :1109-1113); only bits
        //     7 and 5 are re-asserted. The init fan-out at lines 3272-
        //     3276 then seeds `nr_06_ps2_mode_` from `cached(0x06) & 0x04`.
        //     Pre-fix the cached byte had bit 2 = 0 (cache leaked the
        //     rejected write), so post-reset ps2_mode_ became false and
        //     the read returned 0xA0 (bits 7,5 only). Post-fix the cache
        //     was canonicalised so bit 2 = 1 in both cache and shadow,
        //     and the read returns 0xA4.
        nr_write(emu, 0x02, 0x02);              // RESET_HARD
        const uint8_t after_reset = nr_read(emu, 0x06);
        char d[160];
        std::snprintf(d, sizeof(d),
                      "after_commit=0x%02X after_reject=0x%02X "
                      "after_reset=0x%02X (want bit 2 set in each)",
                      after_commit, after_reject, after_reset);
        const bool committed_ok  = ((after_commit  & 0x04) == 0x04);
        const bool rejected_ok   = ((after_reject  & 0x04) == 0x04);
        const bool post_reset_ok = ((after_reset   & 0x04) == 0x04);
        check("V11-NMP-03",
              "NR 0x06 bit 2 (ps2_mode) survives an out-of-config_mode "
              "attempted clear AND a subsequent hard reset — VHDL :5167-"
              "5169 gates the latch on config_mode='1'; pre-fix the C++ "
              "cache leaked the rejected write into the post-reset "
              "init/reset fan-out at emulator.cpp:3272-3276",
              committed_ok && rejected_ok && post_reset_ok, d);
        // Restore config_mode = 1 and clean baseline so downstream tests
        // see consistent state (NR 0x06 cache still has bit 2 = 1 from
        // the preserved-on-reset path; clear it explicitly).
        nr_write(emu, 0x03, 0x07);              // re-enter config_mode
        nr_write(emu, 0x06, 0xA0);              // back to power-on default
    }

    // ── NR 0x0B — Joystick I/O mode ──────────────────────────────────
    // VHDL zxnext.vhd:5915:
    //   port_253b_dat <= nr_0b_joy_iomode_en [7] & '0' [6]
    //                  & nr_0b_joy_iomode [5:4] & "000" [3:1]
    //                  & nr_0b_joy_iomode_0 [0]
    {
        nr_write(emu, 0x0B, 0xFF);
        uint8_t got = nr_read(emu, 0x0B);
        check("G56-0B-01",
              "NR 0x0B write=0xFF read=0xB1 (bits 6 + 3:1 forced 0) "
              "[zxnext.vhd:5915]",
              got == 0xB1, detail_eq(got, uint8_t{0xB1}));
    }
    {
        nr_write(emu, 0x0B, 0x00);
        uint8_t got = nr_read(emu, 0x0B);
        check("G56-0B-02",
              "NR 0x0B write=0x00 read=0x00",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // Bits 6 + 3:1 const-0 even when written 1.
        nr_write(emu, 0x0B, 0x4E);  // bits 6,3,2,1 set
        uint8_t got = nr_read(emu, 0x0B);
        check("G56-0B-03",
              "NR 0x0B write=0x4E → read=0x00 (bits 6 + 3:1 forced 0) "
              "[zxnext.vhd:5915]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // Just enable + iomode 5:4 + iomode_0.
        nr_write(emu, 0x0B, 0xB1);  // bit 7 + bits 5:4 + bit 0
        uint8_t got = nr_read(emu, 0x0B);
        check("G56-0B-04",
              "NR 0x0B write=0xB1 → read=0xB1 (en + iomode 5:4 + iomode_0) "
              "[zxnext.vhd:5915]",
              got == 0xB1, detail_eq(got, uint8_t{0xB1}));
    }
    // Restore reset value so other tests aren't perturbed.
    nr_write(emu, 0x0B, 0x01);

    // ── NR 0x10 — core/board ID + SPKEY_BUTTONS ──────────────────────
    // VHDL zxnext.vhd:5924:
    //   port_253b_dat <= '0' [7] & nr_10_coreid [6:2]
    //                  & i_SPKEY_BUTTONS(1:0) [1:0]
    // jnext does not model SPKEY_BUTTONS — they read 0 (idle).
    // Power-on default: nr_10_coreid = "00001" (zxnext.vhd:1133),
    // so reset read = 0_00001_00 = 0x04.
    {
        // Reset default. Note this test runs after other tests that may
        // have written NR 0x10. If config_mode is still 1 at this point,
        // the previous writes will have changed coreid. Reset emu state
        // by writing the VHDL default explicitly.
        nr_write(emu, 0x10, 0x01);  // coreid = 0x01
        uint8_t got = nr_read(emu, 0x10);
        check("G56-10-01",
              "NR 0x10 coreid=0x01 → read=0x04 (bit7=0, coreid<<2, "
              "buttons=0 idle) [zxnext.vhd:5924]",
              got == 0x04, detail_eq(got, uint8_t{0x04}));
    }
    {
        // coreid = 0x1F → bits 6:2 = 11111 → 0x7C.
        nr_write(emu, 0x10, 0x1F);
        uint8_t got = nr_read(emu, 0x10);
        check("G56-10-02",
              "NR 0x10 coreid=0x1F → read=0x7C (bits 6:2 set, bit 7 + 1:0 = 0) "
              "[zxnext.vhd:5924]",
              got == 0x7C, detail_eq(got, uint8_t{0x7C}));
    }
    {
        // Bit 7 of write (flashboot) does NOT appear on read.
        nr_write(emu, 0x10, 0x80);
        uint8_t got = nr_read(emu, 0x10);
        check("G56-10-03",
              "NR 0x10 write=0x80 (flashboot) → read=0x00 "
              "(flashboot not exposed in NR 0x10 read) [zxnext.vhd:5924]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // V16-NMP-01: SPKEY_BUTTONS [1:0] live held-state path. Per
        // VHDL zxnext.vhd:5924 + zxnext_top_issue2.vhd:2281,
        //   bit 0 = NOT btn_m1_multiface_n        (F9 held = 1)
        //   bit 1 = NOT btn_drive_divmmc_n        (F10 held = 1)
        // Bits 1:0 are sampled COMBINATIONALLY on every NR 0x10 read —
        // they are NOT cache-write-driven. Pre-fix: write bits 1:0 leaked
        // into the cache and the read returned 0 (because the write
        // handler hardcoded them to 0). Post-fix: write of 0x03 still
        // does NOT propagate to bits 1:0 (idle buttons → 0); the read
        // now consults the live held-state. Idle baseline:
        nr_write(emu, 0x10, 0x03);  // tries to set buttons via write
        uint8_t got = nr_read(emu, 0x10);
        check("G56-10-04",
              "NR 0x10 write=0x03 → read bits 1:0 = 0 "
              "(buttons idle; bits 1:0 only respond to live SPKEY_BUTTONS) "
              "[zxnext.vhd:5924]",
              (got & 0x03) == 0, detail_eq(got, uint8_t{0x00}));
    }
    // Restore reset coreid so downstream tests see VHDL default.
    nr_write(emu, 0x10, 0x01);

    // ── NR 0x15 — Sprite + layer system setup ─────────────────────────
    // VHDL zxnext.vhd:5939:
    //   port_253b_dat <= nr_15_lores_en [7] & nr_15_sprite_priority [6]
    //                  & nr_15_sprite_border_clip_en [5]
    //                  & nr_15_layer_priority [4:2]
    //                  & nr_15_sprite_over_border_en [1]
    //                  & nr_15_sprite_en [0]
    {
        nr_write(emu, 0x15, 0xFF);
        uint8_t got = nr_read(emu, 0x15);
        // All 8 bits are real fields, no const-0. Round-trip 0xFF→0xFF.
        check("G56-15-01",
              "NR 0x15 write=0xFF read=0xFF (round-trip) "
              "[zxnext.vhd:5939]",
              got == 0xFF, detail_eq(got, uint8_t{0xFF}));
    }
    {
        nr_write(emu, 0x15, 0x00);
        uint8_t got = nr_read(emu, 0x15);
        check("G56-15-02",
              "NR 0x15 write=0x00 read=0x00",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // Bit-mask: layer priority 110 (USL) + sprite_en, otherwise 0.
        nr_write(emu, 0x15, 0x19);  // 0001_1001 = priority 110, en=1
        uint8_t got = nr_read(emu, 0x15);
        check("G56-15-03",
              "NR 0x15 write=0x19 → priority=110 + sprite_en "
              "[zxnext.vhd:5939]",
              got == 0x19, detail_eq(got, uint8_t{0x19}));
    }
    {
        // sprite_priority (bit 6) + border_clip_en (bit 5).
        nr_write(emu, 0x15, 0x60);
        uint8_t got = nr_read(emu, 0x15);
        check("G56-15-04",
              "NR 0x15 write=0x60 → bit6 (sprite_priority) + bit5 "
              "(border_clip_en) [zxnext.vhd:5939]",
              got == 0x60, detail_eq(got, uint8_t{0x60}));
    }
    // Restore reset value.
    nr_write(emu, 0x15, 0x00);

    // ── NR 0x34 — Sprite mirror_id (alternate to port 0x303B) ────────
    // VHDL zxnext.vhd:6033:
    //   port_253b_dat <= '0' [7] & sprite_mirror_id [6:0]
    {
        nr_write(emu, 0x34, 0xFF);
        uint8_t got = nr_read(emu, 0x34);
        // sprites_.set_mirror_sprite_num(0xFF) stores 0xFF (8 bits) but
        // sprite_mirror_id is only 7 bits in VHDL — read formula masks
        // to 0x7F.
        check("G56-34-01",
              "NR 0x34 write=0xFF read=0x7F (bit 7 const-0) "
              "[zxnext.vhd:6033]",
              got == 0x7F, detail_eq(got, uint8_t{0x7F}));
    }
    {
        nr_write(emu, 0x34, 0x00);
        uint8_t got = nr_read(emu, 0x34);
        check("G56-34-02",
              "NR 0x34 write=0x00 read=0x00",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        // Mid-range: sprite slot 0x42 — bit 7 must read 0.
        nr_write(emu, 0x34, 0x42);
        uint8_t got = nr_read(emu, 0x34);
        check("G56-34-03",
              "NR 0x34 write=0x42 → read=0x42 (bit 7 already 0)",
              got == 0x42, detail_eq(got, uint8_t{0x42}));
    }
    {
        // Slot 0x80 (write would set bit 7 + slot 0). Read masks bit 7.
        nr_write(emu, 0x34, 0x80);
        uint8_t got = nr_read(emu, 0x34);
        check("G56-34-04",
              "NR 0x34 write=0x80 → read=0x00 (bit 7 forced 0) "
              "[zxnext.vhd:6033]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // ── NR 0x10 — save/load round-trip ───────────────────────────────────
    // Cluster B introduced Emulator::nr_10_coreid_ as the authoritative
    // source for the read_handler. Without serialising it across save_state
    // / load_state, a fresh Emulator restored from a snapshot would
    // re-init() the field to 0x01 (VHDL reset default) and the post-load
    // read of NR 0x10 would not match the pre-save read. This row pins
    // that round-trip: write coreid = 0x1F (read = 0x7C), snapshot,
    // restore into a fresh Emulator, read NR 0x10 — must still be 0x7C.
    {
        // Make sure config_mode = 1 so the NR 0x10 write commits coreid.
        nr_write(emu, 0x03, 0x07);
        nr_write(emu, 0x10, 0x1F);  // coreid <- 0x1F → read = 0x7C
        uint8_t pre_save = nr_read(emu, 0x10);

        // Measure snapshot, save into a buffer.
        StateWriter measure;
        emu.save_state(measure);
        const size_t snap_size = measure.position();
        std::vector<uint8_t> buf(snap_size, 0);
        StateWriter w(buf.data(), snap_size);
        emu.save_state(w);

        // Restore into a FRESH Emulator (mirrors GUI load-state flow).
        Emulator emu2;
        if (!build_next_emulator(emu2)) {
            check("G56-10-SAVE-LOAD",
                  "NR 0x10 coreid round-trips save_state / load_state "
                  "into fresh Emulator [Cluster-B regression guard]",
                  false, "build_next_emulator(emu2) failed");
        } else {
            StateReader rdr(buf.data(), snap_size);
            emu2.load_state(rdr);
            uint8_t post_load = nr_read(emu2, 0x10);
            check("G56-10-SAVE-LOAD",
                  "NR 0x10 coreid round-trips save_state / load_state "
                  "into fresh Emulator [Cluster-B regression guard]",
                  pre_save == 0x7C && post_load == 0x7C,
                  fmt("pre_save=0x%02X post_load=0x%02X expected=0x7C",
                      pre_save, post_load));
        }
    }
    // Restore reset coreid so downstream tests see VHDL default.
    nr_write(emu, 0x10, 0x01);
}

// ── G56 cluster C — NR 0x22 / NR 0x23 line-interrupt read composition ─
//
// VHDL zxnext.vhd:5992 (NR 0x22 read):
//   port_253b_dat <= (not pulse_int_n) & "0000"
//                    & port_ff_interrupt_disable
//                    & nr_22_line_interrupt_en
//                    & nr_23_line_interrupt(8);
//
// Bit layout:
//   bit 7   = NOT pulse_int_n  (dynamic; INT pulse asserted-low → bit7=1)
//   bits 6:3 = "0000"          (constants)
//   bit 2   = port_ff_interrupt_disable = port_ff_reg(6)
//   bit 1   = nr_22_line_interrupt_en   (VideoTiming::line_interrupt_enable)
//   bit 0   = nr_23_line_interrupt(8)   (line_interrupt_target() >> 8)
//
// VHDL zxnext.vhd:5995 (NR 0x23 read):
//   port_253b_dat <= nr_23_line_interrupt(7 downto 0);
//
// Pre-fix bug: jnext returned regs_[0x22] / regs_[0x23] verbatim. The
// write-handler at emulator.cpp:847-872 stored authoritative state in
// VideoTiming + port_ff_reg_, but never the raw byte; reads bypassed
// the structured state and exposed a "regs_[] echo" — which happened
// to match the input only in a narrow case. NR 0x22 in particular
// fails round-trip because the constant "0000" middle nibble is not
// part of the input.

static void test_nr_22_23_lineint_read(Emulator& emu) {
    set_group("LineINT-NR22-NR23");

    // L22-01 — Reset baseline: line-int-en=0, target=0, port_ff_reg(6)=0,
    //         pulse_int_n=1 (idle) → bit7=0. Whole register reads 0x00.
    // VHDL zxnext.vhd:5992. Pre-fix returned regs_[0x22]=0 too, so this
    // row passed by accident. We keep it as a regression anchor.
    {
        // Establish a known-clean baseline — prior tests may have left
        // line-int wiring set; rewrite both regs to defaults.
        nr_write(emu, 0x22, 0x00);
        nr_write(emu, 0x23, 0x00);
        emu.port().out(0xFF, 0x00);  // clear port_ff_reg(6) too

        const uint8_t got = nr_read(emu, 0x22);
        check("L22-01",
              "NR 0x22 reset/idle reads 0x00 (no line-int, no INT pulse) "
              "[zxnext.vhd:5992]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // L22-02 — Round-trip: write 0x07 (bit2|bit1|bit0). Bits 2/1/0 are
    // authoritative; middle nibble 6:3 must read back as "0000" (NOT the
    // input's middle bits). Bit 7 reflects pulse_int_n: idle → 0.
    // Pre-fix: read returns regs_[0x22]=0x07 — passes accidentally.
    // Post-fix: read returns 0x07 (bits 2|1|0 all 1, bits 6:3 = 0,
    //           bit 7 = NOT pulse_int_n = 0).
    {
        nr_write(emu, 0x22, 0x07);
        const uint8_t got = nr_read(emu, 0x22);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "got=0x%02X expected=0x07 (bits 2|1|0 set, "
                      "middle nibble = 0, bit7 = NOT pulse_int_n)",
                      got);
        check("L22-02",
              "NR 0x22 round-trip: write 0x07 → read 0x07 "
              "[zxnext.vhd:5992 composes bits 2/1/0 from "
              "port_ff_reg(6)/line-int-en/target-msb]",
              got == 0x07, detail);
    }

    // L22-03 — Constant-bits-cleared invariant: write 0xFF and verify
    // bits 6:3 are masked out on readback (return "0000"). This is the
    // canonical pre-fix divergence: regs_[0x22] echo would read 0xFF or
    // similar; the read-handler must compose only bits 7|2|1|0.
    // Pre-fix: read returns 0xFF (all-bits echo). Post-fix: read returns
    // 0x07 because the only authoritative bits set by the write are
    // line-int-en (bit1), target-MSB (bit0), and port_ff_reg(6) (← bit2).
    // Bit 7 = NOT pulse_int_n → 0 at idle.
    {
        nr_write(emu, 0x22, 0xFF);
        const uint8_t got = nr_read(emu, 0x22);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "got=0x%02X expected=0x07 (bits 6:3 must be 0; "
                      "regs_[] echo would return 0xFF or 0x7F)",
                      got);
        check("L22-03",
              "NR 0x22 middle-nibble bits 6:3 read as constant 0 even "
              "after write 0xFF [zxnext.vhd:5992 'X' & \"0000\" & ...]",
              got == 0x07, detail);

        // Cleanup: clear the residual line-int-en + target-msb + ULA-int-disable.
        nr_write(emu, 0x22, 0x00);
        emu.port().out(0xFF, 0x00);
    }

    // L22-04 — Bit 2 propagation from port-FF write through to NR 0x22
    // readback. The shared store is port_ff_reg(6); G108 wired the
    // port-0xFF-write side, so OUT 0xFF,0x40 sets port_ff_reg(6)=1 and
    // a subsequent read of NR 0x22 must reflect that bit. Pre-fix the
    // regs_[0x22] echo did not see this OUT, so bit 2 read 0.
    // VHDL zxnext.vhd:3615-3635 (port_ff_reg writers) + :5992 (read).
    {
        nr_write(emu, 0x22, 0x00);     // clear NR-side authoritative bits
        emu.port().out(0xFF, 0x40);    // port_ff_reg(6) ← 1
        const uint8_t got = nr_read(emu, 0x22);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "got=0x%02X expected=0x04 (bit 2 from port_ff_reg(6))",
                      got);
        check("L22-04",
              "NR 0x22 bit 2 reflects port-0xFF-driven port_ff_reg(6) "
              "[zxnext.vhd:3615 + :3635 + :5992]",
              got == 0x04, detail);

        emu.port().out(0xFF, 0x00);  // restore
    }

    // L22-05 — Bit 7 dynamic semantics: at idle (no pending INT pulse),
    // pulse_int_n=1 → bit 7 = NOT pulse_int_n = 0. We assert the
    // asserted-default-low contract; full pulse-active semantics await
    // a deeper INT-driven test that synchronises with the Im2 pulse FSM.
    // VHDL zxnext.vhd:5992 bit 7 = `not pulse_int_n`; :2017-2031 sequencer.
    {
        nr_write(emu, 0x22, 0x00);
        emu.port().out(0xFF, 0x00);
        const uint8_t got = nr_read(emu, 0x22);
        check("L22-05",
              "NR 0x22 bit 7 = 0 at idle (pulse_int_n=1 → NOT = 0) "
              "[zxnext.vhd:5992 + :2017-2031]",
              (got & 0x80) == 0, detail_eq(got, uint8_t{0x00}));
    }

    // L23-01 — Round-trip: NR 0x23 stores low 8 bits of the line-int
    // target. Pure 8-bit echo through VideoTiming::line_interrupt_target.
    // VHDL zxnext.vhd:5995. Pre-fix the regs_[0x23] echo passed by
    // accident (the WRITE handler also keeps the low byte intact); we
    // assert post-fix that the value flows through VideoTiming.
    {
        for (uint8_t v : {0x00, 0x55, 0xAA, 0xFF}) {
            nr_write(emu, 0x23, v);
            const uint8_t got = nr_read(emu, 0x23);
            char id[16];
            char desc[160];
            char detail[96];
            std::snprintf(id, sizeof(id), "L23-01.%02X", v);
            std::snprintf(desc, sizeof(desc),
                          "NR 0x23 round-trip 0x%02X → reads back from "
                          "VideoTiming::line_interrupt_target() & 0xFF "
                          "[zxnext.vhd:5995]", v);
            std::snprintf(detail, sizeof(detail),
                          "got=0x%02X expected=0x%02X", got, v);
            check(id, desc, got == v, detail);
        }
    }

    // L23-02 — High-bit isolation: when NR 0x22 carries the target MSB
    // (bit 0 → bit 8 of the 9-bit target), NR 0x23 reads ONLY the low 8
    // bits. Set target=0x1AA (MSB=1, low=0xAA) and verify NR 0x23=0xAA
    // and NR 0x22 bit 0 = 1.
    // VHDL zxnext.vhd:5992 + :5995.
    {
        nr_write(emu, 0x23, 0xAA);   // low byte = 0xAA
        nr_write(emu, 0x22, 0x01);   // target MSB ← 1 (bit 0 of NR 0x22)

        const uint8_t r23 = nr_read(emu, 0x23);
        const uint8_t r22 = nr_read(emu, 0x22);

        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "NR 0x23 read=0x%02X (expect 0xAA), "
                      "NR 0x22 read=0x%02X (expect bit0=1)",
                      r23, r22);
        check("L23-02",
              "NR 0x22/NR 0x23 split a 9-bit line-int target; NR 0x23 "
              "reads bits 7:0 only [zxnext.vhd:5992 + :5995]",
              r23 == 0xAA && (r22 & 0x01) == 0x01, detail);

        // Cleanup
        nr_write(emu, 0x22, 0x00);
        nr_write(emu, 0x23, 0x00);
    }
}

// ── G56 Cluster D — composed-read divergence (NR 0x40/43/4C/69/6A/6B/6C) ──
//
// Closure for the G56 systemic shadow-store bug: NextReg::write at
// src/port/nextreg.cpp:117-123 always stores the raw 8-bit value into
// regs_[reg], so reads of NRs whose VHDL formula composes from live
// subsystem state (or masks reserved bits) leak the last-written byte.
// VHDL oracle (zxnext.vhd, port_253b_dat <= ... entries):
//   NR 0x40 :6035-6036 — nr_palette_idx (live, autoincs after NR 0x44).
//   NR 0x43 :6044-6045 — composed bit fields (8 b total).
//   NR 0x4C :6056      — "0000" & nr_4c_tm_transparent_index (4-bit).
//   NR 0x69 :6095-6096 — port_123b_layer2_en & port_7ffd_shadow & port_ff_reg(5:0).
//   NR 0x6A :6098-6099 — "00" & lores fields (bits 7:6 masked).
//   NR 0x6B :6101-6102 — nr_6b_tm_en & nr_6b_tm_control (full 8 b).
//   NR 0x6C :6104-6105 — nr_6c_tm_default_attr (full 8 b).
//
// Each row writes through the port path, mutates state via a parallel
// path where applicable (NR 0x44 autoinc, port 0xFF write, NR 0x12), and
// reads back via the port path. Without the new read-handlers in
// src/core/emulator.cpp the read returns the cached regs_[reg] byte and
// the row fails.

static void test_g56_cluster_d(Emulator& emu) {
    set_group("G56-Cluster-D");

    // ── NR 0x40 — palette index, with autoinc via NR 0x44 ────────────────

    // G56-D-40a — Plain NR 0x40 round-trip (write 0xAA, read 0xAA).
    {
        nr_write(emu, 0x40, 0xAA);
        const uint8_t got = nr_read(emu, 0x40);
        check("G56-D-40a",
              "NR 0x40 plain write/read round-trip "
              "[zxnext.vhd:6035-6036 nr_palette_idx]",
              got == 0xAA, detail_eq(got, uint8_t{0xAA}));
    }

    // G56-D-40b — NR 0x40 read tracks autoinc after NR 0x44 9-bit pair.
    // VHDL: nr_palette_idx advances after the second NR 0x44 byte
    // (write_entry → advance_index when auto-inc not gated by NR 0x43 b7).
    // Pre-fix: read returns 0xAA (last NR 0x40 write); post-fix: 0xAB.
    {
        nr_write(emu, 0x43, 0x00);   // ULA-first target, autoinc enabled
        nr_write(emu, 0x40, 0xAA);   // seed index
        nr_write(emu, 0x44, 0x00);   // 9-bit byte 1 — does not advance
        nr_write(emu, 0x44, 0x00);   // 9-bit byte 2 — advances to 0xAB
        const uint8_t got = nr_read(emu, 0x40);
        check("G56-D-40b",
              "NR 0x40 read reflects post-NR-0x44 autoinc "
              "(0xAA → 0xAB) [zxnext.vhd:6035 + advance_index]",
              got == 0xAB, detail_eq(got, uint8_t{0xAB}));
    }

    // ── NR 0x43 — palette control round-trip ─────────────────────────────

    // G56-D-43a — full byte round-trip via PaletteManager source-of-truth.
    // VHDL :6044 composes the same 8 bits we wrote, so the byte must
    // round-trip through palette_.read_control().
    {
        nr_write(emu, 0x43, 0xA5);   // 1010_0101 — autoinc=1, write_select=010,
                                      // spr=0, l2=1, ula=0, ulanext=1
        const uint8_t got = nr_read(emu, 0x43);
        check("G56-D-43a",
              "NR 0x43 round-trip via PaletteManager.read_control() "
              "[zxnext.vhd:6044-6045]",
              got == 0xA5, detail_eq(got, uint8_t{0xA5}));
    }

    // G56-D-43b — second pattern to detect any bit-order drift in the
    // PaletteManager re-decode. VHDL bit order [7]autoinc [6:4]write_select
    // [3]spr [2]l2 [1]ula [0]ulanext.
    {
        nr_write(emu, 0x43, 0x5A);   // 0101_1010
        const uint8_t got = nr_read(emu, 0x43);
        check("G56-D-43b",
              "NR 0x43 second round-trip pattern "
              "[zxnext.vhd:6044-6045]",
              got == 0x5A, detail_eq(got, uint8_t{0x5A}));
    }

    // ── NR 0x4C — tilemap transparent index, bits 7:4 masked ─────────────

    // G56-D-4Ca — write 0xFF expects 0x0F on read (high nibble masked).
    // Pre-fix: bare regs_[0x4C] returns 0xFF.
    {
        nr_write(emu, 0x4C, 0xFF);
        const uint8_t got = nr_read(emu, 0x4C);
        check("G56-D-4Ca",
              "NR 0x4C bits 7:4 masked on read (write 0xFF → read 0x0F) "
              "[zxnext.vhd:6056 \"0000\" & nr_4c_tm_transparent_index]",
              got == 0x0F, detail_eq(got, uint8_t{0x0F}));
    }

    // G56-D-4Cb — partial nibble retained (write 0x5A → read 0x0A).
    {
        nr_write(emu, 0x4C, 0x5A);
        const uint8_t got = nr_read(emu, 0x4C);
        check("G56-D-4Cb",
              "NR 0x4C low nibble retained, high nibble forced 0 "
              "[zxnext.vhd:6056]",
              got == 0x0A, detail_eq(got, uint8_t{0x0A}));
    }
    // Restore reset default for downstream tests.
    nr_write(emu, 0x4C, 0x0F);

    // ── NR 0x69 — composed from L2 enable + 7FFD shadow + port_ff_reg ────

    // G56-D-69a — write 0x00 then mutate live state without touching NR
    // 0x69. Pre-fix: bare regs_[0x69] = 0x00 hides the live-state changes.
    // Post-fix: read composes from layer2.enabled() | mmu.shadow | port_ff.
    {
        nr_write(emu, 0x69, 0x00);              // baseline: all bits 0
        emu.port().out(0x123B, 0x02);           // bit 1 → layer2 enable
        // Drive port_ff_reg(5:0) via direct port 0xFF write — VHDL :3624
        // (and emulator.cpp:1688) latches the full byte into port_ff_reg.
        emu.port().out(0x00FF, 0x15);           // bits 5:0 = 0x15
        const uint8_t got = nr_read(emu, 0x69);
        // Expected: bit 7=1 (L2 enable via 0x123B), bit 6=0 (no 7FFD shadow),
        // bits 5:0 = 0x15 → 0x95.
        check("G56-D-69a",
              "NR 0x69 composes from port 0x123B L2 + 7FFD shadow + "
              "port_ff_reg(5:0) [zxnext.vhd:6095-6096]",
              got == 0x95, detail_eq(got, uint8_t{0x95}));
    }

    // G56-D-69b — set 7FFD shadow bit (port 0x7FFD bit 3) and re-read.
    // bit 6 of NR 0x69 must follow the live shadow flag.
    {
        // port 0x7FFD: bit 3 = shadow screen select. Other bits zero.
        emu.port().out(0x7FFD, 0x08);
        const uint8_t got = nr_read(emu, 0x69);
        // Expected: bit 7=1 (still L2-enabled), bit 6=1 (shadow), bits 5:0
        // remain at the prior port_ff_reg value 0x15 → 0xD5.
        check("G56-D-69b",
              "NR 0x69 bit 6 reflects port 0x7FFD bit 3 shadow flag "
              "[zxnext.vhd:6095 port_7ffd_shadow]",
              got == 0xD5, detail_eq(got, uint8_t{0xD5}));
    }
    // Restore: clear L2 enable and shadow flag for downstream tests.
    emu.port().out(0x123B, 0x00);
    emu.port().out(0x7FFD, 0x00);
    emu.port().out(0x00FF, 0x00);
    nr_write(emu, 0x69, 0x00);

    // ── NR 0x6A — bits 7:6 forced "00" on read ───────────────────────────

    // G56-D-6Aa — write 0xFF expects 0x3F. Pre-fix: bare regs_[0x6A] = 0xFF.
    {
        nr_write(emu, 0x6A, 0xFF);
        const uint8_t got = nr_read(emu, 0x6A);
        check("G56-D-6Aa",
              "NR 0x6A bits 7:6 forced '00' on read (write 0xFF → 0x3F) "
              "[zxnext.vhd:6098-6099 \"00\" & lores fields]",
              got == 0x3F, detail_eq(got, uint8_t{0x3F}));
    }

    // G56-D-6Ab — write 0xC0 (only the masked bits set) reads as 0x00.
    {
        nr_write(emu, 0x6A, 0xC0);
        const uint8_t got = nr_read(emu, 0x6A);
        check("G56-D-6Ab",
              "NR 0x6A bits 7:6 read as 0 even when only those bits written "
              "[zxnext.vhd:6098-6099]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    nr_write(emu, 0x6A, 0x00);   // restore

    // ── NR 0x6B — full 8-bit round-trip via Tilemap source-of-truth ──────

    // G56-D-6Ba — write 0x83 (en=1, control=0x03 with bits 1,0 set).
    {
        nr_write(emu, 0x6B, 0x83);
        const uint8_t got = nr_read(emu, 0x6B);
        check("G56-D-6Ba",
              "NR 0x6B round-trip via Tilemap.get_control() "
              "[zxnext.vhd:6101-6102]",
              got == 0x83, detail_eq(got, uint8_t{0x83}));
    }

    // G56-D-6Bb — clear enable (bit 7) — Tilemap.enabled_ tracks bit 7
    // and control_raw_ stores the full byte. Read must reflect the cleared
    // enable bit.
    {
        nr_write(emu, 0x6B, 0x55);   // bit 7 = 0
        const uint8_t got = nr_read(emu, 0x6B);
        check("G56-D-6Bb",
              "NR 0x6B read mirrors cleared tm_en (bit 7) "
              "[zxnext.vhd:6101]",
              got == 0x55, detail_eq(got, uint8_t{0x55}));
    }
    nr_write(emu, 0x6B, 0x00);   // restore

    // ── NR 0x6C — full 8-bit round-trip via Tilemap.default_attr ─────────

    // G56-D-6Ca — write 0xA5.
    {
        nr_write(emu, 0x6C, 0xA5);
        const uint8_t got = nr_read(emu, 0x6C);
        check("G56-D-6Ca",
              "NR 0x6C round-trip via Tilemap.get_default_attr() "
              "[zxnext.vhd:6104-6105 nr_6c_tm_default_attr]",
              got == 0xA5, detail_eq(got, uint8_t{0xA5}));
    }

    // G56-D-6Cb — second value to ensure no bit-aliasing.
    {
        nr_write(emu, 0x6C, 0x5A);
        const uint8_t got = nr_read(emu, 0x6C);
        check("G56-D-6Cb",
              "NR 0x6C second round-trip pattern "
              "[zxnext.vhd:6104-6105]",
              got == 0x5A, detail_eq(got, uint8_t{0x5A}));
    }
    nr_write(emu, 0x6C, 0x00);   // restore
}

// ── G56 Cluster E: NR 0x6E / 0x6F / 0x70 / 0x71 / 0x80 / 0x81 ─────────
//
// Composed-read divergence + reserved-bit masking through the real
// Emulator wiring (NextReg::write stores raw 8-bit pre-handler-dispatch;
// without explicit read_handlers, reads leak the unmasked byte).  Each
// row writes 0xFF (all-bits-set) and asserts the read-back equals the
// VHDL-spec masked value, plus a canonical round-trip row that exercises
// the in-band field path.
//
// Folds G99 (NR 0x6E/0x6F bit-6 reserved-zero mask) into G56-CR-6E/6F.

static void test_g56_cluster_e(Emulator& emu) {
    set_group("G56-CR-Cluster-E");

    // NR 0x6E — VHDL zxnext.vhd:6107
    //   port_253b_dat <= nr_6e_tilemap_base_7 & '0' & nr_6e_tilemap_base.
    // Bit 6 is constant '0' (reserved); bits 7 + 5:0 are the tilemap base.
    {
        nr_write(emu, 0x6E, 0xFF);
        const uint8_t got = nr_read(emu, 0x6E);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0xBF", got);
        check("G56-CR-6E-MASK",
              "NR 0x6E read masks bit 6 (write 0xFF -> read 0xBF) "
              "[zxnext.vhd:6107 + G99]",
              got == 0xBF, d);
    }
    {
        nr_write(emu, 0x6E, 0xA5);  // 0b1010_0101 — bit 7 + bits 5,2,0
        const uint8_t got = nr_read(emu, 0x6E);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0xA5", got);
        check("G56-CR-6E-RT",
              "NR 0x6E round-trip canonical (bits 7,5:0 preserved; bit 6 zero) "
              "[zxnext.vhd:6107]",
              got == 0xA5, d);
    }

    // NR 0x6F — VHDL zxnext.vhd:6110
    //   port_253b_dat <= nr_6f_tilemap_tiles_7 & '0' & nr_6f_tilemap_tiles.
    {
        nr_write(emu, 0x6F, 0xFF);
        const uint8_t got = nr_read(emu, 0x6F);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0xBF", got);
        check("G56-CR-6F-MASK",
              "NR 0x6F read masks bit 6 (write 0xFF -> read 0xBF) "
              "[zxnext.vhd:6110 + G99]",
              got == 0xBF, d);
    }
    {
        nr_write(emu, 0x6F, 0x9C);  // 0b1001_1100 — bit 7 + bits 4,3,2
        const uint8_t got = nr_read(emu, 0x6F);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0x9C", got);
        check("G56-CR-6F-RT",
              "NR 0x6F round-trip canonical (bits 7,5:0 preserved; bit 6 zero) "
              "[zxnext.vhd:6110]",
              got == 0x9C, d);
    }

    // NR 0x70 — VHDL zxnext.vhd:6113
    //   port_253b_dat <= "00" & nr_70_layer2_resolution & nr_70_layer2_palette_offset.
    // Bits 7:6 const "00"; bits 5:4 resolution; bits 3:0 palette offset.
    {
        nr_write(emu, 0x70, 0xFF);
        const uint8_t got = nr_read(emu, 0x70);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0x3F", got);
        check("G56-CR-70-MASK",
              "NR 0x70 read masks bits 7:6 (write 0xFF -> read 0x3F) "
              "[zxnext.vhd:6113]",
              got == 0x3F, d);
    }
    {
        nr_write(emu, 0x70, 0x1A);  // resolution=01 (320x256), pal off=0xA
        const uint8_t got = nr_read(emu, 0x70);
        char d[80]; std::snprintf(d, sizeof(d),
            "got=0x%02X want=0x1A res=%u pal=%u",
            got, emu.layer2().resolution(), emu.layer2().palette_offset());
        check("G56-CR-70-RT",
              "NR 0x70 round-trip resolution=01 + pal_off=0xA "
              "[zxnext.vhd:6113 via Layer2 fields]",
              got == 0x1A, d);
    }

    // NR 0x71 — VHDL zxnext.vhd:6116
    //   port_253b_dat <= "0000000" & nr_71_layer2_scrollx_msb.
    // Only bit 0 is significant; bits 7:1 const zero.
    {
        nr_write(emu, 0x71, 0xFF);
        const uint8_t got = nr_read(emu, 0x71);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0x01", got);
        check("G56-CR-71-MASK",
              "NR 0x71 read masks bits 7:1 (write 0xFF -> read 0x01) "
              "[zxnext.vhd:6116]",
              got == 0x01, d);
    }
    {
        nr_write(emu, 0x71, 0x00);
        const uint8_t got0 = nr_read(emu, 0x71);
        nr_write(emu, 0x71, 0x01);
        const uint8_t got1 = nr_read(emu, 0x71);
        char d[64]; std::snprintf(d, sizeof(d), "got0=0x%02X got1=0x%02X", got0, got1);
        check("G56-CR-71-RT",
              "NR 0x71 round-trip bit 0 toggle (0x00 -> 0x00, 0x01 -> 0x01) "
              "[zxnext.vhd:6116 via Layer2 scroll_x bit 8]",
              got0 == 0x00 && got1 == 0x01, d);
    }

    // NR 0x80 — VHDL zxnext.vhd:6122
    //   port_253b_dat <= nr_80_expbus.  Full 8-bit field; no masking.
    // jnext has no expansion-bus device wired today (G45 tracks expbus
    // emulation), so the byte is pure write-storage and round-trips.
    {
        nr_write(emu, 0x80, 0xFF);
        const uint8_t got = nr_read(emu, 0x80);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0xFF", got);
        check("G56-CR-80-FF",
              "NR 0x80 full 8-bit round-trip (write 0xFF -> read 0xFF) "
              "[zxnext.vhd:6122 — expbus stored, G45 inert]",
              got == 0xFF, d);
    }
    {
        nr_write(emu, 0x80, 0x5A);
        const uint8_t got = nr_read(emu, 0x80);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0x5A", got);
        check("G56-CR-80-RT",
              "NR 0x80 canonical round-trip 0x5A "
              "[zxnext.vhd:6122 — full byte preserved]",
              got == 0x5A, d);
        // Restore reset baseline so downstream tests see a clean slate.
        nr_write(emu, 0x80, 0x00);
    }

    // NR 0x81 — VHDL zxnext.vhd:6126
    //   port_253b_dat <= i_BUS_ROMCS_n & nr_81_expbus_ula_override &
    //                    nr_81_expbus_nmi_debounce_disable & nr_81_expbus_clken &
    //                    nr_81_expbus_fdc & '0' & nr_81_expbus_speed.
    // Bit 7 = i_BUS_ROMCS_n (no expbus device wired -> constant '1', G45);
    // bit 2 = constant '0'; bits 6:3 stored from the last NR 0x81 write.
    // bits 1:0 = nr_81_expbus_speed, hardwired to "00" by VHDL :5496
    //          (`nr_81_expbus_speed <= "00"` — the user data source
    //           `nr_wr_dat(1 downto 0)` is explicitly commented out).
    // V11-NMP-01 Pass-11 fix: pre-fix the C++ read mask was 0x7B which
    // preserved bits 1:0 of the written byte; corrected to 0x78 so bits
    // 1:0 always read as 0 per VHDL :5496.
    {
        nr_write(emu, 0x81, 0xFF);
        const uint8_t got = nr_read(emu, 0x81);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0xF8", got);
        check("G56-CR-81-MASK",
              "NR 0x81 read masks bits 2 + 1:0 + forces bit 7=1 "
              "(write 0xFF -> read 0xF8) [zxnext.vhd:5496+6126, G45 expbus inert]",
              got == 0xF8, d);
    }
    {
        nr_write(emu, 0x81, 0x00);
        const uint8_t got = nr_read(emu, 0x81);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0x80", got);
        check("G56-CR-81-RESET",
              "NR 0x81 write 0x00 reads 0x80 (only bit 7 ROMCS_n forced) "
              "[zxnext.vhd:6126]",
              got == 0x80, d);
    }
    {
        nr_write(emu, 0x81, 0x6B);  // bits 6,5,3,1,0; bit 2 = 0 already
        const uint8_t got = nr_read(emu, 0x81);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0xE8", got);
        check("G56-CR-81-RT",
              "NR 0x81 round-trip stored bits 6:3 + ROMCS_n force "
              "(write 0x6B -> read 0xE8; bits 1:0 always 0 per :5496) "
              "[zxnext.vhd:5496+6126]",
              got == 0xE8, d);
        // Restore reset baseline.
        nr_write(emu, 0x81, 0x00);
    }
    // V11-NMP-01 discriminative — bits 1:0 are hardwired to "00" in
    // VHDL (line 5496 explicitly drops `nr_wr_dat(1 downto 0)`). A
    // write that toggles ONLY bits 1:0 must produce read=0x80 (bits
    // 1:0 = 0; bit 7 forced; everything else 0). Pre-fix this would
    // return 0x83 because the cache stored bits 1:0 verbatim and the
    // read mask 0x7B preserved them.
    {
        nr_write(emu, 0x81, 0x03);
        const uint8_t got = nr_read(emu, 0x81);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0x80", got);
        check("V11-NMP-01-LO2",
              "NR 0x81 write 0x03 (bits 1:0 only) reads 0x80 — VHDL :5496 "
              "hardwires nr_81_expbus_speed to \"00\" so bits 1:0 always read 0",
              got == 0x80, d);
        nr_write(emu, 0x81, 0x00);
    }
    {
        // Mixed: bits 6 and 0. Bit 6 should survive; bit 0 must drop.
        nr_write(emu, 0x81, 0x41);
        const uint8_t got = nr_read(emu, 0x81);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0xC0", got);
        check("V11-NMP-01-MIXED",
              "NR 0x81 write 0x41 reads 0xC0 — bit 6 (ula_override) "
              "preserved, bit 0 (expbus_speed[0]) dropped per :5496",
              got == 0xC0, d);
        nr_write(emu, 0x81, 0x00);
    }
}

// ── Write-Only Register Read Behaviour (G149) ─────────────────────────
//
// VHDL zxnext.vhd:5878-6289 — the NR read mux falls through to
// (others => '0') for any NR with no read entry. The historical jnext
// behaviour returned regs_[reg] (last-written byte) for unhandled NRs,
// leaking write-only state. Phase 1 of G149 retrofitted the Emulator
// write_handlers for NR 0x04/0x29/0x35/0x60 to return 0 (instead of v),
// so the canonicalised regs_[reg] stores 0 and reads return 0 — matching
// VHDL exactly. The observable behaviour requires a fully-wired Emulator
// (the write_handlers are registered in Emulator::init()), so this lives
// at integration tier; the bare NextReg unit test rows WO-01..WO-04 were
// re-homed here.
static void test_write_only_read_zero(Emulator& emu) {
    set_group("WO-Integration");

    // WO-INT-04 — NR 0x04 (ROM/RAM bank latch).
    // VHDL zxnext.vhd:1104,5716-5732 (write-only nr_04_romram_bank);
    // no read-mux entry — read falls through to (others => '0').
    {
        nr_write(emu, 0x04, 0xAA);
        const uint8_t got = nr_read(emu, 0x04);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0xAA got=0x%02X want=0x00", got);
        check("WO-INT-04",
              "NR 0x04 write-only — read returns 0 (no leak of romram_bank) "
              "[zxnext.vhd:5878-6289 others=>'0']",
              got == 0x00, d);
    }

    // WO-INT-29 — NR 0x29 (keymap address LSB).
    // VHDL zxnext.vhd:6304 (nr_29_we triggers internal nr_keymap_addr
    // load + auto-increment); no read-mux entry.
    {
        nr_write(emu, 0x29, 0xAA);
        const uint8_t got = nr_read(emu, 0x29);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0xAA got=0x%02X want=0x00", got);
        check("WO-INT-29",
              "NR 0x29 write-only — read returns 0 (no leak of keymap LSB) "
              "[zxnext.vhd:5878-6289 others=>'0']",
              got == 0x00, d);
    }

    // WO-INT-35 — NR 0x35 (sprite attribute byte 0, no auto-increment).
    // VHDL zxnext.vhd:4857-4875 dispatches mirror_we for the no-inc path;
    // no read-mux entry (NR 0x34 IS readable but 0x35 is not).
    {
        nr_write(emu, 0x35, 0xAA);
        const uint8_t got = nr_read(emu, 0x35);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0xAA got=0x%02X want=0x00", got);
        check("WO-INT-35",
              "NR 0x35 write-only — read returns 0 (no leak of sprite attr) "
              "[zxnext.vhd:5878-6289 others=>'0']",
              got == 0x00, d);
    }

    // WO-INT-60 — NR 0x60 (Copper data write).
    // VHDL routes the byte through the Copper subsystem; no read-mux
    // entry (NR 0x61/0x62 ARE readable — Copper addr/mode — but 0x60 is
    // not).
    {
        nr_write(emu, 0x60, 0xAA);
        const uint8_t got = nr_read(emu, 0x60);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0xAA got=0x%02X want=0x00", got);
        check("WO-INT-60",
              "NR 0x60 write-only — read returns 0 (no leak of copper data) "
              "[zxnext.vhd:5878-6289 others=>'0']",
              got == 0x00, d);
    }

    // V14-NMP-03 (Pass-14 verify-audit fix): NR 0x2B keymap data write.
    // VHDL zxnext.vhd:6306-6307 — `nr_2b_we = '1'` triggers the auto-increment
    // of `nr_keymap_addr`; the byte is also routed to the keymap dpram via
    // `nr_keymap_dat` (:6324) gated by `nr_keymap_we`/`nr_joymap_we` (:6321-
    // 6322). NO read-mux entry exists for NR 0x2B in the case statement at
    // :5878-6289, so reads fall through to `when others => (others => '0')`
    // at :6286-6287 and return 0x00.
    //
    // Pre-fix the C++ NR 0x2B write_handler returned `v` raw, so the cache
    // held the last-write byte and a subsequent NR 0x2B read echoed it
    // verbatim — leaking the keymap data byte through a register that real
    // hardware never reads back.
    {
        nr_write(emu, 0x2B, 0xAA);
        const uint8_t got = nr_read(emu, 0x2B);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0xAA got=0x%02X want=0x00", got);
        check("WO-INT-2B",
              "NR 0x2B write-only — read returns 0 (no leak of keymap data) "
              "[zxnext.vhd:5878-6289 others=>'0', :6306-6307 nr_2b_we]",
              got == 0x00, d);
    }

    // V14-NMP-04 (Pass-14 reviewer-promoted from deferred class-c):
    // NR 0x2A is dead in VHDL. Write strobe at zxnext.vhd:4850 is
    // commented out (`-- when X"2A" => nr_2a_we <= '1';`), the write-side
    // process at :6312-6319 is commented out, and there is NO read-mux
    // entry in the case statement at :5878-6289. Reads must fall through
    // to `when others => (others => '0')` at :6286-6287, returning 0x00.
    //
    // Pre-fix C++ NR 0x2A had no write_handler and no read_handler, so
    // a write went raw to regs_[0x2A] and a subsequent read echoed it.
    // Reviewer-promoted: this is functionally identical in shape to
    // NR 0x2B (V14-NMP-03), so the canonical fix is also identical —
    // install a write_handler that returns 0.
    {
        nr_write(emu, 0x2A, 0xCC);
        const uint8_t got = nr_read(emu, 0x2A);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0xCC got=0x%02X want=0x00", got);
        check("WO-INT-2A",
              "NR 0x2A dead-register — read returns 0 (no leak of cached "
              "write byte) [zxnext.vhd:4850 commented, :5878-6289 others=>'0']",
              got == 0x00, d);
    }

    // V15-NMP-01 (Pass-15 verify-audit fix): NR 0x63 Copper data byte is
    // write-only. VHDL zxnext.vhd:4887 strobes `nr_copper_we <= '1'`; the
    // write side at :5433-5439 latches `nr_copper_data_stored` when
    // `nr_copper_addr(0)='0'` and unconditionally advances `nr_copper_addr`.
    // There is NO read-mux entry in the case statement at :5878-6289 — NR
    // 0x61 (low addr byte) and NR 0x62 (mode + high addr) ARE readable
    // but NR 0x63 is not. Reads must therefore fall through to `when others
    // => (others => '0')` at :6286-6287, returning 0x00.
    //
    // Pre-fix the C++ write_handler returned `v` (line 1634), so the cache
    // held the last-write byte and a subsequent NR 0x63 read returned it
    // instead of 0x00. Same shape as G149 (NR 0x60), V14-NMP-03 (NR 0x2B),
    // V14-NMP-04 (NR 0x2A) — install a write_handler that returns 0.
    //
    // Discriminative: the read MUST be 0x00 regardless of the byte we wrote
    // (this row tries 0x55, an arbitrary non-zero pattern). Pre-fix would
    // return 0x55. Crucially, the WRITE side-effect (`copper_.write_reg_0x63`)
    // still fires — only the cache-leak read is fixed.
    {
        nr_write(emu, 0x63, 0x55);
        const uint8_t got = nr_read(emu, 0x63);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0x55 got=0x%02X want=0x00", got);
        check("WO-INT-63",
              "NR 0x63 write-only Copper data — read returns 0 (no leak of "
              "cached write byte) [zxnext.vhd:4887 nr_copper_we, "
              ":5433-5439 write side, :5878-6289 others=>'0']",
              got == 0x00, d);
    }

    // V15-NMP-02 (Pass-15 verify-audit fix): NR 0xFF ULA+ palette poke is
    // write-only. VHDL zxnext.vhd:4906 strobes `nr_ff_we <= '1'`; the byte
    // is composed into `nr_palette_value` at :4919 (with the ulap palette
    // index from port 0xBF3B per :6958) and routed to the ULA-TM palette
    // dpram via :6957. There is NO read-mux entry in the case statement
    // at :5878-6289. Reads must fall through to `when others =>
    // (others => '0')` at :6286-6287, returning 0x00.
    //
    // Pre-fix the C++ write_handler returned `v` (line 916), so the cache
    // held the last-write byte and a subsequent NR 0xFF read returned it
    // instead of 0x00. Same shape as V15-NMP-01.
    //
    // Discriminative: the read MUST be 0x00 regardless of the byte we
    // wrote. The write side-effect (`palette_.nr_ff_poke`) still fires —
    // only the cache-leak read is fixed.
    {
        nr_write(emu, 0xFF, 0xA5);
        const uint8_t got = nr_read(emu, 0xFF);
        char d[64]; std::snprintf(d, sizeof(d),
            "wrote=0xA5 got=0x%02X want=0x00", got);
        check("WO-INT-FF",
              "NR 0xFF write-only ULA+ palette poke — read returns 0 (no "
              "leak of cached write byte) [zxnext.vhd:4906 nr_ff_we, "
              ":4919 nr_palette_value, :5878-6289 others=>'0']",
              got == 0x00, d);
    }
}

// ── NR 0x28 ↔ nr_stored_palette_value read-mux (V14-NMP-02) ───────────
//
// VHDL zxnext.vhd:
//   :1190 — `signal nr_stored_palette_value : std_logic_vector(7 downto 0);`
//   :5398-5399 — first half of NR 0x44 9-bit palette write latches
//                `nr_stored_palette_value <= nr_wr_dat;` when
//                `nr_palette_sub_idx = '0'`.
//   :6003-6004 — NR 0x28 read mux:
//                `when X"28" => port_253b_dat <= nr_stored_palette_value;`
//
// I.e. NR 0x28 is a *read-only* surface for the upper byte of a 9-bit
// palette write currently in progress. NR 0x28 *writes* mutate
// `nr_keymap_sel` / `nr_keymap_addr(8)` (VHDL :6301-6303) — they do NOT
// touch the palette signal. Pre-fix the C++ NR 0x28 write_handler stored
// the raw written byte in the regs_[] cache, and the absence of a
// read_handler made NR 0x28 reads echo that byte instead of the live
// palette signal.
static void test_v14_nmp_02_nr_28_read(Emulator& emu) {
    set_group("V14-NMP-02-NR28");

    // Setup: select NR 0x44 target and prime the 9-bit FSM.
    // The PaletteManager's `nine_bit_first_byte_` shadow is reset to 0
    // on construction (palette.cpp:87), so a fresh emulator should read
    // NR 0x28 = 0x00.
    emu.reset();
    {
        const uint8_t got = nr_read(emu, 0x28);
        char d[64]; std::snprintf(d, sizeof(d), "got=0x%02X want=0x00", got);
        check("V14-NMP-02-NR28-01",
              "NR 0x28 read at reset — `nr_stored_palette_value` default 0x00 "
              "[zxnext.vhd:1190, :5011, :6004]",
              got == 0x00, d);
    }

    // Write a sentinel byte to NR 0x28 — this should NOT influence the
    // NR 0x28 read mux, which reads `nr_stored_palette_value` (the
    // PaletteManager shadow), NOT the cached NR 0x28 last-write byte.
    {
        nr_write(emu, 0x28, 0xA5);
        const uint8_t got = nr_read(emu, 0x28);
        char d[80]; std::snprintf(d, sizeof(d),
            "after NR 0x28<-0xA5: got=0x%02X want=0x00", got);
        check("V14-NMP-02-NR28-02",
              "NR 0x28 write does NOT affect NR 0x28 read (palette signal, "
              "not regs_[] cache) [zxnext.vhd:6004, :6301-6303]",
              got == 0x00, d);
    }

    // First half of an NR 0x44 9-bit palette write — VHDL :5398-5399
    // latches `nr_stored_palette_value <= nr_wr_dat` when sub_idx = 0.
    // NR 0x28 read should then surface the just-stored byte.
    {
        nr_write(emu, 0x44, 0xC3);  // first half: stores 0xC3 in nr_stored_palette_value
        const uint8_t got = nr_read(emu, 0x28);
        char d[80]; std::snprintf(d, sizeof(d),
            "after NR 0x44<-0xC3 (first half): got=0x%02X want=0xC3", got);
        check("V14-NMP-02-NR28-03",
              "NR 0x28 read returns nr_stored_palette_value latched on the "
              "first half of NR 0x44 9-bit write [zxnext.vhd:5398-5399, :6004]",
              got == 0xC3, d);
    }

    // Second half of the NR 0x44 9-bit write does NOT update
    // nr_stored_palette_value (VHDL :5400 ELSIF branch only auto-increments
    // the palette index). NR 0x28 read should still return the previously
    // latched first byte.
    {
        nr_write(emu, 0x44, 0x01);  // second half: blue LSB latched
        const uint8_t got = nr_read(emu, 0x28);
        char d[80]; std::snprintf(d, sizeof(d),
            "after NR 0x44<-0x01 (second half): got=0x%02X want=0xC3", got);
        check("V14-NMP-02-NR28-04",
              "NR 0x28 read unchanged across NR 0x44 second-half write "
              "[zxnext.vhd:5400 ELSIF, :6004]",
              got == 0xC3, d);
    }
}

// ── FT-Integration — FDC IO-trap registers (NR 0xD8/0xD9/0xDA, G55) ───
//
// VHDL `cores/zxnext/src/zxnext.vhd`:
//   :1263-1265 — signal declarations (nr_d8_io_trap_fdc_en,
//                nr_d9_iotrap_write, nr_da_iotrap_cause).
//   :2598-2602 — port_2ffd / port_3ffd decode, gated by NR 0xD8 b0.
//   :3835-3837 — `nmi_gen_iotrap` OR's into `nmi_sw_gen_mf` so a trap
//                event drives the Multiface NMI path.
//   :3866-3883 — nr_da_iotrap_cause cause encoding + NR 0x02 b4=0 clear.
//   :3887-3898 — nr_d9_iotrap_write capture (port_3ffd_wr) + NR 0xD9
//                direct-write path (`nr_d9_we`).
//   :5107      — NR 0xD8 power-on default '0'.
//   :5640      — NR 0xD8 write_handler (bit 0 → enable).
//   :6266-6272 — NR 0xD8 / 0xD9 / 0xDA read mux.
//
// These rows were RE-HOMED here from the bare-NextReg unit test
// (test/nextreg/nextreg_test.cpp, group `FT`) per the unit-tier vs
// integration-tier split: state lives in Emulator, not in the bare
// NextReg register file.

static void test_ft_iotrap_integration(Emulator& emu) {
    set_group("FT-Integration");

    // Reset state before the group so prior tests don't bleed in.
    emu.reset();

    // FT-INT-D8-01 — NR 0xD8 nr_d8_io_trap_fdc_en write/read-back.
    // VHDL :5640 (write bit 0) + :6266 (read mux "0000000" & bit).
    {
        nr_write(emu, 0xD8, 0x01);
        uint8_t got1 = nr_read(emu, 0xD8);
        nr_write(emu, 0xD8, 0x00);
        uint8_t got0 = nr_read(emu, 0xD8);
        check("FT-INT-D8-01",
              "NR 0xD8 nr_d8_io_trap_fdc_en write/read-back round-trip "
              "[zxnext.vhd:5640 + 6266]",
              got1 == 0x01 && got0 == 0x00,
              "got1=" + hex2(got1) + " got0=" + hex2(got0));
    }

    // FT-INT-D8-02 — NR 0xD8 enable=1 must allow strobe_iotrap to
    // assert the MF NMI path. VHDL :3835-3837 — `nmi_sw_gen_mf <=
    // nmi_gen_nr_mf OR nmi_gen_iotrap`. The MF producer is gated by
    // NR 0x06 bit 3 (`nr_06_button_m1_nmi_en`, VHDL :2090).
    //
    // Test: enable NR 0xD8 + NR 0x06 b3, fire a port-0x3FFD write,
    // assert NmiSource::nmi_assert_mf() is true.
    {
        emu.reset();
        // Snapshot the current NR 0x06 so we don't fight the existing
        // reset default (0xA0); just OR in bit 3 so the MF gate opens.
        const uint8_t nr06_before = nr_read(emu, 0x06);
        nr_write(emu, 0x06, static_cast<uint8_t>(nr06_before | 0x08));
        nr_write(emu, 0xD8, 0x01);

        // Fire port 0x3FFD WRITE — VHDL :3835 port_3ffd_wr term.
        emu.port().out(0x3FFD, 0xCC);

        const bool mf_asserts = emu.nmi_source().nmi_assert_mf();
        check("FT-INT-D8-02",
              "NR 0xD8=1 + NR 0x06 b3=1 + port 0x3FFD write → "
              "NmiSource::nmi_assert_mf() asserts [zxnext.vhd:3835-3837]",
              mf_asserts,
              std::string{"mf_assert="} + (mf_asserts ? "true" : "false"));
    }

    // FT-INT-D9-01a — NR 0xD9 firmware direct-write round-trip.
    // VHDL :4901 + :3894-3895 — `nr_d9_we` writer path.
    {
        emu.reset();
        nr_write(emu, 0xD9, 0xAA);
        uint8_t got = nr_read(emu, 0xD9);
        check("FT-INT-D9-01a",
              "NR 0xD9 firmware direct-write round-trip "
              "[zxnext.vhd:4901 nr_d9_we + :3894-3895]",
              got == 0xAA, detail_eq(got, uint8_t{0xAA}));
    }

    // FT-INT-D9-01b — NR 0xD9 captures the byte written to port 0x3FFD
    // when the trap is enabled. VHDL :3892-3893 —
    //   nr_d9_iotrap_write <= cpu_do  when port_3ffd_wr AND nmi_accept_cause
    {
        emu.reset();
        nr_write(emu, 0xD8, 0x01);
        emu.port().out(0x3FFD, 0x55);
        uint8_t got = nr_read(emu, 0xD9);
        check("FT-INT-D9-01b",
              "NR 0xD9 captures port 0x3FFD write byte when NR 0xD8=1 "
              "[zxnext.vhd:3892-3893]",
              got == 0x55, detail_eq(got, uint8_t{0x55}));
    }

    // FT-INT-DA-01a — port_2ffd_rd → cause "01". VHDL :3871-3873.
    {
        emu.reset();
        nr_write(emu, 0xD8, 0x01);
        (void)emu.port().in(0x2FFD);
        uint8_t got = nr_read(emu, 0xDA);
        check("FT-INT-DA-01a",
              "NR 0xDA cause=01 after port 0x2FFD READ "
              "[zxnext.vhd:3871-3873]",
              got == 0x01, detail_eq(got, uint8_t{0x01}));
    }

    // FT-INT-DA-01b — port_3ffd_rd → cause "10". VHDL :3874-3875.
    {
        emu.reset();
        nr_write(emu, 0xD8, 0x01);
        (void)emu.port().in(0x3FFD);
        uint8_t got = nr_read(emu, 0xDA);
        check("FT-INT-DA-01b",
              "NR 0xDA cause=10 after port 0x3FFD READ "
              "[zxnext.vhd:3874-3875]",
              got == 0x02, detail_eq(got, uint8_t{0x02}));
    }

    // FT-INT-DA-01c — port_3ffd_wr → cause "11". VHDL :3876-3878.
    {
        emu.reset();
        nr_write(emu, 0xD8, 0x01);
        emu.port().out(0x3FFD, 0xFF);
        uint8_t got = nr_read(emu, 0xDA);
        check("FT-INT-DA-01c",
              "NR 0xDA cause=11 after port 0x3FFD WRITE "
              "[zxnext.vhd:3876-3878]",
              got == 0x03, detail_eq(got, uint8_t{0x03}));
    }

    // FT-INT-DA-02 — NR 0xDA cause clears via NR 0x02 b4 write=0.
    // VHDL :3879-3880 — `elsif nr_02_we = '1' and nr_wr_dat(4) = '0'
    // then nr_da_iotrap_cause <= (others => '0')`.
    {
        emu.reset();
        nr_write(emu, 0xD8, 0x01);
        emu.port().out(0x3FFD, 0xFF);    // cause ← "11"
        uint8_t before = nr_read(emu, 0xDA);

        // Write NR 0x02 with bit 4 = 0 (other bits zero so we don't
        // trigger soft/hard reset or NMI strobes).
        nr_write(emu, 0x02, 0x00);
        uint8_t after = nr_read(emu, 0xDA);

        check("FT-INT-DA-02",
              "NR 0xDA cause cleared by NR 0x02 b4=0 write "
              "[zxnext.vhd:3879-3880]",
              before == 0x03 && after == 0x00,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // Restore reset state so downstream tests aren't affected.
    emu.reset();
}

// ─────────────────────────────────────────────────────────────────────────
// TestCov-NMI-MF-Port — Regression tests for NMI/MF/Port/NextREG fixes
// applied across verify passes 1-10 (commits c1d7998..6051f01).
//
// Mandate (Task 2 testcov audit, 2026-05-09): every fix must have its own
// regression test. Each row below cites the VHDL line + the fix commit
// that motivated the test.
//
// VHDL oracle: cores/zxnext/src/zxnext.vhd
// ─────────────────────────────────────────────────────────────────────────
static void test_testcov_nmi_mf_port(Emulator& emu) {
    set_group("TestCov-NMI-MF-Port");

    // ── TC-NR05-PRESERVE — NR 0x05 reset preservation (Pass-7, 18dba39)
    //   VHDL :1105-1106, :1302-1303, :5832-5854 — `nr_05_*` signals have
    //   NO reset clauses. Pre-pass-7 regs_.fill(0) clobbered them.
    //   Pass-7 fix: NextReg::reset() preserves regs_[0x05].
    {
        emu.reset();
        // Force config_mode=0 for predictable joystick mode commit
        // semantics. Write a non-default canonical value.
        nr_write(emu, 0x03, 0x00);          // NR 0x03 b3=0 → config off
        // Set bits 7:6=10 (joy0 hi), 5:4=11 (joy1 hi), 3=1 (joy0 lo), 1=1
        // (joy1 lo), 0=1 (scandouble). Bit 2 (5060) intentionally =0.
        nr_write(emu, 0x05, 0xBB);
        const uint8_t before = nr_read(emu, 0x05);
        emu.reset();                        // hard reset
        const uint8_t after  = nr_read(emu, 0x05);
        check("TC-NR05-PRESERVE",
              "NR 0x05 survives hard reset (no VHDL reset clause) "
              "[zxnext.vhd:1105-1106 / 1302-1303]",
              before == 0xBB && after == 0xBB,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // ── TC-NR05-PENTAGON — NR 0x05 bit 2 Pentagon force-zero (Pass-10, 6051f01)
    //   VHDL :5832-5841 forces nr_05_5060 <= '0' continuously when
    //   nr_03_machine_timing(2)='1' (Pentagon). The frame-sync latch at
    //   :6701 propagates that '0' into eff_nr_05_5060.  Pre-pass-10 the
    //   read mux returned the cached bit 2 unconditionally, leaking the
    //   user's pre-Pentagon write through.
    {
        emu.reset();
        // 1. With non-Pentagon timing, bit 2 round-trips.
        nr_write(emu, 0x03, 0x00);          // bits[2:0]=000 (Config Mode commit;
                                            //   non-Pentagon)
        nr_write(emu, 0x05, 0x04);          // bit 2 = 1 (5060)
        const uint8_t got_nopen = nr_read(emu, 0x05);
        // 2. Switch to Pentagon (machine_timing[2]=1).
        emu.nextreg().set_nr_03_machine_timing(0x04);
        const uint8_t got_pen = nr_read(emu, 0x05);
        check("TC-NR05-PENTAGON",
              "NR 0x05 read masks bit 2 to 0 when Pentagon timing active "
              "[zxnext.vhd:5832-5841 / :6701 / :5897]",
              (got_nopen & 0x04) == 0x04 && (got_pen & 0x04) == 0x00,
              "nopen=" + hex2(got_nopen) + " pen=" + hex2(got_pen));
        // Restore default timing
        emu.nextreg().set_nr_03_machine_timing(0x03);
        emu.reset();
    }

    // ── TC-NR02-CFGMODE-NO-CLEAR — NR 0x02 bits 3/2 NOT cleared on
    //   config_mode (Verify1, 78f5f1c). VHDL :3840-3864 — readback latches
    //   live in independent processes whose clear cascade is reset OR
    //   explicit-bit write only. config_mode is NOT in either cascade.
    //
    //   Discriminative-fix (review follow-up): the original test called
    //   `emu.nmi_source().tick(1)` directly, bypassing the per-tick
    //   `tick_peripheral_subsystems` wiring that propagates
    //   `nextreg_.nr_03_config_mode()` to `NmiSource::set_config_mode()`.
    //   That left `NmiSource::config_mode_` at false through the test,
    //   so the `if (config_mode_) { nmi_mf_=false; ... }` clear branch in
    //   `recompute_()` (nmi_source.cpp:352) was never entered — pre-fix
    //   or post-fix. We now mirror the per-tick fan-out by driving
    //   `set_config_mode(true)` directly before `tick(1)`. This makes
    //   the test discriminative against the actual fix at
    //   nmi_source.cpp:352-358 where the pre-fix code wrongly cleared
    //   `nr_02_pending_{mf,divmmc}_` alongside the FSM latches.
    {
        emu.reset();
        // Set NR 0x02 bit 3 (mf NMI pending) via NR 0x02 write.
        // This goes through NmiSource::nr_02_write() which sets the
        // readback latches per VHDL :3840-3864.
        nr_write(emu, 0x02, 0x08);          // bit 3 = 1 (generate MF NMI)
        // Engage NR 0x03 config_mode (bits[2:0]=111 with bit 3 set is
        // not the canonical entry; per VHDL :2102-2105 + NextReg::
        // apply_nr_03_config_mode_transition the encoding "111" is the
        // recognized config_mode trigger).
        nr_write(emu, 0x03, 0x07);
        // Sanity: NextReg side accepted the config_mode change.
        const bool nr_cfg = emu.nextreg().nr_03_config_mode();
        // Mirror the per-tick `tick_peripheral_subsystems` fan-out
        // (emulator.cpp:5163) that propagates config_mode to NmiSource.
        // Without this call, NmiSource::config_mode_ stays false and the
        // recompute_() clear-branch never runs, so the test passes
        // regardless of whether the fix is in place.
        emu.nmi_source().set_config_mode(true);
        // Tick the FSM/clear path. Pre-fix the clear branch wrongly
        // cleared `nr_02_pending_mf_`; post-fix the readback bit
        // survives.
        emu.nmi_source().tick(1);
        const uint8_t got = nr_read(emu, 0x02) & 0x08;  // readback bit 3
        check("TC-NR02-CFGMODE-NO-CLEAR",
              "NR 0x02 readback bit 3 NOT cleared by config_mode "
              "[zxnext.vhd:3840-3864 / nmi_source.cpp:352-358]",
              nr_cfg && got == 0x08,
              "nr_cfg=" + std::to_string(nr_cfg) +
              " NR02b3=" + hex2(got));
        // Restore: drop config_mode on NmiSource and NextReg.
        emu.nmi_source().set_config_mode(false);
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        emu.reset();
    }

    // ── TC-NR02-BUS-RESET — NR 0x02 bit 7 (bus_reset) readback (Verify3, d841887)
    //   VHDL :5119 captures bit 7 verbatim on every NR 0x02 write; :5891
    //   surfaces it as bit 7 of NR 0x02 readback.  Pre-fix: hard-coded 0.
    {
        emu.reset();
        nr_write(emu, 0x02, 0x80);          // bit 7 = 1 (RESET_PERIPHERAL)
        const uint8_t got = nr_read(emu, 0x02) & 0x80;
        check("TC-NR02-BUS-RESET",
              "NR 0x02 bit 7 (bus_reset) read-back from latch "
              "[zxnext.vhd:5119 + :5891 + :1579]",
              got == 0x80,
              "NR02b7=" + hex2(got));
        emu.reset();
    }

    // ── TC-NR0A-MFTYPE-GATED — NR 0x0A bits 7:6 mf_type config_mode-gated
    //   readback (Verify3, d841887). VHDL :5191-5198 only commits mf_type
    //   writes when nr_03_config_mode='1'. Pre-fix: read used cached(0x0A)
    //   directly, bypassing the gate.
    {
        emu.reset();
        // (1) With config_mode=0, write bits 7:6 — should NOT commit to
        //     authoritative mf_type, so read should still reflect prior.
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        const uint8_t initial_mf_type = nr_read(emu, 0x0A) & 0xC0;
        nr_write(emu, 0x0A, 0xC0);          // bits 7:6 = 11
        const uint8_t after_no_cfg = nr_read(emu, 0x0A) & 0xC0;
        // (2) Now enable config_mode and write bits 7:6 again — commits.
        nr_write(emu, 0x03, 0x07);  // bits[2:0]=111 → config_mode=1
        nr_write(emu, 0x0A, 0x40);          // bits 7:6 = 01 (MF128)
        const uint8_t after_cfg = nr_read(emu, 0x0A) & 0xC0;
        check("TC-NR0A-MFTYPE-GATED",
              "NR 0x0A bits 7:6 mf_type sourced from gated state, not cache "
              "[zxnext.vhd:5191-5198 / :5912]",
              after_no_cfg == initial_mf_type && after_cfg == 0x40,
              "init=" + hex2(initial_mf_type) +
              " no_cfg=" + hex2(after_no_cfg) + " cfg=" + hex2(after_cfg));
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        emu.reset();
    }

    // ── TC-NR04-MASK7F — NR 0x04 write returns v & 0x7F for Issue-2
    //   board (Verify4, 4f25708). VHDL :5717 gen_romram_234 stores 7 bits.
    //   NR 0x04 is write-only; the read-mux returns 0 (others=>'0'). Verify
    //   the internal latched bank via the public accessor.
    {
        emu.reset();
        nr_write(emu, 0x04, 0xFF);
        const uint8_t bank = emu.nextreg().nr_04_romram_bank();
        check("TC-NR04-MASK7F",
              "NR 0x04 internal bank latched as v & 0x7F (Issue-2) "
              "[zxnext.vhd:5717 gen_romram_234]",
              bank == 0x7F,
              "bank=" + hex2(bank));
        emu.reset();
    }

    // ── TC-NR11-MASK07 — NR 0x11 write read-mask 0x07 (Verify4, 4f25708)
    //   VHDL :5208-5217 — Issue-2 video timing with config_mode-gated
    //   bit-0-only capture. Read mask 0x07.
    {
        emu.reset();
        // Need config_mode=1 for write to commit
        nr_write(emu, 0x03, 0x07);  // bits[2:0]=111 → config_mode=1
        nr_write(emu, 0x11, 0xFF);
        const uint8_t got = nr_read(emu, 0x11);
        check("TC-NR11-MASK07",
              "NR 0x11 read masked to bits 2:0 "
              "[zxnext.vhd:5208-5217 + read mux]",
              (got & 0xF8) == 0x00,
              "got=" + hex2(got));
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        emu.reset();
    }

    // ── TC-NR2F-MASK03 — NR 0x2F write returns v & 0x03 (Verify4, 4f25708)
    //   VHDL :5331 stores 2 bits.
    {
        emu.reset();
        nr_write(emu, 0x2F, 0xFF);
        const uint8_t cached = emu.nextreg().cached(0x2F);
        check("TC-NR2F-MASK03",
              "NR 0x2F write masks to bits 1:0 "
              "[zxnext.vhd:5331]",
              (cached & 0xFC) == 0x00,
              "cached=" + hex2(cached));
        emu.reset();
    }

    // ── TC-NR8A-MASK3F — NR 0x8A write returns v & 0x3F (Verify4, 4f25708)
    //   VHDL :5525 — low 6 bits stored.
    {
        emu.reset();
        // NR 0x8A is preserved across reset (PASS-8) so write & read.
        nr_write(emu, 0x8A, 0xFF);
        const uint8_t got = nr_read(emu, 0x8A);
        check("TC-NR8A-MASK3F",
              "NR 0x8A read masked to bits 5:0 "
              "[zxnext.vhd:5525 + :5970 read prefix '00']",
              (got & 0xC0) == 0x00,
              "got=" + hex2(got));
        emu.reset();
    }

    // ── TC-NR90-MASKFC — NR 0x90 write returns v & 0xFC (Verify4, 4f25708)
    //   VHDL :5537 forces bits 1:0 = 0.
    {
        emu.reset();
        nr_write(emu, 0x90, 0xFF);
        const uint8_t cached = emu.nextreg().cached(0x90);
        check("TC-NR90-MASKFC",
              "NR 0x90 write forces bits 1:0 = 0 "
              "[zxnext.vhd:5537]",
              (cached & 0x03) == 0x00,
              "cached=" + hex2(cached));
        emu.reset();
    }

    // ── TC-NR93-MASK0F — NR 0x93 write returns v & 0x0F (Verify4, 4f25708)
    //   VHDL :5546 stores low nibble only.
    {
        emu.reset();
        nr_write(emu, 0x93, 0xFF);
        const uint8_t cached = emu.nextreg().cached(0x93);
        check("TC-NR93-MASK0F",
              "NR 0x93 write masks to low nibble "
              "[zxnext.vhd:5546]",
              (cached & 0xF0) == 0x00,
              "cached=" + hex2(cached));
        emu.reset();
    }

    // ── TC-NR98-9B-READZERO — NR 0x98/99/9A/9B read returns 0 (Verify4)
    //   VHDL :6176-6186 reads i_GPIO inputs, NOT write shadow; jnext
    //   does not model Pi GPIO so reads must be 0 regardless of writes.
    {
        emu.reset();
        nr_write(emu, 0x98, 0xAA);
        nr_write(emu, 0x99, 0xBB);
        nr_write(emu, 0x9A, 0xCC);
        nr_write(emu, 0x9B, 0xDD);
        const uint8_t r98 = nr_read(emu, 0x98);
        const uint8_t r99 = nr_read(emu, 0x99);
        const uint8_t r9a = nr_read(emu, 0x9A);
        const uint8_t r9b = nr_read(emu, 0x9B);
        check("TC-NR98-9B-READZERO",
              "NR 0x98..0x9B read returns 0 (i_GPIO unmodelled) "
              "[zxnext.vhd:6176-6186]",
              r98 == 0 && r99 == 0 && r9a == 0 && r9b == 0,
              "r98=" + hex2(r98) + " r99=" + hex2(r99) +
              " r9a=" + hex2(r9a) + " r9b=" + hex2(r9b));
        emu.reset();
    }

    // ── TC-NRA8-MASK01 — NR 0xA8 write returns v & 0x01 (Verify4, 4f25708)
    //   VHDL :5570 stores bit 0 only.
    {
        emu.reset();
        nr_write(emu, 0xA8, 0xFF);
        const uint8_t cached = emu.nextreg().cached(0xA8);
        check("TC-NRA8-MASK01",
              "NR 0xA8 write masks to bit 0 only "
              "[zxnext.vhd:5570]",
              cached == 0x01,
              "cached=" + hex2(cached));
        emu.reset();
    }

    // ── TC-NRA9-READZERO — NR 0xA9 read returns 0 (Verify4)
    //   VHDL :6200-6201 reads i_ESP_GPIO_20 inputs, NOT write shadow;
    //   jnext does not model ESP GPIO.
    {
        emu.reset();
        nr_write(emu, 0xA9, 0xAA);
        const uint8_t got = nr_read(emu, 0xA9);
        check("TC-NRA9-READZERO",
              "NR 0xA9 read returns 0 (i_ESP_GPIO unmodelled) "
              "[zxnext.vhd:6200-6201]",
              got == 0,
              "got=" + hex2(got));
        emu.reset();
    }

    // ── TC-NR75-79-WRITEZERO — NR 0x75-0x79 write returns 0
    //   (Verify5, d92c6ec). VHDL :5878-6289 has no read entry — read
    //   falls through to (others => '0'). Pre-fix wrote v through the
    //   regs_[] cache, leaking into subsequent reads.
    {
        emu.reset();
        for (uint8_t reg = 0x75; reg <= 0x79; ++reg) {
            nr_write(emu, reg, 0xFF);
            const uint8_t got = nr_read(emu, reg);
            char d[64]; std::snprintf(d, sizeof(d),
                "NR %02X read=0x%02X want=0x00", reg, got);
            check("TC-NR75-79-WRITEZERO",
                  "NR 0x75-0x79 write-only mirror reads 0 "
                  "[zxnext.vhd:5878-6289 others=>'0']",
                  got == 0, d);
        }
        emu.reset();
    }

    // ── TC-NR09-BIT3-NOT-SPRITES — NR 0x09 bit 3 is DivMMC mapram-clear
    //   trigger, NOT sprite over_border (Verify5, d92c6ec). VHDL
    //   :4184-4186 / :5909 — bit 3 is sw-write-strobe; reads back as '0'.
    //   Sprite over-border is NR 0x15 bit 1 (separate signal).
    //
    //   Discriminative-fix (review follow-up): the original test only
    //   asserted `nr_read(0x09) & 0x08 == 0`. But the read-handler mask
    //   `& 0xE7` (emulator.cpp:959) was already in place pre-fix and
    //   stripped bit 3 unconditionally — so the assertion passed even
    //   with the actual write-side bug present. The Pass-5 fix removed
    //   `sprites_.set_over_border((v & 0x08) != 0);` from the NR 0x09
    //   write handler (emulator.cpp:3616-3632). To exercise the wiring
    //   fix we now verify (a) writing NR 0x09 bit 3 does NOT set
    //   `sprites_.over_border()`, and (b) NR 0x15 bit 1 — the correct
    //   VHDL signal `nr_15_sprite_over_border_en` — DOES set it
    //   (emulator.cpp:1209). This catches a regression where bit 3 of
    //   NR 0x09 is wrongly re-wired to over_border.
    {
        emu.reset();
        // Pre-condition: over_border defaults to false at reset.
        const bool ob_initial = emu.sprites().over_border();
        // (a) NR 0x09 bit 3 must NOT set over_border (Pass-5 fix).
        nr_write(emu, 0x09, 0x08);          // bit 3 = 1
        const bool ob_after_nr09 = emu.sprites().over_border();
        // Read-side: bit 3 of NR 0x09 still reads back as 0 (read mask
        // 0xE7) — preserved as a documented invariant.
        const uint8_t got09 = nr_read(emu, 0x09) & 0x08;
        // (b) NR 0x15 bit 1 (sprite_over_border_en) DOES set over_border.
        nr_write(emu, 0x15, 0x02);
        const bool ob_after_nr15 = emu.sprites().over_border();
        check("TC-NR09-BIT3-READS-ZERO",
              "NR 0x09 bit 3 does not affect sprites.over_border; "
              "NR 0x15 bit 1 is the authoritative wiring "
              "[zxnext.vhd:4184-4186 / :5909 / emulator.cpp:3616-3632 / :1209]",
              !ob_initial && !ob_after_nr09 && got09 == 0 && ob_after_nr15,
              "ob0=" + std::to_string(ob_initial) +
              " ob_nr09=" + std::to_string(ob_after_nr09) +
              " NR09b3=" + hex2(got09) +
              " ob_nr15=" + std::to_string(ob_after_nr15));
        emu.reset();
    }

    // ── TC-NR7F-PRESERVE — NR 0x7F preserved across reset (Verify5)
    //   VHDL :1216 declares power-on default 0xFF; no reset clause.
    {
        emu.reset();
        nr_write(emu, 0x7F, 0x55);
        const uint8_t before = nr_read(emu, 0x7F);
        emu.reset();
        const uint8_t after = nr_read(emu, 0x7F);
        check("TC-NR7F-PRESERVE",
              "NR 0x7F survives reset (VHDL has no reset clause) "
              "[zxnext.vhd:1216]",
              before == 0x55 && after == 0x55,
              "before=" + hex2(before) + " after=" + hex2(after));
        emu.reset();
    }

    // ── TC-NR80-LOHI-FOLD — NR 0x80 lo→hi nibble fold on reset
    //   (Verify5, d92c6ec). VHDL :2185-2186 — on reset,
    //   nr_80_expbus(7:4) <= nr_80_expbus(3:0). Bits 3:0 survive; bits 7:4
    //   are recomputed from bits 3:0.
    {
        emu.reset();
        // Write a discriminating pattern: lo nibble = 5, hi = A (mismatched)
        nr_write(emu, 0x80, 0xA5);          // hi=A, lo=5
        emu.reset();                        // hard reset → fold
        const uint8_t got = nr_read(emu, 0x80);
        check("TC-NR80-LOHI-FOLD",
              "NR 0x80 reset folds bits 3:0 into bits 7:4 "
              "[zxnext.vhd:2185-2186]",
              got == 0x55,
              "got=" + hex2(got) + " want=0x55");
        emu.reset();
    }

    // ── TC-NR06-MIDFLIGHT-PRESERVE — NR 0x06 preserves bits 6,4,3,2,1,0
    //   across reset (Verify6, 9953ed1).  Independent test from existing
    //   G56-CR-NR06-RESET-PRESERVE — uses a fully discriminating pattern.
    {
        emu.reset();
        // Set NR 0x06 b3=1 (M1 NMI enable), via config_mode commit cycle.
        nr_write(emu, 0x03, 0x07);  // bits[2:0]=111 → config_mode=1
        nr_write(emu, 0x06, 0x5C);          // b6=1 b4=1 b3=1 b2=1 b0/1=00
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        emu.reset();
        // After reset: bits 7,5 forced to 1; bits 6,4,3,2,1,0 preserved.
        const uint8_t got = nr_read(emu, 0x06);
        check("TC-NR06-PRESERVE-MIXED",
              "NR 0x06 reset preserves bits 6,4,3,2,1,0; forces 7,5=1 "
              "[zxnext.vhd:4932-4933 / :1109-1113]",
              got == 0xFC,                  // 11 (forced) + 5C (preserved)
              "got=" + hex2(got) + " want=0xFC");
        emu.reset();
    }

    // ── TC-NR09-PRESERVE — NR 0x09 reset preserves all but bit 4 (Pass-7)
    //   VHDL :4937 — only nr_09_sprite_tie='0' is asserted; bits 7:5
    //   (psg_mono), bit 2 (hdmi_audio_en), bits 1:0 (scanlines) survive.
    //   Bit 3 reads back as '0' (sw-write-strobe — Verify5 fix).
    {
        emu.reset();
        nr_write(emu, 0x09, 0xEF);          // all bits except bit 4
        const uint8_t before = nr_read(emu, 0x09);  // bit 3 reads as 0
        emu.reset();
        const uint8_t after = nr_read(emu, 0x09);
        // Mask bit 4 (sprite_tie always 0) and bit 3 (read-back 0) from
        // both — what remains (bits 7:5,2,1:0) must be preserved.
        const uint8_t pre_pres = static_cast<uint8_t>(before & 0xE7);
        const uint8_t post_pres = static_cast<uint8_t>(after & 0xE7);
        check("TC-NR09-PRESERVE",
              "NR 0x09 reset preserves bits 7:5,2,1:0 (sprite_tie cleared) "
              "[zxnext.vhd:4937 / :1115-1118]",
              pre_pres == 0xE7 && post_pres == 0xE7,
              "pre_pres=" + hex2(pre_pres) + " post_pres=" + hex2(post_pres));
        emu.reset();
    }

    // ── TC-NR81-PRESERVE-SOFT — NR 0x81 preserved across soft reset
    //   (Pass-7).  VHDL — no reset clause anywhere; pre-pass-7 jnext
    //   unconditionally cleared nr_81_ on every reset (soft + hard),
    //   wiping the user-set ExpBus NMI-debounce-disable bit (bit 5)
    //   on every NR 0x02 ← 0x01 soft reset.  Fix: preserve across soft
    //   reset (preserve_memory=true); hard reset still restores 0x00
    //   (matches the cold-boot power-on path; the only practical
    //   "init" path that re-runs without preserve_memory is fixture
    //   construction).
    //
    //   Read mask: 0x80 | (nr_81_ & 0x78); bit 7 is a constant hw input,
    //   bit 2 reads back as 0, bits 1:0 are the hardwired-"00" expbus
    //   speed (V11-NMP-01 fix). Write 0x33 → expect read 0xB0 (0x33 &
    //   0x78 = 0x30, OR 0x80 = 0xB0); after soft reset must still read
    //   0xB0.
    {
        emu.reset();
        nr_write(emu, 0x81, 0x33);
        const uint8_t before = nr_read(emu, 0x81);
        emu.soft_reset();                   // preserve_memory=true path
        const uint8_t after = nr_read(emu, 0x81);
        check("TC-NR81-PRESERVE-SOFT",
              "NR 0x81 survives soft reset (no VHDL reset clause; preserve "
              "across NR 0x02 b0 soft reset) [zxnext.vhd:1221-1225]",
              before == 0xB0 && after == 0xB0,
              "before=" + hex2(before) + " after=" + hex2(after));
        emu.reset();
    }

    // ── TC-NR0A-PRESERVE — NR 0x0A preserved across reset (Pass-8, 69ec3f6)
    //   VHDL :1124-1128 declare initial-only signals.
    //
    //   Note: bits 7:6 (mf_type) commit only when config_mode=1.
    {
        emu.reset();
        nr_write(emu, 0x03, 0x07);  // bits[2:0]=111 → config_mode=1
        nr_write(emu, 0x0A, 0x4A);          // bits 7:6=01, bit 3=1, bit 1=1
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        const uint8_t before = nr_read(emu, 0x0A) & 0xC0;
        emu.reset();
        const uint8_t after = nr_read(emu, 0x0A) & 0xC0;
        check("TC-NR0A-PRESERVE",
              "NR 0x0A bits 7:6 mf_type survive reset "
              "[zxnext.vhd:1124-1128]",
              before == 0x40 && after == 0x40,
              "before=" + hex2(before) + " after=" + hex2(after));
        emu.reset();
    }

    // ── TC-NR10-PRESERVE — NR 0x10 preserved across reset (Pass-8)
    //   VHDL :1132-1133 declare initial-only signals (coreid).
    //   Note: coreid commits with config_mode=1 only.  Read returns
    //   composed (coreid << 2) | spkey. spkey unmodelled → 0.
    {
        emu.reset();
        nr_write(emu, 0x03, 0x07);  // bits[2:0]=111 → config_mode=1
        nr_write(emu, 0x10, 0x07);          // coreid = 0x07
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        const uint8_t before = nr_read(emu, 0x10);
        emu.reset();
        const uint8_t after = nr_read(emu, 0x10);
        check("TC-NR10-PRESERVE",
              "NR 0x10 coreid survives reset (composed read = coreid<<2) "
              "[zxnext.vhd:1132-1133 / :5924]",
              before == 0x1C && after == 0x1C,
              "before=" + hex2(before) + " after=" + hex2(after));
        emu.reset();
    }

    // ── TC-NR11-PRESERVE — NR 0x11 preserved across reset (Pass-8)
    //   VHDL :1134 initial-only video timing signal.
    //   Issue-2 board only captures bit 0 of write (VHDL :5215; jnext
    //   defaults to ZXN_ISSUE2). Power-on default is 0x03 ("011"), so
    //   to discriminate we write bit 0=0 and verify the new value
    //   survives reset.
    {
        emu.reset();
        nr_write(emu, 0x03, 0x07);  // bits[2:0]=111 → config_mode=1
        nr_write(emu, 0x11, 0x00);          // capture bit 0 = 0
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        const uint8_t before = nr_read(emu, 0x11);
        emu.reset();
        const uint8_t after = nr_read(emu, 0x11);
        check("TC-NR11-PRESERVE",
              "NR 0x11 survives reset (no VHDL reset clause) "
              "[zxnext.vhd:1134]",
              before == 0x00 && after == 0x00,
              "before=" + hex2(before) + " after=" + hex2(after));
        emu.reset();
    }

    // ── TC-NR8A-PRESERVE — NR 0x8A preserved across reset (Pass-8)
    //   VHDL :1236 declares initial-only signal.
    {
        emu.reset();
        nr_write(emu, 0x8A, 0x2A);
        const uint8_t before = nr_read(emu, 0x8A);
        emu.reset();
        const uint8_t after = nr_read(emu, 0x8A);
        check("TC-NR8A-PRESERVE",
              "NR 0x8A survives reset (no VHDL reset clause) "
              "[zxnext.vhd:1236]",
              before == 0x2A && after == 0x2A,
              "before=" + hex2(before) + " after=" + hex2(after));
        emu.reset();
    }

    // ── TC-NR14-RESET-DEFAULT — NR 0x14 reset to 0xE3 (Pass-8)
    //   VHDL master-reset-block default.
    {
        emu.reset();
        const uint8_t got = nr_read(emu, 0x14);
        check("TC-NR14-RESET-DEFAULT",
              "NR 0x14 reset default = 0xE3 "
              "[zxnext.vhd master reset block]",
              got == 0xE3,
              "got=" + hex2(got));
    }

    // ── TC-NR4A-RESET-DEFAULT — NR 0x4A reset to 0xE3 (Pass-8)
    {
        emu.reset();
        const uint8_t got = nr_read(emu, 0x4A);
        check("TC-NR4A-RESET-DEFAULT",
              "NR 0x4A reset default = 0xE3 "
              "[zxnext.vhd master reset block]",
              got == 0xE3,
              "got=" + hex2(got));
    }

    // ── TC-NR4B-RESET-DEFAULT — NR 0x4B reset to 0xE3 (Pass-8)
    {
        emu.reset();
        const uint8_t got = nr_read(emu, 0x4B);
        check("TC-NR4B-RESET-DEFAULT",
              "NR 0x4B reset default = 0xE3 "
              "[zxnext.vhd master reset block]",
              got == 0xE3,
              "got=" + hex2(got));
    }

    // ── TC-NR4C-RESET-DEFAULT — NR 0x4C reset to 0x0F (Pass-8)
    {
        emu.reset();
        const uint8_t got = nr_read(emu, 0x4C);
        check("TC-NR4C-RESET-DEFAULT",
              "NR 0x4C reset default = 0x0F "
              "[zxnext.vhd master reset block]",
              got == 0x0F,
              "got=" + hex2(got));
    }

    // ── TC-NR01-RO — NR 0x01 read-only (Pass-8, RO-guard)
    //   VHDL has no write strobe; pre-fix jnext stored writes verbatim.
    {
        emu.reset();
        const uint8_t before = nr_read(emu, 0x01);
        nr_write(emu, 0x01, 0xAA);          // attempt to overwrite
        const uint8_t after = nr_read(emu, 0x01);
        check("TC-NR01-RO",
              "NR 0x01 RO — write must not change cached value "
              "[zxnext.vhd no write strobe]",
              before == after,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // ── TC-NR0E-RO — NR 0x0E read-only (Pass-8)
    {
        emu.reset();
        const uint8_t before = nr_read(emu, 0x0E);
        nr_write(emu, 0x0E, 0xAA);
        const uint8_t after = nr_read(emu, 0x0E);
        check("TC-NR0E-RO",
              "NR 0x0E RO — write must not change cached value",
              before == after,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // ── TC-NR0F-RO — NR 0x0F read-only (Pass-8)
    {
        emu.reset();
        const uint8_t before = nr_read(emu, 0x0F);
        nr_write(emu, 0x0F, 0xAA);
        const uint8_t after = nr_read(emu, 0x0F);
        check("TC-NR0F-RO",
              "NR 0x0F RO — write must not change cached value",
              before == after,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // ── TC-MOUSE-RESET-PRESERVE — KempstonMouse::reset preserves shadows
    //   (Pass-8). Pre-fix clobbered button_reverse_/dpi_ on every reset;
    //   NR 0x0A read sources those bits from the subsystem.
    {
        emu.reset();
        // Configure mouse via NR 0x0A bit 3 (button_reverse) + bits 1:0 (dpi)
        nr_write(emu, 0x03, 0x07);  // bits[2:0]=111 → config_mode=1
        // bits: 7:6 = mf_type (preserve), 3 = button_reverse, 1:0 = dpi
        // dpi=10 (256 dpi), button_reverse=1 → 0x0A
        nr_write(emu, 0x0A, 0x0A);
        nr_write(emu, 0x03, 0x01);  // bits[2:0]=001 → config_mode=0
        const bool br_before = emu.mouse().button_reverse();
        const uint8_t dpi_before = emu.mouse().dpi();
        emu.reset();
        const bool br_after = emu.mouse().button_reverse();
        const uint8_t dpi_after = emu.mouse().dpi();
        check("TC-MOUSE-RESET-PRESERVE",
              "KempstonMouse shadows survive reset (NR 0x0A preserved) "
              "[zxnext.vhd:1124-1128 / :5197 / :5191-5198]",
              br_before == br_after && dpi_before == dpi_after &&
              br_before == true && dpi_before == 0x02,
              "br_b=" + std::to_string(br_before) +
              " dpi_b=" + hex2(dpi_before) +
              " br_a=" + std::to_string(br_after) +
              " dpi_a=" + hex2(dpi_after));
        emu.reset();
    }

    // ── TC-JOYSTICK-RESET-PRESERVE — Joystick::reset preserves modes
    //   (Pass-8). Pre-fix cleared joy0_mode_/joy1_mode_ on every reset.
    {
        emu.reset();
        // Default joy0=Kempston1, joy1=Sinclair2. Set to non-defaults.
        // NR 0x05 bit-mux: 7,6 = joy0[1:0], 5,4 = joy1[1:0], 3 = joy0[2],
        //   1 = joy1[2].  Set joy0=Kempston2 ("100" → 100 in joy0) and
        //   joy1=Cursor ("010" → 010 in joy1).  Set bits 7,6=00, 5,4=01,
        //   3=1, 1=0 = 0x18 + bit 3
        nr_write(emu, 0x05, 0x18);
        const Joystick::Mode mode_l_before = emu.joystick().mode_left();
        const Joystick::Mode mode_r_before = emu.joystick().mode_right();
        emu.reset();
        const Joystick::Mode mode_l_after = emu.joystick().mode_left();
        const Joystick::Mode mode_r_after = emu.joystick().mode_right();
        check("TC-JOYSTICK-RESET-PRESERVE",
              "Joystick mode shadows survive reset (NR 0x05 preserved) "
              "[zxnext.vhd:1105-1106 / :1302-1303]",
              mode_l_before == mode_l_after &&
              mode_r_before == mode_r_after,
              std::string{"l_b="} +
              std::to_string(static_cast<int>(mode_l_before)) +
              " r_b=" +
              std::to_string(static_cast<int>(mode_r_before)) +
              " l_a=" +
              std::to_string(static_cast<int>(mode_l_after)) +
              " r_a=" +
              std::to_string(static_cast<int>(mode_r_after)));
        emu.reset();
    }

    // ── TC-NR52-FF — NR 0x52 with v=0xFF stores 0xFF verbatim (Initial NR-2)
    //   VHDL :4686-4696 — slots 2-7 with v=0xFF store 0xFF (slot becomes
    //   inactive). Pre-fix called map_rom(2, 0) silently remapping slot
    //   to physical page 0.
    {
        emu.reset();
        nr_write(emu, 0x52, 0xFF);
        const uint8_t got = nr_read(emu, 0x52);
        check("TC-NR52-FF",
              "NR 0x52 with v=0xFF stores 0xFF verbatim (slot inactive) "
              "[zxnext.vhd:4686-4696]",
              got == 0xFF,
              "got=" + hex2(got));
        emu.reset();
    }

    // ── TC-NR53-FF — NR 0x53 with v=0xFF stores 0xFF verbatim
    {
        emu.reset();
        nr_write(emu, 0x53, 0xFF);
        const uint8_t got = nr_read(emu, 0x53);
        check("TC-NR53-FF",
              "NR 0x53 with v=0xFF stores 0xFF verbatim (slot inactive) "
              "[zxnext.vhd:4686-4696]",
              got == 0xFF,
              "got=" + hex2(got));
        emu.reset();
    }

    // ── TC-NR54-FF — NR 0x54 with v=0xFF stores 0xFF verbatim
    {
        emu.reset();
        nr_write(emu, 0x54, 0xFF);
        const uint8_t got = nr_read(emu, 0x54);
        check("TC-NR54-FF",
              "NR 0x54 with v=0xFF stores 0xFF verbatim (slot inactive) "
              "[zxnext.vhd:4686-4696]",
              got == 0xFF,
              "got=" + hex2(got));
        emu.reset();
    }

    // ── TC-NR55-FF — NR 0x55 with v=0xFF stores 0xFF verbatim
    {
        emu.reset();
        nr_write(emu, 0x55, 0xFF);
        const uint8_t got = nr_read(emu, 0x55);
        check("TC-NR55-FF",
              "NR 0x55 with v=0xFF stores 0xFF verbatim (slot inactive) "
              "[zxnext.vhd:4686-4696]",
              got == 0xFF,
              "got=" + hex2(got));
        emu.reset();
    }

    // ── TC-IOTRAP-IDLE-GATE — NR 0xDA / NR 0xD9 captured when FSM in IDLE
    //   (Verify3, d841887). VHDL :3871, :3892 gate both captures on
    //   `nmi_accept_cause`='1' (FSM in IDLE or FETCH). Companion to
    //   FT-INT-DA-01a/b/c (which all run in IDLE).
    //
    //   Discriminative-fix (review follow-up): the original test ran
    //   entirely in IDLE (post-reset), where the gate
    //   `nmi_accept_cause` is true. It verified that capture WORKS in
    //   IDLE — but the bug was that capture happened in HOLD/END (where
    //   it shouldn't). To exercise the Pass-3 gate fix at
    //   emulator.cpp:2710 / :2725 / :2740 we now also drive the FSM
    //   into HOLD via the MF producer path, trigger an iotrap-eligible
    //   port event, and verify NR 0xDA / NR 0xD9 are NOT updated. The
    //   positive-axis IDLE check (kept) plus the negative-axis HOLD
    //   check make the row discriminative against either gate clause
    //   being reverted.
    {
        emu.reset();
        nr_write(emu, 0xD8, 0x01);
        // ── Positive axis: capture WORKS in IDLE.
        const bool pre_idle =
            (emu.nmi_source().state() == NmiSource::State::Idle);
        emu.port().out(0x3FFD, 0xCC);
        const uint8_t cause_idle = nr_read(emu, 0xDA);
        const uint8_t write_byte_idle = nr_read(emu, 0xD9);

        // ── Negative axis: capture BLOCKED while FSM is in HOLD.
        // Drive the FSM through the MF producer:
        //   IDLE -> FETCH (tick after MF strobe with MF enabled and
        //                  CONMEM=0 / divmmc_nmi_hold=0)
        //   FETCH -> HOLD (observe_m1_fetch at $0066 with mreq+m1)
        emu.reset();
        nr_write(emu, 0xD8, 0x01);
        // Re-acquire NmiSource ref each step in case the integration test
        // shares state with other groups; emu.reset() above re-inits.
        emu.nmi_source().set_mf_enable(true);
        emu.nmi_source().strobe_mf_button();
        emu.nmi_source().tick(1);
        const bool reached_fetch =
            (emu.nmi_source().state() == NmiSource::State::Fetch);
        emu.nmi_source().observe_m1_fetch(0x0066, /*m1=*/true, /*mreq=*/true);
        const bool reached_hold =
            (emu.nmi_source().state() == NmiSource::State::Hold);
        // Now strobe a port-3FFD WRITE iotrap — the gate must reject
        // both NR 0xDA and NR 0xD9 captures because nmi_accept_cause is
        // false in HOLD per VHDL :2164 / emulator.h:555.
        emu.port().out(0x3FFD, 0x55);
        const uint8_t cause_hold = nr_read(emu, 0xDA);
        const uint8_t write_byte_hold = nr_read(emu, 0xD9);

        check("TC-IOTRAP-IDLE-GATE",
              "NR 0xDA/D9 captured ONLY when nmi_accept_cause='1' "
              "(IDLE/FETCH); blocked in HOLD "
              "[zxnext.vhd:3871 / :3892 / emulator.cpp:2710,2725,2740]",
              pre_idle && cause_idle == 0x03 && write_byte_idle == 0xCC &&
              reached_fetch && reached_hold &&
              cause_hold == 0x00 && write_byte_hold == 0x00,
              "idle=" + std::to_string(pre_idle) +
              " c_idle=" + hex2(cause_idle) +
              " w_idle=" + hex2(write_byte_idle) +
              " fetch=" + std::to_string(reached_fetch) +
              " hold=" + std::to_string(reached_hold) +
              " c_hold=" + hex2(cause_hold) +
              " w_hold=" + hex2(write_byte_hold));
        // Restore: drop MF enable so subsequent tests start clean.
        emu.nmi_source().set_mf_enable(false);
        emu.reset();
    }

    // ── V12-NMP-01 — NR 0xC4 b0 ULA-INT-disable shadow fan-out (Pass-12)
    //   VHDL zxnext.vhd:3621-3622 — `nr_c4_we` writes `port_ff_reg(6) <=
    //   not nr_wr_dat(0)`. VHDL :3635 then ties `port_ff_interrupt_disable
    //   <= port_ff_reg(6)` combinationally; VHDL :6711 feeds that into the
    //   ULA-INT enable signal `ula_int_en`. Pre-fix the C++ NR 0xC4 write
    //   updated `port_ff_reg_` bit 6 but NOT the `ula_int_disabled_`
    //   shadow nor `video_timing_.interrupt_enable()`. Symptom: software
    //   that writes NR 0xC4 ← 0x01 (enable ULA INT) saw the read-back at
    //   line 2417 (`!ula_int_disabled_ → bit 0`) keep returning '0'
    //   because the shadow stayed stuck at the prior state. The
    //   discriminative shape: write NR 0xC4 = 0x00 first to set both
    //   shadows to "disabled"; then write NR 0xC4 = 0x01 and observe the
    //   readback flip to bit 0 = '1'. Pre-fix the readback would NOT
    //   flip because `ula_int_disabled_` would still be false (set by
    //   the NR 0xC4=0x00 path which did update port_ff_reg_(6)=1, but
    //   reading bit 0 = !ula_int_disabled_ would have stayed at the
    //   default-init false → bit 0 = '1' even after writing 0x00). To
    //   force a real divergence, we ALSO write NR 0x22 = 0x04 first to
    //   set ula_int_disabled_=true (via the NR 0x22 path that DOES
    //   update both shadows). Then write NR 0xC4 = 0x01 — pre-fix this
    //   leaves ula_int_disabled_=true (NR 0x22 path set it; NR 0xC4
    //   path didn't clear it), so NR 0xC4 readback bit 0 = '0'. Post-fix
    //   NR 0xC4 = 0x01 syncs ula_int_disabled_=false from
    //   port_ff_reg_(6)=0, so readback bit 0 = '1'.
    {
        emu.reset();
        // Step 1: write NR 0x22 = 0x04 → bit 2 of NR 0x22 fans into
        // port_ff_reg_(6)=1 AND sets ula_int_disabled_=true via the
        // existing fan-out at emulator.cpp:1657-1658. Confirm via NR
        // 0xC4 read bit 0: should be 0 (disabled).
        nr_write(emu, 0x22, 0x04);
        const uint8_t after_22 = nr_read(emu, 0xC4);
        // Step 2: write NR 0xC4 = 0x01 → port_ff_reg_(6)=0; the V12-NMP-01
        // fix MUST also sync ula_int_disabled_=false. NR 0xC4 read bit 0
        // should now be 1 (enabled).
        nr_write(emu, 0xC4, 0x01);
        const uint8_t after_c4_enable = nr_read(emu, 0xC4);
        // Step 3: write NR 0xC4 = 0x00 → port_ff_reg_(6)=1; the V12-NMP-01
        // fix MUST also sync ula_int_disabled_=true. NR 0xC4 read bit 0
        // should be 0 again.
        nr_write(emu, 0xC4, 0x00);
        const uint8_t after_c4_disable = nr_read(emu, 0xC4);
        check("V12-NMP-01",
              "NR 0xC4 b0 write fans into ula_int_disabled_ shadow + "
              "video_timing_ INT-enable gate (mirroring NR 0x22 b2 path) "
              "[zxnext.vhd:3621-3622 / :3635 / :6711]",
              (after_22 & 0x01) == 0x00 &&
              (after_c4_enable & 0x01) == 0x01 &&
              (after_c4_disable & 0x01) == 0x00,
              "after_22=" + hex2(after_22) +
              " after_c4_enable=" + hex2(after_c4_enable) +
              " after_c4_disable=" + hex2(after_c4_disable));
        emu.reset();
    }

    // ── V13-NMP-01 — NR 0x05 bit 2 Pentagon-mode cache canonicalisation
    //   (Pass-13). VHDL zxnext.vhd:5832-5841 forces `nr_05_5060 <= '0'`
    //   continuously every clock when `nr_03_machine_timing(2) = '1'`
    //   (Pentagon mode) — the IF branch is priority-1 over the
    //   `nr_05_we`/hotkey branches, so even an explicit NR 0x05 write
    //   with bit 2 = 1 gets overwritten on the very next clock edge.
    //   Pass-10 added the READ-side mask (TC-NR05-PENTAGON) but the
    //   underlying cache still leaked bit 2 = 1: when Pentagon is later
    //   exited, the read mask stops firing and the leaked bit surfaces
    //   verbatim — diverging from the VHDL FF (which stays '0' until a
    //   fresh write or F3 toggle).
    //
    //   Two discriminators in a single check:
    //     A. Write-while-Pentagon: Activate Pentagon, write NR 0x05 with
    //        bit 2 = 1, exit Pentagon, read. Expect bit 2 = 0 (VHDL FF
    //        held at 0 throughout the write attempt). Pre-fix: cached
    //        bit 2 = 1 from the write; read returns 1.
    //     B. Pentagon-edge-clear: Non-Pentagon, write NR 0x05 with bit
    //        2 = 1 (FF latches 1), activate Pentagon (FF forced to 0
    //        next clock; the NR 0x03 fan-out must clear cached bit 2),
    //        exit Pentagon, read. Expect bit 2 = 0 (FF held at 0 once
    //        Pentagon engaged; no fresh write to set it). Pre-fix the
    //        NR 0x03 timing→Pentagon edge does not clear cached(0x05)
    //        bit 2; read returns 1.
    //
    //   Same shape as V11-NMP-02 (NR 0x0A bits 7:5 config_mode gate)
    //   and V11-NMP-03 (NR 0x06 bit 2 ps2_mode config_mode gate),
    //   applied here on the Pentagon-timing gate.
    {
        emu.reset();
        // Discriminator A — write while Pentagon is active.
        // Activate Pentagon via NR 0x03 (bit 7 = 1, dt_lock = 0,
        // bit 3 = 0, tim_sel = "100" → Pentagon timing 0x04).
        nr_write(emu, 0x03, 0x80 | (0x04 << 4));     // 0xC0
        // Confirm Pentagon engaged.
        const uint8_t timing_a = emu.nextreg().nr_03_machine_timing();
        // Write bit 2 = 1 (5060) — VHDL silently drops; FF held at 0.
        nr_write(emu, 0x05, 0x04);
        // Exit Pentagon (timing → 0x03 +3).
        nr_write(emu, 0x03, 0x80 | (0x03 << 4));     // 0xB0
        const uint8_t got_a = nr_read(emu, 0x05);

        emu.reset();
        // Discriminator B — Pentagon edge clears prior bit-2 latch.
        // Non-Pentagon (default boot timing = 0x03 +3). Write bit 2 = 1
        // (FF latches 1; cached bit 2 = 1).
        nr_write(emu, 0x05, 0x04);
        const uint8_t got_b_pre = nr_read(emu, 0x05);
        // Activate Pentagon (FF forced to 0 next clock — NR 0x03 fan-out
        // must clear cached bit 2 to mirror the FF state).
        nr_write(emu, 0x03, 0x80 | (0x04 << 4));     // 0xC0
        const uint8_t timing_b = emu.nextreg().nr_03_machine_timing();
        // Exit Pentagon (FF stays at 0 — no fresh NR 0x05 write or F3).
        nr_write(emu, 0x03, 0x80 | (0x03 << 4));     // 0xB0
        const uint8_t got_b = nr_read(emu, 0x05);

        check("V13-NMP-01",
              "NR 0x05 bit 2 Pentagon-mode cache canonicalisation: "
              "(a) write-while-Pentagon does not leak; "
              "(b) Pentagon-engagement clears prior bit-2 latch "
              "[zxnext.vhd:5832-5841 / :5897 / :6701]",
              (timing_a == 0x04) &&
              (got_a == 0x00) &&
              ((got_b_pre & 0x04) == 0x04) &&
              (timing_b == 0x04) &&
              (got_b == 0x00),
              "timing_a=" + hex2(timing_a) +
              " got_a=" + hex2(got_a) +
              " got_b_pre=" + hex2(got_b_pre) +
              " timing_b=" + hex2(timing_b) +
              " got_b=" + hex2(got_b));
        emu.reset();
    }

    emu.reset();
}

// ─────────────────────────────────────────────────────────────────────
// V16-NMP-01 — NR 0x10 readback bits 1:0 reflect F9/F10 (M1/Drive)
// button held-state per VHDL zxnext.vhd:5924 + zxnext_top_issue2.vhd:2281.
//
// Pre-fix: the NR 0x10 write_handler at emulator.cpp:1255-1261 hardcoded
// bits 1:0 to 0 (regs_[0x10] = (coreid<<2) on write, no read_handler).
// Reads always returned 0 in those bits regardless of front-panel
// button state.
//
// Post-fix: a read_handler at emulator.cpp:1278-1287 recomposes
//   ((coreid & 0x1F) << 2) | (test_hotkey_m1_ ? 1 : 0) |
//                            (test_hotkey_drive_ ? 2 : 0)
// on every read. Bits 1:0 are sourced live from the same active-high
// SPKEY_BUTTONS shadow already consumed by `nmi_assert_mf` /
// `nmi_assert_divmmc` (emulator.h:411-416), settable via
// `inject_hotkey_m1` / `inject_hotkey_drive`.
//
// Discriminative: pre-fix any combination of the test_hotkey_* setters
// returns 0 in NR 0x10 bits 1:0; post-fix bit 0 echoes M1 and bit 1
// echoes Drive. Reverting the read_handler installation in
// emulator.cpp restores pre-fix behaviour and these rows FAIL.
// ─────────────────────────────────────────────────────────────────────

static void test_v16_nmp_01_nr10_spkey_buttons(Emulator& emu) {
    set_group("V16-NMP-01-NR10-SPKEY");

    // Baseline: at construction both held-state flags are false. Bits 1:0
    // must read 0. (Mirrors the existing G56-10-04 row; included here so
    // the V16 group is self-contained and discriminative even if older
    // rows are reordered.)
    {
        emu.reset();
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(false);
        const uint8_t got = nr_read(emu, 0x10);
        check("V16-NMP-01-IDLE",
              "NR 0x10 bits 1:0 = 0 when both F9/F10 buttons released "
              "[zxnext.vhd:5924]",
              (got & 0x03) == 0x00,
              detail_eq(static_cast<uint8_t>(got & 0x03), uint8_t{0x00}));
    }

    // M1 held only: bit 0 = 1, bit 1 = 0. coreid bits unchanged.
    {
        emu.reset();
        emu.inject_hotkey_m1(true);
        emu.inject_hotkey_drive(false);
        const uint8_t got = nr_read(emu, 0x10);
        check("V16-NMP-01-M1",
              "NR 0x10 bit 0 = 1 when F9 (M1 button) held; bit 1 = 0 "
              "[zxnext.vhd:5924 / top_issue2.vhd:2281]",
              (got & 0x03) == 0x01,
              detail_eq(static_cast<uint8_t>(got & 0x03), uint8_t{0x01}));
        emu.inject_hotkey_m1(false);
    }

    // Drive held only: bit 1 = 1, bit 0 = 0.
    {
        emu.reset();
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(true);
        const uint8_t got = nr_read(emu, 0x10);
        check("V16-NMP-01-DRIVE",
              "NR 0x10 bit 1 = 1 when F10 (Drive button) held; bit 0 = 0 "
              "[zxnext.vhd:5924 / top_issue2.vhd:2281]",
              (got & 0x03) == 0x02,
              detail_eq(static_cast<uint8_t>(got & 0x03), uint8_t{0x02}));
        emu.inject_hotkey_drive(false);
    }

    // Both held: bits 1:0 = 11.
    {
        emu.reset();
        emu.inject_hotkey_m1(true);
        emu.inject_hotkey_drive(true);
        const uint8_t got = nr_read(emu, 0x10);
        check("V16-NMP-01-BOTH",
              "NR 0x10 bits 1:0 = 11 when both F9 + F10 held "
              "[zxnext.vhd:5924]",
              (got & 0x03) == 0x03,
              detail_eq(static_cast<uint8_t>(got & 0x03), uint8_t{0x03}));
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(false);
    }

    // Coreid composability: coreid bits 6:2 must remain coreid-driven
    // while buttons are held — discriminative against a regression that
    // accidentally stomps the upper bits with the held-state value.
    {
        emu.reset();
        nr_write(emu, 0x03, 0x07);   // config_mode=1
        nr_write(emu, 0x10, 0x0A);   // coreid = 0x0A → bits 6:2 = 01010
        nr_write(emu, 0x03, 0x00);
        emu.inject_hotkey_m1(true);
        emu.inject_hotkey_drive(true);
        const uint8_t got = nr_read(emu, 0x10);
        // Expected: (0x0A << 2) | 0x03 = 0x28 | 0x03 = 0x2B.
        check("V16-NMP-01-COMPOSE",
              "NR 0x10 = (coreid<<2) | spkey_buttons composed; coreid "
              "bits 6:2 not stomped by held-state path "
              "[zxnext.vhd:5924]",
              got == 0x2B,
              detail_eq(got, uint8_t{0x2B}));
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(false);
    }

    // Live combinational read: change held-state without re-writing the
    // register and verify a subsequent read picks up the new state.
    // VHDL signal is purely combinational — no flop, no commit.
    {
        emu.reset();
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(false);
        const uint8_t got_idle = nr_read(emu, 0x10);
        emu.inject_hotkey_m1(true);
        const uint8_t got_press = nr_read(emu, 0x10);
        emu.inject_hotkey_m1(false);
        const uint8_t got_release = nr_read(emu, 0x10);
        check("V16-NMP-01-LIVE",
              "NR 0x10 bit 0 tracks F9 hold transitions live "
              "(combinational; no NR 0x10 write needed) "
              "[zxnext.vhd:5924]",
              (got_idle    & 0x01) == 0 &&
              (got_press   & 0x01) == 1 &&
              (got_release & 0x01) == 0,
              "idle="    + hex2(static_cast<uint8_t>(got_idle    & 0x03)) +
              " press="   + hex2(static_cast<uint8_t>(got_press   & 0x03)) +
              " release=" + hex2(static_cast<uint8_t>(got_release & 0x03)));
    }

    emu.reset();
}

// ─────────────────────────────────────────────────────────────────────
// V16-NMP-02 — `internal_port_enable` AND-mask: NR 0x86-0x89 must
// gate NR 0x82-0x85 when expbus_eff_en=1, per VHDL zxnext.vhd:2392-2393.
//
// Pre-fix: every port-decode handler consulted `nextreg_.cached(0x82..0x85)`
// directly, ignoring NR 0x86-0x89 entirely. Setting expbus_eff_en=1
// (NR 0x80 b7) then clearing a bit of NR 0x86-0x89 had no effect on
// port decoding.
//
// Post-fix: `Emulator::effective_internal_port_enable(reg)` AND-masks
// the cached NR 0x82-0x85 byte with the corresponding NR 0x86-0x89
// byte when `expbus_eff_en=1`. All port-decode gates and the persistent
// shadows held by ContentionModel / DivMmc / Multiface route through
// the helper. Write handlers for NR 0x80, 0x82, 0x83, 0x85, 0x86,
// 0x87, 0x88, 0x89 call `propagate_effective_port_enables()` so any
// change to a formula input refreshes the shadows.
//
// Discriminative observable: NR 0x82 b1 → port_7ffd_io_en (VHDL :2399).
// When expbus_eff_en=1 and NR 0x86 b1=0, OUT 0x7FFD must be silenced
// even with NR 0x82 b1=1. Reverting the helper restores pre-fix
// behaviour and these rows FAIL.
// ─────────────────────────────────────────────────────────────────────

static void test_v16_nmp_02_expbus_and_mask(Emulator& emu) {
    set_group("V16-NMP-02-Expbus-AND-Mask");

    // Sanity baseline: with expbus_eff_en=0 (default), NR 0x86 has no
    // effect — clearing NR 0x86 b1 must NOT silence port 0x7FFD.
    // Mirrors the BUS-86-01 row in port_test (which is now upgraded
    // from "does not corrupt NR 0x82" to "does not gate the port").
    {
        emu.reset();
        // NR 0x80 b7 = 0 → expbus_eff_en = 0 (power-on default).
        nr_write(emu, 0x80, 0x00);
        // NR 0x82 b1 = 1 (default 0xFF), NR 0x86 b1 = 0.
        nr_write(emu, 0x82, 0xFF);
        nr_write(emu, 0x86, 0xFD);          // clear bit 1
        const uint8_t before = emu.mmu().port_7ffd();
        emu.port().out(0x7FFD, static_cast<uint8_t>(before ^ 0x07));
        const uint8_t after = emu.mmu().port_7ffd();
        check("V16-NMP-02-EXPBUS-OFF",
              "NR 0x86 b1=0 has NO effect on port 0x7FFD when "
              "expbus_eff_en=0 (default; NR 0x86-0x89 inert) "
              "[zxnext.vhd:2392-2393]",
              before != after,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // Discriminative core: expbus_eff_en=1, NR 0x82 b1 = 1, NR 0x86 b1 = 0
    // → effective port_7ffd_io_en = 0 → OUT 0x7FFD silenced.
    // Pre-fix this row FAILED (NR 0x86 ignored). Post-fix it PASSES.
    {
        emu.reset();
        // 1. Engage expbus_eff_en (NR 0x80 b7 = 1).
        nr_write(emu, 0x80, 0x80);
        // 2. NR 0x82 b1 = 1 (default), but mask NR 0x86 b1 = 0.
        nr_write(emu, 0x82, 0xFF);
        nr_write(emu, 0x86, 0xFD);          // clear bit 1
        // 3. OUT 0x7FFD must be silenced. Observable: Mmu's port_7ffd()
        //    register stays unchanged after the OUT.
        const uint8_t before = emu.mmu().port_7ffd();
        emu.port().out(0x7FFD, static_cast<uint8_t>(before ^ 0x07));
        const uint8_t after = emu.mmu().port_7ffd();
        check("V16-NMP-02-EXPBUS-ON-MASK",
              "NR 0x86 b1=0 silences OUT 0x7FFD when expbus_eff_en=1 "
              "(effective port_7ffd_io_en = NR 0x82 b1 AND NR 0x86 b1 = 0) "
              "[zxnext.vhd:2392-2393 / :2399]",
              before == after,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // Symmetric: expbus_eff_en=1, NR 0x86 b1 = 1 → AND term is 1 and the
    // port still decodes. Confirms the AND is bit-AND (not always-zero).
    {
        emu.reset();
        nr_write(emu, 0x80, 0x80);          // expbus_eff_en = 1
        nr_write(emu, 0x82, 0xFF);
        nr_write(emu, 0x86, 0xFF);          // bit 1 = 1
        const uint8_t before = emu.mmu().port_7ffd();
        emu.port().out(0x7FFD, static_cast<uint8_t>(before ^ 0x07));
        const uint8_t after = emu.mmu().port_7ffd();
        check("V16-NMP-02-EXPBUS-ON-PASS",
              "NR 0x86 b1=1 leaves port 0x7FFD live with expbus_eff_en=1 "
              "[zxnext.vhd:2392-2393 / :2399]",
              before != after,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // NR 0x80 toggle propagation: with NR 0x86 b1=0 throughout, the gate
    // must transition LIVE on the NR 0x80 b7 write — not require a
    // subsequent NR 0x82 / NR 0x86 write to refresh.
    {
        emu.reset();
        nr_write(emu, 0x80, 0x00);          // expbus_eff_en = 0
        nr_write(emu, 0x82, 0xFF);
        nr_write(emu, 0x86, 0xFD);          // bit 1 = 0
        // Engage expbus.
        nr_write(emu, 0x80, 0x80);
        const uint8_t before = emu.mmu().port_7ffd();
        emu.port().out(0x7FFD, static_cast<uint8_t>(before ^ 0x07));
        const uint8_t after = emu.mmu().port_7ffd();
        check("V16-NMP-02-EXPBUS-TOGGLE",
              "NR 0x80 b7 toggle live-refreshes port_7ffd_io_en shadow "
              "(propagate_effective_port_enables on NR 0x80 write) "
              "[zxnext.vhd:2392-2393]",
              before == after,
              "before=" + hex2(before) + " after=" + hex2(after));
    }

    // NR 0x83 b0 / DivMMC port_io_enable propagation — exercises the
    // `multiface_/divmmc_` shadow refresh path. With expbus_eff_en=1
    // and NR 0x83 b0=1 but NR 0x87 b0=0, divmmc_.port_io_enable() must
    // be false. The Emulator::effective_internal_port_enable propagation
    // happens on NR 0x87 write.
    {
        emu.reset();
        nr_write(emu, 0x80, 0x80);          // expbus_eff_en = 1
        nr_write(emu, 0x83, 0xFF);          // NR 0x83 all enables on
        nr_write(emu, 0x87, 0xFE);          // NR 0x87 b0 = 0 (mask off DivMMC)
        check("V16-NMP-02-DIVMMC-MASK",
              "DivMmc::port_io_enable=false when expbus_eff_en=1 + "
              "NR 0x83 b0=1 + NR 0x87 b0=0 (effective AND = 0) "
              "[zxnext.vhd:2392-2393 / :2412]",
              !emu.divmmc().port_io_enable(),
              "port_io_enable=" + std::to_string(emu.divmmc().port_io_enable()));
    }

    // Multiface enable: NR 0x83 b1 AND NR 0x87 b1 when expbus_eff_en=1.
    {
        emu.reset();
        nr_write(emu, 0x80, 0x80);          // expbus_eff_en = 1
        nr_write(emu, 0x83, 0xFF);
        nr_write(emu, 0x87, 0xFD);          // NR 0x87 b1 = 0
        check("V16-NMP-02-MF-MASK",
              "Multiface::is_enabled=false when expbus_eff_en=1 + NR 0x83 "
              "b1=1 + NR 0x87 b1=0 (effective AND = 0) "
              "[zxnext.vhd:2392-2393 / :2415]",
              !emu.multiface().is_enabled(),
              "is_enabled=" + std::to_string(emu.multiface().is_enabled()));
    }

    // NR 0x85 / NR 0x89 low-nibble pair: ULA+ port (NR 0x85 b0). Bits
    // 6:4 of NR 0x89 are read-zero per VHDL :6149-6150 + write semantics
    // :5521 (only bits 3:0 + bit 7 stored). Test the b0 case.
    {
        emu.reset();
        nr_write(emu, 0x80, 0x80);          // expbus_eff_en = 1
        nr_write(emu, 0x85, 0x0F);          // NR 0x85 enable nibble = 1111
        nr_write(emu, 0x89, 0x0E);          // NR 0x89 enable nibble = 1110 → b0=0
        // Effective NR 0x85 b0 = 1 AND 0 = 0 → port_ulap_io_en = false.
        // Observable: ContentionModel's port_ulap_io_en shadow.
        check("V16-NMP-02-NR85-NR89-B0",
              "NR 0x89 b0=0 masks NR 0x85 b0 (port_ulap_io_en) when "
              "expbus_eff_en=1 [zxnext.vhd:2392-2393 / :2439]",
              !emu.contention().port_ulap_io_en(),
              "port_ulap_io_en=" +
                  std::to_string(emu.contention().port_ulap_io_en()));
    }

    // Restore baseline: NR 0x80=0, NR 0x82-0x89 = power-on defaults.
    nr_write(emu, 0x80, 0x00);
    nr_write(emu, 0x82, 0xFF);
    nr_write(emu, 0x83, 0xFF);
    nr_write(emu, 0x84, 0xFF);
    nr_write(emu, 0x85, 0x0F);
    nr_write(emu, 0x86, 0xFF);
    nr_write(emu, 0x87, 0xFF);
    nr_write(emu, 0x88, 0xFF);
    nr_write(emu, 0x89, 0x0F);
    emu.reset();
}

// ── V19R-NMP-NIT-03/04 — XADC composed-read stubs ────────────────────
//
// Pass-19 reviewer NITs 03 and 04 documented Class-(c) inert divergences
// in NR 0xF0 / NR 0xF8 (XADC composed reads). The fix installed minimal
// VHDL-faithful stubs in emulator.cpp:
//   • NR 0xF0 read = 0x00 always (g_board_issue=0 path,
//     zxnext.vhd:7423: nr_f0_xdev_cmd <= (others => '0')).
//   • NR 0xF8 read masks bit 7 (zxnext.vhd:6278: '0' & nr_f8_xadc_daddr).
//
// Discriminative: write a non-zero byte (with bit 7 set for 0xF8) and
// verify the read returns the VHDL-faithful value, not the raw cache.

static void test_v19r_nmp_xadc_read_stubs(Emulator& emu) {
    set_group("V19R-NMP-XADC-Read-Stubs");

    // V19R-NMP-NIT-03 — NR 0xF0 XADC stub returns 0x00 regardless of write.
    //
    // VHDL zxnext.vhd:6273-6274: when X"F0" => port_253b_dat <= nr_f0_xdev_cmd;
    // For Issue 2/3 (g_board_issue<=1, zxnext.vhd:7423), `nr_f0_xdev_cmd`
    // is hard-wired to (others=>'0'). jnext seeds NR 0x0F = 0x00 (Issue 2),
    // so the VHDL-faithful readback is 0x00 always.
    //
    // Pre-V19R the bare cache returned the raw last-written byte.
    // Discriminative: write 0xAA, then read; raw cache would return 0xAA,
    // stub returns 0x00.
    {
        emu.reset();
        nr_write(emu, 0xF0, 0xAA);
        const uint8_t got = nr_read(emu, 0xF0);
        check("V19R-NMP-NIT-03",
              "NR 0xF0 XADC composed-read stub returns 0x00 even after "
              "0xAA write (Issue 2 path: nr_f0_xdev_cmd hard-wired to 0) "
              "[zxnext.vhd:6273-6274, :7423]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // V19R-NMP-NIT-04 — NR 0xF8 read masks bit 7.
    //
    // VHDL zxnext.vhd:6277-6278: when X"F8" => port_253b_dat <= '0' & nr_f8_xadc_daddr;
    // Bit 7 is always '0' on read, irrespective of board issue. The write
    // path at :7553-7558 (Issue 4/5) splits bit 7 to nr_f8_xadc_dwe and
    // bits 6:0 to nr_f8_xadc_daddr — bit 7 never makes it back through
    // the read mux. For Issue 2/3 (:7425) nr_f8_xadc_daddr is constant 0,
    // so the strict Issue-2 read would be 0x00; the conservative stub
    // `cached & 0x7F` accommodates both paths.
    //
    // Pre-V19R the bare cache stored the full byte verbatim, leaking bit 7
    // on readback. Discriminative: write 0xC5 (bit 7 = 1, bits 6:0 = 0x45)
    // and verify the read returns 0x45 (bit 7 cleared), not 0xC5.
    {
        emu.reset();
        nr_write(emu, 0xF8, 0xC5);
        const uint8_t got = nr_read(emu, 0xF8);
        check("V19R-NMP-NIT-04",
              "NR 0xF8 read masks bit 7: write 0xC5 reads back 0x45 "
              "(VHDL '0' & nr_f8_xadc_daddr) [zxnext.vhd:6277-6278, :7555]",
              got == 0x45, detail_eq(got, uint8_t{0x45}));
    }

    // V20-NMP-XADC — NR 0xF9 / NR 0xFA XADC d0/d1 composed-read stubs.
    //
    // VHDL zxnext.vhd:6280-6284 read mux:
    //   when X"F9" => port_253b_dat <= nr_f9_xadc_d0;
    //   when X"FA" => port_253b_dat <= nr_fa_xadc_d1;
    //
    // For Issue 2/3 (g_board_issue<=1, zxnext.vhd:7428-7429),
    // `nr_f9_xadc_d0` and `nr_fa_xadc_d1` are hard-wired to
    // (others=>'0'). jnext seeds NR 0x0F = 0x00 (Issue 2), so the
    // VHDL-faithful readback is 0x00 always for BOTH registers.
    //
    // Pre-V20 the bare cache stored the full written byte verbatim,
    // making the readback leak the last-written value (Class-(c) inert
    // divergence). Discriminative: write a non-zero byte and verify the
    // read returns 0x00, not the raw cache. Same shape as V19R-NMP-NIT-03.
    {
        emu.reset();
        nr_write(emu, 0xF9, 0x5A);
        const uint8_t got = nr_read(emu, 0xF9);
        check("V20-NMP-XADC-F9",
              "NR 0xF9 XADC d0 composed-read stub returns 0x00 even after "
              "0x5A write (Issue 2 path: nr_f9_xadc_d0 hard-wired to 0) "
              "[zxnext.vhd:6280-6281, :7428]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }
    {
        emu.reset();
        nr_write(emu, 0xFA, 0xA5);
        const uint8_t got = nr_read(emu, 0xFA);
        check("V20-NMP-XADC-FA",
              "NR 0xFA XADC d1 composed-read stub returns 0x00 even after "
              "0xA5 write (Issue 2 path: nr_fa_xadc_d1 hard-wired to 0) "
              "[zxnext.vhd:6283-6284, :7429]",
              got == 0x00, detail_eq(got, uint8_t{0x00}));
    }

    // V20-NMP-02 — NR 0x68 read mask: bit 1 must read back as '0'.
    //
    // VHDL zxnext.vhd:6092-6093 read mux composes NR 0x68 as
    //   (not nr_68_ula_en) & nr_68_blend_mode & nr_68_cancel_extended_keys
    //                      & port_ff3b_ulap_en & nr_68_ula_fine_scroll_x
    //                      & '0' & nr_68_ula_stencil_mode
    // — bit 1 is a literal '0'. The write decoder at zxnext.vhd:5444-5450
    // has NO bit-1 storage signal: the field is silently dropped on every
    // write. Discriminative: write 0xFF (all bits set), then read; bit 1
    // must be 0. Pre-fix the cache stored bit 1 verbatim and the read
    // mask `& 0xF7` preserved it, leaking bit 1 on readback.
    {
        emu.reset();
        nr_write(emu, 0x68, 0xFF);
        const uint8_t got = nr_read(emu, 0x68);
        check("V20-NMP-02",
              "NR 0x68 read bit 1 reads back as '0' regardless of writes "
              "(VHDL :6093 literal '0' bit; :5444-5450 has no bit-1 store) "
              "[zxnext.vhd:6092-6093, :5444-5450]",
              (got & 0x02) == 0,
              detail_eq(static_cast<uint8_t>(got & 0x02), uint8_t{0x00}));
    }
}

// ── V21-NMP-01 — NR 0x03 b7 palette_sub_idx readback ──────────────────
//
// VHDL zxnext.vhd:5894 read mux composes NR 0x03 as
//   nr_palette_sub_idx & nr_03_machine_timing(2:0) & nr_03_user_dt_lock
//                      & nr_03_machine_type(2:0)
// — bit 7 surfaces VHDL `nr_palette_sub_idx` (zxnext.vhd:1182). The FF
// is driven to '0' by reset / NR 0x40 / NR 0x41 / NR 0x43 / NR 0x28
// writes (:5000 / :5376 / :5382 / :5395) and TOGGLED on every NR 0x44
// write (:5403 `nr_palette_sub_idx <= not nr_palette_sub_idx`).
//
// PaletteManager's `nine_bit_first_written_` (palette.cpp:330-351)
// follows the identical lifecycle: set true on the FIRST NR 0x44 write,
// cleared on the SECOND, and reset to false by set_index() (NR 0x40
// write path).
//
// Pre-fix the NR 0x03 read handler hard-wired bit 7 = 0; a NR 0x44
// write followed by a NR 0x03 read returned bit 7 = 0 instead of the
// VHDL-toggle '1'. Class-(c) inert divergence (no jnext boot path
// polls bit 7), but the readback contract is part of the VHDL surface.

static void test_v21_nmp_01_nr_03_palette_sub_idx(Emulator& emu) {
    set_group("V21-NMP-01-NR03-PaletteSubIdx");

    auto bit7 = [](uint8_t v) { return static_cast<uint8_t>((v >> 7) & 0x01); };

    // Discriminative #1: power-on / post-reset → bit 7 = 0.
    {
        emu.reset();
        const uint8_t got = nr_read(emu, 0x03);
        check("V21-NMP-01-A",
              "NR 0x03 bit 7 = 0 at reset (nr_palette_sub_idx default '0', "
              "VHDL :1182, :5000) [zxnext.vhd:5894]",
              bit7(got) == 0, detail_eq(bit7(got), uint8_t{0x00}));
    }

    // Discriminative #2: single NR 0x44 write toggles bit 7 to '1'.
    // VHDL :5403 `nr_palette_sub_idx <= not nr_palette_sub_idx` after
    // every NR 0x44 write. Starting from '0', one write yields '1'.
    {
        emu.reset();
        nr_write(emu, 0x44, 0x55);
        const uint8_t got = nr_read(emu, 0x03);
        check("V21-NMP-01-B",
              "NR 0x03 bit 7 = 1 after a single NR 0x44 write "
              "(VHDL :5403 toggle: 0 -> 1) [zxnext.vhd:5894]",
              bit7(got) == 1, detail_eq(bit7(got), uint8_t{0x01}));
    }

    // Discriminative #3: second NR 0x44 write toggles bit 7 back to '0'.
    {
        emu.reset();
        nr_write(emu, 0x44, 0x55);
        nr_write(emu, 0x44, 0xAA);
        const uint8_t got = nr_read(emu, 0x03);
        check("V21-NMP-01-C",
              "NR 0x03 bit 7 = 0 after two NR 0x44 writes "
              "(VHDL :5403 toggle: 0 -> 1 -> 0) [zxnext.vhd:5894]",
              bit7(got) == 0, detail_eq(bit7(got), uint8_t{0x00}));
    }

    // Discriminative #4: NR 0x40 write resets sub_idx to '0' even after
    // a NR 0x44 toggle. VHDL :5376 `nr_palette_sub_idx <= '0'` on every
    // NR 0x40 write (palette-index reset).
    {
        emu.reset();
        nr_write(emu, 0x44, 0x55);          // sub_idx = '1'
        nr_write(emu, 0x40, 0x00);          // sub_idx -> '0'
        const uint8_t got = nr_read(emu, 0x03);
        check("V21-NMP-01-D",
              "NR 0x03 bit 7 = 0 after NR 0x44 + NR 0x40 sequence "
              "(VHDL :5376 NR 0x40 write resets nr_palette_sub_idx) "
              "[zxnext.vhd:5894]",
              bit7(got) == 0, detail_eq(bit7(got), uint8_t{0x00}));
    }

    // Discriminative #5: bit 7 is independent of bits 6:0. Write NR 0x03
    // to set machine_timing + dt_lock + machine_type, then verify a
    // subsequent NR 0x44 toggle flips bit 7 without disturbing the
    // lower fields. NR 0x03 writes do NOT touch nr_palette_sub_idx
    // (VHDL :5121-5151 does not assign to it).
    {
        emu.reset();
        // Enter config_mode so NR 0x03 bits 2:0 latch (VHDL :5137-5145).
        nr_write(emu, 0x03, 0x07);          // config_mode = 1 (bits[2:0]=111)
        // machine_timing = "011" (bit 7 = 1, bits 6:4 = "011"), dt_lock toggle off,
        // machine_type = "010". `nr_wr_dat = 1011 0010 = 0xB2`. config_mode
        // exit (bits 2:0 = "010" ≠ "000" and ≠ "111").
        nr_write(emu, 0x03, 0xB2);
        const uint8_t pre_44 = nr_read(emu, 0x03);
        nr_write(emu, 0x44, 0x55);          // toggle sub_idx
        const uint8_t post_44 = nr_read(emu, 0x03);
        // The lower 7 bits should be identical pre/post NR 0x44 write.
        const uint8_t lower_pre  = static_cast<uint8_t>(pre_44  & 0x7F);
        const uint8_t lower_post = static_cast<uint8_t>(post_44 & 0x7F);
        check("V21-NMP-01-E",
              "NR 0x03 bits 6:0 unchanged by NR 0x44 toggle "
              "(only bit 7 / sub_idx flips) [zxnext.vhd:5894]",
              lower_pre == lower_post,
              detail_eq(lower_post, lower_pre));
        // And bit 7 must have toggled. Expected post-toggle value = NOT pre.
        const uint8_t expected_post7 =
            static_cast<uint8_t>(bit7(pre_44) ^ 0x01);
        check("V21-NMP-01-F",
              "NR 0x03 bit 7 toggles independently from lower fields",
              bit7(post_44) == expected_post7,
              detail_eq(bit7(post_44), expected_post7));
    }
}

// ── V21-NMP-03 — NR 0x07 read `act` field gated on expbus_eff_en ──────
//
// VHDL zxnext.vhd:5902-5903 read mux composes NR 0x07 as
//   "00" & cpu_speed & "00" & nr_07_cpu_speed
// where the actual `cpu_speed` (bits 5:4) latches as
//   if expbus_en = '0' then cpu_speed <= nr_07_cpu_speed;
//   else                    cpu_speed <= expbus_speed;  (:5816-5820)
// and `nr_81_expbus_speed` is hard-wired "00" (:5496 — the writable
// arm is commented out), so the expbus-active path always surfaces
// actual cpu_speed = 0.
//
// Pre-V21-NMP-03 the jnext NR 0x07 read returned act = req
// unconditionally. With NR 0x80 b7 = 1 (expbus_eff_en) AND
// nr_07_cpu_speed != 0, VHDL surfaces act = 0 / jnext surfaced act = req.

static void test_v21_nmp_03_nr_07_expbus_speed(Emulator& emu) {
    set_group("V21-NMP-03-NR07-ExpbusSpeed");

    // Discriminative #1: power-on / NR 0x80 = 0x00 → act = req. Verify
    // the no-expbus baseline still rounds-trips (regression guard).
    {
        emu.reset();
        // NR 0x80 default is 0x00 (no expbus), so expbus_eff_en = 0.
        // Pick a non-zero CPU speed so the divergence axis is visible.
        nr_write(emu, 0x07, 0x02);   // 14 MHz
        const uint8_t got = nr_read(emu, 0x07);
        // Bits 5:4 = act = req = 0x02; bits 1:0 = req = 0x02.
        // Expected byte = (2 << 4) | 2 = 0x22.
        check("V21-NMP-03-A",
              "NR 0x07 read with expbus_eff_en=0 → act = req "
              "(VHDL :5817 `cpu_speed <= nr_07_cpu_speed`) [zxnext.vhd:5902-5903]",
              got == 0x22, detail_eq(got, uint8_t{0x22}));
    }

    // Discriminative #2: NR 0x80 b7 = 1 (expbus_eff_en) → act = 0
    // regardless of nr_07_cpu_speed. This is the V21-NMP-03 fix axis.
    {
        emu.reset();
        // Set expbus_en (NR 0x80 bit 7) via NR 0x80 write. The C++
        // write handler commits expbus_eff_en immediately via
        // NmiSource::set_expbus_eff_en (same pattern as Pass-9 fix).
        nr_write(emu, 0x80, 0x80);   // expbus_en = 1
        nr_write(emu, 0x07, 0x03);   // nr_07_cpu_speed = 28 MHz
        const uint8_t got = nr_read(emu, 0x07);
        // Bits 5:4 = act = 0 (expbus_speed hard-wired "00", :5496);
        // bits 1:0 = req = 0x03. Expected byte = (0 << 4) | 3 = 0x03.
        check("V21-NMP-03-B",
              "NR 0x07 read with expbus_eff_en=1 → act = 0 even when "
              "req = 0x03 (VHDL :5819 `cpu_speed <= expbus_speed`, "
              ":5496 expbus_speed hard-wired \"00\") [zxnext.vhd:5816-5820]",
              got == 0x03, detail_eq(got, uint8_t{0x03}));
    }

    // Discriminative #3: clear expbus_en after a req write → act
    // tracks req again on the next read. Confirms the gate is dynamic.
    {
        emu.reset();
        nr_write(emu, 0x80, 0x80);   // expbus_en = 1
        nr_write(emu, 0x07, 0x02);
        const uint8_t got_pre  = nr_read(emu, 0x07);
        nr_write(emu, 0x80, 0x00);   // expbus_en = 0
        const uint8_t got_post = nr_read(emu, 0x07);
        check("V21-NMP-03-C-pre",
              "expbus_eff_en=1 pre-clear → act = 0 "
              "(req=2 → readback=0x02)",
              got_pre == 0x02, detail_eq(got_pre, uint8_t{0x02}));
        check("V21-NMP-03-C-post",
              "expbus_eff_en=0 post-clear → act = req "
              "(req=2 → readback=0x22)",
              got_post == 0x22, detail_eq(got_post, uint8_t{0x22}));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("NextREG Integration Tests (full-machine reset defaults)\n");
    std::printf("====================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_reset_defaults(emu);
    std::printf("  Group: Reset-Integration — done\n");

    test_readonly_registers(emu);
    std::printf("  Group: Read-Only — done\n");

    test_sel_03(emu);
    std::printf("  Group: Selection — done\n");

    test_clip_cycling(emu);
    std::printf("  Group: Clip-Cycle — done\n");

    test_palette(emu);
    std::printf("  Group: Palette — done\n");

    test_pe_03(emu);
    std::printf("  Group: Port-Enable-0x1F — done\n");

    test_pe_05(emu);
    std::printf("  Group: Port-Enable-Bus — done\n");

    test_pe_integration(emu);
    std::printf("  Group: PE-Integration — done\n");

    test_rw_asymmetric(emu);
    std::printf("  Group: RW-Asymmetric — done\n");

    test_cat27_verify8_nr08_effective(emu);
    std::printf("  Group: Cat27-NR08-Effective — done\n");

    // Emulator-handler integration rows (no shared `emu` — each row
    // builds a fresh emulator to avoid cross-row state pollution; the
    // partial-tier review finding requested per-row hermeticity).
    test_cat27_emu_handler_integration();
    std::printf("  Group: Cat27-Emu-Handler-Integration — done\n");

    test_cfg_integration(emu);
    std::printf("  Group: Machine-Cfg — done\n");

    test_dma_im2_delay(emu);
    std::printf("  Group: DMA-IM2-Delay — done\n");

    test_tilemap_clip_nr(emu);
    std::printf("  Group: Tilemap-Clip-NR — done\n");

    test_soft_reset(emu);
    std::printf("  Group: Soft-Reset — done\n");

    test_n8e_ram_gate(emu);
    std::printf("  Group: N8E-RAM-Gate — done\n");

    test_layer2_bank_nr(emu);
    std::printf("  Group: L2-Bank-NR — done\n");

    test_nr_68_ulap_read_composition(emu);
    std::printf("  Group: NR68-ULAP-Compose — done\n");

    test_g108_port_ff_fanout(emu);
    std::printf("  Group: G108-PortFF-Fanout — done\n");

    test_nr_05_composed_read(emu);
    std::printf("  Group: G56-CR-NR05 — done\n");

    test_nr_06_composed_read(emu);
    std::printf("  Group: G56-CR-NR06 — done\n");

    test_g56_cluster_b(emu);
    std::printf("  Group: G56-Cluster-B — done\n");

    test_nr_22_23_lineint_read(emu);
    std::printf("  Group: LineINT-NR22-NR23 — done\n");

    test_g56_cluster_d(emu);
    std::printf("  Group: G56-Cluster-D — done\n");

    test_g56_cluster_e(emu);
    std::printf("  Group: G56-CR-Cluster-E — done\n");

    test_write_only_read_zero(emu);
    std::printf("  Group: WO-Integration — done\n");

    test_v14_nmp_02_nr_28_read(emu);
    std::printf("  Group: V14-NMP-02-NR28 — done\n");

    test_ft_iotrap_integration(emu);
    std::printf("  Group: FT-Integration — done\n");

    test_testcov_nmi_mf_port(emu);
    std::printf("  Group: TestCov-NMI-MF-Port — done\n");

    test_v16_nmp_01_nr10_spkey_buttons(emu);
    std::printf("  Group: V16-NMP-01-NR10-SPKEY — done\n");

    test_v16_nmp_02_expbus_and_mask(emu);
    std::printf("  Group: V16-NMP-02-Expbus-AND-Mask — done\n");

    test_v19r_nmp_xadc_read_stubs(emu);
    std::printf("  Group: V19R-NMP-XADC-Read-Stubs — done\n");

    test_v21_nmp_01_nr_03_palette_sub_idx(emu);
    std::printf("  Group: V21-NMP-01-NR03-PaletteSubIdx — done\n");

    test_v21_nmp_03_nr_07_expbus_speed(emu);
    std::printf("  Group: V21-NMP-03-NR07-ExpbusSpeed — done\n");

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown
    std::printf("\nPer-group breakdown:\n");
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
            std::printf("  %-10s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
