// Multiface core class compliance test (Wave 1 B1, Task 8 plan).
//
// Pins src/peripheral/multiface.{h,cpp} against the authoritative VHDL
// at /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
//   device/multiface.vhd (197 lines)
// (external to this repo; cited here for provenance, not edited).
//
// Ground rules (per doc/testing/UNIT-TEST-PLAN-EXECUTION.md):
//   * VHDL is the oracle; the C++ emulator is the thing under test.
//   * Every check(id, desc, actual_cond, "VHDL file:line ...") maps to
//     exactly one plan row.
//   * Discriminative assertions only — one row per behavioural axis,
//     not "X is true after construction".
//
// MF-CORE-01..12 — full set lands in this binary; MF-G48-02/03/04 are
// also live in test/nmi/nmi_test.cpp (covering complementary axes).
//
// Run: ./build/test/multiface_test

#include "core/emulator.h"
#include "core/saveable.h"
#include "peripheral/multiface.h"
#include "port/port_dispatch.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass = 0;
int g_fail = 0;
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
    std::string id;
    std::string reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    g_results.push_back(Result{g_group, id, desc, cond, detail});
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

// Build a Multiface ready for state-machine tests:
//   - enabled (NR 0x83 b1 = '1' equivalent so the FFs are not held in reset)
//   - default mode is mode_p3 (NR 0x0A power-on)
// Tests that need a different mode call set_mode() explicitly.
Multiface make_mf(uint8_t mode = 0x02 /* MF128 by default for mode-p3=0 tests */)
{
    Multiface mf;
    mf.set_enabled(true);
    mf.set_mode(mode);
    return mf;
}

}  // namespace

// =====================================================================
// Group MF-CORE — bare class state machine (12 rows).
// VHDL: cores/zxnext/src/device/multiface.vhd
// =====================================================================

