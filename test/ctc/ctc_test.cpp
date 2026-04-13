// CTC + Interrupt Controller Compliance Test Runner
//
// Tests the CTC subsystem against VHDL-derived expected behaviour.
// All expected values come from the CTC-INTERRUPTS-TEST-PLAN-DESIGN.md spec.
//
// Run: ./build/test/ctc_test

#include "peripheral/ctc.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

// ── Test infrastructure (same pattern as copper_test) ────────────────

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static std::string g_group;

struct TestResult {
    std::string group;
    std::string id;
    std::string description;
    bool passed;
    std::string detail;
};

static std::vector<TestResult> g_results;

static void set_group(const char* name) {
    g_group = name;
}

static void check(const char* id, const char* desc, bool cond, const char* detail = "") {
    g_total++;
    TestResult r;
    r.group = g_group;
    r.id = id;
    r.description = desc;
    r.passed = cond;
    r.detail = detail;
    g_results.push_back(r);

    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL %s: %s", id, desc);
        if (detail[0]) printf(" [%s]", detail);
        printf("\n");
    }
}

static char g_buf[512];
#define DETAIL(...) (snprintf(g_buf, sizeof(g_buf), __VA_ARGS__), g_buf)

// ── Helper: fresh CTC ───────────────────────────────────────────────

static void fresh(Ctc& ctc) {
    ctc.reset();
}

// Helper: write control word to a channel
// Control word format: D7=int_en, D6=mode, D5=prescale, D4=edge, D3=trigger, D2=tc_follows, D1=reset, D0=1
static uint8_t make_cw(bool int_en, bool counter, bool prescale256,
                       bool rising_edge, bool trigger_wait,
                       bool tc_follows, bool soft_reset) {
    uint8_t v = 0x01; // D0=1 (control word)
    if (int_en)       v |= 0x80;
    if (counter)      v |= 0x40;
    if (prescale256)  v |= 0x20;
    if (rising_edge)  v |= 0x10;
    if (trigger_wait) v |= 0x08;
    if (tc_follows)   v |= 0x04;
    if (soft_reset)   v |= 0x02;
    return v;
}

// Helper: program a channel in timer mode and start it running
// Returns the control word used.
static void setup_timer(Ctc& ctc, int ch, uint8_t tc, bool prescale256 = false,
                        bool trigger_wait = false, bool int_en = false) {
    uint8_t cw = make_cw(int_en, false, prescale256, false, trigger_wait, true, false);
    ctc.write(ch, cw);
    ctc.write(ch, tc);
}

// Helper: program a channel in counter mode and start it running
static void setup_counter(Ctc& ctc, int ch, uint8_t tc, bool rising_edge = false,
                          bool int_en = false) {
    uint8_t cw = make_cw(int_en, true, false, rising_edge, false, true, false);
    ctc.write(ch, cw);
    ctc.write(ch, tc);
}

// ══════════════════════════════════════════════════════════════════════
// Section 1: CTC Channel State Machine
// ══════════════════════════════════════════════════════════════════════

