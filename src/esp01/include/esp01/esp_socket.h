#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/// Non-blocking outbound TCP/UDP transport for the emulated ESP-01 (GH #25,
/// branch 2 of 6; UDP added by GH #198). This branch is the TRANSPORT ONLY: no
/// AT parser, no ESP state machine, no `UartDevice` wiring, no CLI flags, no
/// frame-loop call site. Those are branches 3 and 4.
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
/// NOR DOES THE IMPLEMENTATION BLOCK. `getaddrinfo` — the one call in this
/// layer with no portable non-blocking form — runs on a short-lived resolver
/// thread, and `poll()` does no more than test a flag for its result. An
/// `AI_NUMERICHOST` fast path handles IP literals first, on the calling thread,
/// with no network traffic and no thread at all. `Resolving` was made a
/// distinct observable state precisely so that this swap could happen behind
/// the interface rather than to it, and it did: not one signature changed.
/// The lifetime argument that makes the thread safe is on
/// `make_socket_transport` below, and it is the load-bearing part.
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

/// The inverse of `to_string` for a NUMERIC address, and numeric only: a name
/// is rejected rather than looked up (GH #210).
///
/// It is the platform's `AI_NUMERICHOST` pass, so it accepts every spelling the
/// platform does — dotted-quad, `::1`, an IPv6 form with `::` elision — and
/// touches no network and no resolver. Refusing names is the point rather than
/// a limitation: the only caller is a host turning a configured BIND address
/// into one, and a bind address that could depend on DNS would be a bind
/// address that could change under the user (design doc §13.4).
bool parse_ip(const std::string& text, IpAddress& out);
bool        operator==(const IpAddress& a, const IpAddress& b);

/// Collapse the IPv6 forms that ARE an IPv4 address into that address, so one
/// set of V4 rules covers all of them. Without this, `::ffff:127.0.0.1` is a
/// one-line bypass of the loopback deny. Handles:
///   * IPv4-mapped     ::ffff:0:0/96   (what a dual-stack getaddrinfo emits)
///   * IPv4-compatible ::/96           (deprecated by RFC 4291 but still parsed)
///   * NAT64 well-known 64:ff9b::/96   (RFC 6052 — a real translated-reach path)
/// Anything else is returned unchanged. These three are ALIASES — the V6 form
/// and the V4 form name the same host — which is what makes collapsing them
/// lossless. Tunnelling prefixes are a different relation entirely; see
/// `tunnel_endpoint`.
IpAddress normalize(const IpAddress& ip);

/// If `ip` sits under a tunnelling prefix, yield the IPv4 endpoint that
/// packets for it are physically delivered to, and return true.
///
/// Currently that means 6to4 (`2002::/16`, RFC 3056): anything under
/// `2002:AABB:CCDD::/48` is encapsulated to the IPv4 address `AABB:CCDD`, so
/// `2002:7f00:1::1` reaches 127.0.0.1 on this host. That is NOT an alias
/// relation — the two are different hosts, one reached via the other — which
/// is why it is kept out of `normalize()`: folding it in would assert an
/// equality that does not hold, and would make `select_candidate` treat a 6to4
/// address as an IPv4 candidate although its socket is `AF_INET6`.
///
/// Teredo and ISATAP are deliberately excluded; the implementation states why
/// for each, and `POL-TUN-07..09` pin the exclusions so they cannot be
/// "fixed" silently.
bool tunnel_endpoint(const IpAddress& ip, IpAddress& endpoint);

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
    /// KNOWN LIMITATION, stated here because this is where someone would turn
    /// it off: clearing this flag does NOT make link-local peers reachable.
    /// `IpAddress` carries no scope id, and both platform twins drop
    /// `sin6_scope_id` on the way out of `resolve()` and never restore it in
    /// their sockaddr builders — so a `fe80::/10` connect would fail with
    /// EINVAL for want of a zone, not succeed. Nothing in production clears
    /// this flag today (there is no CLI for it; that is branch 4), and the
    /// unit suite only ever clears `deny_loopback`, so the gap is latent
    /// rather than live. Making the flag honest means adding a scope id to
    /// `IpAddress`, carrying it through `net::resolve` and
    /// `fill_sockaddr`/`begin_connect` in BOTH twins, and — the part that
    /// matters — finding a hermetic way to test it, which needs a link-local
    /// peer on a real interface. Deliberately not built blind: untested
    /// plumbing for an unreachable capability is worth less than this comment.
    bool deny_link_local         = true;
    bool deny_cloud_metadata     = true;
    bool deny_unspecified        = true;
    bool deny_multicast_reserved = true;
    bool deny_private            = false;  ///< RFC1918/ULA reachable (owner decision)
};

