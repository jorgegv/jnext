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
//   error  resolve / connect / send / recv failure with the OS error text
//   debug  request accepted, resolve timing + result count, byte counts
//   trace  per-poll state, resolve still outstanding, would-block events
// Resolve TIMING is debug, not warn: since the lookup moved off the calling
// thread it can no longer stall anything, so a slow-but-successful resolve is
// ordinary detail. The pathological case — a resolver that never answers — is
// bounded by AT+CIPSTART's deadline, and that warning is the engine's to emit.
// Only info and above are emitted at the default level, which is exactly the
// owner's "nothing on by default except connection open/close" — and that is
// the default of the module's own seam (esp_log.h), not of a host's logger, so
// the contract holds for a consumer that binds a sink and filters nothing.

#include "esp01/esp_socket.h"

#include "esp01/esp_log.h"
#include "esp01/esp_socket_platform.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>

namespace esp {
namespace {

/// The result of ONE asynchronous name lookup, and the only thing a resolver
/// thread can reach.
///
/// Heap-allocated and jointly owned by the thread and the transport, so its
/// lifetime is the union of theirs rather than either one's. That is what makes
/// abandoning a lookup free: the transport drops its reference and returns; the
/// thread writes into memory that is still its own. See make_socket_transport()
/// in esp_socket.h for the full argument.
///
/// SINGLE-SHOT, and the ordering is the contract: the thread writes `ok`,
/// `addrs` and `err`, THEN releases `done`; the owner reads them only after
/// acquiring `done`, and only once. Nothing is written after publication, so
/// there is a happens-before edge and no data race — not "an unlikely race".
struct ResolveJob {
    std::atomic<bool>                     done{false};
    bool                                  ok = false;
    std::vector<IpAddress>                addrs;
    std::string                           err;
    std::chrono::steady_clock::time_point started{};
};

class SocketTransport final : public EspTransport {
public:
    SocketTransport(const AddressPolicy& policy, ResolveFn resolver)
        : policy_(policy), resolver_(std::move(resolver)) {}

    ~SocketTransport() override { release(); }

    bool begin_connect(const std::string& host, std::uint16_t port) override {
        if (state_ == TransportState::Resolving ||
            state_ == TransportState::Connecting ||
            state_ == TransportState::Connected) {
            log_debug("connect to {}:{} rejected: busy in state {}", host,
                                port, transport_state_text(state_));
            return false;
        }
        if (host.empty() || port == 0) {
            log_debug("connect rejected: empty host or port 0 (host='{}' port={})",
                                host, port);
            return false;
        }
        release();
        host_       = host;
        port_       = port;
        peer_       = IpAddress{};
        last_error_.clear();
        state_ = TransportState::Resolving;
        log_debug("connect requested: {}:{} (resolution deferred to poll)",
                            host_, port_);
        return true;
    }

    void poll() override {
        switch (state_) {
            case TransportState::Resolving:  step_resolve();  break;
            case TransportState::Connecting: step_connect();  break;
            default:
                log_trace("poll: nothing to do in state {}",
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
            log_trace("send: would block ({} bytes offered)", len);
        else
            log_debug("sent {}/{} bytes to {}", n, len, to_string(peer_));
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
            log_info("connection to {}:{} closed by peer", host_, port_);
            release();
            state_ = TransportState::Closed;
            return 0;
        }
        if (n == 0)
            log_trace("recv: no data available");
        else
            log_debug("received {} bytes from {}", n, to_string(peer_));
        return n;
    }

    void close() override {
        if (state_ == TransportState::Idle) return;
        const bool was_live = (state_ == TransportState::Connected);
        release();
        state_ = TransportState::Closed;
        if (was_live)
            log_info("connection to {}:{} closed locally", host_, port_);
        else
            log_debug("transport closed before the connection was live");
    }

private:
    void release() {
        if (sock_ != net::kInvalidSocket) {
            net::close(sock_);
            sock_ = net::kInvalidSocket;
        }
        // ABANDON any lookup still in flight. Dropping our reference is the
        // whole of it — no join, no cancel, no wait — which is why close(), a
        // failure and ~SocketTransport() are all immediate even mid-resolve.
        // It also guarantees a stale result can never be consumed: a later
        // begin_connect() allocates a fresh block, so the abandoned one has no
        // reader left at all.
        job_.reset();
    }

    void fail(std::string why) {
        release();
        last_error_ = std::move(why);
        state_      = TransportState::Failed;
        log_error("{}:{} — {}", host_, port_, last_error_);
    }

    /// Advance name resolution WITHOUT EVER WAITING FOR IT. At most three
    /// distinct visits, and none of them blocks:
    ///
    ///   first    the `AI_NUMERICHOST` pass, on THIS thread. An IP literal is
    ///            parsed here with no network traffic and no thread, and goes
    ///            straight on to the policy gate — the pre-existing behaviour,
    ///            unchanged, for the target nextsync actually uses. Anything
    ///            else starts a resolver thread and returns at once.
    ///   middle   one relaxed test of the result flag. Not ready → return.
    ///   last     the result is consumed, exactly once, and the connect starts.
    void step_resolve() {
        if (!job_) {
            std::vector<IpAddress> literal;
            std::string            err;
            if (net::resolve(host_, /*numeric_only=*/true, literal, err) &&
                !literal.empty()) {
                log_debug("'{}' is a numeric address — no DNS lookup", host_);
                connect_to_resolved(literal);
            } else {
                start_async_resolve();
            }
            return;
        }

        if (!job_->done.load(std::memory_order_acquire)) {
            log_trace("poll: still resolving '{}'", host_);
            return;
        }

        // CONSUME ONCE, ON OUR OWN COPY. The block is detached from the
        // transport before a single field is read out of it, and the addresses
        // are MOVED into a local before the policy sees them. So the list
        // classify_address() judges is bit-for-bit the list connect() is issued
        // against: there is no second reader, nothing that can substitute an
        // address between the verdict and the syscall, and no way to widen the
        // allowlist by winning a race — the resolver thread never had a vote in
        // the decision, only in the candidates.
        const std::shared_ptr<ResolveJob> job = std::move(job_);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - job->started)
                            .count();
        if (!job->ok || job->addrs.empty()) {
            fail("cannot resolve '" + host_ + "': " +
                 (job->err.empty() ? "no addresses" : job->err));
            return;
        }
        std::vector<IpAddress> found = std::move(job->addrs);
        // Elapsed time is debug detail now, not a warning: a slow lookup no
        // longer stalls anything, and the pathological case (a resolver that
        // never answers) is bounded by AT+CIPSTART's deadline, which is where
        // the visible warning belongs.
        log_debug("resolved '{}' to {} address(es) in {} ms", host_, found.size(), ms);
        connect_to_resolved(found);
    }