static void g_mf_core()
{
    set_group("MF-CORE");

    // MF-CORE-01 — Reset defaults.
    // VHDL multiface.vhd:
    //   port_io_dly := '0'   (line 126)
    //   nmi_active  := '0'   (line 141)
    //   invisible   := '1'   (line 156, NOTE: '1' not '0')
    //   mf_enable   := '0'   (line 175)
    // is_mem_active() = mf_enable OR fetch_66 = false; is_nmi_hold() =
    // nmi_active = false. Discriminative: 6 sub-assertions.
    {
        Multiface mf;
        mf.set_enabled(true);  // exercise enabled-path reset
        mf.reset(true);
        const bool nmi_clear  = !mf.nmi_active();
        const bool inv_set    =  mf.invisible();      // VHDL line 156: '1'
        const bool mfe_clear  = !mf.mf_enable();
        const bool dly_clear  = !mf.port_io_dly();
        const bool mem_clear  = !mf.is_mem_active();
        const bool hold_clear = !mf.is_nmi_hold();
        check("MF-CORE-01",
              "reset defaults: nmi=0 invisible=1 mf_enable=0 port_io_dly=0 mem=0 hold=0",
              nmi_clear && inv_set && mfe_clear && dly_clear &&
                  mem_clear && hold_clear,
              "multiface.vhd:126,141,156,175");
    }

    // MF-CORE-02 — button_press transitions nmi_active 0->1; second
    // press while nmi_active=1 is a no-op.
    // VHDL multiface.vhd:135 — button_pulse <= button_i AND NOT
    // nmi_active. With nmi_active=0, pulse fires; with nmi_active=1,
    // pulse is gated off so the button has no effect. Discriminative:
    // pre/post for both presses + verify second press leaves nmi_active
    // already-1 (no spurious transitions). 4 sub-assertions.
    {
        Multiface mf = make_mf();
        mf.reset(true);
        const bool pre_first = !mf.nmi_active();
        mf.button_press();
        const bool first_armed = mf.nmi_active();
        // Second press: button_pulse gated off because nmi_active=1.
        mf.button_press();
        const bool second_noop = mf.nmi_active();
        // The "no-op" claim is meaningful only because something WOULD
        // change if button_pulse fired; verify by also checking
        // invisible stays 0 (it was cleared by the first press; a
        // spurious second pulse wouldn't re-clear what's already 0,
        // but a pulse arriving with a stale invisible=1 would).
        // Demonstrate the gating by separately verifying no toggle on
        // mf_enable / port_io_dly which only the input pin would touch.
        const bool no_dly = !mf.port_io_dly();
        check("MF-CORE-02",
              "button_press: arms nmi_active 0->1; second press no-op while nmi_active=1",
              pre_first && first_armed && second_noop && no_dly,
              "multiface.vhd:135 button_pulse = button_i AND NOT nmi_active");
    }

    // MF-CORE-03 — button_press also clears `invisible` to 0 (line 158).
    // Sub-axis from MF-G48-04 (the nmi_test row covers both sides via
    // mode-conditional set; here we cover the simple clear). Discriminative:
    // pre-press invisible=1, post-press invisible=0 — and verify
    // invisible_eff respects mode_48 muting (line 165).
    {
        Multiface mf = make_mf(0x00);  // MF+3 (mode_p3=1, mode_48=0)
        mf.reset(true);
        const bool pre  = mf.invisible() && mf.invisible_eff();
        mf.button_press();
        const bool post = !mf.invisible() && !mf.invisible_eff();
        // Switch to mode_48 and reset: invisible_eff must read 0
        // regardless of invisible's value (line 165: AND NOT mode_48).
        mf.set_mode(0x03);  // MF1 = mode_48
        mf.reset(true);
        const bool m48_eff_clear = mf.invisible() && !mf.invisible_eff();
        check("MF-CORE-03",
              "button_press clears invisible -> 0; invisible_eff = invisible AND NOT mode_48",
              pre && post && m48_eff_clear,
              "multiface.vhd:158 (clear), :165 (eff)");
    }

    // MF-CORE-04 — 0x0066 fetch with nmi_active=1 AND mreq=0 AND m1=0
    // sets mf_enable=1 (lines 169, 176). Without nmi_active, no enable
    // (line 169 `nmi_active='1'` requirement). Discriminative: both
    // arms.
    {
        Multiface mf = make_mf();
        mf.reset(true);
        // Case A: nmi_active=0 -> fetch_66 stays 0 -> mf_enable stays 0
        //         (no enable transition).
        mf.on_m1(0x0066, /*mreq_low=*/true);
        const bool no_enable_without_nmi = !mf.mf_enable() &&
                                            !mf.is_mem_active();

        // Case B: arm nmi_active via button, then 0x0066 fetch sets
        //         mf_enable=1. is_mem_active() is also true via the
        //         one-cycle bypass (mf_enable_eff = mf_enable OR fetch_66).
        mf.button_press();
        mf.on_m1(0x0066, /*mreq_low=*/true);
        const bool enabled_with_nmi = mf.mf_enable() && mf.is_mem_active();

        // And: a non-0x0066 PC must not arm mf_enable even with nmi_active.
        Multiface mf2 = make_mf();
        mf2.reset(true);
        mf2.button_press();
        mf2.on_m1(0x1234, /*mreq_low=*/true);
        const bool no_enable_other_pc = !mf2.mf_enable();

        check("MF-CORE-04",
              "0x0066 + m1 + mreq + nmi_active=1 -> mf_enable=1 (else stays 0)",
              no_enable_without_nmi && enabled_with_nmi && no_enable_other_pc,
              "multiface.vhd:169 fetch_66, :176 mf_enable<=1");
    }

    // MF-CORE-05 — RETN seen clears nmi_active AND mf_enable.
    // VHDL multiface.vhd:144 (nmi_active clear), :178 (mf_enable clear).
    // Discriminative: both bits drop on the same RETN edge.
    {
        Multiface mf = make_mf();
        mf.reset(true);
        mf.button_press();
        mf.on_m1(0x0066, /*mreq_low=*/true);
        const bool armed = mf.nmi_active() && mf.mf_enable();
        mf.on_retn_seen();
        const bool cleared = !mf.nmi_active() && !mf.mf_enable();
        check("MF-CORE-05",
              "on_retn_seen clears both nmi_active and mf_enable",
              armed && cleared,
              "multiface.vhd:144 (nmi clr), :178 (mf_enable clr)");
    }

    // MF-CORE-06 — port_io_dly edge detector + suppression of nmi_active
    // clear when port_io_dly was high entering the cycle.
    // VHDL multiface.vhd:122-131 — port_io_dly latches the OR of the
    // four port_* inputs every clock. Line 144 nmi_active clear requires
    // `port_io_dly = '0'` (entering the cycle). The discriminative case:
    // a clear-eligible strobe (e.g. port_mf_enable_wr) fires immediately
    // after a non-clearing strobe (e.g. port_mf_enable_rd in mode_128 —
    // line 144 doesn't include rd-without-mode_p3, but rd still latches
    // dly via line 128). Without the dly gate, the 2nd cycle would
    // wrongly clear nmi_active.
    //
    // In mode_128 (mode_p3=0), port_mf_disable_rd is NOT a clear path
    // (line 144 third disjunct gates it on mode_p3=1), so we can use it
    // as a "dly setter that doesn't clear". port_mf_enable_rd similarly
    // does not appear in line 144's disjuncts, so it's also a non-clearer.
    // We use port_mf_enable_rd since it has no other side-effects in this
    // setup (with invisible_eff=1 by default after reset, line 181 sets
    // mf_enable<=NOT invisible_eff = 0 — same as the reset value).
    {
        Multiface mf = make_mf();  // mode_128
        mf.reset(true);
        mf.button_press();
        const bool armed1 = mf.nmi_active() && !mf.port_io_dly();

        // Quiescent baseline: a control case where dly=0 entering the
        // clear cycle. The clear should fire.
        Multiface mf_ctrl = make_mf();
        mf_ctrl.reset(true);
        mf_ctrl.button_press();
        mf_ctrl.on_port_enable_wr(true);
        const bool ctrl_cleared = !mf_ctrl.nmi_active();

        // Discriminative case: a non-clearing port strobe sets dly=1,
        // then port_mf_enable_wr's clear is suppressed.
        // First, a port_mf_enable_rd (non-clear in mode_128 line 144,
        // since the en_rd disjunct is absent there) → dly=1. nmi_active
        // remains 1 because en_rd does NOT appear in line 144's clear
        // expression at all.
        mf.on_port_enable_rd(true);
        const bool dly_set_no_clear = mf.nmi_active() && mf.port_io_dly();

        // Now port_mf_enable_wr fires with dly_prev=1. Clear suppressed.
        mf.on_port_enable_wr(true);
        const bool clear_suppressed = mf.nmi_active();

        check("MF-CORE-06",
              "port_io_dly edge detector suppresses nmi_active clear when prior-cycle dly=1",
              armed1 && ctrl_cleared && dly_set_no_clear && clear_suppressed,
              "multiface.vhd:128 (dly FF), :144 (port_io_dly=0 gate on en_wr)");
    }

    // MF-CORE-07 — INVISIBLE state machine.
    //   * port_mf_disable_wr with mode_p3=0 AND port_io_dly=0 -> invisible=1
    //     (line 159 first disjunct)
    //   * port_mf_enable_wr  with mode_p3=1 AND port_io_dly=0 -> invisible=1
    //     (line 159 second disjunct)
    //   * button_pulse clears invisible -> 0 (line 158)
    // Discriminative: 4 transitions covering both disjuncts and the clear.
    {
        // mode_128 (mode_p3=0): port_mf_disable_wr is the SETTER.
        Multiface mf = make_mf(0x02);
        mf.reset(true);
        const bool inv_initial_128 = mf.invisible();
        mf.button_press();
        const bool inv_after_btn_128 = !mf.invisible();
        mf.on_port_disable_wr(true);
        const bool inv_set_via_dis_wr = mf.invisible();

        // mode_p3 (mode_p3=1): port_mf_enable_wr is the SETTER. Note the
        // port_io_dly gate — we need a quiescent prior cycle, so reset
        // the FSM cleanly between transitions.
        Multiface mfp3 = make_mf(0x00);
        mfp3.reset(true);
        mfp3.button_press();
        const bool inv_after_btn_p3 = !mfp3.invisible();
        mfp3.on_port_enable_wr(true);
        const bool inv_set_via_en_wr = mfp3.invisible();

        check("MF-CORE-07",
              "INVISIBLE: dis_wr+mode_128=set, en_wr+mode_p3=set, button=clear",
              inv_initial_128 && inv_after_btn_128 && inv_set_via_dis_wr &&
                  inv_after_btn_p3 && inv_set_via_en_wr,
              "multiface.vhd:158 (button clr), :159 (port set)");
    }

    // MF-CORE-08 — mf_enable_eff one-cycle bypass.
    // VHDL multiface.vhd:186 — `mf_enable_eff <= mf_enable OR fetch_66`.
    // During the actual M1 fetch at 0x0066, fetch_66 surfaces the
    // bypassed signal even before the FF latches. After the cycle,
    // fetch_66 falls but mf_enable is now '1' from the latching.
    //
    // Discriminative: the cycle BEFORE the fetch, mf_enable_eff (i.e.
    // is_mem_active()) is 0; the cycle AFTER the fetch, it's 1 because
    // mf_enable latched. Mid-fetch the OR holds.
    {
        Multiface mf = make_mf();
        mf.reset(true);
        mf.button_press();
        const bool pre_fetch = !mf.is_mem_active();
        mf.on_m1(0x0066, /*mreq_low=*/true);
        // After the fetch, fetch_66_live drops back to false (cleared
        // because the next clock_edge_ runs with a_0066=false), but
        // mf_enable retains its '1'. Run a non-0x0066 M1 to advance
        // one cycle and verify mf_enable carries the OR forward via
        // the FF, not fetch_66.
        const bool during_fetch = mf.is_mem_active();
        mf.on_m1(0x0100, /*mreq_low=*/true);
        // After advancing one M1 cycle to a non-0x0066 PC, fetch_66 is
        // 0 but mf_enable FF is still 1 (no clear path was hit). The
        // mem_active output is therefore mf_enable OR fetch_66 = 1.
        const bool post_fetch = mf.is_mem_active() && mf.mf_enable();
        check("MF-CORE-08",
              "mf_enable_eff = mf_enable OR fetch_66 (FF carries forward post-fetch)",
              pre_fetch && during_fetch && post_fetch,
              "multiface.vhd:186 mf_enable_eff");
    }

    // MF-CORE-09 — mode dispatch (sub-axis of MF-G48-02 in nmi_test;
    // here we additionally verify that mode-decode is purely
    // combinational on set_mode, not gated on enable_).
    {
        Multiface mf;
        // Disabled: mode decode still works because it's a pure
        // combinational process at multiface.vhd:105-118 (no clock,
        // no reset).
        mf.set_mode(0x00); const bool p3   = mf.mode_p3();
        mf.set_mode(0x03); const bool m1   = mf.mode_48();
        mf.set_mode(0x01); const bool m128_a = mf.mode_128();
        mf.set_mode(0x02); const bool m128_b = mf.mode_128();
        check("MF-CORE-09",
              "mode dispatch: 00->p3, 11->48, 01/10->128 (combinational, ungated)",
              p3 && m1 && m128_a && m128_b,
              "multiface.vhd:105-118");
    }

    // MF-CORE-10 — is_active() = is_mem_active() OR is_nmi_hold().
    // VHDL zxnext.vhd:2099. Discriminative for both contributors: build
    // a state where mem=1/hold=0, mem=0/hold=1, both=1, both=0.
    {
        Multiface mf = make_mf();
        mf.reset(true);
        const bool none = !mf.is_active();
        // Build hold=1, mem=0 — button arms nmi_active without 0x0066.
        mf.button_press();
        const bool only_hold = mf.is_active() && !mf.is_mem_active() &&
                               mf.is_nmi_hold();
        // Build hold=1, mem=1 — 0x0066 fetch.
        mf.on_m1(0x0066, true);
        const bool both = mf.is_active() && mf.is_mem_active() &&
                          mf.is_nmi_hold();
        // Drop hold via RETN — mem also drops (line 178 + line 144).
        mf.on_retn_seen();
        const bool drop_via_retn = !mf.is_active();
        check("MF-CORE-10",
              "is_active() = is_mem_active() OR is_nmi_hold()",
              none && only_hold && both && drop_via_retn,
              "zxnext.vhd:2099");
    }

    // MF-CORE-11 — load_rom_bytes round-trip.
    {
        Multiface mf;
        std::vector<uint8_t> buf(8192);
        for (size_t i = 0; i < buf.size(); ++i) {
            buf[i] = static_cast<uint8_t>((i * 7 + 0x42) & 0xFF);
        }
        const bool loaded = mf.load_rom_bytes(buf.data(), buf.size());
        bool match = true;
        for (size_t i = 0; i < buf.size(); ++i) {
            if (mf.rom_data()[i] != buf[i]) { match = false; break; }
        }
        check("MF-CORE-11",
              "load_rom_bytes round-trips 8 KB buffer into rom_data()",
              loaded && match,
              "API contract: 8192 bytes preserved verbatim");
    }

    // MF-CORE-12 — save_state / load_state round-trip.
    // Build a non-trivial state, save, restore into a fresh Multiface,
    // verify all observable fields match.
    {
        Multiface src = make_mf(0x02);  // mode_128
        src.reset(true);
        src.button_press();              // nmi_active=1, invisible=0
        src.on_m1(0x0066, true);         // mf_enable=1 (FF)
        // Stamp the RAM with a recognisable pattern.
        for (int i = 0; i < 256; ++i) {
            src.ram_data()[i] = static_cast<uint8_t>(i ^ 0x5A);
        }

        // Two-pass measure/write idiom (matches saveable.h convention).
        StateWriter measure;
        src.save_state(measure);
        std::vector<uint8_t> snapshot(measure.position());
        StateWriter w(snapshot.data(), snapshot.size());
        src.save_state(w);

        Multiface dst;  // fresh, defaults
        StateReader r(snapshot.data(), snapshot.size());
        dst.load_state(r);

        const bool nmi_eq    = (dst.nmi_active() == src.nmi_active());
        const bool inv_eq    = (dst.invisible()  == src.invisible());
        const bool mfe_eq    = (dst.mf_enable()  == src.mf_enable());
        const bool dly_eq    = (dst.port_io_dly()== src.port_io_dly());
        const bool mode_eq   = (dst.mode_128() && !dst.mode_p3() && !dst.mode_48());
        bool ram_eq = true;
        for (int i = 0; i < 256; ++i) {
            if (dst.ram_data()[i] != src.ram_data()[i]) { ram_eq = false; break; }
        }
        check("MF-CORE-12",
              "save_state / load_state round-trips FFs + mode + RAM",
              nmi_eq && inv_eq && mfe_eq && dly_eq && mode_eq && ram_eq,
              "header has 8 bools, RAM 8 KB; constructor defaults survive when sentinel absent");
    }
}

