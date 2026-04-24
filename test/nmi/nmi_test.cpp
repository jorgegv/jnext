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
// Main
// =====================================================================

int main() {
    std::printf("NMI Source Pipeline Compliance Tests (Phase 1 scaffold)\n");
    std::printf("=========================================================\n\n");

    g_rst_defaults(); std::printf("  RST reset-defaults -- done\n");

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
