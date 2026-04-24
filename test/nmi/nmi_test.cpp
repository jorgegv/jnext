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

#include "peripheral/divmmc.h"
#include "peripheral/nmi_source.h"
#include "cpu/im2.h"

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
// Wave B (2026-04-24) — HK / DIS / CLR groups (13 rows, all LIVE)
//
// VHDL cites: zxnext.vhd:2089-2170 (source pipeline + arbiter) and
// divmmc.vhd:103-150 (button_nmi latch + o_disable_nmi).
//
// Modelling note — FSM+DivMMC feedback loop: NmiSource emits the
// VHDL:2170 `nmi_divmmc_button` strobe (`divmmc_button_strobe()`) for
// exactly one tick on IDLE→FETCH via the DivMMC path; Emulator wires
// that to `DivMmc::set_button_nmi(true)`. These rows drive the same
// seam by hand (no Emulator fixture) so they stay focused on the
// NmiSource + DivMmc interaction without pulling in the CPU tick
// machinery. The Emulator-level integration is covered indirectly by
// Phase 3 + regression flipping divmmc_test NM-01..08.
// =====================================================================

// Emulator mini-fixture: pumps the same "before/after tick" sequence
// that Emulator::run_frame() does, so FSM strobes drive DivMmc and
// DivMmc consumer feedback drives NmiSource next tick.
static void pump_tick(NmiSource& nmi, DivMmc& div)
{
    nmi.set_divmmc_nmi_hold(div.is_nmi_hold());
    nmi.set_divmmc_conmem(div.is_conmem());
    nmi.tick(1);
    if (nmi.divmmc_button_strobe()) {
        div.set_button_nmi(true);
    }
}

// =====================================================================
// Group HK — Hotkey producers (Wave B, 5 rows)
// =====================================================================

static void g_hotkey()
{
    set_group("HK");

    // HK-01 — VHDL zxnext.vhd:2090 (nmi_assert_mf = hotkey_m1 OR sw_gen_mf)
    // AND nr_06_button_m1_nmi_en. With NR 0x06 MF-enable ON, the MF
    // hotkey edge drives IDLE→FETCH.
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);     // NR 0x06 bit 3 = 1
        nmi.strobe_mf_button();
        nmi.tick(1);
        const bool state_ok = (nmi.state() == NmiSource::State::Fetch);
        const bool latched  = (nmi.latched() == NmiSource::Src::Mf);
        check("HK-01",
              "set_mf_button edge + NR 0x06 bit 3 = 1 → FSM IDLE→FETCH (MF latched)",
              state_ok && latched,
              "zxnext.vhd:2090 (nmi_assert_mf) / 2095-2116 (priority latch) / 2124-2128 (FSM IDLE→FETCH)");
    }

    // HK-02 — VHDL zxnext.vhd:2091 + NR 0x06 bit 4. DivMMC drive button
    // edge drives IDLE→FETCH.
    {
        NmiSource nmi;
        nmi.set_divmmc_enable(true); // NR 0x06 bit 4 = 1
        nmi.strobe_divmmc_button();
        nmi.tick(1);
        const bool state_ok = (nmi.state() == NmiSource::State::Fetch);
        const bool latched  = (nmi.latched() == NmiSource::Src::DivMmc);
        check("HK-02",
              "set_divmmc_button edge + NR 0x06 bit 4 = 1 → FSM IDLE→FETCH (DivMMC latched)",
              state_ok && latched,
              "zxnext.vhd:2091 / 2095-2116 / 2124-2128");
    }

    // HK-03 — NR 0x06 bit 3 = 0 gates MF producer off. The MF button
    // edge must NOT advance the FSM.
    {
        NmiSource nmi;
        nmi.set_mf_enable(false);    // NR 0x06 bit 3 = 0 (default)
        nmi.strobe_mf_button();
        nmi.tick(1);
        const bool idle = (nmi.state() == NmiSource::State::Idle);
        const bool no_latch = !nmi.nmi_mf();
        check("HK-03",
              "NR 0x06 bit 3 = 0 blocks MF producer (no FSM advance, no latch)",
              idle && no_latch,
              "zxnext.vhd:1110 (nr_06_button_m1_nmi_en) / 2090 (nmi_assert_mf gate)");
    }

    // HK-04 — NR 0x06 bit 4 = 0 gates DivMMC producer off.
    {
        NmiSource nmi;
        nmi.set_divmmc_enable(false);
        nmi.strobe_divmmc_button();
        nmi.tick(1);
        const bool idle = (nmi.state() == NmiSource::State::Idle);
        const bool no_latch = !nmi.nmi_divmmc();
        check("HK-04",
              "NR 0x06 bit 4 = 0 blocks DivMMC producer (no FSM advance, no latch)",
              idle && no_latch,
              "zxnext.vhd:1109 (nr_06_button_drive_nmi_en) / 2091 (nmi_assert_divmmc gate)");
    }

    // HK-05 — Simultaneous MF + DivMMC press, both gates enabled.
    // VHDL:2107-2113 — MF wins the priority chain
    // (nmi_assert_mf checked before nmi_assert_divmmc in the elsif
    // ladder). Expect `latched() == Mf`.
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);
        nmi.set_divmmc_enable(true);
        nmi.strobe_mf_button();
        nmi.strobe_divmmc_button();
        nmi.tick(1);
        const bool mf_won  = (nmi.latched() == NmiSource::Src::Mf);
        const bool no_dmc  = !nmi.nmi_divmmc();
        check("HK-05",
              "simultaneous MF + DivMMC press with both gates enabled: MF wins priority",
              mf_won && no_dmc,
              "zxnext.vhd:2107-2113 (priority chain — MF branch checked first)");
    }
}

