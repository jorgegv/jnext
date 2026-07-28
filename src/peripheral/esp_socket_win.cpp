// Windows (Winsock2) socket backend for the emulated ESP-01 transport
// (GH #25, branch 2). Whole-file #ifdef'd twin of esp_socket_posix.cpp — same
// contracts, same error semantics — mirroring
// sdcard_provisioner_net_{curl,win}.cpp so the file(GLOB_RECURSE) in
// src/peripheral/CMakeLists.txt stays correct on every platform: exactly one
// twin has content per build.
//
// Only the OS primitives are twinned; the state machine, address policy and
// tracing live once in the portable esp_socket.cpp. See the long note at the
// top of esp_socket_posix.cpp for why the split is drawn there.
//
// ws2_32 is an OS component present in every Windows since 95, and every API
// used here predates Windows 7 by years (getaddrinfo is XP-era, select and
// ioctlsocket are original Winsock), so the SDL-only build's pinned
// _WIN32_WINNT=0x0601 is satisfied with room to spare. No new dependency: the
// same posture as WinHTTP/BCrypt in the SD-card provisioner.
//
// HONEST LIMITATION: this file cannot be exercised on the maintainer's Linux
// host. The Windows targets all build with -DENABLE_TESTS=OFF (Makefile:187,
// 215, 236, 251), so esp_socket_test never runs here. It is kept to syscall
// shims precisely so that the untestable surface is as small as possible.
#ifdef _WIN32

#include "peripheral/esp_socket_platform.h"

// winsock2.h MUST precede windows.h or windows.h drags in the Winsock 1
// declarations and they conflict.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstring>

namespace esp {
namespace net {
namespace {

std::string wsa_text(int code) {
    char* msg = nullptr;
    const DWORD n = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<char*>(&msg), 0,
        nullptr);
    std::string out;
    if (n && msg) {
        out.assign(msg, n);
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
            out.pop_back();
    }
    if (out.empty()) out = "winsock error " + std::to_string(code);
    if (msg) ::LocalFree(msg);
    return out;
}

bool would_block(int e) {
    return e == WSAEWOULDBLOCK || e == WSAEINTR || e == WSAEINPROGRESS;
}

int fill_sockaddr(const IpAddress& ip, std::uint16_t port, sockaddr_storage& out) {
    std::memset(&out, 0, sizeof(out));
    if (ip.family == IpFamily::V4) {
        auto* sin = reinterpret_cast<sockaddr_in*>(&out);
        sin->sin_family = AF_INET;
        sin->sin_port   = htons(port);
        std::memcpy(&sin->sin_addr, ip.bytes.data(), 4);
        return static_cast<int>(sizeof(sockaddr_in));
    }
    auto* sin6 = reinterpret_cast<sockaddr_in6*>(&out);
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port   = htons(port);
    std::memcpy(&sin6->sin6_addr, ip.bytes.data(), 16);
    return static_cast<int>(sizeof(sockaddr_in6));
}

/// Clamp to what Winsock's int-typed send/recv can express.
int clamp_len(std::size_t len) {
    constexpr std::size_t kMax = 1u << 20;
    return static_cast<int>(len > kMax ? kMax : len);
}

}  // namespace

bool init(std::string& err) {
    // Winsock refcounts WSAStartup, so calling it once per process and never
    // cleaning up is the normal pattern for a program that uses sockets for
    // its whole life.
    static bool        ok = false;
    static bool        done = false;
    static std::string saved_err;
    if (!done) {
        done = true;
        WSADATA wsa{};
        const int rc = ::WSAStartup(MAKEWORD(2, 2), &wsa);
        ok = (rc == 0);
        if (!ok) saved_err = wsa_text(rc);
    }
    if (!ok) err = saved_err;
    return ok;
}

bool resolve(const std::string& host, bool numeric_only,
             std::vector<IpAddress>& out, std::string& err) {
    out.clear();

    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (numeric_only) hints.ai_flags = AI_NUMERICHOST;

    addrinfo* res = nullptr;
    const int rc  = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0) {
        err = wsa_text(rc);
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
            // sin6_scope_id is dropped here, exactly as in the POSIX twin —
            // see the note there and at AddressPolicy::deny_link_local.
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
    const int    domain = (family == IpFamily::V4) ? AF_INET : AF_INET6;
    const SOCKET s      = ::socket(domain, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        err = wsa_text(::WSAGetLastError());
        return kInvalidSocket;
    }

    u_long nonblocking = 1;
    if (::ioctlsocket(s, FIONBIO, &nonblocking) != 0) {
        err = wsa_text(::WSAGetLastError());
        ::closesocket(s);
        return kInvalidSocket;
    }

    // Same reasoning as the POSIX twin: tiny request/response pairs, so Nagle
    // would add latency for no benefit. (Windows has no SIGPIPE, so the
    // MSG_NOSIGNAL/SO_NOSIGPIPE half of the twin has no counterpart here.)
    BOOL nodelay = TRUE;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay),
                 sizeof(nodelay));

    return static_cast<NativeSocket>(s);
}

