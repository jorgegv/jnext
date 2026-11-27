#pragma once

// ---------------------------------------------------------------------------
// ClientLink: the byte pipe a remote-debugger CLIENT talks over (GH #511, epic
// #486). The outbound twin of what `remote::Server` is for the servers.
//
// A link moves bytes and knows nothing of any protocol. `DzrpClient`
// (dzrp/dzrp_client.h) owns one and never touches a socket. Implementations:
//
//   TcpLink     (tcp_link.h)     a real TCP connection, over the esp socket seam.
//   MemoryLink  (memory_link.h)  the client end of an in-memory `FakeListener`
//                                pair, for the unit suites.
//
// Everything is non-blocking except `poll(wait_ms)`, which waits at most
// `wait_ms` for progress. Nothing here throws.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>

namespace jnext {
namespace remote {

/// What the identity rule of a protocol client reads (the same bytes over TCP and
/// over a serial line can mean a different remote).
enum class LinkKind : std::uint8_t { Tcp, Serial };

enum class LinkState : std::uint8_t { Idle, Connecting, Open, Closed, Failed };

class ClientLink {
public:
    virtual ~ClientLink() = default;

    /// Start the link. Never blocks. False = rejected now (`last_error()` says why); a
    /// connect that fails later is reported as `Failed`.
    virtual bool open() = 0;

    /// Advance the link and move bytes; waits at most `wait_ms` for progress (0 = one pass).
    virtual void poll(int wait_ms) = 0;

    virtual LinkState state() const = 0;

    /// Take up to `cap` received bytes. Never blocks; 0 = nothing buffered.
    virtual std::size_t read(std::uint8_t* buf, std::size_t cap) = 0;

    /// Queue ALL `n` bytes, or fail the link (an output backlog past the cap) and return false.
    virtual bool write(const std::uint8_t* p, std::size_t n) = 0;

    /// Idempotent.
    virtual void close() = 0;

    virtual LinkKind           kind() const       = 0;
    /// Human text for logs: `tcp:192.168.1.50:11000`.
    virtual std::string        describe() const   = 0;
    virtual const std::string& last_error() const = 0;
};

/// A parsed `--remote-target` style address.
struct LinkSpec {
    LinkKind      kind = LinkKind::Tcp;
    std::string   host;          // an IPv6 literal without its brackets
    std::uint16_t port = 0;
};

/// `tcp:HOST:PORT`. HOST is a numeric IPv4, a bracketed IPv6 (`[::1]`) or a DNS name of
/// `[A-Za-z0-9.-]` (1..253 characters, no empty label); PORT is 1..65535, decimal digits
/// only. `serial:` is refused ("not in this build"), as is any other scheme. False sets
/// `error`.
bool parse_link_uri(const std::string& uri, LinkSpec& out, std::string& error);

}  // namespace remote
}  // namespace jnext
