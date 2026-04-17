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

std::string detail_eq(uint8_t got, uint8_t expected) {
    return "got=" + hex2(got) + " expected=" + hex2(expected);
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

// ── 3. Reset Defaults (RST-01..09) — integration tier ────────────────
//
// These rows were skipped in the bare-NextReg test (nextreg_test.cpp)
// because the VHDL reset defaults are owned by subsystem classes, not by
// the bare NextReg register file. Here we verify them through the full
// emulator wiring.

static void test_reset_defaults(Emulator& emu) {
    set_group("Reset-Integration");

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

    std::printf("\n====================================\n");
    std::printf("Total: %d  Passed: %d  Failed: %d  Skipped: %zu\n",
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
