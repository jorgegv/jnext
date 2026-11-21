#include "platform/startup_stop.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "core/emulator.h"
#include "core/log.h"

using jnext::dbg::ClientId;
using jnext::dbg::PauseReason;

namespace {

enum class Format { Nex, Snapshot, Tape, Other };

Format format_of(const std::string& file) {
    const auto dot = file.rfind('.');
    if (dot == std::string::npos) return Format::Other;
    std::string ext = file.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".nex") return Format::Nex;
    if (ext == ".sna" || ext == ".z80" || ext == ".szx" || ext == ".jns" || ext == ".rzx")
        return Format::Snapshot;
    if (ext == ".tap" || ext == ".tzx" || ext == ".wav") return Format::Tape;
    return Format::Other;
}

std::string hex4(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof b, "$%04X", v & 0xFFFFu);
    return b;
}

}  // namespace

StartupStop::~StartupStop() {
    // The backend outlives this object (declared before it in every loop owner).
    if (dbg_ && cid_ != jnext::dbg::CLIENT_NONE) (void)dbg_->detach(cid_);
}

bool StartupStop::start(jnext::dbg::Debugger& dbg, Emulator& emu, const EmulatorConfig& config,
                        int programs_coming, bool has_window, std::string& why) {
    if (!config.debugger_start) return true;
    dbg_ = &dbg;
    emu_ = &emu;
    break_at_ = config.break_at;
    const auto id = dbg.attach(jnext::dbg::ClientInfo{"command line",
                                                       jnext::dbg::ClientKind::CommandLine});
    if (id.status != jnext::dbg::Result::Ok) {
        why = "--debugger: the debugger refused the command-line client";
        return false;
    }
    cid_ = id.value;
    landings_left_ = programs_coming;
    phase_ = Phase::Waiting;

    if (!has_window) {
        const char* const addr = config.debug_listen_address.c_str();
        if (config.dzrp_port >= 0)
            Log::platform()->info("debugger: waiting for a debugger on dzrp {}:{}", addr, config.dzrp_port);
        if (config.zrcp_port >= 0)
            Log::platform()->info("debugger: waiting for a debugger on zrcp {}:{}", addr, config.zrcp_port);
        if (config.gdb_port >= 0)
            Log::platform()->info("debugger: waiting for a debugger on gdb {}:{}", addr, config.gdb_port);
    }
    if (landings_left_ > 0) return true;
    if (!arm("power-on", why)) {
        finish();
        return false;
    }
    return true;
}

bool StartupStop::resolve_target(Target& t, std::string& why) const {
    // The Breakpoints panel's parser, in the panel's order: a number or a symbol (a name
    // keeps the memory it was assembled for), then a source line.
    if (const auto r = dbg_->symbols().resolve_key(break_at_)) {
        t.addr  = r->addr;
        t.space = r->space ? *r->space : jnext::dbg::MemSpace::cpu();
        return true;
    }
    const jnext::dbg::SourceResolution src = dbg_->source_map().resolve(break_at_);
    if (!src.starts.empty()) {
        t.source_line = true;
        return true;
    }
    why = "--break-at " + break_at_ + ": not an address, a symbol or a source line" +
          (src.error.empty() ? std::string() : " (" + src.error + ")");
    return false;
}

// Arm the stop that applies now: --break-at's run-to, else a pause with `default_why`.
bool StartupStop::arm(const std::string& default_why, std::string& why) {
    if (!break_at_.empty() && last_ok_) {
        Target t;
        if (!resolve_target(t, why)) return false;
        const jnext::dbg::Result r = t.source_line ? dbg_->run_to_source_line(cid_, break_at_)
                                                   : dbg_->run_to(cid_, t.addr, t.space);
        if (r != jnext::dbg::Result::Ok) {
            why = "--break-at " + break_at_ + ": the debugger refused the run-to";
            return false;
        }
        phase_ = Phase::Armed;
        return true;
    }
    (void)dbg_->pause(cid_);
    phase_ = Phase::Stopped;
    stop_cycle_ = dbg_->state().cycle;
    log_stop(default_why.c_str());
    return true;
}

bool StartupStop::program_landed(const std::string& file, bool ok, bool injected,
                                 std::string& why) {
    if (phase_ != Phase::Waiting || landings_left_ <= 0) return true;
    last_file_     = file;
    last_injected_ = injected;
    last_ok_       = ok;
    if (--landings_left_ > 0) return true;

    std::string default_why;
    if (!ok) {
        default_why = "the load failed";
    } else if (injected) {
        default_why = "the injected program";
    } else {
        switch (format_of(file)) {
        case Format::Nex:
            default_why = emu_->cpu_parked() ? "the load-only NEX file, the CPU is parked"
                                             : "the NEX entry";
            break;
        case Format::Snapshot: default_why = "the restored PC"; break;
        case Format::Tape:
            if (break_at_.empty()) {
                // The default tape stop is not built yet: say so rather than run on silently.
                Log::platform()->warn("debugger: no default stop for a tape yet; running "
                                      "(use --break-at)");
                finish();
                return true;
            }
            break;
        case Format::Other: default_why = "the loaded program"; break;
        }
    }
    if (!arm(default_why, why)) {
        finish();
        return false;
    }
    return true;
}

void StartupStop::log_stop(const char* what) {
    stop_why_ = what;
    Log::platform()->info("debugger: stopped at {} ({})", hex4(dbg_->state().pc), what);
}

void StartupStop::finish() {
    if (dbg_ && cid_ != jnext::dbg::CLIENT_NONE) (void)dbg_->detach(cid_);
    cid_   = jnext::dbg::CLIENT_NONE;
    phase_ = Phase::Done;
}

void StartupStop::poll() {
    if (!dbg_ || cid_ == jnext::dbg::CLIENT_NONE) return;
    const jnext::dbg::RunState st = dbg_->state();
    switch (phase_) {
    case Phase::Armed:
        if (!st.paused) break;
        if (st.pause_reason.by == cid_) {
            stop_cycle_ = st.cycle;
            log_stop(("--break-at " + break_at_).c_str());
            phase_ = Phase::Stopped;
        } else {
            finish();   // someone else paused first: the stop is cancelled
        }
        break;
    case Phase::Stopped:
        // The machine has run again (resumed, or stepped): the client's job is done. Its
        // detach never resumes anything: no pause is its any more.
        if (!st.paused || st.cycle != stop_cycle_) finish();
        break;
    default:
        break;
    }
}

void StartupStop::on_cold_boot() {
    if (phase_ == Phase::Waiting || phase_ == Phase::Armed) finish();
}
