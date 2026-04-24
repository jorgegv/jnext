// CTC + Interrupt Controller Integration Test — full-machine rows re-homed
// from test/ctc/ctc_test.cpp (Section C Phase 3c of the CTC-INTERRUPTS
// skip-reduction plan, 2026-04-21).
//
// These plan rows cannot be exercised on a bare Ctc or bare
// Im2Controller — they span NextReg + Im2Controller + ULA + port-dispatch
// wiring. They live on the integration tier rather than the subsystem
// tier, and they test observable state via the same port path the real
// Z80 uses (OUT 0x243B / OUT 0x253B / IN 0x253B) or via the public
// Emulator accessors.
//
// 2026-04-24: added ULA-INT-04 (line interrupt at cvc match) and
// ULA-INT-06 (line 0 → c_max_vc wrap) re-homed from ctc_test.cpp.
//
// Reference plan: doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md,
// Section C Phase 3. Reference structural template:
// test/nextreg/nextreg_integration_test.cpp.
//
// Run: ./build/test/ctc_int_test

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

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Fresh-state idiom: re-initialise the emulator before each scenario so
// cross-test state (scheduler queues, latched interrupt status, etc.)
// cannot leak between scopes.
static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Read NextREG register through the real port path (OUT 0x243B,reg;
// IN 0x253B). Mirrors the idiom used by nextreg_integration_test.cpp.
static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

// Write NextREG register through the real port path.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// ══════════════════════════════════════════════════════════════════════
// Section 12 — ULA / Line interrupt integration (ULA-INT-01/02/03/05)
// ══════════════════════════════════════════════════════════════════════

static void test_ula_int_integration(Emulator& emu) {
    set_group("ULA-Integration");

    // ULA-INT-01 — ULA HC/VC interrupt fires at (int_h, int_v).
    // VHDL: zxnext.vhd:1937,1941 (im2_int_req bit 11 = ula_int_pulse);
    // emulator.cpp:1988-1998 schedules ULA int one scanline into the
    // frame via the scheduler + im2_.raise_req(DevIdx::ULA). Observable:
    // after running one frame with ULA int_en = 1 (port_ff_interrupt_disable
    // = 0 at reset), NR 0xC8 bit 0 = ULA int_status must be set.
    {
        fresh(emu);
        // Reset leaves ula_int_disabled_=false (enabled). Run one frame —
        // the scheduled ULA interrupt fires during frame execution and
        // raise_req() is invoked on the Im2 fabric; the wrapper-layer edge
        // detect (step_devices) latches im2_int_req within the same frame.
        emu.run_frame();
        const uint8_t c8 = nr_read(emu, 0xC8);
        // o_int_status = int_status OR im2_int_req (im2.cpp:264-266). Bit 0
        // of NR 0xC8 = ULA; it should be set once the fabric has ticked.
        check("ULA-INT-01",
              "ULA HC/VC interrupt fires at int_h/int_v → NR 0xC8 bit 0 set "
              "[zxnext.vhd:1937,1941; emulator.cpp:1988-1998; im2.cpp:264-266]",
              (c8 & 0x01) != 0,
              "NR 0xC8=" + hex2(c8) + " (expected bit 0 set)");
    }

    // ULA-INT-02 — port 0xFF bit 6 suppresses ULA interrupt.
    // VHDL: zxnext.vhd:3635 (port_ff_interrupt_disable <= port_ff_reg(6)),
    // :6711 (ula_int_en(0) = NOT port_ff_interrupt_disable), :3619-3620
    // (NR 0x22 bit 2 mirrors into port_ff_reg(6)).
    //
    // jnext observable path: NR 0x22 bit 2 drives `ula_int_disabled_` —
    // the same bit that VHDL mirrors into port_ff_reg(6). Setting it
    // BEFORE run_frame must prevent the scheduler from arming the ULA
    // interrupt (emulator.cpp:1989 gate), so NR 0xC8 bit 0 stays clear.
    //
    // DIRECT `OUT 0xFF` TO DISABLE: jnext wires port 0xFF only to the
    // Timex screen-mode write (emulator.cpp:1103-1108); port_ff_reg bit 6
    // is NOT fed back through the port path, so an `OUT 0xFF,0x40`
    // cannot currently set port_ff_interrupt_disable. This is a latent
    // subsystem gap — VHDL drives bit 6 from port 0xFF writes too. Out
    // of scope for Phase 3c; this row exercises the NR-22 mirror, which
    // is the VHDL-equivalent observable.
    {
        fresh(emu);
        // Set NR 0x22 bit 2 → ula_int_disabled_ = true.
        nr_write(emu, 0x22, 0x04);
        emu.run_frame();
        const uint8_t c8 = nr_read(emu, 0xC8);
        check("ULA-INT-02",
              "ULA int suppressed when port_ff(6)/NR 0x22[2] set → NR 0xC8 bit 0 clear "
              "[zxnext.vhd:3619-3620, :3635, :6711; emulator.cpp:1989 gate]",
              (c8 & 0x01) == 0,
              "NR 0xC8=" + hex2(c8) + " (expected bit 0 clear)");
    }

    // ULA-INT-03 — ula_int_en = NOT port_ff_interrupt_disable.
    // VHDL: zxnext.vhd:6711 (ula_int_en <= nr_22_line_interrupt_en &
    // (NOT port_ff_interrupt_disable)); NR 0xC4 read format E_00000_UU
    // returns ula_int_en in bits 1:0 (zxnext.vhd:6239).
    //
    // At reset port_ff_interrupt_disable=0 so bit 0 of NR 0xC4 read must
    // be 1. After NR 0x22 bit 2 → port_ff_interrupt_disable=1, bit 0
    // of NR 0xC4 read must flip to 0.
    {
        fresh(emu);
        const uint8_t before = nr_read(emu, 0xC4);
        nr_write(emu, 0x22, 0x04);              // set port_ff_interrupt_disable
        const uint8_t after = nr_read(emu, 0xC4);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "before=0x%02X after=0x%02X (bit0 should be 1 then 0)",
                      before, after);
        check("ULA-INT-03",
              "NR 0xC4 read bit 0 = NOT port_ff_interrupt_disable "
              "[zxnext.vhd:6239, :6711; emulator.cpp:802]",
              (before & 0x01) != 0 && (after & 0x01) == 0, detail);
    }

    // ULA-INT-05 — NR 0x22 line_interrupt_en bit gates line interrupt.
    // VHDL: zxnext.vhd:5607-5610 (NR 0x22 bit 1 → nr_22_line_interrupt_en);
    // also zxnext.vhd:6239 (NR 0xC4 read bit 1 = nr_22_line_interrupt_en).
    //
    // Writing NR 0x22 with bit 1 set must make NR 0xC4 read bit 1
    // become 1 (VHDL read mux); clearing it must turn bit 1 back to 0.
    {
        fresh(emu);
        const uint8_t before = nr_read(emu, 0xC4);
        nr_write(emu, 0x22, 0x02);              // line_interrupt_en = 1
        const uint8_t on = nr_read(emu, 0xC4);
        nr_write(emu, 0x22, 0x00);              // line_interrupt_en = 0
        const uint8_t off = nr_read(emu, 0xC4);
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "before=0x%02X on=0x%02X off=0x%02X "
                      "(bit1 pattern should be 0,1,0)",
                      before, on, off);
        check("ULA-INT-05",
              "NR 0x22 bit 1 drives line_interrupt_en, visible on NR 0xC4 read bit 1 "
              "[zxnext.vhd:5607-5610, :6239; emulator.cpp:542-546, :801]",
              (before & 0x02) == 0 && (on & 0x02) != 0 && (off & 0x02) == 0,
              detail);
    }

    // ULA-INT-04 — Line interrupt fires at the configured scanline.
    // VHDL: zxula_timing.vhd:577-582 (int_line = '1' when i_inten_line=1
    // AND hc_ula=255 AND cvc=int_line_num). int_line pulses into
    // zxnext.vhd:1941 im2_int_req bit 0 (LINE, priority slot 0).
    // emulator.cpp:2392-2402 schedules the LINE interrupt at
    // frame_cycle + line_int_value * master_cycles_per_line; the callback
    // calls im2_.raise_req(DevIdx::LINE), which — once step_devices() ticks
    // — latches im2_int_req[0]. Observable: NR 0xC8 bit 1 = LINE status
    // set after run_frame().
    //
    // Stimulus: NR 0x22 bit 1 = 1 (line_interrupt_en), NR 0x23 = mid-frame
    // line (e.g. 100). After run_frame, NR 0xC8 bit 1 must be set. The
    // ULA int also fires (bit 0); we only assert bit 1.
    {
        fresh(emu);
        nr_write(emu, 0x22, 0x02);              // line_interrupt_en = 1
        nr_write(emu, 0x23, 100);               // line 100 (low 8 bits)
        // NR 0x23 bit 8 stays 0 via NR 0x22 bit 0 (not set here).
        emu.run_frame();
        const uint8_t c8 = nr_read(emu, 0xC8);
        check("ULA-INT-04",
              "Line interrupt fires at cvc match (int_line pulse → NR 0xC8 bit 1) "
              "[zxula_timing.vhd:577-582; zxnext.vhd:1941; emulator.cpp:2392-2402]",
              (c8 & 0x02) != 0,
              "NR 0xC8=" + hex2(c8) + " (expected bit 1 LINE set)");
    }

    // ULA-INT-06 — Line 0 maps to c_max_vc (wrap semantics).
    // VHDL: zxula_timing.vhd:566-570 — when i_int_line=0, int_line_num
    // latches to c_max_vc (the last scanline of the frame); otherwise
    // int_line_num = i_int_line - 1. So writing NR 0x23=0 must still
    // cause LINE to fire once per frame (at the last scanline). The jnext
    // emulator schedules the callback for line_int_value=0 at frame_cycle
    // (value < lines_per_frame gate at emulator.cpp:2392 passes; the
    // exact cycle is a separate VHDL-timing concern re-homed to
    // doc/design/VIDEOTIMING-EXPANSION-PLAN.md).
    //
    // Stimulus: NR 0x22 bit 1 = 1 + NR 0x23 = 0. After one run_frame,
    // NR 0xC8 bit 1 must be set (i.e. line_int_value=0 is NOT ignored —
    // it fires somewhere within the frame, matching the VHDL wrap
    // invariant that value 0 is still a valid firing line).
    {
        fresh(emu);
        nr_write(emu, 0x22, 0x02);              // line_interrupt_en = 1
        nr_write(emu, 0x23, 0x00);              // line 0 → VHDL wraps to c_max_vc
        emu.run_frame();
        const uint8_t c8 = nr_read(emu, 0xC8);
        check("ULA-INT-06",
              "Line interrupt value 0 still fires within frame (VHDL: wraps to "
              "c_max_vc) [zxula_timing.vhd:566-570; emulator.cpp:2392-2402]",
              (c8 & 0x02) != 0,
              "NR 0xC8=" + hex2(c8) + " (expected bit 1 LINE set)");
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 13 — NextREG 0xC0 / 0xC4 / 0xC6 readback composition
// ══════════════════════════════════════════════════════════════════════

static void test_nr_c0_c4_c6(Emulator& emu) {
    set_group("NR-C0-C4-C6");

    // NR-C0-04 — NR 0xC0 read composes VVV_0_S_MM_I.
    // VHDL: zxnext.vhd:6229-6230.
    //   bits 7:5 = nr_c0_im2_vector (vector base MSBs)
    //   bit  4   = '0' (unused)
    //   bit  3   = nr_c0_stackless_nmi
    //   bits 2:1 = z80_im_mode (read-only)
    //   bit  0   = nr_c0_int_mode_pulse_0_im2_1
    //
    // Exercise: write VVV=101 (0xA0) | stackless=1 (0x08) | im2_mode=1 (0x01)
    // → 0xA9. Read must come back with VVV, stackless and im2 bits preserved.
    // bits 2:1 reflect whatever the IM decoder latched; at reset/power-on
    // z80_im_mode=0, so bits 2:1 = 00 and the full read = 0xA9.
    {
        fresh(emu);
        nr_write(emu, 0xC0, 0xA9);
        const uint8_t got = nr_read(emu, 0xC0);
        // Check the VHDL composition: VVV mask, stackless bit 3, mode bit 0.
        // Bit 4 is constant-'0'. Bits 2:1 = im_mode (expect 00 post-reset).
        const bool vvv_ok      = ((got >> 5) & 0x07) == 0x05;
        const bool bit4_zero   = (got & 0x10) == 0;
        const bool stackless   = (got & 0x08) != 0;
        const bool immode_zero = ((got >> 1) & 0x03) == 0;
        const bool im2_mode    = (got & 0x01) != 0;
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "got=0x%02X vvv=%d b4=%d S=%d MM=%d I=%d",
                      got, (got >> 5) & 0x07, (got & 0x10) >> 4,
                      (got & 0x08) >> 3, (got >> 1) & 0x03, got & 0x01);
        check("NR-C0-04",
              "NR 0xC0 read composes VVV_0_S_MM_I (vector + stackless + "
              "im_mode + int_mode) [zxnext.vhd:6229-6230; emulator.cpp:771-778]",
              vvv_ok && bit4_zero && stackless && immode_zero && im2_mode,
              detail);
    }

    // NR-C4-02 — NR 0xC4 bit 1 drives line_interrupt_en.
    // VHDL: zxnext.vhd:5607-5610 (write path). The NR 0xC4 write mirrors
    // bit 1 into nr_22_line_interrupt_en; then NR 0xC4 read bit 1 reflects
    // it (VHDL:6239). Use the NR 0xC4 write → NR 0xC4 read path (the
    // ULA-INT-05 row exercised the NR 0x22 write path).
    {
        fresh(emu);
        nr_write(emu, 0xC4, 0x02);              // set line_interrupt_en via NR C4
        const uint8_t on  = nr_read(emu, 0xC4);
        nr_write(emu, 0xC4, 0x00);              // clear
        const uint8_t off = nr_read(emu, 0xC4);
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "on=0x%02X off=0x%02X (bit1 should be 1 then 0)", on, off);
        check("NR-C4-02",
              "NR 0xC4 bit 1 write drives line_interrupt_en (read bit 1 round-trip) "
              "[zxnext.vhd:5607-5610, :6239; emulator.cpp:792]",
              (on & 0x02) != 0 && (off & 0x02) == 0, detail);
    }

    // NR-C4-03 — NR 0xC4 readback format E_00000_UU.
    // VHDL: zxnext.vhd:6239 — port_253b_dat <= nr_c4_int_en_0_expbus & "00000"
    //                         & nr_22_line_interrupt_en & (NOT port_ff_interrupt_disable).
    //
    // Exercise: write expbus=1 (bit 7) + line=1 (bit 1). ULA-INT bit 0 is
    // driven by !ula_int_disabled_ (=true at reset). Ensure bits 6:2 read 0.
    {
        fresh(emu);
        nr_write(emu, 0xC4, 0x82);              // expbus=1 + line=1
        const uint8_t got = nr_read(emu, 0xC4);
        // Expected: E_00000_UU with E=1, UU={line,ula}={1,1}=11 → 0x83.
        check("NR-C4-03",
              "NR 0xC4 readback format E_00000_UU (expbus, 5x zero, line, ula) "
              "[zxnext.vhd:6239; emulator.cpp:796-804]",
              got == 0x83, detail_eq(got, 0x83));
    }

    // NR-C6-02 — NR 0xC6 readback format 0_654_0_210.
    // VHDL: zxnext.vhd:6244-6245 — '0' & nr_c6_int_en_2_654 & '0'
    //                              & nr_c6_int_en_2_210.
    // Bits 7 and 3 must read 0 regardless of the written value.
    //
    // Exercise: write 0xFF, read should be 0x77 (bits 7 and 3 masked).
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0xFF);
        const uint8_t got = nr_read(emu, 0xC6);
        check("NR-C6-02",
              "NR 0xC6 read format 0_654_0_210 (bits 7 and 3 read as 0) "
              "[zxnext.vhd:6244-6245; emulator.cpp:828-831]",
              got == 0x77, detail_eq(got, 0x77));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 14 — Legacy NR 0x20 / NR 0x22 int-status readbacks
