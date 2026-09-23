// Breakpoint enable/disable — GH #225, the data-model half.
//
// THE FEATURE. A breakpoint gains an ENABLED flag of its own, and the whole
// set gains a MASTER SWITCH. A disabled breakpoint stays in the list, keeps
// its address and its type, and does not fire. The master switch suspends
// every breakpoint at once without deleting any and without touching any
// per-breakpoint flag, so switching it back restores exactly what was there.
//
// No VHDL oracle, and there cannot be one: the T80N core contains no debugger
// (same position as GH #219's and GH #222's suites). The oracle is the
// contract the issue states, and the way it is asserted below is deliberately
// the machine's BEHAVIOUR — did execution stop, and where — rather than any
// flag the implementation happens to expose. A row that reads back the flag it
// just wrote proves only that a bool stores a bool.
//
// THREE GROUPS.
//
//   BPEN-0x  PER-BREAKPOINT enable, against a real Emulator running a real
//            program: execute breakpoints and memory watchpoints, each
//            asserted by where the machine ended up.
//
//   BPEN-1x  THE MASTER SWITCH, and the COMPOSITION of the two. The rows that
//            matter most are the round trip: a set holding one enabled and one
//            disabled breakpoint must come back out of a master off/on cycle
//            with both states intact — asserted once on the model (BPEN-13)
//            and once on the machine (BPEN-14), because "the flags look right"
//            and "the right one fires" are different claims.
//
//   BPEN-2x  THE HOT PATH's view. has_pc() and has_any_watchpoints() are what
//            the per-instruction and per-memory-access gates read, and they
//            must see a disabled breakpoint as ABSENT — not as present and
//            skipped. That is what makes a disabled breakpoint cost nothing
//            rather than merely little: disable the only watchpoint and
//            has_any_watchpoints() goes false again, so the eight Mmu sites
//            and PortDispatch short-circuit exactly as on a machine that never
//            had one.
//
// No ROM, no SD image: a 48K machine with a program written straight into RAM
// and PC/SP set by hand (same idiom as test/debug/persistent_bp_test.cpp).
//
// Run: ./build/test/bp_enable_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"

#include <cstdint>
#include <cstdio>

// ── Tiny test harness (matches test/debug/persistent_bp_test.cpp style) ─

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const char* detail = nullptr) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (detail) std::printf(" [%s]", detail);
        std::printf("\n");
    }
}

// ── The machine under test ─────────────────────────────────────────────

static constexpr uint16_t PROG    = 0x8000;
static constexpr uint16_t BP_ADDR = 0x8006;   // the LD A,(DATA) below
static constexpr uint16_t BP2     = 0x8009;   // the LD A,0x5A below
static constexpr uint16_t DATA    = 0x9000;
static constexpr uint16_t AFTER_READ  = 0x8009;
static constexpr uint16_t AFTER_WRITE = 0x800E;
static constexpr uint16_t PARK    = 0x8011;
static constexpr uint16_t TEST_SP = 0xFF00;

