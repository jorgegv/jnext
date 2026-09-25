#pragma once

#include "esp01/esp_socket.h"

#include <cstdint>
#include <memory>
#include <string>

/// SNTP for `AT+CIPSNTPCFG` / `AT+CIPSNTPTIME?` (GH #154, owner decision Q7).
///
/// THE OWNER ASKED FOR A REAL QUERY — "configuring the server and querying the
/// real NTP server" — so this sends an actual SNTP request over UDP/123 rather
/// than answering from a clock jnext already has. That has a consequence worth
/// meeting head-on rather than discovering from a flaky screenshot:
///
///   **IT DIVERGES FROM `--rtc`.** `--rtc` pins the emulated real-time clock so
///   that boot screenshots are deterministic. A guest that asks SNTP for the
///   time gets the answer a real time server gives, which is wall-clock, even
///   when the RTC is pinned. The two clocks disagree on purpose. A regression
///   row that screenshots an SNTP-derived date would therefore be
///   non-deterministic — which is why none does, and why this is documented in
///   the design doc and the user guide rather than left as a trap.
///
/// NO NEW DEPENDENCY. SNTP is a 48-byte request and one 32-bit field of the
/// reply; there is no library here, just a UDP datagram.
namespace esp {

enum class SntpState {
    Idle,      ///< nothing asked for
    Querying,  ///< `begin()` accepted; the answer arrives via `poll()`
    Done,      ///< `unix_time()` is valid
    Failed,    ///< no answer, refused by policy, or a malformed reply
};

/// One SNTP exchange. A seam for the same reason `EspPinger` and `ResolveFn`
/// are: no test in this project may depend on an external network, and the
/// threaded pattern is already established beside it.
class EspSntpClient {
public:
    virtual ~EspSntpClient() = default;

    /// Query `server`. Returns false — state untouched — when one is already
    /// in flight or `server` is not a plausible host. Never blocks.
    virtual bool begin(const std::string& server) = 0;

    /// Advance. Idempotent, cheap, and **must not block**.
    virtual void poll() = 0;

    virtual SntpState state() const = 0;

    /// Valid only in `Done`: seconds since the Unix epoch, UTC.
    virtual std::int64_t unix_time() const = 0;

    virtual const std::string& last_error() const = 0;
    virtual void               reset()            = 0;
};

/// Turn an NTP transmit timestamp into Unix seconds.
///
/// PURE AND SEPARATELY TESTED, because it is the one place an off-by-70-years
/// error hides. NTP counts seconds from 1900-01-01; Unix counts from
/// 1970-01-01; the difference is 2 208 988 800 seconds. A zero timestamp means
/// "unsynchronised" and is refused rather than turned into 1900.
bool ntp_to_unix(std::uint32_t ntp_seconds, std::int64_t& unix_seconds);

/// Format `unix_seconds` + `timezone_hours` the way `AT+CIPSNTPTIME?` answers:
/// asctime style, e.g. `Thu Aug 04 14:48:05 2016` (1.x manual §5.2.29).
///
/// DELIBERATELY NOT `std::asctime`/`localtime`: those consult the HOST's locale
/// and timezone, and this string must be a function of the guest's configured
/// `<timezone>` alone. A module that reported the developer's timezone would be
/// leaking host state into the guest, which §8.3 forbids.
std::string format_sntp_time(std::int64_t unix_seconds, int timezone_hours);

/// Build the real client. `policy` judges the resolved server address, exactly
/// as it judges every other address the guest can reach.
std::unique_ptr<EspSntpClient> make_udp_sntp_client(const AddressPolicy& policy,
                                                    unsigned             timeout_s = 4);

}  // namespace esp
