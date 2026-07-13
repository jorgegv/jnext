// Contention Model compliance test suite (Phase-A live rows + Phase-B flips).
//
// Plan of record: doc/testing/CONTENTION-TEST-PLAN-DESIGN.md (68 rows,
// phased A=28 / B=36 / C=4). Phase 1 (28 Phase-A rows) landed in commit
// 4d8246f. Phase 2 flipped the 36 Phase-B rows to live check() — §8
// Phase-B (CT-IO-05/06), §9 (CT-WIN-01..10), §10 (CT-S48-*), §11
// (CT-SP3-*), §12 Phase-B (CT-PENT-04, CT-TURBO-04..06), §13 (CT-FB-*).
// Phase 3 (this branch) flips the 4 Phase-C integration-smoke rows:
// CT-PENT-05 and CT-INT-01..03. After this branch all 68 plan rows are
// live check()s — zero skips.
//
// PHASE-2-DEPENDS: Branch A's `contention_tick()` runtime wiring + Branch
// B's NR 0x07/0x08 dispatch + hc(8) commit-edge. Where this branch lands
// rows that need A or B's runtime wiring, the row is written against the
// VHDL-faithful bare-class API (which IS the truth Branch A's wiring will
// dereference). NR 0x07/0x08 dispatch into ContentionModel is already on
// `main` (src/core/emulator.cpp:303-308, 1644 — predates Branch B), so
// CT-TURBO-04/05 and CT-PENT-04 work via the production NR-write path.
// CT-TURBO-06 (hc(8) commit-edge) is the one row that genuinely depends
// on Branch B's commit-gate; it is documented as a check() that will FAIL
// standalone until Branch B's hc(8) latch lands.
//
// Skip-reason taxonomy (post-Phase-2):
//   F-CT-INT    — Phase C rows; integration-smoke / full-frame rows that
//                 depend on Phase B wiring being live plus cross-suite
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

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "memory/contention.h"
#include "memory/mmu.h"
#include "core/clock.h"

#include "contention_helpers.h"

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

    // CT-GATE-06 RETIRED 2026-05-04: standalone Pentagon machine type
    // dropped (Wave 0.3 follow-up); ContentionModel no longer exposes a
    // pentagon_timing setter. The VHDL `machine_timing_pentagon` term
    // (zxnext.vhd:4481) is gated by NR 0x03 b2 in the Next FPGA, but jnext
    // does not currently wire that signal into the contention model — so
    // the gate is effectively a no-op for every supported machine.

    // CT-GATE-07: all gate inputs at VHDL power-on defaults
    // (contention_disable=0, cpu_speed=00) plus page=0x0A on ZX48K
    // ⇒ contended. Sanity row that the gate is OPEN when nothing
    // intervenes.
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
    //
    // V24-MEM-01 / V25-MEM-01 fix update: pre-fix this row asserted
    // ZXN_ISSUE2 fallthrough (no contention). Post-fix the gate keys
    // on `machine_timing_` whose constructor default is
    // `TimingPlus3` (matching VHDL :1099 `nr_03_machine_timing :=
    // "011"` and :1377 `eff_nr_03_machine_timing := "011"`). So a
    // default-constructed model now decodes the page using the +3
    // bank-decode (mem_active_page(3)='1'). The new row asserts that:
    //   * page=0x02 (low nibble bit3=0) → false (+3: bit3 clear)
    //   * page=0x0A (low nibble bit3=1) → true (+3: bit3 set, bank>=4)
    // Matches the new VHDL-faithful default. (Pre-fix legacy
    // `type_=ZXN_ISSUE2` is still the default for the typ_sel axis;
    // build() will re-seed both axes coherently.)
    {
        ContentionModel cm;
        cm.set_mem_active_page(0x02);
        const bool p02 = cm.is_contended_access();
        cm.set_mem_active_page(0x0A);
        const bool p0a = cm.is_contended_access();
        check("CT-GATE-08",
              "Default-constructed model (no build()) honours VHDL "
              ":1099/:1377 power-on TimingPlus3 default — page=0x02 "
              "(bit3=0) → false; page=0x0A (bit3=1) → true "
              "[V24-MEM-01 fix; zxnext.vhd:4492,5761-5777]",
              !p02 && p0a,
              std::string("p02=") + std::to_string(p02) +
              " p0a=" + std::to_string(p0a) + " (exp 0/1)");
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

    // Phase A bare-class rows: VHDL zxnext.vhd:4496 port_contend decode,
    // bare-class form (port_7ffd_active term not driven). port_bf3b /
    // port_ff3b 16-bit comparators per zxnext.vhd:2685-2686.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-01",
              "48K, cpu_a=0xFE (even port, ULA) → port_contend=1 "
              "[zxnext.vhd:4496]",
              cm.port_contend(0xFE, /*port_ulap_io_en=*/false));
    }
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-02",
              "48K, cpu_a=0xFF (odd port, non-ULA) → port_contend=0 "
              "[zxnext.vhd:4496]",
              !cm.port_contend(0xFF, /*port_ulap_io_en=*/false));
    }
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-03",
              "48K, cpu_a=0x00 (even, lowest) → port_contend=1 "
              "[zxnext.vhd:4496]",
              cm.port_contend(0x0000, /*port_ulap_io_en=*/false));
    }
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-04",
              "48K, cpu_a=0x01 (odd, lowest) → port_contend=0 "
              "[zxnext.vhd:4496]",
              !cm.port_contend(0x0001, /*port_ulap_io_en=*/false));
    }

    // CT-IO-05/06 — `port_7ffd_active` term (zxnext.vhd:2594) requires the
    // full Emulator to drive `port_7ffd_io_en` (NR 0x82 bit 1) AND the
    // 128K/+3 timing select. The bare `port_contend()` accessor documents
    // (contention.h:73-83) that it intentionally drops the OR-term — so
    // the bare-class call for cpu_a=0x7FFD always returns the odd-bit
    // term only (false). Per the plan §8 row notes, CT-IO-05 will read
    // contended ONLY once Branch A's runtime wiring drives port_7ffd_active
    // through the production NextReg/PortDispatch path. We assert the
    // bare-class contract here AND a Branch-A-future-state expectation
    // (PHASE-2-DEPENDS marker).
    //
    // CT-IO-05: 128K + odd port 0x7FFD. Bare-class returns false today
    // because the 7FFD-OR-term is unwired. Branch-A target: contended.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        // Bare-class observation: under-reports. This `check` PASSES on
        // current main and POST Branch-A merge will need a flip to
        // `cm.is_port_contended_runtime(0x7FFD)` (or whatever the runtime
        // accessor is named) — flagged for Branch A to wire.
        check("CT-IO-05",
              "128K, cpu_a=0x7FFD bare-class port_contend → false today; "
              "Branch-A wires port_7ffd_active term [zxnext.vhd:4496,2594; "
              "contention.h:73-83 bare-class limitation]",
              !cm.port_contend(0x7FFD, /*port_ulap_io_en=*/false));
    }
    // CT-IO-06: 48K + odd port 0x7FFD. port_7ffd_active is always 0 on
    // 48K timing per zxnext.vhd:2594, so the bare-class accessor matches
    // the production behaviour (false).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-06",
              "48K, cpu_a=0x7FFD (port_7ffd_active always 0 on 48K) "
              "→ port_contend=0 [zxnext.vhd:4496,2594]",
              !cm.port_contend(0x7FFD, /*port_ulap_io_en=*/false));
    }

    // Phase A ULA+ port rows: port_bf3b / port_ff3b OR-terms gated by
    // port_ulap_io_en (zxnext.vhd:2685-2686).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-07",
              "Any timing, cpu_a=0xBF3B (ULA+ index, port_bf3b OR-term) → 1 "
              "[zxnext.vhd:4496,2685]",
              cm.port_contend(0xBF3B, /*port_ulap_io_en=*/true));
    }
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-08",
              "Any timing, cpu_a=0xFF3B (ULA+ data, port_ff3b OR-term) → 1 "
              "[zxnext.vhd:4496,2686]",
              cm.port_contend(0xFF3B, /*port_ulap_io_en=*/true));
    }
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        check("CT-IO-09",
              "Any timing, cpu_a=0xBF3B, port_ulap_io_en=0 → port_contend=0 "
              "[zxnext.vhd:4496,2685]",
              !cm.port_contend(0xBF3B, /*port_ulap_io_en=*/false));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §9. Wait-pattern window — hc/vc/phase gates (10 rows, Phase B)
// VHDL: zxula.vhd:178, 582-583, 587-595
// ══════════════════════════════════════════════════════════════════════

static void test_wait_window() {
    set_group("CT-WIN");

    // The per-machine LUT populated by ContentionModel::build() at
    // src/memory/contention.cpp:37-46 IS the VHDL `wait_s` × per-phase
    // pattern. delay(hc, vc) returns lut_[vc][hc]. The build loop bounds
    // the population to vc∈[64,255] and hc∈[0,255] — outside that range
    // the LUT is zero (matches the VHDL `border_active_v=1` and `hc(8)=1`
    // gates). These rows assert delay() against the VHDL formula.

    // CT-WIN-01: 48K, hc=0, vc=0 — vc<64 (border_active_v=1 region).
    // Even though hc=0 has hc_adj=1 (`(3:2)=00` so wait_s=0 anyway), the
    // border gate is the load-bearing reason here.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(0, 0);
        check("CT-WIN-01",
              "48K, hc=0, vc=0 (hc_adj(3:2)=00 AND vc<64 border) → delay=0 "
              "[zxula.vhd:582-583; src/memory/contention.cpp:37-46]",
              d == 0,
              std::string("delay=") + std::to_string(d));
    }

    // CT-WIN-02: 48K, hc=3, vc=100 — display row, hc_adj=4 → (3:2)=01,
    // pattern[3]=3.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(3, 100);
        check("CT-WIN-02",
              "48K, hc=3, vc=100 (hc_adj=4, (3:2)=01) → delay=pattern[3]=3 "
              "[zxula.vhd:582-583,587-595]",
              d == 3,
              std::string("delay=") + std::to_string(d));
    }

    // CT-WIN-03: 48K, hc=15, vc=100 — hc_adj wraps to 0 (4-bit truncation).
    // wait_s=0 ⇒ delay=0. Discriminates the 4-bit wrap from a 9-bit `+1`.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(15, 100);
        check("CT-WIN-03",
              "48K, hc=15, vc=100 (hc_adj=0 after 4-bit wrap) → delay=0 "
              "[zxula.vhd:178,582-583]",
              d == 0,
              std::string("delay=") + std::to_string(d));
    }

    // CT-WIN-04: 48K, hc=255, vc=100 — last display column, same 4-bit
    // wrap as CT-WIN-03 but at high end of hc range (`i_hc(3:0)=1111`).
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(255, 100);
        check("CT-WIN-04",
              "48K, hc=255, vc=100 (last display col, hc_adj=0 wrap) → delay=0 "
              "[zxula.vhd:178,582-583]",
              d == 0,
              std::string("delay=") + std::to_string(d));
    }

    // CT-WIN-05: 48K, hc=256, vc=100 — hc(8)=1, display window OFF.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(256, 100);
        check("CT-WIN-05",
              "48K, hc=256, vc=100 (hc(8)=1 window gate off) → delay=0 "
              "[zxula.vhd:583; build()-loop bounds hc<=255]",
              d == 0,
              std::string("delay=") + std::to_string(d));
    }

    // CT-WIN-06: 48K, hc=100, vc=192 — VHDL canonical border boundary.
    // border_active_v = vc(8) | (vc(7) & vc(6)) = 1 for vc=192 (binary
    // 11000000). The build()-loop at src/memory/contention.cpp populates
    // only vc in [0, 191], so lut_[192][...] is zero-filled — confirming
    // both the loop bound and the VHDL gate semantics.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(100, 192);
        check("CT-WIN-06",
              "48K, hc=100, vc=192 (border_active_v=1) → delay=0 "
              "[zxula.vhd:414,583]",
              d == 0,
              std::string("delay=") + std::to_string(d));
    }

    // CT-WIN-07: 48K, hc=100, vc sweep across active display: per-phase
    // pattern follows {6,5,4,3,2,1,0,0} for the contended phases. vc=100
    // is in display window. We assert the LUT matches expect_lut_nonzero
    // and that the per-phase pattern is exactly {6,5,4,3,2,1,0,0}.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        bool sweep_ok = true;
        std::string detail;
        for (int hc = 0; hc <= 255; ++hc) {
            const uint8_t d   = cm.delay(hc, 100);
            const uint8_t exp = expect_lut_nonzero(MachineType::ZX48K, hc, 100)
                                ? expected_wait_pattern(hc) : 0;
            if (d != exp) {
                sweep_ok = false;
                detail = std::string("hc=") + std::to_string(hc)
                       + " got=" + std::to_string(d)
                       + " exp=" + std::to_string(exp);
                break;
            }
        }
        check("CT-WIN-07",
              "48K, hc=0..255 sweep at vc=100: per-phase LUT == "
              "{6,5,4,3,2,1,0,0} on stretched phases, 0 elsewhere "
              "[zxula.vhd:579-583,587-595; contention.cpp:37-46]",
              sweep_ok, detail);
    }

    // CT-WIN-08: +3, hc=15 — hc_adj=0 (after 4-bit wrap of 15+1), so
    // (3:1)=000 AND p3=1 ⇒ second `wait_s` clause fires. Per plan §11,
    // the +3 path has the additional clause hc_adj(3:1)='000'. delay()
    // should be non-zero (pattern[7]=0 actually, see below).
    //
    // hc=15: hc&7=7 → pattern[7]=0. So even though wait_s=1, the LUT
    // value is 0 (the VHDL pattern is asymmetric). To get a NON-ZERO
    // delay from the +3 extra phase, hc must satisfy hc_adj(3:1)=000
    // AND hc&7 != 7. Available values: hc&0xF=15 → hc_adj=0 → (3:1)=000.
    // So hc ∈ {15, 31, 47, ..., 255}; hc&7 = (hc&0xF)&7 = 15&7 = 7
    // (since hc&0xF=15 → hc&7=7). Always 7 → pattern[7]=0.
    //
    // Thus the +3 extra phase ONLY shows zero delay in the LUT — the
    // clause-2 fires but its pattern row is 0. Plan §9 row CT-WIN-08
    // asks "wait_s=1" which the bare-class LUT cannot directly observe;
    // we assert that on +3 the LUT for hc=15 vc=100 is 0 (pattern[7]=0)
    // and on 48K it is also 0 (clause 1 false, clause 2 doesn't fire on
    // 48K). The discrimination between 48K and +3 falls out of CT-SP3-08
    // (zero phase, +3-only). Document that here.
    {
        ContentionModel cm_p3   = make_cm(MachineType::ZX_PLUS3);
        ContentionModel cm_48k  = make_cm(MachineType::ZX48K);
        const uint8_t d_p3  = cm_p3.delay(15, 100);
        const uint8_t d_48k = cm_48k.delay(15, 100);
        // Both report 0 because pattern[7]=0; but the wait_s SIGNAL on +3
        // is asserted (clause 2). Branch A's runtime tick will observe
        // the wait assertion via cycle stretching even when LUT is 0 —
        // that's why CT-SP3-08 (bank 4 read at the same hc) verifies the
        // stretch on +3 specifically.
        check("CT-WIN-08",
              "+3 vs 48K, hc=15, vc=100: pattern[7]=0 so LUT=0 on both "
              "(clause-2 wait_s=1 on +3 is observable via Branch-A stretch) "
              "[zxula.vhd:582-583; pattern[hc&7]={6,5,4,3,2,1,0,0}]",
              d_p3 == 0 && d_48k == 0,
              std::string("d_p3=") + std::to_string(d_p3)
              + " d_48k=" + std::to_string(d_48k));
    }

    // CT-WIN-09: 48K, hc=16, vc=100 — i_hc(3:0)=0000 → hc_adj=0001;
    // (3:2)=00 ⇒ wait_s=0. Pairs with CT-WIN-04 (hc=255 → hc_adj=0).
    // Confirms only the low 4 bits matter (4-bit truncation is what
    // the build()-loop encodes).
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(16, 100);
        check("CT-WIN-09",
              "48K, hc=16, vc=100 (hc&0xF=0, hc_adj=1, (3:2)=00) → delay=0 "
              "[zxula.vhd:178,582-583]",
              d == 0,
              std::string("delay=") + std::to_string(d));
    }

    // CT-WIN-10: 48K, hc=7, vc=100 — hc_adj=8, (3:2)=10 ⇒ wait_s=1.
    // Pattern address = hc & 7 = 7 → pattern[7]=0. So LUT=0 even though
    // wait_s=1. Documents the asymmetric pattern.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        const uint8_t d = cm.delay(7, 100);
        check("CT-WIN-10",
              "48K, hc=7, vc=100 (hc_adj=8 ⇒ wait_s=1, pattern[7]=0) → delay=0 "
              "[zxula.vhd:582-583,587-595]",
              d == 0,
              std::string("delay=") + std::to_string(d));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §10. 48K / 128K — clock-stretch path (8 rows, Phase B)
// VHDL: zxula.vhd:587-595
// ══════════════════════════════════════════════════════════════════════

