// CTC + Interrupt Controller Compliance Test Runner
//
// Full rewrite (Task 1 Wave 3, 2026-04-15) against the VHDL-derived
// doc/testing/CTC-INTERRUPTS-TEST-PLAN-DESIGN.md. Every assertion cites a
// specific VHDL file and line from the authoritative FPGA source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo — cited for provenance, not edited).
//
// Ground rules (per process manual, doc/testing/UNIT-TEST-PLAN-EXECUTION.md):
//   * No C++-as-oracle. Expected values come from VHDL.
//   * Pass/fail/skip are the only outcomes. Skips are announced at the end.
//   * One section per plan row or tight group, named with the plan ID.
//   * Every check() cites VHDL file:line in the description.
//
// Surface: Sections 1-6 drive the jnext CTC C++ class
// (src/peripheral/ctc.{h,cpp}) exposing the 4-channel CTC as `Ctc` with
// per-channel write/read/tick/trigger and a single `on_interrupt`
// callback plus `set_int_enable(mask)` + `get_int_enable()` accessor.
// Sections 7-17 drive the `Im2Controller` (src/cpu/im2.{h,cpp}) directly
// after the Phase 2 wave landed the full IM2 fabric (decoder + device
// state machines + daisy chain + pulse fabric + wrapper + NR-C*
// composers). A handful of rows stay as skip() or re-home comments for
// genuine boundary reasons: NMI-blocked (NR-C0-02, DMA-04), Ula-coupled
// (ULA-INT-01..06 all re-homed to ctc_interrupts_test.cpp), joystick-io
// (JOY-01/02 at Emulator level), and CTC-NR-04 (D-pattern, structurally
// unreachable — see inline comment at end of section 6).
//
// Run: ./build/test/ctc_test
//
// BUILD NOTE (Phase 3b, 2026-04-21): sections 7-11, 13-16 now drive the
// bare `Im2Controller` directly. That pulls in `src/cpu/im2.cpp`, so
// `test/CMakeLists.txt` must add `jnext_cpu` to the ctc_test target's
// link line (alongside the existing `jnext_peripheral jnext_port`). The
// main session owns that CMakeLists.txt edit; Phase 3b only changes the
// test source.

#include "peripheral/ctc.h"
#include "peripheral/nmi_source.h"
#include "cpu/im2.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
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