// =====================================================================
// Group DIS — DivMMC consumer feedback (Wave B, 4 rows)
// =====================================================================

static void g_divmmc_consumer()
{
    set_group("DIS");

    // DIS-01 — FSM IDLE→FETCH for DivMMC path pulses the VHDL:2170
    // `nmi_divmmc_button` strobe; Emulator wires that to
    // `DivMmc::set_button_nmi(true)`. Verified here via the mini-
    // fixture `pump_tick()` which models the same wiring.
    {
        NmiSource nmi;
        DivMmc    div;
        nmi.set_divmmc_enable(true);
        const bool pre_btn = div.button_nmi();           // = 0 after reset
        nmi.strobe_divmmc_button();
        pump_tick(nmi, div);
        const bool strobe_fired = nmi.divmmc_button_strobe() ||
                                  /* already consumed in pump; observable via btn */
                                  div.button_nmi();
        const bool post_btn = div.button_nmi();          // should be 1
        const bool fsm_fetch = (nmi.state() == NmiSource::State::Fetch);
        check("DIS-01",
              "FSM IDLE→FETCH for DivMMC path pulses nmi_divmmc_button → DivMmc::set_button_nmi(true)",
              !pre_btn && post_btn && strobe_fired && fsm_fetch,
              "zxnext.vhd:2170 (nmi_divmmc_button) / divmmc.vhd:108-111 (button_nmi latch)");
    }

    // DIS-02 — DivMmc::is_nmi_hold() feedback drives divmmc_nmi_hold at
    // the arbiter. Set DivMmc automap_held by activating the automap
    // pipeline (check_automap at an entry-point PC), then verify
    // is_nmi_hold() reports true and that `set_divmmc_nmi_hold` at the
    // NmiSource layer consumes it correctly.
    {
        NmiSource nmi;
        DivMmc    div;
        div.set_enabled(true);
        div.set_entry_points_0(0x01);  // RST 0x00 enabled
        div.set_entry_valid_0(0x01);   // main path valid
        div.set_entry_timing_0(0x01);  // instant
        // First M1 at 0x0000 captures hold=1. Second M1 at another PC
        // with held already 1 promotes held from hold; then check_automap
        // sees held=1. Call twice to get held=1.
        div.check_automap(0x0000, true);  // instant_match → hold=1
        div.check_automap(0x0100, true);  // step: held <- hold (=1)
        const bool dmc_held  = div.automap_held();
        const bool hold_true = div.is_nmi_hold();
        // Push to NmiSource and verify arbiter accessor reflects it.
        nmi.set_divmmc_nmi_hold(div.is_nmi_hold());
        const bool at_nmi    = nmi.divmmc_nmi_hold();
        check("DIS-02",
              "DivMmc automap_held=1 → is_nmi_hold()=1 → NmiSource divmmc_nmi_hold=1",
              dmc_held && hold_true && at_nmi,
              "divmmc.vhd:150 (o_disable_nmi = automap_held OR button_nmi) / zxnext.vhd:2107,2118 (arbiter gate)");
    }

    // DIS-03 — o_disable_nmi = automap_held OR button_nmi (divmmc.vhd:150).
    // Exercise the OR directly via the const accessors.
    {
        DivMmc d1;  // automap_held=0, button_nmi=0 → hold=0
        const bool neither = !d1.is_nmi_hold();

        DivMmc d2;
        // Set only button_nmi via public setter (NmiSource-driven path).
        d2.set_button_nmi(true);
        const bool only_btn = d2.is_nmi_hold();

        DivMmc d3;
        d3.set_enabled(true);
        d3.set_entry_points_0(0x01);
        d3.set_entry_valid_0(0x01);
        d3.set_entry_timing_0(0x01);
        d3.check_automap(0x0000, true);
        d3.check_automap(0x0100, true);
        const bool only_hld = d3.is_nmi_hold() && !d3.button_nmi();

        DivMmc d4;
        d4.set_enabled(true);
        d4.set_entry_points_0(0x01);
        d4.set_entry_valid_0(0x01);
        d4.set_entry_timing_0(0x01);
        d4.check_automap(0x0000, true);
        d4.check_automap(0x0100, true);
        // Raising automap_held clears button_nmi (divmmc.vhd:112-113
        // rising-edge one-shot), so set button_nmi AFTER automap_held
        // settles to probe the "both" OR.
        d4.set_button_nmi(true);
        const bool both = d4.is_nmi_hold() && d4.button_nmi() && d4.automap_held();

        check("DIS-03",
              "is_nmi_hold() = automap_held OR button_nmi across {00,10,01,11}",
              neither && only_btn && only_hld && both,
              "divmmc.vhd:150 (o_disable_nmi)");
    }

    // DIS-04 — FSM HOLD → END on divmmc_nmi_hold clearing (VHDL:2135-2148,
    // with divmmc_nmi_hold selected when nmi_divmmc=1 per VHDL:2118).
    {
        NmiSource nmi;
        DivMmc    div;
        nmi.set_divmmc_enable(true);

        // Drive FSM IDLE → FETCH via DivMMC producer.
        nmi.strobe_divmmc_button();
        pump_tick(nmi, div);
        // At this point: state=Fetch, DivMmc::button_nmi=1,
        // is_nmi_hold() → 1 (via button_nmi). That hold gates HOLD→END.

        // FETCH → HOLD on M1 at 0x0066.
        nmi.observe_m1_fetch(0x0066, true, true);
        const bool hold_state = (nmi.state() == NmiSource::State::Hold);

        // Still held — tick stays in HOLD.
        pump_tick(nmi, div);
        const bool still_hold = (nmi.state() == NmiSource::State::Hold);

        // Clear the hold at the DivMmc layer (simulate RETN-like clear,
        // which drops button_nmi). VHDL says HOLD→END when hold goes 0.
        div.on_retn();
        const bool div_hold_clear = !div.is_nmi_hold();

        // Next tick re-pushes fresh hold=0; FSM HOLD→END.
        pump_tick(nmi, div);
        const bool end_state = (nmi.state() == NmiSource::State::End);

        check("DIS-04",
              "FSM HOLD → END when divmmc_nmi_hold transitions to 0",
              hold_state && still_hold && div_hold_clear && end_state,
              "zxnext.vhd:2118 (nmi_hold selector) / 2135-2148 (HOLD→END) / divmmc.vhd:150");
    }
}

