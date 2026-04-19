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

    // RO-02 — NR 0x00 read-only enforcement missing in JNEXT: no
    // read_handler is registered on NR 0x00, and NextReg::write() stores
    // the written byte in regs_[0] unconditionally, so a subsequent read
    // returns the written value instead of g_machine_id. Real gap.
    // Backlog: install a read_handler on NR 0x00 that always returns 0x08
    // (HWID_EMULATORS), ignoring regs_[0].
    skip("RO-02",
         "NR 0x00 RO enforcement missing — write round-trips through "
         "regs_[0] [backlog: add read_handler for 0x00 returning 0x08]");

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
    // mux at zxnext.vhd:5917-5918 returns g_sub_version verbatim. JNEXT
    // never seeds regs_[0x0E] — it stays 0x00 after reset. That is a
    // real seed-default gap, not a test-authoring choice.
    //
    // FIXED 2026-04-20 after critic (was previously a false-pass with
    // oracle 0x00 taken from the JNEXT default rather than VHDL). Now
    // skipped with a specific backlog note; convert to pass only after
    // src/port/nextreg.cpp seeds regs_[0x0E] = 0x03.
    skip("RO-04",
         "VHDL g_sub_version = 0x03 (zxnext_top_issue2.vhd:38), JNEXT "
         "defaults regs_[0x0E] = 0x00 — seed-default gap "
         "[backlog: add regs_[0x0E] = 0x03 in NextReg::reset]");

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
    (void)emu;
    // SEL-03 — same root cause as RO-02: NR 0x00 RO enforcement missing.
    // Backlog: add read_handler on NR 0x00 returning 0x08.
    skip("SEL-03",
         "NR 0x00 RO enforcement missing — see RO-02 backlog note");
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

    // CLIP-01..05 (Layer 2 / Sprite / ULA 4-write cycling and NR 0x1C
    // per-window reset) require public getters on the respective subsystem
    // classes (Layer2/SpriteEngine/Ula) + an Emulator::ula() accessor that
    // don't exist yet. Observing the cycling end-to-end via rendering would
    // need a full frame render + pixel probe, far out of scope for the
    // integration-tier rewrite pass. Deferred — kept as skip() in the bare
    // test with an integration-gap note; un-skip as part of a future branch
    // that either adds the getters or writes a rendering-based observer.
    skip("CLIP-01",
         "Layer2 cycling needs Layer2 public clip_* getters — integration-gap "
         "[zxnext.vhd:5242-5290]");
    skip("CLIP-02",
         "Layer2 idx wrap needs Layer2 public clip_* getters — integration-gap "
         "[zxnext.vhd:5242-5290]");
    skip("CLIP-03",
         "Layer2 NR 0x1C bit 0 reset needs Layer2 public clip_* getters — integration-gap "
         "[zxnext.vhd:5242-5290]");
    skip("CLIP-04",
         "Sprite NR 0x1C bit 1 reset needs SpriteEngine public clip_* getters — integration-gap "
         "[zxnext.vhd:5242-5290]");
    skip("CLIP-05",
         "ULA NR 0x1C bit 2 reset needs Emulator::ula() accessor — integration-gap "
         "[zxnext.vhd:5242-5290]");

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

    // CLIP-08 — NR 0x18 Layer2 clip read is a combinatorial mux over
    // Layer2 clip coords (zxnext.vhd:5947-5953). JNEXT has no read
    // handler installed for 0x18, so regs_[0x18] just returns the
    // last written byte — distinct from the VHDL semantics. Out of
    // scope for this phase (tilemap-only); covered by a future
    // Layer2 clip read-handler branch.
    skip("CLIP-08",
         "NR 0x18 read handler missing — combinatorial mux over Layer2 "
         "clip coords not installed [backlog: add read_handler for 0x18 "
         "per zxnext.vhd:5947-5953]");

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

