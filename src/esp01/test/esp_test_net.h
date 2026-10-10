// Socket scaffolding shared by the ESP-01 transport suites (GH #25, GH #214).
//
// A hermetic transport test needs a peer, and the module deliberately does not
// ship one: `Listener` below is that peer, an in-process TCP listener on
// 127.0.0.1:0. The `net_*` shims are the only places the POSIX and Winsock
// spellings differ; everything else in the suites is portable C++17.
//
// Included by esp_socket_test.cpp and esp_sigpipe_test.cpp. The helpers sit in
// an anonymous namespace so each suite gets its own copy.
#pragma once

#include "esp01/esp_socket.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <process.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <thread>

namespace {

using namespace esp;

#ifdef _WIN32
/// Winsock must be started before the first socket() (the module's own
/// transport starts it itself, but the scaffolding opens sockets first).
void net_init() {
    static WSADATA wsa;
    static const int rc = ::WSAStartup(MAKEWORD(2, 2), &wsa);
    (void)rc;
}
int net_socket(int domain, int type, int proto) {
    const SOCKET s = ::socket(domain, type, proto);
    return s == INVALID_SOCKET ? -1 : static_cast<int>(s);
}
int net_close(int fd) { return ::closesocket(static_cast<SOCKET>(fd)); }
int net_poll(pollfd* fds, unsigned n, int timeout_ms) { return ::WSAPoll(fds, n, timeout_ms); }
int net_setsockopt(int fd, int level, int opt, const void* val, int len) {
    return ::setsockopt(static_cast<SOCKET>(fd), level, opt, static_cast<const char*>(val), len);
}
int net_accept(int fd) {
    const SOCKET s = ::accept(static_cast<SOCKET>(fd), nullptr, nullptr);
    return s == INVALID_SOCKET ? -1 : static_cast<int>(s);
}
int net_getpid() { return ::_getpid(); }
#else
void net_init() {}
int net_socket(int domain, int type, int proto) { return ::socket(domain, type, proto); }
int net_close(int fd) { return ::close(fd); }
int net_poll(pollfd* fds, unsigned n, int timeout_ms) { return ::poll(fds, n, timeout_ms); }
int net_setsockopt(int fd, int level, int opt, const void* val, int len) {
    return ::setsockopt(fd, level, opt, val, static_cast<socklen_t>(len));
}
int net_accept(int fd) { return ::accept(fd, nullptr, nullptr); }
int net_getpid() { return static_cast<int>(::getpid()); }
#endif

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class Listener {
public:
    bool start() {
        fd_ = net_socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int on = 1;
        net_setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        sockaddr_in sa{};
        sa.sin_family      = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port        = 0;  // kernel picks a free port — no fixed port, ever
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) return false;
        if (::listen(fd_, 4) != 0) return false;

        sockaddr_in bound{};
        socklen_t   len = sizeof(bound);
        if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&bound), &len) != 0) return false;
        port_ = ntohs(bound.sin_port);
        return port_ != 0;
    }

    /// Accept one pending connection, waiting up to `timeout_ms`.
    int accept_one(int timeout_ms) {
        pollfd p{};
        p.fd     = fd_;
        p.events = POLLIN;
        if (net_poll(&p, 1, timeout_ms) <= 0) return -1;
        return net_accept(fd_);
    }

    std::uint16_t port() const { return port_; }

    void stop() {
        if (fd_ >= 0) net_close(fd_);
        fd_ = -1;
    }
    ~Listener() { stop(); }

private:
    int           fd_   = -1;
    std::uint16_t port_ = 0;
};

/// Drive poll() until the transport reaches `want` or settles in a terminal
/// state. Bounded: loopback resolves this in microseconds, so a timeout here
/// is a real failure, not a slow machine.
bool pump_until(EspTransport& t, TransportState want, int timeout_ms = 2000) {
    for (int waited = 0; waited <= timeout_ms; waited += 2) {
        t.poll();
        if (t.state() == want) return true;
        if (t.state() == TransportState::Failed || t.state() == TransportState::Closed)
            return false;  // settled somewhere else
        sleep_ms(2);
    }
    return false;
}

/// The policy the socket rows run under: identical to production except that
/// loopback is reachable, because the listener lives on 127.0.0.1.
AddressPolicy loopback_ok() {
    AddressPolicy p;
    p.deny_loopback = false;
    return p;
}

}  // namespace