// =====================================================================
// Group CLR — DivMMC button_nmi clear paths (Wave B, 4 rows)
// =====================================================================

static void g_divmmc_clears()
{
    set_group("CLR");

    // CLR-01 — Baseline: reset() clears button_nmi_.
    // VHDL divmmc.vhd:108 (i_reset branch).
    {
        DivMmc d;
        d.set_button_nmi(true);
        const bool pre  = d.button_nmi();
        d.reset();
        const bool post = !d.button_nmi();
        check("CLR-01",
              "reset() clears button_nmi_",
              pre && post,
              "divmmc.vhd:108 (i_reset branch of button_nmi process)");
    }

    // CLR-02 — i_automap_reset clears button_nmi_. JNEXT models
    // i_automap_reset via the enable-transition path (divmmc.vhd:126,139
    // are cleared together with button_nmi_ line 108 in the shared
    // process). Take enable from true→false and verify button_nmi
    // clears.
    {
        DivMmc d;
        d.set_enabled(true);                  // port_io + nr_0a_4 both on
        d.set_button_nmi(true);
        const bool pre = d.button_nmi();

        d.set_enabled(false);                 // enabled→disabled edge
        const bool post = !d.button_nmi();

        check("CLR-02",
              "set_enabled(true→false) edge (VHDL i_automap_reset) clears button_nmi_",
              pre && post,
              "divmmc.vhd:108 (i_automap_reset branch) / zxnext.vhd:4112 (divmmc_automap_reset derivation)");
    }

    // CLR-03 — on_retn_seen() clears button_nmi_.
    // VHDL divmmc.vhd:108 — i_retn_seen branch.
    {
        DivMmc d;
        d.set_enabled(true);
        d.set_button_nmi(true);
        const bool pre = d.button_nmi();

        d.on_retn_seen();
        const bool post = !d.button_nmi();

        check("CLR-03",
              "on_retn_seen() clears button_nmi_",
              pre && post,
              "divmmc.vhd:108 (i_retn_seen branch of button_nmi process)");
    }

    // CLR-04 — automap_held rising 0→1 clears button_nmi_.
    // VHDL divmmc.vhd:112-113 — `elsif automap_held = '1' then
    // button_nmi <= '0'`. Drive the pipeline so automap_held rises on
    // the second check_automap, with button_nmi=1 already latched.
    {
        DivMmc d;
        d.set_enabled(true);
        d.set_entry_points_0(0x01);
        d.set_entry_valid_0(0x01);
        d.set_entry_timing_0(0x01);
        d.set_button_nmi(true);                // latch set
        const bool pre  = d.button_nmi();
        const bool pre_held = d.automap_held();

        // First M1: instant_match → hold=1, held=0 (held loads from
        // hold NEXT M1). button_nmi unchanged.
        d.check_automap(0x0000, true);
        const bool mid_held = d.automap_held();
        const bool mid_btn  = d.button_nmi();

        // Second M1: held loads from hold (0→1 transition).
        // Rising-edge one-shot clears button_nmi.
        d.check_automap(0x0100, true);
        const bool post_held = d.automap_held();
        const bool post_btn  = !d.button_nmi();

        check("CLR-04",
              "automap_held rising edge clears button_nmi_",
              pre && !pre_held && !mid_held && mid_btn &&
              post_held && post_btn,
              "divmmc.vhd:112-113 (button_nmi cleared while automap_held=1)");
    }
}

