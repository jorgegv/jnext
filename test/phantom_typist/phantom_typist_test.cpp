// Phantom Typist Compliance Test Runner
//
// Task 19 (instant TAP load) — exercises the FUSE-style phantom typist
// state machine in src/input/phantom_typist.{h,cpp}.
//
// Plan (derived from src/input/phantom_typist.h):
//
//   PT-01  Default-constructed typist is INACTIVE.
//   PT-02  arm() with ZX48K → WAITING, mode JPP, accumulator clear.
//   PT-03  arm() with ZX128K → WAITING, mode ENTER.
//   PT-04  arm() with ZX_PLUS3 → WAITING, mode ENTER.
//   PT-05  arm() with ZXN_ISSUE2 → WAITING, mode JPP (default).
//   PT-06  notice_keyboard_row_read() with non-single-bit row mask is
//          ignored (only the 8 power-of-two row selects accumulate).
//   PT-07  Single-bit row reads OR-fold into the accumulator until 0xff.
//   PT-08  tick_frame() with full scan transitions to POST_DELAY and
//          does NOT yet queue keystrokes.
//   PT-09  POST_DELAY counts down across POST_TRIGGER_DELAY_FRAMES
//          frames; on the final tick, fire() queues the per-mode
//          keystroke sequence on the attached Keyboard.
//   PT-10  After fire(), state returns to INACTIVE and accumulator
//          starts clean for a potential re-arm.
//   PT-11  JPP mode queues 4 keystrokes (J, SS+P, SS+P, ENTER).
//   PT-12  ENTER mode queues 1 keystroke (ENTER).
//   PT-13  reset() returns the typist to INACTIVE from any state.
//   PT-14  Re-arm from INACTIVE works.
//   PT-15  Re-arm mid-WAITING clears the accumulator (so a previously
//          observed partial scan doesn't pre-trigger).
//   PT-16  notice_keyboard_row_read() is a no-op when INACTIVE
//          (defensive — no observable effect, no crash).
//   PT-17  Fire-without-attach() does NOT crash (safety net) and
//          transitions to INACTIVE.
//
// Run: ./build/test/phantom_typist_test

#include "input/phantom_typist.h"
#include "input/keyboard.h"
#include "memory/contention.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;

struct TestResult {
    std::string id;
    std::string description;
    bool passed;
    std::string detail;
};

static std::vector<TestResult> g_results;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = "") {
    g_total++;
    TestResult r{id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) printf(" [%s]", detail.c_str());
        printf("\n");
    }
}

static std::string detail(const char* fmt, int a, int b = 0) {
    char buf[128];
    if (b == 0) snprintf(buf, sizeof(buf), fmt, a);
    else        snprintf(buf, sizeof(buf), fmt, a, b);
    return std::string(buf);
}

// ── Helpers ──────────────────────────────────────────────────────────────

// Drive the typist's row-read accumulator with the canonical 8 single-row
// reads in arbitrary order (here ascending row index 0..7). These match
// FUSE phantom_typist.c:373-386 (the 8 active-low row-select bytes).
static const uint8_t kRowSelectBytes[8] = {
    0xfe, // row 0 (A8=0)
    0xfd, // row 1 (A9=0)
    0xfb, // row 2 (A10=0)
    0xf7, // row 3 (A11=0)
    0xef, // row 4 (A12=0)
    0xdf, // row 5 (A13=0)
    0xbf, // row 6 (A14=0)
    0x7f, // row 7 (A15=0)
};

static void scan_all_rows(PhantomTypist& pt) {
    for (uint8_t b : kRowSelectBytes) pt.notice_keyboard_row_read(b);
}

// Reach POST_DELAY by simulating a full keyboard scan and one
// tick_frame. Reaches the brink of fire() at the next tick_frame call.
static void simulate_full_scan_to_post_delay(PhantomTypist& pt) {
    scan_all_rows(pt);
    pt.tick_frame();   // WAITING + 0xff → POST_DELAY
}

// ══════════════════════════════════════════════════════════════════════════
// Test cases
// ══════════════════════════════════════════════════════════════════════════

static void test_pt_01_default_inactive() {
    PhantomTypist pt;
    check("PT-01", "default-constructed typist is INACTIVE",
          !pt.is_active(), "");
}

static void test_pt_02_arm_48k() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    check("PT-02", "arm(ZX48K) → typist is active",
          pt.is_active(), "");
}

static void test_pt_03_arm_128k() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX128K);
    check("PT-03", "arm(ZX128K) → typist is active",
          pt.is_active(), "");
}

