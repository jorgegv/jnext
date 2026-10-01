#include "platform/debug_servers.h"

#include "core/log.h"
#include "remote/dzrp/dzrp_server.h"
#include "remote/gdb/rsp_server.h"

using jnext::dbg::PumpBudget;
using jnext::dbg::ServiceHint;

DebugServers::DebugServers() = default;

// Out of line: the servers are only forward-declared in the header.
DebugServers::~DebugServers() = default;

bool DebugServers::start(jnext::dbg::Debugger& dbg, const EmulatorConfig& cfg) {
    if (cfg.dzrp_port >= 0) {
        auto s = std::make_unique<jnext::remote::dzrp::DzrpServer>(dbg);
        // `open()` logs `dzrp: listening on <addr>:<port>` — the BOUND port,
        // which is how a `--dzrp-port 0` run tells its user (and a regression
        // row) where it is — or `dzrp: cannot listen on …` with the reason.
        if (!s->server().open(cfg.debug_listen_address,
                              static_cast<std::uint16_t>(cfg.dzrp_port))) {
            Log::platform()->error("--dzrp-port {}: the DZRP server cannot listen on {} ({}) — "
                                   "exiting",
                                   cfg.dzrp_port, cfg.debug_listen_address,
                                   s->server().last_error());
            return false;
        }
        dbg.add_service(s->server());
        dzrp_ = std::move(s);
    }
    if (cfg.gdb_port >= 0) {
        auto s = std::make_unique<jnext::remote::gdb::GdbServer>(dbg);
        // `gdb: listening on <addr>:<port>` — the line a `--gdb-port 0` user
        // (and the regression rows) read the bound port from.
        if (!s->server().open(cfg.debug_listen_address,
                              static_cast<std::uint16_t>(cfg.gdb_port))) {
            Log::platform()->error("--gdb-port {}: the GDB RSP server cannot listen on {} ({}) — "
                                   "exiting",
                                   cfg.gdb_port, cfg.debug_listen_address,
                                   s->server().last_error());
            return false;
        }
        dbg.add_service(s->server());
        gdb_ = std::move(s);
    }
    return true;
}

PumpBudget DebugServers::frame_loop_budget(bool paused, const ServiceHint& last) {
    // Drain a paused client's command chain in this tick (a DeZog step is 4-6
    // sequential round trips, dzrp-frontend.md §4.2.3), and never block the
    // tick: a GUI stays responsive while DeZog holds the machine.
    if (paused && last.remote_attached) return PumpBudget{0, 2, 10};
    return PumpBudget{};
}

bool DebugServers::headless_should_wait(bool paused, const ServiceHint& last) {
    return paused && last.remote_attached;
}

PumpBudget DebugServers::headless_wait_budget() {
    // Up to 50 ms for the first command, then the same drain as the GUI. A
    // command that arrives mid-wait is answered within ~1 ms (T's
    // `service_once` sleeps in 1 ms steps), which is also the headless half of
    // REQ-dzrp-9's "≤ 2 ms paused cadence".
    return PumpBudget{50, 2, 10};
}
