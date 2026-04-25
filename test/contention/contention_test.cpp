// Contention Model compliance test suite (Phase-A live rows + scaffolding).
//
// Plan of record: doc/testing/CONTENTION-TEST-PLAN-DESIGN.md (68 rows,
// phased A=28 / B=36 / C=4). The 21 Phase-A rows owned by Branch B of
// the 2026-04-25 wave are now live (§4 enable gate, §5/§6/§7 memory
// decode, §12 Pentagon + turbo subset). The §8 Phase-A I/O rows land in
// Branch C (uses the new port_contend() accessor from Branch A's audit).
// All §9-§14 Phase-B/C rows remain skip() pending tick-loop wiring.
//
// Skip-reason taxonomy (per the session prompt 2026-04-24):
//   F-CT-AUDIT  — Phase A rows; bare-class tests pending the VHDL audit
//                 of src/memory/contention.{h,cpp} against
//                 zxnext.vhd:4481-4520 + 5787-5828.
//   F-CT-DELAY  — Phase B rows; full-Emulator tests pending the runtime
//                 wiring of ContentionModel::delay() into the Z80 tick
//                 loop (today jnext uses FUSE ula_contention[] table).
//   F-CT-INT    — Phase C rows; integration-smoke rows that depend on
//                 Phase B wiring being live plus additional cross-suite
//                 reference capture.
//
// Structural template: test/ula/ula_integration_test.cpp and
// test/ctc_interrupts/ctc_interrupts_test.cpp.
//
// Migration note: the 13 CON-* rows previously in test/mmu/mmu_test.cpp
// (Cat-16) have been deleted in this same commit per the plan §15 C2-move
// commitment; the CT-* rows below cover the same VHDL semantics.
//
// Run: ./build/test/contention_test

#include "memory/contention.h"

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
    std::string group;
    std::string id;
    std::string desc;
    std::string reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

[[maybe_unused]] void check(const char* id, const char* desc, bool cond,
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

void skip(const char* id, const char* desc, const char* reason) {
    g_skipped.push_back({g_group, id, desc, reason});
}

} // namespace

// ══════════════════════════════════════════════════════════════════════
// §4. Enable gate — i_contention_en composition (8 rows, Phase A)
// VHDL: zxnext.vhd:4481, 5800-5823
// ══════════════════════════════════════════════════════════════════════

static void test_gate_enable() {
    set_group("CT-GATE");

    // CT-GATE-01: ZX48K + page=0x0A (bank 5) — VHDL zxnext.vhd:4481 enable
    // gate ON (defaults), and zxnext.vhd:4490 mem_contend asserts on
    // page(3:1)="101". Combined ⇒ contended.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        check("CT-GATE-01",
              "ZX48K, mem_active_page=0x0A → is_contended_access()==true "
              "[zxnext.vhd:4481,4490]",
              cm.is_contended_access());
    }

    // CT-GATE-02: contention_disable (NR 0x08 bit 6) gates the enable
    // line off — VHDL zxnext.vhd:4481 (eff_nr_08_contention_disable).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        cm.set_contention_disable(true);
        check("CT-GATE-02",
              "ZX48K, page=0x0A, set_contention_disable(true) → false "
              "[zxnext.vhd:4481]",
              !cm.is_contended_access());
    }

    // CT-GATE-03: cpu_speed=1 (7 MHz) — VHDL zxnext.vhd:4481 requires
    // cpu_speed(1)='0' AND cpu_speed(0)='0'. Speed 1 trips bit 0.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        cm.set_cpu_speed(1);
        check("CT-GATE-03",
              "ZX48K, page=0x0A, set_cpu_speed(1) → false "
              "[zxnext.vhd:4481,5817]",
              !cm.is_contended_access());
    }

    // CT-GATE-04: cpu_speed=2 (14 MHz) trips cpu_speed(1).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        cm.set_cpu_speed(2);
        check("CT-GATE-04",
              "ZX48K, page=0x0A, set_cpu_speed(2) → false "
              "[zxnext.vhd:4481,5817]",
              !cm.is_contended_access());
    }

    // CT-GATE-05: cpu_speed=3 (28 MHz) trips both bits.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        cm.set_cpu_speed(3);
        check("CT-GATE-05",
              "ZX48K, page=0x0A, set_cpu_speed(3) → false "
              "[zxnext.vhd:4481,5817]",
              !cm.is_contended_access());
    }

    // CT-GATE-06: pentagon_timing flag gates the enable line off — VHDL
    // zxnext.vhd:4481 `not machine_timing_pentagon`. Discriminative
    // (uses ZX48K type with the Pentagon flag flipped on) so a broken
    // gate cannot pass via the Pentagon switch fall-through.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        cm.set_pentagon_timing(true);
        check("CT-GATE-06",
              "ZX48K, page=0x0A, set_pentagon_timing(true) → false "
              "[zxnext.vhd:4481]",
              !cm.is_contended_access());
    }

    // CT-GATE-07: all gate inputs at VHDL power-on defaults
    // (contention_disable=0, cpu_speed=00, pentagon_timing=0) plus
    // page=0x0A on ZX48K ⇒ contended. Sanity row that the gate is OPEN
    // when nothing intervenes.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        check("CT-GATE-07",
              "ZX48K, all gates off, page=0x0A → true "
              "[zxnext.vhd:4481,4490]",
              cm.is_contended_access());
    }

    // CT-GATE-08: default-constructed ContentionModel — no build() call.
    // type_ defaults to MachineType::ZXN_ISSUE2 (contention.h:56), and
    // the switch fallthrough at contention.cpp:87-90 returns false.
    // Exercises the ZXN_ISSUE2 branch.
    {
        ContentionModel cm;
        cm.set_mem_active_page(0x0A);
        check("CT-GATE-08",
              "Default-constructed model (no build()), page=0x0A → false "
              "[src/memory/contention.h:56; contention.cpp:87-90]",
              !cm.is_contended_access());
    }
}

