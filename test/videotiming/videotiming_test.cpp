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
    {
        VideoTiming vt;
        vt.init(MachineType::PENTAGON);
        check("VT-03",
              "Pentagon hc_max()=447, vc_max()=319 after init(PENTAGON) "
              "(zxula_timing.vhd:160,168)",
              vt.hc_max() == 447 && vt.vc_max() == 319);
    }
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
        check("VT-04", "128K display_origin() = {136, 64} (VHDL :195,:203)",
              p.hc == 136 && p.vc == 64,
              "got {" + std::to_string(p.hc) + "," + std::to_string(p.vc) + "}");
    }
    {
        VideoTiming vt;
        vt.init(MachineType::PENTAGON);
        auto p = vt.display_origin();
        check("VT-05", "Pentagon display_origin() = {128, 80} (VHDL :159,:167)",
              p.hc == 128 && p.vc == 80,
              "got {" + std::to_string(p.hc) + "," + std::to_string(p.vc) + "}");
    }
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        auto p = vt.display_origin();
        check("VT-06", "48K display_origin() = {128, 64} (VHDL :261,:269)",
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
        check("VT-07", "48K ula_prefetch_origin_hc() = 128 - 12 = 116 (VHDL :423)",
              hc == 116, "got " + std::to_string(hc));
    }
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        int hc = vt.ula_prefetch_origin_hc();
        check("VT-08", "128K ula_prefetch_origin_hc() = 136 - 12 = 124 (VHDL :423)",
              hc == 124, "got " + std::to_string(hc));
    }
    {
        VideoTiming vt;
        vt.init(MachineType::PENTAGON);
        int hc = vt.ula_prefetch_origin_hc();
        check("VT-09", "Pentagon ula_prefetch_origin_hc() = 128 - 12 = 116 (VHDL :423)",
              hc == 116, "got " + std::to_string(hc));
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
    {
        VideoTiming vt;
        vt.init(MachineType::PENTAGON);
        auto p = vt.int_position();
        check("VT-12", "Pentagon int_position = {439, 319} (zxula_timing.vhd:155,163)",
              p.hc == 439 && p.vc == 319);
    }
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
    {
        VideoTiming vt;
        vt.init(MachineType::PENTAGON);
        vt.set_line_interrupt_target(0);
        check("VT-20",
              "Pentagon target=0 → int_line_num() == c_max_vc == 319 "
              "(zxula_timing.vhd:566-570)",
              vt.int_line_num() == 319);
    }
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

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("VideoTiming Expansion Compliance Tests\n");
    std::printf("======================================\n\n");
    std::printf("  All 27 rows live post Section-7 production-scheduler\n");
    std::printf("  wiring (G71/G106/G107/G109 closure, 2026-04-28).\n");
    std::printf("  See doc/testing/VIDEOTIMING-TEST-PLAN-DESIGN.md.\n\n");

    section1_frame_envelope();
    std::printf("  Section 1: VT-S1-FRAME-ENVELOPE     — done (3 live)\n");

    section2_display_origin();
    std::printf("  Section 2: VT-S2-DISPLAY-ORIGIN     — done (3 live)\n");

    section3_ula_prefetch_origin();
    std::printf("  Section 3: VT-S3-ULA-PREFETCH       — done (3 live)\n");

    section4_int_position();
    std::printf("  Section 4: VT-S4-INT-POSITION       — done (4 live)\n");

    section5_60hz_variant();
    std::printf("  Section 5: VT-S5-60HZ-VARIANT       — done (5 live)\n");

    section6_line_int_target();
    std::printf("  Section 6: VT-S6-LINE-INT-TARGET    — done (4 live)\n");

    section7_scheduler_wiring();
    std::printf("  Section 7: VT-S7-SCHEDULER-WIRING   — done (5 live)\n");

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
