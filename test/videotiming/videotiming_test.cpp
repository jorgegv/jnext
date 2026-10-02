// VideoTiming Expansion Compliance Test — scaffolding suite for the 22
// plan rows defined in doc/testing/VIDEOTIMING-TEST-PLAN-DESIGN.md
// (VideoTiming expansion plan, 2026-04-24).
//
// Every row starts as skip() with a reason code:
//   - F-VT-ACCESSOR   — new accessor must be added; row flips when the
//                       accessor lands.
//   - F-VT-MAX-REBASE — semantic rebase of an existing accessor
//                       (vc_max_ / hc_max_) storage; requires caller audit
//                       and a coupled-commit with Section 6 rows
//                       (VT-18..VT-20). See §Implementation coupling in
//                       the plan doc.
//
// Both codes are class-F per UNIT-TEST-PLAN-EXECUTION §Skip taxonomy
// ("real TODO blocked on emulator change"). The suffix distinguishes
// add-a-new-getter vs change-what-an-existing-getter-returns.
//
// The suite exists so the project's test dashboard can honestly reflect
// the 22 outstanding plan rows before any implementation lands. No live
// check() rows are emitted by this scaffold commit — that flip is
// reserved for the Phase-1 accessor-land commits per the 1:1:1 protocol
// in doc/testing/UNIT-TEST-PLAN-EXECUTION.md §4.
//
// Reference plan: doc/testing/VIDEOTIMING-TEST-PLAN-DESIGN.md
// Reference structural template:
//   test/ula/ula_integration_test.cpp (skip-tracking harness)
//   test/ctc_interrupts/ctc_interrupts_test.cpp (skip() helper idiom)
//
// Run: ./build/test/videotiming_test

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "video/timing.h"
#include "memory/contention.h"  // MachineType

// Section 8 (G163) drives the full Emulator to exercise mid-frame
// line-interrupt re-evaluation on NR 0x22 / NR 0x23 writes.
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"  // StateWriter/StateReader (Section 11, Task 56)
#include "core/jns_snapshot.h"          // Section 16 (GH #290): .jns round trip
#include "save/state_desc_json.h"      // Section 16: a pre-GH #290 tail block
#include "save/state_desc_defaults.h"  // Section 16: §12.2 default gate
#include "../row_id.h"

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

[[maybe_unused]] void check(const char* id, const char* desc, bool cond,
                            const std::string& detail = {}) {
    report_row_id(id);
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
    report_row_id(id);
    g_skipped.push_back({id, reason});
}

} // namespace

// ══════════════════════════════════════════════════════════════════════
// Section 1 — Per-machine frame envelope (c_max_hc / c_max_vc)
// VHDL: zxula_timing.vhd:147-312
// Skip reason: F-VT-MAX-REBASE (rebase vc_max_ storage + drop -1 in
//              int_line_num(); see plan §Implementation coupling).
//              Rows VT-01..VT-03 share a single implementation commit
//              with Section 6 rows VT-18..VT-20 — un-skipping one
//              without the other flips the other set to FAIL.
// ══════════════════════════════════════════════════════════════════════

static void section1_frame_envelope() {
    set_group("VT-S1-FRAME-ENVELOPE");

    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        check("VT-01",
              "48K hc_max()=447, vc_max()=311 after init(ZX48K) "
              "(zxula_timing.vhd:262,270)",
              vt.hc_max() == 447 && vt.vc_max() == 311);
    }
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        check("VT-02",
              "128K hc_max()=455, vc_max()=310 after init(ZX128K) "
              "(zxula_timing.vhd:196,204)",
              vt.hc_max() == 455 && vt.vc_max() == 310);
    }
    // VT-03 RETIRED 2026-05-04: standalone Pentagon machine type dropped
    // (Wave 0.3 follow-up). VideoTiming no longer has a Pentagon branch.
}

// ══════════════════════════════════════════════════════════════════════
// Section 2 — Per-machine active-display origin
// VHDL: zxula_timing.vhd:147-312 (c_min_hactive / c_min_vactive)
// Skip reason: F-VT-ACCESSOR (new RasterPos display_origin() accessor).
// ══════════════════════════════════════════════════════════════════════

static void section2_display_origin() {
    set_group("VT-S2-DISPLAY-ORIGIN");

    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        auto p = vt.display_origin();
        check("VT-04", "128K display_origin() = {136, 64} (zxula_timing.vhd:195,203)",
              p.hc == 136 && p.vc == 64,
              "got {" + std::to_string(p.hc) + "," + std::to_string(p.vc) + "}");
    }
    // VT-05 RETIRED 2026-05-04: standalone Pentagon machine type dropped.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        auto p = vt.display_origin();
        check("VT-06", "48K display_origin() = {128, 64} (zxula_timing.vhd:261,269)",
              p.hc == 128 && p.vc == 64,
              "got {" + std::to_string(p.hc) + "," + std::to_string(p.vc) + "}");
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 3 — ULA prefetch origin (c_min_hactive − 12) and vc_ula reset
// VHDL: zxula_timing.vhd:423-451
// Skip reason: F-VT-ACCESSOR (new int ula_prefetch_origin_hc() accessor).
// ══════════════════════════════════════════════════════════════════════

static void section3_ula_prefetch_origin() {
    set_group("VT-S3-ULA-PREFETCH-ORIGIN");

    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        int hc = vt.ula_prefetch_origin_hc();
        check("VT-07", "48K ula_prefetch_origin_hc() = 128 - 12 = 116 (zxula_timing.vhd:423)",
              hc == 116, "got " + std::to_string(hc));
    }
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        int hc = vt.ula_prefetch_origin_hc();
        check("VT-08", "128K ula_prefetch_origin_hc() = 136 - 12 = 124 (zxula_timing.vhd:423)",
              hc == 124, "got " + std::to_string(hc));
    }
    // VT-09 RETIRED 2026-05-04: standalone Pentagon machine type dropped.

    // ── GH #181 — hc_ula_zero_raw_hc(): the REGISTERED reset origin ──
    //
    // `ula_prefetch_origin_hc()` above is where the hc_ula reset is ARMED:
    //   ula_min_hactive <= c_min_hactive - 12;              (:423)
    //   ula_max_hc      <= '1' when hc = ula_min_hactive;   (:424, combinational)
    // but the reset itself lives inside `process (i_CLK_7)` (:427-436), so it
    // is REGISTERED and lands one 7 MHz tick later — by which time the raw
    // frame counter `hc` (itself registered, :316-324) has advanced to
    // `c_min_hactive - 11`. Hence
    //     hc_ula == 0   <=>   raw hc == c_min_hactive - 11
    // which is the "EVERYTHING BELOW DELAYED ONE PIXEL FROM FRAME COUNTER"
    // note at :344, and the origin AttributeMux independently derives
    // (attribute_mux.h:281, FUSE-verified in Task 54).
    //
    // These rows assert the value EXACTLY. The GH181-HCULA-* integration
    // rows in copper_integration_test measure through a NOP sled and so
    // cannot resolve a single pixel; that +1 is the whole point of the fix,
    // so it gets a direct, slack-free row per timing mode here.
    //
    // c_min_hactive per branch of the `process (i_timing, i_50_60)` at :147:
    //   :150 i_timing(2)='1'            -> Pentagon           :159 = 128
    //   :178 elsif i_timing(1)='1'      -> 128K-class
    //        :180 i_50_60='0' (50 Hz)                         :195 = 136
    //        :214 else        (60 Hz)                         :229 = 136
    //   :250 else                       -> 48K-class
    //        :252 i_50_60='0' (50 Hz)                         :261 = 128
    //        :280 else        (60 Hz)                         :289 = 128
    // 128K vs +3 is i_timing(0) INSIDE the 128K-class branch (:186-189) and
    // only moves c_int_h — both share :195 at 50 Hz. All rows below use the
    // 50 Hz default, so none of them cites the 60 Hz lines.
    {
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::Timing48);
        int hc = vt.hc_ula_zero_raw_hc();
        check("VT-GH181-01",
              "48K timing: hc_ula==0 at raw hc = c_min_hactive - 11 = 117 "
              "(VHDL zxula_timing.vhd:261,423-436,344)",
              hc == 117, "got " + std::to_string(hc));
    }
    {
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::Timing128);
        int hc = vt.hc_ula_zero_raw_hc();
        check("VT-GH181-02",
              "128K timing: hc_ula==0 at raw hc = c_min_hactive - 11 = 125 "
              "(VHDL zxula_timing.vhd:195,423-436,344)",
              hc == 125, "got " + std::to_string(hc));
    }
    {
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::TimingPlus3);
        int hc = vt.hc_ula_zero_raw_hc();
        check("VT-GH181-03",
              "+3 timing: hc_ula==0 at raw hc = c_min_hactive - 11 = 125 "
              "(VHDL zxula_timing.vhd:195,423-436,344)",
              hc == 125, "got " + std::to_string(hc));
    }
    {
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::TimingPentagon);
        int hc = vt.hc_ula_zero_raw_hc();
        check("VT-GH181-04",
              "Pentagon timing: hc_ula==0 at raw hc = c_min_hactive - 11 = "
              "117 (VHDL zxula_timing.vhd:159,423-436,344)",
              hc == 117, "got " + std::to_string(hc));
    }
    {
        // The machine axis the Copper harness actually runs on.
        VideoTiming vt;
        vt.init(MachineType::ZXN_ISSUE2);
        int hc = vt.hc_ula_zero_raw_hc();
        check("VT-GH181-05",
              "Next (ZXN_ISSUE2, 128K-class slot): hc_ula==0 at raw hc = 125 "
              "(VHDL zxula_timing.vhd:195,423-436,344)",
              hc == 125, "got " + std::to_string(hc));
    }
    {
        // Exactly one pixel apart, by construction — the relationship the
        // fix depends on, stated independently of the absolute values.
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::Timing128);
        int armed = vt.ula_prefetch_origin_hc();
        int zero  = vt.hc_ula_zero_raw_hc();
        check("VT-GH181-06",
              "the registered reset puts hc_ula==0 exactly ONE pixel after "
              "the armed origin (VHDL zxula_timing.vhd:424 vs :427-436)",
              zero - armed == 1,
              "armed=" + std::to_string(armed) + " zero=" + std::to_string(zero));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 4 — Per-machine interrupt position (c_int_h / c_int_v)
// VHDL: zxula_timing.vhd:155-293, zxula_timing.vhd:548-557
// Skip reason: F-VT-ACCESSOR (new RasterPos int_position() accessor).
// ══════════════════════════════════════════════════════════════════════

static void section4_int_position() {
    set_group("VT-S4-INT-POSITION");

    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        auto p = vt.int_position();
        check("VT-10", "48K int_position = {116, 0} (zxula_timing.vhd:257,265)",
              p.hc == 116 && p.vc == 0);
    }
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        auto p = vt.int_position();
        check("VT-11", "128K int_position = {128, 1} (zxula_timing.vhd:187,199)",
              p.hc == 128 && p.vc == 1);
    }
    // VT-12 RETIRED 2026-05-04: standalone Pentagon machine type dropped.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX_PLUS3);
        auto p = vt.int_position();
        check("VT-13", "+3 int_position = {126, 1} — i_timing(0)='1' (zxula_timing.vhd:189,199)",
              p.hc == 126 && p.vc == 1);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 5 — 60 Hz variant (48K / 128K / +3)
// VHDL: zxula_timing.vhd:214-308 (i_50_60='1' branch)
// Skip reason: F-VT-ACCESSOR (new 60 Hz enum variant OR
//              set_refresh_60hz(bool) switch on VideoTiming; see plan
//              §Open questions Q2).
// ══════════════════════════════════════════════════════════════════════

static void section5_60hz_variant() {
    set_group("VT-S5-60HZ-VARIANT");

    // VT-14 / VT-15 — frame T-state count is convention-independent;
    // assert by counting advance() calls until frame_complete(). 48K
    // 60Hz: 264 lines * 448 pixel-ticks/line / 2 ticks-per-Tstate = 59136.
    // 128K 60Hz: 264 * 456 / 2 = 60192. (VHDL c_max_hc=447/455 +1.)
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K, /*refresh_60hz=*/true);
        int t = 0;
        while (!vt.frame_complete()) { vt.advance(1); ++t; }
        check("VT-14", "48K 60Hz frame = 448*264/2 = 59136 T-states "
                       "(zxula_timing.vhd:290,298)",
              t == 59136,
              std::string("got ") + std::to_string(t));
    }
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K, /*refresh_60hz=*/true);
        int t = 0;
        while (!vt.frame_complete()) { vt.advance(1); ++t; }
        check("VT-15", "128K 60Hz frame = 456*264/2 = 60192 T-states "
                       "(zxula_timing.vhd:230,238)",
              t == 60192,
              std::string("got ") + std::to_string(t));
    }
    // VT-16 — uses Branch B's display_origin() accessor (duplicated in
    // this branch with a // COORDINATION: marker; cherry-pick conflict
    // expected — accept Branch B's version on resolution).
    {
        VideoTiming vt48; vt48.init(MachineType::ZX48K, true);
        VideoTiming vt128; vt128.init(MachineType::ZX128K, true);
        check("VT-16", "60Hz display_origin.vc = 40 for 48K + 128K "
                       "(zxula_timing.vhd:297,237)",
              vt48.display_origin().vc  == 40 &&
              vt128.display_origin().vc == 40);
    }
    {
        VideoTiming vt48; vt48.init(MachineType::ZX48K, true);
        VideoTiming vt128; vt128.init(MachineType::ZX128K, true);
        auto p48  = vt48.int_position();
        auto p128 = vt128.int_position();
        check("VT-17", "60Hz int_position.vc = 0 for 48K + 128K "
                       "(zxula_timing.vhd:293,233)",
              p48.vc == 0 && p128.vc == 0);
    }
    {
        VideoTiming vt_p3;   vt_p3.init(MachineType::ZX_PLUS3, true);
        VideoTiming vt_128k; vt_128k.init(MachineType::ZX128K, true);
        check("VT-17b", "+3 60Hz int_h=126 vs 128K 60Hz int_h=128 — "
                        "i_timing(0)='1' split (zxula_timing.vhd:221,223)",
              vt_p3.int_position().hc   == 126 &&
              vt_128k.int_position().hc == 128);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 6 — Line-interrupt target mapping (int_line_num)
// VHDL: zxula_timing.vhd:566-570
// Skip reason:
//   VT-18..VT-20 — F-VT-MAX-REBASE (coupled-commit with Section 1 V1
//                  rebase; see plan §Implementation coupling).
//   VT-21        — F-VT-ACCESSOR (promote private int_line_num() to a
//                  public/friend observer accessor; no storage rebase).
// ══════════════════════════════════════════════════════════════════════

static void section6_line_int_target() {
    set_group("VT-S6-LINE-INT-TARGET");

    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        vt.set_line_interrupt_target(0);
        check("VT-18",
              "48K target=0 → int_line_num() == c_max_vc == 311 "
              "(zxula_timing.vhd:566-570)",
              vt.int_line_num() == 311);
    }
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        vt.set_line_interrupt_target(0);
        check("VT-19",
              "128K target=0 → int_line_num() == c_max_vc == 310 "
              "(zxula_timing.vhd:566-570)",
              vt.int_line_num() == 310);
    }
    // VT-20 RETIRED 2026-05-04: standalone Pentagon machine type dropped.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        vt.set_line_interrupt_target(10);
        check("VT-21",
              "any machine: target=10 → int_line_num() == 9 "
              "(zxula_timing.vhd:568)",
              vt.int_line_num() == 9);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 7 — Production scheduler wiring (G106 / G107 / G109 / G71)
// VHDL: zxula_timing.vhd:455-466, :548-557, :563-583
//
// All five rows live as of 2026-04-28 — the Emulator scheduler now
// consumes VideoTiming::int_line_num() / frame_int_master_cycle_offset()
// / line_int_master_cycle_offset(); the local Emulator shadow fields
// (line_int_enabled_, line_int_value_) were removed in the same commit.
// The pulse-counter logic in advance() is also re-verified against the
// VHDL cvc reload semantics (cu_offset shift at ula_min_vactive).
// ══════════════════════════════════════════════════════════════════════

namespace {

// Step the raster forward by exactly one full scanline (`(hc_max+1)/2`
// T-states; valid because hc_max+1 is always even — 448 or 456).
void step_one_line(VideoTiming& vt) {
    vt.advance((vt.hc_max() + 1) / 2);
}

}  // namespace

static void section7_scheduler_wiring() {
    set_group("VT-S7-SCHEDULER-WIRING");

    // VT-22 — G106: 48K target=10 → int_line_num=9; cvc(vc) reloads at
    // ula_min_vactive=64 (VHDL :462), so cvc==9 when vc = 64 + 9 = 73
    // (cu_offset=0). Pulse fires once per frame when leaving vc=73.
    // VHDL: zxula_timing.vhd:566-570 (target!=0 → int_line_num=target-1),
    //       :577 (compare against cvc).
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        vt.set_line_interrupt_enable(true);
        vt.set_line_interrupt_target(10);
        vt.clear_int_counts();
        // Step to end of vc=72: pulse must not have fired yet.
        for (int line = 0; line <= 72; ++line) step_one_line(vt);
        const int before = vt.line_int_pulse_count();
        // Cross vc=73 → vc=74: should fire exactly once.
        step_one_line(vt);
        const int after = vt.line_int_pulse_count();
        check("VT-22",
              "G106: 48K target=10 → pulse on cvc=9 transition (vc=73 with "
              "min_vactive=64, cu_offset=0); zxula_timing.vhd:462,566-570,577",
              before == 0 && after == 1,
              "before=" + std::to_string(before) + " after=" + std::to_string(after));
    }

    // VT-23 — G106: 48K target=0 → line-int fires at cvc=c_max_vc=311 (last line of frame).
    // VHDL zxula_timing.vhd:566-570: target=0 → int_line_num = c_max_vc.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        vt.set_line_interrupt_enable(true);
        vt.set_line_interrupt_target(0);
        vt.clear_int_counts();
        // Step a complete frame: target line is the very last (vc=311).
        // Drive one full frame's T-states.
        const int frame_t = (vt.hc_max() + 1) * (vt.vc_max() + 1) / 2;  // 448*312/2
        vt.advance(frame_t);
        const int pulses = vt.line_int_pulse_count();
        check("VT-23",
              "G106: 48K target=0 → one pulse per frame at cvc=c_max_vc=311 "
              "(zxula_timing.vhd:566-570)",
              pulses == 1,
              std::string("pulses=") + std::to_string(pulses));
    }

    // VT-24 — G107: 128K frame-INT fires at (hc=128, vc=1).
    // VHDL :551 — int_ula='1' when hc==c_int_h and vc==c_int_v.
    // Master-cycle = (vc * pixels_per_line + hc) * 4 = (1*456 + 128)*4 = 2336.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        const uint64_t got      = vt.frame_int_master_cycle_offset();
        const uint64_t expected = (1ULL * 456 + 128) * 4;  // 2336
        check("VT-24",
              "G107: 128K frame-INT offset = (vc=1 * 456 + hc=128) * 4 = 2336 "
              "master cycles (zxula_timing.vhd:187,199,551)",
              got == expected,
              std::string("got ") + std::to_string(got)
                  + " expected " + std::to_string(expected));
    }

    // VT-25 — G109: NR 0x64 cu_offset shifts the line-int compare.
    // VHDL :462 reloads cvc to ('0' & i_cu_offset) at ula_min_vactive;
    // :577 compares cvc, not raw vc. So target=10 with cu_offset=5 fires
    // when cvc=9 == (vc - min_vactive + 5) mod (c_max_vc+1), i.e.
    //   vc = (9 + min_vactive - 5) mod (c_max_vc+1).
    // 48K: min_vactive=64, vc=64+9-5=68. With cu_offset=0 baseline vc=9
    // — distinct, so the offset is observable.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        vt.set_cu_offset(5);
        vt.set_line_interrupt_enable(true);
        vt.set_line_interrupt_target(10);
        vt.clear_int_counts();
        // Step to vc=67 — should not fire (target line is 68).
        for (int line = 0; line <= 67; ++line) step_one_line(vt);
        const int before = vt.line_int_pulse_count();
        // Cross vc=68 → vc=69 — fire.
        step_one_line(vt);
        const int after = vt.line_int_pulse_count();
        check("VT-25",
              "G109: NR 0x64=5 + target=10 → pulse at vc=68 (cvc=9; "
              "min_vactive=64); zxula_timing.vhd:462,577",
              before == 0 && after == 1,
              "before=" + std::to_string(before) + " after=" + std::to_string(after));
    }

    // VT-26 — G71: Emulator shadow fields line_int_enabled_/line_int_value_
    // removed; VideoTiming is the single source of truth for the line-int
    // enable + 9-bit target. The cleanest unit-level invariant is
    // "VideoTiming round-trips writes through the same surface the
    // production scheduler reads from", which the previous tests already
    // exercise. Here we lock the contract: enabling+target survives
    // through the public VideoTiming API exactly (no shadow drift).
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        vt.set_line_interrupt_enable(true);
        vt.set_line_interrupt_target(0x123);
        const bool en_ok  = vt.line_interrupt_enable();
        const auto tgt    = vt.line_interrupt_target();
        // int_line_num for target=0x123 → target-1 = 0x122 = 290.
        const auto ilnum  = vt.int_line_num();
        check("VT-26",
              "G71: VideoTiming line-int enable + 9-bit target round-trip; "
              "int_line_num(target=0x123)=0x122 (zxula_timing.vhd:566-570)",
              en_ok && tgt == 0x123 && ilnum == 0x122,
              "en=" + std::to_string(en_ok)
                  + " tgt=0x" + std::to_string(tgt)
                  + " ilnum=" + std::to_string(ilnum));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 8 — G163 mid-frame line-interrupt re-evaluation
