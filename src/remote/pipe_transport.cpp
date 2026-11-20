#include "remote/pipe_transport.h"

#include <algorithm>
#include <deque>
#include <utility>

namespace jnext {
namespace remote {

namespace {

// One session's shared state: the transport (jnext's end) and the channel (the
// host's end) both hold it.
struct Session {
    std::deque<std::uint8_t>  in;            // the client sent, jnext has not read
    std::vector<std::uint8_t> out;           // jnext wrote, the host has not taken
    bool                      host_closed = false;   // the client left
    bool                      jnext_closed = false;  // jnext ended it
};

const esp::IpAddress& loopback() {
    static const esp::IpAddress ip = esp::ipv4(127, 0, 0, 1);
    return ip;
}

}  // namespace

// The channel's state, shared with the listener.
struct PipeChannel::State {
    std::size_t                                inbound_cap = PipeChannel::kInboundCap;
    bool                                       listening = false;
    std::shared_ptr<Session>                   cur;     // the session the host sees
    std::deque<std::unique_ptr<esp::EspTransport>> queue;  // parked for accept()
};

namespace {

class PipeTransport final : public esp::EspTransport {
public:
    explicit PipeTransport(std::shared_ptr<Session> s) : s_(std::move(s)) {}
    // A transport destroyed without close() (a Server torn down) still ends
    // its session.
    ~PipeTransport() override { close(); }

    bool begin_connect(const std::string&, std::uint16_t, esp::Protocol,
                       std::uint16_t) override {
        return false;
    }
    void                  poll() override {}
    esp::TransportState   state() const override { return state_; }
    const std::string&    last_error() const override { return last_error_; }
    const esp::IpAddress& peer_address() const override { return loopback(); }

    std::size_t send(const std::uint8_t* data, std::size_t len) override {
        if (state_ != esp::TransportState::Connected) return 0;
        // A client that has left: the byte stream towards it is over.
        if (s_->host_closed) {
            state_ = esp::TransportState::Closed;
            return 0;
        }
        const std::size_t room = PipeChannel::kOutboundWindow > s_->out.size()
                                     ? PipeChannel::kOutboundWindow - s_->out.size()
                                     : 0;
        const std::size_t n = std::min(len, room);
        s_->out.insert(s_->out.end(), data, data + n);
        return n;
    }

    std::size_t recv(std::uint8_t* buf, std::size_t cap) override {
        if (state_ != esp::TransportState::Connected) return 0;
        const std::size_t n = std::min(cap, s_->in.size());
        std::copy(s_->in.begin(), s_->in.begin() + static_cast<std::ptrdiff_t>(n), buf);
        s_->in.erase(s_->in.begin(), s_->in.begin() + static_cast<std::ptrdiff_t>(n));
        // End of stream only once everything the client sent has been read.
        if (n == 0 && s_->host_closed) state_ = esp::TransportState::Closed;
        return n;
    }

    void close() override {
        s_->jnext_closed = true;
        state_           = esp::TransportState::Closed;
    }

private:
    std::shared_ptr<Session> s_;
    esp::TransportState      state_ = esp::TransportState::Connected;
    std::string              last_error_;
};

class PipeListener final : public esp::EspListener {
public:
    explicit PipeListener(std::shared_ptr<PipeChannel::State> st) : st_(std::move(st)) {}
    ~PipeListener() override { close(); }

    bool open(std::uint16_t) override {
        st_->listening = true;
        return true;
    }
    void close() override {
        st_->listening = false;
        // Parked, never accepted: their sessions end.
        for (auto& t : st_->queue) t->close();
        st_->queue.clear();
    }
    bool               listening() const override { return st_->listening; }
    std::uint16_t      port() const override { return 0; }
    const std::string& last_error() const override { return last_error_; }
    void               poll() override {}
    std::unique_ptr<esp::EspTransport> accept() override {
        if (!st_->listening || st_->queue.empty()) return nullptr;
        std::unique_ptr<esp::EspTransport> t = std::move(st_->queue.front());
        st_->queue.pop_front();
        return t;
    }

private:
    std::shared_ptr<PipeChannel::State> st_;
    std::string                         last_error_;
};

}  // namespace

PipeChannel::PipeChannel(std::size_t inbound_cap) : st_(std::make_shared<State>()) {
    st_->inbound_cap = inbound_cap;
}

PipeChannel::~PipeChannel() = default;

std::unique_ptr<esp::EspListener> PipeChannel::make_listener() {
    return std::unique_ptr<esp::EspListener>(new PipeListener(st_));
}

PipeResult PipeChannel::host_open() {
    if (!st_->listening) return PipeResult::NoChannel;
    // Open, or ended and not yet reported to the host.
    if (st_->cur) return PipeResult::WrongState;
    st_->cur = std::make_shared<Session>();
    st_->queue.push_back(std::unique_ptr<esp::EspTransport>(new PipeTransport(st_->cur)));
    return PipeResult::Ok;
}

PipeResult PipeChannel::host_send(const std::uint8_t* data, std::size_t len) {
    if (!st_->listening) return PipeResult::NoChannel;
    if (!st_->cur) return PipeResult::WrongState;
    Session& s = *st_->cur;
    if (s.jnext_closed) return PipeResult::Ok;  // the host has not been told yet: dropped
    const std::size_t cap = st_->inbound_cap;
    if (len > cap - std::min(cap, s.in.size())) return PipeResult::Full;
    s.in.insert(s.in.end(), data, data + len);
    return PipeResult::Ok;
}

PipeResult PipeChannel::host_close() {
    if (!st_->listening) return PipeResult::NoChannel;
    if (!st_->cur) return PipeResult::WrongState;
    // The old session's unread output is unreachable from here on: take_output() reads `cur`.
    st_->cur->host_closed = true;
    st_->cur.reset();
    return PipeResult::Ok;
}

std::vector<std::uint8_t> PipeChannel::take_output() {
    std::vector<std::uint8_t> out;
    if (st_->cur) out.swap(st_->cur->out);
    return out;
}

bool PipeChannel::take_ended() {
    if (!st_->cur || !st_->cur->jnext_closed || !st_->cur->out.empty()) return false;
    st_->cur.reset();
    return true;
}

}  // namespace remote
}  // namespace jnext