// ══════════════════════════════════════════════════════════════════════
// §5. Memory contention — 48K (5 rows, Phase A)
// VHDL: zxnext.vhd:4489-4490
// ══════════════════════════════════════════════════════════════════════

static void test_mem_48k() {
    set_group("CT-M48");

    // CT-M48-01: page 0x0A — bank 5 mapping; bits(3:1)=101.
    // VHDL zxnext.vhd:4490 fires.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        check("CT-M48-01",
              "48K, page=0x0A (bank 5, bits(3:1)=101) → contended "
              "[zxnext.vhd:4490]",
              cm.is_contended_access());
    }

    // CT-M48-03: page 0x00 — bits(3:1)=000 ≠ 101.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x00);
        check("CT-M48-03",
              "48K, page=0x00 (bank 0, bits(3:1)=000) → not contended "
              "[zxnext.vhd:4490]",
              !cm.is_contended_access());
    }

    // CT-M48-05: page 0x0E — bits(3:1)=111 ≠ 101.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0E);
        check("CT-M48-05",
              "48K, page=0x0E (bank 7, bits(3:1)=111) → not contended "
              "[zxnext.vhd:4490]",
              !cm.is_contended_access());
    }

    // CT-M48-06: high-nibble guard — VHDL zxnext.vhd:4489 first clause
    // forces mem_contend='0' whenever mem_active_page(7:4) /= "0000".
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x10);
        check("CT-M48-06",
              "48K, page=0x10 (high nibble != 0) → not contended "
              "[zxnext.vhd:4489]",
              !cm.is_contended_access());
    }

    // CT-M48-08: page 0xFF — floating-bus sentinel; high-nibble guard
    // zeroes mem_contend.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0xFF);
        check("CT-M48-08",
              "48K, page=0xFF (floating-bus sentinel) → not contended "
              "[zxnext.vhd:4489]",
              !cm.is_contended_access());
    }
}

// ══════════════════════════════════════════════════════════════════════
// §6. Memory contention — 128K (3 rows, Phase A)
// VHDL: zxnext.vhd:4491
// ══════════════════════════════════════════════════════════════════════

static void test_mem_128k() {
    set_group("CT-M128");

    // CT-M128-01: 128K page 0x02 — bit(1)=1 (odd bank). VHDL:4491.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_mem_active_page(0x02);
        check("CT-M128-01",
              "128K, page=0x02 (bank 1, bit(1)=1) → contended "
              "[zxnext.vhd:4491]",
              cm.is_contended_access());
    }

    // CT-M128-03: 128K page 0x04 — bit(1)=0 (even bank).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_mem_active_page(0x04);
        check("CT-M128-03",
              "128K, page=0x04 (bank 2, bit(1)=0) → not contended "
              "[zxnext.vhd:4491]",
              !cm.is_contended_access());
    }

    // CT-M128-08: 128K high-nibble guard — VHDL:4489 zeroes mem_contend.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_mem_active_page(0x10);
        check("CT-M128-08",
              "128K, page=0x10 (high nibble != 0) → not contended "
              "[zxnext.vhd:4489]",
              !cm.is_contended_access());
    }
}