// VHDL: zxula_timing.vhd:577 (fire predicate, fully dynamic — fires every
//       cycle that hc_ula==255 AND cvc==int_line_num);
//       zxula_timing.vhd:566-570 (target=0 → c_max_vc; target=N → N-1);
//       zxnext.vhd:5607-5610, :6752-6753 (NR 0x22 bit 1 + NR 0x23 →
//       i_inten_line, i_int_line).
//
// These rows exercise the dynamic re-scheduling on NR 0x22 / NR 0x23
// mid-frame writes (the parallax.nex chained-line-IRQ pattern). Pre-G163
// jnext scheduled the line-int once at frame start in run_frame() and
// the NR write handlers updated VideoTiming state without rescheduling,
// silently swallowing 12/13 chained line interrupts per frame on
// parallax.nex. See doc/issues/parallax-demo/PARALLAX-NEX-INVESTIGATION.md and gap
// doc G163 for the root cause + fix shape.
//
// Test harness: drive the real Emulator (ZXN_ISSUE2 — 128K/Next timing,
// master_cycles_per_line=1824). Place a 2-byte JR-to-self at PC=0xC000
// (12 T-states / 96 master cycles per iteration) and step the CPU via
// `execute_single_instruction()` until the desired raster position is
// reached. Observe production line-int fires through
// `Emulator::line_int_fire_count()` — incremented exactly once per
// non-superseded EventType::CPU_INT lambda from
// `reschedule_line_interrupt()`. CPU IFF=0 at reset, so even though
// `request_interrupt(0xFF)` is called inside the lambda the Z80 ignores
// it; this lets us observe pure scheduler behaviour without the demo's
// IRQ handler running.
// ══════════════════════════════════════════════════════════════════════

namespace g163 {

// Build a fresh ZXN_ISSUE2 emulator with no ROM-driven boot side effects.
static bool build_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Read NR via the real port path (OUT 0x243B,reg; IN 0x253B).
[[maybe_unused]] static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

// Write NR via the real port path (OUT 0x243B,reg; OUT 0x253B,val).
// This routes through NextReg::write so the NR 0x22 / NR 0x23 write
// handlers (which now call reschedule_line_interrupt()) fire exactly as
// they would when the Z80 executes a real OUT instruction.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// Place a `JR $` (0x18 0xFE — jumps to itself, 12 T-states) loop at
// PC=0xC000 and point the CPU at it. Each execute_single_instruction()
// returns 12 T-states = 96 master cycles. Used to advance the master
// clock incrementally so we can perform mid-frame NR writes at
// arbitrary raster positions.
static void install_jr_self_loop(Emulator& emu) {
    constexpr uint16_t code_addr = 0xC000;
    emu.mmu().write(code_addr + 0, 0x18);  // JR
    emu.mmu().write(code_addr + 1, 0xFE);  // -2 (self)
    auto regs = emu.cpu().get_registers();
    regs.PC = code_addr;
    emu.cpu().set_registers(regs);
}

// Step the CPU until clock_.get() crosses target_master_cycle.
// Bounded so a runaway test (e.g. JR loop never advancing clock) cannot
// hang indefinitely. Returns true if target was reached, false on
// safety bound.
static bool step_until_master_cycle(Emulator& emu,
                                    uint64_t target_master_cycle,
                                    int max_steps = 100000) {
    for (int i = 0; i < max_steps; ++i) {
        if (emu.clock().get() >= target_master_cycle) return true;
        emu.execute_single_instruction();
    }
    return emu.clock().get() >= target_master_cycle;
}

// Helper: master-cycle offset of raw hc 0 of the line a `target` (NR 0x23,
// MSB=0) line-int fires on. Since GH #257 it fires at hc 380 of that line
// (VT-GH257-01), inside the rows' +1824 slack. Standalone so the
// test row can compute reference values without re-driving the
// emulator. ZXN_ISSUE2 (128K timing): min_vactive=64, lines_per_frame
// =311, master_cycles_per_line=1824, cu_offset=0 default.
static uint64_t line_int_offset_master_cycles(uint16_t target) {
    constexpr uint64_t min_vactive = 64;
    constexpr uint64_t lines_per_frame = 311;
    constexpr uint64_t master_cycles_per_line = 1824;
    const uint64_t int_line_num =
        (target == 0) ? (lines_per_frame - 1) : (target - 1);
    const uint64_t vc_fire =
        (int_line_num + min_vactive + lines_per_frame) % lines_per_frame;
    return vc_fire * master_cycles_per_line;
}

}  // namespace g163

