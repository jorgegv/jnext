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

void check(const char* id, const char* desc, bool cond,
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

    skip("VT-01",
         "F-VT-MAX-REBASE: 48K hc_max()=447, vc_max()=311 after init(ZX48K) "
         "(zxula_timing.vhd:262,270) — coupled with VT-18 via V1 rebase");
    skip("VT-02",
         "F-VT-MAX-REBASE: 128K hc_max()=455, vc_max()=310 after init(ZX128K) "
         "(zxula_timing.vhd:196,204) — coupled with VT-19 via V1 rebase");
    skip("VT-03",
         "F-VT-MAX-REBASE: Pentagon hc_max()=447, vc_max()=319 after "
         "init(PENTAGON) (zxula_timing.vhd:160,168) — coupled with VT-20 "
         "via V1 rebase");
}

// ══════════════════════════════════════════════════════════════════════
// Section 2 — Per-machine active-display origin
// VHDL: zxula_timing.vhd:147-312 (c_min_hactive / c_min_vactive)
// Skip reason: F-VT-ACCESSOR (new RasterPos display_origin() accessor).
// ══════════════════════════════════════════════════════════════════════

static void section2_display_origin() {
    set_group("VT-S2-DISPLAY-ORIGIN");

    skip("VT-04",
         "F-VT-ACCESSOR: 128K display_origin() = {136, 64} after init(ZX128K) "
         "(zxula_timing.vhd:195,203) [re-home: S13.05]");
    skip("VT-05",
         "F-VT-ACCESSOR: Pentagon display_origin() = {128, 80} after "
         "init(PENTAGON) (zxula_timing.vhd:159,167) [re-home: S13.06]");
    skip("VT-06",
         "F-VT-ACCESSOR: 48K display_origin() = {128, 64} after init(ZX48K) "
         "(zxula_timing.vhd:261,269) — symmetry baseline for VT-04/05");
}

// ══════════════════════════════════════════════════════════════════════
// Section 3 — ULA prefetch origin (c_min_hactive − 12) and vc_ula reset
// VHDL: zxula_timing.vhd:423-451
// Skip reason: F-VT-ACCESSOR (new int ula_prefetch_origin_hc() accessor).
// ══════════════════════════════════════════════════════════════════════

static void section3_ula_prefetch_origin() {
    set_group("VT-S3-ULA-PREFETCH-ORIGIN");

    skip("VT-07",
         "F-VT-ACCESSOR: 48K ula_prefetch_origin_hc() = 128 − 12 = 116 "
         "(zxula_timing.vhd:423) [re-home: S13.07]");
    skip("VT-08",
         "F-VT-ACCESSOR: 128K ula_prefetch_origin_hc() = 136 − 12 = 124 "
         "(zxula_timing.vhd:423)");
    skip("VT-09",
         "F-VT-ACCESSOR: Pentagon ula_prefetch_origin_hc() = 128 − 12 = 116 "
         "(zxula_timing.vhd:423)");
}

// ══════════════════════════════════════════════════════════════════════
// Section 4 — Per-machine interrupt position (c_int_h / c_int_v)
// VHDL: zxula_timing.vhd:155-293, zxula_timing.vhd:548-557
// Skip reason: F-VT-ACCESSOR (new RasterPos int_position() accessor).
// ══════════════════════════════════════════════════════════════════════

static void section4_int_position() {
    set_group("VT-S4-INT-POSITION");

    skip("VT-10",
         "F-VT-ACCESSOR: 48K int_position() = {116, 0} after init(ZX48K) "
         "(zxula_timing.vhd:257,265) [re-home: S14.01]");
    skip("VT-11",
         "F-VT-ACCESSOR: 128K int_position() = {128, 1} after init(ZX128K) "
         "(zxula_timing.vhd:187,199) [re-home: S14.02]");
    skip("VT-12",
         "F-VT-ACCESSOR: Pentagon int_position() = {439, 319} after "
         "init(PENTAGON) (zxula_timing.vhd:155,163) [re-home: S14.03]");
    skip("VT-13",
         "F-VT-ACCESSOR: +3 int_position() = {126, 1} after init(ZX_PLUS3) — "
         "VHDL i_timing(0)='1' selects 136+2−12=126 vs 128K 128 "
         "(zxula_timing.vhd:189,199)");
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

    skip("VT-14",
         "F-VT-ACCESSOR: 48K 60 Hz vc_max()=263, frame = 448*264/2 = 59,136 "
         "T-states (zxula_timing.vhd:290,298) [re-home: S13.08]");
    skip("VT-15",
         "F-VT-ACCESSOR: 128K 60 Hz vc_max()=263, frame = 456*264/2 = 60,192 "
         "T-states (zxula_timing.vhd:230,238)");
    skip("VT-16",
         "F-VT-ACCESSOR: 60 Hz display_origin().vc = 40 for both 48K/128K "
         "60 Hz (zxula_timing.vhd:297,237)");
    skip("VT-17",
         "F-VT-ACCESSOR: 60 Hz int_position().vc = 0 for both 48K 60 Hz and "
         "128K 60 Hz (zxula_timing.vhd:293,233)");
    skip("VT-17b",
         "F-VT-ACCESSOR: +3 60 Hz int_position().hc = 126 (zxula_timing.vhd:223) "
         "vs 128K 60 Hz = 128 (zxula_timing.vhd:221) — i_timing(0)='1' split");
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

    skip("VT-18",
         "F-VT-MAX-REBASE: 48K target=0 → int_line_num() = c_max_vc = 311 "
         "(zxula_timing.vhd:566-570) — coupled with VT-01 via V1 rebase");
    skip("VT-19",
         "F-VT-MAX-REBASE: 128K target=0 → int_line_num() = c_max_vc = 310 "
         "(zxula_timing.vhd:566-570) — coupled with VT-02 via V1 rebase");
    skip("VT-20",
         "F-VT-MAX-REBASE: Pentagon target=0 → int_line_num() = c_max_vc = 319 "
         "(zxula_timing.vhd:566-570) — coupled with VT-03 via V1 rebase");
    skip("VT-21",
         "F-VT-ACCESSOR: any machine: target=10 → int_line_num() = 9 — "
         "non-zero branch (zxula_timing.vhd:568)");
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("VideoTiming Expansion Compliance Tests\n");
    std::printf("======================================\n\n");
    std::printf("  Scaffolding suite — all 22 plan rows are skip()-scaffolded\n");
    std::printf("  pending the VideoTiming per-machine accessor expansion.\n");
    std::printf("  See doc/testing/VIDEOTIMING-TEST-PLAN-DESIGN.md.\n\n");

    section1_frame_envelope();
    std::printf("  Section 1: VT-S1-FRAME-ENVELOPE     — done (3 skipped)\n");

    section2_display_origin();
    std::printf("  Section 2: VT-S2-DISPLAY-ORIGIN     — done (3 skipped)\n");

    section3_ula_prefetch_origin();
    std::printf("  Section 3: VT-S3-ULA-PREFETCH       — done (3 skipped)\n");

    section4_int_position();
    std::printf("  Section 4: VT-S4-INT-POSITION       — done (4 skipped)\n");

    section5_60hz_variant();
    std::printf("  Section 5: VT-S5-60HZ-VARIANT       — done (5 skipped)\n");

    section6_line_int_target();
    std::printf("  Section 6: VT-S6-LINE-INT-TARGET    — done (4 skipped)\n");

    std::printf("\n======================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

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
