#pragma once

#include "esp01/esp_socket.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

/// AT command engine for the emulated ESP-01 (GH #25, branch 3 of 6).
///
/// This is the PASSIVE CORE: an `EspDevice` driving an `EspTransport`, with no
/// thread, no clock and no host types, and nothing constructs it yet. No
/// `Emulator` wiring, no CLI flags, no config, no per-frame `poll()` call site
/// — those are branch 4. It is therefore constructible and fully testable
/// standalone against a fake transport, which is exactly how
/// `src/esp01/test/esp_at_test.cpp` drives it: no sockets, no DNS, no listener.
///
/// DEPENDENCY SURFACE, deliberately tiny: `EspTransport` (the transport seam)
/// and `esp_log.h` (the logging seam). NOTHING from jnext — not `core/log.h`,
/// not `Emulator`, not `NextReg`, not even a jnext include root. That is what
/// makes the module droppable into another project.
///
/// ---------------------------------------------------------------------------
/// THE COMMAND SET IS EVIDENCED, AND DELIBERATELY NARROW
/// ---------------------------------------------------------------------------
/// Every command below appears in software that actually runs on a Next: the
/// NextZXOS ESP driver and dot commands, NXtel (`src/esp.asm`, `src/c31.asm`)
/// and nextsync (`sync/nextsync.c`). Nothing here is speculative, and the
/// omissions are as deliberate as the inclusions:
///
///   * NO server/listen mode. `AT+CIPSERVER` appears exactly once in all the
///     software examined, and only to turn it OFF. Not building it also
///     removes the inbound attack surface the issue was worried about.
///   * NO UDP, NO passthrough (`AT+CIPMODE`). Zero consumers anywhere.
///   * NO multiplexed connections. `AT+CIPMUX=1` is REFUSED with `ERROR`
///     rather than accepted-and-ignored, because nextsync never sends
///     `AT+CIPMUX` at all — it relies on the power-on default being 0 — and
///     its `+IPD` reader silently CORRUPTS the multiplexed
///     `+IPD,<id>,<len>:` form rather than rejecting it (see
///     `queue_ipd_header` for the exact mechanism). Since no command can
///     correct a wrong default at runtime, the default has to be right and the
///     wrong value has to be rejected loudly.
///
/// Widening the surface (full datasheet-level fidelity) is tracked as its own
/// v1.1 issue. Do not grow this file by guessing. What this file DOES do is
/// keep three shapes open so that widening is filling in blanks rather than
/// surgery — see "SHAPED FOR v1.1" at the bottom.
///
/// ---------------------------------------------------------------------------
/// THE RESPONSE BYTES ARE PARSER-CRITICAL
/// ---------------------------------------------------------------------------
/// Three separate guest parsers busy-wait on these strings with NO timeout, so
/// a missing or misframed reply hangs the emulated machine forever rather than
/// failing:
///
///   * `AT+CIPSEND=<n>` must answer `"\r\nOK\r\n> "` — trailing space
///     included. NXtel's `ESPReceiveWaitOK` needs the `OK` CRLF-terminated,
///     its `ESPReceiveWaitPrompt` spins until a bare `>`, and the NextZXOS
///     `.UART` dot command matches the exact sequence `OK`,13,10,`>`.
///   * `.ESPBAUD` compares a reply against `"OK\r\n"` EXACTLY, which is why
///     every terminator here is CRLF and never a bare LF.
///   * After the payload: `"\r\nSEND OK\r\n"`. See the SEND OK note below —
///     this was flagged as an open risk and is now settled from source.
///
/// Equally important is what is NEVER emitted: `busy p...`,
/// `ALREADY CONNECTED`, `SEND FAIL`, `link is not valid`, `no ip`, `ready`.
/// Nothing parses them, and `ESPATreadme.TXT:92` warns that an unexpected
/// `CLOSED` / `WIFI DISCONNECT` leaves the NextZXOS driver in an unknown
/// state. `CLOSED` is emitted only when a connection genuinely closed.
///
/// SEND OK vs NXtel's `'S'` BRANCH — RESOLVED, from `NXtel/src/esp.asm`.
/// The worry was that `ESPReceiveWaitOK` treats a leading `'S'` as the start
/// of `SEND FAIL` and would swallow `SEND OK`. It does enter that branch, and
/// it does not matter, because `MatchSendFail` (esp.asm:418-427) loads
/// `hl, Error` where every sibling loads its own string — so after `'S'` it
/// demands the impossible 16-byte sequence `RROR\r\nEND FAIL\r\n`
/// (`Compare = SendFailEnd`). The very next byte `'E'` mismatches `'R'`, the
/// FSM resets to `FirstChar`, `'N' 'D' ' '` are discarded, and the `'O'` of
/// `OK` then takes the `MatchOK` path and completes on `K CR LF`. So
/// `"\r\nSEND OK\r\n"` DOES satisfy the wait — and it is the ONLY thing that
/// can, because `AT+CIPSEND=1` (esp.asm:70-74) has no other reply coming.
///
/// ---------------------------------------------------------------------------
/// PAYLOAD BYTE ACCOUNTING — ALSO RESOLVED FROM SOURCE
/// ---------------------------------------------------------------------------
/// NXtel's `AT+CIPSEND=3` is followed by FIVE bytes, not three:
/// `db 255, 253, 39, CR, LF` with `IacDoNewEnvironLen equ $-IacDoNewEnviron`
/// (esp.asm:46-48). Its `AT+CIPSEND=10` is followed by TWELVE:
/// `db 255, 253, 142, 6, "LATENT", CR, LF` (c31.asm:368-371). The trailing
/// CR,LF is NOT payload — it is structural: `ESPSendProc` plants the bytes
/// inline in the code stream and resumes with `jp (hl)` at the byte after
/// them (esp.asm:558-575), so every inline block is CRLF-terminated whether
/// it is an AT line or not.
///
/// So after exactly `<n>` payload bytes the engine returns to command mode
/// and the trailing CR,LF forms an EMPTY COMMAND LINE, answered `ERROR` —
/// which is the same behaviour nextsync depends on for its `\r\n` probe, and
/// which NXtel's `ESPReceiveWaitOK` happens to accept as well
/// (`'E'` + `"RROR\r\n"` is its one branch that is not miswired). Consuming
/// the CR,LF as payload instead would send two bytes the guest never meant to
/// send and desynchronise the stream.
///
/// ---------------------------------------------------------------------------
/// TIMING: TWO STAGES, TWO CLOCKS
/// ---------------------------------------------------------------------------
/// `poll()` runs in WALL-CLOCK time (once per frame, from the host loop) and
/// does all socket work, filling an UNBOUNDED host-side buffer. `tick()` runs
/// in EMULATED time (per Z80 instruction, gated) and drains that buffer toward
/// the guest at one byte per `prescaler * frame_bits` — the very clock the TX
/// path already uses. Consequences, all of them required:
///
///   * A burst is never dumped into the 512-byte RX FIFO. At nextsync's
///     1.152 Mbaud a per-frame model would deliver 2304 bytes/frame — 4.5x
///     the FIFO — and truncate silently.
///   * A `+IPD,<len>:` header cannot straddle a delivery gap, because there
///     are no gaps: every byte of the queue leaves at the same cadence.
///   * `.ESPBAUD` and nextsync's `AT+UART_CUR` switch work for free, because
///     the rate is read live from the channel on every tick rather than
///     hardcoded at 115200.
///
/// The emulated ESP is also the ONLY source of backpressure in the system:
/// Issue 2 hardwires CTS asserted (`zxnext_top_issue2.vhd:2387`) and nextsync
/// explicitly disables flow control, so not sending faster than the guest
/// drains is the whole flow-control story.
///
/// ---------------------------------------------------------------------------
/// DELIBERATE MODELLING SIMPLIFICATIONS (say them out loud)
/// ---------------------------------------------------------------------------
///  1. RESPONSES ARE SERIALISED. Real hardware happily interleaves an
///     unsolicited `+IPD` between a command line and its `OK`. This engine
///     never does: a `+IPD` is framed only when the guest-bound queue is
///     empty AND no command is in flight. That makes every guest parser's
///     job strictly easier and no parser's job harder.
///  2. ECHO DEFAULTS OFF, unlike real AT firmware which powers up with it on.
///     Every evidenced client sends `ATE0` before anything that matters and
///     none relies on the echo, while `.ESPBAUD`'s EXACT compare against
///     `"OK\r\n"` is precisely the kind of parser an unexpected echo breaks.
///     `ATE0`/`ATE1` are both implemented and really do toggle it, so the
///     capability is real and the default is one line to flip.
///  3. `AT+CIPSENDEX` IS AN ALIAS for `AT+CIPSEND`. Its distinguishing
///     feature — early-terminate on a `\0` in the payload — is never
///     exercised: nextsync's payloads are 3-7 bytes and contain no NUL.
///  4. `AT+CIPSTART` failure answers `ERROR` only; `FAIL` is never emitted.
///     `ERROR` is the refusal path every client already handles gracefully.
///  5. NO SAVE-STATE. A live TCP connection is host topology, not machine
///     state, and rewinding into a re-established socket is meaningless. The
///     branch that makes the ESP reachable owns the `replay_mode_` gate.
///  6. ONLY `AT+CIPSTART` HAS A TIMEOUT. It needs one because a host that
///     silently black-holes SYN — an ordinary stateful-firewall posture — is
///     otherwise answered only when the OS gives up (~127 s on Linux), and the
///     guest is not merely waiting during that: NXtel's `ESPReceiveWaitOK`
///     after `Connect` (esp.asm:39-40) has no timeout AND runs under `di`, so
///     a black-holed connect FREEZES the guest rather than degrading it. No
///     other command can outlive its own dispatch, so none needs a deadline.
///     ONE RESIDUAL GAP, stated rather than hidden: an ESTABLISHED connection
///     that goes silent is never timed out, because TCP itself does not
///     consider that an error and no evidenced client expects one.
///     A SECOND GAP RECORDED HERE IS NOW CLOSED — the deadline used to bound
///     the TCP handshake only, because it is checked in `poll()` and the
///     transport's `getaddrinfo` was SYNCHRONOUS, so `poll()` did not return
///     until the lookup was over. The socket transport now resolves on its own
///     thread (esp_socket.h, `make_socket_transport`), so `poll()` returns
///     while a lookup is outstanding and the check below actually runs against
///     it. Pinned by `esp_socket_test` ASYNC-09: a 150 ms deadline fires
///     against a resolver that would take 5 s to give up.
///  7. THE `+IPD` CHUNK FLOOR IS A CONSEQUENCE, NOT AN INVARIANT. nextsync
///     budgets 5 chunks per server packet (`timeout=5` in its own source),
///     which the research framed as "emit chunks >= 292 bytes". Only the 2048
///     CEILING is enforced here. The floor emerges from coalescing — a chunk
///     is cut only once the guest-bound queue has drained, so under load
///     chunks are large — but it is probabilistic, not guaranteed: at 1.152M
///     baud a 1460-byte chunk drains in ~12.7 ms (1460 x 10 bits / 1 152 000;
///     the "~10 ms" written here originally was an arithmetic slip, corrected
///     2026-07-28 — same order, same conclusion), and sufficiently jittery TCP
///     fragmentation could in principle cut a sub-292-byte chunk and pressure
///     the budget. Unlikely on localhost/LAN and not deterministically
///     unit-testable, so it is recorded as a known characteristic rather than
///     defended by a floor the code does not implement.
///
/// ---------------------------------------------------------------------------
/// SHAPED FOR v1.1 (issue #154) WITHOUT IMPLEMENTING ANY OF IT
/// ---------------------------------------------------------------------------
/// Three structural choices cost nothing today and are the class of thing a
/// later branch cannot cheaply retrofit:
///
///  A. CONNECTIONS ARE A TABLE, not "the connection". `conn_` is a fixed
///     array of `MAX_CONNECTIONS` slots and every per-connection field —
///     transport, host, port, buffers, close state — lives IN the slot. v1.0
///     only ever touches `SINGLE_CID`, and only that slot is given a
///     transport, but the state machine already speaks in connection ids.
///  B. THE `+IPD` EMITTER TAKES AN ID AND A MULTIPLEXED FLAG. The wire format
///     genuinely differs (`+IPD,<len>:` vs `+IPD,<id>,<len>:`), so the choice
///     is made in ONE function rather than baked into the call site. v1.0
///     passes `SINGLE_CID, false` and produces exactly the unmultiplexed
///     bytes nextsync's FSM requires.
///  C. AT DISPATCH IS A TABLE. `kCommands` maps a command string to a
///     uniform `void(const std::string& args)` member handler; adding the ~40
///     commands v1.1 wants is adding rows, not extending an if/else chain.
namespace esp {

/// WHAT A DRIVER OF THE EMULATED ESP-01 SEES. Bytes in, bytes out, two service
/// points, one gate — and not one type from any host project.
///
/// Two things implement it, which is the whole reason it exists:
///   * `AtEngine`, the PASSIVE core. Drive it inline: no thread, no clock of
///     its own, nothing running behind your back.
///   * `ThreadedEsp` (esp01/esp_threaded.h), the OPTIONAL wrapper that owns a
///     core and a thread and does the socket half off your caller's thread.
/// A consumer picks one and never learns which by accident: the contract is
/// identical, so switching is a constructor change.
///
/// The interface used to be jnext's `UartDevice`, and that was the last piece
/// of host coupling in the module (GH #25 branch 3.5). jnext's `UartDevice`
/// implementation is now a five-method adapter that forwards to this
/// (`src/peripheral/esp_uart_adapter.h`), which is where the coupling belongs.
class EspDevice {
public:
    /// Guest-bound byte sink. A callback rather than a UART reference on
    /// purpose: it keeps the module free of any host header, so a consumer can
    /// point it at a `std::vector` and unit-test the whole engine.
    ///
    /// THREADING CONTRACT, and it is a real one: the sink is invoked ONLY from
    /// `tick()`, never from `receive()` or `poll()`. So under `ThreadedEsp` it
    /// still runs on whichever thread calls `tick()` — the emulation thread,
    /// for jnext — and never on the wrapper's thread. A sink may therefore
    /// touch caller-thread-only state (jnext's RX FIFO does).
    using ByteSink = std::function<void(std::uint8_t byte)>;

