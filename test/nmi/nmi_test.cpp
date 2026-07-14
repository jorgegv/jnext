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
#include "peripheral/multiface.h"
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

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
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

    // RST-04 — VHDL zxnext.vhd:1306, 5891 — NR 0x02 reset_type[2:0] FSM
    // power-on default = "100"; bits 1:0 of read = "00", bit 2 latent.
    // (G153 closure: NmiSource::reset_type_ = 0b100 init-value, surfaced
    //  via nr_02_read() bits 1:0.)
    {
        NmiSource nmi;
        const uint8_t rt    = nmi.reset_type();
        const uint8_t r02   = nmi.nr_02_read();
        const bool   ok_rt  = (rt == 0b100);
        const bool   ok_r02 = ((r02 & 0x03) == 0x00);
        check("RST-04",
              "NR 0x02 reset_type[2:0] power-on default = \"100\" (read bits 1:0 = \"00\")",
              ok_rt && ok_r02,
              "zxnext.vhd:1306 (init), 5891 (readback)");
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
    // NR02-05 — Readback bits 3/2 are NOT auto-cleared by the FSM.
    // They follow the VHDL clear path at zxnext.vhd:3847-3848,3860-3861:
    // a write to NR 0x02 with the corresponding bit explicitly low
    // clears the readback latch. Walking the FSM through S_NMI_END
    // (which DOES clear the priority latches per VHDL:2102-2105) leaves
    // the readback bits untouched — they survive until either reset or
    // an explicit write-back with the bit cleared.
    // ------------------------------------------------------------------
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);
        nmi.nr_02_write(0x08);
        nmi.tick(1);   // Idle → Fetch (is_activated == true)
        // Pre-END sanity: readback shows bit 3 set while the FSM holds
        // the request.
        const uint8_t pre = nmi.nr_02_read();

        // Fetch → Hold on M1 fetch at 0x0066 (VHDL:2135-2138).
        nmi.observe_m1_fetch(0x0066, /*m1=*/true, /*mreq=*/true);
        nmi.tick(1);

        // Hold → End: mf_nmi_hold defaults false, so the FSM reaches END,
        // which clears the priority latch (`nmi_mf`) but NOT the readback
        // bit (`nr_02_generate_mf_nmi`).
        nmi.tick(1);
        const uint8_t after_end = nmi.nr_02_read();

        // Now write NR 0x02 with bit 3 = 0 — the VHDL clear path.
        nmi.nr_02_write(0x00);
        const uint8_t after_clear_write = nmi.nr_02_read();

        check("NR02-05",
              "NR 0x02 readback bit 3 survives FSM END; clears on a "
              "subsequent NR 0x02 write with bit 3 = 0 "
              "[zxnext.vhd:3847-3848, 3860-3861]",
              (pre & 0x08) != 0
                  && (after_end & 0x08) != 0
                  && (after_clear_write & 0x0C) == 0,
              "pre=" + std::to_string(pre)
                  + " after_end=" + std::to_string(after_end)
                  + " after_clear=" + std::to_string(after_clear_write));
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

    // NR02-07 — VHDL zxnext.vhd:1732-1739
    // Soft-reset rising edge advances reset_type via
    //   '0' & rt(2) & (rt(1) OR rt(0))
    // (G153 closure: NmiSource::strobe_soft_reset()).
    //
    // Trace from power-on:
    //   100 -> 010 -> 001 -> 001 ... (saturates at 001 because once b0=1
    //   the new b0 = b1 OR b0 stays 1 forever; b2 has already shifted out).
    {
        NmiSource nmi;
        const uint8_t s0 = nmi.reset_type();
        nmi.strobe_soft_reset();
        const uint8_t s1 = nmi.reset_type();
        nmi.strobe_soft_reset();
        const uint8_t s2 = nmi.reset_type();
        nmi.strobe_soft_reset();
        const uint8_t s3 = nmi.reset_type();
        nmi.strobe_soft_reset();
        const uint8_t s4 = nmi.reset_type();
        const bool ok = s0 == 0b100 && s1 == 0b010 && s2 == 0b001
                        && s3 == 0b001 && s4 == 0b001;
        check("NR02-07",
              "soft-reset edge advances reset_type FSM "
              "(100 -> 010 -> 001, saturates at 001)",
              ok,
              "zxnext.vhd:1732-1739; trace=" +
                  std::to_string(s0) + "," + std::to_string(s1) + "," +
                  std::to_string(s2) + "," + std::to_string(s3) + "," +
                  std::to_string(s4));
    }

    // NR02-08 — VHDL zxnext.vhd:5891
    // NR 0x02 readback bits 1:0 reflect reset_type[1:0]; independent of
    // bits 3/2 (FSM auto-clear of bits 3/2 does not touch the
    // reset_type FSM). (G153 closure.)
    {
        NmiSource nmi;
        // power-on: rt=100 -> bits 1:0 = "00"
        const uint8_t r_pwr = nmi.nr_02_read() & 0x03;
        nmi.strobe_soft_reset();
        // rt=010 -> bits 1:0 = "10"
        const uint8_t r_aft1 = nmi.nr_02_read() & 0x03;
        nmi.strobe_soft_reset();
        // rt=001 -> bits 1:0 = "01"
        const uint8_t r_aft2 = nmi.nr_02_read() & 0x03;
        // Now write bit 3 to set MF pending and verify bits 3/2 don't
        // disturb reset_type bits 1:0.
        nmi.set_mf_enable(true);
        nmi.nr_02_write(0x08);
        const uint8_t r_with_mf = nmi.nr_02_read();
        const bool ok = r_pwr == 0b00 && r_aft1 == 0b10 && r_aft2 == 0b01
                        && (r_with_mf & 0x03) == 0b01
                        && (r_with_mf & 0x08) != 0;
        check("NR02-08",
              "NR 0x02 readback bits 1:0 reflect reset_type[1:0] independent of bits 3/2",
              ok,
              "zxnext.vhd:5891");
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

    // HK-06..HK-09 — Host F-key dispatch (G152 closure).
    // VHDL zxnext.vhd:6340-6349, 6370-6371, 2089-2091. The Emulator
    // exposes the four host-side hotkey dispatchers
    // `on_hotkey_f1_hard_reset / f4_soft_reset / f9_mf_nmi / f10_divmmc_nmi`
    // (src/core/emulator.h, .cpp:Emulator::on_hotkey_f*) which the GUI
    // (gui/main_window.cpp keyPressEvent) wires to Qt::Key_F1/F4/F9/F10.
    // The unit-test exercises the Emulator-level seam directly; the
    // GUI plumbing is verified at integration / manual test time.
    {
        // HK-06 — F9 -> hotkey_m1 -> NmiSource MF producer.
        Emulator emu;
        build_next_emulator(emu);
        emu.nmi_source().set_mf_enable(true);     // NR 0x06 bit 3 = 1
        emu.on_hotkey_f9_mf_nmi();
        emu.nmi_source().tick(1);
        check("HK-06",
              "F9 dispatcher strobes NmiSource MF button -> nmi_mf latch",
              emu.nmi_source().nmi_mf()
                  && emu.nmi_source().latched() == NmiSource::Src::Mf,
              "zxnext.vhd:6348 (hotkey_m1) -> 2090 (nmi_assert_mf) -> 2097 (latch)");
    }
    {
        // HK-07 — F10 -> hotkey_drive -> NmiSource DivMMC producer.
        // VHDL gates F10 on port_divmmc_io_en (NR 0x83 bit 0). Enable
        // both gates and verify the strobe; companion HK-07b verifies
        // the gate-off path is a no-op.
        Emulator emu;
        build_next_emulator(emu);
        emu.nmi_source().set_divmmc_enable(true); // NR 0x06 bit 4 = 1
        emu.divmmc().set_port_io_enable(true);    // NR 0x83 bit 0 = 1
        emu.on_hotkey_f10_divmmc_nmi();
        emu.nmi_source().tick(1);
        check("HK-07",
              "F10 dispatcher strobes NmiSource DivMMC button -> nmi_divmmc latch",
              emu.nmi_source().nmi_divmmc()
                  && emu.nmi_source().latched() == NmiSource::Src::DivMmc,
              "zxnext.vhd:6349 (hotkey_drive) -> 2091 -> 2099 (latch)");
    }
    {
        // HK-07b — F10 with port_divmmc_io_en=0 must NOT strobe.
        Emulator emu;
        build_next_emulator(emu);
        emu.nmi_source().set_divmmc_enable(true);
        // Pass-6 verify-audit fix (Task 2 verify6-divmmc-sd-spi): post-reset
        // VHDL default is `port_divmmc_io_en=1` (NR 0x83 bit 0 = 1, regs_[0x83]
        // reloads to 0xFF when reset_type_1=true per zxnext.vhd:5052-5057), so
        // we must EXPLICITLY clear it to exercise the gate-off path. Pre-fix
        // the test relied on a stale "default-false" invariant that diverged
        // from the VHDL — corrected here as part of the same pass-6 sweep
        // that introduced the post-reset DivMmc/Multiface NR 0x83 sync.
        emu.divmmc().set_port_io_enable(false);
        emu.on_hotkey_f10_divmmc_nmi();
        emu.nmi_source().tick(1);
        check("HK-07b",
              "F10 honours port_divmmc_io_en gate (no strobe when NR 0x83 bit 0 = 0)",
              !emu.nmi_source().nmi_divmmc(),
              "zxnext.vhd:6349 (port_divmmc_io_en AND ...)");
    }
    {
        // HK-08 — F4 -> hotkey_soft_reset -> reset_type FSM advance.
        // VHDL zxnext.vhd:6370 gates on NOT nr_03_config_mode (NR 0x03
        // power-on default '1' per VHDL:1102). We exercise both
        // halves of the gate: gate-OPEN (config_mode=0) advances the
        // FSM; gate-CLOSED (config_mode=1) is a no-op.
        Emulator emu;
        build_next_emulator(emu);
        const uint8_t rt0 = emu.nmi_source().reset_type();   // 100

        // Default state: nr_03_config_mode='1' → F4 must NOT advance.
        emu.on_hotkey_f4_soft_reset();
        const uint8_t rt_gated = emu.nmi_source().reset_type(); // expect 100

        // Clear config_mode by writing NR 0x03 with bits 2:0 ∈ {001..110}
        // (VHDL:5147-5151). Use the canonical 'exit-config' value 0x00
        // — the low 3 bits "000" is documented as no-change in VHDL but
        // jnext's apply_nr_03_config_mode_transition() drops config_mode
        // for any non-"111" write per zxnext.vhd:5137 (the machine-type
        // commit edge). To stay strictly VHDL-faithful pick low 3 bits
        // "010" (keep machine_type 010 = ZX Next).
        emu.port().out(0x243B, 0x03);
        emu.port().out(0x253B, 0x02);

        // Gate open — F4 should advance.
        emu.on_hotkey_f4_soft_reset();
        const uint8_t rt1 = emu.nmi_source().reset_type();    // expect 010

        const bool ok = rt0 == 0b100 && rt_gated == 0b100 && rt1 == 0b010;
        check("HK-08",
              "F4 dispatcher advances reset_type FSM and honours config_mode gate",
              ok,
              std::string("zxnext.vhd:6370; rt0=") + std::to_string(rt0)
                  + " rt_gated=" + std::to_string(rt_gated)
                  + " rt1=" + std::to_string(rt1));
    }
    {
        // HK-09 — F1 -> hotkey_hard_reset -> full Emulator reset.
        // VHDL zxnext.vhd:6371 has no config_mode gate. Verify by
        // dirtying CPU state then checking it returns to power-on PC=0.
        Emulator emu;
        build_next_emulator(emu);
        // Dirty something observable: write NR 0x06 bit 3 (MF-enable)
        // and verify the value is mirrored, then hard-reset and confirm
        // it's wiped (NextReg::reset() clears the register file).
        emu.nmi_source().set_mf_enable(true);
        const bool pre = emu.nmi_source().mf_enable();
        emu.on_hotkey_f1_hard_reset();
        const bool post = emu.nmi_source().mf_enable();
        // After hard reset NmiSource gates default to false (VHDL:1109-
        // 1110 power-on '0'); reset_type FSM is preserved (VHDL has no
        // reset branch for nr_02_reset_type).
        const bool ok = pre && !post
                        && emu.nmi_source().state() == NmiSource::State::Idle;
        check("HK-09",
              "F1 dispatcher triggers hard reset (mf_enable cleared, FSM idle)",
              ok,
              "zxnext.vhd:6371 (hotkey_hard_reset)");
    }
}

