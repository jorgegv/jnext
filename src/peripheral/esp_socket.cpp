// Portable half of the emulated ESP-01 TCP transport (GH #25, branch 2):
// the state machine, the address-policy enforcement and all the tracing.
//
// Everything OS-specific is reached through esp_socket_platform.h, whose two
// whole-file-#ifdef'd twins (esp_socket_posix.cpp / esp_socket_win.cpp)
// provide it. This file compiles identically on every platform.
//
// Tracing (owner made it a first-class requirement on GH #25, decision 3):
//   info   connection opened / closed              — user-visible, security-
//   warn   connection refused by the address policy   relevant, ON by default
//   warn   DNS failure, or a resolve slow enough to have stalled the frame
//   error  connect / send / recv failure with the OS error text
//   debug  request accepted, resolve timing + result count, byte counts
//   trace  per-poll state, would-block events
// Only info and above are emitted at the default level, which is exactly the
// owner's "nothing on by default except connection open/close".

#include "peripheral/esp_socket.h"

#include "core/log.h"
#include "peripheral/esp_socket_platform.h"

#include <chrono>
#include <utility>

namespace esp {
namespace {

/// A resolve slower than this is reported at warn: it is the one place this
/// layer can stall the frame loop, so it must never be silent.
constexpr long kSlowResolveMs = 200;

class SocketTransport final : public EspTransport {
public:
    explicit SocketTransport(const AddressPolicy& policy) : policy_(policy) {}

    ~SocketTransport() override { release(); }

    bool begin_connect(const std::string& host, std::uint16_t port) override {
        if (state_ == TransportState::Resolving ||
            state_ == TransportState::Connecting ||
            state_ == TransportState::Connected) {
            Log::esp01()->debug("connect to {}:{} rejected: busy in state {}", host,
                                port, transport_state_text(state_));
            return false;
        }
        if (host.empty() || port == 0) {
            Log::esp01()->debug("connect rejected: empty host or port 0 (host='{}' port={})",
                                host, port);
            return false;
        }
        release();
        host_       = host;
        port_       = port;
        peer_       = IpAddress{};
        last_error_.clear();
        state_ = TransportState::Resolving;
        Log::esp01()->debug("connect requested: {}:{} (resolution deferred to poll)",
                            host_, port_);
        return true;
    }

    void poll() override {
        switch (state_) {
            case TransportState::Resolving:  step_resolve();  break;
            case TransportState::Connecting: step_connect();  break;
            default:
                Log::esp01()->trace("poll: nothing to do in state {}",
                                    transport_state_text(state_));
                break;
        }
    }

    TransportState     state() const override        { return state_; }
    const std::string& last_error() const override   { return last_error_; }
    const IpAddress&   peer_address() const override { return peer_; }

    std::size_t send(const std::uint8_t* data, std::size_t len) override {
        if (state_ != TransportState::Connected || data == nullptr || len == 0)
            return 0;
        bool        failed = false;
        std::string err;
        const std::size_t n = net::send(sock_, data, len, failed, err);
        if (failed) {
            fail("send failed: " + err);
            return 0;
        }
        if (n == 0)
            Log::esp01()->trace("send: would block ({} bytes offered)", len);
        else
            Log::esp01()->debug("sent {}/{} bytes to {}", n, len, to_string(peer_));
        return n;
    }

    std::size_t recv(std::uint8_t* buf, std::size_t cap) override {
        if (state_ != TransportState::Connected || buf == nullptr || cap == 0)
            return 0;
        bool        eof = false, failed = false;
        std::string err;
        const std::size_t n = net::recv(sock_, buf, cap, eof, failed, err);
        if (failed) {
            fail("recv failed: " + err);
            return 0;
        }
        if (eof) {
            Log::esp01()->info("connection to {}:{} closed by peer", host_, port_);
            release();
            state_ = TransportState::Closed;
            return 0;
        }
        if (n == 0)
            Log::esp01()->trace("recv: no data available");
        else
            Log::esp01()->debug("received {} bytes from {}", n, to_string(peer_));
        return n;
    }

    void close() override {
        if (state_ == TransportState::Idle) return;
        const bool was_live = (state_ == TransportState::Connected);
        release();
        state_ = TransportState::Closed;
        if (was_live)
            Log::esp01()->info("connection to {}:{} closed locally", host_, port_);
        else
            Log::esp01()->debug("transport closed before the connection was live");
    }

private:
    void release() {
        if (sock_ != net::kInvalidSocket) {
            net::close(sock_);
            sock_ = net::kInvalidSocket;
        }
    }