ConnectProgress begin_connect(NativeSocket s, const IpAddress& ip,
                              std::uint16_t port, std::string& err) {
    sockaddr_storage sa{};
    const int        len = fill_sockaddr(ip, port, sa);
    if (len == 0) {
        err = "unsupported address family";
        return ConnectProgress::Failed;
    }
    if (::connect(static_cast<SOCKET>(s), reinterpret_cast<sockaddr*>(&sa), len) == 0)
        return ConnectProgress::Connected;

    const int e = ::WSAGetLastError();
    if (would_block(e) || e == WSAEALREADY) return ConnectProgress::Pending;
    err = wsa_text(e);
    return ConnectProgress::Failed;
}

ConnectProgress poll_connect(NativeSocket s, std::string& err) {
    const SOCKET sock = static_cast<SOCKET>(s);

    // select(), not WSAPoll(): WSAPoll has the long-standing documented defect
    // that it does NOT report a FAILED non-blocking connect, so a refused
    // connection would hang in Pending forever. select's exceptfds does report
    // it, which is why the POSIX twin can use poll() and this one cannot.
    fd_set wr, ex;
    FD_ZERO(&wr);
    FD_ZERO(&ex);
    FD_SET(sock, &wr);
    FD_SET(sock, &ex);

    // {0,0}: returns immediately, always — the zero-timeout guarantee.
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    const int rc = ::select(0, nullptr, &wr, &ex, &tv);
    if (rc == SOCKET_ERROR) {
        const int e = ::WSAGetLastError();
        if (would_block(e)) return ConnectProgress::Pending;
        err = wsa_text(e);
        return ConnectProgress::Failed;
    }
    if (rc == 0) return ConnectProgress::Pending;

    int so_err = 0;
    int elen   = sizeof(so_err);
    if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_err),
                     &elen) != 0) {
        err = wsa_text(::WSAGetLastError());
        return ConnectProgress::Failed;
    }
    if (so_err != 0) {
        err = wsa_text(so_err);
        return ConnectProgress::Failed;
    }
    if (FD_ISSET(sock, &ex)) {
        err = "connect failed";
        return ConnectProgress::Failed;
    }
    return ConnectProgress::Connected;
}

std::size_t send(NativeSocket s, const std::uint8_t* data, std::size_t len,
                 bool& failed, std::string& err) {
    failed = false;
    const int n = ::send(static_cast<SOCKET>(s),
                         reinterpret_cast<const char*>(data), clamp_len(len), 0);
    if (n >= 0) return static_cast<std::size_t>(n);
    const int e = ::WSAGetLastError();
    if (would_block(e)) return 0;
    failed = true;
    err    = wsa_text(e);
    return 0;
}

std::size_t recv(NativeSocket s, std::uint8_t* buf, std::size_t cap, bool& eof,
                 bool& failed, std::string& err) {
    eof    = false;
    failed = false;
    const int n =
        ::recv(static_cast<SOCKET>(s), reinterpret_cast<char*>(buf), clamp_len(cap), 0);
    if (n > 0) return static_cast<std::size_t>(n);
    if (n == 0) {
        eof = true;
        return 0;
    }
    const int e = ::WSAGetLastError();
    if (would_block(e)) return 0;
    failed = true;
    err    = wsa_text(e);
    return 0;
}

void close(NativeSocket s) {
    if (s != kInvalidSocket) ::closesocket(static_cast<SOCKET>(s));
}

}  // namespace net
}  // namespace esp

#endif  // _WIN32