// =====================================================================
// Group Z80 — NMIACK PC capture cross-links (G88)
//
// VHDL zxnext.vhd:2050-2085 latches `nr_c2_retn_address_lsb` and
// `nr_c3_retn_address_msb` on `Z80N_command_s = NMIACK_LSB / NMIACK_MSB
// AND cpu_wr_n='0'`. Read at zxnext.vhd:6232-6236.
//
// RE-HOME (2026-04-28, Wave 2): Z80-04 was a duplicate cross-link to the
// CTC interrupts plan (primary owner). The CTC suite already covers G88
// via rows NR-C2-01 / NR-C3-01 in test/ctc_interrupts/ctc_interrupts_test.cpp
// — see doc/testing/CTC-INTERRUPTS-TEST-PLAN-DESIGN.md. Skip removed
// here; closure of G88 is tracked in the CTC plan.
// =====================================================================

static void g_nmiack_pc_capture()
{
    // Intentionally empty — Z80-04 RE-HOMED to CTC plan (NR-C2-01/NR-C3-01).
    // Keep this hook so the test layout remains stable for future cross-link
    // rows that genuinely belong here (e.g. NMI-side observable behaviour
    // distinct from the NextReg shadow capture).
}

// =====================================================================
// Group MF (G162-parked rows; future migration to MULTIFACE-TEST-PLAN-DESIGN.md
// when it's authored)
// =====================================================================

