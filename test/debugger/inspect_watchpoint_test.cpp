// Debugger inspection reads must not fire the user's watchpoints.
//
// THE DEFECT. Mmu::read() raises DebugState::data_bp_hit on a READ watchpoint,
// and the Watches, Memory, Stack and Disassembly panels all read guest memory
// through that same Mmu::read(). So a READ (or READ/WRITE) watchpoint on any
// address one of those panels happened to display was latched by the PANEL'S
// OWN REFRESH, not by the guest. The latch survives run_to_cycle(), step_into()
// and every other resume except DebugState::resume(), so the next Run or Step
// stopped one instruction later at an address the watchpoint had nothing to do
// with — and DebuggerManager::refresh_panels() runs at ~4 Hz while the machine
// is running, so it re-armed continuously. User-visible symptom: set a read
// watchpoint on an address you are also watching in a panel, press F5, and the
// machine stops immediately, repeatedly, for no reason.
//
// The same shape existed twice more INSIDE the emulator's own loop, and for
// exactly the user who had the debugger open, since the debugger is what
// switches both on: the trace log reads FOUR opcode bytes at PC and the
// call-stack tracker reads THREE, whatever the instruction's real length, so
// both read data bytes past a short instruction that the CPU never fetches.
//
// THE FIX. Watchpoints are live only while the EMULATED MACHINE is executing:
// DebugState::watchpoints_live() is armed() AND inside a
// DebugState::GuestExecutionScope, which only Emulator::run_frame(),
// step_frame_slot() and execute_single_instruction() take. A panel, a tool or a
// snapshot saver reading guest memory from outside the emulator's own execution
// therefore cannot fire a watchpoint, whatever entry point it calls — there is
// no call site for a future panel's author to remember.
//
// WHAT THESE ROWS ASSERT. Where execution ENDED UP, not the state of a flag: a
// row that checks `!data_bp_hit()` would pass against a fix that merely cleared
// the latch somewhere, which is the stopgap this replaces. Every inspection row
// asserts the machine is NOT paused and reached its park. The CONTROL rows
// (INSPW-10..14, INSPW-17) assert that a genuine guest access to the same kind
// of address still stops the machine, at the right PC — without them, "nothing
// ever stops" would pass every other row in this file.
//
// EACH ROW IS ATTRIBUTABLE TO ONE READER. The fixture's watched addresses are
// chosen so that exactly one inspection path touches each (see the address
// table below), which is what makes a failing row name the reader that broke.
//
// It drives the REAL DebuggerManager / DebuggerWindow and the real panels
// against a real 48K Emulator, so nothing here is a stub. Qt is required (the
// panels are QWidgets), a display is not: main() forces the offscreen QPA
// platform, like the other debugger suites.
//
// Run: ./build/test/debugger_inspect_watchpoint_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/sna_saver.h"
#include "debug/debug_state.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/callstack_panel.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/watch_panel.h"

#include <QApplication>
#include <QMainWindow>
#include <QPixmap>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_total = 0;
int g_pass  = 0;
int g_fail  = 0;

void check(const char* id, const char* desc, bool cond, const char* detail = "") {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc,
                    detail[0] ? "  |  " : "", detail);
    }
}

