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

#include <cstdio>
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

} // namespace

// ── Emulator construction helper ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.roms_directory = "/usr/share/fuse";
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
    // VHDL zxnext.vhd:5242-5290 — tilemap clip resets to
    //   x1=0, x2=0x9F, y1=0, y2=0xFF.
    // The clip window is written via a 4-write rotating index on NR 0x1B.
    // Reading NR 0x1B should cycle through the clip values. However, the
    // current emulator does not implement clip READ cycling on NR 0x1B
    // (only write cycling is implemented). The tilemap clip defaults live
    // inside the TilemapEngine subsystem and are not directly readable via
    // the NextREG port path without a read handler.
    skip("RST-09",
         "NR 0x1B clip read cycling not implemented — tilemap clip "
         "defaults owned by TilemapEngine, not readable via NR port "
         "[zxnext.vhd:5242-5290]. Un-skip when clip READ handlers "
         "are added to Emulator::init().");
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

    test_dma_im2_delay(emu);
    std::printf("  Group: DMA-IM2-Delay — done\n");

    test_tilemap_clip_nr(emu);
    std::printf("  Group: Tilemap-Clip-NR — done\n");

    test_soft_reset(emu);
    std::printf("  Group: Soft-Reset — done\n");

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
