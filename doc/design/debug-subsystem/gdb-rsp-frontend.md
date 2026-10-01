# GDB Remote Serial Protocol frontend — design (GH #281)

> Status: **v1 — mapped against `backend.md` v1** (epic #276, gate #277).
> Owner of this file: the RSP frontend design agent. Backend capability IDs
> (`CAP-*`) are those of [`backend.md`](backend.md); every served packet names
> the CAP it is served by, and nothing here reaches around the backend.
>
> **Revision log**
> - v1 (2026-09-26): client measured (two versions), packet table, target.xml
>   verified against the real `z88dk-gdb` binary, REQ ledger closed (15/15
>   ACCEPTED), shared transport model proposed to DZRP/ZRCP.
> - v1.1 (2026-09-26 night): re-mapped against `backend.md` **v3** — no CAP
>   this adapter uses changed shape (21/24/0 stands). Adopted from v3: the
>   `watch:` address comes from `Paused.matched[] : Hit{event_id, addr,
>   access, value}` (§5.3); `clockl/clockh` from `Time.tstates_total`;
>   `pump(PumpBudget)` runs *after* the tick's frame batch and drains while
>   paused (§6.2); CAP-SES-04: under `--headless` a Stop **pauses and
>   notifies** while a remote client is connected instead of exiting
>   non-zero (owner-ADOPTED 2026-09-27, see v1.4; was backend §13.2), so
>   the §7.2 regression row's `break _main` + `cont` genuinely stops the
>   machine for the client.
> - v1.2 (2026-09-26, after the protocols reviewer's REJECT,
>   `scratchpad/reviews/protocols.md`): **R-2** `monitor reset hard` rewritten
>   — CAP-CTL-12 Hard is today a *deferred* cold boot, so the verb is served
>   only under the synchronous contract of REQ-zrcp-15 and answers `E01`
>   until then (§4.3); **R-5** one port rule for the three servers (absent =
>   off, `0` = OS-chosen ephemeral, logged) and one shared
>   `--debug-listen-address` help text (§6.4, §10); **N-3** stale jnext line
>   citations regenerated (§5.1); **N-8** wait-for-listen and the boot-hold
>   explanation in the regression row (§7.2); **N-12** why the same-pump `T`
>   reply to `?` is load-bearing (§5.4).
> - v1.3 (2026-09-26, backend v4 FYI): REQ-zrcp-15 landed as the CAP-CTL-12
>   Hard reconstruct contract (+ CAP-SES-07 driver) — `monitor reset hard`
>   is now served, `E01` only on `RefusedUnavailable`; guest hard reset is
>   NOT a stop (no `pause_reason` for it, §4.3/§5.3); WP-4 wires the socket over the
>   shared transport package **T** (arch doc) rather than its own listener;
>   CAP-SES-04 "remote connected ⇒ Pause" under `--headless`/SDL was then an
>   owner-pending proposal (backend §13.2); adopted 2026-09-27 (v1.4).
>   Round-2 N-1 closed: a guest hard reset never pauses and has no
>   `pause_reason`; a mid-run client gets nothing until a real stop (§4.3);
>   `qRcmd` joins the pause-first packet set (§5.4 rule 4).
> - v1.4 (owner review 2026-09-27): every §10 question settled — explicit
>   ports (off unless `--gdb-port` given, no default), `monitor in/out` kept
>   and labelled perturbing, `k` = detach, headless/SDL Stop pauses +
>   notifies while a remote client is connected (adopted; the §7.2 row's
>   dependency is closed), a remote pause opens the Qt window. Noted without
>   design change: DSL scripts may mutate the machine; DZRP → 2.2.0.
> - v1.5 (2026-09-29, milestone 1 — WP-1..WP-4 implemented, branch
>   `gh281-gdb-rsp`): §12 records what was built, every deviation from this
>   design with its reason, and the findings. Sections 0-11 are unchanged:
>   where the implementation differs, §12 says so and why.

Every claim carries a `file:line` citation or a captured transcript. Paths:
`z88dk/…` = `/home/jorgegv/src/spectrum/z88dk` (checkout at v2.4, HEAD
`4d530b6eb7` 2025-10-01; `src/ticks/debugger_gdb.c` last changed 2025-03-08);
`up/…` = the **upstream master** copy of the same files fetched 2026-09-26 from
`raw.githubusercontent.com/z88dk/z88dk/master/src/ticks/` (1716 lines vs 1381
local — the client has moved since v2.4, and both versions are targeted);
`src/…` = jnext `main @ 974b0ab19`. Transcripts are in §7 and were produced by
running the **real** `z88dk/bin/z88dk-gdb` against a Python stub that serves
exactly the packets and XML this design proposes.

---

## Work packages — the tracker for this package

Mirrors this package's row in [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
§10.1, which stays authoritative: if the two ever disagree, §10.1 wins and this
table is stale. It exists because §10.1 states each package's sequence as one
long table cell, which is unreadable as a plan and impossible to track against.

Status values: `todo` · `in progress` · `in review` · **`done`** (independently
reviewed and APPROVED). The whole package lands on **one branch** and merges
whole, so `done` here means the sub-item is approved, not merged.

| WP | Branch `gh281-gdb-rsp` (issue #281) | Status |
|---|---|---|
| **WP-1** | codec | **done** (§12; independently reviewed and APPROVED at `0673fca34`) |
| **WP-2** | target description + register packing. **The XML must stay under the 1023-byte ceiling** — a larger one segfaults `z88dk-gdb` v2.4 | **done** (§12; independently reviewed and APPROVED at `0673fca34`) |
| **WP-3** | server | **done** (§12; independently reviewed and APPROVED at `0673fca34`) |
| **WP-4** | wiring / CLI over the shared transport (T) | **done** (§12; independently reviewed and APPROVED at `0673fca34`) |
| **WP-5** | acceptance row + user guide. §11 item 7: upstream-master `z88dk-gdb` `monitor` handling was designed from source and run only against v2.4 — close that here | in review (§13) |
| **WP-6** | the z88dk wiki listing — **post-release**, out of scope for the epic itself | todo |

Depends on: B0 (landed), B, T.

Settled: `monitor in/out` is **kept and labelled perturbing**, and `k` means
**detach** (owner, §1.3 item 19). The client is `z88dk-gdb`, not real gdb —
Fedora's gdb has no `z80` architecture at all, which is why the Z80 knowledge
living on our side is not a limitation.

Every sub-item is reviewed by an agent or person that did NOT write it, and the
branch does not merge until the full §10.3 gate is green on the tip.

---

## 0. Premise, re-verified

1. **A distro gdb cannot debug Z80.** `gdb -batch -ex 'set architecture z80'`
   on this host (GNU gdb Fedora 17.2-2.fc44) prints `Undefined item: "z80".`
   [transcript, 2026-09-26].
2. **The client is `z88dk-gdb`.** `--version` prints `GNU gdb (GDB) 11.0` then
   `The line above is fake, we're pretending to be a gdb here.`
   [`z88dk/src/ticks/debugger_gdb.c:1294-1297`; binary run]. Usage:
   `z88dk-gdb -h <host> -p <port> -x <debug symbols> [-x …] [-v]`
   [`:1343`]; upstream adds `-d <device>` (serial) [`up/debugger_gdb.c:1592`],
   which does not concern a socket server.
3. **The server supplies the target description.** The client refuses the
   connection unless `qSupported` contains `qXfer:features:read+`
   [`debugger_gdb.c:982-987`], then reads `target.xml` and derives the
   register packing from it [`:1011-1069`]. That is why the missing distro
   support is irrelevant: the Z80 register model lives in this adapter.
4. **RSP's worldview stays here.** Flat 64 K address space, one thread, fixed
   16-bit register packing, hex-text framing — all are adapter concerns. The
   backend (`backend.md` §4) has `MemSpace{Cpu, Page, Rom}`, a struct of
   registers and typed events; this adapter projects those onto RSP and
   declines what RSP cannot carry (§2).
5. **Honest subset.** Every packet not in the served set gets the empty reply
   `$#00`, which RSP defines as "unsupported" [gdb manual, Overview: "the
   empty response is used to indicate that a packet is not supported"]. Nothing
   is silently accepted.
6. **The wiki list.** The client itself points at
   `https://github.com/z88dk/z88dk/wiki/Tool-z88dk-gdb` [`:1338-1339`]; the
   raw wiki page (fetched 2026-09-26) currently names no servers, only "MAME,
   FUSE, …" in a methodology table and "see project issues/docs for current
   list". Getting jnext onto that page is an upstream documentation PR after
   #281 ships — noted in §9 as a work package, not a design item.

---

## 1. The client, measured

### 1.1 Everything `z88dk-gdb` ever sends

Derived by grepping every `send_request`, `send_request_no_response`,
`schedule_write_packet` and `schedule_write_raw` call in both versions. The
set is identical in v2.4 and upstream except for `qRcmd` (upstream only).

| Packet | Sent by | When | Cite (v2.4 / upstream) |
|---|---|---|---|
| `qSupported` (bare, **no** client feature list) | `connect_to_gdbserver` | first packet after TCP connect | `:982` / `up:1078` |
| `qXfer:features:read:target.xml:0,3fff` | same | immediately after; one read, expects the whole document in one `l` reply | `:1011-1012` / `up:1107` |
| `?` | same | last step of connect, **no response awaited** — comment says "this should break us" | `:1108-1109` / `up:1207` |
| `g` | `fetch_registers` | on every prompt, after every stop (`registers_invalidated = 1` each main-loop pass) | `:227`, `:1358` |
| `G<hex>` | `set_regs` | `set <reg> <val>`, `restore_pc`; **every register the client does not know is sent as 0000** (`rr[32] = {0}`, `default: continue`) | `:420`, `:483-487` |
| `m<addr>,<len>` | `get_memory` | 32-byte chunks (`MEM_FETCH_SIZE`), `addr` rounded down by 4, clipped at 0x10000 | `:363-408` |
| `M<addr>,<len>:<hex>` | `debugger_restore` | `restore`/`restore_pc`, chunk = `(PacketSize-16)/2` bytes | `:670-732` |
| `Z0,<addr>,1` / `z0,<addr>,1` | `gdb_add/remove_breakpoint` | `break <addr>` / `delete`; also `finish` (Z0 at return address, `external=1`); **`z0` is sent for client-side-only breakpoints too** (`delete_all_breakpoints` on quit) | `:554-601`, `:1634`, `breakpoints.c:97-99` |
| `s` | `debugger_step` / `debugger_next` | `stepi`, `step`, and `nexti` on a non-call | `:654`, `:666` |
| `i<len>` (**decimal**, non-standard) | `debugger_next` | `nexti`/`next` when the opcode at PC is `CD`/`C4`/`CC`/`D4`/`DC`/`E4`/`EC`/`F4` **or the prefix `ED` or `CB`**; `len` = instruction length computed by the client's own disassembler | `:618-651` |
| `c` | `debugger_resume` | `cont`, `finish`; no response awaited | `:548-552` |
| raw byte `0x03` (not a packet) | `debugger_gdb_break` | Ctrl-C at the prompt while running; also sent as a *temporary* break when the client wants to add/remove a breakpoint while running (`BREAKPOINT_ERROR_RUNNING` → `bk.break_(1)`) | `:524-539`, `breakpoints.c:46-49`, `:73-77` |
| `D` | `debugger_detach` | `quit`; a response **is** awaited | `:541-546` |
| `qRcmd,<hex>` | `send_monitor_command` | `monitor <text>` — **upstream only** | `up:206-278`, `up/debugger.c:188` |

**Never sent** (verified by absence in both files): `Z1`–`Z4`/`z1`–`z4` (the
client's own `switch` refuses `BK_BREAKPOINT_HARDWARE`=1 and
`BK_BREAKPOINT_REGISTER`=2 — and `BK_BREAKPOINT_WATCHPOINT` is *also* 2
[`breakpoints.h:9-12`], so `break memory8/16` never reaches the wire and is
evaluated client-side at each stop [`debugger.c:648-658`]), `p`/`P`, `X`, `H`,
`qC`, `qAttached`, `vCont`/`vCont?`, `k`, `QStartNoAckMode`, `bc`/`bs`,
`qOffsets`, `qSymbol`, `T<tid>`, `vCtrlC`. `out <port> <val>` is a client-side
no-op over gdb [`:520`].

### 1.2 What the client requires of replies

- **Framing.** `$data#xx`, checksum = sum mod 256 of `data` [`:839-844`];
  a bad checksum drops the packet silently [`:844-851`]. The client **never
  sends `+`/`-` acks** (`write_packet` frames only [`debugger_gdb_packets.c:174-187`])
  but **tolerates** receiving `+` (erased [`debugger_gdb.c:819-825`]). It
  never retransmits. ⇒ the server sends `+` per RSP (harmless, keeps a
  generic client working) and never waits for one.
- **Response routing is positional.** After a `send_request`, the *next*
  well-formed packet is taken as the response, whatever it is
  [`:857-866`]. Consequence: the server must emit a stop reply **only** as the
  reply to `c`/`s`/`i`/`?`/`0x03`, never spontaneously while the client may be
  mid-request. §5.4 and §6.3 keep that invariant.
- **Stop replies must start with `T`.** The only unsolicited packet dispatched
  is `case 'T'` [`:873-881`]; `S05` would be ignored and the client would sit
  forever after `?`. Upstream additionally parses the two characters after
  `T` with `strtol(…, 10)` [`up` `process_packet`], so the signal must be two
  decimal-looking digits: `T05` is read as 5 in both versions. The `n:r` pairs
  are ignored by both.
- **`qSupported` parsing.** `PacketSize=%d` via `sscanf` — **decimal**
  [`:995-1006`]. GDB proper parses the same field as hex (`remote.c`,
  `strtol(…, 16)` — from GDB source knowledge, not re-verified on this host).
  `PacketSize=4000` reads as 4000 to z88dk-gdb and 16384 to gdb; both are
  within the server's 16 KiB receive buffer (§6.1). The client also looks for
  the substring `NonBreakable` and, if present, disables Ctrl-C
  [`:989-992`] — we do not send it.
- **Response size limit (v2.4): 1023 bytes.** `char recv_data[1024];
  strcpy(recv_data, &inbuf[1])` [`:853-854`] and `request_response[1024]`
  [`:56`]. A 1094-byte `target.xml` reply **segfaults the v2.4 client**
  (reproduced, §7.1). Upstream raised both to `PACKET_BUF_SIZE` (16 KiB)
  [`up:64`, `up` `process_packet`]. ⇒ `target.xml` must be ≤ 1022 bytes on the
  wire, and `m` replies are naturally 64 hex chars.
- **`target.xml` parsing** (sxmlc): root must be `target`; `target/architecture`
  text must equal `z80` [`:1028-1038`]; registers are collected from
  `target/feature[@name='*z80*']/reg` — the **feature name must contain
  "z80"** [`:1044`]; only the `name` attribute is read [`:1049`]; every reg is
  assumed **16-bit** (`register_mappings_count * 4` hex chars, `uint16_t
  rr[32]`) [`:228-235`]; recognised names: `af bc de hl af' bc' de' hl' ix iy
  sp pc clockl clockh` [`:95-115`]; unknown names occupy a slot and are
  ignored on read [`:314-318`]; `pc` and `sp` missing → "Insufficient register
  information" (warning only) [`:1098-1100`]; `clockl` present →
  `has_clock_register`, used by the profiler as `(clockh<<16)|clockl`
  [`:1152-1161`]. Maximum 32 registers (`register_mappings[32]`) [`:117`].
- **Byte order.** `hex2mem` into a `uint16_t[]` on the host [`:235`], so each
  register is two bytes **little-endian** in `g`/`G` (target byte order per
  RSP, and the Z80 is little-endian; the `__BIG_ENDIAN__` branches swap for a
  big-endian host).
- **`qRcmd` (upstream).** Reply parsing is non-standard: `OK` = no output,
  `OK<hex>` = output, `E<hex>` = error, anything else printed raw
  [`up:231-269`]. **But** upstream also handles standard `O<hex>` console
  packets by decoding and printing them [`up` `process_packet` `case 'O'`],
  and `OK` alone is taken as the response. ⇒ the server replies to `qRcmd`
  with zero or more `O<hex>` packets followed by `OK` — the **standard** form
  (gdb manual, General Query Packets: "`O output` … may be repeated") — which
  upstream prints correctly and a real gdb would too. Errors are `E01` (no
  text — z88dk would try to hex-decode the text and, for odd lengths, print
  garbage).
- **`-x` symbol file** is a z80asm/zcc **`.map`** (`read_symbol_file`
  [`z88dk/src/ticks/syms.c:90-`] parses `name = $addr ; …` lines; sections from
  `__*_head/_size`). Produced by adding `-m` to the `zcc` line (verified:
  `demo/magic_bp_demo` + `-m` → 5353-line `mbp.map` with `_main = $816A`,
  §7.3). The client resolves `break _main` itself and sends `Z0,816a,1`; the
  server never sees a symbol. (`debugger_read_symbol_file` [`debugger.c:300-330`]
  additionally uploads a sibling `.bin` via `M` on connect, but only for a map
  given as a *positional* argument in `--interpreter=mi2` mode [`:1304`,
  `:1322-1326`]; the `-x` path does not.)

### 1.3 Two client versions, one server

| Difference | v2.4 (this host) | upstream master | Server policy |
|---|---|---|---|
| Max reply size | 1023 B | 16 KiB | ≤ 1022 B `target.xml`; chunk nothing else beyond 64 hex |
| `monitor` | absent | `qRcmd` | serve `qRcmd` (§4.3) |
| `T` signal parse | ignored | decimal 2 chars | always `T05` |
| `O` packets | would be taken as a response | printed | emit `O` only inside a `qRcmd` exchange (§4.3) |
| Serial `-d` | — | yes | out of scope (socket only) |

---

## 2. Packet table

Classes: **S** served (mapped to a CAP), **D** declined by design (RSP can
express it, jnext deliberately does not serve it — reply is still an honest
empty/`E` reply), **U** unsupported (empty reply `$#00`). "Generic minimum" is
what the gdb manual calls the minimum a stub must support (`?`, `g`, `G`,
`m`, `M`) plus the handful a stock gdb sends at connect; served so that a
self-built gdb with the z80 target (`--enable-targets=all`) is not rejected
at the door — **not validated**, see §7.4.

| # | Packet | Class | Reply | Backend CAP | Notes |
|---|---|---|---|---|---|
| 1 | `qSupported[:…]` | S | `PacketSize=4000;qXfer:features:read+;swbreak+;hwbreak+` | — | Decimal-digits-only size (§1.2). No `QStartNoAckMode`, no `vContSupported`, no `ConditionalBreakpoints` (conditions declined), no `multiprocess`. |
| 2 | `qXfer:features:read:target.xml:<off>,<len>` | S | `l<xml>` (or `m…` if `<len>` < remaining) | — | The document of §3, ≤ 1022 B. Any other annex → `E00`. |
| 3 | `?` | S | `T05thread:1;` after the machine is paused | CAP-CTL-01, CAP-CTL-13 | If running: `pause()` then reply once the `Paused` edge is observed (§5.4). If already paused: reply immediately. |
| 4 | `g` | S | 14 × 4 hex, order of §3 | CAP-INS-01, CAP-INS-07 | Refused with `E01` if the machine is running (§6.3 policy pauses first, so in practice never). |
| 5 | `G<56 hex>` | S | `OK` | CAP-INS-01 `set_register` × 12 | Applies the 12 pairs only; `clockl/clockh` ignored (the client sends them as 0 — transcript §7.2). Wrong length → `E01`. |
| 6 | `p<n>` / `P<n>=<hex>` | S (generic) | 4 hex / `OK` | CAP-INS-01 | `n` in the §3 numbering; 12/13 (`clock*`) read-only → `P` replies `E01`. Out of range → `E01`. z88dk-gdb never sends these. |
| 7 | `m<addr>,<len>` | S | `<len>` × 2 hex | CAP-INS-02 `peek(Cpu)` | Logical CPU view (§4.1). `addr+len > 0x10000` → clipped to 0x10000 (the client never asks past it, `:384-386`). `len` > 4096 → `E01`. |
| 8 | `M<addr>,<len>:<hex>` | S | `OK` / `E01` | CAP-INS-02 `poke(Cpu)` | `E01` if any byte lands on a read-only page (backend returns `RefusedReadOnly`, REQ-gdb-6). |
| 9 | `X<addr>,<len>:<bin>` | S (generic) | `OK` / `E01` | CAP-INS-02 `poke(Cpu)` | Binary with `}` escaping; same rules as `M`. |
| 10 | `Z0,<addr>,<kind>` / `z0,…` | S | `OK` | CAP-EVT `Execute[addr,addr]`, `Stop`, owner = this client | `kind` ignored (Z80 has no breakpoint-instruction size). **`z0` of an unknown address replies `OK`** — the client sends `z0` for breakpoints it never registered (§7.2). Duplicate `Z0` at the same address is idempotent (`OK`, one subscription). |
| 11 | `Z1,…` / `z1,…` | S | `OK` | same as `Z0` | jnext has no distinction; RSP allows serving Z0 as Z1. Stop reason field: `hwbreak:;` is *not* emitted (see #17). |
| 12 | `Z2,<addr>,<len>` / `z2` | S | `OK` | CAP-EVT `MemWrite[addr, addr+len-1]` | Write watch; `len` ≥ 1. |
| 13 | `Z3,<addr>,<len>` / `z3` | S | `OK` | CAP-EVT `MemRead[…]` | Read watch. |
| 14 | `Z4,<addr>,<len>` / `z4` | S | `OK` | CAP-EVT `MemRead` + `MemWrite`, one RSP id → two subscriptions | Access watch. |
| 15 | `c` (no addr) | S | stop reply, later | CAP-CTL-02 | `c <addr>` → `P` semantics first (`set_register(PC)`) then run. A resume refused by the corruption gate (CAP-CTL-11) replies `E01` immediately and stays paused. |
| 16 | `s` (no addr) | S | `T05thread:1;` | CAP-CTL-03 | Synchronous in the backend; reply in the same `pump`. |
| 17 | `i<decimal-len>` | S (non-standard) | stop reply, later | CAP-CTL-06 `run_to(pc+len)` | RSP's `i` means "cycle step"; z88dk-gdb means "run to PC+len" (§1.1). Served as z88dk defines it. `i` with no number, or `i<addr>,<n>` → `E01` (we do not cycle-step). |
| 18 | `0x03` (raw byte) | S | `T02thread:1;` once paused | CAP-CTL-01 | Signal 2 = SIGINT, the RSP convention for an interrupt; both client versions treat any `T` as "stopped" (§1.2). |
| 19 | `D` | S | `OK` | CAP-SES-01 `detach(cid)` | Removes this client's subscriptions; per CAP-SES-01, resumes the machine iff it was paused *by this client* and no other arming client remains (otherwise the pause passes to one — GH #280 N1). Socket closed after the reply is flushed. |
| 20 | `k` | S (generic) | none (socket closed) | CAP-SES-01 | Same as `D` — jnext does **not** exit on `k`; "the exact effect is not specified" by RSP and killing the emulator from a debugger is not a feature anyone asked for. |
| 21 | `qRcmd,<hex>` | S | `O<hex>`… then `OK` / `E01` | CAP-INS-03/04/05/02(Page)/07, CAP-SYM, CAP-CTL-12 | Monitor vocabulary in §4.3. Unknown command → **`OK` with an `O` line "unknown monitor command; try help"**, not empty (empty would mean "qRcmd unsupported" and upstream would print nothing). |
| 22 | `H<op><tid>` | S (generic) | `OK` | — | Single thread; any tid accepted. |
| 23 | `qC` | S (generic) | `QC1` | — | One thread, id 1. |
| 24 | `qAttached` | S (generic) | `1` | — | "attached to an existing process" — so a gdb `quit` detaches instead of killing. |
| 25 | `qfThreadInfo` / `qsThreadInfo` | S (generic) | `m1` / `l` | — | |
| 26 | `vCont?` | U | empty | — | Deliberately: with `vCont` unsupported, gdb falls back to `c`/`s`, which is the served set. |
| 27 | `vCont…`, `vCtrlC`, `vRun`, `vAttach`, `vKill`, `R` | U | empty | — | `R`/`vRun` would map onto CAP-CTL-12; declined because RSP restarts imply "run the program again from its entry", which jnext cannot express for a NEX/TAP session without re-loading — a `monitor reset` exists instead. |
| 28 | `bc` / `bs` (reverse continue/step) | D | empty | CAP-ST-04 **declined** | z88dk-gdb never sends them (§1.1). A real gdb only sends them after `ReverseContinue+`/`ReverseStep+` in `qSupported`, which we do not advertise. The capability stays in the backend (owner principle); serving it later is `qSupported` + two packets over CAP-CTL-09, no backend change. |
| 29 | `Z0,addr,kind;<cond_list>` | D | `E01` | conditions **declined** | Never offered: `ConditionalBreakpoints+` absent from `qSupported`, so gdb evaluates conditions itself; z88dk-gdb has no syntax for them. |
| 30 | `QStartNoAckMode` | U | empty | — | Acks stay on (costless; the client ignores them). |
| 31 | `qOffsets`, `qSymbol`, `qTStatus`, `qXfer:*` other annexes, `QNonStop`, `qHostInfo`, `qProcessInfo` (lldb) | U | empty | — | |
| 32 | `T<tid>` (thread alive) | S (generic) | `OK` | — | |
| 33 | anything else | U | empty | — | Logged at `debug` level on the `remote` log channel with the raw packet, so a user can see what their client wanted. |

**Counts:** 23 served (18 of them exercised by z88dk-gdb or its generic
minimum; 5 generic-only), 2 declined by design, 8 unsupported classes (one
row each for the families). Zero packets served by reaching past
`jnext::dbg::Debugger`.

---

## 3. Target description and register packing

### 3.1 The document served (verbatim, 600 bytes)

```xml
<?xml version="1.0"?>
<target version="1.0">
<architecture>z80</architecture>
<feature name="org.gnu.gdb.z80.cpu">
<reg name="af" bitsize="16"/>
<reg name="bc" bitsize="16"/>
<reg name="de" bitsize="16"/>
<reg name="hl" bitsize="16"/>
<reg name="af'" bitsize="16"/>
<reg name="bc'" bitsize="16"/>
<reg name="de'" bitsize="16"/>
<reg name="hl'" bitsize="16"/>
<reg name="ix" bitsize="16"/>
<reg name="iy" bitsize="16"/>
<reg name="sp" bitsize="16" type="data_ptr"/>
<reg name="pc" bitsize="16" type="code_ptr"/>
<reg name="clockl" bitsize="16"/>
<reg name="clockh" bitsize="16"/>
</feature>
</target>
```

Line by line, against what the client parses (§1.2):

| Line | Why it is there | Why nothing more |
|---|---|---|
| `<?xml …?>` | sxmlc accepts it; harmless for gdb. | The `<!DOCTYPE target SYSTEM "gdb-target.dtd">` line is optional per the gdb manual ("can be omitted"), costs 42 bytes and was verified harmless (§7.1, `target_doctype.xml` connects), but the budget is 1022 bytes and every byte spent here is a byte a future register cannot have. Omitted. |
| `<target version="1.0">` | Root must be `target` (`target/architecture` XPath, `:1028`). | |
| `<architecture>z80</architecture>` | `strcmp(arch->text, "z80")` (`:1035`). | Exactly `z80`; not `z80n`, not `Z80`. |
| `<feature name="org.gnu.gdb.z80.cpu">` | The XPath filter is `feature[@name='*z80*']` (`:1044`): the name must contain `z80`. The `org.gnu.gdb.<arch>.<unit>` spelling is the gdb convention for a standard feature. | One feature only: the client counts regs across *all* matching features in document order, and a second feature would have to contain `z80` in its name too or be invisible — and invisible registers would still occupy `g` slots for a real gdb, desynchronising the two clients' packings. |
| `af bc de hl af' bc' de' hl' ix iy sp pc` | The 12 names in `register_mapping_names[]` (`:95-107`), in the order the client's own local emulator uses. The order is *ours* to choose (the client maps by name), but keeping the canonical order makes `p<n>` numbering readable. | `af'` uses a literal apostrophe: the client compares with `strcmp` against `"af'"`; an XML entity would not match. |
| `bitsize="16"` on every reg | The client assumes 16 bits for every reg (`* 4` hex chars, `:228`); a `bitsize="8"` reg would desynchronise `g`. | |
| `sp type="data_ptr"`, `pc type="code_ptr"` | For a real gdb (`$pc`/`$sp` typing); ignored by z88dk-gdb. | |
| `clockl`, `clockh` | The client's profiler reads `(clockh<<16)|clockl` as a deterministic tick counter when present (`:1152-1161`); jnext has one — `Emulator::monotonic_tstates()` [`src/core/emulator.h:500`, `emulator.cpp:7984`]. Served as the low/high 16 bits of that 64-bit count (wraps every 2^32 T-states ≈ 20 min at 3.5 MHz; the client uses differences, `profiler.c`). | |

**Deliberately absent: `i`, `r`, `iff1`, `iff2`, `im`, `memptr`, `halted`.**
Two reasons, both measured: (a) the client's `G` writes **0000** into every
register it does not recognise (`set_regs`, `:420`, `:483-487` — transcript
§7.2 shows `…00000000` for the clock pair), so exposing `I`/`R`/`IFF` as regs
means every `set hl 1234` at the z88dk prompt would also clear `I`, disable
interrupts and zero `R`; (b) they are 8/1-bit values and the client packs
every reg as 16 bits. They are reachable read/write through `monitor regs`
(§4.3) instead. **Register numbering** (for `p`/`P`): 0 = `af` … 11 = `pc`,
12 = `clockl`, 13 = `clockh` — the document order, per the gdb rule "regnum
defaults to one greater than the previous register".

### 3.2 `g` / `G` encoding

- `g` reply: 14 registers × 2 bytes, each little-endian, hex — 56 characters.
  Byte 0 is `F`, byte 1 is `A` (`AF` little-endian), matching what
  `unwrap_reg` expects (`:242-244` with `:361-372`).
- `G`: 56 hex chars; the adapter applies bytes 0–23 as the 12 pairs through
  `set_register(RegId, value)` (CAP-INS-01) and ignores bytes 24–27. A `G`
  shorter or longer than 56 chars → `E01` (a generic gdb with a different
  `target.xml` cannot happen — it read ours).
- Source values: `Z80Registers{AF, BC, DE, HL, AF2, BC2, DE2, HL2, IX, IY, SP,
  PC, …}` [`src/cpu/z80_cpu.h:6-24`]; `clock` from CAP-INS-07
  `monotonic_tstates` (REQ-gdb-10, ACCEPTED with the correction that the
  accessor exists).

---

## 4. Memory model

### 4.1 `m`/`M`/`X`: the flat 64 K CPU view

`MemSpace::Cpu` (CAP-INS-02) is "what the Z80 sees now" — the live MMU
mapping including DivMMC/Multiface overlays and the Layer 2 write-through
window, resolved without the +3 floating-bus latch (finding F1 in
`backend.md` §2.2). That is the only address space RSP can name, and it is the
right one: the client's disassembler, stack walker and `x` command all read
"memory at PC/SP as the program sees it". There is no address-space
extension for RSP (the `qXfer:memory-map` annex describes *regions*, not
banks, and z88dk-gdb does not read it), so **no attempt is made to encode a
bank in the address**: `0x00000`–`0x0FFFF` is the CPU view, full stop.

A write to a ROM-mapped or otherwise read-only page replies `E01`; the client
prints "Warning: Cannot restore file at addr …" (`:696`) and stops the upload.
Today `Mmu::write` silently drops ROM writes; the backend's `poke` checks
before writing (REQ-gdb-6, ACCEPTED).

### 4.2 What is unreachable through RSP proper, and said so

Physical pages other than the eight mapped ones, ROM images not mapped,
NextREGs, ports, MMU slot assignments, the raster position, sprites, copper,
palette, AY, rewind, screenshots, input injection. None has an RSP packet.
They are either reachable through `monitor` (the next section) or not at all
(sprites/copper/palette/AY/rewind/screenshots/input — DZRP, ZRCP, the DSL and
the GUI carry those; RSP is the developer's *code* debugger).

### 4.3 `qRcmd` — the monitor vocabulary

`monitor <cmd>` is the sanctioned RSP escape hatch for target-specific
commands (gdb manual: "Remote Serial Protocol … sends `qRcmd` for `monitor`
commands"). It keeps RSP's worldview pure — nothing here bends `m`/`g` — and
it is the *only* place Next-specific state appears. Output is `O<hex>` lines
then `OK`; errors `E01`. Numbers accept `0x…`, `$…` or decimal; the smallest
useful set, each mapped to a CAP:

| Command | Output | CAP |
|---|---|---|
| `help` | the list below | — |
| `regs` | `I=xx R=xx IFF1=n IFF2=n IM=n HALT=n MEMPTR=xxxx` (+ `PC/SP/…` for completeness) | CAP-INS-01 |
| `set <i\|r\|iff1\|iff2\|im> <val>` | `OK` | CAP-INS-01 `set_register` |
| `mmu` | 8 lines `slot n: page pp (effective ee) [ROM]` + `7FFD/1FFD/DFFD` | CAP-INS-03 |
| `mmu <slot> <page>` | `OK` | CAP-INS-03 `set_mmu_slot` |
| `nextreg` / `nextreg <reg>` / `nextreg <reg> <val>` | 256-entry dump / one value / `OK` | CAP-INS-04 `nextreg_peek` (side-effect free) / `nextreg_write` |
| `page <n> <off> [len]` | hex dump of physical 8 K page `n` (default 16 bytes) | CAP-INS-02 `peek(Page{n})` |
| `in <port>` / `out <port> <val>` | value / `OK` | CAP-INS-05 — **perturbing** (a port read has side effects); documented as such in the help text. This gives z88dk-gdb's own `out` command (a no-op over gdb, `:520`) a working equivalent. |
| `sym <name>` / `sym <addr>` | `name = $addr` / nearest symbol | CAP-SYM (jnext's own loaded map, GUI **Map** menu or `--map` if the backend adds a CLI row — the client has its own `-x` table, so this is for cross-checking) |
| `time` | `frame=N cycle=M tstates=T vc=.. hc=..` | CAP-INS-07 |
| `reset [soft\|hard]` | `OK` / `E01` | CAP-CTL-12. **`soft`**: `Emulator::soft_reset()` runs inline [`src/core/emulator.h:198`], the machine stays paused, the next `g` shows the post-reset state. **`hard`** is different and was wrong in v1 (review R-2): today `request_hard_reset()` only *records* a flag [`emulator.h:199-208`] and the loop owner performs `emulator_cold_boot()` **after the tick's frames** [`sdl_app.cpp:409`, `qt_app.cpp:510`, `headless_app.cpp:691`], destroying and reconstructing the `Emulator`; the transient run state "is intentionally not restored — the machine starts fresh and running" [`src/platform/emulator_boot.h:122-124`]. An `OK` in the same pump would therefore be answered against the old machine, and one tick later the pause (and, absent a backend contract, this client's `Z0`s) would be gone. So the verb is served under the **CAP-CTL-12 Hard reconstruct contract** (REQ-zrcp-15, ACCEPTED in backend.md v4; driver registered by the loop owner via CAP-SES-07): `reset(Hard)` from inside `pump` runs the loop owner's cold-boot sequence *before it returns*, the backend re-binds to the reconstructed `Emulator` and re-applies every client's subscriptions and settings, **paused stays paused** (PC = 0x0000 of `nextboot.rom`), and `Reset{Hard}` reaches every listener before the call returns. What the client then observes: `monitor reset hard` → `O`-line "hard reset: machine at PC=0000 (nextboot.rom), still stopped" + `OK`; its next `g` shows PC=0000 and its breakpoints are still listed by `monitor bp`. No stop reply is emitted (the client did not resume, so none is owed — §5.4). With no driver registered (a bare test harness) the backend returns `RefusedUnavailable` and the adapter answers `E01` with an `O`-line "hard reset not available over this connection" — never a reset that lands late. A **guest-initiated** hard reset (NR 0x02) keeps the deferred path and reaches the adapter as the same `Reset{Hard}` (CAP-SES-07 `on_cold_boot_begin()` / `on_cold_boot_done()`, so a stopped client stays stopped across it too). **Backend round-2 decision (final):** a reset never pauses a running machine and there is no `Reset` pause reason — paused stays paused at PC 0, running stays running; `Reset{Hard}` is a listener event only. Adapter policy: a client mid-run (`c`/`i` outstanding) gets **nothing** until a real stop — "stopped means stopped" is preserved and the client sees the fresh machine when it next stops; a stopped client sees nothing either (its next `g` reads PC=0000). The loop owner's flag poll precedes `pump()` in every loop, so a guest reset and a client `monitor reset hard` in one tick run in that order. |
| `bp` | list all subscriptions with owner (this client / gui / dsl / …) | CAP-INS-17 |

Not offered (and why): `save/load state` (bookmarks are DZRP's model, and a
`monitor` bookmark is a second wire for the same thing), `screenshot` (the
DSL/CLI own it), `step-back` (owner principle: offered by the backend, but a
monitor verb is not "the client deciding to use it" — it would be jnext
inventing a reverse-debug UI inside a client that has none; revisit if a user
asks).

---

## 5. Breakpoints, stepping, stop replies, interrupts

### 5.1 Who inserts what

| Concern | Owner | Why |
|---|---|---|
| Instruction length for `nexti` | **client** (its disassembler, `disassemble2`, `:626`) — sent as `i<len>` | z88dk decided it; the server only runs to `pc+len`. |
| Temp breakpoint to step *off* a breakpoint at PC on `c` | **backend** — `DebugState::step_off_pending_` armed on the paused→running edge [`src/debug/debug_state.h:301`, `unpause_()`], consumed exactly once before the first `should_break` [`consume_step_off()` `:206` and its comment block; `src/core/emulator.cpp:9319-9325`] | Already exists (GH #221); the adapter inserts nothing. Real gdb *also* steps off itself (`s` then `c`), which composes: a `s` from a paused machine is CAP-CTL-03, unaffected. |
| Temp breakpoint for `finish` | **client** (`Z0` at the return address + `c`, `:1630-1639`) | |
| `step`/`next` at source-line granularity | **client** (loops `s`/`i` until the line changes, `breakpoints.c:222-247`) | Each iteration is one round trip — slow over 20 ms ticks (§6.2), acceptable. |
| Client-side "breakpoints" (`memory8/16`, `register`) | **client** — evaluated at every stop, never on the wire | Nothing to serve; the `z0` they emit on quit must reply `OK` (row 10). |
| Original opcode under a `Z0` | **nobody** — jnext breakpoints are PC-compare, not opcode patching (`BreakpointSet::should_break`), so memory reads through `m` never show a trap byte. | Strictly better than a patching server: the client's disassembly is always the real code. |

### 5.2 Steps

- `s` → CAP-CTL-03 `step_into()`: one instruction, `Emulator::debugger_step()`
  semantics — frame bookkeeping included, and a HALT is run out (GH #207,
  `emulator.h:751-761`). Synchronous: the reply `T05thread:1;` is sent in the
  same `pump()`.
- `i<len>` → CAP-CTL-06 `run_to((pc + len) & 0xFFFF)`: a one-shot `Execute`
  outside the master switch, **owner = internal** (so it never appears in the
  Qt breakpoint panel — design-qt's condition, same as the GUI's own Step Over
  one-shot), all other breakpoints live (so a breakpoint inside the called
  routine still stops first, which is what `nexti` means in gdb too).
  Asynchronous: reply when `Paused{reason: RunTo(id)}` (or any other pause)
  is observed.
- `c` → CAP-CTL-02 `run()`. Asynchronous.
- No `step_out` (CAP-CTL-05): RSP has no packet; z88dk's `finish` is
  client-side. No `run_to_end_of_frame/scanline` (CAP-CTL-08): no packet;
  `monitor` does not add one because the client cannot show a raster anyway.

### 5.3 Stop replies

Always the `T` form (the client dispatches only on `T`, §1.2). Signal 05
(SIGTRAP) for every debugger-caused stop, 02 (SIGINT) for a `0x03`
interrupt or a pause by another frontend — gdb prints "Program received signal
SIGINT", z88dk ignores the number. The `n:r` pairs, built from
CAP-CTL-13 `state().pause_reason` (REQ-gdb-9, ACCEPTED):

| `pause_reason` | Reply |
|---|---|
| `Breakpoint(id)` where id is a Z0/Z1 of this client | `T05thread:1;swbreak:;` |
| `Watch(id, kind, addr)` from a Z2 | `T05thread:1;watch:<addr hex>;` |
| … Z3 / Z4 | `rwatch:` / `awatch:` |
| `Step`, `RunTo(id)` (our one-shot) | `T05thread:1;` |
| `User(cid == us)` (our `?`/`0x03`) | `T02thread:1;` |
| `User(other)`, `Magic`, `Script`, `Breakpoint/Watch` owned by another client, `Corrupt` (a guest hard reset is NOT a stop — §4.3) | `T02thread:1;` — "something else stopped it"; the client shows the PC and the user reads the GUI/log for why |

`swbreak+`/`hwbreak+` are advertised in `qSupported` so a real gdb accepts
the `swbreak:` field; z88dk-gdb ignores fields. Address in `watch:` is the
faulting address from `Paused.matched[]` (`Hit{event_id, addr, access,
value}`, backend.md v3 §4.3 / CAP-SES-02); when several of this client's
subscriptions matched at one stop the adapter reports the first `Hit` whose
`event_id` it owns (RSP carries one reason). The adapter uses `addr` only. PC in the reply is *not* included (the client always
re-reads `g`).

### 5.4 Interrupt and the "no spontaneous stop reply" invariant

A stop reply may only be sent as the reply to `c`, `s`, `i`, `?` or `0x03`
(§1.2: the client would otherwise consume it as the answer to an unrelated
request). The adapter therefore keeps one bit, `stop_reply_owed`, set by those
five inputs and cleared when the reply goes out. On each `pump()`:

1. Drain the socket, decode complete packets, serve each synchronously.
2. If the backend delivered `Paused{…}` since the last pump **and**
   `stop_reply_owed`, emit the §5.3 reply, clear the bit.
3. If `Paused` was delivered and the bit is **not** set — the GUI or the DSL
   paused the machine while the client believed it was stopped already, or
   before it ever resumed — send nothing. The client's model ("stopped") is
   already right; its next `g` gets fresh registers.
4. If the machine is *running* and the client thought it stopped (a GUI Run
   happened behind it): the next inspection packet (`g`/`G`/`m`/`M`/`X`/`p`/
   `P`/`Z`/`z`/`qRcmd`) **pauses first** (CAP-CTL-01), then is served, then — since no
   reply is owed — nothing else. Logged at `info`: "gdb client re-paused the
   machine (resumed by <cid>)". This keeps RSP's stopped/running model
   consistent from the client's side without an ownership lock (agreed model,
   §6.3).

**Why the same-pump reply to `?` is load-bearing (review N-12).** The client
starts with `debugger_active = 0` [`z88dk/src/ticks/debugger.c:206`] and its
main loop only enters the prompt — and only then sends `g` — once
`debugger_active` becomes 1, which happens solely when a `T` packet arrives
[`debugger_gdb.c:1357-1363`, `:790-803`]. So the `T` for `?` is the single
event that starts the session, it must be emitted **exactly once**, and it
must be the first thing the adapter sends after the XML: the client sends
`g` immediately after it and takes the next packet as `g`'s reply. Rule 2
above gives exactly that when the machine is already paused (reply in the
same pump); when it is running, CAP-CTL-01 takes effect at the next
instruction boundary and the reply waits for the `Paused` edge — during
which rule 3 guarantees nothing else is sent.

`0x03` while paused: reply `T02thread:1;` immediately (the client sends a
*temporary* break to add a breakpoint while it believes the machine runs,
`breakpoints.c:46-49`; if we are already paused the answer is simply "yes,
stopped").

---

## 6. Transport, loop ownership, arbitration, CLI

### 6.1 Transport

- **TCP, IPv4/IPv6, listener bound to `127.0.0.1` by default**, one client at
  a time (RSP is a single-session protocol; a second connection is accepted
  and closed with nothing sent, logged at `warn`). Non-blocking throughout,
  through the **public** seam of the existing portable socket layer:
  `esp::EspListener` / `esp::EspTransport` obtained from
  `esp::make_socket_listener(bind_address)`
  [`src/esp01/include/esp01/esp_socket.h:258`, `:509`, `:561`] — *not* the
  primitives in `esp_socket_platform.h`, which declares itself PRIVATE
  ("nothing outside src/peripheral should ever include it", [`:10-12`]).
  Same Windows-twinned code, same "bind exactly what you were given" policy;
  if reuse from `src/remote/` proves awkward the lift is `esp::net` →
  `src/net/`, one shared REQ (agreed with design-dzrp 2026-09-26). No new
  socket code; no thread.
- **Framing layer** (`src/remote/gdb/rsp_codec.{h,cpp}`, pure, testable):
  `$…#xx` parse with checksum, `+` emission, `}`-escape decode for `X`,
  `}`/`*` escape encode for `qXfer` payloads (only `#`, `$`, `}`, `*` need
  it; the XML of §3 contains none), run-length encoding **not** used on
  output (optional per RSP; the client does not decode it — a `*` in a reply
  would be taken literally; and gdb treats `*` as RLE only in replies, so
  emitting none is safe both ways). Receive buffer 16 KiB (`PacketSize=4000`
  read as hex by gdb = 16384 + framing); a packet exceeding it is dropped
  with `-` and logged.
- **Loop ownership**: `Debugger::pump(max_wait_ms)` (CAP-SES-03) is called by
  the loop owner once per tick, running **and** paused, in all three
  frontends; the RSP server is a registered service, its `poll()` runs inside
  `pump`. Headless replaces its paused busy-spin with `pump(50)` (backend.md
  §5). Nothing in the adapter runs outside `pump`.

### 6.2 Latency

One round trip per tick: ≤ 20 ms at 100 % speed running, and while paused
the Qt/SDL ticks keep firing at the same cadence. `s` is synchronous
(CAP-CTL-03) and replies inside the same `pump()`, so z88dk's `stepi` costs
one tick. `i<len>` and `c` stop *during* the tick's frames; with `pump()`
called only before the frame batch, that stop would be reported one tick
late (two ticks per `nexti`). **Agreed shared model (design-dzrp amendment,
2026-09-26, mirrored as REQ-dzrp-8/9):** the loop owner also flushes
notifications after the frame batch in the same tick, and `pump()` returns
"a remote client is attached and the machine is paused" so the loop owner may
shorten its paused tick (needs-prototype on the DZRP side; RSP works at the
normal cadence and merely benefits). **design-zrcp's amendment (REQ-zrcp-01,
agreed):** while paused, `pump()` *drains* — after answering a packet it waits
~2 ms for the next complete one and answers it too, until quiet or a ~10 ms
budget. RSP benefits identically: one z88dk `stepi` is `s` + `g` + two or
three `m` reads (transcript §7.1), i.e. 4–5 sequential round trips, which is
~100 ms at one packet per 20 ms tick and ~1 tick with draining. A `next` over
a source line that spans k instructions is k such sequences — fine for a
human at a prompt. Nothing in this adapter assumes the cadence.

### 6.3 Arbitration with the Qt debugger and other frontends

One `DebugState`, no ownership token, last verb wins (backend.md §4.1
"N attached"). The RSP adapter's obligations under that model are exactly
§5.4 rules 2–4. The Qt side needs nothing new: `check_breakpoint_hit()`
already detects a pause it did not cause and opens the window
[`src/debugger/debugger_manager.cpp:682-706`]; breakpoints set over RSP are
subscriptions in the shared table and appear in the breakpoint panel with
their owner (CAP-INS-17). On `D` or socket loss, CAP-SES-01 removes this
client's subscriptions and releases a pause *it* holds.

### 6.4 CLI rows (`src/core/cli_options.h`; `make cli-check` gates the man page)

```cpp
GdbPort,             // OptId
DebugListenAddress,  // OptId (shared with --dzrp-port / --zrcp-port)
…
{ "--gdb-port", 1, Doc::Documented, OptId::GdbPort,
  "PORT",
  "Serve the GDB remote protocol (z88dk-gdb) on TCP PORT.\n"
  "Off unless given; PORT 0 binds an OS-chosen port and logs it." },
{ "--debug-listen-address", 1, Doc::Documented, OptId::DebugListenAddress,
  "ADDR",
  "Bind address for the debugger protocol ports (--dzrp-port,\n"
  "--zrcp-port, --gdb-port). Default 127.0.0.1. A non-loopback\n"
  "address exposes the debugger to the network: none of these\n"
  "protocols has any authentication." },
```

**One port rule for the three `--<proto>-port` rows** (review R-5; proposed
by design-dzrp, adopted verbatim by design-zrcp and here): *absent = off;
`0` = an OS-chosen ephemeral port, the bound port logged at startup as
`gdb: listening on 127.0.0.1:NNNNN`; any other value = that port.*
`EspListener::open(0)` already contracts exactly that ("`port` 0 asks the OS
to choose, and `port()` then reports what it chose",
[`src/esp01/include/esp01/esp_socket.h:518-527`]), and the regression rows
need a collision-free port. v1's "`0` = off" is withdrawn: it duplicated
"absent". The `--debug-listen-address` row above is the one shared text
(whoever lands first adds it; the others reuse the `OptId`); spelled after
the existing `--esp-listen-address` [`src/core/cli_options.h:414-416`].

Both documented in `doc/man/jnext.1.md` OPTIONS in the same change (the
cli-check diff is bidirectional). Also a `Settings → Preferences` row is
**not** proposed: a listening debug port is a per-run choice, not a
persistent preference (and `app_config` never applies to headless).
`--debug-listen-address 0.0.0.0` is the user's explicit choice to expose an
unauthenticated debugger on the LAN; the man page says so in one sentence.

---

## 7. Validation

### 7.1 Done now, against the real client (design-time)

Stub: `scratchpad/gdb/fake_server.py` (Python, ~100 lines) serving exactly
§2's replies and §3's XML; client `z88dk/bin/z88dk-gdb` (v2.4 build on this
host). All runs 2026-09-26 with `timeout --kill-after=2s 8s`.

| Run | Input | Result |
|---|---|---|
| 1094-byte `target.xml` (DOCTYPE + `type`/`group` attrs) | connect | **client SIGSEGV** (exit 139) right after `qXfer` — the `recv_data[1024]` overflow of `:853`. Design consequence: ≤ 1022 B. |
| 600-byte XML (§3.1) | `reg`, `x/8 0x8000`, `break 0x8010`, `stepi`, `nexti`, `cont`, `breakpoints`, `quit` | exit 0. Client log: `Registers:  af bc de hl af' bc' de' hl' ix iy sp pc clockl clockh`, `Remote has 'clock' register.`, `Execution stopped`, registers printed correctly incl. `clockh=0001, clockhl=2345`, breakpoint added, `s`/`c` stops shown. Wire: `qSupported`, `qXfer…:0,3fff`, `?`, `g`, `m7ffc,20`, `g`, `mfefd,20`, `m7ffc,20`, `Z0,8010,1`, `s`, `g`, `s`, `g`, `c`, `g`, `z0,8010,1`, `D`. |
| 642-byte XML with DOCTYPE | same | exit 0, identical — the earlier crash was size, not the DOCTYPE. |
| stub memory `CD 00 90` at PC, `ED B0` next | `nexti`, `nexti`, `set hl 0x1234`, `reg`, `break memory8 0x8000 = 5`, `break register a = 1`, `finish`, `out 0xfe 1`, `quit` | Wire: **`i3`** for the CALL, **`i2`** for LDIR (decimal lengths, confirmed); `G440034127856341211112222333344445555666600ff058000000000` — HL=1234 written, **clock pair sent as 0000 0000** (confirmed: server must ignore); `break memory8`/`register` and `out` sent **nothing**; on quit **`z0,8000,1` and `z0,a,1`** were sent for those client-side entries (confirmed: `z0` must be idempotent-OK); `finish` → "return address is unknown" (no symbols); `D`. |
| `-x mbp.map` (built from `demo/magic_bp_demo` with `-m`) | `break _main`, `cont`, `bt`, `quit` | `Adding breakpoint at '_main' $816a (_main)` → wire `Z0,816a,1`, `c`; stub stops at 816a → `Hit breakpoint 1: @816a (_main)`, source line shown, `bt` prints `_main+0 at magic_bp_demo.c`; `z0,816a,1`, `D`. Symbol resolution is entirely client-side; the map is z88dk's own `-m` output. |

Not exercised at design time: Ctrl-C (needs a TTY signal; the raw `0x03`
path is read from `:537-538` and served by the stub), `restore` (`M`), the
upstream `monitor` (no upstream binary on this host — the upstream source was
read, §1.2).

### 7.2 The #281 acceptance script (regression row `gdb-z88dk-func`)

Shape follows `test/00regression/scripts/magic-port-func.sh` (sourced by the
harness; **no `trap`**, every `timeout` with `--kill-after`). Fixture: a
demo built with `-m` so the `.map` is checked in next to the `.nex` under
`test/00regression/nex/` (license-clean: our own demo). Steps:

```bash
# 1. jnext headless, gdb server on an OS-chosen port (--gdb-port 0, §6.4),
#    demo loaded, generous exit bound
"$JNEXT" --headless "${SD_CARD_ARGS[@]}" --gdb-port 0 \
    --load "$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex" \
    --delayed-automatic-exit 20 >"$TMP_DIR/jnext.log" 2>&1 &
jnext_pid=$!
# 1b. wait for the listener (review N-8): the same 100 x 0.1 s poll as the
#     ready-file handshake in esp-server-func.sh:92-104, but on jnext's own
#     "gdb: listening on 127.0.0.1:NNNNN" log line, which also yields the port.
for _ in $(seq 1 100); do
    port=$(sed -n 's/.*gdb: listening on 127\.0\.0\.1:\([0-9]*\).*/\1/p' "$TMP_DIR/jnext.log" | head -1)
    [[ -n "$port" ]] && break
    kill -0 "$jnext_pid" 2>/dev/null || break
    sleep 0.1
done
[[ -n "$port" ]] || { fail_row " (gdb server never listened)"; }
# 2. the real client, scripted through stdin, verbose so the wire is in the log
printf 'break _main\ncont\nreg\nx/16 _main\nstepi\nnexti\nmonitor mmu\nmonitor nextreg 0x07\nquit\ny\n' \
  | timeout --foreground --kill-after=5s 15s "$Z88DK_GDB" -v -h 127.0.0.1 -p "$port" \
      -x "$PROJECT_DIR/test/00regression/nex/magic_bp_demo.map" >"$TMP_DIR/gdb.log" 2>&1
# 3. assertions on the client log (what the USER sees), not on jnext's log:
grep -q "Registers:  af bc de hl af' bc' de' hl' ix iy sp pc clockl clockh" gdb.log
grep -q "Hit breakpoint 1: @816a (_main)" gdb.log        # the address from the map
grep -q "^pc=816a" gdb.log                                 # g after the stop
grep -q "w: i[0-9]" gdb.log || grep -q "w: s" gdb.log       # nexti used the i packet on a CALL, s otherwise
grep -q "slot 0: page" gdb.log                              # monitor output reached the client
```

**How `break _main` + `cont` reaches `_main` although `?` paused the machine
at connect (review N-8).** The NEX loader arms a boot hold of N frames; the
countdown is decremented **only when `run_frame()` completes a frame**
[`src/core/emulator.cpp:9469-9470`], and a paused `run_frame()` returns before
executing anything [`:9304-9305`] — so the hold does not tick while the client
is at its prompt. `?` pauses the machine somewhere in the held frames (or
before them), `Z0,816a,1` is registered while paused, and `c` resumes: the
remaining held frames run out, the NEX entry runs, `_main` is reached and the
breakpoint fires. The row therefore does not depend on how quickly the client
connects. (Same mechanism DZRP's WP-5 relies on: "no frame-countdown
decrement while paused".)

Skipped (SKIP, declared) when `z88dk-gdb` is not on the host — the harness
already has the `want`/`skip_row` idiom; the binary's path comes from
`Z88DK_GDB` with a default of `$HOME/src/spectrum/z88dk/bin/z88dk-gdb`
(`reference_z88dk_local_install`: z88dk is a local install here, never
docker). Since `regression.sh` runs the full declared suite in CI, the row
must SKIP cleanly there rather than fail — CI has no z88dk.

### 7.3 Unit suite `gdb_rsp_test` (Qt-free, `test/remote/`)

Over the backend's fake `Transport` (backend.md §7): push bytes, read bytes.
Rows (IDs literal, one per line — the traceability rules):

- **GDB-FRM-01..06** framing: checksum accept/reject, `+` emitted, `}` decode
  in `X`, oversize packet → `-`, `0x03` outside a packet, garbage before `$`.
- **GDB-SUP-01..03** `qSupported` reply contains exactly the four stanzas; the
  size is decimal-digit-only; `qXfer` reply is `l` + the 600-byte document,
  and **the reply is < 1023 bytes** (the row that guards the v2.4 crash).
- **GDB-REG-01..08** `g` = 56 hex LE with AF first; `G` applies 12 pairs and
  leaves `I/R/IFF/IM` untouched (the zero-clobber row); `p12`/`p13` are the
  low/high of `monotonic_tstates`; `P12=` → `E01`; bad length → `E01`.
- **GDB-MEM-01..06** `m` reads the CPU view through a remapped slot (set MMU
  slot 6 to two different pages, read 0xC000 twice); `M` into RAM → `OK`,
  readback matches; `M` onto a ROM slot → `E01` and memory unchanged; `X`
  binary with escapes; clip at 0x10000.
- **GDB-BP-01..10** `Z0` then `c` → `T05…swbreak:;` at the address (machine
  actually stopped — the #203 shape); `z0` unknown → `OK`; duplicate `Z0` one
  subscription; `Z2` range hit on the last byte and one past (both edges);
  `Z3`, `Z4`; `watch:` carries the faulting address; `D` removes only this
  client's subscriptions (one set by a fake second client survives).
- **GDB-STP-01..05** `s` replies in the same pump with PC+len; `i3` on a CALL
  stops at PC+3 after the callee ran (control row: the same program with `s`
  stops inside the callee); `c` from a PC with a `Z0` on it steps off (no
  immediate re-stop); `c` refused by the corruption gate → `E01`.
- **GDB-STOP-01..04** the §5.4 invariant: a `Paused{by: other}` with no reply
  owed sends nothing; `?` while running pauses and replies `T05`; `0x03`
  while paused replies `T02` at once; inspection packet while running pauses
  first.
- **GDB-MON-01..06** `qRcmd` for `mmu`, `nextreg 7`, `regs`, `page`, unknown
  command (→ `O…` + `OK`, never empty), `reset soft`.
- **GDB-UNS-01..03** `vCont?`, `bc`, `QStartNoAckMode` → empty reply.

Count target ~55 rows; the exact pin goes into `test/unit-tests.conf` when
the suite exists. Every row derives from the client source or the RSP manual
cited in §1–§5, not from the adapter code.

### 7.4 The subset claimed, and what is *not* validated

**Claimed:** "jnext serves the GDB remote protocol subset that `z88dk-gdb`
(v2.4 and current master) uses — registers, memory, software breakpoints,
step/next/continue, interrupt, detach, `monitor` — plus RSP watchpoints and
the generic stub minimum. Target: ZX Spectrum Next CPU view; Next-specific
state through `monitor`." **Validated against:** the real `z88dk-gdb` v2.4
binary (design-time stub transcripts above; the #281 regression row once
implemented). **Not validated:** (a) upstream-master `z88dk-gdb` — read, not
run; its `qRcmd` handling is designed from source and must be re-run when a
newer z88dk lands here; (b) a real gdb built with the z80 target — the
generic rows exist so it is not rejected at connect, but gdb's `z80-tdep.c`
may impose its own register naming/numbering on top of `target.xml`, which
was not checked; if it does, a second `target.xml` variant selected by the
presence of a feature list in `qSupported:` (z88dk sends none) is the fix, and
it is adapter-local. Neither is a blocker for #281's user-facing claim.

---

## 8. REQ ledger (backend v1)

All sent 2026-09-26, all answered by `design-backend` the same evening.

| REQ | Capability | Status | CAP |
|---|---|---|---|
| REQ-gdb-1 | pause, synchronous from the host loop, idempotent | ACCEPTED | CAP-CTL-01 |
| REQ-gdb-2 | resume; step-off owned by the backend | ACCEPTED | CAP-CTL-02 (GH #221 arm stays in `DebugState`) |
| REQ-gdb-3 | step one instruction, HALT run-out, synchronous | ACCEPTED | CAP-CTL-03 |
| REQ-gdb-4 | run until PC == given address (`i<len>`) | ACCEPTED | CAP-CTL-06 `run_to(pc+len)` |
| REQ-gdb-5 | registers get; **per-register** set (partial `G`) | ACCEPTED | CAP-INS-01 `set_register(RegId, v)` |
| REQ-gdb-6 | bulk peek/poke of the live CPU view; poke reports read-only | ACCEPTED | CAP-INS-02 `MemSpace::Cpu`, `poke` → count + `RefusedReadOnly` (backend checks before writing; v2 §4.2) |
| REQ-gdb-7 | unconditional PC breakpoints | ACCEPTED | CAP-EVT `Execute[a,a]` Stop, `EventId` ↔ `Z0` |
| REQ-gdb-8 | range watch read/write/access, faulting addr at stop | ACCEPTED | CAP-EVT `MemRead/MemWrite[lo,hi]`; payload addr; same primitive as the DSL's (design-dsl concurs) |
| REQ-gdb-9 | stop reason with kind/addr, and "who paused" | ACCEPTED | CAP-CTL-13 `pause_reason` ∈ {User(cid), Breakpoint(id), Watch(id,kind,addr), Step, RunTo(id), Magic, Corrupt, Script(id)} |
| REQ-gdb-10 | monotonic T-state counter | ACCEPTED, **with correction** | CAP-INS-07 — backend first proposed `master_cycle / divisor`; corrected to expose `Emulator::monotonic_tstates()` [`emulator.h:500`, `emulator.cpp:7984`], because the divisor changes at runtime under NR 0x07 and a derived quotient is not monotonic across a speed change. Backend to reflect in v2. |
| REQ-gdb-11 | NextREG peek/write, MMU slots, physical page peek, symbols | ACCEPTED | CAP-INS-04, -03, -02 `Page{n}`, CAP-SYM |
| REQ-gdb-12 | per-client breakpoint ownership; merged visibility | ACCEPTED | CAP-EVT-09 owner + CAP-SES-01 `detach`, CAP-INS-17 list |
| REQ-gdb-13 | pump hook, running and paused, single thread | ACCEPTED | CAP-SES-03 `pump(max_wait_ms)`; adapters register as services. Folded in (no new REQ): design-dzrp's REQ-dzrp-8 (post-frame notification flush in the same tick) and REQ-dzrp-9 (`pump` reports "remote attached and paused" so the owner may shorten the paused tick) — RSP benefits, does not require them. |
| REQ-gdb-14 | paused/resumed edges with origin, same thread | ACCEPTED | CAP-SES-02 listener, delivered inside `pump` |
| REQ-gdb-15 | run-state query | ACCEPTED | CAP-CTL-13 `state().paused` |

**Cross-frontend agreements recorded:** design-dsl — same range-watch
primitive, payload `{addr, value, phys_page, pc_pre_exec, cycle, frame}`,
predicate slot left empty by RSP. design-qt — Qt adapter attached for the
process lifetime and receives every `Paused{by, reason}` (as built by GH #278
WP2: attached only while its window is open, because an attach arms the
machine; it pulls the pause state every tick, so it still reacts to every
pause, gdb's included — `qt-frontend.md` §4.1); gdb-owned
subscriptions shown read-only with their owner in the Breakpoints panel;
transient one-shots are `owner=internal`. design-dzrp — shared transport,
notification timing and CLI spelling as amended in §6; DZRP documents a
GUI-originated resume rather than re-pausing, RSP re-pauses on the next
inspection packet (§5.4) — both consistent with one pause state. design-zrcp
— same model; adds `pump` draining while paused (§6.2) and the rule that a
protocol's "disable all breakpoints" suspends only that client's
subscriptions, never the backend master switch (REQ-zrcp-03). RSP has no such
packet — `z`/`Z` are per-breakpoint — so the rule costs this adapter nothing,
and its `D` already touches only its own subscriptions (CAP-SES-01).

**MAPPED line sent:** 21 CAPs used, 24 declined (conditions; CAP-CTL-04/05/
07/08/09/10/11-modal; CAP-ST-01..04; CAP-INS-06/08/09/10/12/13/14/15/16;
CAP-IN-*; CAP-CAP-*; CAP-TIME-02/03; CAP-SES-04/05), 0 REQs open, **0
reach-arounds**.

---

## 9. Implementation work packages for #281 (parallel-safe)

All on one branch (`gh281-gdb-rsp`) per the multi-stage-issue rule, but the
packages are independent files and can be written by parallel agents; each
gets its own reviewer.

| WP | Deliverable | Depends on |
|---|---|---|
| WP-1 `rsp_codec` | `src/remote/gdb/rsp_codec.{h,cpp}`: framing, checksum, escapes, hex helpers; rows GDB-FRM-*. Pure, no backend. | nothing |
| WP-2 `target_desc` | the §3 document as a `constexpr` string + a unit row pinning its byte length ≤ 1022 and its register order against the `g` packer; `g`/`G`/`p`/`P` packing over CAP-INS-01/07. Rows GDB-SUP-*, GDB-REG-*. | backend CAP-INS-01/07 |
| WP-3 `rsp_server` | packet dispatch (§2 table), stop-reply state machine (§5.4), breakpoint id map, `qRcmd` vocabulary (§4.3); over the fake `Transport`. Rows GDB-MEM/BP/STP/STOP/MON/UNS-*. | WP-1, WP-2, backend CAP-CTL/EVT/SES |
| WP-4 wiring | `--gdb-port`/`--debug-listen-address` rows in `cli_options.h` + `main.cpp` switch + `jnext.1.md`; service registration in the three loop owners via `pump`; the socket comes from the shared transport package **T** (arch doc; `esp::make_socket_listener` seam), not an RSP-private listener. `make cli-check`, `make docs-man`. | WP-3, package T, backend CAP-SES-03 |
| WP-5 acceptance | regression row `gdb-z88dk-func` (§7.2) + `.map` fixture + `functional_tests.conf` count; user-guide chapter section "Debugging with z88dk-gdb" (source under `src/doc/user-guide`, re-rendered). | WP-4 |
| WP-6 upstream listing | after release: a z88dk wiki PR adding jnext to the compatible-servers list, with the `--gdb-port` one-liner. | shipped #281 |

Reviewer mutations to derive from the diff (not from the row list): serve
`S05` instead of `T05` (client hangs after `?` — the row must fail);
`PacketSize=3fff` (client `sscanf` reads 3 → `M` chunking breaks); 1023-byte
XML (crash row); `G` applying 14 registers (I/R clobber row); `z0` unknown →
`E01` (quit path row); stop reply emitted on a foreign `Paused` (invariant
row).

---

## 10. Owner decisions (review 2026-09-27) — all questions settled

None open. Recorded as decided:

1. **Port (Q8):** ports are explicit — the server is off unless `--gdb-port`
   is given; no default port. The man page keeps `--gdb-port 3333` as its
   *example* only. §6.4's shared rule (absent = off, `0` = ephemeral, logged)
   stands.
2. **`monitor in/out` (Q11):** kept, labelled as perturbing in the `help`
   text and in §4.3.
3. **`k`:** detach, never exit jnext (§2 row 20 stands).
4. **Headless stop policy (Q3):** the exception is ADOPTED — under
   `--headless` (and SDL) a `Stop` **pauses and notifies** while a remote
   client is connected, instead of logging and exiting non-zero
   (CAP-SES-04). The §7.2 regression row's dependency is therefore settled:
   `break _main` + `cont` stops the machine for the client under
   `--headless`.
5. **Remote pause opens the Qt window (Q5):** a pause caused by a remote
   client (`?`, `0x03`, a gdb-owned breakpoint) opens the local debugger
   window, as `check_breakpoint_hit()` does today (§6.3 stands unchanged).

Noted, no design change for this adapter: the owner has allowed machine
*mutation* from DSL scripts, and DZRP moves to protocol version 2.2.0 —
neither touches an RSP packet or a CAP this file uses.

---

## 11. Not verified / cannot be known without a prototype

- Real gdb's z80 target against §3's XML (§7.4 b).
- Whether the Qt/SDL paused-tick cadence makes z88dk's source-line `step`
  (one round trip per instruction) feel acceptable; measurable only with the
  implementation.
- Upstream z88dk-gdb `monitor` end to end (source-designed; re-run on the next
  z88dk update of this host).

---

## 12. Milestone 1 — WP-1..WP-4 as built (2026-09-29)

### 12.1 What was built

| WP | Files | What |
|---|---|---|
| WP-1 | `src/remote/gdb/rsp_codec.{h,cpp}` | Checksum, `}` escape/unescape, `frame_packet` (escapes `# $ } *` unconditionally — a text reply has none, a `qXfer` reply is then always well formed), hex helpers, and `RspParser`: incremental, yields ONE event at a time (packet / 0x03 outside a packet / bad checksum / oversize), resynchronises on `$`, drops an oversize body as it arrives. Pure: no backend. |
| WP-2 | `src/remote/gdb/target_desc.{h,cpp}` | §3.1 verbatim as a `constexpr` string, 600 bytes, with a `static_assert` at 1022; `REG_NAMES` / `REG_IDS` in document order; `pack_registers` / `unpack_registers` / `pack_register` / `unpack_register` (16-bit LE, lower-case hex); clock pair from `Time::tstates_total`. |
| WP-3 | `src/remote/gdb/rsp_server.{h,cpp}` | `GdbServer`: a `remote::Protocol` + `dbg::Listener` over T. §2's table, §5.4's state machine (`owed_` ∈ {None, Continue, Question, Interrupt}), the `Z` id map, `monitor` (§4.3). Attaches on connect (`ClientKind::GdbRsp`), detaches on `D` / `k` / hang-up. |
| WP-4 | `src/core/cli_options.h`, `src/main.cpp`, `src/core/emulator_config.h`, `src/platform/debug_servers.*`, `doc/man/jnext.1.md` | `--gdb-port` (the one port rule), `EmulatorConfig::gdb_port`, `DebugServers::start` opens a `GdbServer` beside the `DzrpServer` — its own listener and backend client — so all three loop owners host it with no loop-owner edit; `--debug-listen-address` accepts `--gdb-port` as its server port. Man page: the OPTIONS row and a **REMOTE DEBUGGING (Z88DK-GDB)** section. |

Tests: `gdb_rsp_test` (96 rows after review round 1, `gate: none`) — GDB-FRM 11, GDB-SUP 6, GDB-REG 8,
GDB-MEM 10 (three of them the F1 fix's, §12.4), GDB-BP 11, GDB-STP 9, GDB-STOP 12, GDB-MON 12, GDB-UNS 4, GDB-GEN 1,
GDB-SES 6. Regression rows `gdb-cli-func` (the CLI, a port in use, the
z88dk-gdb connect sequence over a real socket with this suite's own framing,
DZRP + GDB at once), `gdb-sdl-func`, `gdb-qt-func` (the two GUI loop owners).
Hand-run against the real `z88dk-gdb` v2.4 (`reg`, `break 0x0038`, `cont`,
`stepi`, `nexti`, `set hl`, `breakpoints`, `quit`): every exchange as §7.1
predicted, `set hl` logs ONE `MUTATE`, `quit` sends `z0` + `D` and the detach
releases the client's pause.

### 12.2 Deviations from sections 0-11, each with its reason

1. **`Z4` is ONE subscription**, `Mem` with `Access::ReadWrite`, not "`MemRead`
   + `MemWrite`, one RSP id → two subscriptions" (§2 row 14). The landed
   backend has one `Mem` kind with an access mask (`events.h`); one
   subscription is the same filter and keeps the id map one to one.
2. ~~`M`/`X` onto ROM is refused by the ADAPTER~~ — **superseded by the F1
   fix (§12.4)**: the backend's `poke(Cpu)` now reports the bytes that landed,
   as §4.1 / REQ-gdb-6 said it would, and `M`/`X` answer `E01` unless all of
   them did. One difference from §4.1 remains, by the manager's decision: a
   range straddling ROM and RAM is not refused up front — its RAM bytes land
   (as the CPU's write would), and the reply is `E01`.
3. **`?` always answers `T05`** — §2 row 3 and §7.3's GDB-STOP row — where
   §5.3's table lists "our `?`/`0x03`" together under `T02`. 0x03 answers
   `T02` (§2 row 18). The two sections disagreed; row 3 is the one the client
   depends on (N-12) and the unit rows were written from.
4. **`G` writes only the registers that CHANGE** (§2 row 5 said "applies the
   12 pairs"). z88dk-gdb's `set <reg>` resends the whole file (§7.1), and
   CAP-INS-01 `set_register(PC)` clears `halted` (§4.2a): applying an
   unchanged PC would un-HALT the CPU on every `set hl`, and log eleven
   `MUTATE` lines for one register. Row GDB-REG-04.
5. **Unsupported packets are logged on the `debugger` channel**, not a `remote`
   one (§2 row 33) — T's decision 11: no new `--log-level` token.
6. **Replies the design did not specify**, chosen so none is ever empty by
   accident (an empty reply means "unsupported"): `m` with length 0 or starting
   past 0xFFFF → `E01`; `X`/`M` with length 0 → `OK` (gdb probes `X` support
   with `X<addr>,0:`); `M`/`X` running past 0xFFFF, or a watch range wrapping
   past it, or of length 0 → `E01`; `qXfer:features:read` with length 0 →
   `E00`; `i0` → `E01`. `c <addr>` and `s <addr>` both set PC first (row 16
   named only `c`).
7. **`monitor bp` names owners by client id**, marking this client's
   "(this client)" (§4.3 said "this client / gui / dsl / …"): the frozen API
   publishes no client-list query.
8. **`monitor reset hard` prints the live PC and run state** it reads after the
   reset ("hard reset: machine at PC=0000, still stopped"), not a fixed
   "(nextboot.rom)" string — the machine type decides the PC. `reset` alone is
   `reset soft`.
9. **Three regression rows land with WP-4** (`gdb-cli-func`, `gdb-sdl-func`,
   `gdb-qt-func`); §9 named only `cli-check` / `docs-man`. `main.cpp`'s parse
   loop and the loop owners are linked into no unit suite, so only a row proves
   them — D's `dzrp-cli/sdl/qt-func` precedent. WP-5 keeps `gdb-z88dk-func`,
   the row with the real client.

### 12.3 Findings

- **F1 — `backend.md` CAP-INS-02 and the frozen header disagree on
  `poke(Cpu)` over ROM.** FIXED — see §12.4. `backend.md` (CAP-INS-02, REQ-gdb-6 ACCEPTED) says
  it "returns the count written and `RefusedReadOnly` when the range is
  read-only"; `debugger.h` says "ROM ignored", and the implementation returns
  `Ok` with the full count. DZRP relies on the header's reading
  (DZRP-MEM-07). Not changed here: it is the frozen interface's documented
  semantics, and a change would alter D. **Consequence for RSP:** the adapter's
  slot check is conservative — an overlay that makes part of a ROM slot
  writable (Layer 2 write-over at 0x0000-0x3FFF, DivMMC RAM or Multiface RAM
  at 0x2000-0x3FFF) is refused `E01` over RSP although the CPU could write
  there. Resolving it needs a backend query ("would a CPU write to `addr`
  land?") or a `poke` mode that refuses before writing — an owner decision on
  the frozen header.

### 12.4 The F1 fix (manager decision, 2026-09-29)

No frozen-header change: `poke()` already returns `Expected<size_t>`, and
CAP-INS-02 / REQ-gdb-6 already promised "the count written and
`RefusedReadOnly`". The defect was the backend body reporting every byte as
written. Now:

- `Mmu::write_landed(addr, val)` is `Mmu::write` returning its own routing
  decision (`write()` is it with the answer discarded, forced inline so the
  guest's hot path is unchanged); `DivMmc::write` returns whether it stored the
  byte, which `Mmu::divmmc_write` passes up. No overlay rule is restated.
- `poke(Cpu)` offers every byte, counts what landed, returns `Ok` iff all of
  them did and `RefusedReadOnly` with the partial count otherwise (0 when none
  did); a MUTATE line only for what landed. Contract written into `backend.md`
  CAP-INS-02 for packages Q and S.
- GDB `M`/`X`: the adapter's ROM-slot pre-check is gone; `E01` unless the
  backend says `Ok`. Layer 2 write-over and DivMMC / Multiface RAM over a ROM
  slot now land (rows GDB-MEM-08/09); a DivMMC ROM/RAM straddle is `E01` with
  its RAM half written (GDB-MEM-10); GDB-MEM-03 now asserts the straddle's RAM
  byte LANDS.
- DZRP `CMD_WRITE_MEM`: same wire (a seq-only reply, the same bytes landing);
  the read-back that counted undelivered bytes is replaced by the backend's
  count, which no longer mistakes a Layer 2 write-over byte (it lands in the
  Layer 2 page, reads come from the normal map) for a dropped one. MEM-09's log
  text changed ("did not land"); MEM-10 is new. `CMD_WRITE_BANK` /
  `WRITE_BANK_MEM` use `poke(Page)` and are unaffected.

### 12.5 Review round 1 (2026-09-29) — §5.4 corrected

The review reproduced, with the real `z88dk-gdb` 2.4, one `c` answered by a
`T02` and two `T05`s: a Ctrl-C at the prompt puts a 0x03 on the wire next to
the following `c`, and §5.4's "0x03 while paused: reply `T02` at once" answered
a request nobody was waiting on — the client then took it as the reply to the
`c`, and the real stop reply landed where it expected its `g`/`m` reply. Two
rules replace that part of §5.4 (this section supersedes §2 row 18 and §5.4's
last paragraph):

1. **0x03 is not a request.** It only asks for the stop that answers an
   outstanding `c`/`s`/`i`/`?`; that stop's edge sends the one reply (`T02` for
   this client's own pause, §5.3). With nothing owed — the client at its prompt,
   or the stop reply already on its way — a running machine is paused and
   **nothing** is sent. gdbserver answers nothing there either. (`Owed::Interrupt`
   is gone.)
2. **An inspection packet abandons an owed reply.** A client sends `g G p P m M
   X Z z qRcmd` only when it believes the machine stopped, so it has already
   taken some packet as its `c`/`i`/`?` reply; sending the owed `T` afterwards
   would put it where the client expects this packet's reply. So rule 4 also
   drops the owed reply, whether the machine still runs or has stopped with its
   edge not yet sent.

Rows: GDB-STOP-13 (0x03 then `c` at a breakpoint stop: one stop packet),
GDB-STOP-14 (`?` during a `c` pauses; one reply), GDB-STOP-15 (the abandon, both
orders); GDB-STOP-03 and GDB-FRM-06 now assert a 0x03 with nothing owed sends
nothing. Re-verified with the real client in both wire orders (0x03 before and
after the `c`). Also from the review: BP-12/13 (insert, remove — with another
`kind` — insert again: live), SES-07 (the next session's breakpoint at the same
address is live), REG-02 reads the clock pair from the served `g`, MON-10 hides
another client's transient, and `debugger_backend_test` INS-02-22 adds the
one-byte ROM poke (no MUTATE line).

---

## 13. Milestone 2 — WP-5 as built (2026-09-30)

### 13.1 What was built

- **`gdb-z88dk-func`** (`test/00regression/scripts/gdb-z88dk-func.sh`): the
  real `z88dk-gdb` against a live headless jnext (`--gdb-port 0`, the harness's
  per-run SD clone), scripted through stdin, every assertion on the CLIENT's log
  plus jnext's `DETACH … (released its pause)`. Client lookup: `$Z88DK_GDB`,
  then `PATH`, then the source-tree default `$HOME/src/spectrum/z88dk/bin/z88dk-gdb`;
  none found → a declared SKIP naming `Z88DK_GDB` (CI). No `trap`,
  `timeout --foreground --kill-after`, `LANG=C` children; the client runs in
  `$TMP_DIR` (it writes `.ticks_history.txt` into its current directory).
- **Fixture** `test/00regression/nex/magic_bp_demo.map`: the z88dk linker map of
  `demo/magic_bp_demo` (`zcc … -m`), whose NEX rebuilt byte-identical to the
  checked-in `magic_bp_demo.nex`; trimmed to what the client reads (the demo
  module's symbols and the non-empty sections' `__*_head/_tail/_size`; 88 lines,
  6.7 KB instead of 700 KB) with the z88dk install prefix in its comment fields
  rewritten to `z88dk/`.
- **User guide** 6.11 "Debugging with z88dk-gdb"; **developer guide** 3.11 "The
  GDB RSP server"; FEATURES.md; a ChangeLog Unreleased Developer Features line.

### 13.2 Deviations from §7.2, each with its reason

1. **The session restarts `main` itself** (`set pc 0x816a`) and breaks on
   `_print_str`, not `_main`. §7.2 assumed `?` would stop the machine inside the
   NEX boot hold; headless runs at full speed, and the demo is at its final HALT
   loop before the client can connect (measured). `_print_str` is called six
   times from `main`, so `break _print_str` + `cont` stops deterministically, and
   `break 0x8152` + `nexti` puts `nexti` on a CALL (the `i3` packet). `set pc`
   also exercises `G`.
2. **`monitor` is asserted only when the client sent `qRcmd`.** v2.4 has no
   `monitor` (it sends nothing for it); the PASS line says which client ran.
3. **Exit status 1 is tolerated for one documented reason:** upstream
   z88dk-gdb's network thread calls `remote_closed()` → `exit(1)` once the socket
   ends after `D`, racing the main thread's `exit(0)` (upstream
   `debugger_gdb.c` `network_read_thread` / `gdb_remote_closed`). It is accepted
   only with `D` answered `OK` and "Connection to remote closed." printed after
   it; v2.4 exits 0.

### 13.3 §11 item 7 closed: the upstream client, run

Upstream `z88dk-gdb` (master `a61dbb0`, 2026-09-30) was built from source in
scratch and run through the same session: the connect sequence, map symbols,
`set pc`, `break`/`cont`/`stepi`/`nexti` (`i3`) and `quit` behave as with v2.4,
and `monitor mmu` / `monitor nextreg 0x07` print the jnext output exactly —
upstream's `process_packet` decodes the `O` packets before the positional reply
routing, and the final `OK` ends the request (the client prints it). The row
passed with both clients.
