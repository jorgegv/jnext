// Step Out (F8) unit tests — GH #203.
//
// Two halves, deliberately:
//
//   STPOUT-P*  the DebugState::check_step_out() PREDICATE, in isolation.
//   STPOUT-W*  the WIRING — a real Emulator, a real Z80 program, F8 armed
//              through the same DebugState the debugger uses, and an
//              assertion that the machine actually stops.
//
// The second half is the point of this suite. Before GH #203 the predicate
// was already present and (nearly) right, but `check_step_out` had NO CALLER
// anywhere in the emulator: StepMode::OUT was written and never read, so F8
// resumed and ran until a breakpoint or F9. A predicate-only suite would have
// been green throughout. Every STPOUT-W row below fails on the pre-fix tree.
//
// No ROM, no SD image: a 48K machine with a program written straight into RAM
// and PC/SP set by hand (same idiom as test/rewind/rewind_test.cpp).
//
// Run: ./build/test/step_out_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// ── Tiny test harness (matches test/debug/resume_guard_test.cpp style) ──

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

// ── Predicate helpers ──────────────────────────────────────────────────

// A DebugState with Step Out armed at `sp`.
static DebugState armed_at(uint16_t sp) {
    DebugState ds;
    ds.set_active(true);
    ds.pause();
    ds.step_out(sp);
    return ds;
}

// "The instruction at the armed level returned": popped one return address
// from `sp_before`, leaving SP at sp_before + 2.
static bool ret_from(const DebugState& ds, uint16_t sp_before,
                     uint8_t op, uint8_t op2 = 0x00) {
    return ds.check_step_out(sp_before, static_cast<uint16_t>(sp_before + 2), op, op2);
}

// ── Emulator helpers ───────────────────────────────────────────────────

static constexpr uint16_t PROG = 0x8000;   // caller
static constexpr uint16_t SUB  = 0x9000;   // subroutine under test
static constexpr uint16_t SUB2 = 0x9100;   // nested subroutine
static constexpr uint16_t TEST_SP = 0xFF00;

// Bring up a 48K machine with `main` at 0x8000 and no interrupts, then write
// the two byte blobs at SUB / SUB2. `main` is always:
//     8000  CD 00 90    CALL SUB
//     8003  18 FE       JR $        <- the "we came back" landing marker
// so a Step Out armed inside SUB must stop with PC == 0x8003.
static void build(Emulator& emu,
                  const std::vector<uint8_t>& sub,
                  const std::vector<uint8_t>& sub2 = {})
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    emu.init(cfg);

    const uint8_t main_prog[] = { 0xCD, 0x00, 0x90, 0x18, 0xFE };
    for (size_t i = 0; i < sizeof(main_prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), main_prog[i]);
    for (size_t i = 0; i < sub.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(SUB + i), sub[i]);
    for (size_t i = 0; i < sub2.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(SUB2 + i), sub2[i]);

    Z80Registers r = emu.cpu().get_registers();
    r.PC = PROG;
    r.SP = TEST_SP;
    r.IFF1 = 0;              // no frame interrupt while stepping out
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// Execute the CALL at 0x8000, then arm Step Out at the subroutine's entry SP —
// exactly what DebuggerManager::on_step_out() does (pause, then step_out(SP)).
static void enter_sub_and_arm(Emulator& emu) {
    emu.execute_single_instruction();          // CALL SUB
    emu.debug_state().set_active(true);
    emu.debug_state().pause();
    emu.debug_state().step_out(emu.cpu().get_registers().SP);
}

// Free-run up to `max_frames` frames, stopping as soon as the debugger pauses.
static void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

// Same, driven one instruction at a time through the debugger's single-step
// entry point instead of run_frame().
static void step_until_paused(Emulator& emu, int max_steps = 200) {
    for (int i = 0; i < max_steps && !emu.debug_state().paused(); ++i)
        emu.execute_single_instruction();
}

