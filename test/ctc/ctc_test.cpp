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
// Surface: the jnext CTC C++ class (src/peripheral/ctc.{h,cpp}) exposes the
// 4-channel CTC as `Ctc` with per-channel write/read/tick/trigger and a
// single `on_interrupt` callback plus `set_int_enable(mask)`. There is no
// IM2 controller, no daisy-chain state machine, no pulse-mode fabric, no
// NextReg 0xC0..0xCE plumbing, no ULA/line interrupt path, no DMA integration,
// and no joystick-IO-mode wiring on the class under test. Plan rows in
// Sections 7-17 (and NR-02 in Section 6) are therefore unreachable and
// marked skip() with a one-line reason — they are not silently dropped.
//
// Run: ./build/test/ctc_test

#include "peripheral/ctc.h"

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

    // CTC-CW-11 — ctc_chan.vhd:250-256: iowr = i_iowr AND NOT iowr_d.
    // This is the rising-edge detection that prevents a held write signal
    // from issuing multiple writes. jnext write() is a discrete call so
    // the facility is not observable at this layer.
    skip("CTC-CW-11",
         "iowr rising-edge detect is not observable: jnext write() is a discrete API call");
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

    // CTC-NR-02 — NR 0xC5 read returns ctc_int_en[7:0]. jnext Ctc has no
    // readback accessor for the full int-enable byte (only per-channel
    // bool), and there is no NextReg 0xC5 handler in the subsystem under
    // test.
    skip("CTC-NR-02",
         "no Ctc::get_int_enable() accessor and no NR 0xC5 read path in scope");

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

    // CTC-NR-04 — zxnext.vhd constraint: nr_c5_we must not overlap
    // i_iowr because control_reg is a single-ported register updated by
    // whichever strobe is active. jnext has no cycle-accurate bus model
    // for this collision.
    skip("CTC-NR-04",
         "NR 0xC5 vs port write overlap is not modelled (no shared strobe)");
}

// ══════════════════════════════════════════════════════════════════════
// Sections 7-17: unreachable through the Ctc class under test
// ══════════════════════════════════════════════════════════════════════
//
// The jnext Ctc subsystem does not carry the IM2 interrupt fabric,
// the NextREG 0xC0..0xCE register file, the pulse-mode interrupt path,
// ULA/line interrupts, DMA interrupt integration, unqualified interrupts,
// or the joystick-IO-mode wiring. Every row in Sections 7-17 is marked
// skip() with a one-line reason.

void section7_im2_control() {
    set_group("7. IM2 Control (skipped)");
    skip("IM2C-01", "im2_control state machine not modelled in ctc subsystem (CPU-side IM2 only)");
    skip("IM2C-02", "o_reti_seen pulse not exposed; RETI detection lives in cpu/im2.cpp");
    skip("IM2C-03", "o_retn_seen pulse not exposed");
    skip("IM2C-04", "ED-prefix fall-through state not observable from Ctc API");
    skip("IM2C-05", "o_reti_decode window not exposed");
    skip("IM2C-06", "CB prefix handling outside CTC scope");
    skip("IM2C-07", "DD/FD prefix chain outside CTC scope");
    skip("IM2C-08", "o_dma_delay over RETI not exposed");
    skip("IM2C-09", "SRL delay states not exposed");
    skip("IM2C-10", "im_mode=00 (IM0) detection outside CTC scope");
    skip("IM2C-11", "im_mode=01 (IM1) detection outside CTC scope");
    skip("IM2C-12", "im_mode=10 (IM2) detection outside CTC scope");
    skip("IM2C-13", "falling-edge CLK_CPU update timing not observable");
    skip("IM2C-14", "im_mode reset default not observable from Ctc");
}

void section8_im2_device() {
    set_group("8. IM2 Device (skipped)");
    skip("IM2D-01", "im2_device state machine not implemented in ctc subsystem");
    skip("IM2D-02", "INT_n signal not produced by Ctc class");
    skip("IM2D-03", "IEI chain input not modelled");
    skip("IM2D-04", "IM2 mode select not routed through Ctc");
    skip("IM2D-05", "Z80 interrupt acknowledge cycle not driven by Ctc");
    skip("IM2D-06", "S_ACK→S_ISR transition not observable");
    skip("IM2D-07", "S_ISR→S_0 on RETI not observable");
    skip("IM2D-08", "S_ISR hold without RETI not observable");
    skip("IM2D-09", "o_vec during ACK not exposed");
    skip("IM2D-10", "vector=0 outside ACK not exposed");
    skip("IM2D-11", "o_isr_serviced pulse not exposed");
    skip("IM2D-12", "o_dma_int not exposed on Ctc");
}