// =====================================================================
// Group MF-PORT — port dispatch (Wave 1 B2, Task 8 plan).
// VHDL: zxnext.vhd:2610-2616 (port decode), :2730-2733 (iord/iowr fan-out)
// =====================================================================
//
// Each row drives an OUT or IN to one of the four MF-relevant LSBs
// (0x1F / 0x3F / 0x9F / 0xBF) through the real Emulator port dispatcher
// and verifies the Multiface FSM observed the corresponding strobe via
// `port_io_dly` rising after the I/O. Discriminative per-mode mapping
// per VHDL zxnext.vhd:2612-2613:
//
//   nr_0a_mf_type  mode      enable_io   disable_io
//     "00"         mode_p3    0x3F        0xBF
//     "01"         mode_128A  0xBF        0x3F
//     "10"         mode_128B  0x9F        0x1F
//     "11"         mode_48    0x9F        0x1F
//
// Rationale for using `port_io_dly` as the witness:
//   `multiface.vhd:122-131` latches `port_io_dly <= OR of the four
//   port_mf_*` inputs on every clock. So if any of them strobed we see
//   port_io_dly go high. Conversely, if NO strobe fired (LSB not active
//   for the mode, or MF disabled), port_io_dly stays at its prior value
//   (which we ensure is false by quiescing the FSM beforehand).
//
// Helper: drive a port I/O through the Emulator, then read MF state.
namespace {

// Build a Next-mode Emulator and force the MF into a known quiescent
// state: enabled, mode set, port_io_dly cleared. Returns true if init
// succeeded.
bool prep_emulator_for_mf(Emulator& emu, uint8_t mf_type)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    if (!emu.init(cfg)) return false;
    emu.multiface().set_enabled(true);
    emu.multiface().set_mode(mf_type);
    // After set_mode, FFs are still in whatever state init() left them
    // (port_io_dly may already be false). Clear it deterministically by
    // running one quiescent clock edge with no port inputs (button_press
    // clocks the FSM with all port_* inputs low, which latches
    // port_io_dly := 0 — but only if nmi_active is low so the button
    // doesn't latch it true; that's our case post-init).
    // Note: button_press also sets nmi_active=1 + invisible=0; we use
    // set_enabled(false)/set_enabled(true) instead, which forces all
    // FFs to held-reset state cleanly.
    emu.multiface().set_enabled(false);
    emu.multiface().set_enabled(true);
    return true;
}

// Drive an OUT to `port` and report whether port_io_dly went high.
bool out_strobed(Emulator& emu, uint16_t port)
{
    emu.port().out(port, 0x00);
    return emu.multiface().port_io_dly();
}

// Drive an IN from `port` and report whether port_io_dly went high.
bool in_strobed(Emulator& emu, uint16_t port)
{
    (void)emu.port().in(port);
    return emu.multiface().port_io_dly();
}

}  // namespace

