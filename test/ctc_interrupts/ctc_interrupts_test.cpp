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
#include "core/saveable.h"
#include "cpu/im2.h"
#include "peripheral/ctc.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
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
              // :5297 is the NR 0x22 write case (`when X"22" =>
              // nr_22_line_interrupt_en <= nr_wr_dat(1)`), which is what this
              // row writes. It used to cite :5607-5610, the NR 0xC4 write case
              // — a different register that happens to set the same signal
              // (GH #151). :6239 stays: it is the NR 0xC4 READ this row checks.
              "NR 0x22 bit 1 drives line_interrupt_en, visible on NR 0xC4 read bit 1 "
              "[zxnext.vhd:5297, :6239; emulator.cpp:542-546, :801]",
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

    // ULA-INT-V19-IM2-01 — NR 0x22 bit 1 must propagate to IM2 fabric LINE
    // int_en, not just to the line-int generation gate.
    //
    // VHDL: zxnext.vhd:5297 — `nr_22_we and nr_22 bit 1 → nr_22_line_interrupt_en`.
    // Same flip-flop is also written by NR 0xC4 bit 1 (line :5610). The
    // flip-flop feeds `im2_int_en[0]` (= LINE i_int_en, line :1949-1950 +
    // :6711 ula_int_en(1)). Pre-V19 jnext only updated
    // `video_timing_.set_line_interrupt_enable()` (the line-int generation
    // gate), but the IM2 fabric's `dev_[DevIdx::LINE].int_en` stayed false
    // — so in IM2 mode a LINE raise_req() set int_status but NOT
    // im2_int_req (required edge AND int_en); state stayed S_0; the IM2
    // daisy chain never asserted /INT to the Z80.
    //
    // Discriminative check: enter IM2 mode, write NR 0x22 bit 1 = 1 (the
    // ONLY enabler — do NOT touch NR 0xC4), raise_req(LINE), tick. The
    // device must reach S_REQ. Pre-fix: stays at S_0; int_line_asserted=false.
    //
    // Note: do NOT call emu.im2().reset() — fresh(emu) → init() already
    // initialises the fabric (V19-IM2-01/02 init sets ULA int_en=1, LINE
    // int_en=0). A bare im2().reset() would wipe that.
    {
        fresh(emu);
        emu.im2().set_mode(true);   // IM2 mode (NR 0xC0 bit 0)
        // V21-IM2-01 — feed ED 5E (IM 2) to the IM2-control decoder so
        // `im_mode_` becomes 2 (= VHDL `i_im2_mode='1'`). Pre-V21 the
        // int_line_asserted gate only checked `im2_mode_` (NR 0xC0 b0);
        // post-V21 the gate also requires `im_mode_ == 2` per VHDL
        // im2_device.vhd:150 (`o_int_n` gates on `i_im2_mode`).
        emu.im2().on_m1_cycle(0x0000, 0xED);
        emu.im2().on_m1_cycle(0x0001, 0x5E);
        // Write NR 0x22 with bit 1 = 1. This is the ONLY path enabling
        // LINE int_en in this scenario; NR 0xC4 is left at reset default.
        nr_write(emu, 0x22, 0x02);
        emu.im2().raise_req(Im2Controller::DevIdx::LINE);
        emu.im2().tick(1);
        const Im2Controller::DevState st = emu.im2().state(Im2Controller::DevIdx::LINE);
        const bool int_line = emu.im2().int_line_asserted();
        char detail[200];
        std::snprintf(detail, sizeof(detail),
                      "after NR 0x22<-0x02 + raise_req(LINE) + tick: state=%d "
                      "(post-fix S_REQ=1; pre-fix S_0=0); int_line=%d "
                      "(post-fix 1; pre-fix 0)",
                      static_cast<int>(st), int_line ? 1 : 0);
        check("ULA-INT-V19-IM2-01",
              "NR 0x22 bit 1 propagates to IM2 fabric dev_[LINE].int_en "
              "[zxnext.vhd:5297, :1950, :6711]",
              st == Im2Controller::DevState::S_REQ && int_line, detail);
    }

    // ULA-INT-V19-IM2-02 — port_ff_reg(6) must propagate to IM2 fabric ULA
    // int_en across all THREE writers (port-FF, NR 0x22 b2, NR 0xC4 b0-NOT).
    //
    // VHDL: zxnext.vhd:6711 — `ula_int_en(0) = NOT port_ff_interrupt_disable`,
    // = NOT port_ff_reg(6) (line :3635). That bit feeds `im2_int_en[11]`
    // (= ULA i_int_en, line :1949). At reset port_ff_reg(6)=0 so ULA
    // int_en should be 1. Pre-V19 jnext NEVER updated dev_[ULA].int_en
    // anywhere — every FRAME-INT raise_req(ULA) in IM2 mode set int_status
    // but NOT im2_int_req → daisy chain stayed in S_0 → /INT never asserted.
    //
    // Three sub-checks: a) reset state ULA int_en should be true via
    // raise_req+tick reaching S_REQ; b) NR 0x22 bit 2 = 1 disables ULA
    // int_en, raise_req+tick stays at S_0 (or drops back); c) NR 0xC4
    // bit 0 = 1 (NOT polarity) re-enables, raise_req+tick reaches S_REQ.
    {
        fresh(emu);
        emu.im2().set_mode(true);   // IM2 mode
        // (a) Reset default: port_ff_reg(6)=0, so ULA int_en=1. Verify
        // by raising ULA req and ticking — must reach S_REQ. Pre-fix:
        // dev_[ULA].int_en was never initialised to true (always false),
        // so the device stays S_0.
        emu.im2().raise_req(Im2Controller::DevIdx::ULA);
        emu.im2().tick(1);
        const Im2Controller::DevState a_st =
            emu.im2().state(Im2Controller::DevIdx::ULA);

        // (b) Disable via NR 0x22 bit 2.
        fresh(emu);
        emu.im2().set_mode(true);
        nr_write(emu, 0x22, 0x04);  // port_ff_reg(6) ← 1, ULA int_en = 0
        emu.im2().raise_req(Im2Controller::DevIdx::ULA);
        emu.im2().tick(1);
        const Im2Controller::DevState b_st =
            emu.im2().state(Im2Controller::DevIdx::ULA);

        // (c) Re-enable via NR 0xC4 bit 0 (NOT polarity: 1=enable).
        // Note: in this scenario start with NR 0x22 bit 2 = 1 (disabled),
        // then NR 0xC4 bit 0 = 1 to clear port_ff_reg(6) back to 0
        // (re-enable). Verify ULA reaches S_REQ.
        fresh(emu);
        emu.im2().set_mode(true);
        nr_write(emu, 0x22, 0x04);  // disable
        nr_write(emu, 0xC4, 0x01);  // bit 0 = 1 → port_ff_reg(6) = NOT 1 = 0 → enable
        emu.im2().raise_req(Im2Controller::DevIdx::ULA);
        emu.im2().tick(1);
        const Im2Controller::DevState c_st =
            emu.im2().state(Im2Controller::DevIdx::ULA);

        char detail[280];
        std::snprintf(detail, sizeof(detail),
                      "(a) reset+raise+tick: state=%d (post-fix S_REQ=1; pre-fix S_0=0); "
                      "(b) NR0x22<-0x04+raise+tick: state=%d (must be S_0=0); "
                      "(c) NR0x22<-0x04;NR0xC4<-0x01+raise+tick: state=%d "
                      "(post-fix S_REQ=1; pre-fix S_0=0)",
                      static_cast<int>(a_st), static_cast<int>(b_st),
                      static_cast<int>(c_st));
        check("ULA-INT-V19-IM2-02",
              "port_ff_reg(6) propagates to IM2 fabric dev_[ULA].int_en across "
              "all 3 writers (port-FF, NR 0x22 b2, NR 0xC4 b0 NOT) "
              "[zxnext.vhd:3614-3622, :3635, :6711, :1949]",
              a_st == Im2Controller::DevState::S_REQ
                  && b_st == Im2Controller::DevState::S_0
                  && c_st == Im2Controller::DevState::S_REQ,
              detail);
    }

    // ULA-INT-V19-IM2-02-PORTFF — direct OUT (0xFF),A path also fans into
    // IM2 fabric ULA int_en. Same VHDL writer set (zxnext.vhd:3614-3616
    // port_ff_wr branch latches the entire byte, so bit 6 maps directly).
    {
        fresh(emu);
        emu.im2().set_mode(true);
        // Verify reset state: ULA int_en is enabled (bit 6 = 0).
        emu.port().out(0x00FF, 0x40);  // disable ULA INT via direct port-FF
        emu.im2().raise_req(Im2Controller::DevIdx::ULA);
        emu.im2().tick(1);
        const Im2Controller::DevState st_dis =
            emu.im2().state(Im2Controller::DevIdx::ULA);

        fresh(emu);
        emu.im2().set_mode(true);
        emu.port().out(0x00FF, 0x40);  // disable
        emu.port().out(0x00FF, 0x00);  // re-enable
        emu.im2().raise_req(Im2Controller::DevIdx::ULA);
        emu.im2().tick(1);
        const Im2Controller::DevState st_en =
            emu.im2().state(Im2Controller::DevIdx::ULA);

        char detail[180];
        std::snprintf(detail, sizeof(detail),
                      "after OUT 0xFF,0x40 (disable): state=%d (must be S_0=0); "
                      "after OUT 0xFF,0x40 then 0xFF,0x00 (re-enable): state=%d "
                      "(post-fix S_REQ=1; pre-fix S_0=0)",
                      static_cast<int>(st_dis), static_cast<int>(st_en));
        check("ULA-INT-V19-IM2-02-PORTFF",
              "Direct OUT (0xFF),A bit 6 fans into IM2 fabric dev_[ULA].int_en "
              "[zxnext.vhd:3614-3616, :3635, :6711, :1949]",
              st_dis == Im2Controller::DevState::S_0
                  && st_en == Im2Controller::DevState::S_REQ,
              detail);
    }

    // CTC-INT-V20-IM2-01 — Pulse-mode CTC INT must drive CPU /INT.
    //
    // VHDL: zxnext.vhd:1840 — `z80_int_n <= ((pulse_int_n AND im2_int_n)
    // OR NOT expbus_disable_int) AND ...`. In the default scenario
    // (expbus_disable_int='1'), this reduces to `pulse_int_n AND
    // im2_int_n`. When CTC ZC/TO fires in pulse mode (NR 0xC0 bit 0=0,
    // the power-on default), the IM2 fabric drops pulse_int_n via
    // im2_peripheral.vhd:186-194's o_pulse_en for non-exception
    // devices. The Z80 /INT pin should be asserted.
    //
    // Pre-V20-IM2-01 jnext only called `cpu_.request_interrupt(0xFF)`
    // from the ULA frame-INT and LINE-INT scheduler callbacks
    // (emulator.cpp:5445, 6655). CTC's `on_interrupt` (line 4668) and
    // UART's TX/RX hooks (4717/4722) ONLY routed through
    // `im2_.raise_req(DevIdx)` — the fabric's pulse_int_n correctly
    // dropped, but NO code notified the CPU. Result: in pulse mode
    // (the default!), CTC ZC/TO and UART interrupts were SILENTLY
    // DROPPED — the daisy chain advanced in the fabric, but the Z80
    // /INT pin was never asserted, so the CPU never serviced the ISR.
    //
    // Discriminative test: pulse mode (don't write NR 0xC0; default
    // is im2_mode=0), enable CTC0 int_en via NR 0xC5 bit 0, IFF1=1 +
    // IM=1, fire CTC0 via emu.im2().raise_req(CTC0) bypassing the
    // CTC peripheral timing (simulates a ZC/TO at frame start). Run a
    // few instructions. Pre-fix: PC stays at the parked address.
    // Post-fix: pulse_int_n drops → poll fires request_interrupt →
    // CPU accepts → IM1 vector → PC=0x0038.
    {
        fresh(emu);
        // Pulse mode is the default (NR 0xC0 bit 0 = 0).
        // DISABLE ULA frame INT so it doesn't trigger /INT independently
        // of our CTC fixture — that's the existing legacy path and would
        // mask the discriminative observation. NR 0x22 bit 2 = 1 sets
        // port_ff_reg(6) = 1 (port_ff_interrupt_disable=1), suppressing
        // the ULA INT scheduler arm (emulator.cpp:5439 `ula_int_disabled_`
        // gate). LINE int is OFF by default.
        nr_write(emu, 0x22, 0x04);
        // Enable CTC0 int_en via NR 0xC5 bit 0.
        nr_write(emu, 0xC5, 0x01);
        // Configure CPU: IFF1=1, IM=1 (accept INT, jump to 0x0038).
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM = 1;
        regs.PC = 0x8000;  // park PC in user RAM (NOPs)
        regs.SP = 0xFFFE;
        emu.cpu().set_registers(regs);
        // Snapshot pulse_int_n state BEFORE the raise — it must be high
        // (idle), otherwise the test setup is wrong.
        const bool pulse_before = emu.im2().pulse_int_n();
        // Fire CTC0 via the fabric (simulates CTC peripheral on_interrupt
        // callback). This is the SAME entry point ctc_.on_interrupt
        // would use; the fix is whether subsequent run_frame notifies
        // the CPU.
        emu.im2().raise_req(Im2Controller::DevIdx::CTC0);
        emu.run_frame();
        // Post-fix: at some point during the frame, the pulse_int_n
        // poll fires request_interrupt(0xFF); the CPU accepts in IM=1
        // and jumps to 0x0038. PC ends up in low-mem ROM territory.
        // Pre-fix: PC stays in the 0x8000..0xFFFE range executing NOPs,
        // never reaching 0x0038 — because no code wires CTC's
        // raise_req → cpu_.request_interrupt in pulse mode.
        const auto post_regs = emu.cpu().get_registers();
        // Strict discriminative threshold: post-fix PC must be in ROM
        // (< 0x4000). Pre-fix PC stays at 0x8000+ (RAM NOP territory).
        const bool int_was_accepted = (post_regs.PC < 0x4000);
        char detail[200];
        std::snprintf(detail, sizeof(detail),
                      "pulse_before_raise=%d (must be 1); "
                      "post-run_frame PC=0x%04X (post-fix: PC < 0x4000 "
                      "after IM1 vector at 0x0038; pre-fix: PC stays "
                      "in 0x8000+ RAM, CTC INT never reached CPU "
                      "in pulse mode)",
                      pulse_before ? 1 : 0, post_regs.PC);
        check("CTC-INT-V20-IM2-01",
              "Pulse-mode CTC INT drives CPU /INT via pulse_int_n poll "
              "[zxnext.vhd:1840 z80_int_n composition; "
              "im2_peripheral.vhd:186 o_pulse_en for non-exception devices]",
              pulse_before && int_was_accepted, detail);
    }

    // V20R-CPU-NIT-01-PREV-PULSE-PERSIST — `prev_pulse_int_n_` (the
    // Pass-20 falling-edge shadow at `emulator.cpp` line ~5791) must
    // round-trip through `Emulator::save_state()` / `load_state()`.
    //
    // Reviewer's class-(c) diagnosis: pre-fix the shadow was net-new
    // Pass-20 state and was not added to the save/load schema.
    // Im2Controller::save_state() already persists `pulse_int_n_`;
    // without the matching companion slot for the Emulator-level
    // shadow, a snapshot taken mid-pulse (cur=0) restored fresh would
    // leave the shadow at its `init()` / `reset()` default `true`.
    // The next-tick V20 poll would then see `!cur && prev` = falling
    // edge and fire a SPURIOUS `cpu_.request_interrupt(0xFF)` —
    // harmless in practice (the 32/36T drop arm in Z80Cpu::execute()
    // cleans up the phantom INT within one pulse window) but a
    // class-(c) divergence of the V20 fix's "exactly ONCE per pulse"
    // invariant. Filed as V20R-CPU-NIT-01.
    //
    // Discriminative test:
    //   1) Build emulator A. Drive `prev_pulse_int_n_` to FALSE via
    //      the test-only setter (so we don't depend on the precise
    //      tick window where the V20 poll happens to fire mid-frame).
    //   2) Save_state. The append-only V20R-CPU-NIT-01 slot writes the
    //      shadow (false) at the end of the stream.
    //   3) Build emulator B (fresh init → `prev_pulse_int_n_=true`,
    //      matching the reset default).
    //   4) Load_state into B.
    //   5) Sandwich-verify: B's accessor must return FALSE — meaning
    //      the slot round-tripped. Pre-fix (no slot in save/load):
    //      B's accessor stays TRUE (the reset default), and the test
    //      FAILs (we never wrote the slot so we never read it back).
    {
        Emulator emu_save;
        if (!build_next_emulator(emu_save)) {
            check("V20R-CPU-NIT-01-PREV-PULSE-PERSIST",
                  "emu_save construction failed",
                  false, "");
        } else {
            // Drive the shadow to a non-default value.
            emu_save.set_prev_pulse_int_n_for_test(false);
            // Sanity: the setter took effect.
            const bool pre_save_shadow = emu_save.prev_pulse_int_n_for_test();

            // Measure snapshot size and serialise.
            StateWriter measure;
            emu_save.save_state(measure);
            const size_t snap_size = measure.position();
            std::vector<uint8_t> buf(snap_size, 0);
            StateWriter w(buf.data(), snap_size);
            emu_save.save_state(w);

            // Fresh emulator → shadow is the reset default (true).
            Emulator emu_load;
            (void)build_next_emulator(emu_load);
            const bool pre_load_shadow = emu_load.prev_pulse_int_n_for_test();

            // Load. The append-only V20R-CPU-NIT-01 slot (after
            // `nr_02_bus_reset_`) restores the shadow.
            StateReader r(buf.data(), snap_size);
            emu_load.load_state(r);
            const bool post_load_shadow = emu_load.prev_pulse_int_n_for_test();

            char detail[240];
            std::snprintf(detail, sizeof(detail),
                          "emu_save pre-save shadow=%d (must be 0 after setter); "
                          "emu_load pre-load shadow=%d (must be 1 = reset default); "
                          "emu_load post-load shadow=%d "
                          "(post-fix: 0 — slot round-trips; "
                          "pre-fix: 1 — slot absent, accessor still reset default)",
                          pre_save_shadow ? 1 : 0,
                          pre_load_shadow ? 1 : 0,
                          post_load_shadow ? 1 : 0);
            check("V20R-CPU-NIT-01-PREV-PULSE-PERSIST",
                  "Emulator::prev_pulse_int_n_ round-trips through save_state/load_state "
                  "[reviewer V20R-CPU-NIT-01; pre-fix slot absent → load restores "
                  "to reset default `true` and the next-tick V20 poll spuriously fires]",
                  pre_save_shadow == false
                      && pre_load_shadow == true
                      && post_load_shadow == false,
                  detail);
        }
    }

    // V20R-CPU-NIT-02-NO-DOUBLE-STAMP — exactly ONE
    // `cpu_.request_interrupt(0xFF)` per ULA frame-INT pulse.
    //
    // Reviewer's class-(c) finding: the legacy scheduler-callback
    // `cpu_.request_interrupt(0xFF)` (at the FRAME-INT scheduler
    // emulator.cpp:5443+ and the LINE-INT scheduler
    // `reschedule_line_interrupt()` ~:6716) AND the V20 falling-edge
    // poll (~:5791) BOTH stamped the same pulse → `int_requested_at_`
    // was re-stamped at a LATER tstate than the original callback fire.
    // Harmless for boot-realistic ISRs (32/36-cycle window expires
    // before any EI), but a latent double-INT trap if an ISR did fast
    // EI within the re-stamped window. Filed as V20R-CPU-NIT-02.
    //
    // Option A fix (reviewer recommended): drop the legacy
    // scheduler-callback `cpu_.request_interrupt(0xFF)` and let the
    // V20 falling-edge poll be the sole driver of pulse-mode /INT —
    // VHDL-faithful and symmetric with the V19 IM2-mode poll.
    //
    // Discriminative test: fresh emulator + pulse mode (NR 0xC0 b0=0
    // default) + ULA INT enabled (port_ff_reg(6)=0 default) + IFF1=1 +
    // IM=1 + PC parked in RAM. Reset request_interrupt counter.
    // Run one frame. The FRAME-INT scheduler fires once → raise_req(ULA)
    // → next instruction's im2_.tick step_pulse drops pulse_int_n=false
    // → V20 poll fires falling-edge → exactly ONE
    // request_interrupt(0xFF). The pulse window expires at +32/+36T
    // and prev_pulse_int_n_ tracks back to true; no re-fire until next
    // frame. So `request_interrupt_count()` MUST be exactly 1.
    //
    // Post-fix: count == 1.
    // Pre-fix Option A (callback re-added): count == 2 (callback + poll).
    {
        fresh(emu);
        // Configure CPU: IFF1=1, IM=1, PC in NOP RAM.
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM = 1;
        regs.PC = 0x8000;
        regs.SP = 0xFFFE;
        emu.cpu().set_registers(regs);
        // Disable LINE INT so only the ULA FRAME-INT fires (otherwise
        // both raise + their respective poll would each contribute,
        // muddying the count). Default NR 0x22 has line_interrupt_en=0,
        // so this is implicit — we explicitly write 0x00 for clarity.
        nr_write(emu, 0x22, 0x00);
        // Reset the request_interrupt counter AFTER setting up the
        // emulator (init may have called request_interrupt during
        // boot-state setup; we only care about the frame we're about
        // to run).
        emu.cpu().reset_request_interrupt_count();
        emu.run_frame();
        const uint32_t cnt = emu.cpu().request_interrupt_count();
        char detail[200];
        std::snprintf(detail, sizeof(detail),
                      "request_interrupt_count after 1 frame "
                      "(pulse mode + ULA INT enabled) = %u "
                      "(post-fix: 1 — V20 poll is sole driver; "
                      "pre-fix: 2 — legacy callback + V20 poll BOTH "
                      "stamp the same pulse)",
                      cnt);
        check("V20R-CPU-NIT-02-NO-DOUBLE-STAMP",
              "Exactly one CPU /INT stamp per pulse-mode ULA frame-INT pulse "
              "[reviewer V20R-CPU-NIT-02; pre-fix legacy scheduler callback "
              "+ V20 falling-edge poll both stamped → latent fast-EI double-INT]",
              cnt == 1, detail);
    }

    // ULA-INT-V19-IM2-04 — IM2 fabric int_line_asserted() must drive the
    // CPU /INT pin in IM2 mode.
    //
    // VHDL: zxnext.vhd:1840 — `z80_int_n <= ((pulse_int_n AND im2_int_n)
    // OR NOT expbus_disable_int) AND ...`. Either pulse_int_n OR im2_int_n
    // pulled low asserts /INT. Pre-V19 jnext only called
    // cpu_.request_interrupt(0xFF) for the legacy pulse-mode path; in
    // IM2 mode the comment at the FRAME-INT scheduler said the fabric's
    // int_line_asserted() would drive the Z80 INT, but no code ever READ
    // it. Result in IM2 mode: the daisy chain reached S_REQ but the CPU
    // never saw the request — int_pending_ stayed false, on_int_ack was
    // never invoked, the IM2 priority chain remained latched forever.
    //
    // Discriminative test: configure IM2 mode + IFF1=1 + IM=2; run_frame.
    // The frame-int scheduler raises ULA. With the V19-IM2-04 polling
    // hook (after im2_.tick), int_pending_ is set; on the next CPU
    // instruction the interrupt is accepted via on_int_ack →
    // ack_vector(), advancing ULA from S_REQ → S_ACK. The next tick
    // advances S_ACK → S_ISR. So ULA's state should be >= 2 (S_ACK or
    // S_ISR) after run_frame. Pre-fix: ULA stays at S_REQ (state=1)
    // forever because the CPU never sees /INT.
    {
        fresh(emu);
        // Set IM2 mode via NR 0xC0 bit 0.
        nr_write(emu, 0xC0, 0x01);
        // V21-IM2-01 — pre-feed the IM2-control decoder with an ED 5E
        // (IM 2) so its `im_mode_` shadow becomes 2 (= VHDL
        // `i_im2_mode='1'`). The Z80 register `regs.IM=2` below is the
        // FUSE-side bit; the IM2 controller's separate decoder is
        // driven by on_m1_cycle from the CPU's M1 callback. In a real
        // boot the supervisor executes ED 5E itself which updates both
        // sides; this test bypasses the FUSE Z80 to keep the setup
        // minimal, so feed the decoder directly.
        emu.im2().on_m1_cycle(0x0000, 0xED);
        emu.im2().on_m1_cycle(0x0001, 0x5E);
        // Set IFF1=1 + IM=2 so the CPU will accept interrupts in IM2.
        // (Z80Cpu reset clears IFF1; this is the minimal setup to make
        // the test exercise the IntAck path without requiring EI/IM2
        // instructions in RAM.)
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM = 2;
        regs.PC = 0x8000;  // park PC in user RAM (NOPs)
        emu.cpu().set_registers(regs);
        emu.run_frame();
        // After run_frame, the ULA device should have progressed past
        // S_REQ (state=1) — at minimum to S_ACK (state=2) or S_ISR
        // (state=3). The frame-INT scheduler raises ULA early in the
        // frame, the polling fires request_interrupt, and the next
        // instruction does the IntAck.
        // Pre-fix: ULA stuck at S_REQ (state=1).
        // Post-fix: state >= 2.
        const Im2Controller::DevState ula_st =
            emu.im2().state(Im2Controller::DevIdx::ULA);
        char detail[200];
        std::snprintf(detail, sizeof(detail),
                      "ULA state after run_frame in IM2 mode + IFF1=1+IM=2: %d "
                      "(post-fix: >= 2 [S_ACK=2 or S_ISR=3]; "
                      "pre-fix: 1 [S_REQ stuck])",
                      static_cast<int>(ula_st));
        check("ULA-INT-V19-IM2-04",
              "IM2 fabric int_line_asserted() drives CPU /INT in IM2 mode "
              "[zxnext.vhd:1840 z80_int_n composition]",
              static_cast<int>(ula_st) >= 2, detail);
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

    // ── PULSE-G89-INT / -INT-02 / -INT-03 / -INT-04 — inter-iteration INT
    //    sampling, once per opcode. A frame INT raised mid-block must be
    //    taken at the very next execute() (between iterations) instead of
    //    being silently dropped at the end of the loop. This is the actual
    //    user-visible fix: long block transfers no longer block IM2 music
    //    drivers / vblank schedulers.
    //
    //    One probe, four opcodes (GH #201 review). PULSE-G89-01..04 above
    //    prove only the PC-rewind SHAPE; the INT sample is a separate claim,
    //    and until this review it was measured for LDIRX alone while the
    //    other three rested on the inference that they share the /INT check
    //    at the top of Z80Cpu::execute(). They do share it — but each opcode
    //    is a SEPARATELY hand-written case block in src/cpu/z80n_ext.cpp,
    //    not a shared helper, so a copy-paste divergence in one of them is
    //    not structurally excluded and the inference is not evidence. Each
    //    opcode now runs the stimulus for itself.
    //
    //    Strategy: BC=10, IM=1, IFF1=1 — run one iteration (BC=9, PC rewound
    //    to 0xC000), then request_interrupt(0xFF). The next execute() must
    //    service the INT (push PC, jump to the IM 1 vector 0x0038, IFF1=0)
    //    and BC must STILL be 9 — no further iteration ran inside that call.
    //    The pushed return address must be 0xC000, the rewound PC the block
    //    op resumes from after RETI.
    struct IntSampleProbe {
        bool     iter1_ok;
        bool     int_taken;
        bool     no_extra_iter;
        bool     return_pc_ok;
        uint16_t bc;
        uint16_t pc;
        uint16_t return_pc;
    };
    // Memory window wide enough for every addressing shape these four use:
    // LDIRX/LDIRSCALE walk HL up from 0xC100, LDDRX walks it down, LDPIRX
    // holds it fixed at (HL & 0xFFF8) | (E & 7). None of the fill bytes is
    // 0xFF, so with A=0xFF no iteration takes the transparency path.
    const auto int_sample_probe = [&emu](uint8_t opcode) -> IntSampleProbe {
        fresh(emu);
        for (int i = 0; i < 32; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xC0F0 + i),
                            static_cast<uint8_t>(0xA0 + (i & 0x1F)));
        for (int i = 0; i < 16; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xC200 + i), 0x00);
        park_cpu_with_program(emu, 0xC000, {0xED, opcode});
        auto regs = emu.cpu().get_registers();
        regs.AF = 0xFF00; regs.HL = 0xC100; regs.DE = 0xC200; regs.BC = 0x000A;
        regs.IFF1 = 1; regs.IFF2 = 1; regs.IM = 1;
        regs.SP = 0xFFFE;
        emu.cpu().set_registers(regs);

        emu.cpu().execute();  // iteration 1: BC 10->9, PC rewound to 0xC000
        regs = emu.cpu().get_registers();
        IntSampleProbe p{};
        p.iter1_ok = (regs.BC == 0x0009) && (regs.PC == 0xC000)
                  && (regs.IFF1 == 1);

        // Request a frame INT. With IFF1=1 and the pulse just started, the
        // next execute() MUST service it before running another iteration.
        emu.cpu().request_interrupt(0xFF);

        emu.cpu().execute();  // INT serviced first; the block op does NOT step
        regs = emu.cpu().get_registers();
        p.bc            = regs.BC;
        p.pc            = regs.PC;
        p.int_taken     = (regs.IFF1 == 0) && (regs.PC == 0x0038);
        p.no_extra_iter = (regs.BC == 0x0009);
        p.return_pc     = static_cast<uint16_t>(
                              (emu.mmu().read(0xFFFD) << 8) | emu.mmu().read(0xFFFC));
        p.return_pc_ok  = (p.return_pc == 0xC000);
        return p;
    };
    const auto probe_detail = [](const IntSampleProbe& p) {
        char buf[176];
        std::snprintf(buf, sizeof(buf),
                      "iter1=%d int_taken=%d BC=0x%04X (expect 0x0009) "
                      "PC=0x%04X (expect 0x0038) return_pc=0x%04X (expect 0xC000)",
                      static_cast<int>(p.iter1_ok), static_cast<int>(p.int_taken),
                      p.bc, p.pc, p.return_pc);
        return std::string(buf);
    };
    {
        const IntSampleProbe p = int_sample_probe(0xB4);
        check("PULSE-G89-INT",
              "LDIRX inter-iteration INT sampling: pending /INT serviced "
              "between iterations (BC unchanged across the INT) "
              "[VHDL t80n_mcode.vhd:2095-2138 + zxnext.vhd INT path]",
              p.iter1_ok && p.int_taken && p.no_extra_iter && p.return_pc_ok,
              probe_detail(p));
    }
    {
        const IntSampleProbe p = int_sample_probe(0xBC);
        check("PULSE-G89-INT-02",
              "LDDRX inter-iteration INT sampling: pending /INT serviced "
              "between iterations (BC unchanged across the INT) "
              "[VHDL t80n_mcode.vhd:2230-2256 + zxnext.vhd INT path]",
              p.iter1_ok && p.int_taken && p.no_extra_iter && p.return_pc_ok,
              probe_detail(p));
    }
    {
        const IntSampleProbe p = int_sample_probe(0xB7);
        check("PULSE-G89-INT-03",
              "LDPIRX inter-iteration INT sampling: pending /INT serviced "
              "between iterations (BC unchanged across the INT) "
              "[VHDL t80n_mcode.vhd:1953-1991 + zxnext.vhd INT path]",
              p.iter1_ok && p.int_taken && p.no_extra_iter && p.return_pc_ok,
              probe_detail(p));
    }
    {
        const IntSampleProbe p = int_sample_probe(0xB6);
        check("PULSE-G89-INT-04",
              "LDIRSCALE inter-iteration INT sampling: pending /INT serviced "
              "between iterations (BC unchanged across the INT) "
              "[VHDL t80n_mcode.vhd:2188-2226 + zxnext.vhd INT path]",
              p.iter1_ok && p.int_taken && p.no_extra_iter && p.return_pc_ok,
              probe_detail(p));
    }

    // RE-HOME PULSE-G90-01 → contention plan (NEW-CONT-3): 28 MHz SRAM-read
    //   wait state (VHDL zxnext.vhd:3171-3181) is a contention/timing concern,
    //   not interrupt routing. ctc_interrupts_test scope ends at the IM2 +
    //   NMI fabric; cpu_speed=11 SRAM stalls belong to ContentionModel.
}

