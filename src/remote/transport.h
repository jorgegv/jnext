#pragma once

// ---------------------------------------------------------------------------
// jnext::remote — the one socket transport the debugger protocol servers share
// (GH #287, package T of epic #276).
//
// DZRP (#12), ZRCP (#280) and GDB RSP (#281) all need the same thing: one
// listening socket, one client at a time, non-blocking accept/read/write
// driven from `Debugger::pump()`, and a log line that names the bound port.
// This file is that mechanism, written once. It carries NO PROTOCOL CONTENT:
// it never parses a byte. What a command is, what a reply says and what a
// second client is told are the adapter's (`Protocol`, `ServerConfig`).
//
// Design and the reasons for each decision:
// doc/design/debug-subsystem/transport.md.
//
// ── THE THREE PIECES ───────────────────────────────────────────────────────
//
//   Server      a `jnext::dbg::Service`. It owns an `esp::EspListener`, admits
//               one client, hands every other one the adapter's busy reply,
//               buffers both directions and calls the adapter's `Protocol`.
//   Connection  the adapter's view of its one client: `read` / `write` /
//               `close`. Writes never block and are never dropped: they are
//               queued, sent when the adapter's callback returns, and the rest
//               goes out as the peer accepts it.
//   Protocol    what an adapter implements. `on_service` executes AT MOST ONE
//               complete command, which is `Service::service_once`'s contract.
//
// ── THE SEAM UNDERNEATH ────────────────────────────────────────────────────
//
// Sockets come ONLY from the public `esp::make_socket_listener` /
// `esp::EspListener` / `esp::EspTransport` interface
// (src/esp01/include/esp01/esp_socket.h), which is already non-blocking and
// Windows-twinned. This module makes no socket call of its own. The same
// `Server` runs over `FakeListener` (fake_transport.h) in the unit suites, so
// the adapters' suites push bytes through exactly this code, minus the kernel.
//
// ── THREADING ──────────────────────────────────────────────────────────────
//
// None. Everything runs on the emulation thread, inside `pump()` (arch §5).
// The only wait is `service_once(wait_ms)`, bounded by `wait_ms`.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "debug/debugger.h"
#include "esp01/esp_socket.h"

namespace jnext {
namespace remote {

/// Per-server settings. Only `name` has no default.
struct ServerConfig {
    /// The protocol's short name: `dzrp`, `zrcp`, `gdb`. It prefixes every log
    /// line this server writes, including the one the regression rows wait for:
    /// `<name>: listening on 127.0.0.1:NNNNN`.
    std::string name;

    /// Bytes written to a SECOND concurrent connection before it is closed.
    /// Empty = close it with nothing sent. The three designs differ here
    /// (ZRCP answers `Error. Another ZRCP client is connected\n`; DZRP and RSP
    /// send nothing), so it is the adapter's to choose. The refusal itself —
    /// one client per listener, logged at warn — is the same for all three.
    std::string busy_reply;

    /// Stop reading a peer once this many received bytes are waiting for the
    /// adapter. The kernel buffer then fills and the peer's own sends stall:
    /// backpressure, never a loss. An adapter that reads everything it is
    /// offered into its own parser never reaches it.
    std::size_t max_input = std::size_t{1} << 20;

    /// A peer this many bytes behind on its replies is disconnected rather
    /// than buffered without bound (32 MiB: twice DZRP's 16 MiB frame cap).
    std::size_t max_output = std::size_t{32} << 20;

    /// How long a connection the ADAPTER closed may take to deliver what it
    /// still owes the peer (a ZRCP `Sayonara baby`, a busy reply) before it is
    /// closed anyway.
    int linger_ms = 2000;
};

/// One accepted client, as its adapter sees it. Handed to the `Protocol`
/// callbacks; valid only for the duration of a call. An adapter must not keep
/// the reference past `on_disconnect()`.
class Connection {
public:
    ~Connection();
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    /// Received bytes not yet taken by `read`.
    std::size_t available() const { return in_.size() - in_off_; }

    /// Take up to `cap` received bytes. Never blocks; 0 = nothing buffered.
    std::size_t read(std::uint8_t* buf, std::size_t cap);

    /// Queue bytes for the peer. Never blocks and never drops. The Server sends
    /// the queue when the current callback returns, and what the kernel does
    /// not take then goes out on later passes. A peer that falls
    /// `ServerConfig::max_output` bytes behind (a single reply larger than that
    /// counts) is disconnected instead.
    void write(const std::uint8_t* data, std::size_t len);
    void write(const std::string& text);

    /// End the session from this side. What is already queued is still
    /// delivered (bounded by `ServerConfig::linger_ms`); `on_disconnect()`
    /// follows at the end of the current pass. Idempotent.
    void close();

    /// True once `close()` has been called.
    bool closing() const { return closing_; }

    /// True once the peer has closed or the connection has failed. Bytes it
    /// sent before that are still `available()` and are still offered to
    /// `on_service()`.
    bool peer_gone() const { return gone_; }

    /// The peer's numeric address (no port: the seam does not report one).
    const std::string& peer() const { return peer_; }

