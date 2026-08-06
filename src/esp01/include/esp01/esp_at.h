#pragma once

#include "esp01/esp_socket.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
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
///   * SERVER MODE IS IMPLEMENTED (GH #210). It was omitted from v1.0 for want
///     of a consumer — `AT+CIPSERVER` appeared exactly once in all the software
///     examined, and only to turn it OFF — and `dezogif_ng` is one: a Z80 debug
///     stub that must LISTEN, because DeZog always dials out and never listens.
///     The inbound attack surface v1.0 avoided by not building it is answered
///     by design doc §13.4 rather than by the absence: the listener binds
///     LOOPBACK unless the user widens it, and nothing listens until the guest
///     sends `AT+CIPSERVER` itself.
///   * NO passthrough (`AT+CIPMODE`). Zero consumers anywhere, and server mode
///     forbids it in any case (`CIPSERVER` needs `CIPMUX=1`, which needs
///     `CIPMODE=0`), so the chosen design could not use it if it existed.
///   * UDP IS IMPLEMENTED (GH #198). It was omitted from v1.0 for want of a
///     consumer; `newt` (github.com/chris-y/newt, GPLv3) is one — its `sntp`
///     command is `AT+CIPSTART="UDP","<server>",123` followed by a 48-byte NTP
///     request and a `+IPD` reply. SSL is still out: still no consumer.
///   * MULTIPLEXED CONNECTIONS ARE IMPLEMENTED (GH #210), and the reasoning
///     that made v1.0 REFUSE `AT+CIPMUX=1` survives intact rather than being
///     overturned. nextsync never sends `AT+CIPMUX` at all — it relies on the
///     power-on default being 0 — and its `+IPD` reader silently CORRUPTS the
///     multiplexed `+IPD,<id>,<len>:` form rather than rejecting it (see
///     `queue_ipd_header` for the exact mechanism). Since no command can
///     correct a wrong default at runtime, THE DEFAULT STAYS 0: accepting the
///     command is not the same as changing the default, and the default is the
///     part nextsync depends on. The multiplexed form reaches only a connection
///     that was opened by a session which asked for it, and `Connection::
///     multiplexed` is where that is recorded.
///
///     THE GUARANTEE HAS A PRECONDITION, AND IT IS THIS: "nextsync keeps
///     working" holds from a FRESH POWER-ON, or after an `AT+RST`. It is not a
///     property of every moment. `cipmux_` is module state, not per-program
///     state, and nothing resets it when the guest loads a different program —
///     a SOFT reset deliberately preserves the whole ESP, connection and all
///     (the module is on the far end of a cable and does not see the Next's
///     reset line; design doc §4.3). So a program that sets `AT+CIPMUX=1` and
///     exits leaves it set, and a nextsync started in the SAME power cycle with
///     no intervening `AT+RST` has its own outbound connection captured
///     `multiplexed = true` and receives the framing its reader silently
///     corrupts.
///     THIS IS FIRMWARE-FAITHFUL — a real ESP-01 is sticky in exactly the same
///     way, for exactly the same reason — so it is DISCLOSED rather than fixed:
///     "reset the module between programs" is what the hardware demands too,
///     and a jnext-only auto-reset would be a divergence sold as a safety net.
///     A hard reset (jnext power-cycles the emulated ESP, `emulator.h`) and
///     `AT+RST` both clear it. NR 0x02 bit 7 — the hardware reset line, and
///     what nextsync's own recovery path drives — does NOT, because jnext
///     only latches that bit for readback and nothing consumes it yet (design
///     doc SS4.2). Do not cite it here as a protection until it is wired.
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
///
///     WHAT "IN FLIGHT" MEANS WAS TOO BROAD, AND GH #198 NARROWED IT. It used
///     to be "`line_` is non-empty", and that reading really did make one
///     parser's job harder — impossible, in fact. newt's `uart_tx_bin`
///     (github.com/chris-y/newt, `uart.c`) is `do {…} while (size--)`, which
///     sends `size + 1` bytes: after `AT+CIPSEND=48` it puts FORTY-NINE bytes
///     on the wire. The 49th is one byte past its own `calloc(48)` — arbitrary
///     heap content — and it lands in `line_`, where it stays until the next
///     CR. Under the old rule that byte held off every `+IPD` FOREVER, so the
///     reply newt was waiting for was never framed and it died on its own 5 s
///     timeout. Real firmware frames the URC regardless and the tool works.
///     So the guard now asks whether the partial line CAN STILL BECOME AN AT
///     COMMAND (`command_line_in_flight`): every command begins `AT`, so debris
///     that does not is not a transaction and does not hold off a URC. This is
///     strictly closer to hardware, and it keeps the property the serialisation
///     exists for — a genuinely half-typed `AT+CIPM` still holds the `+IPD`
///     back, which is what stops a `+IPD` payload byte from being mistaken for
///     the `> ` prompt NXtel busy-waits on with no timeout.
///     RESIDUAL, stated rather than hidden: if that stray byte happens to BE
///     `A`, it is indistinguishable from a guest starting to type, and the
///     stall returns. One value in 256, and the honest fix for it is the guest's
///     own off-by-one, not a rule this engine can write.
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
///  4b. `CONNECT` PRECEDES `OK` FOR UDP AND NOT FOR TCP, and the asymmetry is
///     deliberate rather than an oversight. Real firmware prints
///     `CONNECT\r\n\r\nOK\r\n` for BOTH — note the missing leading CRLF, which
///     is the firmware's own framing and not a slip: `CONNECT` is a STATUS
///     line rather than a result code, so the blank line a transcript shows
///     before `OK` belongs to the `\r\nOK\r\n` after it. newt's
///     `net_connect_udp` reads exactly one line and demands it start with
///     `CONNECT` — so UDP has to emit it, unprefixed, or the tool cannot
///     connect at all. TCP's bare `\r\nOK\r\n` is
///     PRE-EXISTING v1.0 behaviour derived from the evidenced clients, it is
///     pinned by `esp-loopback-func`'s exact byte stream, and the two clients
///     whose parsers would have to survive the change (NXtel, nextsync) have no
///     source on this machine to check it against — the design doc says so
///     itself (§12, provenance note). Making TCP firmware-exact is a real
///     improvement and a SEPARATE change with its own evidence to gather; #198
///     is not it, and quietly bundling it would put the one live-traffic-proven
///     path at risk for a client that does not use TCP.
///  4c. `AT+CIPSTART="UDP"` ACCEPTS `<local port>` AND REFUSES `<mode>` 1 or 2.
///     Mode 0 — the default, and the only one any client sends — fixes the
///     peer for the life of the connection, which is exactly a `connect()`ed
///     datagram socket. Modes 1 and 2 re-point the peer at whoever last sent to
///     us, which needs an UNconnected socket, `recvfrom` bookkeeping and a
///     second security decision about who may become the peer. Refused rather
///     than accepted-and-ignored, for the `AT+CIPMUX=1` reason: silently
///     accepting promises a behaviour the guest cannot ask back.
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
///  8. SERVER MODE DEVIATES FROM THE FIRMWARE IN FOUR PLACES (GH #210), and
///     each one is a decision rather than an omission. Design doc §13.7 is the
///     long form.
///     (a) AN INBOUND CONNECTION NEVER TAKES SLOT 0. Real firmware would give
///         the first client id 0; here ids 1..4 are the inbound ones. Slot 0's
///         transport is the one handed in at construction and it is the ONLY
///         object that can serve an `AT+CIPSTART`, so letting a peer take that
///         slot would cost the guest its outbound capability for the rest of
///         the session. The first `<id>,CONNECT` is therefore `1,CONNECT`.
///     (b) `AT+CIPSERVER=0` WITH NO SERVER RUNNING ANSWERS `ERROR`. Real
///         firmware answers `OK`. The one appearance of `AT+CIPSERVER` in all
///         the software surveyed is a client turning it OFF at init — which
///         v1.0 answered `ERROR` (an unknown command) and which that client
///         evidently survives. Answering `ERROR` keeps that path
///         byte-identical; answering `OK` would change an evidenced client's
///         input on no evidence at all. It also matches `AT+CIPCLOSE` with
///         nothing open, where the `ERROR` is load-bearing.
///     (c) `AT+CIPMUX=<n>` REFUSES A CHANGE, NOT THE COMMAND. Real firmware
///         refuses `AT+CIPMUX` outright while a connection is open; here a
///         request for the mode that is ALREADY in force still answers `OK`.
///         That is what keeps `AT+CIPMUX=0` — which NXtel sends at init and
///         which v1.0 always answered `OK` — behaving exactly as it did. A
///         genuine change is refused, which is the part that matters: switching
///         framing under a live peer hands it a wire format it never
///         negotiated.
///     (d) `AT+CIPCLOSE=<id>` EXISTS AS A SECOND SPELLING, NOT AS A SECOND
///         MEANING FOR THE BARE ONE (GH #211; design doc §14). GH #210 shipped
///         without it — DeZog closes from its end, which arrives as
///         `<id>,CLOSED` and frees the slot — and a peer that WEDGES rather
///         than closing is what reopened it: four wedged peers exhaust the four
///         inbound slots and every later client is refused, with nothing the
///         guest can say about it. So the argument form closes the connection
///         it names and frees its slot, and the bare `AT+CIPCLOSE` keeps its
///         v1.0 meaning — close the OUTBOUND connection — in every mode,
///         because nextsync loops that exact spelling and its behaviour must
///         not depend on a mode nextsync never sets.
///         THREE REFUSALS COME WITH IT, all `ERROR`, all deliberate: the
///         argument form under `CIPMUX=0` (the argument list is read from the
///         MODE, exactly as `AT+CIPSEND`'s is); an id with no live connection
///         (which is what the bare form already answers, and what nextsync
///         depends on); and `AT+CIPCLOSE=5`, real firmware's "close every
///         connection", which is refused for the reason `AT+CIPSERVER=0`'s
///         `<close_all>` is — a bulk close promises a choice about connections
///         the guest did not name, and no consumer asks for one.
///
/// ---------------------------------------------------------------------------
/// SHAPED FOR v1.1 (issue #154), AND TWO OF THE THREE HAVE NOW PAID
/// ---------------------------------------------------------------------------
/// Three structural choices cost nothing when they were made and are the class
/// of thing a later branch cannot cheaply retrofit. GH #210 is the branch that
/// tested the claim, and it held: (A) and (B) really were filling in blanks.
///
///  A. CONNECTIONS ARE A TABLE, not "the connection". `conn_` is a fixed
///     array of `MAX_CONNECTIONS` slots and every per-connection field —
///     transport, host, port, buffers, close state — lives IN the slot. v1.0
///     only ever touched `SINGLE_CID`; server mode fills slots 1..4 from the
///     listener and changed no loop bound, because the state machine already
///     spoke in connection ids.
///     WHAT IT DID NOT COVER, since the prediction is only worth as much as its
///     exceptions: the table said nothing about who OWNS a slot's transport.
///     Slot 0's is borrowed and must not be freed, an accepted one is owned and
///     must be — so `Connection::transport` became a `unique_ptr` with a
///     stateful deleter (`TransportOwnership`) rather than a raw pointer plus a
///     comment.
///  B. THE `+IPD` EMITTER TAKES AN ID AND A MULTIPLEXED FLAG. The wire format
///     genuinely differs (`+IPD,<len>:` vs `+IPD,<id>,<len>:`), so the choice
///     is made in ONE function rather than baked into the call site. v1.0
///     passed `SINGLE_CID, false` everywhere; GH #210 passes the connection's
///     own id and its own `multiplexed` flag, and touched nothing else.
///  C. AT DISPATCH IS A TABLE. `kCommands` maps a command string to a
///     uniform `void(const std::string& args)` member handler; adding the ~40
///     commands v1.1 wants is adding rows, not extending an if/else chain.
///     `AT+CIPSERVER=` is one row.
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