int main() {
    std::printf("\n======================================================\n");
    std::printf("Step Out (F8) unit tests (GH #203)\n");
    std::printf("======================================================\n\n");

    // ───────────────────────── Predicate ──────────────────────────────

    // STPOUT-P01 — plain RET out of the armed frame.
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P01", "RET popping past the armed SP satisfies Step Out",
              ret_from(ds, 0xFF00, 0xC9));
    }

    // STPOUT-P02 — a TAKEN conditional return is a return. All eight of them:
    // the old predicate matched none, so `RET NZ` ran straight past the exit.
    {
        DebugState ds = armed_at(0xFF00);
        const uint8_t ret_cc[] = { 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8 };
        bool all = true;
        for (uint8_t op : ret_cc)
            all = all && ret_from(ds, 0xFF00, op);
        check("STPOUT-P02", "all eight taken RET cc satisfy Step Out", all);
    }

    // STPOUT-P03 — an UNTAKEN RET cc moved no stack, so it is not a return.
    // This is what the SP-delta condition buys: the opcode alone cannot tell.
    {
        DebugState ds = armed_at(0xFF00);
        bool none = true;
        const uint8_t ret_cc[] = { 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8 };
        for (uint8_t op : ret_cc)
            none = none && !ds.check_step_out(0xFF00, 0xFF00, op, 0x00);
        check("STPOUT-P03", "untaken RET cc (SP unchanged) does not satisfy Step Out",
              none);
    }

    // STPOUT-P03b — the same, from a routine that has already popped its way
    // ABOVE the armed SP (a routine that lifts its own return address to read
    // inline data does exactly this). Here condition 3 is satisfied, so ONLY
    // the pop requirement can reject it — without it, an untaken RET cc ends
    // the step in the middle of the routine.
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P03b", "untaken RET cc with SP already above the armed point does not satisfy",
              !ds.check_step_out(0xFF04, 0xFF04, 0xC0, 0x00));
    }

    // STPOUT-P04 / P05 — RETI and RETN.
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P04", "RETI (ED 4D) satisfies Step Out",
              ret_from(ds, 0xFF00, 0xED, 0x4D));
        check("STPOUT-P05", "RETN (ED 45) satisfies Step Out",
              ret_from(ds, 0xFF00, 0xED, 0x45));
    }

    // STPOUT-P06 — the six undocumented RETN aliases the Z80 decodes as RETN.
    {
        DebugState ds = armed_at(0xFF00);
        const uint8_t aliases[] = { 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D };
        bool all = true;
        for (uint8_t op2 : aliases)
            all = all && ret_from(ds, 0xFF00, 0xED, op2);
        check("STPOUT-P06", "undocumented RETN aliases (ED 55/5D/65/6D/75/7D) satisfy",
              all);
    }

    // STPOUT-P07 — an ED-prefixed instruction that is NOT a return does not
    // satisfy it even when the stack happens to move (LDIR moves none, but the
    // row pins the opcode discrimination, not the SP one).
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P07", "ED B0 (LDIR) does not satisfy Step Out",
              !ret_from(ds, 0xFF00, 0xED, 0xB0));
    }

    // STPOUT-P08 — a bare POP moves SP exactly like a RET does. Only the
    // opcode test separates them.
    {
        DebugState ds = armed_at(0xFF00);
        const uint8_t pops[] = { 0xC1, 0xD1, 0xE1, 0xF1 };
        bool none = true;
        for (uint8_t op : pops)
            none = none && !ret_from(ds, 0xFF00, op);
        check("STPOUT-P08", "POP BC/DE/HL/AF does not satisfy Step Out", none);
    }

    // STPOUT-P09 — THE off-by-one row. A subroutine called AFTER arming pushes
    // below the armed SP, so its own RET pops back exactly ONTO it. That is a
    // return into the armed routine, not out of it. The dead predicate used
    // `sp >= step_out_sp_` and would have ended the step here.
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P09", "a nested call's RET (lands ON the armed SP) does not satisfy",
              !ret_from(ds, 0xFEFE, 0xC9));
    }

    // STPOUT-P10 — two levels deeper, likewise.
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P10", "a doubly-nested RET does not satisfy Step Out",
              !ret_from(ds, 0xFEFC, 0xC9));
    }

    // STPOUT-P11 — an interrupt accepted while stepping out pushes below the
    // armed SP; its RETI returns INTO the armed routine and must be ignored.
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P11", "an ISR's RETI (returning into the armed routine) does not satisfy",
              !ret_from(ds, 0xFEFE, 0xED, 0x4D));
    }

    // STPOUT-P12 — arming DEEPER: the same nested RET DOES end a Step Out that
    // was armed inside the nested routine. Same stimulus as P09, different
    // arming point, opposite verdict — so P09 cannot pass by simply always
    // returning false.
    {
        DebugState ds = armed_at(0xFEFE);
        check("STPOUT-P12", "the nested RET ends a Step Out armed inside the nested routine",
              ret_from(ds, 0xFEFE, 0xC9));
    }

    // STPOUT-P13 — SP moving by anything other than +2 is not a return
    // (a PUSH, or a wholesale LD SP,nn).
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P13a", "SP-2 (push-shaped) does not satisfy Step Out",
              !ds.check_step_out(0xFF00, 0xFEFE, 0xC9, 0x00));
        check("STPOUT-P13b", "SP+4 does not satisfy Step Out",
              !ds.check_step_out(0xFF00, 0xFF04, 0xC9, 0x00));
        check("STPOUT-P13c", "SP unchanged does not satisfy Step Out",
              !ds.check_step_out(0xFF00, 0xFF00, 0xC9, 0x00));
    }

    // STPOUT-P14 — inert unless StepMode::OUT is the live mode.
    {
        DebugState ds;
        ds.set_active(true);
        check("STPOUT-P14a", "mode NONE: predicate is inert",
              !ret_from(ds, 0xFF00, 0xC9));
        ds.step_into();
        check("STPOUT-P14b", "mode INTO: predicate is inert",
              !ret_from(ds, 0xFF00, 0xC9));
        ds.step_over(0x8003);
        check("STPOUT-P14c", "mode OVER: predicate is inert",
              !ret_from(ds, 0xFF00, 0xC9));
        ds.run_to_cycle(1000);
        check("STPOUT-P14d", "mode RUN_TO_CYCLE: predicate is inert",
              !ret_from(ds, 0xFF00, 0xC9));
    }

    // STPOUT-P15 — the step is CONSUMED by the pause it causes: mode returns
    // to NONE, so a second return does not re-trigger it.
    {
        DebugState ds = armed_at(0xFF00);
        check("STPOUT-P15a", "armed: step_mode() is OUT",
              ds.step_mode() == StepMode::OUT);
        ds.pause();
        check("STPOUT-P15b", "after the pause it causes, step_mode() is NONE",
              ds.step_mode() == StepMode::NONE);
        check("STPOUT-P15c", "and the predicate no longer fires",
              !ret_from(ds, 0xFF00, 0xC9));
    }

    // STPOUT-P16 — resume() (F5) also disarms it.
    {
        DebugState ds = armed_at(0xFF00);
        ds.resume();
        check("STPOUT-P16", "resume() disarms Step Out",
              ds.step_mode() == StepMode::NONE && !ret_from(ds, 0xFF00, 0xC9));
    }

    // ────────────────────────── Wiring ────────────────────────────────

    // STPOUT-W01 — the headline defect. Arm F8 inside a subroutine, free-run,
    // and the machine must STOP, on the instruction after the CALL.
    //   9000  00        NOP
    //   9001  C9        RET
    {
        Emulator emu;
        build(emu, { 0x00, 0xC9 });
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W01a", "Step Out from a subroutine pauses the machine (pre-#203: never did)",
              emu.debug_state().paused());
        // `paused() &&` is not decoration: main's landing marker is a self-loop
        // at 0x8003, so a machine that never stopped at all also sits at that PC.
        check("STPOUT-W01b", "and it stops on the instruction after the CALL",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
        check("STPOUT-W01c", "and the step mode is consumed (back to NONE)",
              emu.debug_state().step_mode() == StepMode::NONE);
    }

    // STPOUT-W02 — a nested CALL's RET does not end the step. If it did, the
    // machine would stop at 0x9004 (just after the inner CALL) instead.
    //   9000  00           NOP
    //   9001  CD 00 91     CALL SUB2
    //   9004  00           NOP
    //   9005  C9           RET
    //   9100  00           NOP
    //   9101  C9           RET
    {
        Emulator emu;
        build(emu, { 0x00, 0xCD, 0x00, 0x91, 0x00, 0xC9 }, { 0x00, 0xC9 });
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W02a", "a nested call's RET does not end the step",
              emu.debug_state().paused() && emu.cpu().get_registers().PC != 0x9004);
        check("STPOUT-W02b", "the step ends on the OUTER return, at 0x8003",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }

    // STPOUT-W03 — a taken conditional return ends the step.
    //   9000  3E 01     LD A,1
    //   9002  B7        OR A        (Z=0)
    //   9003  C0        RET NZ      -> taken
    //   9004  C9        RET         (never reached)
    {
        Emulator emu;
        build(emu, { 0x3E, 0x01, 0xB7, 0xC0, 0xC9 });
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W03", "a taken RET NZ ends the step at 0x8003",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }

    // STPOUT-W04 — an UNTAKEN conditional return does not. A false stop here
    // would leave PC at 0x9004, the instruction after the RET NZ.
    //   9000  AF        XOR A       (Z=1)
    //   9001  C0        RET NZ      -> not taken
    //   9002  00        NOP
    //   9003  C9        RET
    {
        Emulator emu;
        build(emu, { 0xAF, 0xC0, 0x00, 0xC9 });
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W04", "an untaken RET NZ does not end the step (stops at 0x8003, not 0x9002)",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }

    // STPOUT-W05 — RETI and RETN are returns too, through the real CPU.
    {
        Emulator emu;
        build(emu, { 0xED, 0x4D });                    // RETI
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W05a", "RETI ends the step at 0x8003",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }
    {
        Emulator emu;
        build(emu, { 0xED, 0x45 });                    // RETN
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W05b", "RETN ends the step at 0x8003",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }

    // STPOUT-W06 — a bare POP that unwinds past the armed SP is not a return.
    // A false stop leaves PC at 0x9001.
    //   9000  E1        POP HL      (SP now armed+2, HL = return address)
    //   9001  E5        PUSH HL     (SP back to armed)
    //   9002  C9        RET
    {
        Emulator emu;
        build(emu, { 0xE1, 0xE5, 0xC9 });
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W06", "POP HL past the armed SP does not end the step",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }

    // STPOUT-W07 — the NEGATIVE control. A subroutine that never returns must
    // keep running, with Step Out still armed. Without this row every other
    // wiring row above could be satisfied by a fix that just pauses always.
    //   9000  18 FE     JR $
    {
        Emulator emu;
        build(emu, { 0x18, 0xFE });
        enter_sub_and_arm(emu);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        check("STPOUT-W07a", "a subroutine that never returns keeps running",
              !emu.debug_state().paused());
        check("STPOUT-W07b", "and Step Out stays armed while it does",
              emu.debug_state().step_mode() == StepMode::OUT);
    }

    // STPOUT-W08 — the single-step path reaches the check too. The predicate
    // is called from the ONE shared per-instruction body, so driving the
    // machine with execute_single_instruction() must stop at the same place
    // free-running does. (A fix wired only into run_frame()'s loop fails here.)
    {
        Emulator emu;
        build(emu, { 0x00, 0xCD, 0x00, 0x91, 0x00, 0xC9 }, { 0x00, 0xC9 });
        enter_sub_and_arm(emu);
        step_until_paused(emu);
        check("STPOUT-W08", "execute_single_instruction() honours Step Out identically",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }

    // STPOUT-W09 — a PC breakpoint inside the subroutine still wins while the
    // step is running out.
    //   9000  00 00 00  NOP NOP NOP
    //   9003  C9        RET
    {
        Emulator emu;
        build(emu, { 0x00, 0x00, 0x00, 0xC9 });
        enter_sub_and_arm(emu);
        emu.debug_state().breakpoints().add_pc(0x9002);
        run_until_paused(emu);
        check("STPOUT-W09", "a breakpoint hit during a Step Out stops there, not at the return",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x9002);
    }

    // STPOUT-W10 — the same shape end to end: a subroutine that lifts its own
    // return address off the stack (SP now ABOVE the armed point) and then
    // runs an UNTAKEN conditional return. Nothing was popped by it, so the
    // step must continue; a false stop leaves PC at 0x9003.
    //   9000  D1        POP DE      (SP = armed+2, DE = 0x8003)
    //   9001  AF        XOR A       (Z=1)
    //   9002  C0        RET NZ      -> not taken
    //   9003  D5        PUSH DE     (SP = armed)
    //   9004  C9        RET
    {
        Emulator emu;
        build(emu, { 0xD1, 0xAF, 0xC0, 0xD5, 0xC9 });
        enter_sub_and_arm(emu);
        run_until_paused(emu);
        check("STPOUT-W10", "an untaken RET cc above the armed SP does not end the step",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
    }

    // STPOUT-W11 — the step must not READ memory the CPU does not. Deciding
    // whether the instruction was a return needs its opcode bytes, and they
    // are read through the ordinary Mmu::read(), which fires watchpoints and
    // latches the +3 floating bus. So only bytes the CPU itself fetches may
    // be read: the byte at PC always, the byte at PC+1 only behind an 0xED
    // prefix. Reading PC+1 unconditionally trips a READ watchpoint on the
    // byte after any one-byte instruction — here, the byte after the RET.
    //   9000  AF        XOR A
    //   9001  C9        RET
    //   9002            never fetched by the CPU  <- watchpoint here
    {
        Emulator emu;
        build(emu, { 0xAF, 0xC9 });
        enter_sub_and_arm(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x9002, WatchType::READ);
        step_until_paused(emu);
        check("STPOUT-W11a", "Step Out still ends at 0x8003 with a watchpoint armed",
              emu.debug_state().paused() && emu.cpu().get_registers().PC == 0x8003);
        check("STPOUT-W11b", "and it did not read (or trip a watchpoint on) a byte the CPU never fetched",
              !emu.debug_state().data_bp_hit());
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
