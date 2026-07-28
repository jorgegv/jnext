#pragma once

#include "peripheral/esp_socket.h"

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

/// One-time process-wide network init (WSAStartup on Windows, nothing
/// elsewhere). Idempotent; safe to call from every factory invocation.
bool init(std::string& err);

/// Resolve `host` to numeric addresses. THE ONE BLOCKING CALL in the whole
/// layer — and only when `numeric_only` is false. With `numeric_only` set the
/// lookup is `AI_NUMERICHOST`: an IP literal succeeds with no network traffic
/// and anything else fails immediately.
bool resolve(const std::string& host, bool numeric_only,
             std::vector<IpAddress>& out, std::string& err);

/// Create a TCP socket for `family` already in non-blocking mode (and with
/// SIGPIPE suppressed where that is a per-socket option). Returns
/// `kInvalidSocket` on failure.
NativeSocket open_nonblocking(IpFamily family, std::string& err);

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
std::size_t recv(NativeSocket s, std::uint8_t* buf, std::size_t cap, bool& eof,
                 bool& failed, std::string& err);

void close(NativeSocket s);

}  // namespace net
}  // namespace esp