/// Frees an OWNED connection transport and leaves a BORROWED one alone.
///
/// THE OWNERSHIP IS IN THE TYPE, not in a comment beside a raw pointer, and
/// that is worth a stateful deleter: the two kinds of connection slot really do
/// have different lifetimes — slot 0's transport is handed in by reference at
/// construction and outlives the engine, an accepted one is manufactured by the
/// listener and dies with its connection — and a `delete` on the wrong one is a
/// double free of an object the HOST still holds. Written as `EspTransport*`
/// plus a `bool owned`, the invariant would have to be restated in every
/// function that clears a slot; written this way, clearing a slot is
/// `transport.reset()` and it is right by construction.
///
/// IT SITS AT NAMESPACE SCOPE ONLY BECAUSE IT CANNOT SIT INSIDE `AtEngine`,
/// which is where it belongs: a deleter nested in a class that is still being
/// defined, and not trivially default-constructible, is rejected by GCC 16
/// ("could not convert '<brace-enclosed initializer list>()'") the moment the
/// class default-initialises a `unique_ptr` using it. Moving it out is the
/// smaller price than dropping the member initialiser and relying on
/// value-initialisation to zero it.
struct TransportOwnership {
    bool owned = false;
    void operator()(EspTransport* t) const {
        if (owned) delete t;
    }
};