static void g_mf_g162_skips()
{
    set_group("MF");

    // MF-G162-01 — VHDL zxnext.vhd:3835-3837
    //   nmi_sw_gen_mf <= nmi_gen_nr_mf or nmi_gen_iotrap;
    //   nmi_assert_mf <= '1' when (hotkey_m1 OR nmi_sw_gen_mf) AND
    //                              nr_06_button_m1_nmi_en;
    // (G162 closure: NmiSource::strobe_iotrap() is now OR'd into the
    //  nmi_assert_mf path with the NR 0x06 bit 3 gate honoured.)
    //
    // The VHDL upstream gates the iotrap by `nr_d8_io_trap_fdc_en`, so
    // by contract `strobe_iotrap()` is only invoked when the FDC trap
    // enable register is on. We model that contract here by simply
    // calling the strobe; the NR-D8 gate itself is owned by the future
    // port 0x2FFD/0x3FFD trap-decode handler (MF-G162-02).
    {
        NmiSource nmi;
        nmi.set_mf_enable(true);                 // NR 0x06 bit 3 = 1
        nmi.strobe_iotrap();
        nmi.tick(1);
        const bool latched = (nmi.latched() == NmiSource::Src::Mf);
        const bool fsm_ok  = (nmi.state()  == NmiSource::State::Fetch);
        check("MF-G162-01",
              "strobe_iotrap() OR's into nmi_assert_mf and latches MF",
              latched && fsm_ok,
              "zxnext.vhd:3835-3837 (iotrap->MF), 2090 (mf assert), 2097 (latch)");
    }
    {
        // Companion: NR 0x06 bit 3 = 0 still gates the iotrap path off.
        NmiSource nmi;
        nmi.set_mf_enable(false);                // gate closed
        nmi.strobe_iotrap();
        nmi.tick(1);
        check("MF-G162-01b",
              "iotrap honours NR 0x06 bit 3 gate (no latch when MF-en off)",
              nmi.latched() == NmiSource::Src::None
                  && nmi.state() == NmiSource::State::Idle,
              "zxnext.vhd:2090 gate (`nr_06_button_m1_nmi_en`)");
    }

    // MF-G162-02 — VHDL zxnext.vhd:3835-3837, 2598-2602.
    //   nmi_gen_iotrap <= port_2ffd_rd OR port_3ffd_rd OR port_3ffd_wr
    //   port_xffd  <= A15:14="00" AND A1:0="01"
    //   port_2ffd  <= cpu_a(13:12)="10" AND port_xffd AND nr_d8_io_trap_fdc_en
    //   port_3ffd  <= cpu_a(13:12)="11" AND port_xffd AND nr_d8_io_trap_fdc_en
    // (G162 closure: port handlers in Emulator::init() invoke
    //  NmiSource::strobe_iotrap() when NR 0xD8 bit 0 is on; the strobe
    //  OR's into nmi_assert_mf and latches MF when NR 0x06 bit 3 is on.)
    {
        Emulator emu;
        build_next_emulator(emu);
        emu.nmi_source().set_mf_enable(true);  // NR 0x06 bit 3 = 1

        // Enable NR 0xD8 bit 0 (nr_d8_io_trap_fdc_en).
        emu.port().out(0x243B, 0xD8);
        emu.port().out(0x253B, 0x01);

        // Trigger via OUT 0x3FFD (port_3ffd_wr term in iotrap).
        emu.port().out(0x3FFD, 0x55);
        emu.nmi_source().tick(1);
        const bool latched_w = emu.nmi_source().nmi_mf();

        // Reset and try IN 0x2FFD (port_2ffd_rd term in iotrap).
        emu.reset();
        emu.nmi_source().set_mf_enable(true);
        emu.port().out(0x243B, 0xD8);
        emu.port().out(0x253B, 0x01);
        (void)emu.port().in(0x2FFD);
        emu.nmi_source().tick(1);
        const bool latched_r = emu.nmi_source().nmi_mf();

        // With NR 0xD8 bit 0 = 0, the port should NOT trap.
        emu.reset();
        emu.nmi_source().set_mf_enable(true);
        // (NR 0xD8 default is 0 after reset.)
        emu.port().out(0x3FFD, 0xAA);
        emu.nmi_source().tick(1);
        const bool no_trap = !emu.nmi_source().nmi_mf();

        check("MF-G162-02",
              "port 0x2FFD READ + 0x3FFD WRITE strobe iotrap when NR 0xD8 bit 0 = 1, gated off when bit 0 = 0",
              latched_w && latched_r && no_trap,
              std::string("zxnext.vhd:2598-2602,3835-3837; w=") +
                  std::to_string((int)latched_w) + " r=" + std::to_string((int)latched_r) +
                  " gated=" + std::to_string((int)no_trap));
    }

    // MF-G48-01..07 — Multiface peripheral expansion (G48). Wave 1 B1
    // (TASK-8-MULTIFACE-PLAN.md, 2026-05-04) landed the core class; Wave
    // 1 B2 (2026-05-04) wires the per-mode port-LSB dispatch through
    // PortDispatch::add_io_observer (src/core/emulator.cpp). This row
    // closes MF-G48-01: end-to-end port dispatch via the real Emulator,
    // exercising MF1 mode (mf_type=11) where button + OUT 0x9F should
    // (a) leave nmi_active high (button armed it; the OUT is enable_wr
    //     which clears nmi_active, but only when port_io_dly was 0
    //     entering the OUT — so we re-arm AFTER the prep cycle quiesces
    //     port_io_dly), and
    // (b) port_io_dly should latch high after the OUT itself.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Configure MF for MF1 mode and enable it via the real NextReg
        // port path. NR 0x0A b7:6 = 11 (mf_type=11 → mode_48 = MF1);
        // NR 0x83 b1 = 1 (port_multiface_io_en).
        emu.port().out(0x243B, 0x0A);
        emu.port().out(0x253B, 0xC0);  // b7:6 = 11
        emu.port().out(0x243B, 0x83);
        emu.port().out(0x253B, 0xFF);  // b1=1 (and all other bits 1, VHDL reset value)

        const bool mode_ok = emu.multiface().mode_48()
                            && emu.multiface().is_enabled()
                            && emu.multiface().mf_type() == 0x03;

        // Quiesce port_io_dly. The two NR writes above set it high;
        // toggling enable off/on holds the FFs in reset (port_io_dly := 0).
        emu.multiface().set_enabled(false);
        emu.multiface().set_enabled(true);

        // Arm the NMI source via button (button_pulse fires because
        // nmi_active is currently 0).
        emu.multiface().button_press();
        const bool armed_post_button = emu.multiface().is_nmi_hold();

        // Now drive an OUT to MF1's enable_io (0x9F). VHDL line 144 third
        // disjunct doesn't apply to enable_wr (that's first disjunct); the
        // first disjunct fires when port_io_dly was 0 (just quiesced) AND
        // the strobe is enable_wr. So nmi_active should clear; port_io_dly
        // should latch to 1.
        emu.port().out(0x009F, 0x00);
        const bool port_strobed = emu.multiface().port_io_dly();
        const bool nmi_cleared  = !emu.multiface().is_nmi_hold();

        check("MF-G48-01",
              "MF1 mode: NR 0x0A=11/NR 0x83 b1=1 → button arms NMI; "
              "OUT 0x9F clears nmi_active and latches port_io_dly",
              mode_ok && armed_post_button && port_strobed && nmi_cleared,
              "VHDL zxnext.vhd:2612-2616 (decode), :2730-2733 (iord/iowr fan-out), "
              "multiface.vhd:122-131 (port_io_dly), :137-148 (nmi_active clear)");
    }

    // MF-G48-02 — NR 0x0A b7:6 mf_type forwarding. VHDL multiface.vhd:
    //   105-118 — combinational decode:
    //     "00" -> mode_p3 (= MF +3)
    //     "11" -> mode_48 (= MF1)
    //     others ("01"/"10") -> mode_128
    // Wave 1 B1 lands set_mode(mf_type) on the Multiface class; verify
    // both extreme codes plus one of the "others" codes resolve to the
    // right mode flag. Discriminative across all three branches.
    {
        Multiface mf;
        // "00" → MF+3
        mf.set_mode(0x00);
        const bool p3   = mf.mode_p3()  && !mf.mode_128() && !mf.mode_48();
        // "11" → MF1
        mf.set_mode(0x03);
        const bool m1   = !mf.mode_p3() && !mf.mode_128() && mf.mode_48();
        // "10" → MF128 (one of the "others" branches)
        mf.set_mode(0x02);
        const bool m128 = !mf.mode_p3() && mf.mode_128() && !mf.mode_48();
        check("MF-G48-02",
              "NR 0x0A b7:6 = 00 -> MF+3, 11 -> MF1, others -> MF128",
              p3 && m1 && m128,
              "multiface.vhd:105-118 mf_mode_i decode");
    }

    // MF-G48-03 — port_io_dly rising-edge detector (multiface.vhd:122-131).
    // VHDL latches `port_io_dly <= port_mf_enable_rd OR port_mf_enable_wr
    //                                OR port_mf_disable_rd OR port_mf_disable_wr`
    // on every rising clock edge. The downstream nmi_active clear at line
    // 144 requires `port_io_dly = '0'` — i.e. the PRIOR cycle had no
    // port-IO strobe. So a back-to-back port_mf_enable_wr (two consecutive
    // cycles) only clears nmi_active on the FIRST cycle; the second is
    // suppressed because port_io_dly latched the first cycle's pulse.
    //
    // Discriminative test: arm nmi_active=1 via button_press, then in
    // mode_128 fire two consecutive port_mf_enable_wr strobes. After the
    // first, nmi_active should be 0 and port_io_dly should be 1; after
    // the second, port_io_dly stays 1 (because the second cycle re-OR'd
    // it) and nmi_active stays 0 (already cleared). Re-arm nmi_active
    // and fire ONE port_mf_enable_wr after a quiescent cycle: this time
    // the prior port_io_dly was 0 so the clear takes effect. The
    // single-cycle "two pulses" test isolates the dly gating from the
    // clear semantics.
    {
        Multiface mf;
        mf.set_enabled(true);
        mf.set_mode(0x02);  // MF128 (mode_p3=0)
        mf.button_press();
        const bool armed_first = mf.nmi_active() && !mf.port_io_dly();
        mf.on_port_enable_wr(true);
        // After 1 strobe: nmi_active cleared (port_io_dly was 0 entering
        // this cycle), and port_io_dly latches to 1 for the next cycle.
        const bool first_clears = !mf.nmi_active() && mf.port_io_dly();
        // Re-arm via button.
        mf.button_press();
        const bool armed_second = mf.nmi_active();
        // Now do a port_mf_enable_wr while port_io_dly=0 (button_press
        // did not assert any port input, so port_io_dly latched 0 on
        // that cycle). This single strobe should clear nmi_active.
        mf.on_port_enable_wr(true);
        const bool single_clears = !mf.nmi_active();
        check("MF-G48-03",
              "port_io_dly edge detector gates nmi_active clear on prior-cycle quiescence",
              armed_first && first_clears && armed_second && single_clears,
              "multiface.vhd:122-131 (FF), :144 (port_io_dly=0 gate)");
    }

    // MF-G48-04 — INVISIBLE FF (multiface.vhd:152-163).
    // Reset value is '1' (line 156). button_pulse clears it (line 158).
    // In mode_128 (mode_p3=0), port_mf_disable_wr with port_io_dly=0
    // sets it to '1' (line 159 first disjunct). In mode_p3=1,
    // port_mf_enable_wr with port_io_dly=0 sets it to '1' (line 159
    // second disjunct).
    {
        Multiface mf;
        mf.set_enabled(true);
        mf.set_mode(0x02);  // MF128
        // After enable+reset, invisible should be 1 (line 156).
        const bool reset_one = mf.invisible();
        // Press button: invisible -> 0 (line 158).
        mf.button_press();
        const bool btn_clears = !mf.invisible();
        // Now port_mf_disable_wr (mode_p3=0 path): invisible -> 1.
        mf.on_port_disable_wr(true);
        const bool dis_wr_sets_128 = mf.invisible();
        // Switch to MF+3 mode and re-arm: re-press the button to drop
        // the invisible we just set, then the port_mf_enable_wr path
        // (mode_p3=1) should set it back.
        mf.set_mode(0x00);  // MF+3
        // First press would no-op since nmi_active is still set from
        // the original button_press above and not yet cleared in MF+3
        // (the mode_128 disable_wr above did clear it via line 144 first
        // disjunct since mode_p3=0 then). Verify quiescent state, then
        // re-arm.
        mf.button_press();
        // Now invisible should drop back to 0 again (line 158).
        const bool btn_clears_again = !mf.invisible();
        mf.on_port_enable_wr(true);
        const bool en_wr_sets_p3 = mf.invisible();
        check("MF-G48-04",
              "INVISIBLE FF: reset=1, button=0, mode_128 disable_wr=1, mode_p3 enable_wr=1",
              reset_one && btn_clears && dis_wr_sets_128 &&
                  btn_clears_again && en_wr_sets_p3,
              "multiface.vhd:152-163 (process), :156 reset, :158 button, :159 set");
    }

    // MF-G48-05 — MF +3 port 0x1FFD / 0x7FFD readback mux on cpu_a(15:12).
    // VHDL zxnext.vhd:4310-4327 — when mf_port_en='1' AND nr_0a_mf_type="00",
    // reading port `xx3F` returns:
    //   cpu_a(15:12)="0001" → port_1ffd reconstructed (motor + reg(2:0))
    //   cpu_a(15:12)="0111" → port_7ffd_reg (full 8-bit)
    // Wave 1 B3 (2026-05-04) wires the read handler in src/core/emulator.cpp
    // gating on Multiface::is_enabled() && mf_type==0 && !invisible_eff().
    // Discriminative integration: same low byte 0x3F, different high
    // nibbles → DIFFERENT bytes (port_1ffd vs port_7ffd) when both
    // registers are loaded with distinguishable values.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Configure MF for MF+3 mode and enable via the real NextReg path.
        // NR 0x0A b7:6 = 00 (mf_type=00 → mode_p3); NR 0x83 b1 = 1 (enable).
        emu.port().out(0x243B, 0x0A);
        emu.port().out(0x253B, 0x00);  // b7:6 = 00
        emu.port().out(0x243B, 0x83);
        emu.port().out(0x253B, 0xFF);  // b1=1 (and other bits per VHDL reset)

        // Clear invisible (button_press flips invisible 1→0 per
        // multiface.vhd:158 since nmi_active was 0 entering it).
        emu.multiface().button_press();

        // Load 0x1FFD with 0x07 (motor=0, bank-bits 7) and 0x7FFD with
        // 0xC2 (a high-bit-set sentinel that 0x1FFD's narrow path
        // cannot reproduce, so a wrong cpu_a(15:12) dispatch shows up).
        emu.port().out(0x1FFD, 0x07);
        emu.port().out(0x7FFD, 0xC2);

        // Z80 IN at port 0x_3F with cpu_a(15:12) = 0x1 / 0x7.
        const uint8_t got_1 = emu.port().in(0x113F);  // port_1ffd readback
        const uint8_t got_7 = emu.port().in(0x713F);  // port_7ffd readback

        // Expected: 0x07 = port_1ffd & 0x0F (verbatim cpu_do bits 3:0);
        //           0xC2 = port_7ffd_reg (full 8-bit).
        check("MF-G48-05",
              "MF +3 readback mux: cpu_a(15:12)=0x1 → port_1ffd, 0x7 → port_7ffd",
              got_1 == 0x07 && got_7 == 0xC2,
              "VHDL zxnext.vhd:4312-4313 (case 0001 / 0111), :2816 (mf_port_en gate)");
    }

    // MF-G48-06 — Wave 1 F-gate: DivMMC retn_seen AND-NOT mf_is_active.
    // VHDL zxnext.vhd:4111:
    //   `divmmc_retn_seen <= z80_retn_seen_28 AND NOT mf_is_active;`
    // mf_is_active = mf_mem_en OR mf_nmi_hold (zxnext.vhd:2099).
    //
    // When MF is active, the RETN pulse is suppressed for DivMMC's
    // automap_held delay-clear path; DivMMC state is preserved until
    // MF deactivates. Discriminative: same RETN pulse sequence with MF
    // INACTIVE clears automap_held; with MF ACTIVE preserves it. Both
    // arms are exercised here at the same wiring point the Emulator
    // uses (`divmmc_.on_m1_retn_delay(retn_pulse && !mf.is_active())`
    // — see emulator.cpp on_m1_cycle lambda).
    {
        // Helper to bring a fresh DivMmc into automap_held=1 state.
        // Mirrors the DIS-02 setup pattern.
        auto make_held_divmmc = []() -> DivMmc {
            DivMmc d;
            d.set_enabled(true);
            d.set_nr_0a_4_enable(true);
            d.set_entry_points_0(0x01);
            d.set_entry_valid_0(0x01);
            d.set_entry_timing_0(0x01);
            d.check_automap(0x0000, true);  // instant_match → hold=1
            d.check_automap(0x0100, true);  // step: held <- hold (=1)
            return d;
        };

        // Arm A: MF INACTIVE, RETN seen → DivMmc automap_held clears.
        // The clear is delayed one M1 (post-G87 register-shift shape).
        DivMmc d_inactive = make_held_divmmc();
        Multiface mf_inactive;  // default state: mf_enable=0, nmi_active=0
        const bool mf_a_inactive = !mf_inactive.is_active();
        const bool d_a_held_pre  = d_inactive.automap_held();
        // Drive the gate exactly as emulator.cpp does at on_m1_cycle.
        const bool retn_pulse_a = true;
        d_inactive.on_m1_retn_delay(retn_pulse_a && !mf_inactive.is_active());
        // The shift register applies the clear on the NEXT M1; pulse a
        // post-RETN M1 (no further retn this cycle).
        d_inactive.on_m1_retn_delay(false);
        const bool d_a_held_post = d_inactive.automap_held();

        // Arm B: MF ACTIVE, RETN seen → DivMmc automap_held PRESERVED.
        DivMmc d_active = make_held_divmmc();
        Multiface mf_active;
        // VHDL multiface.vhd:103 — `reset = reset_i OR NOT enable_i`,
        // so nmi_active can only set while enable_i='1'. Enable first,
        // then drive button_press: latches nmi_active=1 (line 137-138)
        // ⇒ is_nmi_hold()=1 ⇒ is_active()=true.
        mf_active.set_enabled(true);
        mf_active.button_press();
        const bool mf_b_active = mf_active.is_active();
        const bool d_b_held_pre = d_active.automap_held();
        const bool retn_pulse_b = true;
        d_active.on_m1_retn_delay(retn_pulse_b && !mf_active.is_active());
        d_active.on_m1_retn_delay(false);
        const bool d_b_held_post = d_active.automap_held();

        check("MF-G48-06",
              "DivMMC retn_seen gated by NOT mf_is_active: clears with MF inactive, preserved with MF active",
              mf_a_inactive && d_a_held_pre && !d_a_held_post   // arm A: clear path
              && mf_b_active && d_b_held_pre && d_b_held_post, // arm B: gate suppresses
              "VHDL zxnext.vhd:4111 (divmmc_retn_seen AND-NOT mf_is_active) / :2099 (mf_is_active)");
    }

    // MF-G48-07 — port 0xDFFD bit 6 storage for MF readback (G148 sibling).
    // VHDL zxnext.vhd:3694 stores cpu_do(6) into the separate
    // `port_dffd_reg_6` flip-flop (declared at :877 outside the 5-bit
    // port_dffd_reg vector). VHDL :4314 composes the MF +3 DFFD readback
    // as `'0' & port_dffd_reg_6 & '0' & port_dffd_reg`. Wave 1 B3
    // exposes the storage end-to-end via the read handler at LSB 0x3F.
    //
    // Discriminative: write 0x40 to 0xDFFD (bit 6 only) and verify the
    // MF readback at cpu_a(15:12)=0xD shows bit 6 set (0x40), bit 5
    // forced to 0, bits 4:0 = 0 (since cpu_do(4:0)=0). Then write 0x60
    // (bits 6 + 5) and verify bit 5 STILL reads 0 in the readback —
    // pinning the layout `0_reg_6_0_reg(4:0)` rather than `0_reg_6_reg_5_reg(4:0)`.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Set up MF +3 + enable + clear invisible (same prep as MF-G48-05).
        emu.port().out(0x243B, 0x0A);
        emu.port().out(0x253B, 0x00);  // mf_type = 00
        emu.port().out(0x243B, 0x83);
        emu.port().out(0x253B, 0xFF);  // NR 0x83 b1 = 1
        emu.multiface().button_press();

        // Write 0x40 to 0xDFFD: cpu_do(6)=1, cpu_do(4:0)=0, cpu_do(5)=0.
        emu.port().out(0xDFFD, 0x40);
        const uint8_t got_b6_only = emu.port().in(0xD13F);  // expect 0x40

        // Write 0x60 to 0xDFFD: cpu_do(6)=1, cpu_do(5)=1, cpu_do(4:0)=0.
        // bit 5 of the readback is HARDWIRED to '0' per VHDL :4314, so
        // the readback must STILL be 0x40 (not 0x60).
        emu.port().out(0xDFFD, 0x60);
        const uint8_t got_b6_plus_b5 = emu.port().in(0xD13F);  // expect 0x40

        check("MF-G48-07",
              "port 0xDFFD bit 6 latches into port_dffd_reg_6; bit 5 hardwired to 0 in readback",
              got_b6_only == 0x40 && got_b6_plus_b5 == 0x40,
              "VHDL zxnext.vhd:3694 (reg_6 storage), :4314 (mux layout 0|reg_6|0|reg(4:0))");
    }
}

