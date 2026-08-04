// POSIX (Linux/macOS) socket backend for the emulated ESP-01 transport
// (GH #25, branch 2). Whole-file #ifdef'd twin of esp_socket_win.cpp, exactly
// like sdcard_provisioner_net_curl.cpp / _net_win.cpp: both implement the same
// contracts declared in esp_socket_platform.h, and exactly one of the pair has
// content per build, which is what keeps the file(GLOB_RECURSE) in
// src/peripheral/CMakeLists.txt correct on every platform.
//
// WHERE THE SPLIT IS DRAWN, AND WHY IT IS NOT WHOLE-SUBSYSTEM.
// The SD-card provisioner's twins are whole-file because curl+OpenSSL and
// WinHTTP+BCrypt are entirely different APIs with nothing to share. BSD
// sockets and Winsock are the same API with different spellings, so twinning
// the whole transport would mean two ~400-line files that are 95% identical —
// and one of them cannot be built or run on the maintainer's host, which makes
// silent drift between them not a risk but an inevitability. So the twins hold
// ONLY the OS primitives (this file is ~150 lines of syscall shims), and the
// state machine, the security policy and the tracing live once in the portable
// esp_socket.cpp where they are actually tested.
//
// No third-party dependency is added: BSD sockets are an OS component, the
// same way WinHTTP and BCrypt are on the Windows side. There is deliberately
// no TLS here — no ZX Next software found in the GH #25 survey uses SSL.
#ifndef _WIN32

#include "esp01/esp_socket_platform.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace esp {
namespace net {
namespace {

std::string errno_text(int e) { return std::strerror(e); }

/// errno values that mean "not now", never "broken".
bool would_block(int e) {
    return e == EAGAIN || e == EWOULDBLOCK || e == EINTR;
}

/// Build the sockaddr for `ip`:`port`. Returns the used length, 0 on a family
/// we cannot express.
/// Read `ip` and `port` back out of a sockaddr the kernel filled in — the
/// inverse of `fill_sockaddr`, used for `getsockname` on a listener and for the
/// peer address `accept` hands back. Returns false on a family we cannot
/// express.
bool read_sockaddr(const sockaddr_storage& sa, IpAddress& ip, std::uint16_t& port) {
    ip = IpAddress{};
    if (sa.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(&sa);
        ip.family = IpFamily::V4;
        std::memcpy(ip.bytes.data(), &sin->sin_addr, 4);
        port = ntohs(sin->sin_port);
        return true;
    }
    if (sa.ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(&sa);
        ip.family = IpFamily::V6;
        std::memcpy(ip.bytes.data(), &sin6->sin6_addr, 16);
        port = ntohs(sin6->sin6_port);
        return true;
    }
    return false;
}

/// Put an accepted socket into the same shape an outbound one is created in:
/// non-blocking, SIGPIPE suppressed where that is a per-socket option, Nagle
/// off. Linux does NOT inherit `O_NONBLOCK` across `accept` (BSD does), so this
/// is required rather than tidy — an inherited-mode assumption would make the
/// engine's non-blocking `recv` block on Linux only.
bool prepare_accepted(int s, std::string& err) {
    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0 || ::fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        err = errno_text(errno);
        return false;
    }
#ifdef SO_NOSIGPIPE
    const int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    // Same reasoning as the outbound socket: DZRP is small request/response
    // frames, so Nagle would add ~40 ms to every exchange for no benefit.
    const int nodelay = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    return true;
}

socklen_t fill_sockaddr(const IpAddress& ip, std::uint16_t port,
                        sockaddr_storage& out) {
    std::memset(&out, 0, sizeof(out));
    if (ip.family == IpFamily::V4) {
        auto* sin = reinterpret_cast<sockaddr_in*>(&out);
        sin->sin_family = AF_INET;
        sin->sin_port   = htons(port);
        std::memcpy(&sin->sin_addr, ip.bytes.data(), 4);
        return sizeof(sockaddr_in);
    }
    auto* sin6 = reinterpret_cast<sockaddr_in6*>(&out);
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port   = htons(port);
    std::memcpy(&sin6->sin6_addr, ip.bytes.data(), 16);
    return sizeof(sockaddr_in6);
}

}  // namespace

bool init(std::string&) { return true; }  // nothing to do outside Windows