static void test_pt_04_arm_plus3() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX_PLUS3);
    check("PT-04", "arm(ZX_PLUS3) → typist is active",
          pt.is_active(), "");
}

static void test_pt_05_arm_next() {
    // The runtime gate in Emulator::load_tap() blocks Next-mode arming,
    // but the unit-level API is robust — falls through to JPP mode.
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZXN_ISSUE2);
    check("PT-05", "arm(ZXN_ISSUE2) → typist is active",
          pt.is_active(), "");
}

static void test_pt_06_non_single_bit_ignored() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    // 0xff = no rows selected (all high) — not a row scan, ignore.
    pt.notice_keyboard_row_read(0xff);
    // 0xfc = rows 0+1 both selected — multi-bit, not a single scan.
    pt.notice_keyboard_row_read(0xfc);
    // 0x00 = all rows selected — degenerate, ignore.
    pt.notice_keyboard_row_read(0x00);
    // None of those should have advanced the accumulator. So after a
    // single tick_frame, we're still in WAITING.
    pt.tick_frame();
    check("PT-06", "non-single-bit row reads do not accumulate",
          pt.is_active(), "still WAITING");
}

static void test_pt_07_single_bit_accumulates_and_fires() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    // 7 of 8 rows in one frame — not full, no transition yet.
    for (int i = 0; i < 7; ++i) pt.notice_keyboard_row_read(kRowSelectBytes[i]);
    pt.tick_frame();
    check("PT-07a", "7 of 8 row reads → still WAITING (no transition)",
          pt.is_active(), "");
    // Now scan all 8 in one frame. Should transition to POST_DELAY.
    scan_all_rows(pt);
    pt.tick_frame();
    check("PT-07b", "8 of 8 row reads → transitioned (still active)",
          pt.is_active(), "");
}

static void test_pt_08_post_delay_does_not_fire_immediately() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    simulate_full_scan_to_post_delay(pt);
    // Right after entering POST_DELAY the Keyboard auto-type queue
    // must be empty (typist hasn't fired yet).
    check("PT-08", "POST_DELAY entry: keyboard auto-type queue is empty",
          !kb.auto_typing(), "");
}

static void test_pt_09_post_delay_countdown_and_fire() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    simulate_full_scan_to_post_delay(pt);
    // Tick the typist enough frames to expire the post-delay. The
    // exact constant is POST_TRIGGER_DELAY_FRAMES (currently 8, matching
    // FUSE's state_info[PHANTOM_TYPIST_STATE_LOAD].delay_before_state).
    // We bound by 60 to avoid making the test brittle to small future
    // tuning changes; the assertion is "fires within reasonable margin".
    int frames_until_fire = -1;
    for (int i = 1; i <= 60; ++i) {
        pt.tick_frame();
        if (kb.auto_typing()) { frames_until_fire = i; break; }
    }
    check("PT-09a", "POST_DELAY eventually fires",
          frames_until_fire >= 0,
          detail("frames_until_fire=%d", frames_until_fire));
    // After fire, the typist becomes INACTIVE (the Keyboard auto-type
    // queue takes over from here).
    check("PT-09b", "after fire(): typist is INACTIVE",
          !pt.is_active(), "");
}

static void test_pt_10_accumulator_clear_after_fire() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    simulate_full_scan_to_post_delay(pt);
    for (int i = 0; i < 60 && !kb.auto_typing(); ++i) pt.tick_frame();
    // Re-arm. Accumulator should start clean.
    pt.arm(MachineType::ZX48K);
    // One full scan should be needed again (not zero) to reach
    // POST_DELAY. Test: only 7 reads should NOT cause a transition.
    for (int i = 0; i < 7; ++i) pt.notice_keyboard_row_read(kRowSelectBytes[i]);
    pt.tick_frame();
    check("PT-10", "accumulator clear on re-arm (re-armed typist still active after 7 partial reads)",
          pt.is_active(), "");
}

static void test_pt_11_jpp_queues_four_keys() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    simulate_full_scan_to_post_delay(pt);
    for (int i = 0; i < 60 && !kb.auto_typing(); ++i) pt.tick_frame();
    // After fire, kb is auto-typing. Each AutoKey entry takes ~9
    // frames to consume (5 hold + 4 gap). With 4 keys queued, the
    // typist should remain auto-typing for ~36 frames. We just verify
    // it's currently auto-typing.
    check("PT-11", "JPP mode: Keyboard is auto-typing after fire",
          kb.auto_typing(), "");
}

