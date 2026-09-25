#include "esp01/esp_sntp.h"

#include "esp01/esp_log.h"
#include "esp01/esp_ping.h"          // plausible_ping_host — same host alphabet
#include "esp01/esp_socket_platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace esp {
namespace {

/// Seconds between 1900-01-01 (the NTP epoch) and 1970-01-01 (the Unix one).
/// The single constant an off-by-70-years bug hides behind, which is why
/// `ntp_to_unix` is a separately tested pure function rather than two lines
/// buried in the socket path.
constexpr std::int64_t NTP_TO_UNIX_EPOCH = 2208988800LL;

constexpr std::size_t NTP_PACKET_BYTES     = 48;
constexpr std::size_t NTP_TRANSMIT_OFFSET  = 40;  ///< transmit timestamp, seconds
constexpr std::uint16_t NTP_PORT           = 123;

struct SntpJob {
    std::atomic<bool> done{false};
    bool              ok = false;
    std::int64_t      unix_time = 0;
    std::string       err;
};

#if defined(_WIN32)
using SocketFd = SOCKET;
constexpr SocketFd kBadSocket = INVALID_SOCKET;
inline void close_socket(SocketFd s) { ::closesocket(s); }
#else
using SocketFd = int;
constexpr SocketFd kBadSocket = -1;
inline void close_socket(SocketFd s) { ::close(s); }
#endif

/// One SNTP exchange: 48 bytes out, 48 bytes back, one field read.
///
/// NO DEPENDENCY. SNTP's client mode is a single datagram whose only field we
/// need is a big-endian 32-bit second count. Pulling in a library for that
/// would be the larger change, not the smaller one.
bool sntp_query(const IpAddress& addr, unsigned timeout_s, std::int64_t& out, std::string& err) {
    const SocketFd fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == kBadSocket) {
        err = "could not open a UDP socket";
        return false;
    }

#if defined(_WIN32)
    DWORD tv = (timeout_s ? timeout_s : 1u) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv);
#else
    timeval tv{};
    tv.tv_sec  = static_cast<long>(timeout_s ? timeout_s : 1u);
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif

    std::uint8_t packet[NTP_PACKET_BYTES] = {};
    // LI = 0 (no warning), VN = 3, Mode = 3 (client). Version 3 rather than 4
    // because that is what the 1.x firmware's own SNTP client speaks, and every
    // public server answers it.
    packet[0] = 0x1B;

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(NTP_PORT);
    std::memcpy(&dst.sin_addr, addr.bytes.data(), sizeof dst.sin_addr);

    if (::sendto(fd, reinterpret_cast<const char*>(packet), sizeof packet, 0,
                 reinterpret_cast<sockaddr*>(&dst), sizeof dst) < 0) {
        err = "could not send the SNTP request";
        close_socket(fd);
        return false;
    }

    std::uint8_t reply[NTP_PACKET_BYTES] = {};
    const auto   n = ::recvfrom(fd, reinterpret_cast<char*>(reply), sizeof reply, 0, nullptr,
                                nullptr);
    close_socket(fd);
    if (n < static_cast<decltype(n)>(NTP_PACKET_BYTES)) {
        err = "no SNTP reply before the timeout";
        return false;
    }

    const std::uint32_t secs =
        (static_cast<std::uint32_t>(reply[NTP_TRANSMIT_OFFSET + 0]) << 24) |
        (static_cast<std::uint32_t>(reply[NTP_TRANSMIT_OFFSET + 1]) << 16) |
        (static_cast<std::uint32_t>(reply[NTP_TRANSMIT_OFFSET + 2]) << 8) |
        (static_cast<std::uint32_t>(reply[NTP_TRANSMIT_OFFSET + 3]));
    if (!ntp_to_unix(secs, out)) {
        err = "the SNTP reply carried no usable timestamp";
        return false;
    }
    return true;
}

class UdpSntpClient final : public EspSntpClient {
public:
    UdpSntpClient(const AddressPolicy& policy, unsigned timeout_s)
        : policy_(policy), timeout_s_(timeout_s) {}

    bool begin(const std::string& server) override {
        if (state_ == SntpState::Querying) return false;
        if (!plausible_ping_host(server)) return false;   // same host alphabet
        server_ = server;
        time_   = 0;
        err_.clear();
        state_ = SntpState::Querying;
        job_   = std::make_shared<SntpJob>();

        auto                job = job_;
        const std::string   sv  = server_;
        const unsigned      t   = timeout_s_;
        const AddressPolicy p   = policy_;
        try {
            std::thread([job, sv, t, p]() {
                // NOTHING MAY ESCAPE — an exception out of a thread entry point
                // is std::terminate, a process abort rather than an error.
                bool         ok = false;
                std::int64_t when = 0;
                std::string  err;
                try {
                    std::vector<IpAddress> found;
                    std::string            rerr;
                    if (!net::resolve(sv, /*numeric_only=*/false, found, rerr) || found.empty()) {
                        err = "cannot resolve '" + sv + "'";
                    } else {
                        IpAddress  chosen;
                        DenyReason why = DenyReason::None;
                        if (!select_candidate(found, p, chosen, why)) {
                            // Same rule as AT+CIPSTART and AT+PING: the guest
                            // may not reach an address the policy refuses, and
                            // an NTP server is not an exception to that.
                            err = "address policy refused every address for '" + sv + "'";
                        } else if (chosen.family != IpFamily::V4) {
                            err = "no IPv4 address for '" + sv + "'";
                        } else {
                            ok = sntp_query(chosen, t, when, err);
                        }
                    }
                } catch (const std::exception& e) {
                    err = std::string("SNTP query failed: ") + e.what();
                } catch (...) {
                    err = "SNTP query failed with a non-std exception";
                }
                job->ok        = ok;
                job->unix_time = when;
                job->err       = std::move(err);
                job->done.store(true, std::memory_order_release);
            }).detach();
        } catch (const std::system_error& e) {
            job_.reset();
            err_   = std::string("cannot start SNTP thread: ") + e.what();
            state_ = SntpState::Failed;
            return true;
        }
        return true;
    }