void check(const char* id, bool cond, const char* desc, const std::string& detail = {}) {
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

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

// ── CTC control-word encoding (ctc_chan.vhd:265-276) ──────────────────
// Control word (D0=1):
//   D7 int_en  D6 counter  D5 prescale256  D4 rising_edge
//   D3 trig_wait  D2 tc_follows  D1 soft_reset  D0=1
// time constant write follows when channel is in S_RESET_TC or S_RUN_TC
// (ctc_chan.vhd:257). Interrupt vector is any byte with D0=0 written
// outside the tc-follows states (ctc_chan.vhd:259).
uint8_t cw(bool int_en, bool counter, bool prescale256,
           bool rising_edge, bool trig_wait, bool tc_follows, bool soft_reset) {
    uint8_t v = 0x01;
    if (int_en)      v |= 0x80;
    if (counter)     v |= 0x40;
    if (prescale256) v |= 0x20;
    if (rising_edge) v |= 0x10;
    if (trig_wait)   v |= 0x08;
    if (tc_follows)  v |= 0x04;
    if (soft_reset)  v |= 0x02;
    return v;
}

void fresh(Ctc& ctc) { ctc.reset(); }
void fresh(Im2Controller& im2) { im2.reset(); }

// Shortcut alias to avoid repeating the enum tag everywhere in IM2-related
// sections. Kept inside the anonymous namespace so it doesn't leak into
// other TUs.
using Dev = Im2Controller::DevIdx;
using DevState = Im2Controller::DevState;

// ══════════════════════════════════════════════════════════════════════
// Section 1: CTC Channel State Machine (ctc_chan.vhd:98-242)
// ══════════════════════════════════════════════════════════════════════

void section1_state_machine() {
    set_group("1. State Machine");
    Ctc ctc;

    // CTC-SM-01 — ctc_chan.vhd:189-192 state<=S_RESET on reset; t_count
    // loads from time_constant_reg which is 0 after reset (ctc_chan.vhd:279-287).
    {
        fresh(ctc);
        uint8_t v = ctc.read(0);
        check("CTC-SM-01", v == 0x00,
              "ctc_chan.vhd:189 S_RESET entry; time_constant_reg=0 after hard reset",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-02 — ctc_chan.vhd:208-213: in S_RESET, CW with D2=0 keeps
    // state_next=S_RESET (line 212); no counting because reset_soft=1
    // holds p_count=0 (ctc_chan.vhd:117,134-139).
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, false, false));
        ctc.tick(256);
        uint8_t v = ctc.read(0);
        check("CTC-SM-02", v == 0x00,
              "ctc_chan.vhd:212 S_RESET stays on CW with D2=0; no count",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-03 — ctc_chan.vhd:208-211: S_RESET + CW with D2=1 → S_RESET_TC.
    // While in S_RESET_TC the counter is not running, t_count mirrors
    // time_constant_reg (still 0).
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.tick(256);
        uint8_t v = ctc.read(0);
        check("CTC-SM-03", v == 0x00,
              "ctc_chan.vhd:210 S_RESET_TC awaits TC; no count",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-04 — ctc_chan.vhd:214-227: TC write in S_RESET_TC loads
    // time_constant_reg; timer/D3=0 goes straight to S_RUN; t_count loads
    // the new TC (ctc_chan.vhd:158-163) and then decrements on prescaler.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        uint8_t before = ctc.read(0);
        ctc.tick(16);  // one prescaler-16 period
        uint8_t after = ctc.read(0);
        check("CTC-SM-04", before == 0x10 && after == 0x0F,
              "ctc_chan.vhd:216,223-226 S_RESET_TC→S_TRIGGER→S_RUN and t_count reload+decrement",
              fmt("before=0x%02x after=0x%02x", before, after));
    }

    // CTC-SM-05 — ctc_chan.vhd:214-225: timer mode (D6=0), D3=1, and
    // clk_trg_edge=0 routes to S_TRIGGER (line 224). In S_TRIGGER the
    // channel waits: t_count stays at TC.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, true, true, false));
        ctc.write(0, 0x10);
        ctc.tick(1024);
        uint8_t v = ctc.read(0);
        check("CTC-SM-05", v == 0x10,
              "ctc_chan.vhd:216,224 timer D3=1 waits in S_TRIGGER",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-06 — ctc_chan.vhd:222-226: timer, D3=0 → straight to S_RUN
    // after TC load.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.tick(16);
        uint8_t v = ctc.read(0);
        check("CTC-SM-06", v == 0x0F,
              "ctc_chan.vhd:216,223-226 timer D3=0 → immediate S_RUN",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-07 — ctc_chan.vhd:220-226: counter mode (D6=1) skips the
    // S_TRIGGER wait and goes straight to S_RUN. First external trigger
    // then decrements t_count (ctc_chan.vhd:150,162).
    {
        fresh(ctc);
        ctc.write(0, cw(false, true, false, false, false, true, false));
        ctc.write(0, 0x05);
        ctc.trigger(0);
        uint8_t v = ctc.read(0);
        check("CTC-SM-07", v == 0x04,
              "ctc_chan.vhd:226 counter mode → S_RUN; trigger decrements",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-08 — ctc_chan.vhd:228-233: CW with D2=1 while in S_RUN
    // transitions to S_RUN_TC. Next write is consumed as TC regardless
    // of D0 (ctc_chan.vhd:257).
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);  // S_RUN, t_count=0x10
        ctc.write(0, cw(false, false, false, false, false, true, false));  // → S_RUN_TC
        ctc.write(0, 0x20);  // TC=0x20 → S_RUN
        uint8_t v = ctc.read(0);
        check("CTC-SM-08", v == 0x20,
              "ctc_chan.vhd:230 S_RUN→S_RUN_TC then TC reload",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-09 — ctc_chan.vhd:234-238: S_RUN_TC + TC write → S_RUN,
    // counter resumes counting from new TC.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x30);
        ctc.tick(16);
        uint8_t v = ctc.read(0);
        check("CTC-SM-09", v == 0x2F,
              "ctc_chan.vhd:236 S_RUN_TC→S_RUN with reloaded TC, continues counting",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-10 — ctc_chan.vhd:201-206: any state + CW with D1=1,D2=0
    // → S_RESET. p_count reset via reset_soft=1 (line 117); t_count no
    // longer advances.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.tick(32);
        ctc.write(0, cw(false, false, false, false, false, false, true));
        uint8_t v1 = ctc.read(0);
        ctc.tick(256);
        uint8_t v2 = ctc.read(0);
        check("CTC-SM-10", v1 == v2,
              "ctc_chan.vhd:202 soft reset D1=1,D2=0 → S_RESET, counter stops",
              fmt("v1=0x%02x v2=0x%02x", v1, v2));
    }

    // CTC-SM-11 — ctc_chan.vhd:201-205: D1=1,D2=1 from any state → S_RESET_TC.
    // Next write is the new TC and the channel restarts.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.write(0, cw(false, false, false, false, false, true, true));
        ctc.write(0, 0x20);
        uint8_t v = ctc.read(0);
        check("CTC-SM-11", v == 0x20,
              "ctc_chan.vhd:204 soft reset D1=1,D2=1 → S_RESET_TC",
              fmt("got 0x%02x", v));
    }

    // CTC-SM-12 — ctc_chan.vhd:214-217,257: while in S_RESET_TC any byte
    // (even one that looks like a CW with D1=1) is consumed as the time
    // constant. The raw byte value becomes t_count.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        uint8_t reset_cw = cw(false, false, false, false, false, false, true);
        ctc.write(0, reset_cw);  // consumed as TC, not as new CW
        uint8_t v = ctc.read(0);
        check("CTC-SM-12", v == reset_cw,
              "ctc_chan.vhd:257 S_RESET_TC: any write is TC, raw byte latched",
              fmt("got 0x%02x expected 0x%02x", v, reset_cw));
    }

    // CTC-SM-13 — ctc_chan.vhd:228-232: CW with D1=0,D2=0 while in S_RUN
    // stays in S_RUN (line 232) and updates control_reg (line 269). The
    // channel keeps counting after the reconfigure.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.tick(16);
        uint8_t before = ctc.read(0);
        // reconfigure: prescale=256, no reset, no tc_follows → stays in RUN
        ctc.write(0, cw(false, false, true, false, false, false, false));
        ctc.tick(256);
        uint8_t after = ctc.read(0);
        check("CTC-SM-13", after == static_cast<uint8_t>(before - 1),
              "ctc_chan.vhd:232 CW update in S_RUN keeps counting with new prescaler",
              fmt("before=0x%02x after=0x%02x", before, after));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 2: CTC Timer Mode / Prescaler (ctc_chan.vhd:134-165)
// ══════════════════════════════════════════════════════════════════════

void section2_timer_mode() {
    set_group("2. Timer Mode");
    Ctc ctc;

    // CTC-TM-01 — ctc_chan.vhd:143,146: prescale=16 fires when
    // p_count(3:0)="1111". 16 i_CLK ticks decrement t_count by 1.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.tick(16);
        uint8_t v = ctc.read(0);
        check("CTC-TM-01", v == 0x0F,
              "ctc_chan.vhd:146 prescale=16 decrements every 16 clocks",
              fmt("got 0x%02x", v));
    }

    // CTC-TM-02 — ctc_chan.vhd:144,146: prescale=256 fires only when
    // p_count(7:4)="1111" AND p_count(3:0)="1111".
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, true, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.tick(256);
        uint8_t v = ctc.read(0);
        check("CTC-TM-02", v == 0x0F,
              "ctc_chan.vhd:146 prescale=256 decrements every 256 clocks",
              fmt("got 0x%02x", v));
    }

    // CTC-TM-03 — ctc_chan.vhd:152-170: TC=1 means one prescaler cycle
    // to zero (t_count_next_zero when t_count=1).
    {
        fresh(ctc);
        int zc = 0;
        ctc.on_interrupt = [&](int) { ++zc; };
        ctc.write(0, cw(true, false, false, false, false, true, false));
        ctc.write(0, 0x01);
        ctc.tick(16);
        check("CTC-TM-03", zc == 1,
              "ctc_chan.vhd:170 TC=1 → ZC/TO after one prescaler cycle",
              fmt("zc=%d", zc));
    }

    // CTC-TM-04 — ctc_chan.vhd:158-163: t_count <= time_constant_reg on
    // load. TC=0 loads 0x00; decrement wraps through 0xFF → full 256 ticks
    // before the next zero. ctc_chan.vhd:170 uses t_count_next_zero so the
    // ZC fires when t_count reaches 1 and decrements to 0.
    {
        fresh(ctc);
        int zc = 0;
        ctc.on_interrupt = [&](int) { ++zc; };
        ctc.write(0, cw(true, false, false, false, false, true, false));
        ctc.write(0, 0x00);
        ctc.tick(16 * 255);
        int before = zc;
        ctc.tick(16);
        check("CTC-TM-04", before == 0 && zc == 1,
              "ctc_chan.vhd:158 TC=0 ≡ 256 decrements before first ZC/TO",
              fmt("before=%d after=%d", before, zc));
    }

    // CTC-TM-05 — ctc_chan.vhd:117,134-139: reset_soft=1 whenever state
    // is not S_RUN/S_RUN_TC, which forces p_count<=0. After a soft reset
    // a fresh start counts a full prescaler period.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.tick(10);  // partial prescaler
        ctc.write(0, cw(false, false, false, false, false, false, true));  // soft reset
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        ctc.tick(16);
        uint8_t v = ctc.read(0);
        check("CTC-TM-05", v == 0x0F,
              "ctc_chan.vhd:136 prescaler cleared on soft reset (reset_soft)",
              fmt("got 0x%02x", v));
    }

    // CTC-TM-06 — ctc_chan.vhd:158-163: on ZC/TO the next clock reloads
    // t_count<=time_constant_reg (line 162 decrements; line 158 covers
    // the iowr_tc reload). The channel keeps firing every TC*prescaler.
    {
        fresh(ctc);
        int zc = 0;
        ctc.on_interrupt = [&](int) { ++zc; };
        ctc.write(0, cw(true, false, false, false, false, true, false));
        ctc.write(0, 0x02);
        ctc.tick(16 * 2);
        int first = zc;
        ctc.tick(16 * 2);
        check("CTC-TM-06", first == 1 && zc == 2,
              "ctc_chan.vhd:170 ZC/TO auto-reloads TC and continues",
              fmt("first=%d second=%d", first, zc));
    }

    // CTC-TM-07 — ctc_chan.vhd:170,183: zc_to is asserted "for one i_CLK
    // cycle" (comment) and output via zc_to_d, so each underflow produces
    // exactly one pulse observable as exactly one callback invocation.
    {
        fresh(ctc);
        int zc = 0;
        ctc.on_interrupt = [&](int) { ++zc; };
        ctc.write(0, cw(true, false, false, false, false, true, false));
        ctc.write(0, 0x01);
        ctc.tick(16);
        check("CTC-TM-07", zc == 1,
              "ctc_chan.vhd:170 zc_to is a single-cycle pulse per underflow",
              fmt("zc=%d", zc));
    }

    // CTC-TM-08 — ctc_chan.vhd:168: o_cpu_d <= t_count. Reading the port
    // returns the current down-counter value.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x10);
        uint8_t v0 = ctc.read(0);
        ctc.tick(16 * 3);
        uint8_t v1 = ctc.read(0);
        check("CTC-TM-08", v0 == 0x10 && v1 == 0x0D,
              "ctc_chan.vhd:168 port read returns t_count",
              fmt("v0=0x%02x v1=0x%02x", v0, v1));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 3: CTC Counter Mode (ctc_chan.vhd:128,150)
// ══════════════════════════════════════════════════════════════════════

void section3_counter_mode() {
    set_group("3. Counter Mode");
    Ctc ctc;

    // CTC-CM-01 — ctc_chan.vhd:128: clk_trg_edge uses
    // (clk_trg_d AND NOT i_clk_trg) when control_reg(4-3)=0 (falling edge).
    // ctc_chan.vhd:150 gates t_count_en=clk_trg_edge in counter mode.
    // The jnext trigger() API injects one counted edge regardless of
    // polarity (no clk_trg signal is exposed), so we verify that a single
    // falling-edge-configured channel decrements on trigger().
    {
        fresh(ctc);
        ctc.write(0, cw(false, true, false, false, false, true, false));
        ctc.write(0, 0x05);
        ctc.trigger(0);
        uint8_t v = ctc.read(0);
        check("CTC-CM-01", v == 0x04,
              "ctc_chan.vhd:128 falling-edge counter mode: trigger decrements",
              fmt("got 0x%02x", v));
    }

    // CTC-CM-02 — ctc_chan.vhd:128: rising-edge variant uses
    // (i_clk_trg AND NOT clk_trg_d). jnext trigger() injects one logical
    // edge so rising-edge channel also decrements by one.
    {
        fresh(ctc);
        ctc.write(0, cw(false, true, false, true, false, true, false));
        ctc.write(0, 0x05);
        ctc.trigger(0);
        uint8_t v = ctc.read(0);
        check("CTC-CM-02", v == 0x04,
              "ctc_chan.vhd:128 rising-edge counter mode: trigger decrements",
              fmt("got 0x%02x", v));
    }

    // CTC-CM-03 — ctc_chan.vhd:170: zc_to when t_count_next_zero AND
    // t_count_en, i.e. after TC external edges.
    {
        fresh(ctc);
        int zc = 0;
        ctc.on_interrupt = [&](int) { ++zc; };
        ctc.write(0, cw(true, true, false, false, false, true, false));
        ctc.write(0, 0x03);
        ctc.trigger(0);
        ctc.trigger(0);
        ctc.trigger(0);
        check("CTC-CM-03", zc == 1,
              "ctc_chan.vhd:170 counter mode ZC/TO after TC edges",
              fmt("zc=%d", zc));
    }

    // CTC-CM-04 — ctc_chan.vhd:158-163: auto-reload after ZC/TO in
    // counter mode, same as timer mode.
    {
        fresh(ctc);
        int zc = 0;
        ctc.on_interrupt = [&](int) { ++zc; };
        ctc.write(0, cw(true, true, false, false, false, true, false));
        ctc.write(0, 0x02);
        ctc.trigger(0);
        ctc.trigger(0);  // first ZC/TO, reload to 2
        uint8_t reload = ctc.read(0);
        ctc.trigger(0);
        ctc.trigger(0);  // second ZC/TO
        check("CTC-CM-04", zc == 2 && reload == 0x02,
              "ctc_chan.vhd:158 counter-mode auto-reload after ZC/TO",
              fmt("zc=%d reload=0x%02x", zc, reload));
    }

    // CTC-CM-05 — ctc_chan.vhd:289,128: clk_edge_change is raised on
    // iowr_cw when D4 flips vs control_reg(4-3); clk_trg_edge ORs this
    // in, so writing a CW that toggles D4 while in S_RUN counter mode
    // produces one counted edge.
    {
        fresh(ctc);
        ctc.write(0, cw(false, true, false, false, false, true, false));
        ctc.write(0, 0x05);
        // Reconfigure D4 from falling to rising; D1=0,D2=0 so stays in RUN.
        ctc.write(0, cw(false, true, false, true, false, false, false));
        uint8_t v = ctc.read(0);
        check("CTC-CM-05", v == 0x04,
              "ctc_chan.vhd:289 edge-select change is a counted clk_trg_edge",
              fmt("got 0x%02x", v));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 4: CTC Chaining (zxnext.vhd:4084)
// ══════════════════════════════════════════════════════════════════════

void section4_chaining() {
    set_group("4. Chaining");
    Ctc ctc;

    // VHDL (zxnext.vhd:4084):
    //   i_clk_trg(3 downto 0) <= ctc_zc_to(2 downto 0) & ctc_zc_to(3)
    // So: ch0.clk_trg = ch3.zc_to,  ch1.clk_trg = ch0.zc_to,
    //     ch2.clk_trg = ch1.zc_to,  ch3.clk_trg = ch2.zc_to.
    // This is a ring, not a line.

    // CTC-CH-01 — ch0.clk_trg = ch3.zc_to (ring wrap-around).
    // VHDL zxnext.vhd:4084: i_clk_trg <= ctc_zc_to(2 downto 0) & ctc_zc_to(3).
    {
        fresh(ctc);
        // ch3 timer: fastest ZC/TO
        ctc.write(3, cw(false, false, false, false, false, true, false));
        ctc.write(3, 0x01);
        // ch0 counter, TC=3
        ctc.write(0, cw(false, true, false, false, false, true, false));
        ctc.write(0, 0x03);
        uint8_t before = ctc.read(0);
        ctc.tick(16);  // ch3 fires; if wired correctly, ch0 counts one edge
        uint8_t after = ctc.read(0);
        check("CTC-CH-01", after == static_cast<uint8_t>(before - 1),
              "zxnext.vhd:4084 ch0.clk_trg = ch3.zc_to (ring wrap)",
              fmt("before=0x%02x after=0x%02x", before, after));
    }

    // CTC-CH-02 — ch1.clk_trg = ch0.zc_to.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x01);
        ctc.write(1, cw(false, true, false, false, false, true, false));
        ctc.write(1, 0x03);
        ctc.tick(16);
        ctc.tick(16);
        uint8_t v = ctc.read(1);
        check("CTC-CH-02", v == 0x01,
              "zxnext.vhd:4084 ch1.clk_trg = ch0.zc_to",
              fmt("ch1=0x%02x", v));
    }

    // CTC-CH-03 — ch2.clk_trg = ch1.zc_to.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x01);
        ctc.write(1, cw(false, true, false, false, false, true, false));
        ctc.write(1, 0x01);  // TC=1 → fires each ch0 pulse
        ctc.write(2, cw(false, true, false, false, false, true, false));
        ctc.write(2, 0x03);
        ctc.tick(16);
        ctc.tick(16);
        uint8_t v = ctc.read(2);
        check("CTC-CH-03", v == 0x01,
              "zxnext.vhd:4084 ch2.clk_trg = ch1.zc_to",
              fmt("ch2=0x%02x", v));
    }

    // CTC-CH-04 — ch3.clk_trg = ch2.zc_to.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x01);
        ctc.write(1, cw(false, true, false, false, false, true, false));
        ctc.write(1, 0x01);
        ctc.write(2, cw(false, true, false, false, false, true, false));
        ctc.write(2, 0x01);
        ctc.write(3, cw(false, true, false, false, false, true, false));
        ctc.write(3, 0x03);
        ctc.tick(16);
        ctc.tick(16);
        uint8_t v = ctc.read(3);
        check("CTC-CH-04", v == 0x01,
              "zxnext.vhd:4084 ch3.clk_trg = ch2.zc_to",
              fmt("ch3=0x%02x", v));
    }

    // CTC-CH-05 — 3-stage cascade ch0 timer (TC=1) → ch1 counter (TC=2) →
    // ch2 counter (TC=2). After 4 ch0 pulses: ch1 fires 2x, ch2 fires 1x.
    {
        fresh(ctc);
        int zc2 = 0;
        ctc.on_interrupt = [&](int ch) { if (ch == 2) ++zc2; };
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x01);
        ctc.write(1, cw(false, true, false, false, false, true, false));
        ctc.write(1, 0x02);
        ctc.write(2, cw(true, true, false, false, false, true, false));
        ctc.write(2, 0x02);
        ctc.tick(16 * 4);
        check("CTC-CH-05", zc2 == 1,
              "zxnext.vhd:4084 3-stage cascade yields TC-product ZC/TO rate",
              fmt("zc2=%d", zc2));
    }

    // CTC-CH-06 — four counter-mode channels form a ring with no timer
    // driver; no channel ever produces a ZC/TO. ctc_chan.vhd:150 gates
    // t_count_en on clk_trg_edge which never rises.
    {
        fresh(ctc);
        for (int ch = 0; ch < 4; ++ch) {
            ctc.write(ch, cw(false, true, false, false, false, true, false));
            ctc.write(ch, 0x03);
        }
        ctc.tick(4096);
        bool all_stuck =
            ctc.read(0) == 0x03 && ctc.read(1) == 0x03 &&
            ctc.read(2) == 0x03 && ctc.read(3) == 0x03;
        check("CTC-CH-06", all_stuck,
              "ctc_chan.vhd:150 all-counter ring is dead (no source edges)",
              fmt("ch0=0x%02x ch1=0x%02x ch2=0x%02x ch3=0x%02x",
                  ctc.read(0), ctc.read(1), ctc.read(2), ctc.read(3)));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 5: CTC Control Word / Vector Protocol (ctc_chan.vhd:257-289, ctc.vhd:150)
// ══════════════════════════════════════════════════════════════════════

void section5_control_vector() {
    set_group("5. Control/Vector");
    Ctc ctc;

    // CTC-CW-01 — ctc_chan.vhd:265-271: iowr_cw latches i_cpu_d(7:3) into
    // control_reg. D7 = int_en is observable via channel().int_enabled().
    // Other bits are observable via resulting behaviour; here we verify D7.
    {
        fresh(ctc);
        ctc.write(0, cw(true, true, true, true, true, false, false));
        check("CTC-CW-01", ctc.channel(0).int_enabled(),
              "ctc_chan.vhd:269 control_reg<=i_cpu_d(7:3); D7 → o_int_en",
              "int_en not set");
    }

    // CTC-CW-02 — ctc.vhd:150: o_vector_wr <= iowr_vc(0); only channel 0's
    // iowr_vc drives the top-level vector write strobe. jnext Ctc has no
    // o_vector_wr accessor, so we verify that writing a D0=0 byte to ch0
    // is accepted (no state change, no crash).
    {
        fresh(ctc);
        ctc.write(0, 0x00);  // vector write, D0=0
        // No TC loaded yet, counter should still read 0 and channel
        // should still accept a proper CW afterwards.
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x11);
        uint8_t v = ctc.read(0);
        check("CTC-CW-02", v == 0x11,
              "ctc.vhd:150 o_vector_wr = iowr_vc(0); ch0 accepts vector then resumes",
              fmt("got 0x%02x", v));
    }

    // CTC-CW-03 — ctc.vhd:150: iowr_vc(1..3) NOT routed to o_vector_wr.
    // Per ctc_chan.vhd:259, iowr_vc is still recognised by each channel's
    // own state machine but the top-level strobe is ch0-only. Observable:
    // channels 1-3 accept a D0=0 byte without disturbing state.
    {
        fresh(ctc);
        ctc.write(1, 0x00);
        ctc.write(2, 0x00);
        ctc.write(3, 0x00);
        ctc.write(1, cw(false, false, false, false, false, true, false));
        ctc.write(1, 0x22);
        uint8_t v = ctc.read(1);
        check("CTC-CW-03", v == 0x22,
              "ctc.vhd:150 ch1-3 consume vector writes without o_vector_wr",
              fmt("ch1=0x%02x", v));
    }

    // CTC-CW-04 — ctc_chan.vhd:257: iowr_tc fires in S_RESET_TC/S_RUN_TC.
    // After a CW with D2=1 the next write is TC regardless of D0.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x42);
        uint8_t v = ctc.read(0);
        check("CTC-CW-04", v == 0x42,
              "ctc_chan.vhd:257 TC-follows: next write is time constant",
              fmt("got 0x%02x", v));
    }

    // CTC-CW-05 — ctc_chan.vhd:257-258: iowr_tc takes priority over
    // iowr_cw/iowr_vc in S_RESET_TC, so a CW-shaped byte (D0=1) is still
    // a TC.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x85);  // D0=1, D7=1 — looks like a CW
        uint8_t v = ctc.read(0);
        check("CTC-CW-05", v == 0x85,
              "ctc_chan.vhd:257 S_RESET_TC: CW-shaped byte still latched as TC",
              fmt("got 0x%02x", v));
    }

    // CTC-CW-06 — ctc_chan.vhd:269,276: D7=1 sets control_reg(7-3);
    // o_int_en = control_reg(7-3).
    {
        fresh(ctc);
        ctc.write(0, cw(true, false, false, false, false, false, false));
        check("CTC-CW-06", ctc.channel(0).int_enabled(),
              "ctc_chan.vhd:276 o_int_en=control_reg(7-3); D7=1 enables",
              "");
    }

    // CTC-CW-07 — ctc_chan.vhd:269: a subsequent CW with D7=0 overwrites
    // bit (7-3) of control_reg, disabling the interrupt.
    {
        fresh(ctc);
        ctc.write(0, cw(true, false, false, false, false, false, false));
        ctc.write(0, cw(false, false, false, false, false, false, false));
        check("CTC-CW-07", !ctc.channel(0).int_enabled(),
              "ctc_chan.vhd:269 D7=0 clears control_reg(7-3)",
              "");
    }

    // CTC-CW-08 — ctc_chan.vhd:271: when i_int_en_wr is asserted
    // control_reg(7-3) is overwritten by i_int_en, overriding the D7 bit
    // written via a CW. jnext exposes this path as Ctc::set_int_enable.
    {
        fresh(ctc);
        ctc.write(0, cw(true, false, false, false, false, false, false));
        ctc.set_int_enable(0x00);
        check("CTC-CW-08", !ctc.channel(0).int_enabled(),
              "ctc_chan.vhd:271 i_int_en_wr overrides D7 of control_reg",
              "");
    }

    // CTC-CW-09 — ctc_chan.vhd:267: control_reg<=(others=>'0') on
    // i_reset_hard.
    {
        fresh(ctc);
        ctc.write(0, cw(true, true, true, true, true, false, false));
        ctc.reset();
        check("CTC-CW-09", !ctc.channel(0).int_enabled(),
              "ctc_chan.vhd:267 hard reset clears control_reg",
              "");
    }

    // CTC-CW-10 — ctc_chan.vhd:279-287: time_constant_reg cleared on
    // hard reset; t_count follows at line 159.
    {
        fresh(ctc);
        ctc.write(0, cw(false, false, false, false, false, true, false));
        ctc.write(0, 0x42);
        ctc.reset();
        uint8_t v = ctc.read(0);
        check("CTC-CW-10", v == 0x00,
              "ctc_chan.vhd:281 hard reset clears time_constant_reg",
              fmt("got 0x%02x", v));
    }

    // CTC-CW-11 — (D) structurally unreachable stimulus.
    // ctc_chan.vhd:250-256 `iowr = i_iowr AND NOT iowr_d` is the VHDL
    // rising-edge detect that prevents a held i_iowr signal from issuing
    // multiple writes. jnext's write() is a discrete API call; there is
    // no held-signal scenario to construct at this abstraction layer, so
    // the invariant has no outcome-observable surface. Outcome-equivalent
    // behaviour (exactly-one write per call, no double-writes) is covered
    // end-to-end by every CTC-SM-* / CTC-CW-* check above, which would
    // all fail if any single write() produced two register updates.
}

