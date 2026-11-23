#pragma once

// GH #492 (--bug-report): the ONE place the three loop owners (HeadlessApp, SdlApp, QtApp)
// write the bug report at exit, so the three sites are the same line (the pattern of
// re_export_at_exit.h). Called beside write_re_exports_at_exit(), while the debugger
// backend and the machine are still alive.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/output_files.h"
#include "debug/debugger.h"
#include "platform/bug_report.h"

#include <SDL3/SDL.h>

#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace bug_report {

/// Local time now, as a broken-down time.
inline std::tm local_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

}  // namespace bug_report

/// "SDL 3.x.y": the SDL the process runs against (the toolkit string of a non-Qt frontend).
inline std::string sdl_toolkit_version() {
    const int v = SDL_GetVersion();
    return "SDL " + std::to_string(SDL_VERSIONNUM_MAJOR(v)) + "." + std::to_string(SDL_VERSIONNUM_MINOR(v)) +
           "." + std::to_string(SDL_VERSIONNUM_MICRO(v));
}

/// Write the report `config.bug_report_path` asks for, if any. False when it failed; the
/// loop owner then exits non-zero. `frontend` is "qt", "sdl" or "headless"; `toolkit` is
/// the Qt or SDL version string. `config_file`: the settings file as the frontend read it (the Qt
/// frontend), or null for a frontend that reads none.
inline bool write_bug_report_at_exit(Emulator& emu, const jnext::dbg::Debugger& debugger,
                                     const EmulatorConfig& config, const char* frontend,
                                     const std::string& toolkit,
                                     const std::vector<std::pair<std::string, std::string>>* config_file = nullptr) {
    if (config.bug_report_path.empty()) return true;
    std::string path, why;
    if (!bug_report::resolve_path(config.bug_report_path, bug_report::program_name(emu),
                                  bug_report::local_now(), path, why)) {
        Log::platform()->error("--bug-report {}: {}", config.bug_report_path, why);
        return false;
    }
    bug_report::Inputs in;
    in.argv = config.command_line;
    in.frontend = frontend;
    in.toolkit = toolkit;
    if (config_file) { in.config_file = *config_file; in.config_file_read = true; }
    const bug_report::Outcome o = bug_report::write(emu, debugger, in, path);
    if (!o.ok) {
        Log::platform()->error("--bug-report {}: {}", config.bug_report_path, o.why);
        return false;
    }
    output_files::written(path);
    Log::platform()->info("bug report saved: {} (memory: {})", path,
                          o.memory_included ? "included" : "not included: " + o.memory_reason);
    for (const std::string& w : o.warnings) Log::platform()->warn("bug report: {}", w);
    return true;
}
