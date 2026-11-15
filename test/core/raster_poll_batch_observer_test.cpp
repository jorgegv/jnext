// GH #496 - the raster-poll batch with an ESP-01 or a debugger client attached: bit-identical
// equivalence (batch off vs on, per frame) and the engagement counter, on synthetic fixtures. The
// debugger-window and ESP rows of halt_batch_observer_test, for the poll batch (the shared gates G2
// and G10 are the ones GH #489 relaxed). See raster_poll_fixtures.h for the method.
// RPB-15 (an idle ESP-01 and the debugger window) lives in raster_poll_batch_observer_native_test:
// a live ESP needs a worker thread, which the WebAssembly build has not.
#include "raster_poll_fixtures.h"

int main() {
    std::printf("GH #496 raster-poll batch with a debugger client attached (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

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
