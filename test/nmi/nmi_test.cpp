// NMI Source Pipeline Compliance Test Runner
//
// Phase 1 scaffold (2026-04-24) against
// doc/testing/NMI-PIPELINE-TEST-PLAN-DESIGN.md. Every assertion cites
// the exact VHDL file and line from the authoritative FPGA source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo; cited here for provenance, not edited).
//
// Ground rules (per doc/testing/UNIT-TEST-PLAN-EXECUTION.md):
//   * VHDL is the oracle; the C++ emulator is the thing under test.
//   * Every check(id, desc, actual_cond, "VHDL file:line ...") maps to
//     exactly one plan row.
//   * A plan row that the current public API in src/peripheral/
//     cannot reach uses skip(id, "reason") — no skip() calls in this
//     scaffold; Phase 1 carries one live row only.
//
// Phase 1 scope: a single RST-01 row exercising the reset defaults of
// the new NmiSource class. Subsequent waves (Phase 2 A/B/C/E) add the
// remaining ~48 rows per the plan appendix.
//
// Run: ./build/test/nmi_test

#include "peripheral/nmi_source.h"

// Wave A integration row also exercises NR 0x02 through the real port
// path (Emulator → NextReg → write_handler → NmiSource::nr_02_write).
#include "core/emulator.h"
#include "core/emulator_config.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- Test infrastructure --------------------------------------------

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
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    g_results.push_back(Result{g_group, id, desc, cond, detail});
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

} // namespace

// =====================================================================
// Group RST — Reset defaults (3 rows; only RST-01 live in Phase 1)
// =====================================================================

static void g_rst_defaults()
{
    set_group("RST");

    // RST-01 — VHDL zxnext.vhd:2120, 2149 (FSM power-on S_NMI_IDLE),
    // 2095-2105 (latches clear on i_reset), 1109-1110, 1222 (gate
    // flags power-on '0'). Plus VHDL:2164-2170 (nmi_generate_n idle
    // = '1') and VHDL:2107 (is_activated = 0 with all latches clear).
    //
    // This scaffold row bundles every visible reset default into a
    // single aggregate check() so Phase 2 waves can split it into
    // RST-01 / RST-02 / RST-03 per the plan doc. Until then, a single
    // failure here means any one of the default conditions is wrong.
    {
        NmiSource nmi;
        // Fresh construction already calls reset(); call it again to
        // exercise the explicit path too.
        nmi.reset();

        const bool state_idle       = (nmi.state() == NmiSource::State::Idle);
        const bool latch_mf_clear   = !nmi.nmi_mf();
        const bool latch_dmc_clear  = !nmi.nmi_divmmc();
        const bool latch_exp_clear  = !nmi.nmi_expbus();
        const bool gate_mf_off      = !nmi.mf_enable();
        const bool gate_dmc_off     = !nmi.divmmc_enable();
        const bool gate_expdbc_off  = !nmi.expbus_debounce_disable();
        const bool cfg_mode_off     = !nmi.config_mode();
        const bool nmi_gen_idle     = nmi.nmi_generate_n();       // '1' inactive
        const bool not_activated    = !nmi.is_activated();
        const bool expbus_pin_idle  = nmi.expbus_nmi_n();          // '1' idle
        const bool latched_none     = (nmi.latched() == NmiSource::Src::None);

        const bool all_defaults_ok =
            state_idle &&
            latch_mf_clear && latch_dmc_clear && latch_exp_clear &&
            gate_mf_off && gate_dmc_off && gate_expdbc_off &&
            cfg_mode_off &&
            nmi_gen_idle && not_activated &&
            expbus_pin_idle && latched_none;

        check("RST-01",
              "FSM idle + latches clear + gates off + nmi_generate_n high + not activated after reset",
              all_defaults_ok,
              "zxnext.vhd:2120,2149 (FSM) / 2095-2105 (latches) / 1109-1110,1222 (gates) / 2164-2170 (nmi_generate_n) / 2107 (nmi_activated)");
    }
}

// =====================================================================
// Group NR02 — NR 0x02 software NMI (Wave A, 6 rows)
//
// VHDL zxnext.vhd:3830-3838 — bits 3/2 of an NR 0x02 write drive the
// `nmi_gen_nr_mf` / `nmi_gen_nr_divmmc` CPU/Copper strobes which OR
// into `nmi_sw_gen_mf` / `nmi_sw_gen_divmmc` and feed the central
// producer/priority-latch pipeline. VHDL zxnext.vhd:5891 composes the
// readback byte so bits 3/2 report MF / DivMMC request pending until
// the FSM reaches `S_NMI_END` (VHDL:2149-2162), at which point the
// latches and readback-pending flags clear together.
//
// Rows NR02-01..05 exercise the `NmiSource` API directly (stand-alone)
// so they stay deterministic and fast. Row NR02-06 covers the
// enable-gate behaviour. An additional NR02-INT row drives the full
// Emulator path through the NextReg select/data port to confirm the
// Wave A write handler is wired (Deliverable 1).
// =====================================================================

