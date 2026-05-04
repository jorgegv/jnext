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
// parallax.nex. See doc/issues/PARALLAX-NEX-INVESTIGATION.md and gap
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

// Helper: master-cycle offset at which a line-int with `target`
// (NR 0x23, MSB=0) fires within the frame. Matches
// VideoTiming::line_int_master_cycle_offset() but standalone so the
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
              "NEXT 50Hz vblank_top() = min_vactive(64) - DISP_Y(32) = 32",
              v == 32, "got " + std::to_string(v));
    }
    // VT-VBT-02 — 48K 50 Hz: same 64-32=32.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX48K);
        const int v = vt.vblank_top();
        check("VT-VBT-02",
              "48K 50Hz vblank_top() = min_vactive(64) - DISP_Y(32) = 32",
              v == 32, "got " + std::to_string(v));
    }
    // VT-VBT-03 — 128K 50 Hz: same 64-32=32.
    {
        VideoTiming vt;
        vt.init(MachineType::ZX128K);
        const int v = vt.vblank_top();
        check("VT-VBT-03",
              "128K 50Hz vblank_top() = min_vactive(64) - DISP_Y(32) = 32",
              v == 32, "got " + std::to_string(v));
    }
    // VT-VBT-04 — Pentagon: min_vactive=80, DISP_Y=32 → 48 (off-by-16
    // from NEXT, the bug Task 13 fixes).
    {
        VideoTiming vt;
        vt.init(MachineType::PENTAGON);
        const int v = vt.vblank_top();
        check("VT-VBT-04",
              "Pentagon vblank_top() = min_vactive(80) - DISP_Y(32) = 48",
              v == 48, "got " + std::to_string(v));
    }
    // VT-VBT-05 — NEXT 60 Hz: min_vactive=40, DISP_Y=32 → 8 (off-by-24
    // from NEXT 50Hz).
    {
        VideoTiming vt;
        vt.init(MachineType::ZXN_ISSUE2, /*refresh_60hz=*/true);
        const int v = vt.vblank_top();
        check("VT-VBT-05",
              "NEXT 60Hz vblank_top() = min_vactive(40) - DISP_Y(32) = 8",
              v == 8, "got " + std::to_string(v));
    }
    // VT-VBT-06 — Pentagon refresh_60hz silently ignored (no 60Hz VHDL
    // branch for Pentagon, init() resets refresh_60hz_ to false).
    {
        VideoTiming vt;
        vt.init(MachineType::PENTAGON, /*refresh_60hz=*/true);
        const int v = vt.vblank_top();
        check("VT-VBT-06",
              "Pentagon vblank_top() ignores 60Hz flag (no Pentagon 60Hz "
              "VHDL branch) → still 48",
              v == 48, "got " + std::to_string(v));
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
    std::printf("  (Task 13, 2026-05-01). 37 live.\n");
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

    section8_g163_dynamic_reschedule();
    std::printf("  Section 8: VT-S8-G163-DYNAMIC-RESCHEDULE — done (4 live)\n");

    section9_vblank_top();
    std::printf("  Section 9: VT-S9-VBLANK-TOP         — done (6 live)\n");

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
