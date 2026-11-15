// GH #496 - the raster-poll batch with an ESP-01 or a debugger client attached: bit-identical
// equivalence (batch off vs on, per frame) and the engagement counter, on synthetic fixtures. The
// debugger-window and ESP rows of halt_batch_observer_test, for the poll batch (the shared gates G2
// and G10 are the ones GH #489 relaxed). See raster_poll_fixtures.h for the method.
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

} // namespace

int main() {
    std::printf("GH #496 raster-poll batch with an ESP-01 / debugger client attached (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // RPB-15: an ESP-01 enabled and idle (it wants no tick, so G10 lets the batch through), and then the
    // debugger window open on top of it (a client with the live raster and the call-stack tracker: G2
    // does not refuse, the raster walk is advanced over the window).
    {
        Scenario esp = poll_fx("esp-idle", 3, 0xFF, guide_loop());
        esp.config = [](EmulatorConfig& c) { c.esp_enabled = true; };
        auto w = std::make_shared<Window>();
        Scenario win = with_window(poll_fx("debugger-window", 3, 0xFF, preselected_loop(0xB4, true)), w);
        check_poll("RPB-15", "an idle ESP-01, and the debugger window open (a client with the live raster and call-stack tracker): identical per frame, engaged",
                   {esp, win});
    }

    // RPB-27: a persistent breakpoint set elsewhere refuses the batch (G2); one INSIDE the loop stops the
    // machine on the same cycle with the batch on (a resume per frame, so one resume is one stop).
    {
        Scenario elsewhere = poll_fx("breakpoint-elsewhere", 3, 0xFF, guide_loop(), 30, 0xC000, [](Emulator& e) {
            e.debug_state().set_persistent_breakpoints(true);
            e.debug_state().breakpoints().add_pc(0xC800);
        });
        Scenario inside = poll_fx("breakpoint-inside", 3, 0xFF, guide_loop(), 30, 0xC000, [](Emulator& e) {
            e.debug_state().set_persistent_breakpoints(true);
            e.debug_state().breakpoints().add_pc(0xC00D);   // the JR Z of the first wait
        });
        inside.before_frame = [](Emulator& e, int) { e.debug_state().resume(); };
        inside.alive = [](Emulator& e) { return e.clock().get() > 0; };
        elsewhere.alive = [](Emulator& e) { return e.mmu().peek(kCount) >= 5; };
        check_poll("RPB-27", "a breakpoint elsewhere and one inside the loop (a resume per frame): identical per frame, REFUSED (G2)",
                   {elsewhere, inside}, Poll::Refused);
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