void section9_daisy_chain() {
    set_group("9. Daisy Chain (skipped)");
    skip("IM2P-01", "IEI/IEO chain not modelled in Ctc");
    skip("IM2P-02", "IEO during reti_decode not observable");
    skip("IM2P-03", "IEO=0 blocking not observable");
    skip("IM2P-04", "peripheral index 0 IEI wiring not exposed");
    skip("IM2P-05", "simultaneous-request priority resolution not modelled");
    skip("IM2P-06", "queued lower-priority device not modelled");
    skip("IM2P-07", "post-RETI chain restore not modelled");
    skip("IM2P-08", "3-way chain not modelled");
    skip("IM2P-09", "AND-reduction of INT_n not modelled");
    skip("IM2P-10", "OR-reduction of vectors not modelled");
}

void section10_pulse_mode() {
    set_group("10. Pulse Mode (skipped)");
    skip("PULSE-01", "pulse_en path not present in Ctc (no nr_c0 or int_unq in scope)");
    skip("PULSE-02", "IM2-mode pulse suppression not modelled");
    skip("PULSE-03", "ULA EXCEPTION pulse path outside CTC scope");
    skip("PULSE-04", "pulse_int_n waveform not exposed");
    skip("PULSE-05", "48K/+3 pulse duration not modelled in Ctc");
    skip("PULSE-06", "128K/Pentagon pulse duration not modelled in Ctc");
    skip("PULSE-07", "pulse counter reset not exposed");
    skip("PULSE-08", "INT_n = pulse_int_n AND im2_int_n not composed in Ctc");
    skip("PULSE-09", "o_BUS_INT_n not exposed");
}

void section11_im2_peripheral() {
    set_group("11. IM2 Peripheral (skipped)");
    skip("IM2W-01", "int_req edge detect not exposed (no int_req_d accessor)");
    skip("IM2W-02", "im2_int_req latch not exposed");
    skip("IM2W-03", "im2_isr_serviced clear not exposed");
    skip("IM2W-04", "int_status set-path not exposed");
    skip("IM2W-05", "int_status_clear NR path not in Ctc");
    skip("IM2W-06", "o_int_status composite not exposed");
    skip("IM2W-07", "im2_reset_n composite not exposed");
    skip("IM2W-08", "int_unq bypass not modelled");
    skip("IM2W-09", "isr_serviced cross-domain edge detect not observable");
}

void section12_ula_line_int() {
    set_group("12. ULA/Line Int (skipped)");
    skip("ULA-INT-01", "ULA HC/VC interrupt not in Ctc subsystem");
    skip("ULA-INT-02", "port 0xFF interrupt disable not in Ctc subsystem");
    skip("ULA-INT-03", "ula_int_en wiring not in Ctc subsystem");
    skip("ULA-INT-04", "line interrupt at cvc match not in Ctc subsystem");
    skip("ULA-INT-05", "NR 0x22 line_interrupt_en not in Ctc subsystem");
    skip("ULA-INT-06", "line 0 → c_max_vc wrap not in Ctc subsystem");
    skip("ULA-INT-07", "im2 priority index 11 not modelled");
    skip("ULA-INT-08", "im2 priority index 0 not modelled");
    skip("ULA-INT-09", "ULA EXCEPTION='1' instantiation not in Ctc");
}

void section13_nextreg_int_regs() {
    set_group("13. NR 0xC0-0xCE (skipped)");
    skip("NR-C0-01", "NR 0xC0 im2_vector MSBs not in Ctc scope");
    skip("NR-C0-02", "NR 0xC0 stackless_nmi not in Ctc scope");
    skip("NR-C0-03", "NR 0xC0 pulse/IM2 mode bit not in Ctc scope");
    skip("NR-C0-04", "NR 0xC0 read format not exposed");
    skip("NR-C4-01", "NR 0xC4 expbus int enable not in Ctc scope");
    skip("NR-C4-02", "NR 0xC4 line_interrupt_en not in Ctc scope");
    skip("NR-C4-03", "NR 0xC4 readback not exposed");
    skip("NR-C5-01", "duplicate of CTC-NR-01, covered in Section 6");
    skip("NR-C5-02", "duplicate of CTC-NR-02, no readback accessor");
    skip("NR-C6-01", "NR 0xC6 UART int_en not in Ctc scope");
    skip("NR-C6-02", "NR 0xC6 read not in Ctc scope");
    skip("NR-C8-01", "NR 0xC8 line/ULA status not in Ctc scope");
    skip("NR-C9-01", "NR 0xC9 CTC int status not exposed on Ctc class");
    skip("NR-CA-01", "NR 0xCA UART status not in Ctc scope");
    skip("NR-CC-01", "NR 0xCC DMA int_en group 0 not in Ctc scope");
    skip("NR-CD-01", "NR 0xCD DMA int_en group 1 not in Ctc scope");
    skip("NR-CE-01", "NR 0xCE DMA int_en group 2 not in Ctc scope");
}

