#pragma once

#include "esp01/esp_socket.h"

#include <memory>
#include <string>

/// ICMP echo for `AT+PING` (GH #154, owner decision on Q6).
///
/// IN-PROCESS, ON AN UNPRIVILEGED ICMP SOCKET — NOT by spawning `ping(8)`.
///
/// THE EVIDENCE THAT CHOSE THIS. An earlier draft shelled out to the platform
/// `ping`, on the reasoning that the system had already granted that binary the
/// privilege to send an echo. Measuring the development host disproved the
/// premise: `/usr/bin/ping` there is plain `0755` — no setuid bit, no file
/// capabilities — and it works because `net.ipv4.ping_group_range` is
/// `0 2147483647`, i.e. the kernel permits ANY group to open an ICMP DATAGRAM
/// socket. That is a capability jnext already has, on the same terms, so the
/// binary was never the thing holding the privilege.
///
/// WHAT SHELLING OUT WOULD HAVE COST, all of it deleted by doing this in
/// process:
///   * THE FLATPAK BUILD COULD NEVER PING AT ALL. Verified by running all
///     three installed runtimes: `org.kde.Platform` 6.8, 6.10 and 6.11 ship
///     `ffmpeg` but NO `ping`. `--share=network` grants a network, not a
///     binary. An in-process socket works there exactly as it does natively.
///   * OUTPUT PARSING AND ITS LOCALE TRAPS. There is no text to read, so
///     `tiempo=` versus `time=`, the summary line's decoy `time 0ms`, and the
///     comma-decimal separator stop being hazards rather than being defended
///     against.
///   * AN ARGV-INJECTION SURFACE fed by a guest-supplied hostname.
///
/// PRIVILEGE IS STILL NOT GUARANTEED, and that is a first-class outcome rather
/// than an edge case. A host with a restrictive `ping_group_range` refuses the
/// socket with `EACCES`/`EPERM`; a Windows API call can fail. Either way the
/// answer is the module's honest failure reply — never a crash, never a hang,
/// never a silent success. See `PingState::Failed`.
///
/// IPv4 ONLY, matching the 1.x `AT+PING` surface that Q1 settled.

namespace esp {

/// Where a ping is in its life. Deliberately the shape of `ResolveState`: a
/// consumer that has driven one has driven both.
enum class PingState {
    Idle,     ///< nothing asked for
    Pinging,  ///< `begin()` accepted; the answer arrives via `poll()`
    Done,     ///< `rtt_ms()` is valid
    Failed,   ///< unreachable, timed out, or no usable `ping` on this host
};

/// Round-trip time for one echo, or a failure.
///
/// A SEAM FOR THE SAME REASON `ResolveFn` IS ONE: the suite must be able to
/// drive every outcome — success, timeout, a missing binary, a hostile
/// hostname — without a network and without a real `ping`. No test in this
/// project may depend on either.
class EspPinger {
public:
    virtual ~EspPinger() = default;

    /// Start one echo. Returns false — state untouched — when a ping is
    /// already in flight, or `host` is empty or not a plausible host.
    /// NEVER blocks: the child process runs on a detached thread.
    virtual bool begin(const std::string& host) = 0;

    /// Advance. Idempotent, cheap in every state, and **must not block** —
    /// the same contract `EspTransport::poll` carries, for the same reason.
    virtual void poll() = 0;

    virtual PingState state() const = 0;

    /// Valid only in `Done`. Milliseconds, rounded; 0 is a legitimate value
    /// for a sub-millisecond localhost reply.
    virtual unsigned rtt_ms() const = 0;

    /// Empty unless the last transition was a failure.
    virtual const std::string& last_error() const = 0;

    /// Abandon any ping in flight and return to `Idle`. Always safe.
    virtual void reset() = 0;
};

/// Is `host` something we are willing to hand to `ping` as an argument?
///
/// THIS IS A SECURITY CHECK, NOT A CONVENIENCE. The string comes from the
/// GUEST. Two distinct hazards, and only one of them is handled by using an
/// argv array:
/// SINCE THE PING IS IN PROCESS, NEITHER SHELL NOR OPTION INJECTION EXISTS ANY
/// MORE — there is no command line for a hostname to escape into. The check
/// survives the change because what it defends was never only the shell: it
/// bounds what reaches the RESOLVER, and keeps a hostile string out of the log
/// lines and the address-policy path. A name that cannot be a name is refused
/// before any of that, which is narrower and cheaper than finding out from
/// `getaddrinfo`. Its rejections stay strict for a second reason: it is the
/// check that would have to come back untouched if anything here ever spawned
/// a process again.
///

/// Accepts only what a hostname or IP literal can contain: letters, digits,
/// `.`, `-`, `_` and `:` (IPv6). Refuses an empty string, a leading `-`, and
/// anything over 255 bytes.
bool plausible_ping_host(const std::string& host);

/// Build the real pinger: an unprivileged ICMP echo, in process.
///
/// `policy` judges the address the name RESOLVES to, exactly as it judges the
/// one `AT+CIPSTART` would dial and the one `AT+CIPDOMAIN` would report. That
/// is a deliberate widening over the shell-out draft, which could not apply it
/// at all because `ping` did its own resolution inside a process we never saw:
/// a guest that may not CONNECT to the cloud-metadata address must not be able
/// to learn it is there by pinging it either. `--esp-allow` gates the NAME on
/// top of this, in `EspGatedPinger`.
///
/// `timeout_s` bounds the receive, so a host that silently drops echo requests
/// costs one timeout rather than hanging a guest.
std::unique_ptr<EspPinger> make_icmp_pinger(const AddressPolicy& policy,
                                            unsigned             timeout_s = 4);

}  // namespace esp