bool resolve(const std::string& host, bool numeric_only,
             std::vector<IpAddress>& out, std::string& err) {
    out.clear();

    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    // AI_NUMERICHOST is what makes the IP-literal path provably non-blocking:
    // the resolver never consults the network, it either parses the literal or
    // fails immediately.
    if (numeric_only) hints.ai_flags = AI_NUMERICHOST;

    addrinfo* res = nullptr;
    const int rc  = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0) {
        err = ::gai_strerror(rc);
        return false;
    }

    for (addrinfo* a = res; a != nullptr; a = a->ai_next) {
        IpAddress ip;
        if (a->ai_family == AF_INET) {
            ip.family = IpFamily::V4;
            std::memcpy(ip.bytes.data(),
                        &reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr, 4);
        } else if (a->ai_family == AF_INET6) {
            ip.family = IpFamily::V6;
            std::memcpy(ip.bytes.data(),
                        &reinterpret_cast<sockaddr_in6*>(a->ai_addr)->sin6_addr, 16);
            // sin6_scope_id is DROPPED here and fill_sockaddr() never restores
            // it, so a scoped address (fe80::1%eth0) loses its zone. That is
            // harmless while link-local is denied — which it is by default —
            // and is documented as a known limitation at AddressPolicy::
            // deny_link_local, the flag someone would have to clear to hit it.
        } else {
            continue;
        }
        out.push_back(ip);
    }
    ::freeaddrinfo(res);

    if (out.empty()) {
        err = "no usable addresses";
        return false;
    }
    return true;
}

NativeSocket open_nonblocking(IpFamily family, Protocol proto,
                              std::uint16_t local_port, std::string& err) {
    const bool udp    = (proto == Protocol::Udp);
    const int  domain = (family == IpFamily::V4) ? AF_INET : AF_INET6;
    const int  s      = udp ? ::socket(domain, SOCK_DGRAM, IPPROTO_UDP)
                            : ::socket(domain, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        err = errno_text(errno);
        return kInvalidSocket;
    }

    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0 || ::fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        err = errno_text(errno);
        ::close(s);
        return kInvalidSocket;
    }

#ifdef SO_NOSIGPIPE
    // macOS/BSD have no MSG_NOSIGNAL, so SIGPIPE is suppressed per socket
    // instead. Without one of the two, writing to a peer that has gone away
    // KILLS the emulator process outright.
    const int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

    if (udp) {
        // AT+CIPSTART's optional <local port>. Bound to the WILDCARD address,
        // not to the peer's family-matching local address: the guest names a
        // port, never an interface, and the socket is `connect()`ed straight
        // afterwards, which is what restricts inbound datagrams to the peer.
        // No SO_REUSEADDR — a port already in use must fail loudly rather than
        // silently share a UDP port with another process on this host.
        if (local_port != 0) {
            IpAddress        any;  // all-zero bytes == INADDR_ANY / in6addr_any
            any.family = family;
            sockaddr_storage sa{};
            const socklen_t  len = fill_sockaddr(any, local_port, sa);
            if (::bind(s, reinterpret_cast<sockaddr*>(&sa), len) < 0) {
                err = errno_text(errno);
                ::close(s);
                return kInvalidSocket;
            }
        }
        // No TCP_NODELAY: there is no Nagle on a datagram socket, and the
        // option is at the TCP level anyway.
        return s;
    }

    // The emulated link carries tiny request/response pairs (nextsync sends
    // 3-7 byte commands and waits), so Nagle would add ~40 ms of latency to
    // every exchange for no benefit.
    const int nodelay = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    return s;
}

ConnectProgress begin_connect(NativeSocket s, const IpAddress& ip,
                              std::uint16_t port, std::string& err) {
    sockaddr_storage sa{};
    const socklen_t  len = fill_sockaddr(ip, port, sa);
    if (len == 0) {
        err = "unsupported address family";
        return ConnectProgress::Failed;
    }
    if (::connect(s, reinterpret_cast<sockaddr*>(&sa), len) == 0)
        return ConnectProgress::Connected;  // loopback commonly completes at once

    const int e = errno;
    if (e == EINPROGRESS || e == EALREADY || e == EINTR)
        return ConnectProgress::Pending;
    err = errno_text(e);
    return ConnectProgress::Failed;
}

ConnectProgress poll_connect(NativeSocket s, std::string& err) {
    pollfd pfd{};
    pfd.fd     = s;
    pfd.events = POLLOUT;

    // Timeout 0: this returns immediately, always. It is the only readiness
    // primitive the layer has, which is what makes blocking unreachable.
    const int rc = ::poll(&pfd, 1, 0);
    if (rc < 0) {
        if (would_block(errno)) return ConnectProgress::Pending;
        err = errno_text(errno);
        return ConnectProgress::Failed;
    }
    if (rc == 0) return ConnectProgress::Pending;

    // Readiness alone does not mean success: a refused connect also makes the
    // descriptor writable, so SO_ERROR is the authority.
    int       so_err = 0;
    socklen_t elen   = sizeof(so_err);
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &so_err, &elen) < 0) {
        err = errno_text(errno);
        return ConnectProgress::Failed;
    }
    if (so_err != 0) {
        err = errno_text(so_err);
        return ConnectProgress::Failed;
    }
    return ConnectProgress::Connected;
}

std::size_t send(NativeSocket s, const std::uint8_t* data, std::size_t len,
                 bool& failed, std::string& err) {
    failed = false;
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;  // Linux: suppress SIGPIPE on a dead peer
#else
    const int flags = 0;             // macOS/BSD: handled by SO_NOSIGPIPE above
#endif
    const ssize_t n = ::send(s, data, len, flags);
    if (n >= 0) return static_cast<std::size_t>(n);
    if (would_block(errno)) return 0;
    failed = true;
    err    = errno_text(errno);
    return 0;
}