// =====================================================================
// Group MF-INT — Wave 1 F-gate live wiring (Multiface ↔ NmiSource)
// VHDL zxnext.vhd:2099/:2105/:2118 + :4111. Verifies that
// `nmi_source_.set_mf_is_active()` / `set_mf_nmi_hold()` are driven
// from the live `Multiface::is_active()` / `is_nmi_hold()` accessors
// per cycle, replacing the always-false stubs from the
// TASK-NMI-SOURCE-PIPELINE-PLAN.md scaffolding.
// =====================================================================

static void g_mf_int_wiring()
{
    set_group("MF-INT");

    // MF-INT-01 — NmiSource sees mf_is_active=true when Multiface is
    // in the NMI-hold state (nmi_active=1 latched via button press).
    // VHDL :2099 — mf_is_active = mf_mem_en OR mf_nmi_hold.
    //
    // Discriminative: pre-button-press, NmiSource::mf_is_active() is
    // false; post-button-press + execute_single_instruction (which
    // drives the per-tick wiring), NmiSource::mf_is_active() is true.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Configure MF MF1 mode + enable via NextReg (mirrors MF-G48-01).
        emu.port().out(0x243B, 0x0A);
        emu.port().out(0x253B, 0xC0);  // mf_type=11 (MF1)
        emu.port().out(0x243B, 0x83);
        emu.port().out(0x253B, 0xFF);  // NR 0x83 b1=1 enables MF

        const bool pre_inactive  = !emu.multiface().is_active();
        emu.execute_single_instruction();
        const bool ns_pre_clear  = !emu.nmi_source().mf_is_active();

        // Press MF button — latches multiface nmi_active=1 ⇒
        // is_nmi_hold()=true ⇒ is_active()=true.
        emu.multiface().button_press();
        const bool mf_active_now = emu.multiface().is_active();

        // One instruction: tick path now propagates is_active() to
        // NmiSource::set_mf_is_active(true).
        emu.execute_single_instruction();
        const bool ns_post_active = emu.nmi_source().mf_is_active();

        check("MF-INT-01",
              "NmiSource::mf_is_active() reflects Multiface::is_active() per tick (live wiring)",
              pre_inactive && ns_pre_clear && mf_active_now && ns_post_active,
              "VHDL zxnext.vhd:2099 (mf_is_active = mf_mem_en OR mf_nmi_hold) — Wave 1 F-gate live wiring");
    }

    // MF-INT-02 — NmiSource sees mf_nmi_hold=true when Multiface
    // nmi_active=1.
    // VHDL :2118 — `nmi_hold <= mf_nmi_hold WHEN nmi_mf='1' ELSE
    //              divmmc_nmi_hold WHEN nmi_divmmc='1' ELSE
    //              nmi_assert_expbus;`
    // We pin the wiring (set_mf_nmi_hold ↔ Multiface::is_nmi_hold)
    // here; the priority-arbiter selector itself is covered by the
    // GATE-* rows.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        emu.port().out(0x243B, 0x0A);
        emu.port().out(0x253B, 0xC0);
        emu.port().out(0x243B, 0x83);
        emu.port().out(0x253B, 0xFF);

        emu.execute_single_instruction();
        const bool ns_pre = !emu.nmi_source().mf_nmi_hold();
        const bool mf_pre = !emu.multiface().is_nmi_hold();

        emu.multiface().button_press();
        const bool mf_post = emu.multiface().is_nmi_hold();

        emu.execute_single_instruction();
        const bool ns_post = emu.nmi_source().mf_nmi_hold();

        check("MF-INT-02",
              "NmiSource::mf_nmi_hold() reflects Multiface::is_nmi_hold() per tick (live wiring)",
              ns_pre && mf_pre && mf_post && ns_post,
              "VHDL zxnext.vhd:2118 (nmi_hold selector) — Wave 1 F-gate live wiring");
    }
}

