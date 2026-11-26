#include "remote/memory_link.h"

#include <algorithm>

namespace jnext {
namespace remote {

MemoryLink::MemoryLink(FakeListener& listener, LinkKind impersonated, std::string label)
    : listener_(listener), kind_(impersonated), label_(std::move(label)) {}

bool MemoryLink::open() {
    peer_ = listener_.connect();
    if (!peer_) {
        error_ = "connection refused";
        state_ = LinkState::Failed;
        return false;
    }
    state_ = LinkState::Open;
    return true;
}

LinkState MemoryLink::state() const {
    // What the server wrote before closing stays readable: Closed only once drained.
    if (state_ == LinkState::Open && peer_ && peer_->closed_by_server() && peer_->pending() == 0)
        return LinkState::Closed;
    return state_;
}

std::size_t MemoryLink::read(std::uint8_t* buf, std::size_t cap) {
    if (!peer_) return 0;
    const std::string s = peer_->take(cap);
    std::copy(s.begin(), s.end(), buf);
    return s.size();
}

bool MemoryLink::write(const std::uint8_t* p, std::size_t n) {
    if (state_ != LinkState::Open || !peer_ || peer_->closed_by_server()) return false;
    peer_->send(std::string(reinterpret_cast<const char*>(p), n));
    return true;
}

void MemoryLink::close() {
    if (peer_) peer_->close();
    if (state_ != LinkState::Failed) state_ = LinkState::Closed;
}

}  // namespace remote
}  // namespace jnext