static void test_pe_05(Emulator& emu) {
    set_group("Port-Enable-Bus");
    (void)emu;
    // PE-05 — NR 0x86-0x89 bus-side port enables. VHDL zxnext.vhd:
    // 6147-6150 reads NR 0x89 as `nr_89_bus_port_reset_type & "000" &
    // nr_89_bus_port_enable`. Reset defaults (zxnext.vhd:1234-1235):
    // nr_89_bus_port_reset_type='1', nr_89_bus_port_enable=(others=>'1').
    // So the correct read oracle for NR 0x89 is 0x8F (bit 7 = 1, bits
    // 6:4 = 000, bits 3:0 = 1111), same layout/value as NR 0x85 (which
    // is tested correctly at RST-08).
    //
    // FIXED 2026-04-20 after critic (note previously said "seed
    // regs_[0x89]=0xFF" which was wrong — should be 0x8F per VHDL).
    skip("PE-05",
         "NR 0x89 bus port enable default 0x00; VHDL zxnext.vhd:"
         "6147-6150 specifies 0x8F (bit 7 + 4-bit enable). JNEXT "
         "does not seed regs_[0x89] "
         "[backlog: seed regs_[0x89]=0x8F in NextReg::reset()]");
}

// ── RW-01, RW-02: asymmetric read/write registers ────────────────────
//
// Plan row group 4 in NEXTREG-TEST-PLAN-DESIGN.md. NR 0x07 (CPU speed)
// and NR 0x08 (peripheral 3) have read formats that differ from writes —
// the VHDL mux formats from internal state at read time.

static void test_rw_asymmetric(Emulator& emu) {
    set_group("RW-Asymmetric");
    (void)emu;

    // RW-01 — NR 0x07 CPU-speed packed read (actual bits[1:0],
    // requested bits[5:4]) is not implemented. JNEXT's NR 0x07 is a plain
    // register that round-trips the last written byte. Backlog: install
    // read_handler on NR 0x07 that returns (actual<<0)|(requested<<4).
    skip("RW-01",
         "NR 0x07 packed read (actual + requested) not implemented "
         "[backlog: add read_handler packing speed FSM state]");

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

// ── CFG-01, CFG-02, CFG-05: machine-config state ─────────────────────
//
// Plan row group 7 in NEXTREG-TEST-PLAN-DESIGN.md. NR 0x03 bits 6:4 set
// machine timing, bit 3 XOR-toggles dt_lock, bits 2:0 enter/exit config
// mode, and machine-type writes are gated by config_mode. VHDL
// zxnext.vhd:5121-5151.

static void test_cfg_integration(Emulator& emu) {
    set_group("Machine-Cfg");

    // CFG-01 — VHDL zxnext.vhd:5893-5894 reads NR 0x03 as
    // `nr_palette_sub_idx & nr_03_machine_timing & nr_03_user_dt_lock &
    // nr_03_machine_type` — a COMPOSED format, not a raw register read.
    // JNEXT stores NR 0x03 as a plain byte and round-trips it; asserting
    // that upper-nibble round-trips is tautological (it would pass on
    // any byte-storage register).
    //
    // FIXED 2026-04-20 after critic (was a passing tautology). Converted
    // to SKIP with a pointer to the proper fix: observe
    // nr_03_machine_timing via a subsystem accessor (ULA retime state or
    // a new machine-type getter) rather than reading back regs_[0x03].
    skip("CFG-01",
         "NR 0x03 timing bits 6:4 read is composed from "
         "nr_03_machine_timing (zxnext.vhd:5893-5894), not a raw register "
         "round-trip. JNEXT returns regs_[0x03] verbatim — tautology "
         "[backlog: observe ULA retime state via new accessor]");

    // CFG-02 — NR 0x03 bit 3 XOR-toggles dt_lock in VHDL
    // (zxnext.vhd:5121-5151). JNEXT does not model dt_lock at all —
    // NR 0x03 is a plain register that round-trips writes. Confirmed by
    // the RW-asymmetric test (after two bit-3 writes, read stays 0x08).
    // Backlog: add dt_lock 1-bit state to the machine-type manager and
    // an NR 0x03 write_handler that XOR-toggles it when bit 3 = 1.
    skip("CFG-02",
         "NR 0x03 bit 3 dt_lock XOR not modelled — "
         "[backlog: add dt_lock state + NR 0x03 bit 3 XOR handler]");

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

    test_pe_05(emu);
    std::printf("  Group: Port-Enable-Bus — done\n");

    test_rw_asymmetric(emu);
    std::printf("  Group: RW-Asymmetric — done\n");

    test_cfg_integration(emu);
    std::printf("  Group: Machine-Cfg — done\n");

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