// ══════════════════════════════════════════════════════════════════════
// Section 15 — Task 60a: debugger single-step must deliver interrupts
// exactly like free-running execution.
//
// Bug: Emulator::execute_single_instruction() was a hand-maintained copy
// of run_frame()'s per-instruction cluster and had drifted — it never
// called im2_.tick(), never polled the IM2-mode / pulse-mode /INT lines
// (VHDL zxnext.vhd:1840 `z80_int_n <= pulse_int_n AND im2_int_n` with
// expbus_disable_int='1'), never ticked md6_ and never recorded trace
// entries. Stepping through interrupt-driven code therefore never
// delivered CTC/UART/ULA-frame interrupts. Fix: both paths now share
// one body (Emulator::step_one_instruction +
// tick_devices_after_instruction).
//
// These rows are the single-step analogues of CTC-INT-V20-IM2-01 /
// ULA-INT-V19-IM2-04 above: identical stimulus, but driven through
// execute_single_instruction() instead of run_frame(). Each row FAILS
// on the pre-fix code (mutation-tested by reverting the fix).
// ══════════════════════════════════════════════════════════════════════

static void test_single_step_int_delivery(Emulator& emu) {
    set_group("SingleStep");

    // SSTEP-01 — pulse-mode CTC INT delivered while single-stepping.
    // Same fixture as CTC-INT-V20-IM2-01 (pulse mode is the power-on
    // default; CTC0 int_en via NR 0xC5 bit 0; IFF1=1 + IM=1; ULA frame
    // INT disabled via NR 0x22 bit 2 so it cannot mask the observation),
    // but the CPU is advanced with execute_single_instruction() only.
    // Post-fix: im2_.tick() advances the pulse fabric, pulse_int_n drops
    // (im2_peripheral.vhd:186-194), the falling-edge poll fires
    // cpu_.request_interrupt(0xFF), the CPU accepts in IM=1 → PC=0x0038.
    // Pre-fix: single-step never ticks the fabric nor polls the line —
    // PC stays in the 0x8000+ NOP field forever.
    {
        fresh(emu);
        nr_write(emu, 0x22, 0x04);   // disable ULA frame INT
        nr_write(emu, 0xC5, 0x01);   // CTC0 int_en
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM   = 1;
        regs.PC   = 0x8000;  // park PC in user RAM (zeros = NOPs)
        regs.SP   = 0xFFFE;
        emu.cpu().set_registers(regs);
        const bool pulse_before = emu.im2().pulse_int_n();
        emu.im2().raise_req(Im2Controller::DevIdx::CTC0);
        int steps = 0;
        uint16_t pc = regs.PC;
        for (; steps < 100; ++steps) {
            emu.execute_single_instruction();
            pc = emu.cpu().get_registers().PC;
            if (pc < 0x4000) break;  // IM1 vector 0x0038 reached (ROM)
        }
        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "pulse_before_raise=%d (must be 1); PC=0x%04X after "
                      "%d single-steps (post-fix: <0x4000 via IM1 vector "
                      "0x0038; pre-fix: stuck at 0x8000+)",
                      pulse_before ? 1 : 0, pc, steps + 1);
        check("SSTEP-01",
              "Pulse-mode CTC INT delivered during debugger single-step "
              "[zxnext.vhd:1840; im2_peripheral.vhd:186-194]",
              pulse_before && pc < 0x4000, detail);
    }

    // SSTEP-02 — IM2-mode fabric INT delivered while single-stepping.
    // Same fixture as ULA-INT-V19-IM2-04 but with the CTC0 device and
    // execute_single_instruction(). Delivery proof is the daisy-chain
    // device state: raise_req latches the request; im2_.tick() advances
    // S_0 → S_REQ; the IM2-mode poll fires request_interrupt; the CPU
    // IntAck calls ack_vector() which advances the winning device
    // S_REQ → S_ACK (→ S_ISR on a later tick). Pre-fix: single-step
    // never ticks the controller — CTC0 stays at S_0 forever.
    {
        fresh(emu);
        nr_write(emu, 0x22, 0x04);   // disable ULA frame INT
        nr_write(emu, 0xC0, 0x01);   // IM2 hardware mode (bit 0)
        nr_write(emu, 0xC5, 0x01);   // CTC0 int_en
        // Feed the IM2 controller's own IM-mode decoder (ED 5E = IM 2),
        // mirroring ULA-INT-V19-IM2-04 — the FUSE-side regs.IM below is
        // a separate shadow.
        emu.im2().on_m1_cycle(0x0000, 0xED);
        emu.im2().on_m1_cycle(0x0001, 0x5E);
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM   = 2;
        regs.PC   = 0x8000;
        regs.SP   = 0xFFFE;
        emu.cpu().set_registers(regs);
        emu.im2().raise_req(Im2Controller::DevIdx::CTC0);
        int st = 0;
        int steps = 0;
        for (; steps < 100; ++steps) {
            emu.execute_single_instruction();
            st = static_cast<int>(
                emu.im2().state(Im2Controller::DevIdx::CTC0));
            if (st >= 2) break;  // S_ACK reached — IntAck happened
        }
        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "CTC0 state=%d after %d single-steps (post-fix: >=2 "
                      "[S_ACK=2/S_ISR=3 via IntAck+ack_vector]; pre-fix: "
                      "0 [S_0, controller never ticked])",
                      st, steps + 1);
        check("SSTEP-02",
              "IM2-mode CTC INT delivered during debugger single-step "
              "[zxnext.vhd:1840, :1999 ack vector composition]",
              st >= 2, detail);
    }

    // SSTEP-03 — trace log records entries during single-step. run_frame
    // records a pre-execution TraceEntry per instruction when the trace
    // is enabled; the pre-fix single-step path recorded nothing, so a
    // stepped section of a program was invisible in the exported trace
    // (and in the rewind lookup that consumes it).
    {
        fresh(emu);
        auto regs = emu.cpu().get_registers();
        regs.PC = 0x8000;
        regs.SP = 0xFFFE;
        emu.cpu().set_registers(regs);
        emu.trace_log().set_enabled(true);
        const size_t size0 = emu.trace_log().size();
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        const size_t size1 = emu.trace_log().size();
        emu.trace_log().set_enabled(false);
        char detail[120];
        std::snprintf(detail, sizeof(detail),
                      "trace entries before=%zu after 3 steps=%zu "
                      "(post-fix: +3; pre-fix: +0)", size0, size1);
        check("SSTEP-03",
              "Trace log records one entry per debugger single-step "
              "(parity with run_frame's per-instruction record)",
              size1 == size0 + 3, detail);
    }

    // SSTEP-04 — MD6 connector FSM advances during single-step. The MD6
    // shared FSM (md6_joystick_connector_x2.vhd:103-114, one CLK_EN per
    // 128 master cycles) latches the raw joystick inputs during its
    // 16-phase sampling burst (phase 0110 latches left bits 5:0,
    // :151-152). Pre-fix, md6_.tick() was missing from the single-step
    // cluster entirely — the latched connector word stayed at its reset
    // value 0 no matter how long the debugger stepped. 200 NOP steps at
    // 3.5 MHz = 200×32 = 6400 master cycles = 50 CLK_ENs — the burst
    // (states 0..15) completes and the FSM parks in the rest window
    // (state(8:4) != 0, :100) where latches are frozen.
    {
        fresh(emu);
        emu.md6().set_raw_left(0x003F);  // bits 5:0 pressed (active-high)
        auto regs = emu.cpu().get_registers();
        regs.PC = 0x8000;
        regs.SP = 0xFFFE;
        emu.cpu().set_registers(regs);
        for (int s = 0; s < 200; ++s)
            emu.execute_single_instruction();
        const uint16_t L = emu.md6().joy_left_word();
        char detail[120];
        std::snprintf(detail, sizeof(detail),
                      "joy_left_word=0x%03X after 200 single-steps "
                      "(post-fix: bits 5:0 latched = 0x03F; pre-fix: 0)",
                      L);
        check("SSTEP-04",
              "MD6 FSM latches raw inputs during debugger single-step "
              "[md6_joystick_connector_x2.vhd:103-114, :151-152]",
              (L & 0x003F) == 0x003F, detail);
    }

    // ── GH #207 — single-stepping must turn FRAMES over, not just
    //    instructions ────────────────────────────────────────────────────
    //
    // Task 60a shared the per-INSTRUCTION body; the per-FRAME body stayed in
    // run_frame(), and the frontends stop calling run_frame() entirely while
    // the debugger is paused (frame_sequencer.h: `if (!fx.paused())`). So
    // during a stepping session the clock ran straight past the end of the
    // frame it was in and no new frame ever began: begin_new_frame() is the
    // only site that schedules the ULA frame interrupt (zxula_timing.vhd:551,
    // fired at (c_int_h, c_int_v) once per frame), the line interrupt, and
    // the per-scanline SCANLINE/VSYNC events. Nothing per-frame could fire
    // again for the rest of the session — and a HALTed CPU, which leaves the
    // halt only on an accepted interrupt (t80n.vhd:1727), could never be
    // woken by any number of Steps. That is GH #207.
    //
    // These four rows drive the machine ONLY through Emulator::debugger_step()
    // — the entry point DebuggerManager::on_step_into() uses — from the exact
    // state the debugger is in after Break: debug_state active + paused, and
    // no frame in flight (frame_in_progress_ == false). The raw one-slot
    // primitive execute_single_instruction() (SSTEP-01..04 above, and most of
    // the test tree) deliberately keeps its old frame-agnostic behaviour: a
    // test that drives its own frames must keep its own frame loop.

    // Put the emulator in the state the debugger leaves it in after Break.
    //
    // DebugState is NOT reset by Emulator::init(), so the data-breakpoint
    // latch is cleared here too: without it SSTEP-10 would inherit whatever
    // SSTEP-09 left behind and stop being an independent row (it does test
    // the latch, and that is SSTEP-09's job alone).
    auto attach_debugger = [](Emulator& e) {
        e.debug_state().set_active(true);
        e.debug_state().set_data_bp_hit(false);
        e.debug_state().pause();
    };

    // SSTEP-05 — the ULA frame interrupt reaches the CPU while single-
    // stepping. Deliberately NOT the CTC (SSTEP-01/02 already cover a device
    // whose request is raised by hand): the ULA frame interrupt is the one
    // that must be SCHEDULED by begin_new_frame(), so it is the one the
    // missing frame boundary silenced. Fresh machine = clock 0, frame_cycle_
    // 0, no frame in flight; IM 1 so acceptance is visible as PC entering the
    // ROM at the 0x0038 vector.
    {
        fresh(emu);
        attach_debugger(emu);
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM   = 1;
        regs.PC   = 0x8000;   // user RAM: zeros = NOPs
        regs.SP   = 0xFFFE;
        emu.cpu().set_registers(regs);
        int steps = 0;
        uint16_t pc = regs.PC;
        for (; steps < 500; ++steps) {
            emu.debugger_step();
            pc = emu.cpu().get_registers().PC;
            if (pc < 0x4000) break;   // IM1 vector 0x0038 reached
        }
        char detail[176];
        std::snprintf(detail, sizeof(detail),
                      "PC=0x%04X after %d single-steps (post-fix: <0x4000 via "
                      "the scheduled ULA frame INT; pre-fix: stuck at 0x8000+ "
                      "— begin_new_frame() never runs while stepping)",
                      pc, steps + 1);
        check("SSTEP-05",
              "ULA frame INT is scheduled and delivered during debugger "
              "single-step [zxula_timing.vhd:551; zxnext.vhd:1840]",
              pc < 0x4000, detail);
    }

    // SSTEP-06 — a Step issued at a HALT leaves the halt.
    // VHDL t80n.vhd:496 freezes PC while Halt_FF is set and :502-503 forces
    // IR to 0x00, so the core issues M1 NOP fetches at the same address
    // indefinitely; t80n.vhd:1727 clears Halt_FF only on an accepted
    // interrupt or NMI cycle. One Step at the halt must therefore end with
    // the CPU out of the halt state and PC in the interrupt handler.
    {
        fresh(emu);
        attach_debugger(emu);
        emu.mmu().write(0x8000, 0x76);   // HALT
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM   = 1;
        regs.PC   = 0x8000;
        regs.SP   = 0xFFFE;
        emu.cpu().set_registers(regs);
        emu.debugger_step();          // executes HALT → halted
        const bool halted_after_1 = emu.cpu().is_halted();
        emu.debugger_step();          // the Step under test
        const bool halted_after_2 = emu.cpu().is_halted();
        const uint16_t pc = emu.cpu().get_registers().PC;
        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "halted after step 1=%d (must be 1); after step 2=%d, "
                      "PC=0x%04X (post-fix: 0 and <0x4000 via the 0x0038 "
                      "vector; pre-fix: 1 and 0x8000 forever)",
                      halted_after_1 ? 1 : 0, halted_after_2 ? 1 : 0, pc);
        check("SSTEP-06",
              "A debugger Step at a HALT leaves the halt into the ISR "
              "[t80n.vhd:496, :502-503, :1727]",
              halted_after_1 && !halted_after_2 && pc < 0x4000, detail);
    }

    // SSTEP-07 — the frame keeps turning over, so a SECOND halt is left too.
    // SSTEP-06 alone only proves the "begin a frame if none is in flight"
    // half: from a fresh machine the very first frame's interrupt is enough.
    // The frame's ULA interrupt is a one-shot scheduler event, so a second
    // halt can only end if end_of_frame() advanced frame_cycle_ and
    // begin_new_frame() scheduled the NEXT frame's interrupt — the other half
    // of the fix. The second halt is entered deliberately at a LATER cycle
    // than the first interrupt fired at.
    {
        fresh(emu);
        attach_debugger(emu);
        emu.mmu().write(0x8000, 0x76);   // HALT
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM   = 1;
        regs.PC   = 0x8000;
        regs.SP   = 0xFFFE;
        emu.cpu().set_registers(regs);
        emu.debugger_step();          // enter halt
        emu.debugger_step();          // first INT ends it
        const uint64_t cycle_after_1 = emu.clock().get();
        // Re-arm: the accepted interrupt cleared IFF1, so put the CPU back at
        // the HALT with interrupts enabled, exactly as an `EI : HALT` loop
        // would on the next pass.
        regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM   = 1;
        regs.PC   = 0x8000;
        emu.cpu().set_registers(regs);
        emu.debugger_step();          // enter halt again
        emu.debugger_step();          // second INT must end it
        const bool halted = emu.cpu().is_halted();
        const uint16_t pc = emu.cpu().get_registers().PC;
        const uint64_t cycle_after_2 = emu.clock().get();
        char detail[208];
        std::snprintf(detail, sizeof(detail),
                      "second halt: halted=%d PC=0x%04X; clock 1st exit=%llu "
                      "2nd exit=%llu (post-fix: 0, <0x4000, later frame; "
                      "pre-fix: the next frame's INT is never scheduled)",
                      halted ? 1 : 0, pc,
                      static_cast<unsigned long long>(cycle_after_1),
                      static_cast<unsigned long long>(cycle_after_2));
        check("SSTEP-07",
              "Frames keep turning over while stepping — a second HALT is "
              "also left [zxula_timing.vhd:551; t80n.vhd:1727]",
              !halted && pc < 0x4000 && cycle_after_2 > cycle_after_1, detail);
    }

    // SSTEP-08 — `DI : HALT` is bounded, not a hang. With IFF1 = 0 no
    // interrupt can ever be accepted (t80n.vhd:1727 needs IntCycle), so real
    // hardware waits for an NMI or a reset and nothing else. The Step must
    // return, report no progress (still halted, PC unmoved), and burn no more
    // than the two-frame budget — the guard against the halt-run loop
    // becoming an unbounded one that freezes the GUI.
    {
        fresh(emu);
        attach_debugger(emu);
        emu.mmu().write(0x8000, 0x76);   // HALT
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 0;                   // DI
        regs.IFF2 = 0;
        regs.IM   = 1;
        regs.PC   = 0x8000;
        regs.SP   = 0xFFFE;
        emu.cpu().set_registers(regs);
        emu.debugger_step();          // enter halt
        const uint64_t before = emu.clock().get();
        emu.debugger_step();          // the bounded Step
        const uint64_t spent = emu.clock().get() - before;
        const uint64_t budget = 2u * emu.timing().master_cycles_per_frame;
        const bool halted = emu.cpu().is_halted();
        const uint16_t pc = emu.cpu().get_registers().PC;
        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "halted=%d PC=0x%04X master cycles spent=%llu budget=%llu "
                      "(must still be halted, PC unmoved, and within budget)",
                      halted ? 1 : 0, pc,
                      static_cast<unsigned long long>(spent),
                      static_cast<unsigned long long>(budget));
        check("SSTEP-08",
              "A Step at a DI'd HALT is bounded and reports no progress "
              "[t80n.vhd:1727 — Halt_FF clears only on IntCycle/NMICycle]",
              halted && pc == 0x8000 && spent > 0 &&
                  spent <= budget + emu.timing().master_cycles_per_line,
              detail);
    }

    // SSTEP-09 — the data-breakpoint latch must be CONSUMED by a Step.
    //
    // `data_bp_hit_` is set by the MMU and cleared by exactly one place:
    // run_frame()'s early-return branch (emulator.cpp:7434-7437, `pause();
    // set_data_bp_hit(false);`). The halt-run loop READS that flag, so if a
    // Step does not consume it too, the first watchpoint to fire poisons every
    // later Step: the loop condition is false at iteration 0 and a Step at a
    // HALT collapses back to one 4-T-state NOP slot — the GH #207 symptom
    // itself, resurrected by the fix's own guard.
    //
    // A READ watchpoint on the halted PC is the sharpest case, because the
    // core re-fetches that byte every internal M1 slot (t80n.vhd:502-503
    // forces IR to 0x00 but the fetch still happens), so it fires on entry to
    // every halt-run. The row therefore checks both halves: the flag is
    // consumed at the end of each Step, and once the watchpoint is REMOVED a
    // further Step runs the halt out normally. Pre-fix the removal changes
    // nothing — the stale latch keeps the loop dead forever.
    {
        fresh(emu);
        attach_debugger(emu);
        emu.mmu().write(0x8000, 0x76);   // HALT
        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;
        regs.IFF2 = 1;
        regs.IM   = 1;
        regs.PC   = 0x8000;
        regs.SP   = 0xFFFE;
        emu.cpu().set_registers(regs);
        emu.debug_state().breakpoints().add_watchpoint(0x8000, WatchType::READ);

        emu.debugger_step();                       // executes HALT; fetch trips it
        const bool consumed_1 = !emu.debug_state().data_bp_hit();
        emu.debugger_step();                       // halt-run stops on the hit
        const bool consumed_2 = !emu.debug_state().data_bp_hit();
        const bool halted_while_armed = emu.cpu().is_halted();

        // Disarm and step again: the halt must now run out.
        emu.debug_state().breakpoints().remove_watchpoint(0x8000, WatchType::READ);
        emu.debugger_step();
        const bool halted_after = emu.cpu().is_halted();
        const uint16_t pc = emu.cpu().get_registers().PC;

        char detail[224];
        std::snprintf(detail, sizeof(detail),
                      "latch consumed after step1=%d step2=%d; halted while "
                      "armed=%d; after disarm halted=%d PC=0x%04X (pre-fix: "
                      "latch stays set and every later Step is a single NOP "
                      "slot — halted=1 PC=0x8000 forever)",
                      consumed_1 ? 1 : 0, consumed_2 ? 1 : 0,
                      halted_while_armed ? 1 : 0, halted_after ? 1 : 0, pc);
        check("SSTEP-09",
              "A Step consumes the data-breakpoint latch, so a watchpoint "
              "firing inside a halt cannot freeze every later Step "
              "[t80n.vhd:502-503 — the halted core re-fetches every slot]",
              consumed_1 && consumed_2 && halted_while_armed &&
                  !halted_after && pc < 0x4000,
              detail);
    }

    // SSTEP-10 — the halt-run crosses a real frame boundary with the REWIND
    // BUFFER enabled.
    //
    // Every other row runs with rewind_buffer_frames = 0, so nothing proved
    // that the frames step_frame_slot() now begins and ends are real frames as
    // far as the rest of the machine is concerned. begin_new_frame() takes the
    // rewind snapshot, and its comment requires the scheduler queue to be
    // empty at that point — a promise the stepping path had never had to keep,
    // because it never reached begin_new_frame() at all.
    //
    // Same shape as SSTEP-07 (a SECOND halt, which can only end in a LATER
    // frame), plus: the buffer must have grown by a frame, and step_back()
    // must still restore coherently afterwards.
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        attach_debugger(emu);
        emu.trace_log().set_enabled(true);          // required by step_back()

        emu.mmu().write(0x8000, 0x76);   // HALT
        auto arm = [&emu]() {
            auto r = emu.cpu().get_registers();
            r.IFF1 = 1;
            r.IFF2 = 1;
            r.IM   = 1;
            r.PC   = 0x8000;
            r.SP   = 0xFFFE;
            emu.cpu().set_registers(r);
        };

        arm();
        emu.debugger_step();                        // enter halt
        emu.debugger_step();                        // frame 0's INT ends it
        const auto* rb = emu.rewind_buffer();
        const size_t depth_1 = rb ? rb->depth() : 0;
        const uint32_t newest_1 = (rb && depth_1) ? rb->newest_frame_num() : 0;

        arm();                                      // re-arm: IFF1 was cleared
        emu.debugger_step();                        // enter halt again
        emu.debugger_step();                        // must roll into frame 1

        const size_t depth_2 = rb ? rb->depth() : 0;
        const uint32_t newest_2 = (rb && depth_2) ? rb->newest_frame_num() : 0;
        const bool halted = emu.cpu().is_halted();
        const uint16_t pc = emu.cpu().get_registers().PC;
        const bool stepped_back = emu.step_back(1);

        char detail[240];
        std::snprintf(detail, sizeof(detail),
                      "rewind depth %zu->%zu newest_frame_num %u->%u; halted=%d "
                      "PC=0x%04X; step_back=%d (a halt-run that crosses a frame "
                      "boundary must take the frame's rewind snapshot)",
                      depth_1, depth_2, newest_1, newest_2,
                      halted ? 1 : 0, pc, stepped_back ? 1 : 0);
        check("SSTEP-10",
              "A halt-run crossing a frame boundary takes the frame's rewind "
              "snapshot and leaves the machine rewindable "
              "[zxula_timing.vhd:551; t80n.vhd:1727]",
              !halted && pc < 0x4000 && depth_2 > depth_1 &&
                  newest_2 > newest_1 && stepped_back,
              detail);
    }
}