class AtEngine : public EspDevice {
public:
    /// Connection-slot ceiling. This is ESP-AT's own `AT+CIPMUX=1` limit
    /// (ids 0..4), which is why the table is this size and not some rounder
    /// number. v1.0 uses SLOT 0 ONLY — see shape note (A).
    static constexpr std::size_t MAX_CONNECTIONS = 5;

    /// The connection id an OUTBOUND `AT+CIPSTART` always uses, and the only
    /// one a `CIPMUX=0` session can ever see. Every evidenced v1.0 client
    /// either sends `AT+CIPMUX=0` or (nextsync) relies on it being the power-on
    /// default, so id 0 is the only one that can appear on their wire.
    ///
    /// It is also the only slot whose transport is BORROWED — handed in at
    /// construction — which is why inbound connections start at
    /// `FIRST_INBOUND_CID` instead of taking it (simplification 8a).
    static constexpr std::size_t SINGLE_CID = 0;

    /// Lowest connection id an accepted inbound connection may occupy
    /// (GH #210). Everything from here to `MAX_CONNECTIONS - 1` is inbound
    /// territory, so a server can hold four clients at once.
    static constexpr std::size_t FIRST_INBOUND_CID = SINGLE_CID + 1;

    /// Largest `AT+CIPSEND=<n>` accepted. Matches real ESP-AT firmware.
    static constexpr std::size_t MAX_SEND_LEN = 2048;

