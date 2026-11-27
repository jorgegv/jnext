#include "remote/tcp_link.h"

#include <algorithm>
#include <thread>

namespace jnext {
namespace remote {

namespace {
constexpr std::size_t IN_BUFFER_CAP = std::size_t{1} << 20;
constexpr std::size_t RECV_CHUNK    = std::size_t{16} << 10;
}  // namespace

esp::AddressPolicy client_policy() {
    esp::AddressPolicy p;
    p.deny_loopback = false;
    return p;
}

TcpLink::TcpLink(std::string host, std::uint16_t port, TcpLinkOptions opt)
    : host_(std::move(host)), port_(port), opt_(std::move(opt)) {}

TcpLink::~TcpLink() { close(); }

std::string TcpLink::describe() const {
    const bool v6 = host_.find(':') != std::string::npos;
    return "tcp:" + (v6 ? "[" + host_ + "]" : host_) + ":" + std::to_string(port_);
}

bool TcpLink::open() {
    if (state_ != LinkState::Idle) {
        error_ = "link already opened";
        return false;
    }
    t_ = esp::make_socket_transport(client_policy(), opt_.resolver);
    if (!t_ || !t_->begin_connect(host_, port_)) {
        error_ = "connect to " + describe() + " rejected";
        state_ = LinkState::Failed;
        return false;
    }
    opened_ = std::chrono::steady_clock::now();
    state_  = LinkState::Connecting;
    return true;
}

LinkState TcpLink::state() const {
    // What the peer sent before it closed stays readable: Closed only once drained.
    if (state_ == LinkState::Open && peer_closed_ && in_off_ >= in_.size())
        return LinkState::Closed;
    return state_;
}

bool TcpLink::pass() {
    bool moved = false;
    if (!t_ || state_ == LinkState::Idle || state_ == LinkState::Closed ||
        state_ == LinkState::Failed)
        return false;
    if (!peer_closed_) t_->poll();
    const esp::TransportState ts = t_->state();
    switch (ts) {
    case esp::TransportState::Resolving:
    case esp::TransportState::Connecting: {
        const auto waited = std::chrono::steady_clock::now() - opened_;
        if (waited >= opt_.connect_timeout) {
            t_->close();
            error_ = "connect to " + describe() + " timed out after " +
                     std::to_string(opt_.connect_timeout.count()) + " ms";
            state_ = LinkState::Failed;
            return true;
        }
        return false;
    }
    case esp::TransportState::Failed: {
        const esp::DenyReason dr = t_->denial_reason();
        error_ = dr != esp::DenyReason::None
                     ? "connect to " + describe() + " refused by the address policy: " +
                           esp::deny_reason_text(dr)
                     : "connect to " + describe() + " failed: " + t_->last_error();
        state_ = LinkState::Failed;
        return true;
    }
    case esp::TransportState::Idle:
        return false;
    case esp::TransportState::Connected:
    case esp::TransportState::Closed:
        break;
    }
    if (state_ == LinkState::Connecting) {
        state_ = LinkState::Open;
        moved  = true;
    }
    // Flush the out queue.
    while (out_off_ < out_.size() && ts == esp::TransportState::Connected && !peer_closed_) {
        const std::size_t k = t_->send(out_.data() + out_off_, out_.size() - out_off_);
        if (k == 0) break;
        out_off_ += k;
        moved = true;
    }
    if (out_off_ >= out_.size()) {
        out_.clear();
        out_off_ = 0;
    }
    // Pull what arrived (also after a peer close: the kernel still holds bytes before the FIN).
    std::uint8_t buf[RECV_CHUNK];
    while (in_.size() - in_off_ < IN_BUFFER_CAP && !peer_closed_) {
        const std::size_t k = t_->recv(buf, sizeof(buf));
        if (k == 0) break;
        in_.insert(in_.end(), buf, buf + k);
        moved = true;
    }
    const esp::TransportState after = t_->state();
    if (after == esp::TransportState::Failed) {
        error_ = "connection to " + describe() + " failed: " + t_->last_error();
        state_ = LinkState::Failed;
        return true;
    }
    if (after == esp::TransportState::Closed && !peer_closed_) {
        peer_closed_ = true;
        moved        = true;
    }
    return moved;
}

void TcpLink::poll(int wait_ms) {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const bool moved = pass();
        if (moved || wait_ms <= 0) return;
        if (state_ == LinkState::Idle || state_ == LinkState::Closed ||
            state_ == LinkState::Failed)
            return;
        if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(wait_ms)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::size_t TcpLink::read(std::uint8_t* buf, std::size_t cap) {
    const std::size_t k = std::min(cap, in_.size() - in_off_);
    std::copy(in_.begin() + static_cast<std::ptrdiff_t>(in_off_),
              in_.begin() + static_cast<std::ptrdiff_t>(in_off_ + k), buf);
    in_off_ += k;
    if (in_off_ >= in_.size()) {
        in_.clear();
        in_off_ = 0;
    }
    return k;
}

bool TcpLink::write(const std::uint8_t* p, std::size_t n) {
    if (state_ != LinkState::Open && state_ != LinkState::Connecting) return false;
    if (peer_closed_) return false;
    if (out_.size() - out_off_ + n > opt_.max_output) {
        error_ = "output backlog to " + describe() + " exceeds " +
                 std::to_string(opt_.max_output) + " bytes";
        state_ = LinkState::Failed;
        if (t_) t_->close();
        return false;
    }
    out_.insert(out_.end(), p, p + n);
    return true;
}

void TcpLink::close() {
    if (t_) t_->close();
    if (state_ != LinkState::Failed) state_ = LinkState::Closed;
}

}  // namespace remote
}  // namespace jnext