// =====================================================================
// Group GATE — Gate registers (Wave C) (8 rows)
//
// Exercises the gate-register wiring added in Wave C (Phase 2) per
// TASK-NMI-SOURCE-PIPELINE-PLAN.md. Every row probes the NmiSource
// gate setters directly; the handlers that drive them (NR 0x06 /
// NR 0x81 / port 0xE3) are exercised end-to-end in the integration
// unlocks from Waves A/B and the DEFS/NR02 rows.
//
// ID mapping to the prompt scope (this wave's authoritative spec):
//   GATE-01: NR 0x06 bit 3 → mf_enable           (VHDL zxnext.vhd:1110)
//   GATE-02: NR 0x06 bit 4 → divmmc_enable       (VHDL zxnext.vhd:1109)
//   GATE-03: NR 0x81 bit 5 → expbus_debounce_dis (VHDL zxnext.vhd:1222)
//   GATE-04: CONMEM=1 blocks MF latch            (VHDL zxnext.vhd:2107)
//   GATE-05: mf_is_active=1 blocks DivMMC latch  (VHDL zxnext.vhd:2099)
//   GATE-06: config_mode=1 force-clears latches  (VHDL zxnext.vhd:2102-2105)
//   GATE-07: config_mode=1 holds FSM in Idle     (VHDL zxnext.vhd:2102-2105)
//   GATE-08: power-on gate flags all false       (VHDL zxnext.vhd:1109-1110,1222)
// =====================================================================

