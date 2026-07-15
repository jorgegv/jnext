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
    // Timer mode, prescaler 16, TC=3: prescaler_clk every 16 ticks
    // (ctc_chan.vhd:143-146), ZC/TO on every 3rd count step (:162-170)
    // → events at ticks 48, 96, 144; the 4th lands at 192, exactly 42
    // ticks after the 150-span (phase 150 mod 16 = 6 must survive).
    {
        Ctc ctc;
        std::vector<int> seq;
        ctc.on_zc_to = [&](int ch) { seq.push_back(ch); };
        prog(ctc, 0, 0x07, 3);   // int off, timer, /16, auto-start, TC=3

        ctc.tick(150);                            // one span, 3 events
        const size_t after_span     = seq.size();
        const uint8_t counter_after = ctc.read(0);  // reloaded to 3 at 144
        ctc.tick(41);                             // ticks 151..191: none
        const size_t before_edge    = seq.size();
        ctc.tick(1);                              // tick 192: 4th ZC/TO
        const size_t at_edge        = seq.size();

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "after_span=%zu counter=%u before_edge=%zu at_edge=%zu",
                      after_span, counter_after, before_edge, at_edge);
        check("CTC-C1-ACC-01",
              "timer /16 TC=3: single tick(150) span fires exactly the 3 "
              "ZC/TO at 48/96/144 [ctc_chan.vhd:143-146,:162-170]; "
              "prescaler phase survives the closed-form jump (4th ZC/TO "
              "exactly at 192)",
              after_span == 3 && counter_after == 3
                  && before_edge == 3 && at_edge == 4,
              detail);
    }

    // CTC-C1-ACC-02 — daisy-chain inside one span + span-splitting
    // invariance. ch0 timer /16 TC=3 (ZC/TO at 48/96/144/192); ch1
    // counter mode TC=2 fed by the ch0→ch1 daisy-chain (zxnext.vhd:4084)
    // → ch1 fires on every 2nd ch0 pulse (96, 192). One tick(200) call
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
                              && ctc_span.read(0) == 3    // reloaded at 192
                              && ctc_span.read(1) == 2;   // reloaded at 192

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

    // CTC-C1-ACC-03 — mid-span TRIGGER→RUN activation ordering. ch0
    // timer /16 TC=1 auto-start (ZC/TO at 16/32/...); ch1 timer /16
    // TC=1 with D3=1 (waits for CLK/TRG — ctc_chan.vhd S_TRIGGER). The
    // ch0 pulse at tick 16 starts ch1 via the daisy-chain with its
    // prescaler cleared, and ch1 (index > firing channel) still counts
    // that same cycle — established per-cycle model semantics — so
    // ch1's first ZC/TO lands at tick 31, one BEFORE ch0's at 32. The
    // event-horizon loop must reproduce this exactly, and the stepped
    // twin must agree.
    {
        const auto run = [&](Ctc& ctc, std::vector<int>& seq, bool stepped) {
            ctc.on_zc_to = [&seq](int ch) { seq.push_back(ch); };
            prog(ctc, 0, 0x07, 1);   // timer, /16, auto-start, TC=1
            prog(ctc, 1, 0x0F, 1);   // timer, /16, CLK/TRG-start, TC=1
            if (stepped) {
                for (int i = 0; i < 31; ++i) ctc.tick(1);
            } else {
                ctc.tick(31);
            }
        };

        Ctc ctc_span, ctc_step;
        std::vector<int> seq_span, seq_step;
        run(ctc_span, seq_span, false);
        run(ctc_step, seq_step, true);

        const std::vector<int> expected = {0, 1};  // ch0@16, ch1@31
        const bool span_ok = (seq_span == expected);
        const bool step_ok = (seq_step == expected);

        // One more tick: ch0's second ZC/TO at 32 (ch1's next is at 47).
        seq_span.clear();
        ctc_span.tick(1);
        const bool edge_ok = (seq_span == std::vector<int>{0});

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "span=[%s] step=[%s] edge_ok=%d",
                      span_ok ? "0,1" : "?", step_ok ? "0,1" : "?", edge_ok);
        check("CTC-C1-ACC-03",
              "timer ch1 armed by D3=1 starts mid-span from ch0's ZC/TO "
              "at 16 [ctc_chan.vhd S_TRIGGER; zxnext.vhd:4084] and fires "
              "at 31: activation cycle still ticks the newly-RUN channel; "
              "tick(31) == 31x tick(1)",
              span_ok && step_ok && edge_ok,
              detail);
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

    test_im2_decoder_gaps(emu);
    std::printf("  Group: IM2-Decoder-Gaps — done\n");

    test_single_step_int_delivery(emu);
    std::printf("  Group: SingleStep — done\n");

    test_cim2_quiescence();
    std::printf("  Group: CIM2-Quiescence — done\n");

    test_c1_ctc_accumulator();
    std::printf("  Group: CTC-C1-ACC — done\n");

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
