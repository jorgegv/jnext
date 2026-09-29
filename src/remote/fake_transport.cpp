#include "remote/fake_transport.h"

#include <algorithm>

namespace jnext {
namespace remote {

struct FakePipe {
    std::deque<std::uint8_t> to_server;   // the client said, the server has not read
    std::deque<std::uint8_t> to_client;   // the server wrote, the client has not taken
    bool        client_closed = false;
    bool        server_closed = false;
    bool        broken        = false;
    std::size_t window        = std::size_t{64} << 20;
    std::size_t recv_chunk    = static_cast<std::size_t>(-1);
};

// ── FakePeer ───────────────────────────────────────────────────────────────

void FakePeer::send(const std::string& bytes) {
    if (pipe_->client_closed || pipe_->server_closed || pipe_->broken) return;
    pipe_->to_server.insert(pipe_->to_server.end(), bytes.begin(), bytes.end());
}

std::string FakePeer::take(std::size_t max) {
    const std::size_t n = std::min(max, pipe_->to_client.size());
    std::string out(pipe_->to_client.begin(),
                    pipe_->to_client.begin() + static_cast<std::ptrdiff_t>(n));
    pipe_->to_client.erase(pipe_->to_client.begin(),
                           pipe_->to_client.begin() + static_cast<std::ptrdiff_t>(n));
    return out;
}

std::size_t FakePeer::pending() const { return pipe_->to_client.size(); }

void FakePeer::close() { pipe_->client_closed = true; }

void FakePeer::reset() { pipe_->broken = true; }

bool FakePeer::closed_by_server() const { return pipe_->server_closed; }

void FakePeer::set_window(std::size_t bytes) { pipe_->window = bytes; }

void FakePeer::set_recv_chunk(std::size_t bytes) { pipe_->recv_chunk = bytes; }

// ── FakeTransport ──────────────────────────────────────────────────────────

FakeTransport::FakeTransport(std::shared_ptr<FakePipe> pipe, const esp::IpAddress& from)
    : pipe_(std::move(pipe)), from_(from) {}

bool FakeTransport::begin_connect(const std::string&, std::uint16_t, esp::Protocol,
                                  std::uint16_t) {
    return false;
}

std::size_t FakeTransport::send(const std::uint8_t* data, std::size_t len) {
    if (state_ != esp::TransportState::Connected) return 0;
    if (pipe_->broken) {
        state_      = esp::TransportState::Failed;
        last_error_ = "connection reset by peer";
        return 0;
    }
    // A client that has hung up is answered, on a real socket, with a reset;
    // the byte stream towards it is over either way.
    if (pipe_->client_closed) {
        state_ = esp::TransportState::Closed;
        return 0;
    }
    const std::size_t room =
        pipe_->window > pipe_->to_client.size() ? pipe_->window - pipe_->to_client.size() : 0;
    const std::size_t n = std::min(len, room);
    pipe_->to_client.insert(pipe_->to_client.end(), data, data + n);
    return n;
}

std::size_t FakeTransport::recv(std::uint8_t* buf, std::size_t cap) {
    if (state_ != esp::TransportState::Connected) return 0;
    if (pipe_->broken) {
        state_      = esp::TransportState::Failed;
        last_error_ = "connection reset by peer";
        return 0;
    }
    const std::size_t n = std::min({cap, pipe_->recv_chunk, pipe_->to_server.size()});
    std::copy(pipe_->to_server.begin(),
              pipe_->to_server.begin() + static_cast<std::ptrdiff_t>(n), buf);
    pipe_->to_server.erase(pipe_->to_server.begin(),
                           pipe_->to_server.begin() + static_cast<std::ptrdiff_t>(n));
    // End of stream only once everything the client sent has been read, as
    // TCP delivers a FIN after the data in front of it.
    if (n == 0 && pipe_->client_closed) state_ = esp::TransportState::Closed;
    return n;
}

void FakeTransport::close() {
    pipe_->server_closed = true;
    state_               = esp::TransportState::Closed;
}

// ── FakeListener ───────────────────────────────────────────────────────────

bool FakeListener::open(std::uint16_t port) {
    close();
    if (!refuse_.empty()) {
        last_error_ = refuse_;
        refuse_.clear();
        return false;
    }
    last_error_.clear();
    listening_ = true;
    port_      = port ? port : kEphemeralPort;
    return true;
}

void FakeListener::close() {
    listening_ = false;
    port_      = 0;
    // Arrived but never accepted: their clients see the connection close.
    for (auto& t : queue_) t->close();
    queue_.clear();
}

std::unique_ptr<esp::EspTransport> FakeListener::accept() {
    if (!listening_ || queue_.empty()) return nullptr;
    std::unique_ptr<esp::EspTransport> t = std::move(queue_.front());
    queue_.pop_front();
    return t;
}

std::shared_ptr<FakePeer> FakeListener::connect(const esp::IpAddress& from) {
    if (!listening_) return nullptr;
    auto pipe = std::make_shared<FakePipe>();
    queue_.push_back(std::unique_ptr<esp::EspTransport>(new FakeTransport(pipe, from)));
    return std::make_shared<FakePeer>(pipe);
}

void FakeListener::refuse_next_open(const std::string& why) { refuse_ = why; }

void FakeListener::fail(const std::string& why) {
    close();
    last_error_ = why;
}

}  // namespace remote
}  // namespace jnext