// ══════════════════════════════════════════════════════════════════════
// §7. Memory contention — +3 (3 rows, Phase A)
// VHDL: zxnext.vhd:4492
// ══════════════════════════════════════════════════════════════════════

static void test_mem_plus3() {
    set_group("CT-MP3");

    // CT-MP3-01: +3 page 0x08 — bit(3)=1 (banks ≥ 4). VHDL:4492.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x08);
        check("CT-MP3-01",
              "+3, page=0x08 (bank 4, bit(3)=1) → contended "
              "[zxnext.vhd:4492]",
              cm.is_contended_access());
    }

    // CT-MP3-05: +3 page 0x00 — bit(3)=0 (banks < 4).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x00);
        check("CT-MP3-05",
              "+3, page=0x00 (bank 0, bit(3)=0) → not contended "
              "[zxnext.vhd:4492]",
              !cm.is_contended_access());
    }

    // CT-MP3-08: +3 page 0xF0 — ROM-style high page; high-nibble guard
    // VHDL:4489 zeroes mem_contend regardless of low nibble.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0xF0);
        check("CT-MP3-08",
              "+3, ROM access (page >= 0xF0) → not contended "
              "[zxnext.vhd:4489]",
              !cm.is_contended_access());
    }
}

// ══════════════════════════════════════════════════════════════════════
// §8. I/O port contention (9 rows, mixed Phase A / Phase B)
// VHDL: zxnext.vhd:4496, 2594, 2685-2686
//
// Phase A rows (bare-class port_contend decode):
//   CT-IO-01, CT-IO-02, CT-IO-03, CT-IO-04, CT-IO-07, CT-IO-08, CT-IO-09
// Phase B rows (require full Emulator for port_7ffd_active wiring):
//   CT-IO-05, CT-IO-06
// ══════════════════════════════════════════════════════════════════════

static void test_io_port() {
    set_group("CT-IO");

    skip("CT-IO-01",
         "48K, cpu_a=0xFE (even port, ULA) → port_contend=1 "
         "[zxnext.vhd:4496]",
         "F-CT-AUDIT");
    skip("CT-IO-02",
         "48K, cpu_a=0xFF (odd port, non-ULA) → port_contend=0 "
         "[zxnext.vhd:4496]",
         "F-CT-AUDIT");
    skip("CT-IO-03",
         "48K, cpu_a=0x00 (even, lowest) → port_contend=1 "
         "[zxnext.vhd:4496]",
         "F-CT-AUDIT");
    skip("CT-IO-04",
         "48K, cpu_a=0x01 (odd, lowest) → port_contend=0 "
         "[zxnext.vhd:4496]",
         "F-CT-AUDIT");
    skip("CT-IO-05",
         "128K, cpu_a=0x7FFD (odd + port_7ffd_active=1) → port_contend=1 "
         "[zxnext.vhd:4496,2594]",
         "F-CT-DELAY");
    skip("CT-IO-06",
         "48K, cpu_a=0x7FFD (port_7ffd_active=0 on 48K) → port_contend=0 "
         "[zxnext.vhd:4496,2594]",
         "F-CT-DELAY");
    skip("CT-IO-07",
         "Any timing, cpu_a=0xBF3B (ULA+ index, port_bf3b OR-term) → 1 "
         "[zxnext.vhd:4496,2685]",
         "F-CT-AUDIT");
    skip("CT-IO-08",
         "Any timing, cpu_a=0xFF3B (ULA+ data, port_ff3b OR-term) → 1 "
         "[zxnext.vhd:4496,2686]",
         "F-CT-AUDIT");
    skip("CT-IO-09",
         "Any timing, cpu_a=0xBF3B, port_ulap_io_en=0 → port_contend=0 "
         "[zxnext.vhd:4496,2685]",
         "F-CT-AUDIT");
}

// ══════════════════════════════════════════════════════════════════════
// §9. Wait-pattern window — hc/vc/phase gates (10 rows, Phase B)
// VHDL: zxula.vhd:178, 582-583, 587-595
// ══════════════════════════════════════════════════════════════════════

