// Log change-gate test (Task 24, 2026-07-12).
//
// Guest state changes must log ONLY when the value actually changes. See the
// level policy in src/core/log.h.
//
// The bug that started Task 24: NR 0x07 (CPU speed) logged on EVERY write, at
// info. Programs rewrite it constantly — some demos change speed every frame —
// so the console flooded. Demoting it to debug is only half the fix: ungated, it
// would still flood anyone running `--log-level emulator=debug`, which is
// exactly the person who needs to read the log.
//
// This drives a real Emulator through the real port path (OUT 0x243B / 0x253B)
// and counts the log lines the NR 0x07 write handler actually emits, by hanging
// a ringbuffer sink off the emulator logger.
//
// It lives in its own binary because nextreg_integration_test — the natural home
// — has a local `fmt()` helper that collides with spdlog's `fmt` namespace.
//
// Run: ./build/test/log_gate_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

/// Write a NextREG through the real port path, exactly as Z80 code would.
void nr_write(Emulator& emu, uint8_t reg, uint8_t val)
{
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

}  // namespace

int main()
{
    std::printf("Log change-gate (Task 24) — guest state changes log only on CHANGE\n");
    std::printf("====================================================\n\n");

    Emulator emu;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);

    // Capture what the emulator logger emits, at debug (where the policy puts
    // guest-driven state changes).
    auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    auto logger = Log::emulator();
    logger->sinks().push_back(ring);
    logger->set_level(spdlog::level::debug);

    auto speed_lines = [&]() {
        int n = 0;
        for (const auto& line : ring->last_formatted())
            if (line.find("CPU speed changed") != std::string::npos) ++n;
        return n;
    };

    char detail[192];

    // --- GATE-01: rewriting the SAME speed logs once, not once per write -------
    nr_write(emu, 0x07, 0x03);                                // 3.5 -> 28 MHz
    const int after_change = speed_lines();
    for (int i = 0; i < 20; i++) nr_write(emu, 0x07, 0x03);   // same value, x20
    const int after_repeats = speed_lines();
    std::snprintf(detail, sizeof(detail),
                  "lines after the change=%d (want 1), after 20 identical rewrites=%d "
                  "(want 1; ungated gives 21)", after_change, after_repeats);
    check("GATE-01", "20 rewrites of the SAME CPU speed log only once",
          after_change == 1 && after_repeats == 1, detail);

    // --- GATE-02: a genuine change still logs ----------------------------------
    nr_write(emu, 0x07, 0x01);                                // 28 -> 7 MHz
    std::snprintf(detail, sizeof(detail), "lines=%d want 2", speed_lines());
    check("GATE-02", "an actual change of CPU speed does log", speed_lines() == 2, detail);

    // --- GATE-03: the gate clears on reset -------------------------------------
    // NR 0x07 returns to its power-on 3.5 MHz across a reset, so a write back to
    // the pre-reset speed is a REAL change and must log. If the gate is not
    // cleared, that line is swallowed as "unchanged".
    nr_write(emu, 0x07, 0x03);                                // 7 -> 28 MHz  (3 lines)
    const int before_reset = speed_lines();
    emu.reset();
    nr_write(emu, 0x07, 0x03);                                // 3.5 -> 28 again
    std::snprintf(detail, sizeof(detail),
                  "lines before reset=%d, after the post-reset write=%d "
                  "(want %d; a swallowed write leaves it at %d)",
                  before_reset, speed_lines(), before_reset + 1, before_reset);
    check("GATE-03", "the gate clears on reset, so the first post-reset write logs",
          before_reset == 3 && speed_lines() == 4, detail);

    logger->sinks().pop_back();

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