// ══════════════════════════════════════════════════════════════════════
// Section 6: CTC Interrupt Enable via NextREG (partial coverage)
// ══════════════════════════════════════════════════════════════════════

void section6_nextreg_int_enable() {
    set_group("6. NR 0xC5 int_en");
    Ctc ctc;

    // CTC-NR-01 — zxnext.vhd wires nr_c5_we → i_int_en_wr for the CTC.
    // ctc_chan.vhd:271 shows how i_int_en writes control_reg(7-3).
    // jnext exposes this via Ctc::set_int_enable(mask).
    {
        fresh(ctc);
        ctc.set_int_enable(0x05);  // ch0 and ch2
        bool ok = ctc.channel(0).int_enabled() &&
                  !ctc.channel(1).int_enabled() &&
                  ctc.channel(2).int_enabled() &&
                  !ctc.channel(3).int_enabled();
        check("CTC-NR-01", ok,
              "ctc_chan.vhd:271 NR 0xC5 write routes bits to per-channel i_int_en",
              fmt("ch0=%d ch1=%d ch2=%d ch3=%d",
                  ctc.channel(0).int_enabled(), ctc.channel(1).int_enabled(),
                  ctc.channel(2).int_enabled(), ctc.channel(3).int_enabled()));
    }

    // CTC-NR-02 — NR 0xC5 read returns ctc_int_en[7:0]. Phase 2 added
    // Ctc::get_int_enable() which packs bits 0..3 = channel 0..3 int_enabled;
    // bits 4..7 always 0 (CTC4..7 hardwired to '0' in the Next VHDL,
    // zxnext.vhd:4092).
    {
        fresh(ctc);
        ctc.set_int_enable(0x05);  // ch0 and ch2
        uint8_t got = ctc.get_int_enable();
        check("CTC-NR-02", got == 0x05,
              "zxnext.vhd:4078 NR 0xC5 read returns ctc_int_en[7:0]",
              fmt("got 0x%02x expected 0x05", got));
    }

    // CTC-NR-03 — CW D7 and NR 0xC5 both drive control_reg(7-3). We
    // verify that programming int_en via a CW alone (ignoring NR path)
    // still takes effect.
    {
        fresh(ctc);
        ctc.set_int_enable(0x00);
        ctc.write(0, cw(true, false, false, false, false, false, false));
        check("CTC-NR-03", ctc.channel(0).int_enabled(),
              "ctc_chan.vhd:269-271 both CW D7 and i_int_en_wr reach control_reg(7-3)",
              "");
    }

    // CTC-NR-04 — (D) structurally unreachable stimulus.
    // zxnext.vhd invariant: nr_c5_we must not overlap i_iowr because
    // control_reg is a single-ported register updated by whichever strobe
    // is active. jnext's two write paths — Ctc::set_int_enable() (NR 0xC5)
    // and Ctc::write(channel, CW) (i_iowr port write) — are both discrete
    // C++ API calls; there is no cycle-level simultaneity to construct at
    // this abstraction layer. Same D-pattern as CTC-CW-11 (rising-edge
    // iowr detect). Outcome-equivalent behaviour (exactly-one write per
    // call, no lost or double writes, whichever path was called last
    // wins per the VHDL "whichever strobe is active" rule) is already
    // covered end-to-end by CTC-CW-08 (i_int_en_wr overrides CW D7) and
    // CTC-NR-01/02/03 (NR 0xC5 write + read + CW D7 equivalence).
}

// ══════════════════════════════════════════════════════════════════════
// Sections 7-17: Im2Controller / pulse fabric / NR 0xC0-0xCE / unq / DMA
// ══════════════════════════════════════════════════════════════════════
//
// Phase 2 (Waves 1-3) landed the full IM2 fabric inside src/cpu/im2.{h,cpp}:
// the `Im2Controller` class owns the RETI/RETN/IM decoder, per-device
// state machines (S_0/S_REQ/S_ACK/S_ISR), the IEI/IEO daisy chain, the
// pulse-mode INT sequencer, the im2_peripheral wrapper (int_status +
// im2_int_req latch + int_unq bypass), the NR 0xC0/C4/C5/C6 enable fan-
// out, the NR 0xC8/C9/CA int_status composers, NR 0xCC/CD/CE DMA int
// enable mask fan-out, and im2_dma_delay latch. Sections 7-16 drive the
// Im2Controller directly (no full Emulator needed). Section 17 (JOY)
// re-homes to a future Emulator/input test.

void section7_im2_control() {
    set_group("7. IM2 Control");
    Im2Controller im2;

    // IM2C-01 — im2_control.vhd:161-170: from S_0, opcode=0xED moves
    // state_next to S_ED_T4. Observable via reti_decode_active() which
    // im2_control.vhd:233 drives true iff state == S_ED_T4.
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        check("IM2C-01", im2.reti_decode_active(),
              "im2_control.vhd:163 S_0 + ED → S_ED_T4 (o_reti_decode=1)",
              "reti_decode expected true after ED");
    }

    // IM2C-02 — im2_control.vhd:234: reti_seen pulses when state_next
    // enters S_ED4D_T4, i.e. on the ED 4D sequence.
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);
        bool pulse = im2.reti_seen_this_cycle();
        // Next on_m1_cycle should clear the pulse.
        im2.on_m1_cycle(0x0002, 0x00);
        check("IM2C-02", pulse && !im2.reti_seen_this_cycle(),
              "im2_control.vhd:234 reti_seen = 1 for the one cycle state_next=S_ED4D_T4",
              fmt("pulse=%d now=%d", pulse, im2.reti_seen_this_cycle()));
    }

    // IM2C-03 — im2_control.vhd:236: retn_seen pulses when state_next
    // enters S_ED45_T4, i.e. on the ED 45 sequence.
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x45);
        bool pulse = im2.retn_seen_this_cycle();
        im2.on_m1_cycle(0x0002, 0x00);
        check("IM2C-03", pulse && !im2.retn_seen_this_cycle(),
              "im2_control.vhd:236 retn_seen = 1 for the one cycle state_next=S_ED45_T4",
              fmt("pulse=%d now=%d", pulse, im2.retn_seen_this_cycle()));
    }

    // IM2C-04 — im2_control.vhd:171-180: after ED, any byte that is not
    // 4D/45 and doesn't match the IM-mode mask (opcode(7:6)="01",
    // opcode(2:0)="110") falls through to S_0 with neither RETI nor RETN
    // pulse. We pick 0x00 which clearly doesn't match.
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x00);
        check("IM2C-04",
              !im2.reti_seen_this_cycle() && !im2.retn_seen_this_cycle() &&
              !im2.reti_decode_active(),
              "im2_control.vhd:171-180 ED + non-4D/45 non-IM → S_0, no pulse",
              fmt("reti=%d retn=%d decode=%d",
                  im2.reti_seen_this_cycle(), im2.retn_seen_this_cycle(),
                  im2.reti_decode_active()));
    }

    // IM2C-05 — im2_control.vhd:233: o_reti_decode = '1' when state = S_ED_T4.
    // Observable as reti_decode_active() after ED is fetched and BEFORE the
    // next opcode is fetched.
    {
        fresh(im2);
        bool before = im2.reti_decode_active();
        im2.on_m1_cycle(0x0000, 0xED);
        bool during = im2.reti_decode_active();
        im2.on_m1_cycle(0x0001, 0x00);
        bool after = im2.reti_decode_active();
        check("IM2C-05", !before && during && !after,
              "im2_control.vhd:233 o_reti_decode high only while state=S_ED_T4",
              fmt("before=%d during=%d after=%d", before, during, after));
    }

    // IM2C-06 — im2_control.vhd:193-198: CB prefix goes to S_CB_T4 and
    // does NOT assert reti_decode (that's S_ED_T4 only) and does NOT
    // produce any RETI/RETN pulse.
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xCB);
        check("IM2C-06",
              !im2.reti_decode_active() &&
              !im2.reti_seen_this_cycle() && !im2.retn_seen_this_cycle(),
              "im2_control.vhd:193 CB → S_CB_T4; never asserts reti_seen/retn_seen",
              fmt("decode=%d reti=%d retn=%d",
                  im2.reti_decode_active(), im2.reti_seen_this_cycle(),
                  im2.retn_seen_this_cycle()));
    }

    // IM2C-07 — im2_control.vhd:199-206: DD/FD chain stays in S_DDFD_T4.
    // From S_DDFD_T4, DD/FD keeps us there. No RETI/RETN pulses along the
    // way.
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xDD);
        im2.on_m1_cycle(0x0001, 0xFD);
        im2.on_m1_cycle(0x0002, 0xDD);
        check("IM2C-07",
              !im2.reti_decode_active() &&
              !im2.reti_seen_this_cycle() && !im2.retn_seen_this_cycle(),
              "im2_control.vhd:199-206 DD/FD chain stays in S_DDFD_T4",
              "no pulses or reti_decode expected along a DDFD chain");
    }

    // IM2C-08 — im2_control.vhd:238: o_dma_delay high across all RETI
    // decode states: S_ED_T4, S_ED4D_T4, S_ED45_T4, S_SRL_T1, S_SRL_T2.
    // Track across an ED 4D sequence.
    {
        fresh(im2);
        bool ok = true;
        ok = ok && !im2.dma_delay_control();
        im2.on_m1_cycle(0x0000, 0xED);
        ok = ok && im2.dma_delay_control();  // S_ED_T4
        im2.on_m1_cycle(0x0001, 0x4D);
        ok = ok && im2.dma_delay_control();  // S_ED4D_T4
        im2.on_m1_cycle(0x0002, 0x00);
        ok = ok && im2.dma_delay_control();  // S_SRL_T1 (next-state = SRL_T2)
        im2.on_m1_cycle(0x0003, 0x00);
        ok = ok && im2.dma_delay_control();  // S_SRL_T2
        im2.on_m1_cycle(0x0004, 0x00);
        ok = ok && !im2.dma_delay_control(); // back to S_0
        check("IM2C-08", ok,
              "im2_control.vhd:238 o_dma_delay covers S_ED_T4 / S_ED4D_T4 / S_SRL_T1/T2",
              "dma_delay_control sequence mismatch");
    }

    // IM2C-09 — im2_control.vhd:186-192: the SRL_T1/T2 pair adds 2 "ticks"
    // (two on_m1_cycle calls) of dma_delay_control after ED 4D / ED 45.
    // From S_ED45_T4 → S_SRL_T1 → S_SRL_T2 → S_0.
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x45);  // now S_ED45_T4 (dma_delay high)
        int delay_cycles = 0;
        if (im2.dma_delay_control()) ++delay_cycles;
        im2.on_m1_cycle(0x0002, 0x00);  // S_SRL_T1 (dma_delay high)
        if (im2.dma_delay_control()) ++delay_cycles;
        im2.on_m1_cycle(0x0003, 0x00);  // S_SRL_T2 (dma_delay high)
        if (im2.dma_delay_control()) ++delay_cycles;
        im2.on_m1_cycle(0x0004, 0x00);  // S_0 (dma_delay low)
        check("IM2C-09", delay_cycles == 3 && !im2.dma_delay_control(),
              "im2_control.vhd:186-192 SRL_T1/T2 extend dma_delay beyond RETN",
              fmt("extra delay cycles=%d (expected 3)", delay_cycles));
    }

    // IM2C-10 — im2_control.vhd:218-227: ED 46 decodes IM 0 (bit4=0).
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x46);
        check("IM2C-10", im2.im_mode() == 0,
              "im2_control.vhd:224 ED 46 → im_mode = 00 (IM 0)",
              fmt("im_mode=%u", im2.im_mode()));
    }

    // IM2C-11 — im2_control.vhd:218-227: ED 56 decodes IM 1
    // (bit4=1, bit3=0 → lower-bit = bit4 AND NOT bit3 = 1).
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x56);
        check("IM2C-11", im2.im_mode() == 1,
              "im2_control.vhd:224 ED 56 → im_mode = 01 (IM 1)",
              fmt("im_mode=%u", im2.im_mode()));
    }

    // IM2C-12 — im2_control.vhd:218-227: ED 5E decodes IM 2
    // (bit4=1, bit3=1 → upper-bit = bit4 AND bit3 = 1).
    {
        fresh(im2);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x5E);
        check("IM2C-12", im2.im_mode() == 2,
              "im2_control.vhd:224 ED 5E → im_mode = 10 (IM 2)",
              fmt("im_mode=%u", im2.im_mode()));
    }

    // IM2C-13 — (B) VHDL-internal pipeline signal. im2_control.vhd updates
    // im_mode on `falling_edge(i_CLK_CPU)`; a single-threaded tick-based
    // emulator collapses rising and falling edges into one function call,
    // so the half-cycle delay is unobservable. Outcome-equivalent behaviour
    // (im_mode reflects the latest ED-4[6/E]/56 decode) is covered by
    // IM2C-10/11/12.

    // IM2C-14 — im2_control.vhd:222: im_mode <= (others => '0') on reset.
    {
        fresh(im2);
        // Drive to IM 2 first so we can see reset actually clears it.
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x5E);
        im2.reset();
        check("IM2C-14", im2.im_mode() == 0,
              "im2_control.vhd:222 im_mode defaults to 00 on reset",
              fmt("im_mode=%u after reset", im2.im_mode()));
    }
}