// ── Group: C-IM2 quiescent early-out equivalence (Task 27 C-IM2) ──────
//
// Im2Controller::tick() early-outs when the fabric is quiescent (see
// im2.cpp tick() for the field-by-field proof). These rows pin the
// contract on a bare Im2Controller:
//   - a quiescent stretch of ticks must leave the ENTIRE serialized
//     controller state byte-identical (skipped ticks are provable
//     no-ops), and
//   - activity arriving after a long quiescent stretch must behave
//     exactly as if every tick had run: the pulse fires with the exact
//     VHDL width (zxnext.vhd:2033-2044: terminal 36 CPU cycles in
//     non-48K/+3 timing), the wrapper edge detector is not left with a
//     stale int_req_d (im2_peripheral.vhd:98-101 — a stale delayed copy
//     would mask the next edge), and the im2-mode daisy chain still
//     ACKs and clears on RETI (im2_device.vhd:111-128).

namespace {
std::vector<uint8_t> im2_snapshot(const Im2Controller& im2) {
    StateWriter measure;
    im2.save_state(measure);
    std::vector<uint8_t> buf(measure.position(), 0);
    StateWriter w(buf.data(), buf.size());
    im2.save_state(w);
    return buf;
}
}  // namespace

void test_cim2_quiescence() {
    set_group("CIM2-Quiescence");

    // CIM2-QUIESCE-01 — pulse mode (the boot-nextzxos shape).
    {
        Im2Controller im2;                       // reset: pulse mode, idle
        im2.set_int_en(Im2Controller::DevIdx::ULA, true);
        im2.tick(4);                             // settle (recompute cache)

        // (a) 1000 quiescent ticks leave serialized state byte-identical.
        const auto snap_a = im2_snapshot(im2);
        for (int i = 0; i < 1000; ++i) im2.tick(7);
        const auto snap_b = im2_snapshot(im2);
        const bool identical = (snap_a == snap_b);

        // (b) pulse fires after the stretch with the exact VHDL width:
        // terminal 36 (machine_48_or_p3=false), advance 4 T/tick →
        // low for the start tick + 8 counting ticks (count 4..32),
        // terminates on the 9th counting tick (count 36).
        im2.raise_req(Im2Controller::DevIdx::ULA);
        im2.tick(4);
        const bool started = !im2.pulse_int_n();
        bool low_through_32 = true;
        for (int i = 0; i < 8; ++i) {
            im2.tick(4);
            if (im2.pulse_int_n()) low_through_32 = false;
        }
        im2.tick(4);
        const bool ended_at_36 = im2.pulse_int_n();

        // (c) stale-int_req_d guard: an edge on a DISABLED device sets
        // int_status only (im2_peripheral.vhd:154-162; no pulse, no
        // im2_int_req in pulse mode). After a quiescent stretch — whose
        // FIRST tick must have run the delayed-copy update int_req_d :=
        // int_req (vhdl:98) before the cache engages — a fresh edge must
        // still be detected. A wrong early-out that skips with
        // int_req_d==true masks the second edge and int_status stays 0.
        im2.raise_req(Im2Controller::DevIdx::CTC0);   // int_en=false
        im2.tick(4);                                  // edge → int_status
        const bool status_first =
            im2.int_status(Im2Controller::DevIdx::CTC0);
        for (int i = 0; i < 50; ++i) im2.tick(4);     // quiescent stretch
        im2.clear_status(Im2Controller::DevIdx::CTC0);
        im2.raise_req(Im2Controller::DevIdx::CTC0);
        im2.tick(4);                                  // second edge
        const bool status_second =
            im2.int_status(Im2Controller::DevIdx::CTC0);

        char detail[200];
        std::snprintf(detail, sizeof(detail),
                      "identical=%d started=%d low_through_32=%d "
                      "ended_at_36=%d status_first=%d status_second=%d",
                      identical, started, low_through_32, ended_at_36,
                      status_first, status_second);
        check("CIM2-QUIESCE-01",
              "pulse mode: quiescent ticks are serialized no-ops; pulse "
              "after stretch keeps exact 36-cycle width "
              "[zxnext.vhd:2033-2044] and edge detect is not masked by a "
              "stale int_req_d [im2_peripheral.vhd:98-101]",
              identical && started && low_through_32 && ended_at_36
                  && status_first && status_second,
              detail);
    }

    // CIM2-QUIESCE-02 — im2 mode: S_REQ / S_ISR are stable quiescent
    // states; ACK and RETI still work after long skipped stretches.
    {
        Im2Controller im2;
        im2.set_mode(true);                      // NR 0xC0 b0=1
        im2.on_m1_cycle(0, 0xED);                // IM 2 → im_mode_=2
        im2.on_m1_cycle(0, 0x5E);                // (im2_control.vhd:218-227)
        im2.set_int_en(Im2Controller::DevIdx::CTC0, true);

        im2.raise_req(Im2Controller::DevIdx::CTC0);
        im2.tick(1);                             // edge → latch → S_REQ
        im2.tick(1);                             // settle int_req_d
        const bool req_state =
            im2.state(Im2Controller::DevIdx::CTC0)
                == Im2Controller::DevState::S_REQ
            && im2.int_line_asserted();

        // S_REQ pending across a quiescent stretch: byte-identical.
        const auto snap_a = im2_snapshot(im2);
        for (int i = 0; i < 1000; ++i) im2.tick(3);
        const auto snap_b = im2_snapshot(im2);
        const bool identical_req = (snap_a == snap_b);

        // IntAck after the stretch: CTC0 (DevIdx 3) vector = 3<<1 = 0x06
        // (zxnext.vhd:1999, base 0).
        const uint8_t vec = im2.ack_vector();
        im2.tick(1);                             // S_ACK → S_ISR
        const bool isr_state =
            im2.state(Im2Controller::DevIdx::CTC0)
                == Im2Controller::DevState::S_ISR;

        // S_ISR latched across a quiescent stretch: byte-identical.
        const auto snap_c = im2_snapshot(im2);
        for (int i = 0; i < 500; ++i) im2.tick(9);
        const auto snap_d = im2_snapshot(im2);
        const bool identical_isr = (snap_c == snap_d);

        // RETI after the stretch, via the TICK path (not on_reti()):
        // the live reti_seen_pulse_ check must force a full tick whose
        // S_ISR branch clears the device (im2_device.vhd:123-128).
        im2.on_m1_cycle(0, 0xED);
        im2.on_m1_cycle(0, 0x4D);                // reti_seen pulse live
        im2.tick(1);
        const bool cleared =
            im2.state(Im2Controller::DevIdx::CTC0)
                == Im2Controller::DevState::S_0;

        // A fresh request after everything must re-enter S_REQ.
        im2.on_m1_cycle(0, 0x00);                // drop the RETI pulse
        im2.raise_req(Im2Controller::DevIdx::CTC0);
        im2.tick(1);
        const bool re_req =
            im2.state(Im2Controller::DevIdx::CTC0)
                == Im2Controller::DevState::S_REQ;

        char detail[200];
        std::snprintf(detail, sizeof(detail),
                      "req_state=%d identical_req=%d vec=0x%02X "
                      "isr_state=%d identical_isr=%d cleared=%d re_req=%d",
                      req_state, identical_req, vec, isr_state,
                      identical_isr, cleared, re_req);
        check("CIM2-QUIESCE-02",
              "im2 mode: S_REQ/S_ISR stable across quiescent stretches "
              "(serialized no-ops); ACK vector [zxnext.vhd:1999] and "
              "RETI clear via tick [im2_device.vhd:123-128] still exact "
              "after skipped stretches",
              req_state && identical_req && vec == 0x06 && isr_state
                  && identical_isr && cleared && re_req,
              detail);
    }
}

