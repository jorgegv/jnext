// I/O watchpoints (WatchType::IO_READ / IO_WRITE) — GH #222.
//
// THE DEFECT. Both enum values were declared in src/debug/breakpoints.h and
// nothing anywhere read them: no port-side has_watchpoint() call existed, and
// the Breakpoints panel's type combo offered Execute / Read / Write /
// Read-Write only. Meanwhile the man page and the user guide both advertised
// "execution, read, write and I/O watchpoints, in one panel". The owner chose
// to make the promise true rather than delete it.
//
// No VHDL oracle, and there cannot be one: the T80N core contains no debugger.
// The oracle is the DOCUMENTED CONTRACT — a watchpoint on a port stops the
// machine on an access to that port, at the same moment and in the same way a
// memory watchpoint does.
//
// Two halves, deliberately, mirroring test/debug/persistent_bp_test.cpp:
//
//   IOWP-*  THE ADDRESS RULE, as a pure predicate on BreakpointSet. A ZX port
//           is decoded by address-line MASKING, not a 16-bit compare, so
//           "a watchpoint on port 0xFE" has to be given a meaning:
//
//             high byte ZERO      -> partial decode: any port with that LOW
//                                    BYTE. 0xFE catches every ULA access,
//                                    whatever `OUT (254),A` left in the high
//                                    byte.
//             high byte NON-ZERO  -> full 16-bit decode: that port exactly.
//                                    0x243B catches the NextREG select port
//                                    and NOT 0x253B, whose low byte is equal.
//
//           IOWP-05 and IOWP-12 are the rows that pin the decision itself: the
//           first says the exact form does not degrade into a low-byte match,
//           the second says where the threshold between the two forms is. A
//           later change to either rule fails here, loudly, instead of
//           silently changing what a user's watchpoint means.
//
//   IOWW-*  THE WIRING, against a real Emulator running real IN/OUT
//           instructions. The check lives in PortDispatch::read/write — the
//           one choke point BOTH the CPU's in()/out() and the DMA's own port
//           transfers (emulator.cpp's dma_.read_io / dma_.write_io) reach —
//           and raises the SAME data_bp_hit() latch the eight Mmu watchpoint
//           sites raise, so the hot loop stops the machine after the
//           instruction that made the access, exactly as for memory.
//
// The GATE is pinned as hard as the feature (IOWW-09/-10): the check sits
// behind DebugState::armed(), the predicate GH #219 established — breakpoints
// LIVE, not "the debugger window is open" — so it costs a machine nobody is
// debugging nothing at all.
//
// No ROM, no SD image: a 48K machine with a program written straight into RAM
// and PC/SP set by hand (same idiom as test/debug/persistent_bp_test.cpp).
//
// MUTATION-TESTED, one at a time, against the product (reverted from a `cp`
// backup each time). What each mutation kills:
//
//   the match rule forced to EXACT always     IOWP-01/02/07, IOWW-01/02/10/13
//   the match rule forced to LOW-BYTE always  IOWP-05/06/07
//   the check dropped from read()             IOWW-01/03/10/13
//   the check dropped from write()            IOWW-02/04/13
//   the gate changed to active()              IOWW-01/02/03/04/13
//   port_.set_debug_state() left unwired      IOWW-01/02/03/04/10/13
//   read() checking IO_WRITE (direction swap) IOWW-01/02/03/06/10/13
//
// Run: ./build/test/io_watchpoint_test

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