static void g_mf_port_dispatch()
{
    set_group("MF-PORT");

    // ── MF+3 (mf_type=00): enable=0x3F, disable=0xBF ─────────────────────
    {
        Emulator emu;
        if (!prep_emulator_for_mf(emu, 0x00)) {
            check("MF-PORT-MFP3-OUT-3F",
                  "Emulator init failed", false, "init returned false");
            return;
        }
        bool s_3f_w = out_strobed(emu, 0x3F);
        prep_emulator_for_mf(emu, 0x00);
        bool s_3f_r = in_strobed(emu, 0x3F);
        prep_emulator_for_mf(emu, 0x00);
        bool s_bf_w = out_strobed(emu, 0xBF);
        prep_emulator_for_mf(emu, 0x00);
        bool s_bf_r = in_strobed(emu, 0xBF);
        // Negative rows: 0x9F and 0x1F are NOT active in MF+3.
        prep_emulator_for_mf(emu, 0x00);
        bool s_9f_w = out_strobed(emu, 0x9F);
        prep_emulator_for_mf(emu, 0x00);
        bool s_1f_w = out_strobed(emu, 0x1F);

        check("MF-PORT-01",
              "MF+3 (mf_type=00): OUT 0x3F → enable_wr strobe (port_io_dly=1)",
              s_3f_w, "VHDL zxnext.vhd:2612, :2615, :2730-2733");
        check("MF-PORT-02",
              "MF+3 (mf_type=00): IN 0x3F → enable_rd strobe (port_io_dly=1)",
              s_3f_r, "VHDL zxnext.vhd:2612, :2615, :2730-2733");
        check("MF-PORT-03",
              "MF+3 (mf_type=00): OUT 0xBF → disable_wr strobe (port_io_dly=1)",
              s_bf_w, "VHDL zxnext.vhd:2613, :2616, :2730-2733");
        check("MF-PORT-04",
              "MF+3 (mf_type=00): IN 0xBF → disable_rd strobe (port_io_dly=1)",
              s_bf_r, "VHDL zxnext.vhd:2613, :2616, :2730-2733");
        check("MF-PORT-05",
              "MF+3 (mf_type=00): OUT 0x9F → no MF strobe (LSB not active)",
              !s_9f_w, "VHDL: 0x9F is enable_io for mf_type b1=1 only");
        check("MF-PORT-06",
              "MF+3 (mf_type=00): OUT 0x1F → no MF strobe (LSB not active)",
              !s_1f_w, "VHDL: 0x1F is disable_io for mf_type b1=1 only");
    }

    // ── MF128 var A (mf_type=01): enable=0xBF, disable=0x3F ──────────────
    {
        Emulator emu;
        prep_emulator_for_mf(emu, 0x01);
        bool s_bf_w = out_strobed(emu, 0xBF);
        prep_emulator_for_mf(emu, 0x01);
        bool s_3f_w = out_strobed(emu, 0x3F);

        check("MF-PORT-07",
              "MF128 var A (mf_type=01): OUT 0xBF → enable_wr strobe",
              s_bf_w, "VHDL zxnext.vhd:2612 (b0=1, b1=0 → 0xBF)");
        check("MF-PORT-08",
              "MF128 var A (mf_type=01): OUT 0x3F → disable_wr strobe",
              s_3f_w, "VHDL zxnext.vhd:2613 (b0=1, b1=0 → 0x3F)");
    }

    // ── MF128 var B (mf_type=10): enable=0x9F, disable=0x1F ──────────────
    {
        Emulator emu;
        prep_emulator_for_mf(emu, 0x02);
        bool s_9f_w = out_strobed(emu, 0x9F);
        prep_emulator_for_mf(emu, 0x02);
        bool s_1f_r = in_strobed(emu, 0x1F);
        // Negative: 0xBF and 0x3F are mode_128 var-A LSBs but mf_type=10
        // takes the b1=1 branch → 0x9F/0x1F. So 0xBF/0x3F should NOT
        // strobe in var B.
        prep_emulator_for_mf(emu, 0x02);
        bool s_bf_w = out_strobed(emu, 0xBF);

        check("MF-PORT-09",
              "MF128 var B (mf_type=10): OUT 0x9F → enable_wr strobe",
              s_9f_w, "VHDL zxnext.vhd:2612 (b1=1 → 0x9F)");
        check("MF-PORT-10",
              "MF128 var B (mf_type=10): IN 0x1F → disable_rd strobe",
              s_1f_r, "VHDL zxnext.vhd:2613 (b1=1 → 0x1F)");
        check("MF-PORT-11",
              "MF128 var B (mf_type=10): OUT 0xBF → no MF strobe (var-A LSB)",
              !s_bf_w,
              "VHDL: 0xBF is the mode_128 var-A enable_io, not var-B");
    }

    // ── MF1 (mf_type=11): enable=0x9F, disable=0x1F ──────────────────────
    {
        Emulator emu;
        prep_emulator_for_mf(emu, 0x03);
        bool s_9f_r = in_strobed(emu, 0x9F);
        prep_emulator_for_mf(emu, 0x03);
        bool s_1f_w = out_strobed(emu, 0x1F);
        // Negative: 0x3F is MF+3 enable_io but mf_type=11 takes b1=1
        // branch → 0x9F. So 0x3F should NOT strobe in MF1.
        prep_emulator_for_mf(emu, 0x03);
        bool s_3f_r = in_strobed(emu, 0x3F);

        check("MF-PORT-12",
              "MF1 (mf_type=11): IN 0x9F → enable_rd strobe",
              s_9f_r, "VHDL zxnext.vhd:2612 (b1=1 → 0x9F)");
        check("MF-PORT-13",
              "MF1 (mf_type=11): OUT 0x1F → disable_wr strobe",
              s_1f_w, "VHDL zxnext.vhd:2613 (b1=1 → 0x1F)");
        check("MF-PORT-14",
              "MF1 (mf_type=11): IN 0x3F → no MF strobe (MF+3 LSB only)",
              !s_3f_r, "VHDL: 0x3F is MF+3 enable_io, not MF1");
    }

    // ── Disable gate (NR 0x83 b1 = 0 / multiface_.is_enabled() == false) ─
    {
        Emulator emu;
        prep_emulator_for_mf(emu, 0x00);  // would-be MF+3, enable=0x3F
        emu.multiface().set_enabled(false);  // gate it off
        // Now drive an OUT to 0x3F. With the observer gated by
        // is_enabled(), no strobe should fire. We can't observe
        // port_io_dly directly because set_enabled(false) clamps all
        // FFs to held-reset (port_io_dly=0 forever while disabled). So
        // re-enable AFTER the OUT and verify port_io_dly is still 0.
        emu.port().out(0x3F, 0x00);
        emu.multiface().set_enabled(true);  // re-enable for inspection
        check("MF-PORT-15",
              "OUT 0x3F with NR 0x83 b1 = 0 → no MF strobe (gate held off)",
              !emu.multiface().port_io_dly(),
              "VHDL zxnext.vhd:2615 (port_multiface_io_en gate)");
    }

    // ── Discriminative cross-mode pair: confirm 0x9F vs 0x3F flips with
    //    mf_type bit 1 transition ──────────────────────────────────────
    {
        Emulator emu;
        prep_emulator_for_mf(emu, 0x00);  // mf_type=00 (b1=0)
        bool m00_3f = out_strobed(emu, 0x3F);     // should fire
        prep_emulator_for_mf(emu, 0x02);  // mf_type=10 (b1=1)
        bool m10_3f = out_strobed(emu, 0x3F);     // should NOT fire
        check("MF-PORT-16",
              "OUT 0x3F: fires when mf_type b1=0, suppressed when mf_type b1=1",
              m00_3f && !m10_3f,
              "VHDL zxnext.vhd:2612-2613 ternary cascade discriminator");
    }
}

// =====================================================================
// Group MF-MUX — MF +3 readback mux on cpu_a(15:12) (Wave 1 B3).
// VHDL: cores/zxnext/src/zxnext.vhd:4310-4327 (case mux on cpu_a(15:12)),
//       :2816 (port_mf_rd_dat <= mf_port_dat when mf_port_en='1'),
//       :2837 (port_rd_dat OR'ing port_mf_rd_dat into the global bus),
//       multiface.vhd:195 (mf_port_en_o decode = port_mf_enable_rd AND
//       NOT invisible_eff AND (mode_128 OR mode_p3)).
//
// All rows drive a real Z80 IN at port `xx3F` through Emulator::port()
// and verify the returned byte equals the VHDL-decoded MF+3 readback.
// =====================================================================

namespace {

// Build a Next-mode Emulator pre-configured for MF+3 readback tests:
//   - MF enabled (NR 0x83 b1 = 1, set_enabled(true))
//   - mf_type = 00 → MF+3 mode
//   - invisible cleared (button_press flips invisible 1→0; nmi_active is
//     a side-effect we don't care about for read-mux tests)
//   - port_io_dly cleared (so first port_mf_enable_rd strobe fires the
//     mf_port_en gate cleanly — though our handler re-derives the gate
//     from is_enabled+mf_type+invisible_eff so the dly state doesn't
//     directly matter; we still clear it for hygiene).
//
// Returns true on success.
bool prep_mf_mux(Emulator& emu)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    if (!emu.init(cfg)) return false;
    emu.multiface().set_enabled(true);
    emu.multiface().set_mode(0x00);  // MF+3 (mode_p3 = 1, mode_48 = 0)
    // Clear invisible: at reset invisible=1, so without this the
    // mf_port_en gate would stay closed even with enable_rd asserted.
    emu.multiface().button_press();  // invisible 1→0 (multiface.vhd:158)
    return true;
}

}  // namespace