static void test_wait_window() {
    set_group("CT-WIN");

    skip("CT-WIN-01",
         "48K, hc=0, vc=0 (hc_adj=1 → (3:2)=00) → wait_s=0 "
         "[zxula.vhd:582-583]",
         "F-CT-DELAY");
    skip("CT-WIN-02",
         "48K, hc=3, vc=100 (hc_adj=4 → (3:2)=01) → wait_s=1 "
         "[zxula.vhd:582-583]",
         "F-CT-DELAY");
    skip("CT-WIN-03",
         "48K, hc=15, vc=100 (hc_adj wraps to 0) → wait_s=0 "
         "[zxula.vhd:178,582-583]",
         "F-CT-DELAY");
    skip("CT-WIN-04",
         "48K, hc=255, vc=100 (last display column, wrap) → wait_s=0 "
         "[zxula.vhd:178,582-583]",
         "F-CT-DELAY");
    skip("CT-WIN-05",
         "48K, hc=256, vc=100 (hc(8)=1 window gate off) → wait_s=0 "
         "[zxula.vhd:583]",
         "F-CT-DELAY");
    skip("CT-WIN-06",
         "48K, hc=100, vc=192 (border_active_v=1) → wait_s=0 "
         "[zxula.vhd:583]",
         "F-CT-DELAY");
    skip("CT-WIN-07",
         "48K, hc=100, vc sweep 0..191, bank-5 mem cycle: per-phase LUT "
         "{6,5,4,3,2,1,0,0} [zxula.vhd:579-583,587-595]",
         "F-CT-DELAY");
    skip("CT-WIN-08",
         "+3, hc_adj=1 (hc_adj(3:1)=000 AND p3=1) → wait_s=1 "
         "[zxula.vhd:582-583]",
         "F-CT-DELAY");
    skip("CT-WIN-09",
         "48K, hc=16, vc=100 (hc_adj=1 after 4-bit wrap) → wait_s=0 "
         "[zxula.vhd:178,582-583]",
         "F-CT-DELAY");
    skip("CT-WIN-10",
         "48K, hc=7, vc=100 (hc_adj=8, (3:2)=10) → wait_s=1, pattern[7]=0 "
         "[zxula.vhd:582-583,587-595]",
         "F-CT-DELAY");
}

// ══════════════════════════════════════════════════════════════════════
// §10. 48K / 128K — clock-stretch path (8 rows, Phase B)
// VHDL: zxula.vhd:587-595
// ══════════════════════════════════════════════════════════════════════

static void test_stretch_48k() {
    set_group("CT-S48");

    skip("CT-S48-01",
         "48K, bank 5 memory read, display window, stretched phase "
         "→ non-zero LUT delay [zxula.vhd:587-595]",
         "F-CT-DELAY");
    skip("CT-S48-02",
         "48K, bank 5 memory read, display window, non-stretched phase "
         "→ zero added T-states [zxula.vhd:582-595]",
         "F-CT-DELAY");
    skip("CT-S48-03",
         "48K, bank 0 memory read (never contended) → zero added T-states "
         "[zxula.vhd:595; zxnext.vhd:4490]",
         "F-CT-DELAY");
    skip("CT-S48-04",
         "128K, bank 1 memory read, display window, stretched phase "
         "→ LUT delay [zxula.vhd:587-595; zxnext.vhd:4491]",
         "F-CT-DELAY");
    skip("CT-S48-05",
         "128K, bank 4 memory read (even bank, not contended) → zero "
         "[zxnext.vhd:4491]",
         "F-CT-DELAY");
    skip("CT-S48-06",
         "48K, I/O port 0xFE (port_contend=1), stretched phase → "
         "o_cpu_contend=1 [zxula.vhd:587-595; zxnext.vhd:4496]",
         "F-CT-DELAY");
    skip("CT-S48-07",
         "48K, I/O port 0xFF (port_contend=0), display window → zero "
         "[zxnext.vhd:4496]",
         "F-CT-DELAY");
    skip("CT-S48-08",
         "48K, memory read outside display window (border_active_v=1) "
         "→ zero [zxula.vhd:583]",
         "F-CT-DELAY");
}

// ══════════════════════════════════════════════════════════════════════
// §11. +3 — WAIT_n path (8 rows, Phase B)
// VHDL: zxula.vhd:599-600
// ══════════════════════════════════════════════════════════════════════