/// Pure. Returns `DenyReason::None` when the address may be connected to.
/// Normalizes IPv4-in-IPv6 aliases first, then — if the address is still
/// allowed — re-runs the same rules against any tunnelling endpoint
/// `tunnel_endpoint` yields, so a 6to4 wrapper cannot launder a denied
/// destination.
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

/// Which transport protocol a connection speaks (GH #198).
///
/// It is on the TRANSPORT rather than being a separate transport class because
/// the AT engine is handed ONE transport per connection slot, at construction
/// (`AtEngine::AtEngine`), and `AT+CIPSTART="UDP",…` chooses the protocol at
/// run time — a slot cannot swap its transport object mid-session.
///
/// THE TWO ARE NOT THE SAME SHAPE, and every difference below is load-bearing
/// rather than cosmetic:
///   * `Udp` never reaches `Connecting`. `::connect()` on a datagram socket only
///     records the peer; it completes immediately, so `begin_connect()` lands in
///     `Connected` on the first `poll()` and no handshake is ever in flight.
///   * `recv()` returning 0 means END OF STREAM for `Tcp` and NOTHING RECEIVED
///     for `Udp` — a datagram socket has no EOF, and a zero-length datagram is
///     legal. Reading a UDP 0 as EOF would close a live connection at the first
///     empty datagram a peer chose to send.
///   * `Udp` preserves DATAGRAM BOUNDARIES end to end. One `recv()` yields at
///     most one datagram (a buffer smaller than the datagram DISCARDS the
///     remainder — see `MAX_DATAGRAM` in esp_at.h), and one `send()` emits
///     exactly one datagram, all-or-nothing. The AT engine relies on both to
///     emit one `+IPD` per datagram.
enum class Protocol : std::uint8_t { Tcp, Udp };
const char* protocol_text(Protocol p);

/// ---------------------------------------------------------------------------
/// NO METHOD HERE MAY THROW — and what the module does if one does anyway
/// ---------------------------------------------------------------------------
/// Report failure through the state machine: `Failed` plus `last_error()`, or
/// a `false`/`0` return. That is the vocabulary every method already has, and
/// the AT engine already turns it into the `ERROR` every evidenced client
/// handles.
///
/// The reason it is a CONTRACT and not a preference is where these methods are
/// called from: `esp::ThreadedEsp` drives them on a worker thread, and an
/// exception that escapes a `std::thread` entry point does not propagate to
/// anybody — it calls `std::terminate` and takes the host program with it.
///
/// So the wrapper GUARANTEES it will not let that happen: it catches everything
/// around a service pass, loses that pass, counts it (`pass_exceptions()`),
/// logs the first occurrence at error, and keeps the worker alive so `stop()`
/// still joins. A throwing transport therefore costs connections, never the
/// process. Do not read that guarantee as permission — a lost service pass
/// during a transfer is data the guest never sees.
class EspTransport {
public:
    virtual ~EspTransport() = default;

