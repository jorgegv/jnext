#pragma once

// ---------------------------------------------------------------------------
// GdbServer — the GDB Remote Serial Protocol adapter (GH #281, epic #276
// package G, WP-3), so `z88dk-gdb` can debug a program running in jnext.
//
// A `remote::Protocol` over the shared transport (package T, transport.h): it
// owns one `remote::Server`, parses RSP packets from its `Connection`
// (rsp_codec.h), executes each one synchronously against the backend
// (`jnext::dbg::Debugger`) and writes the reply. It never touches a socket and
// never touches `Emulator`.
//
// Design: doc/design/debug-subsystem/gdb-rsp-frontend.md — the packet table
// is §2, the target description §3 (target_desc.h), the memory model and the
// `monitor` vocabulary §4, stepping and stop replies §5, transport and loop
// ownership §6.
//
// ── WIRING ─────────────────────────────────────────────────────────────────
//
//     GdbServer gdb(debugger);
//     gdb.server().open(cfg.debug_listen_address, port);
//     debugger.add_service(gdb.server());
//
// (`platform/debug_servers.cpp` does exactly that for `--gdb-port`.) The unit
// suite opens the same Server over T's `FakeListener` instead.
//
// ── THE STOP-REPLY STATE MACHINE (§5.4) ────────────────────────────────────
//
// The client routes replies POSITIONALLY: the next packet after a request is
// taken as its reply, whatever it says (§1.2). So a stop reply (`T…`) may only
// ever be the answer to a request the client is WAITING on — `c`, `s`, `i` or
// `?` — never spontaneous, and exactly one per request. One field, `owed_`,
// records that one is owed and to which kind of request:
//
//   request     machine paused            machine running
//   `?`         reply T05 now             pause(); owed = Question
//   `s`         step_into(); reply T05 now (synchronous)
//   `c`, `i`    run() / run_to(); owed = Continue (refused → E01, nothing owed)
//   0x03        with a reply owed: pause a running machine; the edge answers
//               with NOTHING owed: pause a running machine; reply NOTHING
//
// 0x03 IS NOT A REQUEST (review round 1, reproduced with the real z88dk-gdb
// 2.4): it only asks for the stop that answers an outstanding `c`/`s`/`i`.
// With nothing owed the client is at its prompt, or has already been sent the
// stop reply and not read it yet — any `T` then would be taken as the reply to
// its NEXT request (a `g`, an `m`). gdbserver answers nothing there either.
// `?` while a reply is already owed owes no second one: the pause edge answers
// the first.
//
// A backend `Paused` push is kept only while a reply is owed; `on_notify()`
// (after the pump's drain) turns it into ONE packet and clears `owed_`. A
// `Paused` with nothing owed — a GUI pause while the client already believes
// the machine stopped — sends nothing: the client's model is already right.
// Question → T05; Continue → by the reason (§5.3):
//
//   one of this client's Z0/Z1 matched   T05thread:1;swbreak:;
//   one of this client's Z2/Z3/Z4        T05thread:1;watch|rwatch|awatch:<addr>;
//   Step / RunTo by this client          T05thread:1;
//   anything else                        T02thread:1;   (another client, magic…)
//
// Rule 4: an inspection packet (`g G p P m M X Z z qRcmd`) that arrives while
// the machine runs — resumed behind the client's back by the GUI — PAUSES it
// first, then is served; no stop reply is owed, so none is sent. And an
// inspection packet while a reply IS owed means the client already counts the
// machine stopped (it has taken some earlier packet as that reply): the owed
// reply is ABANDONED, so it cannot arrive later as the answer to something
// else (review round 1).
//
// ── OWNERSHIP ──────────────────────────────────────────────────────────────
//
// The client is attached on connect. Its `Z` breakpoints are ordinary
// subscriptions it owns (listed read-only in the GUI); `i`'s run-to target is
// the backend's TRANSIENT `run_to` subscription, owned by this client too but
// hidden from every user list. SES-01's detach — on `D`, `k` or a dropped
// socket — removes both, and resumes the machine iff the pause is this
// client's, so a crashed client can never leave a stopping breakpoint behind.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "debug/debugger.h"
#include "remote/gdb/rsp_codec.h"
#include "remote/transport.h"

namespace jnext {
namespace remote {
namespace gdb {

class GdbServer final : public Protocol, public jnext::dbg::Listener {
public:
    explicit GdbServer(jnext::dbg::Debugger& dbg);