void section8_im2_device() {
    set_group("8. IM2 Device");
    Im2Controller im2;

    // IM2D-01 — im2_device.vhd:105-110: from S_0, on i_int_req='1' (and not
    // M1 low), state_next <= S_REQ. Exposed here via state(d) transition.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        check("IM2D-01", im2.state(Dev::CTC0) == DevState::S_REQ,
              "im2_device.vhd:106 S_0 → S_REQ on i_int_req=1",
              fmt("state=%d", static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2D-02 — im2_device.vhd:150: o_int_n = 0 when state = S_REQ AND
    // i_iei = '1' AND i_im2_mode = '1'. The aggregate int_line_asserted()
    // reflects this for device 0 (LINE, IEI=1 by construction).
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.raise_req(Dev::LINE);
        im2.tick(1);
        check("IM2D-02", im2.int_line_asserted(),
              "im2_device.vhd:150 o_int_n=0 in S_REQ with IEI=1 and im2_mode=1",
              "int_line_asserted expected true for LINE in S_REQ");
    }

    // IM2D-03 — im2_device.vhd:150: o_int_n stays '1' if IEI=0. Put CTC0
    // (higher priority than CTC1 in the chain) into S_ACK (blocks IEO) and
    // raise CTC1 — CTC1 must NOT drive int line.
    {
        fresh(im2);
        im2.set_mode(true);
        // CTC0 into S_ACK via ack_vector() path.
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);  // CTC0 now in S_REQ
        (void)im2.ack_vector();  // CTC0 → S_ACK (blocks IEO)
        // Verify that raising CTC1 doesn't produce int_line (CTC1's IEI=0
        // because CTC0 in S_ACK blocks).
        im2.set_int_en(Dev::CTC1, true);
        im2.raise_req(Dev::CTC1);
        im2.tick(1);
        bool ctc1_blocked = !im2.int_line_asserted();
        check("IM2D-03", ctc1_blocked,
              "im2_device.vhd:150 o_int_n stays high when IEI=0 (CTC0 S_ACK blocks chain)",
              fmt("int_line=%d", im2.int_line_asserted()));
    }

    // IM2D-04 — im2_device.vhd:150: o_int_n gated by i_im2_mode. In pulse
    // mode (im2_mode_=false) the device state machine is held at S_0
    // (im2_peripheral.vhd:105 im2_reset_n gate), so int_line_asserted()
    // stays false even if a device would otherwise be in S_REQ.
    {
        fresh(im2);
        im2.set_mode(false);  // pulse mode
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        check("IM2D-04",
              !im2.int_line_asserted() && im2.state(Dev::CTC0) == DevState::S_0,
              "im2_device.vhd:150 o_int_n disabled when im2_mode=0",
              fmt("int_line=%d state=%d", im2.int_line_asserted(),
                  static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2D-05 — im2_device.vhd:111-116: S_REQ → S_ACK on i_m1_n='0' AND
    // i_iorq_n='0' AND i_iei='1' AND i_im2_mode='1'. jnext models the
    // IntAck cycle via ack_vector() which latches S_REQ→S_ACK.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        (void)im2.ack_vector();
        check("IM2D-05", im2.state(Dev::CTC0) == DevState::S_ACK,
              "im2_device.vhd:112 S_REQ → S_ACK on IntAck (M1=0,IORQ=0,IEI=1)",
              fmt("state=%d", static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2D-06 — im2_device.vhd:117-122: S_ACK → S_ISR on i_m1_n='1' (i.e.
    // the cycle after IntAck). In jnext this is advanced by the next tick().
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        (void)im2.ack_vector();  // S_ACK
        im2.tick(1);             // next cycle: S_ACK → S_ISR
        check("IM2D-06", im2.state(Dev::CTC0) == DevState::S_ISR,
              "im2_device.vhd:119 S_ACK → S_ISR on next cycle",
              fmt("state=%d", static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2D-07 — im2_device.vhd:123-128: S_ISR → S_0 on i_reti_seen=1
    // AND i_iei=1 AND i_im2_mode=1.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        (void)im2.ack_vector();
        im2.tick(1);  // S_ISR
        // Drive RETI decode via the controller.
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);
        im2.tick(1);  // state machine consumes reti_seen pulse
        check("IM2D-07", im2.state(Dev::CTC0) == DevState::S_0,
              "im2_device.vhd:125 S_ISR → S_0 on reti_seen with IEI=1",
              fmt("state=%d", static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2D-08 — im2_device.vhd:123-128: S_ISR stays without RETI.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        (void)im2.ack_vector();
        im2.tick(1);  // S_ISR
        for (int i = 0; i < 10; ++i) im2.tick(1);
        check("IM2D-08", im2.state(Dev::CTC0) == DevState::S_ISR,
              "im2_device.vhd:123-128 S_ISR holds until RETI seen",
              fmt("state=%d", static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2D-09 — im2_device.vhd:155: o_vec <= i_vec while state = S_ACK
    // (or state_next = S_ACK). ack_vector() returns the composed vector
    // = (nr_c0_msb3 << 5) | (devidx << 1). CTC0 = devidx 3.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_vector_base(0x02);  // MSB3 bits = 010 → high nibble 0b010_xxxxx
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        uint8_t v = im2.ack_vector();
        uint8_t expected = static_cast<uint8_t>((0x02 << 5) | (3 << 1));  // 0x46
        check("IM2D-09", v == expected,
              "im2_device.vhd:155 + zxnext.vhd:1999 vector = msb3<<5 | idx<<1",
              fmt("got 0x%02x expected 0x%02x", v, expected));
    }

    // IM2D-10 — im2_device.vhd:155: outside S_ACK each device contributes
    // 0 to the OR-reduced vector. ack_vector() with no pending request
    // returns 0xFF (our sentinel for "nothing to ACK"), which itself is
    // evidence no device is in S_ACK.
    {
        fresh(im2);
        im2.set_mode(true);
        uint8_t v = im2.ack_vector();
        check("IM2D-10", v == 0xFF,
              "im2_device.vhd:155 no device in S_ACK → vector OR = 0 (0xFF sentinel)",
              fmt("got 0x%02x", v));
    }

    // IM2D-11 — im2_device.vhd:159: o_isr_serviced = 1 when state = S_ISR
    // AND state_next = S_0. In jnext this one-cycle pulse manifests as the
    // im2_int_req latch being cleared when S_ISR → S_0. We observe the
    // latch indirectly via int_status() dropping back after the serviced
    // cycle plus a clear_status() to drop the other half.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);  // hold int_req high through tick
        im2.tick(1);               // edge detected, latch set, state S_0→S_REQ
        (void)im2.ack_vector();    // S_REQ → S_ACK
        im2.clear_req(Dev::CTC0);  // drop peripheral-side req (AFTER first tick)
        im2.tick(1);               // S_ACK → S_ISR
        bool in_isr = (im2.state(Dev::CTC0) == DevState::S_ISR);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);
        im2.tick(1);               // reti_seen → S_ISR → S_0, latch cleared
        // After isr_serviced pulse, state = S_0 and im2_int_req cleared.
        // Clear int_status register explicitly so the composite reflects
        // only the latch state.
        im2.clear_status(Dev::CTC0);
        bool serviced = !im2.int_status(Dev::CTC0);
        check("IM2D-11", in_isr && serviced &&
                          im2.state(Dev::CTC0) == DevState::S_0,
              "im2_device.vhd:159 o_isr_serviced pulse clears im2_int_req latch",
              fmt("in_isr=%d serviced=%d state=%d",
                  in_isr, serviced,
                  static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2D-12 — im2_device.vhd:151: o_dma_int = '1' when state /= S_0 AND
    // i_dma_int_en = '1'. Drive CTC0 into S_REQ with dma_int_en set; the
    // aggregate dma_int_pending() reflects the OR-reduction.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        uint16_t mask = 1u << static_cast<int>(Dev::CTC0);
        im2.set_dma_int_en_mask(mask);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        check("IM2D-12", im2.dma_int_pending(),
              "im2_device.vhd:151 o_dma_int = 1 when state /= S_0 AND dma_int_en=1",
              fmt("dma_int_pending=%d", im2.dma_int_pending()));
    }
}

void section9_daisy_chain() {
    set_group("9. Daisy Chain");
    Im2Controller im2;

    // IM2P-01 — im2_device.vhd:139-140: in S_0, o_ieo = i_iei. Device 0's
    // IEI is hardwired to 1 (peripherals.vhd:82), so device 0's IEO is 1
    // when it is idle. We observe via ieo(LINE).
    {
        fresh(im2);
        im2.set_mode(true);
        check("IM2P-01", im2.ieo(Dev::LINE),
              "im2_device.vhd:139-140 S_0 IEO = IEI (device 0 IEI hardwired 1)",
              fmt("ieo(LINE)=%d", im2.ieo(Dev::LINE)));
    }

    // IM2P-02 — im2_device.vhd:141-142: in S_REQ, o_ieo = i_iei AND
    // i_reti_decode. Outside reti_decode, IEO drops to 0 for an S_REQ
    // device; during reti_decode, IEO passes through.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.raise_req(Dev::LINE);
        im2.tick(1);  // LINE now in S_REQ
        bool ieo_off = !im2.ieo(Dev::LINE);
        // Drive reti_decode high via ED prefix.
        im2.on_m1_cycle(0x0000, 0xED);
        bool ieo_on = im2.ieo(Dev::LINE);
        check("IM2P-02", ieo_off && ieo_on,
              "im2_device.vhd:141-142 S_REQ IEO = IEI AND reti_decode",
              fmt("outside=%d during=%d", !ieo_off, ieo_on));
    }

    // IM2P-03 — im2_device.vhd:143-144: in S_ACK/S_ISR, o_ieo = 0
    // unconditionally (blocks lower-priority devices).
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.raise_req(Dev::LINE);
        im2.tick(1);
        (void)im2.ack_vector();  // S_ACK
        bool ack_blocked = !im2.ieo(Dev::LINE);
        im2.tick(1);             // S_ISR
        bool isr_blocked = !im2.ieo(Dev::LINE);
        check("IM2P-03", ack_blocked && isr_blocked,
              "im2_device.vhd:143-144 S_ACK / S_ISR hold IEO=0",
              fmt("ack=%d isr=%d", ack_blocked, isr_blocked));
    }

    // IM2P-04 — peripherals.vhd:82 + zxnext.vhd:1984: peripheral index 0
    // (LINE) has its i_iei hardwired to '1'. When all devices are idle,
    // device 0's IEO must therefore be 1. (This is the base case of the
    // chain head.)
    {
        fresh(im2);
        im2.set_mode(true);
        check("IM2P-04", im2.ieo(Dev::LINE),
              "peripherals.vhd:82 + zxnext.vhd:1984 chain-head IEI hardwired 1",
              "chain head (LINE) IEO expected 1 when idle");
    }

    // IM2P-05 — peripherals.vhd:86-128 priority generate-loop: when two
    // devices request simultaneously, only the higher-priority device's
    // o_int_n drives the line; the lower one is blocked. Verify by
    // ack_vector() selecting the higher-priority device (LINE=0) over
    // CTC3=6.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.set_int_en(Dev::CTC3, true);
        im2.raise_req(Dev::LINE);
        im2.raise_req(Dev::CTC3);
        im2.tick(1);
        uint8_t v = im2.ack_vector();
        // LINE = devidx 0 → vector = (0 << 5) | (0 << 1) = 0x00
        // CTC3 = devidx 6 → vector = 0x0C (with base=0)
        check("IM2P-05",
              v == 0x00 && im2.state(Dev::LINE) == DevState::S_ACK &&
              im2.state(Dev::CTC3) == DevState::S_REQ,
              "peripherals.vhd:86-128 IntAck resolves to higher-priority device (LINE over CTC3)",
              fmt("vec=0x%02x LINE=%d CTC3=%d", v,
                  static_cast<int>(im2.state(Dev::LINE)),
                  static_cast<int>(im2.state(Dev::CTC3))));
    }

    // IM2P-06 — peripherals.vhd:86-128 + im2_device.vhd:139-144: while a
    // higher-priority device is in S_ACK or S_ISR, a lower-priority
    // device raising its request can enter S_REQ but is blocked from
    // driving the int line (IEI=0 due to upstream block).
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.set_int_en(Dev::CTC3, true);
        im2.raise_req(Dev::LINE);
        im2.tick(1);
        (void)im2.ack_vector();  // LINE → S_ACK
        im2.tick(1);             // LINE → S_ISR
        im2.raise_req(Dev::CTC3);
        im2.tick(1);             // CTC3 can enter S_REQ but is blocked
        bool ctc3_in_req = (im2.state(Dev::CTC3) == DevState::S_REQ);
        // int_line_asserted requires IEI=1 at the S_REQ device; since LINE
        // is in S_ISR (IEO=0), CTC3's IEI is 0 and no int is asserted.
        bool int_blocked = !im2.int_line_asserted();
        check("IM2P-06", ctc3_in_req && int_blocked,
              "im2_device.vhd:143-144 lower-priority S_REQ waits while LINE S_ISR blocks chain",
              fmt("ctc3_req=%d int_blocked=%d", ctc3_in_req, int_blocked));
    }

    // IM2P-07 — im2_device.vhd:123-128 + peripherals.vhd: after LINE
    // returns to S_0 via RETI, CTC3 (still in S_REQ) sees IEI rise and can
    // now drive the int line.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.set_int_en(Dev::CTC3, true);
        im2.raise_req(Dev::LINE);
        im2.tick(1);
        (void)im2.ack_vector();
        im2.tick(1);             // LINE in S_ISR
        im2.clear_req(Dev::LINE);  // peripheral deasserts
        im2.raise_req(Dev::CTC3);
        im2.tick(1);             // CTC3 in S_REQ
        // RETI: LINE S_ISR → S_0
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);
        im2.tick(1);             // consume reti_seen
        bool line_cleared = (im2.state(Dev::LINE) == DevState::S_0);
        bool ctc3_can_int = im2.int_line_asserted();
        check("IM2P-07", line_cleared && ctc3_can_int,
              "im2_device.vhd:123-128 + chain: post-RETI lower-priority proceeds",
              fmt("line_cleared=%d ctc3_int=%d", line_cleared, ctc3_can_int));
    }

    // IM2P-08 — peripherals.vhd generate-loop with 3 devices: priority
    // chain resolves IntAck through LINE > CTC0 > CTC3 in order.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.set_int_en(Dev::CTC0, true);
        im2.set_int_en(Dev::CTC3, true);
        im2.raise_req(Dev::LINE);
        im2.raise_req(Dev::CTC0);
        im2.raise_req(Dev::CTC3);
        im2.tick(1);
        // First ack: LINE (idx 0)
        uint8_t v1 = im2.ack_vector();
        check("IM2P-08", v1 == 0x00,
              "peripherals.vhd generate: 3-way chain — LINE wins first ack",
              fmt("vec1=0x%02x", v1));
    }

    // IM2P-09 — peripherals.vhd:146-156: o_int_n is AND-reduced across all
    // devices' local o_int_n. Equivalent to: the aggregate line is low iff
    // ANY device drives its local low — so raising ONE device in S_REQ is
    // sufficient.
    {
        fresh(im2);
        im2.set_mode(true);
        bool idle = !im2.int_line_asserted();
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool one_raised = im2.int_line_asserted();
        check("IM2P-09", idle && one_raised,
              "peripherals.vhd:146-156 AND-reduction: any S_REQ device asserts aggregate",
              fmt("idle=%d raised=%d", idle, one_raised));
    }

    // IM2P-10 — peripherals.vhd:134-144: o_vec is OR-reduced across all
    // devices (each drives 0 except the one in S_ACK). Verify by acking
    // CTC1 (idx 4): vector low nibble = idx<<1 = 0x08.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC1, true);
        im2.raise_req(Dev::CTC1);
        im2.tick(1);
        uint8_t v = im2.ack_vector();
        uint8_t expected = static_cast<uint8_t>(4 << 1);  // 0x08
        check("IM2P-10", v == expected,
              "peripherals.vhd:134-144 vector OR-reduction selects S_ACK device",
              fmt("got 0x%02x expected 0x%02x", v, expected));
    }
}

void section10_pulse_mode() {
    set_group("10. Pulse Mode");
    Im2Controller im2;

    // PULSE-01 — im2_peripheral.vhd:186 / zxnext.vhd:1996: pulse_en path
    // driven by (int_req AND int_en) OR int_unq in pulse mode. A qualifying
    // edge must drop pulse_int_n from high to low.
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(false);  // pulse mode
        im2.set_int_en(Dev::CTC0, true);
        bool before = im2.pulse_int_n();
        im2.raise_req(Dev::CTC0);
        im2.tick(1);  // step_pulse sees int_req edge, drops INT_n
        bool after = im2.pulse_int_n();
        check("PULSE-01", before && !after,
              "im2_peripheral.vhd:186 pulse_en → pulse_int_n drops on edge",
              fmt("before=%d after=%d", before, after));
    }

    // PULSE-02 — im2_peripheral.vhd:186: non-exception devices produce
    // pulse_en only when i_mode_pulse_0_im2_1='0'. In IM2 mode (true),
    // CTC0 must not generate a pulse.
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(true);  // IM2 mode suppresses non-exception pulses
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        check("PULSE-02", im2.pulse_int_n(),
              "im2_peripheral.vhd:186 non-exception pulse suppressed in IM2 mode",
              fmt("pulse_int_n=%d (expected 1)", im2.pulse_int_n()));
    }

    // PULSE-03 — im2_peripheral.vhd:192: ULA EXCEPTION='1' still produces
    // pulse in IM2 mode, provided Z80 is not itself in IM=2.
    //
    // BUG FLAG NOTE: im2.cpp step_pulse() gates ULA exception with
    // "!im2_mode_ || (im_mode_ != 2)" — i.e. always fires unless CPU is
    // in IM=2. We honour that gate here (leave im_mode as 0 = IM 0).
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(true);          // IM2 mode (for non-exception path)
        im2.set_int_en(Dev::ULA, true);
        im2.raise_req(Dev::ULA);
        im2.tick(1);
        check("PULSE-03", !im2.pulse_int_n(),
              "im2_peripheral.vhd:192 ULA EXCEPTION fires even in IM2 mode (CPU not in IM 2)",
              fmt("pulse_int_n=%d (expected 0)", im2.pulse_int_n()));
    }

    // PULSE-04 — zxnext.vhd:2017-2031: once dropped, pulse_int_n stays
    // low for the machine-specific duration. Verify it is still low after
    // a few ticks within the pulse window.
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(false);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);  // drops
        bool stays = true;
        for (int i = 0; i < 8; ++i) {
            im2.tick(1);
            stays = stays && !im2.pulse_int_n();
        }
        check("PULSE-04", stays,
              "zxnext.vhd:2017-2031 pulse_int_n stays low during pulse window",
              fmt("pulse held low for 8 ticks: %d", stays));
    }

    // PULSE-05 — zxnext.vhd:2033: 48K/+3 pulse terminates when
    // pulse_count bit 5 becomes 1 (count = 32). Our sequencer drops on
    // tick 1 (count=0), increments on each subsequent tick; termination
    // when count reaches 32. Use a slightly wider window to be robust to
    // fencepost fencing (counter starts at 0, bit5 set at 32).
    //
    // Note: we hold int_req high for the duration — the edge detect
    // naturally suppresses re-trigger (int_req_d tracks int_req after tick 1).
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(false);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);   // drops to 0
        bool low_during = !im2.pulse_int_n();
        int ticks_low = 1;  // count the drop tick
        while (!im2.pulse_int_n() && ticks_low < 60) {
            im2.tick(1);
            ++ticks_low;
        }
        check("PULSE-05",
              low_during && ticks_low >= 30 && ticks_low <= 40,
              "zxnext.vhd:2033 48K/+3 pulse width ~ 32 cycles (bit5 terminator)",
              fmt("ticks_low=%d (expected 30..40)", ticks_low));
    }

    // PULSE-06 — zxnext.vhd:2033: 128K/Pentagon pulse terminates when
    // bit5 AND bit2 (count = 36). We test by leaving
    // set_machine_timing_48_or_p3 off (false = default) so pulse_count_end
    // = bit5 AND bit2.
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(false);
        im2.set_mode(false);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        int ticks_low = 1;
        while (!im2.pulse_int_n() && ticks_low < 80) {
            im2.tick(1);
            ++ticks_low;
        }
        check("PULSE-06",
              ticks_low >= 34 && ticks_low <= 44,
              "zxnext.vhd:2033 128K/Pentagon pulse width ~ 36 cycles (bit5 AND bit2)",
              fmt("ticks_low=%d (expected 34..44)", ticks_low));
    }

    // PULSE-07 — zxnext.vhd:2036-2044: pulse counter resets to 0 while
    // pulse_int_n='1' (idle). After a full pulse, the next pulse starts
    // fresh (not from the previous terminator count).
    //
    // A new edge must happen AFTER the first pulse drains. The int_req
    // edge detector uses int_req_d to gate; we clear_req (int_req=0,
    // int_req_d=1) and tick to let int_req_d settle, then raise_req
    // again to generate a fresh edge for the second pulse.
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(false);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        // Drain the first pulse.
        while (!im2.pulse_int_n()) im2.tick(1);
        // Drop int_req and let int_req_d settle.
        im2.clear_req(Dev::CTC0);
        im2.tick(1);
        im2.tick(1);
        // Trigger second edge.
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool second_drops = !im2.pulse_int_n();
        int ticks_low = 1;
        while (!im2.pulse_int_n() && ticks_low < 60) {
            im2.tick(1);
            ++ticks_low;
        }
        check("PULSE-07",
              second_drops && ticks_low >= 30 && ticks_low <= 40,
              "zxnext.vhd:2036-2044 pulse counter reset while pulse_int_n=1",
              fmt("second_drops=%d ticks_low=%d", second_drops, ticks_low));
    }

    // PULSE-08 — zxnext.vhd:1840: aggregate INT_n to Z80 is
    // pulse_int_n AND im2_int_n. In pulse mode im2_int_n is always high
    // (state machine held at S_0 by im2_reset_n=0), so INT_n = pulse_int_n.
    // Check the AND by confirming int_line_asserted() is false in pulse
    // mode even when a pulse is active (the Z80 sees INT via pulse_int_n,
    // not im2 fabric).
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(false);  // pulse mode
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool pulse_low = !im2.pulse_int_n();
        bool im2_line = im2.int_line_asserted();  // always false in pulse mode
        check("PULSE-08", pulse_low && !im2_line,
              "zxnext.vhd:1840 INT_n = pulse_int_n AND im2_int_n; im2_int_n high in pulse mode",
              fmt("pulse_low=%d im2_line=%d", pulse_low, im2_line));
    }

    // PULSE-09 — (B) VHDL-internal pipeline signal. zxnext.vhd drives
    // `o_BUS_INT_n <= pulse_int_n AND im2_int_n` out to the expansion-bus
    // connector. jnext has no modelled expansion bus at this layer; the
    // compose is outcome-identical to PULSE-08 (internal INT_n to Z80).
}