void section14_status_clear() {
    set_group("14. Status/Clear (skipped)");
    skip("ISC-01", "NR 0xC8 line status clear not in Ctc scope");
    skip("ISC-02", "NR 0xC8 ULA status clear not in Ctc scope");
    skip("ISC-03", "NR 0xC9 CTC status clear not exposed on Ctc");
    skip("ISC-04", "NR 0xCA UART1 TX clear not in Ctc scope");
    skip("ISC-05", "NR 0xCA UART0 TX clear not in Ctc scope");
    skip("ISC-06", "NR 0xCA UART1 RX clear not in Ctc scope");
    skip("ISC-07", "NR 0xCA UART0 RX clear not in Ctc scope");
    skip("ISC-08", "status re-set under pending clear not modelled");
    skip("ISC-09", "legacy NR 0x20 read not in Ctc scope");
    skip("ISC-10", "legacy NR 0x22 read not in Ctc scope");
}

void section15_dma_int() {
    set_group("15. DMA Int (skipped)");
    skip("DMA-01", "im2_dma_int OR-reduction not in Ctc scope");
    skip("DMA-02", "im2_dma_delay latch not in Ctc scope");
    skip("DMA-03", "dma_delay hold not in Ctc scope");
    skip("DMA-04", "NMI-driven DMA delay not in Ctc scope");
    skip("DMA-05", "DMA delay reset not exposed");
    skip("DMA-06", "per-peripheral DMA int enables via NR 0xCC-CE not in Ctc scope");
}

void section16_unqualified() {
    set_group("16. Unqualified (skipped)");
    skip("UNQ-01", "NR 0x20 unqualified line interrupt not in Ctc scope");
    skip("UNQ-02", "NR 0x20 unqualified CTC bits not wired to Ctc class");
    skip("UNQ-03", "NR 0x20 unqualified ULA interrupt not in Ctc scope");
    skip("UNQ-04", "int_unq bypass-int_en path not modelled on Ctc");
    skip("UNQ-05", "int_unq sets int_status not observable");
}

void section17_joystick_iomode() {
    set_group("17. Joystick IO (skipped)");
    skip("JOY-01", "ctc_zc_to(3) → joy_iomode_pin7 toggle not wired through Ctc class");
    skip("JOY-02", "NR 0x0B joy_iomode_0 guard condition not in Ctc scope");
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
    section7_im2_control();       std::printf("  Section 7  IM2 Control           done (all skip)\n");
    section8_im2_device();        std::printf("  Section 8  IM2 Device            done (all skip)\n");
    section9_daisy_chain();       std::printf("  Section 9  Daisy Chain           done (all skip)\n");
    section10_pulse_mode();       std::printf("  Section 10 Pulse Mode            done (all skip)\n");
    section11_im2_peripheral();   std::printf("  Section 11 IM2 Peripheral        done (all skip)\n");
    section12_ula_line_int();     std::printf("  Section 12 ULA/Line Int          done (all skip)\n");
    section13_nextreg_int_regs(); std::printf("  Section 13 NR 0xC0-0xCE          done (all skip)\n");
    section14_status_clear();     std::printf("  Section 14 Status/Clear          done (all skip)\n");
    section15_dma_int();          std::printf("  Section 15 DMA Int               done (all skip)\n");
    section16_unqualified();      std::printf("  Section 16 Unqualified           done (all skip)\n");
    section17_joystick_iomode();  std::printf("  Section 17 Joystick IO           done (all skip)\n");

    std::printf("\n================================================\n");
    std::printf("Total: %d  Passed: %d  Failed: %d  Skipped: %zu\n",
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
        std::printf("\nSkipped plan rows (unrealisable with current Ctc C++ API):\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-12s %s\n", s.id, s.reason);
        }
    }

    return g_fail > 0 ? 1 : 0;
}