    /// Hand `host_` to a resolver thread and return. The thread captures a
    /// reference to the result block plus COPIES of the host and the resolver
    /// function — deliberately no `this`, which is the whole safety argument.
    void start_async_resolve() {
        auto job     = std::make_shared<ResolveJob>();
        job->started = std::chrono::steady_clock::now();

        ResolveFn         fn   = resolver_;
        const std::string host = host_;
        try {
            std::thread([job, host, fn]() {
                // NOTHING MAY ESCAPE THIS LAMBDA. An exception that leaves a
                // `std::thread`'s entry point does not propagate anywhere — it
                // goes straight to `std::terminate`, killing the whole process.
                // `resolver` is a PUBLIC seam documented for consumers who
                // already own a resolver, and reporting failure by exception is
                // ordinary in C++ network libraries, so this is a reachable
                // path for anyone reusing the module rather than a theoretical
                // one. A library that converts its caller's exception into a
                // process abort is not reusable. Degrade to a FAILED LOOKUP
                // instead: the engine already handles that (`AT+CIPSTART`
                // answers `ERROR`), so a throwing resolver costs a connection,
                // not the emulator.
                std::vector<IpAddress> out;
                std::string            err;
                bool                   ok = false;
                try {
                    ok = fn ? fn(host, out, err)
                            : net::resolve(host, /*numeric_only=*/false, out, err);
                } catch (const std::exception& e) {
                    ok  = false;
                    out.clear();  // a partially-filled list must never be used
                    err = std::string("the resolver threw: ") + e.what();
                } catch (...) {
                    ok  = false;
                    out.clear();
                    err = "the resolver threw a non-std exception";
                }
                job->ok    = ok;
                job->addrs = std::move(out);
                job->err   = std::move(err);
                // Publish LAST. Everything above happens-before any acquire of
                // this flag, and nothing is written after it.
                job->done.store(true, std::memory_order_release);
            }).detach();
        } catch (const std::system_error& e) {
            // Thread exhaustion is a real failure mode, not an impossibility,
            // and silently parking in Resolving forever would be the worst
            // possible answer to it.
            fail(std::string("cannot start resolver thread: ") + e.what());
            return;
        }
        // Adopted only once the thread exists, so a failed spawn cannot leave
        // behind a job nobody will ever complete.
        job_ = std::move(job);
        log_debug("resolving '{}' off the emulation thread", host_);
    }

    /// Apply the address policy to `found` and start the TCP connect. Runs on
    /// the owning thread, always — a resolver thread never reaches this.
    void connect_to_resolved(const std::vector<IpAddress>& found) {
        std::string err;
        IpAddress   chosen;
        DenyReason  reason = DenyReason::None;
        if (!select_candidate(found, policy_, chosen, reason)) {
            // Refusals are warn, not debug: the owner requires a visible line
            // on every connection made OR refused (GH #25 decision 1).
            log_warn("connection to {}:{} REFUSED by address policy: {} ({})",
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
                log_debug("connect to {}:{} in progress", to_string(peer_), port_);
                break;
        }
    }

    void step_connect() {
        std::string err;
        switch (net::poll_connect(sock_, err)) {
            case net::ConnectProgress::Connected: on_connected(); break;
            case net::ConnectProgress::Failed:    fail("connect failed: " + err); break;
            case net::ConnectProgress::Pending:
                log_trace("connect to {}:{} still pending", to_string(peer_), port_);
                break;
        }
    }

    void on_connected() {
        state_ = TransportState::Connected;
        log_info("connection OPENED to {}:{} (host '{}')", to_string(peer_),
                           port_, host_);
    }

    AddressPolicy     policy_;
    ResolveFn         resolver_;  ///< null = the platform's getaddrinfo
    TransportState    state_ = TransportState::Idle;
    net::NativeSocket sock_  = net::kInvalidSocket;
    std::string       host_;
    std::uint16_t     port_ = 0;
    IpAddress         peer_;
    std::string       last_error_;
    /// Non-null only between starting a lookup and consuming its result. The
    /// resolver thread holds the other reference.
    std::shared_ptr<ResolveJob> job_;
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

std::unique_ptr<EspTransport> make_socket_transport(const AddressPolicy& policy,
                                                    ResolveFn resolver) {
    std::string err;
    if (!net::init(err)) {
        log_error("network initialisation failed: {}", err);
        return nullptr;
    }
    return std::unique_ptr<EspTransport>(new SocketTransport(policy, std::move(resolver)));
}

}  // namespace esp