    /// Largest single `+IPD` chunk emitted. nextsync budgets 5 chunks per
    /// server packet and its largest packet is 1460 bytes, so a 2048-byte
    /// ceiling means a packet is never split at all in practice.
    static constexpr std::size_t MAX_IPD_CHUNK = 2048;

    /// Buffer one UDP datagram is read into, and therefore the largest datagram
    /// that survives intact: the kernel DISCARDS whatever does not fit in the
    /// buffer handed to `recv` (`EspTransport::recv`), so this is a real
    /// truncation ceiling and not a chunking one. It is `MAX_IPD_CHUNK` rather
    /// than `RECV_CHUNK` for exactly that reason — reading a datagram in
    /// 1024-byte bites is not possible, so the read buffer has to be as large
    /// as the biggest datagram that may be framed whole. Above it a datagram
    /// arrives truncated, which is also what a real ESP-01 does with an
    /// oversized packet; nothing on the evidenced path comes close (an SNTP
    /// reply is 48 bytes, and a UDP datagram over Ethernet is 1472 before
    /// fragmentation).
    static constexpr std::size_t MAX_DATAGRAM = MAX_IPD_CHUNK;

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

    /// Non-owning, both of them: the transport and the listener must outlive
    /// the engine. The transport becomes the BORROWED transport of connection
    /// slot `SINGLE_CID`; slots `FIRST_INBOUND_CID`..`MAX_CONNECTIONS - 1`
    /// start transport-less and are filled, with OWNED transports, by whatever
    /// the listener accepts.
    ///
    /// A NULL LISTENER IS THE NORMAL CASE FOR A CONSUMER THAT WANTS NO SERVER,
    /// and it is not a degraded mode: `AT+CIPSERVER=1,<port>` then answers
    /// `ERROR`, which is the same refusal a failed bind gives and which every
    /// client already handles. So "this build cannot listen" and "this port
    /// could not be bound" look identical to the guest, deliberately — neither
    /// tells it anything it can act on differently.
    explicit AtEngine(EspTransport& transport, EspListener* listener = nullptr);

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
    /// True after an accepted `AT+CIPMUX=1`. Power-on default FALSE, and that
    /// is the property nextsync depends on — see the header note.
    bool        cipmux() const { return cipmux_; }
    /// True while `AT+CIPSERVER=1,<port>` has a port bound.
    bool        server_listening() const;
    /// The port the server is bound to; 0 when it is not listening.
    std::uint16_t server_port() const;
    /// How many connection slots hold a live inbound connection.
    std::size_t inbound_connections() const;
    bool        connected() const { return conn_[SINGLE_CID].open; }
    /// What the live (or last requested) connection speaks.
    Protocol    protocol() const { return conn_[SINGLE_CID].protocol; }
    bool        awaiting_connect() const { return conn_[SINGLE_CID].connecting; }
    /// Bytes queued toward the guest, not yet released by the pacer.
    std::size_t pending_to_guest() const { return out_.size(); }
    /// Bytes read from the socket, not yet framed into a `+IPD`. On a UDP
    /// connection this is the total across the queued datagrams — the number a
    /// caller wants either way is "how much is waiting", not how it is boxed.
    std::size_t pending_from_peer() const;
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