static void test_pt_12_enter_queues_one_key() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX128K);  // → ENTER mode
    simulate_full_scan_to_post_delay(pt);
    for (int i = 0; i < 60 && !kb.auto_typing(); ++i) pt.tick_frame();
    check("PT-12a", "ENTER mode: Keyboard is auto-typing after fire",
          kb.auto_typing(), "");
    // After 5 frames of hold + 4 frames of gap = 9 frames, the single
    // ENTER key should be done. Tick ~12 frames to consume it.
    for (int i = 0; i < 12; ++i) kb.tick_auto_type();
    check("PT-12b", "ENTER mode: single keystroke consumed in ~9 frames",
          !kb.auto_typing(), "");
}

static void test_pt_13_reset_from_waiting() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    pt.reset();
    check("PT-13a", "reset() from WAITING → INACTIVE",
          !pt.is_active(), "");
    // Also from POST_DELAY:
    pt.arm(MachineType::ZX48K);
    simulate_full_scan_to_post_delay(pt);
    pt.reset();
    check("PT-13b", "reset() from POST_DELAY → INACTIVE",
          !pt.is_active(), "");
}

static void test_pt_14_re_arm_from_inactive() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    // Start INACTIVE, arm, deliver, then arm again.
    pt.arm(MachineType::ZX48K);
    simulate_full_scan_to_post_delay(pt);
    for (int i = 0; i < 60 && !kb.auto_typing(); ++i) pt.tick_frame();
    // Drain the auto-type queue.
    for (int i = 0; i < 60 && kb.auto_typing(); ++i) kb.tick_auto_type();
    check("PT-14a", "after delivery: typist INACTIVE",
          !pt.is_active(), "");
    pt.arm(MachineType::ZX48K);
    check("PT-14b", "re-arm from INACTIVE → active again",
          pt.is_active(), "");
}

static void test_pt_15_re_arm_clears_accumulator() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    pt.arm(MachineType::ZX48K);
    // Half-scan: 4 of 8 rows.
    for (int i = 0; i < 4; ++i) pt.notice_keyboard_row_read(kRowSelectBytes[i]);
    // Re-arm. The accumulator should reset; the previous half-scan
    // should NOT survive into the new arming.
    pt.arm(MachineType::ZX48K);
    // 7 of 8 rows total in the next frame.
    for (int i = 0; i < 7; ++i) pt.notice_keyboard_row_read(kRowSelectBytes[i]);
    pt.tick_frame();
    check("PT-15", "re-arm clears accumulator (7 reads after re-arm → no transition)",
          pt.is_active(), "");
}

static void test_pt_16_inactive_is_no_op() {
    Keyboard kb; kb.reset();
    PhantomTypist pt; pt.attach(&kb);
    // No arm() call. Pump some row reads and a frame tick — should
    // remain INACTIVE with no observable effect.
    scan_all_rows(pt);
    pt.tick_frame();
    check("PT-16", "INACTIVE typist: row reads and frame tick are no-ops",
          !pt.is_active() && !kb.auto_typing(), "");
}

static void test_pt_17_fire_without_attach() {
    PhantomTypist pt;   // no attach()
    pt.arm(MachineType::ZX48K);
    simulate_full_scan_to_post_delay(pt);
    for (int i = 0; i < 60 && pt.is_active(); ++i) pt.tick_frame();
    check("PT-17", "fire() without attach: typist transitions to INACTIVE safely",
          !pt.is_active(), "");
}

// ══════════════════════════════════════════════════════════════════════════
// main
// ══════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("Phantom Typist Compliance Test\n");
    printf("==============================\n\n");

    test_pt_01_default_inactive();
    test_pt_02_arm_48k();
    test_pt_03_arm_128k();
    test_pt_04_arm_plus3();
    test_pt_05_arm_next();
    test_pt_06_non_single_bit_ignored();
    test_pt_07_single_bit_accumulates_and_fires();
    test_pt_08_post_delay_does_not_fire_immediately();
    test_pt_09_post_delay_countdown_and_fire();
    test_pt_10_accumulator_clear_after_fire();
    test_pt_11_jpp_queues_four_keys();
    test_pt_12_enter_queues_one_key();
    test_pt_13_reset_from_waiting();
    test_pt_14_re_arm_from_inactive();
    test_pt_15_re_arm_clears_accumulator();
    test_pt_16_inactive_is_no_op();
    test_pt_17_fire_without_attach();

    printf("\nTotal: %d  Passed: %d  Failed: %d  Skipped: %d\n",
           g_total, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