    virtual ~EspDevice() = default;

    /// Install (or clear, with `nullptr`) the guest-bound sink. Whatever the
    /// engine wanted to say while no sink was installed is DROPPED at the
    /// pacing stage, not buffered forever.
    virtual void set_output(ByteSink sink) = 0;

    /// One byte has arrived from the guest's transmitter.
    virtual void receive(std::uint8_t byte) = 0;

    /// WALL-CLOCK service: sockets, connect completion, timeouts. Cheap and
    /// idempotent; a host calls it at whatever cadence suits (jnext: once per
    /// frame). Never delivers a byte to the sink.
    virtual void poll() = 0;

    /// EMULATED-TIME service: releases at most one guest-bound byte per
    /// `ticks_per_byte` of `elapsed_ticks`.
    ///
    /// THE UNITS ARE THE CALLER'S. The module owns no clock and never asks
    /// what time it is; it is told how much time passed and how much time a
    /// byte costs, in whatever unit the caller counts in. jnext passes 28 MHz
    /// ticks and `UartChannel::byte_transfer_ticks()`; a consumer with no baud
    /// to model can pass `(1, 1)` and get one byte per call.
    ///
    /// `ticks_per_byte` is passed FRESH on every call rather than configured
    /// once, so a mid-stream baud change (nextsync's `AT+UART_CUR`) takes
    /// effect immediately.
    virtual void tick(std::uint32_t elapsed_ticks, std::uint32_t ticks_per_byte) = 0;