// =====================================================================
// Group BOOT — NextZXOS boot ladder + bypass + dot-cmd (G46/G47/G59/G60)
// =====================================================================
//
// Task 8a re-audit (2026-07-13): the native firmware-faithful boot
// landed 2026-07-10 (v0.94.0) and regression screenshots now exist at
// test/00regression/regression_tests.conf: boot-nextzxos-splash (f252),
// boot-nextzxos-welcome (f400), boot-nextzxos-menu (f450). Two of the
// five original skip rows are genuinely exercised by those regression
// tests and are re-homed below (comment only, no unit-tier row). The
// `--bypass-tbblue-fw` feature (G59) was REMOVED from src/ on 2026-07-11
// per explicit user decision (EMULATOR-DESIGN-PLAN.md Phase 11); its two
// rows are WONT, not skip — there is nothing left to implement.

static void g_boot_skips()
{
    set_group("BOOT");

    // BOOT-LOOP-01 — COVERED AT regression tier (not a skip). G46(b)'s
    // RAM-test outer-loop (112 banks via NR 0x56, ~208 passes/bank) sits
    // between the splash log and the welcome screen in the real boot
    // ladder (doc/issues/nextzxos-boot/...). boot-nextzxos-welcome
    // (test/00regression/regression_tests.conf, frame 400 = 8s emulated)
    // only renders if the RAM-test loop completes and falls through to
    // BASIC — if it hung (its pre-fix behaviour), the welcome screenshot
    // would not match the reference. See
    // test/00regression/regression_tests.conf: boot-nextzxos-welcome.

    // BOOT-LOGO-01 — COVERED AT regression tier (not a skip). G46(c)'s
    // "missing logo + 4-entry loader log" defect is exactly what
    // boot-nextzxos-splash pins: its conf comment reads "the fixed frame
    // shows the clean white-on-black loading log (incl. the Multiface
    // ROM line)" at frame 252. See
    // test/00regression/regression_tests.conf: boot-nextzxos-splash.

    // BOOT-DOT-01 — COVERED AT regression tier (not a skip). G47's
    // dot-command shell is exercised end-to-end by boot-nextzxos-dotls
    // (Task 57, 2026-07-14): boot NextZXOS, SPACE past the welcome tour,
    // DOWN+ENTER into "Command Line", type ".ls" (the '.' via the SYM+M
    // compound `--delayed-keypress-frames` now supports) + ENTER, and
    // pin the resulting SD-root directory listing ("total 22 files",
    // names/sizes verified against the image's FAT32 root via mdir) at
    // frame 800 on the settled "scroll?" prompt. See
    // test/00regression/regression_tests.conf: boot-nextzxos-dotls.

    // WONT BYPASS-FAT-01 — G59's `--bypass-tbblue-fw` boot path (the
    // only consumer that needed a *direct host-side load of
    // enNextZX.rom*) was removed from src/ on 2026-07-11 per explicit
    // user decision (EMULATOR-DESIGN-PLAN.md Phase 11 "the flag and its
    // C++ route were REMOVED ... native boot is the only path"). The
    // generic host-side FAT32 reader this row's old reason called
    // "missing" now exists (src/core/sd_rom_extractor.{h,cpp},
    // src/core/fat32_image.{h,cpp}) and is used for 48/128/plus3.rom,
    // enNxtmmc.rom, enNextMf.rom and the Alt ROM — but nothing calls it
    // for enNextZX.rom, because nothing needs to: firmware-faithful boot
    // loads that ROM through the emulated SD/SPI path, not the host
    // reader. Reopen only if a bypass-style boot path is reintroduced.

    // WONT BYPASS-INI-01 — G60's config.ini/menu.ini/menu.def parser was
    // scoped exclusively as a dependency of G59 ("Dependencies: G59
    // lands first" — doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md).
    // G59 is gone (see BYPASS-FAT-01 above), so this row has no feature
    // left to implement. Reopen only if G59-equivalent work returns.
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
        div.set_nr_0a_4_enable(true);  // simulate firmware NR 0x0A bit 4 set
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
        d3.set_nr_0a_4_enable(true);   // simulate firmware NR 0x0A bit 4 set
        d3.set_entry_points_0(0x01);
        d3.set_entry_valid_0(0x01);
        d3.set_entry_timing_0(0x01);
        d3.check_automap(0x0000, true);
        d3.check_automap(0x0100, true);
        const bool only_hld = d3.is_nmi_hold() && !d3.button_nmi();

        DivMmc d4;
        d4.set_enabled(true);
        d4.set_nr_0a_4_enable(true);   // simulate firmware NR 0x0A bit 4 set
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
        d.set_enabled(true);
        d.set_nr_0a_4_enable(true);           // simulate firmware NR 0x0A bit 4 set
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
        d.set_nr_0a_4_enable(true);           // simulate firmware NR 0x0A bit 4 set
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
        d.set_nr_0a_4_enable(true);            // simulate firmware NR 0x0A bit 4 set
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
    //   VHDL zxnext.vhd:369-371 — expbus_eff_en / expbus_eff_disable_mem
    //                              power-on '0' (Pass-9 fix).
    //   RST-01 bundles this; here we isolate the gate-flag defaults into
    //   a dedicated row so Wave C owns the gate-register reset contract.
    {
        NmiSource nmi;  // constructor calls reset()
        const bool mf_off       = !nmi.mf_enable();
        const bool divmmc_off   = !nmi.divmmc_enable();
        const bool expbus_off   = !nmi.expbus_debounce_disable();
        const bool cfg_mode_off = !nmi.config_mode();
        const bool expbus_eff_en_off          = !nmi.expbus_eff_en();
        const bool expbus_eff_disable_mem_off = !nmi.expbus_eff_disable_mem();
        check("GATE-08",
              "power-on gate flags (mf_en, divmmc_en, expbus_debounce_dis, expbus_eff_en, expbus_eff_disable_mem, config_mode) all false",
              mf_off && divmmc_off && expbus_off && cfg_mode_off &&
              expbus_eff_en_off && expbus_eff_disable_mem_off,
              "zxnext.vhd:1109-1110 / 1222 / 369-371 / NR 0x03 reset-to-config semantics");
    }

    // GATE-09 — NR 0x80 bit 7 (`expbus_eff_en`) gates `nmi_assert_expbus`.
    //   VHDL zxnext.vhd:2089 — nmi_assert_expbus <= '1' when
    //     expbus_eff_en = '1' AND expbus_eff_disable_mem = '0' AND
    //     i_BUS_NMI_n = '0' else '0'. With expbus_eff_en='0', driving the
    //     pin active must NOT raise the producer (and so the latch stays
    //     clear). Pass-9 verify-audit row.
    {
        NmiSource nmi;
        nmi.reset();
        // expbus_eff_en defaults to false.
        nmi.set_expbus_nmi_n(false);   // assert /BUS_NMI (active-low)
        nmi.tick(1);
        const bool producer_off = !nmi.nmi_assert_expbus();
        const bool latch_off    = !nmi.nmi_expbus();
        const bool fsm_idle     = nmi.state() == NmiSource::State::Idle;
        check("GATE-09",
              "expbus_eff_en=0 blocks nmi_assert_expbus even with /BUS_NMI low",
              producer_off && latch_off && fsm_idle,
              "zxnext.vhd:2089 expbus_eff_en AND gate on nmi_assert_expbus");
    }

    // GATE-10 — NR 0x80 bit 4 (`expbus_eff_disable_mem`) blocks
    //   `nmi_assert_expbus`. With expbus_eff_en=1, expbus_eff_disable_mem=1
    //   and the pin active, the producer must NOT fire (VHDL gate AND NOT).
    //   Pass-9 verify-audit row.
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_expbus_eff_en(true);
        nmi.set_expbus_eff_disable_mem(true);
        nmi.set_expbus_nmi_n(false);   // assert /BUS_NMI (active-low)
        nmi.tick(1);
        const bool producer_off = !nmi.nmi_assert_expbus();
        const bool latch_off    = !nmi.nmi_expbus();
        const bool fsm_idle     = nmi.state() == NmiSource::State::Idle;
        check("GATE-10",
              "expbus_eff_disable_mem=1 blocks nmi_assert_expbus",
              producer_off && latch_off && fsm_idle,
              "zxnext.vhd:2089 NOT expbus_eff_disable_mem AND gate on nmi_assert_expbus");
    }

    // GATE-11 — full ExpBus path: enable, no disable_mem, pin asserted →
    //   producer + latch fire. Confirms the Pass-9 gate refactor still
    //   admits the legitimate path that the previous (under-gated) code
    //   admitted.
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_expbus_eff_en(true);
        nmi.set_expbus_eff_disable_mem(false);
        nmi.set_expbus_nmi_n(false);   // assert /BUS_NMI (active-low)
        nmi.tick(1);
        const bool producer_on = nmi.nmi_assert_expbus();
        const bool latch_on    = nmi.nmi_expbus();
        const bool fsm_fetch   = nmi.state() == NmiSource::State::Fetch;
        check("GATE-11",
              "expbus_eff_en=1 + disable_mem=0 + pin low → producer + latch + FSM fetch",
              producer_on && latch_on && fsm_fetch,
              "zxnext.vhd:2089 ExpBus full-path producer assertion");
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
        // Pass-9 verify-audit: `nmi_assert_expbus` is now properly gated
        // on NR 0x80 bit 7 (`expbus_eff_en`) per VHDL zxnext.vhd:2089.
        // Power-on default of NR 0x80 is X"00" → expbus_eff_en='0' so the
        // pin assertion alone no longer latches. Enable expbus first to
        // model a software-configured expansion bus.
        nmi.set_expbus_eff_en(true);
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
// Group TestCov — Regression rows for NMI/MF/Port/NextREG verify-pass
// fixes (commits c1d7998..6051f01) that lack a dedicated unit-tier test.
// Each row cites VHDL line + fix commit. See report:
//   doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-NMI-MF-PORT.md
// =====================================================================

