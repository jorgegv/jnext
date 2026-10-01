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
        if (host.exits_) host.request(code);
    }
    void on_log(dbg::LogLevel, const std::string& text) override { host.capture(text); }
};

ScriptHost::ScriptHost() = default;

ScriptHost::~ScriptHost() {
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
    if (!engine_) return;
    const std::string tag = " [client " + std::to_string(engine_->client()) + "]";
    if (text.size() < tag.size() || text.compare(text.size() - tag.size(), tag.size(), tag) != 0) return;
    remember(text.substr(0, text.size() - tag.size()));
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

bool ScriptHost::ensure_engine() {
    if (engine_) return true;
    if (listener_cid_ == dbg::CLIENT_NONE) {
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
    }
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
    if (opt.scripts.empty()) {
        if (!opt.keys.empty()) {
            error("--script-key needs a --script to deliver the key to");
            return false;
        }
        return true;
    }
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

}  // namespace script
}  // namespace jnext