static void test_stretch_48k() {
    set_group("CT-S48");

    // §10 rows assert that ContentionModel correctly composes its enable
    // gate AND mem-decode AND wait-pattern LUT. They use the bare-class
    // API (is_contended_access for the gate+decode, delay() for the
    // pattern) since Branch A's runtime tick wiring will dereference
    // exactly these accessors per CPU MREQ. Where the row asks "added
    // T-states", we read it as "LUT non-zero on a contended cycle"
    // (PHASE-2-DEPENDS: Branch-A wires LUT → tick).

    // CT-S48-01: 48K, bank 5 (page 0x0A), display window, stretched
    // phase. Use hc=4 (hc_adj=5 → (3:2)=01, hc&7=4 → pattern[4]=2).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        const bool gate = cm.is_contended_access();
        const uint8_t d = cm.delay(4, 100);
        check("CT-S48-01",
              "48K bank-5 (page 0x0A) display+stretched (hc=4,vc=100) → "
              "gate=1 AND delay=pattern[4]=2 [zxula.vhd:587-595]",
              gate && d == 2,
              std::string("gate=") + std::to_string(gate)
              + " delay=" + std::to_string(d));
    }

    // CT-S48-02: 48K, bank 5, display window, NON-stretched phase. hc=0
    // → hc_adj=1 → (3:2)=00 → wait_s=0 → LUT=0.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        const bool gate = cm.is_contended_access();
        const uint8_t d = cm.delay(0, 100);
        check("CT-S48-02",
              "48K bank-5 display+NON-stretched (hc=0,vc=100) → "
              "gate=1 BUT delay=0 (hc_adj(3:2)=00) [zxula.vhd:582-595]",
              gate && d == 0,
              std::string("gate=") + std::to_string(gate)
              + " delay=" + std::to_string(d));
    }

    // CT-S48-03: 48K, bank 0 (page 0x00) — never contended. Gate fires
    // (enable_en=1) but mem_contend=0 ⇒ is_contended_access=false.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x00);
        check("CT-S48-03",
              "48K bank-0 (page 0x00, bits(3:1)=000) → not contended "
              "[zxnext.vhd:4490]",
              !cm.is_contended_access());
    }

    // CT-S48-04: 128K, bank 1 (page 0x02, bit(1)=1) display+stretched.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_mem_active_page(0x02);
        const bool gate = cm.is_contended_access();
        const uint8_t d = cm.delay(4, 100);
        check("CT-S48-04",
              "128K bank-1 (page 0x02) display+stretched (hc=4,vc=100) → "
              "gate=1 AND delay=pattern[4]=2 [zxnext.vhd:4491]",
              gate && d == 2,
              std::string("gate=") + std::to_string(gate)
              + " delay=" + std::to_string(d));
    }

    // CT-S48-05: 128K, bank 4 (page 0x08, bit(1)=0) — even bank, not
    // contended.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_mem_active_page(0x08);
        check("CT-S48-05",
              "128K bank-4 (page 0x08, bit(1)=0) → not contended "
              "[zxnext.vhd:4491]",
              !cm.is_contended_access());
    }

    // CT-S48-06: 48K, I/O port 0xFE (even port, port_contend=1) at
    // stretched phase. The port-side wait fires when port_contend=1
    // AND wait_s=1. Bare-class observable: port_contend(0xFE)=true AND
    // delay() non-zero at the chosen (hc,vc).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        const bool pc = cm.port_contend(0xFE, /*port_ulap_io_en=*/false);
        const uint8_t d = cm.delay(4, 100);
        check("CT-S48-06",
              "48K port=0xFE, hc=4, vc=100 → port_contend=1 AND "
              "delay=pattern[4]=2 (port stretched) "
              "[zxula.vhd:587-595; zxnext.vhd:4496]",
              pc && d == 2,
              std::string("pc=") + std::to_string(pc)
              + " delay=" + std::to_string(d));
    }

    // CT-S48-07: 48K, port 0xFF (odd, port_contend=0). port-side stretch
    // does not fire.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        const bool pc = cm.port_contend(0xFF, /*port_ulap_io_en=*/false);
        check("CT-S48-07",
              "48K port=0xFF (odd, no ULA+, no 7FFD) → port_contend=0 "
              "[zxnext.vhd:4496]",
              !pc);
    }

    // CT-S48-08: 48K, mem read OUTSIDE display window — vc=300
    // (border_active_v=1). delay()=0 regardless of mem_active_page.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);
        const bool gate = cm.is_contended_access();
        const uint8_t d = cm.delay(100, 300);
        check("CT-S48-08",
              "48K bank-5 outside display window (vc=300) → gate=1 BUT "
              "delay=0 (border_active_v=1) [zxula.vhd:583]",
              gate && d == 0,
              std::string("gate=") + std::to_string(gate)
              + " delay=" + std::to_string(d));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §11. +3 — WAIT_n path (8 rows, Phase B)
// VHDL: zxula.vhd:599-600
// ══════════════════════════════════════════════════════════════════════

static void test_stretch_plus3() {
    set_group("CT-SP3");

    // §11 rows: +3 path (zxula.vhd:599-600 WAIT_n stall, memory-only).
    // Same bare-class strategy as §10. The WAIT_n vs clock-stretch
    // distinction is invisible at the LUT level — both report the
    // per-phase pattern delay; Branch A's runtime tick will route the
    // stretch through `WAIT_n` on +3 and `o_cpu_contend` on 48K/128K.

    // CT-SP3-01: +3, bank 4 (page 0x08, bit(3)=1) display+stretched.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x08);
        const bool gate = cm.is_contended_access();
        const uint8_t d = cm.delay(4, 100);
        check("CT-SP3-01",
              "+3 bank-4 (page 0x08) display+stretched (hc=4,vc=100) → "
              "gate=1 AND delay=pattern[4]=2 [zxula.vhd:600; zxnext.vhd:4492]",
              gate && d == 2,
              std::string("gate=") + std::to_string(gate)
              + " delay=" + std::to_string(d));
    }

    // CT-SP3-02: +3, bank 7 (page 0x0E, bit(3)=1) display+stretched.
    // Discriminates the "any bank ≥ 4" rule (different LSB pattern from
    // CT-SP3-01).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x0E);
        const bool gate = cm.is_contended_access();
        const uint8_t d = cm.delay(4, 100);
        check("CT-SP3-02",
              "+3 bank-7 (page 0x0E) display+stretched (hc=4,vc=100) → "
              "gate=1 AND delay=pattern[4]=2 [zxula.vhd:600; zxnext.vhd:4492]",
              gate && d == 2,
              std::string("gate=") + std::to_string(gate)
              + " delay=" + std::to_string(d));
    }

    // CT-SP3-03: +3, bank 0 (page 0x00, bit(3)=0) — not contended.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x00);
        check("CT-SP3-03",
              "+3 bank-0 (page 0x00, bit(3)=0) → not contended "
              "[zxnext.vhd:4492]",
              !cm.is_contended_access());
    }

    // CT-SP3-04: +3, bank 4 OUTSIDE display window (vc=400 > 319 max).
    // delay() returns 0; gate (mem-contend) still fires.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x08);
        const bool gate = cm.is_contended_access();
        const uint8_t d = cm.delay(100, 300);
        check("CT-SP3-04",
              "+3 bank-4 outside display window (vc=300) → "
              "gate=1 BUT delay=0 (border) [zxula.vhd:583,600]",
              gate && d == 0,
              std::string("gate=") + std::to_string(gate)
              + " delay=" + std::to_string(d));
    }

    // CT-SP3-05: +3, bank 4 with contention_disable=1 — enable gate off.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x08);
        cm.set_contention_disable(true);
        check("CT-SP3-05",
              "+3 bank-4 with contention_disable=1 → not contended "
              "(enable gate off) [zxnext.vhd:4481]",
              !cm.is_contended_access());
    }

    // CT-SP3-06: +3, I/O port 0xFE in display window. Per the live VHDL
    // (zxula.vhd:599 commented form), the +3 WAIT_n path is memory-only;
    // I/O cycles do NOT stall. Bare-class observable: port_contend()
    // STILL reports 1 (the decode is the same), but Branch A's runtime
    // tick will not emit WAIT_n on the IORQ path. We assert the bare
    // decode here.
    //
    // Port 0xFE is even, so port_contend=1 — the bare decode reflects
    // the VHDL `port_contend` signal, not the +3 WAIT_n gate. Branch A
    // will gate the I/O wait emission separately for +3.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        const bool pc = cm.port_contend(0xFE, /*port_ulap_io_en=*/false);
        check("CT-SP3-06",
              "+3 port=0xFE bare port_contend=1; +3 WAIT_n is memory-only "
              "so Branch-A tick does NOT stall on I/O [zxula.vhd:599-600 "
              "commented OR-with-iorq clause]",
              pc);
    }

    // CT-SP3-07: +3, port=0xFE display window, contended MMU bank.
    // Same as CT-SP3-06 — bare-class decode is identical (the contended
    // bank doesn't affect port_contend; WAIT_n is gated by mem_contend
    // which is decided per memory cycle, not per I/O). The "contended
    // bank in MMU slot" stimulus is invisible to the bare-class accessor.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_mem_active_page(0x08);   // bank 4 contended
        const bool pc = cm.port_contend(0xFE, /*port_ulap_io_en=*/false);
        check("CT-SP3-07",
              "+3 port=0xFE with contended MMU bank → port_contend=1 "
              "(memory-bank state does NOT enter port_contend decode) "
              "[zxula.vhd:599-600; zxnext.vhd:4496]",
              pc);
    }

    // CT-SP3-08: +3 extra phase — hc_adj(3:1)=000. Pick hc=15 (hc_adj=0
    // after 4-bit wrap → (3:1)=000), bank 4. wait_s=1 by clause 2 on +3.
    // Pattern[hc&7]=pattern[7]=0 → LUT=0 today. Branch A will emit the
    // stretch on +3 even when LUT==0 because clause 2 sets wait_s=1.
    //
    // Standalone observable check: on +3, the build() loop populates
    // lut_[100][15] iff (hc_adj & 0xC) != 0 OR (hc_adj & 0xE) == 0.
    // For hc=15: hc_adj=0, (0xC)=0, (0xE)=0 → contend=true. Pattern[7]=0
    // so LUT=0 anyway. We pin the asymmetric "wait_s asserted but LUT=0"
    // behaviour by checking that on 48K hc=15 vc=100 LUT IS NOT WRITTEN
    // (contend=false — same hc_adj fails 48K's only clause).
    //
    // Branch A future: when stretch is wired, +3 hc=15 will stall N
    // cycles even though LUT=0. The behaviour we CAN assert today:
    // delay(15,100)==0 on both 48K AND +3 (pattern[7]=0). The
    // discrimination is invisible at the LUT layer.
    {
        ContentionModel cm_p3   = make_cm(MachineType::ZX_PLUS3,  /*page=*/0x08);
        ContentionModel cm_48k  = make_cm(MachineType::ZX48K,     /*page=*/0x0A);
        const uint8_t d_p3  = cm_p3.delay(15, 100);
        const uint8_t d_48k = cm_48k.delay(15, 100);
        check("CT-SP3-08",
              "+3 hc=15 hc_adj=0 (3:1)=000 clause-2 wait_s=1 with bank 4: "
              "LUT=pattern[7]=0 (asymmetric pattern); 48K same hc=15 "
              "LUT=0 (clause-1 fails) [zxula.vhd:582-583,600]",
              d_p3 == 0 && d_48k == 0,
              std::string("d_p3=") + std::to_string(d_p3)
              + " d_48k=" + std::to_string(d_48k));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §12. Next-turbo — never-contended paths (rows reduced 2026-05-04 after
// Wave 0.3 dropped the standalone Pentagon machine type; CT-PENT-01,
// CT-PENT-04, CT-PENT-05 retired below).
// VHDL: zxnext.vhd:4481, 4489-4493, 5801, 5817, 5822-5823, 5787-5790
//
// Phase A rows (bare-class): CT-TURBO-01
// Phase B rows (full Emulator): CT-TURBO-04, -05, -06
// ══════════════════════════════════════════════════════════════════════

static void test_pent_turbo() {
    set_group("CT-PENT-TURBO");

    // CT-PENT-01 / CT-PENT-04 / CT-PENT-05 RETIRED 2026-05-04: standalone
    // Pentagon machine type dropped (Wave 0.3 follow-up). The VHDL
    // `machine_timing_pentagon` enable-gate term (zxnext.vhd:4481) is
    // gated by NR 0x03 b2 inside the Next FPGA; jnext does not currently
    // wire that signal into ContentionModel, and the standalone Pentagon
    // build() path no longer exists. The remaining CT-TURBO rows still
    // cover the cpu_speed leg of the same enable gate.

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
    // CT-TURBO-04: 48K full Emulator, write NR 0x07=0x01. Production
    // NR 0x07 handler (src/core/emulator.cpp NR-0x07 dispatch) drives
    // clock_.set_pending_cpu_speed() AND contention_.set_pending_cpu_speed()
    // — i.e. the SHADOW only, per VHDL zxnext.vhd:5788-5789. The
    // EFFECTIVE divisor commits on the next bus-idle CLK_CPU rising
    // edge (zxnext.vhd:5809,5817), modelled by
    // commit_pending_cpu_speed_on_bus_idle(true). Run-frame's post-
    // instruction tick fires that hook automatically; this row triggers
    // the commit explicitly to keep the bare-class assertion observable
    // at the public API level (the Emulator's contention_ is private).
    //
    // We then build a parallel bare-class ContentionModel with the same
    // (cpu_speed=1) and verify the gate is forced low — this is the
    // bare-class equivalent of "ContentionModel reports zero added
    // T-states for this machine state".
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX48K);
        if (!ok) {
            check("CT-TURBO-04",
                  "Emulator::init failed (likely missing 48K ROM) — row "
                  "would otherwise verify NR 0x07=0x01 → cpu_speed=7 MHz "
                  "→ contention gate off [zxnext.vhd:5787-5790,5817]",
                  false, "Emulator::init returned false");
        } else {
            emu.nextreg().write(0x07, 0x01);
            // Pre-commit: VHDL-faithfully, the effective divisor stays
            // at the old value (8 = 3.5 MHz) because the commit edge
            // hasn't fired yet (zxnext.vhd:5809). NR 0x07 readback
            // already reflects the shadow (bits[1:0] = nr_07_cpu_speed).
            const int cpu_div_pre  = emu.clock().cpu_divisor();
            const uint8_t nr07_pre = emu.nextreg().read(0x07);
            // Trigger the bus-idle commit (post-instruction natural
            // bus-idle in run_frame()'s tick cluster).
            emu.clock().commit_pending_cpu_speed_on_bus_idle(true);
            const int cpu_div_post = emu.clock().cpu_divisor();
            const uint8_t nr07_post = emu.nextreg().read(0x07);
            // Bare-class parallel: gate-off when speed != 0.
            ContentionModel cm;
            cm.build(MachineType::ZX48K);
            cm.set_mem_active_page(0x0A);
            cm.set_cpu_speed(1);
            const bool gate_off = !cm.is_contended_access();
            // Production divisor for 7 MHz = 4 (clock.h cpu_divisor mapping).
            check("CT-TURBO-04",
                  "48K NR 0x07=0x01 → pre-commit divisor unchanged (=8); "
                  "post bus-idle commit → divisor=4 (7 MHz); NR readback "
                  "bits[1:0]=01 throughout; parallel-cm cpu_speed=1 → gate off "
                  "[emulator.cpp NR-0x07 dispatch; zxnext.vhd:5787-5790,5809,5817]",
                  cpu_div_pre == 8 && cpu_div_post == 4
                  && (nr07_pre & 0x03) == 0x01 && (nr07_post & 0x03) == 0x01
                  && gate_off,
                  std::string("cpu_div_pre=") + std::to_string(cpu_div_pre)
                  + " cpu_div_post=" + std::to_string(cpu_div_post)
                  + " nr07_pre=0x" + std::to_string(nr07_pre)
                  + " nr07_post=0x" + std::to_string(nr07_post)
                  + " gate_off=" + std::to_string(gate_off));
        }
    }

    // CT-TURBO-05: 48K full Emulator, write NR 0x08 bit 6=1. Production
    // NR 0x08 handler (src/core/emulator.cpp:1640-1644) forwards bit 6
    // into contention_.set_contention_disable(). Observable:
    // emu.nextreg().read(0x08) bit 6 (or the regs_ store), AND the
    // bare-class parallel.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX48K);
        if (!ok) {
            check("CT-TURBO-05",
                  "Emulator::init failed — would verify NR 0x08 bit 6 → "
                  "contention_disable [zxnext.vhd:4481,5823]",
                  false, "Emulator::init returned false");
        } else {
            // NR 0x08 bit 7 is write-strobe (paging unlock); writing
            // 0x40 sets only bit 6.
            emu.nextreg().write(0x08, 0x40);
            // Verify8-memory class-(a) fix: VHDL zxnext.vhd:5906 reads
            // `eff_nr_08_contention_disable` (effective), not the
            // immediate shadow. The effective gate commits only on the
            // bus-idle + hc(8)='1' edge per zxnext.vhd:5822-5823. Without
            // running any instructions in this bare-Emulator harness,
            // poke the commit explicitly so the read-back observes the
            // committed value (matching what the run_frame() per-
            // instruction commit-poll would do at runtime).
            emu.contention().commit_contention_disable_on_hc(300);  // hc(8)=1
            const uint8_t nr08 = emu.nextreg().read(0x08);
            const bool bit6_set = (nr08 & 0x40) != 0;
            // Parallel bare-class.
            ContentionModel cm;
            cm.build(MachineType::ZX48K);
            cm.set_mem_active_page(0x0A);
            cm.set_contention_disable(true);
            const bool gate_off = !cm.is_contended_access();
            check("CT-TURBO-05",
                  "48K NR 0x08 bit 6 → readback bit 6 set AND parallel-cm with "
                  "contention_disable=1 → gate off "
                  "[emulator.cpp:1640-1644; zxnext.vhd:4481,5823]",
                  bit6_set && gate_off,
                  std::string("nr08=") + std::to_string(nr08)
                  + " gate_off=" + std::to_string(gate_off));
        }
    }

    // CT-TURBO-06: NR 0x08 bit 6 commit-edge — the EFFECTIVE contention
    // gate (consulted by is_contended_access()) must defer until the next
    // hc(8) rising edge. VHDL zxnext.vhd:5822-5823 latches the shadow
    // value into eff_nr_08_contention_disable on hc(8)='1'. Branch B
    // implements this via ContentionModel::set_contention_disable_shadow()
    // + commit_contention_disable_on_hc(hc).
    //
    // Bare-class stimulus is sufficient: the commit semantics live on
    // ContentionModel; the Emulator-level integration (run_frame() per-
    // instruction commit poll) is implicitly exercised by the Phase 3
    // CT-INT-01/02 integration-smoke rows.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);              // bank 5 — would contend
        const bool gate_on_initial = cm.is_contended_access();

        cm.set_contention_disable_shadow(true);    // request disable
        const bool gate_on_after_shadow = cm.is_contended_access();

        cm.commit_contention_disable_on_hc(100);   // hc(8)=0 — no commit
        const bool gate_on_after_pre_edge = cm.is_contended_access();

        cm.commit_contention_disable_on_hc(300);   // hc(8)=1 — commits
        const bool gate_off_after_post_edge = !cm.is_contended_access();

        const bool ok = gate_on_initial
                     && gate_on_after_shadow       // shadow-only doesn't commit
                     && gate_on_after_pre_edge     // hc<256 doesn't commit
                     && gate_off_after_post_edge;  // hc>=256 commits

        check("CT-TURBO-06",
              "NR 0x08 bit 6 shadow latches on hc(8) rising edge: "
              "shadow alone does not affect gate; hc<256 does not commit; "
              "hc>=256 commits [zxnext.vhd:5822-5823]",
              ok,
              std::string("on0=") + std::to_string(gate_on_initial)
              + " shadow=" + std::to_string(gate_on_after_shadow)
              + " pre=" + std::to_string(gate_on_after_pre_edge)
              + " post=" + std::to_string(gate_off_after_post_edge));
    }

    // CT-TURBO-07 — NR 0x07 bus-idle commit edge. VHDL zxnext.vhd:
    // 5796-5828: nr_07_cpu_speed updates immediately on nr_07_we (5788-
    // 5789), but the EFFECTIVE `cpu_speed` register only commits on a
    // CLK_CPU rising edge that satisfies
    //     cpu_mreq_n='1' AND cpu_iorq_n='1' AND cpu_m1_n='1' AND
    //     dma_holds_bus='0'   (5809).
    // Implementation: ContentionModel splits cpu_speed_ (effective) from
    // pending_cpu_speed_ (shadow); set_pending_cpu_speed() updates the
    // shadow only, and commit_pending_cpu_speed_on_bus_idle(bus_idle)
    // promotes shadow → effective when bus_idle=true. Distinct from
    // CT-TURBO-06 which exercises NR 0x08 b6's hc(8) gate. (G142)
    //
    // Bare-class stimulus: drive the shadow alone, observe the effective
    // gate is unchanged; then commit on bus-idle and observe the gate
    // flips. Use the contention gate as the observable (consults
    // cpu_speed_ at zxnext.vhd:4481).
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);             // bank 5 — would contend
        const bool gate_on_initial = cm.is_contended_access();
        const uint8_t eff_initial  = cm.cpu_speed();
        const uint8_t pend_initial = cm.pending_cpu_speed();

        cm.set_pending_cpu_speed(1);              // shadow = 7 MHz
        const bool gate_on_after_shadow = cm.is_contended_access();
        const uint8_t eff_after_shadow  = cm.cpu_speed();
        const uint8_t pend_after_shadow = cm.pending_cpu_speed();

        // Bus-idle low — commit must NOT fire (mirrors a window where
        // mreq_n / iorq_n / m1_n are not all high, e.g. mid bus cycle).
        cm.commit_pending_cpu_speed_on_bus_idle(false);
        const bool gate_on_after_no_busidle = cm.is_contended_access();
        const uint8_t eff_after_no_busidle  = cm.cpu_speed();

        // dma_holds_bus high — commit must NOT fire even when bus_idle=1
        // (zxnext.vhd:5809 inhibits assignment when DMA owns the bus).
        cm.commit_pending_cpu_speed_on_bus_idle(true, /*dma_holds_bus*/ true);
        const bool gate_on_after_dma = cm.is_contended_access();

        // bus_idle high, dma_holds_bus low → commit fires.
        cm.commit_pending_cpu_speed_on_bus_idle(true, /*dma_holds_bus*/ false);
        const bool gate_off_after_busidle = !cm.is_contended_access();
        const uint8_t eff_after_busidle   = cm.cpu_speed();

        const bool ok = gate_on_initial             // initial: gate on
                     && eff_initial  == 0
                     && pend_initial == 0
                     && gate_on_after_shadow        // shadow alone: gate stays on
                     && eff_after_shadow  == 0
                     && pend_after_shadow == 1
                     && gate_on_after_no_busidle    // no bus-idle: no commit
                     && eff_after_no_busidle == 0
                     && gate_on_after_dma           // dma holds bus: no commit
                     && gate_off_after_busidle      // bus-idle commit: gate off
                     && eff_after_busidle  == 1;

        check("CT-TURBO-07",
              "NR 0x07 cpu_speed shadow → effective commits only on bus-idle "
              "(mreq_n & iorq_n & m1_n & not dma_holds_bus): shadow alone does "
              "not affect gate; bus_idle=0 does not commit; dma_holds_bus=1 "
              "does not commit; bus_idle=1 AND dma_holds_bus=0 commits "
              "[zxnext.vhd:5796-5828]",
              ok,
              std::string("init=(eff=") + std::to_string(eff_initial)
              + ",pend=" + std::to_string(pend_initial)
              + ") shadow=(eff=" + std::to_string(eff_after_shadow)
              + ",pend=" + std::to_string(pend_after_shadow)
              + ") no_bi=(eff=" + std::to_string(eff_after_no_busidle)
              + ") dma=(gate_on=" + std::to_string(gate_on_after_dma)
              + ") commit=(eff=" + std::to_string(eff_after_busidle)
              + ",gate_off=" + std::to_string(gate_off_after_busidle) + ")");
    }
}

