#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the script ENGINE: loaded `.jds` scripts as backend
// subscriptions (dsl-frontend.md §2.2, §2.6, §2.7, §6.3, §6.5; "WP3 as built",
// Appendix I).
//
// ONE ENGINE = ONE BACKEND CLIENT (`ClientKind::Script`). `load()` parses,
// checks (symbols from the backend's table), evaluates the `var` initializers
// and the filter bounds, and registers every rule as backend subscription(s)
// whose `Condition` is the compiled `when` and whose `Handler` runs the body.
// So a non-matching hit never reaches a rule body (§5.2), and the body runs at
// the delivery point, machine stopped, under the backend's InspectionScope.
//
// WHAT RUNS WHERE
//   * event rules      — in the backend's delivery (the Handler);
//   * `on stop` rules  — in `on_paused()`, i.e. from the loop owner's `pump()`,
//                        machine paused, outside `run_frame()`: a stop is not a
//                        backend event, so the engine is a Listener for it;
//   * deferred actions — `joystick` and `compare_scr` at the NEXT FRAME EDGE
//                        through the engine's own `Frame` subscription (made on
//                        first need), or at once when issued from a `frame`
//                        rule, which already runs at the edge (§2.6's edge
//                        rule). `press`/`release` go straight to the backend's
//                        IN-01/IN-02, which queue for the edge themselves;
//                        `screenshot` likewise (CAP-01);
//   * `save_snapshot`  — from `on_frame_ended()`, i.e. the next `pump()` that
//                        finds the machine at a frame boundary: the backend
//                        refuses a save inside a delivery that would have to
//                        run the frame out.
//
// VERDICTS. A rule body returns `Stop` when it ran `stop`, a failed `assert` or
// `exit`; the backend then pauses at the boundary and applies the loop owner's
// SES-04 policy (headless: exit 3). `exit n` first calls `EngineHost::exit(n)`
// — the loop owner keeps the FIRST code it is given, so `exit 0` is not
// overridden by the stop's 3.
//
// RUN-TIME ERRORS (§6.5) disable the rule, are logged with file:line:column,
// and — at the next frame edge — call `EngineHost::exit(1)`.
//
// NOTHING HERE PERTURBS THE MACHINE EXCEPT THE EXPLICIT VERBS (§2.7): `set` on a
// machine lvalue and `out`, through the backend's debugger write paths (logged
// by the backend, no events), plus the input/capture actions.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "debug/debugger.h"
#include "script/ast.h"
#include "script/diagnostic.h"
#include "script/state.h"

namespace jnext {
namespace script {

/// What the loop owner provides (WP4 wires it; tests pass their own).
struct EngineHost {
    /// `exit n`, and 1 after a run-time error (§6.3/§6.5). Empty = the GUI: a
    /// script never exits the process, the `Stop` verdict pauses instead.
    std::function<void(int code)> exit;
};

/// What `load()` reports. Diagnostics are positioned in the loaded text; the
/// file name is in `file`.
struct LoadResult {
    std::string             file;
    std::vector<Diagnostic> errors;    ///< non-empty = nothing was registered
    std::vector<Diagnostic> warnings;  ///< loaded anyway (e.g. the §3(a) PAGE warning)
    bool ok() const { return errors.empty(); }
};

class ScriptEngine : public dbg::Listener {
public:
    /// Most subscriptions one `on execute page P1..P2` rule may become
    /// (Appendix I: one per page, the Execute filter taking one page).
    static constexpr int MAX_EXECUTE_PAGES = 16;
    /// `dump_mem`'s bound (§2.6).
    static constexpr int MAX_DUMP_MEM = 4096;

    ScriptEngine(dbg::Debugger& dbg, EngineHost host = {});
    ~ScriptEngine() override;

    ScriptEngine(const ScriptEngine&)            = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    /// Load one script. On any error nothing of it is registered (§6.5:
    /// "nothing runs partially"). Scripts load in `--script` order; each has
    /// its own variables, labels and snapshot stacks.
    LoadResult load(const std::string& text, const std::string& file);

    /// Remove every loaded script's subscriptions and state.
    void unload_all();

    dbg::ClientId client() const { return cid_; }

    /// One rule, as the GUI's Script tab (WP5) and the rows see it.
    struct RuleView {
        std::string file;
        std::string label;
        SourcePos   pos;
        EventType   type = EventType::Execute;
        std::vector<dbg::EventId> subs;  ///< empty for `on stop` rules
        bool     enabled = true;
        bool     dead    = false;        ///< disabled by a run-time error
        uint64_t hits    = 0;
        bool     spent   = false;        ///< a `once` rule that has fired
        bool     verdict = false;        ///< its body holds an `exit` / `compare_scr`
        std::string event;               ///< the event, filter resolved: `write 9000..9001`
    };
    std::vector<RuleView> rules() const;

