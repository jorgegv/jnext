#pragma once

#include "esp01/esp_socket.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// jnext's SECURITY AND VISIBILITY layer for the emulated ESP-01
/// (GH #25, branch 4 of 6 — see doc/design/ESP01-EMULATOR-DESIGN.md §8).
///
/// WHY IT IS HERE AND NOT IN `src/esp01`. The module already owns the part of
/// the posture that is about ADDRESSES — `esp::AddressPolicy` denies loopback,
/// link-local, cloud metadata, unspecified and multicast, and deliberately
/// ALLOWS RFC1918 so the emulated ESP can reach the user's own LAN. What lives
/// here is the part that is about *this program's* relationship with *its*
/// user: which NAMES the guest may ask for, and how the user finds out that a
/// connection was made or refused. A reusable module cannot own either — the
/// first is a host policy fed from a host CLI, and the second needs a host UI.
///
/// THE THREE PIECES, and the one property each must have:
///
///   `EspHostPolicy`      pure, so the matching rule is exhaustively testable
///                        offline with no socket and no engine.
///   `EspConnectionLog`   thread-safe, because the events are produced on the
///                        `esp::ThreadedEsp` worker thread and consumed on the
///                        GUI thread. Bounded, because nothing guarantees a
///                        consumer exists at all (headless never reads it).
///   `EspGatedTransport`  a DECORATOR over `esp::EspTransport`, because the AT
///                        engine talks to the transport directly and must not
///                        be modified: `src/esp01` is a reusable component with
///                        a proven standalone build, and a jnext allowlist has
///                        no business inside it.
///
/// The decorator is also what makes the "never silent" requirement satisfiable
/// without touching the module: every state change the engine reacts to passes
/// through this object first, so open / failed / closed can be observed from
/// the outside, exactly as the refusal can.

// ---------------------------------------------------------------------------
// Hostname allowlist
// ---------------------------------------------------------------------------

/// ASCII-lowercase and trim a host string, so `" NX.NxTel.ORG "` from a config
/// file and `nx.nxtel.org` from an `AT+CIPSTART` are the same entry. Hostnames
/// are case-insensitive (RFC 4343); the guest's spelling is never normalised
/// for any purpose other than this comparison, so a log line still shows what
/// the guest actually asked for.
std::string esp_normalise_host(const std::string& host);

/// Which hosts the guest may connect to, on top of `esp::AddressPolicy`.
///
/// AN EMPTY LIST MEANS "NO NAME RESTRICTION", not "deny everything". The
/// reachability gate is the enable flag, which is off by default and has to be
/// given explicitly (`--esp`); a user who has turned the ESP on has already
/// decided the guest may talk to the network, and `--esp-allow` NARROWS that
/// decision. Reading the empty list as deny-all would make `--esp` on its own
/// do nothing at all, which is a worse failure: the ESP would answer `ERROR`
/// to everything and look broken rather than restricted.
///
/// MATCHING IS EXACT (case-insensitive), with no wildcards. Both v1.0
/// acceptance targets need exactly that — NXtel connects to the handful of
/// literal hostnames in its own `nxtel.cfg`, and nextsync is normally
/// configured with a literal IP — so a pattern language would be unevidenced
/// surface on a security control, which is the worst place to put any. An IP
/// literal is matched as the string it is, because that is what the guest
/// names in `AT+CIPSTART`; allowing `192.168.1.10` means listing it.
struct EspHostPolicy {
    /// Stored as the user wrote them, so a refusal can be explained in the
    /// user's own spelling. Comparison normalises both sides.
    std::vector<std::string> allowed_hosts;

    bool unrestricted() const { return allowed_hosts.empty(); }
    bool allows(const std::string& host) const;

    /// Add one entry, ignoring blanks and duplicates. Returns false when the
    /// entry was blank (the caller reports that as a usage error) — a silently
    /// dropped allowlist entry is a security bug, not a tidy-up.
    bool add(const std::string& host);
};