std::size_t recv(NativeSocket s, std::uint8_t* buf, std::size_t cap, bool stream,
                 bool& eof, bool& failed, bool& reset, std::string& err) {
    eof    = false;
    failed = false;
    reset  = false;
    const ssize_t n = ::recv(s, buf, cap, 0);
    if (n > 0) return static_cast<std::size_t>(n);
    if (n == 0) {
        // TCP: an orderly close by the peer. UDP: a zero-length datagram, which
        // is legal and is NOT the end of anything — reporting EOF there would
        // close a live connection (GH #198). Nothing is handed on either way,
        // so the datagram is dropped rather than framed as an empty `+IPD`.
        eof = stream;
        return 0;
    }
    if (would_block(errno)) return 0;
    failed = true;
    // ECONNRESET and nothing else: the peer sent a TCP RST. Note what Linux
    // guarantees about the ORDER, because the portable half relies on it —
    // `tcp_recvmsg` hands back everything already queued and only reports the
    // error once the queue is empty, so a reset never arrives here in place of
    // bytes we would otherwise have read (measured on this host: a payload
    // sent immediately before a SO_LINGER(1,0) close is delivered in full,
    // with ECONNRESET on the NEXT call).
    reset  = (errno == ECONNRESET);
    err    = errno_text(errno);
    return 0;
}

NativeSocket open_listener(const IpAddress& ip, std::uint16_t port,
                           std::uint16_t& bound_port, std::string& err) {
    bound_port = 0;
    const int domain = (ip.family == IpFamily::V4) ? AF_INET : AF_INET6;
    const int s      = ::socket(domain, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        err = errno_text(errno);
        return kInvalidSocket;
    }

    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0 || ::fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        err = errno_text(errno);
        ::close(s);
        return kInvalidSocket;
    }

    // SO_REUSEADDR, and POSIX semantics are what make it safe here: it permits
    // a bind over the TIME_WAIT remains of connections a PREVIOUS listener
    // accepted, and nothing else. Two live listeners on one address still
    // conflict — that needs SO_REUSEPORT, which is deliberately not set. So a
    // guest that stops and restarts its server (AT+CIPSERVER=0 then =1 on the
    // same port) is not refused for two minutes by the sockets its last
    // session's peers left behind, while a second process still cannot take
    // the port from under us. The Windows twin needs a DIFFERENT option to get
    // the same property; see it.
    const int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_storage sa{};
    const socklen_t  len = fill_sockaddr(ip, port, sa);
    if (len == 0) {
        err = "unsupported address family";
        ::close(s);
        return kInvalidSocket;
    }
    if (::bind(s, reinterpret_cast<sockaddr*>(&sa), len) < 0) {
        err = errno_text(errno);
        ::close(s);
        return kInvalidSocket;
    }
    if (::listen(s, kListenBacklog) < 0) {
        err = errno_text(errno);
        ::close(s);
        return kInvalidSocket;
    }

    sockaddr_storage bound{};
    socklen_t        blen = sizeof(bound);
    IpAddress        bound_ip;
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &blen) < 0 ||
        !read_sockaddr(bound, bound_ip, bound_port) || bound_port == 0) {
        // A listener whose port cannot be read back is useless rather than
        // merely undiagnosed: with `port` 0 the OS chose one and this is the
        // only way to learn it, and with a named port a disagreement would mean
        // we are not listening where we said. Both are refusals.
        err = "cannot read back the bound port";
        ::close(s);
        return kInvalidSocket;
    }
    return s;
}

NativeSocket accept_nonblocking(NativeSocket listener, IpAddress& peer,
                                std::uint16_t& peer_port, bool& failed,
                                std::string& err) {
    failed    = false;
    peer      = IpAddress{};
    peer_port = 0;

    sockaddr_storage sa{};
    socklen_t        len = sizeof(sa);
    const int        s   = ::accept(listener, reinterpret_cast<sockaddr*>(&sa), &len);
    if (s < 0) {
        if (would_block(errno)) return kInvalidSocket;
        // ECONNABORTED: the peer sent SYN and then went away before we got to
        // it. POSIX explicitly allows accept to report that, the listener is
        // untouched, and the next call must simply be tried — so it is "nothing
        // pending", not a fault.
        if (errno == ECONNABORTED) return kInvalidSocket;
        failed = true;
        err    = errno_text(errno);
        return kInvalidSocket;
    }

    if (!read_sockaddr(sa, peer, peer_port) || !prepare_accepted(s, err)) {
        // Dropped rather than handed on half-configured: a socket whose peer we
        // cannot name, or that we could not put in non-blocking mode, would
        // stall the engine's service pass the first time it was read.
        if (err.empty()) err = "unsupported peer address family";
        ::close(s);
        failed = true;
        return kInvalidSocket;
    }
    return s;
}

void close(NativeSocket s) {
    if (s != kInvalidSocket) ::close(s);
}

}  // namespace net
}  // namespace esp

#endif  // !_WIN32