// A 48K machine running this program from 0x8000, interrupts off:
//
//   8000  00 x6        NOP x6
//   8006  3A 00 90     LD A,(0x9000)   <- BP_ADDR; also the watched READ
//   8009  3E 5A        LD A,0x5A       <- BP2
//   800B  32 00 90     LD (0x9000),A   <- the watched WRITE
//   800E  00 00 00     NOP x3
//   8011  18 FE        JR $            <- PARK
//
// The six leading NOPs put BP_ADDR far from the reset PC, and the three
// trailing ones put PARK clear of AFTER_WRITE, so "stopped on the watchpoint"
// and "ran to the end" can never be the same PC.
static void build(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    // Breakpoints live with no debugger window, which is what lets this suite
    // be a headless backend test (GH #219). It changes nothing about GH #225:
    // armed() is the OUTER gate, the enable model decides what is in the set
    // the gate then consults.
    cfg.persistent_breakpoints = true;
    emu.init(cfg);

    const uint8_t prog[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3A, 0x00, 0x90,
        0x3E, 0x5A,
        0x32, 0x00, 0x90,
        0x00, 0x00, 0x00,
        0x18, 0xFE,
    };
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
    emu.mmu().write(DATA, 0x00);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;              // no frame interrupt in the middle of the run
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

/// Free-run up to `max_frames` frames, stopping as soon as the debugger pauses.
static void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

static uint16_t pc(Emulator& emu) {
    return emu.cpu().get_registers().PC;
}

/// THE assertion this suite is built around: the machine was NOT stopped and
/// reached the park. A row about a breakpoint that must not fire says this,
/// not "a flag reads false".
static bool ran_to_park(Emulator& emu) {
    return !emu.debug_state().paused() && pc(emu) == PARK;
}

/// ... and its opposite: stopped, at exactly this address.
static bool stopped_at(Emulator& emu, uint16_t addr) {
    return emu.debug_state().paused() && pc(emu) == addr;
}

static char g_detail[256];
static const char* detail(Emulator& emu) {
    std::snprintf(g_detail, sizeof(g_detail), "paused=%d PC=$%04X (park=$%04X)",
                  emu.debug_state().paused() ? 1 : 0, pc(emu), PARK);
    return g_detail;
}

int main() {
    std::printf("\n======================================================\n");
    std::printf("Breakpoint enable/disable (master + per-breakpoint), GH #225\n");
    std::printf("======================================================\n\n");

    // ─────────────── BPEN-0x: the per-breakpoint flag ──────────────────

    // BPEN-01 — THE CONTROL, and the DEFAULT the issue asks for: a breakpoint
    // created by add_pc() is enabled, so it fires. Every row below that
    // expects silence is only meaningful next to this one.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        const bool enabled_on_creation = bps.pc_enabled(BP_ADDR);
        run_until_paused(emu);
        check("BPEN-01", "a newly created PC breakpoint is enabled and fires",
              enabled_on_creation && stopped_at(emu, BP_ADDR), detail(emu));
    }

    // BPEN-02 — THE FEATURE. Disabled, the same breakpoint does not stop the
    // machine: it runs to the park. Behaviour, not a label.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.set_pc_enabled(BP_ADDR, false);
        run_until_paused(emu);
        check("BPEN-02", "a DISABLED PC breakpoint does not stop the machine",
              ran_to_park(emu), detail(emu));
    }

    // BPEN-03 — ... and it is still THERE. The issue's words: a disabled
    // breakpoint stays in the list and keeps its address and type. An
    // implementation that "disabled" by deleting passes BPEN-02 and fails
    // here.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.set_pc_enabled(BP_ADDR, false);
        check("BPEN-03", "a disabled PC breakpoint is still in the set, at the "
              "same address",
              bps.pc_exists(BP_ADDR) && !bps.pc_enabled(BP_ADDR) &&
                  bps.pc_breakpoints().size() == 1 &&
                  bps.pc_breakpoints().count(BP_ADDR) == 1 && !bps.empty());
    }

    // BPEN-04 — re-enabling brings it back. The round trip on one breakpoint.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.set_pc_enabled(BP_ADDR, false);
        bps.set_pc_enabled(BP_ADDR, true);
        run_until_paused(emu);
        check("BPEN-04", "re-enabling a disabled PC breakpoint makes it fire "
              "again", stopped_at(emu, BP_ADDR), detail(emu));
    }

    // BPEN-05 — a second add_pc() at the same address must NOT re-arm it. The
    // disassembly gutter's click route reaches add_pc() for an address the
    // user may have disabled from the panel, and silently re-arming it there
    // would make the checkbox lie.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.set_pc_enabled(BP_ADDR, false);
        bps.add_pc(BP_ADDR);              // the second create
        run_until_paused(emu);
        check("BPEN-05", "add_pc() on an already-disabled address leaves it "
              "disabled", !bps.pc_enabled(BP_ADDR) && ran_to_park(emu),
              detail(emu));
    }

    // BPEN-06 — the CONTROL for BPEN-07: an enabled WRITE watchpoint stops the
    // machine on the store, after the instruction that made it.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_watchpoint(DATA, WatchType::WRITE);
        run_until_paused(emu);
        check("BPEN-06", "a newly created WRITE watchpoint is enabled and fires",
              bps.watchpoint_enabled(DATA, WatchType::WRITE) &&
                  stopped_at(emu, AFTER_WRITE), detail(emu));
    }

    // BPEN-07 — the same watchpoint disabled: the store happens and nothing
    // stops. Watchpoints are a separate container from PC breakpoints, so this
    // is a separate claim, not a corollary of BPEN-02.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_watchpoint(DATA, WatchType::WRITE);
        bps.set_watchpoint_enabled(DATA, WatchType::WRITE, false);
        run_until_paused(emu);
        check("BPEN-07", "a DISABLED WRITE watchpoint does not stop the machine",
              ran_to_park(emu) && emu.mmu().read(DATA) == 0x5A, detail(emu));
    }

    // BPEN-08 — and it is still listed, with its address AND its type. The
    // type half is what stops a "disable" that degrades a READ_WRITE into
    // something else on the way back.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_watchpoint(DATA, WatchType::READ_WRITE);
        bps.set_watchpoint_enabled(DATA, WatchType::READ_WRITE, false);
        const bool listed = bps.watchpoints().size() == 1 &&
                            bps.watchpoints()[0].addr == DATA &&
                            bps.watchpoints()[0].type == WatchType::READ_WRITE &&
                            !bps.watchpoints()[0].enabled;
        check("BPEN-08", "a disabled watchpoint keeps its address and its type "
              "in the list", listed && !bps.empty());
    }

    // BPEN-09 — a disabled READ watchpoint, asserted the same way. Two
    // watchpoint types rather than one, because has_watchpoint()'s READ_WRITE
    // matching is the one place a filter could be applied to the wrong list.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_watchpoint(DATA, WatchType::READ_WRITE);
        bps.set_watchpoint_enabled(DATA, WatchType::READ_WRITE, false);
        run_until_paused(emu);
        check("BPEN-09", "a disabled READ_WRITE watchpoint fires on neither the "
              "read nor the write", ran_to_park(emu), detail(emu));
    }

    // BPEN-10 — selectivity. Two breakpoints, one disabled, and the machine
    // stops on the OTHER one — not at the disabled address, not at the park.
    // A "disable" implemented as "suspend everything" passes BPEN-02 and fails
    // here.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.add_pc(BP2);
        bps.set_pc_enabled(BP_ADDR, false);
        run_until_paused(emu);
        check("BPEN-10", "disabling one breakpoint leaves the other firing",
              stopped_at(emu, BP2), detail(emu));
    }

    // ─────────────── BPEN-1x: the master switch, composed ──────────────

    // BPEN-11 — the master switch defaults ON, so a fresh set behaves exactly
    // as it did before this feature existed.
    {
        BreakpointSet bps;
        check("BPEN-11", "a fresh BreakpointSet has the master switch on",
              bps.master_enabled());
    }

    // BPEN-12 — master off suspends an ENABLED breakpoint: nothing stops.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.add_watchpoint(DATA, WatchType::WRITE);
        bps.set_master_enabled(false);
        run_until_paused(emu);
        check("BPEN-12", "master off suspends an enabled breakpoint AND an "
              "enabled watchpoint", ran_to_park(emu), detail(emu));
    }

    // BPEN-13 — THE COMPOSITION, on the model. One enabled breakpoint, one
    // disabled, one enabled watchpoint, one disabled; master off, master on;
    // every address, every type and every per-breakpoint flag identical to
    // what went in. An implementation that suspends by clearing the flags
    // passes BPEN-12 and fails here.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.add_pc(BP2);
        bps.set_pc_enabled(BP_ADDR, false);
        bps.add_watchpoint(DATA, WatchType::READ);
        bps.add_watchpoint(DATA, WatchType::WRITE);
        bps.set_watchpoint_enabled(DATA, WatchType::READ, false);

        bps.set_master_enabled(false);
        // While suspended the per-breakpoint flags must be UNCHANGED — this is
        // the half that makes the restore possible at all.
        const bool flags_held_while_off =
            !bps.pc_enabled(BP_ADDR) && bps.pc_enabled(BP2) &&
            !bps.watchpoint_enabled(DATA, WatchType::READ) &&
            bps.watchpoint_enabled(DATA, WatchType::WRITE);

        bps.set_master_enabled(true);
        const bool restored =
            bps.pc_breakpoints().size() == 2 &&
            bps.pc_exists(BP_ADDR) && bps.pc_exists(BP2) &&
            !bps.pc_enabled(BP_ADDR) && bps.pc_enabled(BP2) &&
            bps.watchpoints().size() == 2 &&
            bps.watchpoint_exists(DATA, WatchType::READ) &&
            bps.watchpoint_exists(DATA, WatchType::WRITE) &&
            !bps.watchpoint_enabled(DATA, WatchType::READ) &&
            bps.watchpoint_enabled(DATA, WatchType::WRITE);

        char d[160];
        std::snprintf(d, sizeof(d), "held_while_off=%d restored=%d",
                      flags_held_while_off ? 1 : 0, restored ? 1 : 0);
        check("BPEN-13", "a master off/on round trip preserves every address, "
              "type and per-breakpoint flag",
              flags_held_while_off && restored, d);
    }

    // BPEN-14 — THE COMPOSITION, on the machine. The same round trip, then
    // run: the enabled breakpoint fires and the disabled one still does not.
    // BPEN-13 says the flags look right; this says the right one stops the
    // CPU, which is the claim a user actually cares about.
    {
        Emulator emu;
        build(emu);
        auto& bps = emu.debug_state().breakpoints();
        bps.add_pc(BP_ADDR);
        bps.add_pc(BP2);
        bps.set_pc_enabled(BP_ADDR, false);
        bps.set_master_enabled(false);
        bps.set_master_enabled(true);
        run_until_paused(emu);
        check("BPEN-14", "after a master round trip the enabled breakpoint "
              "fires and the disabled one does not", stopped_at(emu, BP2),
              detail(emu));
    }

    // BPEN-15 — master off does NOT suspend a one-shot. Step Over, Step Out
    // and Run to Here are how a user navigates a machine whose breakpoints
    // they have deliberately suspended; folding one-shots into the master
    // switch would take stepping away at exactly that moment.
    //
    // BOTH ORDERINGS, and the second is the one that matters: a master flipped
    // BEFORE the one-shot is armed never re-runs the suspension over it, so a
    // suspension that did reach one-shots would still let that case through.
    // The user's own order is the other one — press Step Over, then untick the
    // switch while the machine runs.
    {
        Emulator emu;
        build(emu);
        emu.debug_state().breakpoints().set_master_enabled(false);
        emu.debug_state().run_to(BP2);          // the Run-to-Here route
        run_until_paused(emu);
        const bool armed_after_suspend = stopped_at(emu, BP2);

        Emulator emu2;
        build(emu2);
        emu2.debug_state().run_to(BP2);         // one-shot armed FIRST
        emu2.debug_state().breakpoints().set_master_enabled(false);
        run_until_paused(emu2);
        const bool suspended_after_arm = stopped_at(emu2, BP2);

        char d[160];
        std::snprintf(d, sizeof(d), "armed_after_suspend=%d suspended_after_arm=%d "
                      "(%s)", armed_after_suspend ? 1 : 0,
                      suspended_after_arm ? 1 : 0, detail(emu2));
        check("BPEN-15", "master off leaves one-shots (Step Over / Run to Here) "
              "working, in either order", armed_after_suspend && suspended_after_arm, d);
    }

    // BPEN-16 — the enable state survives a COPY of the set. The header's
    // claim, and it is load-bearing: emulator_cold_boot() saves the set by
    // value, reconstructs the machine and moves it back, so a flag that did
    // not copy would be silently re-armed by a hard reset.
    {
        BreakpointSet src;
        src.add_pc(BP_ADDR);
        src.add_pc(BP2);
        src.set_pc_enabled(BP_ADDR, false);
        src.add_watchpoint(DATA, WatchType::WRITE);
        src.set_watchpoint_enabled(DATA, WatchType::WRITE, false);
        src.set_master_enabled(false);

        BreakpointSet copy = src;                 // copy-construct
        const bool copied = !copy.master_enabled() &&
                            copy.pc_exists(BP_ADDR) && !copy.pc_enabled(BP_ADDR) &&
                            copy.pc_enabled(BP2) &&
                            !copy.watchpoint_enabled(DATA, WatchType::WRITE);

        copy.set_master_enabled(true);            // and the cache follows
        check("BPEN-16", "copying a BreakpointSet carries the master switch and "
              "every per-breakpoint flag",
              copied && copy.has_pc(BP2) && !copy.has_pc(BP_ADDR) &&
                  !copy.has_watchpoint(DATA, WatchType::WRITE));
    }

    // ─────────────── BPEN-2x: what the hot path sees ───────────────────

    // BPEN-20 — the two queries are DISTINCT, and that distinction is the
    // whole design: the hot path asks has_pc() ("will this stop the CPU"), the
    // gutter and the panel ask pc_exists() / pc_enabled() ("is one here, and
    // is it armed"). Collapsing them either makes a disabled breakpoint fire
    // or makes it vanish from the list.
    {
        BreakpointSet bps;
        bps.add_pc(BP_ADDR);
        const bool live_when_enabled = bps.has_pc(BP_ADDR);
        bps.set_pc_enabled(BP_ADDR, false);
        check("BPEN-20", "has_pc() is live while pc_exists() is the model",
              live_when_enabled && !bps.has_pc(BP_ADDR) &&
                  bps.pc_exists(BP_ADDR));
    }

    // BPEN-21 — THE COST CLAIM, as a row. has_any_watchpoints() is the
    // pre-gate every one of the eight Mmu watchpoint sites and PortDispatch
    // reads before scanning; with the only watchpoint disabled it must go
    // FALSE, so those sites short-circuit exactly as on a machine that never
    // had a watchpoint. A disabled watchpoint that merely failed the per-entry
    // compare would leave this true and cost a scan per memory access.
    {
        BreakpointSet bps;
        check("BPEN-21", "has_any_watchpoints() is false on an empty set",
              !bps.has_any_watchpoints());

        bps.add_watchpoint(DATA, WatchType::WRITE);
        const bool on = bps.has_any_watchpoints();
        bps.set_watchpoint_enabled(DATA, WatchType::WRITE, false);
        check("BPEN-22", "disabling the only watchpoint takes the hot path's "
              "pre-gate back to false", on && !bps.has_any_watchpoints());

        bps.set_watchpoint_enabled(DATA, WatchType::WRITE, true);
        bps.set_master_enabled(false);
        check("BPEN-23", "master off takes the hot path's pre-gate to false too",
              !bps.has_any_watchpoints());
    }

    // BPEN-24 — I/O watchpoints ride the same live list. has_io_watchpoint()
    // is a separate scan with its own masking rule (GH #222), so a filter
    // applied to has_watchpoint() alone would leave I/O firing while disabled.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x00FE, WatchType::IO_WRITE);
        const bool on = bps.has_io_watchpoint(0x40FE, WatchType::IO_WRITE);
        bps.set_watchpoint_enabled(0x00FE, WatchType::IO_WRITE, false);
        const bool off = !bps.has_io_watchpoint(0x40FE, WatchType::IO_WRITE);
        bps.set_watchpoint_enabled(0x00FE, WatchType::IO_WRITE, true);
        bps.set_master_enabled(false);
        check("BPEN-24", "a disabled or master-suspended I/O watchpoint stops "
              "matching, low-byte rule and all",
              on && off && !bps.has_io_watchpoint(0x40FE, WatchType::IO_WRITE));
    }

    // BPEN-25 — empty() reads the MODEL. A set holding only disabled
    // breakpoints is not empty: "cleared" and "suspended" must stay
    // distinguishable to every caller, or a UI that asks "is there anything
    // here" gets told no while the user is looking at a list of breakpoints.
    {
        BreakpointSet bps;
        bps.add_pc(BP_ADDR);
        bps.add_watchpoint(DATA, WatchType::WRITE);
        bps.set_pc_enabled(BP_ADDR, false);
        bps.set_watchpoint_enabled(DATA, WatchType::WRITE, false);
        const bool not_empty_disabled = !bps.empty();
        bps.set_master_enabled(false);
        const bool not_empty_suspended = !bps.empty();
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        check("BPEN-25", "a set of disabled or suspended breakpoints is not "
              "empty; a cleared one is",
              not_empty_disabled && not_empty_suspended && bps.empty());
    }

    // BPEN-26 — the OBSERVER fires for both toggles, on the half each one
    // concerns. GH #220 made the set notify so no call site has to remember;
    // an enable toggle that notified nobody would leave both panels stale, and
    // the master switch has to reach the gutter (PcBreakpoints) as well as the
    // list (Watchpoints).
    //
    // It also pins the NO-OP guard on every mutator that carries one — see
    // the `quiet` block below for why all three are re-called and not two.
    {
        BreakpointSet bps;
        bps.add_pc(BP_ADDR);
        bps.add_watchpoint(DATA, WatchType::WRITE);

        int pc_notes = 0, wp_notes = 0;
        const auto id = bps.add_observer([&](BreakpointChange what) {
            if (what == BreakpointChange::PcBreakpoints) ++pc_notes;
            else ++wp_notes;
        });

        bps.set_pc_enabled(BP_ADDR, false);
        const bool pc_only = (pc_notes == 1 && wp_notes == 0);

        bps.set_watchpoint_enabled(DATA, WatchType::WRITE, false);
        const bool wp_only = (pc_notes == 1 && wp_notes == 1);

        bps.set_master_enabled(false);
        const bool master_both = (pc_notes == 2 && wp_notes == 2);

        // A no-op write must notify nobody, or the panel that echoes the model
        // back re-enters refresh() on every repaint.
        //
        // ALL THREE mutators carry that guard, so all three are re-called
        // here. An earlier cut of this row re-called two of them and left
        // set_watchpoint_enabled()'s guard to inference — review found that
        // dropping it kept every suite green. A row that names an invariant
        // has to test it everywhere the invariant is implemented.
        bps.set_pc_enabled(BP_ADDR, false);
        bps.set_watchpoint_enabled(DATA, WatchType::WRITE, false);
        bps.set_master_enabled(false);
        const bool quiet = (pc_notes == 2 && wp_notes == 2);

        bps.remove_observer(id);
        char d[160];
        std::snprintf(d, sizeof(d), "pc_only=%d wp_only=%d both=%d quiet=%d "
                      "(pc=%d wp=%d)", pc_only ? 1 : 0, wp_only ? 1 : 0,
                      master_both ? 1 : 0, quiet ? 1 : 0, pc_notes, wp_notes);
        check("BPEN-26", "enable toggles notify the half they change; the "
              "master switch notifies both; a no-op notifies nobody",
              pc_only && wp_only && master_both && quiet, d);
    }

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