    void fail(std::string why) {
        release();
        last_error_ = std::move(why);
        state_      = TransportState::Failed;
        Log::esp01()->error("{}:{} — {}", host_, port_, last_error_);
    }

    /// Resolve, apply the address policy, and start the connect.
    ///
    /// The `AI_NUMERICHOST` pass runs first and never touches the network, so
    /// an IP-literal target (the common nextsync configuration) reaches the
    /// socket with zero blocking. Only a genuine name falls through to the
    /// real lookup — the single blocking call in this layer, timed and
    /// reported. See make_socket_transport()'s comment for why this is
    /// synchronous.
    void step_resolve() {
        std::vector<IpAddress> found;
        std::string            err;

        if (!net::resolve(host_, /*numeric_only=*/true, found, err) || found.empty()) {
            found.clear();
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = net::resolve(host_, /*numeric_only=*/false, found, err);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0)
                                .count();
            if (!ok || found.empty()) {
                fail("cannot resolve '" + host_ + "': " + (err.empty() ? "no addresses" : err));
                return;
            }
            if (ms >= kSlowResolveMs)
                Log::esp01()->warn(
                    "DNS lookup of '{}' blocked the emulator for {} ms ({} address(es))",
                    host_, ms, found.size());
            else
                Log::esp01()->debug("resolved '{}' to {} address(es) in {} ms", host_,
                                    found.size(), ms);
        } else {
            Log::esp01()->debug("'{}' is a numeric address — no DNS lookup", host_);
        }

        IpAddress  chosen;
        DenyReason reason = DenyReason::None;
        if (!select_candidate(found, policy_, chosen, reason)) {
            // Refusals are warn, not debug: the owner requires a visible line
            // on every connection made OR refused (GH #25 decision 1).
            Log::esp01()->warn("connection to {}:{} REFUSED by address policy: {} ({})",
                               host_, port_, deny_reason_text(reason),
                               found.empty() ? std::string("no candidates")
                                             : to_string(found.front()));
            last_error_ = std::string("address refused by policy: ") + deny_reason_text(reason);
            release();
            state_ = TransportState::Failed;
            return;
        }
        peer_ = normalize(chosen);

        sock_ = net::open_nonblocking(chosen.family, err);
        if (sock_ == net::kInvalidSocket) {
            fail("cannot create socket: " + err);
            return;
        }

        state_ = TransportState::Connecting;
        switch (net::begin_connect(sock_, chosen, port_, err)) {
            case net::ConnectProgress::Connected: on_connected(); break;
            case net::ConnectProgress::Failed:    fail("connect failed: " + err); break;
            case net::ConnectProgress::Pending:
                Log::esp01()->debug("connect to {}:{} in progress", to_string(peer_), port_);
                break;
        }
    }

    void step_connect() {
        std::string err;
        switch (net::poll_connect(sock_, err)) {
            case net::ConnectProgress::Connected: on_connected(); break;
            case net::ConnectProgress::Failed:    fail("connect failed: " + err); break;
            case net::ConnectProgress::Pending:
                Log::esp01()->trace("connect to {}:{} still pending", to_string(peer_), port_);
                break;
        }
    }

    void on_connected() {
        state_ = TransportState::Connected;
        Log::esp01()->info("connection OPENED to {}:{} (host '{}')", to_string(peer_),
                           port_, host_);
    }

    AddressPolicy     policy_;
    TransportState    state_ = TransportState::Idle;
    net::NativeSocket sock_  = net::kInvalidSocket;
    std::string       host_;
    std::uint16_t     port_ = 0;
    IpAddress         peer_;
    std::string       last_error_;
};

}  // namespace

const char* transport_state_text(TransportState s) {
    switch (s) {
        case TransportState::Idle:       return "idle";
        case TransportState::Resolving:  return "resolving";
        case TransportState::Connecting: return "connecting";
        case TransportState::Connected:  return "connected";
        case TransportState::Closed:     return "closed";
        case TransportState::Failed:     return "failed";
    }
    return "unknown";
}

std::unique_ptr<EspTransport> make_socket_transport(const AddressPolicy& policy) {
    std::string err;
    if (!net::init(err)) {
        Log::esp01()->error("network initialisation failed: {}", err);
        return nullptr;
    }
    return std::unique_ptr<EspTransport>(new SocketTransport(policy));
}

}  // namespace esp