static void g_testcov_nmi_regressions()
{
    set_group("TestCov");

    // TC-NMI3-END-IDLE — Subsequent NMIs work after the first
    //   (Initial NMI-3, c1d7998). Pre-fix the FSM stuck in `End` forever
    //   because `observe_cpu_wr()` was defined but never called from the
    //   Emulator. Fix: case End now advances to Idle on the same pass
    //   that clears the priority latches. This row drives a full IDLE →
    //   FETCH → HOLD → END cycle, then triggers a second MF NMI and
    //   confirms the FSM re-enters Fetch (not End-stuck).
    //   VHDL :2149-2162 (S_NMI_END), :2102-2105 (latch clear).
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_mf_enable(true);

        // First NMI cycle.
        nmi.strobe_mf_button();
        nmi.tick(1);                      // IDLE → FETCH
        const bool reached_fetch_1 = nmi.state() == NmiSource::State::Fetch;
        nmi.observe_m1_fetch(0x0066, true, true);  // FETCH → HOLD (direct)
        // Tick once with mf_nmi_hold defaulting false: HOLD → END.
        nmi.tick(1);
        const bool reached_end = nmi.state() == NmiSource::State::End;
        // NMI-3 fix: End advances to Idle on the next tick (otherwise
        // the FSM would stick in End and no second NMI could fire).
        nmi.tick(1);
        const bool back_to_idle = nmi.state() == NmiSource::State::Idle;

        // Second NMI cycle — must fire if End→Idle works.
        nmi.strobe_mf_button();
        nmi.tick(1);
        const bool reached_fetch_2 = nmi.state() == NmiSource::State::Fetch;

        check("TC-NMI3-END-IDLE",
              "FSM advances End → Idle so subsequent NMIs fire "
              "[zxnext.vhd:2149-2162 / Initial NMI-3 fix c1d7998]",
              reached_fetch_1 && reached_end && back_to_idle &&
              reached_fetch_2,
              std::string{"f1="} + std::to_string(reached_fetch_1) +
              " end=" + std::to_string(reached_end) +
              " idle=" + std::to_string(back_to_idle) +
              " f2=" + std::to_string(reached_fetch_2));
    }

    // TC-NMI-HOLD-LINE-HIGH — /NMI line is HIGH (deasserted) during HOLD
    //   (Verify1, 78f5f1c). VHDL :2168 — /NMI is asserted only in
    //   IDLE+activated, FETCH, or expbus debounce. NOT in HOLD.
    //   Pre-fix `nmi_generate_n` was '0' through HOLD (functionally
    //   invisible because Z80 latches on falling edge, but spec violation).
    //
    //   We use the MF producer for clarity and rely on `mf_nmi_hold_`
    //   sticky-set so the FSM stays in HOLD long enough to observe.
    {
        NmiSource nmi;
        nmi.reset();
        nmi.set_mf_enable(true);
        nmi.set_mf_nmi_hold(true);        // hold sticky: HOLD stays in HOLD

        // Drive FSM IDLE → FETCH via MF producer.
        nmi.strobe_mf_button();
        nmi.tick(1);
        const bool fetch = nmi.state() == NmiSource::State::Fetch;
        // /NMI must be LOW (asserted) in FETCH per VHDL :2168.
        const bool nmi_low_in_fetch = !nmi.nmi_generate_n();

        // FETCH → HOLD on M1 fetch at 0x0066.
        nmi.observe_m1_fetch(0x0066, true, true);
        const bool hold = nmi.state() == NmiSource::State::Hold;
        // /NMI must be HIGH (deasserted) in HOLD per VHDL :2168.
        // (The FSM is in HOLD now even before the next tick, since
        // observe_m1_fetch sets state directly.)
        const bool nmi_high_in_hold = nmi.nmi_generate_n();

        check("TC-NMI-HOLD-LINE-HIGH",
              "/NMI deasserted (HIGH) during HOLD state "
              "[zxnext.vhd:2168 / Verify1 78f5f1c]",
              fetch && nmi_low_in_fetch && hold && nmi_high_in_hold,
              std::string{"fetch="} + std::to_string(fetch) +
              " low_fetch=" + std::to_string(nmi_low_in_fetch) +
              " hold=" + std::to_string(hold) +
              " high_hold=" + std::to_string(nmi_high_in_hold));
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
    g_testcov_nmi_regressions(); std::printf("  TC   verify-pass regressions -- done\n");
    g_nmiack_pc_capture(); std::printf("  Z80  NMIACK PC capture -- RE-HOMED to CTC plan (G88)\n");
    g_mf_g162_skips();     std::printf("  MF   G162 parked rows  -- done\n");
    g_mf_int_wiring();     std::printf("  MF-INT F-gate live wiring -- done\n");
    g_boot_skips();        std::printf("  BOOT NextZXOS+bypass   -- done\n");

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