// ── The fixture program ────────────────────────────────────────────────
//
//   8000  00 00 00 00 00 00   NOP x6
//   8006  3A 00 90            LD A,(DATA)   <- the GUEST's read of DATA
//   8009  3E 5A               LD A,$5A      (so the IN below has a fixed A)
//   800B  32 00 90            LD (DATA),A   <- the GUEST's write of DATA
//   800E  DB FE               IN A,($FE)    <- the GUEST's port read, $5AFE
//   8010  00                  NOP           (so the IN's landing PC != the park)
//   8011  18 FE               JR $          <- the park
//   8013  76                  HALT          <- NEVER executed
//
// ── The address table: one reader per watched address ──────────────────
//
//   $9100  SHOWN      the Watches panel, because the user put it there.
//                     The program never touches it.
//   $FF00  = SP       the Stack panel, which reads SP..SP+$2F (24 rows of
//                     two bytes). Nothing pushes (IFF=0, no CALL), so the
//                     guest never touches it.
//   $7FFF  DISASM     the Disassembly panel. activate_follow_pc() centres on
//                     PC=$8000 and starts decoding `half*3` bytes BEFORE it,
//                     so it always decodes forward across $7FFF. The CPU never
//                     goes below $8000.
//   $0010  MEMPANEL   the Memory panel's paintEvent, which draws from $0000.
//                     48K ROM the program never executes. The one address
//                     whose reader depends on widget geometry, so INSPW-06
//                     proves the read happened instead of assuming it.
//   $E000  SAVERD     SnaSaver::save(), which reads every RAM page through the
//   $FEFE  SAVERW     slot-7 window $E000-$FFFF and pushes PC at SP-2.
//   $8013  CALLSTK    the call-stack tracker's 3-byte peek, from the park's
//                     PC=$8011. The CPU never executes it.
//   $8014  TRACE      the trace log's 4-byte capture, from the same PC. One
//                     byte further than the call stack reaches, so the two
//                     rows cannot pass for each other.
//   $9000  DATA       the GUEST. The control rows watch this one.
constexpr uint16_t PROG        = 0x8000;
constexpr uint16_t DATA        = 0x9000;
constexpr uint16_t SHOWN       = 0x9100;
constexpr uint16_t DISASM_ONLY = 0x7FFF;
constexpr uint16_t MEM_ONLY    = 0x0010;
constexpr uint16_t SAVER_READ  = 0xE000;
constexpr uint16_t CALLSTK_ONLY = 0x8013;
constexpr uint16_t TRACE_ONLY  = 0x8014;
constexpr uint16_t AFTER_READ  = 0x8009;
constexpr uint16_t AFTER_WRITE = 0x800E;
constexpr uint16_t AFTER_IN    = 0x8010;
constexpr uint16_t PARK        = 0x8011;
constexpr uint16_t TEST_SP     = 0xFF00;
constexpr uint16_t SAVER_WRITE = TEST_SP - 2;