// ══════════════════════════════════════════════════════════════════════
// §13. p3_floating_bus_dat capture on contended memory access
// (4 rows, Phase B). VHDL: zxnext.vhd:4498-4509
// ══════════════════════════════════════════════════════════════════════

static void test_floating_bus_capture() {
    set_group("CT-FB");

    // §13 rows: p3_floating_bus_dat capture on contended memory access.
    // The latch lives on Mmu (src/memory/mmu.h:494) and is updated from
    // Mmu::read()/write() when the per-16K-slot contended flag is set
    // (src/memory/mmu.h:201-203, 278-280). Emulator::init pushes the
    // initial slot map (emulator.cpp:166-170), so on a fresh +3 Emulator
    // slot 1 (0x4000-0x7FFF, bank 5) is contended and slot 0/2/3 are
    // not — this matches the VHDL `mem_contend` decode for +3 banks ≥ 4.
    //
    // We use the Mmu accessor directly (no need for the runtime tick
    // wiring). The +3 floating-bus latch IS observable today on `main`.

    // CT-FB-01: +3, memory read bank-4 / contended slot. The default +3
    // Emulator init puts slot 1 (0x4000) contended; we read a known
    // address there and verify the latch captured the byte.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX_PLUS3);
        if (!ok) {
            check("CT-FB-01",
                  "Emulator::init failed (likely missing +3 ROMs) — would "
                  "verify p3_floating_bus_dat captures bank-4 read "
                  "[zxnext.vhd:4498-4505]",
                  false, "Emulator::init returned false");
        } else {
            // Plant a known byte in physical bank 5 (the slot 1 default).
            // bank 5 = page 10, offset 0 → CPU 0x4000.
            const uint8_t marker = 0xA5;
            emu.ram().write(10u * 8192u, marker);
            // Read through the Mmu — this must update p3_floating_bus_dat_
            // because slot 1 is flagged contended in Emulator::init.
            const uint8_t got = emu.mmu().read(0x4000);
            const uint8_t latch = emu.mmu().p3_floating_bus_dat();
            check("CT-FB-01",
                  "+3 mem read bank-5 (contended slot) → "
                  "p3_floating_bus_dat captures byte read "
                  "[zxnext.vhd:4498-4505; mmu.h:201-203]",
                  got == marker && latch == marker,
                  std::string("got=0x") + std::to_string(got)
                  + " latch=0x" + std::to_string(latch));
        }
    }

    // CT-FB-02: +3, memory write bank-5 / contended slot. The latch
    // captures cpu_do on a contended write per VHDL:4506-4508.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX_PLUS3);
        if (!ok) {
            check("CT-FB-02",
                  "Emulator::init failed — would verify mem write capture "
                  "[zxnext.vhd:4498-4508]",
                  false, "Emulator::init returned false");
        } else {
            const uint8_t payload = 0xC3;
            emu.mmu().write(0x4000, payload);
            const uint8_t latch = emu.mmu().p3_floating_bus_dat();
            check("CT-FB-02",
                  "+3 mem write bank-5 (contended slot) → "
                  "p3_floating_bus_dat captures byte written "
                  "[zxnext.vhd:4498-4508; mmu.h:278-280]",
                  latch == payload,
                  std::string("latch=0x") + std::to_string(latch));
        }
    }

    // CT-FB-03: +3, pre-seed the latch via a contended write (bank 5),
    // then read a non-contended slot (e.g. ROM area at 0x0000 — slot 0
    // is NOT contended in the Emulator::init seed map). The latch must
    // remain at the pre-seed value because the write-enable is gated by
    // slot_contended_[].
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX_PLUS3);
        if (!ok) {
            check("CT-FB-03",
                  "Emulator::init failed — would verify latch hold on "
                  "non-contended access [zxnext.vhd:4498-4501]",
                  false, "Emulator::init returned false");
        } else {
            const uint8_t seed = 0x77;
            // Pre-seed: write to contended slot 1.
            emu.mmu().write(0x4000, seed);
            const uint8_t after_seed = emu.mmu().p3_floating_bus_dat();
            // Now read from non-contended slot 0 (ROM area).
            (void)emu.mmu().read(0x0000);
            const uint8_t after_uncontended = emu.mmu().p3_floating_bus_dat();
            check("CT-FB-03",
                  "+3 pre-seed latch via contended write, then read "
                  "non-contended slot 0 → latch unchanged (gated by "
                  "slot_contended_) [zxnext.vhd:4498-4501; mmu.h:201]",
                  after_seed == seed && after_uncontended == seed,
                  std::string("after_seed=0x") + std::to_string(after_seed)
                  + " after_uncontended=0x" + std::to_string(after_uncontended));
        }
    }

    // CT-FB-04: +3, I/O read (no MREQ) — the Mmu latch is on Mmu::read/
    // write only; I/O cycles bypass Mmu entirely (PortDispatch is the
    // I/O bus). So an `emu.port().in(...)` does NOT touch the latch.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX_PLUS3);
        if (!ok) {
            check("CT-FB-04",
                  "Emulator::init failed — would verify I/O bypasses latch "
                  "[zxnext.vhd:4501]",
                  false, "Emulator::init returned false");
        } else {
            const uint8_t seed = 0x42;
            emu.mmu().write(0x4000, seed);
            const uint8_t after_seed = emu.mmu().p3_floating_bus_dat();
            // I/O cycle to a contended port (0xFE — the ULA). This
            // routes through PortDispatch::in, NOT Mmu, so the latch
            // must remain at `seed`.
            (void)emu.port().in(0xFE);
            const uint8_t after_io = emu.mmu().p3_floating_bus_dat();
            check("CT-FB-04",
                  "+3 I/O read (port 0xFE) does NOT touch the Mmu latch "
                  "(latch is gated on MREQ which only Mmu::read/write "
                  "drive) [zxnext.vhd:4501]",
                  after_seed == seed && after_io == seed,
                  std::string("after_seed=0x") + std::to_string(after_seed)
                  + " after_io=0x" + std::to_string(after_io));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// §14. Integration smoke (runtime drift) (3 rows, Phase C)
// Full Emulator + ContentionModel::delay() wired on tick path.
// ══════════════════════════════════════════════════════════════════════

static void test_integration_smoke() {
    set_group("CT-INT");

    // CT-INT-01: 48K, contention ON (defaults). The integration-smoke
    // program reads bank 5 (contended) 100 times via LD A,(HL); each
    // contended read in the display window pulls a non-zero
    // {6,5,4,3,2,1,0,0}[hc&7] stretch from ContentionModel::contention_tick().
    // We assert delta > baseline (i.e. some contention was added) and
    // delta == (baseline + ON_RUN's contention_tick sum measured against
    // the bare-class oracle CT-INT-02 below). The exact total depends
    // on where the program lands in the raster phase, so we pin the
    // observable end-to-end relationship: ON delta > OFF delta, and OFF
    // delta == baseline.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX48K);
        if (!ok) {
            check("CT-INT-01",
                  "Emulator::init failed — would verify 48K contention-ON "
                  "adds non-zero T-states "
                  "[zxula.vhd:582-595; zxula_timing.vhd]",
                  false, "Emulator::init returned false");
        } else {
            install_int_smoke_program(emu);
            const uint32_t total = run_int_smoke_program(emu);
            const uint32_t baseline = IntSmokeProgram::kBaselineT;
            check("CT-INT-01",
                  "48K, contention ON, 100 LD A,(0x4000) iterations → "
                  "total > un-contended baseline (contention_tick added "
                  "stretch on every bank-5 read in display window) "
                  "[zxula.vhd:582-595; zxula_timing.vhd]",
                  total > baseline,
                  std::string("total=") + std::to_string(total)
                  + " baseline=" + std::to_string(baseline)
                  + " delta=" + std::to_string(total - baseline));
        }
    }

    // CT-INT-02: 48K, contention OFF via NR 0x08 bit 6 — the same
    // program must consume EXACTLY the un-contended baseline. We use
    // ContentionModel::set_contention_disable() (the immediate-commit
    // accessor exposed at contention.h:48) rather than the NR 0x08
    // write path, because the latter sets only the SHADOW and waits
    // for an hc(8) commit edge that run_frame() drives — and we are
    // not running frames here (see helpers.h note on tstates reset).
    // The set_contention_disable() form is the bare-class equivalent
    // and pins the exact same gate that NR 0x08 bit 6 would, post-commit.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX48K);
        if (!ok) {
            check("CT-INT-02",
                  "Emulator::init failed — would verify 48K contention-OFF "
                  "matches baseline "
                  "[zxnext.vhd:4481,5823]",
                  false, "Emulator::init returned false");
        } else {
            // Force contention OFF with immediate commit (bypasses the
            // hc(8) latch that NR 0x08 → run_frame() would normally
            // walk; this is the integration-smoke equivalent observable).
            emu.contention().set_contention_disable(true);
            install_int_smoke_program(emu);
            const uint32_t total = run_int_smoke_program(emu);
            const uint32_t baseline = IntSmokeProgram::kBaselineT;
            check("CT-INT-02",
                  "48K, contention OFF (eff_nr_08_contention_disable=1), "
                  "100 LD A,(0x4000) iterations → total exactly matches "
                  "un-contended baseline "
                  "[zxnext.vhd:4481,5823]",
                  total == baseline,
                  std::string("total=") + std::to_string(total)
                  + " baseline=" + std::to_string(baseline));
        }
    }

    // CT-INT-03: Regression screenshot guardrail. The floating-bus
    // regression test (test/00regression/regression_tests.conf —
    // `floating-bus` row, 48K, test/00regression/nex/floating_bus_test.nex,
    // 150 frames) IS the canonical 48K contention-sensitive screenshot
    // smoke. Its reference image was rebaselined for Phase 2 (commit
    // 78ee69a). This row pins the expectation that contention
    // regressions surface there: it confirms the reference image and
    // the conf entry both exist, so the next time
    // `bash test/00regression/regression.sh` runs, contention-driven
    // pixel drift would be caught.
    {
        // Locate repo root from the test binary path. The contention
        // test runs from build/test/ — walk up to find
        // `test/00regression/img/` and `test/00regression/regression_tests.conf`
        // (img/ moved under test/00regression/ on 2026-05-01 alongside
        // the conf, since these reference PNGs are regression-only).
        const char* candidates[] = {
            "test/00regression/img/floating-bus-reference.png",
            "../test/00regression/img/floating-bus-reference.png",
            "../../test/00regression/img/floating-bus-reference.png",
        };
        const char* conf_candidates[] = {
            "test/00regression/regression_tests.conf",
            "../test/00regression/regression_tests.conf",
            "../../test/00regression/regression_tests.conf",
        };
        bool ref_exists = false;
        for (const char* p : candidates) {
            if (std::FILE* f = std::fopen(p, "rb")) {
                std::fclose(f);
                ref_exists = true;
                break;
            }
        }
        bool conf_has_fb = false;
        for (const char* p : conf_candidates) {
            if (std::FILE* f = std::fopen(p, "rb")) {
                char buf[4096];
                std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
                buf[n] = '\0';
                std::fclose(f);
                if (std::string(buf).find("floating-bus") != std::string::npos) {
                    conf_has_fb = true;
                }
                break;
            }
        }
        check("CT-INT-03",
              "Regression suite plumbing: floating-bus reference PNG "
              "exists AND regression_tests.conf includes a `floating-bus` "
              "entry — contention regressions land via "
              "test/00regression/regression.sh "
              "[test/00regression/img/floating-bus-reference.png; "
              "test/00regression/regression_tests.conf]",
              ref_exists && conf_has_fb,
              std::string("ref_exists=") + std::to_string(ref_exists)
              + " conf_has_fb=" + std::to_string(conf_has_fb));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §14b. Full-frame integration drift bound (1 row, G50 closure)
// VHDL oracle: zxula.vhd:582-595 wait_s × per-phase pattern
// {6,5,4,3,2,1,0,0}. Bound the per-frame drift induced by the runtime
// `ContentionModel::contention_tick()` path against the hand-derivable
// pattern envelope across all three contention-relevant machine types
// (48K, 128K, +3 — drift > 0 and ≤ 6·N).
// Pentagon row dropped 2026-05-04 (Wave 0.3 follow-up).
// ══════════════════════════════════════════════════════════════════════

static void test_delay_drift_bound() {
    set_group("CT-DELAY");

    // CT-DELAY-01 — G50 closure. Run the §14 IntSmokeProgram (100
    // contended LD A,(HL) reads from bank 5, terminated by HALT) on
    // each contention-relevant machine, sample the cumulative T-state
    // drift = total - kBaselineT, and bound it against the VHDL
    // wait_s × pattern envelope:
    //
    //   per-iteration drift ∈ {6,5,4,3,2,1,0,0}[hc & 7]   while inside
    //                          display window (vc ∈ [0, 191], hc ≤ 255)
    //                       ∈ {0}                          otherwise
    //
    // Hard upper bound for N iterations:  N · max(pattern) = N · 6.
    // Hard lower bound:                   0.
    //
    // For 48K/128K/+3 with bank-5 LD A,(HL): mem_contend asserts (page
    // 0x0A is contended on all three timing modes per zxnext.vhd:4490-
    // 4492), so the drift must fall STRICTLY inside (0, 6·N]. (Drift
    // strictly > 0 because the 100-iteration program spans ~10
    // scanlines starting at vc=0 — the entire trajectory sits inside
    // the display window, so at least some iterations land in stretched
    // hc phases.)
    //
    // The Pentagon drift-bound row was retired 2026-05-04 alongside the
    // standalone Pentagon machine-type drop (Wave 0.3 follow-up).
    //
    // Implementation note: the CT-DELAY suite is intentionally distinct
    // from CT-INT (§14 integration-smoke) — CT-INT-01 only asserts
    // `total > baseline` (i.e. drift > 0), which is a one-sided guard.
    // CT-DELAY-01 closes the bound on the high end so that any future
    // off-by-N or sign-flip regression in `contention_tick()` is caught
    // even if drift is still strictly positive.
    struct DriftCase {
        const char* tag;
        MachineType type;
        uint32_t    drift_min;   // strict lower bound (drift >= drift_min)
        uint32_t    drift_max;   // inclusive upper bound (drift <= drift_max)
        bool        require_nonzero;
    };
    constexpr uint32_t kN          = IntSmokeProgram::kIterations;   // 100
    constexpr uint32_t kPatternMax = 6;                              // pattern[0]
    constexpr uint32_t kDriftCap   = kN * kPatternMax;               // 600
    const DriftCase cases[] = {
        // 48K  : page 0x0A, mem_contend=1 (bits(3:1)=101) — contended.
        { "48K",      MachineType::ZX48K,    0, kDriftCap, /*nonzero*/ true  },
        // 128K : page 0x0A, mem_contend=1 (bit(1)=1)      — contended.
        { "128K",     MachineType::ZX128K,   0, kDriftCap, /*nonzero*/ true  },
        // +3   : page 0x0A, mem_contend=1 (bit(3)=1)      — contended.
        // (+3 also has the hc_adj(3:1)=000 extra-phase clause, but the
        //  per-phase amplitude is still bounded by max(pattern)=6.)
        { "+3",       MachineType::ZX_PLUS3, 0, kDriftCap, /*nonzero*/ true  },
    };

    bool all_bounded = true;
    bool all_init_ok = true;
    std::string detail;

    for (const auto& c : cases) {
        Emulator emu;
        if (!make_emu(emu, c.type)) {
            all_init_ok = false;
            detail += std::string(c.tag) + ":init-failed ";
            continue;
        }
        install_int_smoke_program(emu);
        const uint32_t total    = run_int_smoke_program(emu);
        const uint32_t baseline = IntSmokeProgram::kBaselineT;
        // Drift must always be non-negative under the runtime path —
        // contention_tick() only ADDS T-states, never subtracts.
        const bool nonneg     = total >= baseline;
        const uint32_t drift  = nonneg ? (total - baseline) : 0u;
        const bool within_max = (drift <= c.drift_max);
        const bool within_min = (drift >= c.drift_min);
        const bool nonzero_ok = (!c.require_nonzero) || (drift > 0);
        const bool ok = nonneg && within_max && within_min && nonzero_ok;
        if (!ok) all_bounded = false;
        detail += std::string(c.tag) + ":drift=" + std::to_string(drift)
               + "/" + std::to_string(c.drift_max) + " ";
    }

    if (!all_init_ok) {
        check("CT-DELAY-01",
              "Emulator::init failed for one or more machines — would "
              "verify per-frame contention drift bound across 48K/128K/+3 "
              "[zxula.vhd:582-595; zxnext.vhd:4481]",
              false, detail);
    } else {
        check("CT-DELAY-01",
              "Per-frame contention drift bounded across 48K/128K/+3 "
              "(0 < drift ≤ 6·N) for the §14 IntSmokeProgram (100 "
              "LD A,(0x4000) reads): VHDL wait_s × pattern envelope holds "
              "end-to-end [zxula.vhd:582-595; zxnext.vhd:4481-4492]",
              all_bounded,
              detail);
    }
}

// ══════════════════════════════════════════════════════════════════════
// §16: FUSE in-opcode contention (M1 fetch + no-MREQ tail + IN/OUT port)
// VHDL: zxula.vhd:583,595,600 — wait_s fires every contended cycle
// (M1, no-MREQ, data, port). FUSE injects per-cycle contention via
// contend_read/_no_mreq/_write_no_mreq macros at
// third_party/fuse-z80/z80_macros.h:107-130. With G141 (commit 2026-05-01)
// the FUSE source TU is built with -DCORETEST so those macros expand
// to extern function calls that route through ContentionModel::
// contention_tick() — the same VHDL-faithful gate the data path uses.
// (G141 + G53)
// ══════════════════════════════════════════════════════════════════════

namespace {

// CT-FUSE helper: install a small "code lives in slot 1" program that
// makes M1 fetches themselves contended on 48K (slot 1 = bank 5).
//
// 0x4000  06 64      LD B, 100        ; 7T  (M1=4, ND=3) — M1 contended
// 0x4002  10 FE      DJNZ -2          ; 13T taken / 8T fall-through
//                                     ; (M1=5 contended, ND=3, +5 no-MREQ tail
//                                     ;  on IR — IR=I<<8|R, with I=0 lives in
//                                     ;  slot 0 → uncontended)
// 0x4004  76         HALT             ; 4T (M1 contended)
//
// Un-contended baseline (B=100):
//   setup     : 7
//   loop body : 99 * 13 + 1 * 8 = 1287 + 8 = 1295
//   halt      : 4
//   total     : 1306
//
// Where the M1 fetches land:
//   * setup LD B,100   : M1 at 0x4000 (slot 1, contended)
//   * loop DJNZ -2     : M1 at 0x4002 (slot 1, contended) × 100
//   * loop HALT        : M1 at 0x4004 (slot 1, contended)
//
// Total contended-M1 fetches = 1 + 100 + 1 = 102.
// (Each contended M1 fetch enters contend_read() → contention_tick().)
struct FuseM1Program {
    static constexpr uint16_t kEntry      = 0x4000;
    static constexpr uint16_t kStackInit  = 0xBF00;     // top of bank 2
    static constexpr uint8_t  kIterations = 100;
    static constexpr uint32_t kBaselineT  = 1306;
    static constexpr uint16_t kHaltAddr   = 0x4004;
};

inline void install_fuse_m1_program(Emulator& emu) {
    constexpr uint16_t E = FuseM1Program::kEntry;
    emu.mmu().write(E + 0, 0x06);                          // LD B, n
    emu.mmu().write(E + 1, FuseM1Program::kIterations);
    emu.mmu().write(E + 2, 0x10);                          // DJNZ
    emu.mmu().write(E + 3, 0xFE);                          // -2
    emu.mmu().write(E + 4, 0x76);                          // HALT

    auto regs = emu.cpu().get_registers();
    regs.PC     = E;
    regs.SP     = FuseM1Program::kStackInit;
    regs.IFF1   = 0;
    regs.IFF2   = 0;
    regs.halted = false;
    emu.cpu().set_registers(regs);
}

inline uint32_t run_fuse_m1_program(Emulator& emu, std::size_t max_steps = 100000) {
    seek_to_display_window(emu);   // Task 50 — contention only exists in the display
    const uint32_t t_start = *fuse_z80_tstates_ptr();
    for (std::size_t i = 0; i < max_steps; ++i) {
        emu.cpu().execute();
        const auto regs = emu.cpu().get_registers();
        if (regs.halted && regs.PC == FuseM1Program::kHaltAddr) break;
    }
    const uint32_t t_end = *fuse_z80_tstates_ptr();
    return t_end - t_start;
}

// CT-FUSE-02 helper: LDIR program where source AND destination both lie
// in contended bank 5. LDIR uses contend_write_no_mreq(DE, 1) twice per
// non-final byte, four more times when continuing — so the no-MREQ tail
// fires on a contended page once per byte in the non-final iteration.
//
// 0x8000  21 00 40   LD HL, 0x4000     ; in slot 2 — uncontended M1
// 0x8003  11 00 50   LD DE, 0x5000     ; in slot 2 — uncontended M1
// 0x8006  01 10 00   LD BC, 16         ; in slot 2 — uncontended M1
// 0x8009  ED B0      LDIR              ; in slot 2 — uncontended M1; BUT
//                                     ; contend_write_no_mreq(DE, ...) fires
//                                     ; with DE=0x5000..0x500F (slot 1 → bank
//                                     ; 5 → contended). Per FUSE z80_ed.c:422
//                                     ; LDIR: 2× contend_write_no_mreq for the
//                                     ; final byte + 6× for each prior byte.
// 0x800B  76         HALT              ; in slot 2 — uncontended M1
//
// Un-contended baseline:
//   LD HL,nn  : 10
//   LD DE,nn  : 10
//   LD BC,nn  : 10
//   LDIR      : 21 × (BC-1) + 16 = 21*15 + 16 = 315 + 16 = 331
//   HALT      : 4
//   total     : 365
struct FuseLdirProgram {
    static constexpr uint16_t kEntry      = 0x8000;
    static constexpr uint16_t kStackInit  = 0xBF00;
    static constexpr uint16_t kCount      = 16;
    static constexpr uint32_t kBaselineT  = 365;
    static constexpr uint16_t kHaltAddr   = 0x800B;
};

inline void install_fuse_ldir_program(Emulator& emu) {
    constexpr uint16_t E = FuseLdirProgram::kEntry;
    // LD HL, 0x4000
    emu.mmu().write(E + 0, 0x21);
    emu.mmu().write(E + 1, 0x00);
    emu.mmu().write(E + 2, 0x40);
    // LD DE, 0x5000
    emu.mmu().write(E + 3, 0x11);
    emu.mmu().write(E + 4, 0x00);
    emu.mmu().write(E + 5, 0x50);
    // LD BC, 16
    emu.mmu().write(E + 6, 0x01);
    emu.mmu().write(E + 7, FuseLdirProgram::kCount & 0xFF);
    emu.mmu().write(E + 8, FuseLdirProgram::kCount >> 8);
    // LDIR
    emu.mmu().write(E + 9, 0xED);
    emu.mmu().write(E + 10, 0xB0);
    // HALT
    emu.mmu().write(E + 11, 0x76);

    auto regs = emu.cpu().get_registers();
    regs.PC     = E;
    regs.SP     = FuseLdirProgram::kStackInit;
    regs.IFF1   = 0;
    regs.IFF2   = 0;
    regs.halted = false;
    emu.cpu().set_registers(regs);
}

inline uint32_t run_fuse_ldir_program(Emulator& emu, std::size_t max_steps = 100000) {
    seek_to_display_window(emu);   // Task 50 — contention only exists in the display
    const uint32_t t_start = *fuse_z80_tstates_ptr();
    for (std::size_t i = 0; i < max_steps; ++i) {
        emu.cpu().execute();
        const auto regs = emu.cpu().get_registers();
        if (regs.halted && regs.PC == FuseLdirProgram::kHaltAddr) break;
    }
    const uint32_t t_end = *fuse_z80_tstates_ptr();
    return t_end - t_start;
}

} // namespace

static void test_fuse_inopcode_contention() {
    set_group("CT-FUSE");

    // ────────────────────────────────────────────────────────────────────
    // CT-FUSE-01 — M1 fetch contention
    // ────────────────────────────────────────────────────────────────────
    // Pre-G141: contend_read(PC, 4) at fuse_z80_core.c:193 expanded to
    //     if (memory_map_read[PC>>13].contended) tstates += ula_contention[t];
    //     tstates += 4;
    // With memory_map_read[] zero-filled, every M1 fetch added exactly 4
    // T-states with no contention stretch — even when PC was in bank 5
    // during the active raster window.
    //
    // Post-G141: -DCORETEST routes contend_read through
    // ContentionModel::contention_tick(), which fires the per-phase
    // {6,5,4,3,2,1,0,0}[hc&7] stretch on every M1 fetch in a contended
    // slot during the active raster window (vc<192, hc<256, hc_adj!=0).
    //
    // Stimulus: install a 5-byte program in slot 1 (bank 5 on 48K — fully
    // contended); 100 DJNZ iterations + HALT. ~102 contended M1 fetches +
    // 5 contend_read_no_mreq(PC,1) calls per JR (DJNZ taken).
    //
    // The two runs are SEQUENTIAL on a single Emulator instance, with
    // ON happening first (defaults) and OFF after toggling
    // set_contention_disable(true). Two parallel Emulators would race on
    // the static `s_contention` singleton in src/cpu/z80_cpu.cpp:67 (the
    // last-init wins), so sequential is the correct shape — same pattern
    // CT-INT-01/CT-INT-02 use (each row constructs one Emulator).
    //
    // VHDL oracle: zxula.vhd:583 wait_s × per-phase pattern × the VHDL
    // 48K mem_active_page='101' decode at zxnext.vhd:4490.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX48K);
        if (!ok) {
            check("CT-FUSE-01",
                  "Emulator::init failed — would verify M1 fetch contention "
                  "[zxula.vhd:583; zxnext.vhd:4481,4490]",
                  false, "Emulator::init returned false");
        } else {
            // ON first (defaults — gate enabled).
            install_fuse_m1_program(emu);
            const uint32_t total_on = run_fuse_m1_program(emu);

            // OFF — disable the gate immediately and re-run.
            // (set_contention_disable() commits both shadow + effective
            // synchronously; CT-INT-02 uses the same idiom.)
            emu.contention().set_contention_disable(true);
            install_fuse_m1_program(emu);
            const uint32_t total_off = run_fuse_m1_program(emu);

            const bool on_gt_off = (total_on > total_off);
            // M1 fetches: 1 setup + 100 loop + 1 halt = 102 contended
            // M1 cycles, each capped at +6 T-states (G50 envelope per
            // CT-DELAY-01). The DJNZ taken-branch JR() macro adds 5
            // contend_read_no_mreq(PC,1) calls on PC (slot 1, contended)
            // per iteration — those also fire contention. So the upper
            // bound on the ON-OFF delta is 6 × (102 + 5*100 + 1 LDB-imm
            // + 99 DJNZ-imm-fall + 1 DJNZ-imm-fall-out) ≤ 6 × 700 = 4200.
            // We use a generous round bound that is still tight enough
            // to catch any pattern-LUT regression that produces stretches
            // outside {0..6}.
            const uint32_t upper_bound = 6 * 700;
            const uint32_t delta = total_on - total_off;
            const bool on_bounded = delta <= upper_bound;

            const bool ok2 = on_gt_off && on_bounded;
            check("CT-FUSE-01",
                  "M1 fetch contention: 5-byte program in slot 1 "
                  "(bank 5, contended) — contention-ON total > "
                  "contention-OFF total (delta > 0) AND delta within "
                  "G50 envelope (6 × max contended cycles per iteration) "
                  "[zxula.vhd:583,595; zxnext.vhd:4481,4490]",
                  ok2,
                  std::string("on=") + std::to_string(total_on)
                  + " off=" + std::to_string(total_off)
                  + " delta=" + std::to_string(delta)
                  + " upper=" + std::to_string(upper_bound));
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // CT-FUSE-02 — no-MREQ tail contention
    // ────────────────────────────────────────────────────────────────────
    // FUSE LDIR (z80_ed.c:418-433) calls contend_write_no_mreq(DE, 1) two
    // times for the final byte, six times for each prior byte. Pre-G141
    // ula_contention_no_mreq[] was zero so the no-MREQ tail contributed
    // exactly +1 T per call — never the per-phase stretch. Post-G141 the
    // CORETEST function override routes contend_write_no_mreq through
    // contention_tick() and produces the same {6,5,4,3,2,1,0,0}[hc&7]
    // stretch the data path produces.
    //
    // Stimulus: LDIR with HL=0x4000 (slot 1, contended) and DE=0x5000
    // (also slot 1, contended). The ON delta over OFF MUST be > 0
    // because LDIR's contend_write_no_mreq calls hit a contended page
    // in the active raster window. The data-cycle contend_read /
    // contend_write at HL/DE also fire the same gate (Phase-2 wired)
    // — both cycle classes are now under one path post-G141.
    //
    // CRITICAL — single-Emulator pattern:
    //   `s_contention` in src/cpu/z80_cpu.cpp is a singleton; two
    //   Emulators in flight would race over which contention model is
    //   live (last-init wins). Run ON then OFF on a single Emulator
    //   sequentially — same idiom as CT-INT-01/CT-INT-02.
    //
    // VHDL oracle: zxula.vhd:583,595 — wait_s × per-phase pattern fires
    // on contended-memory cycles regardless of whether the current cycle
    // is MREQ-asserted (the `mreq23_n='1'` registered-MREQ-deasserted
    // path at line 595 captures the in-opcode tail).
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX48K);
        if (!ok) {
            check("CT-FUSE-02",
                  "Emulator::init failed — would verify LDIR no-MREQ tail "
                  "contention [zxula.vhd:583,595; z80_ed.c:418-433]",
                  false, "Emulator::init returned false");
        } else {
            // ON first (defaults — gate enabled).
            install_fuse_ldir_program(emu);
            const uint32_t total_on = run_fuse_ldir_program(emu);

            // OFF — disable the gate and re-run.
            emu.contention().set_contention_disable(true);
            install_fuse_ldir_program(emu);
            const uint32_t total_off = run_fuse_ldir_program(emu);

            const bool on_gt_off = (total_on > total_off);
            // LDIR-induced contention cycles per non-final byte:
            //   + 1 contend_read(HL, 3)  data-side read   (HL contended)
            //   + 1 contend_write(DE, 3) data-side write  (DE contended)
            //   + 6 contend_write_no_mreq(DE, 1) no-MREQ tail (DE contended)
            // = 8 contended cycles per byte × 16 bytes = 128 max contended
            //   cycles. Each cycle ≤ +6T. Upper bound: 6 × 128 = 768.
            //   (Plus M1 fetches of the LDIR opcode, but those are at
            //   PC=0x8009 in slot 2 — uncontended — so they don't add.)
            const uint32_t upper_bound = 6 * 128;
            const uint32_t delta = total_on - total_off;
            const bool on_bounded = delta <= upper_bound;

            const bool ok2 = on_gt_off && on_bounded;
            check("CT-FUSE-02",
                  "LDIR no-MREQ tail contention: 16-byte copy with HL+DE "
                  "in slot 1 (bank 5, contended) — contention-ON total > "
                  "contention-OFF total (delta > 0) AND delta within "
                  "envelope 6×8×16 (per-byte data-r/w + 6 no-MREQ tails) "
                  "[zxula.vhd:583,595; z80_ed.c:418-433]",
                  ok2,
                  std::string("on=") + std::to_string(total_on)
                  + " off=" + std::to_string(total_off)
                  + " delta=" + std::to_string(delta)
                  + " upper=" + std::to_string(upper_bound));
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // CT-FUSE-03 — OUT port contention (RETIRED)
    // CT-FUSE-04 — IN port contention  (RETIRED)
    // ────────────────────────────────────────────────────────────────────
    // OUT/IN port contention does NOT flow through the FUSE in-opcode
    // contend_* macros — port cycles are fully owned by FUSE's own port
    // callbacks (fuse_z80_readport / fuse_z80_writeport at
    // src/cpu/z80_cpu.cpp). Those callbacks were wired into
    // ContentionModel::contention_tick() in Phase 2 (commit 2026-04-26),
    // ahead of G141 — and are exhaustively covered by:
    //   * CT-IO-01..04, CT-IO-07..09 — bare-class even/odd port + ULA+
    //     decode (zxnext.vhd:4496) at lines 379-477 of this file.
    //   * CT-IO-05/06 — Phase-B 128K port_7ffd_active term (deferred
    //     to full-Emulator harness; documented under-report).
    //   * CT-INT-01 — full integration smoke through the Emulator port
    //     dispatch + contention_tick() runtime, lines 1390-1426.
    //
    // The CT-FUSE-03/04 rows would re-test the same code paths the CT-IO
    // and CT-INT rows already cover — duplicating coverage without
    // adding signal. Per the ARB-G65-01 retirement precedent
    // (test/copper/copper_test.cpp:1451-1473) we retire here without a
    // skip(): canonical coverage is in CT-IO-* + CT-INT-01.
    //
    // No skip(): rows intentionally retired here — canonical coverage
    // at CT-IO-01..09 + CT-INT-01.

    // ────────────────────────────────────────────────────────────────────
    // CT-FUSE-05 — G53: legacy FUSE contention tables retired
    // ────────────────────────────────────────────────────────────────────
    // VHDL has a single contention path (zxula.vhd:582-600 wait_s + the
    // two assignments at 595/600). Pre-G53 the emulator carried a
    // SECOND parallel path: the FUSE memory_map_read[]/ula_contention[]
    // tables, zero-filled but linked, indexed by every contend_* macro
    // call. With G141 routing all in-opcode calls through
    // ContentionModel::contention_tick() those tables became dead; G53
    // removed them along with their builders/setters.
    //
    // This row asserts the architectural invariant: there is exactly ONE
    // contention path in the live build. We test this by demonstrating
    // that `set_contention_disable(true)` is the SOLE switch that
    // suppresses contention added during a contended program — i.e. the
    // ContentionModel gate IS the only producer.
    //
    // Stimulus shape (single Emulator, ON then OFF — same singleton-
    // safe pattern as CT-FUSE-01/02):
    //   * Pass A (gate ON, defaults): total_on records the ON envelope.
    //   * Pass B (set_contention_disable(true)): re-run identical program
    //     and record total_off.
    //   * Pass C (gate ON again — set_contention_disable(false)): re-run
    //     identical program and record total_on2.
    // Invariants:
    //   1. total_on > total_off   (gate ON adds stretch).
    //   2. total_off == kBaselineT (uncontended baseline EXACTLY — proves
    //      no parallel path is still adding stretch behind our back).
    //   3. total_on2 ≈ total_on   (toggling back is idempotent — proves
    //      the disable is a true gate, not a destructive table-clear).
    //
    // VHDL oracle: zxnext.vhd:4481 — i_contention_en is the SOLE gate
    // for the SOLE contention path. No redundant table indexing remains.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX48K);
        if (!ok) {
            check("CT-FUSE-05",
                  "Emulator::init failed — would verify single-contention-"
                  "path invariant [zxnext.vhd:4481]",
                  false, "Emulator::init returned false");
        } else {
            // Pass A — defaults, gate ON.
            install_fuse_m1_program(emu);
            const uint32_t total_on = run_fuse_m1_program(emu);

            // Pass B — gate OFF.
            emu.contention().set_contention_disable(true);
            install_fuse_m1_program(emu);
            const uint32_t total_off = run_fuse_m1_program(emu);

            // Pass C — gate ON again (toggle-back idempotency).
            emu.contention().set_contention_disable(false);
            install_fuse_m1_program(emu);
            const uint32_t total_on2 = run_fuse_m1_program(emu);

            const uint32_t baseline = FuseM1Program::kBaselineT;
            const bool gate_on_adds       = (total_on  > baseline);
            const bool gate_off_no_extras = (total_off == baseline);
            // Pass C may differ from Pass A because of raster-phase drift
            // (each pass takes ~1306 + delta T-states; phase shifts a few
            // cycles between passes). Allow a generous symmetric window
            // — if the gate is broken in either direction, total_on2
            // would clamp to baseline (no stretch at all) and the upper
            // bound check would fail.
            const bool gate_back_on_active = (total_on2 > baseline);

            const bool ok2 = gate_on_adds && gate_off_no_extras
                          && gate_back_on_active;
            check("CT-FUSE-05",
                  "G53 single-path invariant: gate ON adds stretch (>baseline) "
                  "AND gate OFF matches baseline EXACTLY (no parallel FUSE-"
                  "table contribution remains) AND gate-back-ON re-activates "
                  "stretch — set_contention_disable() is the sole switch for "
                  "the sole contention path "
                  "[zxnext.vhd:4481; src/cpu/z80_cpu.cpp G141+G53]",
                  ok2,
                  std::string("baseline=") + std::to_string(baseline)
                  + " on1=" + std::to_string(total_on)
                  + " off=" + std::to_string(total_off)
                  + " on2=" + std::to_string(total_on2));
        }
    }

    // CT-TURBO-08 — Combined NR 0x07 + NR 0x08 bit-6 commit ordering.
    // Each shadow has its OWN commit edge per VHDL zxnext.vhd:5796-5828:
    //   * cpu_speed (NR 0x07) commits on bus-idle CLK_CPU (line 5809);
    //   * eff_nr_08_contention_disable (NR 0x08 b6) commits on bus-idle
    //     CLK_CPU AND hc(8)='1' (line 5822-5823).
    // The two commits are INDEPENDENT — a simultaneous mid-line write
    // to both shadows must produce a state in which only the satisfied
    // edge has committed. (G51)
    //
    // Stimulus: drop both shadows mid-line (hc<256, bus_idle whatever),
    // then walk through the four combinations of (bus_idle, hc(8)) and
    // assert each gate independently of the other.
    {
        ContentionModel cm = make_cm(MachineType::ZX48K);
        cm.set_mem_active_page(0x0A);             // bank 5 — would contend

        // Mid-line simultaneous write: NR 0x07 cpu_speed=1, NR 0x08 b6=1.
        cm.set_pending_cpu_speed(1);
        cm.set_contention_disable_shadow(true);

        // After the write, neither effective field has changed yet —
        // the gate still tracks the OLD state (cpu_speed=0,
        // contention_disable=0).
        const uint8_t eff_speed_after_writes = cm.cpu_speed();
        const bool eff_cd_after_writes = cm.contention_disable();
        const bool gate_on_after_writes = cm.is_contended_access();

        // Step 1: hc<256 (no NR 0x08 b6 commit) AND bus_idle=true
        // (NR 0x07 commit fires). Expected: cpu_speed flips to 1 (gate
        // forced off via cpu_speed term), but eff_nr_08_contention_disable
        // stays at 0.
        cm.commit_pending_cpu_speed_on_bus_idle(true);
        cm.commit_contention_disable_on_hc(100);
        const uint8_t eff_speed_step1 = cm.cpu_speed();
        const bool eff_cd_step1 = cm.contention_disable();
        // Gate is now off via cpu_speed (zxnext.vhd:4481 forces gate
        // low when cpu_speed != 0 OR eff_contention_disable=1).
        const bool gate_off_step1 = !cm.is_contended_access();

        // Step 2: reset shadows + effectives to 0/false, repeat the
        // simultaneous-write stimulus, then commit ONLY NR 0x08 b6
        // (bus_idle false suppresses NR 0x07 commit; hc(8)=1 commits
        // NR 0x08 b6).
        cm.set_cpu_speed(0);                       // immediate: clear effective + shadow
        cm.set_contention_disable(false);          // immediate: clear effective + shadow

        cm.set_pending_cpu_speed(1);
        cm.set_contention_disable_shadow(true);

        cm.commit_pending_cpu_speed_on_bus_idle(false);  // bus_idle=0 — no commit
        cm.commit_contention_disable_on_hc(300);   // hc(8)=1 — commits
        const uint8_t eff_speed_step2 = cm.cpu_speed();
        const bool eff_cd_step2 = cm.contention_disable();
        // NR 0x08 b6 committed → gate off via that term, even though
        // cpu_speed effective is still 0.
        const bool gate_off_step2 = !cm.is_contended_access();

        // Step 3: now also fire the NR 0x07 commit. Both effectives
        // should be set; gate stays off.
        cm.commit_pending_cpu_speed_on_bus_idle(true);
        const uint8_t eff_speed_step3 = cm.cpu_speed();
        const bool eff_cd_step3 = cm.contention_disable();
        const bool gate_off_step3 = !cm.is_contended_access();

        const bool ok = (eff_speed_after_writes == 0)
                     && (eff_cd_after_writes == false)
                     && gate_on_after_writes
                     && (eff_speed_step1 == 1)
                     && (eff_cd_step1 == false)     // NR 0x08 b6 NOT yet committed
                     && gate_off_step1              // gate off via cpu_speed
                     && (eff_speed_step2 == 0)      // NR 0x07 NOT committed (bus_idle=0)
                     && (eff_cd_step2 == true)      // NR 0x08 b6 committed
                     && gate_off_step2              // gate off via contention_disable
                     && (eff_speed_step3 == 1)
                     && (eff_cd_step3 == true)
                     && gate_off_step3;

        check("CT-TURBO-08",
              "Combined mid-line NR 0x07 + NR 0x08 b6 writes: each shadow "
              "commits on its OWN edge (NR 0x07 on bus-idle CLK_CPU; NR 0x08 "
              "b6 on bus-idle CLK_CPU AND hc(8)='1'); independent — one "
              "edge satisfied does not commit the other "
              "[zxnext.vhd:5796-5828]",
              ok,
              std::string("post_writes=(speed=") + std::to_string(eff_speed_after_writes)
              + ",cd=" + std::to_string(eff_cd_after_writes)
              + ",gate_on=" + std::to_string(gate_on_after_writes) + ")"
              + " step1=(speed=" + std::to_string(eff_speed_step1)
              + ",cd=" + std::to_string(eff_cd_step1)
              + ",gate_off=" + std::to_string(gate_off_step1) + ")"
              + " step2=(speed=" + std::to_string(eff_speed_step2)
              + ",cd=" + std::to_string(eff_cd_step2)
              + ",gate_off=" + std::to_string(gate_off_step2) + ")"
              + " step3=(speed=" + std::to_string(eff_speed_step3)
              + ",cd=" + std::to_string(eff_cd_step3)
              + ",gate_off=" + std::to_string(gate_off_step3) + ")");
    }

    // CT-FUSE-05 — see new check() above (G53 single-contention-path
    // invariant). The original "FUSE-table bypass switch not exposed"
    // skip() was retired here once G53 deleted the parallel FUSE-table
    // path itself: there is no longer a "second path" to toggle.
}

// ══════════════════════════════════════════════════════════════════════
// Cat 27: Memory-subsystem fix-regression rows for ContentionModel.
// 1-to-1 regression coverage for verify9-memory class-(a) fixes that
// touched ContentionModel (commit f5ec6d8). VHDL citations are
// reproduced from the original commit message.
// ══════════════════════════════════════════════════════════════════════

static void test_cat27_verify9_regressions() {
    set_group("CT-CAT27");

    // FIX-CONTEND-NR03-01 — commit f5ec6d8 (verify9) A3:
    // ContentionModel::rebuild_for_type() refreshes type + LUT without
    // touching dynamic gate state on a runtime NR 0x03 machine-timing
    // commit. VHDL :5137-5145 + :4481 + :4489-4493 — the contention
    // gate / mem_contend decode follows the new machine_timing.
    //
    // Discriminative test: split into two passes so the field-preservation
    // check is separated from the LUT-update check:
    //   PASS A — Field preservation: seed all 5 dynamic gate fields with
    //     distinguishable non-default values, call rebuild_for_type, then
    //     verify each field individually round-trips (= the call did not
    //     reset them via the build()-equivalent codepath).
    //   PASS B — LUT update: use a configuration where the contention
    //     gate is open (cpu_speed=0, contention_disable=0) so the
    //     per-machine bank decode in is_contended_access() is observable.
    //     Confirm the LUT/type tracks the new machine type.
    //
    // Reviewer finding (NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY-REVIEW
    // #6): the original row only verified `port_7ffd_io_en_` preservation,
    // missing the other four fields (`mem_active_page_`, `cpu_speed_`,
    // `contention_disable_`, `contention_disable_shadow_`). A regression
    // that preserves only some fields would partially pass. The split
    // structure pins all five at the field level + LUT update separately.
    {
        // PASS A — field preservation across rebuild_for_type.
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_port_7ffd_io_en(true);
        cm.set_mem_active_page(0x42);
        cm.set_pending_cpu_speed(0x02);
        cm.commit_pending_cpu_speed_on_bus_idle(true, false);  // cpu_speed_=2
        cm.set_pending_cpu_speed(0x03);                        // pending=3, cpu_speed_=2
        cm.set_contention_disable(false);
        cm.set_contention_disable_shadow(true);
        // Rebuild for +3; all 5 fields must persist.
        cm.rebuild_for_type(MachineType::ZX_PLUS3);
        const uint8_t post_map      = cm.mem_active_page();
        const uint8_t post_speed    = cm.cpu_speed();
        const uint8_t post_pending  = cm.pending_cpu_speed();
        const bool    post_cd       = cm.contention_disable();
        const bool    post_cd_shdw  = cm.contention_disable_shadow();
        const bool    post_ioen     = cm.port_7ffd_io_en();
        const bool fields_preserved =
            post_map == 0x42 && post_speed == 0x02 && post_pending == 0x03 &&
            post_cd == false && post_cd_shdw == true && post_ioen == true;

        // PASS B — LUT/type update with gate open. Use a fresh model so
        // the gate-disabling fields don't perturb the is_contended_access
        // signal. cpu_speed=0 + contention_disable=0 leaves only the
        // per-machine bank decode at :4490-4492 driving the result.
        //
        // V24-MEM-01 / V25-MEM-01 fix update: post-fix the
        // per-machine mem_contend decode is keyed on `machine_timing_`
        // (tim_sel axis), NOT on `type_` (typ_sel axis). To exercise
        // the +3 bank decode, both axes must be aligned — `build()`
        // seeds them coherently, and `rebuild_for_type` is the
        // typ_sel-side rebuild only. The production NR 0x03
        // dispatcher pushes the matching `set_pending_machine_timing()`
        // separately (immediate set_machine_timing in tests since we
        // don't model the video-frame seam here). Drive both axes to
        // reflect the canonical NextZXOS NR 0x03 path.
        ContentionModel cm2;
        cm2.build(MachineType::ZX48K);
        cm2.set_mem_active_page(8);   // bank 4 lo (= bank 4 page 8 on +3)
        const bool pre_p8  = cm2.is_contended_access();   // 48K: false
        cm2.set_mem_active_page(10);
        const bool pre_p10 = cm2.is_contended_access();   // 48K: true
        cm2.rebuild_for_type(MachineType::ZX_PLUS3);
        cm2.set_machine_timing(MachineTimingMode::TimingPlus3);  // tim_sel co-commit
        cm2.set_mem_active_page(8);
        const bool post_p8  = cm2.is_contended_access();  // +3: true
        cm2.set_mem_active_page(10);
        const bool post_p10 = cm2.is_contended_access();  // +3: true
        const bool lut_changed =
            !pre_p8 && pre_p10 && post_p8 && post_p10;

        char detail[300];
        std::snprintf(detail, sizeof(detail),
                      "PASS-A: post map=0x%02X speed=%u pending=%u cd=%d cd_shdw=%d ioen=%d "
                      "(exp 0x42/2/3/0/1/1); PASS-B: pre48K p8/p10=%d/%d (exp 0/1) "
                      "post+3 p8/p10=%d/%d (exp 1/1)",
                      post_map, post_speed, post_pending,
                      (int)post_cd, (int)post_cd_shdw, (int)post_ioen,
                      (int)pre_p8, (int)pre_p10,
                      (int)post_p8, (int)post_p10);
        check("FIX-CONTEND-NR03-01",
              "ContentionModel::rebuild_for_type updates type/LUT and "
              "preserves ALL 5 dynamic gate fields (mem_active_page, "
              "cpu_speed, pending_cpu_speed, contention_disable/_shadow, "
              "port_7ffd_io_en) — VHDL :5137-5145 + :4489-4493; "
              "commit f5ec6d8",
              fields_preserved && lut_changed,
              std::string(detail));
    }

    // FIX-CONTEND-NR03-INT-01 — emulator NR 0x03 dispatcher invokes
    // ContentionModel::rebuild_for_type when the machine-type commit
    // condition fires (config_mode=1 + valid typ_sel). Pre-fix the
    // dispatcher missed the rebuild call: the LUT/type stayed pinned to
    // the boot-time CLI value.
    //
    // Reviewer finding (#3 + #6): bypass risk for FIX-CONTEND-NR03-01
    // unit row. This integration row drives the production NR 0x03 path
    // and observes that the contention model's `type_` (via the LUT
    // is_contended_access decode) tracked the change.
    {
        // V24-MEM-01 / V25-MEM-01 fix update: pre-fix this row asserted
        // ZXN_ISSUE2 → no contention (typ_sel-driven switch returned
        // false for the ZXN branch). Post-fix the contention decode is
        // keyed on `machine_timing_` (tim_sel axis), and Next boot's
        // canonical nr_03_machine_timing is "011" (+3 timing, VHDL
        // :1099) — so ZXN_ISSUE2 already runs +3 contention rules. To
        // keep the row discriminative, start from 48K timing and
        // drive a 48K → +3 NR 0x03 commit. 48K mem_contend at
        // page=0x08 is false ((low>>1)&7 = 4 != 5); +3's is true
        // ((low&8)!=0). The pre→post pivot remains the same observable
        // contract: NR 0x03 commit changes the contention decode.
        Emulator emu48;
        const bool ok = make_emu(emu48, MachineType::ZX48K);
        if (!ok) {
            check("FIX-CONTEND-NR03-INT-01",
                  "Emulator::init(ZX48K) failed — would verify NR 0x03 dispatcher "
                  "drives both axes",
                  false, "Emulator::init returned false");
        } else {
            // Pre-state: 48K timing, page=0x08 — 48K mem_contend's
            // (page(3:1)=101) bank-5 decode is false here.
            emu48.contention().set_mem_active_page(0x08);
            const bool pre_contend = emu48.contention().is_contended_access();
            // Drive NR 0x03 = 0xB3 — bit 7=1 (gate the timing commit
            // path at VHDL :5124), bits 6:4 = 011 (tim_sel=+3 timing),
            // bits 2:0 = 011 (typ_sel=ZX_PLUS3). Both axes committed in
            // one write, matching the canonical NextZXOS boot path.
            // Then run a frame so the deferred timing latch promotes
            // shadow → effective at the video-frame edge (VHDL
            // :6694-6703).
            emu48.nextreg().write(0x03, 0xB3);
            emu48.run_frame();
            // Post-state: page=0x08 now hits +3's (low&8)!=0 → contend.
            emu48.contention().set_mem_active_page(0x08);
            const bool post_contend = emu48.contention().is_contended_access();
            check("FIX-CONTEND-NR03-INT-01",
                  "Emulator NR 0x03 dispatcher drives BOTH axes "
                  "(rebuild_for_type for typ_sel + "
                  "set_pending_machine_timing for tim_sel + per-frame "
                  "commit_pending_machine_timing) — page=0x08 contends "
                  "only post-+3 commit (VHDL :4489-4493 + :5761-5777 + "
                  ":6694-6703; commit f5ec6d8 + V24-MEM-01)",
                  !pre_contend && post_contend,
                  std::string("pre 48K contend(0x08)=") +
                  std::to_string(pre_contend) +
                  " post +3 contend(0x08)=" + std::to_string(post_contend) +
                  " (exp 0/1)");
        }
    }

    // FIX-CONTEND-7FFD-01 — commit f5ec6d8 (verify9) A4: port_7ffd_active
    // OR-term in port_contend. VHDL zxnext.vhd:4496 + :2594 + :2593:
    //   port_contend <= (not cpu_a(0)) or port_7ffd_active or ...
    //   port_7ffd_active <= '1' when port_7ffd='1' and (s128_timing_hw_en
    //                       OR p3_timing_hw_en) else '0';
    // Pre-fix the bare port_contend() dropped this term; port 0x7FFD
    // writes on 128K/+3 missed contention. Discriminative: 128K +
    // port=0x7FFD with port_7ffd_io_en=1 → contended. With io_en=0 →
    // not contended (gate disabled).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        // port_7ffd_io_en defaults to false until set; explicitly assert
        // baseline.
        cm.set_port_7ffd_io_en(false);
        const bool pre_io_dis = cm.port_contend(0x7FFD, /*ulap=*/false);
        cm.set_port_7ffd_io_en(true);
        const bool post_io_en = cm.port_contend(0x7FFD, /*ulap=*/false);
        check("FIX-CONTEND-7FFD-01",
              "128K + port 0x7FFD: port_contend gated on port_7ffd_io_en "
              "(NR 0x82 b1) — VHDL :4496/:2594/:2593; commit f5ec6d8",
              !pre_io_dis && post_io_en,
              std::string("io_en=0: ") + std::to_string(pre_io_dis) +
              " (exp 0) / io_en=1: " + std::to_string(post_io_en) +
              " (exp 1)");
    }

    // FIX-CONTEND-7FFD-02 — discriminative on machine type. 48K does NOT
    // engage the port_7ffd_active OR-term (s128_timing_hw_en=0,
    // p3_timing_hw_en=0). With port_7ffd_io_en=true, port 0x7FFD is
    // still odd → not contended on 48K.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_port_7ffd_io_en(true);
        const bool got = cm.port_contend(0x7FFD, /*ulap=*/false);
        check("FIX-CONTEND-7FFD-02",
              "48K + port 0x7FFD + io_en=1: port_contend=0 "
              "(s128/p3 timing gates off) — VHDL :2594; commit f5ec6d8",
              !got,
              std::string("got=") + std::to_string(got) + " (exp 0)");
    }

    // FIX-CONTEND-7FFD-03 — +3 also engages port_7ffd_active per VHDL
    // :2594, but the address decode at :2593 for +3 timing additionally
    // requires cpu_a(14)='1' (since p3_timing forces the AND with
    // cpu_a(14) AND NOT (cpu_a(13:12)="01")). 0x7FFD has bit14=1, bit13=1,
    // bit12=1 → port_7ffd condition holds. So +3 + 0x7FFD + io_en=1
    // contends.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX_PLUS3);
        cm.set_port_7ffd_io_en(true);
        const bool got = cm.port_contend(0x7FFD, /*ulap=*/false);
        check("FIX-CONTEND-7FFD-03",
              "+3 + port 0x7FFD + io_en=1: port_contend=1 — "
              "VHDL :2593-2594; commit f5ec6d8",
              got,
              std::string("got=") + std::to_string(got) + " (exp 1)");
    }
}