void section11_im2_peripheral() {
    set_group("11. IM2 Peripheral");
    Im2Controller im2;

    // IM2W-01 — im2_peripheral.vhd:90-101: edge detect `int_req AND NOT
    // int_req_d` — a held-high int_req latches once and does NOT keep
    // re-pulsing. Verify by: raise, tick, the im2_int_req latch is set;
    // then if we tick again without a new edge, int_status stays but no
    // additional state transitions on already-serviced state (we observe
    // indirectly: pulse fabric fires only once in pulse mode).
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(false);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);  // edge fires pulse
        bool first_pulse = !im2.pulse_int_n();
        // Drain the first pulse.
        while (!im2.pulse_int_n()) im2.tick(1);
        // Continue holding int_req high (no new edge). Tick a few more;
        // pulse should NOT re-fire because int_req_d is now 1 = no edge.
        bool no_retrigger = true;
        for (int i = 0; i < 10; ++i) {
            im2.tick(1);
            if (!im2.pulse_int_n()) { no_retrigger = false; break; }
        }
        check("IM2W-01", first_pulse && no_retrigger,
              "im2_peripheral.vhd:90-101 int_req edge detect fires once per rising edge",
              fmt("first=%d no_retrigger=%d", first_pulse, no_retrigger));
    }

    // IM2W-02 — im2_peripheral.vhd:167-178: im2_int_req latch set on
    // (int_unq='1') OR (int_req edge AND int_en='1'). Observable via
    // int_status() which ORs int_status + im2_int_req.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool set = im2.int_status(Dev::CTC0);
        check("IM2W-02", set,
              "im2_peripheral.vhd:167-178 im2_int_req latched on qualified edge",
              fmt("int_status(CTC0)=%d", set));
    }

    // IM2W-03 — im2_peripheral.vhd:148 + 175: the im2_isr_serviced pulse
    // (S_ISR → S_0) clears im2_int_req. After a full ISR cycle the latch
    // is cleared. int_req is HELD HIGH across the first tick (edge detect
    // requires int_req=1 vs int_req_d=0); clear_req is deferred to after
    // the edge has fired.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);               // edge, latch, S_0 → S_REQ
        (void)im2.ack_vector();    // S_REQ → S_ACK
        im2.clear_req(Dev::CTC0);  // drop peripheral req after edge fired
        im2.tick(1);               // S_ACK → S_ISR
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);
        im2.tick(1);               // S_ISR → S_0, im2_isr_serviced clears latch
        im2.clear_status(Dev::CTC0);  // clear the int_status register too
        bool cleared = !im2.int_status(Dev::CTC0);
        check("IM2W-03", cleared,
              "im2_peripheral.vhd:148,175 im2_isr_serviced clears im2_int_req latch",
              fmt("int_status(CTC0)=%d (expected 0)", im2.int_status(Dev::CTC0)));
    }

    // IM2W-04 — im2_peripheral.vhd:154-162: int_status set by int_req
    // edge. Observable via int_status() after raise_req + tick.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        check("IM2W-04", im2.int_status(Dev::CTC0),
              "im2_peripheral.vhd:160 int_status set on int_req edge",
              "int_status expected 1");
    }

    // IM2W-05 — im2_peripheral.vhd:160: clear_status() clears int_status
    // but NOT im2_int_req (which is only cleared by im2_isr_serviced, line
    // 175). We cover this invariant in two stages:
    //   (a) raise → clear_status → latch keeps composite true;
    //   (b) raise → full service (latch clear) → clear_status → composite
    //       goes fully false.
    //
    // Helper that fully drains the decoder after a RETI so a follow-up
    // ED sequence starts fresh at S_0.
    auto drain_decoder = [&]() {
        // From S_ED4D_T4 → S_SRL_T1 → S_SRL_T2 → S_0. Three no-op opcodes.
        im2.on_m1_cycle(0xF000, 0x00);
        im2.on_m1_cycle(0xF000, 0x00);
        im2.on_m1_cycle(0xF000, 0x00);
    };

    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);               // edge, latch, S_0 → S_REQ
        im2.clear_status(Dev::CTC0);
        // Composite int_status = int_status_reg OR im2_int_req = 0 OR 1 = 1.
        bool composite_still_true = im2.int_status(Dev::CTC0);

        // Service the ISR fully to drop the latch.
        (void)im2.ack_vector();
        im2.clear_req(Dev::CTC0);
        im2.tick(1);               // S_ACK → S_ISR
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);  // reti_seen
        im2.tick(1);               // S_ISR → S_0, latch cleared
        drain_decoder();           // bring dec_state_ back to S_0

        // Raise a new edge → int_status and latch both set.
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool both_set = im2.int_status(Dev::CTC0);

        // Service fully a second time.
        (void)im2.ack_vector();
        im2.clear_req(Dev::CTC0);
        im2.tick(1);               // S_ACK → S_ISR
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);               // latch cleared
        drain_decoder();
        // int_status register still 1; clear it.
        im2.clear_status(Dev::CTC0);
        bool final_cleared = !im2.int_status(Dev::CTC0);
        check("IM2W-05",
              composite_still_true && both_set && final_cleared,
              "im2_peripheral.vhd:160 NR 0xC8/C9/CA write clears int_status register",
              fmt("composite=%d both=%d final=%d",
                  composite_still_true, both_set, final_cleared));
    }

    // IM2W-06 — im2_peripheral.vhd:180: o_int_status = int_status OR
    // im2_int_req. Both halves are exposed via int_status(DevIdx). A
    // raise_unq() sets BOTH directly; clear_status alone leaves the latch
    // set → composite stays true.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.raise_unq(Dev::CTC0);
        im2.tick(1);
        bool unq_composite = im2.int_status(Dev::CTC0);
        im2.clear_status(Dev::CTC0);
        bool composite_after_clear = im2.int_status(Dev::CTC0);
        check("IM2W-06", unq_composite && composite_after_clear,
              "im2_peripheral.vhd:180 o_int_status = int_status OR im2_int_req",
              fmt("unq=%d after_clear=%d",
                  unq_composite, composite_after_clear));
    }

    // IM2W-07 — im2_peripheral.vhd:105: im2_reset_n = i_mode_pulse_0_im2_1
    // AND NOT i_reset. In pulse mode (i_mode_pulse_0_im2_1='0') the state
    // machine is held at S_0 regardless of int_req. Flipping into IM2 mode
    // allows transitions again.
    {
        fresh(im2);
        im2.set_mode(false);  // pulse mode → reset held
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool held = (im2.state(Dev::CTC0) == DevState::S_0);
        im2.set_mode(true);  // IM2 mode → reset released
        im2.tick(1);
        bool released = (im2.state(Dev::CTC0) == DevState::S_REQ);
        check("IM2W-07", held && released,
              "im2_peripheral.vhd:105 im2_reset_n composite gates state transitions",
              fmt("held=%d released=%d state=%d",
                  held, released, static_cast<int>(im2.state(Dev::CTC0))));
    }

    // IM2W-08 — im2_peripheral.vhd:172: int_unq bypasses i_int_en. Without
    // setting int_en, raise_unq must latch im2_int_req anyway.
    {
        fresh(im2);
        im2.set_mode(true);
        // CTC0 int_en is FALSE.
        im2.raise_unq(Dev::CTC0);
        im2.tick(1);
        bool latched = (im2.state(Dev::CTC0) == DevState::S_REQ);
        bool status = im2.int_status(Dev::CTC0);
        check("IM2W-08", latched && status,
              "im2_peripheral.vhd:172 int_unq latches im2_int_req without int_en",
              fmt("state=%d status=%d",
                  static_cast<int>(im2.state(Dev::CTC0)), status));
    }

    // IM2W-09 — (B) VHDL-internal pipeline signal. im2_peripheral.vhd:137-148
    // samples isr_serviced from CLK_CPU into CLK_28. A single-threaded tick-
    // based emulator collapses both domains into one call. Outcome (latch-
    // then-clear) is covered by IM2W-03.
}