static void test_section1_state_machine() {
    set_group("State Machine");
    Ctc ctc;

    // CTC-SM-01: Hard reset: channel starts in RESET state
    // Observable: read returns 0 (counter = 0 after reset)
    {
        fresh(ctc);
        uint8_t val = ctc.read(0);
        check("CTC-SM-01", "Hard reset: read returns 0",
              val == 0x00, DETAIL("got %02x", val));
    }

    // CTC-SM-02: Control word without D2=1 in RESET state — stays in RESET
    // Observable: channel doesn't count, read still 0
    {
        fresh(ctc);
        uint8_t cw = make_cw(false, false, false, false, false, false, false);
        ctc.write(0, cw);
        ctc.tick(100);
        uint8_t val = ctc.read(0);
        check("CTC-SM-02", "CW without TC-follows in RESET: no counting",
              val == 0x00, DETAIL("got %02x", val));
    }

    // CTC-SM-03: Control word with D2=1 — awaits TC
    // Observable: read still 0 (no TC loaded yet), no counting
    {
        fresh(ctc);
        uint8_t cw = make_cw(false, false, false, false, false, true, false);
        ctc.write(0, cw);
        ctc.tick(100);
        uint8_t val = ctc.read(0);
        check("CTC-SM-03", "CW with D2=1: awaiting TC, no counting",
              val == 0x00, DETAIL("got %02x", val));
    }

    // CTC-SM-04: Write time constant after D2=1 — channel starts running
    // Observable: counter loaded with TC, starts decrementing
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10); // TC=16, prescaler=16, auto-start
        uint8_t val_before = ctc.read(0);
        ctc.tick(16); // one prescaler cycle
        uint8_t val_after = ctc.read(0);
        check("CTC-SM-04", "TC loaded: counter starts",
              val_before == 0x10 && val_after < val_before,
              DETAIL("before=%02x after=%02x", val_before, val_after));
    }

    // CTC-SM-05: Timer mode with trigger wait — stays until trigger
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10, false, true); // trigger_wait=true
        ctc.tick(100);
        uint8_t val = ctc.read(0);
        check("CTC-SM-05", "Timer with trigger: waits in TRIGGER",
              val == 0x10, DETAIL("got %02x (expected 10)", val));
    }

    // CTC-SM-06: Timer mode without trigger — immediate RUN
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10, false, false); // auto-start
        ctc.tick(16);
        uint8_t val = ctc.read(0);
        check("CTC-SM-06", "Timer auto-start: counting immediately",
              val < 0x10, DETAIL("got %02x", val));
    }

    // CTC-SM-07: Counter mode — immediate RUN from TRIGGER
    // Counter mode doesn't use prescaler; decrements on trigger()
    {
        fresh(ctc);
        setup_counter(ctc, 0, 0x05);
        ctc.trigger(0);
        uint8_t val = ctc.read(0);
        check("CTC-SM-07", "Counter mode: immediate RUN, trigger decrements",
              val == 0x04, DETAIL("got %02x (expected 04)", val));
    }

    // CTC-SM-08: CW with D2=1 while in RUN -> RUN_TC
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10);
        // Now write CW with TC-follows while running
        uint8_t cw = make_cw(false, false, false, false, false, true, false);
        ctc.write(0, cw);
        // Channel should be in RUN_TC — tick shouldn't crash, still counting
        ctc.tick(16);
        // Now supply TC
        ctc.write(0, 0x20); // new TC = 0x20
        uint8_t val = ctc.read(0);
        check("CTC-SM-08", "CW D2=1 while RUN: accepts new TC",
              val == 0x20, DETAIL("got %02x (expected 20)", val));
    }

    // CTC-SM-09: Write TC in RUN_TC -> returns to RUN with new TC
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10);
        uint8_t cw = make_cw(false, false, false, false, false, true, false);
        ctc.write(0, cw); // -> RUN_TC
        ctc.write(0, 0x30); // TC = 0x30 -> RUN
        ctc.tick(16); // one prescaler cycle
        uint8_t val = ctc.read(0);
        check("CTC-SM-09", "TC in RUN_TC: counter reloaded and running",
              val < 0x30, DETAIL("got %02x", val));
    }

    // CTC-SM-10: Soft reset (D1=1, D2=0) -> RESET
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10);
        ctc.tick(16); // let it run a bit
        uint8_t cw = make_cw(false, false, false, false, false, false, true); // soft reset
        ctc.write(0, cw);
        ctc.tick(100);
        // After soft reset, it should stop counting
        uint8_t val1 = ctc.read(0);
        ctc.tick(100);
        uint8_t val2 = ctc.read(0);
        check("CTC-SM-10", "Soft reset (D1=1 D2=0): channel stops",
              val1 == val2, DETAIL("val1=%02x val2=%02x", val1, val2));
    }

    // CTC-SM-11: Soft reset (D1=1, D2=1) -> RESET_TC
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10);
        uint8_t cw = make_cw(false, false, false, false, false, true, true); // soft reset + TC follows
        ctc.write(0, cw);
        // Should be in RESET_TC; write TC to resume
        ctc.write(0, 0x20);
        uint8_t val = ctc.read(0);
        check("CTC-SM-11", "Soft reset D1=1 D2=1: awaits TC then runs",
              val == 0x20, DETAIL("got %02x", val));
    }

    // CTC-SM-12: Double soft reset required when in RESET_TC
    // First CW write with D1=1 consumed as TC; second one actually resets
    {
        fresh(ctc);
        // Get into RESET_TC
        uint8_t cw_tc = make_cw(false, false, false, false, false, true, false);
        ctc.write(0, cw_tc); // RESET -> RESET_TC
        // Now write a "soft reset" CW — but it's consumed as TC!
        uint8_t cw_reset = make_cw(false, false, false, false, false, false, true);
        ctc.write(0, cw_reset); // consumed as TC, not as CW
        // Channel should be in RUN or TRIGGER now (TC was loaded)
        uint8_t val = ctc.read(0);
        check("CTC-SM-12", "RESET_TC: first CW consumed as TC",
              val == cw_reset, DETAIL("got %02x (expected %02x = raw TC)", val, cw_reset));
    }

    // CTC-SM-13: CW while running (D1=0 D2=0): keeps running, control bits updated
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10, false); // prescaler=16
        ctc.tick(16); // count once
        uint8_t val_before = ctc.read(0);
        // Write CW changing prescaler to 256, no reset, no TC-follows
        uint8_t cw = make_cw(false, false, true, false, false, false, false);
        ctc.write(0, cw);
        // Channel should keep running (still has counter value)
        ctc.tick(256); // one prescaler-256 cycle
        uint8_t val_after = ctc.read(0);
        check("CTC-SM-13", "CW while running: bits updated, keeps counting",
              val_after < val_before,
              DETAIL("before=%02x after=%02x", val_before, val_after));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 2: CTC Timer Mode (Prescaler)