static void section8_g163_dynamic_reschedule() {
    set_group("VT-S8-G163-DYNAMIC-RESCHEDULE");

    // ────────────────────────────────────────────────────────────────
    // VT-G163-MIDRETARGET-01 — chained line-IRQ re-targeting fires
    // BOTH the original and the re-targeted line within the same
    // frame. Pre-G163 only the first scheduled line-int per frame
    // fired and any mid-frame NR 0x23 rewrite was silently swallowed.
    //
    // VHDL: zxula_timing.vhd:577 (fire predicate, fully dynamic);
    //       zxula_timing.vhd:566-570 (target-cvc map);
    //       zxnext.vhd:6753 (i_int_line driven by nr_23_line_interrupt
    //       — comparator re-checks each cycle).
    //
    // Stimulus: enable line-int with target=200; step the CPU past
    // line 200's firing offset (1 fire). Mid-frame, write NR 0x23
    // with target=220 (still future this frame). Step further to
    // cover line 220's offset. Expect line_int_fire_count() == 2.
    // ────────────────────────────────────────────────────────────────
    {
        Emulator emu;
        if (!g163::build_emulator(emu)) {
            check("VT-G163-MIDRETARGET-01",
                  "Emulator construction (ZXN_ISSUE2) — required for "
                  "Section 8",
                  false, "build_emulator returned false");
        } else {
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();

            // Prime the schedule: NR 0x23 = 200 (low byte), NR 0x22 =
            // 0x02 (line-int enable, MSB=0, ULA-int still enabled but
            // not scheduled because run_frame was never called).
            g163::nr_write(emu, 0x23, 200);
            g163::nr_write(emu, 0x22, 0x02);

            // Advance just past line 200's firing offset within the
            // frame. Add one full scanline of slack so the scheduler
            // window unambiguously contains the fire cycle.
            const uint64_t line200_fire = g163::line_int_offset_master_cycles(200);
            g163::step_until_master_cycle(emu, line200_fire + 1824);
            const uint64_t after_first = emu.line_int_fire_count();

            // Mid-frame retarget: NR 0x23 = 220 (still future).
            // The handler calls reschedule_line_interrupt(): bumps
            // the gen, schedules a fresh CPU_INT at line 220's offset
            // within the SAME frame.
            g163::nr_write(emu, 0x23, 220);

            // Advance past line 220's firing offset.
            const uint64_t line220_fire = g163::line_int_offset_master_cycles(220);
            g163::step_until_master_cycle(emu, line220_fire + 1824);
            const uint64_t after_second = emu.line_int_fire_count();

            check("VT-G163-MIDRETARGET-01",
                  "Mid-frame NR 0x23 retarget schedules a fresh line-int "
                  "for the same frame; both fires observed "
                  "(zxula_timing.vhd:577 — fully-dynamic compare)",
                  after_first == 1 && after_second == 2,
                  "after_first=" + std::to_string(after_first)
                      + " after_second=" + std::to_string(after_second));
        }
    }

    // ────────────────────────────────────────────────────────────────
    // VT-G163-WRAP-02 — retarget to a line that has already passed in
    // the current frame: the new line-int does NOT fire again this
    // frame, but DOES fire at the new target's offset in the NEXT
    // frame. Models parallax.nex's `ADD 0x10` 8-bit overflow case
    // (line 244 + 0x10 = 0x04, i.e. line 4 of the next frame).
    //
    // VHDL: same as MIDRETARGET-01. The "roll forward one frame"
    // behaviour matches the VHDL invariant that the comparator
    // continues every cycle: cvc wraps into the next frame's count
    // and the match occurs at line 4 of the next frame.
    //
    // Stimulus: enable line-int with target=200; step past line ~210
    // (1 fire). Write NR 0x23 = 100 (already past in current frame).
    // Continue stepping to end of current frame: still count==1.
    // Then run one full frame: count goes to 2.
    // ────────────────────────────────────────────────────────────────
    {
        Emulator emu;
        if (!g163::build_emulator(emu)) {
            check("VT-G163-WRAP-02",
                  "Emulator construction (ZXN_ISSUE2) — required for "
                  "Section 8",
                  false, "build_emulator returned false");
        } else {
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();

            g163::nr_write(emu, 0x23, 200);
            g163::nr_write(emu, 0x22, 0x02);

            // Step past line 210 — first fire (target=200 → line 199
            // → vc_fire=263) lands first.
            const uint64_t line210_master = g163::line_int_offset_master_cycles(210);
            g163::step_until_master_cycle(emu, line210_master);
            const uint64_t after_first = emu.line_int_fire_count();

            // Retarget to a line that's already passed (100).
            g163::nr_write(emu, 0x23, 100);

            // Step to end of current frame (master_cycles_per_frame
            // = 1824 * 311 = 567264 for ZXN_ISSUE2). Should NOT see
            // another fire in this frame — the new fire-cycle for
            // target=100 is rolled forward by one frame.
            constexpr uint64_t master_cycles_per_frame = 1824ULL * 311;
            g163::step_until_master_cycle(emu, master_cycles_per_frame - 1);
            const uint64_t after_current_frame = emu.line_int_fire_count();

            // Roll the emulator into the NEXT frame. Each run_frame
            // advances the master clock by master_cycles_per_frame
            // and runs the scheduler up to that boundary. The deferred
            // fire for target=100 sits at cycle line100_offset +
            // master_cycles_per_frame ≈ 864 576 (within the SECOND
            // frame), so two run_frame calls are required to land
            // past it. The frame-start reschedule_line_interrupt()
            // also re-schedules at the same cycle 864 576 (with a
            // bumped gen), so exactly one fire lands at that cycle —
            // the others no-op via the gen-mismatch check.
            emu.run_frame();   // close out frame 1 (clock to ~567 264).
            emu.run_frame();   // run frame 2: deferred fire lands.
            const uint64_t after_next_frame = emu.line_int_fire_count();

            check("VT-G163-WRAP-02",
                  "Mid-frame retarget to already-passed line: no extra "
                  "fire this frame; +1 fire after rolling one frame "
                  "(parallax.nex 8-bit ADD 0x10 wrap; "
                  "zxula_timing.vhd:566-570,577)",
                  after_first == 1 && after_current_frame == 1
                      && after_next_frame == 2,
                  "after_first=" + std::to_string(after_first)
                      + " after_current_frame=" + std::to_string(after_current_frame)
                      + " after_next_frame=" + std::to_string(after_next_frame));
        }
    }

    // ────────────────────────────────────────────────────────────────
    // VT-G163-DISABLE-03 — clearing NR 0x22 bit 1 (line-int enable)
    // mid-frame stops the pending fire from happening. The previously
    // scheduled event is invalidated by the generation-counter check
    // inside the lambda; no replacement schedule is enqueued because
    // `video_timing_.line_interrupt_enable()` is false.
    //
    // VHDL: zxnext.vhd:5607-5610 (NR 0x22 bit 1 → nr_22_line_interrupt_en
    //       = i_inten_line). Predicate at zxula_timing.vhd:577 gates
    //       fire on i_inten_line='1'.
    //
    // Stimulus: enable line-int with target=200; before line 200 (e.g.
    // at line 50), write NR 0x22 = 0x00 (line-int disabled, ULA-int
    // still enabled per bit 2 = 0). Step past where line 200 would
    // have fired. Expect ZERO line-int fires.
    // ────────────────────────────────────────────────────────────────
    {
        Emulator emu;
        if (!g163::build_emulator(emu)) {
            check("VT-G163-DISABLE-03",
                  "Emulator construction (ZXN_ISSUE2) — required for "
                  "Section 8",
                  false, "build_emulator returned false");
        } else {
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();

            g163::nr_write(emu, 0x23, 200);
            g163::nr_write(emu, 0x22, 0x02);

            // Step to line 50 (well before line 200's firing offset).
            const uint64_t line50_master = g163::line_int_offset_master_cycles(50);
            g163::step_until_master_cycle(emu, line50_master);
            const uint64_t at_line50 = emu.line_int_fire_count();

            // Disable line-int. Bit 2 stays 0 (ULA-int enabled). The
            // handler calls reschedule_line_interrupt() which bumps
            // the gen and returns without scheduling a replacement;
            // the previously-pending fire at line 200 will see a
            // captured-gen mismatch and no-op.
            g163::nr_write(emu, 0x22, 0x00);

            // Step past where line 200 would have fired (add slack).
            const uint64_t line200_fire = g163::line_int_offset_master_cycles(200);
            g163::step_until_master_cycle(emu, line200_fire + 1824);
            const uint64_t after_disable = emu.line_int_fire_count();

            check("VT-G163-DISABLE-03",
                  "NR 0x22 bit 1 cleared mid-frame: pending line-int "
                  "no-ops via gen-check; zero fires for the rest of "
                  "the frame (zxnext.vhd:5607-5610; "
                  "zxula_timing.vhd:577)",
                  at_line50 == 0 && after_disable == 0,
                  "at_line50=" + std::to_string(at_line50)
                      + " after_disable=" + std::to_string(after_disable));
        }
    }

    // ────────────────────────────────────────────────────────────────
    // VT-G163-C4-DISABLE-04 — same as DISABLE-03 but the disable
    // edge is driven through NR 0xC4 (bit 1) instead of NR 0x22.
    // NR 0xC4 bit 1 is a hardware mirror of NR 0x22 bit 1 — both
    // write the SAME `nr_22_line_interrupt_en` flip-flop
    // (zxnext.vhd:5607-5610) and that FF feeds `i_inten_line` of
    // the line-int comparator (zxnext.vhd:6752; predicate at
    // zxula_timing.vhd:577). The NR 0xC4 write handler must call
    // `reschedule_line_interrupt()` for the same reason NR 0x22
    // does; without it, a demo that re-arms / disables line
    // interrupts through NR 0xC4 silently keeps the stale
    // schedule alive.
    //
    // Stimulus: arm the line-int via NR 0x22 with target=200; at
    // line 50, write NR 0xC4 with bit 1 cleared (other bits
    // preserve the previous handler-side NR 0xC4 state, i.e. all
    // zero at this point). Step past where line 200 would fire.
    // Expect ZERO line-int fires.
    // ────────────────────────────────────────────────────────────────
    {
        Emulator emu;
        if (!g163::build_emulator(emu)) {
            check("VT-G163-C4-DISABLE-04",
                  "Emulator construction (ZXN_ISSUE2) — required for "
                  "Section 8",
                  false, "build_emulator returned false");
        } else {
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();

            g163::nr_write(emu, 0x23, 200);
            g163::nr_write(emu, 0x22, 0x02);

            // Step to line 50 (well before line 200's firing offset).
            const uint64_t line50_master = g163::line_int_offset_master_cycles(50);
            g163::step_until_master_cycle(emu, line50_master);
            const uint64_t at_line50 = emu.line_int_fire_count();

            // Disable line-int via the NR 0xC4 mirror (bit 1 = 0).
            // Bit 0 = 0 means "disable ULA-int" (port_ff_reg(6)=1 via
            // inverted polarity), bit 7 = 0 disables expbus int. The
            // pending fire scheduled via the NR 0x22 handler must be
            // invalidated by the gen-bump inside the NR 0xC4 handler's
            // `reschedule_line_interrupt()` call.
            g163::nr_write(emu, 0xC4, 0x00);

            // Step past where line 200 would have fired (add slack).
            const uint64_t line200_fire = g163::line_int_offset_master_cycles(200);
            g163::step_until_master_cycle(emu, line200_fire + 1824);
            const uint64_t after_disable = emu.line_int_fire_count();

            check("VT-G163-C4-DISABLE-04",
                  "NR 0xC4 bit 1 (mirror of NR 0x22 bit 1) cleared "
                  "mid-frame: pending line-int no-ops via gen-check; "
                  "zero fires for the rest of the frame "
                  "(zxnext.vhd:5607-5610 mirror; :6752 FF feeds "
                  "i_inten_line; zxula_timing.vhd:577 fire predicate)",
                  at_line50 == 0 && after_disable == 0,
                  "at_line50=" + std::to_string(at_line50)
                      + " after_disable=" + std::to_string(after_disable));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 9 — vblank_top() per-machine offset (Task 13)
// VHDL: zxula_timing.vhd:147-312 (c_min_vactive per machine)
// jnext: src/video/timing.h vblank_top()
//
// `vblank_top` = `min_vactive - DISP_Y` is the count of raw-VC scanlines
// that fall ABOVE jnext's framebuffer top border. It's the correct
// offset for converting raw VC to framebuffer row in the per-scanline
// change-log (Emulator::on_scanline). Pre-Task-13 callers used
// `Renderer::DISP_Y` (32) directly, which was correct only for
// machines with min_vactive == 64 (NEXT 50Hz family — numerical
// coincidence). Pentagon (min_vactive=80) and 60Hz overrides
// (min_vactive=40) needed the corrected per-machine accessor.
// ══════════════════════════════════════════════════════════════════════

static void section9_vblank_top() {
    set_group("VT-S9-VBLANK-TOP");

    // VT-VBT-01 — NEXT 50 Hz family: min_vactive=64, DISP_Y=32 → 32.
    {
        VideoTiming vt;
        vt.init(MachineType::ZXN_ISSUE2);
        const int v = vt.vblank_top();
        check("VT-VBT-01",
              "NEXT 50Hz vblank_top() = min_vactive(64) - DISP_Y(32) = 32 "
              "(VHDL zxula_timing.vhd:203)",
              v == 32, "got " + std::to_string(v));
    }
    // VT-VBT-02 — 48K 50 Hz: same 64-32=32.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        const int v = vt.vblank_top();
        check("VT-VBT-02",
              "48K 50Hz vblank_top() = min_vactive(64) - DISP_Y(32) = 32 "
              "(VHDL zxula_timing.vhd:269)",
              v == 32, "got " + std::to_string(v));
    }
    // VT-VBT-03 — 128K 50 Hz: same 64-32=32.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        const int v = vt.vblank_top();
        check("VT-VBT-03",
              "128K 50Hz vblank_top() = min_vactive(64) - DISP_Y(32) = 32 "
              "(VHDL zxula_timing.vhd:203)",
              v == 32, "got " + std::to_string(v));
    }
    // VT-VBT-04 RETIRED 2026-05-04: standalone Pentagon machine type
    // dropped (Wave 0.3 follow-up). VideoTiming no longer has a Pentagon
    // branch, so min_vactive=80 is unreachable.

    // VT-VBT-05 — NEXT 60 Hz: min_vactive=40, DISP_Y=32 → 8 (off-by-24
    // from NEXT 50Hz).
    {
        VideoTiming vt;
        vt.init(MachineType::ZXN_ISSUE2, /*refresh_60hz=*/true);
        const int v = vt.vblank_top();
        check("VT-VBT-05",
              "NEXT 60Hz vblank_top() = min_vactive(40) - DISP_Y(32) = 8 "
              "(VHDL zxula_timing.vhd:237)",
              v == 8, "got " + std::to_string(v));
    }
    // VT-VBT-06 RETIRED 2026-05-04 (paired with VT-VBT-04 retirement).
}

// ══════════════════════════════════════════════════════════════════════
// Section 10 — Task 51: init_timing() (tim_sel axis) + Pentagon block
// VHDL: zxula_timing.vhd:147-280 (constants keyed on i_timing), fed from
// eff_nr_03_machine_timing (zxnext.vhd:6694-6703). The Pentagon branch
// (:150-168) sits OUTSIDE the i_50_60 split — no 60 Hz variant exists.
// ══════════════════════════════════════════════════════════════════════

static void section10_t51_init_timing() {
    set_group("VT-S10-T51-INIT-TIMING");

    // VT-T51-01 — Pentagon timing constants. This block was lost when the
    // standalone Pentagon MachineType was retired (Wave 0.3); NR 0x03
    // tim_sel bit 2 can still select Pentagon timing at runtime.
    {
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::TimingPentagon);
        const bool ok = vt.hc_max() == 447 && vt.vc_max() == 319 &&
                        vt.display_origin().hc == 128 &&
                        vt.display_origin().vc == 80 &&
                        vt.int_position().hc == 439 &&
                        vt.int_position().vc == 319;
        check("VT-T51-01",
              "init_timing(Pentagon): hc_max=447, vc_max=319, display "
              "origin (128,80), INT at (439,319) "
              "[zxula_timing.vhd:155,159,160,163,167,168]",
              ok,
              "hc_max=" + std::to_string(vt.hc_max()) +
              " vc_max=" + std::to_string(vt.vc_max()) +
              " do=(" + std::to_string(vt.display_origin().hc) + "," +
              std::to_string(vt.display_origin().vc) + ")" +
              " int=(" + std::to_string(vt.int_position().hc) + "," +
              std::to_string(vt.int_position().vc) + ")");
    }

    // VT-T51-02 — init(MachineType) delegates through the canonical
    // typ_sel → tim_sel mapping: every constant identical between
    // init(type) and init_timing(default_machine_timing_for(type)).
    {
        bool ok = true;
        std::string detail;
        const MachineType types[] = {MachineType::ZX48K, MachineType::ZX128K,
                                     MachineType::ZX_PLUS3,
                                     MachineType::ZXN_ISSUE2};
        for (MachineType t : types) {
            VideoTiming a, b;
            a.init(t);
            b.init_timing(default_machine_timing_for(t));
            if (a.hc_max() != b.hc_max() || a.vc_max() != b.vc_max() ||
                a.display_origin().hc != b.display_origin().hc ||
                a.display_origin().vc != b.display_origin().vc ||
                a.int_position().hc != b.int_position().hc ||
                a.int_position().vc != b.int_position().vc) {
                ok = false;
                detail = "mismatch for type " +
                         std::to_string(static_cast<int>(t));
                break;
            }
        }
        check("VT-T51-02",
              "init(MachineType) == init_timing(default_machine_timing_for) "
              "for all four machine types (delegation preserves the "
              "pre-Task-51 constants exactly)",
              ok, detail);
    }

    // VT-T51-03 — 60 Hz override applies on the 128K branch but is
    // ignored on Pentagon (no i_50_60 split in the :150-168 block).
    {
        VideoTiming vt128, vtp;
        vt128.init_timing(MachineTimingMode::Timing128, /*refresh_60hz=*/true);
        vtp.init_timing(MachineTimingMode::TimingPentagon,
                        /*refresh_60hz=*/true);
        const bool ok = vt128.vc_max() == 263 && vt128.refresh_60hz() &&
                        vtp.vc_max() == 319 && !vtp.refresh_60hz();
        check("VT-T51-03",
              "init_timing 60Hz: 128K honours (vc_max=263, flag set); "
              "Pentagon ignores (vc_max=319, flag clear) "
              "[zxula_timing.vhd:214-308 vs :150-168]",
              ok,
              "128: vc_max=" + std::to_string(vt128.vc_max()) + " 60hz=" +
              std::to_string(vt128.refresh_60hz()) +
              "; pent: vc_max=" + std::to_string(vtp.vc_max()) + " 60hz=" +
              std::to_string(vtp.refresh_60hz()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 11 — Task 56: runtime NR 0x05 bit-2 (`nr_05_5060`) 50/60 Hz
// frame-edge commit.
// VHDL: zxnext.vhd:6697-6700 — `eff_nr_05_5060 <= nr_05_5060` latched
//       only when `video_frame_sync = '1'` (comment at :6692: "changes
//       to video timing occur during vsync"); :6720 feeds it to
//       zxula_timing's `i_50_60`.
//       zxnext.vhd:5834-5842 — the pending `nr_05_5060` FF: Pentagon
//       timing forces '0' continuously (priority IF, :5835-5836), else
//       NR 0x05 write bit 2 (:5837-5838), else F3 hotkey toggle
//       (:5839-5840).
//       60 Hz constants (128K branch): zxula_timing.vhd:233 (c_int_v=0),
//       :237 (c_min_vactive=40), :238 (c_max_vc=263). hc-axis constants
//       are identical between 50 and 60 Hz.
//
// Harness: full Emulator (ZX128K — 128K timing), NR writes via the real
// port path so the production NR 0x05 / NR 0x03 write handlers fire.
// ══════════════════════════════════════════════════════════════════════

namespace t56 {

static bool build_emu_128k(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX128K;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Write NR via the real port path (OUT 0x243B,reg; OUT 0x253B,val).
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

}  // namespace t56

static void section11_t56_nr05_5060() {
    set_group("VT-S11-T56-NR05-5060");

    // VT-T56-01 / VT-T56-02 — runtime NR 0x05 bit-2 write re-initialises
    // the effective frame geometry at the NEXT frame edge (never
    // mid-frame), in both directions.
    {
        Emulator emu;
        if (!t56::build_emu_128k(emu)) {
            check("VT-T56-01", "Emulator::init(ZX128K) failed", false,
                  "Emulator::init returned false");
            check("VT-T56-02", "Emulator::init(ZX128K) failed", false,
                  "Emulator::init returned false");
        } else {
            // Pre: 128K 50 Hz (zxula_timing.vhd:199,203,204).
            const bool pre_ok =
                emu.video_timing().vc_max() == 310 &&
                emu.video_timing().display_origin().vc == 64 &&
                emu.video_timing().int_position().vc == 1 &&
                !emu.video_timing().refresh_60hz();

            t56::nr_write(emu, 0x05, 0x04);  // bit 2 = 1 (60 Hz pending)
            // Pending only — VHDL latches eff_nr_05_5060 at
            // video_frame_sync (zxnext.vhd:6697-6700); nothing changes
            // before run_frame().
            const bool mid_ok =
                emu.video_timing().vc_max() == 310 &&
                !emu.video_timing().refresh_60hz();

            emu.run_frame();  // frame edge commits + re-push
            const bool post_ok =
                emu.video_timing().vc_max() == 263 &&
                emu.video_timing().display_origin().vc == 40 &&
                emu.video_timing().int_position().hc == 128 &&
                emu.video_timing().int_position().vc == 0 &&
                emu.video_timing().refresh_60hz();

            check("VT-T56-01",
                  "runtime NR 0x05 bit-2 write (50→60 Hz) re-pushes video "
                  "timing at the frame edge: vc_max 310→263, display "
                  "origin vc 64→40, INT (128,1)→(128,0); deferred until "
                  "run_frame [zxnext.vhd:6697-6700; "
                  "zxula_timing.vhd:233,237,238]",
                  pre_ok && mid_ok && post_ok,
                  std::string("pre=") + std::to_string(pre_ok) +
                  " mid=" + std::to_string(mid_ok) +
                  " post=" + std::to_string(post_ok) +
                  " (vc_max=" + std::to_string(emu.video_timing().vc_max()) +
                  " 60hz=" +
                  std::to_string(emu.video_timing().refresh_60hz()) + ")");

            // Reverse direction: 60 → 50 Hz, same deferred semantics.
            t56::nr_write(emu, 0x05, 0x00);  // bit 2 = 0 (50 Hz pending)
            const bool mid2_ok =
                emu.video_timing().vc_max() == 263 &&
                emu.video_timing().refresh_60hz();

            emu.run_frame();
            const bool post2_ok =
                emu.video_timing().vc_max() == 310 &&
                emu.video_timing().display_origin().vc == 64 &&
                emu.video_timing().int_position().vc == 1 &&
                !emu.video_timing().refresh_60hz();

            check("VT-T56-02",
                  "runtime NR 0x05 bit-2 clear (60→50 Hz) restores 128K "
                  "50 Hz geometry at the frame edge: vc_max 263→310, INT "
                  "back to (128,1); deferred until run_frame "
                  "[zxnext.vhd:6697-6700; zxula_timing.vhd:199,203,204]",
                  mid2_ok && post2_ok,
                  std::string("mid=") + std::to_string(mid2_ok) +
                  " post=" + std::to_string(post2_ok) +
                  " (vc_max=" + std::to_string(emu.video_timing().vc_max()) +
                  " 60hz=" +
                  std::to_string(emu.video_timing().refresh_60hz()) + ")");
        }
    }

    // VT-T56-03 — Pentagon gate: entering Pentagon timing clears the
    // pending `nr_05_5060` FF (VHDL zxnext.vhd:5835-5836 forces '0'
    // continuously while nr_03_machine_timing(2)='1'), so 60 Hz does
    // NOT resurrect when Pentagon timing is later exited.
    {
        Emulator emu;
        if (!t56::build_emu_128k(emu)) {
            check("VT-T56-03", "Emulator::init(ZX128K) failed", false,
                  "Emulator::init returned false");
        } else {
            t56::nr_write(emu, 0x05, 0x04);
            emu.run_frame();
            const bool at_60 = emu.video_timing().vc_max() == 263 &&
                               emu.video_timing().refresh_60hz();

            // tim_sel → Pentagon (bit7=1, tim=100, typ=010 unchanged).
            t56::nr_write(emu, 0x03, 0xC2);
            emu.run_frame();
            // Pentagon block sits outside the i_50_60 split
            // (zxula_timing.vhd:150-168) — always 50 Hz.
            const bool pent_ok = emu.video_timing().vc_max() == 319 &&
                                 !emu.video_timing().refresh_60hz();

            // Back to 128K timing: the FF was force-cleared on Pentagon
            // entry, so the machine returns at 50 Hz, not 60.
            t56::nr_write(emu, 0x03, 0xA2);
            emu.run_frame();
            const bool back_50 = emu.video_timing().vc_max() == 310 &&
                                 !emu.video_timing().refresh_60hz();

            check("VT-T56-03",
                  "Pentagon entry clears the pending 5060 FF: 128K@60Hz → "
                  "Pentagon (vc_max=319, 50 Hz) → back to 128K at 50 Hz "
                  "(vc_max=310), 60 Hz does not resurrect "
                  "[zxnext.vhd:5835-5836; zxula_timing.vhd:150-168]",
                  at_60 && pent_ok && back_50,
                  std::string("at_60=") + std::to_string(at_60) +
                  " pent=" + std::to_string(pent_ok) +
                  " back_50=" + std::to_string(back_50) +
                  " (vc_max=" + std::to_string(emu.video_timing().vc_max()) +
                  " 60hz=" +
                  std::to_string(emu.video_timing().refresh_60hz()) + ")");
        }
    }

    // VT-T56-04 — save/load round-trips the 50/60 selection: a snapshot
    // taken at 60 Hz restores 60 Hz geometry into a fresh (50 Hz)
    // emulator. load_state re-pushes video timing from the restored
    // NR 0x05 cache (the pending FF; snapshots are frame-edge, where
    // pending == effective per zxnext.vhd:6697-6700).
    {
        Emulator emu1;
        if (!t56::build_emu_128k(emu1)) {
            check("VT-T56-04", "Emulator::init(ZX128K) #1 failed", false,
                  "Emulator::init returned false");
        } else {
            t56::nr_write(emu1, 0x05, 0x04);
            emu1.run_frame();
            const bool src_60 = emu1.video_timing().refresh_60hz() &&
                                emu1.video_timing().vc_max() == 263;

            StateWriter measure;
            emu1.save_state(measure);
            std::vector<uint8_t> buf(measure.position(), 0);
            StateWriter w(buf.data(), buf.size());
            emu1.save_state(w);

            Emulator emu2;
            if (!t56::build_emu_128k(emu2)) {
                check("VT-T56-04", "Emulator::init(ZX128K) #2 failed",
                      false, "second Emulator::init returned false");
            } else {
                const bool dst_pre_50 = !emu2.video_timing().refresh_60hz() &&
                                        emu2.video_timing().vc_max() == 310;
                StateReader r(buf.data(), buf.size());
                emu2.load_state(r);
                const bool dst_60 = emu2.video_timing().refresh_60hz() &&
                                    emu2.video_timing().vc_max() == 263 &&
                                    emu2.video_timing().int_position().vc == 0;
                check("VT-T56-04",
                      "save/load round-trips 60 Hz: snapshot at 60 Hz "
                      "restores vc_max=263, INT vc=0, refresh_60hz into a "
                      "fresh 50 Hz emulator (load_state re-push from the "
                      "restored NR 0x05 cache) [zxnext.vhd:6697-6700]",
                      src_60 && dst_pre_50 && dst_60,
                      std::string("src_60=") + std::to_string(src_60) +
                      " dst_pre_50=" + std::to_string(dst_pre_50) +
                      " dst_60=" + std::to_string(dst_60) +
                      " (vc_max=" +
                      std::to_string(emu2.video_timing().vc_max()) + ")");
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 12 — GH #237: the boot-time raster/interrupt geometry comes
// from the NR 0x03 tim_sel axis, not from the CLI machine type.
//
// VHDL derivation (every link checked, none inferred from the C++):
//   * zxnext.vhd:6721   — zxula_timing's `i_timing` port is driven by
//                         `eff_nr_03_machine_timing` and by nothing else.
//                         `machine_type_*` (typ_sel, :5741-5757) reaches
//                         the raster generator through no path at all.
//   * zxnext.vhd:6703   — that latch loads from `nr_03_machine_timing`
//                         at every `video_frame_sync`.
//   * zxnext.vhd:1099   — `nr_03_machine_timing := "011"`, an initialiser
//                         only; :1377 gives the eff copy the same "011".
//   * zxnext.vhd:4926-5111 — the master reset block contains NO
//                         assignment to it, so it survives hard AND soft
//                         reset; only the gated NR 0x03 write at
//                         :5124-5135 changes it.
//   ⇒ the Next boots with i_timing = "011": zxula_timing takes the
//     `i_timing(1)='1'` branch (:178) and, inside it, the
//     `i_timing(0)='1'` arm (:189) — c_int_h = 136+2-12 = 126, NOT the
//     128 of the `i_timing(0)='0'` arm at :187.
//
// The 128K and +3 arms differ in c_int_h and in NOTHING else: c_max_hc
// (:196), c_max_vc (:204), c_min_hactive (:195), c_min_vactive (:203),
// c_int_v (:199) and the whole blank/sync/hdmi group sit outside the
// `if i_timing(0)` — at 50 Hz (:182-204) and at 60 Hz (:216-238) alike.
// So the fix must move the interrupt position and must move nothing else.
// ══════════════════════════════════════════════════════════════════════

namespace gh237 {

static bool build_emu(Emulator& emu, MachineType type) {
    EmulatorConfig cfg;
    cfg.type = type;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Write NR via the real port path, so the production NR 0x03 / NR 0x02
// handlers fire exactly as a guest's OUT would make them.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

static std::string geom(const Emulator& emu) {
    return "hc_max=" + std::to_string(emu.video_timing().hc_max()) +
           " vc_max=" + std::to_string(emu.video_timing().vc_max()) +
           " int=(" + std::to_string(emu.video_timing().int_position().hc) +
           "," + std::to_string(emu.video_timing().int_position().vc) + ")" +
           " px/line=" + std::to_string(emu.timing().pixels_per_line) +
           " lines=" + std::to_string(emu.timing().lines_per_frame);
}

}  // namespace gh237

static void section12_gh237_init_from_nr03() {
    set_group("VT-S12-GH237-INIT-FROM-NR03");

    // Every row below folds "did init() succeed" into its own condition
    // rather than emitting a second check() under the same ID from a guard
    // branch: a row is one check() call, so the traceability matrix records
    // the row's real description instead of an init-failure message.

    // VT-GH237-01 — the headline defect. A Next cold boot must place the
    // ULA interrupt on the +3 arm.
    {
        Emulator   emu;
        const bool built = gh237::build_emu(emu, MachineType::ZXN_ISSUE2);
        const RasterPos p =
            built ? emu.video_timing().int_position() : RasterPos{0, 0};
        check("VT-GH237-01",
              "Next cold boot: ULA interrupt at (126, 1) — the "
              "i_timing(0)='1' arm the \"011\" power-on tim_sel selects, not "
              "the 128 of the 128K arm "
              "[zxnext.vhd:1099,:6721; zxula_timing.vhd:186-190,199]",
              built && p.hc == 126 && p.vc == 1,
              built ? gh237::geom(emu) : std::string("init(ZXN) failed"));
    }

    // VT-GH237-02 — the SOURCE, not just the number. The boot geometry has
    // to be what the NR 0x03 tim_sel field decodes to, and must NOT be what
    // the CLI-machine-type mapping decodes to — those two disagree for the
    // Next, which is the whole defect.
    {
        Emulator   emu;
        const bool built = gh237::build_emu(emu, MachineType::ZXN_ISSUE2);
        const uint8_t tim = built ? emu.nextreg().nr_03_machine_timing() : 0xFF;
        VideoTiming from_reg;
        from_reg.init_timing(decode_nr_03_machine_timing(tim));
        VideoTiming from_cli;
        from_cli.init_timing(
            default_machine_timing_for(MachineType::ZXN_ISSUE2));
        const RasterPos p =
            built ? emu.video_timing().int_position() : RasterPos{0, 0};
        const bool tracks_reg = p.hc == from_reg.int_position().hc &&
                                p.vc == from_reg.int_position().vc;
        // Guard the premise: were the two axes ever to agree the row would
        // be vacuous, so assert that they still disagree.
        const bool axes_disagree =
            from_reg.int_position().hc != from_cli.int_position().hc;
        check("VT-GH237-02",
              "Next cold boot: the interrupt position tracks the NR 0x03 "
              "tim_sel register (\"011\") and NOT "
              "default_machine_timing_for(cfg.type), which still says 128K "
              "— the two disagree, and the register wins "
              "[zxnext.vhd:6721 i_timing <= eff_nr_03_machine_timing]",
              built && tim == 0x03 && axes_disagree && tracks_reg,
              "tim_sel=" + std::to_string(tim) + " reg_int_hc=" +
                  std::to_string(from_reg.int_position().hc) +
                  " cli_int_hc=" +
                  std::to_string(from_cli.int_position().hc) +
                  " actual_int_hc=" + std::to_string(p.hc));
    }

    // VT-GH237-03 — only c_int_h moved. Everything else in the 128K/+3
    // branch is shared between the two arms, so a fix that also shifted the
    // frame envelope or the display origin would be wrong even though the
    // interrupt landed right. The master-cycle frame is asserted in the same
    // row because it is derived from the same constants and must not drift.
    {
        Emulator   emu;
        const bool built = gh237::build_emu(emu, MachineType::ZXN_ISSUE2);
        bool ok = false;
        if (built) {
            const auto  p = emu.video_timing().int_position();
            const auto  o = emu.video_timing().display_origin();
            const auto& t = emu.timing();
            ok = p.hc == 126 && p.vc == 1 &&
                 emu.video_timing().hc_max() == 455 &&
                 emu.video_timing().vc_max() == 310 && o.hc == 136 &&
                 o.vc == 64 && t.pixels_per_line == 456 &&
                 t.lines_per_frame == 311 && t.tstates_per_line == 228 &&
                 t.tstates_per_frame == 228 * 311;
        }
        check("VT-GH237-03",
              "Next cold boot: c_int_h is the ONLY constant that moves — "
              "c_max_hc 455, c_max_vc 310, origin (136,64) and the 456x311 "
              "master-cycle frame are shared by the 128K and +3 arms "
              "[zxula_timing.vhd:195,196,203,204, all outside the "
              "if i_timing(0) at :186-190]",
              ok, built ? gh237::geom(emu) : std::string("init(ZXN) failed"));
    }

    // VT-GH237-04 — soft reset. A guest moves the timing axis to +3 on a
    // 128K machine; RESET_SOFT must not put the raster back on the CLI
    // machine's arm. VHDL: no reset clause for nr_03_machine_timing
    // (:4926-5111). i_timing is eff_nr_03_machine_timing (:6721), loaded
    // from it only at video_frame_sync (:6696-6703) and not reset either:
    // the frame the reset lands in stays on the 128K arm, and the +3 arm
    // takes over at the next frame edge (GH #263 follow-up — this row used
    // to assert +3 straight after the reset).
    {
        Emulator   emu;
        const bool built = gh237::build_emu(emu, MachineType::ZX128K);
        RasterPos  boot{0, 0}, post{0, 0}, edge{0, 0};
        if (built) {
            boot = emu.video_timing().int_position();
            // bit7=1 arms the tim_sel commit (:5124), bit3=0 leaves
            // user_dt_lock clear, and bits[2:0]="000" is VHDL's no-change
            // for typ_sel (:5143), so only the timing axis moves.
            gh237::nr_write(emu, 0x03, 0xB0);   // tim_sel = "011" (+3)
            gh237::nr_write(emu, 0x02, 0x01);   // RESET_SOFT
            post = emu.video_timing().int_position();
            emu.run_frame();                    // the next video_frame_sync
            edge = emu.video_timing().int_position();
        }
        check("VT-GH237-04",
              "128K + guest-selected +3 timing: RESET_SOFT neither reverts "
              "the raster to the CLI machine's arm nor applies the pending "
              "+3 early — INT stays at 128 until the next frame edge, then "
              "moves to the +3 arm (126) [zxnext.vhd:1099 no reset clause; "
              ":6696-6703,6721 eff_nr_03_machine_timing; zxula_timing.vhd:187,189]",
              built && boot.hc == 128 && boot.vc == 1 && post.hc == 128 &&
                  post.vc == 1 && edge.hc == 126 && edge.vc == 1,
              "boot_int_hc=" + std::to_string(boot.hc) +
                  " post_reset_int_hc=" + std::to_string(post.hc) +
                  " post_edge " +
                  (built ? gh237::geom(emu) : std::string("init failed")));
    }

    // VT-GH237-05 — the counterpart guard, and the reason the NextReg
    // reset-clobber had to go with this change. A 48K machine that
    // soft-resets with nobody having touched NR 0x03 must come back on 48K
    // constants. With the raster reading tim_sel but reset() still rewriting
    // tim_sel to "011", a plain 48K soft reset silently acquires +3 timing.
    {
        Emulator   emu;
        const bool built = gh237::build_emu(emu, MachineType::ZX48K);
        bool ok = false;
        if (built) {
            gh237::nr_write(emu, 0x02, 0x01);   // RESET_SOFT
            const auto  p = emu.video_timing().int_position();
            const auto& t = emu.timing();
            ok = p.hc == 116 && p.vc == 0 &&
                 emu.video_timing().hc_max() == 447 &&
                 emu.video_timing().vc_max() == 311 &&
                 t.pixels_per_line == 448 && t.lines_per_frame == 312;
        }
        check("VT-GH237-05",
              "48K RESET_SOFT with no guest NR 0x03 write stays on 48K "
              "constants: INT (116,0) and 448x312 in BOTH VideoTiming and "
              "the master-cycle frame [zxula_timing.vhd:257,261,262,265]",
              ok, built ? gh237::geom(emu) : std::string("init(48K) failed"));
    }

    // VT-GH237-06 — the latch itself, asserted directly rather than through
    // a consumer. nr_03_machine_timing has no reset clause anywhere in
    // zxnext.vhd, so a soft reset must not rewrite it to the "011" power-on
    // initialiser.
    {
        Emulator   emu;
        const bool built = gh237::build_emu(emu, MachineType::ZX48K);
        uint8_t boot = 0xFF, post = 0xFF;
        if (built) {
            boot = emu.nextreg().nr_03_machine_timing();
            gh237::nr_write(emu, 0x02, 0x01);   // RESET_SOFT
            post = emu.nextreg().nr_03_machine_timing();
        }
        check("VT-GH237-06",
              "NR 0x03 tim_sel survives RESET_SOFT — it is a plain FF with "
              "an initial value only and appears nowhere in the master reset "
              "block [zxnext.vhd:1099, :4926-5111]",
              built && boot == 0x01 && post == 0x01,
              "boot=" + std::to_string(boot) + " post_reset=" +
                  std::to_string(post));
    }

    // VT-GH237-07 — coherence under a preserved mode that changes the frame
    // ENVELOPE, not just the interrupt position. Pentagon is the only
    // tim_sel value whose c_max_hc/c_max_vc differ from the 128K/+3 pair, so
    // it is the case that proves VideoTiming and the master-cycle frame
    // geometry are programmed from one source and cannot disagree.
    // GH #263 follow-up: the Pentagon constants take over at the frame edge
    // after the reset, not at the reset (eff_nr_03_machine_timing,
    // zxnext.vhd:6696-6703,6721); the reset frame keeps the 128K geometry.
    {
        Emulator   emu;
        const bool built = gh237::build_emu(emu, MachineType::ZX128K);
        bool reset_ok = false, ok = false;
        std::string at_reset;
        if (built) {
            gh237::nr_write(emu, 0x03, 0xC0);   // tim_sel = "100" (Pentagon)
            gh237::nr_write(emu, 0x02, 0x01);   // RESET_SOFT
            at_reset = gh237::geom(emu);
            reset_ok = emu.video_timing().hc_max() == 455 &&
                       emu.video_timing().vc_max() == 310 &&
                       emu.timing().lines_per_frame == 311;
            emu.run_frame();                    // the next video_frame_sync
            const auto  p = emu.video_timing().int_position();
            const auto& t = emu.timing();
            ok = emu.video_timing().hc_max() == 447 &&
                 emu.video_timing().vc_max() == 319 && p.hc == 439 &&
                 p.vc == 319 && t.pixels_per_line == 448 &&
                 t.lines_per_frame == 320 && t.tstates_per_frame == 224 * 320;
        }
        check("VT-GH237-07",
              "128K + guest-selected Pentagon timing across RESET_SOFT: the "
              "reset frame keeps the 128K geometry, and from the next frame "
              "edge VideoTiming AND the master-cycle frame both follow the "
              "preserved tim_sel (448x320, INT (439,319)) — one source, no "
              "drift [zxnext.vhd:6696-6703,6721; "
              "zxula_timing.vhd:155,159,160,163,167,168,196,204]",
              reset_ok && ok,
              built ? "at reset " + at_reset + "; after edge " + gh237::geom(emu)
                    : std::string("init(128K) failed"));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 13 — GH #257: the line interrupt and NR 0x1E/0x1F cvc readback
// on the hc_ula/cvc line, not the raw hc 0 line.
//
// VHDL: zxula_timing.vhd:423-436 (hc_ula reset REGISTERED on hc ==
//       c_min_hactive - 12, so hc_ula == 0 at raw hc c_min_hactive - 11);
//       :457-470 (cvc steps on the same ula_max_hc pulse);
//       :560-583 (line-int pulse when hc_ula == 255 and cvc == int_line_num,
//       "occurs before the line is drawn");
//       zxnext.vhd:5982-5986 (NR 0x1E/0x1F read cvc).
// jnext used to anchor the line-int at raw hc 0 of the firing line (~380
// pixels early) and to step the NR 0x1F cvc at raw hc 0 (~125 early).
// ══════════════════════════════════════════════════════════════════════

static void section13_gh257_line_int_hc() {
    set_group("VT-S13-GH257-LINE-INT-HC");

    // VT-GH257-01 — Next (128K-class slot, 456 x 311): target 208 →
    // int_line_num 207 → raw line (207 + 64) mod 311 = 271; hc_ula 255 at raw
    // hc 125 + 255 = 380. Offset = (271*456 + 380) * 4 = 495824.
    {
        VideoTiming vt;
        vt.init(MachineType::ZXN_ISSUE2);
        vt.set_line_interrupt_target(208);
        const uint64_t got = vt.line_int_master_cycle_offset();
        check("VT-GH257-01",
              "Next: line-int target 208 fires at raw (271, hc 380) = "
              "(271*456+380)*4 = 495824 master cycles, hc_ula 255 not raw hc 0 "
              "(zxula_timing.vhd:423-436,566-570,577)",
              got == 495824ULL, "got " + std::to_string(got));
    }

    // VT-GH257-02 — 48K timing (448 x 312, c_min_hactive 128): hc_ula 255 at
    // raw hc 117 + 255 = 372; raw line (207 + 64) mod 312 = 271.
    // Offset = (271*448 + 372) * 4 = 487120.
    {
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::Timing48);
        vt.set_line_interrupt_target(208);
        const uint64_t got = vt.line_int_master_cycle_offset();
        check("VT-GH257-02",
              "48K timing: line-int target 208 fires at raw (271, hc 372) = "
              "(271*448+372)*4 = 487120 (zxula_timing.vhd:257-270,423-436,577)",
              got == 487120ULL, "got " + std::to_string(got));
    }

    // VT-GH257-03 — Pentagon (448 x 320, c_min_hactive 128, c_min_vactive 80):
    // raw line (207 + 80) mod 320 = 287, hc 372.
    // Offset = (287*448 + 372) * 4 = 515792.
    {
        VideoTiming vt;
        vt.init_timing(MachineTimingMode::TimingPentagon);
        vt.set_line_interrupt_target(208);
        const uint64_t got = vt.line_int_master_cycle_offset();
        check("VT-GH257-03",
              "Pentagon timing: line-int target 208 fires at raw (287, hc 372) "
              "= (287*448+372)*4 = 515792 (zxula_timing.vhd:155-168,423-436,577)",
              got == 515792ULL, "got " + std::to_string(got));
    }

    // VT-GH257-04 — target 0 → int_line_num = c_max_vc = 310 (Next):
    // raw line (310 + 64) mod 311 = 63, hc 380.
    // Offset = (63*456 + 380) * 4 = 116432.
    {
        VideoTiming vt;
        vt.init(MachineType::ZXN_ISSUE2);
        vt.set_line_interrupt_target(0);
        const uint64_t got = vt.line_int_master_cycle_offset();
        check("VT-GH257-04",
              "Next: line-int target 0 (int_line_num = c_max_vc = 310) fires at "
              "raw (63, hc 380) = (63*456+380)*4 = 116432 "
              "(zxula_timing.vhd:566-570,577)",
              got == 116432ULL, "got " + std::to_string(got));
    }

    // VT-GH257-05 — production wiring: the running emulator raises the
    // line interrupt in the instruction that crosses master cycle 495824
    // (VT-GH257-01), not in the one crossing raw hc 0 of line 271
    // (271*1824 = 494304, 1520 master cycles = 15 JR loops earlier).
    {
        Emulator emu;
        bool ok = false;
        std::string detail = "build_emulator returned false";
        if (g163::build_emulator(emu)) {
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();
            g163::nr_write(emu, 0x23, 208);
            g163::nr_write(emu, 0x22, 0x02);
            uint64_t before = 0, after = 0;
            for (int i = 0; i < 20000 && emu.line_int_fire_count() == 0; ++i) {
                before = emu.clock().get();
                emu.execute_single_instruction();
                after = emu.clock().get();
            }
            constexpr uint64_t fire = 495824ULL;
            ok = emu.line_int_fire_count() == 1 && before < fire && fire <= after;
            detail = "fired in step (" + std::to_string(before) + ", "
                   + std::to_string(after) + "], expected to contain "
                   + std::to_string(fire) + ", count="
                   + std::to_string(emu.line_int_fire_count());
        }
        check("VT-GH257-05",
              "Emulator raises the target-208 line interrupt in the instruction "
              "crossing raw (271, hc 380), hc_ula 255 "
              "(zxula_timing.vhd:423-436,577; zxnext.vhd:6752-6758)",
              ok, detail);
    }

    // VT-GH257-06 — NR 0x1E/0x1F read cvc, which steps at hc_ula 0 (raw hc
    // 125 = master cycle 500 into the line), not at raw hc 0. On raw line 64
    // (c_min_vactive) cvc still holds raw line 63's value (63 - 64) mod 311 =
    // 310 up to master cycle 499, and reads 0 from 500. The clock is advanced
    // directly (no instruction), so the boundary is pinned to the exact
    // 28 MHz cycle: the readback is a pure function of (clock - frame start).
    {
        Emulator emu;
        bool ok = false;
        std::string detail = "build_emulator returned false";
        if (g163::build_emulator(emu)) {
            auto cvc_at = [&emu](uint64_t mc) {
                emu.clock().tick(static_cast<int>(mc - emu.clock().get()));
                return (g163::nr_read(emu, 0x1E) << 8) | g163::nr_read(emu, 0x1F);
            };
            constexpr uint64_t line64 = 64ULL * 1824;
            const uint64_t start = emu.clock().get();
            const int at0   = cvc_at(line64);          // raw hc 0
            const int at499 = cvc_at(line64 + 499);    // raw hc 124.75
            const int at500 = cvc_at(line64 + 500);    // raw hc 125
            ok = start == 0 && at0 == 310 && at499 == 310 && at500 == 0;
            detail = "start=" + std::to_string(start)
                   + " cvc@hc0=" + std::to_string(at0)
                   + " cvc@mc499=" + std::to_string(at499)
                   + " cvc@mc500=" + std::to_string(at500)
                   + " (want 310, 310, 0)";
        }
        check("VT-GH257-06",
              "NR 0x1E/0x1F cvc steps at hc_ula 0 (raw hc 125, master cycle "
              "500): raw line 64 reads 310 up to cycle 499 and 0 from 500 "
              "(zxula_timing.vhd:423-436,457-470; zxnext.vhd:5982-5986)",
              ok, detail);
    }
}

// ── Section 14: GH #265 — NR 0x1E/0x1F sampled at the IN's I/O cycle ──
//
// An IN from 0x253B returns port_253b_dat_0 (zxnext.vhd:2819), reloaded from
// port_253b_dat on every CLK_CPU falling edge (:5871-5876); port_253b_dat
// follows cvc on CLK_28 (:5878-5882,5982-5986). The T80 latches the bus into DI_Reg on
// the falling edge of the I/O cycle's T3 (t80na.vhd:214-222); with IOWait=1
// the I/O cycle is four clocks (t80n.vhd:1781-1782, TStates=3 at
// t80n_mcode.vhd:235, T_Res at t80n.vhd:412), so DI_Reg takes the value
// reloaded on the previous falling edge, 2.5 T-states into the I/O cycle —
// the value cvc held just before that edge.
//
// Next timing (1824 master cycles/line, 8 per T-state at 3.5 MHz): cvc steps
// 310 -> 0 at raw line 64, master cycle 64*1824 + 500 = 117236 (VT-GH257-06).
// IN A,(C) = ED 78: two 4-T M1 cycles, then the I/O cycle, so its reload
// edge is 8*8 + 20 = 84 master cycles after the instruction starts; IN A,(n)
// = DB n: M1 4 T + operand read 3 T, edge at 7*8 + 20 = 76. Code at 0x8000
// (bank 2) and port 0x253B are uncontended, and the clock is moved directly,
// so the instruction's start is exactly the requested cycle.

namespace gh265 {

constexpr uint64_t kStep = 64ULL * 1824 + 500;   // cvc 310 -> 0

enum class Form { IN_A_C, IN_A_N };

// Execute one IN that reads NR @p reg, with the instruction starting at
// master cycle @p start. Returns the byte read (A). @p ok is false if the
// emulator could not be built or the clock was already past @p start.
static uint8_t in_nr_at(uint8_t reg, uint64_t start, Form form, bool& ok) {
    Emulator emu;
    ok = g163::build_emulator(emu) && emu.clock().get() <= start;
    if (!ok) return 0;
    emu.port().out(0x243B, reg);                  // select, outside the IN
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000;
    if (form == Form::IN_A_C) {
        emu.mmu().write(0x8000, 0xED);
        emu.mmu().write(0x8001, 0x78);            // IN A,(C)
        regs.BC = 0x253B;
    } else {
        emu.mmu().write(0x8000, 0xDB);
        emu.mmu().write(0x8001, 0x3B);            // IN A,(0x3B)
        regs.AF = static_cast<uint16_t>(0x2500 | (regs.AF & 0x00FF));
    }
    emu.cpu().set_registers(regs);
    emu.clock().tick(static_cast<int>(start - emu.clock().get()));
    emu.cpu().execute();
    return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
}

}  // namespace gh265

// ── Section 15 — in_display() is per-machine (GH #22) ─────────────────
//
// Found while deriving the debugger's raster indicator. `in_display()` tested
// the FIXED 48K constants DISPLAY_LEFT / DISPLAY_TOP instead of the live
// machine's c_min_hactive / c_min_vactive, so it answered 8 ticks off on
// 128K/+3 and 16 lines off on Pentagon. It had no callers, which is why no row
// had caught it — and it sits in the class the raster indicator now treats as
// the single source of per-machine raster constants.
static void section15_gh22_in_display_per_machine() {
    set_group("VT-S15-GH22-IN-DISPLAY");

    // 128K: the active area opens at c_min_hactive = 136, not 128.
    {
        VideoTiming t;
        t.init_timing(MachineTimingMode::Timing128);
        // Walk to (hc=130, vc=100): inside the 48K window, outside the 128K one.
        // One T-state is 2 pixel ticks, so a line is (c_max_hc+1)/2 T-states.
        t.advance(100 * (455 + 1) / 2 + 130 / 2);
        const bool at_130 = t.in_display();
        VideoTiming u;
        u.init_timing(MachineTimingMode::Timing128);
        u.advance(100 * (455 + 1) / 2 + 140 / 2);
        check("VT-GH22-01",
              "128K in_display() uses c_min_hactive=136, not the 48K 128 "
              "(zxula_timing.vhd:195)",
              !at_130 && u.in_display(),
              "hc130=" + std::to_string(int(at_130))
                  + " hc140=" + std::to_string(int(u.in_display())));
    }

    // Pentagon: the active area opens at c_min_vactive = 80, not 64.
    {
        VideoTiming t;
        t.init_timing(MachineTimingMode::TimingPentagon);
        t.advance(70 * (447 + 1) / 2 + 200 / 2);   // raw vc 70
        VideoTiming u;
        u.init_timing(MachineTimingMode::TimingPentagon);
        u.advance(90 * (447 + 1) / 2 + 200 / 2);   // raw vc 90
        check("VT-GH22-02",
              "Pentagon in_display() uses c_min_vactive=80, not the 48K 64 "
              "(zxula_timing.vhd:167)",
              !t.in_display() && u.in_display(),
              "vc70=" + std::to_string(int(t.in_display()))
                  + " vc90=" + std::to_string(int(u.in_display())));
    }
}

static void section14_gh265_nr_read_io_cycle() {
    set_group("VT-S14-GH265-NR-READ-IO-CYCLE");
    using gh265::Form;
    using gh265::in_nr_at;
    using gh265::kStep;

    // VT-GH265-01 — IN A,(C) of NR 0x1F starting 36 master cycles before the
    // step: its reload edge is 48 cycles after it, so it reads cvc 0.
    // Sampling at the instruction's start (pre-fix) reads 310 & 0xFF = 0x36.
    {
        bool ok = false;
        const uint8_t v = in_nr_at(0x1F, kStep - 36, Form::IN_A_C, ok);
        check("VT-GH265-01",
              "IN A,(C) of NR 0x1F starting before the cvc step samples at its "
              "I/O cycle: reads 0x00, not the start-of-instruction 0x36 "
              "(zxnext.vhd:2819,5871-5876,5985-5986; t80na.vhd:214-222)",
              ok && v == 0x00,
              "ok=" + std::to_string(ok) + " v=" + std::to_string(v)
              + " (want 0)");
    }

    // VT-GH265-02 — the same instruction reading NR 0x1E (cvc bit 8): 0 at
    // the I/O cycle, 1 (from 310) at the instruction's start.
    {
        bool ok = false;
        const uint8_t v = in_nr_at(0x1E, kStep - 36, Form::IN_A_C, ok);
        check("VT-GH265-02",
              "IN A,(C) of NR 0x1E starting before the cvc step reads cvc(8) "
              "at its I/O cycle: 0x00, not the start-of-instruction 0x01 "
              "(zxnext.vhd:2819,5871-5876,5982-5983)",
              ok && v == 0x00,
              "ok=" + std::to_string(ok) + " v=" + std::to_string(v)
              + " (want 0)");
    }

    // VT-GH265-03 — the sampling edge to the T-state. Starting 84 master
    // cycles before the step puts the reload edge exactly ON it: the latch
    // takes cvc as it was just before the edge, 310 (0x36). Starting one
    // T-state later puts the edge 8 cycles past it: 0.
    {
        bool ok1 = false, ok2 = false;
        const uint8_t on_edge = in_nr_at(0x1F, kStep - 84, Form::IN_A_C, ok1);
        const uint8_t after   = in_nr_at(0x1F, kStep - 76, Form::IN_A_C, ok2);
        check("VT-GH265-03",
              "IN A,(C) reload edge 2.5 T into the I/O cycle: edge on the cvc "
              "step reads the old line 0x36, one T-state later reads 0x00 "
              "(zxnext.vhd:5871-5876; t80na.vhd:214-222; t80n.vhd:1781-1782)",
              ok1 && ok2 && on_edge == 0x36 && after == 0x00,
              "on_edge=" + std::to_string(on_edge) + " after="
              + std::to_string(after) + " (want 54, 0)");
    }

    // VT-GH265-04 — IN A,(n) reaches its I/O cycle one T-state earlier (M1 +
    // operand read = 7 T, not 8): starting 76 before the step puts its edge
    // on the step (0x36), 68 before puts it 8 past (0x00). IN A,(C) started
    // 76 before reads 0x00 (VT-GH265-03), so the two forms are told apart.
    {
        bool ok1 = false, ok2 = false;
        const uint8_t on_edge = in_nr_at(0x1F, kStep - 76, Form::IN_A_N, ok1);
        const uint8_t after   = in_nr_at(0x1F, kStep - 68, Form::IN_A_N, ok2);
        check("VT-GH265-04",
              "IN A,(n) of NR 0x1F: I/O cycle after 7 T, edge on the cvc step "
              "reads 0x36, one T-state later 0x00 "
              "(zxnext.vhd:5871-5876; t80na.vhd:214-222)",
              ok1 && ok2 && on_edge == 0x36 && after == 0x00,
              "on_edge=" + std::to_string(on_edge) + " after="
              + std::to_string(after) + " (want 54, 0)");
    }

    // VT-GH265-05 — a polling loop, run by the emulator's own per-instruction
    // path: `IN A,(C) / OR A / JR NZ,loop`, 28 T (224 master cycles) a turn,
    // waiting for NR 0x1F to read 0. Started 708 cycles before the step, turn
    // k's IN starts at step - 708 + 224k and its edge falls 84 later: turn 3
    // (edge 48 past the step) is the first to read 0, so the loop leaves
    // after 4 INs, 12 instructions, with the clock at start + 3*224 + 23*8.
    // Sampling at the instruction's start the loop runs one more turn.
    {
        Emulator emu;
        bool ok = g163::build_emulator(emu);
        int steps = 0;
        uint64_t exit_clock = 0;
        const uint64_t start = kStep - 708;
        if (ok) {
            emu.port().out(0x243B, 0x1F);
            const uint8_t code[] = { 0xED, 0x78,        // 8000 IN A,(C)
                                     0xB7,              // 8002 OR A
                                     0x20, 0xFB,        // 8003 JR NZ,8000
                                     0x18, 0xFE };      // 8005 JR 8005
            for (int i = 0; i < 7; ++i)
                emu.mmu().write(static_cast<uint16_t>(0x8000 + i), code[i]);
            auto regs = emu.cpu().get_registers();
            regs.PC = 0x8000;
            regs.BC = 0x253B;
            emu.cpu().set_registers(regs);
            emu.clock().tick(static_cast<int>(start - emu.clock().get()));
            while (emu.cpu().pc() != 0x8005 && steps < 100) {
                emu.execute_single_instruction();
                ++steps;
            }
            exit_clock = emu.clock().get();
        }
        const uint64_t want = start + 3 * 224 + 23 * 8;
        check("VT-GH265-05",
              "a loop polling NR 0x1F for line 0 leaves on the first turn whose "
              "IN samples past the cvc step: 12 instructions, not 15 "
              "(zxnext.vhd:5871-5876,5985-5986; t80na.vhd:214-222)",
              ok && steps == 12 && exit_clock == want,
              "steps=" + std::to_string(steps) + " clock="
              + std::to_string(exit_clock) + " (want 12, "
              + std::to_string(want) + ")");
    }

    // VT-GH265-06 — the offset belongs to the instruction in progress only. A
    // read made outside any instruction (the harness here; the debugger's
    // NextREG panel in practice) samples at the clock itself, even right
    // after an IN has run: 8 master cycles before the step it reads 310.
    // A leftover in-instruction offset from the IN (12 T) would carry it
    // past the step.
    {
        Emulator emu;
        bool ok = g163::build_emulator(emu);
        uint8_t v = 0;
        if (ok) {
            emu.mmu().write(0x8000, 0xED);
            emu.mmu().write(0x8001, 0x78);            // IN A,(C)
            auto regs = emu.cpu().get_registers();
            regs.PC = 0x8000;
            regs.BC = 0x253B;
            emu.cpu().set_registers(regs);
            emu.cpu().execute();
            emu.clock().tick(static_cast<int>(kStep - 8 - emu.clock().get()));
            v = g163::nr_read(emu, 0x1F);
        }
        check("VT-GH265-06",
              "a NR 0x1F read outside any instruction samples at the clock, "
              "even after an IN has executed: 8 cycles before the step it "
              "reads 0x36 (zxnext.vhd:5985-5986; zxula_timing.vhd:457-470)",
              ok && v == 0x36,
              "ok=" + std::to_string(ok) + " v=" + std::to_string(v)
              + " (want 54)");
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 16 — GH #290: cvc reloads from NR 0x64 ONCE PER FRAME
// VHDL: zxula_timing.vhd:423-425 (ula_max_hc, ula_min_vactive), :457-470
//       (cvc <= '0' & i_cu_offset on the ula_max_hc pulse of line
//       c_min_vactive; +1 on every other line; wrap at c_max_vc), :577 (the
//       line-int compare on cvc); zxnext.vhd:5442 (NR 0x64 write), :6090
//       (readback, last-write-wins), :6723 (i_cu_offset => the register),
//       :5982-5986 (NR 0x1E/0x1F read cvc), :3950 (the Copper's vcount_i).
//
// jnext applied the register to cvc at once: a guest that wrote NR 0x64 and
// then polled NR 0x1E/0x1F saw the new offset a frame early, and the line
// interrupt was placed against whichever value it last looked at. The VT-25
// row above sets the offset before any line runs, so it saw neither side.
//
// Harness: ZXN_ISSUE2 (128K/Next timing, 50 Hz: c_min_vactive 64, c_max_vc
// 310, c_min_hactive 136 — zxula_timing.vhd:195,203,204 — so hc_ula reads 0
// at raw hc 125 and the reload is at raw (64, 125)) parked in a `JR $` loop
// with interrupts off. Unlike Section 8 it RUNS FRAMES: the reload is a
// per-frame scheduler event, which only a frame begun by run_frame() has.
// Positions are reached with the debugger's run-to-cycle, which stops at the
// first instruction boundary at or after the target — at most one `JR $`
// turn (96 master cycles, 24 pixels) late, so every target below sits at
// least 24 pixels clear of the hc_ula 0 seam.
// ══════════════════════════════════════════════════════════════════════

namespace gh290 {

static bool build(Emulator& emu, int rewind_frames = 0) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = rewind_frames;
    if (!emu.init(cfg)) return false;
    g163::install_jr_self_loop(emu);
    emu.debug_state().set_clients_attached(true);   // was set_active(true): GH #278 WP4c
    emu.debug_state().set_live_raster(true);
    emu.run_frame();   // one whole frame: every later one is begun by run_frame()
    return true;
}

// The master cycle of raw (line, px) in the frame that starts at `base`.
static uint64_t at(const Emulator& emu, uint64_t base, int line, int px) {
    return base
         + static_cast<uint64_t>(line) * emu.timing().master_cycles_per_line
         + static_cast<uint64_t>(px) * 4;
}

static bool run_to(Emulator& emu, uint64_t target) {
    for (int i = 0; i < 16 && emu.clock().get() < target; ++i) {
        emu.debug_state().run_to_cycle(target);
        emu.run_frame();
    }
    return emu.clock().get() >= target;
}

// cvc as a guest reads it: NR 0x1E bit 0 is cvc(8), NR 0x1F cvc(7:0).
static int read_cvc(Emulator& emu) {
    const int hi = g163::nr_read(emu, 0x1E) & 0x01;
    const int lo = g163::nr_read(emu, 0x1F);
    return (hi << 8) | lo;
}

// The VHDL relation for the part of raw line `line` from hc_ula 0 on, with
// cvc counting from `off`: (line - c_min_vactive + off) mod (c_max_vc + 1).
static int cvc_of(const Emulator& emu, int line, int off) {
    const int lpf  = emu.video_timing().vc_max() + 1;
    const int minv = emu.video_timing().display_origin().vc;
    return ((line - minv + off) % lpf + lpf) % lpf;
}

static std::string gw(const char* what, long got, long want) {
    return std::string(what) + "=" + std::to_string(got) + " (want "
         + std::to_string(want) + ") ";
}

static std::vector<uint8_t> stream_of(Emulator& e) {
    StateWriter measure;
    e.save_state(measure);
    std::vector<uint8_t> buf(measure.position());
    StateWriter w(buf.data(), buf.size());
    e.save_state(w);
    return buf;
}

// A machine standing on the boundary between F1 and F2 whose F1 reloaded
// cvc from 10 (written at line 10, before the reload) and whose NR 0x64 was
// then set to 20 (at line 100, after it). So at the boundary cvc counts from
// 10 while the register holds 20 — the state a snapshot has to carry, with
// neither value 0, so no reset or fresh-machine default can stand in for it.
static bool build_split(Emulator& emu, int rewind_frames = 0) {
    if (!build(emu, rewind_frames)) return false;
    const uint64_t f1 = emu.current_frame_cycle();
    const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
    bool ok = run_to(emu, at(emu, f1, 10, 200));
    g163::nr_write(emu, 0x64, 10);
    ok = ok && run_to(emu, at(emu, f1, 100, 200));
    g163::nr_write(emu, 0x64, 20);
    // To the boundary: the run_frame loop ends F1 at its frame_end, before
    // any F2 instruction, and F2 is only begun by the next run_frame().
    ok = ok && run_to(emu, f2);
    return ok && emu.video_timing().cu_offset() == 10 &&
           g163::nr_read(emu, 0x64) == 20;
}

// `IN A,(C)` of NR 0x1F, started at master cycle `start` in a machine
// already built, through the full per-instruction path (so the frame's events
// run at its end). The clock is moved to `start` between instructions. The
// IN samples 84 cycles after it starts (VT-GH265-03); a `JR $` follows it.
static bool in_1f_at(Emulator& emu, uint64_t start, uint8_t& a) {
    if (emu.clock().get() > start) return false;
    emu.port().out(0x243B, 0x1F);
    emu.mmu().write(0x8000, 0xED);
    emu.mmu().write(0x8001, 0x78);            // IN A,(C)
    emu.mmu().write(0x8002, 0x18);
    emu.mmu().write(0x8003, 0xFE);            // JR $
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000;
    regs.BC = 0x253B;
    emu.cpu().set_registers(regs);
    emu.clock().tick(static_cast<int>(start - emu.clock().get()));
    emu.execute_single_instruction();
    a = static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
    return true;
}

}  // namespace gh290

static void section16_gh290_cvc_reload() {
    set_group("VT-S16-GH290-CVC-RELOAD");
    using gh290::at;
    using gh290::cvc_of;
    using gh290::gw;
    using gh290::read_cvc;
    using gh290::run_to;

    // VT-GH290-01..04 — an NR 0x64 write AFTER this frame's reload (raw line
    // 100 > 64). The readback keeps counting from the old offset for the rest
    // of the frame and through the next frame's lines before its reload, and
    // switches at exactly that reload; the register itself reads back at once.
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
        bool ok = built;
        int at_write = -1, rest = -1, nr64 = -1;
        int pre = -1, pre_seam = -1, post_seam = -1, post = -1;
        if (built) {
            ok = ok && run_to(emu, at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x64, 20);
            at_write = read_cvc(emu);
            nr64     = g163::nr_read(emu, 0x64);
            ok = ok && run_to(emu, at(emu, f1, 150, 200));
            rest = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, f2, 10, 200));
            pre = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, f2, 64, 60));    // before hc_ula 0
            pre_seam = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, f2, 64, 160));   // after it
            post_seam = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, f2, 100, 200));
            post = read_cvc(emu);
        }
        check("VT-GH290-01",
              "NR 0x64 written after the frame's cvc reload: NR 0x1E/0x1F keep "
              "counting from the old offset for the rest of the frame — cvc "
              "samples i_cu_offset only at ula_min_vactive "
              "(zxula_timing.vhd:457-466; zxnext.vhd:5982-5986)",
              ok && at_write == cvc_of(emu, 100, 0) && rest == cvc_of(emu, 150, 0),
              gw("at_write", at_write, cvc_of(emu, 100, 0))
                  + gw("line150", rest, cvc_of(emu, 150, 0)));
        check("VT-GH290-02",
              "…and through the next frame's lines before its ula_min_vactive "
              "line, including that line's own raw hc < c_min_hactive - 11 "
              "(zxula_timing.vhd:423-425,457-470)",
              ok && pre == cvc_of(emu, 10, 0) && pre_seam == cvc_of(emu, 63, 0),
              gw("line10", pre, cvc_of(emu, 10, 0))
                  + gw("line64@px60", pre_seam, cvc_of(emu, 63, 0)));
        check("VT-GH290-03",
              "…and count from the new offset from that line's hc_ula 0 on, the "
              "one reload of the frame (zxula_timing.vhd:457-462)",
              ok && post_seam == cvc_of(emu, 64, 20) && post == cvc_of(emu, 100, 20),
              gw("line64@px160", post_seam, cvc_of(emu, 64, 20))
                  + gw("line100", post, cvc_of(emu, 100, 20)));
        check("VT-GH290-04",
              "the NR 0x64 register itself reads the write back at once, while "
              "cvc has not taken it (zxnext.vhd:5442,6090)",
              ok && nr64 == 20, gw("nr64", nr64, 20));
    }

    // VT-GH290-05 — the other side of the condition: a write BEFORE the
    // frame's reload (raw line 10 < 64) is loaded by that reload, in the same
    // frame, while the lines before it still count from the old value.
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        bool ok = built;
        int before = -1, after = -1;
        if (built) {
            ok = ok && run_to(emu, at(emu, f1, 10, 200));
            g163::nr_write(emu, 0x64, 20);
            before = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, f1, 100, 200));
            after = read_cvc(emu);
        }
        check("VT-GH290-05",
              "NR 0x64 written before the frame's cvc reload: the lines before "
              "it keep the old offset, the reload loads the new one in the same "
              "frame (zxula_timing.vhd:457-462)",
              ok && before == cvc_of(emu, 10, 0) && after == cvc_of(emu, 100, 20),
              gw("line10", before, cvc_of(emu, 10, 0))
                  + gw("line100", after, cvc_of(emu, 100, 20)));
    }

    // VT-GH290-06/07 — the line interrupt compares cvc (zxula_timing.vhd:577),
    // so an NR 0x22/0x23 write after a mid-frame NR 0x64 write schedules
    // against the offset cvc is COUNTING from. Target 150 -> int_line_num 149
    // (:566-570): raw line 64 + 149 - off, i.e. 213 while cvc counts from 0,
    // 193 once it counts from 20. The pulse is at hc_ula 255 (raw hc 380).
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
        bool ok = built;
        uint64_t n1 = 99, n2 = 99, n3 = 99, n4 = 99;
        if (built) {
            ok = ok && run_to(emu, at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x64, 20);
            emu.reset_line_int_fire_count();
            g163::nr_write(emu, 0x23, 150);
            g163::nr_write(emu, 0x22, 0x02);
            ok = ok && run_to(emu, at(emu, f1, 200, 0));
            n1 = emu.line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f1, 220, 0));
            n2 = emu.line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f2, 200, 0));
            n3 = emu.line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f2, 220, 0));
            n4 = emu.line_int_fire_count();
        }
        check("VT-GH290-06",
              "NR 0x22/0x23 written after a mid-frame NR 0x64 write: the line "
              "interrupt fires on the line the RELOADED offset selects (raw "
              "213), not the register's (raw 193) (zxula_timing.vhd:462,577)",
              ok && n1 == 0 && n2 == 1,
              gw("by_line200", long(n1), 0) + gw("by_line220", long(n2), 1));
        check("VT-GH290-07",
              "…and in the next frame, after its reload, on the new offset's "
              "line only (raw 193, not also 213) (zxula_timing.vhd:457-462,577)",
              ok && n3 == 2 && n4 == 2,
              gw("by_f2_line200", long(n3), 2) + gw("by_f2_line220", long(n4), 2));
    }

    // VT-GH290-08 — the schedule a frame starts with places the lines after
    // its reload against the register as it then stands; an NR 0x64 write
    // BEFORE the reload makes that stale, and the reload re-derives it. F2
    // begins with cvc and the register both 0 (fire at raw 213); NR 0x64 = 20
    // at F2 line 10; the reload loads 20, so F2 fires at raw 193 and only there.
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
        bool ok = built;
        uint64_t n1 = 99, n2 = 99;
        if (built) {
            ok = ok && run_to(emu, at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x23, 150);
            g163::nr_write(emu, 0x22, 0x02);
            ok = ok && run_to(emu, at(emu, f2, 10, 200));
            g163::nr_write(emu, 0x64, 20);
            emu.reset_line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f2, 200, 0));
            n1 = emu.line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f2, 220, 0));
            n2 = emu.line_int_fire_count();
        }
        check("VT-GH290-08",
              "NR 0x64 written before the reload in a frame whose line interrupt "
              "is already scheduled: the reload re-derives it, so it fires on "
              "the reloaded offset's line (raw 193) and not the stale one (213) "
              "(zxula_timing.vhd:457-462,577)",
              ok && n1 == 1 && n2 == 1,
              gw("by_line200", long(n1), 1) + gw("by_line220", long(n2), 1));
    }

    // VT-GH290-09 — two offsets in one frame can REPEAT a cvc value, and
    // :577 is evaluated every pixel, so it fires twice. F1 reloads 10 (written
    // at line 10), NR 0x64 = 0 at line 100. Target 6 -> int_line_num 5. In F2
    // the lines before the reload count from 10 (cvc 5 at raw 59), the rest
    // from 0 (cvc 5 again at raw 69).
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
        bool ok = built;
        uint64_t n1 = 99, n2 = 99, n3 = 99;
        if (built) {
            ok = ok && run_to(emu, at(emu, f1, 10, 200));
            g163::nr_write(emu, 0x64, 10);
            ok = ok && run_to(emu, at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x64, 0);
            g163::nr_write(emu, 0x23, 6);
            g163::nr_write(emu, 0x22, 0x02);
            emu.reset_line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f2, 64, 60));
            n1 = emu.line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f2, 100, 0));
            n2 = emu.line_int_fire_count();
            ok = ok && run_to(emu, f2 + emu.timing().master_cycles_per_frame);
            n3 = emu.line_int_fire_count();
        }
        check("VT-GH290-09",
              "a frame whose cvc reloads a smaller offset repeats cvc values: "
              "the line interrupt fires on both (raw 59 counting from 10, raw "
              "69 counting from 0) and on no third line "
              "(zxula_timing.vhd:457-466,577)",
              ok && n1 == 1 && n2 == 2 && n3 == 2,
              gw("by_line64", long(n1), 1) + gw("by_line100", long(n2), 2)
                  + gw("by_f2_end", long(n3), 2));
    }

    // VT-GH290-10 — …and a larger reloaded offset SKIPS cvc values: F2's
    // lines before its reload count from 0 (cvc 247..310), the rest from 10
    // (cvc 10..256), so cvc 5 never occurs in F2 and target 6 does not fire
    // there; F3's lines before its reload count from 10, so it fires at F3
    // raw 59.
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t mcpf = emu.timing().master_cycles_per_frame;
        const uint64_t f3 = f1 + 2 * mcpf;
        bool ok = built;
        uint64_t n1 = 99, n2 = 99;
        if (built) {
            ok = ok && run_to(emu, at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x64, 10);
            g163::nr_write(emu, 0x23, 6);
            g163::nr_write(emu, 0x22, 0x02);
            emu.reset_line_int_fire_count();
            ok = ok && run_to(emu, f3);
            n1 = emu.line_int_fire_count();
            ok = ok && run_to(emu, at(emu, f3, 64, 60));
            n2 = emu.line_int_fire_count();
        }
        check("VT-GH290-10",
              "a frame whose cvc reloads a larger offset skips cvc values: a "
              "target among them does not fire that frame, and fires the next "
              "one before its reload (zxula_timing.vhd:457-466,577)",
              ok && n1 == 0 && n2 == 1,
              gw("by_f3_start", long(n1), 0) + gw("by_f3_line64", long(n2), 1));
    }

    // VT-GH290-11 — the offset cvc counts from is machine state: a snapshot
    // is taken at a frame boundary, where the next frame's first lines still
    // count from the previous frame's reload. build_split() leaves it at 10
    // with NR 0x64 = 20; a fresh machine (0 and 0) loads the binary stream.
    {
        Emulator a;
        const bool split = gh290::build_split(a);
        std::vector<uint8_t> buf = split ? gh290::stream_of(a)
                                         : std::vector<uint8_t>();
        Emulator b;
        bool ok = split && gh290::build(b);
        bool same = false;
        int nr64 = -1, pre = -1, post = -1;
        if (ok) {
            StateReader r(buf.data(), buf.size());
            ok = b.load_state(r);
            same = ok && gh290::stream_of(b) == buf;
            nr64 = g163::nr_read(b, 0x64);
            const uint64_t g = b.current_frame_cycle();
            ok = ok && run_to(b, at(b, g, 10, 200));
            pre = read_cvc(b);
            ok = ok && run_to(b, at(b, g, 100, 200));
            post = read_cvc(b);
        }
        check("VT-GH290-11",
              "save_state/load_state carry the offset cvc counts from apart "
              "from NR 0x64: the restored frame's lines before its reload count "
              "from 10, the reload loads the register's 20, and the stream "
              "re-saves byte-identical (zxula_timing.vhd:457-466)",
              ok && same && nr64 == 20 && pre == cvc_of(b, 10, 10) &&
                  post == cvc_of(b, 100, 20),
              std::string("split=") + std::to_string(split) + " same="
                  + std::to_string(same) + " " + gw("nr64", nr64, 20)
                  + gw("line10", pre, cvc_of(b, 10, 10))
                  + gw("line100", post, cvc_of(b, 100, 20)));
    }

    // VT-GH290-12 — the same through a .jns, whose "emulator" member carries
    // it as the key cvc_offset_delta.
    {
        Emulator a;
        const bool split = gh290::build_split(a);
        std::vector<uint8_t> jns;
        std::string why;
        bool ok = split;
        if (ok) {
            jnext::JnsSaveOptions opt;
            jnext::JnsLoadReport rep;
            ok = a.save_jns(opt, jns, rep, why);
        }
        Emulator b;
        ok = ok && gh290::build(b);
        int nr64 = -1, pre = -1, post = -1;
        if (ok) {
            jnext::JnsLoadOptions lopt;
            jnext::JnsLoadReport rep;
            ok = b.load_jns(jns.data(), jns.size(), lopt, rep, why);
            nr64 = g163::nr_read(b, 0x64);
            const uint64_t g = b.current_frame_cycle();
            ok = ok && run_to(b, at(b, g, 10, 200));
            pre = read_cvc(b);
            ok = ok && run_to(b, at(b, g, 100, 200));
            post = read_cvc(b);
        }
        check("VT-GH290-12",
              "a .jns carries the offset cvc counts from apart from NR 0x64: "
              "the restored frame counts from 10 until its reload, then from "
              "20 (zxula_timing.vhd:457-466)",
              ok && nr64 == 20 && pre == cvc_of(b, 10, 10) &&
                  post == cvc_of(b, 100, 20),
              std::string("split=") + std::to_string(split) + " why='" + why
                  + "' " + gw("nr64", nr64, 20)
                  + gw("line10", pre, cvc_of(b, 10, 10))
                  + gw("line100", post, cvc_of(b, 100, 20)));
    }

    // VT-GH290-13 — …and a rewind. F2 begins with cvc counting from 10 and
    // NR 0x64 = 20 (build_split); run into F2 past its reload (now 20), then
    // rewind to F2 line 10: the ring's frame-start snapshot must bring back
    // 10, and the replayed reload 20.
    {
        Emulator emu;
        const bool split = gh290::build_split(emu, /*rewind_frames=*/4);
        const uint64_t f2 = emu.current_frame_cycle();
        bool ok = split;
        int live = -1, pre = -1, post = -1;
        uint64_t reached = 0;
        if (ok) {
            ok = run_to(emu, at(emu, f2, 150, 200));
            live = read_cvc(emu);
            reached = emu.rewind_to_cycle(at(emu, f2, 10, 200));
            ok = ok && reached != UINT64_MAX && reached >= at(emu, f2, 10, 200)
                    && reached < at(emu, f2, 11, 0);
            pre = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, f2, 100, 200));
            post = read_cvc(emu);
        }
        check("VT-GH290-13",
              "a rewind into a frame restores the offset cvc counts from: before "
              "the frame's reload 10 (not the 20 it had reloaded when the rewind "
              "began), and 20 again after the replayed reload "
              "(zxula_timing.vhd:457-466)",
              ok && live == cvc_of(emu, 150, 20) && pre == cvc_of(emu, 10, 10) &&
                  post == cvc_of(emu, 100, 20),
              std::string("split=") + std::to_string(split)
                  + " reached=" + std::to_string(reached) + " "
                  + gw("live150", live, cvc_of(emu, 150, 20))
                  + gw("line10", pre, cvc_of(emu, 10, 10))
                  + gw("line100", post, cvc_of(emu, 100, 20)));
    }

    // VT-GH290-14 — a .jns written before the key existed (v1.0.41 on) comes
    // from a jnext whose cvc counted from the register itself. The key is
    // stored as the difference from NR 0x64 so that its declared default, 0,
    // restores exactly that machine: cvc counting from NR 0x64.
    {
        Emulator emu;
        const bool split = gh290::build_split(emu);   // cvc 10, register 20
        jnext::save::JsonReadDesc rd(
            "{\"nr_02_bus_reset\": false, \"prev_pulse_int_n\": true}");
        emu.describe_tail(rd);
        const std::string refusal = rd.refusal();
        const int got = emu.video_timing().cu_offset();
        check("VT-GH290-14",
              "a tail block without cvc_offset_delta (a pre-GH #290 .jns) "
              "restores cvc counting from NR 0x64, as that jnext's did, and is "
              "not refused (NEXT-SNAPSHOT-FORMAT.md §12.2)",
              split && refusal.empty() && got == 20,
              std::string("split=") + std::to_string(split) + " refusal='"
                  + refusal + "' " + gw("cvc_offset", got, 20));
    }

    // VT-GH290-15 — §12.2's gate on the declared default: it must equal what
    // a reset leaves. The in-place hard init (the loaders' path) — power-on:
    // NR 0x64 and the offset cvc counts from both 0 — from a machine where
    // they were 20 and 10.
    {
        Emulator emu;
        const bool split = gh290::build_split(emu);
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        const bool inited = emu.init(cfg);
        jnext::save::DefaultCheckDesc d;
        emu.describe_tail(d);
        std::string detail;
        for (const auto& m : d.mismatches())
            detail += m.field + " declared=" + m.declared + " reset=" + m.actual + " ";
        check("VT-GH290-15",
              "a hard init leaves cvc_offset_delta at its declared default 0 "
              "(the offset cvc counts from and NR 0x64 both 0, zxnext.vhd:5024), "
              "and it is the tail block's one defaulted field",
              split && inited && d.mismatches().empty() && d.defaulted() == 1 &&
                  d.undefaulted() == 2 && emu.video_timing().cu_offset() == 0,
              detail + "defaulted=" + std::to_string(d.defaulted())
                  + " undefaulted=" + std::to_string(d.undefaulted())
                  + " " + gw("cvc_offset", emu.video_timing().cu_offset(), 0));
    }

    // VT-GH290-16 — a SOFT reset clears the register (zxnext.vhd:5024) but
    // not cvc: zxula_timing has no reset input at all. So cvc keeps counting
    // from the old value until the frame's next reload, which loads the
    // cleared register.
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
        bool ok = built;
        int nr64 = -1, rest = -1, next = -1;
        if (built) {
            ok = ok && run_to(emu, at(emu, f1, 10, 200));
            g163::nr_write(emu, 0x64, 10);
            ok = ok && run_to(emu, at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x64, 20);
            emu.soft_reset();
            g163::install_jr_self_loop(emu);   // the CPU reset left PC at 0
            nr64 = g163::nr_read(emu, 0x64);
            ok = ok && run_to(emu, at(emu, f1, 150, 200));
            rest = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, f2, 100, 200));
            next = read_cvc(emu);
        }
        check("VT-GH290-16",
              "a soft reset clears NR 0x64 but cvc keeps counting from its last "
              "reload (10) until the next one loads the cleared 0 "
              "(zxnext.vhd:5024; zxula_timing.vhd has no reset input)",
              ok && nr64 == 0 && rest == cvc_of(emu, 150, 10) &&
                  next == cvc_of(emu, 100, 0),
              gw("nr64", nr64, 0) + gw("line150", rest, cvc_of(emu, 150, 10))
                  + gw("f2_line100", next, cvc_of(emu, 100, 0)));
    }

    // VT-GH290-17 — where the reload is, per timing: on the ula_max_hc pulse
    // (raw hc c_min_hactive - 12) of raw line c_min_vactive, visible from the
    // next pixel (zxula_timing.vhd:423-425,457-462). Master cycles =
    // (c_min_vactive * (c_max_hc + 1) + c_min_hactive - 11) * 4, the
    // constants read off the VHDL.
    {
        struct Case { MachineTimingMode mode; bool hz60; uint64_t want; const char* name; };
        const Case cases[] = {
            // 48K 50 Hz: vactive 64 (:269), hactive 128 (:261), max_hc 447 (:262)
            { MachineTimingMode::Timing48,       false, (64ULL * 448 + 117) * 4, "48k-50" },
            // 128K 50 Hz: vactive 64 (:203), hactive 136 (:195), max_hc 455 (:196)
            { MachineTimingMode::Timing128,      false, (64ULL * 456 + 125) * 4, "128k-50" },
            // +3 50 Hz: the same three constants as 128K
            { MachineTimingMode::TimingPlus3,    false, (64ULL * 456 + 125) * 4, "p3-50" },
            // Pentagon: vactive 80 (:167), hactive 128 (:159), max_hc 447 (:160)
            { MachineTimingMode::TimingPentagon, false, (80ULL * 448 + 117) * 4, "pent" },
            // 48K 60 Hz: vactive 40 (:297), hactive 128 (:289), max_hc 447 (:290)
            { MachineTimingMode::Timing48,       true,  (40ULL * 448 + 117) * 4, "48k-60" },
            // 128K 60 Hz: vactive 40 (:237), hactive 136 (:229), max_hc 455 (:230)
            { MachineTimingMode::Timing128,      true,  (40ULL * 456 + 125) * 4, "128k-60" },
        };
        bool all = true;
        std::string detail;
        for (const Case& c : cases) {
            VideoTiming t;
            t.init_timing(c.mode, c.hz60);
            const uint64_t got = t.cvc_reload_master_cycle_offset();
            if (got != c.want) {
                all = false;
                detail += std::string(c.name) + "=" + std::to_string(got) + " (want "
                        + std::to_string(c.want) + ") ";
            }
        }
        check("VT-GH290-17",
              "the cvc reload sits at raw (c_min_vactive, c_min_hactive - 11) on "
              "every timing: 48K, 128K, +3, Pentagon, 48K and 128K at 60 Hz "
              "(zxula_timing.vhd:159-167,195-204,229-238,261-270,289-298,423-425,457-462)",
              all, detail);
    }

    // VT-GH290-18 — and the Emulator reloads there on a timing whose
    // c_min_vactive is not 64: the Next at 60 Hz (NR 0x05 bit 2, committed at
    // the frame edge, zxnext.vhd:6697-6700) has c_min_vactive 40 and c_max_vc
    // 263 (zxula_timing.vhd:237-238).
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        bool ok = built;
        int pre_seam = -1, post_seam = -1;
        bool at_60 = false;
        if (built) {
            g163::nr_write(emu, 0x05, 0x04);
            emu.run_frame();   // commits 60 Hz at its start…
            emu.run_frame();   // …and this one is a whole 60 Hz frame
            at_60 = emu.video_timing().vc_max() == 263 &&
                    emu.video_timing().display_origin().vc == 40;
            const uint64_t f = emu.current_frame_cycle();
            const uint64_t g = f + emu.timing().master_cycles_per_frame;
            ok = ok && run_to(emu, at(emu, f, 100, 200));
            g163::nr_write(emu, 0x64, 20);
            ok = ok && run_to(emu, at(emu, g, 40, 60));
            pre_seam = read_cvc(emu);
            ok = ok && run_to(emu, at(emu, g, 40, 160));
            post_seam = read_cvc(emu);
        }
        check("VT-GH290-18",
              "at 60 Hz the reload is on raw line 40, not 64: NR 0x64 written "
              "mid-frame reaches cvc at the next frame's line 40, hc_ula 0 "
              "(zxula_timing.vhd:237-238,457-462)",
              ok && at_60 && pre_seam == cvc_of(emu, 39, 0) &&
                  post_seam == cvc_of(emu, 40, 20),
              std::string("at_60=") + std::to_string(at_60) + " "
                  + gw("line40@px60", pre_seam, cvc_of(emu, 39, 0))
                  + gw("line40@px160", post_seam, cvc_of(emu, 40, 20)));
    }

    // VT-GH290-19 — the side of the reload is decided to the master cycle,
    // and an IN inside the instruction that crosses it reads the value the
    // reload loads even though the reload's event only runs once that
    // instruction ends. Section 14's fixture (no frames run, so no reload
    // event at all): `IN A,(C)` samples the master cycle before its reload
    // edge, 84 cycles after it starts (VT-GH265-03). The reload is at cycle
    // 64*1824 + 500 = gh265::kStep. NR 0x64 = 20 is written first; cvc counts
    // from 0 until the reload.
    {
        auto in_1f = [](uint64_t start, bool& ok) -> uint8_t {
            Emulator emu;
            ok = g163::build_emulator(emu) && emu.clock().get() <= start;
            if (!ok) return 0;
            g163::nr_write(emu, 0x64, 20);
            emu.port().out(0x243B, 0x1F);
            auto regs = emu.cpu().get_registers();
            regs.PC = 0x8000;
            regs.BC = 0x253B;
            emu.mmu().write(0x8000, 0xED);
            emu.mmu().write(0x8001, 0x78);            // IN A,(C)
            emu.cpu().set_registers(regs);
            emu.clock().tick(static_cast<int>(start - emu.clock().get()));
            emu.cpu().execute();
            return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
        };
        bool ok1 = false, ok2 = false;
        const uint8_t on_edge = in_1f(gh265::kStep - 84, ok1);   // samples kStep - 1
        const uint8_t after   = in_1f(gh265::kStep - 76, ok2);   // samples kStep + 7
        check("VT-GH290-19",
              "an IN sampling the cycle before the reload reads cvc counting "
              "from the old offset (line 63: 310 -> 0x36), one sampling after it "
              "the reloaded 20 (0x14), inside the instruction whose end runs the "
              "reload (zxula_timing.vhd:423-425,457-462; zxnext.vhd:5871-5876)",
              ok1 && ok2 && on_edge == 0x36 && after == 0x14,
              gw("edge_on", on_edge, 0x36) + gw("edge_after", after, 0x14));
    }

    // VT-GH290-20/21 — a CPU NR 0x64 write is ordered against the reload by
    // its commit edge: io_request_edge() + 2 (zxnext.vhd:4739-4777), which for
    // `OUT (C),A` at 3.5 MHz is 74 master cycles after the instruction starts
    // (two M1s, then the I/O cycle's first clock: 9 T-states). The reload
    // samples i_cu_offset on the edge that starts cycle P, so a write
    // committing before it is loaded and one committing on or after it waits
    // a frame. Instruction starts are 8-cycle aligned and P = 117236 = 4 mod
    // 8, so the two nearest commits are P - 2 (start P - 76) and P + 6
    // (start P - 68). Running frame; the clock is moved to the start between
    // instructions, and the OUT is executed through the full per-instruction
    // path that orders deferred CPU writes against the frame's events.
    {
        auto out_at = [](int start_before_reload, int& line100, int& nr64) -> bool {
            Emulator emu;
            if (!gh290::build(emu)) return false;
            const uint64_t f1 = emu.current_frame_cycle();
            const uint64_t p  = f1 + gh265::kStep;       // raw (64, 125): VT-GH257-06
            const uint64_t start = p - static_cast<uint64_t>(start_before_reload);
            bool ok = gh290::run_to(emu, gh290::at(emu, f1, 50, 200)) &&
                      emu.clock().get() <= start;
            if (!ok) return false;
            emu.port().out(0x243B, 0x64);             // select, outside the OUT
            emu.mmu().write(0x8000, 0xED);
            emu.mmu().write(0x8001, 0x79);            // OUT (C),A
            emu.mmu().write(0x8002, 0x18);
            emu.mmu().write(0x8003, 0xFE);            // JR $
            auto regs = emu.cpu().get_registers();
            regs.PC = 0x8000;
            regs.BC = 0x253B;
            regs.AF = static_cast<uint16_t>((20 << 8) | (regs.AF & 0x00FF));
            emu.cpu().set_registers(regs);
            emu.clock().tick(static_cast<int>(start - emu.clock().get()));
            emu.execute_single_instruction();
            ok = gh290::run_to(emu, gh290::at(emu, f1, 100, 200));
            line100 = gh290::read_cvc(emu);
            nr64    = g163::nr_read(emu, 0x64);
            return ok;
        };
        int before_l100 = -1, before_nr64 = -1, after_l100 = -1, after_nr64 = -1;
        const bool ok1 = out_at(76, before_l100, before_nr64);
        const bool ok2 = out_at(68, after_l100, after_nr64);
        check("VT-GH290-20",
              "OUT (C),A committing NR 0x64 = 20 two cycles before the reload is "
              "loaded by it: line 100 of the same frame reads 56 "
              "(zxula_timing.vhd:457-462; zxnext.vhd:4739-4777,5442)",
              ok1 && before_l100 == 56 && before_nr64 == 20,
              gw("line100", before_l100, 56) + gw("nr64", before_nr64, 20));
        check("VT-GH290-21",
              "…and committing six cycles after it is not: line 100 still reads "
              "36, the register already 20 (zxula_timing.vhd:457-462; "
              "zxnext.vhd:4739-4777,5442,6090)",
              ok2 && after_l100 == 36 && after_nr64 == 20,
              gw("line100", after_l100, 36) + gw("nr64", after_nr64, 20));
    }

    // VT-GH290-22/23 — with NO frame events (Section 8's harness: run_frame()
    // is never called, so no reload ever runs) the lines from the reload's
    // position on count from NR 0x64 as it stands: the rule the readback
    // follows there (VT-GH290-19). The line interrupt is placed by the same
    // rule, in the current frame (22) and when it rolls into the next (23).
    // In a running frame this is the prediction the reload then re-derives.
    {
        Emulator emu;
        bool ok = g163::build_emulator(emu);
        uint64_t n1 = 99;
        if (ok) {
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();
            g163::nr_write(emu, 0x64, 20);
            g163::nr_write(emu, 0x23, 150);            // int_line_num 149
            g163::nr_write(emu, 0x22, 0x02);
            ok = g163::step_until_master_cycle(emu, 200ULL * 1824);
            n1 = emu.line_int_fire_count();
        }
        check("VT-GH290-22",
              "no frame events: the line interrupt counts the lines from the "
              "reload's position on from NR 0x64 (raw 64 + 149 - 20 = 193), "
              "as the readback does, not from the never-reloaded 0 (raw 213) "
              "(zxula_timing.vhd:457-462,566-570,577)",
              ok && n1 == 1, gw("by_line200", long(n1), 1));
    }
    {
        Emulator emu;
        bool ok = g163::build_emulator(emu);
        uint64_t n1 = 99, n2 = 99;
        if (ok) {
            const uint64_t mcpf = emu.timing().master_cycles_per_frame;
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();
            g163::nr_write(emu, 0x64, 20);
            g163::nr_write(emu, 0x23, 6);              // int_line_num 5
            g163::nr_write(emu, 0x22, 0x02);
            ok = g163::step_until_master_cycle(emu, mcpf);
            n1 = emu.line_int_fire_count();
            ok = ok && g163::step_until_master_cycle(emu, mcpf + 60ULL * 1824);
            n2 = emu.line_int_fire_count();
        }
        check("VT-GH290-23",
              "no frame events, target 6: neither side of the first frame holds "
              "cvc 5 (raw 69 counting from 0 lies after the reload's position, "
              "raw 49 counting from 20 before it), so it rolls into the next "
              "frame's lines before that position, counting from NR 0x64: raw "
              "49 (zxula_timing.vhd:457-466,577)",
              ok && n1 == 0 && n2 == 1,
              gw("by_frame_end", long(n1), 0) + gw("by_next_line60", long(n2), 1));
    }

    // VT-GH290-24 — the flag that says whether THIS frame's reload has run is
    // cleared at every frame end. F0 reloaded 0; NR 0x64 = 20 at F1 line 10;
    // an IN sampling just after F1's reload position, inside the instruction
    // whose end runs that reload, reads the value it loads (20), not the
    // offset F0's reload left (0).
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        bool ok = built;
        uint8_t v = 0xEE;
        if (built) {
            ok = run_to(emu, at(emu, f1, 10, 200));
            g163::nr_write(emu, 0x64, 20);
            ok = ok && gh290::in_1f_at(emu, f1 + gh265::kStep - 76, v);   // samples P + 7
        }
        check("VT-GH290-24",
              "in the second frame run, an IN sampling just after the reload's "
              "position reads the value it loads (0x14), not the previous "
              "frame's reload (0x00) (zxula_timing.vhd:457-462; "
              "zxnext.vhd:5871-5876)",
              ok && v == 0x14, gw("nr1f", v, 0x14));
    }

    // VT-GH290-25 — …and at every restore: a snapshot sits at a frame
    // boundary, before the frame's reload, whatever the machine it is loaded
    // into was doing. Machine b is past ITS reload when build_split()'s
    // stream (cvc 10, NR 0x64 20) is loaded into it; an IN sampling just after
    // the restored frame's reload position reads 20, not 10.
    {
        Emulator a;
        const bool split = gh290::build_split(a);
        const std::vector<uint8_t> buf = split ? gh290::stream_of(a)
                                               : std::vector<uint8_t>();
        Emulator b;
        bool ok = split && gh290::build(b);
        uint8_t v = 0xEE;
        if (ok) {
            const uint64_t g0 = b.current_frame_cycle();
            ok = run_to(b, at(b, g0, 100, 200));        // b's own reload has run
            StateReader r(buf.data(), buf.size());
            ok = ok && b.load_state(r);
            const uint64_t g = b.current_frame_cycle();
            ok = ok && gh290::in_1f_at(b, g + gh265::kStep - 76, v);
        }
        check("VT-GH290-25",
              "after a restore into a machine that was past its reload, an IN "
              "sampling just after the restored frame's reload position reads "
              "the value it loads (0x14), not the restored pre-reload offset "
              "(0x0a) (zxula_timing.vhd:457-462)",
              ok && v == 0x14, gw("nr1f", v, 0x14));
    }

    // VT-GH290-26/27 — the sample ON the reload cycle P. `cvc` is registered:
    // it loads on the edge that starts cycle P (zxula_timing.vhd:457-462), so
    // P is the first cycle carrying the new value, and an IN that samples P
    // reads it. `IN A,(C)` samples 83 cycles after it starts (VT-GH265-03), so
    // a start of P - 83 samples P. 26 is Section 14's frameless fixture (as
    // VT-GH290-19), 27 a running frame in which the reload event runs at the
    // end of the IN's instruction.
    {
        bool ok = false;
        uint8_t v = 0xEE;
        {
            Emulator emu;
            ok = g163::build_emulator(emu);
            if (ok) {
                g163::nr_write(emu, 0x64, 20);
                ok = gh290::in_1f_at(emu, gh265::kStep - 83, v);
            }
        }
        check("VT-GH290-26",
              "no frame events: an IN sampling the reload cycle itself reads "
              "the value the reload loads (0x14), not line 63's 310 counting "
              "from 0 (zxula_timing.vhd:457-462; zxnext.vhd:5871-5876)",
              ok && v == 0x14, gw("nr1f", v, 0x14));
    }
    {
        Emulator emu;
        const bool built = gh290::build(emu);
        const uint64_t f1 = emu.current_frame_cycle();
        bool ok = built;
        uint8_t v = 0xEE;
        if (built) {
            ok = run_to(emu, at(emu, f1, 10, 200));
            g163::nr_write(emu, 0x64, 20);
            ok = ok && gh290::in_1f_at(emu, f1 + gh265::kStep - 83, v);
        }
        check("VT-GH290-27",
              "running frame: an IN sampling the reload cycle reads the value "
              "the reload loads (0x14), inside the instruction whose end runs "
              "the reload event (zxula_timing.vhd:457-462)",
              ok && v == 0x14, gw("nr1f", v, 0x14));
    }

    // VT-GH290-28/29 — where a line-interrupt TARGET must land to count for
    // a compare. int_line_num is a CLK_7 register loaded from i_int_line on
    // the edge that starts the compare pixel (zxula_timing.vhd:563-572), and
    // the compare at hc_ula 255 is registered on the edge that ends it
    // (:574-583). So a target visible from cycle `now` counts for the compare
    // at pixel c iff now <= c - 1. Target 87 -> int_line_num 86 -> raw line
    // 150, compare at c = line 150, raw hc 380. The line interrupt is enabled
    // earlier with a target long passed; NR 0x23 = 87 is then written with the
    // clock moved exactly to c (28: lands on c, too late — fires next frame)
    // or to c - 1 (29: in time — fires this frame). A write outside any
    // instruction lands at the clock.
    {
        auto land_at = [](int before_c, uint64_t& this_frame, uint64_t& next_frame) -> bool {
            Emulator emu;
            if (!gh290::build(emu)) return false;
            const uint64_t f1 = emu.current_frame_cycle();
            const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
            const uint64_t c  = gh290::at(emu, f1, 150, 380);
            bool ok = gh290::run_to(emu, gh290::at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x23, 1);               // raw 64: long passed
            g163::nr_write(emu, 0x22, 0x02);
            emu.reset_line_int_fire_count();
            const uint64_t when = c - static_cast<uint64_t>(before_c);
            ok = ok && emu.clock().get() <= when;
            if (!ok) return false;
            emu.clock().tick(static_cast<int>(when - emu.clock().get()));
            g163::nr_write(emu, 0x23, 87);
            ok = gh290::run_to(emu, gh290::at(emu, f1, 152, 0));
            this_frame = emu.line_int_fire_count();
            ok = ok && gh290::run_to(emu, gh290::at(emu, f2, 152, 0));
            next_frame = emu.line_int_fire_count();
            return ok;
        };
        uint64_t on_this = 99, on_next = 99, early_this = 99, early_next = 99;
        const bool ok_on    = land_at(0, on_this, on_next);
        const bool ok_early = land_at(1, early_this, early_next);
        check("VT-GH290-28",
              "a line-interrupt target landing ON the compare cycle is one pixel "
              "too late for it: no fire this frame, one the next "
              "(zxula_timing.vhd:563-572,574-583)",
              ok_on && on_this == 0 && on_next == 1,
              gw("this_frame", long(on_this), 0) + gw("next_frame", long(on_next), 1));
        check("VT-GH290-29",
              "…and one landing the cycle before it counts: fires this frame "
              "(zxula_timing.vhd:563-572,574-583)",
              ok_early && early_this == 1 && early_next == 2,
              gw("this_frame", long(early_this), 1)
                  + gw("next_frame", long(early_next), 2));
    }

    // VT-GH290-30 — no frame events, the roll-forward's lines from the reload
    // position on: they count from NR 0x64 too. NR 0x64 = 20 and the target
    // (150 -> int_line_num 149 -> raw 193 counting from 20) written at line
    // 200, after that position has passed: it rolls into the next frame at
    // raw 193, not at 213 (counting from the never-reloaded 0).
    {
        Emulator emu;
        bool ok = g163::build_emulator(emu);
        uint64_t n1 = 99, n2 = 99;
        if (ok) {
            const uint64_t mcpf = emu.timing().master_cycles_per_frame;
            g163::install_jr_self_loop(emu);
            g163::nr_write(emu, 0x64, 20);
            ok = g163::step_until_master_cycle(emu, 200ULL * 1824);
            emu.reset_line_int_fire_count();
            g163::nr_write(emu, 0x23, 150);
            g163::nr_write(emu, 0x22, 0x02);
            ok = ok && g163::step_until_master_cycle(emu, mcpf + 190ULL * 1824);
            n1 = emu.line_int_fire_count();
            ok = ok && g163::step_until_master_cycle(emu, mcpf + 200ULL * 1824);
            n2 = emu.line_int_fire_count();
        }
        check("VT-GH290-30",
              "no frame events: a target whose line has passed rolls into the "
              "next frame's lines from the reload position on, counting from NR "
              "0x64 (raw 193), not from the never-reloaded 0 (raw 213) "
              "(zxula_timing.vhd:457-466,577)",
              ok && n1 == 0 && n2 == 1,
              gw("by_next_line190", long(n1), 0) + gw("by_next_line200", long(n2), 1));
    }

    // VT-GH290-31..35 — where a change of the line-interrupt ENABLE or TARGET
    // must land to reach a compare. The compare at pixel c (hc_ula 255) is
    // registered on the edge that ends the pixel (zxula_timing.vhd:574-583);
    // it takes the enable as it stands in cycle c+3 — `i_inten_line` is
    // nr_22_line_interrupt_en itself (zxnext.vhd:6752) — but the target as it
    // stood in cycle c-1: `int_line_num` is a CLK_7 register loaded on the
    // edge that starts the pixel (:563-572). Same fixture as VT-GH290-28/29:
    // target 87 compares at c = raw line 150, raw hc 380; a write outside an
    // instruction lands at the clock, which is moved there exactly.
    struct LineIntRun { bool ok = false; uint64_t by152 = 99, by170 = 99, next152 = 99; };
    // `setup` runs at F1 line 100; `write` runs with the clock at c + delta.
    auto line_int_run = [](void (*setup)(Emulator&), int64_t delta,
                           void (*write)(Emulator&)) -> LineIntRun {
        LineIntRun r;
        Emulator emu;
        if (!gh290::build(emu)) return r;
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
        const uint64_t c  = gh290::at(emu, f1, 150, 380);
        if (!gh290::run_to(emu, gh290::at(emu, f1, 100, 200))) return r;
        setup(emu);
        emu.reset_line_int_fire_count();
        const uint64_t when = static_cast<uint64_t>(static_cast<int64_t>(c) + delta);
        if (emu.clock().get() > when) return r;
        emu.clock().tick(static_cast<int>(when - emu.clock().get()));
        write(emu);
        bool ok = gh290::run_to(emu, gh290::at(emu, f1, 152, 0));
        r.by152 = emu.line_int_fire_count();
        ok = ok && gh290::run_to(emu, gh290::at(emu, f1, 170, 0));
        r.by170 = emu.line_int_fire_count();
        ok = ok && gh290::run_to(emu, gh290::at(emu, f2, 152, 0));
        r.next152 = emu.line_int_fire_count();
        r.ok = ok;
        return r;
    };
    auto target87_disabled = [](Emulator& e) {
        g163::nr_write(e, 0x23, 87);
        g163::nr_write(e, 0x22, 0x00);
    };
    auto target87_enabled = [](Emulator& e) {
        g163::nr_write(e, 0x23, 87);
        g163::nr_write(e, 0x22, 0x02);
    };
    auto enable = [](Emulator& e) { g163::nr_write(e, 0x22, 0x02); };
    auto retarget100 = [](Emulator& e) { g163::nr_write(e, 0x23, 100); };  // raw 163
    {
        const LineIntRun r = line_int_run(target87_disabled, 3, enable);
        check("VT-GH290-31",
              "the line-interrupt ENABLE landing 3 cycles after the compare "
              "pixel starts still reaches it: fires this frame "
              "(zxula_timing.vhd:574-583; zxnext.vhd:6752)",
              r.ok && r.by152 == 1,
              gw("by_line152", long(r.by152), 1));
    }
    {
        const LineIntRun r = line_int_run(target87_disabled, 4, enable);
        check("VT-GH290-32",
              "…and landing 4 cycles after it (on the edge the compare is "
              "registered) does not: no fire this frame, one the next "
              "(zxula_timing.vhd:574-583)",
              r.ok && r.by152 == 0 && r.next152 == 1,
              gw("by_line152", long(r.by152), 0) + gw("next_152", long(r.next152), 1));
    }
    {
        const LineIntRun r = line_int_run(target87_enabled, 2, enable);
        check("VT-GH290-33",
              "rewriting NR 0x22 unchanged 2 cycles into the compare pixel does "
              "not lose the fire the unchanged enable and target make "
              "(zxula_timing.vhd:563-583)",
              r.ok && r.by152 == 1,
              gw("by_line152", long(r.by152), 1));
    }
    {
        const LineIntRun r = line_int_run(target87_enabled, 0, retarget100);
        check("VT-GH290-34",
              "a TARGET change landing ON the compare cycle is too late for it: "
              "the OLD target still fires there (raw 150), and the new one then "
              "fires at raw 163 (zxula_timing.vhd:563-572,574-583)",
              r.ok && r.by152 == 1 && r.by170 == 2,
              gw("by_line152", long(r.by152), 1) + gw("by_line170", long(r.by170), 2));
    }
    {
        const LineIntRun r = line_int_run(target87_enabled, -1, retarget100);
        check("VT-GH290-35",
              "…and one landing the cycle before the pixel replaces it: no fire "
              "at raw 150, one at raw 163 (zxula_timing.vhd:563-572)",
              r.ok && r.by152 == 0 && r.by170 == 1,
              gw("by_line152", long(r.by152), 0) + gw("by_line170", long(r.by170), 1));
    }

    // VT-GH290-36/37 — a deferred CPU NR write lands on its COMMIT EDGE,
    // io_request_edge() + 2 (zxnext.vhd:4739-4777) — 74 master cycles into
    // `OUT (C),A` at 3.5 MHz (VT-GH290-20/21) — not at the end of its
    // instruction. 36: NR 0x23 = 87 from a passed target, OUT started at
    // c - 80: committed at c - 6, the compare at c, 16 cycles before the OUT
    // ends, sees it and fires this frame. 37: target 87 armed, NR 0x23 = 100
    // by an OUT started at c - 64: committed at c + 10, so the compare at c
    // (registered at c + 4) was over before it — it fires, and 100 fires at
    // raw 163.
    auto out_nr23 = [](uint8_t first_target, uint8_t new_target, int start_before_c,
                       uint64_t& by152, uint64_t& by170) -> bool {
        Emulator emu;
        if (!gh290::build(emu)) return false;
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t c  = gh290::at(emu, f1, 150, 380);
        if (!gh290::run_to(emu, gh290::at(emu, f1, 100, 200))) return false;
        g163::nr_write(emu, 0x23, first_target);
        g163::nr_write(emu, 0x22, 0x02);
        emu.reset_line_int_fire_count();
        const uint64_t start = c - static_cast<uint64_t>(start_before_c);
        if (emu.clock().get() > start) return false;
        emu.port().out(0x243B, 0x23);                 // select, outside the OUT
        emu.mmu().write(0x8000, 0xED);
        emu.mmu().write(0x8001, 0x79);                // OUT (C),A
        emu.mmu().write(0x8002, 0x18);
        emu.mmu().write(0x8003, 0xFE);                // JR $
        auto regs = emu.cpu().get_registers();
        regs.PC = 0x8000;
        regs.BC = 0x253B;
        regs.AF = static_cast<uint16_t>((new_target << 8) | (regs.AF & 0x00FF));
        emu.cpu().set_registers(regs);
        emu.clock().tick(static_cast<int>(start - emu.clock().get()));
        emu.execute_single_instruction();
        bool ok = gh290::run_to(emu, gh290::at(emu, f1, 152, 0));
        by152 = emu.line_int_fire_count();
        ok = ok && gh290::run_to(emu, gh290::at(emu, f1, 170, 0));
        by170 = emu.line_int_fire_count();
        return ok;
    };
    {
        uint64_t by152 = 99, by170 = 99;
        const bool ok = out_nr23(1, 87, 80, by152, by170);
        check("VT-GH290-36",
              "OUT (C),A committing NR 0x23 = 87 six cycles before the compare, "
              "in the same instruction window: the compare sees it and fires this "
              "frame (zxnext.vhd:4739-4777; zxula_timing.vhd:563-583)",
              ok && by152 == 1, gw("by_line152", long(by152), 1));
    }
    {
        uint64_t by152 = 99, by170 = 99;
        const bool ok = out_nr23(87, 100, 64, by152, by170);
        check("VT-GH290-37",
              "OUT (C),A retargeting NR 0x23 ten cycles AFTER the compare, in the "
              "same instruction window: the compare had already fired on the old "
              "target (raw 150), then the new one fires at raw 163 "
              "(zxnext.vhd:4739-4777; zxula_timing.vhd:563-583)",
              ok && by152 == 1 && by170 == 2,
              gw("by_line152", long(by152), 1) + gw("by_line170", long(by170), 2));
    }

    // VT-GH290-38/39 — a DISABLE and the compare it may be too late for. The
    // compare at c is registered on edge c+4 with the enable of cycle c+3
    // (zxula_timing.vhd:574-583, zxnext.vhd:6752). Disabling at c+3 is in time
    // (no fire); at c+4 it is not — the compare was already registered with
    // the old enable, so the event already scheduled for it must survive the
    // reschedule (fires).
    auto disable = [](Emulator& e) { g163::nr_write(e, 0x22, 0x00); };
    {
        const LineIntRun r = line_int_run(target87_enabled, 3, disable);
        check("VT-GH290-38",
              "a line-interrupt DISABLE landing 3 cycles into the compare pixel "
              "stops that compare: no fire (zxula_timing.vhd:574-583; "
              "zxnext.vhd:6752)",
              r.ok && r.by152 == 0, gw("by_line152", long(r.by152), 0));
    }
    {
        const LineIntRun r = line_int_run(target87_enabled, 4, disable);
        check("VT-GH290-39",
              "…one landing on the edge the compare is registered on is too late: "
              "the compare already scheduled keeps its event and fires "
              "(zxula_timing.vhd:574-583)",
              r.ok && r.by152 == 1 && r.next152 == 1,
              gw("by_line152", long(r.by152), 1) + gw("next_152", long(r.next152), 1));
    }

    // VT-GH290-40 — the live line-interrupt events are the machine's, and a
    // restore replaces the machine: none of the replaced machine's may fire,
    // nor be kept by the restored machine's first reschedule. b arms target 87
    // (compare at raw 150 of its F1) and is loaded, at line 100, with a stream
    // whose line interrupt is off and whose clock lies past that compare.
    {
        Emulator a;
        bool ok = gh290::build(a);
        std::vector<uint8_t> buf;
        if (ok) {
            const uint64_t fa = a.current_frame_cycle();
            ok = run_to(a, fa + 2 * a.timing().master_cycles_per_frame);  // a boundary two frames on
            buf = gh290::stream_of(a);
        }
        Emulator b;
        ok = ok && gh290::build(b);
        uint64_t after_step = 99, after_frame = 99;
        if (ok) {
            const uint64_t g0 = b.current_frame_cycle();
            ok = run_to(b, at(b, g0, 100, 200));
            g163::nr_write(b, 0x23, 87);
            g163::nr_write(b, 0x22, 0x02);        // armed at raw 150 of b's frame
            b.reset_line_int_fire_count();
            StateReader r(buf.data(), buf.size());
            ok = ok && b.load_state(r);
            b.execute_single_instruction();       // no begin_new_frame() yet
            after_step = b.line_int_fire_count();
            // Its begin_new_frame() reschedule, then the whole restored frame.
            // Through run_to(): b is paused by the debugger, and a bare
            // run_frame() on a paused machine returns without running.
            ok = ok && run_to(b, b.current_frame_cycle()
                                     + b.timing().master_cycles_per_frame);
            after_frame = b.line_int_fire_count();
        }
        check("VT-GH290-40",
              "a restore drops the replaced machine's pending line interrupt: "
              "no fire before the restored frame begins, nor after its first "
              "reschedule (the restored line interrupt is off)",
              ok && after_step == 0 && after_frame == 0,
              gw("after_step", long(after_step), 0) + gw("after_frame", long(after_frame), 0));
    }

    // VT-GH290-41 — …and so does an in-place hard init (the loaders' path),
    // which empties irq_scheduler_: a machine that had target 87 armed is
    // re-initialised, stepped without frames past where that compare was, and
    // then writes NR 0x22 = 0. Nothing may fire: there is no live event, so
    // none can be "kept".
    {
        Emulator emu;
        bool ok = gh290::build(emu);
        uint64_t n = 99;
        if (ok) {
            const uint64_t f1 = emu.current_frame_cycle();
            const uint64_t c  = at(emu, f1, 150, 380);
            ok = run_to(emu, at(emu, f1, 100, 200));
            g163::nr_write(emu, 0x23, 87);
            g163::nr_write(emu, 0x22, 0x02);     // armed at c
            EmulatorConfig cfg;
            cfg.type = MachineType::ZXN_ISSUE2;
            cfg.rewind_buffer_frames = 0;
            ok = ok && emu.init(cfg);
            g163::install_jr_self_loop(emu);
            emu.reset_line_int_fire_count();
            ok = ok && g163::step_until_master_cycle(emu, c + 1824);
            g163::nr_write(emu, 0x22, 0x00);
            ok = ok && g163::step_until_master_cycle(emu, emu.clock().get() + 1824);
            n = emu.line_int_fire_count();
        }
        check("VT-GH290-41",
              "an in-place hard init leaves no line interrupt live: a later "
              "reschedule has nothing to keep, and nothing fires",
              ok && n == 0, gw("fires", long(n), 0));
    }

    // VT-GH290-42/43 — NR 0x22 bit 0 is the target's MSB, so an NR 0x22 write
    // can change the TARGET too, and the compares in [e-3, e] must read the
    // target from before it (int_line_num, zxula_timing.vhd:563-572). Target
    // 300 (int_line_num 299) compares at raw line 52 of F2 (cvc 299 counting
    // from 0: the lines before the reload); NR 0x22 = 0x02 keeps the enable
    // and clears bit 8, making the target 44 (raw 107). 42: landing 1 cycle
    // into the raw-52 compare pixel, the OLD target still fires there, then
    // 44 fires at raw 107. 43: landing the cycle before it, only 107.
    auto msb_flip = [](int delta, uint64_t& by54, uint64_t& by110) -> bool {
        Emulator emu;
        if (!gh290::build(emu)) return false;
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t f2 = f1 + emu.timing().master_cycles_per_frame;
        const uint64_t c  = gh290::at(emu, f2, 52, 380);
        bool ok = gh290::run_to(emu, gh290::at(emu, f1, 100, 200));
        g163::nr_write(emu, 0x23, 300 & 0xFF);
        g163::nr_write(emu, 0x22, 0x03);          // enable, target MSB 1: 300
        ok = ok && gh290::run_to(emu, gh290::at(emu, f2, 40, 200));
        emu.reset_line_int_fire_count();
        const uint64_t when = static_cast<uint64_t>(static_cast<int64_t>(c) + delta);
        ok = ok && emu.clock().get() <= when;
        if (!ok) return false;
        emu.clock().tick(static_cast<int>(when - emu.clock().get()));
        g163::nr_write(emu, 0x22, 0x02);          // enable, target MSB 0: 44
        ok = gh290::run_to(emu, gh290::at(emu, f2, 54, 0));
        by54 = emu.line_int_fire_count();
        ok = ok && gh290::run_to(emu, gh290::at(emu, f2, 110, 0));
        by110 = emu.line_int_fire_count();
        return ok;
    };
    {
        uint64_t by54 = 99, by110 = 99;
        const bool ok = msb_flip(1, by54, by110);
        check("VT-GH290-42",
              "an NR 0x22 write clearing the target MSB 1 cycle into the old "
              "target's compare pixel is too late for it: target 300 still fires "
              "at raw 52, then 44 at raw 107 (zxula_timing.vhd:563-572,574-583)",
              ok && by54 == 1 && by110 == 2,
              gw("by_line54", long(by54), 1) + gw("by_line110", long(by110), 2));
    }
    {
        uint64_t by54 = 99, by110 = 99;
        const bool ok = msb_flip(-1, by54, by110);
        check("VT-GH290-43",
              "…and the cycle before that pixel it replaces the target: only raw "
              "107 fires (zxula_timing.vhd:563-572)",
              ok && by54 == 0 && by110 == 1,
              gw("by_line54", long(by54), 0) + gw("by_line110", long(by110), 1));
    }

    // VT-GH290-44..47 — `OUT (C),A` to NR 0x22 and to NR 0xC4 (bit 1 of both
    // is nr_22_line_interrupt_en, zxnext.vhd:5607-5610, fed straight into the
    // compare, :6752) lands on its commit edge, start + 74 at 3.5 MHz, while
    // the instruction ends at start + 96 — so the landing cycle and the clock
    // fall on different sides of a boundary. Target 87 compares at c = raw
    // 150, raw hc 380.
    //   ENABLE started at c - 80: lands c - 6, before the compare pixel — it
    //   fires this frame (by the clock, c + 16, it would be too late).
    //   DISABLE started at c - 72: lands c + 2, inside the pixel, before the
    //   enable is sampled in c + 3 — no fire (by the clock, c + 24, the
    //   compare would already have been registered, and fire).
    auto out_enable = [](uint8_t reg, uint8_t val, bool start_enabled,
                         int start_before_c, uint64_t& by152) -> bool {
        Emulator emu;
        if (!gh290::build(emu)) return false;
        const uint64_t f1 = emu.current_frame_cycle();
        const uint64_t c  = gh290::at(emu, f1, 150, 380);
        if (!gh290::run_to(emu, gh290::at(emu, f1, 100, 200))) return false;
        g163::nr_write(emu, 0x23, 87);
        g163::nr_write(emu, 0x22, start_enabled ? 0x02 : 0x00);
        emu.reset_line_int_fire_count();
        const uint64_t start = c - static_cast<uint64_t>(start_before_c);
        if (emu.clock().get() > start) return false;
        emu.port().out(0x243B, reg);                  // select, outside the OUT
        emu.mmu().write(0x8000, 0xED);
        emu.mmu().write(0x8001, 0x79);                // OUT (C),A
        emu.mmu().write(0x8002, 0x18);
        emu.mmu().write(0x8003, 0xFE);                // JR $
        auto regs = emu.cpu().get_registers();
        regs.PC = 0x8000;
        regs.BC = 0x253B;
        regs.AF = static_cast<uint16_t>((val << 8) | (regs.AF & 0x00FF));
        emu.cpu().set_registers(regs);
        emu.clock().tick(static_cast<int>(start - emu.clock().get()));
        emu.execute_single_instruction();
        const bool ok = gh290::run_to(emu, gh290::at(emu, f1, 152, 0));
        by152 = emu.line_int_fire_count();
        return ok;
    };
    {
        uint64_t n = 99;
        const bool ok = out_enable(0x22, 0x02, false, 80, n);
        check("VT-GH290-44",
              "OUT (C),A enabling the line interrupt through NR 0x22, committed 6 "
              "cycles before the compare in the same instruction: it fires this "
              "frame (zxnext.vhd:4739-4777,6752; zxula_timing.vhd:574-583)",
              ok && n == 1, gw("by_line152", long(n), 1));
    }
    {
        uint64_t n = 99;
        const bool ok = out_enable(0x22, 0x00, true, 72, n);
        check("VT-GH290-45",
              "OUT (C),A disabling it through NR 0x22, committed 2 cycles into the "
              "compare pixel: in time for the enable sample, no fire "
              "(zxnext.vhd:4739-4777,6752; zxula_timing.vhd:574-583)",
              ok && n == 0, gw("by_line152", long(n), 0));
    }
    {
        uint64_t n = 99;
        const bool ok = out_enable(0xC4, 0x03, false, 80, n);
        check("VT-GH290-46",
              "the same enable through NR 0xC4 bit 1 (the same flip-flop, "
              "zxnext.vhd:5607-5610): fires this frame (zxula_timing.vhd:574-583)",
              ok && n == 1, gw("by_line152", long(n), 1));
    }
    {
        uint64_t n = 99;
        const bool ok = out_enable(0xC4, 0x01, true, 72, n);
        check("VT-GH290-47",
              "…and the same disable through NR 0xC4: no fire "
              "(zxnext.vhd:5607-5610; zxula_timing.vhd:574-583)",
              ok && n == 0, gw("by_line152", long(n), 0));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("VideoTiming Expansion Compliance Tests\n");
    std::printf("======================================\n\n");
    std::printf("  27 rows from Sections 1-7 (G71/G106/G107/G109 closure,\n");
    std::printf("  2026-04-28) + 4 Section-8 rows for G163 dynamic line-int\n");
    std::printf("  re-evaluation (2026-04-30; +1 NR 0xC4 mirror row) +\n");
    std::printf("  6 Section-9 rows for vblank_top() per-machine offset\n");
    std::printf("  (Task 13, 2026-05-01). 6 Pentagon rows retired 2026-05-04\n");
    std::printf("  (Wave 0.3 follow-up: standalone Pentagon dropped). 31 live.\n");
    std::printf("  See doc/testing/VIDEOTIMING-TEST-PLAN-DESIGN.md.\n\n");

    section1_frame_envelope();
    std::printf("  Section 1: VT-S1-FRAME-ENVELOPE     — done (2 live)\n");

    section2_display_origin();
    std::printf("  Section 2: VT-S2-DISPLAY-ORIGIN     — done (2 live)\n");

    section3_ula_prefetch_origin();
    std::printf("  Section 3: VT-S3-ULA-PREFETCH       — done (2 live)\n");

    section4_int_position();
    std::printf("  Section 4: VT-S4-INT-POSITION       — done (3 live)\n");

    section5_60hz_variant();
    std::printf("  Section 5: VT-S5-60HZ-VARIANT       — done (5 live)\n");

    section6_line_int_target();
    std::printf("  Section 6: VT-S6-LINE-INT-TARGET    — done (3 live)\n");

    section7_scheduler_wiring();
    std::printf("  Section 7: VT-S7-SCHEDULER-WIRING   — done (5 live)\n");

    section8_g163_dynamic_reschedule();
    std::printf("  Section 8: VT-S8-G163-DYNAMIC-RESCHEDULE — done (4 live)\n");

    section9_vblank_top();
    std::printf("  Section 9: VT-S9-VBLANK-TOP         — done (4 live)\n");

    section10_t51_init_timing();
    std::printf("  Section 10: VT-S10-T51-INIT-TIMING  — done (3 live)\n");

    section11_t56_nr05_5060();
    std::printf("  Section 11: VT-S11-T56-NR05-5060    — done (4 live)\n");

    section12_gh237_init_from_nr03();
    std::printf("  Section 12: VT-S12-GH237-INIT-FROM-NR03 — done (7 live)\n");

    section13_gh257_line_int_hc();
    std::printf("  Section 13: VT-S13-GH257-LINE-INT-HC — done (6 live)\n");

    section14_gh265_nr_read_io_cycle();
    std::printf("  Section 14: VT-S14-GH265-NR-READ-IO-CYCLE — done (6 live)\n");

    section15_gh22_in_display_per_machine();
    std::printf("  Section 15: VT-S15-GH22-IN-DISPLAY   — done (2 live)\n");

    section16_gh290_cvc_reload();
    std::printf("  Section 16: VT-S16-GH290-CVC-RELOAD — done (47 live)\n");

    std::printf("\n======================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, static_cast<int>(g_skipped.size()));

    // Per-group breakdown (live rows only — empty in the scaffold).
    if (!g_results.empty()) {
        std::printf("\nPer-group breakdown (live rows only):\n");
        std::string last;
        int gp = 0, gf = 0;
        for (const auto& r : g_results) {
            if (r.group != last) {
                if (!last.empty())
                    std::printf("  %-28s %d/%d\n", last.c_str(), gp, gp + gf);
                last = r.group;
                gp   = gf = 0;
            }
            if (r.passed) ++gp; else ++gf;
        }
        if (!last.empty())
            std::printf("  %-28s %d/%d\n", last.c_str(), gp, gp + gf);
    }

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-8s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