void build(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    emu.init(cfg);

    const uint8_t prog[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3A, 0x00, 0x90,          // 8006  LD A,(DATA)
        0x3E, 0x5A,                // 8009  LD A,$5A
        0x32, 0x00, 0x90,          // 800B  LD (DATA),A
        0xDB, 0xFE,                // 800E  IN A,($FE)
        0x00,                      // 8010  NOP
        0x18, 0xFE,                // 8011  JR $      (park)
        0x76,                      // 8013  HALT      (never executed)
    };
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;                    // no frame interrupt mid-run
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

uint16_t pc(Emulator& emu) { return emu.cpu().get_registers().PC; }

void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

// "Ran to completion without stopping" — the shape every inspection row
// asserts. Not `!data_bp_hit()`: that is the mechanism, this is the symptom.
bool ran_to_park(Emulator& emu) {
    return !emu.debug_state().paused() && pc(emu) == PARK;
}

const char* ran_detail(Emulator& emu) {
    static char buf[96];
    std::snprintf(buf, sizeof(buf), "paused=%d PC=$%04X (want paused=0 PC=$%04X)",
                  emu.debug_state().paused() ? 1 : 0, pc(emu), PARK);
    return buf;
}

// "Run to EOF actually ran" — the shape the two paused-refresh rows assert.
// The machine IS paused at the end (that is what Run to EOF does); what the
// row discriminates on is WHERE. Pre-fix the stale latch stopped it one
// instruction into the run, at $8001; correctly it reaches the park and stops
// there when the target cycle arrives.
bool eof_reached_park(Emulator& emu) {
    return emu.debug_state().paused() && pc(emu) == PARK;
}

const char* eof_detail(Emulator& emu) {
    static char buf[112];
    std::snprintf(buf, sizeof(buf),
                  "paused=%d PC=$%04X (want paused=1 PC=$%04X; pre-fix stops at $8001)",
                  emu.debug_state().paused() ? 1 : 0, pc(emu), PARK);
    return buf;
}

const char* stop_detail(Emulator& emu, uint16_t want) {
    static char buf[96];
    std::snprintf(buf, sizeof(buf), "paused=%d PC=$%04X (want paused=1 PC=$%04X)",
                  emu.debug_state().paused() ? 1 : 0, pc(emu), want);
    return buf;
}

bool stopped_at(Emulator& emu, uint16_t want) {
    return emu.debug_state().paused() && pc(emu) == want;
}

// A 48K machine with the REAL debugger window open on it — the state the bug
// needs, since the panels only read when they exist. set_enabled(true) also
// turns the call-stack tracker on, exactly as it does for a user, which is why
// no inspection row watches an address in $8000-$8013.
struct Fixture {
    Emulator         emu;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;

    Fixture() {
        build(emu);
        mgr = new DebuggerManager(&win, &emu, &win);   // parented -> auto-freed
        mgr->set_enabled(true);                        // creates + shows window
    }

    DebuggerWindow* dbg() const { return mgr->debugger_window_ptr(); }

    // Put `addr` in the Watches panel — the panel a user puts an address in
    // precisely BECAUSE they are also watchpointing it.
    void watch_in_panel(uint16_t addr) {
        dbg()->watch_panel()->add_watch(addr, "probe", 0 /* BYTE */);
    }

    // The real panel refresh: DebuggerWindow::refresh_panels(), which is what
    // DebuggerManager::refresh_panels() calls on every tick (unconditionally
    // while paused, at ~4 Hz while running).
    void refresh() { dbg()->refresh_panels(); }

    // Tell the paused-gated panels (Disassembly, Stack, CPU, Call stack) that
    // the machine is stopped, exactly as DebuggerManager does on a pause.
    void mark_paused(bool p) {
        dbg()->disasm_panel()->set_paused(p);
        dbg()->stack_panel()->set_paused(p);
        dbg()->cpu_panel()->set_paused(p);
        dbg()->callstack_panel()->set_paused(p);
    }

    // Pause, let every panel read, then Run to End of Frame — the exact
    // sequence a user performs: break, look at the address, press the button.
    //
    // "Run to EOF" (DebuggerManager::on_run_to_eof -> DebugState::
    // run_to_cycle) and NOT plain resume(), deliberately. resume() is the ONE
    // transition out of paused that clears data_bp_hit_, so a panel latch
    // raised while paused is swallowed by F5 and a row built on it would pass
    // pre-fix for a reason that has nothing to do with the fix. Every other
    // resume — Run to EOF, Run to EOSL, Step Into/Over/Out, Run to Here, Step
    // Back — leaves the latch standing, which is what makes the paused refresh
    // damaging in the first place.
    void look_then_run_to_eof() {
        emu.debug_state().pause();
        mark_paused(true);
        dbg()->activate_follow_pc();
        refresh();                       // every panel reads
        mgr->on_run_to_eof();            // the real button
        mark_paused(false);
        run_until_paused(emu);
    }
};

} // namespace

