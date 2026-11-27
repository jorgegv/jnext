#pragma once

// ---------------------------------------------------------------------------
// TcpLink: a ClientLink over a real TCP connection (GH #511, epic #486).
//
// It makes no socket call of its own: the connection is the esp socket seam's
// `esp::make_socket_transport` (non-blocking, name lookup on its own thread,
// Windows-twinned). What it adds is the connect deadline, the output queue and
// the input buffer, and the address policy for a DEBUGGER client: the user names
// the host, so loopback (another jnext) and private ranges are allowed; link-local,
// cloud metadata, unspecified and multicast/reserved stay refused as in the esp
// default.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "esp01/esp_socket.h"
#include "remote/client_link.h"

namespace jnext {
namespace remote {

/// The policy a debugger client dials under: the esp default with loopback allowed.
esp::AddressPolicy client_policy();

struct TcpLinkOptions {
    std::chrono::milliseconds connect_timeout{5000};
    /// Queued bytes the peer has not taken before the link fails ("output backlog").
    std::size_t max_output = std::size_t{32} << 20;
    /// A test seam: replaces the lookup of NAMES (null = the platform's getaddrinfo).
    esp::ResolveFn resolver;
};

class TcpLink final : public ClientLink {
public:
    TcpLink(std::string host, std::uint16_t port, TcpLinkOptions opt = {});
    ~TcpLink() override;

    bool        open() override;
    void        poll(int wait_ms) override;
    LinkState   state() const override;
    std::size_t read(std::uint8_t* buf, std::size_t cap) override;
    bool        write(const std::uint8_t* p, std::size_t n) override;
    void        close() override;
    LinkKind    kind() const override { return LinkKind::Tcp; }
    std::string describe() const override;
    const std::string& last_error() const override { return error_; }
    /// Bytes received and not yet read (never above the 1 MiB input cap plus one 16 KiB chunk).
    std::size_t buffered() const { return in_.size() - in_off_; }

private:
    bool pass();   // one pass; true when something moved

    std::string   host_;
    std::uint16_t port_;
    TcpLinkOptions opt_;
    std::unique_ptr<esp::EspTransport> t_;
    LinkState     state_ = LinkState::Idle;
    std::string   error_;
    std::chrono::steady_clock::time_point opened_{};
    std::vector<std::uint8_t> out_;
    std::size_t               out_off_ = 0;
    std::vector<std::uint8_t> in_;
    std::size_t               in_off_ = 0;
    bool                      peer_closed_ = false;
};

}  // namespace remote
}  // namespace jnext
