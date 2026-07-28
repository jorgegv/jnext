#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// Non-blocking outbound TCP transport for the emulated ESP-01 (GH #25,
/// branch 2 of 5). This branch is the TRANSPORT ONLY: no AT parser, no ESP
/// state machine, no `UartDevice` wiring, no CLI flags, no frame-loop call
/// site. Those are branches 3 and 4.
///
/// ---------------------------------------------------------------------------
/// WHY THIS INTERFACE CANNOT BLOCK
/// ---------------------------------------------------------------------------
/// The consumer is an emulated peripheral serviced from jnext's frame loop. A
/// single blocking syscall there stalls the whole machine: audio underruns
/// (the mixer is fed per frame) and intermittent FAILs in the regression
/// suite's real-time-paced rows (`audio-underrun-func`,
/// `screenshot-paused-func` — the project's early-warning system). So the
/// interface is shaped so that a caller CANNOT ask it to wait, even by
/// mistake:
///
///   1. There is no synchronous connect in the vtable AT ALL. `begin_connect`
///      only *starts* an attempt; completion is observed later via `poll()` +
///      `state()`. A caller cannot write `connect(); use_it();` because
///      `begin_connect` returning true does not mean connected.
///   2. `poll()` takes NO timeout argument. There is no knob to pass a
///      non-zero wait, so the zero-timeout readiness check is the only
///      behaviour reachable through the interface.
///   3. `send`/`recv` return a COUNT, never a completion. There is no
///      "transfer exactly N bytes" call to accidentally block on: a partial
///      result is the only contract, so any loop-until-complete a caller
///      writes is visibly its own spin, not something this layer hid.
///   4. Nothing returns a file descriptor or a native handle, so a caller
///      cannot reach around the interface and issue its own blocking call on
///      the socket.
///
/// The single genuinely blocking call in the implementation is `getaddrinfo`
/// — see the DNS note on `make_socket_transport` below. It is confined to
/// `poll()` (the one method the host loop already calls in wall-clock time),
/// preceded by an `AI_NUMERICHOST` fast path that never touches the network,
/// and it is the reason `Resolving` is a distinct observable state: swapping
/// in a resolver thread later is a change behind this interface, not to it.
///
/// ---------------------------------------------------------------------------
/// THE TEST SEAM
/// ---------------------------------------------------------------------------
/// `EspTransport` is abstract, following the local precedent set by
/// `sdcard::DownloadFn` / `ConfirmFn` (sdcard_provisioner.h:39, whose own
/// comment reads "behind std::function seams so tests never touch the
/// network"). A pure vtable is used here rather than a bundle of
/// `std::function`s because a transport is a stateful object with seven
/// correlated operations, not one call. Branch 3's AT engine takes an
/// `EspTransport&` and is testable against an in-memory fake with no sockets,
/// no DNS and no listener.
///
/// The ADDRESS POLICY is deliberately a pure function (`classify_address` /
/// `select_candidate`) rather than an `if` buried inside connect: it is the
/// security-relevant part, it must be exhaustively unit-testable without a
/// network, and it must be OVERRIDABLE — this suite's own socket tests talk
/// to an in-process listener on 127.0.0.1, which the default policy denies.
namespace esp {

// ---------------------------------------------------------------------------
// Addresses
// ---------------------------------------------------------------------------

enum class IpFamily : std::uint8_t { V4, V6 };

/// A resolved numeric IP address in network byte order.
/// V4 uses `bytes[0..3]` and leaves the rest zero; V6 uses all 16.
struct IpAddress {
    IpFamily                     family = IpFamily::V4;
    std::array<std::uint8_t, 16> bytes{};
};

IpAddress   ipv4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);
IpAddress   ipv6(const std::array<std::uint8_t, 16>& raw);
/// Dotted-quad for V4; full uncompressed 8-group hex for V6 (no `::` elision —
/// a log line must be unambiguous, not short).
std::string to_string(const IpAddress& ip);
bool        operator==(const IpAddress& a, const IpAddress& b);

/// Collapse the IPv6 forms that embed an IPv4 address into the plain V4
/// address they denote, so one set of V4 rules covers all of them. Without
/// this, `::ffff:127.0.0.1` is a one-line bypass of the loopback deny.
/// Handles:
///   * IPv4-mapped     ::ffff:0:0/96   (what a dual-stack getaddrinfo emits)
///   * IPv4-compatible ::/96           (deprecated by RFC 4291 but still parsed)
///   * NAT64 well-known 64:ff9b::/96   (RFC 6052 — a real translated-reach path)
/// Anything else is returned unchanged.
IpAddress normalize(const IpAddress& ip);

// ---------------------------------------------------------------------------
// Address policy — the security gate, enforced HERE at the transport
// ---------------------------------------------------------------------------

enum class DenyReason : std::uint8_t {
    None = 0,             ///< allowed
    Loopback,             ///< 127.0.0.0/8, ::1
    LinkLocal,            ///< 169.254.0.0/16, fe80::/10
    CloudMetadata,        ///< 169.254.169.254, 100.100.100.200, fd00:ec2::254
    Unspecified,          ///< 0.0.0.0/8, ::
    MulticastOrReserved,  ///< 224.0.0.0/4, 240.0.0.0/4, 255.255.255.255, ff00::/8
    Private,              ///< RFC1918 / CGNAT / ULA — ALLOWED by default
};
const char* deny_reason_text(DenyReason r);