    /// Hot-path gate for `tick()`: false when there is provably nothing to
    /// release and nothing waiting to be framed. A caller that ticks per
    /// emulated instruction tests this first.
    virtual bool wants_tick() const = 0;
};

class AtEngine : public EspDevice {
public:
    /// Connection-slot ceiling. This is ESP-AT's own `AT+CIPMUX=1` limit
    /// (ids 0..4), which is why the table is this size and not some rounder
    /// number. v1.0 uses SLOT 0 ONLY — see shape note (A).
    static constexpr std::size_t MAX_CONNECTIONS = 5;

    /// The single connection id v1.0 ever uses. Every evidenced client either
    /// sends `AT+CIPMUX=0` or (nextsync) relies on it being the power-on
    /// default, so id 0 is the only one that can appear on the wire.
    static constexpr std::size_t SINGLE_CID = 0;

    /// Largest `AT+CIPSEND=<n>` accepted. Matches real ESP-AT firmware.
    static constexpr std::size_t MAX_SEND_LEN = 2048;

    /// Largest single `+IPD` chunk emitted. nextsync budgets 5 chunks per
    /// server packet and its largest packet is 1460 bytes, so a 2048-byte
    /// ceiling means a packet is never split at all in practice.
    static constexpr std::size_t MAX_IPD_CHUNK = 2048;