    /// Queued bytes the kernel has not taken yet.
    std::size_t pending_output() const { return out_.size() - out_off_; }

private:
    friend class Server;
    Connection(std::unique_ptr<esp::EspTransport> transport, const ServerConfig& cfg);

    bool pull();            // move what the kernel has into `in_`; true if anything came
    void push();            // hand `out_` to the kernel, as much as it takes
    void discard_input();   // pull and throw away (a closing connection)
    void shut();            // release the socket now

    std::unique_ptr<esp::EspTransport> t_;
    const ServerConfig&                cfg_;
    std::string                        peer_;
    std::vector<std::uint8_t>          in_;
    std::size_t                        in_off_ = 0;
    std::vector<std::uint8_t>          out_;
    std::size_t                        out_off_ = 0;
    bool closing_  = false;  // the adapter asked
    bool gone_     = false;  // the peer closed, or the transport failed
    bool overflow_ = false;  // the peer stopped reading its replies
    std::chrono::steady_clock::time_point linger_until_{};
};

/// What a protocol adapter implements. Every call is made by `Server` on the
/// emulation thread, from inside `pump()`, and must return promptly.
///
/// Pure virtual throughout, as the backend's `Listener` is: a silently ignored
/// callback is the failure a default empty body invites.
class Protocol {
public:
    virtual ~Protocol() = default;

    /// A client was admitted. The adapter may write a greeting (ZRCP's
    /// welcome) or attach to the `Debugger` here; DZRP attaches on `CMD_INIT`.
    virtual void on_connect(Connection& c) = 0;

    /// Read what is `available()`, and execute AT MOST ONE complete command,
    /// writing its reply. `Serviced` when a command was executed, `Idle` when
    /// none was complete. Called on every service pass while a client is
    /// connected — also when nothing new arrived, so a command already
    /// buffered behind the last one is not left waiting for more bytes.
    virtual jnext::dbg::ServiceStep on_service(Connection& c) = 0;

    /// Write out queued notifications (a `Paused` turned into a packet). Called
    /// once per `pump()`, after the drain (`Service::flush_notifications`).
    virtual void on_notify(Connection& c) = 0;

    /// The client is gone: the peer closed (after its last bytes were offered
    /// to `on_service` and it reported `Idle`), the connection failed, the peer
    /// stopped reading, the adapter called `close()`, or `Server::stop()` ran.
    /// Called exactly once per `on_connect`. Detach here (SES-01).
    virtual void on_disconnect() = 0;
};

/// One listener, one client at a time, driven by `Debugger::pump()`.
class Server final : public jnext::dbg::Service {
public:
    Server(ServerConfig cfg, Protocol& protocol);

    /// Closes every socket WITHOUT calling the protocol: an adapter that owns
    /// its `Server` as a member may already be half-destroyed by now. Call
    /// `stop()` first for an orderly `on_disconnect()`.
    ~Server() override;

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    /// Listen on the real socket layer. `bind_address` is a NUMERIC IP
    /// (`--debug-listen-address`); a name, or anything else that is not one,
    /// is refused rather than resolved. `port` 0 = an OS-chosen port, which
    /// `port()` and the log line then report. False — with `last_error()` set
    /// and an error logged — when the address is refused or the bind fails.
    bool open(const std::string& bind_address, std::uint16_t port);

    /// The same over any `esp::EspListener` (a `FakeListener` in the unit
    /// suites). `label` is the address as the log line shows it.
    bool open(std::unique_ptr<esp::EspListener> listener, const std::string& label,
              std::uint16_t port);

    /// Stop listening and end the session: the client gets `on_disconnect()`,
    /// sockets close. Idempotent.
    void stop();

    bool               listening() const;
    /// The port actually bound; 0 when not listening.
    std::uint16_t      port() const;
    const std::string& last_error() const { return last_error_; }

    // ── jnext::dbg::Service ────────────────────────────────────────────────
    /// Accept, read, run the adapter's `on_service`, write — repeated until a
    /// command was executed or `wait_ms` has passed. `wait_ms` 0 is exactly one
    /// pass and never sleeps. Also returns early (`Idle`) when the client goes.
    jnext::dbg::ServiceStep service_once(int wait_ms) override;
    void                    flush_notifications() override;
    /// A client is admitted and `on_disconnect()` has not been called.
    bool                    peer_connected() const override { return active_ != nullptr; }

private:
    struct Pass {
        jnext::dbg::ServiceStep step = jnext::dbg::ServiceStep::Idle;
        bool                    ended = false;  // the client went during this pass
    };
    Pass pass();
    void admit(std::unique_ptr<esp::EspTransport> t);
    bool settle(jnext::dbg::ServiceStep last_step);  // retire a finished client
    void retire();
    void reap();

    ServerConfig                             cfg_;
    Protocol&                                protocol_;
    std::unique_ptr<esp::EspListener>        listener_;
    std::string                              label_;
    std::uint16_t                            bound_port_ = 0;
    std::string                              last_error_;
    bool                                     listener_failed_ = false;
    std::unique_ptr<Connection>              active_;
    std::vector<std::unique_ptr<Connection>> lingering_;
};

}  // namespace remote
}  // namespace jnext