/// Per owner decision on GH #25 (2026-07-28): the emulated ESP MUST be able to
/// reach machines on the user's LAN, so RFC1918 is ALLOWED and is not part of
/// the deny set. What is denied is the host itself and the host's own
/// infrastructure: loopback, link-local (which is where every cloud metadata
/// service lives) and the known metadata addresses.
///
/// Each rule is an independent flag so a caller can relax exactly one. The
/// only production caller uses the defaults; the unit suite flips
/// `deny_loopback` so it can talk to its own in-process listener, which is
/// precisely why the policy is a parameter and not a hard-coded `if`.
struct AddressPolicy {
    bool deny_loopback           = true;
    bool deny_link_local         = true;
    bool deny_cloud_metadata     = true;
    bool deny_unspecified        = true;
    bool deny_multicast_reserved = true;
    bool deny_private            = false;  ///< RFC1918/ULA reachable (owner decision)
};

/// Pure. Returns `DenyReason::None` when the address may be connected to.
/// Normalizes IPv4-in-IPv6 forms first, so a mapped address is judged by the
/// V4 rules it actually reaches.
DenyReason classify_address(const IpAddress& ip, const AddressPolicy& policy);

/// Pure. Pick which resolved address to connect to.
///
/// Prefers the first allowed IPv4 candidate and falls back to the first
/// allowed IPv6 one. That ordering is faithful as well as convenient: the
/// ESP-01 on a real Next runs ESP8266 AT firmware with an IPv4-only stack,
/// and every evidenced consumer (nextsync, NXtel, the dot commands) is IPv4.
/// It also sidesteps the classic "getaddrinfo returned AAAA first, host has no
/// IPv6 route, connect fails" trap without needing untestable
/// try-the-next-candidate fallback machinery in the socket path.
///
/// Returns false when nothing is connectable; `reason` then carries the deny
/// reason of the first candidate (`None` if the list was empty).
bool select_candidate(const std::vector<IpAddress>& candidates,
                      const AddressPolicy& policy, IpAddress& chosen,
                      DenyReason& reason);

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

enum class TransportState : std::uint8_t {
    Idle,        ///< nothing open; a fresh transport starts here
    Resolving,   ///< begin_connect accepted; name lookup happens in poll()
    Connecting,  ///< socket open, TCP handshake in flight
    Connected,   ///< send/recv are live
    Closed,      ///< orderly close, by us (close()) or by the peer (EOF)
    Failed,      ///< last_error() says why
};
const char* transport_state_text(TransportState s);

class EspTransport {
public:
    virtual ~EspTransport() = default;

    /// Start an outbound TCP connection. NEVER blocks and never resolves:
    /// it records the target, moves to `Resolving` and returns.
    ///
    /// Returns false — with the state untouched — when the transport is busy
    /// (`Resolving`/`Connecting`/`Connected`) or the port is 0. A false return
    /// is "request rejected", not "connect failed"; a connect failure is
    /// reported later as `Failed` + `last_error()`.
    virtual bool begin_connect(const std::string& host, std::uint16_t port) = 0;

    /// Advance the state machine using zero-timeout readiness checks.
    /// Idempotent and cheap in every state; a no-op in `Idle`/`Closed`/`Failed`.
    /// Safe to call every frame.
    virtual void poll() = 0;

    virtual TransportState     state() const      = 0;
    /// Empty unless the last transition was a failure.
    virtual const std::string& last_error() const = 0;
    /// The address actually connected to. Meaningful from `Connected` onward.
    virtual const IpAddress&   peer_address() const = 0;

    /// Non-blocking. Returns how many of `len` bytes the kernel accepted
    /// (0 when the send buffer is full — that is normal, not an error).
    /// On a real error the state becomes `Failed`/`Closed`; check `state()`.
    virtual std::size_t send(const std::uint8_t* data, std::size_t len) = 0;

    /// Non-blocking. Returns how many bytes were read (0 when none are
    /// available). A peer close moves the state to `Closed`, an error to
    /// `Failed`; both also return 0, so the caller must consult `state()`
    /// rather than treating 0 as "try again forever".
    virtual std::size_t recv(std::uint8_t* buf, std::size_t cap) = 0;

    /// Release the socket. `Idle` stays `Idle` (nothing was open); every other
    /// state becomes `Closed`. Always safe, always immediate.
    virtual void close() = 0;
};

/// Build the real POSIX/Winsock transport.
///
/// DNS: name resolution is SYNCHRONOUS, and that is a deliberate, reversible
/// choice rather than an oversight. `getaddrinfo` has no portable
/// non-blocking form, and the alternative — a resolver thread with a result
/// slot — would be the FIRST thread jnext runs while the frame loop is live
/// (today's only `std::thread`, gui/sdcard_download_dialog.cpp:118, runs
/// before the loop exists). That is not free: `emulator_cold_boot` destroys
/// and placement-news the `Emulator` at the SAME address on every hard reset
/// (platform/emulator_boot.h:67-77), which is exactly the situation in which a
/// still-running thread holding a result slot writes into a reconstructed
/// object and corrupts it silently — the hazard uart_device.h:22-35 documents
/// at length.
///
/// What the stall actually costs is bounded and rare:
///   * an IP-literal target never resolves at all (`AI_NUMERICHOST` is tried
///     first and short-circuits with no network traffic) — and nextsync, the
///     hard v1.0 acceptance target, is normally configured with a literal;
///   * a warm cache is sub-millisecond;
///   * a cold lookup is one-off per `AT+CIPSTART`, on a path where the guest
///     is already committed to a multi-second connect;
///   * no unit or regression test resolves anything, so the paced rows are
///     untouched.
///
/// The honest downside is the unreachable-resolver case, where the stall is a
/// resolver timeout (seconds) and the GUI freezes for it. It is logged loudly
/// (`esp01` at warn, with the measured elapsed time) rather than hidden, so if
/// it ever bites a user the evidence is in the log — and the `Resolving` state
/// exists precisely so that moving the lookup onto a thread later changes only
/// this file.
std::unique_ptr<EspTransport> make_socket_transport(const AddressPolicy& policy);

}  // namespace esp
