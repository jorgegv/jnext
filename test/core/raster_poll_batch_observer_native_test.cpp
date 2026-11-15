// GH #496 - the raster-poll batch with an ESP-01 attached and the debugger window open (RPB-15).
// Split out of raster_poll_batch_observer_test, ID unchanged: the row builds the real ThreadedEsp,
// which needs a worker thread the WebAssembly build has not (the same split as
// halt_batch_observer_native_test). See raster_poll_fixtures.h for the method.
#include "raster_poll_fixtures.h"

#include "debug/debugger.h"

#include <memory>

namespace {

using jnext::dbg::Debugger;

// What the debugger window's attach switches on (DebuggerManager::attach_backend()). The Debugger is
// created in setup and dropped after the last frame, before the emulator it watches is destroyed.
struct Window {
    std::unique_ptr<Debugger> dbg;
    jnext::dbg::ClientId client = jnext::dbg::CLIENT_NONE;
};

Scenario with_window(Scenario s, std::shared_ptr<Window> w) {
    const int frames = s.frames;
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        w->dbg = std::make_unique<Debugger>(e);
        w->client = w->dbg->attach(jnext::dbg::ClientInfo{"Qt GUI", jnext::dbg::ClientKind::Gui}).value;
        w->dbg->set_live_raster(w->client, true);
        w->dbg->set_call_stack_enabled(w->client, true);
    };
    s.after_frame = [=](Emulator&, int f) {
        if (w->dbg) w->dbg->pump(jnext::dbg::PumpBudget{});
        if (f == frames - 1) w->dbg.reset();
    };
    return s;
}

// The call stack and the raster walk (the beam position the debugger window shows), as text.
std::string debug_snapshot(Emulator& e) {
    std::string s;
    for (const CallFrame& f : e.call_stack().frames()) {
        s += std::to_string(f.caller_pc) + "," + std::to_string(f.target_pc) + "," + std::to_string(f.sp_at_call) + "," +
             std::to_string(static_cast<int>(f.type)) + ";";
    }
    const VideoTiming& t = e.video_timing();
    s += "|" + std::to_string(t.pos().hc) + "," + std::to_string(t.pos().vc) + "," + std::to_string(t.frame_complete());
    return s;
}

} // namespace

int main() {
    std::printf("GH #496 raster-poll batch with an ESP-01 and the debugger window (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // RPB-15: an ESP-01 enabled and idle (it wants no tick, so G10 lets the batch through), and then the
    // debugger window open on top of it (a client with the live raster and the call-stack tracker: G2
    // does not refuse, the raster walk is advanced over the window).
    {
        Scenario esp = poll_fx("esp-idle", 3, 0xFF, guide_loop());
        esp.config = [](EmulatorConfig& c) { c.esp_enabled = true; };
        // The window fixture also compares the call stack and the raster walk after every frame and at a
        // mid-frame stop every third frame (run-to-cycle), where the window reads the beam: the walk is
        // advanced over each absorbed pass, and a pass leaves the call stack as the last real one did.
        std::vector<std::string> off, on;
        auto run_window = [&](bool poll, std::vector<std::string>& snaps) {
            auto w = std::make_shared<Window>();
            Scenario win = with_window(poll_fx("debugger-window", 3, 0xFF, preselected_loop(0xB4, true), 30), w);
            auto after = win.after_frame;
            win.after_frame = [=, &snaps](Emulator& e, int f) {
                after(e, f);
                snaps.push_back(debug_snapshot(e));
            };
            win.before_frame = [&snaps](Emulator& e, int f) {
                if (f < 4 || f % 3 != 1) return;
                e.debug_state().run_to_cycle(e.clock().get() + 250000 + static_cast<uint64_t>(f) * 53);
                e.run_frame();                       // stops mid-frame
                snaps.push_back("stop:" + std::to_string(e.debug_state().paused()) + ":" + debug_snapshot(e));
                e.debug_state().resume();
            };
            return batch_ab::run(win, poll, poll, g_scratch, poll ? 1 : 0);
        };
        const Run ra = run_window(false, off);
        const Run rb = run_window(true, on);
        const std::string d = batch_ab::first_diff(ra, rb);
        size_t bad = 0;
        while (bad < off.size() && bad < on.size() && off[bad] == on[bad]) ++bad;
        bool stops = false;
        for (const auto& x : off) stops |= x.rfind("stop:1", 0) == 0;
        std::printf("  RPB-15 debugger-window: poll_passes_batched=%llu, %zu snapshots, first differing %zu\n",
                    static_cast<unsigned long long>(rb.poll_batched), on.size(), bad);
        const bool window_ok = d.empty() && off == on && stops && rb.poll_batched > 0 && ra.poll_batched == 0;
        check_poll("RPB-15", "an idle ESP-01, and the debugger window open (a client with the live raster and call-stack tracker; mid-frame stops): identical per frame, engaged; the call stack and the raster walk identical after every frame and at every stop",
                   {esp}, Poll::Engaged, window_ok,
                   "debugger window: diff=" + d + " snapshots " + std::to_string(off.size()) + "/" + std::to_string(on.size()) +
                   " first=" + std::to_string(bad) + " stops=" + std::to_string(stops) +
                   " engaged=" + std::to_string(rb.poll_batched));
    }
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