    /// Longest command line accepted before the line is answered `ERROR`.
    /// Real firmware caps at 256; this is deliberately looser so that a
    /// legitimate `AT+CIPSTART=` with a maximum-length (253-char) hostname
    /// cannot be rejected as overlong.
    static constexpr std::size_t MAX_COMMAND_LEN = 512;

    /// Bytes read from the socket per `recv` attempt, and the cap on attempts
    /// per `poll()` — together they bound one frame's socket work at 64 KB.
    static constexpr std::size_t RECV_CHUNK    = 1024;
    static constexpr int         RECV_MAX_ITER = 64;

    /// Wall-clock deadline for an `AT+CIPSTART`, in milliseconds.
    ///
    /// TEN SECONDS, and the number is bounded from both sides:
    ///   * ABOVE any real handshake. A TCP connect is one round trip; Linux
    ///     retransmits a lost SYN at ~1 s and again at ~3 s, so even two lost
    ///     SYNs complete by ~4 s and a third by ~7 s. 10 s clears that with
    ///     margin, so a slow-but-real WAN host is never refused.
    ///   * BELOW the OS giving up (~127 s on Linux, longer on some systems).
    ///     That is the whole point: WE decide when to answer, not the kernel,
    ///     because the kernel's answer arrives long after the guest has
    ///     wedged.
    /// It is deliberately not shorter. The guest is busy-waiting, not the
    /// emulator — the frame loop keeps running and the UI stays live — so the
    /// cost of a generous deadline is a stalled guest program, while the cost
    /// of a mean one is refusing connections that would have worked.
    static constexpr int DEFAULT_CONNECT_TIMEOUT_MS = 10000;