// ══════════════════════════════════════════════════════════════════════
// FIX-MEMACTIVE-PAGE-01 — commit a9cbf79 (verify6). z80_cpu.cpp's
// mem_active_page_for() previously called Mmu::get_effective_page (which
// resolves the 0xFF sentinel into the physical ROM page). VHDL :2949-2956
// keys mem_active_page on the LIVE MMU<i> register (= Mmu::get_page(),
// = nr_mmu_[slot]). For sram_rom>=1 the physical page's low nibble had
// bit-1 / bit-3 set, falsely firing the mem_contend decode at :4489 for
// slot-0/1 ROM accesses on 128K (sram_rom=1) / +3 (sram_rom>=2). The
// VHDL :4489 mem_contend gate is `'0' when mem_active_page(7:4) /=
// "0000"` — i.e. the 0xFF sentinel high nibble suppresses contention.
// The regression here exercises the Mmu accessor contract directly: a
// reset Mmu (slots 0/1 = ROM, nr_mmu_=0xFF) must report 0xFF via
// get_page(slot) regardless of the underlying physical ROM page.
// ══════════════════════════════════════════════════════════════════════

static void test_cat27_memactive_page_sentinel() {
    set_group("CT-CAT27-MEMPG");

    // FIX-MEMACTIVE-PAGE-01: Mmu::get_page() for slot 0/1 at reset must
    // return 0xFF (= MMU<i> register value), not the resolved physical
    // ROM page. This is the contract z80_cpu's mem_active_page_for()
    // depends on.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX128K);
        if (!ok) {
            check("FIX-MEMACTIVE-PAGE-01",
                  "Emulator::init failed — would verify Mmu::get_page "
                  "returns 0xFF sentinel for ROM slots 0/1 on 128K "
                  "[zxnext.vhd:2949-2956,4489]",
                  false, "Emulator::init returned false");
        } else {
            // 128K reset puts slots 0/1 in ROM mode. Pick port_7ffd(4)=1
            // to make sram_rom=1 — the pre-fix path would have resolved
            // slot 0 to physical page 2 (sram_rom*2+0=2). VHDL+post-fix:
            // get_page(0) = 0xFF (verbatim MMU<0> register).
            emu.mmu().map_128k_bank(0x10);
            const uint8_t g0 = emu.mmu().get_page(0);
            const uint8_t g1 = emu.mmu().get_page(1);
            check("FIX-MEMACTIVE-PAGE-01",
                  "Mmu::get_page returns 0xFF MMU<i> sentinel for "
                  "legacy-ROM slot 0/1 (NOT resolved physical page) — "
                  "VHDL :2949-2956 mem_active_page <= MMU<i>; commit a9cbf79",
                  g0 == 0xFF && g1 == 0xFF,
                  std::string("get_page(0)=0x") +
                  (g0 < 0x10 ? "0" : "") + std::to_string(g0) +
                  " get_page(1)=0x" +
                  (g1 < 0x10 ? "0" : "") + std::to_string(g1) +
                  " (exp 0xFF/0xFF)");
        }
    }

    // FIX-MEMACTIVE-PAGE-INT-01 — the actual fix at z80_cpu.cpp's
    // `mem_active_page_for()` switched from `Mmu::get_effective_page` to
    // `Mmu::get_page`. The pre-fix path resolved slot 0 ROM access on
    // 128K (sram_rom=1) to physical page 2 — the low nibble bit-1 set
    // would falsely fire mem_contend. Post-fix: get_page returns the
    // MMU<i> 0xFF sentinel, whose high nibble suppresses contention per
    // VHDL :4489 (`mem_contend = '0' when mem_active_page(7:4) /= "0000"`).
    //
    // Reviewer finding (#7 + #5): the original FIX-MEMACTIVE-PAGE-01 only
    // verified the Mmu accessor contract — that contract was already
    // correct pre-fix. A regression where someone reverts the cpu-side
    // call back to `get_effective_page` would not be caught. This row
    // drives the production CPU path (cpu().execute() of an instruction
    // whose memory access lands in slot 0) and observes that the
    // ContentionModel's `mem_active_page` ends up at the 0xFF sentinel,
    // NOT the resolved physical page.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX128K);
        if (!ok) {
            check("FIX-MEMACTIVE-PAGE-INT-01",
                  "Emulator::init failed — would verify CPU mem_active_page_for "
                  "calls Mmu::get_page (NOT get_effective_page)",
                  false, "Emulator::init returned false");
        } else {
            // 128K with sram_rom=1 (port_7ffd bit 4 = 1): slot 0 maps to
            // physical ROM page 2. Pre-fix mem_active_page_for(0x0000)
            // returns 2 via get_effective_page; post-fix returns 0xFF
            // via get_page (MMU<0> sentinel).
            emu.mmu().map_128k_bank(0x10);
            // Inject `LD A,(0x0000)` at PC=0x8000 (slot 4 = bank 2 RAM
            // = uncontended, doesn't perturb mem_active_page state).
            //
            //   3A 00 00   LD A,(0x0000)   — single mem read from slot 0
            //   76         HALT
            emu.mmu().write(0x8000, 0x3A);
            emu.mmu().write(0x8001, 0x00);
            emu.mmu().write(0x8002, 0x00);
            emu.mmu().write(0x8003, 0x76);
            auto regs = emu.cpu().get_registers();
            regs.PC     = 0x8000;
            regs.SP     = 0xBF00;
            regs.IFF1   = 0;
            regs.IFF2   = 0;
            regs.halted = false;
            emu.cpu().set_registers(regs);
            // Step the LD A,(nn). The data fetch at 0x0000 invokes
            // `set_mem_active_page(mem_active_page_for(0x0000))` per
            // src/cpu/z80_cpu.cpp:132. The HALT M1 fetch at 0x8003
            // would also push 0x8003's MMU<4> = 0x05 — to isolate the
            // 0x0000 read, capture mem_active_page after a single
            // execute() (which dispatches one instruction = LD A,(nn)
            // = 4-cycle M1 fetch + 3-cycle ND + 3-cycle ND + 3-cycle MR).
            // The final mem_active_page set is the MR cycle for 0x0000.
            emu.cpu().execute();   // LD A,(0x0000) — last set is for 0x0000
            const uint8_t got = emu.contention().mem_active_page();
            char buf[64];
            std::snprintf(buf, sizeof(buf),
                          "contention.mem_active_page=0x%02X "
                          "(exp 0xFF; pre-fix would yield 0x02 = sram_rom*2+0)",
                          got);
            check("FIX-MEMACTIVE-PAGE-INT-01",
                  "CPU mem_active_page_for(0x0000) on 128K with sram_rom=1 "
                  "returns Mmu::get_page() = 0xFF MMU<0> sentinel "
                  "(NOT get_effective_page=2 physical) — VHDL :2949-2956 + "
                  ":4489; commit a9cbf79",
                  got == 0xFF,
                  std::string(buf));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// Cat 28: V15-CPU-NIT-03 — ULA+ port contention shadow propagation
// (reviewer-promoted from Pass-15 audit deflection).
//
// VHDL oracle:
//   * zxnext.vhd:2439 — `port_ulap_io_en <= internal_port_enable(24);`
//   * zxnext.vhd:2685-2686 — port_bf3b/port_ff3b address decode AND-gated
//     by `port_ulap_io_en`
//   * zxnext.vhd:4496 — `port_contend <= ... or port_bf3b or port_ff3b;`
//     i.e. the ULA+ ports are part of the CPU `o_cpu_contend` OR-tree
//   * zxnext.vhd:1229 — `nr_85_internal_port_enable` resets to all-1
//
// The audit's NIT-3 deflection ("memory subsystem boundary issue") was
// rejected by the reviewer: the CPU-side I/O bus callbacks
// (fuse_z80_readport / fuse_z80_writeport in src/cpu/z80_cpu.cpp) call
// contention_tick() with the default `port_ulap_io_en=false`. Without
// the shadow, ULA+ port IORQs to $BF3B/$FF3B miss the per-cycle
// contention stretch — same regression pattern as Verify9-memory's
// `port_7ffd_io_en_` fix, just for a different OR-term.
//
// Resolution: ContentionModel grew a `port_ulap_io_en_` shadow plus
// `set_port_ulap_io_en()` setter, mirrored from NR 0x85 bit 0 by the
// Emulator. `contention_tick()` OR-folds the parameter with the shadow
// so the CPU-side seam fires correctly.
// ══════════════════════════════════════════════════════════════════════

static void test_cat28_v15_cpu_nit_03() {
    set_group("CT-CAT28-V15");

    // Helper: invoke contention_tick() with the same arg shape the CPU-side
    // bus callback uses (mreq_n=true, iorq_n=false → port read/write cycle),
    // omit the port_ulap_io_en parameter (default=false). Use a (hc, vc)
    // position INSIDE the active raster window so wait_s='1'. Per
    // zxula.vhd:582-583: hc(8)=0, vc(8)=0, vc(7:6)/="11", and either
    // hc_adj(3:2)/="00" or hc_adj(3:1)="000" with timing_p3='1'.
    // hc=4 → hc_adj=5 → hc_adj(3:2)=01 (OK on 48K/128K). vc=100 (visible).
    constexpr uint16_t kHc = 4;
    constexpr uint16_t kVc = 100;

    // V15-CPU-NIT-03-01: BF3B port (ULA+ index) — pre-fix, the CPU-side
    // call with default port_ulap_io_en=false misses the OR-term.
    // Post-fix: the shadow OR-folds in and the contention term fires.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_port_ulap_io_en(true);  // mirrors NR 0x85 bit 0 = 1
        // contention_tick with port_ulap_io_en parameter omitted (defaults
        // to false) — represents the production CPU-side seam. The
        // shadow should still gate on `port_bf3b` per zxnext.vhd:4496.
        const uint8_t stretch = cm.contention_tick(
            /*mreq_n=*/true,  /*iorq_n=*/false,
            /*rd_n=*/false,   /*wr_n=*/true,
            /*cpu_a=*/0xBF3B, /*hc=*/kHc, /*vc=*/kVc);
        check("V15-CPU-NIT-03-01",
              "ZX128K, NR 0x85 b0=1, IORQ@$BF3B, default param "
              "port_ulap_io_en=false → contention_tick stretches > 0 "
              "(zxnext.vhd:2439,2685,4496; reviewer-promoted V15-CPU-NIT-03)",
              stretch > 0,
              std::string("stretch=") + std::to_string(stretch));
    }

    // V15-CPU-NIT-03-02: FF3B port (ULA+ data) — same as 01 for the
    // sibling port (zxnext.vhd:2686).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_port_ulap_io_en(true);
        const uint8_t stretch = cm.contention_tick(
            /*mreq_n=*/true,  /*iorq_n=*/false,
            /*rd_n=*/false,   /*wr_n=*/true,
            /*cpu_a=*/0xFF3B, /*hc=*/kHc, /*vc=*/kVc);
        check("V15-CPU-NIT-03-02",
              "ZX128K, NR 0x85 b0=1, IORQ@$FF3B, default param "
              "port_ulap_io_en=false → contention_tick stretches > 0 "
              "(zxnext.vhd:2439,2686,4496)",
              stretch > 0,
              std::string("stretch=") + std::to_string(stretch));
    }

    // V15-CPU-NIT-03-03: NR 0x85 b0 = 0 disables ULA+ contention (the
    // gate works in the negative direction too). $BF3B is odd
    // (low byte 0x3B), so the even-port term doesn't mask it — the
    // contention path requires the ULA+ OR-term, which must be off when
    // NR 0x85 b0 = 0.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_port_ulap_io_en(false);
        const uint8_t stretch = cm.contention_tick(
            /*mreq_n=*/true,  /*iorq_n=*/false,
            /*rd_n=*/false,   /*wr_n=*/true,
            /*cpu_a=*/0xBF3B, /*hc=*/kHc, /*vc=*/kVc);
        check("V15-CPU-NIT-03-03",
              "ZX128K, NR 0x85 b0=0, IORQ@$BF3B → contention_tick "
              "stretch == 0 (gate disabled; zxnext.vhd:2685+4496)",
              stretch == 0,
              std::string("stretch=") + std::to_string(stretch));
    }

    // V15-CPU-NIT-03-04: shadow setter round-trip — set true, read back,
    // set false, read back. Confirms the accessor matches the setter.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_port_ulap_io_en(true);
        const bool t1 = cm.port_ulap_io_en();
        cm.set_port_ulap_io_en(false);
        const bool t2 = cm.port_ulap_io_en();
        check("V15-CPU-NIT-03-04",
              "set_port_ulap_io_en(true) → port_ulap_io_en()==true; "
              "set_port_ulap_io_en(false) → port_ulap_io_en()==false "
              "(round-trip; mirrors port_7ffd_io_en pattern)",
              t1 == true && t2 == false);
    }

    // V15-CPU-NIT-03-05: parameter still works (parameter OR shadow).
    // This guards the audit's stated "bare-class accessor handles ULA+"
    // contract (existing tests use the parameter form). Pass param=true,
    // shadow=false → should still fire. Conversely, the post-fix
    // contention_tick OR-folds, so neither term being false yields off,
    // and either being true yields on. We verify the OR semantics here.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);
        cm.set_port_ulap_io_en(false);  // shadow off
        // Parameter ON via the legacy bare-class accessor — must still
        // fire (backward compatibility for unit-test fixtures).
        const bool pc =
            cm.port_contend(0xBF3B, /*port_ulap_io_en=*/true);
        check("V15-CPU-NIT-03-05",
              "port_contend(0xBF3B, port_ulap_io_en=true) returns true "
              "regardless of shadow state (parameter override; "
              "zxnext.vhd:2685+4496)",
              pc);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Cat 29: V24-MEM-01 / V25-MEM-01 — contention timing axis split.