static void g_nr02_sw_nmi()
{
    set_group("NR02");

    // ------------------------------------------------------------------
    // NR02-01 — NR 0x02 write bit 3 latches `nmi_mf`.
    // VHDL zxnext.vhd:3832 (nmi_gen_nr_mf = bit 3), :3837
    // (nmi_sw_gen_mf), :2097 (nmi_mf priority latch).
    // MF-enable (NR 0x06 bit 3) must be on for the producer gate in
    // :2090 to pass — Wave C hasn't landed, so we stage the gate via
    // `NmiSource::set_mf_enable(true)` directly.
    // ------------------------------------------------------------------
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);
        nmi.nr_02_write(0x08);        // bit 3 = MF software NMI
        nmi.tick(1);                  // one combinational update
        check("NR02-01",
              "NR 0x02 bit 3 write sets nmi_mf latch "
              "[zxnext.vhd:3832,3837,2097]",
              nmi.nmi_mf() && !nmi.nmi_divmmc() && !nmi.nmi_expbus());
    }

    // ------------------------------------------------------------------
    // NR02-02 — NR 0x02 write bit 2 latches `nmi_divmmc`.
    // VHDL zxnext.vhd:3833 (nmi_gen_nr_divmmc = bit 2), :3838
    // (nmi_sw_gen_divmmc), :2099 (nmi_divmmc priority latch).
    // DivMMC-enable (NR 0x06 bit 4) gates the :2091 assert signal.
    // ------------------------------------------------------------------
    {
        NmiSource nmi;
        nmi.set_divmmc_enable(true);
        nmi.nr_02_write(0x04);        // bit 2 = DivMMC software NMI
        nmi.tick(1);
        check("NR02-02",
              "NR 0x02 bit 2 write sets nmi_divmmc latch "
              "[zxnext.vhd:3833,3838,2099]",
              nmi.nmi_divmmc() && !nmi.nmi_mf() && !nmi.nmi_expbus());
    }

    // ------------------------------------------------------------------
    // NR02-03 — Both bits set → MF wins the priority chain.
    // VHDL zxnext.vhd:2097-2105 — MF latch is the first evaluated;
    // DivMMC latch (:2099) requires NOT nmi_mf, so with both strobes
    // arriving simultaneously only `nmi_mf` ends up set.
    // ------------------------------------------------------------------
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);
        nmi.set_divmmc_enable(true);
        nmi.nr_02_write(0x0C);        // bits 3 AND 2
        nmi.tick(1);
        check("NR02-03",
              "Simultaneous bit 3+2 write: MF wins, DivMMC blocked "
              "[zxnext.vhd:2097-2105]",
              nmi.nmi_mf() && !nmi.nmi_divmmc());
    }

    // ------------------------------------------------------------------
    // NR02-04 — NR 0x02 readback reports bit 3 = 1 after a bit-3 write.
    // VHDL zxnext.vhd:5891 layout (bit 3 = nr_02_generate_mf_nmi,
    // bit 2 = nr_02_generate_divmmc_nmi). The Phase-1 readback owns
    // only bits 3/2 (other fields return 0 per Deliverable 2 contract).
    // ------------------------------------------------------------------
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);
        nmi.nr_02_write(0x08);
        // No tick needed — readback-pending is set inside nr_02_write;
        // VHDL:5891 reports the pending flag without requiring the FSM
        // to advance.
        const uint8_t r = nmi.nr_02_read();
        check("NR02-04",
              "NR 0x02 readback bit 3 = 1 after bit-3 write "
              "[zxnext.vhd:5891]",
              (r & 0x08) != 0 && (r & 0x04) == 0);
    }

    // ------------------------------------------------------------------
    // NR02-05 — Readback bits 3/2 auto-clear when the FSM reaches
    // `S_NMI_END`. VHDL zxnext.vhd:5891 (readback term), :2149-2162
    // (FSM End-state latch clear). Walk the FSM Idle → Fetch → Hold →
    // End via the Phase-1 scaffold's observer API: observe_m1_fetch at
    // PC 0x0066 advances Fetch→Hold, then `mf_nmi_hold` defaulting to
    // false advances Hold→End on the next tick. End-state clears the
    // latch AND `nr_02_pending_mf`, so the next read returns 0.
    // ------------------------------------------------------------------
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);
        nmi.nr_02_write(0x08);
        nmi.tick(1);   // Idle → Fetch (is_activated == true)
        // Pre-END sanity: readback should still show bit 3 set while
        // the FSM holds the request in Fetch/Hold.
        const uint8_t pre = nmi.nr_02_read();

        // Fetch → Hold on M1 fetch at 0x0066 (VHDL:2135-2138).
        nmi.observe_m1_fetch(0x0066, /*m1=*/true, /*mreq=*/true);
        nmi.tick(1);

        // Hold → End — mf_nmi_hold defaults false, so `!hold` is true
        // and the next recompute_ advances to End, which clears the
        // latches + readback-pending bits (VHDL:2149-2162 + 5891).
        nmi.tick(1);

        const uint8_t post = nmi.nr_02_read();
        check("NR02-05",
              "NR 0x02 readback bits 3/2 auto-clear at FSM S_NMI_END "
              "[zxnext.vhd:5891, 2149-2162]",
              (pre & 0x08) != 0 && (post & 0x0C) == 0,
              "pre=" + std::to_string(pre) + " post=" + std::to_string(post));
    }

    // ------------------------------------------------------------------
    // NR02-06 — NR 0x02 bit 3 write with MF-enable gate OFF → no latch.
    // VHDL zxnext.vhd:2090 — `nmi_assert_mf` is gated by
    // `nr_06_button_m1_nmi_en`. With the gate off, the strobe never
    // reaches the priority latch so `nmi_mf` stays clear. The
    // readback-pending bit, however, does still get set inside
    // `nr_02_write` (VHDL-faithful: the strobe fires; only the latch
    // gate blocks downstream acceptance).
    // ------------------------------------------------------------------
    {
        NmiSource nmi;
        // mf_enable default is false — keep it that way.
        nmi.nr_02_write(0x08);        // bit 3 = MF software NMI
        nmi.tick(1);
        check("NR02-06",
              "MF-enable gate OFF blocks nmi_mf latch on NR 0x02 bit 3 "
              "write [zxnext.vhd:2090]",
              !nmi.nmi_mf() && !nmi.nmi_divmmc() && !nmi.nmi_expbus());
    }
}

