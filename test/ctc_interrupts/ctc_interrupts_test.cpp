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
#include <initializer_list>
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
    // V12-NMP-02 closure: the previous "DIRECT `OUT 0xFF` TO DISABLE
    // is a latent gap" note here is now stale — Pass-12 fix-of-reviewer
    // wired port-0xFF write fan-out into `ula_int_disabled_` +
    // `video_timing_.set_interrupt_enable(...)` so all three writers to
    // `port_ff_reg(6)` (port-FF, NR 0x22, NR 0xC4) keep parity with the
    // VHDL-canonical store. The new row ULA-INT-V12-NMP-02 below
    // exercises the direct port-0xFF path end-to-end.
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

    // ULA-INT-V12-NMP-02 — direct OUT (0xFF),A bit 6 must fan out into
    // ula_int_disabled_ shadow + video_timing scheduler gate.
    //
    // VHDL: zxnext.vhd:3614-3616 (port_ff_wr branch latches the entire
    // CPU byte into port_ff_reg, INCLUDING bit 6); :3635
    // (port_ff_interrupt_disable <= port_ff_reg(6)); :6711 (ula_int_en
    // bit 0 = NOT port_ff_interrupt_disable). VHDL has THREE writers
    // feeding port_ff_reg(6): port-FF (full byte), NR 0x22 b2, NR 0xC4
    // b0 NOT. NR 0x22 + NR 0xC4 paths already mirrored the new value
    // into ula_int_disabled_ + video_timing_.set_interrupt_enable(); the
    // direct port-0xFF write was the missing third writer. Pre-V12-NMP-02
    // an `OUT (0xFF),0x40` set port_ff_reg_(6)=1 but left
    // ula_int_disabled_=false — NR 0xC4 read bit 0 was the stale shadow
    // (1, "enabled") instead of the live store (0, "disabled").
    //
    // Discriminative scenario:
    //   1. fresh(emu): ula_int_disabled_=false, port_ff_reg_(6)=0,
    //      NR 0xC4 read bit 0 = 1 (enabled).
    //   2. OUT (0xFF),0x40: port_ff_reg_(6)<=1, ula_int_disabled_<=true.
    //      NR 0xC4 read bit 0 must now be 0 (disabled).
    //   3. OUT (0xFF),0x00: port_ff_reg_(6)<=0, ula_int_disabled_<=false.
    //      NR 0xC4 read bit 0 must be 1 (re-enabled).
    //
    // Pre-fix step 2 returns bit 0 = 1 (stale shadow); step 3 returns
    // bit 0 = 1 also (shadow never moved). Post-fix the readback follows
    // the VHDL contract.
    {
        fresh(emu);
        const uint8_t c4_initial = nr_read(emu, 0xC4);
        emu.port().out(0x00FF, 0x40);            // disable via direct port write
        const uint8_t c4_after_dis = nr_read(emu, 0xC4);
        emu.port().out(0x00FF, 0x00);            // re-enable via direct port write
        const uint8_t c4_after_en  = nr_read(emu, 0xC4);
        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "initial=0x%02X after_OUT_FF_40=0x%02X after_OUT_FF_00=0x%02X "
                      "(bit 0 should follow 1,0,1)",
                      c4_initial, c4_after_dis, c4_after_en);
        check("ULA-INT-V12-NMP-02",
              "OUT (0xFF) bit 6 fans into ula_int_disabled_ shadow / scheduler — "
              "NR 0xC4 read bit 0 follows live port_ff_reg(6) for direct port-0xFF "
              "writes [zxnext.vhd:3614-3616, :3635, :6711, :6239]",
              (c4_initial & 0x01) != 0
                && (c4_after_dis & 0x01) == 0
                && (c4_after_en & 0x01) != 0,
              detail);
    }

    // ULA-INT-V12-NMP-02b — direct OUT (0xFF),A bit 6 must suppress the
    // scheduled ULA interrupt for the upcoming frame (the ula_int_disabled_
    // shadow is consumed by run_frame()'s ULA-INT scheduling gate at
    // emulator.cpp:1989 — same observable as ULA-INT-02 but exercising the
    // direct port-0xFF write path rather than the NR-22 mirror).
    {
        fresh(emu);
        emu.port().out(0x00FF, 0x40);            // bit 6 set → disable ULA INT
        emu.run_frame();
        const uint8_t c8 = nr_read(emu, 0xC8);
        check("ULA-INT-V12-NMP-02b",
              "OUT (0xFF),0x40 suppresses scheduled ULA INT — NR 0xC8 bit 0 stays clear "
              "[zxnext.vhd:3614-3616, :6711; emulator.cpp:1989 gate]",
              (c8 & 0x01) == 0,
              "NR 0xC8=" + hex2(c8) + " (expected bit 0 clear after OUT FF,40)");
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
    // Pass-12 V12-NMP-01 update (2026-05-10): pre-fix the C++ NR 0xC4 write
    // updated `port_ff_reg_(6)` but NOT the `ula_int_disabled_` shadow that
    // the read handler (emulator.cpp:2417) consults for bit 0. The VHDL
    // chain is: NR 0xC4 b0 → port_ff_reg(6) <= NOT b0 → port_ff_interrupt_disable
    // <= port_ff_reg(6) → ula_int_en(0) <= NOT port_ff_interrupt_disable. So
    // a write of NR 0xC4 = 0x82 (b0=0) puts port_ff_reg(6)=1 →
    // port_ff_interrupt_disable=1 → ula_int_en(0)=0. The readback bit 0
    // MUST be 0, giving 0x82 — NOT 0x83. The pre-fix test expected 0x83
    // because the buggy C++ left `ula_int_disabled_` at its default false
    // (set during reset_machine), making readback bit 0 = !false = 1.
    // V12-NMP-01 syncs the shadow on every NR 0xC4 write and the readback
    // now correctly returns 0x82.
    //
    // Exercise: write expbus=1 (bit 7) + line=1 (bit 1) + ULA disable
    // (bit 0=0). Ensure readback returns the VHDL-faithful 0x82.
    {
        fresh(emu);
        nr_write(emu, 0xC4, 0x82);              // expbus=1 + line=1 + ula b0=0
        const uint8_t got = nr_read(emu, 0xC4);
        // VHDL-faithful expected: E_00000_UU = 1_00000_10 = 0x82.
        // (line=1 bit 1, ula=0 bit 0 because b0=0 → port_ff_reg(6)=1
        //  → port_ff_interrupt_disable=1 → ula_int_en(0)=0.)
        check("NR-C4-03",
              "NR 0xC4 readback format E_00000_UU (expbus, 5x zero, line, ula) "
              "[zxnext.vhd:6239 / :3621-3622 / :3635 / :6711; emulator.cpp:796-804]",
              got == 0x82, detail_eq(got, 0x82));
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

// ══════════════════════════════════════════════════════════════════════
// Section IM2-Decoder-Gaps — G87 / G88 closed; G89 deferred; G90 re-homed
// ══════════════════════════════════════════════════════════════════════

// Park the CPU at a deterministic address with a freshly written program
// so single-instruction execution covers the bytes we care about. Returns
// the chosen entry-point address.
static uint16_t park_cpu_with_program(Emulator& emu, uint16_t addr,
                                      std::initializer_list<uint8_t> bytes) {
    uint16_t a = addr;
    for (uint8_t b : bytes) {
        emu.mmu().write(a++, b);
    }
    auto regs = emu.cpu().get_registers();
    regs.PC   = addr;
    regs.SP   = 0xFFFE;
    regs.IFF1 = 0; regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
    return addr;
}

static void test_im2_decoder_gaps(Emulator& emu) {
    set_group("IM2-Decoder-Gaps");

    // ── IM2C-G87-01 — RETI (ED 4D) advances the IM2 decoder FSM.
    //    VHDL im2_control.vhd:158-209 + 234. Each fetched M1 byte
    //    advances the FSM; RETI = ED 4D pulses o_reti_seen on the
    //    transition into S_ED4D_T4. Pre-G87 jnext only delivered the
    //    ED prefix to on_m1_cycle, starving the FSM.
    {
        fresh(emu);
        // Place ED 4D NOP at 0xC000. NMI return-stack target = 0xFFFE so
        // RETI's POP-from-stack pops "previous" PC. We only care that the
        // M1 fetches deliver ED + 4D to on_m1_cycle.
        // Pre-load stack with a benign target (0xC010 = NOP territory)
        emu.mmu().write(0xFFFE, 0x10);
        emu.mmu().write(0xFFFF, 0xC0);
        // 0xC010..0xC01F: NOPs, in case execution flows there.
        for (int i = 0; i < 0x10; ++i) emu.mmu().write(0xC010 + i, 0x00);
        const uint32_t pre_reti = emu.im2().reti_seen_count();
        const uint32_t pre_retn = emu.im2().retn_seen_count();
        park_cpu_with_program(emu, 0xC000, {0xED, 0x4D});  // RETI
        emu.cpu().execute();
        const uint32_t post_reti = emu.im2().reti_seen_count();
        const uint32_t post_retn = emu.im2().retn_seen_count();
        const bool ok = (post_reti == pre_reti + 1)
                     && (post_retn == pre_retn);
        check("IM2C-G87-01",
              "RETI (ED 4D) advances IM2 FSM and pulses o_reti_seen "
              "[VHDL im2_control.vhd:158-209,234]",
              ok,
              "reti_count " + std::to_string(pre_reti) + "->" + std::to_string(post_reti)
              + " retn_count " + std::to_string(pre_retn) + "->" + std::to_string(post_retn));
    }

    // ── IM2C-G87-02 — RETN (ED 45) advances the IM2 decoder FSM.
    //    VHDL im2_control.vhd:233-238. RETN pulses o_retn_seen on the
    //    transition into S_ED45_T4. Same delivery path as G87-01.
    {
        fresh(emu);
        emu.mmu().write(0xFFFE, 0x10);
        emu.mmu().write(0xFFFF, 0xC0);
        for (int i = 0; i < 0x10; ++i) emu.mmu().write(0xC010 + i, 0x00);
        const uint32_t pre_reti = emu.im2().reti_seen_count();
        const uint32_t pre_retn = emu.im2().retn_seen_count();
        park_cpu_with_program(emu, 0xC000, {0xED, 0x45});  // RETN
        emu.cpu().execute();
        const uint32_t post_reti = emu.im2().reti_seen_count();
        const uint32_t post_retn = emu.im2().retn_seen_count();
        const bool ok = (post_retn == pre_retn + 1)
                     && (post_reti == pre_reti);
        check("IM2C-G87-02",
              "RETN (ED 45) advances IM2 FSM and pulses o_retn_seen "
              "[VHDL im2_control.vhd:233-238]",
              ok,
              "reti_count " + std::to_string(pre_reti) + "->" + std::to_string(post_reti)
              + " retn_count " + std::to_string(pre_retn) + "->" + std::to_string(post_retn));
    }

    // ── NR-C2-01 / NR-C3-01 — NMI return-address shadow latch.
    //    VHDL zxnext.vhd:2050-2085, 6232-6236. At NMIACK_LSB/MSB the
    //    pushed PC is mirrored into NR 0xC2/0xC3. jnext: Z80Cpu fires
    //    on_nmi_servicing(saved_pc); Emulator forwards to
    //    NextReg::set_nmi_return_address(pc).
    {
        fresh(emu);
        // Park CPU at 0x1234 (a known PC value, lower 8K is overlaid by
        // ROM but the NMI-time PC we capture is whatever PC is *before*
        // fuse_z80_nmi() rewrites it to 0x0066). Place a NOP so the CPU
        // has something to execute next; SP must point to writable RAM
        // for the NMI push.
        // Use 0xC000 as the parked-PC + NOP target so any post-NMI flow
        // is benign.
        emu.mmu().write(0xC000, 0x00);  // NOP
        auto regs = emu.cpu().get_registers();
        regs.PC   = 0xC000;
        regs.SP   = 0xFFFE;
        regs.IFF1 = 0; regs.IFF2 = 0;
        regs.halted = false;
        emu.cpu().set_registers(regs);
        emu.cpu().request_nmi();
        emu.cpu().execute();   // services the NMI: PC -> 0x0066, NR 0xC2/0xC3 latched

        const uint8_t c2 = nr_read(emu, 0xC2);
        const uint8_t c3 = nr_read(emu, 0xC3);
        check("NR-C2-01",
              "NR 0xC2 mirrors NMI return-address LSB after Z80 services /NMI "
              "[VHDL zxnext.vhd:2050-2085,6232]",
              c2 == 0x00,
              "got NR 0xC2 = " + hex2(c2) + " expected 0x00 (LSB of 0xC000)");
        check("NR-C3-01",
              "NR 0xC3 mirrors NMI return-address MSB after Z80 services /NMI "
              "[VHDL zxnext.vhd:2050-2085,6236]",
              c3 == 0xC0,
              "got NR 0xC3 = " + hex2(c3) + " expected 0xC0 (MSB of 0xC000)");
    }

    // ── PULSE-G89-01..04 — LDIRX/LDDRX/LDPIRX/LDIRSCALE per-iteration step
    //    + PC-rewind shape. Pre-G89, the four Z80N repeating block-move ops
    //    ran their entire BC loop atomically inside one Z80Cpu::execute()
    //    call (up to 65536 iterations ≈ 244 ms wall time at 14 MHz),
    //    silently dropping any frame INT that became pending mid-loop. Post-
    //    G89 each call performs ONE iteration and rewinds PC by 2 if BC!=0
    //    so the next execute() re-fetches the opcode — and gets a chance to
    //    take any pending INT first (z80_cpu.cpp samples INT at the top of
    //    execute()). Mirrors VHDL t80n_mcode.vhd MCycles="100" + standard
    //    Z80 LDIR repeat shape.

    // ── PULSE-G89-01 — LDIRX (ED B4) per-iter step + rewind.
    //    BC=3, A=0xFF (no transparent skips), source 0xC100..0xC102 = 0x10,
    //    0x20, 0x30 → dest 0xC200..0xC202.
    {
        fresh(emu);
        for (int i = 0; i < 3; ++i) emu.mmu().write(0xC100 + i, 0x10 * (i + 1));
        for (int i = 0; i < 3; ++i) emu.mmu().write(0xC200 + i, 0x00);
        park_cpu_with_program(emu, 0xC000, {0xED, 0xB4});  // LDIRX
        auto regs = emu.cpu().get_registers();
        regs.AF = 0xFF00; regs.HL = 0xC100; regs.DE = 0xC200; regs.BC = 0x0003;
        emu.cpu().set_registers(regs);

        emu.cpu().execute();  // iteration 1
        regs = emu.cpu().get_registers();
        bool ok1 = (regs.BC == 0x0002) && (regs.PC == 0xC000)
                && (regs.HL == 0xC101) && (regs.DE == 0xC201);

        emu.cpu().execute();  // iteration 2
        regs = emu.cpu().get_registers();
        bool ok2 = (regs.BC == 0x0001) && (regs.PC == 0xC000)
                && (regs.HL == 0xC102) && (regs.DE == 0xC202);

        emu.cpu().execute();  // iteration 3 (final, no rewind)
        regs = emu.cpu().get_registers();
        bool ok3 = (regs.BC == 0x0000) && (regs.PC == 0xC002)
                && (regs.HL == 0xC103) && (regs.DE == 0xC203);

        bool mem_ok = (emu.mmu().read(0xC200) == 0x10)
                   && (emu.mmu().read(0xC201) == 0x20)
                   && (emu.mmu().read(0xC202) == 0x30);

        check("PULSE-G89-01",
              "LDIRX (ED B4) runs ONE iteration per execute() and rewinds "
              "PC by 2 if BC!=0 [VHDL t80n_mcode.vhd:2095-2138]",
              ok1 && ok2 && ok3 && mem_ok,
              "iter1=" + std::to_string(ok1) + " iter2=" + std::to_string(ok2)
              + " iter3=" + std::to_string(ok3) + " mem=" + std::to_string(mem_ok));
    }

    // ── PULSE-G89-02 — LDDRX (ED BC) per-iter step + rewind.
    //    BC=3, A=0xFF, source 0xC100,0xC0FF,0xC0FE walked downward,
    //    dest 0xC200..0xC202 walked upward (DE still increments per VHDL).
    {
        fresh(emu);
        emu.mmu().write(0xC100, 0xAA);
        emu.mmu().write(0xC0FF, 0xBB);
        emu.mmu().write(0xC0FE, 0xCC);
        for (int i = 0; i < 3; ++i) emu.mmu().write(0xC200 + i, 0x00);
        park_cpu_with_program(emu, 0xC000, {0xED, 0xBC});  // LDDRX
        auto regs = emu.cpu().get_registers();
        regs.AF = 0xFF00; regs.HL = 0xC100; regs.DE = 0xC200; regs.BC = 0x0003;
        emu.cpu().set_registers(regs);

        emu.cpu().execute();  // iteration 1
        regs = emu.cpu().get_registers();
        bool ok1 = (regs.BC == 0x0002) && (regs.PC == 0xC000)
                && (regs.HL == 0xC0FF) && (regs.DE == 0xC201);

        emu.cpu().execute();  // iteration 2
        regs = emu.cpu().get_registers();
        bool ok2 = (regs.BC == 0x0001) && (regs.PC == 0xC000)
                && (regs.HL == 0xC0FE) && (regs.DE == 0xC202);

        emu.cpu().execute();  // iteration 3
        regs = emu.cpu().get_registers();
        bool ok3 = (regs.BC == 0x0000) && (regs.PC == 0xC002)
                && (regs.HL == 0xC0FD) && (regs.DE == 0xC203);

        bool mem_ok = (emu.mmu().read(0xC200) == 0xAA)
                   && (emu.mmu().read(0xC201) == 0xBB)
                   && (emu.mmu().read(0xC202) == 0xCC);

        check("PULSE-G89-02",
              "LDDRX (ED BC) runs ONE iteration per execute() and rewinds "
              "PC by 2 if BC!=0 [VHDL t80n_mcode.vhd:2230-2256]",
              ok1 && ok2 && ok3 && mem_ok,
              "iter1=" + std::to_string(ok1) + " iter2=" + std::to_string(ok2)
              + " iter3=" + std::to_string(ok3) + " mem=" + std::to_string(mem_ok));
    }

    // ── PULSE-G89-03 — LDPIRX (ED B7) per-iter step + rewind.
    //    BC=3, A=0xFF, HL=0xC100 (pattern base, stays fixed),
    //    DE=0xC200..0xC202. Source addr each iter = (HL & 0xFFF8) | (E & 7).
    //    With HL=0xC100 (already 8-aligned) and E starting at 0x00, the
    //    three reads are from 0xC100, 0xC101, 0xC102.
    {
        fresh(emu);
        for (int i = 0; i < 3; ++i) emu.mmu().write(0xC100 + i, 0x40 + i);  // 0x40,0x41,0x42
        for (int i = 0; i < 3; ++i) emu.mmu().write(0xC200 + i, 0x00);
        park_cpu_with_program(emu, 0xC000, {0xED, 0xB7});  // LDPIRX
        auto regs = emu.cpu().get_registers();
        regs.AF = 0xFF00; regs.HL = 0xC100; regs.DE = 0xC200; regs.BC = 0x0003;
        emu.cpu().set_registers(regs);

        emu.cpu().execute();  // iteration 1
        regs = emu.cpu().get_registers();
        bool ok1 = (regs.BC == 0x0002) && (regs.PC == 0xC000)
                && (regs.HL == 0xC100) && (regs.DE == 0xC201);

        emu.cpu().execute();  // iteration 2
        regs = emu.cpu().get_registers();
        bool ok2 = (regs.BC == 0x0001) && (regs.PC == 0xC000)
                && (regs.HL == 0xC100) && (regs.DE == 0xC202);

        emu.cpu().execute();  // iteration 3
        regs = emu.cpu().get_registers();
        bool ok3 = (regs.BC == 0x0000) && (regs.PC == 0xC002)
                && (regs.HL == 0xC100) && (regs.DE == 0xC203);

        bool mem_ok = (emu.mmu().read(0xC200) == 0x40)
                   && (emu.mmu().read(0xC201) == 0x41)
                   && (emu.mmu().read(0xC202) == 0x42);

        check("PULSE-G89-03",
              "LDPIRX (ED B7) runs ONE iteration per execute() and rewinds "
              "PC by 2 if BC!=0; HL stays fixed [VHDL t80n_mcode.vhd:1953-1991]",
              ok1 && ok2 && ok3 && mem_ok,
              "iter1=" + std::to_string(ok1) + " iter2=" + std::to_string(ok2)
              + " iter3=" + std::to_string(ok3) + " mem=" + std::to_string(mem_ok));
    }

    // ── PULSE-G89-04 — LDIRSCALE (ED B6) per-iter step + rewind.
    //    Same shape as LDIRX (HL++, DE++) — VHDL BC'/DE' alternate-register
    //    additions are commented out in the FPGA source.
    {
        fresh(emu);
        for (int i = 0; i < 3; ++i) emu.mmu().write(0xC100 + i, 0x70 + i);  // 0x70,0x71,0x72
        for (int i = 0; i < 3; ++i) emu.mmu().write(0xC200 + i, 0x00);
        park_cpu_with_program(emu, 0xC000, {0xED, 0xB6});  // LDIRSCALE
        auto regs = emu.cpu().get_registers();
        regs.AF = 0xFF00; regs.HL = 0xC100; regs.DE = 0xC200; regs.BC = 0x0003;
        emu.cpu().set_registers(regs);

        emu.cpu().execute();  // iteration 1
        regs = emu.cpu().get_registers();
        bool ok1 = (regs.BC == 0x0002) && (regs.PC == 0xC000)
                && (regs.HL == 0xC101) && (regs.DE == 0xC201);

        emu.cpu().execute();  // iteration 2
        regs = emu.cpu().get_registers();
        bool ok2 = (regs.BC == 0x0001) && (regs.PC == 0xC000)
                && (regs.HL == 0xC102) && (regs.DE == 0xC202);

        emu.cpu().execute();  // iteration 3
        regs = emu.cpu().get_registers();
        bool ok3 = (regs.BC == 0x0000) && (regs.PC == 0xC002)
                && (regs.HL == 0xC103) && (regs.DE == 0xC203);

        bool mem_ok = (emu.mmu().read(0xC200) == 0x70)
                   && (emu.mmu().read(0xC201) == 0x71)
                   && (emu.mmu().read(0xC202) == 0x72);

        check("PULSE-G89-04",
              "LDIRSCALE (ED B6) runs ONE iteration per execute() and rewinds "
              "PC by 2 if BC!=0 [VHDL t80n_mcode.vhd:2188-2226]",
              ok1 && ok2 && ok3 && mem_ok,
              "iter1=" + std::to_string(ok1) + " iter2=" + std::to_string(ok2)
              + " iter3=" + std::to_string(ok3) + " mem=" + std::to_string(mem_ok));
    }

    // ── PULSE-G89-INT — inter-iteration INT sampling: a frame INT raised
    //    mid-LDIRX is taken at the very next execute() (between iterations)
    //    instead of being silently dropped at the end of the loop. This is
    //    the actual user-visible fix: long block transfers no longer block
    //    IM2 music drivers / vblank schedulers.
    //
    //    Strategy: BC=10 LDIRX, IM=1, IFF1=1 — run one iteration (BC=9,
    //    PC=0xC000), then request_interrupt(0xFF). Next execute() must
    //    service the INT (push PC, jump to 0x0038, IFF1=0) and BC must
    //    remain 9 (no further iteration ran inside the same call).
    {
        fresh(emu);
        for (int i = 0; i < 10; ++i) emu.mmu().write(0xC100 + i, 0xA0 + i);
        for (int i = 0; i < 10; ++i) emu.mmu().write(0xC200 + i, 0x00);
        park_cpu_with_program(emu, 0xC000, {0xED, 0xB4});  // LDIRX
        auto regs = emu.cpu().get_registers();
        regs.AF = 0xFF00; regs.HL = 0xC100; regs.DE = 0xC200; regs.BC = 0x000A;
        regs.IFF1 = 1; regs.IFF2 = 1; regs.IM = 1;
        regs.SP = 0xFFFE;
        emu.cpu().set_registers(regs);

        emu.cpu().execute();  // iteration 1: BC 10->9, PC rewound to 0xC000
        regs = emu.cpu().get_registers();
        const bool iter1_ok = (regs.BC == 0x0009) && (regs.PC == 0xC000)
                           && (regs.IFF1 == 1);

        // Request a frame INT. With IFF1=1 and pulse just started, the next
        // execute() MUST service it before running another LDIRX iteration.
        emu.cpu().request_interrupt(0xFF);

        emu.cpu().execute();  // INT serviced first; LDIRX iteration NOT run
        regs = emu.cpu().get_registers();

        // INT serviced => IFF1=0, PC at IM 1 vector 0x0038, return PC pushed.
        const bool int_taken = (regs.IFF1 == 0) && (regs.PC == 0x0038);
        // Critical invariant: BC unchanged from iter1 — no further LDIRX
        // iteration ran inside this execute() call. This is the inter-iter
        // INT-sample property that pre-G89 violated by atomically running
        // all 10 iterations.
        const bool no_extra_iter = (regs.BC == 0x0009);
        // Return-stack should hold 0xC000 (the rewound PC where LDIRX would
        // resume after RETI).
        const uint16_t pushed_lo = emu.mmu().read(0xFFFC);
        const uint16_t pushed_hi = emu.mmu().read(0xFFFD);
        const uint16_t return_pc = (pushed_hi << 8) | pushed_lo;
        const bool return_pc_ok = (return_pc == 0xC000);

        check("PULSE-G89-INT",
              "LDIRX inter-iteration INT sampling: pending /INT serviced "
              "between iterations (BC unchanged across the INT) "
              "[VHDL t80n_mcode.vhd:2095-2138 + zxnext.vhd INT path]",
              iter1_ok && int_taken && no_extra_iter && return_pc_ok,
              "iter1=" + std::to_string(iter1_ok)
              + " int_taken=" + std::to_string(int_taken)
              + " BC=0x" + hex2(regs.BC & 0xFF) + " (low) PC=0x"
              + hex2((regs.PC >> 8) & 0xFF) + hex2(regs.PC & 0xFF)
              + " return_pc_ok=" + std::to_string(return_pc_ok));
    }

    // RE-HOME PULSE-G90-01 → contention plan (NEW-CONT-3): 28 MHz SRAM-read
    //   wait state (VHDL zxnext.vhd:3171-3181) is a contention/timing concern,
    //   not interrupt routing. ctc_interrupts_test scope ends at the IM2 +
    //   NMI fabric; cpu_speed=11 SRAM stalls belong to ContentionModel.
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

    test_im2_decoder_gaps(emu);
    std::printf("  Group: IM2-Decoder-Gaps — done\n");

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