    /// The advertised network. Owner decision (GH #25, 2026-07-28, as
    /// corrected): a CONSTANT, obviously synthetic name — never the host
    /// machine's real SSIDs — and the same principle for the BSSID, channel,
    /// RSSI and IP addresses reported alongside it. The emulated module is
    /// not a radio and must not leak the user's network environment into the
    /// guest. These values are cosmetic: nothing routes through them.
    static constexpr const char* SSID       = "JNextWifiHost";
    static constexpr const char* AP_BSSID   = "02:00:00:00:00:01";
    static constexpr const char* STA_MAC    = "02:00:00:00:00:02";
    static constexpr const char* STA_IP     = "192.168.1.50";
    static constexpr const char* GATEWAY_IP = "192.168.1.1";
    static constexpr const char* NETMASK    = "255.255.255.0";
    static constexpr const char* DNS1       = "192.168.1.1";
    static constexpr const char* DNS2       = "8.8.8.8";

    /// Non-owning: the transport must outlive the engine. It becomes the
    /// transport of connection slot `SINGLE_CID`; the remaining slots stay
    /// transport-less, which is what makes them inert in v1.0.
    explicit AtEngine(EspTransport& transport);

    // ── EspDevice ─────────────────────────────────────────────

    void set_output(ByteSink sink) override { out_sink_ = std::move(sink); }

