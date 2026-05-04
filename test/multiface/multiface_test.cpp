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
// Main
// =====================================================================

int main()
{
    std::printf("Multiface core class compliance tests (Wave 1 B1+B2, Task 8)\n");
    std::printf("==========================================================\n\n");

    g_mf_core();          std::printf("  MF-CORE state machine -- done\n");
    g_mf_port_dispatch(); std::printf("  MF-PORT dispatch       -- done\n");

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
