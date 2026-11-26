#pragma once

// ---------------------------------------------------------------------------
// MemoryLink: a ClientLink over the client end of an in-memory `FakeListener`
// pair (GH #511, epic #486). The unit suites' stand-in for TcpLink: no socket,
// no wait. The other end is the `FakeTransport` a `remote::Server`, or a
// scripted test peer, `accept()`s.
//
// It never pumps its peer: whoever owns the other end advances it (a hook in the
// client's wait loop). `poll()` never waits.
// ---------------------------------------------------------------------------

#include <memory>
#include <string>

#include "remote/client_link.h"
#include "remote/fake_transport.h"

namespace jnext {
namespace remote {

class MemoryLink final : public ClientLink {
public:
    /// `impersonated` is the kind `kind()` reports (so the identity rule can be tested
    /// for a serial line without one); `label` names it in `describe()`.
    explicit MemoryLink(FakeListener& listener, LinkKind impersonated = LinkKind::Tcp,
                        std::string label = "mem");

    bool        open() override;
    void        poll(int) override {}
    LinkState   state() const override;
    std::size_t read(std::uint8_t* buf, std::size_t cap) override;
    bool        write(const std::uint8_t* p, std::size_t n) override;
    void        close() override;
    LinkKind    kind() const override { return kind_; }
    std::string describe() const override { return "mem:" + label_; }
    const std::string& last_error() const override { return error_; }

private:
    FakeListener&              listener_;
    LinkKind                   kind_;
    std::string                label_;
    std::shared_ptr<FakePeer>  peer_;
    LinkState                  state_ = LinkState::Idle;
    std::string                error_;
};

}  // namespace remote
}  // namespace jnext