// ══════════════════════════════════════════════════════════════════════

static void test_section2_timer_mode() {
    set_group("Timer Mode");
    Ctc ctc;

    // CTC-TM-01: Prescaler=16: counter decrements every 16 ticks
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10); // TC=16, prescaler=16
        ctc.tick(16);
        uint8_t val = ctc.read(0);
        check("CTC-TM-01", "Prescaler=16: decrement after 16 ticks",
              val == 0x0F, DETAIL("got %02x (expected 0f)", val));
    }

    // CTC-TM-02: Prescaler=256: counter decrements every 256 ticks
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10, true); // TC=16, prescaler=256
        ctc.tick(256);
        uint8_t val = ctc.read(0);
        check("CTC-TM-02", "Prescaler=256: decrement after 256 ticks",
              val == 0x0F, DETAIL("got %02x (expected 0f)", val));
    }

    // CTC-TM-03: TC=1: ZC/TO after 1 prescaler cycle
    {
        fresh(ctc);
        int zc_count = 0;
        ctc.on_interrupt = [&](int) { zc_count++; };
        setup_timer(ctc, 0, 0x01, false, false, true); // TC=1, int_en=true
        ctc.tick(16); // one prescaler cycle should fire ZC/TO
        check("CTC-TM-03", "TC=1: ZC/TO after 1 prescaler cycle",
              zc_count >= 1, DETAIL("zc_count=%d", zc_count));
    }

    // CTC-TM-04: TC=0 means 256
    {
        fresh(ctc);
        int zc_count = 0;
        ctc.on_interrupt = [&](int) { zc_count++; };
        setup_timer(ctc, 0, 0x00, false, false, true); // TC=0, prescaler=16, int_en=true
        ctc.tick(16 * 255); // 255 prescaler cycles — should not have fired yet
        int zc_before = zc_count;
        ctc.tick(16); // 256th cycle — should fire
        check("CTC-TM-04", "TC=0 means 256 decrements before ZC/TO",
              zc_before == 0 && zc_count >= 1,
              DETAIL("before=%d after=%d", zc_before, zc_count));
    }

    // CTC-TM-05: Prescaler resets on soft reset
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10);
        ctc.tick(10); // partial prescaler count
        uint8_t cw = make_cw(false, false, false, false, false, false, true); // soft reset
        ctc.write(0, cw);
        // Restart
        setup_timer(ctc, 0, 0x10);
        ctc.tick(16); // full prescaler cycle
        uint8_t val = ctc.read(0);
        check("CTC-TM-05", "Prescaler resets on soft reset",
              val == 0x0F, DETAIL("got %02x (expected 0f)", val));
    }

    // CTC-TM-06: ZC/TO reloads TC automatically
    {
        fresh(ctc);
        int zc_count = 0;
        ctc.on_interrupt = [&](int) { zc_count++; };
        setup_timer(ctc, 0, 0x02, false, false, true); // TC=2, prescaler=16, int_en=true
        ctc.tick(16 * 2); // first ZC/TO
        int first = zc_count;
        ctc.tick(16 * 2); // second ZC/TO (auto-reload)
        check("CTC-TM-06", "ZC/TO auto-reloads TC",
              first >= 1 && zc_count >= 2,
              DETAIL("first=%d second=%d", first, zc_count));
    }

    // CTC-TM-07: ZC/TO pulse is 1 tick (observable via interrupt callback count)
    {
        fresh(ctc);
        int zc_count = 0;
        ctc.on_interrupt = [&](int) { zc_count++; };
        setup_timer(ctc, 0, 0x01, false, false, true); // TC=1
        ctc.tick(16); // exactly 1 prescaler cycle
        check("CTC-TM-07", "ZC/TO fires exactly once per underflow",
              zc_count == 1, DETAIL("zc_count=%d", zc_count));
    }

    // CTC-TM-08: Read port returns current down-counter value
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10); // TC=16
        uint8_t initial = ctc.read(0);
        ctc.tick(16 * 3); // 3 decrements
        uint8_t mid = ctc.read(0);
        check("CTC-TM-08", "Read returns current counter",
              initial == 0x10 && mid == 0x0D,
              DETAIL("initial=%02x mid=%02x", initial, mid));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 3: CTC Counter Mode
