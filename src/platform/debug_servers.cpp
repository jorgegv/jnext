#include "platform/debug_servers.h"

#include "core/log.h"
#include "remote/dzrp/dzrp_server.h"
#include "remote/gdb/rsp_server.h"
#include "remote/pipe_transport.h"
#include "remote/zrcp/zrcp_server.h"

using jnext::dbg::PumpBudget;
using jnext::dbg::ServiceHint;

DebugServers::DebugServers() = default;

// Out of line: the servers are only forward-declared in the header.
DebugServers::~DebugServers() = default;

bool DebugServers::start(jnext::dbg::Debugger& dbg, const EmulatorConfig& cfg) {
    // The page channels first, in every build (the browser has no other way in).
    // `open()` logs `<name>: listening on channel:0`, like the socket ones.
    const auto open_channel = [&](auto& server, std::unique_ptr<jnext::remote::PipeChannel>& ch,
                                  const char* flag) {
        ch = std::make_unique<jnext::remote::PipeChannel>();
        if (!server->server().open(ch->make_listener(), "channel", 0)) {
            Log::platform()->error("--debug-channel {}: the page channel cannot open ({}) - "
                                   "exiting",
                                   flag, server->server().last_error());
            return false;
        }
        dbg.add_service(server->server());
        return true;
    };
    if (cfg.dzrp_channel) {
        dzrp_ch_server_ = std::make_unique<jnext::remote::dzrp::DzrpServer>(dbg);
        if (!open_channel(dzrp_ch_server_, dzrp_ch_, "dzrp")) return false;
    }
    if (cfg.gdb_channel) {
        gdb_ch_server_ = std::make_unique<jnext::remote::gdb::GdbServer>(dbg);
        if (!open_channel(gdb_ch_server_, gdb_ch_, "gdb")) return false;
    }
    if (cfg.zrcp_channel) {
        zrcp_ch_server_ = std::make_unique<jnext::remote::zrcp::ZrcpServer>(dbg);
        if (!open_channel(zrcp_ch_server_, zrcp_ch_, "zrcp")) return false;
    }
#ifdef __EMSCRIPTEN__
    // The refused --dzrp-port / --gdb-port / --zrcp-port are never set: no socket server.
    return true;
#else
    if (cfg.dzrp_port >= 0) {
        auto s = std::make_unique<jnext::remote::dzrp::DzrpServer>(dbg);
        // `open()` logs `dzrp: listening on <addr>:<port>` — the BOUND port,
        // which is how a `--dzrp-port 0` run tells its user (and a regression
        // row) where it is — or `dzrp: cannot listen on …` with the reason.
        if (!s->server().open(cfg.debug_listen_address,
                              static_cast<std::uint16_t>(cfg.dzrp_port))) {
            Log::platform()->error("--dzrp-port {}: the DZRP server cannot listen on {} ({}) - "
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
            Log::platform()->error("--gdb-port {}: the GDB RSP server cannot listen on {} ({}) - "
                                   "exiting",
                                   cfg.gdb_port, cfg.debug_listen_address,
                                   s->server().last_error());
            return false;
        }
        dbg.add_service(s->server());
        gdb_ = std::move(s);
    }
    if (cfg.zrcp_port >= 0) {
        // GH #280 — the same shape, the same port rule. `zrcp: listening on
        // <addr>:<port>` is what a `--zrcp-port 0` run (and zrcp-func) reads.
        auto s = std::make_unique<jnext::remote::zrcp::ZrcpServer>(dbg);
        if (!s->server().open(cfg.debug_listen_address,
                              static_cast<std::uint16_t>(cfg.zrcp_port))) {
            Log::platform()->error("--zrcp-port {}: the ZRCP server cannot listen on {} ({}) - "
                                   "exiting",
                                   cfg.zrcp_port, cfg.debug_listen_address,
                                   s->server().last_error());
            return false;
        }
        dbg.add_service(s->server());
        zrcp_ = std::move(s);
    }
    return true;
#endif
}

jnext::remote::PipeChannel* DebugServers::channel(cli::DebugProtocol p) {
    switch (p) {
    case cli::DebugProtocol::Dzrp: return dzrp_ch_.get();
    case cli::DebugProtocol::Zrcp: return zrcp_ch_.get();
    case cli::DebugProtocol::Gdb:  return gdb_ch_.get();
    }
    return nullptr;
}

PumpBudget DebugServers::frame_loop_budget(bool paused, const ServiceHint& last) {
    // Drain a paused client's command chain in this tick (a DeZog step is 4-6
    // sequential round trips, dzrp-frontend.md §4.2.3), and never block the
    // tick: a GUI stays responsive while DeZog holds the machine.
#ifdef __EMSCRIPTEN__
    // A page: while wasm runs no byte can arrive (the host's events wait), so
    // the 2 ms drain would be a pure busy-wait. 0 still answers every command
    // already buffered (one pass per command, never sleeping), bounded by 10 ms.
    if (paused && last.remote_attached) return PumpBudget{0, 0, 10};
#else
    if (paused && last.remote_attached) return PumpBudget{0, 2, 10};
#endif
    return PumpBudget{};
}

bool DebugServers::headless_should_wait(bool paused, const ServiceHint& last,
                                        bool wait_without_remote) {
    return paused && (last.remote_attached || wait_without_remote);
}

PumpBudget DebugServers::headless_wait_budget() {
    // Up to 50 ms for the first command, then the same drain as the GUI. A
    // command that arrives mid-wait is answered within ~1 ms (T's
    // `service_once` sleeps in 1 ms steps), which is also the headless half of
    // REQ-dzrp-9's "≤ 2 ms paused cadence".
    return PumpBudget{50, 2, 10};
}