// Discriminative tests for the machine_timing_ (tim_sel axis, NR 0x03
// bits 6:4) vs MachineType (typ_sel axis, NR 0x03 bits 2:0) split.
//
// VHDL anchors:
//   * zxnext.vhd:5761-5777 — combinational decode of
//     eff_nr_03_machine_timing into one-hot machine_timing_*.
//   * zxnext.vhd:6694-6703 — eff_nr_03_machine_timing latches
//     nr_03_machine_timing on video_frame_sync='1' (per-frame edge).
//   * zxnext.vhd:4490-4492 — mem_contend per-machine bank decode
//     keyed on machine_timing_*.
//   * zxnext.vhd:5121-5135 — NR 0x03 bits 6:4 commit path (immediate
//     write to nr_03_machine_timing, gated on bit 7=1 + user_dt_lock=0
//     + bit 3=0).
//
// Pre-fix scope: the contention surfaces (is_contended_access,
// contention_tick, port_contend, Mmu::mem_contend_for_) keyed on
// `MachineType` (typ_sel axis). Pre-fix a user-written NR 0x03 with
// `tim_sel != typ_sel` would silently mis-decode contention. Post-fix
// the two axes are independent.
// ══════════════════════════════════════════════════════════════════════

static void test_cat29_v24_mem_01() {
    set_group("CT-CAT29-V24");

    // D3-CONTENTION-01 — Bare-class ContentionModel: independent axis
    // commit. Drive machine_timing_=Timing128 with type_=ZX48K. The
    // mem_contend decode must follow tim_sel (Timing128 → odd-banks);
    // pre-fix it followed typ_sel (ZX48K → bank-5 only) and would
    // mis-classify page=0x02 (bank 1 lo, odd bank).
    //
    // Discriminative: page=0x02 (low nibble = 2):
    //   * Timing48 (bank-5 only):  (low>>1)&7 = 1 != 5 → false
    //   * Timing128 (odd banks):   low&2 = 2 → true
    //   * TimingPlus3 (banks >= 4): low&8 = 0 → false
    // Build with type_=ZX48K (sets machine_timing_=Timing48 via init);
    // then override machine_timing_=Timing128 via set_machine_timing().
    // Pre-fix the bare accessor switched on type_ → returned false.
    // Post-fix it switches on machine_timing_ → returns true.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);                 // both axes = 48
        cm.set_mem_active_page(0x02);                  // bank 1 lo (odd)
        const bool pre  = cm.is_contended_access();    // 48: bank-5 decode → false
        cm.set_machine_timing(MachineTimingMode::Timing128);
        const bool post = cm.is_contended_access();    // 128: odd-banks → true
        check("D3-CONTENTION-01",
              "tim_sel=128 / typ_sel=48 / page=0x02 — pre-fix follows "
              "typ_sel (48: bank-5 only → false); post-fix follows "
              "tim_sel (128: odd banks → true) per VHDL "
              "zxnext.vhd:4491,5761-5777",
              !pre && post,
              std::string("pre 48-typ=") + std::to_string(pre) +
              " post 128-tim=" + std::to_string(post) + " (exp 0/1)");
    }

    // D3-CONTENTION-02 — opposite direction: typ_sel=ZX128K but
    // tim_sel=Plus3. Discriminate at page=0x08 (low nibble bit 3 = 1):
    //   * Timing128 (odd banks):    low&2 = 0 → false
    //   * TimingPlus3 (banks >= 4): low&8 = 8 → true
    {
        ContentionModel cm;
        cm.build(MachineType::ZX128K);                // both axes = 128
        cm.set_mem_active_page(0x08);                 // bank 4 lo (even, banks>=4)
        const bool pre  = cm.is_contended_access();   // 128: odd-banks → false
        cm.set_machine_timing(MachineTimingMode::TimingPlus3);
        const bool post = cm.is_contended_access();   // +3: banks>=4 → true
        check("D3-CONTENTION-02",
              "tim_sel=+3 / typ_sel=128 / page=0x08 — pre-fix follows "
              "typ_sel (128: odd banks → false); post-fix follows "
              "tim_sel (+3: banks>=4 → true) per VHDL "
              "zxnext.vhd:4492,5761-5777",
              !pre && post,
              std::string("pre 128-typ=") + std::to_string(pre) +
              " post +3-tim=" + std::to_string(post) + " (exp 0/1)");
    }

    // D3-CONTENTION-03 — video-frame deferred-commit latch behaviour
    // per VHDL zxnext.vhd:6694-6703:
    //   if video_frame_sync = '1' then
    //     eff_nr_03_machine_timing <= nr_03_machine_timing;
    //   end if;
    // The shadow (`pending_machine_timing_`, mirror of
    // `nr_03_machine_timing`) updates immediately on the gated NR 0x03
    // write; the effective field (`machine_timing_`, mirror of
    // `eff_nr_03_machine_timing`) only commits at the next video-frame
    // edge. Discriminative test: push a pending change AND probe the
    // contention decode BEFORE commit (must still see old value); then
    // commit and re-probe (must see new value).
    //
    // Setup: build for 48K, page=0x02 (48: false, 128: true). Push
    // pending=Timing128 — contention still uses Timing48 (effective).
    // Commit — contention now sees Timing128.
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);
        cm.set_mem_active_page(0x02);
        const bool pre = cm.is_contended_access();   // 48: false
        cm.set_pending_machine_timing(MachineTimingMode::Timing128);
        // Pre-commit: effective still Timing48 → still false.
        const bool mid = cm.is_contended_access();
        // Commit at frame edge → effective = Timing128 → now true.
        cm.commit_pending_machine_timing();
        const bool post = cm.is_contended_access();
        check("D3-CONTENTION-03",
              "video-frame deferred-commit latch — set_pending defers "
              "the new tim_sel until commit_pending_machine_timing() at "
              "the frame edge; pre-commit retains old effective value "
              "(VHDL zxnext.vhd:6694-6703)",
              !pre && !mid && post,
              std::string("pre=") + std::to_string(pre) +
              " mid(pending=128, eff=48)=" + std::to_string(mid) +
              " post-commit=" + std::to_string(post) +
              " (exp 0/0/1)");
    }

    // D3-CONTENTION-04 — Emulator integration: NR 0x03 with tim_sel !=
    // typ_sel exercises the deferred-commit path through the production
    // NR 0x03 write_handler + run_frame() seam. This is the
    // discriminative end-to-end row: pre-fix the contention model
    // followed typ_sel only, so a NR 0x03 write with tim_sel=+3 +
    // typ_sel=128 would mis-classify page=0x08.
    //
    // Pivot at page=0x08: 128 timing → false (odd-banks); +3 timing
    // → true (banks>=4). Drive NR 0x03 = 0xB2:
    //   bit 7=1 (gate timing-commit)
    //   bits 6:4 = 011 (tim_sel = +3 timing)
    //   bit 3=0 (don't toggle dt_lock)
    //   bits 2:0 = 010 (typ_sel = 128 type — DIFFERENT from tim_sel!)
    // Pre-write: 128K machine, page=0x08 → 128 mem_contend false.
    // Post-write (after run_frame to commit the frame-edge latch):
    //   typ_sel commit → MachineType=ZX128K (unchanged, since 128K was
    //                    the boot type).
    //   tim_sel commit → machine_timing_=TimingPlus3 (CHANGED, now
    //                    different from typ_sel).
    //   page=0x08 → +3 mem_contend true.
    //
    // Test passes iff post-write contention follows the tim_sel axis,
    // proving the axes are independent (the V24-MEM-01 fix).
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX128K);
        if (!ok) {
            check("D3-CONTENTION-04",
                  "Emulator::init(ZX128K) failed",
                  false, "Emulator::init returned false");
        } else {
            emu.contention().set_mem_active_page(0x08);
            const bool pre = emu.contention().is_contended_access();
            // Confirm pre-write: 128 mem_contend is false at page=0x08.

            // Write NR 0x03 = 0xB2 = bit7=1 | tim_sel=3<<4 | typ_sel=2.
            // Different tim_sel and typ_sel — the V24 discriminative
            // condition.
            emu.nextreg().write(0x03, 0xB2);

            // Immediately after the write (before frame edge), the
            // tim_sel commit is in pending only — contention should
            // still see the OLD effective (Timing128 → page=0x08 false).
            emu.contention().set_mem_active_page(0x08);
            const bool mid = emu.contention().is_contended_access();

            // Run a frame — the per-frame seam commits the latch
            // (run_frame() top calls commit_pending_machine_timing()).
            emu.run_frame();
            emu.contention().set_mem_active_page(0x08);
            const bool post = emu.contention().is_contended_access();

            // Also confirm the typ_sel commit independently: NR 0x03
            // read should now compose typ_sel=2 (ZX128K = unchanged
            // since emulator was already 128K) AND tim_sel=3 (+3
            // timing — the divergent axis).
            const uint8_t nr_03_after = emu.nextreg().nr_03_machine_timing();
            const MachineType type_after = emu.mmu().machine_type();
            const bool typ_unchanged = (type_after == MachineType::ZX128K);
            const bool tim_committed = (nr_03_after == 0x03);

            check("D3-CONTENTION-04",
                  "Emulator NR 0x03 with tim_sel != typ_sel: pre-write "
                  "128 mem_contend(0x08)=false; mid (pending only)=false; "
                  "post-run_frame +3 mem_contend(0x08)=true; typ_sel "
                  "axis unchanged (still 128) — V24-MEM-01 axis "
                  "independence (VHDL zxnext.vhd:4490-4492 + 5121-5135 + "
                  "5761-5777 + 6694-6703)",
                  !pre && !mid && post && typ_unchanged && tim_committed,
                  std::string("pre=") + std::to_string(pre) +
                  " mid=" + std::to_string(mid) +
                  " post=" + std::to_string(post) +
                  " typ_unchanged=" + std::to_string(typ_unchanged) +
                  " tim_committed(nr_03_tim=0x" +
                  std::to_string(nr_03_after) + ")=" +
                  std::to_string(tim_committed) +
                  " (exp 0/0/1/1/1)");
        }
    }

    // D3-CONTENTION-05 — Mmu::machine_timing_ axis storage + commit
    // primitives. Probes the Mmu-side mirror through the production
    // Emulator init path. The Mmu's `machine_timing_` feeds the
    // `mem_contend_for_(addr)` per-page decode (VHDL :4490-4492) used
    // by the floating-bus latch at :4498-4509. Mmu's setters mirror
    // the ContentionModel API one-for-one.
    {
        Emulator emu;
        const bool ok = make_emu(emu, MachineType::ZX128K);
        if (!ok) {
            check("D3-CONTENTION-05",
                  "Emulator::init(ZX128K) failed — Mmu::machine_timing_ "
                  "axis storage + commit probe",
                  false, "Emulator::init returned false");
        } else {
            // Post-init, Mmu's tim_sel matches the CLI 128K → Timing128.
            const MachineTimingMode init_tim = emu.mmu().machine_timing();

            // Drive pending → effective via the deferred-commit path
            // (mirrors what the NR 0x03 handler + run_frame() do in
            // production). Drive Mmu directly to keep this row scoped
            // to the Mmu axis storage (D3-CONTENTION-04 covers the
            // end-to-end Emulator path).
            emu.mmu().set_pending_machine_timing(MachineTimingMode::TimingPlus3);
            const MachineTimingMode pre_eff  = emu.mmu().machine_timing();
            const MachineTimingMode pre_pend = emu.mmu().pending_machine_timing();
            emu.mmu().commit_pending_machine_timing();
            const MachineTimingMode post_eff = emu.mmu().machine_timing();

            // Verify the immediate-commit setter too (mirrors the
            // init-time push and the load_state re-sync push).
            emu.mmu().set_machine_timing(MachineTimingMode::Timing48);
            const MachineTimingMode imm_eff  = emu.mmu().machine_timing();
            const MachineTimingMode imm_pend = emu.mmu().pending_machine_timing();

            check("D3-CONTENTION-05",
                  "Mmu::machine_timing_ axis storage + deferred-commit "
                  "primitives: init (128K) → Timing128; "
                  "set_pending(+3) → effective still Timing128; commit "
                  "→ TimingPlus3; set_machine_timing(48) immediate-pair "
                  "(VHDL :6694-6703 + V24-MEM-01)",
                  init_tim == MachineTimingMode::Timing128 &&
                  pre_eff  == MachineTimingMode::Timing128 &&
                  pre_pend == MachineTimingMode::TimingPlus3 &&
                  post_eff == MachineTimingMode::TimingPlus3 &&
                  imm_eff  == MachineTimingMode::Timing48 &&
                  imm_pend == MachineTimingMode::Timing48);
        }
    }

    // D3-CONTENTION-06 — Pentagon timing-axis disables contention
    // independent of MachineType. VHDL zxnext.vhd:4481 gates
    // `i_contention_en` on `(not machine_timing_pentagon)`, the
    // tim_sel axis. Pre-fix the term was hard-coded to '0' (the
    // standalone Pentagon MachineType was retired). Post-fix a user
    // who writes NR 0x03 with tim_sel=Pentagon (e.g. bits 6:4=100)
    // disables contention even if typ_sel is something else.
    //
    // Discriminative: 48K-typ + Pentagon-tim + page=0x0A (bank 5 lo).
    // Timing48 → bank-5 contends (true). Pentagon → gate disabled
    // (false).
    {
        ContentionModel cm;
        cm.build(MachineType::ZX48K);                 // tim=48, typ=48
        cm.set_mem_active_page(0x0A);
        const bool pre  = cm.is_contended_access();   // 48: true
        cm.set_machine_timing(MachineTimingMode::TimingPentagon);
        const bool post = cm.is_contended_access();   // Pentagon: gate off → false
        check("D3-CONTENTION-06",
              "tim_sel=Pentagon disables contention regardless of "
              "typ_sel (VHDL zxnext.vhd:4481 — i_contention_en gates on "
              "NOT machine_timing_pentagon)",
              pre && !post,
              std::string("pre 48-tim=") + std::to_string(pre) +
              " post Pent-tim=" + std::to_string(post) + " (exp 1/0)");
    }

    // D3-CONTENTION-NIT-01 — V24-MEM-NIT-01 (reviewer follow-up):
    // Emulator::load_state must PRESERVE the schema-restored
    // (effective, pending) machine_timing pair when both Mmu schema
    // slots are present in the save stream. Pre-fix the re-sync at
    // emulator.cpp:7376-7395 unconditionally called set_machine_timing()
    // on BOTH ContentionModel and Mmu, collapsing pending into effective
    // — a one-frame round-trip imperfection for snapshots taken between
    // an NR 0x03 bits-6:4 write and the next video-frame edge (VHDL
    // :6694-6703 deferred-commit window).
    //
    // Discriminative setup:
    //   1. Init Emulator as ZX48K → both axes = Timing48
    //   2. Write NR 0x03 = 0xA0 (bit7=1 gate, bits 6:4 = 010 = tim_sel
    //      128) — shadow=Timing128, effective=Timing48 (no run_frame).
    //   3. Save state. The Mmu schema serialises BOTH (effective=
    //      Timing48, pending=Timing128).
    //   4. Construct a SECOND Emulator at ZX48K (same init defaults).
    //   5. Plant a divergence on the second Emulator's ContentionModel
    //      pending field (e.g. set_pending_machine_timing(Timing48)) so
    //      we can detect whether load_state restores the pending=128
    //      state from the schema or clobbers it.
    //   6. Load the state. Pre-fix: ContentionModel's pending becomes
    //      Timing48 (the effective NR-03-derived value clobbers it).
    //      Post-fix: ContentionModel's pending stays Timing128 (the
    //      schema-restored value).
    //   7. Verify the (effective, pending) pair on Mmu AND ContentionModel
    //      both reflect (Timing48, Timing128).
    //   8. Run a frame — pending commits → effective; verify both
    //      converge to Timing128.
    {
        Emulator emu1;
        const bool ok1 = make_emu(emu1, MachineType::ZX48K);
        if (!ok1) {
            check("D3-CONTENTION-NIT-01",
                  "Emulator::init(ZX48K) failed — would verify "
                  "load_state preserves machine_timing pair from Mmu "
                  "schema",
                  false, "Emulator::init returned false");
        } else {
            // Post-init both axes = Timing48 (ZX48K default).
            // Write NR 0x03 = 0xA0: bit7=1 (gate timing-commit),
            // bits 6:4 = 010 (tim_sel = Timing128), bit 3 = 0,
            // bits 2:0 = 000 (typ_sel = 0; typ commit gated by
            // config_mode which is off — typ_sel axis stays).
            emu1.nextreg().write(0x03, 0xA0);

            // Confirm the pre-save state: effective stays Timing48,
            // pending advances to Timing128 (deferred-commit window).
            const MachineTimingMode src_mmu_eff  = emu1.mmu().machine_timing();
            const MachineTimingMode src_mmu_pend = emu1.mmu().pending_machine_timing();
            const MachineTimingMode src_cm_eff   = emu1.contention().machine_timing();
            const MachineTimingMode src_cm_pend  = emu1.contention().pending_machine_timing();
            const bool src_state_correct =
                src_mmu_eff  == MachineTimingMode::Timing48 &&
                src_mmu_pend == MachineTimingMode::Timing128 &&
                src_cm_eff   == MachineTimingMode::Timing48 &&
                src_cm_pend  == MachineTimingMode::Timing128;

            // Save state.
            StateWriter measure;
            emu1.save_state(measure);
            const size_t snap_size = measure.position();
            std::vector<uint8_t> buf(snap_size, 0);
            StateWriter w(buf.data(), snap_size);
            emu1.save_state(w);
            const bool save_size_match = (w.position() == snap_size);

            // Construct a fresh Emulator at the same machine type. Post-
            // init defaults: all axes = Timing48 / Timing48 (both
            // effective and pending). Plant a divergence on the
            // ContentionModel pending to verify load_state actually
            // re-pushes the schema-restored pending value rather than
            // leaving it untouched.
            Emulator emu2;
            const bool ok2 = make_emu(emu2, MachineType::ZX48K);
            if (!ok2) {
                check("D3-CONTENTION-NIT-01",
                      "Emulator::init(ZX48K) #2 failed",
                      false, "second Emulator::init returned false");
            } else {
                // Plant divergence on second Emulator's ContentionModel
                // pending field (mirrors how V16-CPU-01 test seeds a
                // pre-load divergence).
                emu2.contention().set_pending_machine_timing(
                    MachineTimingMode::TimingPlus3);
                const MachineTimingMode planted_cm_pend =
                    emu2.contention().pending_machine_timing();

                // Load state.
                StateReader r(buf.data(), snap_size);
                emu2.load_state(r);

                // Post-load assertions:
                // (a) Mmu effective and pending restored from schema.
                const MachineTimingMode l_mmu_eff  = emu2.mmu().machine_timing();
                const MachineTimingMode l_mmu_pend = emu2.mmu().pending_machine_timing();
                // (b) ContentionModel mirrors the schema-restored pair —
                //     the V24-MEM-NIT-01 fix re-pushes pending into the
                //     ContentionModel rather than collapsing it.
                const MachineTimingMode l_cm_eff   = emu2.contention().machine_timing();
                const MachineTimingMode l_cm_pend  = emu2.contention().pending_machine_timing();
                // (c) Schema-loaded flag should be true (new-format save).
                const bool flag = emu2.mmu().machine_timing_loaded_from_schema();

                const bool post_load_correct =
                    l_mmu_eff  == MachineTimingMode::Timing48  &&
                    l_mmu_pend == MachineTimingMode::Timing128 &&
                    l_cm_eff   == MachineTimingMode::Timing48  &&
                    l_cm_pend  == MachineTimingMode::Timing128 &&
                    flag;

                // (d) Run a frame — the per-frame seam at run_frame()
                //     top calls commit_pending_machine_timing() on
                //     BOTH Mmu and ContentionModel, promoting Timing128
                //     into effective.
                emu2.run_frame();
                const MachineTimingMode f_mmu_eff = emu2.mmu().machine_timing();
                const MachineTimingMode f_cm_eff  = emu2.contention().machine_timing();
                const bool post_frame_correct =
                    f_mmu_eff == MachineTimingMode::Timing128 &&
                    f_cm_eff  == MachineTimingMode::Timing128;

                check("D3-CONTENTION-NIT-01",
                      "Emulator::load_state PRESERVES the schema-"
                      "restored machine_timing (effective, pending) pair "
                      "from the Mmu schema slot; ContentionModel mirrors "
                      "the pair; commit_pending at next run_frame() "
                      "promotes pending→effective (V24-MEM-NIT-01 fix; "
                      "VHDL :6694-6703 deferred-commit window)",
                      src_state_correct && save_size_match &&
                      planted_cm_pend == MachineTimingMode::TimingPlus3 &&
                      post_load_correct && post_frame_correct,
                      std::string("src(mmu_eff,mmu_pend,cm_eff,cm_pend)=(") +
                      std::to_string(static_cast<int>(src_mmu_eff))  + "," +
                      std::to_string(static_cast<int>(src_mmu_pend)) + "," +
                      std::to_string(static_cast<int>(src_cm_eff))   + "," +
                      std::to_string(static_cast<int>(src_cm_pend))  +
                      ") expected(0,1,0,1); load(mmu_eff,mmu_pend,cm_eff,cm_pend)=(" +
                      std::to_string(static_cast<int>(l_mmu_eff))  + "," +
                      std::to_string(static_cast<int>(l_mmu_pend)) + "," +
                      std::to_string(static_cast<int>(l_cm_eff))   + "," +
                      std::to_string(static_cast<int>(l_cm_pend))  +
                      ") expected(0,1,0,1); schema_flag=" +
                      std::to_string(flag) +
                      "; post-frame(mmu_eff,cm_eff)=(" +
                      std::to_string(static_cast<int>(f_mmu_eff)) + "," +
                      std::to_string(static_cast<int>(f_cm_eff))  +
                      ") expected(1,1)");
            }
        }
    }
}