    /// One byte out of the guest's transmitter. Command bytes accumulate into
    /// a line; payload bytes are counted against the outstanding `AT+CIPSEND`.
    void receive(std::uint8_t byte) override;

    /// Wall-clock service: exactly `advance_transports()` followed by
    /// `service_transports()`, which is what an inline consumer wants and is
    /// byte-for-byte the behaviour this call has always had.
    void poll() override;

    // ── The wall-clock half, split in two ─────────────────────────
    //
    // WHY THE SPLIT EXISTS. A threaded host serialises engine state under a
    // lock of its own, and holding that lock across a transport call is a
    // trap: the transport is the one part of this system that can sit in a
    // syscall. `ThreadedEsp` runs `advance_transports()` OUTSIDE its lock and
    // `service_transports()` inside it, which is what stops a slow transport
    // from starving the guest-bound pacer of already-queued bytes. Measured on
    // the version that did not split: 11 827 226 `tick()` calls over 400 ms
    // delivered ZERO of the 39 bytes queued before the stall began.
    //
    // A host that does not care simply calls `poll()`.

    /// Calls `EspTransport::poll()` on every live slot and NOTHING ELSE. Reads
    /// no engine state, writes no engine state, touches no queue and no
    /// pacing — the whole function is that one call in a loop. That is what
    /// makes it safe to run unlocked alongside a `tick()` on another thread.
    ///
    /// It is still the slowest thing here, because `EspTransport::poll()` is
    /// where an implementation does its socket work.
    void advance_transports();

    /// Everything else the wall-clock half does: settle a pending connect,
    /// flush queued outbound data, drain sockets into the per-slot buffers,
    /// notice a peer close, frame a `+IPD` and refresh the tick gate. Calls
    /// only the transport operations contracted as NON-BLOCKING
    /// (`send`/`recv`/`close`), and mutates engine state throughout — so a
    /// threaded host must serialise this against `receive()` and `tick()`.
    void service_transports();

    /// Emulated-time service: frame `+IPD` when the wire is quiet and release
    /// guest-bound bytes at one per `ticks_per_byte`.
    ///
    /// WHY THE PACER LIVES IN THE CORE and not in the host's adapter, which
    /// was a real choice with a real alternative. It is here because it is
    /// INSEPARABLE from `+IPD` coalescing: a chunk is cut only when `out_` has
    /// drained (`wire_is_quiet`), so a host that drained `out_` at its own rate
    /// would see the engine cut a chunk on every poll — tiny chunks under load,
    /// which is exactly the pressure on nextsync's 5-chunks-per-packet budget
    /// that simplification 7 describes. Moving the pacer out would have meant
    /// either exporting that invariant to every consumer or losing it.
    ///
    /// It is also NOT a clock (see `EspDevice::tick`): the caller supplies both
    /// the elapsed time and the cost of a byte, in the caller's own units, so
    /// the core still owns no notion of time. And a consumer that wants no
    /// pacing at all passes `(1, 1)`.
    void tick(std::uint32_t elapsed_ticks, std::uint32_t ticks_per_byte) override;

    /// True while there is anything queued for the guest or anything waiting
    /// to be framed into a `+IPD`.
    bool wants_tick() const override { return tick_wanted_; }