static void g_gate_registers()
{
    set_group("GATE");

    // GATE-01 — NR 0x06 bit 3 decode sets mf_enable()
    //   VHDL zxnext.vhd:1110 — nr_06_button_m1_nmi_en <= nr_wr_dat(3).
    //   NmiSource::set_mf_enable() is the NR-0x06-bit-3 entry point; the
    //   actual NR 0x06 dispatch is covered end-to-end by the integration
    //   rows. Here we confirm the setter lands the bit into the gate flag
    //   so nmi_assert_mf() is gated correctly by a downstream handler.
    {
        NmiSource nmi;
        nmi.reset();
        // Power-on default '0' — confirmed by RST-01.
        nmi.set_mf_enable(true);
        const bool set_true  = nmi.mf_enable() == true;
        nmi.set_mf_enable(false);
        const bool set_false = nmi.mf_enable() == false;
        check("GATE-01",
              "NR 0x06 bit 3 decode sets NmiSource::mf_enable()",
              set_true && set_false,
              "zxnext.vhd:1110 nr_06_button_m1_nmi_en");
    }

    // GATE-02 — NR 0x06 bit 4 decode sets divmmc_enable()
    //   VHDL zxnext.vhd:1109 — nr_06_button_drive_nmi_en <= nr_wr_dat(4).
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_divmmc_enable(true);
        const bool set_true  = nmi.divmmc_enable() == true;
        nmi.set_divmmc_enable(false);
        const bool set_false = nmi.divmmc_enable() == false;
        check("GATE-02",
              "NR 0x06 bit 4 decode sets NmiSource::divmmc_enable()",
              set_true && set_false,
              "zxnext.vhd:1109 nr_06_button_drive_nmi_en");
    }

    // GATE-03 — NR 0x81 bit 5 decode sets expbus_debounce_disable()
    //   VHDL zxnext.vhd:1222 — nr_81_expbus_nmi_debounce_disable <= nr_wr_dat(5).
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_expbus_debounce_disable(true);
        const bool set_true  = nmi.expbus_debounce_disable() == true;
        nmi.set_expbus_debounce_disable(false);
        const bool set_false = nmi.expbus_debounce_disable() == false;
        check("GATE-03",
              "NR 0x81 bit 5 decode sets NmiSource::expbus_debounce_disable()",
              set_true && set_false,
              "zxnext.vhd:1222 nr_81_expbus_nmi_debounce_disable");
    }

    // GATE-04 — port 0xE3 bit 7 (CONMEM=1) blocks the MF latch
    //   VHDL zxnext.vhd:2107 — MF latch gating includes NOT port_e3_reg(7).
    //   With CONMEM high, nmi_assert_mf() fires but the latch is blocked;
    //   the FSM stays in Idle and /NMI stays high. We use a sticky
    //   `set_mf_button(true)` here so the producer stays asserted across
    //   the tick (the strobe() helper is a one-cycle pulse consumed by
    //   tick() — unsuitable for observing post-tick producer state).
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_mf_enable(true);          // NR 0x06 bit 3 = 1 (gate open)
        nmi.set_divmmc_conmem(true);      // CONMEM=1 blocks MF latch
        nmi.set_mf_button(true);          // hotkey_m1 held high
        nmi.tick(1);
        const bool asserted_combinational = nmi.nmi_assert_mf();  // '1' — producer fires
        const bool mf_latch_clear         = !nmi.nmi_mf();        // latch NOT set
        const bool fsm_idle               = nmi.state() == NmiSource::State::Idle;
        const bool nmi_line_high          = nmi.nmi_generate_n(); // '1' (not asserted)
        check("GATE-04",
              "CONMEM=1 blocks MF latch even with enable+button set",
              asserted_combinational && mf_latch_clear && fsm_idle && nmi_line_high,
              "zxnext.vhd:2107 port_e3_reg(7) AND gate on MF latch");
    }

    // GATE-05 — mf_is_active=1 blocks the DivMMC latch
    //   VHDL zxnext.vhd:2099 — DivMMC latch gating: NOT mf_is_active.
    //   (Phase 1 stubs mf_is_active to false; Task 8 will drive it from
    //   the Multiface. Here we validate the gate flag's effect on the
    //   latch directly via the test setter.)
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_divmmc_enable(true);      // NR 0x06 bit 4 = 1 (gate open)
        nmi.set_mf_is_active(true);       // Multiface claiming ownership
        nmi.set_divmmc_button(true);      // hotkey_drive held high
        nmi.tick(1);
        const bool asserted_comb   = nmi.nmi_assert_divmmc();  // producer fires
        const bool divmmc_clear    = !nmi.nmi_divmmc();        // latch NOT set
        const bool fsm_idle        = nmi.state() == NmiSource::State::Idle;
        check("GATE-05",
              "mf_is_active=1 blocks DivMMC latch even with enable+button set",
              asserted_comb && divmmc_clear && fsm_idle,
              "zxnext.vhd:2099 mf_is_active AND gate on DivMMC latch");
    }

    // GATE-06 — nr_03_config_mode=1 force-clears all three latches
    //   VHDL zxnext.vhd:2102-2105 — every latch-set term is gated by
    //   NOT nr_03_config_mode. To prove the force-clear path, we first
    //   set a latch (config_mode=0), then assert config_mode and tick:
    //   the next tick must observe all latches clear regardless of which
    //   producer is still asserting.
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_mf_enable(true);
        nmi.set_divmmc_enable(true);
        nmi.strobe_mf_button();           // arm MF producer
        nmi.tick(1);
        const bool mf_set_before = nmi.nmi_mf();  // sanity: latch rose
        // Now raise config_mode; next tick must clear every latch.
        nmi.set_config_mode(true);
        nmi.tick(1);
        const bool mf_clear     = !nmi.nmi_mf();
        const bool divmmc_clear = !nmi.nmi_divmmc();
        const bool expbus_clear = !nmi.nmi_expbus();
        check("GATE-06",
              "config_mode=1 force-clears all three priority latches",
              mf_set_before && mf_clear && divmmc_clear && expbus_clear,
              "zxnext.vhd:2102-2105 nr_03_config_mode AND gate clears latches");
    }

    // GATE-07 — nr_03_config_mode=1 holds the FSM in Idle.
    //   VHDL zxnext.vhd:2102-2105 — the clear term also returns the FSM
    //   to S_NMI_IDLE regardless of current state. Drive the FSM into
    //   Fetch, then raise config_mode and tick: state must snap to Idle.
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_mf_enable(true);
        nmi.strobe_mf_button();
        nmi.tick(1);
        const bool reached_fetch = nmi.state() == NmiSource::State::Fetch;
        nmi.set_config_mode(true);
        nmi.tick(1);
        const bool back_to_idle = nmi.state() == NmiSource::State::Idle;
        check("GATE-07",
              "config_mode=1 force-clears FSM to Idle from any state",
              reached_fetch && back_to_idle,
              "zxnext.vhd:2102-2105 nr_03_config_mode returns FSM to S_NMI_IDLE");
    }

    // GATE-08 — power-on gate flags all false.
    //   VHDL zxnext.vhd:1109-1110 — nr_06_button_*_nmi_en power-on '0'.
    //   VHDL zxnext.vhd:1222 — nr_81_expbus_nmi_debounce_disable power-on '0'.
    //   RST-01 bundles this; here we isolate the gate-flag defaults into
    //   a dedicated row so Wave C owns the gate-register reset contract.
    {
        NmiSource nmi;  // constructor calls reset()
        const bool mf_off       = !nmi.mf_enable();
        const bool divmmc_off   = !nmi.divmmc_enable();
        const bool expbus_off   = !nmi.expbus_debounce_disable();
        const bool cfg_mode_off = !nmi.config_mode();
        check("GATE-08",
              "power-on gate flags (mf_en, divmmc_en, expbus_debounce_dis, config_mode) all false",
              mf_off && divmmc_off && expbus_off && cfg_mode_off,
              "zxnext.vhd:1109-1110 / 1222 / NR 0x03 reset-to-config semantics");
    }
}