void section12_ula_line_int() {
    set_group("12. ULA/Line Int");
    Im2Controller im2;

    // ULA-INT-01 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (ULA HC/VC interrupt fires at int_h/int_v — needs full Emulator +
    // Ula + Im2Controller to observe; added in Phase 3).
    // ULA-INT-02 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (port 0xFF bit suppresses ULA interrupt — needs port dispatch +
    // ULA + Im2Controller wiring; added in Phase 3).
    // ULA-INT-03 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (ula_int_en = NOT port_ff_interrupt_disable — same integration
    // surface as ULA-INT-02; added in Phase 3).
    // ULA-INT-04 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (line interrupt fires at cvc match at hc_ula=255 — needs full
    // Emulator + scheduler + Im2Controller; added 2026-04-24).
    // ULA-INT-05 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (NR 0x22 line_interrupt_en bit — needs NextREG + ULA integration;
    // added in Phase 3).
    // ULA-INT-06 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (line 0 wraps to c_max_vc — needs full Emulator + scheduler;
    // added 2026-04-24).

    // ULA-INT-07 — zxnext.vhd:1941: IM2 priority index 11 = ULA. When
    // LINE (index 0) and ULA (index 11) both raise, LINE wins the ack.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.set_int_en(Dev::ULA, true);
        im2.raise_req(Dev::LINE);
        im2.raise_req(Dev::ULA);
        im2.tick(1);
        uint8_t v = im2.ack_vector();
        // LINE idx 0 → vector 0x00 with base=0.
        check("ULA-INT-07",
              v == 0x00 &&
              im2.state(Dev::LINE) == DevState::S_ACK &&
              im2.state(Dev::ULA) == DevState::S_REQ,
              "zxnext.vhd:1941 priority idx11 (ULA) below LINE (idx0)",
              fmt("vec=0x%02x LINE=%d ULA=%d", v,
                  static_cast<int>(im2.state(Dev::LINE)),
                  static_cast<int>(im2.state(Dev::ULA))));
    }

    // ULA-INT-08 — zxnext.vhd:1941: LINE lives at priority index 0 (highest).
    // When LINE alone is raised, it wins and the ack-vector low nibble
    // reflects idx=0.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.raise_req(Dev::LINE);
        im2.tick(1);
        uint8_t v = im2.ack_vector();
        check("ULA-INT-08",
              v == 0x00 && im2.state(Dev::LINE) == DevState::S_ACK,
              "zxnext.vhd:1941 LINE at priority idx0 (chain head)",
              fmt("vec=0x%02x LINE=%d", v,
                  static_cast<int>(im2.state(Dev::LINE))));
    }

    // ULA-INT-09 — zxnext.vhd:1964 / im2_peripheral.vhd:192: EXCEPTION='1'
    // only for ULA. EXCEPTION devices fire a pulse even in IM2 mode
    // (provided CPU is not in IM=2). Non-EXCEPTION devices do not. Compare
    // ULA vs CTC0 in IM2 mode with CPU in IM 0 (default after reset).
    {
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(true);  // IM2 fabric mode
        // Case 1: ULA (EXCEPTION=1) fires a pulse.
        im2.set_int_en(Dev::ULA, true);
        im2.raise_req(Dev::ULA);
        im2.tick(1);
        bool ula_pulsed = !im2.pulse_int_n();
        // Reset and test non-EXCEPTION device.
        fresh(im2);
        im2.set_machine_timing_48_or_p3(true);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool ctc_pulsed = !im2.pulse_int_n();
        check("ULA-INT-09", ula_pulsed && !ctc_pulsed,
              "zxnext.vhd:1964 EXCEPTION=1 only for ULA (IM2-mode pulse fires)",
              fmt("ula_pulsed=%d ctc_pulsed=%d",
                  ula_pulsed, ctc_pulsed));
    }
}

