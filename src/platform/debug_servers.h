#pragma once

// ---------------------------------------------------------------------------
// The debugger protocol servers a loop owner hosts (GH #12 WP-5, epic #276).
//
// Every loop owner — QtApp, SdlApp, HeadlessApp — builds one hosted
// `jnext::dbg::Debugger` and pumps it once per tick (SES-03). This is what
// puts the SOCKET servers on that pump: it opens each one the configuration
// asks for on `EmulatorConfig::debug_listen_address` and registers it with
// `Debugger::add_service()`, and it chooses each tick's `PumpBudget`. Today
// that is DZRP (`--dzrp-port`), GDB RSP (`--gdb-port`, #281) and ZRCP
// (`--zrcp-port`, #280), each its own listener and its own backend client, so
// all may run at once.
//
// ── THE BUDGETS — T's recorded decision (transport.md §2 item 15) ───────────
//
//   loop owner | running        | paused, a remote attached
//   Qt, SDL    | PumpBudget{}   | PumpBudget{0, 2, 10}   drain, never block the tick
//   headless   | PumpBudget{}   | PumpBudget{50, 2, 10}  the spin becomes a wait
//
// "Paused with a remote attached" comes from the PREVIOUS pump's
// `ServiceHint` — the only place `remote_attached` is reported — and the
// machine's live pause state.
//
// ── THE PAGE CHANNEL (GH #545) ───────────────────────────────────────────────
//
// In EVERY build a protocol can also be served over a `remote::PipeChannel`
// (`EmulatorConfig::{dzrp,zrcp,gdb}_channel`, set by `--debug-channel`): the
// same adapters over the same `remote::Server`, whose listener is the host
// feeding bytes in and taking bytes out instead of a socket. Only the browser
// page uses it (main.cpp refuses the flag elsewhere); `channel()` is the
// host's handle. In the browser the page cannot listen, so the socket servers
// are not compiled there and the channel is the only way in.
//
// ── LIFETIME ─────────────────────────────────────────────────────────────────
//
// A server holds the `Debugger&` it was started on and unregisters from it in
// its destructor, so a `DebugServers` member must be DECLARED AFTER the loop
// owner's `debugger_` (members are destroyed in reverse order).
// ---------------------------------------------------------------------------

#include <memory>

#include "core/cli_options.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"

namespace jnext {
namespace remote {
class PipeChannel;
namespace dzrp {
class DzrpServer;
}
namespace gdb {
class GdbServer;
}
namespace zrcp {
class ZrcpServer;
}
}  // namespace remote
}  // namespace jnext

class DebugServers {
public:
    DebugServers();
    ~DebugServers();

    DebugServers(const DebugServers&)            = delete;
    DebugServers& operator=(const DebugServers&) = delete;

    /// Open and register every server `cfg` asks for. False — the reason
    /// already logged — when one cannot listen (a port in use, an address that
    /// is not this machine's): a server the user asked for and cannot reach is
    /// a startup failure, not a warning scrolled past.
    bool start(jnext::dbg::Debugger& dbg, const EmulatorConfig& cfg);

    /// Qt and SDL: this tick's budget.
    static jnext::dbg::PumpBudget frame_loop_budget(bool paused,
                                                    const jnext::dbg::ServiceHint& last);

    /// Headless: true when this tick must not run a frame but wait on the
    /// servers instead (paused, with a remote attached).
    static bool headless_should_wait(bool paused, const jnext::dbg::ServiceHint& last);

    /// Headless: the waiting budget.
    static jnext::dbg::PumpBudget headless_wait_budget();

    /// The host's end of `p`'s page channel; null when `cfg` did not ask for it.
    jnext::remote::PipeChannel* channel(cli::DebugProtocol p);

private:
#ifndef __EMSCRIPTEN__
    std::unique_ptr<jnext::remote::dzrp::DzrpServer> dzrp_;
    std::unique_ptr<jnext::remote::gdb::GdbServer>   gdb_;
    std::unique_ptr<jnext::remote::zrcp::ZrcpServer> zrcp_;
#endif
    // The channel servers, in every build. The channel is declared before its
    // server and so outlives it (the listener shares its state either way).
    std::unique_ptr<jnext::remote::PipeChannel>      dzrp_ch_, gdb_ch_, zrcp_ch_;
    std::unique_ptr<jnext::remote::dzrp::DzrpServer> dzrp_ch_server_;
    std::unique_ptr<jnext::remote::gdb::GdbServer>   gdb_ch_server_;
    std::unique_ptr<jnext::remote::zrcp::ZrcpServer> zrcp_ch_server_;
};