    /// Start an outbound connection. NEVER blocks and never resolves: it
    /// records the target, moves to `Resolving` and returns.
    ///
    /// Returns false — with the state untouched — when the transport is busy
    /// (`Resolving`/`Connecting`/`Connected`) or the port is 0. A false return
    /// is "request rejected", not "connect failed"; a connect failure is
    /// reported later as `Failed` + `last_error()`.
    ///
    /// `local_port` is meaningful for `Protocol::Udp` only — it is
    /// `AT+CIPSTART`'s optional `<local port>` argument, and 0 means "let the
    /// OS pick", which is what every client that omits the argument gets. TCP
    /// ignores it: `AT+CIPSTART`'s fourth TCP argument is a KEEPALIVE, not a
    /// port, and binding a local port for an outbound TCP connection is a
    /// capability no evidenced client asks for.
    ///
    /// THE DEFAULTS ARE DECLARED HERE AND NOWHERE ELSE. An override must take
    /// all four parameters and must NOT restate a default: default arguments
    /// are bound statically, so a derived class with different ones would make
    /// the same call mean different things through a base reference and through
    /// a derived one. Declaring them once, on the interface, is what let GH #198
    /// widen this signature without touching the ~40 two-argument call sites in
    /// `esp_socket_test.cpp` — every one of which goes through an
    /// `EspTransport&` and therefore still means "plain outbound TCP".
    virtual bool begin_connect(const std::string& host, std::uint16_t port,
                               Protocol      protocol   = Protocol::Tcp,
                               std::uint16_t local_port = 0) = 0;

    /// Advance the state machine using zero-timeout readiness checks.
    /// Idempotent and cheap in every state; a no-op in `Idle`/`Closed`/`Failed`.
    /// Safe to call every frame.
    ///
    /// ─────────────────────────────────────────────────────────────────────
    /// THIS CALL MUST NOT BLOCK. It is a CONTRACT, not advice.
    /// ─────────────────────────────────────────────────────────────────────
    /// If you are writing your own transport, this is the one paragraph of
    /// this header you cannot skip.
    ///
    /// `esp::ThreadedEsp` (esp01/esp_threaded.h) runs this on a worker thread
    /// that its destructor JOINS — joining is what makes the wrapper safe to
    /// own as a member of an object that gets torn down and rebuilt, and it is
    /// not negotiable. But a thread parked in a syscall cannot be joined in
    /// bounded time, by anyone, ever. So the wrapper's shutdown is bounded by
    /// exactly ONE call to this function, and a transport that blocks here
    /// hands that duration straight to whoever destroys the wrapper.
    ///
    /// What that looks like in a host: in jnext the wrapper is destroyed by a
    /// cold boot and by quitting, so a transport that blocks for a 20 s
    /// resolver timeout freezes the emulator — GUI, audio and CPU — for 20 s
    /// when the user presses Reset. Measured on a 2000 ms blocking `poll()`:
    /// the destructor took 2007 ms.
    ///
    /// Do the slow part elsewhere and report progress through `state()`:
    /// `Resolving` and `Connecting` exist precisely so that a lookup or a
    /// handshake in flight is an OBSERVABLE STATE rather than a stall.
    ///
    /// THE TRANSPORT THIS MODULE SHIPS HONOURS THIS. It used to be the
    /// contract's one known violator: `make_socket_transport`'s name
    /// resolution was a synchronous `getaddrinfo`, so a hostname target could
    /// stall a wrapper shutdown for a resolver timeout (an IP literal never
    /// resolved at all and was never affected, nor were `send`, `recv` and
    /// `close`, which have always been non-blocking). Resolution now runs on
    /// its own thread and `poll()` only tests a flag — see that function's DNS
    /// note below for the lifetime design that makes the thread safe to
    /// abandon, which is what lets the destructor stay bounded.
    virtual void poll() = 0;

    virtual TransportState     state() const      = 0;
    /// Empty unless the last transition was a failure.
    virtual const std::string& last_error() const = 0;

