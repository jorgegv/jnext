#include "remote/transport.h"

#include <algorithm>
#include <thread>
#include <utility>

#include "core/log.h"

namespace jnext {
namespace remote {

using jnext::dbg::ServiceStep;
using Clock = std::chrono::steady_clock;

namespace {

// One `recv` call's worth. The loop around it stops at `max_input`, so this
// only sets the granularity, never the bound.
constexpr std::size_t kReadChunk = 16 * 1024;

// How many connections one pass takes from the listener. The esp listener
// parks at most one at a time, so this is a bound on a pass, not a queue depth:
// a peer that kept connecting cannot keep a pass going.
constexpr int kAcceptsPerPass = 8;

// Drop the consumed prefix of a buffer once it is at least half the buffer, so
// a byte-at-a-time consumer does not make every read or send O(buffer).
void compact(std::vector<std::uint8_t>& buf, std::size_t& off) {
    if (off == buf.size()) {
        buf.clear();
        off = 0;
    } else if (off > 0 && off >= buf.size() / 2) {
        buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(off));
        off = 0;
    }
}

std::string address_label(const esp::IpAddress& ip) {
    const std::string text = esp::to_string(ip);
    return ip.family == esp::IpFamily::V6 ? "[" + text + "]" : text;
}

}  // namespace

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

Connection::Connection(std::unique_ptr<esp::EspTransport> transport, const ServerConfig& cfg)
    : t_(std::move(transport)), cfg_(cfg), peer_(esp::to_string(t_->peer_address())) {}

Connection::~Connection() { shut(); }

std::size_t Connection::read(std::uint8_t* buf, std::size_t cap) {
    const std::size_t n = std::min(cap, available());
    if (n == 0) return 0;
    std::copy(in_.begin() + static_cast<std::ptrdiff_t>(in_off_),
              in_.begin() + static_cast<std::ptrdiff_t>(in_off_ + n), buf);
    in_off_ += n;
    compact(in_, in_off_);
    return n;
}

void Connection::write(const std::uint8_t* data, std::size_t len) {
    // Nothing more reaches a peer that is gone or was given up on.
    if (gone_ || overflow_ || len == 0) return;
    if (pending_output() + len > cfg_.max_output) {
        // THE PEER HAS STOPPED READING. Buffering without bound is how a stuck
        // debugger client becomes a host out of memory; dropping bytes
        // silently is how it becomes a corrupt stream. Neither: the session
        // ends, loudly, and the adapter gets `on_disconnect()`.
        Log::debugger()->warn(
            "{}: client {} is not reading its replies ({} bytes pending, limit {}) — "
            "disconnecting",
            cfg_.name, peer_, pending_output() + len, cfg_.max_output);
        overflow_ = true;
        out_.clear();
        out_off_ = 0;
        return;
    }
    // QUEUED, NOT SENT: the Server pushes once after each adapter callback, so
    // a reply written in pieces leaves as one send rather than one per piece.
    out_.insert(out_.end(), data, data + len);
}

void Connection::write(const std::string& text) {
    write(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

void Connection::close() { closing_ = true; }

bool Connection::pull() {
    if (!t_ || gone_) return false;
    t_->poll();
    bool          got = false;
    std::uint8_t  buf[kReadChunk];
    // Bounded: every iteration either stores at least one byte towards the
    // `max_input` ceiling or leaves the loop.
    while (available() < cfg_.max_input) {
        const std::size_t want = std::min(kReadChunk, cfg_.max_input - available());
        const std::size_t n    = t_->recv(buf, want);
        if (n == 0) {
            // 0 is "nothing now" only while the transport is still connected;
            // the seam reports a peer close as `Closed` and an error as
            // `Failed`, and both also return 0.
            if (t_->state() != esp::TransportState::Connected) gone_ = true;
            break;
        }
        in_.insert(in_.end(), buf, buf + n);
        got = true;
    }
    return got;
}

void Connection::push() {
    if (!t_) return;
    while (pending_output() > 0 && !gone_) {
        const std::size_t n = t_->send(out_.data() + out_off_, pending_output());
        if (n == 0) {
            // A full send buffer is normal; a transport that left `Connected`
            // is a peer that is not coming back.
            if (t_->state() != esp::TransportState::Connected) gone_ = true;
            break;
        }
        out_off_ += n;
    }
    compact(out_, out_off_);
}

void Connection::discard_input() {
    pull();
    in_.clear();
    in_off_ = 0;
}

void Connection::shut() {
    if (t_) t_->close();
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

Server::Server(ServerConfig cfg, Protocol& protocol)
    : cfg_(std::move(cfg)), protocol_(protocol) {}

Server::~Server() {
    // Deliberately no protocol call (see the header). The unique_ptrs close
    // the sockets.
    lingering_.clear();
    active_.reset();
    if (listener_) listener_->close();
}

bool Server::open(const std::string& bind_address, std::uint16_t port) {
    esp::IpAddress ip;
    if (!esp::parse_ip(bind_address, ip)) {
        stop();
        last_error_ = "'" + bind_address + "' is not a numeric IP address";
        Log::debugger()->error("{}: cannot listen on {}:{} — {}", cfg_.name, bind_address,
                               port, last_error_);
        return false;
    }
    std::unique_ptr<esp::EspListener> listener = esp::make_socket_listener(ip);
    if (!listener) {
        stop();
        last_error_ = "network initialisation failed";
        Log::debugger()->error("{}: cannot listen on {}:{} — {}", cfg_.name,
                               address_label(ip), port, last_error_);
        return false;
    }
    return open(std::move(listener), address_label(ip), port);
}

bool Server::open(std::unique_ptr<esp::EspListener> listener, const std::string& label,
                  std::uint16_t port) {
    stop();
    last_error_.clear();
    listener_failed_ = false;
    if (!listener || !listener->open(port)) {
        last_error_ = listener ? listener->last_error() : std::string("no listener");
        Log::debugger()->error("{}: cannot listen on {}:{} — {}", cfg_.name, label, port,
                               last_error_);
        return false;
    }
    listener_   = std::move(listener);
    label_      = label;
    bound_port_ = listener_->port();
    // THE BOUND PORT, not the requested one: with `port` 0 the OS chose it, and
    // this line is how a regression row (and a user) learns which.
    Log::debugger()->info("{}: listening on {}:{}", cfg_.name, label_, bound_port_);
    return true;
}

void Server::stop() {
    if (listener_) {
        listener_->close();
        listener_.reset();
        Log::debugger()->info("{}: stopped listening on {}:{}", cfg_.name, label_, bound_port_);
        bound_port_ = 0;
    }
    if (active_) {
        active_->push();  // best effort: a shutdown does not linger
        retire();
    }
    lingering_.clear();
}

bool Server::listening() const { return listener_ && listener_->listening(); }

std::uint16_t Server::port() const { return listening() ? listener_->port() : 0; }

ServiceStep Server::service_once(int wait_ms) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(wait_ms, 0));
    for (;;) {
        const Pass p = pass();
        if (p.step == ServiceStep::Serviced || p.ended) return p.step;
        const auto now = Clock::now();
        if (now >= deadline) return ServiceStep::Idle;
        // THE ONLY WAIT IN THIS MODULE. The esp seam has no readiness wait by
        // design (its `poll()` takes no timeout), so a bounded wait is a short
        // sleep between non-blocking passes: a command that arrives mid-wait
        // is answered within ~1 ms, and the loop never outlives `wait_ms`.
        std::this_thread::sleep_for(
            std::min<Clock::duration>(std::chrono::milliseconds(1), deadline - now));
    }
}

void Server::flush_notifications() {
    if (active_ && !active_->closing_) protocol_.on_notify(*active_);
    if (active_) {
        active_->push();
        // `Serviced`, not `Idle`: no `on_service` ran here, so a peer that has
        // gone may still have commands buffered, and those are retired only
        // after the adapter has said it has none (the next pass).
        settle(ServiceStep::Serviced);
    }
    reap();
}

Server::Pass Server::pass() {
    Pass p;
    const auto serve = [&] {
        active_->pull();
        if (!active_->closing_ && !active_->overflow_) p.step = protocol_.on_service(*active_);
        active_->push();
        if (settle(p.step)) p.ended = true;
    };
    // 1. THE CLIENT ALREADY CONNECTED GOES FIRST (GH #12). A client that
    //    reconnects hangs up and dials again in one breath, so both reach the
    //    same pass. Accepting first judged the new connection against a
    //    session that had in fact ended and refused it as "a second client";
    //    served first, the hang-up is seen and the session retired, and the
    //    redial is admitted below. Found by the dezogif_ng conformance suite,
    //    which opens a fresh connection per check: every other one was refused.
    const bool had_client = active_ != nullptr;
    if (active_) serve();
    // 2. Accept — unless the client still here has HUNG UP. Its session is
    //    ending (a command followed at once by a close is still executed,
    //    `settle`, so a hung-up client can outlive this pass), and a redial
    //    waits in the listener's queue for it rather than being refused by it.
    if (listener_) {
        for (int i = 0; i < kAcceptsPerPass && listener_->listening(); ++i) {
            listener_->poll();
            // THE LOOK IS TAKEN HERE, after the listener has parked whatever
            // arrived and before taking it — not only after (1). A client that
            // got its reply in (1) can hang up and redial before this point —
            // it is another process, and this pass may be preempted — and its
            // redial reached the kernel AFTER its hang-up did. So a pull made
            // now sees the hang-up whenever there is a redial to judge, and
            // the redial stays parked in the listener until the old session
            // is retired, instead of being refused by it. (A hang-up already
            // seen in (1) is caught by the same test.)
            if (active_) {
                active_->pull();
                if (active_->gone_) break;
            }
            std::unique_ptr<esp::EspTransport> t = listener_->accept();
            if (!t) break;
            admit(std::move(t));
        }
    }
    if (listener_) {
        // `listener_` is only ever held after a successful `open()`, so one
        // that is not listening now has FAILED (the seam stops rather than
        // spins). Said once: a line per pass would bury everything else.
        if (!listener_->listening() && !listener_failed_) {
            listener_failed_ = true;
            Log::debugger()->error("{}: listener on {}:{} failed — {}; no longer listening",
                                   cfg_.name, label_, bound_port_, listener_->last_error());
        }
    }
    // 3. A client admitted in THIS pass is served in it — its greeting, and a
    //    command that came with its connection — unless the session before it
    //    already ran a command here (AT MOST ONE per pass): then only the
    //    greeting goes out now.
    if (active_ && (!had_client || p.ended)) {
        if (p.step == ServiceStep::Idle) serve();
        else active_->push();
    }
    reap();
    return p;
}

void Server::admit(std::unique_ptr<esp::EspTransport> t) {
    auto c = std::unique_ptr<Connection>(new Connection(std::move(t), cfg_));
    if (active_) {
        // ONE CLIENT PER LISTENER (all three designs). What the second one is
        // told is the adapter's; that it is turned away, and that it is said
        // at warn, is the same everywhere.
        Log::debugger()->warn("{}: refused a connection from {} — a client is already "
                              "connected (one at a time)",
                              cfg_.name, c->peer());
        if (!cfg_.busy_reply.empty()) c->write(cfg_.busy_reply);
        c->close();
        c->linger_until_ = Clock::now() + std::chrono::milliseconds(cfg_.linger_ms);
        lingering_.push_back(std::move(c));
        return;
    }
    Log::debugger()->info("{}: client connected from {}", cfg_.name, c->peer());
    active_ = std::move(c);
    // Its greeting goes out with the rest of this pass (`pass()` pushes).
    protocol_.on_connect(*active_);
}

bool Server::settle(ServiceStep last_step) {
    if (!active_) return false;
    const Connection& c = *active_;
    // A peer that has gone is kept until the adapter has had every byte it
    // sent: a command followed at once by a close is still executed.
    const bool done = c.closing_ || c.overflow_ || (c.gone_ && last_step == ServiceStep::Idle);
    if (done) retire();
    return done;
}

void Server::retire() {
    std::unique_ptr<Connection> c = std::move(active_);
    Log::debugger()->info("{}: client {} disconnected{}", cfg_.name, c->peer(),
                          c->closing_ ? " (closed by the server)" : "");
    protocol_.on_disconnect();
    // A session the ADAPTER ended may still owe the peer bytes; it gets
    // `linger_ms` to deliver them. A peer that is gone or not reading gets
    // nothing more.
    if (c->closing_ && !c->gone_ && !c->overflow_ && c->pending_output() > 0) {
        c->linger_until_ = Clock::now() + std::chrono::milliseconds(cfg_.linger_ms);
        lingering_.push_back(std::move(c));
    }
    // Otherwise `c` goes out of scope here and its socket closes.
}

void Server::reap() {
    const auto now = Clock::now();
    for (std::size_t i = 0; i < lingering_.size();) {
        Connection& c = *lingering_[i];
        // Read and discard: closing a socket with unread input makes the
        // kernel send a reset, which can cost the peer the reply it was owed.
        c.discard_input();
        c.push();
        const bool delivered = c.pending_output() == 0;
        if (delivered || c.gone_ || now >= c.linger_until_) {
            if (!delivered && !c.gone_)
                Log::debugger()->warn("{}: dropped {} undelivered bytes to {} after {} ms",
                                      cfg_.name, c.pending_output(), c.peer(),
                                      cfg_.linger_ms);
            lingering_.erase(lingering_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

}  // namespace remote
}  // namespace jnext
