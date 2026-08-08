# ESP-01 Wi-Fi Module — Emulator Design

**Issue:** [GH #25](https://github.com/jorgegv/jnext/issues/25) (v1.0) — follow-up
[#154](https://github.com/jorgegv/jnext/issues/154) (full datasheet-level emulation).
**Status:** v1.0 **complete**, six branches: 1 (UART device seam), 2 (socket transport), 3 (AT
engine, `6a79f6fd`), 3.5 (modularisation, `779b7103`, v0.99.68), `fix/esp-async-dns` (§7.4), 4
(CLI/config/wiring) and 5 (functional test + the pacing measurement) are all **merged**.
**UDP added by [#198](https://github.com/jorgegv/jnext/issues/198)** — see
[§5.7](#57-udp-gh-198).
**Last updated:** 2026-08-06.
**Audience:** jnext maintainers **and anyone reusing this module in another project**. The module
is deliberately shaped to be liftable; this document is its specification, not a jnext-internal
note.
**Licence:** the module is part of jnext and is therefore **GPLv3**, like the rest of the tree. A
reuser is bound by that — which is worth stating plainly in a document that spends §1.2 verifying
everyone else's licences. It also means the GPL-3.0 candidates surveyed there (sQLux-nextp8,
cuzebox-esp8266) could have been lifted from without a licence problem; they were not, for the
engineering reasons in §1.3.

> **Authority.** Hardware behaviour is cited from the ZX Spectrum Next FPGA VHDL
> (`cores/zxnext/src/`) and `nextreg.txt`. Guest-visible protocol behaviour is cited from the
> software that actually consumes it — the NextZXOS ESP driver and dot commands shipped on the
> official SD card, NXtel, nextsync and (for UDP) newt. Nothing in the v1.0 surface is derived from
> the Espressif AT manual; **§5.7 is the exception and says so** — UDP's wire forms come from the
> Espressif AT command set, cross-checked against the consumer that has to parse them. Nothing is
> asserted without a citation. Where something is untested, it is labelled untested.

---

## Table of contents

- [1. Justification](#1-justification)
  - [1.1 How the AT surface was derived](#11-how-the-at-surface-was-derived)
  - [1.2 The candidate survey](#12-the-candidate-survey)
  - [1.3 Build versus adopt](#13-build-versus-adopt)
  - [1.4 What was deliberately NOT built](#14-what-was-deliberately-not-built)
  - [1.5 The two acceptance targets, and why both](#15-the-two-acceptance-targets-and-why-both)
- [2. The seven deliberate modelling simplifications](#2-the-seven-deliberate-modelling-simplifications)
- [3. The three v1.1 shape choices](#3-the-three-v11-shape-choices)
- [4. The Next-side hardware interface](#4-the-next-side-hardware-interface)
- [5. The command set](#5-the-command-set)
  - [5.7 UDP (GH #198)](#57-udp-gh-198)
- [6. The timing model](#6-the-timing-model)
- [7. Architecture](#7-architecture)
- [8. Security posture](#8-security-posture)
- [9. Rejected alternatives](#9-rejected-alternatives)
- [10. v1.1 extension points](#10-v11-extension-points)
- [11. Open questions — to be tested, not assumed](#11-open-questions--to-be-tested-not-assumed)
- [12. Evidence index](#12-evidence-index)
- [13. Server mode (GH #210)](#13-server-mode-gh-210)
- [14. Per-connection close (GH #211)](#14-per-connection-close-gh-211)
- [15. Server idle timeout (GH #240)](#15-server-idle-timeout-gh-240)

---

## 1. Justification

This section comes first because it gives rise to everything after it. The command set follows
from §1.1, the architecture and timing model from §1.3, the security posture from the owner
decisions recorded in §8, and the extension points from what §1.4 deliberately deferred.

A reader should not need to re-litigate any of it. Each decision below was made against evidence,
and the evidence is named.

### 1.1 How the AT surface was derived

Not from the Espressif AT firmware manual. The surface was derived by reading **the software that
runs on a real Next and talks to a real ESP-01**:

| Source | What it is | What it gave |
|---|---|---|
| NextZXOS `ESPAT.DRV` + `ESPATreadme.TXT` | The OS driver, **source shipped on the official SD card** | Which URCs the driver tolerates, and which leave it in an unknown state (`ESPATreadme.TXT:92`) |
| NextZXOS dot commands (`.HTTP`, `.UART`, `.ESPBAUD`, `.zxdb-dl`, NXTP, internet-nextplorer) | **Source shipped on the official SD card** | The exact byte sequences each parser waits for; `.ESPBAUD`'s exact `"OK\r\n"` compare; `.UART`'s `OK`,13,10,`>` sequence |
| NXtel (`src/esp.asm`, `src/c31.asm`) | Third-party client, acceptance target #49 | The full init path, the `>` prompt busy-wait, the payload byte accounting, the diagnostics screen's `strstr` anchors |
| nextsync (`github.com/jarikomppa/specnext`, dir `sync/`) | Third-party client, acceptance target | An **almost disjoint** command set, the `+IPD` byte FSM, and the baud reprogramming that invalidated the original pacing model |

That the OS driver and the dot commands ship **as source on the SD card** is the reason this
analysis is evidenced rather than inferred, and it is why the v1.0 set is small and exact rather
than broad and approximate.

The result: **seven commands** get a real HTTP fetch working, and three additions
(`AT+CIPSENDEX`, the bare `\r\n` probe, `AT+UART_CUR`) plus six static diagnostic replies cover
both acceptance targets. See [§5](#5-the-command-set) for the table.

Ranking, as originally derived:

- **Rank 1 — get past init (4).** `ATE0`, `AT+CIPCLOSE`, `AT+CIPMUX=0`, `AT+RST`. Unblocks NXtel,
  `.HTTP`, NXTP, internet-nextplorer, `.UART`, `.zxdb-dl`.
- **Rank 2 — real TCP data (3).** `AT+CIPSTART`, `AT+CIPSEND` (with its `> ` prompt and
  `SEND OK`), and the unsolicited `+IPD` / `CLOSED`.
- **Rank 3 — NXtel's diagnostics screen (6 static replies).** `AT`, `AT+GMR`, `AT+CWJAP?`,
  `AT+CIPSTA?`, `AT+CIFSR`, `AT+CIPDNS_CUR?`. NXtel matches short anchor fragments
  (`c31.asm:192-201`), not whole lines, so canned replies suffice.
- **nextsync additions.** `AT+CIPSENDEX`, the empty-line probe, `AT+UART_CUR`.

Every evidenced client either sends `AT+CIPMUX=0` or (nextsync) relies on the power-on default
being 0, so `+IPD` is **always** the unmultiplexed `+IPD,<len>:` form. That halves the receive
path and is a hard constraint, not a convenience — see simplification B in [§3](#3-the-three-v11-shape-choices).

### 1.2 The candidate survey

Licences were checked **per file, from source**, not from repository metadata.

| Candidate | Licence (verified from source) | What it really has | Verdict |
|---|---|---|---|
| **cuzebox-esp8266** | GPL-3.0-**or-later** | `+IPD` **does not exist** (grep proves it). `CIPSEND=<len>` is dead code. UDP is parsed then forced to TCP (line 998). CIPMUX forced to socket 0 (line 997). Server always `ERROR`. **Zero tests.** 10 memory-safety defects including **2 guest-reachable buffer overflows**. ESP files untouched since Dec 2023; hard fork with no merge base; upstream dead since 2020. Misattributed copyright header. Real savings ≈ 200 usable lines. | **Reference only** |
| **sQLux-nextp8 ESP-01 model** (Chris January) | GPL-3.0 | Real TCP/UDP/**TLS**/DNS. Correct `+IPD,<id>,<n>:`, `>` prompt, `SEND OK`, URCs. 30 commands. Byte-at-a-time API that maps directly onto `UartChannel`. Built and driven through a real TCP round trip, real DNS and a real TLS handshake during this research. But: 3 commits, one author, one week old; POSIX-only; `CIPSERVER` stubbed; 3 `strcpy`/`sprintf` sites to audit. | **Strongest reuse candidate — socket-layer reference** |
| **PICSimLab `espmsim`** | GPL-2.0-or-later | Has the **server** side sQLux lacks (`bind`/`listen`/`accept`), and its `tcp.cc` is already dual-ported Linux/winsock2. No `CIPSTART`. | **Complementary; unused (no server mode in v1.0)** |
| **ZEsarUX** | GPL-3.0 | Does **not** emulate the ESP. Its own help text: *"It does NOT emulate a full uart device, just links… to a physical local device"*. Needs a real dongle. | **Dead lead** |
| **CSpect** | Closed source | Does emulate it, and **logs unknown AT commands to file**. Ships `NXtel.nex`. | **Differential oracle, not code** |
| **Espressif `esp-at`** | Apache-2.0 | The real firmware source; authoritative response strings. Apache-2.0 → GPLv3 is a clean one-way relicense. | **The spec** |
| **QEMU / espressif-qemu** | — | `hw/xtensa/` has `esp32.c` and `esp32s3.c` and **no ESP8266 target at all**. | **Ruled out on fact** |

### 1.3 Build versus adopt

**Decision: write our own AT engine against jnext's `UartDevice` seam; read sQLux as the
socket-layer reference; use CSpect + NXtel as the acceptance oracle.**

Licences were compatible in every viable case, so **this turned purely on engineering**:

1. **The required surface is small and now precisely specified**, down to exact response bytes
   (§5). That is roughly 400 lines we own and can test properly, versus auditing 3007 lines of
   week-old single-author C to this project's standard.
2. **The hard parts for jnext are provided by no candidate** — because no other emulator has this
   timing model or these constraints:
   - UART-tick-paced two-stage buffering (§6) — every candidate injects RX unpaced;
   - the `UartDevice` seam and its cold-boot lifetime hazard (§7.7);
   - replay/rewind gating (`replay_mode_`);
   - the Windows socket split;
   - the security model (§8).
3. **sQLux remains valuable** as a socket-layer reference and as a cross-check on framing
   decisions, and its licence permits lifting specific code if a piece proves worth it.

### 1.4 What was deliberately NOT built

These are **decisions with evidence**, not omissions. They are recorded here so a later reader
does not "fix" them.

| Not built | Evidence |
|---|---|
| ~~**Server / listen mode**~~ (`AT+CIPSERVER`) | ~~Appears **exactly once** in all software examined, and only to turn it **off**. NextZXOS's own listen/accept API is marked `***TODO, not implemented`. Not building it also removes the entire inbound attack surface.~~ **BUILT (GH #210)** — the first reason expired: [dezogif_ng](https://github.com/jorgegv/dezogif_ng) is a consumer, and DeZog only ever dials *out*, so something on the Next has to listen. The second did not expire and is **answered rather than retired** ([§13.4](#134-security-review--the-inbound-surface)): the listener binds **127.0.0.1** by default, `--esp-listen-address` is the explicit act that widens it, and a bind failure answers `ERROR` instead of falling back to a wider address. See [§13](#13-server-mode-gh-210). |
| ~~**UDP**~~ | ~~Zero consumers. nextsync is TCP-only; NXtel and the dot commands are TCP-only.~~ **BUILT (GH #198)** — the justification was "no consumer", and [`newt`](https://github.com/chris-y/newt) (GPLv3) is one. See [§5.7](#57-udp-gh-198). |
| **Passthrough** (`AT+CIPMODE`) | Appears **nowhere** in any examined software. |
| ~~**Multiplexed connections**~~ (`AT+CIPMUX=1`) | nextsync never sends `AT+CIPMUX` at all — it relies on the power-on default — and its `+IPD` byte FSM does not merely reject `+IPD,<id>,<len>:`, it **silently mis-parses** it into a corrupted length (§3, choice B). Since **no command can correct a wrong default at runtime**, the default must be 0 ~~and `=1` must be refused loudly rather than accepted-and-ignored~~. **BUILT (GH #210)** — `AT+CIPSERVER` requires it, so the command arrived with server mode and now answers `OK`. The evidence above did **not** expire; it changed what it constrains. It is why the **power-on default stays 0**, why `CIPMUX=1` happens only on explicit command, and why the multiplexed `+IPD` form reaches only connections owned by a session that asked ([§13.3](#133-the-constraint-that-makes-this-real-work)). What v1.0 was protecting was the *default*, and that is untouched. |
| **TLS** | No evidenced consumer. sQLux has it; nothing on the Next asks for it. |

`AT+CIPMUX=1` was answered `ERROR` until GH #210 gave it a consumer; it is answered `OK` now
([§13.2](#132-why-it-is-three-commands-and-not-one)), and `AT+CIPMUX=0` — the line NXtel sends at
init — answers `OK` exactly as it always did.

The sentence that used to follow, *"that is the one place where refusing is safer than
accepting"*, is kept rather than deleted, because it is still true of the thing it was actually
about: silently accepting **and ignoring** would promise a wire format that breaks the one client
which cannot ask for it back. That alternative is still rejected ([§9](#9-rejected-alternatives)).
What GH #210 changed is that the command is now *honoured*, which is the opposite of ignoring it —
and the protection moved to where the evidence always pointed: the power-on **default**, which
stays 0 and which no command can correct at run time.

### 1.5 The two acceptance targets, and why both

NXtel and nextsync were both made hard requirements because their command sets are **near
disjoint**. Passing one proves almost nothing about the other.

| | NXtel (#49) | nextsync |
|---|---|---|
| Init | `ATE0` → `AT+CIPCLOSE` → `AT+CIPMUX=0` | bare `\r\n` probe → `AT+UART_CUR=…` → `ATE0` → `AT+CIPCLOSE` (looped ≤10× **while `ERROR` is NOT seen**) |
| Connect | `AT+CIPSTART="TCP","nx.nxtel.org",23281` | `AT+CIPSTART="TCP","<host>",2048` |
| Send | `AT+CIPSEND=<n>` | **`AT+CIPSENDEX=<n>`** |
| Receive | `+IPD` via the NextZXOS driver | `+IPD,<len>:` via its **own byte FSM** |
| Diagnostics | All six Rank-3 replies (Network Settings screen) | **None** — no `AT+GMR`, `AT+CWJAP`, `AT+CIFSR`, `AT+CIPSTA`, `AT+CIPDNS*`, `AT+CWMODE`, not even `AT+RST` |
| `AT+CIPMUX` | Sent explicitly (`=0`) | **Never sent** |
| Reset path | `AT+RST` | **NextREG 0x02 bit 7** (`nextsync.c:396-399`) — the hardware line |
| Baud | 115200 throughout | **1 152 000** (`sync`) / **2 000 000** (`syncfast`), reprogrammed via `AT+UART_CUR` without waiting for `OK` |
| 8-bit data | ESC-escaped 8-bit bytes — **CSpect fails here** (`docs/dotcommands/http.md:76`) | Raw binary payloads |

nextsync uses **only three** of the original seven and **none** of the diagnostics — and its baud
reprogramming is what invalidated the original per-frame pacing model (§6.2). Discovering that
widened v1.0 scope; it is the single most expensive thing this analysis found, and it was found
only by reading a second, unrelated client.

---

## 2. The seven deliberate modelling simplifications

This section is first-class on purpose. A reader — including someone reusing the module — must be
able to see what was deliberately not modelled, and why, without reading the source. Anything a
later branch adds to the list goes **here**.

Each item is a **decision with a reason**, not an omission.

**1. Responses are serialised.** Real hardware happily interleaves an unsolicited `+IPD` between a
command line and its `OK`. This engine never does: a `+IPD` is framed only when the guest-bound
queue is empty **and** no command is in flight (`wire_is_quiet()`). This makes every guest
parser's job strictly easier and no parser's job harder.

  **What "in flight" means was too broad, and GH #198 narrowed it** — the sentence above was
  falsified by a real client. It used to mean "the command-line buffer is non-empty", and newt
  leaves a byte in it for ever (its `AT+CIPSEND=48` is followed by 49 bytes), so the `+IPD` it was
  waiting for was never framed at all. The gate now asks whether the partial line **can still
  become an AT command**. Strictly closer to hardware; the half-typed-command case is unchanged.
  Full account in [§5.7](#57-udp-gh-198).

**2. Echo defaults OFF**, unlike real AT firmware which powers up with it on. Every evidenced
client sends `ATE0` before anything that matters and none relies on the echo, while `.ESPBAUD`'s
**exact** compare against `"OK\r\n"` is precisely the parser an unexpected echo breaks. `ATE0` and
`ATE1` are both implemented and really do toggle it, so the capability is real and the default is
one line to flip.

**3. `AT+CIPSENDEX` is an alias for `AT+CIPSEND`.** Its distinguishing feature — early-terminate
on a `\0` in the payload — is never exercised: nextsync's payloads are 3–7 bytes and contain no
NUL.

**4. `AT+CIPSTART` failure answers `ERROR` only**; `FAIL` is never emitted. `ERROR` is the refusal
path every client already handles gracefully.

**5. No save-state.** A live TCP connection is host topology, not machine state, and rewinding
into a re-established socket is meaningless. The branch that makes the ESP reachable owns the
`replay_mode_` gate.

**6. Only `AT+CIPSTART` has a timeout** (10 s, `DEFAULT_CONNECT_TIMEOUT_MS`). It needs one because
a host that silently black-holes SYN — an ordinary stateful-firewall posture — is otherwise
answered only when the OS gives up (≈127 s on Linux), and the guest is not merely waiting during
that: NXtel's `ESPReceiveWaitOK` after `Connect` (`esp.asm:39-40`) has **no timeout and runs under
`di`**, so a black-holed connect **freezes** the guest rather than degrading it. No other command
can outlive its own dispatch, so none needs a deadline.

  The 10 s figure is bounded from both sides: **above** any real handshake (Linux retransmits a
  lost SYN at ≈1 s and again at ≈3 s, so even two lost SYNs complete by ≈4 s), and **below** the
  OS giving up. It is deliberately not shorter — the guest is busy-waiting, not the emulator, so a
  generous deadline costs a stalled guest program while a mean one refuses connections that would
  have worked.

  **Two residual gaps, stated rather than hidden:** (a) the deadline is checked in `poll()`, so it
  **cannot preempt the transport's synchronous `getaddrinfo`** — it bounds the TCP handshake, not
  name resolution; (b) an **established** connection that goes silent is never timed out, because
  TCP itself does not consider that an error and no evidenced client expects one.

**7. The `+IPD` chunk floor is a consequence, not an invariant.** nextsync budgets 5 chunks per
server packet (`timeout=5` in its own source), which the research framed as "emit chunks ≥ 292
bytes". Only the **2048-byte ceiling** (`MAX_IPD_CHUNK`) is enforced. The floor *emerges* from
coalescing — a chunk is cut only once the guest-bound queue has drained, so under load chunks are
large — but it is **probabilistic, not guaranteed**: at 1.152 Mbaud an MSS-sized 1460-byte chunk drains in
**≈12.7 ms** (1460 × 10 bits ÷ 1 152 000), i.e. well inside a 20 ms frame, so sufficiently jittery
TCP fragmentation could in principle cut a sub-292-byte chunk and pressure the budget. Unlikely on
localhost/LAN and not deterministically unit-testable, so it is recorded as a known characteristic
rather than defended by a floor the code does not implement.

---

## 3. The three v1.1 shape choices

Also first-class, for the same reason: they cost nothing today and they are the class of thing a
later branch **cannot cheaply retrofit**. They do not widen v1.0 scope; they prevent foreclosing
[#154](https://github.com/jorgegv/jnext/issues/154).

**A. Connections are a table, not "the connection".** `conn_` is a fixed array of
`MAX_CONNECTIONS` (**5** — ESP-AT's own `AT+CIPMUX=1` limit, ids 0..4, which is why it is that
number and not a rounder one). Every per-connection field — transport, host, port, buffers, close
state — lives **in the slot**. v1.0 only ever touches `SINGLE_CID` (0), and only that slot is
given a transport, which is what makes the rest inert with no extra guard: every per-slot loop
skips a slot with no transport. The state machine already speaks in connection ids, so adding
`AT+CIPMUX=1` is filling in blanks, not surgery.

**B. The `+IPD` emitter takes a connection id and a multiplexed flag.** The wire format genuinely
differs (`+IPD,<len>:` vs `+IPD,<id>,<len>:`), so the choice is made in **one function**
(`queue_ipd_header`) rather than baked into the call site. v1.0 passes `SINGLE_CID, false` and
produces exactly the unmultiplexed bytes the parsers require. Emitting the multiplexed form today
would break nextsync outright. Its FSM accumulates **then** validates
(`datalen += r - '0';` runs *before* the `if (r != ':' && (r < '0' || r > '9')) return 0;`, which
applies to the *next* byte), so on `+IPD,0,14:` it silently folds the `,` into the length as a
bogus digit (`',' - '0'` = −4), yielding a corrupted `<len>` and desynchronising the stream — it
does not bail. A silent corruption is worse than a refusal, which is exactly why the decision is
centralised rather than left to whoever adds CIPMUX support.

**C. AT dispatch is a table.** `kCommands` maps a command string to a uniform
`void(const std::string& args)` member handler, with a `prefix` flag distinguishing exact-match
entries from `NAME=<args>` entries. Adding the ≈40 commands v1.1 wants is adding rows, not
extending an if/else chain.

---

## 4. The Next-side hardware interface

Facts about the machine the module attaches to. Two of them were stated wrongly earlier in the
design record and are corrected here; both corrections are VHDL-verified.

### 4.1 The ESP is UART 0

`zxnext.vhd:1611` (`o_UART0_TX <= uart0_tx_esp`), `zxnext.vhd:3381` (`-- uart 0 (esp)`).
UART **1** is the Raspberry Pi GPIO header. jnext follows this:
`src/peripheral/uart.h:496` — *"0 = ESP, 1 = Pi"*.

### 4.2 A real ESP reset line DOES exist — NextREG 0x02 bit 7

An earlier claim that "there is no ESP reset line in the VHDL" is **wrong**. The line is real:

- `zxnext.vhd:5119` — `nr_02_bus_reset <= nr_wr_dat(7)`
- `zxnext.vhd:1579` — `o_RESET_PERIPHERAL <= nr_02_bus_reset`
- `nextreg.txt:48` (write), verbatim: *"Assert and hold reset to the expansion bus and the esp
  wifi (hard reset = 0)"*; `nextreg.txt:39` (read): *"bit 7 = 1 if the reset signal to the
  expansion bus and esp is asserted"*

jnext already latches the bit into `nr_02_bus_reset_` (`src/core/emulator.cpp:2719`) and **nothing
consumes it**. **nextsync uses exactly this as its "Resetting esp, try again" recovery path**
(`nextsync.c:396-399`), so a device-facing hook for it is a real future requirement — one driven
from the **NR 0x02 write**, entirely separate from the UART channel reset. It is classified
"degraded, not blocking" for v1.0 and is not built yet.

### 4.3 What is NOT a reset

- **NR 0xA8 / 0xA9 drive ESP GPIO0 only** — the bootloader-select pin. `nextreg.txt:942-950`:
  *"bit 0 = GPIO0 output enable"*, *"bit 2 = GPIO2 output enable (fixed at 0, GPIO2 is
  read-only)"*. GPIO2 is input-only. Neither is a reset.
- **`UartChannel::reset` must NOT drive a device reset.** It fires on a machine soft reset and on
  a guest write of framing bit 7, both of which reset only the **Next-side** UART state machine:
  `uart_tx.vhd:166` and `uart_rx.vhd:219` both gate on `i_reset = '1' or i_frame(7) = '1'` and
  return TX to `S_IDLE` / RX to `S_PAUSE`. Neither touches the module on the far end of the cable.
  Hanging a device reset off this path would hand the guest a capability the silicon does not
  have. This is why `UartDevice` has **no** `reset()` method.

### 4.4 There is no hardware flow control

Issue 2 hardwires CTS asserted: `zxnext_top_issue2.vhd:2387` — `i_UART0_CTS_n => '0'`. The Next
wiki states plainly that *"the esp ignores hardware flow control"*, and nextsync's own
`AT+UART_CUR=1152000,8,1,0,0` ends in `0` = **flow control disabled**, which turns the assumption
into evidence. internet-nextplorer's author routes through a proxy because of exactly this.

**Consequence:** the emulated ESP is the *only* thing in the system that can provide backpressure,
and it provides it by not sending faster than the guest drains. That is the whole flow-control
story, and it is why §6 exists.

### 4.5 The RX FIFO and its interrupt

- **512 entries, drop-newest on overflow.** `serial/uart.vhd:148-149` — the RX FIFO read/write
  addresses are `std_logic_vector(8 downto 0)`, i.e. 9 bits = 512 slots. `uart.vhd:798` gates the
  write on `(not uart0_rx_fifo_full)`, so a byte arriving at a full FIFO is **dropped**, not
  queued. `uart.vhd:540` latches `uart0_status_rx_err_overflow`.
- **The RX interrupt fires per byte when NR 0xC6 bit 1 is clear.** `zxnext.vhd:1941-1944` feeds
  `uart0_rx_near_full or (uart0_rx_avail and not nr_c6_int_en_2_210(1))` into `im2_int_req`: with
  bit 1 (UART0 Rx near full) **set**, near-full is the only contributor — `nextreg.txt:1101`,
  *"Rx near full overrides Rx available"*. `rx_near_full` is the ¾ mark, i.e. 384 bytes
  (`serial/uart.vhd:427`, `-- held (3/4)`). NextZXOS's `ESPAT.DRV` is interrupt-driven — this
  mattered for the pacing question, which [§11.2](#112-the-rx-pacing-is-settled--baud-pacing-stays)
  now settles by measurement: refilling the FIFO on demand pins this bit's `near_full` term
  permanently high and starves the guest.

---

## 5. The command set

Follows directly from [§1.1](#11-how-the-at-surface-was-derived). Case-insensitive on the command
**name**; arguments are never case-folded (a hostname keeps its case).

### 5.1 The command set, as shipped

**This table is the whole surface, not v1.0's.** It was headed *"v1.0 commands"*
until GH #198, #210 and #211 each added to it, and that label is what let the
`AT+CIPMUX=1` row go on claiming `ERROR` after GH #210 had made it `OK`: a reader
scanning for a command sees a row, never a scope heading. So a post-v1.0 addition
is annotated **in place**, with the issue that brought it and the section that
justifies it — the convention [§5.7](#57-udp-gh-198) already set for UDP — rather
than filed in a separate table the scanner never reaches. The GH tag in the Notes
column *is* the version boundary, and it carries more than a heading could.

| Line from guest | Reply (exact bytes) | Notes |
|---|---|---|
| *(empty line)* | `\r\nERROR\r\n` | nextsync's liveness probe, and what NXtel's trailing payload CR,LF produces |
| `AT` | `\r\nOK\r\n` | |
| `ATE0` / `ATE1` | `\r\nOK\r\n` | Really toggles echo; default OFF (simplification 2) |
| `AT+RST` | `\r\nOK\r\n\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n` | Drops every connection **without** a `CLOSED` — the guest asked for it |
| `AT+CIPSTART="TCP","<host>",<port>[,<keepalive>]` | deferred → `\r\nOK\r\n` or `\r\nERROR\r\n` | Reply comes from `poll()`, not from dispatch. The keepalive is accepted and ignored |
| `AT+CIPSTART="UDP","<host>",<port>[,<local port>[,<mode>]]` | deferred → `CONNECT\r\n\r\nOK\r\n` or `\r\nERROR\r\n` | GH #198, [§5.7](#57-udp-gh-198). **No leading CRLF before `CONNECT`** — it is a status line, not a result code. `<mode>` other than 0 → `ERROR` |
| `AT+CIPSTART="SSL",…` and anything else | `\r\nERROR\r\n` | Still no consumer |
| `AT+CIPSEND=<n>` | `\r\nOK\r\n> ` → *n payload bytes* → `\r\nSEND OK\r\n` | **Trailing space after `>` is mandatory** |
| `AT+CIPSENDEX=<n>` | identical | Alias (simplification 3) |
| `AT+CIPCLOSE` | `\r\nCLOSED\r\n\r\nOK\r\n` — or `\r\n0,CLOSED\r\n\r\nOK\r\n` on a multiplexed session; `\r\nERROR\r\n` if nothing open | Closes the **outbound** connection, in every mode, and never acquires a second meaning under `CIPMUX=1` ([§14.3](#143-the-bare-spelling-does-not-acquire-a-second-meaning)). The `ERROR` case is load-bearing — see below |
| `AT+CIPCLOSE=<id>` | `\r\n<id>,CLOSED\r\n\r\nOK\r\n` | GH #211, [§14](#14-per-connection-close-gh-211). Closes that link id and returns its slot to the pool. `ERROR` under `CIPMUX=0`, for an id with no live connection, and for ESP-AT's close-all `=5` — which is refused as a **decision**, not as a range accident |
| `AT+CIPMUX=0` | `\r\nOK\r\n` | |
| `AT+CIPMUX=1` | `\r\nOK\r\n` | GH #210, [§13](#13-server-mode-gh-210). Refused until server mode had a consumer. The power-on default is still 0, and a real mode **change** is still refused while a connection is open — or, back to 0, while the server is up |
| `AT+CIPSERVER=1,<port>` | `\r\nOK\r\n` | GH #210, [§13](#13-server-mode-gh-210). Needs `AT+CIPMUX=1` first, else `ERROR`. Port 0, a second server, an unbindable port and a missing port are all `ERROR` — a bind failure never falls back to another port or a wider address ([§13.4](#134-security-review--the-inbound-surface)) |
| `AT+CIPSERVER=0` | `\r\nOK\r\n` | Retires the listener and deliberately **leaves established connections alone**. `ERROR` when no server is running — a deliberate divergence from firmware, which says `OK` ([§13.7](#137-what-implementation-decided-that-13-did-not)) — and `ERROR` for ESP-AT's `<close_all>` argument |
| `AT+CIPSTO=<time>` | `\r\nOK\r\n` | GH #240, [§15](#15-server-idle-timeout-gh-240). Server idle timeout, `0~7200` seconds inclusive; anything else — out of range, negative, empty, non-numeric, a trailing argument — is `ERROR`. `0` means never. Enforced against **inbound** connections only, and **not** written to flash |
| `AT+CIPSTO?` | `\r\n+CIPSTO:<time>\r\n\r\nOK\r\n` | GH #240. Power-on **180**, which is what a real Ai-Thinker ESP-01 on AT 1.2.0.0 answers, and what `AT+RST` restores |
| `AT+UART_CUR=<baud>,…` / `AT+UART_DEF=` / `AT+UART=` | `\r\nOK\r\n` | Recorded and traced; pacing follows the channel's **live prescaler**, so nothing else is needed |
| `AT+GMR` | canned version block | Anchors `T version:` and `DK version:`, each printed to the next `(` |
| `AT+CWJAP?` | `\r\n+CWJAP:"<SSID>","<BSSID>",1,-55\r\n\r\nOK\r\n` | |
| `AT+CIPSTA?` | `+CIPSTA:ip/gateway/netmask` block | Anchors `gateway:"`, `netmask:"` |
| `AT+CIFSR` | `+CIFSR:STAIP,…` / `STAMAC,…` | Anchors `TAIP,"`, `TAMAC,"` |
| `AT+CIPDNS_CUR?` | two `+CIPDNS_CUR:` lines | |
| *anything else* | `\r\nERROR\r\n` | Including a line over `MAX_COMMAND_LEN` (512), which is refused **whole** — a truncated `AT+CIPSTART=` is still a valid `AT+CIPSTART=` and would open a connection to a host the guest never named |

**Unsolicited (URC):**

The `+IPD` and `CLOSED` spellings follow the **connection**, fixed when it opens
and never varying byte to byte: a session that never sent `AT+CIPMUX` sees the
unprefixed form it has always seen, and only a connection owned by a `CIPMUX=1`
session sees the prefixed one ([§13.3](#133-the-constraint-that-makes-this-real-work)).

| Emitted | When |
|---|---|
| `\r\n+IPD,<len>:<data>` | Peer data on a single-connection session, framed only when the wire is quiet (simplification 1) |
| `\r\n+IPD,<id>,<len>:<data>` | The same, on a connection owned by a `CIPMUX=1` session (GH #210) |
| `\r\nCLOSED\r\n` | Peer closed — deferred until everything already received has been framed and drained |
| `\r\n<id>,CLOSED\r\n` | Peer closed a multiplexed connection, with the same deferral (GH #210) |
| `\r\n<id>,CONNECT\r\n` | An inbound connection was **accepted** (GH #210, [§13](#13-server-mode-gh-210)). Always prefixed — a server requires `AT+CIPMUX=1` — and `<id>` runs **1..4**, never 0, which stays the guest's own outbound slot |
| `\r\n<id>,CLOSED\r\n` | The **module** hung up on an inbound client that had been silent for `AT+CIPSTO` seconds (GH #240, [§15](#15-server-idle-timeout-gh-240)). Byte-identical to a peer close, and deliberately so — the guest has one thing to parse, not two. That this is the spelling real firmware uses here is **inferred**, not observed ([§15.3](#153-what-is-inferred-and-what-is-measured)) |

### 5.2 The framing constraints that actually bite

- **The `> ` prompt.** Three parsers, three requirements: NXtel busy-waits for a bare `>` **with
  no timeout at all**; internet-nextplorer likewise; `.UART` waits for the exact sequence
  `OK`,13,10,`>`. Emitting `"\r\nOK\r\n> "` — **trailing space included** — satisfies all three.
  Emitting nothing hangs the guest **forever**: these are polled busy loops with no error path.
- **`.ESPBAUD` does a full exact compare against `"OK\r\n"`.** This is why every terminator in the
  engine is CRLF and never a bare LF.
- **`CLOSED` is detected by a 5-byte window** — the client matches `OSED` plus its terminator, not
  the whole word. (Its own comment reads *"It enough to check 'OSED\n' :-)"*, but the emitted
  terminator here is CRLF like every other reply, so what the window actually sees is `OSED\r`.)
- **`AT+CIPCLOSE` with nothing open must answer `ERROR`.** nextsync loops it up to 10 times *while
  `ERROR` is NOT seen*; answering `OK` costs about a second per connect.
- **No `+` may appear between the guest's payload and the `+IPD`** — nextsync's FSM scans for the
  first `+`.
- **`SEND OK` does satisfy NXtel's wait**, despite appearances. `ESPReceiveWaitOK` enters its
  `'S'` → `SEND FAIL` branch, but `MatchSendFail` (`esp.asm:418-427`) loads `hl, Error` where
  every sibling loads its own string, so after `'S'` it demands the impossible 16-byte sequence
  `RROR\r\nEND FAIL\r\n`. The next byte `'E'` mismatches `'R'`, the FSM resets, `N D <space>` are
  discarded, and the `'O'` of `OK` takes the `MatchOK` path and completes on `K CR LF`. So
  `"\r\nSEND OK\r\n"` is not merely acceptable — it is the **only** thing that can satisfy it,
  because `AT+CIPSEND=1` (`esp.asm:70-74`) has no other reply coming.

### 5.3 Payload byte accounting

NXtel's `AT+CIPSEND=3` is followed by **five** bytes: `db 255, 253, 39, CR, LF` with
`IacDoNewEnvironLen equ $-IacDoNewEnviron` (`esp.asm:46-48`). Its `AT+CIPSEND=10` is followed by
**twelve** (`c31.asm:368-371`). The trailing CR,LF is **not payload** — it is structural:
`ESPSendProc` plants bytes inline in the code stream and resumes with `jp (hl)` at the byte after
them (`esp.asm:558-575`), so every inline block is CRLF-terminated whether it is an AT line or not.

So after exactly `<n>` payload bytes the engine returns to command mode, and the trailing CR,LF
forms an **empty command line** answered `ERROR` — which is the same behaviour nextsync depends on
for its probe, and which NXtel's `ESPReceiveWaitOK` happens to accept as well. Consuming the CR,LF
as payload instead would send two bytes the guest never meant to send and desynchronise the
stream by one for everything after.

### 5.4 Strings that must NEVER be emitted

`busy p...`, `ALREADY CONNECTED`, `SEND FAIL`, `link is not valid`, `no ip`, `ready`.

Nothing parses them, and `ESPATreadme.TXT:92` warns that an unexpected `CLOSED` / `WIFI DISCONNECT`
leaves the NextZXOS driver in an unknown state. `CLOSED` is emitted **only** when a connection
genuinely closed. Where real firmware would say `busy p...` (input during an in-flight connect),
this engine **defers the input** instead: nothing is lost and nothing is answered out of order.

### 5.5 8-bit clean

The path from guest to peer and back must be **byte-transparent**. NXtel uses ESC-escaping for
8-bit bytes and **CSpect fails here** (`docs/dotcommands/http.md:76`) — an opportunity to be
better than the incumbent. Nothing in the engine interprets, translates or strips a payload byte.

### 5.6 Synthetic identity

Every value the module reports about "its" network is a **constant** (owner decision, §8.3):

| Field | Value |
|---|---|
| SSID | `JNextWifiHost` |
| AP BSSID | `02:00:00:00:00:01` |
| Station MAC | `02:00:00:00:00:02` |
| Station IP | `192.168.1.50` |
| Gateway | `192.168.1.1` |
| Netmask | `255.255.255.0` |
| DNS1 / DNS2 | `192.168.1.1` / `8.8.8.8` |
| `AT+GMR` | `AT version:1.7.4.0(jnext emulated ESP-01)` … |

These are cosmetic: nothing routes through them. The parenthesised text in `AT+GMR` is invisible
to NXtel (it prints up to the `(`), which is where the honest "this is not real firmware" marker
goes.

### 5.7 UDP (GH #198)

v1.0 refused UDP, and the reason recorded in [§1.4](#14-what-was-deliberately-not-built) was
**"zero consumers"** — not "hard", not "out of scope". [`newt`](https://github.com/chris-y/newt)
(Chris Young, GPLv3) is one, so the reason expired and the feature was built.

**The specification is two documents, and they agree.** The wire forms are Espressif's
(`AT+CIPSTART=<"type">,<"remote host">,<remote port>[,<local port>,<mode>]` answered `CONNECT` then
`OK`; `AT+CIPSEND=<length>` answered `OK` + `>` then `SEND OK`; one `+IPD,<len>:` per received
datagram in single-connection mode). The consumer is newt's `sntp` command, whose exact sequence is
`ATE0` → `AT+CIPCLOSE` → `AT+CIPSTART="UDP","<server>",123` → `AT+CIPSEND=48` + a 48-byte NTP
packet → read one `+IPD` → `AT+CIPCLOSE`. This section records only where the two needed
reconciling, or where a decision was made.

#### It is not TCP with a different socket type

Three properties a byte stream does not have, each of which would pass a lone request/response
exchange (all SNTP is) and corrupt anything busier:

| Property | Why the stream code is wrong for it |
|---|---|
| **Datagram boundaries** | One `AT+CIPSEND` is one datagram and one datagram is one `+IPD`. The per-connection queues are therefore a second pair, of whole messages — reusing the byte deques would concatenate two queued sends into one datagram, and merge two received ones into a single mis-lengthed `+IPD`. |
| **`recv() == 0` is not EOF** | A datagram socket has no end of stream, and a zero-length datagram is legal. `net::recv` takes a `stream` flag; without it the first empty datagram a peer sends closes a live connection. |
| **All-or-nothing reads and writes** | A datagram is read whole or truncated (the kernel discards the remainder), so the read buffer is the `+IPD` ceiling, not the 1 KB stream chunk. Winsock reports that truncation as `WSAEMSGSIZE` where POSIX reports a short read; the twin folds it back so both behave alike. |

`::connect()` on a datagram socket only records the peer, so it completes immediately: a UDP
`AT+CIPSTART` never passes through `Connecting` and is answered on the first `poll()`.

#### `CONNECT` precedes `OK` for UDP, and not for TCP

Real firmware prints `CONNECT\r\n\r\nOK\r\n` for **both**, and newt's `net_connect_udp` reads
exactly ONE line and returns false unless it begins `CONNECT`. So UDP must emit it.

**TCP was left alone**, deliberately. Its bare `\r\nOK\r\n` is pre-existing v1.0 behaviour derived
from the evidenced clients, it is pinned by `esp-loopback-func`'s exact byte stream, and the two
clients whose parsers would have to survive the change — NXtel and nextsync — have no source on
this machine to check it against ([§12](#12-evidence-index) says so itself). Making TCP
firmware-exact is a genuine improvement and a **separate change with its own evidence to gather**;
bundling it here would have put the one live-traffic-proven path at risk for a client that does not
use TCP.

**There is no leading CRLF before `CONNECT`.** `CONNECT` is a *status* line, not a result code, so
the blank line a transcript shows before `OK` belongs to the `\r\nOK\r\n` after it. This was got
wrong first — the implementation prefixed it like every other reply — and **no amount of reading
the code found it**: newt's one-line read saw an empty line, `net_connect_udp` returned false, and
the tool gave up silently without sending anything. It was found by running the real dot command
against a real NTP server, and it is now pinned as its own row (`UDP-01c`) stated as the property
rather than as a substring of a literal.

#### `<mode>` 1 and 2 are refused, not ignored

Mode 0 — the default, and the only one any client sends — fixes the peer for the life of the
connection, which is exactly a `connect()`ed socket. Modes 1 and 2 re-point the peer at whoever
last sent to us; that needs an unconnected socket, `recvfrom` bookkeeping, and a **second security
decision about who is allowed to become the peer**. Refused for the `AT+CIPMUX=1` reason
([§1.4](#14-what-was-deliberately-not-built)): accepting silently would promise a behaviour the
guest cannot ask back.

`<local port>` **is** honoured, and binding it is the only part of this that is observable solely
from the far end — `UDPT-10` asserts the source port the peer sees, with `UDPT-11` as the control.

#### Simplification 1 was narrowed, because a real client falsified it

Recorded here as well as in [§2](#2-the-seven-deliberate-modelling-simplifications) because it is
the one change outside UDP itself. The URC gate used to hold a `+IPD` back whenever `line_` was
non-empty, justified as *"makes every guest parser's job strictly easier and no parser's job
harder"*. It made one parser's job **impossible**.

newt's `uart_tx_bin` (`uart.c`) is `do { … } while (size--)`, which transmits `size + 1` bytes: after
`AT+CIPSEND=48` it puts **49** on the wire. The 49th is one byte past its own `calloc(48)` — arbitrary
heap content — and it lands in the command-line buffer, where it stays until the next CR. Under the
old rule that byte held off every `+IPD` **for ever**, so the reply newt was waiting for was never
framed and it died on its own 5 s timeout. Real firmware frames the URC regardless.

The gate now asks whether the partial line **can still become an AT command**: every command begins
`AT`, so debris that does not is not a transaction. That is strictly closer to hardware and keeps
the property the serialisation exists for — a genuinely half-typed `AT+CIPM` still holds the `+IPD`
back (`IPD-09`, unchanged), which is what stops a `+IPD` payload byte being mistaken for the `> `
prompt NXtel busy-waits on with no timeout.

**Residual, stated rather than hidden:** if that stray byte happens to *be* `A`, it is
indistinguishable from a guest starting to type and the stall returns. One value in 256. The honest
fix is the guest's own off-by-one, not a rule this engine can write. (In the observed run the byte
was `z`.)

#### Evidence

| Claim | How it was established |
|---|---|
| newt's exact AT sequence, and its one-line `CONNECT` compare | `main.c`, `sntp.c`, `net.c`, `uart.c` read from source |
| `uart_tx_bin` sends `size + 1` | Read from source **and** compiled and run — 49 for 48 |
| The whole path works | **The real `.newt` dot command**, built with z88dk, installed into a private clone of the SD image, booted under NextZXOS in jnext, fetching the time from **`pool.ntp.org` over the live internet** (Cloudflare `162.159.200.123`). It printed the correct UTC. The trace shows the stray byte arriving as `AT <- "zAT+CIPCLOSE"` *after* the `+IPD` had been framed |
| It stays working | `esp-udp-sntp-func` — a Z80 guest reproducing newt's off-by-one against a real UDP SNTP peer, asserting the guest-visible stream as an exact byte sequence |

---

## 6. The timing model

### 6.1 Two stages, two clocks

```
   host network                                          emulated machine
        │                                                        ▲
        │  poll()  — WALL CLOCK, once per frame                  │
        ▼                                                        │
  ┌───────────────┐   frame_ipd()   ┌──────────────┐   tick() — EMULATED TIME
  │ per-slot rx   │ ──────────────► │ out_ queue   │ ───────────────┘
  │ (unbounded)   │  when quiet     │ (framed)     │  1 byte per
  └───────────────┘                 └──────────────┘  prescaler × frame_bits
```

- **`poll()` — wall clock.** All socket work: advance the transport, complete a pending connect,
  flush queued outbound data, drain the socket into the **unbounded** per-slot buffer, notice a
  peer close. No pacing at all: the ESP processes as fast as it likes.
- **`tick(elapsed_ticks, ticks_per_byte)` — emulated time.** Frames a `+IPD` when the wire falls
  quiet and releases **one byte per `ticks_per_byte`**. jnext passes 28 MHz cycles and
  `UartChannel::byte_transfer_ticks()` = `prescaler() × frame_bits()`, read **live on every call**,
  which is the whole reason `AT+UART_CUR` and `.ESPBAUD` work for free.

  **The units are the caller's.** The module owns no clock and never asks what time it is; it is
  told how much time passed and how much time a byte costs, in whatever unit the caller counts in.
  A consumer with no baud to model passes `(1, 1)` and gets one byte per call.

  **The pacer lives in the core, deliberately, and not in the host.** It is inseparable from
  `+IPD` coalescing: a chunk is cut only once `out_` has drained (`wire_is_quiet`), so a host that
  drained at its own rate would cut a chunk per poll and destroy the large-chunk property §6.3
  depends on. Keeping it in the core is what makes the module's output correct for *any* host,
  not just for one that happens to drain the way jnext does.

The accumulator only banks cycles while there is something to release; an idle period cannot bank
credit and then dump a burst the instant data appears, and it is zeroed whenever the queue empties
so a sub-byte remainder cannot let a later reply leave early.

### 6.2 Why per-frame batching fails, arithmetically

The original "230 bytes/frame at 115200 is fine, once per frame should be enough" budget was true
**only because it assumed 115200**. nextsync's first act is to invalidate that assumption.

| Link rate | Bytes/s (10 bits/byte) | Bytes per 20 ms frame | Bytes per scanline (312 lines/frame) |
|---|---|---|---|
| 115 200 | 11 520 | 230 | 0.74 |
| **1 152 000** (`sync`) | 115 200 | **2 304** | 7.4 |
| **2 000 000** (`syncfast`) | 200 000 | **4 000** | 12.8 |

Two independent failures follow, and per-frame batching fails **both**:

1. **FIFO overflow.** 2304 bytes into a 512-byte drop-newest FIFO is **4.5× over** — guaranteed
   silent truncation, checksum failure, retry loop. (§4.5)
2. **Header straddle — the fatal one.** After the leading `+`, each byte of the `+IPD,<len>:`
   header gets a **single ≈314 µs window with no outer retry**. The window is nextsync's
   `_receive_slow` (`sync/uart.s`): `ld l,#200` over a 44 T-state poll loop = 8800 T, which at
   **28 MHz** (the CPU speed nextsync runs at) is 314.3 µs; `l` is reloaded on every call, so the
   budget is per byte. "No outer retry" is literal: only the search *for* the leading `+` has a
   budget (`TIMEOUT 20000`); every header byte after it is a bare
   `if (receive_slow() != 'X') return 0;`. A 20 ms delivery gap anywhere inside the header
   therefore fails **100 % of the time, regardless of FIFO size**.

   The clock matters: at 3.5 MHz the same loop would be ≈2.5 ms, not 314 µs. Every microsecond
   figure in this document assumes the 28 MHz CPU speed nextsync selects.

Pacing at the live baud satisfies both automatically: there are no gaps, because every byte of the
queue leaves at the same cadence.

Per-scanline granularity would also be safe on both counts; **UART-tick granularity is what is
shipped**, because the TX path already computes exactly that clock and the pacer can simply reuse
it.

### 6.3 Chunk sizing

`+IPD` chunks are capped at `MAX_IPD_CHUNK` = 2048 bytes and are cut **only when the guest-bound
queue has drained**, so a busy peer produces few large chunks rather than many small ones. That is
what keeps nextsync inside its 5-chunks-per-server-packet budget without an explicit floor
(simplification 7). nextsync's server sends packets of 261 and 1029 bytes (`MAX_PAYLOAD = 1024`,
`nextsync.py:26`, plus header), so its default config **deliberately overruns the 512-byte FIFO by
≈2×**, relying on the Z80 draining concurrently — its author says so at `nextsync.py:31-32`. (The
1460-byte figure quoted during the research is the Ethernet TCP MSS, i.e. a *transport* segment
size, not a nextsync application packet; nothing reachable substantiates a 1460-byte application
packet, so the claim is not made here.)

One chunk is framed per quiet moment; the next follows when that one drains.

### 6.4 The guest is the bottleneck either way

nextsync's own receive loop (`_receive`, `sync/uart.s`) is 93 T-states/byte, which at 28 MHz is a
ceiling of **≈301 KB/s**. 2 Mbaud already delivers 200 KB/s, so any faster delivery scheme buys at
most ≈1.5× over the fastest real
configuration. **Measured** in [§11.2](#112-the-rx-pacing-is-settled--baud-pacing-stays): 1.42×,
and paid for with 99.96 % of the guest's CPU.

---

## 7. Architecture

### 7.1 Layering (owner-approved)

```
EspDevice (interface)    set_output(ByteSink) / receive(byte) / poll()
                         / tick(elapsed, per_byte) / wants_tick()
    ├── AtEngine         the passive core: bytes in -> bytes out, no clock,
    │                    no threads, no host types
    └── ThreadedEsp      OPTIONAL wrapper: owns an AtEngine + a worker thread

any host  ──drives──►  either implementation through EspDevice alone
    └── jnext: EspUartAdapter implements UartDevice in terms of EspDevice,
        and paces guest-bound bytes into the emulated RX FIFO at baud
```

Both the core and the wrapper implement the **same** `EspDevice` interface, so a host chooses
threaded or inline by choosing which to construct and changes nothing else. Note what the core is
*not*: `AtEngine` is **not** a `UartDevice` any more — that host type lives entirely on jnext's
side of the line, in the adapter.

**The core is passive and stays drivable inline.** That is what makes the wrapper *optional*
rather than decorative: another project may drive the module from its own scheduler, or inline
with no thread at all. **Both modes are exercised by the module's own suite** (`esp_at_test.cpp`
drives `ThreadedEsp` as well as the bare engine) — an unexercised alternative mode rots.

**The sink's threading contract is part of the interface**, not an implementation detail: the
`ByteSink` is invoked **only from `tick()`**, never from `receive()` or `poll()`. So even under
`ThreadedEsp` the guest-bound byte path never leaves the caller's thread, which is what makes it
safe for jnext's sink to write straight into the emulated RX FIFO while a socket read is in flight
on the worker.

**Why threading is allowed at all.** The ESP's internal latency is not observable and does not
need emulating: no Next software depends on `AT+CIPSTART` taking a particular time to parse, and
real ESP latencies are milliseconds and highly variable, so anything that survived real hardware
tolerates any speed. Processing instantly is strictly *safer*, not merely cheaper. The engine as
built already implements the decoupling (`poll()` wall-clock, `tick()` emulated-time), so the only
remaining question was **where `poll()` is called from** — and the answer is the wrapper thread,
not the frame loop.

**Consequence for branch 4:** `poll()` is **not** wired into `begin_new_frame()`. The frame loop
drives only the baud-paced `tick()`. This removes the one place a slow `getaddrinfo` or socket
read could stall the frame loop.

It does **not**, on its own, remove the stall from the system — see §7.2, which is the part of
this design that was got wrong first time and is now the most important property of the wrapper.

### 7.2 The core lock is never held across a transport poll

Moving socket work onto a worker thread only helps if the worker does not hand its slowness back
to the emulation thread through the lock. The first implementation did exactly that — it held
`core_mutex_` across `EspTransport::poll()` — and two measurements falsify the reasoning that was
used to justify it:

| Measurement | Result |
|---|---|
| Transport blocking 500 ms inside `poll()`; 39 bytes **already** in the engine's outbound queue *before* the stall | **11 827 226 `tick()` calls over ~400 ms delivered 0 of the 39 bytes.** Post-fix, the same repro delivers **39/39** |
| Transport blocking 2000 ms inside `poll()`; wrapper destroyed | **Destructor took 2007 ms** |

The shipped header had explained the first away as "a few bytes' worth of delivery latency while
the ESP is busy — during which, by construction, the ESP has nothing to say". **That claim is
false**, and the measurement is what disproves it: the bytes were already queued, so the ESP had
plenty to say. Worse, the emulation thread is *not* blocked — the entire point of the wrapper — so
real T-states keep elapsing throughout. That is silence on a wire whose receivers have no retry
(§6.2).

The design that follows:

1. **`AtEngine::poll()` splits into two halves.** `advance_transports()` calls
   `EspTransport::poll()` and touches no engine state; `service_transports()` does the engine
   work. `poll()` is exactly the two in sequence, which is what an inline consumer wants. The
   worker runs `advance_transports()` with the core lock **released** and takes the lock only for
   the engine work either side of it.
2. **`tick()` uses `try_lock`, and on failure returns having done nothing** — the elapsed ticks
   are **dropped, not banked**. Banking would accumulate credit during a stall and then release a
   burst at unbounded speed the moment the lock came free, which is precisely the RX FIFO overrun
   the pacing exists to prevent (the engine's own idle branch makes the same choice for the same
   reason). What is lost is now the duration of one engine service pass — microseconds, bounded by
   the transport contract — rather than the duration of a name lookup.
3. **`set_output()` never touches the core lock.** The core holds a permanent trampoline; the user
   sink is swapped behind a separate `sink_mutex_`. An earlier version took the core lock and
   described it as "briefly contends with the worker", which understated it by the same margin.

**The residual, stated plainly:** the destructor **joins** — it does not detach — so `stop()` and
`~ThreadedEsp()` are bounded by exactly **one** `EspTransport::poll()` call. Against a
contract-violating transport that is unbounded, and **no change confined to the module can fix
it**: a thread parked in a syscall cannot be joined in bounded time by anyone, and detaching is
banned outright (§7.7 — `emulator_cold_boot()` placement-news the `Emulator` at the same address,
so a surviving worker would service a core whose sink points into the newly booted machine:
silent corruption, not a fault). The fix therefore has to be in the transport, which is why
`EspTransport::poll()` now carries an explicit contract (§7.4).

### 7.3 Module layout

Landed layout (branch 3.5, merged at `779b7103`), with host-rooted includes replaced by self-rooted
ones so a consumer does not have to replicate jnext's include root:

```
src/esp01/
  CMakeLists.txt     target `esp01`
  include/esp01/     esp_at.h, esp_socket.h, esp_log.h, esp_threaded.h,
                     esp_socket_platform.h  ← module-private by convention, not by path
  src/               esp_at.cpp, esp_socket.cpp, esp_address_policy.cpp, esp_log.cpp,
                     esp_threaded.cpp, esp_socket_posix.cpp, esp_socket_win.cpp
  test/              esp_at_test.cpp, esp_socket_test.cpp
src/peripheral/
  esp_uart_adapter.{h,cpp}   the jnext side: implements UartDevice
test/esp/
  esp_uart_adapter_test.cpp  the jnext side's own suite (§7.6)
```

`esp_socket_platform.h` is **private** in the design sense — its own header says it is included
only by `esp_socket.cpp` and the two platform twins, and nothing outside the module should ever
include it — but it currently sits in the public include directory alongside the rest, so nothing
enforces that. A consumer must treat it as internal. Moving it into `src/` would make the
constraint structural rather than documentary.

Includes read `#include "esp01/esp_at.h"`, inside the module and out. The `esp01` target links
**nothing from jnext** — not `jnext_peripheral`, not `jnext_core`, not spdlog. Its only
dependencies are the C++17 standard library, `Threads::Threads` (what `std::thread` needs on
POSIX), and `ws2_32` on Windows — both OS components, so the project's no-new-dependency rule is
untouched.

**Reuse is a verified property, not a claim.** Copying `src/esp01/` into an empty directory with a
four-line `CMakeLists.txt` (`cmake_minimum_required` / `project` / `set(ENABLE_TESTS ON)` /
`add_subdirectory(esp01)`) and **no jnext present at all** builds clean, and both suites — roughly
150 rows each — pass identically to their in-tree run, with `ldd` on the resulting binaries showing
only `libstdc++`, `libm`, `libgcc_s` and `libc`. Reproduced independently twice: once in review of
the modularisation, and once while writing this document. A reuse claim that is never executed is a
reuse claim that has already stopped being true.

### 7.4 The seams

Five seams, in dependency order. Together they are what make the module liftable.

| Seam | Contract | Why it is a seam |
|---|---|---|
| **`EspTransport`** (abstract) | `begin_connect` / `poll` / `state` / `send` / `recv` / `close` / `last_error` / `peer_address`. **Nothing may block** — see below, this is a hard contract, not a preference. The interface is shaped so a caller *cannot ask it to*: no synchronous connect in the vtable, `poll()` takes no timeout, `send`/`recv` return a **count** never a completion, and nothing returns a native handle. | A consumer can substitute its own transport. The AT engine is driven against an in-memory fake with no sockets, no DNS and no listener. |
| **`EspDevice`** (abstract) | `set_output(ByteSink)` / `receive(byte)` / `poll()` / `tick(elapsed_ticks, ticks_per_byte)` / `wants_tick()`. Implemented by both `AtEngine` and `ThreadedEsp`. The `ByteSink` is a **callback**, and fires **only from `tick()`**. | Lets a host swap threaded for inline without touching anything else, and keeps the module free of any host header — a consumer can point the sink at a `std::vector` and unit-test the whole engine. |
| **`UartDevice`** (host side) | The jnext seam `EspUartAdapter` implements: `receive(byte)`, `poll()`, `tick(master_cycles, byte_ticks)`, plus an `RxSink` for the guest-bound direction. | The adapter is the *whole* of the coupling and lives on jnext's side of the line. Attachment is **non-owning**, matching `SpiDevice` / `I2cDevice`. |
| **Address policy** | Pure functions `classify_address` / `select_candidate` over `IpAddress` + `AddressPolicy`. No sockets, no DNS, no platform headers, no logging. | The security-relevant part must be exhaustively unit-testable offline **and** overridable — the socket suite's own in-process listener is on 127.0.0.1, which the default policy denies. |
| **Logging** (`esp01/esp_log.h`) | `enum class LogLevel {Trace,Debug,Info,Warn,Error}`; `using LogSink = std::function<void(LogLevel, const std::string&)>`; `set_log_sink(LogSink)` (**`nullptr` restores silence**), `set_log_threshold(LogLevel)` (default `Info`), `log_threshold()`, `log_hex_byte(uint8_t)`, and `log_trace/debug/info/warn/error(fmt, args...)` with minimal `{}` substitution (`{{`/`}}` literal, format specs **ignored**). | Removes the module's last hard host dependency (`core/log.h`, 21 call sites). **Unbound means silent** — a consumer that ignores the header pays nothing and hears nothing, which is the only correct default for a library. |

**The logging binding is global, not per-instance**, and the reasoning is worth keeping: the module
models one physical device on one UART; the transport is created by a *free function* and logs from
a `SocketTransport` the engine never sees, while the address policy is pure free functions — so a
per-instance sink would have to be threaded through both (and every future free function) to cover
what a global covers for nothing. The cost is shared mutable state, so the sink is mutex-guarded
and the **threshold is atomic**, which keeps the gate lock-free on the per-tick pacing path. jnext
binds it in `esp_bind_logging()` (`esp_uart_adapter.cpp`) and calls `esp_sync_log_level()` from
`EspUartAdapter::poll()` — once per frame — so a runtime `--log-level` change takes effect within a
frame. **This is not a logging framework and must not become one:** no timestamps, no logger names,
no file/line, no sink chaining. The host's real logger supplies all of that, because it already
does.

#### `EspTransport::poll()` — the non-blocking contract

Promoted from a design preference to an **explicit interface obligation**, because §7.2's second
measurement showed what violating it costs. If you are writing your own transport this is the one
paragraph you cannot skip:

- `ThreadedEsp` runs `poll()` on a worker thread whose destructor **joins**, and joining a thread
  parked in a syscall is unbounded. So wrapper shutdown is bounded by exactly **one** `poll()`
  call, and a transport that blocks there hands that duration to whoever destroys the wrapper. In
  jnext that is a cold boot or a quit: a 20 s resolver timeout would freeze GUI, audio and CPU for
  20 s when the user presses Reset.
- Do the slow part elsewhere and report progress through `state()`. `Resolving` and `Connecting`
  exist precisely so that a lookup or a handshake in flight is an **observable state** rather than
  a stall.
- **Known violation, stated rather than hidden:** the transport this module ships,
  `make_socket_transport`, currently **does** block here, because its name resolution is a
  synchronous `getaddrinfo` (§7.5). An IP literal never resolves at all, so only a hostname target
  is affected; `send`, `recv` and `close` are already non-blocking. Making it asynchronous is its
  own change, **`fix/esp-async-dns`**, which lands **before** branch 4 wires the ESP into the
  emulator.

### 7.5 Platform split

Only the OS primitives are platform-split, and the split is drawn **below** the state machine, not
whole-file: `esp_socket_platform.h` declares `init` / `resolve` / `open_nonblocking` /
`begin_connect` / `poll_connect` / `send` / `recv` / `close`, spells `NativeSocket` as a plain
integer type, and pulls in **no platform headers of its own**. `esp_socket_posix.cpp` and
`esp_socket_win.cpp` are the twins; the state machine, the policy enforcement and the logging live
once, in the portable file.

**DNS is synchronous, deliberately.** `getaddrinfo` has no portable non-blocking form. It is
confined to `poll()`, preceded by an `AI_NUMERICHOST` fast path that never touches the network
(and nextsync is normally configured with a literal), and `Resolving` exists as a distinct
observable state precisely so that moving the lookup onto a thread later changes only that file.
With `poll()` on the wrapper thread (§7.1) the stall no longer reaches the *frame loop*.

It does still reach **wrapper shutdown**, which is why this is now recorded as a **known violation
of the transport contract** (§7.4) rather than as an accepted compromise, and why
`fix/esp-async-dns` lands before branch 4.

### 7.6 Tests ship with the module

A consumer gets the code and its proof together. Constraints on the move:

1. **Sources live with the module; registration stays with jnext.** Both module suites remain
   declared in `test/unit-tests.conf` with their exact pinned row counts and registered via
   `add_test()` from `src/esp01/CMakeLists.txt` — `run-unit-tests.sh` reads *every*
   `CTestTestfile.cmake` under the build tree, so the declared-suite cross-check sees them exactly
   as it sees the rest. The contract — the harness *refuses to run* if the manifest and CMake
   disagree, and a missing suite is a loud failure — is not weakened for the sake of modularity.
   Only the file location changed. A consumer who does not want jnext's manifest simply runs the
   two binaries.
2. **The tracing rows must assert through the logging seam**, not through a spdlog ringbuffer
   sink. That is what makes them runnable by a consumer who binds a different sink, and it is a
   genuine test of the seam rather than of spdlog.
3. **No sockets in the AT-engine suite** — it drives a fake transport. The socket suite
   legitimately opens an in-process loopback listener; that scaffolding is **POSIX-only**
   (`arpa/inet.h`, `sys/wait.h`), so a Windows consumer needs equivalents. Documented rather than
   pretended away.
4. **`check()` stays file-local or module-local.** There is no jnext-wide test header today; do
   not introduce a dependency on one while tidying up — that would trade one coupling for a worse
   one.

Three suites, and the split between them is itself part of the design:

| Suite | Where | Why there |
|---|---|---|
| `esp_at_test` | `src/esp01/test/` | Portable: fake transport, no socket, no DNS. Drives **both** the bare `AtEngine` and `ThreadedEsp` |
| `esp_socket_test` | `src/esp01/test/` | Address policy is pure and portable; the transport half needs a POSIX-only in-process listener |
| `esp_uart_adapter_test` | `test/esp/` | **jnext-side**: the `Uart`-coupled rows and the spdlog-registration rows cannot live in a portable module suite now that `AtEngine` is not a `UartDevice` |

Exact row counts are pinned in `test/unit-tests.conf`; the harness refuses to run if they drift.
They are deliberately not restated here — an unenforced prose copy of an enforced number only ever
goes stale.

**And one row that is none of the three.** `esp-loopback-func`, in the screenshot/functional
regression suite, is the only thing that proves the WHOLE path — a Z80 guest reaching port 0x133B,
through `UartChannel`, `EspUartAdapter`, `AtEngine` and `EspGatedTransport`, out of a **real
socket** to a **real TCP peer**, and every byte back again. The three unit suites each replace one
end of that with a fake, by design; none of them can fail if the real socket path is broken. The
row asserts the guest-visible AT stream as an **exact ordered byte sequence**, which is also what
narrows §11.1. Its peer binds an **RFC1918** address rather than loopback, because the address
policy denies loopback by default (§8.2) and a test is not a reason to relax a security decision —
and RFC1918 is precisely the configuration owner decision 4 exists for.

That third suite is the visible cost of the core/adapter split, and it is the right cost: the rows
that genuinely test jnext coupling now live with jnext, and the module's own suites stay runnable
by a consumer who has never heard of `Uart` or spdlog.

### 7.7 Lifecycle

- **The wrapper joins its thread before `~Emulator()`.** `emulator_cold_boot`
  (`src/platform/emulator_boot.h:67-77`) does `emu.~Emulator(); new (&emu) Emulator();` — placement
  new **at the same address**. A live thread writing into the reconstructed object is **silent
  corruption, not a crash**. Joining first removes the hazard entirely, which is why the join is in
  the **destructor** rather than in a shutdown method someone can forget to call. `stop()` is
  exposed as well, so an owner can order the shutdown explicitly, but it is not required.
- **Construction does not start the thread.** `ThreadedEsp` builds the core; `start()` is separate,
  so an owner can construct, install the sink, and only then let anything run.
- **Shutdown is bounded by one `EspTransport::poll()`, and no better.** See §7.2's residual: this
  is the price of joining, it cannot be fixed inside the module, and it is why the transport
  contract in §7.4 is a contract.
- **The same hazard applies to the `RxSink`.** `Uart uart_` is a value member of `Emulator`, so a
  sink captured before a cold boot still holds a perfectly valid `Uart*` — pointing at the
  **newly booted** machine's UART. Nothing crashes; the device's next injection lands in the fresh
  machine's RX FIFO. The fix is to re-bind from `ColdBootHooks::rewire_host`
  (`emulator_boot.h:110-115`), the seam that exists for exactly this, not an ad-hoc detach.
  Every hard reset takes this path.
- **Attachment is non-owning.** Detach before destroying either side.
- **Determinism is unchanged.** Any real network is non-deterministic already, which is why the
  functional test uses a loopback listener and RZX replay gates the ESP off entirely
  (`replay_mode_`, branch 4).
- **Save-state is unchanged.** The ESP is not serialised (simplification 5).

### 7.8 Hot-path cost

`UartChannel::tick` runs once per Z80 instruction (10⁵–10⁶ times/frame), which is far too hot for
socket work — `poll()` must never hang off it. The emulated-time hook is gated inside
`UartChannel::service_attached_device`: `if (device_ && device_->tick_wanted())`
(`src/peripheral/uart.h:276`), with `tick_wanted()`
**non-virtual** so an idle device costs a predictable branch rather than a vtable dispatch.
`byte_transfer_ticks()` is computed only inside the guarded branch. With no device attached the
cost is one null test on a pointer already in the cache line being touched.

`UartDevice::receive` is called from `deliver_tx_byte` at **byte boundaries**, not per
instruction — at 115200 8N1 that is ≈230 calls/frame, so a virtual call there is free.

---

## 8. Security posture

An untrusted NEX with an unrestricted `AT+CIPSTART` gets a full outbound byte pipe: localhost
daemons, LAN scanning from inside the perimeter, cloud metadata endpoints, and exfiltration of
anything the guest can read — which includes the **read-write** SD image.

Three facts make this smaller than it looks: **server mode has no consumer** (so there is no
listening capability to gate — see §1.4); **every consumer connects to a named host from config**
(NXtel reads `URL1..URL7` from `nxtel.cfg`), so a hostname allowlist fits naturally; and **`ERROR`
is an already-exercised refusal path**, so a blocked connection has a clean failure mode and no
hang.

### 8.1 The posture (owner decision, 2026-07-28)

1. **Default off.** The ESP is not reachable unless explicitly enabled.
2. **An explicit enable flag.**
3. **A repeatable hostname allowlist.**
4. **RFC1918 is ALLOWED.** The emulated ESP must be able to reach machines on the user's own LAN.
   Private ranges are **not** part of the deny set.
5. **Built-in deny** of loopback (`127.0.0.0/8`, `::1`), link-local (`169.254.0.0/16`, `fe80::/10`)
   and cloud-metadata addresses.
6. **No server/listen mode in v1.**
7. **A visible log line on every connection made or refused** — never silent.

Precedent: `--sdcard-readonly` (`src/core/cli_options.h:168`) is the existing "restrict what the
guest may do to a host resource" flag. Headless deliberately never reads the GUI config, so a
config-only opt-in leaves headless off by default — which is correct.

### 8.2 What the address policy actually enforces

`AddressPolicy` is a struct of independent flags so a caller can relax exactly one:

| Flag | Default | Covers |
|---|---|---|
| `deny_loopback` | **on** | `127.0.0.0/8`, `::1` |
| `deny_link_local` | **on** | `169.254.0.0/16`, `fe80::/10` |
| `deny_cloud_metadata` | **on** | `169.254.169.254` (AWS/GCP/Azure/Oracle/DO/OpenStack), `100.100.100.200` (Alibaba), `fd00:ec2::254` |
| `deny_unspecified` | **on** | `0.0.0.0/8`, `::` |
| `deny_multicast_reserved` | **on** | `224.0.0.0/4`, `240.0.0.0/4` (incl. `255.255.255.255`), `ff00::/8` |
| `deny_private` | **off** | RFC1918 / CGNAT / ULA — reachable, per decision 4 |

Three properties are load-bearing:

- **Rule order matters, for two real reasons.** Cloud metadata sits *inside* link-local (v4) and
  *inside* ULA (v6), so it must be tested first or it would be reported as the vaguer reason; and
  a rule whose flag is off must **fall through** rather than return "allowed", so disabling
  `deny_cloud_metadata` still leaves `169.254.169.254` denied by `deny_link_local`.
- **IPv4-in-IPv6 aliases are normalised first.** Without `normalize()`, `::ffff:127.0.0.1` is a
  one-line bypass of the loopback deny. IPv4-mapped (`::ffff:0:0/96`), IPv4-compatible (`::/96`)
  and NAT64 (`64:ff9b::/96`) all collapse to their V4 form — they are **aliases**, so collapsing
  is lossless.
- **Tunnel endpoints are re-judged, not aliased.** 6to4 (`2002::/16`) is deliberately kept out of
  `normalize()`: `2002:7f00:1::1` and `127.0.0.1` are *different hosts, one reached via the other*.
  `classify_address` therefore re-runs the same rules against the tunnel endpoint, so a 6to4
  wrapper cannot launder a denied destination — while `select_candidate` still correctly treats it
  as an IPv6 candidate. Teredo and ISATAP are excluded on stated grounds (neither can name a host
  on this machine; ISATAP is an interface-identifier pattern that would deny legitimate addresses
  by coincidence), and the exclusions are pinned by tests so they cannot be "fixed" silently.

`select_candidate` prefers the first allowed **IPv4** candidate and falls back to IPv6. That is
faithful as well as convenient: the real ESP-01's AT firmware has an IPv4-only stack and every
evidenced consumer is IPv4. It also sidesteps the "getaddrinfo returned AAAA first, host has no
IPv6 route" trap without untestable try-the-next-candidate machinery.

**Known limitation, stated where someone would turn it off:** clearing `deny_link_local` does
**not** make link-local peers reachable. `IpAddress` carries no scope id, and both platform twins
drop `sin6_scope_id`, so an `fe80::/10` connect fails with `EINVAL` for want of a zone. Nothing in
production clears the flag; the gap is latent, not live. Making it honest means carrying a scope
id end-to-end **and** finding a hermetic way to test it, which needs a link-local peer on a real
interface. Untested plumbing for an unreachable capability is worth less than this paragraph.

**Status: all seven decisions are implemented** as of branch 4, and the two gaps this paragraph
used to list are closed.

The address policy is pure and unit-tested. `--esp` / `--no-esp` carry decision 1 and 2, and
`--esp-allow` carries decision 3 (`EspHostPolicy`, matched case- and whitespace-insensitively;
empty means "any host", never "no host"). The engine logs connection open and close at `info` and
connect failure at `warn`, emitted **by default** — the module's own threshold defaults to `Info`
(`esp_log.h`), spdlog's global default level is `info` (`registry.h:116`), and jnext never lowers
either. Deliberately, those two events are the *only* `Info` lines the module produces; everything
chatty is `Debug` or `Trace`.

1. The **allowlist-decision** line exists: `EspGatedTransport` pushes a `Refused` event and logs
   it, and `setup_esp` names the posture in force at startup — either the allowed hosts or
   "ANY host allowed (no --esp-allow given)". `esp-cli-func` asserts the flag semantics against the
   real binary, including that `--esp-allow` without `--esp` is a **refusal** rather than a silent
   no-op.
2. The GUI is no longer blind: `EspConnectionLog` is what the **status cell**
   (`MainWindow::update_esp_status`) renders, so an opened or refused connection is visible to the
   user who most needs it and not only on stderr.

### 8.3 No host network information may leak into the guest

The advertised SSID is the fixed, obviously-synthetic **`JNextWifiHost`** — never the host
machine's real SSIDs. The same principle applies to every BSSID, MAC, channel, RSSI and IP the
module reports (§5.6): **synthetic and stable, never harvested from the host**. The emulated
module is not a radio.

### 8.4 Tracing is a first-class requirement

Every detail of an app's ESP interaction must be traceable: each AT command received, each response
emitted, the `>` prompt handshake, payload byte counts, `+IPD` framing, connection open/close,
socket errors, allowlist decisions, and the RX pacing / FIFO state.

The module emits through its **own seam** (§7.4) and knows nothing about spdlog. jnext binds that
seam to a dedicated **`esp01`** spdlog logger (`src/core/log.h:92`), selectable at runtime via
`--log-level <subsystem>=<level>` like every other subsystem, and re-reads the level once per frame
so a change takes effect immediately. Two gates therefore apply in series — the module's own
threshold (default `Info`, atomic, so the check is lock-free on the per-tick path) and spdlog's —
and the module's exists purely so it does not pay to build a string the host will discard.

**Nothing is on by default except TCP connection open/close at `info`** — those are user-visible,
security-relevant events and pair with the "never silent" requirement above (§8.2 records where
that requirement is still unmet).

**CLOSED (branch 4).** `esp01` is now named in the `--log-level` subsystem list in
`doc/man/jnext.1.md`, with a paragraph saying what each level shows. It is also **gated**: the
subsystem set is DATA (`Log::SUBSYSTEMS`, which `Log::init()` iterates), and `log_test`'s
`LOG-09..11` diff it against that list in **both** directions — implemented-but-undocumented and
documented-but-unimplemented are both hard failures, exactly as `cli-check` does for the flag set.
That gate exists because `docs-check` could never have caught this: it proves `jnext.1` and
`USAGE.md` were regenerated from `jnext.1.md`, which stays true of an incomplete list.

---

## 9. Rejected alternatives

**Adopting cuzebox-esp8266 wholesale.** Rejected on code, not licence: `+IPD` does not exist,
`CIPSEND=<len>` is dead code, zero tests, two guest-reachable buffer overflows. See §1.2.

**Adopting sQLux's ESP model wholesale.** Rejected on audit cost and fit: 3007 lines of week-old
single-author POSIX-only C, and it still would not supply the parts that are hard *here* (§1.3).
Retained as the socket-layer reference; its licence permits lifting specific code if a piece proves
worth it.

**Per-frame RX batching.** Rejected on arithmetic: 4.5× FIFO overflow *and* a 20 ms gap inside the
`+IPD,<len>:` header, which fails 100 % regardless of FIFO size (§6.2). Note this rejects
*batching*; it does **not** by itself establish that baud pacing is the only alternative — that is
the separate question §11.2 settles.

**On-demand RX refill** (top the FIFO up whenever it has room). Rejected on **measurement**, not on
arithmetic, and deliberately raced rather than dismissed. It avoids both of batching's failure
modes and would have deleted the `tick()` hook outright — and it starves the guest: the main
program's share of a bulk-transfer loop falls from 49.74 % to **0.04 %** at nextsync's 1.152 Mbaud,
and to a measured **0.00 %** at 115200/3.5 MHz, while buying only **1.42×** throughput at the
fastest configuration a real client uses. `rx_near_full` also goes from never asserted to asserted
on 99.8 % of samples. Full rig, controls and numbers in §11.2.

**Coupling the socket directly to `inject_rx()`.** Passes a unit test and destroys data in headless
runs: `inject_rx` has no baud pacing at all (`uart.cpp` paces TX only) and the RX FIFO is 512 bytes
with drop-newest. Pacing is the device's responsibility, and this is exactly why `UartDevice`'s
`send_to_guest` documentation says so.

**Hanging a device reset off `UartChannel::reset`.** Rejected as unfaithful: that path resets only
the Next-side UART state machine (§4.3). The real reset line is NR 0x02 bit 7 and needs its own
hook.

**Accepting `AT+CIPMUX=1` and ignoring it.** Still rejected, and GH #210 did **not** overturn it:
the command is now accepted and *honoured* — a session that asks for multiplexing gets
`+IPD,<id>,<len>:` and a session that never asked keeps `+IPD,<len>:` — which is the opposite of
ignoring it. Accepting-and-ignoring would still promise a wire format that breaks the one client
that cannot ask for it back (§1.4, [§13.3](#133-the-constraint-that-makes-this-real-work)).

**Holding the core lock across the transport poll.** Shipped first, then falsified by measurement:
11 827 226 `tick()` calls delivered **0 of 39 already-queued bytes** across a 500 ms transport
stall, against 39/39 after the split into `advance_transports()` / `service_transports()` (§7.2).
The header's justification — "by construction, the ESP has nothing to say" — was simply untrue of
bytes that were queued before the stall began. Recorded because the *reasoning* failed in a way
that looked sound: a claim about the ESP's *future* output was used to dismiss a stall affecting
its *past* output.

**Detaching the worker thread instead of joining it.** Would bound the destructor, and is banned:
`emulator_cold_boot()` placement-news the `Emulator` at the same address, so a surviving worker
services a core whose sink points into the newly booted machine — silent corruption, not a fault
(§7.7). The bound has to come from the transport honouring its non-blocking contract (§7.4), which
is what `fix/esp-async-dns` delivers.

**Banking the elapsed ticks when `tick()` loses the try-lock.** Rejected for the same reason the
engine's idle branch does not bank: accumulated credit released at unbounded speed the moment the
lock frees is exactly the RX FIFO overrun the pacing exists to prevent (§6.2). Dropping loses one
engine service pass — microseconds — instead.

**"The UART wire is synchronous, therefore the ESP cannot be threaded."** This argument was made
during design and is **wrong**, and the correction matters because the conclusion it was used to
support (no threading) was adopted for a while. The wire *is* synchronous — a baud rate is a shared
clock — but that constrains only the **drain stage**. It says nothing about where ESP *processing*
lives. A true statement about one stage was used to justify a conclusion about the whole component.
The passive-core + optional-threaded-wrapper shape (§7.1) is what replaced it.

**A resolver thread for DNS, in branch 2.** Deferred rather than rejected: it would have been the
first thread jnext runs while the frame loop is live, and the cold-boot placement-new hazard (§7.7)
is exactly the situation in which a still-running thread holding a result slot corrupts a
reconstructed object silently. With the threaded wrapper approved and `poll()` moved off the
emulation thread, the question largely dissolves.

---

## 10. v1.1 extension points

Full datasheet-level ESP-01 / ESP8266 emulation is tracked as
[#154](https://github.com/jorgegv/jnext/issues/154) — *"full ESP-01 / ESP8266 AT emulation to
datasheet spec (beyond the evidenced subset)"*. It is a deliberately different approach: v1.0 is
**evidence-driven** (only what real software uses), v1.1 is **spec-driven**.

What v1.0 already leaves open, at zero cost today (§3):

| Extension | What is already in place | What v1.1 adds |
|---|---|---|
| ~~`AT+CIPMUX=1`, 5 connections~~ | The `conn_` table, per-slot state, per-slot loops, connection ids throughout the state machine | **DONE (GH #210)**, and §3's prediction held — it was filling in blanks. Two corrections to this row's own wording: the usable ceiling is **4 inbound** connections, because slot 0 stays the guest's outbound transport and accepted ids run 1..4 ([§13.7](#137-what-implementation-decided-that-13-did-not)); and `multiplexed` is **captured per connection when it opens**, not passed globally, which is the part that keeps a `CIPMUX=0` session's framing untouched |
| ~~Multiplexed `+IPD,<id>,<len>:`~~ | `queue_ipd_header(cid, multiplexed, len)` — the one place the wire format is decided | **DONE (GH #210)** — exactly as predicted, per connection. `MUX-10`..`MUX-13` pin the two wire forms against each other so neither can drift into the other's session |
| ≈40 further AT commands | `kCommands` table + uniform handler signature | Add rows |
| ~~UDP~~ / TLS / passthrough | `EspTransport` is an interface | **UDP is done (GH #198)**, and the prediction in this row was WRONG in an instructive way: it did not arrive as a new implementation behind an unchanged seam, because the engine is handed ONE transport per slot at construction and `AT+CIPSTART="UDP"` chooses at run time. It arrived as a `Protocol` argument on `begin_connect` instead. TLS and passthrough would go the same way. |
| ~~Server / listen~~ | — | **DONE (GH #210)**, [§13](#13-server-mode-gh-210). The security review this row demanded is [§13.4](#134-security-review--the-inbound-surface), and it did re-open the inbound surface deliberately: bind is **127.0.0.1** by default, `--esp-listen-address` is the explicit widening, and a bind failure answers `ERROR` rather than falling back to a wider address. `AT+CIPCLOSE=<id>` followed in GH #211 ([§14](#14-per-connection-close-gh-211)) |
| NR 0x02 bit 7 hardware reset | The bit is already latched (`emulator.cpp:2719`) | A device-facing hook driven from the NR 0x02 **write** — distinct from `UartChannel::reset` (§4.2/§4.3) |
| Echo on by default | `ATE0`/`ATE1` really toggle it | One line, if datasheet fidelity is wanted (simplification 2) |
| `AT+CIPSENDEX` `\0` early terminate | Currently an alias | Real early-terminate semantics (simplification 3) |
| `FAIL` / `busy p...` / `ALREADY CONNECTED` | On the never-emit list | Only with a driver-compatibility story — `ESPATreadme.TXT:92` is the constraint |

---

## 11. Open questions — to be tested, not assumed

One of the two is now **settled by measurement** ([§11.2](#112-the-rx-pacing-is-settled--baud-pacing-stays)).
The other ([§11.1](#111-nextsyncs-flush_uart_hard-needs-193-ms-of-continuous-silence)) is
**narrowed but still open**, and is labelled untested rather than quietly dropped.

### 11.1 nextsync's `flush_uart_hard()` needs 19.3 ms of continuous silence

`flush_uart_hard()` requires **19.3 ms of continuous silence** on the wire before it proceeds
(`_flush_uart_hard`, `sync/uart.s`: `ld hl,#10000`, reloaded on every byte received — hence
*continuous*, not cumulative; at 28 MHz, §6.2). An
instantly-responding emulated ESP is **faster than real hardware**.

The reasoning that it *should* be safe: the module only speaks when spoken to, and nextsync
controls its own sends. But "we made it faster than hardware and something timing-sensitive broke"
is exactly the class of bug that appears only against real software.

**Still UNTESTED against nextsync, and it is going to stay that way for now.** nextsync is not on
the official SD card and its source is not on the development machine, so branch 5 could not run
it; saying so is better than substituting a proxy and calling the question closed.

**What branch 5 *did* narrow.** The `esp-loopback-func` regression row asserts the guest-visible
byte stream of a whole session — init, connect, send, receive — as an **exact ordered sequence**,
and the observed stream contains precisely the replies to the guest's own commands and nothing
else. So the "module only speaks when spoken to" half of the reasoning is no longer an assumption:
it is a pinned assertion, and any future unsolicited chatter (a banner, a stray `ready`, an early
`CLOSED`) breaks that row. What remains genuinely open is the other half — a `+IPD` or a `CLOSED`
arriving from the **peer** inside a flush window. That is a property of the peer's timing rather
than of this engine, which is a materially smaller question than the one originally recorded.

### 11.2 The RX pacing is SETTLED — baud pacing stays

**Answered by measurement, branch 5.** The instruction was *"do not defend the shipped pacing
merely because it is already built"*, so it was not defended — it was raced against the
alternative, and it won by three orders of magnitude on the metric that matters.

There were **three** options, not two. The §6.2 argument rejects per-frame *batching*; it never
established that baud pacing was the only alternative:

| Option | Verdict |
|---|---|
| Per-frame batch | **Fails** — 4.5× FIFO overflow *and* 20 ms gaps straddle the `+IPD,<len>:` header |
| **Pace at configured baud** (shipped) | **KEPT.** Costs the `tick(master_cycles, byte_ticks)` hook and its accumulator |
| On-demand refill — top the FIFO up whenever it has space | **REJECTED on measurement**: it starves the guest, exactly as the hazard below predicted |

#### The experiment

nextsync — the pacing-critical client — is **not on the official SD card and its source is not on
the development machine**, so it could not be the subject. Stating that plainly matters, because
what was measured instead is a *model* of it and the difference should be visible to a reader.

The subject is a purpose-built Z80 guest that connects to a loopback-network TCP peer, asks for an
endless stream, and then spends the run in a loop whose every iteration does **exactly one** of two
things: consume a waiting RX byte (the "ISR"), or perform one unit of **main-program work**. Both
arms are counted, and the guest reports both counters plus a `rx_near_full` sample count out of the
magic port every 256 bytes. Two runs per configuration, identical frame budget (1000 measured
frames after a 100-frame inject delay), identical guest, identical peer; the only difference is an
env-gated pacing switch, so both arms come from one binary.

**The machine is `--machine 48k`**, and every frame-cycle figure below is that machine's:
224 T-states × 312 lines × 8 = **559 104** master cycles per frame. Say it explicitly, because
`ZXN_ISSUE2` / 128K / +3 are 1824 × 311 = **567 264** (`emulator_config.h:283-286`) and a reader
reconciling the arithmetic against the wrong constant will not get these numbers back. 48K is a
legitimate rig rather than a convenience: the pacer is **machine-independent** — it reads
`UartChannel::byte_transfer_ticks()` (prescaler × frame bits) and jnext's 28 MHz master cycle
count, neither of which is a function of the machine profile — while 48K gives a quiet machine that
is safely injectable 100 frames in, where a mid-boot Next would land the guest on whatever
`tbblue.fw` happened to have mapped.

Two things were done to make the comparison *favour* on-demand rather than the incumbent:

- On-demand was implemented in its **best** form — refill gated on FIFO space, so it never drops a
  byte. The naive form additionally overflows, and was not the one measured.
- The guest polls rather than taking interrupts. That is not a weakening: the consume branch runs
  precisely when a byte is available, which is precisely when an ISR would be entered, so the
  arithmetic is the same one — without making the result depend on an IM2 vector table being set up
  correctly. A real interrupt-driven driver (`ESPAT.DRV` is one) pays ISR entry/exit on top, so its
  figures would be **worse** than these, not better.

#### The numbers

| Link rate / CPU | Pacing | bytes/frame | main-program share of the loop | `rx_near_full` asserted |
|---|---|---|---|---|
| 115 200, 3.5 MHz | **shipped** | 210.7 | **69.18 %** | 0.00 % |
| 115 200, 3.5 MHz | on-demand | 463.1 | **0.00 %** | 99.96 % |
| 115 200, 28 MHz | **shipped** | 212.5 | **95.60 %** | 0.00 % |
| 115 200, 28 MHz | on-demand | 3 040.5 | **0.02 %** | 99.85 % |
| 1.17 Mbaud, 28 MHz | **shipped** | 2 130.7 | **49.74 %** | 0.00 % |
| 1.17 Mbaud, 28 MHz | on-demand | 3 034.4 | **0.04 %** | 99.79 % |

Four independent checks say the rig measured what it claims to:

- **The pacer really follows the live prescaler.** Shipped delivers 210.7 bytes/frame at 115200
  against a theoretical 559 104 ÷ 2430 = 230.1 (92 %), and 2 130.7 at 1.17 Mbaud against
  559 104 ÷ 240 = 2 329.6 (91 %). The *same* ratio at a ~10× different rate is the point — a
  constant would not track. The residual ~9 % is `+IPD` chunk-boundary quiet time (§6.3).
- **The CPU-speed knob took effect.** Work units are 8.7× higher at 28 MHz than at 3.5 MHz on the
  shipped arm, i.e. the NR 0x07 = 3 write landed.
- **On-demand is guest-limited, not network-limited.** At 28 MHz it settles at 184 T-states per
  byte, which is this guest's own consume-and-count path. Before the `rx_near_full` counter was
  added, the same arm measured 115 T/byte — and the bare poll-read-count path is 93 T, which is
  also, by coincidence worth noting, exactly the figure §6.4 quotes for nextsync's own receive
  loop. The peer pushed over 300 MB per run (~50 MB/s) and was never the constraint.
- **Both arms saw the same peer and the same frame budget**, so "bytes/frame" is a like-for-like
  throughput comparison in emulated time.

#### The verdict

**Keep the shipped pacing.** The trade is not close:

- **What on-demand buys** at the fastest configuration a real client actually uses — nextsync's
  1.152 Mbaud — is **1.42×**. That is §6.4's predicted "at most ≈1.5×", now measured instead of
  reasoned. (The 14× at 115200 is not a real-world gain: it is the emulator ignoring a baud rate
  the guest asked for.)
- **What it costs** is the guest. The main program's share of the loop falls from 49.74 % to
  0.04 % — a **1243× reduction** — and at 115200/3.5 MHz it reaches a measured **0.00 %**: the main
  program did not advance once during the transfer. That is precisely the predicted failure,
  *"the transfer works but the program appears frozen"*, and it is the nastier kind because nothing
  reports an error.
- **The secondary prediction held exactly too.** `rx_near_full` (¾ = 384 bytes) goes from **never**
  asserted under pacing to asserted on **99.8 %** of samples under on-demand — the FIFO sits full,
  permanently changing the interrupt pattern `ESPAT.DRV` sees.

So the `tick(master_cycles, byte_ticks)` hook and its accumulator stay, `AT+UART_CUR` stays a live
pacing input rather than an acknowledged no-op, and the "delete the hot-path hook" saving is not
available at any acceptable price. The experimental switch was removed with the measurement; it was
never a feature.

**What would reopen this:** a measurement on a *real* interrupt-driven client showing the shipped
pacing itself starving the main program. The 1.17 Mbaud row is the one to watch — 49.74 % free is
comfortable, but it is half of what 115200 leaves, and 2 Mbaud (`syncfast`) is a further step down
that the rig above can measure the day nextsync is available.

---

## 12. Evidence index

### Hardware (VHDL and `nextreg.txt`, `cores/zxnext/`)

| Claim | Citation |
|---|---|
| ESP is on UART 0 | `zxnext.vhd:1611`, `zxnext.vhd:3381` |
| ESP + expansion-bus reset line, latched | `zxnext.vhd:5119` (`nr_02_bus_reset <= nr_wr_dat(7)`) |
| …driven to the pin | `zxnext.vhd:1579` (`o_RESET_PERIPHERAL <= nr_02_bus_reset`) |
| …documented | `nextreg.txt:39` (read), `nextreg.txt:48` (write) |
| NR 0xA8/0xA9 = GPIO0 only; GPIO2 read-only | `nextreg.txt:942-950` |
| UART reset is Next-side only | `uart_tx.vhd:166`, `uart_rx.vhd:219` |
| No hardware flow control (CTS tied asserted) | `zxnext_top_issue2.vhd:2387` |
| RX FIFO is 512 entries | `serial/uart.vhd:148-149` (9-bit addresses) |
| RX FIFO drops on full | `serial/uart.vhd:798` |
| RX overflow status latch | `serial/uart.vhd:540` |
| RX interrupt per byte / near-full | `zxnext.vhd:1941-1944` |

### Guest software

| Claim | Citation |
|---|---|
| Unexpected `CLOSED`/`WIFI DISCONNECT` breaks the driver | `ESPATreadme.TXT:92` |
| `.UART` waits for `OK`,13,10,`>` | NextZXOS dot-command source (SD card) |
| `.ESPBAUD` exact-compares `"OK\r\n"` | NextZXOS dot-command source (SD card) |
| NXtel diagnostics anchors | `c31.asm:147-196`, `c31.asm:192-201` |
| NXtel init + per-keystroke `AT+CIPSEND=1` | `esp.asm:39-40`, `esp.asm:70-74` |
| `MatchSendFail` misload — why `SEND OK` works | `esp.asm:418-427` |
| Inline CRLF-terminated blocks | `esp.asm:46-48`, `esp.asm:558-575`, `c31.asm:368-371` |
| nextsync resets via NR 0x02 bit 7 | `nextsync.c:396-399` |
| nextsync `+IPD` accumulate-then-validate parse | `sync/nextsync.c` (`datalen += r - '0';` precedes the digit check) |
| nextsync per-header-byte window (314 µs @ 28 MHz) | `sync/uart.s` `_receive_slow` (`ld l,#200`, 44 T loop, reloaded per call) |
| nextsync silence requirement (19.3 ms, continuous) | `sync/uart.s` `_flush_uart_hard` (`ld hl,#10000`, reloaded per byte) |
| nextsync receive loop 93 T-states/byte | `sync/uart.s` `_receive` |
| nextsync packet sizes / FIFO overrun by design | `nextsync.py:26` (`MAX_PAYLOAD = 1024`), `:31-32` (author's own comment) |
| CSpect is not 8-bit clean | `docs/dotcommands/http.md:76` |

**Provenance note.** The `ESPATreadme.TXT`, `esp.asm`, `c31.asm` and dot-command citations were
read during the research recorded on GH #25 and are **not independently re-verified here** — no
NXtel or NextZXOS dot-command source exists on the development machine. They are not known to be
wrong; they are unchecked. Simplification 6 and all of §5.2/§5.3 rest on them, so a reader with
access to those sources should re-check them before relying on the exact line numbers. The
`sync/uart.s` and `sync/nextsync.c` citations above **were** verified against source during the
review of this document.

### jnext code

The module (portable — no jnext dependency):

| Subject | Location |
|---|---|
| `EspDevice` interface + AT engine (rationale in the header) | `src/esp01/include/esp01/esp_at.h`, `src/esp01/src/esp_at.cpp` |
| Transport interface, non-blocking `poll()` contract, DNS note | `src/esp01/include/esp01/esp_socket.h` |
| Optional threaded wrapper + its lifetime contract | `src/esp01/include/esp01/esp_threaded.h`, `src/esp01/src/esp_threaded.cpp` |
| Logging seam | `src/esp01/include/esp01/esp_log.h`, `src/esp01/src/esp_log.cpp` |
| Address policy (pure, portable) | `src/esp01/src/esp_address_policy.cpp` |
| Platform primitives (module-private) | `src/esp01/include/esp01/esp_socket_platform.h` + `src/esp01/src/esp_socket_{posix,win}.cpp` |
| Build target, zero jnext links | `src/esp01/CMakeLists.txt` |
| Module suites | `src/esp01/test/esp_at_test.cpp`, `esp_socket_test.cpp` |

The jnext side:

| Subject | Location |
|---|---|
| Adapter: `UartDevice` ← `EspDevice`, logging binding | `src/peripheral/esp_uart_adapter.{h,cpp}` |
| Adapter suite | `test/esp/esp_uart_adapter_test.cpp` |
| UART device seam + lifetime hazards | `src/peripheral/uart_device.h` |
| Gated `tick()` call site (`service_attached_device`) | `src/peripheral/uart.h:276` |
| `byte_transfer_ticks()` = prescaler × frame_bits | `src/peripheral/uart.cpp:83-87` |
| NR 0x02 bit 7 latched, unconsumed | `src/core/emulator.cpp:2719` |
| `esp01` spdlog logger | `src/core/log.h:92` |
| Cold-boot placement-new hazard | `src/platform/emulator_boot.h:67-77`, `:110-115` |
| Suites + pinned row counts | `test/unit-tests.conf` |

---

## 13. Server mode (GH #210)

v1.0 did not build `AT+CIPSERVER`, and §1.4 gave two reasons: the command appeared
exactly once in all the software surveyed and only to turn it *off*, and not
building it kept the inbound attack surface closed. §10's extension table
therefore records server mode as needing **its own security review** before it
can exist. This section is that review, plus the design.

The first reason has expired: there is now a consumer. The second has not
expired at all, and §13.4 is what answers it.

### 13.1 The consumer, and why server mode is forced rather than chosen

[**dezogif_ng**](https://github.com/jorgegv/dezogif_ng) is a Z80 debug stub that
ships as the Multiface ROM on real Next hardware and speaks **DZRP** to
**DeZog** in VS Code. It uses jnext as its development bench and its entire CI.

DeZog always **dials out** and never listens — every existing DZRP remote
listens instead (the CSpect plugin on port 11000, ZEsarUX on 10000). So for an
unmodified, released DeZog to attach to a Next, something on the Next must be
**listening**. This is a property of the client, not a preference of the design:
a Next in TCP client mode has nothing for DeZog to connect to, and closing the
gap on the PC side means either a relay splicing two inbound connections or a
patched DeZog — both of which defeat the point, which is that a released DeZog
attaches to a Next with no extra moving parts.

### 13.2 Why it is three commands and not one

Espressif's AT set chains the prerequisites: `AT+CIPSERVER` requires
`AT+CIPMUX=1`, and `AT+CIPMUX=1` requires `AT+CIPMODE=0`. So the ask is a triad:

1. `AT+CIPMUX=1` — accepted, where v1.0 **refused** it.
2. `AT+CIPSERVER=1,<port>` / `AT+CIPSERVER=0` — listen / stop.
3. The multiplexed `+IPD,<id>,<len>:` inbound form and `AT+CIPSEND=<id>,<len>`
   outbound, which follow from (1).

`AT+CIPMODE` passthrough is **not** part of this and is not needed: server mode
forbids it, so the chosen design could not use it even if it existed. UDP, SSL,
`_CUR`/`_DEF`, SNTP, ping, GPIO and sleep have no consumer here and stay out.

### 13.3 THE CONSTRAINT THAT MAKES THIS REAL WORK

v1.0 refused `AT+CIPMUX=1` rather than accepting-and-ignoring it, and the
reasoning must survive this change intact (`esp_at.h`, `queue_ipd_header`):

> nextsync never sends `AT+CIPMUX` at all — it relies on the power-on default
> being 0 — and its `+IPD` reader **silently corrupts** the multiplexed
> `+IPD,<id>,<len>:` form rather than rejecting it. Its FSM accumulates before
> it validates, so the separating `,` is folded into the length as a bogus digit
> worth `',' - '0'` = -4 and the parse *continues* with a corrupted `<len>`,
> desynchronising the stream rather than failing.

Since no command can correct a wrong default at run time, the default has to be
right and the wrong value has to be rejected loudly. Therefore:

- **The power-on default stays `AT+CIPMUX=0`.** Accepting the command is not
  the same as changing the default, and the default is the part nextsync
  depends on.
- **`CIPMUX=1` happens only on explicit command**, and the multiplexed `+IPD`
  form is emitted only for connections owned by a `CIPMUX=1` session.
- **nextsync keeps working unchanged**, and that is pinned by a test asserting
  a nextsync-shaped session still sees `+IPD,<len>:` while a `CIPMUX=1` session
  sees `+IPD,<id>,<len>:` — the one thing that could regress silently.

Real firmware also refuses `AT+CIPMUX` while a connection is open; that
restriction is kept, because switching framing mid-connection would hand an
existing peer a wire format it did not negotiate.

#### The guarantee has a precondition, and it is stated rather than implied

**"nextsync keeps working" holds from a fresh power-on, or after an `AT+RST`.**
It is not a property of every moment, and the difference is worth writing down
because nothing in the code can express it.

`cipmux_` is **module** state, not per-program state. Nothing resets it when the
guest loads a different program: a soft reset deliberately preserves the whole
ESP, live connection included, because the module sits on the far end of a cable
and does not see the Next's reset line ([§4.3](#43-what-is-not-a-reset)). So a
program that sets `AT+CIPMUX=1` and exits leaves it set, and a nextsync started
in the **same power cycle** with no intervening `AT+RST` has its own outbound
`AT+CIPSTART` captured `multiplexed = true` — and receives exactly the framing
its reader silently corrupts.

Three things bound it, and none of them is a fix:

- **It is firmware-faithful.** A real ESP-01 is sticky in the same way for the
  same reason. Auto-resetting `cipmux_` between programs would be a jnext-only
  divergence sold as a safety net, and §1's whole posture is that a divergence
  which happens to help is still a divergence.
- **The sequencing is unusual.** It needs two ESP programs in one power cycle,
  the first multiplexed, the second not, with no reset between them. A hard
  reset (jnext power-cycles the emulated ESP — see the member-order note in
  `emulator.h`) and `AT+RST` both clear it.
- **NR 0x02 bit 7 does NOT currently help**, and saying otherwise would be
  worse than saying nothing. nextsync's "Resetting esp, try again" recovery
  path drives that hardware reset line (`nextsync.c:396-399`), but jnext only
  LATCHES the bit for readback and save-state — nothing consumes it, as
  [§4.2](#42-a-real-esp-reset-line-does-exist--nextreg-0x02-bit-7) states
  plainly. So in jnext as built, a client writing it does NOT clear a sticky
  `cipmux_`. Wiring that bit to a device-facing ESP reset is already recorded
  in §4.2 as a real future requirement; when it lands it will close this
  window too, and this bullet should be revisited then. **The trigger is one
  grep, so this paragraph can be checked rather than trusted:**
  `grep -rn RESET_ESPBUS src/` returns two lines today, both of them comments
  in `emulator.cpp` saying the bit is a no-op ("*peripheral-bus ESP reset
  signal, no-op here*" and "*bit 7 alone (RESET_ESPBUS) is intentionally
  ignored — no ESP*"). The day that grep returns **executable** code, this
  bullet is wrong and §4.2's requirement has landed.

Recorded as a **disclosure**, therefore, not as a defect: the behaviour is
correct, and what was missing was the sentence saying when the guarantee
applies.

### 13.4 SECURITY REVIEW — the inbound surface

This is the part §10 demanded, and it is a different question from §8's.

§8's address policy governs **outbound**: which addresses the guest may dial.
Its whole shape — deny loopback and link-local, allow RFC1918 by owner decision
— is about protecting *the host and its infrastructure from the guest*. A
listening socket inverts the direction: it exposes *the guest to the network*,
and `classify_address` has nothing to say about that, because the peer is not
chosen by the guest at all.

Two facts set the risk. The socket is opened on the **host**, not inside any
sandbox; and whatever connects to it is speaking to guest software with no
authentication of any kind — DZRP has none, and a debug stub is by definition a
remote-control protocol.

**Decision (owner, 2026-08-04): bind to loopback by default; widening is an
explicit act.**

- Default bind address is `127.0.0.1`. The evidenced consumer — DeZog in VS Code
  on the developer's own machine — works with no flag at all.
- `--esp-listen-address ADDR` widens it (e.g. `0.0.0.0`). Reaching the emulated
  Next from another machine is a deliberate, visible choice recorded in the
  command line, not a default anyone gets by accident.
- **No separate enable flag** beyond the existing `--esp`. The guest must still
  send `AT+CIPMUX=1` and `AT+CIPSERVER=1,<port>`, so nothing listens unless
  guest software explicitly asks; the AT command *is* the opt-in, and a second
  host-side gate would be ceremony rather than defence. (Rejected alternative,
  recorded because it is the obvious counter-proposal: a `--esp-server` flag
  would protect against an untrusted guest ROM opening a port. That threat is
  not distinguishable from the one already accepted by running an untrusted ROM
  with `--esp` at all, which can already dial out to the LAN.)

Consequences kept deliberately:

- A bind failure (port in use, address not local) answers `ERROR` and logs; it
  never falls back to a wider address. Silent widening is the failure mode this
  decision exists to prevent.
- The listener is closed on `AT+CIPSERVER=0`, on engine teardown, and on the
  same reset paths that drop connections. A port outliving the machine that
  opened it would be the other way this leaks.
- No save-state, exactly as §2's simplification (5): a listening socket is host
  topology, not machine state.

### 13.5 Architecture

Two shapes from §3 pay off here and one piece is genuinely new.

**Already in place.** Connections are a table (`conn_`, `MAX_CONNECTIONS = 5`,
which is ESP-AT's own `CIPMUX=1` ceiling), and `queue_ipd_header(cid,
multiplexed, len)` is the single place the wire format is decided. The AT
dispatch table takes rows. So (1) and (3) of the triad are close to filling in
blanks, which is what §3 predicted.

**Genuinely new: accepting a connection.** `EspTransport` models an *outbound*
socket — `begin_connect`, `send`, `recv`, `close`. Nothing in it can listen, and
nothing in `AtEngine` can own a transport: slot 0's is handed in by reference at
construction and every other slot is inert precisely because it has none.

So server mode adds:

- **`EspListener`** — a listening socket, with the same non-blocking `poll()`
  contract as `EspTransport` (the threaded wrapper runs it on a worker thread,
  so a blocking `accept` would be the same defect a blocking `poll` would be).
  It yields accepted connections as ready-made `EspTransport`s.
- **Per-slot transport ownership.** An accepted connection's transport is
  created by the listener and owned by the slot, alongside slot 0's
  externally-owned pointer. The distinction is real — one is borrowed and must
  not be freed, the other is owned — so it is expressed in the type rather than
  in a comment.
- **Platform twins.** `esp_socket_posix.cpp` / `esp_socket_win.cpp` gain the
  bind/listen/accept half, mirroring the existing split (§7.5).

### 13.6 What this does NOT add

Stated so the surface does not grow past the evidence, exactly as §1.4 did:

- No `AT+CIPMODE` passthrough (forbidden in server mode anyway).
- No UDP server, no SSL server.
- No faithful `busy p...` / `ALREADY CONNECTED` / `SEND FAIL` strings — the
  consumer's parser is being written against whatever this subset emits, which
  is the same bargain §5.4 struck.
- No multiple simultaneous listeners: ESP-AT has one server, and so does this.

### 13.7 What implementation decided that §13 did not

Recorded here because each one is a **deviation from the firmware**, and a
deviation nobody wrote down is a defect waiting to be "fixed". The short form
lives beside the code as simplification (8) in `esp_at.h`.

| Decision | Firmware | Here | Why |
|---|---|---|---|
| **Inbound connection ids start at 1** | the first client gets id 0 | ids 1..4 | Slot 0's transport is the one handed in at construction and the only object that can serve an `AT+CIPSTART`. Giving it to a peer would cost the guest its outbound capability for the session. Four inbound slots remain, which is one fewer than ESP-AT's five. |
| **`AT+CIPSERVER=0` with no server running** | `OK` | `ERROR` | The single appearance of `AT+CIPSERVER` in all the software surveyed (§1.4) is a client turning it *off* at init — which v1.0 answered `ERROR`, as an unknown command, and which that client evidently survives. `ERROR` keeps that path byte-identical; `OK` would change an evidenced client's input on no evidence. It also matches `AT+CIPCLOSE` with nothing open, where the `ERROR` is load-bearing (§5.2). |
| **`AT+CIPMUX=<n>` refuses a CHANGE, not the command** | refuses outright while any connection is open | a request for the mode already in force still answers `OK` | Keeps `AT+CIPMUX=0` — the line NXtel sends at init, and which v1.0 always answered `OK` — behaving exactly as it did in every circumstance. A genuine change is refused, which is the part that matters: switching framing under a live peer hands it a wire format it never negotiated. Going back to `0` is also refused while the server is up, which is ESP-AT's own rule ("you should delete the server first"). |
| **No `AT+CIPCLOSE=<id>`** — **superseded by [§14](#14-per-connection-close-gh-211) (GH #211)** | required under `CIPMUX=1` | *was* absent; the argument form now exists, and the bare `AT+CIPCLOSE` still keeps its v1.0 meaning (close the outbound connection) | The reasoning here was that DeZog closes from its end, which arrives as `<id>,CLOSED` and frees the slot — true, and it does not cover a peer that **wedges** instead of closing. §14 is what that costs and what was built. The second half of the row survives intact and is the constraint §14 is built around: giving the bare spelling a second meaning under `CIPMUX=1` would make nextsync's behaviour depend on a mode nextsync never sets. |
| **`AT+CIPSERVER=1,0` is refused** | port 0 is not special-cased | `ERROR`, although the socket layer accepts 0 as "OS-assigned" | A guest that named no port has no way to be told which one it got. The capability stays available to a HOST (`EspListener::open(0)` + `port()`), which is also how the module's own suite binds without a fixed port. |
| **Windows uses `SO_EXCLUSIVEADDRUSE`, POSIX `SO_REUSEADDR`** | — | the twins differ | The identically-named Winsock option does not mean the POSIX one: on Windows `SO_REUSEADDR` lets a *different process* bind the same address and port and take the connections, which Microsoft documents as a hijacking hazard. Copying the name across would make a local process able to steal a port carrying an unauthenticated debug protocol. The cost is stricter: a rebind while a previous connection is in `TIME_WAIT` can fail with `WSAEADDRINUSE`, surfacing as `ERROR` — loud and retryable, which is what §13.4 asks for. |
| **One pending accepted connection at a time** | — | `EspListener::poll()` takes one and stops until it is collected | The engine collects it on the very next service pass, so a real client is never throttled; without the bound an unattended listener would allocate a transport per inbound SYN while the guest was not looking. The rest wait in the kernel's listen backlog. |
| **A fifth simultaneous peer is accepted and closed at once** | refused past the ceiling | same effect, one slot lower | The peer learns immediately that there is nowhere to go, rather than holding a connection that is open on its side and invisible on ours. |

**One obligation of §8.1 is only partly met, and it is stated rather than
quietly carried:** decision 7 ("a visible log line on every connection made or
refused") holds for inbound connections — the module logs an accepted
connection at `info`, which is on by default — but the **GUI status cell does
not show them**. `EspGatedTransport`, which feeds `EspConnectionLog`, decorates
the *outbound* transport only; an accepted transport comes from the listener and
is not decorated. Closing that gap means giving the listener a way to report an
acceptance to the host, which §13 did not ask for and which is therefore not
built. Headless runs — the evidenced consumer's own configuration — are
unaffected, since there the log *is* the report (§8.2).

---

## 14. Per-connection close (GH #211)

§13.7 recorded "No `AT+CIPCLOSE=<id>`" as a decision, and the reasoning was that
DeZog closes from its end — which arrives as `<id>,CLOSED` and frees the slot,
so nothing needed the command. That is true of a peer that **closes**. It says
nothing about a peer that **wedges**, and that is the case the consumer hit.

### 14.1 The failure this exists for

[dezogif_ng](https://github.com/jorgegv/dezogif_ng) — the same consumer that
forced §13 — re-runs its AT chain after N consecutive transport faults. That
recovery cannot free a connection whose peer has stopped answering rather than
disconnected:

- `AT+CIPSERVER=0` retires the **listener** and deliberately leaves established
  connections alone (§13.7, and it is `SRV-23` in the suite);
- `AT+CIPSERVER=0,<close_all>` is **refused**, and stays refused — see §14.4;
- nothing else in the surface names a connection.

With four inbound slots, four wedged peers exhaust the module and every later
client is turned away for the rest of the session. On real hardware that is a
power-cycle-level failure. The suite states it as a row rather than as prose:
`CLS-20`/`CLS-21` wedge all four slots, watch a fifth peer be turned away, and
then put the module back in service with one command.

### 14.2 What was added

| Line from guest | Reply (exact bytes) | Notes |
|---|---|---|
| `AT+CIPCLOSE=<id>` | `\r\n<id>,CLOSED\r\n\r\nOK\r\n` | Closes the connection with that link id and **returns its slot to the pool**. `<id>` is echoed, so the guest observes the close it asked for |
| `AT+CIPCLOSE` | unchanged — `\r\nCLOSED\r\n\r\nOK\r\n` (`\r\n0,CLOSED\r\n\r\nOK\r\n` on a multiplexed session); `\r\nERROR\r\n` if nothing is open | Still means **close the outbound connection**, in every mode |

Both spellings run the same tear-down (`AtEngine::close_connection`), so the
choice between the prefixed and unprefixed `CLOSED` is made in exactly one
place — the rule `queue_ipd_header` already establishes for `+IPD`. The
notification is emitted **immediately** rather than deferred behind buffered
data, unlike the peer-close path: the guest asked for the close, and anything
received-but-not-yet-framed dies with the connection, which is what the bare
spelling has always done (`CLS-18`/`CLS-18b`).

### 14.3 The bare spelling does not acquire a second meaning

The constraint from §13.7 survives intact and is the reason the argument form is
a separate dispatch entry rather than an optional argument to the old one:
**nextsync loops `AT+CIPCLOSE` up to ten times while `ERROR` is not seen** and
never sends `AT+CIPMUX`. Its behaviour must not become mode-dependent. So
`AT+CIPCLOSE` closes the outbound connection whatever the mode, and
`AT+CIPCLOSE=` — the argument form with no argument — is `ERROR`, **not** a
fall-back to the bare form (`CLS-11`): a spelling that names no connection
closes none.

### 14.4 The refusals, and why each one is `ERROR`

| Input | Answer | Why |
|---|---|---|
| `AT+CIPCLOSE=<id>` under `CIPMUX=0` | `ERROR` | The argument list is read from the **mode**, never sniffed from the text — the rule `AT+CIPSEND` already follows (`SRV-17` is its mirror). Real firmware rejects the parameter in single-connection mode too |
| An id with no live connection | `ERROR` | What the bare spelling already answers for "nothing was open", and what nextsync depends on. `OK` would tell a guest it had freed a slot that was never taken. A connection the **peer** has already dropped is in this state too: its `<id>,CLOSED` is already owed and arrives on its own (`CLS-17`/`CLS-17b`) |
| `AT+CIPCLOSE=5` — ESP-AT's **close-all** | `ERROR` | A **decision**, not a consequence of the slot count. Accepting it would promise a choice about connections the guest did not name — in which order the `CLOSED`s arrive, and whether the outbound slot 0 is among them. That is precisely the promise `AT+CIPSERVER=0,<close_all>` is refused for, and refusing both keeps the model honest. The consumer asked for a per-connection close, so a per-connection close is what exists |
| `AT+CIPCLOSE=9`, `=x`, `=1,1` | `ERROR` | Out of range, not a number, trailing argument. One link id, or nothing |

**`AT+CIPSERVER=0,<close_all>` stays refused.** GH #211 explicitly did not ask
to change it, and it should not be changed: an explicit per-connection close is
a different thing from a bulk close smuggled in as a server-mode argument, and
the refusal is what makes the model's silence about "what happens to existing
connections" honest rather than accidental. `SRV-12` is untouched.

### 14.5 How it is proved

The AT suite drives a fake transport, so its 37 `CLS-*` rows can assert that
`close()` reached the fake and that the slot was reused — not that a real host
socket went away. `esp-close-func`, in the screenshot/functional suite, is the
row that closes that gap: a real client on a real socket is greeted by the
guest, the guest sends `AT+CIPCLOSE=1`, and the client then **dials in again**
and is announced to the guest as `1,CONNECT` — the same id, which is the slot
having genuinely returned to the pool.

**The reconnect is the assertion and the EOF alone is not**, which was measured
rather than reasoned: jnext exits at the end of the row and process exit closes
every socket it owns, so an EOF-only version of that row passed against a build
with the socket close removed *and* against one with the slot release removed as
well. What it still cannot distinguish is stated in the row itself — removing
the explicit `transport->close()` while leaving the slot release in place also
passes, because destroying the accepted transport closes its descriptor anyway.

### 14.6 What this does NOT add

- **No close-all, under any spelling** (§14.4).
- **No `AT+CIPSTATUS`**. Knowing *which* ids are live is the obvious companion
  request, and no consumer has made it — the recovery path closes the id it was
  using. Adding it would mean inventing a status format nothing parses.
- **No change to who may connect.** This command only tears connections down;
  the bind address and the security posture of §13.4 are untouched.

---

## 15. Server idle timeout (GH #240)

`AT+CIPSTO` is the first command in this surface whose behaviour was **measured
on real hardware before anything was written**, and that changes what the model
is answerable to. Everywhere else the authority is VHDL or guest source; here
there is neither — the module is on the far end of a UART cable — so the
oracle is an actual Ai-Thinker ESP-01, plus the [ESP8266 AT Instruction Set
v1.5.4](https://www.espressif.com/sites/default/files/4a-esp8266_at_instruction_set_en_v1.5.4_0.pdf)
§5.17 for the range.

### 15.1 The measurement

Owner's Next, 2026-08-08. Module identified with `.UART`:

```
AT version:1.2.0.0(Jul  1 2016 20:04:45)
SDK version:1.5.4.1(39cb9a32)   Ai-Thinker Technology Co. Ltd.
AT+CIPSTO?  →  +CIPSTO:180
```

Then, over DZRP against `dezogif_ng` running as a TCP server (`AT+CIPMUX=1` +
`AT+CIPSERVER=1,11000`): a client connects, completes a `CMD_INIT` exchange and
then says nothing. The module dropped it after **182.5 s** and **181.8 s** on two
runs. Same subnet, no NAT; the stub has no code path that closes an established
connection, and the client neither sent nor closed.

So the timeout is a **live mechanism** rather than a documented one, 180 is the
firmware default, and it is what quietly kills an idle debug session.

### 15.2 What was added

| Line from guest | Reply (exact bytes) | Notes |
|---|---|---|
| `AT+CIPSTO=<time>` | `\r\nOK\r\n` | `0~7200` seconds, **inclusive** at both ends. Applied live, so raising it extends a client that is already connected |
| `AT+CIPSTO?` | `\r\n+CIPSTO:<time>\r\n\r\nOK\r\n` | 180 at power-on and after `AT+RST` |
| `AT+CIPSTO=7201` / `=-1` / `=` / `=abc` / `=10,20` | `\r\nERROR\r\n` | One in-range number, or nothing — the rule `AT+CIPCLOSE=<id>` already follows |

Enforcement lives in `AtEngine::enforce_server_timeout()`, called from
`service_transports()` **after** `drain_socket` (so a client that spoke on this
pass has re-armed before it is judged) and **before** `frame_ipd` (so the
`CLOSED` it owes is emitted on the same pass a peer close would have been). The
close is modelled exactly as a peer close: `close_pending` defers the
notification until everything already received has been framed, the slot returns
to the pool, and **no `OK` follows** — the guest did not ask for this, so it is a
URC and not a command reply.

### 15.3 What is inferred, and what is measured

The one part of this that hardware has **not** confirmed is what the guest sees.
v1.5.4's `CIPSTO` entry does not say; the `[<id>,]CLOSED` spelling is inferred
from `AT+CIPMODE`'s note that a broken normal-TCP connection prompts exactly
that, and from there being no other spelling a guest could parse. It is recorded
as an inference in `esp_at.h` simplification (9d) rather than presented as fact,
and a real Next could still contradict it.

### 15.4 Three things this deliberately does NOT model

- **Whether server-initiated traffic restarts the timer.** Newer esp-at
  documentation says it does not; **v1.5.4 is silent**, and this module does not
  invent behaviour it cannot cite for the firmware it emulates. An
  implementation must nonetheless pick one, and it picks the one the requirement
  states — *a connection whose client has sent nothing* — so only peer → guest
  bytes re-arm the timer (`AtEngine::drain_socket`) and an `AT+CIPSEND` never
  does. That is a consequence of the requirement, not a claim about hardware.
- **The outbound connection.** This is the TCP *server* timeout, and slot 0 is
  the guest's own dial-out, which no server accepted into (§13.7 (a)). §2's
  simplification 6 — "an established connection that goes silent is never timed
  out" — therefore still holds in full for outbound connections and is now
  false only for inbound ones.
- **Persistence.** `AT+CIPSTO` is absent from v1.5.4's list of commands that
  write to flash, so the value does not survive a restart. `AT+RST` puts it back
  to 180, exactly as `echo_` and `cipmux_` go back to their power-on defaults —
  and a module that had been running for weeks still answering `+CIPSTO:180` is
  the evidence for that.

### 15.5 How it is proved, and what the proof does not cover

The default window is **three minutes**, so a suite that proved this by waiting
would be a suite nobody runs. `AtEngine` therefore gained its one new seam: an
injectable clock (`set_clock`), defaulting to `std::steady_clock::now()` so that
every consumer and every pre-existing row is unchanged by construction. Both
wall-clock reads in the engine — the `AT+CIPSTART` deadline and this one — go
through it, so a caller that supplies time supplies all of it.

The 33 `STO-*` rows in `esp_at_test` drive that clock by hand: the default is
reported *and* enforced, the window is exact at its boundary (29 s leaves the
client alone, 30 s drops it), `0` survives 100 000 s, client traffic restarts
the window while the passage of time alone does not, the outbound slot is
untouched, and `AT+RST` forgets the value.

**What they prove is that the engine honours the window it is given.** They do
not prove the window a real ESP-01 uses — that took the hardware probe in §15.1,
and the issue is explicit that the accepted-and-changed arm is verifiable on
hardware only: re-run the probe with `AT+CIPSTO=7200` and require the connection
to survive past 300 s where it died at ~182 s.