    // ── Introspection (tests, and branch 4's status/trace UI) ─
    //
    // The connection-facing accessors report SLOT 0. They keep their v1.0
    // names because that is what a single-connection caller means; the
    // per-slot state they read is already indexed.

    bool        echo_enabled() const { return echo_; }
    bool        connected() const { return conn_[SINGLE_CID].open; }
    bool        awaiting_connect() const { return conn_[SINGLE_CID].connecting; }
    /// Bytes queued toward the guest, not yet released by the pacer.
    std::size_t pending_to_guest() const { return out_.size(); }
    /// Bytes read from the socket, not yet framed into a `+IPD`.
    std::size_t pending_from_peer() const { return conn_[SINGLE_CID].rx.size(); }
    /// Payload bytes still owed after an `AT+CIPSEND=<n>`; 0 in command mode.
    std::size_t payload_outstanding() const;
    /// Last baud requested via `AT+UART_CUR`/`_DEF`; 0 if never set. The
    /// engine does not act on it — pacing follows the channel's live
    /// prescaler — but it is worth tracing.
    std::uint32_t requested_baud() const { return requested_baud_; }

    /// Override the `AT+CIPSTART` deadline. Configuration, not a test hook:
    /// the CLI/config branch is the natural place to expose it, and the unit
    /// suite uses a zero timeout to make the expiry deterministic.
    void set_connect_timeout(std::chrono::milliseconds t) { connect_timeout_ = t; }
    std::chrono::milliseconds connect_timeout() const { return connect_timeout_; }

private:
    enum class Mode : std::uint8_t { Command, Payload };

    /// One connection slot. Everything that belongs to a connection rather
    /// than to the module lives here, so that adding `AT+CIPMUX=1` later is a
    /// loop bound change rather than a rewrite. In v1.0 only slot
    /// `SINGLE_CID` is given a transport; the rest are permanently idle and
    /// every loop skips them on the null check.
    struct Connection {
        EspTransport* transport     = nullptr;
        bool          open          = false;  ///< live, send/recv are valid
        bool          connecting    = false;  ///< begin_connect issued, reply deferred
        bool          close_pending = false;  ///< closed; CLOSED owed once buffers drain
        std::string   host;
        std::uint16_t port = 0;
        /// When `connecting`, the wall-clock instant after which the connect
        /// is abandoned with `ERROR`. Meaningless otherwise.
        std::chrono::steady_clock::time_point connect_deadline{};
        /// Peer -> guest, unbounded, pre-framing (pacing stage 1).
        std::deque<std::uint8_t> rx;
        /// Guest -> peer, whatever the kernel would not take yet.
        std::deque<std::uint8_t> tx;
    };

    /// Uniform AT handler. Exact-match commands get an empty `args`.
    using Handler = void (AtEngine::*)(const std::string& args);

    struct CommandEntry {
        const char* name;    ///< matched case-insensitively
        bool        prefix;  ///< true: `name` is a prefix and `args` follows
        Handler     handler;
    };
    static const CommandEntry kCommands[];
    static const std::size_t  kCommandCount;

    // Guest TX -> engine.
    void feed(std::uint8_t byte);
    void dispatch_line();
    void finish_payload();

    // Command handlers — all uniform, all table-reachable.
    void cmd_at(const std::string& args);
    void cmd_echo_off(const std::string& args);
    void cmd_echo_on(const std::string& args);
    void cmd_reset(const std::string& args);
    void cmd_cipstart(const std::string& args);
    void cmd_cipsend(const std::string& args);
    void cmd_cipsendex(const std::string& args);
    void cmd_cipclose(const std::string& args);
    void cmd_cipmux(const std::string& args);
    void cmd_uart(const std::string& args);
    void cmd_gmr(const std::string& args);
    void cmd_cwjap(const std::string& args);
    void cmd_cipsta(const std::string& args);
    void cmd_cifsr(const std::string& args);
    void cmd_cipdns(const std::string& args);

