// GH #489 - the HALT batch with an ESP-01 or a debugger client attached: bit-identical equivalence
// (batching off vs on, per frame) and the engagement counter, on synthetic fixtures.
//
// Oracle: the unbatched per-slot path of the same emulator, held to the VHDL by the other suites
// (the method of halt_batch_fixtures.h / batch_ab.h). Each row also states the counter it expects:
// a refusal row is a statement that the batch stands aside, an engaged row that it does not.
//
// Rows: janko's four scenarios (HBQ-60/63/64: ESP at boot, disabled and enabled by a cold boot
// during the run; HBQ-65: the debugger window open) and the observers a client can still hold
// (HBQ-61/62: a UART device that wants ticks, channel 0 and 1; HBQ-66..75: Execute and Mem
// subscriptions, run-to-cycle, a Cycle subscription, coverage).
//
// No ROM, no SD image: a bare Next with a program written straight into RAM.
//
// HBQ-60/63/64 (the rows that build the real ThreadedEsp) live in halt_batch_observer_native_test:
// a live ESP needs a worker thread, which the WebAssembly build has not. The fixtures are in
// halt_batch_observer.h.
#include "halt_batch_observer.h"

using namespace hbq;

int main() {
    std::printf("GH #489 HALT batch with an ESP-01 / debugger client attached (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // HBQ-61: a UART device on channel 0 that wants ticks for a stretch (one byte every 150000 master
    // cycles, from frame 10: wanted through frames 10 and 11): the batch stands aside while it wants
    // them (the bytes land at the same cycles as unbatched) and engages before and after.
    {
        const Spec s = scripted_uart("uart-device-ch0", 0);
        const Pair p = run_pair(s);
        check("HBQ-61", "a UART device on channel 0 that wants ticks for a stretch: identical per frame (every byte lands at the same cycle), the batch engaged before and after",
              p.diff.empty() && p.on.counter[9] > 0 && !grew(p, 9, 11) && grew(p, 11, 39), detail_of(p));
    }

    // HBQ-62: the same on channel 1 (the Pi's channel): kills "only channel 0's device is checked".
    {
        const Spec s = scripted_uart("uart-device-ch1", 1);
        const Pair p = run_pair(s);
        check("HBQ-62", "a UART device on channel 1 that wants ticks for a stretch: identical per frame, the batch engaged before and after",
              p.diff.empty() && p.on.counter[9] > 0 && !grew(p, 9, 11) && grew(p, 11, 39), detail_of(p));
    }

    // HBQ-65: janko's scenario 4 - the debugger window open: a Gui client attached with the live raster
    // and the call-stack tracker (DebuggerManager::attach_backend()). An idle client observes nothing per
    // instruction: the batch engages, and everything it stopped doing is still right: the call stack
    // (the CALL into the HALT routine and the ISR's CALL/RETI frames, every field) and the raster walk
    // (position, frame flag, both interrupt pulse counts), compared per frame.
    {
        Spec s = with_window("window-open", 60);
        const Pair p = run_pair(s);
        check("HBQ-65", "debugger window open (client + live raster + call stack) on EI;HALT at 28 MHz: identical per frame (state, video, audio, call-stack frames, raster walk), the HALT batch engaged",
              p.diff.empty() && p.on.run.halt_batched > 0, detail_of(p));
    }

    // HBQ-66: an Execute subscription AT the HALT, a resume before each frame: each resume stops after
    // exactly one repeat (a batch would carry the stop past the subscription). Refused while it is live,
    // engaged once it is removed (unsubscribe at frame 20).
    {
        Spec s = with_window("window-execute-at-halt", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.id = r.dbg->subscribe(r.client, exec_at(kHaltPc)).value;
        };
        s.before = [](Rig& r, int f) {
            if (f == 20) r.dbg->unsubscribe(r.client, r.id);
            resume_each_frame(r, f);
        };
        const Pair p = run_pair(s);
        check("HBQ-66", "an Execute subscription at the HALT with a resume per frame, then removed at frame 20: identical per frame, no batching while it is live, batched after",
              p.diff.empty() && p.on.counter[19] == 0 && grew(p, 20, 39), detail_of(p));
    }

    // HBQ-67: an Execute subscription at an UNRELATED address: the batch stays off (deliberately
    // conservative: any live subscription refuses); disabled at frame 20 it cannot fire and the batch
    // engages.
    {
        Spec s = with_window("window-execute-elsewhere", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.id = r.dbg->subscribe(r.client, exec_at(0x9000)).value;
        };
        s.before = [](Rig& r, int f) {
            if (f == 20) r.dbg->set_enabled(r.client, r.id, false);
        };
        const Pair p = run_pair(s);
        check("HBQ-67", "an Execute subscription elsewhere: identical per frame and the HALT batch refused while it is live, engaged once it is disabled at frame 20",
              p.diff.empty() && p.on.counter[19] == 0 && grew(p, 20, 39), detail_of(p));
    }

    // HBQ-68: a read watch (a Mem subscription) on the HALT's own address: the M1 fetch is a read for
    // watches, so each resume stops after one repeat, identically; the batch stays off.
    {
        Spec s = with_window("window-read-watch-at-halt", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            jnext::dbg::Subscription w;
            w.kind = jnext::dbg::EventKind::Mem;
            w.filter.lo = w.filter.hi = kHaltPc;
            w.access = jnext::dbg::Access::Read;
            w.action = jnext::dbg::Action::Stop;
            r.id = r.dbg->subscribe(r.client, w).value;
        };
        s.before = resume_each_frame;
        s.alive = [](Rig& r) { return r.emu->call_stack().frames().size() >= 1 && r.emu->debug_state().paused(); };
        const Pair p = run_pair(s);
        check("HBQ-68", "a read watch on the HALT's address with a resume per frame: identical per frame, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0, detail_of(p));
    }

    // HBQ-69: run-to-cycle (StepMode RUN_TO_CYCLE) aimed 100000 master cycles into a halted stretch of
    // frame 5: the stop lands on the identical clock and PC with the batching on as off, the counter
    // does not move while it is in flight, and the batch engages after the resume.
    {
        Spec s = with_window("window-run-to-cycle", 20);
        s.watch_debug = false;
        s.before = [](Rig& r, int f) {
            if (f != 5) return;
            r.dbg->run_to_cycle(r.client, r.emu->clock().get() + 100000);
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
        };
        s.after = [](Rig& r, int f) {
            if (f != 5) return;
            r.log.push_back("stop paused=" + std::to_string(r.emu->debug_state().paused()) +
                            " clock=" + std::to_string(r.emu->clock().get()) +
                            " pc=" + std::to_string(r.emu->cpu().pc()));
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
            r.dbg->run(r.client);
        };
        const Pair p = run_pair(s);
        // log = {#counter when armed, stop note, #counter at the stop}: frozen while in flight.
        const bool frozen = p.on.log.size() == 3 && p.on.log[0].substr(1) == p.on.log[2].substr(1);
        check("HBQ-69", "run-to-cycle into a halted stretch: the stop lands on the identical clock and PC batched as unbatched, the counter frozen while in flight, the batch engaged after the resume",
              p.diff.empty() && frozen && p.on.log[1].find("paused=1") != std::string::npos && grew(p, 5, 19),
              detail_of(p) + " " + (p.on.log.size() == 3 ? p.on.log[0] + " | " + p.on.log[1] + " | " + p.on.log[2] : "log?"));
    }

    // HBQ-70: a Cycle subscription (a boundary comparison against the master clock) aimed into a halted
    // stretch of frame 5: the stop lands on the identical boundary, the counter does not move while
    // it is armed, the batch engages after it fires and is removed.
    {
        Spec s = with_window("window-cycle-subscription", 20);
        s.watch_debug = false;
        s.before = [](Rig& r, int f) {
            if (f != 5) return;
            jnext::dbg::Subscription c;
            c.kind = jnext::dbg::EventKind::Cycle;
            c.filter.cycle = r.emu->clock().get() + 100000;
            c.action = jnext::dbg::Action::Stop;
            c.once = true;
            r.id = r.dbg->subscribe(r.client, c).value;
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
        };
        s.after = [](Rig& r, int f) {
            if (f != 5) return;
            r.log.push_back("stop paused=" + std::to_string(r.emu->debug_state().paused()) +
                            " clock=" + std::to_string(r.emu->clock().get()) +
                            " pc=" + std::to_string(r.emu->cpu().pc()));
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
            r.dbg->unsubscribe(r.client, r.id);
            r.dbg->run(r.client);
        };
        const Pair p = run_pair(s);
        const bool frozen = p.on.log.size() == 3 && p.on.log[0].substr(1) == p.on.log[2].substr(1);
        check("HBQ-70", "a Cycle subscription into a halted stretch: the stop lands on the identical boundary batched as unbatched, the counter frozen while it is armed, the batch engaged after",
              p.diff.empty() && frozen && p.on.log[1].find("paused=1") != std::string::npos && grew(p, 5, 19),
              detail_of(p) + " " + (p.on.log.size() == 3 ? p.on.log[0] + " | " + p.on.log[1] + " | " + p.on.log[2] : "log?"));
    }

    // HBQ-71: PC coverage and memory coverage on: the batch stays off (explicit statements of the
    // contract: both are idempotent bit sets), identical per frame, and the sets are identical.
    {
        Spec s = with_window("window-coverage", 30);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.dbg->coverage_enable(true);
            r.dbg->mem_coverage_enable(true);
        };
        s.after = [](Rig& r, int f) {
            if (f != 29) return;
            size_t pcs = 0;
            for (size_t i = 0; i < 65536; ++i) pcs += r.dbg->coverage().test(i) ? 1 : 0;
            uint8_t flags[64] = {};
            uint32_t sum = 0;
            for (uint32_t a = 0xC000; a < 0xC400; a += 64) {
                for (uint32_t i = 0; i < 64; ++i) flags[i] = r.dbg->mem_coverage_fetch(static_cast<uint16_t>(a + i));
                for (uint8_t b : flags) sum = sum * 31 + b;
            }
            r.log.push_back("pc_bits=" + std::to_string(pcs) + " mem_sum=" + std::to_string(sum));
        };
        const Pair p = run_pair(s);
        check("HBQ-71", "PC and memory coverage on: identical per frame, the coverage sets identical, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0 && p.on.log.size() == 1, detail_of(p));
    }

    // HBQ-73: a LEGACY PC breakpoint (the BreakpointSet model, not a subscription) AT the HALT with a
    // client attached and a resume per frame: each resume stops after one repeat; the batch stays off.
    {
        Spec s = with_window("window-legacy-breakpoint-at-halt", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.emu->debug_state().breakpoints().add_pc(kHaltPc);
        };
        s.before = resume_each_frame;
        s.alive = [](Rig& r) { return r.emu->call_stack().frames().size() >= 1 && r.emu->debug_state().paused(); };
        const Pair p = run_pair(s);
        check("HBQ-73", "a legacy PC breakpoint at the HALT with a client attached and a resume per frame: identical per frame, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0, detail_of(p));
    }

    // HBQ-74: a LEGACY I/O watchpoint (the same set's wp_all_, on the port the ISR writes) with a client
    // attached: the batch stays off (any watchpoint refuses, D3), identical per frame.
    {
        Spec s = with_window("window-legacy-io-watchpoint", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.emu->debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        };
        const Pair p = run_pair(s);
        check("HBQ-74", "a legacy I/O watchpoint with a client attached: identical per frame, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0, detail_of(p));
    }

    // HBQ-75: a Mem subscription elsewhere (no per-kind gate of its own covers it before it latches):
    // the batch stays off while it is live (D3: any live subscription refuses), engaged once it is
    // disabled at frame 20 (a disabled subscription is not live and cannot fire).
    {
        Spec s = with_window("window-mem-subscription-elsewhere", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            jnext::dbg::Subscription w;
            w.kind = jnext::dbg::EventKind::Mem;
            w.filter.lo = w.filter.hi = 0x9000;
            w.access = jnext::dbg::Access::ReadWrite;
            w.action = jnext::dbg::Action::Stop;
            r.id = r.dbg->subscribe(r.client, w).value;
        };
        s.before = [](Rig& r, int f) {
            if (f == 20) r.dbg->set_enabled(r.client, r.id, false);
        };
        const Pair p = run_pair(s);
        check("HBQ-75", "a Mem subscription elsewhere: identical per frame and the HALT batch refused while it is live, engaged once it is disabled at frame 20",
              p.diff.empty() && p.on.counter[19] == 0 && grew(p, 20, 39), detail_of(p));
    }

    // HBQ-72: the window CLOSED with the user's breakpoints kept: a non-arming observer client (the Qt
    // GUI's lifetime client) holds a live Execute subscription at the HALT and nothing arms the machine,
    // so the hot loop never reads it (GH #219): the batch must stay engaged, identical per frame.
    {
        Spec s = plain("observer-breakpoint-unarmed", 40);
        s.setup = [](Rig& r) {
            install_plain(*r.emu);
            r.dbg = std::make_unique<Debugger>(*r.emu);
            jnext::dbg::ClientInfo info{"Qt GUI breakpoints", jnext::dbg::ClientKind::Gui};
            info.observer = true;
            r.client = r.dbg->attach(info).value;
            r.dbg->subscribe(r.client, exec_at(static_cast<uint16_t>(kMain + 7)));
        };
        auto alive = s.alive;
        s.alive = [=](Rig& r) { return alive(r) && !r.emu->debug_state().armed(); };
        const Pair p = run_pair(s);
        check("HBQ-72", "a non-arming observer client with a live Execute subscription at the HALT (window closed, breakpoints kept): identical per frame, the HALT batch engaged",
              p.diff.empty() && p.on.run.halt_batched > 0, detail_of(p));
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
