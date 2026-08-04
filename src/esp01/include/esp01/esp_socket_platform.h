#pragma once

#include "esp01/esp_socket.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// PRIVATE header — the OS primitives the portable transport is built from.
/// Included only by esp_socket.cpp and its two platform twins; nothing outside
/// src/peripheral should ever include it.
///
/// This header deliberately pulls in NO platform headers of its own (no
/// <sys/socket.h>, no <winsock2.h>): `NativeSocket` is spelled as a plain
/// integer type and every address is passed as `esp::IpAddress`, so the
/// sockaddr construction stays inside the twins. That is what lets the state
/// machine, the policy enforcement and the logging live in one portable file
/// instead of being copy-pasted into both twins — see the note in
/// esp_socket_posix.cpp on why the split is drawn here and not whole-file.
namespace esp {
namespace net {

#ifdef _WIN32
// Winsock's SOCKET is UINT_PTR and INVALID_SOCKET is (SOCKET)(~0).
using NativeSocket                     = std::uintptr_t;
constexpr NativeSocket kInvalidSocket  = ~static_cast<std::uintptr_t>(0);
#else
using NativeSocket                     = int;
constexpr NativeSocket kInvalidSocket  = -1;
#endif

enum class ConnectProgress : std::uint8_t { Pending, Connected, Failed };

/// `listen()` backlog for the AT+CIPSERVER socket (GH #210). Both twins must
/// pass the same value, which is why it is declared here rather than spelled
/// twice.
///
/// FOUR, and the number follows from the engine rather than from taste: the
/// AT layer holds at most one accepted connection at a time and the connection
/// table has four inbound slots (`AtEngine::MAX_CONNECTIONS` minus the
/// outbound one), so a deeper backlog would only park connections the guest
/// can never be given. Anything beyond it is refused by the kernel, which is
/// what a backlog is for.
constexpr int kListenBacklog = 4;

/// One-time process-wide network init (WSAStartup on Windows, nothing
/// elsewhere). Idempotent; safe to call from every factory invocation.
bool init(std::string& err);

/// Resolve `host` to numeric addresses. THE ONE BLOCKING CALL in the whole
/// layer — and only when `numeric_only` is false. With `numeric_only` set the
/// lookup is `AI_NUMERICHOST`: an IP literal succeeds with no network traffic
/// and anything else fails immediately.
///
/// PROTOCOL-INDEPENDENT ON PURPOSE (GH #198). The hints stay `SOCK_STREAM` even
/// for a UDP target: with a null service, the socktype hint only collapses the
/// duplicate per-socktype entries `getaddrinfo` would otherwise return, and the
/// ADDRESSES it yields are identical either way — a name resolves to hosts, not
/// to protocols. Keeping one form also keeps the public `ResolveFn` seam
/// (esp_socket.h), which a consumer may replace and which has no socktype
/// parameter, honest about what it is asked for.
bool resolve(const std::string& host, bool numeric_only,
             std::vector<IpAddress>& out, std::string& err);

/// Create a socket for `family` and `proto` already in non-blocking mode (and
/// with SIGPIPE suppressed where that is a per-socket option). Returns
/// `kInvalidSocket` on failure.
///
/// `local_port` binds the socket to that local port before any connect, and is
/// used only by UDP (`AT+CIPSTART`'s optional `<local port>`); 0 leaves the
/// port to the OS. A bind failure — the port is already in use, or privileged —
/// is a HARD failure returning `kInvalidSocket`, never a silent fall back to an
/// ephemeral port: a guest that named a port and got a different one would
/// receive nothing and have no way to find out why.
NativeSocket open_nonblocking(IpFamily family, Protocol proto,
                              std::uint16_t local_port, std::string& err);

/// Issue the non-blocking `connect`. `Pending` is the normal outcome
/// (EINPROGRESS / WSAEWOULDBLOCK); `Connected` happens on an immediate
/// loopback connect.
ConnectProgress begin_connect(NativeSocket s, const IpAddress& ip,
                              std::uint16_t port, std::string& err);

/// Zero-timeout readiness check on an in-flight connect. Never waits.
ConnectProgress poll_connect(NativeSocket s, std::string& err);

/// Non-blocking send. Returns bytes accepted (0 when the buffer is full).
/// `failed` is set only for a real error, never for would-block.
std::size_t send(NativeSocket s, const std::uint8_t* data, std::size_t len,
                 bool& failed, std::string& err);

/// Non-blocking receive. Returns bytes read (0 when nothing is available).
/// `eof` marks an orderly peer close; `failed` a real error.
///
/// `stream` says whether a zero-byte read is an END OF STREAM. It is true for
/// TCP and MUST be false for UDP: a datagram socket never reports EOF, and a
/// zero-length datagram is a legal thing for a peer to send, so reading 0 as
/// EOF there would tear down a perfectly live connection (GH #198). It is a
/// parameter rather than a `Protocol` because this is the only distinction the
/// syscall shims need to make — everything else about the protocol was decided
/// when the socket was created.
///
/// `reset` narrows `failed` to the ONE cause the caller has to treat
/// differently: the peer terminated the connection with a TCP RST
/// (`ECONNRESET` / `WSAECONNRESET`) instead of a FIN. Set only when `failed`
/// is, and never for any other error code — the classification is the whole
/// value of the flag, so widening it to "connection-ish errors" would destroy
/// it. See `SocketTransport::recv` for what the distinction buys (GH #176).
std::size_t recv(NativeSocket s, std::uint8_t* buf, std::size_t cap, bool stream,
                 bool& eof, bool& failed, bool& reset, std::string& err);

/// Create a non-blocking LISTENING socket bound to `ip`:`port` (GH #210).
/// Returns `kInvalidSocket` on failure, with `err` set.
///
/// THE BIND ADDRESS IS A SECURITY DECISION, so there is no fall back of any
/// kind: a bind that cannot have exactly the address it was given fails, and
/// the AT layer answers `ERROR` (design doc §13.4 — "silent widening is the
/// failure mode this decision exists to prevent"). That is also why the address
/// is a parameter rather than a wildcard with a filter bolted on afterwards:
/// the kernel is what enforces it.
///
/// `port` 0 lets the OS choose one, and `bound_port` always reports what was
/// actually bound. A caller that named a port gets it back unchanged; a caller
/// that did not has no other way to learn it, and neither has any way to end up
/// listening somewhere it did not intend.
///
/// THE TWINS DIVERGE ON ONE OPTION, deliberately, and each says why on the
/// spot: POSIX sets `SO_REUSEADDR`, Windows sets `SO_EXCLUSIVEADDRUSE`. The
/// names look like opposites because on Windows they are — see either
/// implementation.
///
/// `hardening_warning` reports THAT option failing to apply, and is the one
/// out-param here that does not mean the call failed: the socket is still
/// bound and usable, but a stated hardening measure is not in force. On
/// Windows that is the security-relevant one — without `SO_EXCLUSIVEADDRUSE`
/// another local process can bind the same address and take the connections —
/// so it must not be a silent degradation. Empty on success, which is the
/// overwhelmingly common case.
///
/// IT IS REPORTED RATHER THAN LOGGED HERE because the twins hold OS primitives
/// only: the tracing lives once, in the portable half, where it is compiled and
/// read on the maintainer's host (§7.5). A `log_warn` in each twin would be one
/// more line that cannot be built on the machine that maintains it.
///
/// The bind is NOT failed over it. The primary defence is the bind ADDRESS —
/// loopback unless the user widened it — and refusing to listen at all because
/// a defence-in-depth option would not apply trades a working feature for a
/// smaller marginal risk.
NativeSocket open_listener(const IpAddress& ip, std::uint16_t port,
                           std::uint16_t& bound_port, std::string& hardening_warning,
                           std::string& err);

/// Take one pending inbound connection off `listener`, or report that there is
/// none. NEVER waits (GH #210).
///
/// Three outcomes, and the caller has to be able to tell them apart:
///   * a socket, with `peer`/`peer_port` filled in and `failed` false — it is
///     already non-blocking and carries the same options an outbound socket
///     does, so it is usable through the ordinary send/recv shims;
///   * `kInvalidSocket` with `failed` FALSE — nothing is pending (the normal
///     case, once per poll), or a peer that hung up between its SYN and our
///     accept, which is the peer's business and leaves the listener good;
///   * `kInvalidSocket` with `failed` TRUE — the listener itself is broken and
///     `err` says how.
NativeSocket accept_nonblocking(NativeSocket listener, IpAddress& peer,
                                std::uint16_t& peer_port, bool& failed,
                                std::string& err);

void close(NativeSocket s);

}  // namespace net
}  // namespace esp