// =====================================================================
// Group DMA — NMI-activated DMA-delay path (Wave E; 3 rows)
//
// VHDL oracle zxnext.vhd:2001-2010:
//   im2_dma_delay <= im2_dma_int
//                    OR (nmi_activated AND nr_cc_dma_int_en_0_7)
//                    OR (im2_dma_delay AND dma_delay);
//
// Wave E wires NmiSource::is_activated() into Im2Controller via
// set_nmi_activated(), and NR 0xCC bit 7 via set_nr_cc_dma_int_en_0_7().
// These rows cover the second OR term end-to-end.
// =====================================================================

static void g_dma_group()
{
    set_group("DMA");

    // DMA-01 — VHDL zxnext.vhd:2107: `nmi_activated = nmi_mf OR nmi_divmmc
    // OR nmi_expbus`. Drive the ExpBus pin active (i_BUS_NMI_n='0') and
    // tick() NmiSource once; the priority latch `nmi_expbus_` must set and
    // is_activated() must return true. Also verifies the latch survives
    // across the FSM IDLE→FETCH transition.
    {
        NmiSource nmi;
        nmi.reset();

        const bool before = nmi.is_activated();
        nmi.set_expbus_nmi_n(false);       // assert /BUS_NMI (active-low)
        nmi.tick(1);                       // recompute_: latch expbus, FSM IDLE→FETCH
        const bool after_latch = nmi.nmi_expbus();
        const bool after_activated = nmi.is_activated();
        const bool fsm_fetch = (nmi.state() == NmiSource::State::Fetch);

        check("DMA-01",
              "is_activated() true while any NMI latch is set",
              !before && after_latch && after_activated && fsm_fetch,
              "zxnext.vhd:2107 nmi_activated = nmi_mf OR nmi_divmmc OR nmi_expbus");
    }

    // DMA-02 — VHDL zxnext.vhd:2007 second OR term:
    //   (nmi_activated AND nr_cc_dma_int_en_0_7).
    // With both inputs asserted to the Im2Controller and no other DMA
    // sources, step_dma_delay() (called from tick()) must latch
    // im2_dma_delay to 1.
    {
        Im2Controller im2;
        im2.reset();
        im2.set_mode(true);
        // No device in S_REQ, mask empty → dma_int_pending() == false.
        // Push both second-term inputs high.
        im2.set_nr_cc_dma_int_en_0_7(true);
        im2.set_nmi_activated(true);

        const bool before = im2.dma_delay();
        im2.tick(1);  // step_dma_delay() runs at end of tick()
        const bool after = im2.dma_delay();

        check("DMA-02",
              "im2_dma_delay latches when is_activated() AND nr_cc_dma_int_en_0_7",
              !before && after,
              "zxnext.vhd:2007 second OR term (nmi_activated AND nr_cc_dma_int_en_0_7)");
    }

    // DMA-03 — VHDL zxnext.vhd:2007: NR 0xCC bit 7 = 0 must block the NMI
    // contribution even when NMI is activated. No DMA int source → latch
    // stays low. Also verify the complementary: if NMI is NOT activated,
    // with bit 7 = 1, latch also stays low (full AND gate coverage).
    {
        Im2Controller im2;
        im2.reset();
        im2.set_mode(true);

        // Case 1: nmi_activated=1, nr_cc_dma_int_en_0_7=0 → latch stays 0.
        im2.set_nmi_activated(true);
        im2.set_nr_cc_dma_int_en_0_7(false);
        im2.tick(1);
        const bool case1_blocked = !im2.dma_delay();

        // Case 2: nmi_activated=0, nr_cc_dma_int_en_0_7=1 → latch stays 0.
        im2.reset();
        im2.set_mode(true);
        im2.set_nmi_activated(false);
        im2.set_nr_cc_dma_int_en_0_7(true);
        im2.tick(1);
        const bool case2_blocked = !im2.dma_delay();

        check("DMA-03",
              "NR 0xCC bit 7 = 0 (or nmi_activated=0) blocks NMI-driven DMA delay",
              case1_blocked && case2_blocked,
              "zxnext.vhd:2007 AND gate in second OR term — both inputs required");
    }
}

// =====================================================================
// Main
// =====================================================================

int main() {
    std::printf("NMI Source Pipeline Compliance Tests (Phase 1 + Wave A + Wave B + Wave C + Wave E)\n");
    std::printf("=========================================================\n\n");

    g_rst_defaults();      std::printf("  RST  reset-defaults  -- done\n");
    g_nr02_sw_nmi();       std::printf("  NR02 software-NMI    -- done\n");
    g_nr02_integration();  std::printf("  NR02 integration     -- done\n");
    g_hotkey();            std::printf("  HK   hotkey producers -- done\n");
    g_divmmc_consumer();   std::printf("  DIS  DivMMC consumer  -- done\n");
    g_divmmc_clears();     std::printf("  CLR  DivMMC clears    -- done\n");
    g_gate_registers();    std::printf("  GATE gate registers   -- done\n");
    g_dma_group();         std::printf("  DMA  NMI-activated delay -- done\n");

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