    /// What a run has reached so far (WP5: the Script tab's verdict line).
    struct Status {
        std::optional<int> exit_code;    ///< the first `exit n` a rule ran
        std::size_t stops = 0;           ///< `stop`s, failed `assert`s and `compare_scr`s
        std::string last_stop;           ///< the reason of the latest
        std::size_t runtime_errors = 0;
        std::size_t unreached = 0;       ///< `unreached_verdicts()`
    };
    Status status() const;

    /// The interpreter state of the `index`-th loaded script (rows).
    ScriptState* state(size_t index);

    /// Run-time errors reported so far.
    size_t runtime_errors() const { return runtime_errors_; }

    /// `--script-key FRAME N` (§6.6): raise host key N (`scriptN`) at the
    /// edge of frame FRAME — E_FRAME, where `on frame FRAME` fires — so a
    /// `hostkey N` rule runs with `FRAME == FRAME`. A frame already past is
    /// delivered at the next edge.
    void queue_host_key(uint32_t frame, int key);

    /// The verdicts a run never reached (§7.3): rules whose body holds an
    /// `exit` or a `compare_scr` and never fired, plus deferred actions and
    /// scheduled host keys still pending. The headless watchdog
    /// (`--delayed-automatic-exit*`) turns a non-zero count into exit 3.
    size_t unreached_verdicts() const;

    // ── dbg::Listener ───────────────────────────────────────────────────────
    void on_paused(const dbg::PausedInfo& info) override;
    void on_resumed(dbg::ClientId) override {}
    void on_reset(dbg::ResetKind) override {}
    void on_frame_ended(uint32_t frame) override;
    void on_subscriptions_changed(dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(dbg::LogLevel, const std::string&) override {}

private:
    struct Unit;
    struct RuleRec;
    struct Deferred;
    struct PendingSave {
        RuleRec*    rule = nullptr;
        std::string file;
    };

    dbg::Action run_rule(RuleRec& r, const dbg::Event& ev, dbg::Debugger& dbg);
    void exec(RuleRec& r, const std::vector<Action>& body, const dbg::Event& ev,
              dbg::Debugger& dbg, dbg::Action& verdict);
    void runtime_error(RuleRec& r, const Diagnostic& d);
    std::vector<dbg::Subscription> subscriptions_for(Unit& u, RuleRec& r,
                                                     std::vector<Diagnostic>& errors,
                                                     std::vector<Diagnostic>& warnings);
    void set_rule_enabled(RuleRec& r, bool on);
    RuleRec* find_label(Unit& u, const std::string& label);
    void ensure_edge();
    dbg::Action run_edge(uint32_t frame);
    dbg::Action compare_scr_now(RuleRec* r, const std::string& file, const std::string& msg);
    void log(dbg::LogLevel level, const std::string& text);
    std::string stamp() const;
    std::string pause_reason_text(const dbg::PausedInfo& info) const;
    void account_static_stops(const dbg::PausedInfo& info);

    dbg::Debugger& dbg_;
    EngineHost     host_;
    dbg::ClientId  cid_ = dbg::CLIENT_NONE;
    std::vector<std::unique_ptr<Unit>> units_;
    std::vector<Deferred> deferred_;
    std::vector<PendingSave> saves_;    ///< `save_snapshot`s for the next boundary
    std::vector<std::pair<uint32_t, int>> host_keys_;  ///< (frame, key) still to raise
    std::optional<uint64_t> cur_cycle_; ///< the running delivery's CYCLE
    dbg::EventId   edge_ = dbg::EVENT_NONE;
    bool           error_exit_pending_ = false;
    bool           rewind_warned_ = false;
    uint64_t       overflow_logged_cycle_ = UINT64_MAX;
    size_t         runtime_errors_ = 0;
    std::string    stop_reason_;       ///< the reason of the engine's own pending stop
    bool           body_stopped_ = false;  ///< a `stop` / failed `assert` ran in the body running now
    std::optional<int> first_exit_;    ///< status(): the first `exit n`
    std::size_t    stops_ = 0;         ///< status(): stop verdicts so far
    std::string    last_stop_;         ///< status(): the latest stop's reason
    std::string    last_stop_reason_;  ///< `REASON` for the `on stop` rules running now
    bool           in_frame_delivery_ = false;
};

}  // namespace script
}  // namespace jnext