int main(int argc, char** argv) {
    // The panels are QWidgets, so a QApplication is required — but not a
    // display: force the offscreen QPA platform.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("\n======================================================\n");
    std::printf("Debugger inspection reads vs watchpoints\n");
    std::printf("======================================================\n\n");

    // ───────── INSPW-01..09: the panels are observers ─────────

    // INSPW-01 — THE REPORTED SYMPTOM. A READ watchpoint on an address the
    // Watches panel displays, refreshed while the machine RUNS. Pre-fix the
    // refresh latched data_bp_hit and run_frame() stopped one instruction into
    // the next call, at a PC the watchpoint has nothing to do with.
    {
        Fixture f;
        f.watch_in_panel(SHOWN);
        f.emu.debug_state().breakpoints().add_watchpoint(SHOWN, WatchType::READ);
        f.refresh();
        run_until_paused(f.emu);
        check("INSPW-01", "a READ watchpoint on an address the Watches panel "
              "shows is not fired by the panel's own refresh",
              ran_to_park(f.emu), ran_detail(f.emu));
    }

    // INSPW-02 — the same for READ_WRITE, which is a separate arm of
    // BreakpointSet::has_watchpoint()'s matching and so cannot be assumed from
    // the row above.
    {
        Fixture f;
        f.watch_in_panel(SHOWN);
        f.emu.debug_state().breakpoints().add_watchpoint(SHOWN, WatchType::READ_WRITE);
        f.refresh();
        run_until_paused(f.emu);
        check("INSPW-02", "the same for a READ_WRITE watchpoint",
              ran_to_park(f.emu), ran_detail(f.emu));
    }

    // INSPW-03 — THE RE-ARMING CASE, through the real throttle.
    // DebuggerManager::refresh_panels() refreshes every REFRESH_INTERVAL ticks
    // while running (~4 Hz), so the latch was not raised once but
    // continuously: clearing it by hand would have been overwritten by the
    // next refresh. 60 manager ticks interleaved with frames is several
    // refresh periods.
    {
        Fixture f;
        f.watch_in_panel(SHOWN);
        f.emu.debug_state().breakpoints().add_watchpoint(SHOWN, WatchType::READ);
        for (int i = 0; i < 60 && !f.emu.debug_state().paused(); ++i) {
            f.mgr->refresh_panels();          // the real 4 Hz throttle
            if ((i % 8) == 0) f.emu.run_frame();
        }
        check("INSPW-03", "repeated refreshes while running never stop the "
              "machine (the ~4 Hz re-arm)",
              ran_to_park(f.emu), ran_detail(f.emu));
    }

    // INSPW-04 — THE STACK PANEL, named. It reads SP..SP+$2F through
    // Mmu::read() on every PAUSED refresh, so a READ watchpoint anywhere in
    // the displayed stack window was fired by looking at the stack. This is
    // also the break-look-run sequence in full.
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(TEST_SP, WatchType::READ);
        f.look_then_run_to_eof();
        check("INSPW-04", "the Stack panel reading SP does not fire a READ "
              "watchpoint on the stack",
              eof_reached_park(f.emu), eof_detail(f.emu));
    }

    // INSPW-05 — THE DISASSEMBLY PANEL, named. It decodes forward from a view
    // address `half*3` bytes BEFORE PC, so it reads bytes below the program
    // that the CPU never fetches.
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(DISASM_ONLY, WatchType::READ);
        f.look_then_run_to_eof();
        check("INSPW-05", "the Disassembly panel decoding below PC does not "
              "fire a READ watchpoint on a byte the CPU never fetches",
              eof_reached_park(f.emu), eof_detail(f.emu));
    }

    // INSPW-06 — THE MEMORY PANEL, named, and through a real paintEvent: it is
    // the one panel that reads while PAINTING (its refresh() only calls
    // update()), so a fix that wrapped the refresh CALL would have missed it
    // entirely. That is the concrete reason the gate is on "is the machine
    // executing" rather than on a scope around panel refresh. render() runs
    // the paint synchronously.
    {
        Fixture f;
        auto* mem = f.dbg()->findChild<MemoryPanel*>();
        f.emu.debug_state().breakpoints().add_watchpoint(MEM_ONLY, WatchType::READ);
        QPixmap px(mem ? mem->size() : QSize(1, 1));

        // NON-VACUITY, proven on the real stimulus. This row is the only one
        // whose stimulus depends on WIDGET GEOMETRY: `MemoryPanel::
        // visible_rows()` has no floor (`if (avail <= 0) return 0;`) and
        // `paintEvent()` returns early on `vis <= 0`, unlike
        // `DisasmPanel::visible_lines()`, which is `std::max(4, ...)` and
        // therefore always reaches $7FFF for INSPW-05. Today the panel gets
        // Qt's implicit 640x480 and draws 27 rows, but an explicit small
        // resize — or a Qt with a different default — would make the paint a
        // no-op and this row would pass having tested nothing.
        //
        // So it is PROVEN rather than assumed, and proven on the address the
        // row is about rather than on a row count (which would not catch a
        // changed scroll offset, BYTES_PER_ROW, or page-selector mode): the
        // panel is rendered ONCE inside a GuestExecutionScope, where a read
        // DOES latch, and the latch must fire at exactly MEM_ONLY. That is
        // the panel demonstrating it reads the watched byte.
        bool panel_really_reads = false;
        if (mem) {
            DebugState::GuestExecutionScope probe(f.emu.debug_state());
            mem->render(&px);                 // forces paintEvent -> read_byte()
            panel_really_reads = f.emu.debug_state().data_bp_hit() &&
                                 f.emu.debug_state().data_bp_addr() == MEM_ONLY;
        }
        // Clear the probe's OWN latch — this row raised it deliberately, and
        // it is the only place in this file that touches the flag by hand.
        f.emu.debug_state().set_data_bp_hit(false);

        // THE ROW ITSELF: the same paint, now outside execution as a real
        // panel refresh is, must leave the machine alone.
        if (mem) mem->render(&px);
        run_until_paused(f.emu);
        char d[160];
        std::snprintf(d, sizeof(d), "panel_really_reads=%d %s",
                      panel_really_reads ? 1 : 0, ran_detail(f.emu));
        check("INSPW-06", "the Memory panel's paintEvent does not fire a READ "
              "watchpoint on a displayed address",
              mem != nullptr && panel_really_reads && ran_to_park(f.emu), d);
    }

    // INSPW-07 — a host-side read AND write through the Mmu from OUTSIDE the
    // emulator's execution — what every panel, tool and saver ultimately does
    // — raise no latch at all. This is the mechanism the rows above assert the
    // consequences of, and the one a future panel inherits without having to
    // know it exists.
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(SHOWN, WatchType::READ);
        f.emu.debug_state().breakpoints().add_watchpoint(SHOWN, WatchType::WRITE);
        (void)f.emu.mmu().read(SHOWN);
        const bool no_read_latch = !f.emu.debug_state().data_bp_hit();
        f.emu.mmu().write(SHOWN, 0x11);
        const bool no_write_latch = !f.emu.debug_state().data_bp_hit();
        run_until_paused(f.emu);
        char d[160];
        std::snprintf(d, sizeof(d), "read_latch=%d write_latch=%d %s",
                      no_read_latch ? 0 : 1, no_write_latch ? 0 : 1,
                      ran_detail(f.emu));
        check("INSPW-07", "a host-side Mmu read AND write outside execution "
              "raise no latch, and the machine still runs to the park",
              no_read_latch && no_write_latch && ran_to_park(f.emu), d);
    }

    // INSPW-08 — a host-side PORT access, the same thing on the other gate:
    // PortDispatch::read/write also raised the latch, so a tool or panel that
    // probed a port stopped the machine too.
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_READ);
        f.emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        (void)f.emu.port().read(0x7FFE);
        const bool no_read_latch = !f.emu.debug_state().data_bp_hit();
        f.emu.port().write(0x40FE, 0x00);
        const bool no_write_latch = !f.emu.debug_state().data_bp_hit();
        check("INSPW-08", "a host-side PortDispatch read AND write outside "
              "execution raise no latch either",
              no_read_latch && no_write_latch);
    }

    // INSPW-09 — SAVING A SNAPSHOT. SnaSaver::save() reads every RAM page
    // through the slot-7 window $E000-$FFFF and WRITES two bytes at SP-2 (the
    // 48K SNA format pushes PC), so File > Save Snapshot with a watchpoint
    // anywhere in that window stopped the machine on the next Run. It runs
    // from the GUI, outside run_frame().
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(SAVER_READ, WatchType::READ);
        f.emu.debug_state().breakpoints().add_watchpoint(SAVER_WRITE, WatchType::WRITE);
        std::vector<uint8_t> snap = SnaSaver::save(f.emu);
        const bool saved = !snap.empty();
        run_until_paused(f.emu);
        check("INSPW-09", "saving a snapshot does not fire the watchpoints its "
              "own reads and its stack push pass over",
              saved && ran_to_park(f.emu), ran_detail(f.emu));
    }

    // ───────── INSPW-10..14: THE CONTROLS ─────────
    //
    // Everything above would also pass if the fix had simply disarmed
    // watchpoints. These are what say it did not.

    // INSPW-10 — a GENUINE guest READ still stops the machine, on the
    // instruction after the load, with the panel showing the same address and
    // refreshing.
    {
        Fixture f;
        f.watch_in_panel(DATA);
        f.emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::READ);
        f.refresh();
        run_until_paused(f.emu);
        check("INSPW-10", "CONTROL: the guest's own read of a watched address "
              "still stops, after the loading instruction",
              stopped_at(f.emu, AFTER_READ), stop_detail(f.emu, AFTER_READ));
    }

    // INSPW-11 — and a genuine guest WRITE. Separate MMU site, separate row.
    {
        Fixture f;
        f.watch_in_panel(DATA);
        f.emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::WRITE);
        f.refresh();
        run_until_paused(f.emu);
        check("INSPW-11", "CONTROL: the guest's own write to a watched address "
              "still stops, after the storing instruction",
              stopped_at(f.emu, AFTER_WRITE), stop_detail(f.emu, AFTER_WRITE));
    }

    // INSPW-12 — the PORT side of the same gate: PortDispatch took armed() too
    // and now takes watchpoints_live(), so an I/O watchpoint has to be proved
    // still live on a real IN. `IN A,($FE)` puts A in the high byte and the
    // preceding LD A,$5A fixes it, so the port is $5AFE — matched by the
    // partial-decode form $00FE (GH #222).
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_READ);
        f.refresh();
        run_until_paused(f.emu);
        check("INSPW-12", "CONTROL: an I/O watchpoint still fires on the "
              "guest's own IN",
              stopped_at(f.emu, AFTER_IN), stop_detail(f.emu, AFTER_IN));
    }

    // INSPW-13 — the debugger's STEP still sees a watchpoint. Stepping runs
    // through step_frame_slot() rather than run_frame(), i.e. a DIFFERENT one
    // of the three execution scopes, so a scope missed on that path would
    // leave watchpoints silently dead while single-stepping.
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::READ);
        for (int i = 0; i < 12 && !f.emu.debug_state().paused(); ++i)
            f.emu.debugger_step();
        check("INSPW-13", "CONTROL: a watchpoint still fires while SINGLE-"
              "STEPPING (the step_frame_slot scope)",
              stopped_at(f.emu, AFTER_READ), stop_detail(f.emu, AFTER_READ));
    }

    // INSPW-14 — ...and through the raw one-slot primitive,
    // execute_single_instruction(), the third scope. It does not consume the
    // latch itself (debugger_step() does), so the latch and its address are
    // this path's only observable. Six NOPs, then the load.
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::READ);
        for (int i = 0; i < 7; ++i) f.emu.execute_single_instruction();
        char d[96];
        std::snprintf(d, sizeof(d), "hit=%d addr=$%04X (want hit=1 addr=$%04X)",
                      f.emu.debug_state().data_bp_hit() ? 1 : 0,
                      f.emu.debug_state().data_bp_addr(), DATA);
        check("INSPW-14", "CONTROL: a watchpoint still fires through "
              "execute_single_instruction() (the third scope)",
              f.emu.debug_state().data_bp_hit() &&
              f.emu.debug_state().data_bp_addr() == DATA, d);
    }

    // ───────── INSPW-15..17: the two in-loop debugger readers ─────────

    // INSPW-15 — THE TRACE LOG. It captures FOUR opcode bytes at PC whatever
    // the instruction's length, so at the park's `JR $` ($8011, two bytes) it
    // reads $8011-$8014. $8014 is one byte further than the call-stack peek
    // reaches, so this row can only be about the trace. The debugger enables
    // the trace, and so does --rewind.
    {
        Fixture f;
        f.emu.trace_log().set_enabled(true);
        f.emu.debug_state().breakpoints().add_watchpoint(TRACE_ONLY, WatchType::READ);
        run_until_paused(f.emu);
        const bool traced = f.emu.trace_log().size() > 0;
        char d[128];
        std::snprintf(d, sizeof(d), "%s traced=%zu", ran_detail(f.emu),
                      f.emu.trace_log().size());
        check("INSPW-15", "the trace log's 4-byte opcode capture does not fire "
              "a READ watchpoint on a byte the CPU never fetches",
              traced && ran_to_park(f.emu), d);
    }

    // INSPW-16 — THE CALL-STACK TRACKER, the same shape with THREE bytes, so
    // at the park it reads $8011-$8013 and $8013 is its own. It is switched on
    // by DebuggerManager::set_enabled(), independently of the trace, which is
    // left off here.
    {
        Fixture f;
        f.emu.debug_state().breakpoints().add_watchpoint(CALLSTK_ONLY, WatchType::READ);
        run_until_paused(f.emu);
        char d[128];
        std::snprintf(d, sizeof(d), "%s callstack_enabled=%d", ran_detail(f.emu),
                      f.emu.call_stack().enabled() ? 1 : 0);
        check("INSPW-16", "the call-stack tracker's 3-byte opcode peek does not "
              "fire a READ watchpoint on a byte the CPU never fetches",
              f.emu.call_stack().enabled() && ran_to_park(f.emu), d);
    }

    // INSPW-17 — CONTROL for both: with the trace AND the call stack on, the
    // guest's own read of a watched address still stops the machine. Without
    // this, INSPW-15/16 would pass against "watchpoints are dead whenever the
    // trace or the call stack is on".
    {
        Fixture f;
        f.emu.trace_log().set_enabled(true);
        f.emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::READ);
        run_until_paused(f.emu);
        check("INSPW-17", "CONTROL: with trace and call stack on, the guest's "
              "own read still stops at the right instruction",
              stopped_at(f.emu, AFTER_READ), stop_detail(f.emu, AFTER_READ));
    }

    // INSPW-18 — the gate as a predicate, and its COMPOSITION with GH #219's
    // armed(). Both halves are required: outside execution it is false even
    // with the debugger open, and inside execution it is false when nothing
    // armed it. A one-sided gate would satisfy every other row in this file.
    {
        Fixture f;                                   // debugger window OPEN
        const bool open_outside = !f.emu.debug_state().watchpoints_live();
        bool open_inside = false;
        {
            DebugState::GuestExecutionScope g(f.emu.debug_state());
            open_inside = f.emu.debug_state().watchpoints_live();
        }
        const bool restored = !f.emu.debug_state().watchpoints_live();

        Emulator bare;                               // no debugger, no flag
        build(bare);
        bool closed_inside = true;
        bool closed_scope_entered = false;
        {
            DebugState::GuestExecutionScope g(bare.debug_state());
            closed_inside = bare.debug_state().watchpoints_live();
            // guest_access(), not watchpoints_live(): it separates "the scope
            // did nothing" from "the scope worked and armed() is the half that
            // is false". Without it, a GuestExecutionScope that had quietly
            // become a no-op would satisfy this half of the row.
            closed_scope_entered = bare.debug_state().guest_access();
        }
        char d[176];
        std::snprintf(d, sizeof(d),
                      "open_outside=%d open_inside=%d restored=%d "
                      "closed_inside=%d closed_scope_entered=%d",
                      open_outside ? 1 : 0, open_inside ? 1 : 0,
                      restored ? 1 : 0, closed_inside ? 1 : 0,
                      closed_scope_entered ? 1 : 0);
        check("INSPW-18", "watchpoints_live() is armed() AND inside execution, "
              "and the scope restores what it found",
              open_outside && open_inside && restored && !closed_inside &&
              closed_scope_entered, d);
    }

    // ── Summary ────────────────────────────────────────────────────────

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
