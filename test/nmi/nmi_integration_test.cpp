// NMI Integration Test — end-to-end button/software NMI chain rows for
// TASK-NMI-SOURCE-PIPELINE-PLAN.md Phase 3.
//
// These rows prove the full button/software-NMI pipeline end-to-end on a
// real Emulator fixture: the NmiSource FSM latches the producer, emits
// the VHDL:2170 arbitration strobe into DivMmc::set_button_nmi, drives
// the Z80 /NMI line via request_nmi(), the Z80 jumps to 0x0066 and
// automap activates (DivMMC ROM overlay), then RETN clears the latches.
//
// The narrower bare-NmiSource / bare-DivMmc rows live in
//   test/nmi/nmi_test.cpp          (NR02-*, HK-*, DIS-*, CLR-*, GATE-*, DMA-*)
//   test/divmmc/divmmc_test.cpp    (NM-01..08)
//   test/copper/copper_test.cpp    (ARB-06)
//   test/ctc/ctc_test.cpp          (DMA-04)
// This integration tier proves the pieces compose correctly inside
// Emulator::execute_single_instruction / run_frame.
//
// Reference plan: doc/design/TASK-NMI-SOURCE-PIPELINE-PLAN.md §Phase 3.
// Structural template: test/uart/uart_integration_test.cpp,
//                      test/ula/ula_integration_test.cpp.
//
// Run: ./build/test/nmi_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// ── Test infrastructure (uniform output per feedback_uniform_test_output) ──

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

void check(const char* id, const char* desc, bool cond,
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

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
}

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

} // namespace

// ── Emulator fixture ──────────────────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Set up a deterministic Z80 loop in RAM area (0xC000) so the CPU has a
// stable "idle" program to run before the NMI fires. NOPs + self-jump.
// After NMI, the Z80 jumps to 0x0066 — slot 0 contents depend on whether
// DivMMC automap activated (DivMMC ROM) or not (boot/48K ROM fallback).
static void inject_idle_loop_at_c000(Emulator& emu) {
    // 20 NOPs then JP 0xC000.
    for (int i = 0; i < 20; ++i)
        emu.mmu().write(static_cast<uint16_t>(0xC000 + i), 0x00);
    emu.mmu().write(0xC014, 0xC3);
    emu.mmu().write(0xC015, 0x00);
    emu.mmu().write(0xC016, 0xC0);
}

// Point PC at our idle loop and set IFF1 (NMI ignores IFF1 but we keep
// things tidy). SP sits in RAM so NMI push works.
static void park_cpu_at_c000(Emulator& emu) {
    auto regs = emu.cpu().get_registers();
    regs.PC   = 0xC000;
    regs.SP   = 0xFFFE;
    regs.IFF1 = 0; regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
}

// Fresh-state helper: initialise the Emulator, park the CPU at 0xC000
// with a NOP/JP idle loop.
//
// Boot-state exit: VHDL zxnext.vhd:1102 sets `nr_03_config_mode` = '1' at
// power-on. That gate (zxnext.vhd:2102-2105) force-clears every NMI latch
// each cycle, so no NMI can be taken in config_mode. Real firmware writes
// NR 0x03 to exit config_mode during boot; we do the same here via the
// OUT 0x253B port path so subsequent NMI tests see a normal run state.
static void fresh_cpu_at_c000(Emulator& emu) {
    build_next_emulator(emu);
    // Exit config_mode: NR 0x03 low 3 bits = 001..110 clears it
    // (VHDL zxnext.vhd:5137 decode).
    emu.port().out(0x243B, 0x03);
    emu.port().out(0x253B, 0x01);
    inject_idle_loop_at_c000(emu);
    park_cpu_at_c000(emu);
}

// Step until the CPU takes an NMI (PC in 0x0066..0x006F) or a watchdog
// fires. The inner tick cluster in execute_single_instruction raises
// `cpu_.request_nmi()` on a falling edge of nmi_generate_n, then the NEXT
// execute() takes the NMI. In the worst case we need ~3 steps (tick to
// strobe, tick to advance FSM, tick to fire edge). The watchdog at 200
// catches pathological cases without hanging the test.
//
// Returns the final PC and the number of steps taken (for diagnostics).
struct NmiStepResult { uint16_t final_pc; int steps; bool took_nmi; };
static NmiStepResult step_until_nmi_taken(Emulator& emu) {
    for (int s = 1; s <= 200; ++s) {
        emu.execute_single_instruction();
        const uint16_t pc = emu.cpu().get_registers().PC;
        if (pc >= 0x0066 && pc <= 0x006F) {
            return {pc, s, true};
        }
    }
    return {emu.cpu().get_registers().PC, 200, false};
}

