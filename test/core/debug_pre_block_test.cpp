// GH #498 - DebugState::pre_block_live(): run_frame()'s per-instruction armed() block is skipped
// while nothing in it can answer "yes".
//
// The block (emulator.cpp, the top of run_frame()'s loop) tests, per instruction: a pause, the
// verify window, a pending step-off, the PC / one-shot breakpoints, an `Execute` subscription and
// the step modes. With a client attached and none of those live (the debugger window open on a
// running program) every test answers no. pre_block_live() is the cached form of "armed() and
// any of them live"; its inputs have many writers, so the rows hold the CACHE to a recomputation
// from the raw inputs after every mutator, and hold the BEHAVIOUR to the same machine run with the
// block forced live.
//
// Oracle: the behaviour the block had before (a breakpoint stops the machine at its PC; the same
// machine hashes the same with the block run or skipped); the cached bit against the recomputed
// predicate. The block's own semantics are held by the breakpoint, step and subscription suites,
// which run unchanged.
//
// No ROM, no SD image: a bare 48K machine with a program written straight into RAM.
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "batch_ab.h"

#include <bitset>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;
int g_eq_checked = 0, g_eq_bad = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;

constexpr uint16_t kMain = 0x8000;

struct Rig {
    std::unique_ptr<Emulator> emu;
    std::unique_ptr<Debugger> dbg;
    ClientId client = jnext::dbg::CLIENT_NONE;

    // `attach`: a Gui client, as the debugger window's. The program is NOP x16 then JR $ at 8010.
    explicit Rig(bool attach = true) {
        emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        emu->init(cfg);
        for (uint16_t i = 0; i < 16; ++i) emu->mmu().write(static_cast<uint16_t>(kMain + i), 0x00);
        emu->mmu().write(kMain + 16, 0x18);
        emu->mmu().write(kMain + 17, 0xFE);
        Z80Registers r = emu->cpu().get_registers();
        r.PC = kMain;
        r.SP = 0xFF00;
        r.IFF1 = r.IFF2 = 0;
        emu->cpu().set_registers(r);
        dbg = std::make_unique<Debugger>(*emu);
        if (attach)
            client = dbg->attach(jnext::dbg::ClientInfo{"Qt GUI", jnext::dbg::ClientKind::Gui}).value;
    }
    DebugState& ds() { return emu->debug_state(); }
    // The cache against the recomputation: every row calls this after each mutator.
    bool eq() {
        ++g_eq_checked;
        const bool same = ds().pre_block_live() == ds().pre_block_live_recomputed_for_test();
        if (!same) ++g_eq_bad;
        return same;
    }
    bool live() { return ds().pre_block_live(); }
};

std::string b2s(bool b) { return b ? "1" : "0"; }

jnext::dbg::Subscription exec_sub(uint16_t pc) {
    jnext::dbg::Subscription s;
    s.kind = jnext::dbg::EventKind::Execute;
    s.filter.lo = s.filter.hi = pc;
    s.action = jnext::dbg::Action::Stop;
    return s;
}

}  // namespace