// ---------------------------------------------------------------------------
// Connection visibility
// ---------------------------------------------------------------------------

/// One user-visible ESP connection event.
///
/// These exist because the log lines the engine already emits go to STDERR,
/// which a GUI user never sees. §8.2 of the design doc records that as an
/// unmet obligation of this branch: a security-relevant event that is
/// invisible to the user who most needs it is not a satisfied requirement.
struct EspEvent {
    enum class Kind : std::uint8_t {
        /// A SECURITY CONTROL said no. Either half of it: the `--esp-allow`
        /// hostname list rejected the name before anything was opened, or
        /// `esp::AddressPolicy` rejected the address it resolved to. Both are
        /// deliberate blocks by jnext, so both must look deliberate — a
        /// loopback or cloud-metadata target used to arrive as a plain
        /// `Failed`, indistinguishable from a DNS miss (GH #161).
        Refused,
        Opened,          ///< TCP connection established
        Failed,          ///< the attempt failed for a NON-policy reason (DNS, refused, timeout)
        Closed,          ///< connection closed (by either end)
        TransportFault,  ///< the transport threw; the ESP has stopped making progress
    };

    Kind          kind = Kind::Refused;
    std::string   host;
    std::uint16_t port = 0;
    std::string   detail;  ///< reason text; empty when there is nothing to add
};

const char* esp_event_text(EspEvent::Kind kind);

/// Bounded, thread-safe record of the most recent events.
///
/// NON-DESTRUCTIVE reads (`snapshot()` + `sequence()`) rather than a drain:
/// there can be more than one observer (the status cell shows the latest, the
/// tooltip shows the history), and a drain would let whichever ran first hide
/// the event from the other. `sequence()` counts everything ever pushed, so a
/// consumer can tell "nothing new" from "nothing at all" with one integer
/// compare and no allocation on the common path.
class EspConnectionLog {
public:
    /// Deliberately small. This is a status-bar history, not an audit trail —
    /// the audit trail is the log, which is on by default for exactly these
    /// events. An unbounded queue in a program that may never read it is a
    /// leak with extra steps.
    static constexpr std::size_t CAPACITY = 32;

    /// Callable from any thread. Called from the ESP worker thread.
    void push(EspEvent event);

    /// Oldest first. Callable from any thread.
    std::vector<EspEvent> snapshot() const;

    /// Total events ever pushed, including those already aged out.
    std::uint64_t sequence() const;

    /// Which log this is. Process-unique, monotonic, and never 0.
    ///
    /// A CONSUMER CANNOT USE THE ADDRESS FOR THIS. A cold boot destroys the
    /// `Emulator` and placement-news a new one, and the replacement log
    /// routinely lands on the freed block — so `&log` is stable across a
    /// rebuild that reset `sequence()` to 0. An observer caching only the
    /// sequence then treats "3 events into the new machine" as "the 3 events I
    /// already rendered" and keeps showing pre-reboot text. Pairing the id with
    /// the sequence makes the pair strictly increasing across a rebuild, so
    /// there is no coincidence to hit.
    std::uint64_t instance_id() const { return instance_id_; }

private:
    static std::uint64_t next_instance_id();

    mutable std::mutex   mutex_;
    std::deque<EspEvent> events_;
    std::uint64_t        sequence_ = 0;
    const std::uint64_t  instance_id_ = next_instance_id();
};

// ---------------------------------------------------------------------------
// The gated transport
// ---------------------------------------------------------------------------