    /// Why the current `Failed` state is a POLICY REFUSAL rather than a
    /// network fault — `DenyReason::None` when it is an ordinary failure
    /// (no route, timeout, connection refused, a lookup that found nothing).
    ///
    /// WHY THE DISTINCTION NEEDS ITS OWN ACCESSOR. Both outcomes land in
    /// `Failed` with a `last_error()` string, and a consumer that has to tell
    /// them apart otherwise has to match on that string — which makes a
    /// SECURITY signal depend on log wording nobody promised to keep stable.
    /// jnext renders a deliberate block differently from a network error (a
    /// red REFUSED cell, not a grey "failed" one), and that rendering is only
    /// as trustworthy as the thing it switches on. GH #161.
    ///
    /// Meaningful only while `state() == Failed`; a fresh `begin_connect()`
    /// clears it, exactly as it clears `last_error()`.
    ///
    /// DEFAULTED, NOT PURE, and deliberately so: a transport that enforces no
    /// address policy — every fake in a consumer's test suite, and any
    /// alternative implementation — can never produce a policy refusal, so
    /// `None` is not a placeholder for it but the correct answer. Making it
    /// pure would break every existing implementor of a published interface to
    /// tell them something they already know.
    virtual DenyReason denial_reason() const { return DenyReason::None; }

    /// The address actually connected to. Meaningful from `Connected` onward.
    virtual const IpAddress&   peer_address() const = 0;

    /// Non-blocking. Returns how many of `len` bytes the kernel accepted
    /// (0 when the send buffer is full — that is normal, not an error).
    /// On a real error the state becomes `Failed`/`Closed`; check `state()`.
    ///
    /// UDP IS ALL-OR-NOTHING: one call is one datagram, so the return is either
    /// `len` or 0. A caller must therefore never re-offer a partial remainder on
    /// a datagram transport — it would put a second, truncated datagram on the
    /// wire rather than finishing the first.
    virtual std::size_t send(const std::uint8_t* data, std::size_t len) = 0;

    /// Non-blocking. Returns how many bytes were read (0 when none are
    /// available). A peer close moves the state to `Closed`, an error to
    /// `Failed`; both also return 0, so the caller must consult `state()`
    /// rather than treating 0 as "try again forever".
    ///
    /// UDP YIELDS AT MOST ONE DATAGRAM PER CALL, and 0 NEVER means end of
    /// stream — a datagram socket has no EOF (`Protocol`). A datagram longer
    /// than `cap` is truncated and its remainder DISCARDED by the kernel, which
    /// is why the engine sizes its buffer at its `+IPD` ceiling rather than at
    /// its ordinary read chunk.
    virtual std::size_t recv(std::uint8_t* buf, std::size_t cap) = 0;

    /// Release the socket. `Idle` stays `Idle` (nothing was open); every other
    /// state becomes `Closed`. Always safe, always immediate.
    virtual void close() = 0;
};

/// How a hostname becomes addresses. Fill `out` with every address `host`
/// resolves to and return true, or return false and set `err`.
///
/// It is a seam for two reasons, in this order of importance:
///   1. IT IS CALLED ON A RESOLVER THREAD, so the suite needs a resolver it can
///      make arbitrarily slow, or make answer with a chosen address, to test
///      the asynchrony and the security gate deterministically. Timing a real
///      DNS server is a flaky test, not a hermetic one, and this module's
///      tests are hermetic by construction.
///   2. A consumer embedding the module may already own a resolver (c-ares, a
///      proxy, a fixed table) and should not be forced onto the platform's.
///
/// THE IP-LITERAL PATH IS NOT ROUTED THROUGH IT. A numeric target is always
/// parsed by the platform's `AI_NUMERICHOST` pass, so a supplied resolver can
/// neither slow down nor reinterpret what an IP literal means.
///
/// ---------------------------------------------------------------------------
/// WHAT THIS IS CALLED ON, AND WHAT HAPPENS IF IT MISBEHAVES
/// ---------------------------------------------------------------------------
/// If you are supplying one of these, this is the paragraph you cannot skip.
///
///   * IT RUNS ON A RESOLVER THREAD, not the caller's. It may block for as
///     long as it likes — that is the entire point, and the reason it exists
///     as a seam at all. It must not touch anything the calling thread owns
///     without synchronising, because the transport that started it may have
///     been destroyed before it returns.
///   * IT MAY THROW, AND THE MODULE GUARANTEES WHAT HAPPENS IF IT DOES. An
///     exception that escapes a `std::thread` entry point calls
///     `std::terminate` — a process abort, not an error. So the module catches
///     everything (`std::exception` and `...`) and degrades to a FAILED
///     LOOKUP: the transport goes to `Failed`, `last_error()` names the
///     exception, and the AT engine answers `ERROR` — a refusal path every
///     evidenced client already handles. That holds however far your resolver
///     got before throwing: the return value was never assigned, so the lookup
///     counts as failed and its address list is never consulted. The module
///     additionally discards a partially-filled `out`, as defence in depth
///     rather than as the thing that makes the guarantee.
///     Throwing is still the worse way to report failure — `return false` with
///     `err` set is cheaper and carries a better message — but it is a
///     supported way, not a way to kill the host program.
using ResolveFn = std::function<bool(const std::string& host,
                                     std::vector<IpAddress>& out, std::string& err)>;