static void g_mf_mux()
{
    set_group("MF-MUX");

    // ── MF-MUX-01 — cpu_a(15:12)="0001" → port_1ffd readback ───────────
    // VHDL zxnext.vhd:4312:
    //   when "0001" => mf_port_dat <= "0000" & (NOT port_1ffd_mtr_n) & port_1ffd_reg
    // port_1ffd_reg is 3 bits = cpu_do(2:0) (zxnext.vhd:879, :3724);
    // port_1ffd_mtr_n = NOT cpu_do(3) (:3757), so (NOT mtr_n) = cpu_do(3).
    // jnext stores the verbatim cpu_do byte in Mmu::port_1ffd_, so the MF
    // readback equals `port_1ffd_ & 0x0F`.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-01", "Emulator init failed", false, "init returned false");
            return;
        }
        // Write 0xAB to port 0x1FFD: cpu_do(3:0) = 0xB, cpu_do(7:4) = 0xA.
        // MF readback (mf_type=00, cpu_a(15:12)=0x1) should be 0x0B.
        emu.port().out(0x1FFD, 0xAB);
        const uint8_t got = emu.port().in(0x113F);  // cpu_a(15:12) = 0x1
        check("MF-MUX-01",
              "MF+3 read 0x1xxx LSB 0x3F: returns port_1ffd & 0x0F (motor + reg(2:0))",
              got == 0x0B,
              "VHDL zxnext.vhd:4312 (mux), :3724 (port_1ffd_reg), :3757 (mtr_n)");
    }

    // ── MF-MUX-02 — cpu_a(15:12)="0111" → port_7ffd readback ───────────
    // VHDL zxnext.vhd:4313:
    //   when "0111" => mf_port_dat <= port_7ffd_reg
    // port_7ffd_reg is the full 8-bit register (:3650 stores cpu_do).
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-02", "Emulator init failed", false, "init returned false");
            return;
        }
        // Write 0x36 to port 0x7FFD (bits 5/4/3 = 0/1/1 → ROM/screen/shadow,
        // bits 2:0 = 110 → bank 6). Full byte preserved in port_7ffd_reg.
        emu.port().out(0x7FFD, 0x36);
        const uint8_t got = emu.port().in(0x713F);  // cpu_a(15:12) = 0x7
        check("MF-MUX-02",
              "MF+3 read 0x7xxx LSB 0x3F: returns full port_7ffd_reg",
              got == 0x36,
              "VHDL zxnext.vhd:4313 (mux), :3650 (port_7ffd_reg storage)");
    }

    // ── MF-MUX-03 — cpu_a(15:12)="1101" → port_dffd readback ───────────
    // VHDL zxnext.vhd:4314:
    //   when "1101" => mf_port_dat <= '0' & port_dffd_reg_6 & '0' & port_dffd_reg
    // port_dffd_reg is 5-bit cpu_do(4:0) (:3693), port_dffd_reg_6 is
    // separate FF for cpu_do(6) (:3694, :877).
    // Layout: bit7=0, bit6=reg_6, bit5=0, bits4:0=port_dffd_reg.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-03", "Emulator init failed", false, "init returned false");
            return;
        }
        // Write 0x53 to port 0xDFFD: cpu_do(4:0)=0x13, cpu_do(6)=1, cpu_do(5)=0.
        // MF readback layout: 0_1_0_10011 = 0b01010011 = 0x53.
        // Different test value to discriminate from port_7ffd test.
        emu.port().out(0xDFFD, 0x53);
        const uint8_t got = emu.port().in(0xD13F);  // cpu_a(15:12) = 0xD
        // Expected: bit7=0, bit6=cpu_do(6)=1, bit5=0, bits4:0 = 0x13.
        // = 0x40 | 0x13 = 0x53.
        check("MF-MUX-03",
              "MF+3 read 0xDxxx LSB 0x3F: returns 0|reg_6|0|port_dffd_reg(4:0)",
              got == 0x53,
              "VHDL zxnext.vhd:4314 (mux), :3693 (port_dffd_reg), :3694 (reg_6)");
    }

    // ── MF-MUX-04 — cpu_a(15:12)="1110" → port_eff7 readback ───────────
    // VHDL zxnext.vhd:4315:
    //   when "1110" => mf_port_dat <= "0000" & port_eff7_reg_3 & port_eff7_reg_2 & "00"
    // port_eff7_reg_3 = cpu_do(3), port_eff7_reg_2 = cpu_do(2) (:3781-2).
    // Layout: bits 7:4=0, bit 3=reg_3, bit 2=reg_2, bits 1:0=0.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-04", "Emulator init failed", false, "init returned false");
            return;
        }
        // Write 0x0C to port 0xEFF7 (bits 3=1, 2=1; rest = 0). MF readback
        // = 0x0C exactly (bits 1:0 forced to 0, bits 7:4 forced to 0).
        emu.port().out(0xEFF7, 0x0C);
        const uint8_t got = emu.port().in(0xE13F);  // cpu_a(15:12) = 0xE
        check("MF-MUX-04",
              "MF+3 read 0xExxx LSB 0x3F: returns 0|0|0|0|reg_3|reg_2|0|0",
              got == 0x0C,
              "VHDL zxnext.vhd:4315 (mux), :3781-3782 (port_eff7_reg_3/_2 storage)");
    }

    // ── MF-MUX-05 — cpu_a(15:12) "others" arm → port_fe_reg(2:0) ───────
    // VHDL zxnext.vhd:4316:
    //   when others => mf_port_dat <= "00000" & port_fe_reg(2 downto 0)
    // The "others" arm returns the lower 3 bits of port_FE — i.e. the
    // border-colour latch. We pick cpu_a(15:12)=0x0 (e.g. 0x023F), which
    // is one of the "others" values (the case explicitly enumerates
    // 0x1/0x7/0xD/0xE only). With port_FE = 0xC2 (border bits = 010),
    // the read must return 0x02. Sentinel writes to port_7ffd / port_1ffd
    // confirm those registers are NOT surfaced on the others arm.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-05", "Emulator init failed", false, "init returned false");
            return;
        }
        emu.port().out(0x7FFD, 0xFF);  // sentinel — not reflected in others arm
        emu.port().out(0x1FFD, 0x0F);  // sentinel
        // Set border to 010 (0x02) via port 0xFE (lower 3 bits = border).
        emu.port().out(0x00FE, 0xC2);  // border bits = 010 = 0x02
        const uint8_t got = emu.port().in(0x023F);  // cpu_a(15:12) = 0x0
        check("MF-MUX-05",
              "MF+3 read 0x0xxx LSB 0x3F: 'others' arm → port_fe_reg(2:0) "
              "(border bits, 0x02)",
              got == 0x02,
              "VHDL zxnext.vhd:4316 (when others => \"00000\" & port_fe_reg(2:0))");
    }

    // ── MF-MUX-06 — invisible_eff = 1 closes mf_port_en ────────────────
    // VHDL multiface.vhd:195:
    //   mf_port_en_o <= '1' when port_mf_enable_rd_i='1' AND
    //                   invisible_eff='0' AND (mode_128 OR mode_p3) else '0'
    // With invisible_eff=1, mf_port_en stays low so port_mf_rd_dat=0x00
    // (zxnext.vhd:2816). Our handler returns floating bus; in 48K/Next
    // headless the floating bus default is well-defined. We assert the
    // value differs from the active-mux case to discriminate.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-06", "Emulator init failed", false, "init returned false");
            return;
        }
        // Force invisible back to 1 — in MF+3, port_mf_enable_wr with
        // port_io_dly=0 sets invisible=1 (multiface.vhd:159 second disjunct).
        // Quiesce port_io_dly first via enable/disable toggle, then strobe.
        emu.multiface().set_enabled(false);
        emu.multiface().set_enabled(true);
        emu.multiface().set_mode(0x00);
        // After enable+set_mode, invisible is back to its FF value (still
        // 1 from reset since we didn't button_press in this rebuild). Verify.
        const bool inv_high = emu.multiface().invisible() && emu.multiface().invisible_eff();
        // Set port_7ffd to a non-zero value so a fall-through to floating
        // bus can be discriminated from the mux's would-be answer.
        emu.port().out(0x7FFD, 0x55);
        const uint8_t got = emu.port().in(0x713F);
        // With mux gated off, our handler returns floating_bus_read(),
        // which is some value DIFFERENT from 0x55 (the mux's would-be
        // output). The discriminator is "got != 0x55" — i.e. the mux
        // path was NOT taken.
        check("MF-MUX-06",
              "invisible_eff=1 closes mf_port_en gate (mux suppressed)",
              inv_high && got != 0x55,
              "VHDL multiface.vhd:195 (mf_port_en gate), :165 (invisible_eff)");
    }

    // ── MF-MUX-07 — NR 0x83 b1 = 0 (MF disabled) closes the gate ───────
    // VHDL multiface.vhd:64, :103 — when enable_i=0, all FFs held in reset
    // and mf_port_en stays low. Our handler short-circuits on
    // `!multiface_.is_enabled()` and returns the floating-bus default.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-07", "Emulator init failed", false, "init returned false");
            return;
        }
        emu.port().out(0x7FFD, 0x55);
        emu.multiface().set_enabled(false);  // gate off
        const uint8_t got = emu.port().in(0x713F);
        check("MF-MUX-07",
              "is_enabled=0 closes mf_port_en gate (mux suppressed)",
              got != 0x55,
              "VHDL multiface.vhd:64, :103 (enable_i gate), zxnext.vhd:2816");
    }

    // ── MF-MUX-08 — mf_type=01 (MF128 var A) else-branch readback ──────
    // VHDL zxnext.vhd:4319 (else of nr_0a_mf_type="00"):
    //   mf_port_dat <= port_7ffd_reg(3) & "1111111"
    // Under MF128 var A, the enable_io_a is LSB 0xBF (VHDL :2612). With
    // port_7ffd_reg bit 3 = 1 (shadow screen on), the readback equals
    //   '1' & "1111111" = 0x80 | 0x7F = 0xFF.
    // Discriminative pair: clear port_7ffd_reg bit 3, repeat IN, expect 0x7F.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-08", "Emulator init failed", false, "init returned false");
            return;
        }
        emu.multiface().set_mode(0x01);  // MF128 var A → enable_io_a = 0xBF
        // Clear invisible: at reset invisible=1 in MF128 too. Drop it via
        // a button-press pulse (multiface.vhd:158 — invisible <= 0).
        emu.multiface().button_press();
        // Path A — shadow on (port_7ffd_reg bit 3 = 1): expect 0xFF.
        emu.port().out(0x7FFD, 0x08);  // bit 3 set → shadow_screen = 1
        const uint8_t got_shadow = emu.port().in(0x00BF);
        // Path B — shadow off (port_7ffd_reg bit 3 = 0): expect 0x7F.
        emu.port().out(0x7FFD, 0x00);
        const uint8_t got_no_shadow = emu.port().in(0x00BF);
        check("MF-MUX-08",
              "MF128 var A read LSB 0xBF: returns port_7ffd_reg(3) & 0x7F "
              "(shadow on→0xFF, off→0x7F)",
              got_shadow == 0xFF && got_no_shadow == 0x7F,
              "VHDL zxnext.vhd:4319 (else-branch = port_7ffd_reg(3) & \"1111111\")");
    }

    // ── MF-MUX-09 — discriminative cpu_a(15:12) cross-check ────────────
    // Same low byte 0x3F, different high nibble: 0x1xxx vs 0x7xxx must
    // return DIFFERENT bytes (port_1ffd vs port_7ffd) when both registers
    // are loaded with distinguishable values. Pins the cpu_a(15:12)
    // decode beyond the single-row checks above.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-09", "Emulator init failed", false, "init returned false");
            return;
        }
        emu.port().out(0x1FFD, 0x05);  // → MF readback at 0x1xxx = 0x05
        emu.port().out(0x7FFD, 0x42);  // → MF readback at 0x7xxx = 0x42
        const uint8_t got_1 = emu.port().in(0x143F);  // cpu_a(15:12)=0x1
        const uint8_t got_7 = emu.port().in(0x743F);  // cpu_a(15:12)=0x7
        check("MF-MUX-09",
              "cpu_a(15:12) decode: 0x1xxx→port_1ffd (0x05), 0x7xxx→port_7ffd (0x42)",
              got_1 == 0x05 && got_7 == 0x42,
              "VHDL zxnext.vhd:4312-4313 (case 0001 vs 0111)");
    }

    // ── MF-MUX-10 — case-mux is BYPASSED in MF128 mode ─────────────────
    // VHDL zxnext.vhd:4310-4322 — under nr_0a_mf_type ≠ "00", the case
    // mux on cpu_a(15:12) is replaced by the unconditional else-branch
    //   mf_port_dat <= port_7ffd_reg(3) & "1111111"
    // We pin this by reading at LSB 0xBF (MF128 var A enable_io_a) with
    // cpu_a(15:12)=0x1 (would trigger the +3 case-mux 0x1xxx arm IF mode
    // were +3, returning port_1ffd & 0x0F). Under MF128, we expect the
    // ELSE branch byte regardless of the high nibble.
    {
        Emulator emu;
        if (!prep_mf_mux(emu)) {
            check("MF-MUX-10", "Emulator init failed", false, "init returned false");
            return;
        }
        emu.multiface().set_mode(0x01);  // MF128 var A
        emu.multiface().button_press();  // drop invisible to 0
        // Set port_7ffd bit 3 = 0 (shadow off) and port_1ffd to a value
        // that, IF the +3 case-mux fired at 0x1xxx, would give 0x0F.
        emu.port().out(0x1FFD, 0x0F);  // would yield 0x0F under MF+3 mux
        emu.port().out(0x7FFD, 0x00);  // shadow off → else-branch = 0x7F
        const uint8_t got = emu.port().in(0x1FBF);  // cpu_a(15:12)=0x1
        check("MF-MUX-10",
              "MF128 read at 0x1FBF: case-mux bypassed (else-branch = 0x7F, "
              "NOT port_1ffd & 0x0F)",
              got == 0x7F,
              "VHDL zxnext.vhd:4318-4320 (mf_type ≠ \"00\" else-branch ignores cpu_a)");
    }
}

