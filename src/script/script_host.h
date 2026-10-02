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
//
// THE GUI (WP5, Appendix K). The Qt Script tab loads, unloads and reloads
// scripts through `load_file()` / `unload_all()` / `reload()`, and shows the
// engine's log lines (`log_since()`), its rules and `ScriptEngine::status()`.
//
// THE RECORDER (WP6, #20 — Appendix L). `start_recording()` attaches one
// `Recorder` (its own backend client) that writes the session as a replay
// script; `--record-script` starts it before the first frame, Script > Record
// Script… at any time. It is stopped — and the script written — by
// `stop_recording()` or when the host goes.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debug/debugger.h"
#include "script/recorder.h"
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
    /// `--record-script FILE` (WP6): record from the first frame, write FILE
    /// when the host goes (or at a Stop Recording). Empty = not recording.
    std::string record_file;
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
    const ScriptEngine* engine() const { return engine_.get(); }

    // ── the GUI (WP5) ────────────────────────────────────────────────────────

    /// Load one more script at run time (Script > Load Script…). Registered at
    /// once; a script with any error registers nothing and the others stay.
    /// Needs a prior `start()`, which names the backend.
    LoadResult load_file(const std::string& file);
    /// Unload every script (and the engine with them: its client arms the
    /// machine).
    void unload_all();
    /// Unload, then load the same files again in the same order.
    std::vector<LoadResult> reload();
    /// The files loaded now, in load order.
    const std::vector<std::string>& files() const { return files_; }

    /// The script log: the engine's lines (without the backend's client tag)
    /// and the host's own errors, the last MAX_LOG_LINES of them. `seq` is a
    /// count of lines ever logged; `log_since(n)` returns those after the n-th
    /// still held.
    static constexpr std::size_t MAX_LOG_LINES = 2000;
    uint64_t log_seq() const { return log_seq_; }
    std::vector<std::string> log_since(uint64_t seq) const;

    // ── the recorder (WP6, #20) ─────────────────────────────────────────────

    /// What the recorder's header says about the session — the program, the
    /// RTC, the SD card. Asked at the start of a recording and at every cold
    /// boot during one. The loop owner sets it; unset, the header says none.
    void set_recording_info(std::function<RecordingInfo()> f) { recording_info_ = std::move(f); }
    /// Start recording into `file` (Script > Record Script…, `--record-script`).
    /// False — logged — when already recording, with no backend, or refused.
    bool start_recording(const std::string& file);
    /// Ask for a capture at the next frame edge (Script > Capture Screen,
    /// Alt+8). False when not recording.
    bool capture_screen();
    /// Stop and write the script. False — logged — when not recording or the
    /// file cannot be written.
    bool stop_recording();
    bool recording() const { return recorder_ && recorder_->recording(); }
    /// The recorder, while one has been made (it outlives a stop, for the
    /// Script tab's last-recording line).
    const Recorder* recorder() const { return recorder_.get(); }

private:
    struct HostListener;
    void request(int code);
    void capture(const std::string& text);
    void remember(const std::string& line);
    void error(const std::string& text);
    bool ensure_listener();
    bool ensure_engine();
    LoadResult load_one(const std::string& file, const char* origin);

    dbg::Debugger*                 dbg_ = nullptr;
    std::unique_ptr<ScriptEngine>  engine_;
    std::unique_ptr<HostListener>  listener_;
    dbg::ClientId                  listener_cid_ = dbg::CLIENT_NONE;
    bool                           exits_     = true;
    bool                           requested_ = false;
    int                            code_      = 0;
    std::vector<std::string>       files_;
    std::deque<std::string>        log_;
    uint64_t                       log_seq_ = 0;
    std::unique_ptr<Recorder>      recorder_;
    std::function<RecordingInfo()> recording_info_;
};

}  // namespace script
}  // namespace jnext
