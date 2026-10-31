#pragma once

// GH #388 (--coverage): the ONE place the three loop owners (HeadlessApp,
// SdlApp, QtApp) apply the flag, so the three sites are the same line. Called
// right after the debugger backend is hosted and BEFORE the first frame, so
// the very first instruction is recorded.

#include "core/emulator_config.h"
#include "debug/debugger.h"

inline void apply_startup_coverage(jnext::dbg::Debugger& debugger, const EmulatorConfig& config) {
    if (config.coverage) debugger.mem_coverage_enable(true);
}