// ── Task 27 C1: CTC event-horizon tick() equivalence ─────────────────
//
// Ctc::tick(N) was rewritten from an O(N*4) per-cycle loop to an
// O(ZC/TO events) event-horizon loop: gap cycles are applied in closed
// form by CtcChannel::advance(), and the cycle in which the earliest
// ZC/TO fires runs the ORIGINAL exact per-channel inner loop (which is
// what the pre-existing 36 VHDL-cited rows validate). These rows pin
// the seam: a single tick(N) span must be observably identical to N
// sequential tick(1) calls, with absolute event positions derived from
// ctc_chan.vhd prescaler/counter semantics (p_count :136-138,
// prescaler_clk :143-146, t_count/zc_to :150-170).

void test_c1_ctc_accumulator() {
    set_group("CTC-C1-ACC");

    // Program a channel: control word, then time constant.
    const auto prog = [](Ctc& ctc, int ch, uint8_t cw, uint8_t tc) {
        ctc.write(ch, cw);
        ctc.write(ch, tc);
    };

    // CTC-C1-ACC-01 — one tick(150) span crossing three ZC/TO events,
    // and prescaler-phase continuity across the closed-form jump.
    // Timer mode, prescaler 16, TC=3: one S_TRIGGER edge after the constant
    // (ctc_chan.vhd:214-226), then prescaler_clk every 16 ticks (:143-146),
    // ZC/TO on every 3rd count step (:162-170) → events at ticks 49, 97,
    // 145; the 4th lands at 193, exactly 43 ticks after the 150-span (the
    // prescaler phase must survive the jump). GH #265 — 48/96/144/192 was
    // the pre-fix model, which had no S_TRIGGER edge.
    {
        Ctc ctc;
        std::vector<int> seq;
        ctc.on_zc_to = [&](int ch) { seq.push_back(ch); };
        prog(ctc, 0, 0x07, 3);   // int off, timer, /16, auto-start, TC=3

        ctc.tick(150);                            // one span, 3 events
        const size_t after_span     = seq.size();
        const uint8_t counter_after = ctc.read(0);  // reloaded to 3 at 145
        ctc.tick(42);                             // ticks 151..192: none
        const size_t before_edge    = seq.size();
        ctc.tick(1);                              // tick 193: 4th ZC/TO
        const size_t at_edge        = seq.size();

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "after_span=%zu counter=%u before_edge=%zu at_edge=%zu",
                      after_span, counter_after, before_edge, at_edge);
        check("CTC-C1-ACC-01",
              "timer /16 TC=3: single tick(150) span fires exactly the 3 "
              "ZC/TO at 49/97/145 [ctc_chan.vhd:214-226,143-146,:162-170]; "
              "prescaler phase survives the closed-form jump (4th ZC/TO "
              "exactly at 193)",
              after_span == 3 && counter_after == 3
                  && before_edge == 3 && at_edge == 4,
              detail);
    }

    // CTC-C1-ACC-02 — daisy-chain inside one span + span-splitting
    // invariance. ch0 timer /16 TC=3 (ZC/TO at 49/97/145/193); ch1
    // counter mode TC=2 fed by the ch0→ch1 daisy-chain (zxnext.vhd:4084)
    // counts each pulse two edges later through clk_trg_d
    // (ctc_chan.vhd:115-127) → ch1 fires on every 2nd ch0 pulse, at 99 and
    // 195 (GH #265; 96 and 192 before). One tick(200) call
    // and 200 tick(1) calls on identically-programmed instances must
    // produce the same absolute callback sequence 0,0,1,0,0,1 and the
    // same final counter values.
    {
        const std::vector<int> expected = {0, 0, 1, 0, 0, 1};

        const auto run = [&](Ctc& ctc, std::vector<int>& seq, bool stepped) {
            ctc.on_zc_to = [&seq](int ch) { seq.push_back(ch); };
            prog(ctc, 0, 0x07, 3);   // timer, /16, auto, TC=3
            prog(ctc, 1, 0x47, 2);   // counter mode, TC=2
            if (stepped) {
                for (int i = 0; i < 200; ++i) ctc.tick(1);
            } else {
                ctc.tick(200);
            }
        };

        Ctc ctc_span, ctc_step;
        std::vector<int> seq_span, seq_step;
        run(ctc_span, seq_span, false);
        run(ctc_step, seq_step, true);

        const bool seq_ok      = (seq_span == expected);
        const bool split_ok    = (seq_step == expected);
        const bool counters_ok = ctc_span.read(0) == ctc_step.read(0)
                              && ctc_span.read(1) == ctc_step.read(1)
                              && ctc_span.read(0) == 3    // reloaded at 193
                              && ctc_span.read(1) == 2;   // reloaded at 195

        char detail[200];
        std::snprintf(detail, sizeof(detail),
                      "span_n=%zu step_n=%zu seq_ok=%d split_ok=%d "
                      "cnt0=%u/%u cnt1=%u/%u",
                      seq_span.size(), seq_step.size(), seq_ok, split_ok,
                      ctc_span.read(0), ctc_step.read(0),
                      ctc_span.read(1), ctc_step.read(1));
        check("CTC-C1-ACC-02",
              "ch0 timer /16 TC=3 chained into ch1 counter TC=2 "
              "[zxnext.vhd:4084]: one tick(200) equals 200 tick(1) calls "
              "— sequence 0,0,1,0,0,1 and identical counters",
              seq_ok && split_ok && counters_ok,
              detail);
    }

    // CTC-C1-ACC-03 — mid-span TRIGGER→RUN activation. ch0 timer /16
    // TC=1 auto-start (ZC/TO at 17/33/49/...); ch1 timer /16 TC=1 with
    // D3=1 (waits for CLK/TRG — ctc_chan.vhd S_TRIGGER, :219-224). ch0's
    // pulse at 17 reaches ch1's clk_trg_edge two edges later (clk_trg_d,
    // :115-127), so ch1 starts on edge 19 with p_count held at 0 on it
    // (reset_soft, :117,:134-139) and its first ZC/TO lands 16 edges on, at
    // 35 — after ch0's second at 33. GH #265 — the per-cycle model started
    // ch1 on ch0's own edge AND counted that edge, firing it at 31. The
    // event-horizon loop must reproduce the exact edge, and the stepped
    // twin must agree.
    {
        const auto run = [&](Ctc& ctc, std::vector<int>& seq, bool stepped,
                             int ticks) {
            ctc.on_zc_to = [&seq](int ch) { seq.push_back(ch); };
            prog(ctc, 0, 0x07, 1);   // timer, /16, auto-start, TC=1
            prog(ctc, 1, 0x0F, 1);   // timer, /16, CLK/TRG-start, TC=1
            if (stepped) {
                for (int i = 0; i < ticks; ++i) ctc.tick(1);
            } else {
                ctc.tick(ticks);
            }
        };

        Ctc ctc_span, ctc_step, ctc_early;
        std::vector<int> seq_span, seq_step, seq_early;
        run(ctc_span, seq_span, false, 35);
        run(ctc_step, seq_step, true, 35);
        run(ctc_early, seq_early, false, 34);

        const std::vector<int> expected = {0, 0, 1};  // ch0@17, ch0@33, ch1@35
        const bool span_ok  = (seq_span == expected);
        const bool step_ok  = (seq_step == expected);
        const bool early_ok = (seq_early == std::vector<int>{0, 0});

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "span=[%s] step=[%s] at34=[%s]",
                      span_ok ? "0,0,1" : "?", step_ok ? "0,0,1" : "?",
                      early_ok ? "0,0" : "?");
        check("CTC-C1-ACC-03",
              "timer ch1 armed by D3=1 started by ch0's ZC/TO at 17 through "
              "clk_trg_d, fires at 35 [ctc_chan.vhd:115-127,219-226,134-139; "
              "zxnext.vhd:4084]; tick(35) == 35x tick(1)",
              span_ok && step_ok && early_ok,
              detail);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Task 60d — IM2 pulse-width CPU-speed invariance
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:2035-2044 — the pulse_int_n LOW counter increments on
// each `rising_edge(i_CLK_CPU)` (one increment per CPU T-state), and the
// pulse lasts 32 (48K/+3) or 36 (128K/Pentagon/Next) CPU cycles
// (zxnext.vhd:2014-2015, terminal gate :2033). i_CLK_CPU is the CPU clock,
// NOT the 28 MHz master clock, so the pulse LOW width in T-states is
// INVARIANT across the 3.5/7/14/28 MHz CPU speeds.
//
// Regression guard for the Task 60d units bug: Emulator::step_one_instruction
// called `im2_.tick(master_cycles)` (= tstates × cpu_divisor, a 28 MHz-domain
// figure) instead of `im2_.tick(tstates)`. Below 28 MHz the counter advanced
// cpu_divisor× too fast, collapsing the pulse to a single instruction (8× fast
// at 3.5 MHz). These rows measure the pulse LOW width in T-states through the
// production step path at two speeds; the 3.5 MHz row is the discriminative
// one (pre-fix width ≈ 4-8, post-fix == terminal).