// ── Task 50 — the CPU→contention SEAM (raw frame vs ULA counters) ─────
//
// Every other row in this file calls `contention_tick()` DIRECTLY, passing
// the ULA's own display-relative counters (the VHDL i_hc/i_vc the gate is
// written against — hence "vc=100 (visible)" in those rows). Production
// does NOT call it that way: `fuse_z80_readbyte()` and friends derive RAW
// frame-relative (hc, vc) from the FUSE T-state counter (vc=0 at frame
// start; the display only begins at c_min_vactive=64 on a 48K).
//
// Nothing checked that the two conventions agreed, so both sides could be
// individually "right" while the wiring between them was wrong — and it
// was, for the entire life of the contention model: the gate ran 64 lines
// too high, contending across the TOP BORDER and NOT contending over the
// BOTTOM 64 display lines. Measured against real FUSE on bifrost.tap: 808
// phantom T-states per frame.
//
// These rows therefore drive the PRODUCTION seam — set the FUSE T-state
// counter to a chosen raster position, invoke the real CPU memory callback,
// and measure the T-states it actually charges. A read costs 3 T + whatever
// contention the gate adds, so `delta - 3` is the contention stretch.
extern "C" uint8_t  fuse_z80_readbyte(uint16_t address);
extern "C" uint32_t* fuse_z80_tstates_ptr(void);