/// Wraps a real `esp::EspTransport`, enforcing the hostname allowlist and
/// reporting every connection outcome.
///
/// REFUSAL USES THE INTERFACE'S OWN VOCABULARY. `begin_connect` returning
/// false is documented as "request rejected, state untouched"
/// (esp01/esp_socket.h), and `AtEngine::cmd_cipstart` turns exactly that into
/// the `ERROR` reply every evidenced client already handles gracefully. So a
/// refused host degrades a guest program instead of hanging it, and no module
/// change is needed to make refusal work.
///
/// THE NON-BLOCKING CONTRACT IS INHERITED, NOT WEAKENED: every method here is
/// a forward plus an enum compare, and `poll()` adds nothing but that. The
/// wrapped transport's own contract (esp01/esp_socket.h) is what bounds
/// `esp::ThreadedEsp`'s shutdown, and this decorator must not be the thing
/// that breaks it.
///
/// THREADING: every method is called on the `ThreadedEsp` worker thread, and
/// the only shared state it touches is the `EspConnectionLog`, which is
/// mutex-guarded. The log must therefore OUTLIVE this object — in `Emulator`
/// it is declared before it, so it is destroyed after.
class EspGatedTransport final : public esp::EspTransport {
public:
    EspGatedTransport(std::unique_ptr<esp::EspTransport> inner, EspHostPolicy policy,
                      EspConnectionLog& log);

    /// Declares NO default arguments — the interface owns them (esp_socket.h).
    /// So a call on a concrete `EspGatedTransport` must name the protocol,
    /// which is the right way round for the object whose job is to gate it.
    bool                  begin_connect(const std::string& host, std::uint16_t port,
                                        esp::Protocol protocol,
                                        std::uint16_t local_port) override;
    void                  poll() override;
    esp::TransportState   state() const override;
    const std::string&    last_error() const override;
    esp::DenyReason       denial_reason() const override;
    const esp::IpAddress& peer_address() const override;
    std::size_t           send(const std::uint8_t* data, std::size_t len) override;
    std::size_t           recv(std::uint8_t* buf, std::size_t cap) override;
    void                  close() override;

    /// How many connection attempts the allowlist refused. Exposed so a test
    /// can ASSERT the refusal happened rather than grep a log for it.
    std::uint64_t refusals() const { return refusals_; }

private:
    /// Compare the wrapped transport's state against the last one seen and
    /// emit an event on a transition. Called after every forward that can
    /// change it, so an event is never more than one call late.
    void note_state();

    std::unique_ptr<esp::EspTransport> inner_;
    EspHostPolicy                      policy_;
    EspConnectionLog&                  log_;

    std::string         host_;
    std::uint16_t       port_       = 0;
    esp::TransportState last_state_ = esp::TransportState::Idle;
    std::uint64_t       refusals_   = 0;
};

// ---------------------------------------------------------------------------
// Transport fault reporting
// ---------------------------------------------------------------------------

/// Report an `esp::ThreadedEsp::pass_exceptions()` count, ONCE.
///
/// THE INHERITED QUESTION, AND THE ANSWER THIS BRANCH GIVES. A transport that
/// throws on every pass leaves the worker spinning at its poll interval making
/// no progress, and `pass_exceptions()` exists precisely so branch 4 can
/// surface that. The choice made here is to REPORT IT LOUDLY AND KEEP RUNNING,
/// not to disable the ESP after N failures:
///
///   * jnext constructs the only transport the ESP ever sees, and
///     `esp::EspTransport`'s methods are contractually non-throwing. A
///     non-zero count is therefore a JNEXT BUG, and the useful response to a
///     bug is a loud, attributable message — not a silent capability
///     downgrade that makes the bug look like "the ESP does nothing".
///   * the count is of LOST PASSES, not of permanent failure. One
///     `std::bad_alloc` under memory pressure should not kill a feature for
///     the rest of the session.
///   * a disable-after-N policy needs a threshold, and there is no evidence
///     for any value of N. The design doc kept that policy out of v1.0 on
///     exactly those grounds; inventing one now would be guessing.
///
/// The residual cost of not disabling is an idle worker waking at its poll
/// interval — measurable, harmless, and named in the message so a bug report
/// can say so.
///
/// @param pass_exceptions  the counter's current value
/// @param already_reported in/out latch, so a per-frame caller reports once
/// @return true exactly once, on the first non-zero observation
bool esp_note_transport_fault(std::uint64_t pass_exceptions, bool& already_reported);