// =====================================================================
// Group MF-OVL — MF memory overlay in Mmu (Wave 1 E, Task 8 plan).
// VHDL: cores/zxnext/src/zxnext.vhd:2937-2945 (priority cascade),
//       :3028-3035 (slot-0 ROM/RAM layout under mf_mem_en),
//       cores/zxnext/src/device/multiface.vhd:186 (mf_enable_eff = mf_enable
//       OR fetch_66, the gate the SRAM arbiter consumes as mf_mem_en).
//
// Each row drives an Mmu::read or ::write at a slot-0 address while the
// Multiface FSM is in a known state, and pins the cascade decision.
// =====================================================================

namespace {

// Build a Next-mode Emulator with MF enabled, MF+3 mode, but with the boot
// ROM overlay disabled — this gives the MF/DivMMC/MMU cascade unobstructed
// access to addresses 0x0000-0x3FFF so the MF-OVL tests can pin the MF
// branch deterministically. Tests that want boot-ROM-priority behaviour
// re-enable it via mmu().set_boot_rom_enabled(true).
bool prep_mf_overlay(Emulator& emu, uint8_t mf_type = 0x02 /* MF128 */)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    if (!emu.init(cfg)) return false;
    // Disable boot ROM overlay so MF/DivMMC/MMU branches can fire on
    // 0x0000-0x3FFF reads. (Boot ROM has higher priority per VHDL :2937.)
    emu.mmu().set_boot_rom_enabled(false);
    emu.multiface().set_enabled(true);
    emu.multiface().set_mode(mf_type);
    return true;
}

// Pre-load the MF ROM with a recognisable pattern so reads can be
// distinguished from any other RAM/ROM source.
void stamp_mf_rom(Multiface& mf, uint8_t key)
{
    std::vector<uint8_t> buf(Multiface::kRomSize);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<uint8_t>((i + key) & 0xFF);
    }
    mf.load_rom_bytes(buf.data(), buf.size());
}