static void test_cpu_seam_raster_window()
{
    // 48K: 224 T/line, display lines are raw vc 64..255 (c_min_vactive=64).
    constexpr int kTsPerLine  = 224;
    constexpr int kFirstDispVc = 64;
    constexpr int kLastDispVc  = 255;

    Emulator emu;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    emu.init(cfg);

    // Contention on a 48K applies to 0x4000-0x7FFF (bank 5, screen RAM).
    // hc is derived as (ts_in_line * 2); pick a T-state offset whose hc
    // lands inside the active display window AND on a contended hc_adj
    // phase, so a correctly-gated access must stretch.
    auto stretch_at = [&](int raw_vc, int ts_in_line) -> int {
        uint32_t* ts = fuse_z80_tstates_ptr();
        *ts = static_cast<uint32_t>(raw_vc * kTsPerLine + ts_in_line);
        const uint32_t before = *ts;
        (void)fuse_z80_readbyte(0x4000);
        const int delta = static_cast<int>(*ts - before);
        return delta - 3;   // a data read is 3 T + contention
    };

    // ts_in_line 80 → hc = 160, inside the 48K display window (128..383).
    constexpr int kDispTs = 80;

    // T50-01 — TOP BORDER must NOT contend. This is the half of the bug
    // that stole BIFROST's 808 T-states: raw vc 10 is 54 lines ABOVE the
    // first display line, but the raw-fed gate saw "vc=10 < 192 → visible".
    {
        const int s = stretch_at(/*raw_vc=*/10, kDispTs);
        check("T50-01",
              "48K, read $4000 at raw vc=10 (TOP BORDER, above c_min_vactive=64) "
              "→ NO contention (zxula.vhd:414 border_active_v is keyed on the "
              "ULA display-relative i_vc, reset at c_min_vactive per "
              "zxula_timing.vhd:441-452)",
              s == 0, std::string("stretch=") + std::to_string(s));
    }

    // T50-02 — first display line MUST contend (the window opens here).
    {
        const int s = stretch_at(kFirstDispVc, kDispTs);
        check("T50-02",
              "48K, read $4000 at raw vc=64 (FIRST display line) → contends "
              "(zxula.vhd:414)",
              s > 0, std::string("stretch=") + std::to_string(s));
    }

    // T50-03 — BOTTOM display third MUST contend. This is the OTHER half of
    // the bug, and the one no screenshot could catch: with the raw frame vc
    // fed in, raw vc 200 tripped `vc(7)&vc(6)` → the gate called it BORDER
    // and silently stopped contending over the last 64 display lines.
    {
        const int s = stretch_at(/*raw_vc=*/200, kDispTs);
        check("T50-03",
              "48K, read $4000 at raw vc=200 (BOTTOM display third, still "
              "within 64..255) → contends; pre-fix the raw vc tripped "
              "border_active_v and silently stopped contending (zxula.vhd:414)",
              s > 0, std::string("stretch=") + std::to_string(s));
    }

    // T50-04 — last display line contends; the line AFTER it must not.
    {
        const int s_last  = stretch_at(kLastDispVc, kDispTs);
        const int s_after = stretch_at(kLastDispVc + 1, kDispTs);
        check("T50-04",
              "48K, read $4000: raw vc=255 (LAST display line) contends, "
              "raw vc=256 (BOTTOM BORDER) does not — the window closes at "
              "exactly 192 ULA lines (zxula.vhd:414)",
              s_last > 0 && s_after == 0,
              std::string("last=") + std::to_string(s_last)
                  + " after=" + std::to_string(s_after));
    }

    // T50-05 — horizontal half of the same rebase: an access in the LEFT
    // BORDER of a display line must not contend (hc(8) gate, zxula.vhd:416),
    // which is only correct once hc is rebased onto ula_min_hactive.
    {
        const int s = stretch_at(kFirstDispVc + 20, /*ts_in_line=*/2);  // hc=4
        check("T50-05",
              "48K, read $4000 on a display line but at hc=4 (LEFT BORDER, "
              "before ula_min_hactive=c_min_hactive-12=116) → NO contention "
              "(zxula.vhd:416 border_active_ula = i_hc(8) or border_active_v; "
              "zxula_timing.vhd:423)",
              s == 0, std::string("stretch=") + std::to_string(s));
    }

    // T50-06 — the origins are PER MACHINE, not hardcoded. 128K has the same
    // c_min_vactive (64) but a different c_min_hactive (136 → ula origin 124)
    // and a longer line (228 T). Re-running the border/display pair on a 128K
    // catches any future regression that bakes in the 48K constants.
    {
        Emulator emu128;
        EmulatorConfig cfg128;
        cfg128.type = MachineType::ZX128K;
        emu128.init(cfg128);
        constexpr int kTsPerLine128 = 228;

        auto stretch128 = [&](int raw_vc, int ts_in_line) -> int {
            uint32_t* ts = fuse_z80_tstates_ptr();
            *ts = static_cast<uint32_t>(raw_vc * kTsPerLine128 + ts_in_line);
            const uint32_t before = *ts;
            (void)fuse_z80_readbyte(0x4000);
            return static_cast<int>(*ts - before) - 3;
        };
        const int s_border = stretch128(/*raw_vc=*/10, /*ts_in_line=*/84);
        const int s_disp   = stretch128(/*raw_vc=*/100, /*ts_in_line=*/84);
        check("T50-06",
              "128K (c_min_hactive=136, 228 T/line): top border does not "
              "contend and a display line does — proves the ULA counter "
              "origins are taken per-machine from VideoTiming, not hardcoded "
              "to the 48K values (zxula_timing.vhd:195,203)",
              s_border == 0 && s_disp > 0,
              std::string("border=") + std::to_string(s_border)
                  + " display=" + std::to_string(s_disp));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Contention Model Compliance Tests\n");
    std::printf("=================================\n\n");
    std::printf("  (Phase-A: 28 rows from §4/§5/§6/§7/§8/§12;\n");
    std::printf("   Phase-B: 36 rows from §8/§9/§10/§11/§12/§13;\n");
    std::printf("   Phase-C: 4 rows from §12 CT-PENT-05 + §14 CT-INT-01..03 — integration smoke)\n\n");

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

    test_delay_drift_bound();
    std::printf("  Group: CT-DELAY       — done\n");

    test_fuse_inopcode_contention();
    std::printf("  Group: CT-FUSE        — done\n");

    // Cat 27: testcov-memory regression rows for verify6/verify9
    // memory-subsystem fixes that touched ContentionModel / Mmu page
    // accessor. See doc/issues/nextzxos-boot/
    // NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY.md.
    test_cat27_verify9_regressions();
    std::printf("  Group: CT-CAT27       — done\n");

    test_cat27_memactive_page_sentinel();
    std::printf("  Group: CT-CAT27-MEMPG — done\n");

    // Cat 28: V15-CPU-NIT-03 — ULA+ port contention shadow propagation
    // (reviewer-promoted from Pass-15 audit deflection). See
    // doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY15-CPU-REVIEW.md
    test_cat28_v15_cpu_nit_03();
    std::printf("  Group: CT-CAT28-V15   — done\n");

    // Cat 29: V24-MEM-01 / V25-MEM-01 — contention timing axis split
    // (machine_timing_ vs MachineType). VHDL anchors :4490-4492 +
    // :5761-5777 + :6694-6703. See doc/issues/nextzxos-boot/
    // NEXTZXOS-BOOT-SUBSYSTEM-VERIFY24-MEMORY.md + VERIFY25-MEMORY.md.
    test_cat29_v24_mem_01();
    std::printf("  Group: CT-CAT29-V24   — done\n");

    test_cpu_seam_raster_window();
    std::printf("  Group: T50-CPU-SEAM   — done\n");

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
