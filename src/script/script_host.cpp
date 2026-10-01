// jnext::script — the loop owners' script host. See script_host.h.

#include "script/script_host.h"

#include <fstream>
#include <sstream>

namespace jnext {
namespace script {

// The backend's `ExitRequested` push (a `Stop` under `StopPolicy::ExitNonZero`)
// reaches only a LISTENER, and a listener belongs to a client. This one is
// NON-ARMING (`ClientInfo::observer`): the engine's own attach already arms the
// machine, and this client must add nothing to that.
struct ScriptHost::ExitListener : dbg::Listener {
    ScriptHost& host;
    explicit ExitListener(ScriptHost& h) : host(h) {}
    void on_paused(const dbg::PausedInfo&) override {}
    void on_resumed(dbg::ClientId) override {}
    void on_reset(dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(dbg::EventKindMask) override {}
    void on_exit_requested(int code) override { host.request(code); }
    void on_log(dbg::LogLevel, const std::string&) override {}
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

bool ScriptHost::start(dbg::Debugger& dbg, const ScriptHostOptions& opt) {
    dbg_ = &dbg;
    auto error = [&](const std::string& text) { dbg.log(dbg::CLIENT_NONE, dbg::LogLevel::Error, text); };

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

    if (opt.exits) {
        dbg::ClientInfo ci;
        ci.name     = "script host";
        ci.kind     = dbg::ClientKind::Script;
        ci.observer = true;
        const auto c = dbg.attach(ci);
        if (!c) {
            error("scripts: the script host could not attach to the debugger");
            return false;
        }
        listener_cid_ = c.value;
        listener_     = std::make_unique<ExitListener>(*this);
        dbg.set_listener(listener_cid_, listener_.get());
    }

    EngineHost host;
    if (opt.exits) host.exit = [this](int code) { request(code); };
    engine_ = std::make_unique<ScriptEngine>(dbg, host);

    bool ok = true;
    for (const std::string& file : opt.scripts) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            error("--script " + file + ": cannot be read");
            ok = false;
            continue;
        }
        std::ostringstream text;
        text << in.rdbuf();
        const LoadResult r = engine_->load(text.str(), file);
        for (const Diagnostic& d : r.errors) error("SCRIPT ERROR " + file + ":" + d.to_string());
        if (!r.ok()) {
            error("--script " + file + ": not loaded (" + std::to_string(r.errors.size()) + " error" +
                  (r.errors.size() == 1 ? "" : "s") + ")");
            ok = false;
        }
    }
    if (!ok) {
        engine_.reset();  // nothing of any script stays registered
        return false;
    }
    for (const auto& k : opt.keys) engine_->queue_host_key(k.first, k.second);
    return true;
}

}  // namespace script
}  // namespace jnext
