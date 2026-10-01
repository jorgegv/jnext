#pragma once

// ---------------------------------------------------------------------------
// jnext::script — what a LOOP OWNER hosts to run `--script` files (GH #26 WP4,
// dsl-frontend.md §6.3, §6.5, §6.6; "WP4 as built", Appendix J).
//
// Every loop owner (HeadlessApp, SdlApp, QtApp) builds one hosted Debugger and
// pumps it; this is the scripting half of that, the way `DebugServers` is the
// socket half. `start()` loads `--map` into the backend's one symbol table,
// then each `--script` file in the order given, and schedules `--script-key`.
// A file that cannot be read, a MAP that cannot be loaded, or any load-time
// error in any script (§6.5) is reported — `file:line:column: message` — and
// `start()` returns false: the loop owner exits 1 BEFORE the machine runs.
//
// THE EXIT CODE (§6.3). The loop owner asks `exit_requested()` after each pump
// and exits with `exit_code()`. The FIRST code wins:
//   * `exit n` in a script: n (the engine hands it over during the delivery,
//     before the backend's stop asks for 3);
//   * a `stop` or a failed `assert` under `StopPolicy::ExitNonZero`: 3 (the
//     backend's request, heard by this host's own non-arming listener);
//   * a run-time error: 1, at the next frame edge.
// With `exits = false` (the Qt GUI) nothing is ever requested: a script `exit`
// logs and pauses, a GUI never exits from a script.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debug/debugger.h"
#include "script/script_engine.h"

namespace jnext {
namespace script {

struct ScriptHostOptions {
    std::string                           map_file;  ///< `--map`, empty = none
    std::vector<std::string>              scripts;   ///< `--script`, in order
    std::vector<std::pair<uint32_t, int>> keys;      ///< `--script-key FRAME N`
    /// True for headless and SDL (a script may end the process); false for
    /// the Qt GUI.
    bool exits = true;
};

class ScriptHost {
public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&)            = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    /// Load everything `opt` names. False — every reason already logged — on
    /// any failure; nothing is then left registered. With nothing to load it
    /// attaches nothing and returns true: a run without scripts is the run it
    /// always was.
    bool start(dbg::Debugger& dbg, const ScriptHostOptions& opt);

    /// Whether scripts are running (so the engine exists).
    bool active() const { return engine_ != nullptr; }

    bool exit_requested() const { return requested_; }
    int  exit_code() const { return code_; }

    /// At the `--delayed-automatic-exit*` bound: how many verdicts the run
    /// never reached (`ScriptEngine::unreached_verdicts()`); 0 with no scripts.
    std::size_t unreached_verdicts() const;

    ScriptEngine* engine() { return engine_.get(); }

private:
    struct ExitListener;
    void request(int code);

    dbg::Debugger*                 dbg_ = nullptr;
    std::unique_ptr<ScriptEngine>  engine_;
    std::unique_ptr<ExitListener>  listener_;
    dbg::ClientId                  listener_cid_ = dbg::CLIENT_NONE;
    bool                           requested_ = false;
    int                            code_      = 0;
};

}  // namespace script
}  // namespace jnext
