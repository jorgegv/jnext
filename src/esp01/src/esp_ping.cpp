#include "esp01/esp_ping.h"

#include "esp01/esp_log.h"
#include "esp01/esp_socket_platform.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <icmpapi.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace esp {
namespace {

/// One ping's result, written ONCE by the worker and published with a release
/// store — the single-shot contract `ResolveJob` established, for the same
/// reason: the worker may outlive the object that started it, so it must touch
/// nothing else.
struct PingJob {
    std::atomic<bool> done{false};
    bool              ok = false;
    unsigned          ms = 0;
    std::string       err;
};

#if !defined(_WIN32)

/// The 16-bit one's-complement checksum ICMP uses.
///
/// COMPUTED EVEN THOUGH LINUX WOULD DO IT. On a `SOCK_DGRAM`/`IPPROTO_ICMP`
/// socket the kernel rewrites the checksum (and the identifier) on the way
/// out; macOS does not promise that. Filling it in is a few instructions and
/// makes the packet correct on both, rather than correct on one and
/// accidentally-correct on the other.
std::uint16_t icmp_checksum(const void* data, std::size_t len) {
    const auto*   p   = static_cast<const std::uint8_t*>(data);
    std::uint32_t sum = 0;
    for (; len > 1; len -= 2, p += 2) sum += static_cast<std::uint32_t>((p[0] << 8) | p[1]);
    if (len) sum += static_cast<std::uint32_t>(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<std::uint16_t>(~sum);
}

struct EchoHeader {
    std::uint8_t  type;
    std::uint8_t  code;
    std::uint16_t checksum;
    std::uint16_t id;
    std::uint16_t seq;
};

constexpr std::uint8_t ICMP_ECHO_REQUEST = 8;
constexpr std::uint8_t ICMP_ECHO_REPLY   = 0;

/// Send one echo to `addr` and wait for its reply. Returns true and fills `ms`
/// on success; on failure sets `err`.
bool icmp_echo(const IpAddress& addr, unsigned timeout_s, unsigned& ms, std::string& err) {
    // AN UNPRIVILEGED ICMP SOCKET. `SOCK_RAW` would need CAP_NET_RAW; this is
    // the same socket type `ping(8)` itself uses on a modern Linux, permitted
    // by `net.ipv4.ping_group_range`. EACCES/EPERM here is the honest "this
    // host will not let us" and becomes the module's ordinary failure reply.
    const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) {
        err = "ICMP socket refused by this host (unprivileged ping not permitted)";
        return false;
    }

    timeval tv{};
    tv.tv_sec  = static_cast<long>(timeout_s ? timeout_s : 1u);
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    // A payload big enough to carry our own marker, which is what correlates a
    // reply on a socket whose ICMP identifier the kernel may have rewritten.
    static std::atomic<std::uint16_t> next_seq{1};
    const std::uint16_t               seq = next_seq.fetch_add(1);

    std::uint8_t packet[sizeof(EchoHeader) + 16] = {};
    auto*        hdr                             = reinterpret_cast<EchoHeader*>(packet);
    hdr->type                                    = ICMP_ECHO_REQUEST;
    hdr->code                                    = 0;
    hdr->id                                      = 0;  // the kernel owns this on a ping socket
    hdr->seq                                     = htons(seq);
    std::memcpy(packet + sizeof(EchoHeader), "jnext-esp-ping!!", 16);
    hdr->checksum = 0;
    hdr->checksum = htons(icmp_checksum(packet, sizeof packet));

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    std::memcpy(&dst.sin_addr, addr.bytes.data(), sizeof dst.sin_addr);

    const auto t0 = std::chrono::steady_clock::now();
    if (::sendto(fd, packet, sizeof packet, 0, reinterpret_cast<sockaddr*>(&dst), sizeof dst) < 0) {
        err = "could not send the echo request";
        ::close(fd);
        return false;
    }

    // Read until OUR sequence comes back or the receive times out. A shared
    // ping socket can deliver a reply meant for another request in this
    // process, so a reply that is not ours is discarded rather than counted.
    for (;;) {
        std::uint8_t reply[1500];
        sockaddr_in  from{};
        socklen_t    from_len = sizeof from;
        const ssize_t n = ::recvfrom(fd, reply, sizeof reply, 0,
                                     reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n < 0) {
            err = "no reply before the timeout";
            ::close(fd);
            return false;
        }
        // A ping socket hands back the ICMP message itself; a raw socket would
        // prepend the IP header. Tolerate both rather than assume.
        std::size_t off = 0;
        if (n >= 20 && (reply[0] >> 4) == 4) off = static_cast<std::size_t>(reply[0] & 0x0F) * 4;
        if (static_cast<std::size_t>(n) < off + sizeof(EchoHeader)) continue;

        const auto* r = reinterpret_cast<const EchoHeader*>(reply + off);
        if (r->type != ICMP_ECHO_REPLY) continue;
        if (ntohs(r->seq) != seq) continue;

        const auto dt = std::chrono::steady_clock::now() - t0;
        ms = static_cast<unsigned>(
            std::chrono::duration_cast<std::chrono::milliseconds>(dt).count());
        ::close(fd);
        return true;
    }
}

#else  // _WIN32

/// `IcmpSendEcho` from iphlpapi: native, needs no elevation, and keeps us off
/// `ping.exe` and its localised output entirely.
bool icmp_echo(const IpAddress& addr, unsigned timeout_s, unsigned& ms, std::string& err) {
    const HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) {
        err = "ICMP handle refused by this host";
        return false;
    }
    IPAddr target = 0;
    std::memcpy(&target, addr.bytes.data(), sizeof target);