// =====================================================================
// Group NR02-INT — Wave A Emulator integration (1 row)
//
// Drives NR 0x02 through the real port path
// (OUT 0x243B,reg; OUT 0x253B,val) to confirm the Wave A write-handler
// wiring in Emulator::init(). This exercises NextReg::write →
// registered write_handler → NmiSource::nr_02_write — so if the
// handler is unwired the row fails even though the stand-alone
// NmiSource rows above still pass.
// =====================================================================

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

static void g_nr02_integration()
{
    set_group("NR02-INT");

    // NR02-INT-01 — Write NR 0x02 = 0x04 (DivMMC bit) via the real
    // port path and observe `NmiSource::nmi_divmmc()` latch.
    // VHDL zxnext.vhd:3833/3838 (write path) + Wave A wiring in
    // Emulator::init() (src/core/emulator.cpp NR 0x02 handler).
    {
        Emulator emu;
        build_next_emulator(emu);

        // Wave C hasn't landed, so the NR 0x06 bit-4 gate isn't
        // plumbed yet — set DivMMC-enable on NmiSource directly so
        // the producer gate at VHDL:2091 passes.
        emu.nmi_source().set_divmmc_enable(true);

        // OUT 0x243B, 0x02 (select NR 0x02);
        // OUT 0x253B, 0x04 (write DivMMC software NMI bit).
        emu.port().out(0x243B, 0x02);
        emu.port().out(0x253B, 0x04);

        // Let the NmiSource combinational step run (producer →
        // priority latch → FSM transition).
        emu.nmi_source().tick(1);

        const bool ok = emu.nmi_source().nmi_divmmc()
                        && !emu.nmi_source().nmi_mf();
        check("NR02-INT-01",
              "NR 0x02 write via OUT 0x253B routes to NmiSource "
              "[Wave A handler, zxnext.vhd:3833,3838,2099]",
              ok);
    }
}

// =====================================================================
// Main
// =====================================================================

int main() {
    std::printf("NMI Source Pipeline Compliance Tests (Phase 1 + Wave A)\n");
    std::printf("=========================================================\n\n");

    g_rst_defaults();   std::printf("  RST reset-defaults -- done\n");
    g_nr02_sw_nmi();    std::printf("  NR02 software-NMI -- done\n");
    g_nr02_integration(); std::printf("  NR02 integration -- done\n");

    std::printf("\n=========================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    if (!g_skipped.empty()) {
        std::printf("\nSkipped rows (facility not reachable via current API):\n");
        for (const auto& s : g_skipped)
            std::printf("  SKIP %-10s %s\n", s.id.c_str(), s.reason.c_str());
    }

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-20s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-20s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