    /// Shared by `AT+CIPSEND` and `AT+CIPSENDEX` — see simplification (3).
    void begin_send(const std::string& args, const char* name);

    // Engine -> guest.
    void queue(const char* text);
    void queue(const std::string& text);
    void queue_raw(const std::uint8_t* data, std::size_t len);
    void queue_ok()    { queue("\r\nOK\r\n"); }
    void queue_error() { queue("\r\nERROR\r\n"); }

    /// Queue a `+IPD` header. The ONE place the wire format is decided:
    /// unmultiplexed `+IPD,<len>:` when `multiplexed` is false (always, in
    /// v1.0), `+IPD,<id>,<len>:` when it is true. Emitting the multiplexed
    /// form today would break nextsync outright, and WORSE THAN CLEANLY: its
    /// reader accumulates before it validates —
    ///     `datalen += r - '0';`  then  `if (r != ':' && (r < '0' || r > '9')) return 0;`
    /// (`sync/nextsync.c`) — so the separating `,` is folded into the length as
    /// a bogus digit worth `',' - '0'` = -4, and the parse CONTINUES with a
    /// corrupted `<len>`, desynchronising the stream rather than failing. A
    /// clean bail would at least be diagnosable; this is silent corruption,
    /// which is exactly why the decision is centralised in one function rather
    /// than left to whoever adds CIPMUX support.
    void queue_ipd_header(std::size_t cid, bool multiplexed, std::size_t len);

    // Socket-side helpers, all per-slot.
    void resolve_connect(std::size_t cid);
    void flush_outbound(std::size_t cid);
    void drain_socket(std::size_t cid);
    void note_peer_close(std::size_t cid);
    void frame_ipd();

    /// True while a `+IPD` may be framed: nothing already queued for the
    /// guest, no partially received command line, not mid-payload and no
    /// connect in flight. This is what makes simplification (1) hold.
    bool wire_is_quiet() const;

    void refresh_tick_gate();

    /// Push one byte toward the guest. A no-op with no sink installed, which
    /// is the correct behaviour for a detached module: the byte is discarded
    /// at the wire, exactly as it would be with nothing plugged into the UART.
    void send_to_guest(std::uint8_t byte) const {
        if (out_sink_) out_sink_(byte);
    }

    ByteSink out_sink_;
    /// Mirrors `wants_tick()`. A plain bool, not a virtual computation: the
    /// caller may test it once per emulated instruction, so it must be a load.
    bool     tick_wanted_ = false;

    std::array<Connection, MAX_CONNECTIONS> conn_{};

    Mode        mode_ = Mode::Command;
    std::string line_;
    bool        line_overflow_ = false;
    bool        echo_          = false;
    /// A CR has just been consumed, so an immediately following LF is the
    /// other half of the terminator and belongs to no command and no payload.
    /// Mode-independent on purpose — see the comment in `feed`.
    bool        expect_lf_     = false;

    std::size_t               payload_len_ = 0;
    std::size_t               payload_cid_ = SINGLE_CID;
    std::vector<std::uint8_t> payload_;

    std::uint32_t             requested_baud_  = 0;
    std::chrono::milliseconds connect_timeout_{DEFAULT_CONNECT_TIMEOUT_MS};

    /// Guest TX arriving while a connect is in flight. Every evidenced client
    /// busy-waits for the reply so this stays empty in practice, but a guest
    /// that types ahead must not lose bytes.
    std::deque<std::uint8_t> deferred_;
    /// Framed and ready for the guest, released one byte per `ticks_per_byte`.
    /// Module-wide, not per-connection: one UART, one wire.
    std::deque<std::uint8_t> out_;

    /// Caller-unit ticks banked toward the next guest-bound byte. Reset to 0
    /// whenever `out_` empties so an idle period cannot bank credit and then
    /// dump a burst at full speed the moment data appears.
    std::uint32_t pace_accum_ = 0;
};

}  // namespace esp