int main() {
    std::printf("GH #498 pre_block_live (the per-instruction armed block)\n");
    std::printf("===============================================\n\n");

    // PRB-01 - unarmed: never live, whatever the breakpoint set holds.
    {
        Rig r(false);
        r.ds().breakpoints().add_pc(kMain + 4);
        check("PRB-01", "no client attached (unarmed): the block is not live, with or without a PC breakpoint",
              !r.ds().armed() && !r.live() && r.eq(), "armed=" + b2s(r.ds().armed()));
    }

    // PRB-02 - a client attached and nothing live: armed, but the block is skipped.
    {
        Rig r;
        check("PRB-02", "a client attached with nothing live: the machine is armed and the block is not live",
              r.ds().armed() && !r.live() && r.eq(), "armed=" + b2s(r.ds().armed()) + " live=" + b2s(r.live()));
    }

    // PRB-03 - PC breakpoints: live while one is enabled, idle when removed, disabled or suspended.
    {
        Rig r;
        r.ds().breakpoints().add_pc(kMain + 4);
        const bool added = r.live() && r.eq();
        r.ds().breakpoints().set_pc_enabled(kMain + 4, false);
        const bool disabled = !r.live() && r.eq();
        r.ds().breakpoints().set_pc_enabled(kMain + 4, true);
        const bool enabled = r.live() && r.eq();
        r.ds().breakpoints().set_master_enabled(false);
        const bool suspended = !r.live() && r.eq();
        r.ds().breakpoints().set_master_enabled(true);
        const bool resumed = r.live() && r.eq();
        r.ds().breakpoints().remove_pc(kMain + 4);
        const bool removed = !r.live() && r.eq();
        r.ds().breakpoints().add_pc(kMain + 6);
        r.ds().breakpoints().add_pc(kMain + 8);
        r.ds().breakpoints().clear_all_pc();
        const bool cleared = !r.live() && r.eq();
        check("PRB-03", "a PC breakpoint makes the block live; disabled, suspended (master switch), removed or cleared it is idle again",
              added && disabled && enabled && suspended && resumed && removed && cleared,
              b2s(added) + b2s(disabled) + b2s(enabled) + b2s(suspended) + b2s(resumed) + b2s(removed) + b2s(cleared));
    }

    // PRB-04 - the one-shot.
    {
        Rig r;
        r.ds().breakpoints().set_oneshot(kMain + 8);
        const bool set = r.live() && r.eq();
        r.ds().breakpoints().clear_oneshot();
        check("PRB-04", "a one-shot breakpoint (Step Over / Run to Here) makes the block live; cleared it is idle",
              set && !r.live() && r.eq());
    }

    // PRB-05 - a pause and its resume: the step-off arm is consumed by the first instruction.
    {
        Rig r;
        r.ds().pause();
        const bool paused = r.live() && r.eq();
        r.ds().resume();
        const bool step_off = r.live() && r.eq();           // the GH #221 step-off is pending
        r.emu->run_frame();                                   // the block consumes it
        // The consume itself (what the block's first instruction does) ends the liveness at once,
        // not at the next frame boundary's gate refresh.
        Rig c;
        c.ds().pause();
        c.ds().resume();
        const bool consumed = c.ds().consume_step_off() && !c.live() && c.eq();
        check("PRB-05", "a pause makes the block live; after the resume the pending step-off keeps it live until the first instruction consumes it, then idle (at once, and after a frame)",
              paused && step_off && !r.live() && r.eq() && !r.ds().paused() && consumed,
              b2s(paused) + b2s(step_off) + " live=" + b2s(r.live()) + " consumed=" + b2s(consumed));
    }

    // PRB-06 - every step mode.
    {
        Rig r;
        bool all = true;
        std::string d;
        auto mode = [&](const char* name, auto fn) {
            fn();
            const bool ok = r.live() && r.eq();
            if (!ok) { all = false; d += std::string(" ") + name; }
            r.ds().resume();                                  // back to no step mode
            r.emu->run_frame();                               // and the step-off consumed
        };
        mode("into", [&] { r.ds().step_into(); });
        mode("over", [&] { r.ds().step_over(kMain + 2); });
        mode("out", [&] { r.ds().step_out(0xFF00); });
        mode("run_to", [&] { r.ds().run_to(kMain + 4); });
        mode("run_to_cycle", [&] { r.ds().run_to_cycle(1000000); });
        mode("step_back", [&] { r.ds().step_back(1); });
        mode("run_back", [&] { r.ds().run_back_to_cycle(1); });
        mode("over_sub", [&] { r.ds().step_over_subscribed(); });
        r.ds().breakpoints().clear_oneshot();
        r.ds().resume();
        r.emu->run_frame();
        check("PRB-06", "each step mode (into, over, out, run to, run to cycle, step back, run back, the subscribed Over) makes the block live",
              all && r.eq(), d);
    }

    // PRB-07 - the tape-identity verify window.
    {
        Rig r;
        r.ds().set_verify_window(0x8000, 0x80FF);
        const bool set = r.live() && r.eq();
        r.ds().clear_verify_window();
        check("PRB-07", "a verify window makes the block live; cleared it is idle", set && !r.live() && r.eq());
    }

    // PRB-08 - an Execute subscription.
    {
        Rig r;
        const auto id = r.dbg->subscribe(r.client, exec_sub(kMain + 8)).value;
        const bool sub = r.live() && r.eq();
        r.dbg->set_enabled(r.client, id, false);
        const bool off = !r.live() && r.eq();
        r.dbg->set_enabled(r.client, id, true);
        const bool on = r.live() && r.eq();
        r.dbg->unsubscribe(r.client, id);
        check("PRB-08", "a live Execute subscription makes the block live; disabled or removed it is idle",
              sub && off && on && !r.live() && r.eq(),
              b2s(sub) + b2s(off) + b2s(on) + " live=" + b2s(r.live()));
    }

    // PRB-09 - the scopes that disarm the machine for a save or a replay.
    {
        Rig r;
        r.ds().breakpoints().add_pc(kMain + 4);
        bool inside_idle = false, inside_eq = false;
        {
            DebugState::SuspendScope scope(r.ds());
            inside_idle = !r.live();
            inside_eq = r.eq();
        }
        const bool after = r.live() && r.eq();
        r.ds().breakpoints().clear_all_pc();
        bool replay_live = false, replay_eq = false;
        {
            DebugState::ReplayArmScope scope(r.ds());
            replay_live = r.ds().armed() && !r.live();       // armed, nothing live
            replay_eq = r.eq();
        }
        // A pending step-off (a pause and its resume) is restored by the scope AFTER its gate refresh.
        Rig s;
        s.ds().pause();
        s.ds().resume();
        const bool pending = s.live() && s.eq();
        { DebugState::SuspendScope scope(s.ds()); }
        const bool pending_after = s.live() && s.eq();
        check("PRB-09", "SuspendScope disarms (idle inside, live again after with its breakpoint or its pending step-off); ReplayArmScope arms without making the block live; the cache is exact throughout",
              inside_idle && inside_eq && after && replay_live && replay_eq && pending && pending_after && r.eq(),
              "pending=" + b2s(pending) + " after=" + b2s(pending_after));
    }

    // PRB-10 - the cache was held to the recomputation at every step above.
    check("PRB-10", "every mutator checked above left the cached bit equal to the predicate recomputed from its raw inputs",
          g_eq_checked >= 30 && g_eq_bad == 0,
          "checked=" + std::to_string(g_eq_checked) + " bad=" + std::to_string(g_eq_bad));

    // PRB-11 - behaviour: with the block skipped, a breakpoint added later still stops the machine
    // at its PC, and removing it lets the machine run on.
    {
        Rig r;
        r.emu->run_frame();                                   // idle: skipped block, machine runs
        const bool ran = !r.ds().paused();
        r.ds().breakpoints().add_pc(kMain + 16);               // the JR $ the machine is parked on
        r.emu->run_frame();
        const bool stopped = r.ds().paused() && r.emu->cpu().get_registers().PC == kMain + 16;
        r.ds().breakpoints().remove_pc(kMain + 16);
        r.ds().resume();
        r.emu->run_frame();
        check("PRB-11", "a machine running with the block skipped stops at a PC breakpoint added later, and runs on after it is removed",
              ran && stopped && !r.ds().paused() && r.eq(),
              b2s(ran) + b2s(stopped) + " paused=" + b2s(r.ds().paused()));
    }

    // PRB-12 - the A/B: the same busy program with the block skipped (nothing live) and with it run
    // (a PC breakpoint at an address the program never reaches) hashes the same every frame.
    {
        auto run = [](bool force_live) {
            std::vector<batch_ab::FrameHash> out;
            Rig r;
            const uint8_t prog[] = {0x21, 0x00, 0x90, 0x34, 0x18, 0xFD};   // LD HL,9000 / INC (HL) / JR -3
            for (size_t i = 0; i < sizeof prog; ++i) r.emu->mmu().write(static_cast<uint16_t>(kMain + i), prog[i]);
            Z80Registers z = r.emu->cpu().get_registers();
            z.PC = kMain;
            r.emu->cpu().set_registers(z);
            if (force_live) r.ds().breakpoints().add_pc(0x0000);
            batch_ab::Scratch scratch;
            for (int f = 0; f < 40; ++f) {
                r.emu->run_frame();
                out.push_back(batch_ab::capture(*r.emu, scratch, true));
            }
            return std::make_pair(out, r.live());
        };
        const auto skipped = run(false);
        const auto forced = run(true);
        bool same = skipped.first.size() == 40 && forced.first.size() == 40;
        for (size_t f = 0; same && f < 40; ++f)
            same = skipped.first[f].state == forced.first[f].state && skipped.first[f].video == forced.first[f].video &&
                   skipped.first[f].audio == forced.first[f].audio;
        check("PRB-12", "a busy program, 40 frames with the gate idle (block skipped) and with it forced live by an unreachable breakpoint: identical state, video and audio hashes every frame (the gate was idle in one run and live in the other; no per-instruction counter, which would cost what the gate saves)",
              same && !skipped.second && forced.second,
              "skipped_live=" + b2s(skipped.second) + " forced_live=" + b2s(forced.second));
    }

    // GH #584 - a slot that starts inside an instruction (a long DD/FD run cut after a prefix M1,
    // Z80Cpu::prefix_pending()): the machine advances a bounded slot at a time, PC-keyed stops are
    // skipped there as they are inside a short chain, coverage marks only the instruction's start.
    // Oracle: t80n.vhd:490-498 (a DD/FD is a 4 T M1), :1765, :1768 (no INT/NMI after a prefix M1).

    // PRB-13 - RAM from 0x4000 to the top all DD, IFF1 = 1: each run_frame() returns within one slot
    // of its frame end, the frame counter advances, no INT is taken, and a Step is one bounded slot.
    {
        Rig r;
        for (uint32_t a = 0x4000; a <= 0xFFFF; ++a) r.emu->mmu().write(static_cast<uint16_t>(a), 0xDD);
        Z80Registers z = r.emu->cpu().get_registers();
        z.PC = 0x4000;
        z.SP = 0xFF00;
        z.IFF1 = z.IFF2 = 1;
        z.IM = 1;
        r.emu->cpu().set_registers(z);
        const uint32_t f0 = r.emu->frame_num();
        const uint64_t t0 = r.emu->monotonic_tstates();
        r.emu->run_frame();
        r.emu->run_frame();
        const uint64_t dt = r.emu->monotonic_tstates() - t0;
        const uint32_t df = r.emu->frame_num() - f0;
        const uint64_t two_frames = 2u * 69888u;
        const bool bounded = dt >= two_frames - 700 && dt <= two_frames + 700;
        const bool no_int = r.emu->cpu().get_registers().SP == 0xFF00 && r.emu->cpu().prefix_pending() != 0;
        // A Step into the chain: one slot, at most 65 prefix M1s.
        const uint16_t pc0 = r.emu->cpu().pc();
        r.emu->execute_single_instruction();
        const uint16_t step_adv = static_cast<uint16_t>(r.emu->cpu().pc() - pc0);
        check("PRB-13", "all-DD RAM, IFF1 = 1: two run_frame() calls end within a slot of 2 frames, the frame counter moves by 2, no INT is taken, and a Step advances at most 65 bytes",
              bounded && df == 2 && no_int && step_adv >= 1 && step_adv <= 65,
              "dt=" + std::to_string(dt) + " frames=" + std::to_string(df) + " step_adv=" + std::to_string(step_adv) +
                  " no_int=" + b2s(no_int));
    }

    // PRB-14 - a PC breakpoint inside a 200 DD chain, on the address a cut slot starts at, never stops
    // the machine; one on the chain's first byte does.
    {
        const uint16_t chain = kMain + 1, inside = chain + 65;
        auto build = [&](Rig& r) {
            r.emu->mmu().write(kMain, 0x00);                                       // NOP
            for (int i = 0; i < 200; ++i) r.emu->mmu().write(static_cast<uint16_t>(chain + i), 0xDD);
            r.emu->mmu().write(chain + 200, 0x00);                                 // NOP ends the chain
            r.emu->mmu().write(chain + 201, 0x18);                                 // JR $
            r.emu->mmu().write(chain + 202, 0xFE);
        };
        Rig a;
        build(a);
        a.ds().breakpoints().add_pc(inside);
        a.emu->run_frame();
        const bool ran_through = !a.ds().paused() && a.emu->cpu().pc() == chain + 201;
        Rig b;
        build(b);
        b.ds().breakpoints().add_pc(chain);
        b.emu->run_frame();
        const bool stopped_at_start = b.ds().paused() && b.emu->cpu().pc() == chain;
        check("PRB-14", "a PC breakpoint on the address a cut slot starts at (inside a 200 DD chain) does not stop the machine; one on the chain's first byte does",
              ran_through && stopped_at_start,
              "ran_through=" + b2s(ran_through) + " stopped_at_start=" + b2s(stopped_at_start));
    }

    // PRB-15 - coverage on a 70 DD + NOP chain marks only the chain's first address.
    {
        Rig r;
        for (int i = 0; i < 70; ++i) r.emu->mmu().write(static_cast<uint16_t>(kMain + i), 0xDD);
        r.emu->mmu().write(kMain + 70, 0x00);
        Z80Registers z = r.emu->cpu().get_registers();
        z.PC = kMain;
        r.emu->cpu().set_registers(z);
        std::bitset<65536> cov;
        r.ds().set_coverage_sink(&cov);
        int slots = 0;
        do {
            r.emu->execute_single_instruction();
            ++slots;
        } while (r.emu->cpu().prefix_pending() != 0 && slots < 8);
        r.ds().set_coverage_sink(nullptr);
        check("PRB-15", "coverage over a 70 DD + NOP chain (two slots) marks only the chain's first address",
              slots == 2 && cov.count() == 1 && cov[kMain],
              "slots=" + std::to_string(slots) + " marked=" + std::to_string(cov.count()));
    }

    // PRB-16 - the Execute-subscription twin of PRB-14: a Stop subscription on the address a cut slot
    // starts at (inside a 200 DD chain) never fires; one on the chain's first byte does.
    {
        const uint16_t chain = kMain + 1, inside = chain + 65;
        auto run = [&](uint16_t at) {
            Rig r;
            r.emu->mmu().write(kMain, 0x00);
            for (int i = 0; i < 200; ++i) r.emu->mmu().write(static_cast<uint16_t>(chain + i), 0xDD);
            r.emu->mmu().write(chain + 200, 0x00);
            r.emu->mmu().write(chain + 201, 0x18);
            r.emu->mmu().write(chain + 202, 0xFE);
            r.dbg->subscribe(r.client, exec_sub(at));
            r.emu->run_frame();
            return std::make_pair(r.ds().paused(), r.emu->cpu().pc());
        };
        const auto a = run(inside), b = run(chain);
        check("PRB-16", "an Execute subscription on the address a cut slot starts at (inside a 200 DD chain) does not fire; one on the chain's first byte does",
              !a.first && a.second == chain + 201 && b.first && b.second == chain,
              "inside_paused=" + b2s(a.first) + " first_paused=" + b2s(b.first));
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