static void check(const char* id, const char* desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

// ── The machine under test ─────────────────────────────────────────────

static constexpr uint16_t PROG    = 0x8000;
static constexpr uint16_t TEST_SP = 0xFF00;
static constexpr uint16_t PARK    = 0x8015;

// A 48K machine running this program from 0x8000, interrupts off. Every port
// it touches is deliberately inert (a keyboard row read, a Kempston read, a
// border write, a Soundrive DAC write) so a row can never pass or fail
// because of what the ACCESS did rather than whether it was noticed.
//
//   8000  01 FE 7F     LD BC,$7FFE
//   8003  ED 78        IN A,(C)      <- IO READ  $7FFE   next PC $8005
//   8005  01 1F 00     LD BC,$001F
//   8008  ED 78        IN A,(C)      <- IO READ  $001F   next PC $800A
//   800A  01 FE 40     LD BC,$40FE
//   800D  ED 79        OUT (C),A     <- IO WRITE $40FE   next PC $800F
//   800F  01 0F 00     LD BC,$000F
//   8012  ED 79        OUT (C),A     <- IO WRITE $000F   next PC $8014
//   8014  00           NOP                                          <- and the
//   8015  18 FE        JR $          <- park                           park is
//                                                                      NOT it
//
// `IN A,(C)` / `OUT (C),A` rather than `IN A,(n)` / `OUT (n),A` on purpose:
// the n-form puts A in the high byte, so the port a row asserts about would
// depend on whatever the previous instruction left in A. Here BC IS the port.
//
// The NOP at $8014 exists so the LAST access's landing PC differs from the
// park: without it "the write watchpoint fired on the final OUT" and "nothing
// fired and the machine ran to the end" would be the same PC.
static void build(Emulator& emu, bool persistent) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.persistent_breakpoints = persistent;
    emu.init(cfg);

    const uint8_t prog[] = {
        0x01, 0xFE, 0x7F,
        0xED, 0x78,
        0x01, 0x1F, 0x00,
        0xED, 0x78,
        0x01, 0xFE, 0x40,
        0xED, 0x79,
        0x01, 0x0F, 0x00,
        0xED, 0x79,
        0x00,
        0x18, 0xFE,
    };
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;              // no frame interrupt in the middle of the run
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// Free-run up to `max_frames` frames, stopping as soon as the debugger pauses.
static void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

static uint16_t pc(const Emulator& emu) {
    return const_cast<Emulator&>(emu).cpu().get_registers().PC;
}

// "Ran to completion without stopping" — the shape every negative row asserts.
static bool ran_to_park(const Emulator& emu) {
    return !emu.debug_state().paused() && pc(emu) == PARK;
}

int main() {
    std::printf("\n======================================================\n");
    std::printf("I/O watchpoints (WatchType::IO_READ / IO_WRITE), GH #222\n");
    std::printf("======================================================\n\n");

    // ────────────── IOWP: the address rule, as a predicate ─────────────

    // IOWP-01 — the partial-decode form, in the case that motivates it: the
    // ULA is reached at 0x7FFE when the keyboard row select is in the high
    // byte, and a user who typed FE means "the ULA".
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x00FE, WatchType::IO_READ);
        check("IOWP-01", "a 00-FF watchpoint matches a port with that low byte",
              bps.has_io_watchpoint(0x7FFE, WatchType::IO_READ));
    }

    // IOWP-02 — the same form at both ends of the high byte: the port whose
    // high byte is zero, and the port whose high byte is 0xFF.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x00FE, WatchType::IO_READ);
        check("IOWP-02", "a 00-FF watchpoint matches regardless of the high byte",
              bps.has_io_watchpoint(0x00FE, WatchType::IO_READ) &&
              bps.has_io_watchpoint(0xFFFE, WatchType::IO_READ));
    }

    // IOWP-03 — and matches nothing else. Without this the rule could be "any
    // port at all".
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x00FE, WatchType::IO_READ);
        check("IOWP-03", "a 00-FF watchpoint does not match a different low byte",
              !bps.has_io_watchpoint(0x7FFD, WatchType::IO_READ) &&
              !bps.has_io_watchpoint(0x00FF, WatchType::IO_READ));
    }

    // IOWP-04 — the full-decode form.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x243B, WatchType::IO_WRITE);
        check("IOWP-04", "a watchpoint above 00FF matches that exact 16-bit port",
              bps.has_io_watchpoint(0x243B, WatchType::IO_WRITE));
    }

    // IOWP-05 — THE DECISION, pinned. 0x243B and 0x253B share the low byte
    // 0x3B (as do 0x123B, 0x303B and a dozen others). An implementation that
    // matched on the low byte unconditionally — the obvious cheaper rule —
    // would stop on the wrong port here and this row is what says so.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x243B, WatchType::IO_WRITE);
        check("IOWP-05", "a full-decode watchpoint does NOT degrade into a "
              "low-byte match (243B is not 253B)",
              !bps.has_io_watchpoint(0x253B, WatchType::IO_WRITE) &&
              !bps.has_io_watchpoint(0x303B, WatchType::IO_WRITE));
    }

    // IOWP-06 — and the converse: the full-decode form does not match the bare
    // low byte either. The two forms are disjoint, not nested.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x243B, WatchType::IO_WRITE);
        check("IOWP-06", "a full-decode watchpoint does not match the bare low byte",
              !bps.has_io_watchpoint(0x003B, WatchType::IO_WRITE));
    }

    // IOWP-12's partner — where the threshold IS. 0x00FF is the last partial
    // form and 0x0100 the first full one; a rule written with the comparison
    // one off in either direction fails here.
    {
        BreakpointSet lo, hi;
        lo.add_watchpoint(0x00FF, WatchType::IO_READ);
        hi.add_watchpoint(0x0100, WatchType::IO_READ);
        check("IOWP-07", "the partial/full threshold is exactly 00FF / 0100",
              lo.has_io_watchpoint(0x12FF, WatchType::IO_READ) &&
              hi.has_io_watchpoint(0x0100, WatchType::IO_READ) &&
              !hi.has_io_watchpoint(0x0200, WatchType::IO_READ) &&
              !hi.has_io_watchpoint(0x0000, WatchType::IO_READ));
    }

    // IOWP-08 — the two directions are separate watchpoints. There is no
    // IO_READ_WRITE value in the enum; a user who wants both adds both.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x00FE, WatchType::IO_READ);
        check("IOWP-08", "an IO_READ watchpoint does not answer IO_WRITE",
              bps.has_io_watchpoint(0x00FE, WatchType::IO_READ) &&
              !bps.has_io_watchpoint(0x00FE, WatchType::IO_WRITE));
    }

    // IOWP-09 — READ_WRITE is a MEMORY convenience and covers neither I/O
    // direction. has_watchpoint() expands it; has_io_watchpoint() must not.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x00FE, WatchType::READ_WRITE);
        check("IOWP-09", "a memory READ_WRITE watchpoint covers no I/O direction",
              !bps.has_io_watchpoint(0x00FE, WatchType::IO_READ) &&
              !bps.has_io_watchpoint(0x00FE, WatchType::IO_WRITE));
    }

    // IOWP-10 — and a plain memory READ/WRITE watchpoint is not an I/O one.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x00FE, WatchType::READ);
        bps.add_watchpoint(0x00FE, WatchType::WRITE);
        check("IOWP-10", "memory READ/WRITE watchpoints are invisible to the "
              "I/O lookup",
              !bps.has_io_watchpoint(0x00FE, WatchType::IO_READ) &&
              !bps.has_io_watchpoint(0x00FE, WatchType::IO_WRITE));
    }

    // IOWP-11 — the leak in the other direction: an I/O watchpoint must not
    // make the MEMORY lookup answer, or every Mmu site would start stopping
    // on it.
    {
        BreakpointSet bps;
        bps.add_watchpoint(0x4000, WatchType::IO_READ);
        bps.add_watchpoint(0x4000, WatchType::IO_WRITE);
        check("IOWP-11", "I/O watchpoints are invisible to the memory lookup",
              !bps.has_watchpoint(0x4000, WatchType::READ) &&
              !bps.has_watchpoint(0x4000, WatchType::WRITE));
    }

    // IOWP-12 — the empty set answers no.
    {
        BreakpointSet bps;
        check("IOWP-12", "an empty set matches no port in either direction",
              !bps.has_io_watchpoint(0x00FE, WatchType::IO_READ) &&
              !bps.has_io_watchpoint(0x243B, WatchType::IO_WRITE) &&
              !bps.has_any_watchpoints());
    }

    // ────────────── IOWW: the wiring, against a real machine ───────────

    // IOWW-01 — THE FEATURE, read side. The watchpoint is on the low byte FE
    // and the program reads 0x7FFE, so this row also carries the partial
    // decode end to end. The machine stops AFTER the IN, exactly as a memory
    // watchpoint stops after the load (persistent_bp_test PBPW-09).
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_READ);
        run_until_paused(emu);
        check("IOWW-01", "an IO_READ watchpoint fires on IN from the watched "
              "port, stopping after the instruction",
              emu.debug_state().paused() && pc(emu) == 0x8005);
    }

    // IOWW-02 — THE FEATURE, write side, on the same low byte: the program
    // writes 0x40FE. Note where it stops — past both INs, which is what says
    // the write watchpoint did not fire on a read.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        run_until_paused(emu);
        check("IOWW-02", "an IO_WRITE watchpoint fires on OUT to the watched "
              "port, stopping after the instruction",
              emu.debug_state().paused() && pc(emu) == 0x800F);
    }

    // IOWW-03 — not merely "the first port access". The watched port is the
    // SECOND one the program reads, and the machine must run past the first.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x001F, WatchType::IO_READ);
        run_until_paused(emu);
        check("IOWW-03", "an IO_READ watchpoint fires on the SECOND watched "
              "read, not the first port access of the run",
              emu.debug_state().paused() && pc(emu) == 0x800A);
    }

    // IOWW-04 — same for the write side, on the last access of the program.
    // The NOP at 0x8014 is what makes this distinguishable from the park.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x000F, WatchType::IO_WRITE);
        run_until_paused(emu);
        check("IOWW-04", "an IO_WRITE watchpoint fires on the last watched "
              "write, at a PC that is not the park",
              emu.debug_state().paused() && pc(emu) == 0x8014);
    }

    // IOWW-05 — a port the program never touches never stops it.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x00E3, WatchType::IO_READ);
        emu.debug_state().breakpoints().add_watchpoint(0x00E3, WatchType::IO_WRITE);
        run_until_paused(emu);
        check("IOWW-05", "watchpoints on an untouched port do not fire",
              ran_to_park(emu));
    }

    // IOWW-06 — DIRECTION, read half: 0x001F is only ever READ by the
    // program, so an IO_WRITE watchpoint on it must not fire.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x001F, WatchType::IO_WRITE);
        run_until_paused(emu);
        check("IOWW-06", "an IO_WRITE watchpoint does not fire on a port that "
              "is only read",
              ran_to_park(emu));
    }

    // IOWW-07 — DIRECTION, write half: 0x000F is only ever WRITTEN.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x000F, WatchType::IO_READ);
        run_until_paused(emu);
        check("IOWW-07", "an IO_READ watchpoint does not fire on a port that "
              "is only written",
              ran_to_park(emu));
    }

    // IOWW-08 — the control. With nothing set the same program runs to the
    // park; every negative row above shares this shape, so it has to be shown
    // to be reachable at all.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        run_until_paused(emu);
        check("IOWW-08", "with no watchpoints the program runs to the park",
              ran_to_park(emu) && !emu.debug_state().breakpoints().has_any_watchpoints());
    }

    // IOWW-09 — THE GATE, default configuration: the debugger window closed
    // and no --persistent-breakpoints means armed() is false, and a machine
    // nobody is debugging pays nothing and stops for nothing. GH #219's
    // predicate, applied to the new site.
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_READ);
        run_until_paused(emu);
        check("IOWW-09", "default (window closed, no flag): an IO_READ "
              "watchpoint does NOT fire",
              !emu.debug_state().armed() && ran_to_park(emu));
    }

    // IOWW-10 — and the ordinary debugger case arms it: window open, no flag.
    // Together with IOWW-09 this says the gate is armed(), not active() alone
    // and not persistent() alone.
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_READ);
        run_until_paused(emu);
        check("IOWW-10", "with the debugger window open, an IO_READ watchpoint "
              "fires without the flag",
              emu.debug_state().paused() && pc(emu) == 0x8005);
    }

    // IOWW-11 — MEMORY watchpoints are not fired by port traffic. 0x7FFE and
    // 0x00FE are never touched as memory by this program (no interrupts, no
    // stack use), but they ARE the port numbers of two of its accesses.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x7FFE, WatchType::READ_WRITE);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::READ_WRITE);
        run_until_paused(emu);
        check("IOWW-11", "an I/O access does not fire a MEMORY watchpoint on "
              "the same number",
              ran_to_park(emu));
    }

    // IOWW-12 — and the converse. 0x8003 is fetched as an opcode byte on every
    // pass through the program, so an I/O watchpoint that had been wired into
    // the Mmu sites as well would stop here immediately.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x8003, WatchType::IO_READ);
        emu.debug_state().breakpoints().add_watchpoint(0x8003, WatchType::IO_WRITE);
        run_until_paused(emu);
        check("IOWW-12", "a MEMORY access does not fire an I/O watchpoint on "
              "the same number",
              ran_to_park(emu));
    }

    // IOWW-13 — THE CHOKE POINT. PortDispatch::read/write, not in()/out(): the
    // DMA's own port transfers are `dma_.read_io = [](p){ return port_.read(p); }`
    // and `dma_.write_io = [](p,v){ port_.write(p,v); }` (emulator.cpp), so a
    // check installed in the CPU's in()/out() would miss every DMA access to a
    // watched port. These two calls are those lambdas' bodies.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_READ);
        emu.port().read(0x7FFE);
        const bool rd = emu.debug_state().data_bp_hit() &&
                        emu.debug_state().data_bp_addr() == 0x7FFE;

        emu.debug_state().set_data_bp_hit(false);
        emu.debug_state().breakpoints().clear_all_watchpoints();
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        emu.port().write(0x40FE, 0x00);
        const bool wr = emu.debug_state().data_bp_hit() &&
                        emu.debug_state().data_bp_addr() == 0x40FE;

        check("IOWW-13", "the check sits on PortDispatch::read/write, so a "
              "non-CPU bus access (the DMA's path) is seen too", rd && wr);
    }

    // ── Summary ────────────────────────────────────────────────────────

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
