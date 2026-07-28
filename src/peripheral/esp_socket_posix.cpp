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

#include "peripheral/esp_socket_platform.h"

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

NativeSocket open_nonblocking(IpFamily family, std::string& err) {
    const int domain = (family == IpFamily::V4) ? AF_INET : AF_INET6;
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

#ifdef SO_NOSIGPIPE
    // macOS/BSD have no MSG_NOSIGNAL, so SIGPIPE is suppressed per socket
    // instead. Without one of the two, writing to a peer that has gone away
    // KILLS the emulator process outright.
    const int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

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

std::size_t recv(NativeSocket s, std::uint8_t* buf, std::size_t cap, bool& eof,
                 bool& failed, std::string& err) {
    eof    = false;
    failed = false;
    const ssize_t n = ::recv(s, buf, cap, 0);
    if (n > 0) return static_cast<std::size_t>(n);
    if (n == 0) {
        eof = true;  // orderly close by the peer
        return 0;
    }
    if (would_block(errno)) return 0;
    failed = true;
    err    = errno_text(errno);
    return 0;
}

void close(NativeSocket s) {
    if (s != kInvalidSocket) ::close(s);
}

}  // namespace net
}  // namespace esp

#endif  // !_WIN32