    void poll() override {
        if (state_ != SntpState::Querying || !job_) return;
        if (!job_->done.load(std::memory_order_acquire)) return;
        const std::shared_ptr<SntpJob> job = std::move(job_);
        if (!job->ok) {
            err_   = job->err.empty() ? "SNTP query failed" : job->err;
            state_ = SntpState::Failed;
            log_debug("AT+CIPSNTPTIME query to '{}' failed: {}", server_, err_);
            return;
        }
        time_  = job->unix_time;
        state_ = SntpState::Done;
        log_info("ESP got the time from '{}'", server_);
    }

    SntpState          state() const override      { return state_; }
    std::int64_t       unix_time() const override  { return time_; }
    const std::string& last_error() const override { return err_; }

    void reset() override {
        job_.reset();
        state_ = SntpState::Idle;
        time_  = 0;
        err_.clear();
    }

private:
    AddressPolicy            policy_;
    unsigned                 timeout_s_;
    std::string              server_;
    std::int64_t             time_  = 0;
    std::string              err_;
    SntpState                state_ = SntpState::Idle;
    std::shared_ptr<SntpJob> job_;
};

}  // namespace

bool ntp_to_unix(std::uint32_t ntp_seconds, std::int64_t& unix_seconds) {
    // A ZERO TIMESTAMP MEANS "UNSYNCHRONISED", not 1900-01-01. A server that
    // has not itself synced answers with zeros.
    //
    // THIS CHECK IS SUBSUMED BY THE NEXT ONE AND IS KEPT ANYWAY — established
    // by mutation, not by inspection: deleting it leaves every row green,
    // because zero minus the epoch offset is negative and the `< 0` test
    // refuses it. The two express DIFFERENT intents, though — "the server said
    // it does not know" versus "this predates the Unix epoch" — and keeping
    // them separate means a later change to either cannot silently turn an
    // unsynchronised reply into a date.
    if (ntp_seconds == 0) return false;
    unix_seconds = static_cast<std::int64_t>(ntp_seconds) - NTP_TO_UNIX_EPOCH;
    // Before the Unix epoch is equally unusable: it means the reply predates
    // 1970, which no real server sends and which a guest cannot render.
    if (unix_seconds < 0) return false;
    return true;
}

std::string format_sntp_time(std::int64_t unix_seconds, int timezone_hours) {
    // DELIBERATELY NOT `localtime`/`asctime`: those consult the HOST's timezone
    // and locale. This string must be a function of the guest's configured
    // `<timezone>` and nothing else — a module that reported the developer's
    // timezone, or Spanish weekday names, would be leaking host state into the
    // guest, which design-doc §8.3 forbids.
    std::int64_t t = unix_seconds + static_cast<std::int64_t>(timezone_hours) * 3600;
    if (t < 0) t = 0;

    const std::int64_t days_total = t / 86400;
    std::int64_t       rem        = t % 86400;
    const int          hour       = static_cast<int>(rem / 3600);
    rem %= 3600;
    const int minute = static_cast<int>(rem / 60);
    const int second = static_cast<int>(rem % 60);

    // 1970-01-01 was a Thursday.
    static const char* const kDays[]   = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char* const dow = kDays[static_cast<std::size_t>(days_total % 7)];

    // Civil-date conversion, proleptic Gregorian, no library and no locale.
    std::int64_t days = days_total;
    int          year = 1970;
    for (;;) {
        const bool leap  = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        const int  in_yr = leap ? 366 : 365;
        if (days < in_yr) break;
        days -= in_yr;
        ++year;
    }
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    int        mlen[12] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int        month = 0;
    while (month < 12 && days >= mlen[month]) {
        days -= mlen[month];
        ++month;
    }
    const int day = static_cast<int>(days) + 1;

    char buf[64];
    std::snprintf(buf, sizeof buf, "%s %s %02d %02d:%02d:%02d %d", dow, kMonths[month], day, hour,
                  minute, second, year);
    return buf;
}

std::unique_ptr<EspSntpClient> make_udp_sntp_client(const AddressPolicy& policy,
                                                    unsigned             timeout_s) {
    return std::unique_ptr<EspSntpClient>(new UdpSntpClient(policy, timeout_s));
}

}  // namespace esp