static void test_stretch_plus3() {
    set_group("CT-SP3");

    skip("CT-SP3-01",
         "+3, bank 4 memory read, display window, stretched phase "
         "→ WAIT_n=0 for N cycles [zxula.vhd:600]",
         "F-CT-DELAY");
    skip("CT-SP3-02",
         "+3, bank 7 memory read, display window, stretched phase "
         "→ LUT stall [zxula.vhd:600; zxnext.vhd:4492]",
         "F-CT-DELAY");
    skip("CT-SP3-03",
         "+3, bank 0 memory read (page bit 3=0) → zero added T-states "
         "[zxnext.vhd:4492]",
         "F-CT-DELAY");
    skip("CT-SP3-04",
         "+3, bank 4 memory read outside display window → zero "
         "[zxula.vhd:583,600]",
         "F-CT-DELAY");
    skip("CT-SP3-05",
         "+3, bank 4 memory read with contention_disable=1 → zero "
         "[zxnext.vhd:4481]",
         "F-CT-DELAY");
    skip("CT-SP3-06",
         "+3, I/O read port 0xFE in display window → zero from WAIT path "
         "(live VHDL is memory-only) [zxula.vhd:599-600]",
         "F-CT-DELAY");
    skip("CT-SP3-07",
         "+3, I/O read port 0xFE in display window, contended MMU bank "
         "→ zero from WAIT path [zxula.vhd:599-600]",
         "F-CT-DELAY");
    skip("CT-SP3-08",
         "+3, hc_adj(3:1)=000 extra phase, bank 4 read → stall asserts "
         "[zxula.vhd:582-583,600]",
         "F-CT-DELAY");
}

// ══════════════════════════════════════════════════════════════════════
// §12. Pentagon and Next-turbo — never-contended paths (7 rows, mixed)
// VHDL: zxnext.vhd:4481, 4489-4493, 5801, 5817, 5822-5823, 5787-5790
//
// Phase A rows (bare-class): CT-PENT-01, CT-TURBO-01
// Phase B rows (full Emulator): CT-PENT-04, CT-TURBO-04, -05, -06
// Phase C row (integration):   CT-PENT-05
// ══════════════════════════════════════════════════════════════════════

static void test_pent_turbo() {
    set_group("CT-PENT-TURBO");

    // CT-PENT-01: Pentagon machine type, page=0x0A. build(PENTAGON) sets
    // pentagon_timing_=true so the VHDL:4481 enable gate fires; in
    // addition, the PENTAGON case in is_contended_access() falls through
    // to the default-false return at contention.cpp:87-90, exercising the
    // switch fallthrough. Both belt and suspenders: not contended.
    {
        ContentionModel cm;
        cm.build(MachineType::PENTAGON);
        cm.set_mem_active_page(0x0A);
        check("CT-PENT-01",
              "Pentagon, page=0x0A (would contend on 48K): enable gate AND "
              "mem_contend switch-fallthrough both suppress → not contended "
              "[zxnext.vhd:4481,4489-4493; contention.cpp:87-90]",
              !cm.is_contended_access());
    }
    skip("CT-PENT-04",
         "Pentagon, full Emulator, I/O port 0xFE → zero added T-states "
         "(enable gate blocks) [zxnext.vhd:4481]",
         "F-CT-DELAY");
    skip("CT-PENT-05",
         "Pentagon, full-frame of contended program vs 48K → frame "
         "matches Pentagon 71680 T-state budget, no contention added "
         "[zxnext.vhd:4481; zxula_timing.vhd]",
         "F-CT-INT");
    // CT-TURBO-01: 48K + cpu_speed=1 (7 MHz). VHDL:4481 enable gate
    // requires cpu_speed(0)='0' AND cpu_speed(1)='0'; speed 1 trips
    // bit 0 ⇒ gate forced low.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        cm.set_cpu_speed(1);
        check("CT-TURBO-01",
              "48K, cpu_speed=1 (7 MHz), page=0x0A → not contended (gate off) "
              "[zxnext.vhd:4481,5817]",
              !cm.is_contended_access());
    }
    skip("CT-TURBO-04",
         "48K full Emulator, write NR 0x07=0x01 then bank 5 read → zero "
         "added T-states (NR 0x07 → cpu_speed path) "
         "[zxnext.vhd:5787-5790,5817]",
         "F-CT-DELAY");
    skip("CT-TURBO-05",
         "48K full Emulator, write NR 0x08 bit 6=1 then bank 5 read → "
         "zero added T-states (NR 0x08 bit 6 → contention_disable path) "
         "[zxnext.vhd:4481,5823]",
         "F-CT-DELAY");
    skip("CT-TURBO-06",
         "48K full Emulator, write NR 0x08 bit 6 mid-scanline; read "
         "before vs. after next hc(8) rising edge → pre-commit contended, "
         "post-commit uncontended (hc(8) commit gate) "
         "[zxnext.vhd:5822-5823]",
         "F-CT-DELAY");
}

