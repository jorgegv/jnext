// emulator_boot_test rows that need a thread (EB-18a..d).
//
// A live emulated ESP-01 runs its AT engine on a worker thread (ThreadedEsp), which
// the WebAssembly build does not have (and refuses --esp), so these rows moved, IDs,
// fixtures and assertions unchanged, out of emulator_boot_test into a suite that is
// `# os: native` in test/unit-tests.conf. Scaffolding (check, base_config) is repeated
// from emulator_boot_test.cpp.
//
// Run: ./build/test/emulator_boot_native_test

#include "platform/emulator_boot.h"
#include "core/saveable.h"
#include "peripheral/esp_host_policy.h"
#include "peripheral/uart.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

/// A cheap machine to boot: 48K is the smallest RAM and the fastest init, and
/// needs no SD image (same choice rewind_test makes).
EmulatorConfig base_config()
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    return cfg;
}

}  // namespace

int main()
{
    std::printf("emulator_boot_native_test (cold boot rows that need a thread)\n");

    // --- EB-18: a LIVE emulated ESP survives the cold boot ------------------
    //
    // GH #25. This is the one hazard the ESP's whole ownership design exists to
    // defeat, and its failure mode is the nastiest in the codebase: this helper
    // does `emu.~Emulator(); new (&emu) Emulator();` — placement-new at the SAME
    // address — so a worker thread that outlived the destructor would go on
    // servicing a core whose sink points into the NEWLY BOOTED machine. Nothing
    // crashes and no sanitiser fires; the fresh machine's UART just starts
    // receiving a dead machine's bytes.
    //
    // The defence is that `~ThreadedEsp()` joins, and that the ESP members are
    // declared so they are destroyed before `uart_`. Nothing about that is
    // visible at a call site, so it is exercised HERE, against the real helper,
    // with a worker that has actually been started and run.
    //
    // Identity is checked by the connection log's instance id, not its address:
    // the replacement routinely lands on the block the old one was freed from,
    // so a pointer compare would pass for the wrong reason.
    {
        EmulatorConfig cfg = base_config();
        cfg.esp_enabled = true;

        Emulator emu;
        emu.init(cfg);
        check("EB-18a", "the ESP is up and attached to UART 0 before the boot",
              emu.esp_enabled() && emu.uart().device(0) != nullptr &&
                  emu.esp_events() != nullptr);

        emu.run_frame();   // the worker is started and has serviced the device
        const std::uint64_t id_before =
            emu.esp_events() ? emu.esp_events()->instance_id() : 0;

        emulator_cold_boot(emu, cfg);   // ~Emulator() -> placement-new -> init()

        check("EB-18b", "the cold boot returns with a REBUILT ESP, not the old one",
              emu.esp_enabled() && emu.esp_events() != nullptr &&
                  emu.esp_events()->instance_id() > id_before,
              "before=" + std::to_string(id_before) + " after=" +
                  std::to_string(emu.esp_events() ? emu.esp_events()->instance_id() : 0));
        check("EB-18c", "...attached to UART 0 of the machine that now exists",
              emu.uart().device(0) != nullptr);
        check("EB-18d", "...and the rebuilt machine runs",
              (emu.run_frame(), true));
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