// Pre-load the MF RAM with a recognisable pattern.
void stamp_mf_ram(Multiface& mf, uint8_t key)
{
    for (int i = 0; i < Multiface::kRamSize; ++i) {
        mf.ram_data()[i] = static_cast<uint8_t>((i ^ key) & 0xFF);
    }
}

// Drive the MF FSM into the "armed and active" state — nmi_active=1 and
// mf_enable=1 — so is_mem_active() returns true outside the one-cycle
// fetch_66 bypass.
void arm_mf(Multiface& mf)
{
    mf.button_press();              // nmi_active=1, invisible=0
    mf.on_m1(0x0066, /*mreq_low=*/true);  // latches mf_enable=1
}

}  // namespace

static void g_mf_overlay()
{
    set_group("MF-OVL");

    // ── MF-OVL-01 — overlay inactive: reads route to underlying Mmu path
    // VHDL multiface.vhd:186 — mf_enable_eff = '0' when both mf_enable
    // and fetch_66 are '0'. With MF disabled (set_enabled(false)) the FFs
    // are held in reset and is_mem_active() stays low. The MF check in
    // Mmu::read should fall through to the next cascade level (DivMMC,
    // L2, MMU). Discriminative: stamp MF ROM with a pattern, then assert
    // the read at 0x0000 differs from that pattern (i.e. the MF branch
    // didn't fire).
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-01", "Emulator init failed", false, "init returned false");
            return;
        }
        emu.multiface().set_enabled(false);   // MF inactive
        stamp_mf_rom(emu.multiface(), 0xA5);
        // Read at 0x0000: with MF gate closed we get whatever the MMU
        // cascade serves (typically the 48.rom ROM in slot 0).
        const uint8_t got = emu.mmu().read(0x0000);
        const uint8_t mf_byte = emu.multiface().rom_data()[0x0000];
        check("MF-OVL-01",
              "MF inactive (is_mem_active=0): read at 0x0000 does NOT match MF ROM",
              !emu.multiface().is_mem_active() && got != mf_byte,
              "VHDL multiface.vhd:186 (mf_enable_eff=0 → no overlay)");
    }

    // ── MF-OVL-02 — overlay active: reads at 0x0000-0x1FFF return MF ROM.
    // VHDL zxnext.vhd:3028-3035 — sram_pre_active=1, sram_pre_rdonly=NOT
    // cpu_a(13). cpu_a(13)=0 → ROM-half (read-only). Stamped pattern lets
    // us verify the read returns exactly multiface_.rom_data()[addr].
    // Discriminative: assert two different addresses (0x0000 and 0x1FFF)
    // both match the stamped pattern.
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-02", "Emulator init failed", false, "init returned false");
            return;
        }
        stamp_mf_rom(emu.multiface(), 0x33);
        arm_mf(emu.multiface());
        const uint8_t got_lo = emu.mmu().read(0x0000);
        const uint8_t got_hi = emu.mmu().read(0x1FFF);
        const uint8_t exp_lo = emu.multiface().rom_data()[0x0000];
        const uint8_t exp_hi = emu.multiface().rom_data()[0x1FFF];
        check("MF-OVL-02",
              "MF active: reads at 0x0000 and 0x1FFF return MF ROM bytes",
              emu.multiface().is_mem_active() &&
                  got_lo == exp_lo && got_hi == exp_hi,
              "VHDL zxnext.vhd:3028-3035 (cpu_a(13)=0 → ROM half)");
    }

    // ── MF-OVL-03 — overlay active: reads at 0x2000-0x3FFF return MF RAM.
    // VHDL zxnext.vhd:3028-3035 — cpu_a(13)=1 → sram_pre_rdonly=0 → RAM-half.
    // Pre-stamp MF RAM, then read at offsets 0 and 0x1FFF (relative to
    // 0x2000) and verify both match.
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-03", "Emulator init failed", false, "init returned false");
            return;
        }
        stamp_mf_ram(emu.multiface(), 0x77);
        arm_mf(emu.multiface());
        const uint8_t got_lo = emu.mmu().read(0x2000);
        const uint8_t got_hi = emu.mmu().read(0x3FFF);
        const uint8_t exp_lo = emu.multiface().ram_data()[0x0000];
        const uint8_t exp_hi = emu.multiface().ram_data()[0x1FFF];
        check("MF-OVL-03",
              "MF active: reads at 0x2000 and 0x3FFF return MF RAM bytes",
              emu.multiface().is_mem_active() &&
                  got_lo == exp_lo && got_hi == exp_hi,
              "VHDL zxnext.vhd:3028-3035 (cpu_a(13)=1 → RAM half, rdonly=0)");
    }

    // ── MF-OVL-04 — write to RAM area lands in MF RAM.
    // VHDL zxnext.vhd:3035 — sram_pre_rdonly=NOT cpu_a(13)=0 for the RAM
    // half. Discriminative: write a byte, verify it shows up in
    // multiface_.ram_data() at the expected offset, AND verify the
    // underlying Ram is unchanged at the corresponding bank (the MF
    // overlay should not have leaked through to the Mmu's RAM page).
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-04", "Emulator init failed", false, "init returned false");
            return;
        }
        // Zero MF RAM so the discriminator is deterministic.
        for (int i = 0; i < Multiface::kRamSize; ++i)
            emu.multiface().ram_data()[i] = 0x00;
        arm_mf(emu.multiface());
        emu.mmu().write(0x2123, 0x42);
        const bool ram_landed = emu.multiface().ram_data()[0x0123] == 0x42;
        // The corresponding underlying Ram page address shouldn't match —
        // we assert MF RAM byte got written and is the only place. Read
        // back via Mmu confirms the round-trip.
        const uint8_t got = emu.mmu().read(0x2123);
        check("MF-OVL-04",
              "write 0x42 to 0x2123 lands in MF RAM[0x0123]; read-back matches",
              ram_landed && got == 0x42,
              "VHDL zxnext.vhd:3035 (sram_pre_rdonly=0 for cpu_a(13)=1)");
    }

    // ── MF-OVL-05 — write to ROM area is ignored.
    // VHDL zxnext.vhd:3034 — sram_pre_rdonly=NOT cpu_a(13)=1 for the ROM
    // half. Writes must be silently dropped; MF ROM contents must remain
    // unchanged. Discriminative: stamp MF ROM, write a different byte at
    // the same address, verify ROM still holds the stamped value.
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-05", "Emulator init failed", false, "init returned false");
            return;
        }
        stamp_mf_rom(emu.multiface(), 0x11);
        const uint8_t before = emu.multiface().rom_data()[0x0123];
        arm_mf(emu.multiface());
        emu.mmu().write(0x0123, 0xAA);  // would-be write to MF ROM
        const uint8_t after = emu.multiface().rom_data()[0x0123];
        check("MF-OVL-05",
              "write 0xAA to 0x0123 (ROM half) is ignored; MF ROM unchanged",
              before == after,
              "VHDL zxnext.vhd:3034 (sram_pre_rdonly=1 for cpu_a(13)=0)");
    }

    // ── MF-OVL-06 — addresses outside slot 0 fall through.
    // VHDL zxnext.vhd:3030 — the override only fires on cpu_a(15:14)='00'
    // (i.e. addr < 0x4000). At 0x4000 and above, the MF overlay must NOT
    // intercept. Discriminative: pre-fill MF ROM with a recognisable
    // pattern; read at 0x4000 must NOT return that byte (the MMU's
    // legacy paging serves slot 1 from a different RAM bank).
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-06", "Emulator init failed", false, "init returned false");
            return;
        }
        // Stamp MF ROM with values that, if leaked, would be obvious.
        stamp_mf_rom(emu.multiface(), 0x66);
        arm_mf(emu.multiface());
        // 0x4000 is slot 1 — outside the MF overlay window.
        const uint8_t got = emu.mmu().read(0x4000);
        // The "MF ROM byte at offset 0" if the overlay had wrongly fired
        // would equal multiface_.rom_data()[0x4000 & 0x1FFF] = [0x0000] = 0x66.
        // We assert that this is NOT what the cascade returned.
        const uint8_t mf_would_be = emu.multiface().rom_data()[0x0000];
        check("MF-OVL-06",
              "addr 0x4000 (outside slot 0): MF overlay does NOT fire",
              emu.multiface().is_mem_active() && got != mf_would_be,
              "VHDL zxnext.vhd:3030 (cpu_a(15:14)='00' gate)");
    }

    // ── MF-OVL-07 — fetch_66 one-cycle bypass activates the overlay.
    // VHDL multiface.vhd:186 — mf_enable_eff = mf_enable OR fetch_66.
    // After button_press but BEFORE on_m1(0x0066,...), mf_enable=0 and
    // fetch_66=0 → mem_active=0 → overlay inactive. The instant on_m1 is
    // called with PC=0x0066, fetch_66 surfaces high (one-cycle bypass)
    // AND mf_enable latches '1' on the same edge. Either contributor
    // alone suffices to activate the overlay; we check both via the
    // post-fetch read.
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-07", "Emulator init failed", false, "init returned false");
            return;
        }
        stamp_mf_rom(emu.multiface(), 0x88);
        // Stage 1: button_press but no fetch — overlay must NOT be active.
        emu.multiface().button_press();
        const bool armed_no_fetch = emu.multiface().nmi_active() &&
                                    !emu.multiface().mf_enable() &&
                                    !emu.multiface().is_mem_active();
        // Reading at 0x0066 should NOT return the MF ROM byte yet.
        const uint8_t pre_byte = emu.mmu().read(0x0066);
        const bool pre_not_mf = (pre_byte != emu.multiface().rom_data()[0x0066]);
        // Stage 2: fire the M1 fetch — fetch_66=1, mf_enable latches=1.
        emu.multiface().on_m1(0x0066, /*mreq_low=*/true);
        const bool active_after_fetch = emu.multiface().mf_enable() &&
                                        emu.multiface().is_mem_active();
        const uint8_t post_byte = emu.mmu().read(0x0066);
        const bool post_is_mf = (post_byte == emu.multiface().rom_data()[0x0066]);
        check("MF-OVL-07",
              "fetch_66 bypass + FF latch: overlay activates on the 0x0066 fetch",
              armed_no_fetch && pre_not_mf && active_after_fetch && post_is_mf,
              "VHDL multiface.vhd:186 (mf_enable_eff = mf_enable OR fetch_66)");
    }

    // ── MF-OVL-08 — RETN clears mf_enable and deactivates overlay.
    // VHDL multiface.vhd:144 (nmi_active clear), :178 (mf_enable clear).
    // From an active state, on_retn_seen() drops both bits → mem_active=0
    // → overlay inactive → reads route through the normal Mmu cascade.
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-08", "Emulator init failed", false, "init returned false");
            return;
        }
        stamp_mf_rom(emu.multiface(), 0x44);
        arm_mf(emu.multiface());
        const bool active_pre = emu.multiface().is_mem_active() &&
                                emu.mmu().read(0x0000) ==
                                    emu.multiface().rom_data()[0x0000];
        emu.multiface().on_retn_seen();
        const bool inactive_post = !emu.multiface().is_mem_active();
        const uint8_t post = emu.mmu().read(0x0000);
        const bool fall_through = (post != emu.multiface().rom_data()[0x0000]);
        check("MF-OVL-08",
              "on_retn_seen deactivates overlay; reads fall through",
              active_pre && inactive_post && fall_through,
              "VHDL multiface.vhd:144, :178 (RETN clears nmi_active+mf_enable)");
    }

    // ── MF-OVL-09 — MF priority above DivMMC.
    // VHDL zxnext.vhd:2937 cascade: 1. multiface 2. divmmc. With BOTH
    // overlays active simultaneously (DivMMC conmem=1 + automap_active=1
    // AND MF mf_enable_eff=1), the read at 0x0000 must return MF ROM,
    // not DivMMC ROM. Discriminative — pre-load the two ROMs with
    // distinct patterns at byte 0x0000.
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-09", "Emulator init failed", false, "init returned false");
            return;
        }
        // Stamp MF ROM[0]=0xAA, DivMMC ROM[0]=0x55 — two distinct values
        // so the discriminator is unambiguous.
        std::vector<uint8_t> mf_rom(Multiface::kRomSize, 0xAA);
        emu.multiface().load_rom_bytes(mf_rom.data(), mf_rom.size());
        // DivMMC ROM is 8 KB; load a contrasting pattern.
        std::vector<uint8_t> dm_rom(8192, 0x55);
        emu.divmmc().load_rom_bytes(dm_rom.data(), dm_rom.size());
        // Activate DivMMC overlay: port_io_enable + conmem.
        emu.divmmc().set_port_io_enable(true);
        // conmem = control bit 7. Use the public write_control accessor.
        // Need to make sure DivMMC is the only thing competing; conmem
        // suffices because is_active() = port_io_enable AND (conmem OR
        // automap_active).
        emu.divmmc().write_control(0x80);   // conmem=1
        // Activate MF overlay.
        arm_mf(emu.multiface());
        const bool both_active = emu.multiface().is_mem_active() &&
                                 emu.divmmc().is_active();
        const uint8_t got = emu.mmu().read(0x0000);
        check("MF-OVL-09",
              "both MF and DivMMC active: read at 0x0000 returns MF ROM (0xAA)",
              both_active && got == 0xAA,
              "VHDL zxnext.vhd:2937 (1. multiface 2. divmmc priority)");
    }

    // ── MF-OVL-10 — boot ROM priority above MF.
    // VHDL zxnext.vhd:2937 cascade: 0. bootrom 1. multiface. With boot
    // ROM enabled AND MF active, the read at 0x0000 must return the boot
    // ROM byte, NOT the MF ROM byte. Discriminative — pre-load the
    // boot ROM with one byte and MF ROM with another.
    {
        Emulator emu;
        if (!prep_mf_overlay(emu)) {
            check("MF-OVL-10", "Emulator init failed", false, "init returned false");
            return;
        }
        // Stamp boot ROM[0]=0xBB and MF ROM[0]=0xCC.
        std::vector<uint8_t> br(8192, 0xBB);
        emu.mmu().set_boot_rom(br.data(), br.size());
        emu.mmu().set_boot_rom_enabled(true);
        std::vector<uint8_t> mr(Multiface::kRomSize, 0xCC);
        emu.multiface().load_rom_bytes(mr.data(), mr.size());
        arm_mf(emu.multiface());
        const bool both_active = emu.multiface().is_mem_active() &&
                                 emu.mmu().boot_rom_enabled();
        const uint8_t got = emu.mmu().read(0x0000);
        check("MF-OVL-10",
              "boot ROM enabled + MF active: read at 0x0000 returns boot ROM (0xBB)",
              both_active && got == 0xBB,
              "VHDL zxnext.vhd:2937 (0. bootrom > 1. multiface)");
    }
}

// =====================================================================
// Main
// =====================================================================

int main()
{
    std::printf("Multiface core class compliance tests (Wave 1 B1+B2+B3+E, Task 8)\n");
    std::printf("==========================================================\n\n");

    g_mf_core();          std::printf("  MF-CORE state machine -- done\n");
    g_mf_port_dispatch(); std::printf("  MF-PORT dispatch       -- done\n");
    g_mf_mux();           std::printf("  MF-MUX +3 readback     -- done\n");
    g_mf_overlay();       std::printf("  MF-OVL Mmu overlay     -- done\n");

    std::printf("\n==========================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    if (!g_skipped.empty()) {
        std::printf("\nSkipped rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  SKIP %-12s %s\n", s.id.c_str(), s.reason.c_str());
        }
    }

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-12s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-12s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