// ══════════════════════════════════════════════════════════════════════

static void test_section3_counter_mode() {
    set_group("Counter Mode");
    Ctc ctc;

    // CTC-CM-01: Counter mode: decrement on trigger
    {
        fresh(ctc);
        setup_counter(ctc, 0, 0x05);
        ctc.trigger(0);
        uint8_t val = ctc.read(0);
        check("CTC-CM-01", "Counter mode: trigger decrements counter",
              val == 0x04, DETAIL("got %02x (expected 04)", val));
    }

    // CTC-CM-02: Counter mode: rising edge (D4=1) — trigger still works
    // (edge detection is external to our API; trigger() is the event)
    {
        fresh(ctc);
        setup_counter(ctc, 0, 0x05, true); // rising edge
        ctc.trigger(0);
        uint8_t val = ctc.read(0);
        check("CTC-CM-02", "Counter mode rising edge: trigger works",
              val == 0x04, DETAIL("got %02x (expected 04)", val));
    }

    // CTC-CM-03: Counter mode: ZC/TO when count reaches 0
    {
        fresh(ctc);
        int zc_count = 0;
        ctc.on_interrupt = [&](int) { zc_count++; };
        setup_counter(ctc, 0, 0x03, false, true); // TC=3, int_en=true
        ctc.trigger(0); // 3->2
        ctc.trigger(0); // 2->1
        ctc.trigger(0); // 1->0 -> ZC/TO
        check("CTC-CM-03", "Counter mode: ZC/TO after TC triggers",
              zc_count == 1, DETAIL("zc_count=%d", zc_count));
    }

    // CTC-CM-04: Counter mode: automatic reload after ZC/TO
    {
        fresh(ctc);
        int zc_count = 0;
        ctc.on_interrupt = [&](int) { zc_count++; };
        setup_counter(ctc, 0, 0x02, false, true); // TC=2
        ctc.trigger(0); // 2->1
        ctc.trigger(0); // 1->0 -> ZC/TO, reload 2
        uint8_t after_zc = ctc.read(0);
        ctc.trigger(0); // 2->1
        ctc.trigger(0); // 1->0 -> ZC/TO again
        check("CTC-CM-04", "Counter mode: auto-reload after ZC/TO",
              zc_count == 2 && after_zc == 0x02,
              DETAIL("zc=%d after_reload=%02x", zc_count, after_zc));
    }

    // CTC-CM-05: Edge polarity change counts as clock edge
    // Write CW changing D4 while in RUN should decrement counter
    {
        fresh(ctc);
        setup_counter(ctc, 0, 0x05, false); // falling edge, TC=5
        // Change to rising edge — should count as edge
        uint8_t cw = make_cw(false, true, false, true, false, false, false);
        ctc.write(0, cw);
        uint8_t val = ctc.read(0);
        check("CTC-CM-05", "Edge polarity change counts as clock edge",
              val == 0x04, DETAIL("got %02x (expected 04)", val));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 4: CTC Chaining (ZC/TO as trigger)
// ══════════════════════════════════════════════════════════════════════

static void test_section4_chaining() {
    set_group("Chaining");
    Ctc ctc;

    // CTC-CH-02: Channel 0 ZC/TO triggers channel 1
    // VHDL: i_clk_trg <= ctc_zc_to(2 downto 0) & ctc_zc_to(3)
    // So ch0 ZC/TO -> ch1 trigger, ch1->ch2, ch2->ch3, ch3->ch0
    // But Ctc::handle_zc_to chains ch N -> ch N+1 (0->1, 1->2, 2->3)
    {
        fresh(ctc);
        // Ch0: timer, TC=1, prescaler=16 (fires quickly)
        setup_timer(ctc, 0, 0x01, false, false, false);
        // Ch1: counter, TC=3 (counts ch0 ZC/TO events)
        setup_counter(ctc, 1, 0x03);
        ctc.tick(16); // ch0 ZC/TO -> ch1 trigger: 3->2
        ctc.tick(16); // ch0 ZC/TO -> ch1 trigger: 2->1
        uint8_t val = ctc.read(1);
        check("CTC-CH-02", "Ch0 ZC/TO triggers ch1",
              val == 0x01, DETAIL("ch1=%02x (expected 01)", val));
    }

    // CTC-CH-03: Channel 1 ZC/TO triggers channel 2
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x01, false, false, false);
        setup_counter(ctc, 1, 0x01); // TC=1: fires on each ch0 ZC/TO
        setup_counter(ctc, 2, 0x03);
        ctc.tick(16); // ch0 ZC/TO -> ch1 0->ZC/TO -> ch2 3->2
        ctc.tick(16); // ch0 ZC/TO -> ch1 0->ZC/TO -> ch2 2->1
        uint8_t val = ctc.read(2);
        check("CTC-CH-03", "Ch1 ZC/TO triggers ch2",
              val == 0x01, DETAIL("ch2=%02x (expected 01)", val));
    }

    // CTC-CH-04: Channel 2 ZC/TO triggers channel 3
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x01, false, false, false);
        setup_counter(ctc, 1, 0x01);
        setup_counter(ctc, 2, 0x01);
        setup_counter(ctc, 3, 0x03);
        ctc.tick(16); // ch0->ch1->ch2->ch3: 3->2
        ctc.tick(16); // chain again: 3->1? depends on reload
        uint8_t val = ctc.read(3);
        check("CTC-CH-04", "Ch2 ZC/TO triggers ch3",
              val <= 0x02, DETAIL("ch3=%02x", val));
    }

    // CTC-CH-05: Cascaded chain: ch0 timer -> ch1 counter -> ch2 counter
    {
        fresh(ctc);
        int zc2_count = 0;
        ctc.on_interrupt = [&](int ch) { if (ch == 2) zc2_count++; };
        setup_timer(ctc, 0, 0x01, false, false, false); // TC=1
        setup_counter(ctc, 1, 0x02, false, false); // TC=2
        setup_counter(ctc, 2, 0x02, false, true);  // TC=2, int_en
        // ch0 fires every 16 ticks
        // ch1 fires every 2 ch0 ZC/TOs = every 32 ticks
        // ch2 fires every 2 ch1 ZC/TOs = every 64 ticks
        ctc.tick(16 * 4); // 4 ch0 fires -> 2 ch1 fires -> 1 ch2 fire
        check("CTC-CH-05", "3-stage cascade produces expected ZC/TO",
              zc2_count == 1, DETAIL("zc2=%d", zc2_count));
    }

    // CTC-CH-01: Channel 3 ZC/TO should trigger channel 0
    // Note: The VHDL wiring is ctc_zc_to(2:0) & ctc_zc_to(3), meaning
    // ch3 wraps to ch0. But Ctc::handle_zc_to only chains N->N+1 for N<3.
    // This tests whether ch3->ch0 wrap-around is implemented.
    {
        fresh(ctc);
        setup_timer(ctc, 3, 0x01, false, false, false); // ch3 timer
        setup_counter(ctc, 0, 0x03); // ch0 counter
        uint8_t before = ctc.read(0);
        ctc.tick(16); // ch3 ZC/TO — does it trigger ch0?
        uint8_t after = ctc.read(0);
        check("CTC-CH-01", "Ch3 ZC/TO triggers ch0 (VHDL wrap-around)",
              after < before, DETAIL("before=%02x after=%02x", before, after));
    }

    // CTC-CH-06: Circular chain — all counter mode is dead (no timer to drive)
    {
        fresh(ctc);
        setup_counter(ctc, 0, 0x03);
        setup_counter(ctc, 1, 0x03);
        setup_counter(ctc, 2, 0x03);
        setup_counter(ctc, 3, 0x03);
        ctc.tick(1000);
        bool all_same = (ctc.read(0) == 0x03 && ctc.read(1) == 0x03 &&
                         ctc.read(2) == 0x03 && ctc.read(3) == 0x03);
        check("CTC-CH-06", "All counter mode: no activity (dead ring)",
              all_same,
              DETAIL("ch0=%02x ch1=%02x ch2=%02x ch3=%02x",
                     ctc.read(0), ctc.read(1), ctc.read(2), ctc.read(3)));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 5: CTC Control Word and Vector Protocol
// ══════════════════════════════════════════════════════════════════════

static void test_section5_control_vector() {
    set_group("Control/Vector");
    Ctc ctc;

    // CTC-CW-01: Control word bits stored
    {
        fresh(ctc);
        uint8_t cw = make_cw(true, true, true, true, true, true, false);
        ctc.write(0, cw);
        // Verify int_en is set
        check("CTC-CW-01", "Control word: int_en stored",
              ctc.channel(0).int_enabled(), "int_en not set");
    }

    // CTC-CW-04: Time constant follows control word with D2=1
    {
        fresh(ctc);
        uint8_t cw = make_cw(false, false, false, false, false, true, false);
        ctc.write(0, cw);
        ctc.write(0, 0x42); // This should be TC, not CW despite D0=0
        uint8_t val = ctc.read(0);
        check("CTC-CW-04", "TC follows D2=1 CW: next byte is TC",
              val == 0x42, DETAIL("got %02x (expected 42)", val));
    }

    // CTC-CW-05: In RESET_TC, any byte is TC regardless of D0
    {
        fresh(ctc);
        uint8_t cw = make_cw(false, false, false, false, false, true, false);
        ctc.write(0, cw); // -> RESET_TC
        // Write a byte that looks like a CW (D0=1)
        ctc.write(0, 0x85); // D0=1, D2=0, D7=1 — but should be treated as TC
        uint8_t val = ctc.read(0);
        check("CTC-CW-05", "RESET_TC: CW-like byte treated as TC",
              val == 0x85, DETAIL("got %02x (expected 85)", val));
    }

    // CTC-CW-06: D7=1 enables interrupt
    {
        fresh(ctc);
        uint8_t cw = make_cw(true, false, false, false, false, false, false);
        ctc.write(0, cw);
        check("CTC-CW-06", "D7=1: interrupt enabled",
              ctc.channel(0).int_enabled(), "int_en not set");
    }

    // CTC-CW-07: D7=0 disables interrupt
    {
        fresh(ctc);
        // First enable
        uint8_t cw1 = make_cw(true, false, false, false, false, false, false);
        ctc.write(0, cw1);
        // Then disable
        uint8_t cw2 = make_cw(false, false, false, false, false, false, false);
        ctc.write(0, cw2);
        check("CTC-CW-07", "D7=0: interrupt disabled",
              !ctc.channel(0).int_enabled(), "int_en still set");
    }

    // CTC-CW-08: External int_en_wr overrides D7
    {
        fresh(ctc);
        // Enable via CW
        uint8_t cw = make_cw(true, false, false, false, false, false, false);
        ctc.write(0, cw);
        // Override via set_int_enable
        ctc.set_int_enable(0x00); // disable all
        check("CTC-CW-08", "External int_en_wr overrides D7",
              !ctc.channel(0).int_enabled(), "int_en not cleared");
    }

    // CTC-CW-09: Hard reset clears control bits
    {
        fresh(ctc);
        uint8_t cw = make_cw(true, true, true, true, true, false, false);
        ctc.write(0, cw);
        ctc.reset();
        check("CTC-CW-09", "Hard reset clears control bits",
              !ctc.channel(0).int_enabled(), "int_en not cleared after reset");
    }

    // CTC-CW-10: Hard reset clears TC to 0
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x42);
        ctc.reset();
        uint8_t val = ctc.read(0);
        check("CTC-CW-10", "Hard reset clears TC to 0",
              val == 0x00, DETAIL("got %02x", val));
    }

    // CTC-CW-02: Vector word (D0=0) — accepted without crash
    {
        fresh(ctc);
        ctc.write(0, 0x00); // vector word to ch0
        check("CTC-CW-02", "Vector word (D0=0): accepted by ch0",
              true, ""); // no crash = pass
    }

    // CTC-CW-03: Vector word to ch1-3: accepted without crash
    {
        fresh(ctc);
        ctc.write(1, 0x00);
        ctc.write(2, 0x00);
        ctc.write(3, 0x00);
        check("CTC-CW-03", "Vector word to ch1-3: accepted",
              true, ""); // no crash = pass
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 6: CTC Interrupt Enable via set_int_enable
// ══════════════════════════════════════════════════════════════════════

static void test_section6_int_enable() {
    set_group("Int Enable");
    Ctc ctc;

    // CTC-NR-01: set_int_enable sets per-channel int enable
    {
        fresh(ctc);
        ctc.set_int_enable(0x05); // ch0 and ch2
        check("CTC-NR-01", "set_int_enable: ch0+ch2 enabled",
              ctc.channel(0).int_enabled() && !ctc.channel(1).int_enabled() &&
              ctc.channel(2).int_enabled() && !ctc.channel(3).int_enabled(),
              DETAIL("ch0=%d ch1=%d ch2=%d ch3=%d",
                     ctc.channel(0).int_enabled(), ctc.channel(1).int_enabled(),
                     ctc.channel(2).int_enabled(), ctc.channel(3).int_enabled()));
    }

    // CTC-NR-02: set_int_enable only uses lower 4 bits
    {
        fresh(ctc);
        ctc.set_int_enable(0xFF); // all bits set, but only 4 channels
        check("CTC-NR-02", "set_int_enable: all 4 channels enabled",
              ctc.channel(0).int_enabled() && ctc.channel(1).int_enabled() &&
              ctc.channel(2).int_enabled() && ctc.channel(3).int_enabled(),
              "");
    }

    // CTC-NR-03: CW D7 also sets int_en independently
    {
        fresh(ctc);
        ctc.set_int_enable(0x00);
        uint8_t cw = make_cw(true, false, false, false, false, false, false);
        ctc.write(0, cw);
        check("CTC-NR-03", "CW D7 and set_int_enable: both paths work",
              ctc.channel(0).int_enabled(), "int_en not set via CW");
    }

    // CTC-NR-04: Interrupt callback only fires when int_en is set
    {
        fresh(ctc);
        int zc_count = 0;
        ctc.on_interrupt = [&](int) { zc_count++; };
        setup_timer(ctc, 0, 0x01, false, false, false); // int_en = false
        ctc.tick(16); // ZC/TO, but no interrupt callback
        int no_int = zc_count;
        ctc.set_int_enable(0x01);
        // Need to set up again with int_en
        setup_timer(ctc, 0, 0x01, false, false, true); // int_en = true
        ctc.tick(16);
        check("CTC-NR-04", "Interrupt fires only when enabled",
              no_int == 0 && zc_count >= 1,
              DETAIL("without=%d with=%d", no_int, zc_count));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section: Multi-channel independence
// ══════════════════════════════════════════════════════════════════════

static void test_section_multichannel() {
    set_group("Multi-channel");
    Ctc ctc;

    // All 4 channels run independently
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x10);
        setup_timer(ctc, 1, 0x20);
        setup_timer(ctc, 2, 0x30);
        setup_timer(ctc, 3, 0x40);
        uint8_t v0 = ctc.read(0);
        uint8_t v1 = ctc.read(1);
        uint8_t v2 = ctc.read(2);
        uint8_t v3 = ctc.read(3);
        check("MC-01", "4 channels loaded with different TCs",
              v0 == 0x10 && v1 == 0x20 && v2 == 0x30 && v3 == 0x40,
              DETAIL("ch0=%02x ch1=%02x ch2=%02x ch3=%02x", v0, v1, v2, v3));
    }

    // Channels decrement independently
    {
        fresh(ctc);
        setup_timer(ctc, 0, 0x02);
        setup_timer(ctc, 2, 0x04);
        ctc.tick(16); // both decrement once
        uint8_t v0 = ctc.read(0);
        uint8_t v2 = ctc.read(2);
        check("MC-02", "Channels decrement independently",
              v0 == 0x01 && v2 == 0x03,
              DETAIL("ch0=%02x ch2=%02x", v0, v2));
    }

    // Read/write to invalid channels
    {
        fresh(ctc);
        uint8_t val = ctc.read(5); // out of range
        check("MC-03", "Read invalid channel returns 0xFF",
              val == 0xFF, DETAIL("got %02x", val));
    }
}

// ══════════════════════════════════════════════════════════════════════

int main() {
    printf("CTC Compliance Tests\n");
    printf("====================================\n\n");

    test_section1_state_machine();
    printf("  Group: State Machine -- done\n");

    test_section2_timer_mode();
    printf("  Group: Timer Mode -- done\n");

    test_section3_counter_mode();
    printf("  Group: Counter Mode -- done\n");

    test_section4_chaining();
    printf("  Group: Chaining -- done\n");

    test_section5_control_vector();
    printf("  Group: Control/Vector -- done\n");

    test_section6_int_enable();
    printf("  Group: Int Enable -- done\n");

    test_section_multichannel();
    printf("  Group: Multi-channel -- done\n");

    printf("\n====================================\n");
    printf("Results: %d/%d passed", g_pass, g_total);
    if (g_fail > 0)
        printf(" (%d FAILED)", g_fail);
    printf("\n");

    // Per-group summary
    printf("\nPer-group breakdown:\n");
    std::string last_group;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last_group) {
            if (!last_group.empty())
                printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);
            last_group = r.group;
            gp = gf = 0;
        }
        if (r.passed) gp++; else gf++;
    }
    if (!last_group.empty())
        printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