void section13_nextreg_int_regs() {
    set_group("13. NR 0xC0-0xCE");
    Im2Controller im2;

    // NR-C0-01 — zxnext.vhd:5597/1999: NR 0xC0 bits [7:5] store
    // nr_c0_im2_vector[2:0] (vector MSBs). Bare-Im2 readback via
    // vector_base().
    {
        fresh(im2);
        im2.set_vector_base(0x05);
        check("NR-C0-01", im2.vector_base() == 0x05,
              "zxnext.vhd:5597/1999 NR 0xC0 stores im2_vector_base[2:0]",
              fmt("got 0x%02x", im2.vector_base()));
    }

    // WONT NR-C0-02: Stackless NMI (NR 0xC0 bit 3). Implementing requires
    // patching the FUSE Z80 core (no pre-NMI-push hook, no RETN
    // interception) and risks the 1356-row FUSE regression for a single
    // test row's benefit. Wave D cut per TASK-NMI-SOURCE-PIPELINE-PLAN.md
    // Q1. Revisit only if a second driver row or user-visible bug appears.

    // NR-C0-03 — zxnext.vhd:5599/1975: NR 0xC0 bit 0 (int_mode_pulse_0_im2_1)
    // selects pulse (0) vs IM2 (1) mode. Bare-Im2 accessor via is_im2_mode()
    // and set_mode().
    {
        fresh(im2);
        bool defaulted = !im2.is_im2_mode();
        im2.set_mode(true);
        bool on = im2.is_im2_mode();
        im2.set_mode(false);
        bool off = !im2.is_im2_mode();
        check("NR-C0-03", defaulted && on && off,
              "zxnext.vhd:5599/1975 NR 0xC0 bit 0 toggles pulse/IM2 mode",
              fmt("default=%d on=%d off=%d", defaulted, on, off));
    }

    // NR-C0-04 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (NR 0xC0 readback composes vector-MSBs + stackless + im_mode +
    // int_mode — needs full NextReg + Im2Controller integration).

    // NR-C4-01 — zxnext.vhd NR 0xC4: bit 1 enables LINE interrupt fan-out.
    // Bare-Im2 observable via set_int_en_c4 + a subsequent raise_req/
    // int_line_asserted flow for LINE.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en_c4(0x02);  // bit 1 = LINE enable
        im2.raise_req(Dev::LINE);
        im2.tick(1);
        check("NR-C4-01", im2.int_line_asserted(),
              "zxnext.vhd NR 0xC4 bit1 enables LINE interrupt fan-out",
              fmt("int_line=%d (expected 1)", im2.int_line_asserted()));
    }

    // NR-C4-02 — RE-HOME (NR 0xC4 bit 1 drives line_interrupt_en via NR 0x22).
    // NR-C4-03 — RE-HOME (NR 0xC4 readback E_00000_UU).

    // NR-C5-01 — zxnext.vhd:4078/1949: NR 0xC5 bits 0..7 fan out to
    // CTC0..7 int_en. Bare-Im2 drives via set_int_en_c5 and observes via
    // state() transitions (gated by int_en).
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en_c5(0x01);  // CTC0 enabled
        im2.raise_req(Dev::CTC0);
        im2.raise_req(Dev::CTC1);  // not enabled
        im2.tick(1);
        bool ctc0_req = (im2.state(Dev::CTC0) == DevState::S_REQ);
        bool ctc1_zero = (im2.state(Dev::CTC1) == DevState::S_0);
        check("NR-C5-01", ctc0_req && ctc1_zero,
              "zxnext.vhd:4078/1949 NR 0xC5 fans out CTC int_en bits 0..7",
              fmt("CTC0_state=%d CTC1_state=%d",
                  static_cast<int>(im2.state(Dev::CTC0)),
                  static_cast<int>(im2.state(Dev::CTC1))));
    }

    // NR-C5-02 — (E) redundant coverage. Duplicate of CTC-NR-02 in
    // Section 6. A second assertion here would add no invariant coverage.

    // NR-C6-01 — zxnext.vhd NR 0xC6/1949: UART int enable bits. Bit 6 =
    // UART1 TX; we drive that and observe fan-out to UART1_TX int_en via
    // a raise_req + state() transition.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en_c6(0x40);  // bit 6 = UART1_TX
        im2.raise_req(Dev::UART1_TX);
        im2.tick(1);
        check("NR-C6-01",
              im2.state(Dev::UART1_TX) == DevState::S_REQ,
              "zxnext.vhd NR 0xC6/1949 UART1_TX int_en enables fabric path",
              fmt("state=%d", static_cast<int>(im2.state(Dev::UART1_TX))));
    }

    // NR-C6-02 — RE-HOME (NR 0xC6 readback format 0_654_0_210).

    // NR-C8-01 — zxnext.vhd:1952-1955 / 6247: NR 0xC8 read = {0,0,0,0,0,0,
    // LINE,ULA} int_status bits. Drive status high on both, observe the
    // packed byte.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::LINE, true);
        im2.set_int_en(Dev::ULA, true);
        im2.raise_req(Dev::LINE);
        im2.raise_req(Dev::ULA);
        im2.tick(1);
        uint8_t got = im2.int_status_mask_c8();
        check("NR-C8-01", got == 0x03,
              "zxnext.vhd:6247 NR 0xC8 read {LINE,ULA} → bits 1:0",
              fmt("got 0x%02x expected 0x03", got));
    }

    // NR-C9-01 — zxnext.vhd:1953 / 6250: NR 0xC9 read = CTC7..0 int_status.
    // Drive CTC0 and CTC2; expected bits 0 and 2 set.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.set_int_en(Dev::CTC2, true);
        im2.raise_req(Dev::CTC0);
        im2.raise_req(Dev::CTC2);
        im2.tick(1);
        uint8_t got = im2.int_status_mask_c9();
        check("NR-C9-01", got == 0x05,
              "zxnext.vhd:6250 NR 0xC9 read CTC7..0 packs int_status bits",
              fmt("got 0x%02x expected 0x05", got));
    }

    // NR-CA-01 — zxnext.vhd:1952/1954 / 6253: NR 0xCA read packs UART
    // status — UART1_TX → bit 6, UART1_RX → bits 5+4, UART0_TX → bit 2,
    // UART0_RX → bits 1+0.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::UART0_RX, true);
        im2.set_int_en(Dev::UART1_TX, true);
        im2.raise_req(Dev::UART0_RX);
        im2.raise_req(Dev::UART1_TX);
        im2.tick(1);
        uint8_t got = im2.int_status_mask_ca();
        uint8_t expected = 0x40 | 0x03;  // UART1_TX bit 6 + UART0_RX bits 1,0
        check("NR-CA-01", got == expected,
              "zxnext.vhd:6253 NR 0xCA read packs UART int_status",
              fmt("got 0x%02x expected 0x%02x", got, expected));
    }

    // NR-CC-01 — zxnext.vhd:5629-5630/1957-1958: NR 0xCC DMA int enable
    // group 0 (bits i = DMA_INT_EN for device i). set_dma_int_en_mask
    // fans the 14-bit mask out to dev_[].dma_int_en. Drive a mask with
    // CTC0 bit set; verify that CTC0 in S_REQ produces dma_int_pending.
    {
        fresh(im2);
        im2.set_mode(true);
        uint16_t mask = 1u << static_cast<int>(Dev::CTC0);
        im2.set_dma_int_en_mask(mask);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        check("NR-CC-01", im2.dma_int_pending(),
              "zxnext.vhd:5629-5630/1957-1958 NR 0xCC DMA int enable bit routes through mask",
              fmt("dma_int_pending=%d", im2.dma_int_pending()));
    }

    // NR-CD-01 — zxnext.vhd:5633/1957: NR 0xCD DMA int enable group 1.
    // The mask is a single 14-bit fan-out (compose_im2_dma_int_en already
    // combines CC/CD/CE in the emulator). At the Im2Controller layer the
    // accessor is the same; we drive a bit in the upper nibble (ULA=11)
    // and observe.
    {
        fresh(im2);
        im2.set_mode(true);
        uint16_t mask = 1u << static_cast<int>(Dev::ULA);
        im2.set_dma_int_en_mask(mask);
        im2.set_int_en(Dev::ULA, true);
        im2.raise_req(Dev::ULA);
        im2.tick(1);
        check("NR-CD-01", im2.dma_int_pending(),
              "zxnext.vhd:5633/1957 NR 0xCD DMA int enable bit routes through mask",
              fmt("dma_int_pending=%d", im2.dma_int_pending()));
    }

    // NR-CE-01 — zxnext.vhd:5636-5637/1957-1958: NR 0xCE DMA int enable
    // group 2. Drive UART1_TX (bit 13) through the mask.
    {
        fresh(im2);
        im2.set_mode(true);
        uint16_t mask = 1u << static_cast<int>(Dev::UART1_TX);
        im2.set_dma_int_en_mask(mask);
        im2.set_int_en(Dev::UART1_TX, true);
        im2.raise_req(Dev::UART1_TX);
        im2.tick(1);
        check("NR-CE-01", im2.dma_int_pending(),
              "zxnext.vhd:5636-5637/1957-1958 NR 0xCE DMA int enable bit routes through mask",
              fmt("dma_int_pending=%d", im2.dma_int_pending()));
    }
}

void section14_status_clear() {
    set_group("14. Status/Clear");
    Im2Controller im2;

    // Helper: raise a clean edge and service its ISR fully so the
    // im2_int_req latch is dropped, leaving the int_status register as
    // the only bit contributing to int_status(). Needed because
    // clear_status() does NOT clear the latch (VHDL im2_peripheral.vhd:175).
    //
    // int_req is HELD HIGH across the first tick (the edge detect needs
    // int_req=1 while int_req_d=0). clear_req is deferred to after the
    // first tick so the latch doesn't re-fire on subsequent ticks.
    //
    // After the RETI decode consumes the latch, we drain the decoder
    // state machine back to S_0 (via three dummy opcodes) so a subsequent
    // ED/4D sequence in the calling test body starts fresh.
    auto raise_and_service = [&](Dev d) {
        im2.set_int_en(d, true);
        im2.raise_req(d);
        im2.tick(1);            // edge, latch, S_0 → S_REQ
        (void)im2.ack_vector(); // S_REQ → S_ACK
        im2.clear_req(d);       // drop peripheral req AFTER the edge fires
        im2.tick(1);            // S_ACK → S_ISR
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);
        im2.tick(1);            // im2_isr_serviced → latch cleared
        // Drain decoder: S_ED4D_T4 → S_SRL_T1 → S_SRL_T2 → S_0.
        im2.on_m1_cycle(0xF000, 0x00);
        im2.on_m1_cycle(0xF000, 0x00);
        im2.on_m1_cycle(0xF000, 0x00);
    };

    // ISC-01 — zxnext.vhd:1955 / im2_peripheral.vhd:160: NR 0xC8 b1 clears
    // LINE int_status. After service (latch cleared) a new edge re-sets
    // int_status; clear_status(LINE) drops it.
    {
        fresh(im2);
        im2.set_mode(true);
        raise_and_service(Dev::LINE);
        im2.raise_req(Dev::LINE);
        im2.tick(1);
        bool set = im2.int_status(Dev::LINE);
        im2.clear_req(Dev::LINE);
        // Service to drop the latch so clear_status alone can be observed.
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);
        // Now only int_status register is still true. Clear it.
        im2.clear_status(Dev::LINE);
        bool cleared = !im2.int_status(Dev::LINE);
        check("ISC-01", set && cleared,
              "zxnext.vhd:1955 NR 0xC8 b1 clears LINE int_status",
              fmt("set=%d cleared=%d", set, cleared));
    }

    // ISC-02 — zxnext.vhd:1952: NR 0xC8 b0 clears ULA int_status.
    {
        fresh(im2);
        im2.set_mode(true);
        raise_and_service(Dev::ULA);
        im2.raise_req(Dev::ULA);
        im2.tick(1);
        bool set = im2.int_status(Dev::ULA);
        im2.clear_req(Dev::ULA);
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);
        im2.clear_status(Dev::ULA);
        bool cleared = !im2.int_status(Dev::ULA);
        check("ISC-02", set && cleared,
              "zxnext.vhd:1952 NR 0xC8 b0 clears ULA int_status",
              fmt("set=%d cleared=%d", set, cleared));
    }

    // ISC-03 — zxnext.vhd:1953: NR 0xC9 bits clear CTC7..0 int_status.
    // Verify for CTC0.
    {
        fresh(im2);
        im2.set_mode(true);
        raise_and_service(Dev::CTC0);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool set = im2.int_status(Dev::CTC0);
        im2.clear_req(Dev::CTC0);
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);
        im2.clear_status(Dev::CTC0);
        bool cleared = !im2.int_status(Dev::CTC0);
        check("ISC-03", set && cleared,
              "zxnext.vhd:1953 NR 0xC9 bit clears CTC0 int_status",
              fmt("set=%d cleared=%d", set, cleared));
    }

    // ISC-04 — zxnext.vhd:1952: NR 0xCA b6 clears UART1_TX int_status.
    {
        fresh(im2);
        im2.set_mode(true);
        raise_and_service(Dev::UART1_TX);
        im2.raise_req(Dev::UART1_TX);
        im2.tick(1);
        bool set = im2.int_status(Dev::UART1_TX);
        im2.clear_req(Dev::UART1_TX);
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);
        im2.clear_status(Dev::UART1_TX);
        bool cleared = !im2.int_status(Dev::UART1_TX);
        check("ISC-04", set && cleared,
              "zxnext.vhd:1952 NR 0xCA b6 clears UART1_TX int_status",
              fmt("set=%d cleared=%d", set, cleared));
    }

    // ISC-05 — zxnext.vhd:1952: NR 0xCA b2 clears UART0_TX int_status.
    {
        fresh(im2);
        im2.set_mode(true);
        raise_and_service(Dev::UART0_TX);
        im2.raise_req(Dev::UART0_TX);
        im2.tick(1);
        bool set = im2.int_status(Dev::UART0_TX);
        im2.clear_req(Dev::UART0_TX);
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);
        im2.clear_status(Dev::UART0_TX);
        bool cleared = !im2.int_status(Dev::UART0_TX);
        check("ISC-05", set && cleared,
              "zxnext.vhd:1952 NR 0xCA b2 clears UART0_TX int_status",
              fmt("set=%d cleared=%d", set, cleared));
    }

    // ISC-06 — zxnext.vhd:1954: NR 0xCA bits 5|4 clear UART1_RX int_status.
    {
        fresh(im2);
        im2.set_mode(true);
        raise_and_service(Dev::UART1_RX);
        im2.raise_req(Dev::UART1_RX);
        im2.tick(1);
        bool set = im2.int_status(Dev::UART1_RX);
        im2.clear_req(Dev::UART1_RX);
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);
        im2.clear_status(Dev::UART1_RX);
        bool cleared = !im2.int_status(Dev::UART1_RX);
        check("ISC-06", set && cleared,
              "zxnext.vhd:1954 NR 0xCA bits 5|4 clear UART1_RX int_status",
              fmt("set=%d cleared=%d", set, cleared));
    }

    // ISC-07 — zxnext.vhd:1954: NR 0xCA bits 1|0 clear UART0_RX int_status.
    {
        fresh(im2);
        im2.set_mode(true);
        raise_and_service(Dev::UART0_RX);
        im2.raise_req(Dev::UART0_RX);
        im2.tick(1);
        bool set = im2.int_status(Dev::UART0_RX);
        im2.clear_req(Dev::UART0_RX);
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0002, 0xED);
        im2.on_m1_cycle(0x0003, 0x4D);
        im2.tick(1);
        im2.clear_status(Dev::UART0_RX);
        bool cleared = !im2.int_status(Dev::UART0_RX);
        check("ISC-07", set && cleared,
              "zxnext.vhd:1954 NR 0xCA bits 1|0 clear UART0_RX int_status",
              fmt("set=%d cleared=%d", set, cleared));
    }

    // ISC-08 — im2_peripheral.vhd:160: int_status equation is
    //   int_status <= (int_req or int_unq) or (int_status and not clear)
    // So if a new int_req edge arrives in the SAME cycle as a clear, the
    // new edge wins (set dominates). In our model the wrapper step runs
    // the edge-detect after (and then) the clear was applied, so a
    // subsequent raise_req + tick re-sets int_status even if we just
    // cleared it.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        // Simulate pending-clear + new edge: clear, then in the SAME
        // perceived cycle raise a fresh edge and tick. The new edge must
        // re-set int_status.
        im2.clear_req(Dev::CTC0);
        im2.tick(1);  // advance so int_req_d settles
        im2.clear_status(Dev::CTC0);
        // clear_status() cleared int_status but im2_int_req latch is still
        // set until isr_serviced. To isolate, service fully.
        (void)im2.ack_vector();
        im2.tick(1);
        im2.on_m1_cycle(0x0000, 0xED);
        im2.on_m1_cycle(0x0001, 0x4D);
        im2.tick(1);  // latch cleared
        im2.clear_status(Dev::CTC0);
        bool starting_clean = !im2.int_status(Dev::CTC0);
        // Now simultaneous clear + new edge scenario.
        im2.raise_req(Dev::CTC0);
        im2.clear_status(Dev::CTC0);  // clear happens first
        im2.tick(1);  // step_devices sees the edge → sets int_status
        bool set_wins = im2.int_status(Dev::CTC0);
        check("ISC-08", starting_clean && set_wins,
              "im2_peripheral.vhd:160 new int_req edge re-sets int_status during clear",
              fmt("starting_clean=%d set_wins=%d", starting_clean, set_wins));
    }

    // ISC-09 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (legacy NR 0x20 read composes line_ula_00_ctc6..ctc3).
    // ISC-10 — RE-HOME to test/ctc_interrupts/ctc_interrupts_test.cpp
    // (legacy NR 0x22 read bit 7 = NOT pulse_int_n).
}