static bool build_emulator(Emulator& emu, MachineType type) {
    EmulatorConfig cfg;
    cfg.type = type;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Start a pulse-mode interrupt via CTC0 and step NOPs through the real
// Emulator::step_one_instruction() path until pulse_int_n() rises again,
// returning the LOW width in CPU T-states. Negative sentinel on setup
// failure. `speed` selects the CPU clock.
static int measure_pulse_width_tstates(MachineType type, CpuSpeed speed,
                                       bool soft_reset_first = false) {
    Emulator emu;
    if (!build_emulator(emu, type)) return -1;
    // GH #237 — optionally take the machine through a real RESET_SOFT
    // (NR 0x02 bit 0) before measuring. Everything below re-runs after it,
    // because the reset re-enters Emulator::init() and re-seeds the CPU
    // speed, RAM and IM2 state this measurement depends on.
    if (soft_reset_first) nr_write(emu, 0x02, 0x01);
    emu.clock().set_cpu_speed(speed);

    // Uncontended NOP field at 0x8000 (RAM on both machines at reset); PC
    // pointed there; CPU interrupts disabled (IFF1=IFF2=0) so the pulse is
    // never serviced — we observe pulse_int_n() directly.
    for (uint16_t a = 0x8000; a < 0x8080; ++a) emu.mmu().write(a, 0x00);
    if (emu.mmu().read(0x8000) != 0x00) return -5;   // 0x8000 not writable RAM
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000; regs.IFF1 = 0; regs.IFF2 = 0;
    emu.cpu().set_registers(regs);

    // Pulse mode (NR 0xC0 bit0 = 0). Silence every real interrupt-source
    // enable so a scheduled ULA/line/UART interrupt cannot start a competing
    // pulse; enable + raise CTC0 as the sole (non-exception) pulse source.
    auto& im2 = emu.im2();
    im2.set_mode(false);
    im2.set_int_en_c4(0);
    im2.set_int_en_c5(0);
    im2.set_int_en_c6(0);
    im2.set_int_en(Im2Controller::DevIdx::CTC0, true);
    im2.raise_req(Im2Controller::DevIdx::CTC0);

    // First step: the tick observes the int_req rising edge → pulse starts
    // (pulse_int_n LOW, counter reset). The starting tick does NOT advance
    // the counter (VHDL:2038 holds it 0 while pulse_int_n='1' at tick entry).
    emu.execute_single_instruction();
    if (im2.pulse_int_n()) return -2;   // pulse never started

    int width = 0;
    for (int guard = 0; guard < 500 && !im2.pulse_int_n(); ++guard) {
        width += emu.execute_single_instruction();   // returns CPU T-states
    }
    return im2.pulse_int_n() ? width : -3;            // -3 = never terminated
}

static void test_pulse_width_speed_invariance() {
    set_group("Pulse-Width-60d");

    // Every row ID is spelled out, rather than built from a stem with
    // `std::string(c.id) + "-35"`. The stem form emitted the same six rows,
    // but no reader that scans the SOURCE could see them: the literals
    // "PW-48K"/"PW-NEXT" are not row IDs (nothing asserts them), while the
    // six IDs that are asserted existed only at runtime. The traceability
    // matrix therefore carried two rows that do not exist and none of the six
    // that do — 44 scanned vs 48 run, and the gap was invisible because both
    // numbers came from different readers. (GH #196 phase 2)
    struct Case {
        const char* id_35;
        const char* id_28;
        const char* id_inv;
        MachineType type;
        int         terminal;
    };
    // GH #232 — the terminal is `pulse_count(5) and (machine_timing_48 or
    // machine_timing_p3 or pulse_count(2))` (zxnext.vhd:2033): 32 when the
    // TIMING axis is 48K or +3, 36 otherwise. That axis is
    // eff_nr_03_machine_timing (:5761-5776), NOT the machine personality —
    // there is no "Next timing" in the VHDL at all. A Next that no firmware
    // has written NR 0x03 on sits at the FF's initial value "011" = +3
    // (zxnext.vhd:1099/:1377), which jnext seeds identically in
    // Emulator::init(). So the Next's cold-boot terminal is 32, not 36.
    //
    // The 36 these rows used to expect came from keying the gate on the CLI
    // MachineType, which conflated typ_sel with tim_sel: it made a guest
    // writing NR 0x03 back with the value already in the register flip the
    // width from 36 to 32. `terminal` is now what the cited VHDL line
    // computes from the register the machine actually boots with.
    const Case cases[] = {
        {"PW-48K-35",  "PW-48K-28",  "PW-48K-INV",
         MachineType::ZX48K,      32},
        {"PW-NEXT-35", "PW-NEXT-28", "PW-NEXT-INV",
         MachineType::ZXN_ISSUE2, 32},
        // GH #232 — with the Next now correctly on the 32-cycle branch,
        // nothing else pinned the OTHER side of zxnext.vhd:2033. 128K
        // (tim_sel "010" → machine_timing_128) is the machine that takes it:
        // neither machine_timing_48 nor machine_timing_p3 is set, so the
        // terminal needs pulse_count(2) as well and the width is 36.
        {"PW-128K-35", "PW-128K-28", "PW-128K-INV",
         MachineType::ZX128K,     36},
    };

    for (const auto& c : cases) {
        const int w_slow = measure_pulse_width_tstates(c.type, CpuSpeed::MHZ_3_5);
        const int w_fast = measure_pulse_width_tstates(c.type, CpuSpeed::MHZ_28);

        const std::string d =
            std::string("3.5MHz=") + std::to_string(w_slow)
            + " 28MHz=" + std::to_string(w_fast)
            + " terminal=" + std::to_string(c.terminal);

        // Discriminative row: pre-fix the 3.5 MHz pulse collapsed to a
        // single instruction (~4-8 T), so this flips to FAIL if the call
        // site reverts to master_cycles.
        check(c.id_35,
              "pulse LOW width at 3.5 MHz == terminal CPU T-states "
              "[zxnext.vhd:2035-2044,2014-2015,2033]",
              w_slow == c.terminal, d);

        // Reference row: at 28 MHz master_cycles == tstates, so this held
        // both pre- and post-fix — pins the terminal value.
        check(c.id_28,
              "pulse LOW width at 28 MHz == terminal CPU T-states "
              "[zxnext.vhd:2035-2044]",
              w_fast == c.terminal, d);

        // Speed-invariance: identical width across the CPU speeds (i_CLK_CPU
        // domain), and equal to the VHDL terminal.
        check(c.id_inv,
              "pulse LOW width is CPU-speed invariant "
              "[zxnext.vhd:2035-2044 i_CLK_CPU domain]",
              w_slow == w_fast && w_slow == c.terminal, d);
    }

    // PW-GH237-128K-SOFT — the same gate, after a RESET_SOFT.
    //
    // Every row above measures a COLD boot, so none of them sees what the
    // gate does once the machine has been soft-reset. GH #232 rerouted this
    // gate from cfg.type onto `init_tim_mode`, i.e. onto
    // nextreg_.nr_03_machine_timing() — correct per zxnext.vhd:2033/:5761-5776,
    // but NextReg::reset() was still rewriting that field to "011" (+3) on
    // every reset. A 128K machine therefore came out of RESET_SOFT claiming
    // +3 timing and took the 32-cycle branch, where :2033 wants 36:
    // nr_03_machine_timing has no reset clause in the VHDL (:1099 initialiser
    // only, absent from :4926-5111), so tim_sel is still "010" and
    // machine_timing_128 is still the one-hot that is set.
    //
    // 48K is deliberately not given a companion row: its cold-boot and
    // clobbered-to-+3 values are both 32, so such a row would assert nothing.
    {
        const int cold = measure_pulse_width_tstates(MachineType::ZX128K,
                                                     CpuSpeed::MHZ_3_5);
        const int soft = measure_pulse_width_tstates(MachineType::ZX128K,
                                                     CpuSpeed::MHZ_3_5,
                                                     /*soft_reset_first=*/true);
        check("PW-GH237-128K-SOFT",
              "128K keeps the 36-cycle /INT pulse width across RESET_SOFT — "
              "tim_sel \"010\" survives the reset, so machine_timing_128 is "
              "still the one-hot and the terminal still needs pulse_count(2) "
              "[zxnext.vhd:2033; :1099 + :4926-5111 no reset clause]",
              cold == 36 && soft == 36,
              "cold=" + std::to_string(cold) + " after_soft_reset=" +
                  std::to_string(soft) + " (want 36; 32 = the +3/48K branch)");
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 13 — CTC control-word interrupt-enable → IM2 fabric (GH #47/#48)
//
// VHDL device/ctc_chan.vhd:265-276 keeps ONE interrupt-enable flip-flop
// per channel, `control_reg(7-3)`, and gives it two writers:
//
//    if reset_hard = '1'   then control_reg <= (others => '0');
//    elsif iowr_cw = '1'   then control_reg <= i_cpu_d(7 downto 3);
//    elsif i_int_en_wr='1' then control_reg(7-3) <= i_int_en;
//    ...
//    o_int_en <= control_reg(7-3);
//
// `iowr_cw` is a CTC control-word port write (0x183B..0x1B3B, D0=1);
// `i_int_en_wr` is an NR 0xC5 write (zxnext.vhd:4078-4079). `o_int_en`
// is exported as `ctc_int_en` and is the ONLY signal zxnext.vhd:1949
// fans into `im2_int_en` — so both writers reach the IM2 fabric, and
// the later one wins.
//
// jnext refreshed the fabric copy from the NR 0xC5 handler only. A
// control word therefore updated the CTC's own enable (visible in the
// NR 0xC5 readback, which is sourced from Ctc::get_int_enable()) while
// the fabric's dev_[CTCn].int_en stayed at whatever NR 0xC5 last said —
// the channel's ZC/TO raised int_req but the wrapper's int_en AND
// discarded it. Both audio players shipped on the official SD card
// (NextSIDplayer, nxmodplayer) write NR 0xC5 first and then program the
// channels with control words carrying D7=1, so their engine interrupts
// never reached the Z80 and both played silence.
// ══════════════════════════════════════════════════════════════════════

static void test_ctc_control_word_int_en(Emulator& emu) {
    set_group("CTC-CW-INTEN");

    // Put the fabric in hw-im2 mode with the Z80 in IM 2 — the two gates
    // the device state machine needs before an int_req can leave S_0
    // (im2_peripheral.vhd:105,167-178 + im2_device.vhd:150). Same idiom
    // as ULA-INT-V19-IM2-01 above.
    const auto arm_im2 = [](Emulator& e) {
        e.im2().set_mode(true);
        e.im2().on_m1_cycle(0x0000, 0xED);
        e.im2().on_m1_cycle(0x0001, 0x5E);
    };

    // CTC-CW-INTEN-01 — the GH #47/#48 shape, verbatim: NR 0xC5 enables
    // CTC0 only, then channel 1 is programmed with control word 0xA5
    // (D7=1 → interrupt enable, timer, /256, TC follows) + TC. Per
    // ctc_chan.vhd:269 that control word writes control_reg(7)=1, so
    // CTC1's request must now reach the fabric.
    //
    // Discriminative: pre-fix dev_[CTC1].int_en stayed false (NR 0xC5
    // bit 1 = 0), the im2_int_req latch never set, and the device sat
    // in S_0 forever.
    {
        fresh(emu);
        arm_im2(emu);
        nr_write(emu, 0xC5, 0x01);          // NR 0xC5: CTC0 only
        emu.port().out(0x183B, 0x85);       // ch0 control word (D7=1)
        emu.port().out(0x183B, 0x72);       // ch0 time constant
        emu.port().out(0x193B, 0xA5);       // ch1 control word (D7=1)
        emu.port().out(0x193B, 0xFA);       // ch1 time constant

        emu.im2().raise_req(Im2Controller::DevIdx::CTC1);
        emu.im2().tick(1);
        const Im2Controller::DevState st = emu.im2().state(Im2Controller::DevIdx::CTC1);
        const uint8_t c5 = nr_read(emu, 0xC5);

        char detail[220];
        std::snprintf(detail, sizeof(detail),
                      "NR 0xC5<-0x01 then ch1 CW 0xA5: state=%d "
                      "(post-fix S_REQ=1; pre-fix S_0=0) NR 0xC5 readback=%s",
                      static_cast<int>(st), hex2(c5).c_str());
        check("CTC-CW-INTEN-01",
              "CTC control word D7=1 enables that channel's IM2 interrupt "
              "even when NR 0xC5 left it masked "
              "[ctc_chan.vhd:269,276 + zxnext.vhd:1949]",
              st == Im2Controller::DevState::S_REQ && c5 == 0x03, detail);
    }

    // CTC-CW-INTEN-02 — the opposite direction. NR 0xC5 enables all four
    // channels, then channel 1's control word 0x25 (D7=0, timer, /256,
    // TC follows) clears control_reg(7). ctc_chan.vhd:269 writes the
    // WHOLE control_reg from i_cpu_d(7 downto 3), so D7=0 disables the
    // channel; the fabric must follow and hold CTC1 in S_0.
    //
    // Discriminative: pre-fix the fabric kept NR 0xC5's 0x0F and the
    // device advanced to S_REQ — a phantom interrupt from a channel
    // software had just disabled.
    {
        fresh(emu);
        arm_im2(emu);
        nr_write(emu, 0xC5, 0x0F);          // NR 0xC5: all four channels
        emu.port().out(0x193B, 0x25);       // ch1 control word (D7=0)
        emu.port().out(0x193B, 0xFA);       // ch1 time constant

        emu.im2().raise_req(Im2Controller::DevIdx::CTC1);
        emu.im2().tick(1);
        const Im2Controller::DevState st = emu.im2().state(Im2Controller::DevIdx::CTC1);
        const uint8_t c5 = nr_read(emu, 0xC5);

        char detail[220];
        std::snprintf(detail, sizeof(detail),
                      "NR 0xC5<-0x0F then ch1 CW 0x25: state=%d "
                      "(post-fix S_0=0; pre-fix S_REQ=1) NR 0xC5 readback=%s",
                      static_cast<int>(st), hex2(c5).c_str());
        check("CTC-CW-INTEN-02",
              "CTC control word D7=0 disables that channel's IM2 interrupt "
              "even when NR 0xC5 had enabled it "
              "[ctc_chan.vhd:269,276 + zxnext.vhd:1949]",
              st == Im2Controller::DevState::S_0 && c5 == 0x0D, detail);
    }

    // CTC-CW-INTEN-03 — selectivity: a control word enables exactly the
    // channel it addresses, leaves the other channels' enables intact,
    // and never lights up CTC4..7 (only four channels are instantiated —
    // zxnext.vhd:4067 — and :4093 hardwires
    // `ctc_int_en(7 downto 4) <= "0000"`).
    //
    // NR 0xC5 ← 0x01 enables CTC0 only; ch2's control word 0xA5 (D7=1)
    // must then add CTC2 and nothing else. CTC2 is what makes this row
    // discriminative for the fix as well as a selectivity guard: pre-fix
    // its fabric int_en stayed false (no NR 0xC5 write ever set bit 2),
    // so CTC2 could not leave S_0. CTC3/CTC7 are the negative controls
    // and CTC0 the positive one — those three hold in both directions.
    {
        fresh(emu);
        arm_im2(emu);
        nr_write(emu, 0xC5, 0x01);          // CTC0 enabled
        emu.port().out(0x1A3B, 0xA5);       // ch2 control word (D7=1)
        emu.port().out(0x1A3B, 0x10);       // ch2 time constant

        emu.im2().raise_req(Im2Controller::DevIdx::CTC0);
        emu.im2().raise_req(Im2Controller::DevIdx::CTC2);
        emu.im2().raise_req(Im2Controller::DevIdx::CTC3);
        emu.im2().raise_req(Im2Controller::DevIdx::CTC7);
        emu.im2().tick(1);
        const Im2Controller::DevState s0 = emu.im2().state(Im2Controller::DevIdx::CTC0);
        const Im2Controller::DevState s2 = emu.im2().state(Im2Controller::DevIdx::CTC2);
        const Im2Controller::DevState s3 = emu.im2().state(Im2Controller::DevIdx::CTC3);
        const Im2Controller::DevState s7 = emu.im2().state(Im2Controller::DevIdx::CTC7);
        const uint8_t c5 = nr_read(emu, 0xC5);

        char detail[260];
        std::snprintf(detail, sizeof(detail),
                      "after ch2 CW 0xA5: CTC0 state=%d (S_REQ=1) "
                      "CTC2 state=%d (post-fix S_REQ=1; pre-fix S_0=0) "
                      "CTC3 state=%d (S_0=0) CTC7 state=%d (S_0=0) "
                      "NR 0xC5 readback=%s",
                      static_cast<int>(s0), static_cast<int>(s2),
                      static_cast<int>(s3), static_cast<int>(s7),
                      hex2(c5).c_str());
        check("CTC-CW-INTEN-03",
              "a control word enables exactly its own channel, leaves the "
              "others' enables intact, and never enables CTC4..7 "
              "[ctc_chan.vhd:269,276 + zxnext.vhd:4067,4093]",
              s0 == Im2Controller::DevState::S_REQ
                  && s2 == Im2Controller::DevState::S_REQ
                  && s3 == Im2Controller::DevState::S_0
                  && s7 == Im2Controller::DevState::S_0
                  && c5 == 0x05, detail);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section GH201 — CTC+Interrupts plan rows that nothing asserted
//
// Four rows of doc/testing/CTC-INTERRUPTS-TEST-PLAN-DESIGN.md were listed
// in the plan and asserted nowhere, so the generated traceability matrix
// published them as `missing`. All four need the full Emulator fixture:
// the NextREG read path (NR-C0-02, NR-C5-02) and the CTC-ZC/TO-to-
// joystick-pin-7 wiring (CTC-JOY-01/02) only exist once NextReg, Ctc and
// IoMode have been wired together by Emulator::init().
// ══════════════════════════════════════════════════════════════════════

static void test_gh201_plan_rows(Emulator& emu) {
    set_group("GH201");

    // ── NR-C0-02 — NR 0xC0 bit 3 is the stackless-NMI enable ───────────
    // VHDL zxnext.vhd:5598 `nr_c0_stackless_nmi <= nr_wr_dat(3)`, consumed
    // at :2075-2085: while the bit is set the NMI acknowledge substitutes
    // the NR 0xC3:0xC2 pair for the stack, so `cpu_mreq_n` stays high for
    // both acknowledge write cycles and RAM is never touched — while the
    // T80N still runs those cycles and still decrements SP (t80n.vhd:
    // 1765-1767 + the I_RST/NMI microcode).
    //
    // NR-C0-04 above pins the bit's READBACK and atic_atac_nmi_test's
    // ATIC-NMI-02 pins the ENABLED behaviour end to end. Neither pins the
    // DISABLED arm, which is what makes bit 3 a control bit rather than a
    // constant: with it clear the acknowledge is a conventional one and
    // the interrupted PC really is written to the stack. Both arms run
    // the same stimulus here, so an emulator that ignored the bit in
    // either direction fails this row.
    const auto nmi_stack_probe = [](Emulator& e, uint8_t nr_c0,
                                    uint16_t& sp_out, uint8_t& lo_out,
                                    uint8_t& hi_out, uint16_t& pc_out,
                                    bool& latch_out) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        e.init(cfg);
        nr_write(e, 0x03, 0x01);          // leave config mode (zxnext.vhd:5147)
        nr_write(e, 0x50, 0x20);          // slot 0 -> RAM page 0x20: 0x0066 is ours
        e.mmu().write(0x0066, 0x00);      // NOP handler; this row stops at entry
        e.mmu().write(0xC000, 0x00);
        e.mmu().write(0xFFFC, 0xA5);      // stack markers
        e.mmu().write(0xFFFD, 0x5A);
        nr_write(e, 0xC0, nr_c0);
        auto r = e.cpu().get_registers();
        r.PC = 0xC000;
        r.SP = 0xFFFE;
        r.IFF1 = 1;
        r.IFF2 = 1;
        e.cpu().set_registers(r);
        e.cpu().request_nmi();
        e.execute_single_instruction();
        const auto at = e.cpu().get_registers();
        sp_out    = at.SP;
        pc_out    = at.PC;
        lo_out    = e.mmu().read(0xFFFC);
        hi_out    = e.mmu().read(0xFFFD);
        latch_out = e.cpu().stackless_retn_active();
    };
    {
        Emulator on_emu;
        uint16_t on_sp = 0, on_pc = 0;
        uint8_t  on_lo = 0, on_hi = 0;
        bool     on_latch = false;
        nmi_stack_probe(on_emu, 0x08, on_sp, on_lo, on_hi, on_pc, on_latch);

        Emulator off_emu;
        uint16_t off_sp = 0, off_pc = 0;
        uint8_t  off_lo = 0, off_hi = 0;
        bool     off_latch = false;
        nmi_stack_probe(off_emu, 0x00, off_sp, off_lo, off_hi, off_pc, off_latch);

        char detail[240];
        std::snprintf(detail, sizeof(detail),
                      "bit3=1: pc=0x%04X sp=0x%04X mem=%s/%s latch=%d | "
                      "bit3=0: pc=0x%04X sp=0x%04X mem=%s/%s latch=%d",
                      on_pc, on_sp, hex2(on_lo).c_str(), hex2(on_hi).c_str(),
                      static_cast<int>(on_latch),
                      off_pc, off_sp, hex2(off_lo).c_str(), hex2(off_hi).c_str(),
                      static_cast<int>(off_latch));
        check("NR-C0-02",
              "NR 0xC0 bit 3 selects the stackless NMI acknowledge: set, the "
              "two acknowledge writes leave RAM untouched and arm the RETN "
              "substitution; clear, the interrupted PC is written to the "
              "stack and no substitution is armed (SP -= 2 either way) "
              "[zxnext.vhd:5598, :2075-2085; t80n.vhd:1765-1767]",
              on_pc == 0x0066 && on_sp == 0xFFFC
                  && on_lo == 0xA5 && on_hi == 0x5A && on_latch
                  && off_pc == 0x0066 && off_sp == 0xFFFC
                  && off_lo == 0x00 && off_hi == 0xC0 && !off_latch,
              detail);
    }

    // ── NR-C5-02 — NR 0xC5 read returns the LIVE ctc_int_en[7:0] ───────
    // VHDL zxnext.vhd:6242 `port_253b_dat <= ctc_int_en`, where
    // ctc_int_en(3:0) is the CTC's exported `o_int_en` (:4089 ->
    // ctc_chan.vhd:276 `o_int_en <= control_reg(7-3)`) and
    // ctc_int_en(7:4) is hardwired "0000" (:4093). Two consequences the
    // read must show and that no other row asserts:
    //
    //   * a WRITE only reaches four bits — :4079 feeds the CTC
    //     `i_int_en <= nr_wr_dat(3 downto 0)` — so NR 0xC5 <- 0xFA reads
    //     back as 0x0A, not 0xFA. A handler that cached the written byte
    //     and replayed it would return 0xFA.
    //   * the value is the channels' live enable, not an NR shadow: a CTC
    //     control word with D7=1 (ctc_chan.vhd:269) changes it with no
    //     NR 0xC5 write in between.
    {
        fresh(emu);
        nr_write(emu, 0xC5, 0xFA);          // ch1 + ch3 enabled; 0xF0 must not stick
        const uint8_t after_write = nr_read(emu, 0xC5);
        emu.port().out(0x1A3B, 0x85);       // ch2 control word, D7=1
        emu.port().out(0x1A3B, 0x40);       // ch2 time constant
        const uint8_t after_cw = nr_read(emu, 0xC5);
        emu.port().out(0x193B, 0x05);       // ch1 control word, D7=0 -> disables ch1
        emu.port().out(0x193B, 0x40);       // ch1 time constant
        const uint8_t after_clear = nr_read(emu, 0xC5);

        char detail[180];
        std::snprintf(detail, sizeof(detail),
                      "NR 0xC5<-0xFA reads %s (expect 0x0a); after ch2 CW D7=1 "
                      "%s (expect 0x0e); after ch1 CW D7=0 %s (expect 0x0c)",
                      hex2(after_write).c_str(), hex2(after_cw).c_str(),
                      hex2(after_clear).c_str());
        check("NR-C5-02",
              "NR 0xC5 read returns the live per-channel ctc_int_en with bits "
              "7:4 hardwired 0 — only nr_wr_dat(3:0) reaches the CTC on a "
              "write, and a control word's D7 moves the same bit "
              "[zxnext.vhd:6242, :4079, :4089, :4093; ctc_chan.vhd:269,276]",
              after_write == 0x0A && after_cw == 0x0E && after_clear == 0x0C,
              detail);
    }

    // ── CTC-JOY-01 — ctc_zc_to(3) drives the joystick pin-7 mux ────────
    // VHDL zxnext.vhd:3518-3524: with NR 0x0B bits 5:4 = "01" the pin-7
    // register toggles on each `ctc_zc_to(3)` pulse. Element 3 of that
    // vector is CTC channel 3 alone (:4088 `o_zc_to => ctc_zc_to(3 downto
    // 0)`), and the toggle is gated on the RAW ZC/TO, not on the
    // channel's interrupt-enable bit — so the CTC works here as a plain
    // clock source with its IRQ off, which is why the control words below
    // carry D7=0.
    //
    // Channel 3 is programmed as a COUNTER (control word D6=1,
    // ctc_chan.vhd:150) with time constant 1, so one external CLK/TRG
    // edge is exactly one ZC/TO (:152-153, :170) and the pulse count is
    // exact instead of a function of how long the row happens to run.
    {
        fresh(emu);
        nr_write(emu, 0x0B, 0x91);          // en=1, iomode="01", iomode_0=1
        const bool p_reset = emu.iomode().pin7();   // '1' per zxnext.vhd:3516
        emu.port().out(0x1B3B, 0x45);       // ch3 CW: counter, TC follows, D7=0
        emu.port().out(0x1B3B, 0x01);       // ch3 time constant = 1
        emu.ctc().trigger(3);
        const bool p1 = emu.iomode().pin7();
        emu.ctc().trigger(3);
        const bool p2 = emu.iomode().pin7();
        // Channels 0..2 are not wired to this mux at all.
        emu.port().out(0x183B, 0x45);
        emu.port().out(0x183B, 0x01);
        emu.ctc().trigger(0);
        const bool p_ch0 = emu.iomode().pin7();

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "pin7 reset=%d, after ch3 ZC/TO #1=%d #2=%d, "
                      "after a ch0 ZC/TO=%d (expect 1,0,1,1)",
                      static_cast<int>(p_reset), static_cast<int>(p1),
                      static_cast<int>(p2), static_cast<int>(p_ch0));
        check("CTC-JOY-01",
              "in NR 0x0B iomode \"01\" each CTC channel-3 ZC/TO toggles "
              "joy_iomode_pin7, and a channel-0 ZC/TO does not "
              "[zxnext.vhd:3518-3524, :4088]",
              p_reset && !p1 && p2 && p_ch0 == p2, detail);
    }

    // ── CTC-JOY-02 — the toggle's guard term ───────────────────────────
    // VHDL zxnext.vhd:3522 gates the toggle on
    //   (nr_0b_joy_iomode_0 = '1' OR joy_iomode_pin7 = '0')
    // so with NR 0x0B bit 0 clear the pin can only ever move TOWARDS '1'
    // and then stops: a ZC/TO arriving while pin7 is already '1' does
    // nothing. CTC-JOY-01 exercises the bit-0 = 1 arm (free-running
    // toggle); this row exercises the other one, in both of its states.
    //
    // pin7 is taken low through iomode "00", whose continuous assignment
    // is `joy_iomode_pin7 <= nr_0b_joy_iomode_0` (:3519-3520) — the one
    // path that can set the register without a ZC/TO.
    {
        fresh(emu);
        nr_write(emu, 0x0B, 0x90);          // en=1, iomode="01", iomode_0=0
        emu.port().out(0x1B3B, 0x45);       // ch3 counter, TC follows
        emu.port().out(0x1B3B, 0x01);
        emu.ctc().trigger(3);
        const bool blocked = emu.iomode().pin7();   // guard false -> still '1'

        nr_write(emu, 0x0B, 0x80);          // iomode "00", iomode_0=0 -> pin7 = 0
        const bool low = emu.iomode().pin7();
        nr_write(emu, 0x0B, 0x90);          // back to "01", iomode_0 still 0
        emu.ctc().trigger(3);
        const bool released = emu.iomode().pin7();  // pin7='0' satisfied the guard
        emu.ctc().trigger(3);
        const bool stuck = emu.iomode().pin7();     // guard false again -> holds

        char detail[190];
        std::snprintf(detail, sizeof(detail),
                      "iomode_0=0: ZC/TO with pin7=1 leaves %d (expect 1); "
                      "iomode \"00\" drives pin7 to %d (expect 0); ZC/TO then "
                      "gives %d (expect 1); a further ZC/TO gives %d (expect 1)",
                      static_cast<int>(blocked), static_cast<int>(low),
                      static_cast<int>(released), static_cast<int>(stuck));
        check("CTC-JOY-02",
              "the pin-7 toggle is conditioned on (nr_0b_joy_iomode_0='1' OR "
              "joy_iomode_pin7='0'): with NR 0x0B bit 0 clear a ZC/TO moves "
              "pin7 only from '0' to '1' and never back "
              "[zxnext.vhd:3519-3524]",
              blocked && !low && released && stuck, detail);
    }
}

// ── Main ──────────────────────────────────────────────────────────────

// ── GH #265 — interrupt timing inside and at the end of an instruction ──
//
// The IM2 fabric used to be ticked straight after each CPU instruction,
// BEFORE the devices (CTC, UART) and the frame/line interrupt events were
// ticked for that instruction. Every request raised during an instruction
// therefore reached the fabric one instruction late, the CPU took it one
// instruction after that, and an interrupt-status read (NR 0x20, 0xC8-0xCA)
// or pulse read (NR 0x22 bit 7) inside an instruction saw the fabric as of
// the previous one. These rows pin the VHDL timeline:
//
//   * a request raised "at edge te" has int_req high during the CLK_28
//     cycle [te, te+1) (im2_peripheral.vhd:90-101); int_status and
//     im2_int_req are set on edge te+1 (:154-178); pulse_int_n falls on the
//     CLK_28 FALLING edge te+0.5 (zxnext.vhd:2017-2031) and is released
//     after pulse_count, advanced on CPU rising edges, reaches 32 (48K/+3)
//     or 36 (:2033-2044);
//   * the ULA frame interrupt int_ula is registered on CLK_7 at
//     hc == c_int_h (zxula_timing.vhd:548-557): high for the pixel after,
//     te = c_int_h*4 + 4 master cycles into the frame (48K: 116*4+4 = 468;
//     128K: (456+128)*4+4 = 2340);
//   * the T80 samples INT_n into INT_s on every CPU rising edge (t80n.vhd:
//     1664) and takes the interrupt at the edge ending an instruction with
//     the INT_s taken on the edge before, i.e. at the start of the
//     instruction's last T-state (:1742-1772). At 3.5 MHz the CPU rising
//     edges are the master cycles that are multiples of 8 (the clock is
//     hc_ula(0) = 0 -> rising, zxnext.vhd:1575, zxnext_top_issue2.vhd:
//     1028-1036; hc_ula is odd on even raw pixels);
//   * so a pulse whose first sampled edge is E_1 is taken at a boundary B
//     with E_1 + 8 <= B <= E_1 + 32*8 (48K) / 36*8 (128K).
//     48K: E_1 = 472 -> B in [60 T, 91 T]; 128K: E_1 = 2344 -> [294, 329].
//   * in IM2 hardware mode a device enters S_REQ on the first CPU rising
//     edge after im2_int_req is set with i_m1_n = '1' (im2_device.vhd:
//     91-107; M1_n is low for T1-T2 of each opcode fetch, t80n.vhd:1729-1731,
//     1761, 1788), its o_int_n is low from then (:150), INT_s one edge
//     later: taken at B >= E_req + 16.
namespace gh265 {

bool build(Emulator& emu, MachineType type) {
    EmulatorConfig cfg;
    cfg.type = type;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// IM 2 table at 0xFD00-0xFE00 (I = 0xFD): every vector reads 0xFDFD.
void install_im2_table(Emulator& emu, bool reti) {
    for (int a = 0xFD00; a <= 0xFE00; ++a)
        emu.mmu().write(static_cast<uint16_t>(a), 0xFD);
    if (reti) {
        emu.mmu().write(0xFDFD, 0xED);
        emu.mmu().write(0xFDFE, 0x4D);
    } else {
        emu.mmu().write(0xFDFD, 0xC9);
    }
}

void write_code(Emulator& emu, uint16_t at, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) emu.mmu().write(at++, b);
}

void set_pc_im2(Emulator& emu, uint16_t pc, bool iff1) {
    auto r = emu.cpu().get_registers();
    r.PC = pc;
    r.SP = 0xFFF0;
    r.I  = 0xFD;
    r.IM = 2;
    r.IFF1 = r.IFF2 = iff1 ? 1 : 0;
    emu.cpu().set_registers(r);
}

// Step the machine (debugger_step(): frames begin and end as in run_frame())
// until PC reaches the ISR, or the clock passes @p limit_t T-states. Returns
// the ISR entry position in T-states from the start of the frame it is in,
// or -1.
long run_to_isr(Emulator& emu, long limit_t) {
    for (int guard = 0; guard < 200000; ++guard) {
        if (emu.cpu().pc() == 0xFDFD)
            return static_cast<long>((emu.clock().get()
                                      - emu.current_frame_cycle()) / 8);
        if (static_cast<long>(emu.clock().get() / 8) > limit_t) return -1;
        emu.debugger_step();
    }
    return -1;
}

// IM 2 (ED 5E, 8 T), EI (4 T), j x LD A,0 (7 T), HALT, from the start of the
// first frame: the HALT's M1 cycles end at 12 + 7j + 4k T, a phase of
// (3j mod 4). Returns the ISR entry, T-states from the frame start.
long halt_isr_entry(MachineType type, int j, bool im2_hw) {
    Emulator emu;
    if (!build(emu, type)) return -2;
    install_im2_table(emu, im2_hw);
    if (im2_hw) nr_write(emu, 0xC0, 0x01);   // hardware IM2 mode
    uint16_t pc = 0x8000;
    write_code(emu, pc, {0xED, 0x5E, 0xFB});
    pc += 3;
    for (int i = 0; i < j; ++i) { write_code(emu, pc, {0x3E, 0x00}); pc += 2; }
    write_code(emu, pc, {0x76});
    set_pc_im2(emu, 0x8000, false);
    return run_to_isr(emu, 2000);
}

// j x LD A,0 then n x NOP, EI, NOP, then NOPs: interrupts disabled until the
// boundary after the NOP that follows EI, at 7j + 4n + 8 T.
long ei_window_isr_entry(MachineType type, int j, int n) {
    Emulator emu;
    if (!build(emu, type)) return -2;
    install_im2_table(emu, false);
    uint16_t pc = 0x8000;
    for (int i = 0; i < j; ++i) { write_code(emu, pc, {0x3E, 0x00}); pc += 2; }
    for (int i = 0; i < n; ++i) write_code(emu, pc++, {0x00});
    write_code(emu, pc++, {0xFB});
    for (int i = 0; i < 400; ++i) write_code(emu, pc++, {0x00});
    set_pc_im2(emu, 0x8000, false);
    return run_to_isr(emu, 2000);
}

// One instruction at 0x8000 run by Z80Cpu::execute() alone, starting at
// master cycle @p start on a machine no frame has begun on (so nothing but
// what the row raises is pending). Returns A.
uint8_t in_nr_direct(Emulator& emu, uint8_t reg, uint64_t start,
                     void (*raise)(Emulator&)) {
    emu.port().out(0x243B, reg);                 // select, outside the IN
    write_code(emu, 0x8000, {0xED, 0x78});        // IN A,(C)
    auto r = emu.cpu().get_registers();
    r.PC = 0x8000;
    r.BC = 0x253B;
    emu.cpu().set_registers(r);
    emu.clock().tick(start - emu.clock().get());
    if (raise) raise(emu);
    emu.cpu().execute();
    return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
}

}  // namespace gh265

static void test_gh265_int_timing() {
    set_group("GH265-INT");
    using namespace gh265;

    // INT-GH265-01 — 48K pulse mode, ISR entry after HALT for the four HALT
    // phases. VHDL window [60, 91] T: taken at the first HALT boundary >= 60
    // (phases 0,3,2,1 -> 60,63,62,61), entry 19 T later (IM 2 acknowledge).
    // Pre-fix: the second boundary at or after the frame interrupt's compare
    // position 58 (entries 83,82,81,84).
    {
        const long want[4] = {79, 82, 81, 80};
        long got[4];
        bool ok = true;
        for (int j = 0; j < 4; ++j) {
            got[j] = halt_isr_entry(MachineType::ZX48K, j, false);
            ok = ok && got[j] == want[j];
        }
        char d[160];
        std::snprintf(d, sizeof d, "entries %ld %ld %ld %ld (want 79 82 81 80)",
                      got[0], got[1], got[2], got[3]);
        check("INT-GH265-01",
              "48K pulse-mode INT taken at the first boundary whose last "
              "T-state starts on a CPU edge that samples the pulse low "
              "(zxula_timing.vhd:548-557; im2_peripheral.vhd:90-101,184-194; "
              "zxnext.vhd:2017-2031; t80n.vhd:1664,1742-1772)",
              ok, d);
    }

    // INT-GH265-02 — the same for 128K: compare (128, 1), te = 2340,
    // E_1 = 2344, window from 294 T: phases 0,3,2,1 -> 296,295,294,297,
    // entries 315,314,313,316. Pre-fix 315,318,317,316.
    {
        const long want[4] = {315, 314, 313, 316};
        long got[4];
        bool ok = true;
        for (int j = 0; j < 4; ++j) {
            got[j] = halt_isr_entry(MachineType::ZX128K, j, false);
            ok = ok && got[j] == want[j];
        }
        char d[160];
        std::snprintf(d, sizeof d, "entries %ld %ld %ld %ld (want 315 314 313 316)",
                      got[0], got[1], got[2], got[3]);
        check("INT-GH265-02",
              "128K pulse-mode INT taken at the first boundary >= 294 T "
              "(zxula_timing.vhd:187,199,548-557; zxnext.vhd:2017-2031; "
              "t80n.vhd:1664,1742-1772)",
              ok, d);
    }

    // INT-GH265-03 — hardware IM2 mode (NR 0xC0 = 1), 48K. im2_int_req is
    // set on edge 469; S_REQ on the first CPU edge after it whose preceding
    // T-state had M1_n high: the HALT's M1 cycles hold M1_n low for T1-T2,
    // so phase 2 (M1 cycle 58-62) cannot take edges 59/60 and waits for 61.
    // Taken at B >= E_req + 16: phases 0,3,2,1 -> 64,63,66,65, entries
    // 83,82,85,84. Pre-fix (and without the M1 gate, phase 2) 83,82,81,84.
    {
        const long want[4] = {83, 82, 85, 84};
        long got[4];
        bool ok = true;
        for (int j = 0; j < 4; ++j) {
            got[j] = halt_isr_entry(MachineType::ZX48K, j, true);
            ok = ok && got[j] == want[j];
        }
        char d[160];
        std::snprintf(d, sizeof d, "entries %ld %ld %ld %ld (want 83 82 85 84)",
                      got[0], got[1], got[2], got[3]);
        check("INT-GH265-03",
              "hardware-IM2 INT: S_REQ on the first CPU edge after "
              "im2_int_req with M1_n high, INT_s one edge later "
              "(im2_peripheral.vhd:167-178; im2_device.vhd:91-107,150; "
              "t80n.vhd:1729-1731,1761,1788)",
              ok, d);
    }

    // INT-GH265-04 — the end of the 48K window: EI then NOP, so the first
    // boundary interrupts are enabled at is 7j + 4n + 8 T. At 91 T (j=1,
    // n=19) it is the last one INT_s is set for (E_N = 472 + 31*8 = 720,
    // B - 8 = 720): taken, entry 110. At 92 T (j=0, n=21) the pulse has gone.
    // Pre-fix the window was [63, 95]: taken at 92 as well.
    {
        const long in_window  = ei_window_isr_entry(MachineType::ZX48K, 1, 19);
        const long past_end   = ei_window_isr_entry(MachineType::ZX48K, 0, 21);
        char d[120];
        std::snprintf(d, sizeof d, "last=%ld (want 110) past=%ld (want -1)",
                      in_window, past_end);
        check("INT-GH265-04",
              "48K pulse: 32 CPU edges sample it low, the last at "
              "E_1 + 31*8; the boundary after that edge is the last taken "
              "(zxnext.vhd:2033-2044; t80n.vhd:1664,1742-1772)",
              in_window == 110 && past_end == -1, d);
    }

    // INT-GH265-05 — the same end for 128K (36 edges): last boundary
    // 2344 + 36*8 = 2632 = 329 T (j=3, n=75: entry 348), 330 T (j=2, n=77)
    // is past it. Pre-fix the window ended at 298 + 36 = 334 T.
    {
        const long in_window = ei_window_isr_entry(MachineType::ZX128K, 3, 75);
        const long past_end  = ei_window_isr_entry(MachineType::ZX128K, 2, 77);
        char d[120];
        std::snprintf(d, sizeof d, "last=%ld (want 348) past=%ld (want -1)",
                      in_window, past_end);
        check("INT-GH265-05",
              "128K pulse: 36 CPU edges, last boundary E_1 + 36*8 "
              "(zxnext.vhd:2033 pulse_count(5) and pulse_count(2))",
              in_window == 348 && past_end == -1, d);
    }

    // INT-GH265-06 — an interrupt still pending when the frame ends is
    // taken in the next frame. The pulse knows nothing of frames
    // (zxnext.vhd:2017-2044); jnext restarts its T-state counter at every
    // frame, and a request made before the restart used to be dropped at
    // the first boundary after it as "expired". A CTC0 ZC/TO (pulse mode,
    // NR 0xC5 = 1) is raised with interrupts disabled 20 T before the end
    // of the frame, the clock is run past the frame end, and interrupts
    // are enabled 1 T-state... at the next boundary: the IntAck follows.
    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZX48K);
        install_im2_table(emu, false);
        for (int a = 0x8000; a < 0x9000; ++a) emu.mmu().write(static_cast<uint16_t>(a), 0x00);
        set_pc_im2(emu, 0x8000, false);
        nr_write(emu, 0xC5, 0x01);
        const uint64_t frame = 69888ULL * 8;
        long taken_at = -1;
        uint64_t raised_at = 0;
        if (ok) {
            while (emu.clock().get() < frame - 20 * 8) emu.debugger_step();
            raised_at = emu.clock().get();
            emu.im2().raise_req(Im2Controller::DevIdx::CTC0, raised_at);
            while (emu.clock().get() < frame) emu.debugger_step();
            auto r = emu.cpu().get_registers();
            r.IFF1 = r.IFF2 = 1;
            emu.cpu().set_registers(r);
            emu.debugger_step();
            if (emu.cpu().pc() == 0xFDFD)
                taken_at = static_cast<long>(emu.clock().get() / 8);
        }
        char d[120];
        std::snprintf(d, sizeof d, "raised at %llu, ISR entered at %ld T (want > %llu)",
                      static_cast<unsigned long long>(raised_at / 8), taken_at,
                      static_cast<unsigned long long>(frame / 8));
        check("INT-GH265-06",
              "a pulse straddling the frame edge is still taken after it "
              "(zxnext.vhd:2017-2044 has no frame term)",
              ok && taken_at > static_cast<long>(frame / 8), d);
    }

    // INT-GH265-10 — the same straddling pulse through a snapshot taken at
    // the frame edge. The pulse is a timeline, not a counter
    // (zxnext.vhd:2017-2044): restored into another machine it must still be
    // taken at the first enabled boundary after the edge. The window lives
    // on jnext's per-frame T-state counter, which a load does not restore
    // (it is re-seeded at the next frame start); written absolute, a
    // restored window sat a whole frame ahead of the new counter.
    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZX48K);
        install_im2_table(emu, false);
        for (int a = 0x8000; a < 0x9000; ++a) emu.mmu().write(static_cast<uint16_t>(a), 0x00);
        set_pc_im2(emu, 0x8000, false);
        nr_write(emu, 0xC5, 0x01);
        const uint64_t frame = 69888ULL * 8;
        long taken_at = -1;
        if (ok) {
            while (emu.clock().get() < frame - 20 * 8) emu.debugger_step();
            emu.im2().raise_req(Im2Controller::DevIdx::CTC0, emu.clock().get());
            while (emu.clock().get() < frame) emu.debugger_step();
            StateWriter measure;
            emu.save_state(measure);
            std::vector<uint8_t> buf(measure.position(), 0);
            StateWriter w(buf.data(), buf.size());
            emu.save_state(w);
            Emulator emu2;
            ok = build(emu2, MachineType::ZX48K);
            StateReader r(buf.data(), buf.size());
            ok = ok && emu2.load_state(r);
            auto regs = emu2.cpu().get_registers();
            regs.IFF1 = regs.IFF2 = 1;
            emu2.cpu().set_registers(regs);
            emu2.debugger_step();
            if (emu2.cpu().pc() == 0xFDFD)
                taken_at = static_cast<long>(emu2.clock().get() / 8);
        }
        char d[120];
        std::snprintf(d, sizeof d, "restored machine entered the ISR at %ld T (want > %llu)",
                      taken_at, static_cast<unsigned long long>(frame / 8));
        check("INT-GH265-10",
              "a pulse straddling the frame edge survives a snapshot taken there "
              "(zxnext.vhd:2017-2044)",
              ok && taken_at > static_cast<long>(frame / 8), d);
    }

    // INT-GH265-11 — a pulse pending across a CPU-speed change. The pulse
    // counts CPU clock edges (zxnext.vhd:2035-2044): whatever the speed, it
    // is low for 36 of them (Next timing), so the edges still to come after
    // a change arrive at the new rate. jnext's /INT window is on its T-state
    // counter, which NR 0x07 (committed at the next bus-idle boundary)
    // re-bases in the new unit, and the fabric's pulse edges are CLK_28
    // edges. A CTC0 pulse is raised with interrupts off at 3.5 MHz (first
    // CPU edge 8 cycles on); NR 0x07 = 3 (28 MHz) is committed at the end of
    // the next instruction, 8 edges into the pulse, so 28 remain and the last
    // boundary that takes it is 29 T-states on. Interrupts are enabled
    // after k more NOPs (4 T each at 28 MHz): taken for k = 2 (8 T), not
    // for k = 10 (40 T), when the pulse is over — pulse_int_n high again.
    {
        auto run = [&](int k, bool& taken, bool& pulse_high, int& divisor) {
            Emulator emu;
            if (!build(emu, MachineType::ZXN_ISSUE2)) return false;
            install_im2_table(emu, false);
            for (int a = 0x8000; a < 0x9000; ++a) emu.mmu().write(static_cast<uint16_t>(a), 0x00);
            set_pc_im2(emu, 0x8000, false);
            nr_write(emu, 0xC5, 0x01);
            while (emu.clock().get() < 20000ULL * 8) emu.debugger_step();
            emu.im2().raise_req(Im2Controller::DevIdx::CTC0, emu.clock().get());
            emu.debugger_step();                 // the pulse starts, its window is set
            nr_write(emu, 0x07, 0x03);           // 28 MHz from the next boundary
            emu.debugger_step();
            divisor = static_cast<int>(emu.clock().cpu_divisor());
            for (int n = 0; n < k; ++n) emu.debugger_step();
            pulse_high = emu.im2().pulse_int_n();
            auto r = emu.cpu().get_registers();
            r.IFF1 = r.IFF2 = 1;
            emu.cpu().set_registers(r);
            emu.debugger_step();
            taken = emu.cpu().pc() == 0xFDFD;
            return true;
        };
        bool t2 = false, h2 = true, t10 = true, h10 = false;
        int d2 = -1, d10 = -1;
        const bool ok = run(2, t2, h2, d2) && run(10, t10, h10, d10);
        char d[160];
        std::snprintf(d, sizeof d, "k=2: divisor %d taken %d pulse_int_n %d; "
                      "k=10: taken %d pulse_int_n %d (want 1 1 0; 0 1)",
                      d2, t2 ? 1 : 0, h2 ? 1 : 0, t10 ? 1 : 0, h10 ? 1 : 0);
        check("INT-GH265-11",
              "a pulse pending across a CPU-speed change lasts its remaining "
              "CPU edges at the new speed (zxnext.vhd:2035-2044)",
              ok && d2 == 1 && t2 && !h2 && !t10 && h10, d);
    }

    // INT-GH265-12 — INT-GH265-07's EI, the last instruction of the frame,
    // then a snapshot there, restored into another machine: the grace
    // (t80n.vhd:1768, no interrupt at the boundary straight after EI) must
    // survive — the NOP after EI runs, the IntAck comes one instruction
    // later. FUSE's EI stamp used to be saved as a counter value, and a
    // load re-seeds the counter, so the restored boundary took the pending
    // interrupt at once.
    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZX48K);
        install_im2_table(emu, false);
        for (int a = 0x8000; a < 0x9000; ++a) emu.mmu().write(static_cast<uint16_t>(a), 0x00);
        emu.mmu().write(0x7000, 0xFB);            // EI
        emu.mmu().write(0x7001, 0x00);
        set_pc_im2(emu, 0x8000, false);
        nr_write(emu, 0xC5, 0x01);
        const uint64_t frame = 69888ULL * 8;
        uint16_t pc_next = 0, pc_second = 0;
        if (ok) {
            while (emu.clock().get() < frame - 20 * 8) emu.debugger_step();
            emu.im2().raise_req(Im2Controller::DevIdx::CTC0, emu.clock().get());
            while (emu.clock().get() < frame - 4 * 8) emu.debugger_step();
            auto r = emu.cpu().get_registers();
            r.PC = 0x7000;
            emu.cpu().set_registers(r);
            emu.debugger_step();                  // EI, the frame's last instruction
            ok = ok && emu.clock().get() >= frame;
            StateWriter measure;
            emu.save_state(measure);
            std::vector<uint8_t> buf(measure.position(), 0);
            StateWriter w(buf.data(), buf.size());
            emu.save_state(w);
            Emulator emu2;
            ok = ok && build(emu2, MachineType::ZX48K);
            // A running machine, as a rewind restores into: its counter is
            // not at 0 when the snapshot is loaded.
            for (int n = 0; n < 100; ++n) emu2.debugger_step();
            StateReader rd(buf.data(), buf.size());
            ok = ok && emu2.load_state(rd);
            emu2.debugger_step();                 // NOP (grace)
            pc_next = emu2.cpu().pc();
            emu2.debugger_step();                 // IntAck
            pc_second = emu2.cpu().pc();
        }
        char d[120];
        std::snprintf(d, sizeof d, "restored: pc after 1 step = 0x%04X (want 0x7002), "
                      "after 2 = 0x%04X (want 0xFDFD)", pc_next, pc_second);
        check("INT-GH265-12",
              "EI grace survives a snapshot taken straight after the EI "
              "(t80n.vhd:1768 SetEI = '0')",
              ok && pc_next == 0x7002 && pc_second == 0xFDFD, d);
    }

    // INT-GH265-07 — EI as the last instruction of a frame keeps its grace:
    // t80n.vhd:1768 takes no interrupt at the boundary straight after EI
    // (SetEI = '1'), even though that boundary is the first of a new frame.
    // A CTC0 pulse is pending across the edge; EI is placed so it ends on or
    // after it. The next step must run the NOP after EI, the one after that
    // the IntAck.
    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZX48K);
        install_im2_table(emu, false);
        for (int a = 0x8000; a < 0x9000; ++a) emu.mmu().write(static_cast<uint16_t>(a), 0x00);
        // EI and the NOP after it at 0x7000: the NOPs run from 0x8000 cover
        // 17472 bytes in a frame and never reach it.
        emu.mmu().write(0x7000, 0xFB);            // EI
        emu.mmu().write(0x7001, 0x00);
        set_pc_im2(emu, 0x8000, false);
        nr_write(emu, 0xC5, 0x01);
        const uint64_t frame = 69888ULL * 8;
        uint16_t pc_after_ei_next = 0, pc_after_second = 0;
        if (ok) {
            while (emu.clock().get() < frame - 20 * 8) emu.debugger_step();
            emu.im2().raise_req(Im2Controller::DevIdx::CTC0, emu.clock().get());
            while (emu.clock().get() < frame - 4 * 8) emu.debugger_step();
            auto r = emu.cpu().get_registers();
            r.PC = 0x7000;
            emu.cpu().set_registers(r);
            emu.debugger_step();                  // EI, ends at/after frame end
            ok = ok && emu.clock().get() >= frame;
            emu.debugger_step();                  // NOP (grace)
            pc_after_ei_next = emu.cpu().pc();
            emu.debugger_step();                  // IntAck
            pc_after_second = emu.cpu().pc();
        }
        char d[120];
        std::snprintf(d, sizeof d, "pc after EI+1 = 0x%04X (want 0x7002), "
                      "after EI+2 = 0x%04X (want 0xFDFD)",
                      pc_after_ei_next, pc_after_second);
        check("INT-GH265-07",
              "EI grace across the frame edge (t80n.vhd:1768 SetEI = '0')",
              ok && pc_after_ei_next == 0x7002 && pc_after_second == 0xFDFD, d);
    }

    // INT-GH265-08 — in hardware IM2 mode the ULA is the one device that
    // still pulses when the CPU is not in IM 2 (im2_peripheral.vhd:192,
    // EXCEPTION = '1'), and that pulse reaches the CPU through the same
    // AND as any other (zxnext.vhd:1840). NR 0xC0 = 1, CPU in IM 1 with
    // interrupts enabled, ULA request: the CPU restarts at 0x0038.
    // Pre-fix the pulse was only ever requested in pulse mode.
    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZX48K);
        nr_write(emu, 0xC0, 0x01);
        for (int a = 0x8000; a < 0x8100; ++a) emu.mmu().write(static_cast<uint16_t>(a), 0x00);
        auto r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFFF0; r.IM = 1; r.IFF1 = r.IFF2 = 1;
        emu.cpu().set_registers(r);
        emu.im2().raise_req(Im2Controller::DevIdx::ULA, emu.clock().get());
        bool rst38 = false;
        for (int i = 0; i < 4 && ok; ++i) {
            emu.execute_single_instruction();
            if (emu.cpu().pc() == 0x0038) rst38 = true;
        }
        check("INT-GH265-08",
              "IM2 hardware mode, CPU in IM 1: the ULA's exception pulse "
              "is taken (im2_peripheral.vhd:192; zxnext.vhd:1840)",
              ok && rst38,
              "pc=" + std::to_string(emu.cpu().pc()));
    }

    // INT-GH265-09 — NR 0x20 (unqualified ULA request) written by OUT (C),A
    // interrupts at the boundary right after the OUT. OUT (C),A = ED 79:
    // I/O cycle at 8 T; IORQ+WR from the edge 9 T in; cpu_req on the next
    // CLK_28 edge; nr_20_we (so int_unq) high for the cycle that starts
    // (zxnext.vhd:4747-4777,1946-1947): te = start + 73, pulse falls at
    // start + 73.5, first CPU edge after it start + 80, taken at a boundary
    // >= start + 88 — the OUT ends at start + 96. Pre-fix: one instruction
    // later.
    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZX48K);
        install_im2_table(emu, false);
        emu.port().out(0x243B, 0x20);
        write_code(emu, 0x8000, {0xED, 0x79, 0x00, 0x00});   // OUT (C),A; NOP; NOP
        set_pc_im2(emu, 0x8000, true);
        auto r = emu.cpu().get_registers();
        r.BC = 0x253B;
        r.AF = static_cast<uint16_t>(0x4000 | (r.AF & 0x00FF));
        emu.cpu().set_registers(r);
        emu.execute_single_instruction();          // OUT
        emu.execute_single_instruction();          // IntAck (pre-fix: NOP)
        check("INT-GH265-09",
              "NR 0x20 unqualified request taken at the boundary after the "
              "OUT that writes it (zxnext.vhd:1946-1947,4747-4777; "
              "t80n.vhd:1664,1742-1772)",
              ok && emu.cpu().pc() == 0xFDFD,
              "pc=" + std::to_string(emu.cpu().pc()) + " (want 0xFDFD)");
    }
}