    using TransportPtr = std::unique_ptr<EspTransport, TransportOwnership>;

    /// One connection slot. Everything that belongs to a connection rather
    /// than to the module lives here, so that adding `AT+CIPMUX=1` was a
    /// question of filling slots rather than a rewrite. Slot `SINGLE_CID`
    /// holds the borrowed outbound transport; slots `FIRST_INBOUND_CID`
    /// upwards hold owned, accepted ones and are transport-less — and
    /// therefore skipped by every per-slot loop's null check — until a peer
    /// connects.
    struct Connection {
        TransportPtr  transport;
        bool          open          = false;  ///< live, send/recv are valid
        bool          connecting    = false;  ///< begin_connect issued, reply deferred
        bool          close_pending = false;  ///< closed; CLOSED owed once buffers drain
        /// Whether THIS connection's URCs take the multiplexed wire form
        /// (`+IPD,<id>,<len>:` and `<id>,CLOSED`). Captured from `cipmux_` when
        /// the connection opens rather than read live, because it describes
        /// what the peer at the other end was promised — and a peer must keep
        /// the framing its session negotiated even if the guest's mode were
        /// somehow to move underneath it.
        bool          multiplexed   = false;
        std::string   host;
        std::uint16_t port = 0;
        /// What the guest asked for in `AT+CIPSTART`. Chosen per connection, so
        /// a session can be TCP and the next one UDP through the same slot and
        /// the same transport object.
        Protocol      protocol   = Protocol::Tcp;
        /// `AT+CIPSTART`'s optional UDP `<local port>`; 0 = OS-assigned.
        std::uint16_t local_port = 0;
        /// When `connecting`, the wall-clock instant after which the connect
        /// is abandoned with `ERROR`. Meaningless otherwise.
        std::chrono::steady_clock::time_point connect_deadline{};

        // ── The two directions, once per protocol ─────────────────────
        //
        // TWO PAIRS RATHER THAN ONE, because a stream and a datagram are not
        // the same object. TCP is a BYTE queue: a partial `send` is normal and
        // the remainder is re-offered, and a `+IPD` may cut the stream
        // anywhere. UDP is a queue of WHOLE MESSAGES: one `AT+CIPSEND` is
        // exactly one datagram, one datagram is exactly one `+IPD`, and a
        // partial send does not exist. Flattening UDP into the byte queues
        // would silently coalesce two queued datagrams into one on the wire
        // and merge two received ones into a single mis-lengthed `+IPD` — the
        // "TCP-shaped UDP" that happens to work for a lone request/response
        // and corrupts everything else. Only the pair matching `protocol` is
        // ever used; the other stays empty.

        /// TCP peer -> guest, unbounded, pre-framing (pacing stage 1).
        std::deque<std::uint8_t> rx;
        /// TCP guest -> peer, whatever the kernel would not take yet.
        std::deque<std::uint8_t> tx;
        /// UDP peer -> guest, one entry per received datagram.
        std::deque<std::vector<std::uint8_t>> rx_datagrams;
        /// UDP guest -> peer, one entry per `AT+CIPSEND`, still unsent.
        std::deque<std::vector<std::uint8_t>> tx_datagrams;