void section15_dma_int() {
    set_group("15. DMA Int");
    Im2Controller im2;

    // DMA-01 — peripherals.vhd:174-184 / zxnext.vhd:1994: im2_dma_int is
    // the OR-reduction of per-device o_dma_int (state /= S_0 AND
    // i_dma_int_en). Drive one device into S_REQ with dma_int_en set and
    // observe dma_int_pending() true.
    {
        fresh(im2);
        im2.set_mode(true);
        uint16_t mask = 1u << static_cast<int>(Dev::CTC0);
        im2.set_dma_int_en_mask(mask);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);  // CTC0 → S_REQ
        check("DMA-01", im2.dma_int_pending(),
              "peripherals.vhd:174-184 im2_dma_int OR-reduction across devices",
              fmt("dma_int_pending=%d", im2.dma_int_pending()));
    }

    // DMA-02 — zxnext.vhd:2001-2010: im2_dma_delay latches to 1 when
    // im2_dma_int is asserted. step_dma_delay() is called at end of tick.
    {
        fresh(im2);
        im2.set_mode(true);
        uint16_t mask = 1u << static_cast<int>(Dev::CTC0);
        im2.set_dma_int_en_mask(mask);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        check("DMA-02", im2.dma_delay(),
              "zxnext.vhd:2001-2010 im2_dma_delay latches on im2_dma_int",
              fmt("dma_delay=%d", im2.dma_delay()));
    }

    // DMA-03 — zxnext.vhd:2007: self-hold term is
    //   im2_dma_delay AND dma_delay
    // Where dma_delay comes from im2_control's reti_decode SRL window.
    // Once latched, the delay is held as long as the decoder is in one of
    // the RETI/SRL states.
    {
        fresh(im2);
        im2.set_mode(true);
        uint16_t mask = 1u << static_cast<int>(Dev::CTC0);
        im2.set_dma_int_en_mask(mask);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool latched = im2.dma_delay();
        // Remove the DMA int source but open the SRL window via RETI decode.
        im2.clear_req(Dev::CTC0);
        (void)im2.ack_vector();  // CTC0 → S_ACK (state != S_0 still)
        im2.tick(1);             // S_ACK → S_ISR
        im2.on_m1_cycle(0x0000, 0xED);  // open decode window
        im2.on_m1_cycle(0x0001, 0x4D);  // reti_seen in S_ED4D_T4
        im2.tick(1);  // im2_isr_serviced → CTC0 → S_0; but decoder is now
                      // in S_SRL_T1 (dma_delay_control=1) this tick, so the
                      // self-hold keeps dma_delay latched.
        check("DMA-03", latched && im2.dma_delay(),
              "zxnext.vhd:2007 im2_dma_delay self-holds during RETI SRL window",
              fmt("latched_initial=%d still_held=%d",
                  latched, im2.dma_delay()));
    }

    // DMA-04 — zxnext.vhd:2007 second OR term:
    //   im2_dma_delay <= ... OR (nmi_activated AND nr_cc_dma_int_en_0_7) ...
    // Un-skipped by TASK-NMI-SOURCE-PIPELINE-PLAN.md Wave E: Emulator pushes
    // NmiSource::is_activated() into Im2Controller::set_nmi_activated()
    // every tick, and NR 0xCC bit 7 into set_nr_cc_dma_int_en_0_7(). Drive
    // NmiSource to is_activated()=1 via the ExpBus pin (no consumer-feedback
    // dependency) and assert the NMI contribution to im2_dma_delay.
    {
        NmiSource nmi;
        nmi.reset();
        fresh(im2);
        im2.set_mode(true);

        // No CTC/other DMA source pending; isolate the NMI contribution.
        im2.set_nr_cc_dma_int_en_0_7(true);            // NR 0xCC bit 7 = 1
        const bool before = im2.dma_delay();

        nmi.set_expbus_nmi_n(false);                   // i_BUS_NMI_n='0'
        nmi.tick(1);                                   // latch nmi_expbus,
                                                       // is_activated()=1
        im2.set_nmi_activated(nmi.is_activated());
        im2.tick(1);                                   // step_dma_delay runs
        const bool after = im2.dma_delay();

        check("DMA-04", !before && nmi.is_activated() && after,
              "zxnext.vhd:2007 NMI-activated DMA delay (nmi_activated AND "
              "nr_cc_dma_int_en_0_7)",
              fmt("before=%d is_activated=%d after=%d",
                  before, nmi.is_activated(), after));
    }

    // DMA-05 — zxnext.vhd:2004-2005: reset clears im2_dma_delay.
    // reset() should clear the latch.
    {
        fresh(im2);
        im2.set_mode(true);
        uint16_t mask = 1u << static_cast<int>(Dev::CTC0);
        im2.set_dma_int_en_mask(mask);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool before = im2.dma_delay();
        im2.reset();
        bool after = im2.dma_delay();
        check("DMA-05", before && !after,
              "zxnext.vhd:2004-2005 reset clears im2_dma_delay latch",
              fmt("before=%d after=%d", before, after));
    }

    // DMA-06 — zxnext.vhd:1957-1958: compose_im2_dma_int_en is a 14-bit
    // mask; set_dma_int_en_mask() fans it out per-device. Flipping the
    // mask bit off for a device should remove that device from the
    // OR-reduction.
    {
        fresh(im2);
        im2.set_mode(true);
        // Put CTC0 in S_REQ but with mask OFF → no DMA pending.
        im2.set_dma_int_en_mask(0);
        im2.set_int_en(Dev::CTC0, true);
        im2.raise_req(Dev::CTC0);
        im2.tick(1);
        bool off = !im2.dma_int_pending();
        // Now enable mask bit → pending.
        uint16_t mask = 1u << static_cast<int>(Dev::CTC0);
        im2.set_dma_int_en_mask(mask);
        bool on = im2.dma_int_pending();
        check("DMA-06", off && on,
              "zxnext.vhd:1957-1958 per-device dma_int_en fan-out via mask",
              fmt("off=%d on=%d", off, on));
    }
}

void section16_unqualified() {
    set_group("16. Unqualified");
    Im2Controller im2;

    // UNQ-01 — zxnext.vhd:1946-1947: NR 0x20 b7 triggers an unqualified
    // LINE interrupt pulse. Bare-Im2 exercised via raise_unq(LINE).
    // int_unq bypasses int_en.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.raise_unq(Dev::LINE);  // no set_int_en call
        im2.tick(1);
        check("UNQ-01",
              im2.state(Dev::LINE) == DevState::S_REQ &&
              im2.int_status(Dev::LINE),
              "zxnext.vhd:1946-1947 NR 0x20 b7 unqualified LINE int (bypasses int_en)",
              fmt("state=%d status=%d",
                  static_cast<int>(im2.state(Dev::LINE)),
                  im2.int_status(Dev::LINE)));
    }

    // UNQ-02 — zxnext.vhd:1946-1947: NR 0x20 b3:0 trigger unqualified
    // CTC0..3 pulses. Verified here for CTC0.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.raise_unq(Dev::CTC0);
        im2.tick(1);
        check("UNQ-02",
              im2.state(Dev::CTC0) == DevState::S_REQ &&
              im2.int_status(Dev::CTC0),
              "zxnext.vhd:1946-1947 NR 0x20 b0 unqualified CTC0 (bypasses int_en)",
              fmt("state=%d status=%d",
                  static_cast<int>(im2.state(Dev::CTC0)),
                  im2.int_status(Dev::CTC0)));
    }

    // UNQ-03 — zxnext.vhd:1946-1947: NR 0x20 b6 triggers unqualified ULA
    // interrupt.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.raise_unq(Dev::ULA);
        im2.tick(1);
        check("UNQ-03",
              im2.state(Dev::ULA) == DevState::S_REQ &&
              im2.int_status(Dev::ULA),
              "zxnext.vhd:1946-1947 NR 0x20 b6 unqualified ULA (bypasses int_en)",
              fmt("state=%d status=%d",
                  static_cast<int>(im2.state(Dev::ULA)),
                  im2.int_status(Dev::ULA)));
    }

    // UNQ-04 — im2_peripheral.vhd:172: int_unq bypasses i_int_en. Without
    // int_en set, a raise_req alone must NOT latch im2_int_req (the
    // SM stays at S_0 because the wrapper only latches the im2_int_req
    // on the edge when int_en is high). raise_unq ignores int_en and
    // does latch im2_int_req, so the SM advances to S_REQ. Note that
    // int_status IS set on any edge per VHDL :160 (independent of
    // int_en), so we observe the latching via `state`, not int_status.
    {
        // Baseline: plain raise_req with int_en false → im2_int_req
        // stays low → SM stays in S_0.
        fresh(im2);
        im2.set_mode(true);
        im2.raise_req(Dev::CTC1);
        im2.tick(1);
        bool baseline_off = (im2.state(Dev::CTC1) == DevState::S_0);
        // Unqualified path: int_en false, but raise_unq bypasses it.
        fresh(im2);
        im2.set_mode(true);
        im2.raise_unq(Dev::CTC1);
        im2.tick(1);
        bool unq_on = (im2.state(Dev::CTC1) == DevState::S_REQ);
        check("UNQ-04", baseline_off && unq_on,
              "im2_peripheral.vhd:172 int_unq bypasses i_int_en",
              fmt("baseline_off=%d unq_on=%d", baseline_off, unq_on));
    }

    // UNQ-05 — im2_peripheral.vhd:160: int_unq also sets int_status.
    // Immediately after raise_unq (before tick), our model sets int_status
    // directly (combinational collapse). Still true after tick.
    {
        fresh(im2);
        im2.set_mode(true);
        im2.raise_unq(Dev::CTC2);
        bool status_immediate = im2.int_status(Dev::CTC2);
        im2.tick(1);
        bool status_after_tick = im2.int_status(Dev::CTC2);
        check("UNQ-05", status_immediate && status_after_tick,
              "im2_peripheral.vhd:160 int_unq feeds int_status register",
              fmt("immediate=%d after=%d", status_immediate, status_after_tick));
    }
}

void section17_joystick_iomode() {
    set_group("17. Joystick IO");
    // JOY-01 — (re-home) ctc_zc_to(3) → joy_iomode_pin7 toggle lives in the
    // Emulator/Input layer (Emulator::on_ctc_interrupt lambda + NR 0x0B
    // handler + joy_iomode_pin7_ field). The bare Ctc / Im2Controller
    // surface under test here cannot observe the pin7 field. Proper home
    // is test/input or a dedicated joy_iomode integration test — neither
    // the Phase 3 ctc_interrupts_test nor this file is in scope.
    // JOY-02 — (re-home) same as JOY-01: NR 0x0B joy_iomode_0 guard is
    // emulator-level. Deferred to the same future input/emulator test.
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════
// Main
// ══════════════════════════════════════════════════════════════════════

int main() {
    std::printf("CTC + Interrupt Controller Compliance Tests\n");
    std::printf("================================================\n\n");

    section1_state_machine();     std::printf("  Section 1  State Machine         done\n");
    section2_timer_mode();        std::printf("  Section 2  Timer Mode            done\n");
    section3_counter_mode();      std::printf("  Section 3  Counter Mode          done\n");
    section4_chaining();          std::printf("  Section 4  Chaining              done\n");
    section5_control_vector();    std::printf("  Section 5  Control/Vector        done\n");
    section6_nextreg_int_enable();std::printf("  Section 6  NR 0xC5 int_en        done\n");
    section7_im2_control();       std::printf("  Section 7  IM2 Control           done\n");
    section8_im2_device();        std::printf("  Section 8  IM2 Device            done\n");
    section9_daisy_chain();       std::printf("  Section 9  Daisy Chain           done\n");
    section10_pulse_mode();       std::printf("  Section 10 Pulse Mode            done\n");
    section11_im2_peripheral();   std::printf("  Section 11 IM2 Peripheral        done\n");
    section12_ula_line_int();     std::printf("  Section 12 ULA/Line Int          done (mostly re-homed)\n");
    section13_nextreg_int_regs(); std::printf("  Section 13 NR 0xC0-0xCE          done\n");
    section14_status_clear();     std::printf("  Section 14 Status/Clear          done\n");
    section15_dma_int();          std::printf("  Section 15 DMA Int               done\n");
    section16_unqualified();      std::printf("  Section 16 Unqualified           done\n");
    section17_joystick_iomode();  std::printf("  Section 17 Joystick IO           done (re-homed)\n");

    std::printf("\n================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-28s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-28s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows (NMI-blocked, review-later, or re-homed to other subsystems):\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-12s %s\n", s.id, s.reason);
        }
    }

    return g_fail > 0 ? 1 : 0;
}