// ── GH #265 — interrupt status and pulse reads at the IN's latch point ──
//
// An IN from 0x253B returns port_253b_dat_0, reloaded from port_253b_dat on
// every CLK_CPU falling edge; port_253b_dat takes im2_int_status /
// NOT pulse_int_n on CLK_28 (zxnext.vhd:5871-5882, 5991-5992, 6247-6254).
// The IN latches the reload made 2.5 T-states into its I/O cycle, so for
// IN A,(C) (I/O cycle at 8 T) the port_253b_dat load edge is Sn =
// start + 64 + 19 = start + 83 and it sees im2_int_status as it was before
// that edge: a status set on edge te+1 is seen iff te + 2 <= Sn, pulse_int_n
// (low from te+0.5) iff te + 1 <= Sn.
static void test_gh265_status_reads() {
    set_group("GH265-ISC");
    using namespace gh265;

    // ISC-GH265-01 — NR 0xC8 bit 0 (ULA) at the latch edge. Raised at
    // te = Sn - 2: set on Sn - 1, seen. Raised at te = Sn - 1: set on Sn
    // itself, not seen. Pre-fix neither: the request reached the fabric
    // only after the instruction.
    {
        constexpr uint64_t start = 5000;
        constexpr uint64_t sn    = start + 83;
        static uint64_t te;
        auto raise = [](Emulator& e) {
            e.im2().raise_req(Im2Controller::DevIdx::ULA, te);
        };
        Emulator a, b;
        bool ok = build(a, MachineType::ZX48K) && build(b, MachineType::ZX48K);
        te = sn - 2;
        const uint8_t seen = in_nr_direct(a, 0xC8, start, raise);
        te = sn - 1;
        const uint8_t unseen = in_nr_direct(b, 0xC8, start, raise);
        check("ISC-GH265-01",
              "NR 0xC8 read by IN A,(C) sees a status set before the "
              "port_253b_dat load 83 cycles in, not one set on it "
              "(zxnext.vhd:5871-5882,6247-6248; im2_peripheral.vhd:154-162)",
              ok && seen == 0x01 && unseen == 0x00,
              "seen=" + hex2(seen) + " unseen=" + hex2(unseen) + " (want 0x01, 0x00)");
    }

    // ISC-GH265-02 — end to end on a running 48K: the frame interrupt
    // (te = 468) polled by `IN A,(C) / AND 1 / JR Z` from the frame start.
    // Prologue LD BC,0x243B / LD A,0xC8 / OUT (C),A / INC B = 33 T, then a
    // pad puts the first IN at 48 T or 49 T. The status is seen when
    // start*8 + 83 >= 470: the IN at 49 T (Sn 475) sees it, the one at 48 T
    // (Sn 467) does not and the loop runs one more 31-T turn.
    // Pre-fix the IN at 49 T missed it too (the event was raised after it).
    {
        auto ins_until_seen = [](int pad_nops_then_ld) -> int {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return -1;
            uint16_t pc = 0x8000;
            write_code(emu, pc, {0x01, 0x3B, 0x24, 0x3E, 0xC8, 0xED, 0x79, 0x04});
            pc += 8;
            if (pad_nops_then_ld == 15) {          // 15 T: NOP NOP LD D,0
                write_code(emu, pc, {0x00, 0x00, 0x16, 0x00}); pc += 4;
            } else {                               // 16 T: 4 x NOP
                write_code(emu, pc, {0x00, 0x00, 0x00, 0x00}); pc += 4;
            }
            const uint16_t loop = pc;
            write_code(emu, pc, {0xED, 0x78, 0xE6, 0x01, 0x28, 0xFA, 0x76});
            auto r = emu.cpu().get_registers();
            r.PC = 0x8000; r.IFF1 = r.IFF2 = 0;
            emu.cpu().set_registers(r);
            int ins = 0;
            for (int g = 0; g < 2000; ++g) {
                if (emu.cpu().pc() == loop + 6) return ins;   // reached HALT
                if (emu.cpu().pc() == loop) ++ins;
                emu.debugger_step();
            }
            return -1;
        };
        const int at48 = ins_until_seen(15);
        const int at49 = ins_until_seen(16);
        check("ISC-GH265-02",
              "polling NR 0xC8 for the frame interrupt: an IN starting at "
              "49 T sees it, one at 48 T does not "
              "(zxula_timing.vhd:548-557; im2_peripheral.vhd:154-162; "
              "zxnext.vhd:5871-5882,6247-6248)",
              at48 == 2 && at49 == 1,
              "INs at 48 T: " + std::to_string(at48) + " (want 2), at 49 T: "
              + std::to_string(at49) + " (want 1)");
    }

    // ISC-GH265-03 — NR 0x22 bit 7 = NOT pulse_int_n, pulse start. Pulse
    // mode, ULA raised at te: pulse_int_n falls at te + 0.5, so the load on
    // edge Sn captures it low iff te + 1 <= Sn: te = Sn - 1 reads bit 7,
    // te = Sn does not.
    {
        constexpr uint64_t start = 5000;
        constexpr uint64_t sn    = start + 83;
        static uint64_t te;
        auto raise = [](Emulator& e) {
            e.im2().raise_req(Im2Controller::DevIdx::ULA, te);
        };
        Emulator a, b;
        bool ok = build(a, MachineType::ZX48K) && build(b, MachineType::ZX48K);
        te = sn - 1;
        const uint8_t low = in_nr_direct(a, 0x22, start, raise);
        te = sn;
        const uint8_t high = in_nr_direct(b, 0x22, start, raise);
        check("ISC-GH265-03",
              "NR 0x22 bit 7 sees pulse_int_n fall on the CLK_28 falling "
              "edge after the request (zxnext.vhd:2017-2031,5991-5992)",
              ok && (low & 0x80) != 0 && (high & 0x80) == 0,
              "te=Sn-1: " + hex2(low) + " te=Sn: " + hex2(high));
    }

    // ISC-GH265-04 — NR 0x22 bit 7, pulse end. The pulse is started by an
    // instruction (a NOP whose first edge is 5000, raised at 5000: E_1 =
    // 5008), so its last low edge is E_N = 5008 + 31*8 = 5256 (48K): an IN
    // whose load edge Sn is 5256 reads bit 7 set, one at 5257 reads it clear
    // (pulse_int_n rises at 5256.5). Pre-fix the bit was the pulse state at
    // the end of the last instruction.
    {
        auto read_at = [](uint64_t sn) -> uint8_t {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return 0xEE;
            emu.mmu().write(0x7000, 0x00);                 // NOP
            auto r = emu.cpu().get_registers();
            r.PC = 0x7000;
            emu.cpu().set_registers(r);
            emu.clock().tick(5000 - emu.clock().get());
            emu.im2().raise_req(Im2Controller::DevIdx::ULA, 5000);
            emu.execute_single_instruction();              // starts the pulse
            return in_nr_direct(emu, 0x22, sn - 83, nullptr);
        };
        const uint8_t last  = read_at(5256);
        const uint8_t after = read_at(5257);
        check("ISC-GH265-04",
              "NR 0x22 bit 7 clears on the load edge after the 32nd CPU edge "
              "of the pulse (zxnext.vhd:2033-2044,5991-5992)",
              (last & 0x80) != 0 && (after & 0x80) == 0,
              "Sn=E_N: " + hex2(last) + " Sn=E_N+1: " + hex2(after));
    }

    // ISC-GH265-05 — NR 0xC8 clear against a request on the same edge. OUT
    // (C),A of 0x01 to NR 0xC8: the clear commits on edge start + 74
    // (IORQ+WR at 72, cpu_req 73, nr_c8_we during [73,74) — zxnext.vhd:
    // 4747-4777,1952-1955). A request raised at 72 set the status on 73 and
    // is cleared; one raised at 73 sets it on 74, where im2_peripheral.vhd:160
    // gives the request priority, and survives. Pre-fix both were cleared
    // (the request met the fabric before the deferred clear).
    {
        auto after_clear = [](uint64_t te_off) -> uint8_t {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return 0xEE;
            emu.port().out(0x243B, 0xC8);
            write_code(emu, 0x8000, {0xED, 0x79});        // OUT (C),A
            auto r = emu.cpu().get_registers();
            r.PC = 0x8000; r.BC = 0x253B;
            r.AF = static_cast<uint16_t>(0x0100 | (r.AF & 0x00FF));
            emu.cpu().set_registers(r);
            emu.clock().tick(5000 - emu.clock().get());
            emu.im2().raise_req(Im2Controller::DevIdx::ULA, 5000 + te_off);
            emu.execute_single_instruction();
            return nr_read(emu, 0xC8);
        };
        const uint8_t before_edge = after_clear(72);
        const uint8_t on_edge     = after_clear(73);
        check("ISC-GH265-05",
              "NR 0xC8 clear commits on its edge: a request set before it "
              "is cleared, one set on it survives (im2_peripheral.vhd:160; "
              "zxnext.vhd:1952-1955,4747-4777)",
              before_edge == 0x00 && on_edge == 0x01,
              "te=72: " + hex2(before_edge) + " te=73: " + hex2(on_edge)
              + " (want 0x00, 0x01)");
    }

    // ISC-GH265-06 — NR 0xC5 CTC interrupt enable against a ZC/TO, hardware
    // IM2 mode. The enable commits on start + 74 (as above); the request edge
    // is qualified with i_int_en as it is during its own cycle
    // (im2_peripheral.vhd:167-178): raised at 73 it meets the old (disabled)
    // enable and CTC0 stays S_0, raised at 74 it meets the new one and CTC0
    // reaches S_REQ. Pre-fix both met the new enable.
    {
        auto state_after = [](uint64_t te_off) -> int {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return -1;
            nr_write(emu, 0xC0, 0x01);
            nr_write(emu, 0xC5, 0x00);
            emu.im2().on_m1_cycle(0x0000, 0xED);   // decoder: IM 2
            emu.im2().on_m1_cycle(0x0001, 0x5E);
            emu.port().out(0x243B, 0xC5);
            write_code(emu, 0x8000, {0xED, 0x79});        // OUT (C),A
            auto r = emu.cpu().get_registers();
            r.PC = 0x8000; r.BC = 0x253B; r.IFF1 = r.IFF2 = 0;
            r.AF = static_cast<uint16_t>(0x0100 | (r.AF & 0x00FF));
            emu.cpu().set_registers(r);
            emu.clock().tick(5000 - emu.clock().get());
            emu.im2().raise_req(Im2Controller::DevIdx::CTC0, 5000 + te_off);
            emu.execute_single_instruction();
            return static_cast<int>(emu.im2().state(Im2Controller::DevIdx::CTC0));
        };
        const int before_edge = state_after(73);
        const int on_edge     = state_after(74);
        check("ISC-GH265-06",
              "NR 0xC5 enable commits on its edge: a ZC/TO before it is "
              "not latched, one on it is (im2_peripheral.vhd:167-178; "
              "zxnext.vhd:1949,4747-4777)",
              before_edge == 0 && on_edge == 1,
              "te=73: " + std::to_string(before_edge) + " te=74: "
              + std::to_string(on_edge) + " (want S_0=0, S_REQ=1)");
    }

    // ISC-GH265-07 — the line interrupt (NR 0xC8 bit 1) on the same
    // pipeline. 48K, NR 0x23 = 10 (int_line_num = 9, zxula_timing.vhd:
    // 566-570) enabled by NR 0x22 bit 1: the compare cvc = 9 at hc_ula = 255
    // (:574-583) is raw line 9 + 64 (cvc origin c_min_vactive, :455-472),
    // raw pixel 117 + 255 = 372 (hc_ula resets at c_min_hactive - 12, one
    // pixel late, :423-436): master cycle (73*448 + 372)*4 = 132304.
    // int_line is registered, so int_req is high from 132308; the status is
    // set on 132309 and an IN A,(C) loading on 132310 (start 132227) sees
    // it, one loading on 132309 (start 132226) does not.
    {
        auto seen_at = [](uint64_t start) -> uint8_t {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return 0xEE;
            nr_write(emu, 0x23, 10);
            nr_write(emu, 0x22, 0x02);
            return in_nr_direct(emu, 0xC8, start, nullptr);
        };
        const uint8_t seen   = seen_at(132227);
        const uint8_t unseen = seen_at(132226);
        check("ISC-GH265-07",
              "NR 0xC8 bit 1 sees the line interrupt from its registered "
              "int_line, one pixel after the hc_ula = 255 compare "
              "(zxula_timing.vhd:423-436,455-472,566-583; "
              "im2_peripheral.vhd:154-162; zxnext.vhd:5871-5882)",
              (seen & 0x02) != 0 && (unseen & 0x02) == 0,
              "start 132227: " + hex2(seen) + " start 132226: " + hex2(unseen));
    }

    // ISC-GH265-08 — a request while the pulse is still low starts no new
    // one (zxnext.vhd:2023-2031 only lets pulse_en in while pulse_int_n =
    // '1'). A ULA pulse from an instruction starting at 5000 (raised at
    // 5000: E_1 = 5008, last low edge E_N = 5256, pulse_int_n high from
    // 5256.5). A CTC0 request at 5256 falls on the falling edge 5256.5, when
    // pulse_int_n is still '0': ignored, NR 0x22 bit 7 reads clear at 5300.
    // At 5257 pulse_int_n is '1': a new pulse, bit 7 set.
    {
        auto bit7_after = [](uint64_t te2) -> uint8_t {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return 0xEE;
            nr_write(emu, 0xC5, 0x01);
            emu.mmu().write(0x7000, 0x00);
            auto r = emu.cpu().get_registers();
            r.PC = 0x7000;
            emu.cpu().set_registers(r);
            emu.clock().tick(5000 - emu.clock().get());
            emu.im2().raise_req(Im2Controller::DevIdx::ULA, 5000);
            emu.execute_single_instruction();
            emu.im2().raise_req(Im2Controller::DevIdx::CTC0, te2);
            return in_nr_direct(emu, 0x22, 5300 - 83, nullptr);
        };
        const uint8_t ignored = bit7_after(5256);
        const uint8_t started = bit7_after(5257);
        check("ISC-GH265-08",
              "a request reaching the pulse fabric before pulse_int_n has "
              "returned to '1' is lost; one after it starts a new pulse "
              "(zxnext.vhd:2017-2044,5991-5992)",
              (ignored & 0x80) == 0 && (started & 0x80) != 0,
              "te2=5256: " + hex2(ignored) + " te2=5257: " + hex2(started));
    }
}

