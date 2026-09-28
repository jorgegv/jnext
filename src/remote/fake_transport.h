#pragma once

// ---------------------------------------------------------------------------
// The in-memory fake transport (GH #287, package T of epic #276).
//
// The adapter suites (`dzrp_adapter_test`, `zrcp_adapter_test`,
// `gdb_rsp_test`) run their `Protocol` inside a real `remote::Server` and push
// packets through THIS instead of a socket: no network, no port, no timing.
// Arch §9: "each server over a fake in-memory `Transport`".
//
// It implements the SAME interfaces the socket layer does —
// `esp::EspListener` and `esp::EspTransport` — so everything between the
// listener and the adapter (admission, the one-client rule and busy reply,
// buffering, partial writes, disconnect, linger) is the production code. Only
// the kernel is replaced.
//
//   FakeListener   an `esp::EspListener`. `connect()` is a client arriving:
//                  it queues the server end and returns the peer end.
//   FakeTransport  the server end of one connection (`esp::EspTransport`).
//   FakePeer       the test's end: `send` what the client says, `take` what
//                  the server wrote, `close` like a client hanging up.
//
// EVERYTHING IS BOUNDED, BY CONSTRUCTION. Nothing here waits, and nothing
// answers forever: the peer's receive side holds at most `window` bytes (64 MiB
// unless a row lowers it), after which the server's `send` takes nothing —
// exactly a kernel buffer that is full. An unbounded fake once turned a
// mutation into a 96 GiB host outage.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "esp01/esp_socket.h"

namespace jnext {
namespace remote {

/// The shared state of one fake connection. Internal; rows use `FakePeer`.
struct FakePipe;

/// The test's end of one fake connection.
class FakePeer {
public:
    explicit FakePeer(std::shared_ptr<FakePipe> pipe) : pipe_(std::move(pipe)) {}

    /// The client sends. Ignored once either side has closed.
    void send(const std::string& bytes);

    /// Take up to `max` bytes the server has written, oldest first.
    std::string take(std::size_t max = static_cast<std::size_t>(-1));

    /// Bytes the server has written that `take` has not collected.
    std::size_t pending() const;

    /// The client hangs up (a FIN). What it sent before is still delivered.
    void close();

    /// The connection breaks (a reset): the server's next `recv`/`send`
    /// reports `Failed`.
    void reset();

    /// The server closed its end.
    bool closed_by_server() const;

    /// The most bytes that may sit un-taken on the client's side — a kernel
    /// buffer. `send` beyond it takes only what fits, which is a partial write;
    /// 0 is a client that has stopped reading altogether.
    void set_window(std::size_t bytes);

    /// The most bytes one server `recv` may return — a partial read.
    void set_recv_chunk(std::size_t bytes);

private:
    std::shared_ptr<FakePipe> pipe_;
};

/// The server end of one fake connection. Born `Connected`, as an accepted
/// socket is.
class FakeTransport final : public esp::EspTransport {
public:
    FakeTransport(std::shared_ptr<FakePipe> pipe, const esp::IpAddress& from);

    /// An accepted connection is busy: refused, as the socket layer does.
    bool begin_connect(const std::string& host, std::uint16_t port,
                       esp::Protocol protocol, std::uint16_t local_port) override;
    void                     poll() override {}
    esp::TransportState      state() const override { return state_; }
    const std::string&       last_error() const override { return last_error_; }
    const esp::IpAddress&    peer_address() const override { return from_; }
    std::size_t              send(const std::uint8_t* data, std::size_t len) override;
    std::size_t              recv(std::uint8_t* buf, std::size_t cap) override;
    void                     close() override;

private:
    std::shared_ptr<FakePipe> pipe_;
    esp::IpAddress            from_;
    esp::TransportState       state_ = esp::TransportState::Connected;
    std::string               last_error_;
};

/// A listener clients arrive at by `connect()`.
class FakeListener final : public esp::EspListener {
public:
    /// What `open(0)` reports as the chosen port.
    static constexpr std::uint16_t kEphemeralPort = 49152;

    bool               open(std::uint16_t port) override;
    void               close() override;
    bool               listening() const override { return listening_; }
    std::uint16_t      port() const override { return port_; }
    const std::string& last_error() const override { return last_error_; }
    void               poll() override {}
    std::unique_ptr<esp::EspTransport> accept() override;

    /// A client arrives. Null while not listening (a refused connection).
    std::shared_ptr<FakePeer> connect(const esp::IpAddress& from = esp::ipv4(127, 0, 0, 1));

    /// Make the next `open()` fail with `why` (an address in use, say).
    void refuse_next_open(const std::string& why);

    /// The listening socket faults: it stops listening, `last_error()` = `why`.
    void fail(const std::string& why);

    /// Connections that arrived and have not been accepted yet.
    std::size_t queued() const { return queue_.size(); }

private:
    bool                                      listening_ = false;
    std::uint16_t                             port_      = 0;
    std::string                               last_error_;
    std::string                               refuse_;
    std::deque<std::unique_ptr<esp::EspTransport>> queue_;
};

}  // namespace remote
}  // namespace jnext
