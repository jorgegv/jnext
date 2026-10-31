#pragma once

// GH #388 (--coverage): the ONE place the three loop owners (HeadlessApp,
// SdlApp, QtApp) apply the flag, so the three sites are the same line. Called
// right after the debugger backend is hosted and BEFORE the first frame, so
// the very first instruction is recorded.

#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debugger.h"

#include <cstddef>
#include <cstdint>
#include <vector>

inline void apply_startup_coverage(jnext::dbg::Debugger& debugger, const EmulatorConfig& config) {
    if (!config.coverage) return;
    debugger.mem_coverage_enable(true);
    Log::platform()->info("Memory coverage recording: on (--coverage)");
}

/// GH #388: the one-line summary the loop owners log at exit while coverage is
/// recording, so `--coverage` has a result in the frontends that have no viewer
/// (SDL, --headless) and a run can be told from one that recorded nothing.
inline void log_coverage_summary(const jnext::dbg::Debugger& debugger) {
    if (!debugger.mem_coverage_enabled()) return;
    size_t x = 0, r = 0, w = 0;
    const auto spaces = debugger.mem_coverage_spaces();
    std::vector<uint8_t> map;
    for (const jnext::dbg::MemSpace& sp : spaces) {
        const size_t n = jnext::dbg::space_size(sp.kind);
        map.assign(n, 0);
        debugger.mem_coverage(sp, 0, n, map.data());
        for (uint8_t b : map) {
            x += (b & jnext::dbg::COVERAGE_EXEC) != 0;
            r += (b & jnext::dbg::COVERAGE_READ) != 0;
            w += (b & jnext::dbg::COVERAGE_WRITE) != 0;
        }
    }
    Log::platform()->info("Memory coverage at exit: {} space(s), {} byte(s) executed, {} read, {} written",
                          spaces.size(), x, r, w);
}