    /// Unregisters its Server from the backend and ends a live session (the
    /// client is detached). The `Debugger` must outlive this object.
    ~GdbServer() override;

    GdbServer(const GdbServer&)            = delete;
    GdbServer& operator=(const GdbServer&) = delete;

    /// The transport this adapter serves on: `open()` it, `add_service()` it.
    Server& server() { return server_; }

    /// The backend client of the connected session; `CLIENT_NONE` without one.
    jnext::dbg::ClientId client() const { return cid_; }

    // ── remote::Protocol ──────────────────────────────────────────────────
    void                    on_connect(Connection& c) override;
    jnext::dbg::ServiceStep on_service(Connection& c) override;
    void                    on_notify(Connection& c) override;
    void                    on_disconnect() override;

    // ── dbg::Listener (SES-02) ────────────────────────────────────────────
    void on_paused(const jnext::dbg::PausedInfo& info) override;
    void on_resumed(jnext::dbg::ClientId by) override;
    void on_reset(jnext::dbg::ResetKind kind) override;
    void on_frame_ended(std::uint32_t frame) override;
    void on_subscriptions_changed(jnext::dbg::EventKindMask kinds) override;
    void on_exit_requested(int code) override;
    void on_log(jnext::dbg::LogLevel level, const std::string& text) override;

private:
    /// Which request a stop reply is owed to (see the header banner).
    enum class Owed : std::uint8_t { None, Continue, Question };

    /// One `Z` insertion: type 0..4, address, and — for a watch — its length.
    struct BpKey {
        char          type;
        std::uint32_t addr;
        std::uint32_t len;  // 0 for Z0/Z1: a code breakpoint's `kind` is ignored
        bool operator<(const BpKey& o) const {
            if (type != o.type) return type < o.type;
            if (addr != o.addr) return addr < o.addr;
            return len < o.len;
        }
    };

    void dispatch(const std::string& body);
    void reply(const std::string& body);
    void unsupported(const std::string& body);
    void end_session();
    /// Rule 4: pause a machine that is running behind the client's back, and
    /// abandon a stop reply the client is evidently no longer waiting for.
    void pause_first();
    /// The §5.3 stop reply for a `Continue`.
    std::string stop_reply(const jnext::dbg::PausedInfo& info) const;

    // Packet handlers, one per served packet family.
    void pkt_query(const std::string& body);
    void pkt_question();
    void pkt_interrupt();
    void pkt_read_registers();
    void pkt_write_registers(const std::string& body);
    void pkt_read_register(const std::string& body);
    void pkt_write_register(const std::string& body);
    void pkt_read_memory(const std::string& body);
    void pkt_write_memory(const std::string& body, bool binary);
    void pkt_breakpoint(const std::string& body);
    void pkt_continue(const std::string& body);
    void pkt_step(const std::string& body);
    void pkt_step_over(const std::string& body);
    void pkt_detach(bool reply_ok);
    void pkt_xfer(const std::string& body);
    void pkt_monitor(const std::string& body);

    /// `monitor` (qRcmd) — §4.3. Output lines go out as `O` packets.
    void monitor_line(const std::string& text);
    void monitor_help();
    bool monitor_regs(const std::vector<std::string>& args);
    bool monitor_set(const std::vector<std::string>& args);
    bool monitor_mmu(const std::vector<std::string>& args);
    bool monitor_nextreg(const std::vector<std::string>& args);
    bool monitor_page(const std::vector<std::string>& args);
    bool monitor_in(const std::vector<std::string>& args);
    bool monitor_out(const std::vector<std::string>& args);
    bool monitor_sym(const std::vector<std::string>& args);
    bool monitor_time(const std::vector<std::string>& args);
    bool monitor_reset(const std::vector<std::string>& args);
    bool monitor_bp(const std::vector<std::string>& args);

    jnext::dbg::Debugger& dbg_;
    Server                server_;

    // Per-connection state.
    Connection*               conn_ = nullptr;  // valid inside on_service only
    RspParser                 parser_;
    std::vector<std::uint8_t> scratch_;

    // Per-session state: set on connect, cleared on detach / disconnect.
    jnext::dbg::ClientId                    cid_          = jnext::dbg::CLIENT_NONE;
    jnext::dbg::ClientId                    last_resumer_ = jnext::dbg::CLIENT_NONE;
    Owed                                    owed_         = Owed::None;
    std::optional<jnext::dbg::PausedInfo>   pending_;
    std::map<BpKey, jnext::dbg::EventId>    bps_;
};

}  // namespace gdb
}  // namespace remote
}  // namespace jnext