// ── GH #265 — CTC port reads and writes at their bus timing ──────────
//
// port_ctc_dat is reloaded from the channel's t_count on every CLK_CPU
// falling edge (zxnext.vhd:4095-4100) and the IN latches the reload made
// 2.5 T-states into its I/O cycle (t80na.vhd:214-222, t80n.vhd:1781-1782):
// for IN A,(C) that is the count after edge start + 83. A write reaches the
// channel as iowr from the CPU edge IORQ+WR assert on — the second clock of
// the I/O cycle (t80na.vhd:148-150) — and is taken on the CLK_28 edge after
// (ctc_chan.vhd:246-254). The CTC used to be ticked only between
// instructions, so a read saw the count at the instruction's START (~5
// counts high at /16) and a write took effect there.
static void test_gh265_ctc_ports() {
    set_group("GH265-CTC");
    using namespace gh265;

    // CTC-RD-GH265-01 — a real program: LD BC,0x183B; LD A,0x05 (timer
    // /16, constant follows); OUT (C),A; LD A,0x80; OUT (C),A; LD D,0;
    // IN A,(C). The constant's OUT has its I/O cycle at 44 T: taken on edge
    // 45*8+1 = 361, one S_TRIGGER edge (ctc_chan.vhd:214-226), counts on
    // 378 + 16k. The IN starts at 55 T: load edge 55*8+83 = 523, after ten
    // counts (378..522): 0x80 - 10 = 0x76. Pre-fix 0x77 (the write took
    // effect at its instruction's start and the read saw the count there).
    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZX48K);
        write_code(emu, 0x8000, {0x01, 0x3B, 0x18, 0x3E, 0x05, 0xED, 0x79,
                                 0x3E, 0x80, 0xED, 0x79, 0x16, 0x00,
                                 0xED, 0x78, 0x76});
        auto r = emu.cpu().get_registers();
        r.PC = 0x8000;
        emu.cpu().set_registers(r);
        for (int i = 0; i < 7 && ok; ++i) emu.execute_single_instruction();
        const uint8_t v = static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
        check("CTC-RD-GH265-01",
              "CTC programmed and read by OUT/IN: written on its commit edge, "
              "read at the port_ctc_dat reload (zxnext.vhd:4095-4100; "
              "ctc_chan.vhd:214-226,246-254; t80na.vhd:148-150,214-222)",
              ok && v == 0x76, "A=" + hex2(v) + " (want 0x76)");
    }

    // CTC-RD-GH265-02 — the read edge exactly. The channel is programmed
    // outside any instruction (constant 0x80 on edge 0: counts on 17 + 16k)
    // and IN A,(C) of 0x183B starts at @p s; its load edge s + 83 counts
    // the count ON it. s = 734: edge 817 = 17 + 16*50, 51 counts, 0x4D.
    // s = 733: edge 816, 50 counts, 0x4E. Pre-fix both read the count at
    // s: 45 counts, 0x5B.
    {
        auto read_at = [](uint64_t s) -> uint8_t {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return 0xEE;
            emu.port().out(0x183B, 0x05);
            emu.port().out(0x183B, 0x80);
            write_code(emu, 0x8000, {0xED, 0x78});
            auto r = emu.cpu().get_registers();
            r.PC = 0x8000; r.BC = 0x183B;
            emu.cpu().set_registers(r);
            emu.ctc().tick(static_cast<uint32_t>(s));   // devices stand at the clock
            emu.clock().tick(s - emu.clock().get());
            emu.cpu().execute();
            return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
        };
        const uint8_t on_count = read_at(734);
        const uint8_t before   = read_at(733);
        check("CTC-RD-GH265-02",
              "IN of a CTC port latches t_count as of the edge before the "
              "port_ctc_dat reload 83 cycles in (zxnext.vhd:4095-4100)",
              on_count == 0x4D && before == 0x4E,
              "s=734: " + hex2(on_count) + " s=733: " + hex2(before)
              + " (want 0x4D, 0x4E)");
    }

    // CTC-WR-GH265-01 — a write inside an instruction takes effect on its
    // commit edge, and the ZC/TO it leads to is stamped with its own edge.
    // Program ch0 (int on, timer /16, TC follows) outside, then OUT (C),A
    // of TC=8 at start 5000: commit edge 5000 + 9*8 + 1 = 5073, S_TRIGGER
    // until 5074, eighth count and ZC/TO on 5074 + 8*16 = 5202, status set
    // on 5203 (im2_peripheral.vhd:154-162) — an NR 0xC9 IN whose load edge
    // is 5204 sees it, one at 5203 does not. Pre-fix the constant took
    // effect at 5000 and the ZC/TO landed on 5128.
    {
        auto status_seen = [](uint64_t sn) -> uint8_t {
            Emulator emu;
            if (!build(emu, MachineType::ZX48K)) return 0xEE;
            emu.port().out(0x183B, 0x85);            // int on, timer, TC follows
            write_code(emu, 0x8000, {0xED, 0x79});    // OUT (C),A
            auto r = emu.cpu().get_registers();
            r.PC = 0x8000; r.BC = 0x183B;
            r.AF = static_cast<uint16_t>(0x0800 | (r.AF & 0x00FF));
            emu.cpu().set_registers(r);
            emu.ctc().tick(5000);
            emu.clock().tick(5000 - emu.clock().get());
            emu.execute_single_instruction();          // OUT: 5000..5096
            // Bring the CTC to the IN's start, then read NR 0xC9.
            const uint64_t s = sn - 83;
            emu.ctc().set_time(emu.clock().get());
            emu.ctc().tick(static_cast<uint32_t>(s - emu.clock().get()));
            return in_nr_direct(emu, 0xC9, s, nullptr);
        };
        const uint8_t seen   = status_seen(5204);
        const uint8_t unseen = status_seen(5203);
        check("CTC-WR-GH265-01",
              "a CTC constant written by OUT is taken on its commit edge: "
              "its first count 17 edges on, its ZC/TO 16 per count after "
              "(ctc_chan.vhd:214-226,246-254; t80na.vhd:148-150; "
              "im2_peripheral.vhd:154-162)",
              (seen & 0x01) != 0 && (unseen & 0x01) == 0,
              "Sn=5204: " + hex2(seen) + " Sn=5203: " + hex2(unseen));
    }
}

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

    test_single_step_int_delivery(emu);
    std::printf("  Group: SingleStep — done\n");

    test_cim2_quiescence();
    std::printf("  Group: CIM2-Quiescence — done\n");

    test_c1_ctc_accumulator();
    std::printf("  Group: CTC-C1-ACC — done\n");

    test_pulse_width_speed_invariance();
    std::printf("  Group: Pulse-Width-60d — done\n");

    test_ctc_control_word_int_en(emu);
    std::printf("  Group: CTC-CW-INTEN — done\n");

    test_gh201_plan_rows(emu);
    std::printf("  Group: GH201 — done\n");

    test_gh265_int_timing();
    std::printf("  Group: GH265-INT — done\n");

    test_gh265_status_reads();
    std::printf("  Group: GH265-ISC — done\n");

    test_gh265_ctc_ports();
    std::printf("  Group: GH265-CTC — done\n");

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