// ══════════════════════════════════════════════════════════════════════

static void test_legacy_status_reads(Emulator& emu) {
    set_group("Legacy-Status");

    // ISC-09 — legacy NR 0x20 read returns mixed status
    // LINE_ULA_00_CTC6..CTC3.
    // VHDL: zxnext.vhd:5988-5989
    //   port_253b_dat <= im2_int_status(0)     -- LINE, bit 7
    //                  & im2_int_status(11)    -- ULA,  bit 6
    //                  & "00"
    //                  & im2_int_status(6 downto 3);  -- CTC3..CTC0, bits 3..0
    // NR 0x20 write raises int_unq for the matching devices; int_status
    // becomes observable on read (im2.cpp:244-248).
    //
    // Exercise: raise int_unq on LINE + ULA + CTC0 via a single NR 0x20
    // write, then read back. Expected composition: LINE→0x80, ULA→0x40,
    // CTC0→0x01 = 0xC1.
    {
        fresh(emu);
        nr_write(emu, 0x20, 0xC1);              // LINE | ULA | CTC0
        const uint8_t got = nr_read(emu, 0x20);
        // Bits 5:4 must always read 0 per VHDL literal "00" concat.
        const bool literal_zero = (got & 0x30) == 0;
        const bool line = (got & 0x80) != 0;
        const bool ula  = (got & 0x40) != 0;
        const bool ctc0 = (got & 0x01) != 0;
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "got=0x%02X LINE=%d ULA=%d CTC0=%d b5:4=0x%X",
                      got, line, ula, ctc0, (got >> 4) & 0x3);
        check("ISC-09",
              "NR 0x20 read = LINE_ULA_00_CTC3..CTC0 mixed status "
              "[zxnext.vhd:5988-5989; emulator.cpp:917-926]",
              line && ula && ctc0 && literal_zero, detail);
    }

    // ISC-10 — legacy NR 0x22 read bit 7 = NOT pulse_int_n.
    // VHDL: zxnext.vhd:5991-5992
    //   port_253b_dat <= (NOT pulse_int_n) & "0000"
    //                  & port_ff_interrupt_disable
    //                  & nr_22_line_interrupt_en
    //                  & nr_23_line_interrupt(8);
    //
    // At reset pulse_int_n = '1' (Im2Controller::pulse_int_n_ defaults true),
    // port_ff_interrupt_disable = 0, nr_22_line_interrupt_en = 0, and
    // nr_23_line_interrupt(8) = 0. The VHDL composition therefore reads
    // 0x00 on a clean reset.
    //
    // KNOWN GAP (flag, do NOT fix in this phase): jnext installs no
    // read handler for NR 0x22. NextReg::read() at src/port/nextreg.cpp:
    // 101-109 falls back to regs_[0x22] — the raw byte last written. After
    // a fresh reset regs_[0x22] = 0x00 (NextReg::reset fills with 0 and
    // does not seed 0x22), so the fallback happens to coincide with the
    // VHDL composition. Writes to NR 0x22 would break the equivalence
    // (raw fallback ≠ VHDL mask). A follow-up should install a read
    // handler that composes bits per zxnext.vhd:5991-5992; this row
    // asserts only the reset-state invariant.
    {
        fresh(emu);
        const uint8_t got = nr_read(emu, 0x22);
        check("ISC-10",
              "NR 0x22 read bit 7 = NOT pulse_int_n (reset: 0x00) "
              "[zxnext.vhd:5991-5992 — NR 0x22 read_handler missing in jnext; "
              "reset-state invariant only]",
              got == 0x00, detail_eq(got, 0x00));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("CTC + Interrupt Controller Integration Tests\n");
    std::printf("===============================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_ula_int_integration(emu);
    std::printf("  Group: ULA-Integration — done\n");

    test_nr_c0_c4_c6(emu);
    std::printf("  Group: NR-C0-C4-C6 — done\n");

    test_legacy_status_reads(emu);
    std::printf("  Group: Legacy-Status — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
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