// ══════════════════════════════════════════════════════════════════════
// §13. p3_floating_bus_dat capture on contended memory access
// (4 rows, Phase B). VHDL: zxnext.vhd:4498-4509
// ══════════════════════════════════════════════════════════════════════

static void test_floating_bus_capture() {
    set_group("CT-FB");

    skip("CT-FB-01",
         "+3, memory read bank 4, any display phase → "
         "p3_floating_bus_dat == byte read [zxnext.vhd:4498-4505]",
         "F-CT-DELAY");
    skip("CT-FB-02",
         "+3, memory write bank 4, any display phase → "
         "p3_floating_bus_dat == byte written [zxnext.vhd:4498-4508]",
         "F-CT-DELAY");
    skip("CT-FB-03",
         "+3, pre-seed latch with bank-4 access then read bank 0 "
         "(non-contended) → latch unchanged (gated by mem_contend) "
         "[zxnext.vhd:4498-4501]",
         "F-CT-DELAY");
    skip("CT-FB-04",
         "+3, I/O read (no MREQ) to contended page → latch unchanged "
         "(capture gated on MREQ) [zxnext.vhd:4501]",
         "F-CT-DELAY");
}

// ══════════════════════════════════════════════════════════════════════
// §14. Integration smoke (runtime drift) (3 rows, Phase C)
// Full Emulator + ContentionModel::delay() wired on tick path.
// ══════════════════════════════════════════════════════════════════════

static void test_integration_smoke() {
    set_group("CT-INT");

    skip("CT-INT-01",
         "48K HALT-in-loop program sized 1 frame, contention ON → frame "
         "T-state count matches VHDL-derived total (69888 + LUT stretch) "
         "[zxula.vhd:582-595; zxula_timing.vhd]",
         "F-CT-INT");
    skip("CT-INT-02",
         "48K same program, contention OFF via NR 0x08 bit 6 → frame "
         "T-state count matches 69888 baseline [zxnext.vhd:4481,5823]",
         "F-CT-INT");
    skip("CT-INT-03",
         "Regression screenshot suite — 48K contention-sensitive demo "
         "matches reference (baseline re-captured when contention lands)",
         "F-CT-INT");
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Contention Model Compliance Tests\n");
    std::printf("=================================\n\n");
    std::printf("  (Phase-A live: 21 rows from §4/§5/§6/§7/§12;\n");
    std::printf("   §8 Phase-A I/O lands in Branch C of the wave;\n");
    std::printf("   §9-§14 Phase-B/C remain skipped pending tick-loop wiring)\n\n");

    test_gate_enable();
    std::printf("  Group: CT-GATE        — done\n");

    test_mem_48k();
    std::printf("  Group: CT-M48         — done\n");

    test_mem_128k();
    std::printf("  Group: CT-M128        — done\n");

    test_mem_plus3();
    std::printf("  Group: CT-MP3         — done\n");

    test_io_port();
    std::printf("  Group: CT-IO          — done\n");

    test_wait_window();
    std::printf("  Group: CT-WIN         — done\n");

    test_stretch_48k();
    std::printf("  Group: CT-S48         — done\n");

    test_stretch_plus3();
    std::printf("  Group: CT-SP3         — done\n");

    test_pent_turbo();
    std::printf("  Group: CT-PENT-TURBO  — done\n");

    test_floating_bus_capture();
    std::printf("  Group: CT-FB          — done\n");

    test_integration_smoke();
    std::printf("  Group: CT-INT         — done\n");

    std::printf("\n=================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

    // Per-group breakdown (live rows only — empty in this scaffolding
    // commit; included for template symmetry with the ULA/CTC suites).
    if (!g_results.empty()) {
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
    }

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows (grouped by section):\n");
        std::string last;
        int gcount = 0;
        for (const auto& s : g_skipped) {
            if (s.group != last) {
                if (!last.empty())
                    std::printf("  %-22s (%d skipped)\n", last.c_str(), gcount);
                last = s.group;
                gcount = 0;
            }
            ++gcount;
        }
        if (!last.empty())
            std::printf("  %-22s (%d skipped)\n", last.c_str(), gcount);
        std::printf("  (%zu total skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