/// Build the real POSIX/Winsock transport. `resolver` replaces the lookup of
/// NAMES only; leave it null for the platform's `getaddrinfo`.
///
/// DNS IS ASYNCHRONOUS, and the LIFETIME of the thread that does it is the
/// entire argument for why that is safe in a program which destroys and
/// placement-news its `Emulator` at the same address on every hard reset
/// (platform/emulator_boot.h:67-77).
///
/// `getaddrinfo` has no portable non-blocking form, so a name lookup runs on a
/// short-lived DETACHED thread, one per lookup. What that thread can reach is
/// the whole of the safety property:
///
///   * a `shared_ptr` to a heap-allocated result block, and NOTHING else — no
///     `this`, no transport, no engine, no emulator. The host name and the
///     resolver function are copies it owns.
///   * it writes that block exactly once and publishes it with a release
///     store to an atomic flag; it never touches the block again.
///   * it does not log. The log sink is a `std::function` a consumer may
///     replace at any moment, and this is not the only thread in the program;
///     the owning thread logs the outcome instead, which is also where the
///     information is (it knows the host, the port and the policy verdict).
///
/// So destroying a transport with a lookup in flight — a hard reset, a quit,
/// or an ordinary `close()` — drops one `shared_ptr` and returns IMMEDIATELY.
/// The block outlives the transport, the thread finishes writing into heap
/// memory that is provably still alive, and whichever of the two releases the
/// last reference frees it. No join, so no stall; no back-pointer, so no write
/// into a reconstructed object. That is exactly why detaching the WRAPPER's
/// worker is unacceptable while detaching this one is not: the worker owns the
/// engine, this thread owns a `vector<IpAddress>` and two strings.
///
/// The `AI_NUMERICHOST` fast path stays SYNCHRONOUS and stays first. A literal
/// involves no network at all, so a thread would be pure cost — and nextsync,
/// the hard v1.0 acceptance target, is normally configured with a literal.
///
/// What this buys, concretely, beyond not freezing the emulator on Reset or
/// quit: `AT+CIPSTART`'s deadline now bounds NAME RESOLUTION as well as the TCP
/// handshake, because `poll()` returns while the lookup is outstanding and the
/// engine's deadline check therefore gets to run at all.
std::unique_ptr<EspTransport> make_socket_transport(const AddressPolicy& policy,
                                                    ResolveFn resolver = nullptr);

// ---------------------------------------------------------------------------
// Listener — the INBOUND half (GH #210)
// ---------------------------------------------------------------------------

