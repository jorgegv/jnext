// GH #489 - the HALT batch with an ESP-01 attached (HBQ-60/63/64: ESP at boot, disabled and enabled
// by a cold boot during the run). Split out of halt_batch_observer_test, IDs unchanged: a live ESP
// needs a worker thread, which the WebAssembly build has not (the same split as
// uart_integration_native_test). Fixtures: halt_batch_observer.h.
//
// Oracle: the unbatched per-slot path of the same emulator (see halt_batch_observer_test).
//
// No ROM, no SD image: a bare Next with a program written straight into RAM.
#include "halt_batch_observer.h"

using namespace hbq;

int main() {
    std::printf("GH #489 HALT batch with an ESP-01 attached (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // HBQ-60: janko's scenario 1 - ESP enabled at boot (the production setup_esp(): the real
    // ThreadedEsp, the adapter, attach_device(0)). An idle ESP wants no tick, so it must not
    // refuse the batch (uart.h Uart::batch_quiescent()). The idle ESP is deterministic.
    {
        Spec s = plain("esp-at-boot", 60);
        s.config = [](EmulatorConfig& c) { c.esp_enabled = true; };
        auto alive = s.alive;
        s.alive = [=](Rig& r) { return alive(r) && r.emu->uart().device(0) != nullptr; };
        const Pair p = run_pair(s);
        check("HBQ-60", "ESP enabled at boot, EI;HALT at 28 MHz: identical per frame, the HALT batch engaged with the device attached",
              p.diff.empty() && p.on.run.halt_batched > 0, detail_of(p));
    }

    // HBQ-63: janko's scenario 2 - the ESP "disabled during the run": the machine keeps its device
    // until the next cold boot (Preferences cannot detach it), then the cold boot builds none.
    // One machine per run, 40 frames each side; the batch engaged on both.
    {
        Spec s = plain("esp-then-cold-boot-without", 80);
        s.config = [](EmulatorConfig& c) { c.esp_enabled = true; };
        s.before = [](Rig& r, int f) {
            if (f == 0) r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
            if (f != 40) return;
            EmulatorConfig off = r.emu->config();
            off.esp_enabled = false;
            emulator_cold_boot(*r.emu, off);
            r.emu->set_cycle_batching_for_test(r.batch, r.batch);
            install_plain(*r.emu);
            r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
        };
        const Pair p = run_pair(s);
        check("HBQ-63", "ESP enabled, then a cold boot without it during the run: identical per frame across the whole sequence, the HALT batch engaged in both halves, the device present then absent",
              p.diff.empty() && p.on.counter[39] > 0 && grew(p, 40, 79) &&
                  p.on.log.size() == 2 && p.on.log[0] == "device=1" && p.on.log[1] == "device=0",
              detail_of(p));
    }

    // HBQ-64: janko's scenario 3 - ESP enabled during the run after a start without it: a cold boot
    // with it (the only way a running machine gets one). Batch engaged in both halves.
    {
        Spec s = plain("no-esp-then-cold-boot-with", 80);
        s.before = [](Rig& r, int f) {
            if (f == 0) r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
            if (f != 40) return;
            EmulatorConfig on = r.emu->config();
            on.esp_enabled = true;
            emulator_cold_boot(*r.emu, on);
            r.emu->set_cycle_batching_for_test(r.batch, r.batch);
            install_plain(*r.emu);
            r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
        };
        const Pair p = run_pair(s);
        check("HBQ-64", "no ESP, then a cold boot with it during the run: identical per frame across the whole sequence, the HALT batch engaged in both halves, the device absent then present",
              p.diff.empty() && p.on.counter[39] > 0 && grew(p, 40, 79) &&
                  p.on.log.size() == 2 && p.on.log[0] == "device=0" && p.on.log[1] == "device=1",
              detail_of(p));
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
