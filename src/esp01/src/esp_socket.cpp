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
//   warn   a peer that had served data closed with an RST instead of a FIN
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

    bool begin_connect(const std::string& host, std::uint16_t port, Protocol protocol,
                       std::uint16_t local_port) override {
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
        protocol_   = protocol;
        // TCP has no local-port argument at all (AT+CIPSTART's fourth TCP field
        // is a keepalive), so a caller's value is discarded rather than acted
        // on — a bind that the interface says is UDP-only must not happen to
        // work for TCP and become relied upon.
        local_port_ = (protocol == Protocol::Udp) ? local_port : 0;
        peer_       = IpAddress{};
        last_error_.clear();
        // Cleared with `last_error_` and for the same reason: all three
        // describe the PREVIOUS attempt, and a fresh request must not inherit
        // its verdict or its byte count.
        denial_   = DenyReason::None;
        received_ = 0;
        state_    = TransportState::Resolving;
        log_debug("connect requested: {} {}:{} (resolution deferred to poll)",
                            protocol_text(protocol_), host_, port_);
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
    DenyReason         denial_reason() const override { return denial_; }

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
        bool        eof = false, failed = false, reset = false;
        std::string err;
        const std::size_t n = net::recv(sock_, buf, cap,
                                        /*stream=*/protocol_ == Protocol::Tcp, eof, failed,
                                        reset, err);
        if (failed) {
            if (reset && received_ != 0)
                note_peer_reset();
            else
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
        received_ += n;
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

    /// Take over a socket that is ALREADY CONNECTED — an inbound connection a
    /// listener accepted (GH #210). Only `SocketListener` below calls it.
    ///
    /// IT DOES NOT TOUCH THE OUTBOUND STATE MACHINE, and could not: it lands
    /// directly in `Connected`, which every outbound entry point already treats
    /// as busy. `begin_connect` refuses (its first test), `poll()` has nothing
    /// to do in that state, and `send`/`recv`/`close` are written against
    /// `Connected` and do not care how it was reached. So an adopted transport
    /// is an ordinary live transport to every existing caller, which is what
    /// lets the AT engine hold inbound and outbound connections in the same
    /// table without a second type.
    ///
    /// `host_` becomes the peer's numeric address because that is the only name
    /// an inbound connection has — nobody dialled a hostname — and every log
    /// line in this file reads `host_:port_` as "the other end".
    ///
    /// THE ADDRESS POLICY IS NOT CONSULTED, deliberately: the peer was not
    /// chosen by the guest, so `classify_address` has nothing to judge. What
    /// bounds who may reach this socket is the listener's BIND ADDRESS (see
    /// `make_socket_listener`), enforced by the kernel rather than here.
    void adopt_connected(net::NativeSocket s, const IpAddress& peer, std::uint16_t port) {
        release();
        sock_       = s;
        peer_       = normalize(peer);
        host_       = to_string(peer_);
        port_       = port;
        protocol_   = Protocol::Tcp;
        local_port_ = 0;
        last_error_.clear();
        denial_     = DenyReason::None;
        received_   = 0;
        state_      = TransportState::Connected;
        log_info("TCP connection ACCEPTED from {}:{}", host_, port_);
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

    /// A peer that had already SERVED US BYTES terminated the connection with
    /// a TCP RST instead of a FIN. Same outcome as `fail()` in every respect
    /// that any caller can observe — `Failed`, `last_error()` set, the socket
    /// dropped — but reported at WARN, and this is the one place in the module
    /// where the severity is an argument rather than a fact (GH #176).
    ///
    /// WHY IT IS NOT AN ERROR. `error` in this file means "an OS call failed
    /// and the connection could not do its job". A peer closing with
    /// `SO_LINGER(1,0)` — which is what the NextSync server does — makes its
    /// `close()` emit an RST, and the whole transfer has already succeeded by
    /// then: every byte reached the guest, `CLOSED` is still emitted, and the
    /// guest prints "All done". Calling that an error means the only red line
    /// in a perfect run says the run failed.
    ///
    /// WHAT THIS DOES **NOT** CLAIM, stated plainly because the honest answer
    /// is a narrow one. This layer CANNOT tell a deliberate linger-close from
    /// a mid-stream reset caused by a crashed server or a middlebox: the wire
    /// event is one RST either way, carrying no reason, and TCP has no
    /// "I meant it" bit. So the discrimination made here is the one that IS
    /// observable — whether the peer served anything at all before it reset:
    ///
    ///   * reset with NOTHING served (`received_ == 0`) is a failed exchange
    ///     however you read it — a rejecting server, a crash on accept, a
    ///     firewall — and stays `error`, loud, via `fail()`.
    ///   * reset AFTER data is the linger-close SHAPE, and is reported here.
    ///
    /// A genuine mid-stream reset therefore lands in the second bucket, and
    /// the line below is written so that it is still useful when it does: it
    /// names the reset, counts what was received, and says outright that
    /// anything the peer had not yet sent is gone. `warn` is above the
    /// module's default threshold and above jnext's, so it is emitted on every
    /// default run — the event is not hidden, it is merely no longer
    /// attributed to a failure this transport did not have.
    ///
    /// The count is meaningful because of an ordering guarantee, not by luck:
    /// the OS hands back everything already queued and reports the reset only
    /// once that queue is empty (see the note in `esp_socket_posix.cpp`), so
    /// `received_` is the whole of what the peer's TCP managed to deliver.
    void note_peer_reset() {
        log_warn("connection to {}:{} was RESET by the peer after {} byte(s) received "
                 "(TCP RST, not an orderly close) — everything it delivered has been "
                 "handed on; anything it had not yet sent is lost",
                 host_, port_, received_);
        release();
        last_error_ = "connection reset by the peer after " + std::to_string(received_) +
                      " byte(s) received";
        state_      = TransportState::Failed;
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
                //
                // WHICH PROPERTY IS LOAD-BEARING, stated because the handlers
                // below LOOK like they carry the guarantee and do not. `ok` is
                // initialised false above, and `ok = fn(...)` cannot complete
                // its assignment when the right-hand side throws — so a thrown
                // lookup is a FAILED lookup by construction, and the consumer's
                // `!job->ok` gate refuses it without ever examining the address
                // list. The `ok = false` / `out.clear()` in the handlers
                // RESTATE that; they do not establish it. Verified in review by
                // deleting both `out.clear()` calls: the suite stays at 142/142
                // and still connects nowhere. They are kept as defence in depth
                // because they cost nothing and they keep the two properties
                // independent — a later refactor that decouples the emptiness
                // check from the `ok` check must not be able to turn a
                // half-built list into a connect.
                std::vector<IpAddress> out;
                std::string            err;
                bool                   ok = false;
                try {
                    ok = fn ? fn(host, out, err)
                            : net::resolve(host, /*numeric_only=*/false, out, err);
                } catch (const std::exception& e) {
                    ok  = false;
                    out.clear();  // defence in depth, not the barrier — see above
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
            // The verdict is recorded as a VALUE, not only inside that string:
            // this is the half of the security gate a host has to be able to
            // render as a deliberate block rather than as "the network didn't
            // work" (GH #161), and no host should have to grep a message to
            // find that out.
            denial_ = reason;
            release();
            state_  = TransportState::Failed;
            return;
        }
        peer_ = normalize(chosen);

        sock_ = net::open_nonblocking(chosen.family, protocol_, local_port_, err);
        if (sock_ == net::kInvalidSocket) {
            fail("cannot create socket: " + err);
            return;
        }

        // UDP takes the `Connected` arm below on the spot: `::connect()` on a
        // datagram socket only records the peer, so it returns 0 and the state
        // never passes through `Connecting`. That is why an `AT+CIPSTART="UDP"`
        // is answered on the very first poll after dispatch.
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
        log_info("{} connection OPENED to {}:{} (host '{}')", protocol_text(protocol_),
                           to_string(peer_), port_, host_);
    }

    AddressPolicy     policy_;
    ResolveFn         resolver_;  ///< null = the platform's getaddrinfo
    TransportState    state_ = TransportState::Idle;
    net::NativeSocket sock_  = net::kInvalidSocket;
    std::string       host_;
    std::uint16_t     port_ = 0;
    Protocol          protocol_   = Protocol::Tcp;
    /// UDP only; 0 = let the OS pick. See `open_nonblocking`.
    std::uint16_t     local_port_ = 0;
    IpAddress         peer_;
    std::string       last_error_;
    /// Non-`None` only while `state_ == Failed` because the address policy
    /// refused the target. See `EspTransport::denial_reason()`.
    DenyReason        denial_ = DenyReason::None;
    /// Bytes handed to the consumer on the CURRENT connection. Its only job is
    /// to tell a peer that reset after serving from one that reset instead of
    /// serving — see `note_peer_reset()`.
    std::uint64_t     received_ = 0;
    /// Non-null only between starting a lookup and consuming its result. The
    /// resolver thread holds the other reference.
    std::shared_ptr<ResolveJob> job_;
};

/// The real listener (GH #210). Everything OS-specific is two calls into the
/// platform twins; what lives here is the one-pending-connection rule, the
/// fault handling and the tracing.
class SocketListener final : public EspListener {
public:
    explicit SocketListener(const IpAddress& bind_address) : bind_(bind_address) {}
    ~SocketListener() override { drop_socket(); }

    bool open(std::uint16_t port) override {
        close();
        last_error_.clear();

        std::string   err;
        std::string   hardening_warning;
        std::uint16_t bound = 0;
        const net::NativeSocket s =
            net::open_listener(bind_, port, bound, hardening_warning, err);
        // Emitted whether the bind then succeeded or not: a hardening option
        // that would not apply is worth saying either way, and this is the one
        // place in the module where a socket OPTION — not a socket operation —
        // gets a line of its own.
        if (!hardening_warning.empty())
            log_warn("listening socket for {}:{} is not fully hardened — {}",
                     to_string(bind_), port, hardening_warning);
        if (s == net::kInvalidSocket) {
            last_error_ = err;
            // WARN, matching the outbound refusal: a listening socket the user
            // asked for and did not get is exactly as user-visible an event as
            // a connection that was refused, and it must never be inferred from
            // the absence of a line.
            log_warn("cannot listen on {}:{} — {}", to_string(bind_), port, err);
            return false;
        }
        sock_ = s;
        port_ = bound;
        log_info("listening for inbound connections on {}:{}", to_string(bind_), port_);
        return true;
    }

    void close() override {
        if (sock_ == net::kInvalidSocket) return;
        const std::uint16_t was = port_;
        drop_socket();
        log_info("stopped listening on {}:{}", to_string(bind_), was);
    }

    bool               listening() const override  { return sock_ != net::kInvalidSocket; }
    std::uint16_t      port() const override       { return port_; }
    const std::string& last_error() const override { return last_error_; }

    void poll() override {
        if (sock_ == net::kInvalidSocket) return;
        // ONE PENDING CONNECTION AT A TIME, and the bound is the point rather
        // than a shortcut. `accept()` is called from the very next engine
        // service pass, so a depth of one never throttles a real client — but
        // without it, an unattended listener would allocate a transport per
        // inbound SYN for as long as anything kept connecting, while the guest
        // was not looking. Everything beyond it waits in the kernel's listen
        // backlog, which is what a backlog is for.
        if (pending_) return;

        IpAddress     peer;
        std::uint16_t peer_port = 0;
        bool          failed    = false;
        std::string   err;
        const net::NativeSocket s =
            net::accept_nonblocking(sock_, peer, peer_port, failed, err);
        if (s == net::kInvalidSocket) {
            if (!failed) {
                log_trace("poll: nothing waiting on {}:{}", to_string(bind_), port_);
                return;
            }
            // The LISTENING socket is broken, not a peer — a peer that vanished
            // mid-handshake comes back as "nothing pending" from the twins. So
            // stop rather than spin: the guest can re-issue AT+CIPSERVER, and a
            // listener that kept failing every poll forever would be the worse
            // of the two silences.
            last_error_ = err;
            log_error("accept failed on {}:{} — {}; no longer listening",
                      to_string(bind_), port_, err);
            drop_socket();
            return;
        }

        // Created here rather than by the factory because it is born connected:
        // there is no policy to apply and no resolver to give it.
        auto adopted = std::unique_ptr<SocketTransport>(
            new SocketTransport(AddressPolicy{}, nullptr));
        adopted->adopt_connected(s, peer, peer_port);
        pending_ = std::move(adopted);
    }

    std::unique_ptr<EspTransport> accept() override { return std::move(pending_); }

private:
    /// Release the socket without saying anything. `close()` is this plus the
    /// log line; the fault path and the destructor want the release alone.
    void drop_socket() {
        if (sock_ != net::kInvalidSocket) {
            net::close(sock_);
            sock_ = net::kInvalidSocket;
        }
        port_ = 0;
        // An accepted-but-never-taken connection has no other owner, so
        // dropping it here is what closes its socket.
        pending_.reset();
    }

    const IpAddress               bind_;
    net::NativeSocket             sock_ = net::kInvalidSocket;
    std::uint16_t                 port_ = 0;
    std::string                   last_error_;
    std::unique_ptr<EspTransport> pending_;
};

}  // namespace

const char* protocol_text(Protocol p) {
    switch (p) {
        case Protocol::Tcp: return "TCP";
        case Protocol::Udp: return "UDP";
    }
    return "unknown";
}

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

bool parse_ip(const std::string& text, IpAddress& out) {
    std::string err;
    // Windows needs WSAStartup before getaddrinfo, and this is reachable before
    // any transport has been built (a host validating a configured address).
    if (!net::init(err)) return false;

    std::vector<IpAddress> found;
    if (!net::resolve(text, /*numeric_only=*/true, found, err) || found.empty()) return false;
    out = found.front();
    return true;
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

std::unique_ptr<EspListener> make_socket_listener(const IpAddress& bind_address) {
    std::string err;
    if (!net::init(err)) {
        log_error("network initialisation failed: {}", err);
        return nullptr;
    }
    return std::unique_ptr<EspListener>(new SocketListener(bind_address));
}

}  // namespace esp