// ══════════════════════════════════════════════════════════════════════
// Section INT — End-to-end button/software NMI rows
// ══════════════════════════════════════════════════════════════════════

static void test_int_rows() {
    set_group("INT");

    // INT-01 — DivMMC button press → NmiSource latches DivMMC producer
    // (gate: NR 0x06 bit 4) → FSM IDLE→FETCH → nmi_generate_n falling
    // edge → Emulator calls cpu_.request_nmi() → Z80 takes NMI on the
    // next execute → PC jumps to 0x0066. VHDL chain:
    //   zxnext.vhd:2091 (nmi_assert_divmmc)
    //   zxnext.vhd:2095-2116 (priority latch)
    //   zxnext.vhd:2120-2162 (FSM IDLE→FETCH)
    //   zxnext.vhd:2164-2170 (nmi_generate_n)
    //   zxnext.vhd:1841      (z80_nmi_n <= nmi_generate_n)
    {
        Emulator emu;
        fresh_cpu_at_c000(emu);
        emu.nmi_source().set_divmmc_enable(true);   // NR 0x06 bit 4 = 1
        emu.nmi_source().strobe_divmmc_button();    // hotkey_drive edge

        const uint16_t pc_before = emu.cpu().get_registers().PC;
        const auto r = step_until_nmi_taken(emu);

        check("INT-01",
              "DivMMC button → NmiSource FSM → /NMI → Z80 PC=0x0066 "
              "(VHDL zxnext.vhd:2091, :2095-2170, :1841)",
              r.took_nmi && pc_before == 0xC000
                         && r.final_pc >= 0x0066 && r.final_pc <= 0x006F,
              fmt("pc_before=0x%04x final_pc=0x%04x steps=%d took_nmi=%d",
                  pc_before, r.final_pc, r.steps, r.took_nmi));
    }

    // INT-02 — At PC=0x0066 M1, DivMmc::button_nmi_ is already latched by
    // the arbitration strobe (VHDL:2170 → DivMmc::set_button_nmi(true))
    // fired the same tick the FSM advanced IDLE→FETCH. On the 0x0066
    // fetch, the DivMmc automap instant-on path activates (divmmc.vhd:120
    // `automap_nmi_instant_on` gated on button_nmi_) — automap_active()
    // reports true by the time the handler is fetched from slot 0.
    //
    // Precondition: DivMMC port-io + NR 0x0A bit-4 both on (set_enabled
    // is collapsed into port_io + nr_0a_4) AND NR 0xBB bit 1 = 1 (0x0066
    // entry point enabled — default NR 0xBB = 0xCD has bit 1 = 0).
    {
        Emulator emu;
        fresh_cpu_at_c000(emu);
        emu.divmmc().set_enabled(true);                       // port_io+NR 0x0A
        emu.divmmc().set_entry_points_1(0xCD | 0x02);         // NR BB bit 1 = 1
        emu.nmi_source().set_divmmc_enable(true);             // NR 06 bit 4 = 1
        emu.nmi_source().strobe_divmmc_button();

        const auto r = step_until_nmi_taken(emu);
        const bool btn_at_0066 = emu.divmmc().button_nmi();

        // `fuse_z80_nmi()` pushes PC and sets PC=0x0066, but does NOT do
        // the M1 fetch at 0x0066 — that happens inside the NEXT execute()
        // call, which is when on_m1_prefetch(0x0066) fires and automap
        // activates. Step one more instruction to observe automap_active.
        emu.execute_single_instruction();
        const bool automap_on = emu.divmmc().automap_active();
        const bool button_nmi = emu.divmmc().button_nmi();

        check("INT-02",
              "At PC=0x0066 after NMI + one M1 fetch, DivMmc automap "
              "instant-on fires via button_nmi latch "
              "(VHDL divmmc.vhd:120 / zxnext.vhd:2170)",
              r.took_nmi && btn_at_0066 && automap_on,
              fmt("took_nmi=%d pc_after_nmi=0x%04x btn_at_0066=%d "
                  "button_nmi=%d automap=%d",
                  r.took_nmi, r.final_pc, btn_at_0066, button_nmi,
                  automap_on));
    }

    // INT-03 — RETN (ED 45) decoded during the NMI handler clears both
    // DivMmc::button_nmi_ and the automap hold chain (divmmc.vhd:108
    // i_retn_seen branch + :126/:139 automap_hold/held clears). Emulator
    // plumbing: Im2Controller::on_m1_cycle decodes the ED 45 sequence and
    // emits retn_seen_this_cycle(), the on_m1_cycle callback in
    // Emulator::init() forwards that to divmmc_.on_retn().
    //
    // Rather than rely on ROM contents at 0x0066 (which depend on
    // DivMMC/boot ROM availability), we directly exercise the seam: land
    // an NMI so button_nmi_ latches, then fire on_retn_seen() and verify
    // the latch clears. Im2Controller's ED 45 decode is already pinned
    // by ctc_test RETI/RETN rows and nmi_test CLR-03.
    {
        Emulator emu;
        fresh_cpu_at_c000(emu);
        emu.divmmc().set_enabled(true);
        emu.divmmc().set_entry_points_1(0xCD | 0x02);
        emu.nmi_source().set_divmmc_enable(true);
        emu.nmi_source().strobe_divmmc_button();
        (void)step_until_nmi_taken(emu);  // land in NMI handler

        const bool btn_before = emu.divmmc().button_nmi();
        emu.divmmc().on_retn_seen();
        const bool btn_after = emu.divmmc().button_nmi();

        check("INT-03",
              "RETN (ED 45) clears DivMmc button_nmi_ "
              "(VHDL divmmc.vhd:108 i_retn_seen branch)",
              btn_before && !btn_after,
              fmt("btn_before=%d btn_after=%d", btn_before, btn_after));
    }

    // INT-04 — NR 0x02 software NMI path. Writing NR 0x02 bit 2 via the
    // real OUT 0x253B port path routes through NextReg::write →
    // write_handler → NmiSource::nr_02_write (Wave A wiring) → same
    // downstream chain as INT-01. VHDL zxnext.vhd:3830-3838,
    // :3833 `nmi_cpu_02_we and cpu_requester_dat(2)` →
    // `nmi_sw_gen_divmmc`.
    {
        Emulator emu;
        fresh_cpu_at_c000(emu);
        emu.nmi_source().set_divmmc_enable(true);
        // OUT 0x243B, 0x02 (select NR 0x02).
        emu.port().out(0x243B, 0x02);
        // OUT 0x253B, 0x04 (write DivMMC software NMI bit).
        emu.port().out(0x253B, 0x04);

        const auto r = step_until_nmi_taken(emu);

        check("INT-04",
              "NR 0x02 bit 2 write via OUT 0x253B → NMI → PC=0x0066 "
              "(VHDL zxnext.vhd:3833, :3838, :2091, :2095-2170, :1841)",
              r.took_nmi && r.final_pc >= 0x0066 && r.final_pc <= 0x006F,
              fmt("took_nmi=%d final_pc=0x%04x steps=%d",
                  r.took_nmi, r.final_pc, r.steps));
    }

    // INT-05 — MF path at the integration tier. The MF software NMI
    // (NR 0x02 bit 3) latches nmi_mf when NR 0x06 bit 3 (mf_enable) is
    // set. Unlike DivMMC, the MF consumer-feedback inputs
    // (mf_is_active / mf_nmi_hold) are stubbed false until Task 8 lands,
    // so the MF latch will not auto-clear. We still exercise the
    // structural path: /NMI falls, Z80 takes NMI, PC jumps to 0x0066.
    // The MF-specific hold/release behaviour is a Task 8 follow-up and
    // is flagged in the plan §Risk 5.
    {
        Emulator emu;
        fresh_cpu_at_c000(emu);
        emu.nmi_source().set_mf_enable(true);          // NR 0x06 bit 3 = 1
        // OUT 0x243B, 0x02; OUT 0x253B, 0x08 (NR 0x02 bit 3 = MF sw NMI).
        emu.port().out(0x243B, 0x02);
        emu.port().out(0x253B, 0x08);

        const auto r = step_until_nmi_taken(emu);
        const bool mf_latched = emu.nmi_source().nmi_mf();

        check("INT-05",
              "NR 0x02 bit 3 (MF sw-NMI) with NR 0x06 bit 3 set → MF "
              "latches, /NMI falls, Z80 PC=0x0066 (VHDL zxnext.vhd:2090, "
              ":2095-2170, :1841; MF consumer feedback stubbed → Task 8)",
              r.took_nmi && mf_latched
                         && r.final_pc >= 0x0066 && r.final_pc <= 0x006F,
              fmt("took_nmi=%d mf_latched=%d final_pc=0x%04x steps=%d",
                  r.took_nmi, mf_latched, r.final_pc, r.steps));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("NMI End-to-End Integration Tests\n");
    std::printf("===============================================\n\n");

    test_int_rows();
    std::printf("  Group: INT — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

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
