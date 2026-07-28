# ESP-01 Wi-Fi Module — Emulator Design

**Issue:** [GH #25](https://github.com/jorgegv/jnext/issues/25) (v1.0) — follow-up
[#154](https://github.com/jorgegv/jnext/issues/154) (full datasheet-level emulation).
**Status:** v1.0 in progress. Branches 1 (UART device seam) and 2 (socket transport) merged;
branch 3 (AT engine) merged at `6a79f6fd`; branch 3.5 (modularisation + this document) in flight;
branches 4 (CLI/config/wiring) and 5 (functional test) not started.
**Last updated:** 2026-07-28.
**Audience:** jnext maintainers **and anyone reusing this module in another project**. The module
is deliberately shaped to be liftable; this document is its specification, not a jnext-internal
note.

> **Authority.** Hardware behaviour is cited from the ZX Spectrum Next FPGA VHDL
> (`cores/zxnext/src/`) and `nextreg.txt`. Guest-visible protocol behaviour is cited from the
> software that actually consumes it — the NextZXOS ESP driver and dot commands shipped on the
> official SD card, NXtel, and nextsync. Nothing here is derived from the Espressif AT manual, and
> nothing is asserted without a citation. Where something is untested, it is labelled untested.

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
- [6. The timing model](#6-the-timing-model)
- [7. Architecture](#7-architecture)
- [8. Security posture](#8-security-posture)
- [9. Rejected alternatives](#9-rejected-alternatives)
- [10. v1.1 extension points](#10-v11-extension-points)
- [11. Open questions — to be tested, not assumed](#11-open-questions--to-be-tested-not-assumed)
- [12. Evidence index](#12-evidence-index)

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
   - the `UartDevice` seam and its cold-boot lifetime hazard (§7.6);
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
| **Server / listen mode** (`AT+CIPSERVER`) | Appears **exactly once** in all software examined, and only to turn it **off**. NextZXOS's own listen/accept API is marked `***TODO, not implemented`. Not building it also removes the entire inbound attack surface. |
| **UDP** | Zero consumers. nextsync is TCP-only; NXtel and the dot commands are TCP-only. |
| **Passthrough** (`AT+CIPMODE`) | Appears **nowhere** in any examined software. |
| **Multiplexed connections** (`AT+CIPMUX=1`) | nextsync never sends `AT+CIPMUX` at all — it relies on the power-on default — and its `+IPD` reader is a byte FSM that dies on `+IPD,<id>,<len>:`. Since **no command can correct a wrong default at runtime**, the default must be 0 and `=1` must be refused loudly rather than accepted-and-ignored. |
| **TLS** | No evidenced consumer. sQLux has it; nothing on the Next asks for it. |

`AT+CIPMUX=1` is answered `ERROR`. That is the one place where refusing is safer than accepting:
silently accepting would promise a wire format that breaks the one client which cannot ask for it
back.

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
large — but it is **probabilistic, not guaranteed**: at 1.152 Mbaud a 1460-byte chunk drains in
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
would break nextsync outright — after `+IPD,` its FSM reads the id digit, hits the `,` which is
neither `:` nor a digit, and bails. Centralising the decision is exactly why whoever adds CIPMUX
support cannot get it wrong by accident.

**C. AT dispatch is a table.** `kCommands` maps a command string to a uniform
`void(const std::string& args)` member handler, with a `prefix` flag distinguishing exact-match
entries from `NAME=<args>` entries. Adding the ≈40 commands v1.1 wants is adding rows, not
extending an if/else chain.

---

## 4. The Next-side hardware interface

Facts about the machine the module attaches to. Two of them were stated wrongly earlier in the
design record and are corrected here; both corrections are VHDL-verified.

### 4.1 The ESP is UART 0

`zxnext.vhd:1611` (`-- uart 0 (esp)`), `zxnext.vhd:3381` (`o_UART0_TX <= uart0_tx_esp`).
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
- **The RX interrupt fires per byte.** `zxnext.vhd:1941-1944` feeds
  `uart0_rx_near_full or (uart0_rx_avail and not nr_c6_int_en_2_210(1))` into `im2_int_req`.
  `rx_near_full` is the ¾ mark (384 bytes). NextZXOS's `ESPAT.DRV` is interrupt-driven — this
  matters for the open question in [§11.2](#112-the-rx-pacing-may-be-replaceable--and-the-hazard-is-interrupt-saturation).

---

## 5. The command set

Follows directly from [§1.1](#11-how-the-at-surface-was-derived). Case-insensitive on the command
**name**; arguments are never case-folded (a hostname keeps its case).

### 5.1 v1.0 commands

| Line from guest | Reply (exact bytes) | Notes |
|---|---|---|
| *(empty line)* | `\r\nERROR\r\n` | nextsync's liveness probe, and what NXtel's trailing payload CR,LF produces |
| `AT` | `\r\nOK\r\n` | |
| `ATE0` / `ATE1` | `\r\nOK\r\n` | Really toggles echo; default OFF (simplification 2) |
| `AT+RST` | `\r\nOK\r\n\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n` | Drops every connection **without** a `CLOSED` — the guest asked for it |
| `AT+CIPSTART="TCP","<host>",<port>` | deferred → `\r\nOK\r\n` or `\r\nERROR\r\n` | Reply comes from `poll()`, not from dispatch. Non-TCP protocol → `ERROR` |
| `AT+CIPSEND=<n>` | `\r\nOK\r\n> ` → *n payload bytes* → `\r\nSEND OK\r\n` | **Trailing space after `>` is mandatory** |
| `AT+CIPSENDEX=<n>` | identical | Alias (simplification 3) |
| `AT+CIPCLOSE` | `\r\nCLOSED\r\n\r\nOK\r\n`; `\r\nERROR\r\n` if nothing open | The `ERROR` case is load-bearing — see below |
| `AT+CIPMUX=0` | `\r\nOK\r\n` | |
| `AT+CIPMUX=1` | `\r\nERROR\r\n` | Refused, not ignored (§1.4) |
| `AT+UART_CUR=<baud>,…` / `AT+UART_DEF=` / `AT+UART=` | `\r\nOK\r\n` | Recorded and traced; pacing follows the channel's **live prescaler**, so nothing else is needed |
| `AT+GMR` | canned version block | Anchors `T version:` and `DK version:`, each printed to the next `(` |
| `AT+CWJAP?` | `\r\n+CWJAP:"<SSID>","<BSSID>",1,-55\r\n\r\nOK\r\n` | |
| `AT+CIPSTA?` | `+CIPSTA:ip/gateway/netmask` block | Anchors `gateway:"`, `netmask:"` |
| `AT+CIFSR` | `+CIFSR:STAIP,…` / `STAMAC,…` | Anchors `TAIP,"`, `TAMAC,"` |
| `AT+CIPDNS_CUR?` | two `+CIPDNS_CUR:` lines | |
| *anything else* | `\r\nERROR\r\n` | Including a line over `MAX_COMMAND_LEN` (512), which is refused **whole** — a truncated `AT+CIPSTART=` is still a valid `AT+CIPSTART=` and would open a connection to a host the guest never named |

**Unsolicited (URC):**

| Emitted | When |
|---|---|
| `\r\n+IPD,<len>:<data>` | Peer data, framed only when the wire is quiet (simplification 1) |
| `\r\nCLOSED\r\n` | Peer closed — deferred until everything already received has been framed and drained |

### 5.2 The framing constraints that actually bite

- **The `> ` prompt.** Three parsers, three requirements: NXtel busy-waits for a bare `>` **with
  no timeout at all**; internet-nextplorer likewise; `.UART` waits for the exact sequence
  `OK`,13,10,`>`. Emitting `"\r\nOK\r\n> "` — **trailing space included** — satisfies all three.
  Emitting nothing hangs the guest **forever**: these are polled busy loops with no error path.
- **`.ESPBAUD` does a full exact compare against `"OK\r\n"`.** This is why every terminator in the
  engine is CRLF and never a bare LF.
- **`CLOSED` is detected by a 5-byte window** (`OSED\r`); the client source comment reads *"It
  enough to check 'OSED\n' :-)"*.
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
- **`tick(master_cycles, byte_ticks)` — emulated time.** Frames a `+IPD` when the wire falls quiet
  and releases **one byte per `byte_ticks`**, where `byte_ticks` is
  `UartChannel::byte_transfer_ticks()` = `prescaler() × frame_bits()` — read **live on every
  call**, which is the whole reason `AT+UART_CUR` and `.ESPBAUD` work for free.

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
   header gets a **single ≈314 µs window with no outer retry** in nextsync's FSM. A 20 ms delivery
   gap anywhere inside the header therefore fails **100 % of the time, regardless of FIFO size**.

Pacing at the live baud satisfies both automatically: there are no gaps, because every byte of the
queue leaves at the same cadence.

Per-scanline granularity would also be safe on both counts; **UART-tick granularity is what is
shipped**, because the TX path already computes exactly that clock and the pacer can simply reuse
it.

### 6.3 Chunk sizing

`+IPD` chunks are capped at `MAX_IPD_CHUNK` = 2048 bytes and are cut **only when the guest-bound
queue has drained**, so a busy peer produces few large chunks rather than many small ones. That is
what keeps nextsync inside its 5-chunks-per-server-packet budget without an explicit floor
(simplification 7). nextsync's server packets are 261 / 1029 / 1460 bytes, so its default config
**deliberately overruns the 512-byte FIFO by ≈2×**, relying on the Z80 draining concurrently — its
author says so in `nextsync.py:33-34`.

One chunk is framed per quiet moment; the next follows when that one drains.

### 6.4 The guest is the bottleneck either way

nextsync's own receive loop is 93 T-states/byte at 28 MHz — a ceiling of **≈301 KB/s**. 2 Mbaud
already delivers 200 KB/s. Any faster delivery scheme buys at most ≈1.5× over the fastest real
configuration; see [§11.2](#112-the-rx-pacing-may-be-replaceable--and-the-hazard-is-interrupt-saturation).

---

## 7. Architecture

### 7.1 Layering (owner-approved)

```
ESP core (passive):   bytes in -> bytes out, poll(), no clock, no threads, no jnext types
    │ optional
Threaded wrapper:     owns core + thread + two SPSC queues
    │
jnext adapter:        implements UartDevice; drains the outbound queue into the RX FIFO at baud
```

**The core is passive and must stay drivable inline.** That is what makes the wrapper *optional*
rather than decorative: another project may want to drive the module from its own scheduler, or
inline with no thread at all. The test suite exercises **both** modes.

**Why threading is allowed at all.** The ESP's internal latency is not observable and does not
need emulating: no Next software depends on `AT+CIPSTART` taking a particular time to parse, and
real ESP latencies are milliseconds and highly variable, so anything that survived real hardware
tolerates any speed. Processing instantly is strictly *safer*, not merely cheaper. The engine as
built already implements the decoupling (`poll()` wall-clock, `tick()` emulated-time), so the only
remaining question was **where `poll()` is called from** — and the answer is the wrapper thread,
not the frame loop.

**Consequence for branch 4:** `poll()` is **not** wired into `begin_new_frame()`. The frame loop
drives only the baud-paced `tick()`. This removes the one place a slow `getaddrinfo` or socket
read could stall the frame loop, which means the synchronous-DNS compromise in the transport stops
being a compromise — it just happens off the emulation thread.

### 7.2 Module layout

Target layout (branch 3.5), with jnext-rooted includes replaced by self-rooted ones so a consumer
does not have to replicate jnext's include root:

```
src/esp01/
  include/esp01/     public interface:  esp_at.h, esp_socket.h, esp_log.h, esp_threaded.h
  src/               implementation:    esp_at.cpp, esp_socket.cpp, esp_address_policy.cpp,
                                        esp_socket_platform.h (private), esp_socket_posix.cpp,
                                        esp_socket_win.cpp
  test/              esp_at_test.cpp, esp_socket_test.cpp
src/peripheral/
  esp_uart_adapter.{h,cpp}              the jnext side: implements UartDevice
```

Includes read `#include "esp01/esp_at.h"`. CMake target: **`esp01`**.

<!-- VERIFY: the layout above is the mandated target for branch feat/esp-modularise, which had not
     landed when this document was written. Reconcile the directory names, the header file names
     (esp_log.h / esp_threaded.h), the CMake target name and the adapter path against the landed
     tree. -->

### 7.3 The seams

Four seams, in dependency order. Together they are what make the module liftable.

| Seam | Contract | Why it is a seam |
|---|---|---|
| **`EspTransport`** (abstract) | `begin_connect` / `poll` / `state` / `send` / `recv` / `close` / `last_error` / `peer_address`. **Nothing may block**, and the interface is shaped so a caller *cannot ask it to*: no synchronous connect in the vtable, `poll()` takes no timeout, `send`/`recv` return a **count** never a completion, and nothing returns a native handle. | A consumer can substitute its own transport. The AT engine is driven against an in-memory fake with no sockets, no DNS and no listener. |
| **`UartDevice`** | `receive(byte)` (one byte out of the guest TX FIFO), `poll()`, `tick(master_cycles, byte_ticks)`, plus a `RxSink` **callback** for the guest-bound direction. | The callback — rather than a `Uart&` — is what keeps ESP code free of any UART header. A device can be unit-tested by pointing the sink at a `std::vector`. Attachment is **non-owning**, matching `SpiDevice` / `I2cDevice`. |
| **Address policy** | Pure functions `classify_address` / `select_candidate` over `IpAddress` + `AddressPolicy`. No sockets, no DNS, no platform headers, no logging. | The security-relevant part must be exhaustively unit-testable offline **and** overridable — the socket suite's own in-process listener is on 127.0.0.1, which the default policy denies. |
| **Logging** | Every trace call goes through a single accessor; the module must not require another project's logging framework. jnext binds it to the `esp01` spdlog logger in the adapter. | The one hard jnext dependency the module still had (`core/log.h`, 21 call sites in the socket layer plus the engine's `lg()`). Removing it makes the module **and its tests** portable in one move. |

<!-- VERIFY: the logging seam's exact API (free function vs. sink object vs. macro, and how the
     jnext binding is installed) is being decided on feat/esp-modularise. The contract above is
     what must hold; substitute the real signature once it lands. -->

### 7.4 Platform split

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
With `poll()` on the wrapper thread (§7.1) the stall no longer reaches the frame loop at all.

### 7.5 Tests ship with the module

A consumer gets the code and its proof together. Constraints on the move:

1. **Sources live with the module; registration stays with jnext.** Both suites remain declared in
   `test/unit-tests.conf` with their exact pinned row counts and registered via `add_test()`. The
   declared-suite contract — the harness *refuses to run* if the manifest and CMake disagree, and
   a missing suite is a loud failure — is not weakened for the sake of modularity. Only the file
   location changes.
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

Current counts: `esp_socket_test` 121 rows, `esp_at_test` 126 rows.

<!-- VERIFY: suite names and pinned row counts after the move — update from the landed
     test/unit-tests.conf if branch 3.5 renamed or re-registered either suite. -->

### 7.6 Lifecycle

- **The wrapper joins its thread before `~Emulator()`.** `emulator_cold_boot`
  (`src/platform/emulator_boot.h:67-77`) does `emu.~Emulator(); new (&emu) Emulator();` — placement
  new **at the same address**. A live thread writing into the reconstructed object is **silent
  corruption, not a crash**. Joining first removes the hazard entirely.
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

### 7.7 Hot-path cost

`UartChannel::tick` runs once per Z80 instruction (10⁵–10⁶ times/frame), which is far too hot for
socket work — `poll()` must never hang off it. The emulated-time hook is gated:
`if (device_ && device_->tick_wanted())` (`src/peripheral/uart.h:276`), with `tick_wanted()`
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

**Status:** the address policy is implemented and unit-tested. The **enable flag, the hostname
allowlist and the connection log line are branch-4 work and do not exist yet.**

### 8.3 No host network information may leak into the guest

The advertised SSID is the fixed, obviously-synthetic **`JNextWifiHost`** — never the host
machine's real SSIDs. The same principle applies to every BSSID, MAC, channel, RSSI and IP the
module reports (§5.6): **synthetic and stable, never harvested from the host**. The emulated
module is not a radio.

### 8.4 Tracing is a first-class requirement

Every detail of an app's ESP interaction must be traceable: each AT command received, each response
emitted, the `>` prompt handshake, payload byte counts, `+IPD` framing, connection open/close,
socket errors, allowlist decisions, and the RX pacing / FIFO state. This uses the existing
framework — spdlog named loggers selectable at runtime via `--log-level <subsystem>=<level>` — with
a dedicated **`esp01`** subsystem (`src/core/log.h:92`). `SPDLOG_ACTIVE_LEVEL` strips trace/debug
in release builds and the runtime check is an inline integer compare, so this is free when off.

**Nothing is on by default**, with the likely exception of TCP connection open/close at `info` —
those are user-visible, security-relevant events and pair naturally with the "never silent"
requirement above.

<!-- VERIFY: `--log-level` subsystem documentation. src/core/log.h:89 carries a note that the
     subsystem list in doc/man/jnext.1.md must gain `esp01`; at the time of writing the man page
     did not list it. Confirm branch 4 lands that edit (and that docs-check passes). -->

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
*batching*; it does **not** by itself establish that baud pacing is the only alternative — see
§11.2.

**Coupling the socket directly to `inject_rx()`.** Passes a unit test and destroys data in headless
runs: `inject_rx` has no baud pacing at all (`uart.cpp` paces TX only) and the RX FIFO is 512 bytes
with drop-newest. Pacing is the device's responsibility, and this is exactly why `UartDevice`'s
`send_to_guest` documentation says so.

**Hanging a device reset off `UartChannel::reset`.** Rejected as unfaithful: that path resets only
the Next-side UART state machine (§4.3). The real reset line is NR 0x02 bit 7 and needs its own
hook.

**Accepting `AT+CIPMUX=1` and ignoring it.** Rejected: it would promise a wire format that breaks
the one client that cannot ask for it back (§1.4).

**"The UART wire is synchronous, therefore the ESP cannot be threaded."** This argument was made
during design and is **wrong**, and the correction matters because the conclusion it was used to
support (no threading) was adopted for a while. The wire *is* synchronous — a baud rate is a shared
clock — but that constrains only the **drain stage**. It says nothing about where ESP *processing*
lives. A true statement about one stage was used to justify a conclusion about the whole component.
The passive-core + optional-threaded-wrapper shape (§7.1) is what replaced it.

**A resolver thread for DNS, in branch 2.** Deferred rather than rejected: it would have been the
first thread jnext runs while the frame loop is live, and the cold-boot placement-new hazard (§7.6)
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
| `AT+CIPMUX=1`, 5 connections | The `conn_` table, per-slot state, per-slot loops, connection ids throughout the state machine | Give slots 1..4 transports; implement the command; pass `multiplexed=true` |
| Multiplexed `+IPD,<id>,<len>:` | `queue_ipd_header(cid, multiplexed, len)` — the one place the wire format is decided | Flip the flag per connection |
| ≈40 further AT commands | `kCommands` table + uniform handler signature | Add rows |
| UDP / TLS / passthrough | `EspTransport` is an interface | New transport implementations behind the same seam |
| Server / listen | — | New capability; **re-opens the inbound attack surface deliberately closed in v1.0**, so it needs its own security review |
| NR 0x02 bit 7 hardware reset | The bit is already latched (`emulator.cpp:2719`) | A device-facing hook driven from the NR 0x02 **write** — distinct from `UartChannel::reset` (§4.2/§4.3) |
| Echo on by default | `ATE0`/`ATE1` really toggle it | One line, if datasheet fidelity is wanted (simplification 2) |
| `AT+CIPSENDEX` `\0` early terminate | Currently an alias | Real early-terminate semantics (simplification 3) |
| `FAIL` / `busy p...` / `ALREADY CONNECTED` | On the never-emit list | Only with a driver-compatibility story — `ESPATreadme.TXT:92` is the constraint |

---

## 11. Open questions — to be tested, not assumed

Two things are genuinely unsettled. They are stated as questions, not as settled design.

### 11.1 nextsync's `flush_uart_hard()` needs 19.3 ms of continuous silence

`flush_uart_hard()` requires **19.3 ms of continuous silence** on the wire before it proceeds. An
instantly-responding emulated ESP is **faster than real hardware**.

The reasoning that it *should* be safe: the module only speaks when spoken to, and nextsync
controls its own sends. But "we made it faster than hardware and something timing-sensitive broke"
is exactly the class of bug that appears only against real software. **UNTESTED.** It belongs in
the branch-5 acceptance test.

### 11.2 The RX pacing may be replaceable — and the hazard is interrupt saturation

There are **three** options, not two. The §6.2 argument rejects per-frame *batching*; it does not
establish that baud pacing is the only alternative.

| Option | Verdict |
|---|---|
| Per-frame batch | **Fails** — 4.5× FIFO overflow *and* 20 ms gaps straddle the `+IPD,<len>:` header |
| **Pace at configured baud** (shipped) | Works; costs the `tick(master_cycles, byte_ticks)` hook and its accumulator |
| **On-demand refill** — top the FIFO up whenever it has space | **Avoids both failure modes**: no overflow because we only write when there is room; no gaps because the next byte is always already there |

**What on-demand refill would buy:** delete the `tick()` hook entirely, along with the hot-path
work its placement required. `AT+UART_CUR` becomes an acknowledged no-op rather than a live pacing
input. The speed gain is **modest, not transformative** — the guest is the bottleneck either way
(§6.4), roughly 1.5× at best over the fastest real configuration.

**The hazard to test, not assume: interrupt saturation.** The RX interrupt fires on **every byte
pushed** (`push_rx_with_flag` → IM2 level 1, `zxnext.vhd:1941-1944`) and NextZXOS's `ESPAT.DRV` is
interrupt-driven. On real hardware the 8.7 µs inter-byte gap at 1.152 Mbaud is what lets the main
program run between IRQs. Refill the instant space appears and the ISR may return to find another
byte already waiting — the Z80 could spend effectively all its time in the handler.

**That presents as "the transfer works but the program appears frozen"**, which is considerably
nastier to diagnose than a checksum failure. Secondary effect: `rx_near_full` (¾ = 384 bytes) would
be permanently asserted during a transfer, where at 115200 it rarely fires — changing the interrupt
pattern the driver sees.

**The experiment** (branch 5, cheap once the functional test exists): run **nextsync** against both
pacings and watch whether the guest **makes progress**, not merely whether the transfer completes.
If there is no starvation, the simpler design wins and the tick hook can be deleted.

Explicitly: **do not defend the shipped pacing merely because it is already built.**

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
| nextsync chunk budget / packet sizes | `nextsync.py:33-34` (author's own comment) |
| CSpect is not 8-bit clean | `docs/dotcommands/http.md:76` |

### jnext code

| Subject | Location |
|---|---|
| AT engine (rationale in the header) | `src/peripheral/esp_at.h`, `esp_at.cpp` |
| Transport interface + DNS note | `src/peripheral/esp_socket.h` |
| Address policy (pure, portable) | `src/peripheral/esp_address_policy.cpp` |
| Platform primitives (private) | `src/peripheral/esp_socket_platform.h` + `_posix.cpp` / `_win.cpp` |
| UART device seam + lifetime hazards | `src/peripheral/uart_device.h` |
| Gated `tick()` call site | `src/peripheral/uart.h:276` |
| `byte_transfer_ticks()` = prescaler × frame_bits | `src/peripheral/uart.cpp:83-87` |
| NR 0x02 bit 7 latched, unconsumed | `src/core/emulator.cpp:2719` |
| `esp01` logger | `src/core/log.h:92` |
| Cold-boot placement-new hazard | `src/platform/emulator_boot.h:67-77`, `:110-115` |
| Suites + pinned row counts | `test/unit-tests.conf` |

<!-- VERIFY: every `src/peripheral/esp_*` path above moves to `src/esp01/...` when branch 3.5
     lands. Re-point this table at the final locations. -->