    char          payload[16] = "jnext-esp-ping!";
    std::vector<char> reply(sizeof(ICMP_ECHO_REPLY) + sizeof payload + 8);
    const DWORD   count = IcmpSendEcho(h, target, payload, sizeof payload, nullptr, reply.data(),
                                       static_cast<DWORD>(reply.size()),
                                       (timeout_s ? timeout_s : 1u) * 1000);
    IcmpCloseHandle(h);
    if (count == 0) {
        err = "no reply before the timeout";
        return false;
    }
    const auto* r = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
    if (r->Status != IP_SUCCESS) {
        err = "host did not answer";
        return false;
    }
    ms = static_cast<unsigned>(r->RoundTripTime);
    return true;
}

#endif

/// The real pinger. It owns a host string, a job block and a verdict, and it
/// borrows the detached-thread lifetime story the resolver proved: destroying
/// it mid-ping drops one `shared_ptr` and returns at once.
class IcmpPinger final : public EspPinger {
public:
    IcmpPinger(const AddressPolicy& policy, unsigned timeout_s)
        : policy_(policy), timeout_s_(timeout_s) {}

    bool begin(const std::string& host) override {
        if (state_ == PingState::Pinging) return false;
        if (!plausible_ping_host(host)) return false;
        host_ = host;
        rtt_  = 0;
        err_.clear();
        state_ = PingState::Pinging;
        job_   = std::make_shared<PingJob>();

        auto              job = job_;
        const std::string h   = host_;
        const unsigned    t   = timeout_s_;
        const AddressPolicy p = policy_;
        try {
            std::thread([job, h, t, p]() {
                // NOTHING MAY ESCAPE. An exception leaving a thread's entry
                // point is `std::terminate` — a process abort, not an error.
                bool        ok = false;
                unsigned    ms = 0;
                std::string err;
                try {
                    // Resolution happens HERE, on the worker, which is what
                    // makes the address policy applicable at all — the
                    // shell-out draft could not see the address `ping`
                    // resolved to.
                    std::vector<IpAddress> found;
                    std::string            rerr;
                    if (!net::resolve(h, /*numeric_only=*/false, found, rerr) || found.empty()) {
                        err = "cannot resolve '" + h + "'";
                    } else {
                        IpAddress  chosen;
                        DenyReason why = DenyReason::None;
                        if (!select_candidate(found, p, chosen, why)) {
                            // Indistinguishable from any other failure on the
                            // wire, deliberately — see EspGatedPinger.
                            err = "address policy refused every address for '" + h + "'";
                        } else if (chosen.family != IpFamily::V4) {
                            // IPv4 only, matching the 1.x AT+PING surface.
                            err = "no IPv4 address for '" + h + "'";
                        } else {
                            ok = icmp_echo(chosen, t, ms, err);
                        }
                    }
                } catch (const std::exception& e) {
                    err = std::string("ping failed: ") + e.what();
                } catch (...) {
                    err = "ping failed with a non-std exception";
                }
                job->ok  = ok;
                job->ms  = ms;
                job->err = std::move(err);
                job->done.store(true, std::memory_order_release);
            }).detach();
        } catch (const std::system_error& e) {
            job_.reset();
            err_   = std::string("cannot start ping thread: ") + e.what();
            state_ = PingState::Failed;
            return true;  // accepted then failed; the caller reads state()
        }
        return true;
    }

    void poll() override {
        if (state_ != PingState::Pinging || !job_) return;
        if (!job_->done.load(std::memory_order_acquire)) return;
        const std::shared_ptr<PingJob> job = std::move(job_);
        if (!job->ok) {
            err_   = job->err.empty() ? "ping failed" : job->err;
            state_ = PingState::Failed;
            log_debug("AT+PING '{}' failed: {}", host_, err_);
            return;
        }
        rtt_   = job->ms;
        state_ = PingState::Done;
        log_info("ESP pinged '{}': {} ms", host_, rtt_);
    }

    PingState          state() const override      { return state_; }
    unsigned           rtt_ms() const override     { return rtt_; }
    const std::string& last_error() const override { return err_; }

    void reset() override {
        job_.reset();
        state_ = PingState::Idle;
        rtt_   = 0;
        err_.clear();
    }

private:
    AddressPolicy            policy_;
    unsigned                 timeout_s_;
    std::string              host_;
    unsigned                 rtt_   = 0;
    std::string              err_;
    PingState                state_ = PingState::Idle;
    std::shared_ptr<PingJob> job_;
};

}  // namespace

bool plausible_ping_host(const std::string& host) {
    if (host.empty() || host.size() > 255) return false;
    if (host.front() == '-') return false;
    for (const unsigned char c : host) {
        const bool allowed = std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':';
        if (!allowed) return false;
    }
    return true;
}

std::unique_ptr<EspPinger> make_icmp_pinger(const AddressPolicy& policy, unsigned timeout_s) {
    return std::unique_ptr<EspPinger>(new IcmpPinger(policy, timeout_s));
}

}  // namespace esp
