#pragma once

// ---------------------------------------------------------------------------
// The debugger protocol servers a loop owner hosts (GH #12 WP-5, epic #276).
//
// Every loop owner — QtApp, SdlApp, HeadlessApp — builds one hosted
// `jnext::dbg::Debugger` and pumps it once per tick (SES-03). This is what
// puts the SOCKET servers on that pump: it opens each one the configuration
// asks for on `EmulatorConfig::debug_listen_address` and registers it with
// `Debugger::add_service()`, and it chooses each tick's `PumpBudget`. Today
// that is DZRP (`--dzrp-port`) and ZRCP (`--zrcp-port`, GH #280); GDB RSP
// (#281) joins here.
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
// ── LIFETIME ─────────────────────────────────────────────────────────────────
//
// A server holds the `Debugger&` it was started on and unregisters from it in
// its destructor, so a `DebugServers` member must be DECLARED AFTER the loop
// owner's `debugger_` (members are destroyed in reverse order).
// ---------------------------------------------------------------------------

#include <memory>

#include "core/emulator_config.h"
#include "debug/debugger.h"

namespace jnext {
namespace remote {
namespace dzrp {
class DzrpServer;
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

private:
    std::unique_ptr<jnext::remote::dzrp::DzrpServer> dzrp_;
    std::unique_ptr<jnext::remote::zrcp::ZrcpServer> zrcp_;
};