        /// True while anything from the peer is waiting to be framed, whichever
        /// protocol this connection speaks.
        bool has_peer_data() const { return !rx.empty() || !rx_datagrams.empty(); }
        void clear_buffers() {
            rx.clear();
            tx.clear();
            rx_datagrams.clear();
            tx_datagrams.clear();
        }
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
    void cmd_cipclose_id(const std::string& args);
    void cmd_cipmux(const std::string& args);
    void cmd_cipserver(const std::string& args);
    void cmd_uart(const std::string& args);
    void cmd_gmr(const std::string& args);
    void cmd_cwjap(const std::string& args);
    void cmd_cipsta(const std::string& args);
    void cmd_cifsr(const std::string& args);
    void cmd_cipdns(const std::string& args);

    /// Shared by `AT+CIPSEND` and `AT+CIPSENDEX` — see simplification (3).
    void begin_send(const std::string& args, const char* name);

    /// Tear a LIVE connection down at the guest's request and tell it so:
    /// close the socket, drop whatever was buffered, emit
    /// `[<id>,]CLOSED` + `OK`, and return the slot to the pool. Shared by both
    /// spellings of `AT+CIPCLOSE` for one reason — the framing decision
    /// (prefixed or not) must be made in ONE place, exactly as
    /// `queue_ipd_header` is, or the two spellings drift apart on the wire.
    /// The caller has already established that the connection is there;
    /// deciding WHICH `ERROR` a missing one deserves is the caller's job,
    /// because the two spellings answer to different guests.
    void close_connection(std::size_t cid);

    // Engine -> guest.
    void queue(const char* text);
    void queue(const std::string& text);
    void queue_raw(const std::uint8_t* data, std::size_t len);
    void queue_ok()    { queue("\r\nOK\r\n"); }
    void queue_error() { queue("\r\nERROR\r\n"); }

    /// Queue a `+IPD` header. The ONE place the wire format is decided:
    /// unmultiplexed `+IPD,<len>:` when `multiplexed` is false,
    /// `+IPD,<id>,<len>:` when it is true. The caller passes the CONNECTION's
    /// own flag (`Connection::multiplexed`), never the engine's current mode,
    /// so a session that did not ask for multiplexing cannot be handed the
    /// multiplexed form by something another session did.
    ///
    /// Emitting the multiplexed form to a client that did not ask for it breaks
    /// nextsync outright, and WORSE THAN CLEANLY: its
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

    /// Take whatever the listener accepted into a free inbound slot, and emit
    /// the `<id>,CONNECT` that tells the guest about it. Engine state, so it
    /// belongs to the LOCKED half — see `EspListener`'s poll/accept split.
    void accept_connections();

    /// Drop an inbound connection's transport and return its slot to the pool.
    /// A no-op on `SINGLE_CID`, whose transport is borrowed and permanent.
    void release_inbound(std::size_t cid);

    /// True while a `+IPD` may be framed: nothing already queued for the
    /// guest, no command line in flight, not mid-payload and no connect in
    /// flight. This is what makes simplification (1) hold.
    bool wire_is_quiet() const;

    /// True while the bytes received since the last terminator could still
    /// become an AT command — i.e. the guest is part-way through a
    /// command/response transaction this engine must not interleave a URC
    /// into. Every command in `kCommands` begins `AT`, so a partial line that
    /// is neither a prefix of `AT` nor an extension of it is DEBRIS, not a
    /// transaction. See simplification (1) for the client that proved the
    /// difference matters.
    bool command_line_in_flight() const;

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

    /// Non-owning; null when the host built no server capability. The engine
    /// never creates one — a listener needs a bind address, which is host
    /// configuration and a security decision (design doc §13.4).
    EspListener* listener_ = nullptr;

    /// `AT+CIPMUX`. FALSE at power-on and after `AT+RST`, and that default is
    /// load-bearing rather than arbitrary: nextsync never sends the command and
    /// cannot survive the multiplexed `+IPD` form.
    bool        cipmux_ = false;

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
