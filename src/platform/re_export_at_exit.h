#pragma once

// GH #389 (--export-skoolkit, --export-ghidra): the ONE place the three loop
// owners (HeadlessApp, SdlApp, QtApp) write the reverse-engineering exports at
// exit, so the three sites are the same line (the #388 pattern of
// coverage_startup.h). Called beside `log_coverage_summary()`, while the debugger
// backend and the machine are still alive.

#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debugger.h"

#include <string>

namespace re_export_detail {

/// Run one export; log what it did. False when it failed.
template <class Fn>
bool run_export(const char* flag, const std::string& dir, Fn export_fn) {
    if (dir.empty()) return true;
    jnext::dbg::ReReport rep;
    std::string why;
    const jnext::dbg::Result r = export_fn(dir, &rep, &why);
    if (r != jnext::dbg::Result::Ok) {
        Log::platform()->error("{} {}: {}", flag, dir,
                               why.empty() ? jnext::dbg::result_name(r) : why.c_str());
        return false;
    }
    Log::platform()->info("{} {}: {} files, {} labels, {} comments", flag, dir, rep.files,
                          rep.labels, rep.comments);
    for (const std::string& n : rep.notes) Log::platform()->warn("{} {}: {}", flag, dir, n);
    return true;
}

}  // namespace re_export_detail

/// Write the exports `config` asks for. False when any of them failed; the loop
/// owner then exits non-zero.
inline bool write_re_exports_at_exit(const jnext::dbg::Debugger& debugger, const EmulatorConfig& config) {
    bool ok = re_export_detail::run_export("--export-skoolkit", config.export_skoolkit_dir,
        [&](const std::string& d, jnext::dbg::ReReport* r, std::string* w) { return debugger.export_skoolkit(d, r, w); });
    ok = re_export_detail::run_export("--export-ghidra", config.export_ghidra_dir,
        [&](const std::string& d, jnext::dbg::ReReport* r, std::string* w) { return debugger.export_ghidra(d, r, w); }) && ok;
    return ok;
}