/// A listening socket, and the source of the `EspTransport`s an inbound
/// connection arrives as.
///
/// WHY IT IS A SECOND INTERFACE RATHER THAN A MODE OF `EspTransport`. A
/// transport models ONE connection: it has a peer, a state machine that walks
/// toward that peer, and buffers belonging to it. A listener has no peer, no
/// handshake and no data — it manufactures transports. Folding the two together
/// would put `begin_connect` and `accept` on the same object and leave every
/// caller checking which half of it is live.
///
/// ---------------------------------------------------------------------------
/// `poll()` MUST NOT BLOCK, for exactly the reason `EspTransport::poll()` must
/// not
/// ---------------------------------------------------------------------------
/// `esp::ThreadedEsp` drives this from the same worker loop and its destructor
/// JOINS, so shutdown is bounded by one `poll()` call here as well as by one on
/// the transport. Read the contract on `EspTransport::poll()`; it applies here
/// word for word, and the measurement behind it (a 2000 ms blocking poll made
/// the destructor take 2007 ms) did not care which object was slow.
///
/// ---------------------------------------------------------------------------
/// THE SPLIT BETWEEN `poll()` AND `accept()` IS THE THREADING DESIGN
/// ---------------------------------------------------------------------------
/// It mirrors `AtEngine::advance_transports()` / `service_transports()`, and
/// for the same reason. `poll()` is PURE SOCKET WORK — it takes whatever the
/// kernel has ready and parks it — so the engine runs it with its core lock
/// RELEASED. `accept()` hands a connection over to a connection SLOT, which is
/// engine state, so it runs under the lock. A listener that did both in one
/// call would force the socket work back inside the lock, which is the defect
/// §7.2 of the design doc records being measured and fixed.
///
/// NO METHOD MAY THROW, on the same terms as `EspTransport`: this is driven
/// from a `std::thread` entry point where an escaping exception is a process
/// abort rather than an error. Report failure through `listening()` and
/// `last_error()`.
class EspListener {
public:
    virtual ~EspListener() = default;

    /// Start listening on `port`. Returns false — with `last_error()` set and
    /// nothing listening — when the bind or the listen fails, which is what the
    /// AT layer turns into `ERROR`.
    ///
    /// `port` 0 asks the OS to choose, and `port()` then reports what it chose.
    /// That is not a guest-reachable spelling — `AT+CIPSERVER=1,0` is refused,
    /// because a guest that named no port has no way to be told which one it
    /// got — but a host that wants an ephemeral port has no other way to ask,
    /// and neither has this module's own suite, which is forbidden a fixed
    /// port.
    virtual bool open(std::uint16_t port) = 0;

    /// Stop listening and release the port. Always safe, always immediate,
    /// idempotent. Connections already accepted are NOT affected — they are
    /// their own objects by then, owned by whoever took them.
    virtual void close() = 0;

    virtual bool               listening() const  = 0;
    /// The port actually bound; 0 when not listening.
    virtual std::uint16_t      port() const       = 0;
    /// Empty unless the last `open()` failed or the listener faulted.
    virtual const std::string& last_error() const = 0;

    /// Take whatever the kernel has ready. See the split note above: this is
    /// the unlocked half, so it must touch nothing but the socket.
    virtual void poll() = 0;

    /// Hand over the next connection `poll()` accepted, already `Connected`, or
    /// null when there is none. The caller OWNS what it gets back.
    virtual std::unique_ptr<EspTransport> accept() = 0;
};

/// Build the real POSIX/Winsock listener, bound to `bind_address` whenever it
/// is opened.
///
/// THE ADDRESS IS FIXED AT CONSTRUCTION, and that is the security shape rather
/// than an API convenience: it comes from the host's configuration
/// (`--esp-listen-address`, defaulting to loopback), and the guest chooses only
/// the PORT. A guest that could choose the address could widen the exposure the
/// user set — which is the whole of the owner decision in design doc §13.4, so
/// the interface is built so the guest has no way to express it.
///
/// NO ADDRESS POLICY APPLIES, and its absence is deliberate rather than
/// forgotten. `AddressPolicy` governs which addresses the guest may DIAL, to
/// protect the host from the guest; a listening socket inverts the direction
/// and `classify_address` has nothing to say about a peer the guest did not
/// choose. What protects the guest here is the bind address, enforced by the
/// kernel.
std::unique_ptr<EspListener> make_socket_listener(const IpAddress& bind_address);

}  // namespace esp
