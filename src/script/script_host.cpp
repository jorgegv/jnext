// jnext::script — the loop owners' script host. See script_host.h.

#include "script/script_host.h"

#include <fstream>
#include <sstream>

namespace jnext {
namespace script {

// The backend's `ExitRequested` push (a `Stop` under `StopPolicy::ExitNonZero`)
// and its log lines reach only a LISTENER, and a listener belongs to a client.
// This one is NON-ARMING (`ClientInfo::observer`): the engine's own attach
// already arms the machine, and this client must add nothing to that.
struct ScriptHost::HostListener : dbg::Listener {
    ScriptHost& host;
    explicit HostListener(ScriptHost& h) : host(h) {}
    void on_paused(const dbg::PausedInfo&) override {}
    void on_resumed(dbg::ClientId) override {}
    void on_reset(dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(dbg::EventKindMask) override {}
    void on_exit_requested(int code) override {
        // A script `exit` at this boundary is the engine's to hand over, at
        // the pause, once every stop of the boundary is known (it may turn an
        // `exit 0` into this 3): leave it to that.
        if (host.engine_ && host.engine_->exit_pending()) return;
        if (host.exits_) host.request(code);
    }
    void on_log(dbg::LogLevel, const std::string& text) override { host.capture(text); }
};

ScriptHost::ScriptHost() = default;

ScriptHost::~ScriptHost() {
    recorder_.reset();   // a recording in progress is written: ~Recorder stops it
    engine_.reset();
    if (dbg_ && listener_cid_ != dbg::CLIENT_NONE) {
        dbg_->set_listener(listener_cid_, nullptr);
        dbg_->detach(listener_cid_);
    }
}

void ScriptHost::request(int code) {
    if (requested_) return;  // the FIRST code wins (§6.3)
    requested_ = true;
    code_      = code;
}

std::size_t ScriptHost::unreached_verdicts() const {
    return engine_ ? engine_->unreached_verdicts() : 0;
}

// The engine's own lines carry the backend's ` [client N]` suffix for the
// engine's client; they are kept (without it) for the Script tab.
void ScriptHost::capture(const std::string& text) {
    for (const dbg::ClientId c : {engine_ ? engine_->client() : dbg::CLIENT_NONE,
                                  recorder_ ? recorder_->client() : dbg::CLIENT_NONE}) {
        if (c == dbg::CLIENT_NONE) continue;
        const std::string tag = " [client " + std::to_string(c) + "]";
        if (text.size() < tag.size() || text.compare(text.size() - tag.size(), tag.size(), tag) != 0) continue;
        remember(text.substr(0, text.size() - tag.size()));
        return;
    }
}

void ScriptHost::remember(const std::string& line) {
    log_.push_back(line);
    if (log_.size() > MAX_LOG_LINES) log_.pop_front();
    ++log_seq_;
}

std::vector<std::string> ScriptHost::log_since(uint64_t seq) const {
    std::vector<std::string> out;
    if (seq >= log_seq_) return out;
    const uint64_t first = log_seq_ - log_.size();  // the sequence number of log_.front()
    for (uint64_t k = seq < first ? first : seq; k < log_seq_; ++k)
        out.push_back(log_[static_cast<std::size_t>(k - first)]);
    return out;
}

void ScriptHost::error(const std::string& text) {
    if (dbg_) dbg_->log(dbg::CLIENT_NONE, dbg::LogLevel::Error, text);
    remember(text);
}

// The host's own non-arming listener: hears the backend's ExitRequested push
// and every log line (kept for the Script tab when the engine's or the
// recorder's).
bool ScriptHost::ensure_listener() {
    if (listener_cid_ != dbg::CLIENT_NONE) return true;
    dbg::ClientInfo ci;
    ci.name     = "script host";
    ci.kind     = dbg::ClientKind::Script;
    ci.observer = true;
    const auto c = dbg_->attach(ci);
    if (!c) {
        error("scripts: the script host could not attach to the debugger");
        return false;
    }
    listener_cid_ = c.value;
    listener_     = std::make_unique<HostListener>(*this);
    dbg_->set_listener(listener_cid_, listener_.get());
    return true;
}

bool ScriptHost::ensure_engine() {
    if (engine_) return true;
    if (!ensure_listener()) return false;
    EngineHost host;
    if (exits_) host.exit = [this](int code) { request(code); };
    engine_ = std::make_unique<ScriptEngine>(*dbg_, host);
    return true;
}

LoadResult ScriptHost::load_one(const std::string& file, const char* origin) {
    LoadResult r;
    r.file = file;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        r.errors.push_back(Diagnostic{SourcePos{}, "cannot be read"});
        error(std::string(origin) + " " + file + ": cannot be read");
        return r;
    }
    std::ostringstream text;
    text << in.rdbuf();
    r = engine_->load(text.str(), file);
    for (const Diagnostic& d : r.errors) error("SCRIPT ERROR " + file + ":" + d.to_string());
    if (!r.ok())
        error(std::string(origin) + " " + file + ": not loaded (" + std::to_string(r.errors.size()) + " error" +
              (r.errors.size() == 1 ? "" : "s") + ")");
    return r;
}

bool ScriptHost::start(dbg::Debugger& dbg, const ScriptHostOptions& opt) {
    dbg_   = &dbg;
    exits_ = opt.exits;
    // The backend's FIRST pump only adopts the machine's state as its baseline
    // and pushes nothing, so a stop in the loop's first tick (`on frame 0 do
    // exit 0`) would never reach `on_paused()`, where a script's `exit` is
    // handed over: the run would not exit at all. The loop owners call this
    // before their first tick; pumping here takes that baseline now.
    dbg.pump(dbg::PumpBudget{});

    if (!opt.map_file.empty()) {
        const auto n = dbg.load_map(opt.map_file, dbg::MapFormat::Z88dk);
        if (!n) {
            error("--map " + opt.map_file + ": cannot be loaded (" + dbg::result_name(n.status) + ")");
            return false;
        }
        if (n.value == 0) {
            error("--map " + opt.map_file + ": no symbols found (a z88dk .map is expected)");
            return false;
        }
        dbg.log(dbg::CLIENT_NONE, dbg::LogLevel::Info,
                "--map " + opt.map_file + ": " + std::to_string(n.value) + " symbols");
    }
    // `--script-key` reaches the recorder too (key 8 is its capture), so with
    // `--record-script` a key needs no script to be delivered to.
    if (opt.scripts.empty() && !opt.keys.empty() && opt.record_file.empty()) {
        error("--script-key needs a --script (or --record-script) to deliver the key to");
        return false;
    }
    if (!opt.record_file.empty() && !start_recording(opt.record_file)) return false;
    if (opt.scripts.empty() && opt.keys.empty()) return true;
    if (!ensure_engine()) return false;

    bool ok = true;
    for (const std::string& file : opt.scripts)
        if (load_one(file, "--script").ok()) files_.push_back(file);
        else ok = false;
    if (!ok) {
        engine_.reset();  // nothing of any script stays registered
        files_.clear();
        error("--script: not starting; no script is loaded (exit 1)");
        return false;
    }
    for (const auto& k : opt.keys) engine_->queue_host_key(k.first, k.second);
    return true;
}

LoadResult ScriptHost::load_file(const std::string& file) {
    if (!dbg_) {
        LoadResult r;
        r.file = file;
        r.errors.push_back(Diagnostic{SourcePos{}, "no debugger to load into"});
        return r;
    }
    if (!ensure_engine()) {
        LoadResult r;
        r.file = file;
        r.errors.push_back(Diagnostic{SourcePos{}, "the script host could not attach to the debugger"});
        return r;
    }
    const LoadResult r = load_one(file, "Load Script");
    if (r.ok()) {
        files_.push_back(file);
        // §2.4: FRAME is the machine's own frame number, not counted from the
        // load (Appendix K) — said where the user loaded it.
        const std::string note = "SCRIPT " + file + " loaded at FRAME " + std::to_string(dbg_->time().frame) +
                                 " (FRAME and `on frame N` count the machine's frames)";
        dbg_->log(dbg::CLIENT_NONE, dbg::LogLevel::Info, note);
        remember(note);
    }
    return r;
}

void ScriptHost::unload_all() {
    // The engine goes with its scripts: its client ARMS the machine, and a GUI
    // with nothing loaded must not keep every instruction paying for it.
    engine_.reset();
    if (!files_.empty()) {
        const std::string note = "SCRIPT: " + std::to_string(files_.size()) + " script(s) unloaded";
        if (dbg_) dbg_->log(dbg::CLIENT_NONE, dbg::LogLevel::Info, note);
        remember(note);
    }
    files_.clear();
}

std::vector<LoadResult> ScriptHost::reload() {
    const std::vector<std::string> files = files_;
    unload_all();
    std::vector<LoadResult> out;
    for (const std::string& f : files) out.push_back(load_file(f));
    return out;
}

// ---------------------------------------------------------------------------
// The recorder (WP6)
// ---------------------------------------------------------------------------

bool ScriptHost::start_recording(const std::string& file) {
    if (!dbg_) {
        remember("RECORD: no debugger to record from");
        return false;
    }
    if (!ensure_listener()) return false;
    if (!recorder_) recorder_ = std::make_unique<Recorder>(*dbg_);
    std::string why;
    if (!recorder_->start(file, recording_info_, why)) {
        error("RECORD " + file + ": " + why);
        return false;
    }
    return true;
}

bool ScriptHost::capture_screen() {
    return recorder_ && recorder_->capture();
}

bool ScriptHost::stop_recording() {
    if (!recorder_) {
        remember("RECORD: not recording");
        return false;
    }
    std::string why;
    if (!recorder_->stop(why)) {
        error("RECORD: " + why);
        return false;
    }
    remember("RECORD: wrote " + recorder_->path() + " — " + std::to_string(recorder_->edges()) +
             " input edges, " + std::to_string(recorder_->captures()) + " captures");
    return true;
}

}  // namespace script
}  // namespace jnext
