# ZRCP frontend — design (working file)

> Status: **v3.2 — owner review 2026-09-27: every open question settled; all 15 REQs answered** (GH #280, epic #276, gate #277).
> Owner of this file: the ZRCP frontend design agent. Backend capability IDs
> (`CAP-…`) are those of `backend.md` v1; requirements sent to the backend are
> `REQ-zrcp-<n>` (§7). Sibling files: `dzrp.md`, `gdb.md`, `qt.md`, `dsl.md`.
>
> **Revision log**
> - v1 (2026-09-26): command table from the real client and the real server,
>   mapped; transport model agreed with `design-gdb`/`design-dzrp`; REQs sent.
> - v2 (2026-09-26, later): backend verdicts on REQ-zrcp-01..14 recorded (§7);
>   pending cells resolved (`smartload` → CAP-CTL-15, bookmarks, coverage →
>   CAP-INS-20, clip windows → CAP-INS-15); DSL spellings recorded (§3.2).
> - v3 (2026-09-26, night): independent protocol review (`protocols.md`,
>   verdict REJECT) — R-2 `hard-reset-cpu` was designed against a synchronous
>   reset that does not exist (it is a deferred cold boot); decided (a):
>   synchronous cold boot inside `pump` with the contract in §4.6, sent as
>   REQ-zrcp-15. R-3 transport moved to the public `esp::` seam. R-5 one port
>   rule (`0` = ephemeral). R-6 §6.3 prerequisites restated. N-1/2/3/9/10/11.
> - v3.1 (2026-09-27): round-2 review APPROVE (`protocols-r2.md`; the contested
>   transcript line was retracted by the reviewer). Notes folded: N-2 headless
>   stop policy marked owner-pending (settled 2026-09-27, v3.2); N-3 WP-1
>   consumes the shared transport package T; N-4 `smartload` takes the direct
>   load path, not the GUI's cold-boot-then-schedule path; N-1 (guest hard
>   reset during a `run`) tracked pending the backend's rule 3/4 resolution —
>   resolved by backend v5: never a pause, no `Reset` reason, the adapter
>   completes a blocked `run` from the `Reset{Hard}` event (§4.3/§4.6 aligned).
> - v3.2 (2026-09-27, owner review): Q9 `hard-reset-cpu` is a faithful cold
>   boot (document `resetOnLaunch: false` with `--load`); Q10 `get-version` =
>   `12.0-jnext-<ver>`; Q3 headless Stop pauses + notifies while a remote client
>   is connected — `zrcp-func`'s dependency settled; Q8 ports explicit, off
>   unless `--zrcp-port` given. §9 moved to settled. Noted: DSL scripts may
>   mutate the machine; DZRP moves to 2.2.0 — neither touches this adapter.
> - v3.3 (2026-09-29, milestone 1 — WP-1 + WP-2 + WP-3 implemented, branch
>   `gh280-zrcp`): §11 records what was built, every deviation from this
>   design with its reason, the backend defect fixed on the branch
>   (REQ-zrcp-05) and the design findings. Sections 0-10 are unchanged: where
>   the implementation differs, §11 says so and why.

Every claim carries one of three kinds of evidence:

- `[T<n>]` — a transcript captured on 2026-09-26 against the **real server**:
  `ZEsarUX 12.0` (build `1737051251`, `/home/jorgegv/src/spectrum/ZEsarUX-12.0/zesarux`),
  launched `--noconfigfile --nowelcomemessage --nosplash --disablebetawarning 12.0
  --quickexit --enable-remoteprotocol --remoteprotocol-port 10777 --vo null --ao
  null --machine TBBlue`, driven with a Python socket client that records the
  exact request/response bytes. Five batches, T1..T5. Byte strings quoted below
  are verbatim (`\n` is a real LF). The transcripts live in the design agent's
  scratchpad; WP-6 (§8) turns the load-bearing ones into a committed fixture.
- `dezog:<file>:<line>` — the **real client**: DeZog 3.7.4's ZEsarUX remote,
  `/home/jorgegv/src/spectrum/dezog.jorgegv/src/remotes/zesarux/` (`zesaruxremote.ts`,
  `zesaruxsocket.ts`, `zesaruxcpuhistory.ts`, `decodezesaruxdata.ts`) and
  `src/settings/settings.ts`. **This is what defines the subset that matters**:
  what DeZog sends and how it parses the answer.
- `jnext:<file>:<line>` — the worktree at `main @ 974b0ab19`.

Nothing here is designed against `doc/design/EMULATOR-DESIGN-PLAN.md`.

---

## Work packages — the tracker for this package

Mirrors this package's row in [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
§10.1, which stays authoritative: if the two ever disagree, §10.1 wins and this
table is stale. It exists because §10.1 states each package's sequence as one
long table cell, which is unreadable as a plan and impossible to track against.

Status values: `todo` · `in progress` · `in review` · **`done`** (independently
reviewed and APPROVED). The whole package lands on **one branch** and merges
whole, so `done` here means the sub-item is approved, not merged.

| WP | Branch `gh280-zrcp` (issue #280) | Status |
|---|---|---|
| **WP-1** | session skeleton over the shared transport (T) | **done** |
| **WP-2** | formatters | **done** |
| **WP-3** | control / run | **done** |
| **WP-4** | breakpoints + conditions — **needs S's WP1**, the DSL's `compile_expr` exported as a library. §11 item 8: whether that library covers ZRCP's honoured condition subset without a fallback parser is measured here (answer: §11.8) | **done** |
| **WP-5** | history / coverage / load (record: §11.9) | in review |
| **WP-6** | fixtures + docs | todo |

WP-2..WP-5 may run in parallel after WP-1. Depends on: B0 (landed), B, T; WP-4 also on S WP1.

Settled: `hard-reset-cpu` is the **cold boot** (Task 70 semantics) and
`get-version` answers `12.0-jnext-<ver>` (owner, §1.3 items 17-18).

Every sub-item is reviewed by an agent or person that did NOT write it, and the
branch does not merge until the full §10.3 gate is green on the tip.

---

## 0. Summary

- **What ZRCP is on the wire** (§1): a telnet-style line protocol. Server sends
  a welcome, then `command> ` (or `command@cpu-step> ` in step mode); client
  sends one line; server sends the reply lines and the prompt again. Errors are
  plain lines beginning `Error` / `ERROR`; the client detects them by prefix.
  `run` is the one command that does not answer immediately; **any bytes** sent
  while it runs stop the machine and are discarded.
- ZEsarUX 12.0 lists **125** commands (`ls`, [T1]). **Served: 67 command
  names** (every one DeZog 3.7.4 sends, plus the telnet conveniences);
  **declined by design: 1 command (`exit-emulator`) and 6 options** (`run
  verbose` / `no-stop-on-data` / `update-immediately`, `cpu-history restore`,
  `set-debug-settings` bit 5, breakpoint actions other than break/print);
  **unsupported, reported as such: the remaining 57** (§2). The only DeZog
  traffic that draws an error is `set-debug-settings 32` under the non-default
  `skipInterrupt: true` (§2.5).
- **Conditions** (§3): ZEsarUX breakpoints are *pure conditions* (`SP>=8000H`,
  `PC=PEEKW(SP-2) AND SP>=…`) evaluated server-side on every instruction. The
  adapter compiles ZEsarUX's expression grammar into a backend predicate
  (`CAP-EVT` condition callback); the compile is a small token translation
  into the DSL's expression compiler (`design-dsl`, §3.3), with a `PC=nnnn`
  fast path that becomes an `Execute[addr,addr]` cheap filter. The honoured
  subset is stated; everything else fails `set-breakpoint` with the same error
  text ZEsarUX uses for a parse failure.
- **Session state** (§4) — cpu-step mode, the prompt, breakpoint slots 1..100,
  memory-breakpoint map, partial T-state base, history filters, `set-cr` —
  lives entirely in the adapter. The backend learns none of it.
- **Transport** (§5): one model shared with DZRP and RSP — non-blocking
  listener over the existing `esp01` socket twins, pumped by the loop owner
  through `CAP-SES-03`, single-threaded, no ownership lock; `--zrcp-port N`
  (absent = off; `10000` is what DeZog assumes) and the shared
  `--debug-listen-address ADDR` (default `127.0.0.1`; spelling per `design-dzrp`, after the existing `--esp-listen-address`).
- **Validation** (§6): by hand over `nc`; a Python fake client as a regression
  functional row; DeZog 3.7.4 with `"remoteType": "zrcp"` as the real client,
  exercising launch → smartload → breakpoints → step → step-over → step-out →
  reverse-step → disconnect.
- **REQs**: 15 sent, 15 answered — 12 ACCEPTED (incl. the review-driven
  hard-reset contract REQ-zrcp-15), 2 CONFIRMED-as-drafted, 1 ALTERNATIVE
  (clip windows from live layer state) (§7); 0 reach-arounds.

---

## 1. The protocol as it actually is

### 1.1 Wire framing

| Fact | Evidence |
|---|---|
| Welcome on connect: `b'Welcome to ZEsarUX remote command protocol (ZRCP)\nWrite help for available commands\n\ncommand> '` | [T1] |
| DeZog waits for *any* first text before it will send (`CONNECTED_WAITING_ON_WELCOME_MSG` → `connected`) | dezog:zesaruxsocket.ts:231-238, :314-315 |
| Prompt when running: `command> `; in cpu-step mode: `command@cpu-step> ` | [T3] `enter-cpu-step` → `b'\ncommand@cpu-step> '` |
| DeZog's end-of-reply detector: last line `startsWith('command')` **and** `endsWith('> ')`; everything before it is the reply; `zesaruxState = lastLine.substring(8)` | dezog:zesaruxsocket.ts:561-574 |
| Line terminator: `\n`. `set-cr` makes the server add `\r` before each `\n` | [T1] `help set-cr`; DeZog strips `\r` only where it splits stack lines (`data.replace(/\r/gm, "")`, dezog:zesaruxremote.ts:503, :832) |
| Every reply, even an empty one, is `<lines>\n<prompt>`; an empty reply is `b'\ncommand> '` | [T1] `noop`, `set-debug-settings 32` |
| A blank line at the prompt is an empty command: `b'\ncommand> '` | [T5] |
| Command names are exact, lowercase, no leading whitespace (`'   get-registers   '` → `Unknown command`; `GET-REGISTERS` → `Unknown command`); short aliases exist (`gr`, `sb`, `cs`, `d`, …) and are listed in each `help` entry; extra arguments are ignored (`get-registers foo bar` answers normally) | [T5], [T1] help texts |
| Unknown command: `b'Unknown command\ncommand> '` | [T1] |
| Errors: a line starting `Error` (most commands) or `ERROR` (`tbblue-*`, `cpu-history get` out of range). DeZog: `concData.substring(0,5).toLowerCase() == 'error'` → UI warning unless the sender suppressed it | [T1], dezog:zesaruxsocket.ts:609-618 |
| Asynchronous log lines: `log> <text>\n` may appear anywhere; DeZog cuts them out of the stream and forwards them to its debug console | dezog:zesaruxsocket.ts:502-524 |
| Numbers: decimal by default, hex with `H` suffix (`0038H`, `38h`); `0x38` is a **parse error** (`set-breakpoint 12 PC=0x38` → `Error. Error setting breakpoint`; `evaluate 0x10` → `Error parsing`) | [T2], [T4] |

### 1.2 `run` and how it is interrupted — the one asynchronous command

| Fact | Evidence |
|---|---|
| `run` outside cpu-step mode: `b'Running until a breakpoint, key press or data sent, menu opening or other event\n\rError. You must first enter cpu-step mode\ncommand> '` (note the stray `\r` ZEsarUX emits here) | [T2] |
| `run` in cpu-step mode answers **immediately** with exactly one line `Running until a breakpoint, key press or data sent, menu opening or other event\n` and **no prompt**; the socket then stays silent until the machine stops | [T3], [T4] |
| DeZog asserts that first line: `Utility.assert(splitData[0].startsWith("Running until"))` and forbids interrupting until it has arrived (`interruptableRunCmdCriticalPhase`) | dezog:zesaruxsocket.ts:533-559, :448-452 |
| Stop by breakpoint: `Breakpoint fired: PC=83H\n` + register line with ` TSTATES: 32` + `\n  0083 CALL 1C5E\n` + prompt | [T5] |
| Stop by memory breakpoint: `Breakpoint fired: Memory Breakpoint Write Address: FFFEH\n` + register line + disasm + prompt | [T4] |
| Stop by a pure condition: `Breakpoint fired: SP<FFF0H\n…` — the condition text is echoed in ZEsarUX's canonical spelling (`PC=0038H` set → `PC=38H` echoed) | [T4], [T2] `get-breakpoints` |
| Stop by **data sent**: any bytes arriving on the socket stop the run; the reply is the register line + disasm + prompt with **no** `fired` line, and **the received line is discarded, not executed** (`get-registers` sent during a run produced one stop reply and no second reply) | [T3], [T4] |
| DeZog's pause is exactly that: `sendBlank()` writes `\n`; and its command queue interrupts a run by unshifting an empty command `''` whose reply is the run's stop output, then re-sends `run` when the queue drains | dezog:zesaruxsocket.ts:479-486, :378-390, :544-558, :628-632 |
| DeZog's break reason = the first reply line containing `point hit` or `point fired` | dezog:zesaruxremote.ts:581-591 |
| `run <n>`: `Running until …, 100 opcodes run, or other event\nReturning after 100 opcodes\n` + registers + disasm | [T3] |
| `run verbose` prints registers + disasm after every instruction (10 MB in 8 s) | [T3] |
| `run no-stop-on-data` ignores socket data — the connection is then unusable until a breakpoint fires | [T3] (everything after it timed out) |
| A `run` interrupted by a stop the client did not request (e.g. another client paused the machine) still needs a terminating reply — otherwise DeZog waits `socketTimeout` (5 s default) and drops the connection | dezog:zesaruxsocket.ts:289-297; settings.ts:595-596 |

### 1.3 Stepping

| Fact | Evidence |
|---|---|
| `enter-cpu-step`: `b'\ncommand@cpu-step> '`; idempotent (second call same reply) | [T2] |
| `exit-cpu-step`: `b'\ncommand> '`; a second one: `Error. You are not in step to step mode` | [T3] |
| `cpu-step` reply: `PC=0001 SP=ffff … MMU=… TSTATES: 4\n  0001 IM 1\n` — the register line (§1.4) with ` TSTATES: <frame-relative t-state>` appended, then the disassembly of the **new** PC | [T3] |
| `cpu-step-over`: same reply shape; semantics "run until PC reaches the next instruction", except RET/JP → single step. On `JR $` it **never returns** (T2 hung there — this is why DeZog only uses it for LDIR/LDDR/CPIR/CPDR and otherwise does its own step-over with `run` + a `SP>=` breakpoint) | [T2], [T3], dezog:zesaruxremote.ts:610-682 |
| DeZog `stepInto` = `cpu-step`, then `get-registers`, then call-stack, then coverage, then history spot | dezog:zesaruxremote.ts:707-726 |
| DeZog `stepOver`: `disassemble <pc>` and reads **columns 7..10** of the reply (`disasm.substring(7, 7+4)`) for `"RST "` / `"CALL"` → sets breakpoint **100** to `SP>=<sp>` and `run`s; LDIR/LDDR/CPIR/CPDR → `cpu-step-over`; else `cpu-step` | dezog:zesaruxremote.ts:629-696 |
| DeZog `stepOut`: `extended-stack get <depth>`, finds the first `call`/`rst`/`*interrupt*` entry, sets breakpoint 100 to `PC=PEEKW(SP-2) AND SP>=<sp+2k>`, `run`s, then `disable-breakpoint 100` | dezog:zesaruxremote.ts:801-892 |
| `disassemble [addr] [n]` reply lines are `  0083 CALL 1C5E` — two spaces, four upper-case hex digits, one space, mnemonic; operands upper-case hex **without** `H` suffix; wraps past `FFFF` to `0000` | [T1], [T4] |

### 1.4 Registers and memory

| Fact | Evidence |
|---|---|
| `get-registers`: `PC=0136 SP=ffdd AF=03be BC=4f9d HL=03da DE=0000 IX=ffff IY=16a0 AF'=ffff BC'=ffff HL'=ffff DE'=ffff I=00 R=59  F=S-5H3PN- F'=SZ5H3PNC MEMPTR=0136 IM1 IFF-- VPS: 0 MMU=00000000000a000b0004000500000001` — lower-case hex, **two spaces** before `F=`, flags as `SZ5H3PNC` with `-` for clear, `IM0/1/2`, `IFF12` with `-` for clear (`IFF1-`, `IFF--`), `MMU=` eight 4-hex-digit slot values with no separators | [T1], [T4] |
| DeZog parses by **searching for the labels once** (`indexOf('PC=')` etc. cached) and reading a fixed width after each: 4 hex digits for 16-bit, 2 for I/R, `IM` + one digit. Any label missing → assertion. `F=` / `MEMPTR` / `VPS` are not parsed | dezog:decodezesaruxdata.ts:84-234 |
| `MMU=` decode on a Next: `value >= 0x8000` → ROM bank `0xFC + (value & 3)`; else the 8K page number. DeZog's Next model: `0xFC` ROM0 low 8K, `0xFD` ROM0 high, `0xFE` ROM1 low, `0xFF` ROM1 high | dezog:decodezesaruxdata.ts:288-318; MemoryModel/zxnextmemorymodels.ts:59-64 |
| ZEsarUX 12.0 prints `MMU=0000 0000 …` for the two ROM slots on TBBlue even though `get-memory-pages` says `RO RO` — its own inconsistency, which DeZog would read as "page 0" | [T1], [T3] |
| `get-memory-pages`: `RO RO A10 A11 A4 A5 A0 A1 ` (trailing space); `verbose` prints a segment block per slot | [T3] |
| DeZog defines `getSlotsFromEmulator()` over `get-memory-pages` but **no caller uses it** (grep over `src/` outside the zesarux directory: none); and its 8-slot parse would yield `NaN` for `RO` anyway. The `MMU=` field is the path that matters | dezog:zesaruxremote.ts:356-395 |
| `set-register NAME=VALUE`: reply is the full register line; `XX=1` → `Error changing register`; `IM=2` → `Error changing register` (IM is not settable in 12.0); `IFF1=1`, `F=1`, `A'=12H`, `HL'=1234H`, `I=3FH`, `R=7` work | [T1], [T4] |
| `read-memory addr len`: upper-case hex, two digits per byte, no separators, one line; `read-memory addr` = 1 byte; bare `read-memory` = the whole 64 KB (131 072 chars); `len` past 64 KB is still served by ZEsarUX (65 537 bytes came back for `read-memory 32768 65537`; whether it wraps or pads was not probed) | [T1] |
| DeZog reads memory in 64 KB chunks and asserts `data.length/2 == size` | dezog:zesaruxremote.ts:1308-1330 |
| `write-memory addr v1 v2 …` (decimal bytes, space separated) and `write-memory-raw addr HEXSTRING`; both reply empty. Writes to ROM are silently ignored (`write-memory 0 1` then `read-memory 0 1` → `F3`) | [T1], [T4] |
| DeZog writes with `write-memory-raw` in 64 KB chunks and single bytes with `write-memory` followed by a read-back | dezog:zesaruxremote.ts:1338-1374 |
| `hexdump addr len`: `  0000H F3 ED 56 … \|..V…\|` lines + blank line; `get-crc32 addr len`: 8 lower-case hex digits | [T1] |

### 1.5 Breakpoints

| Fact | Evidence |
|---|---|
| Two independent mechanisms: **condition breakpoints** in 100 numbered slots (`set-breakpoint N <expr>`, `enable-breakpoint N`, `disable-breakpoint N`, `set-breakpointaction N [action]`) and **memory breakpoints** (`set-membreakpoint addr type [items]`, per-address, type 1 read / 2 write / 3 both / 0 remove) | [T1] `help` texts |
| A global master switch: with it off, **every** slot command fails `Error. You must enable breakpoints first`; `enable-breakpoints` turns it on, a second call → `Error. Already enabled`; `disable-breakpoints` when off → `Error. Already disabled` | [T1], [T2] |
| DeZog init: `clear-membreakpoints`, `enable-breakpoints` (error suppressed), then `disable-breakpoint 1` … `100` fire-and-forget, then free ids 99..1 (100 reserved for step-over/out) | dezog:zesaruxremote.ts:338-350, :1251-1259 |
| Slot range: `set-breakpoint 0 …` / `101 …` / `disable-breakpoint 200` → `Error. Index out of range` | [T2] |
| `set-breakpoint N` with no condition clears the slot (`Enabled 1: None` after enabling) and a slot with no condition never fires ("handled as disabled") | [T2], [T1] help |
| DeZog sets a breakpoint as three commands: `set-breakpointaction N` (empty action = break; required since 10.2), `set-breakpoint N <cond>`, `enable-breakpoint N`; removes with `disable-breakpoint N` only | dezog:zesaruxremote.ts:1143-1166 |
| `get-breakpoints [i] [n]`: `Breakpoints: On\nEnabled 1: PC=38H\nDisabled 2: None\n\n` — the stored condition is re-printed **canonicalised** (upper-case, spaces removed around operators, leading zeros dropped: `A<>0 and (HL & 0FFH) = 5` → `A<>0 AND (HL&FFH)=5`) | [T2] |
| Parse failures: `this is garbage`, `PC=0 && A==1`, `PC=0x38` → `Error. Error setting breakpoint` and the slot is left `None` | [T2] |
| `set-breakpointpasscount` does **not exist** in 12.0 (`Unknown command`); DeZog gates it on version ≥ 12.1 | [T1]; dezog:zesaruxremote.ts:59-61, :217-219 |
| Memory breakpoints: `set-membreakpoint 4000h 2 1`; `get-membreakpoints [addr] [n]` lists `4000H : 3` per address; type 0 over a range removes; `clear-membreakpoints` | [T1], [T3] |
| DeZog `setWatchpoint`: `set-membreakpoint <hex>h <1|2|3> <size>`; a watchpoint with a condition is **not set at all** (ZEsarUX cannot); removal = type 0 over the same range | dezog:zesaruxremote.ts:903-937 |

### 1.6 Conditions — the ZEsarUX expression language

From `help set-breakpoint` [T1] and probes [T2], [T4]:

- Variables: `A,B,C,D,E,F,H,L,AF,BC,DE,HL,A',…,HL',I,R,SP,PC,IX,IY`; flags
  `FS,FZ,FP,FV,FH,FN,FC`; `IFF1,IFF2`; `SEG0..SEG7` (8K page in each Next
  slot; `evaluate SEG7` → `1`); `RAM`/`ROM` (128K paging); `OPCODE1..4`;
  `EPC`; `COPPERPC`; the last-access variables `MRA/MRV/MWA/MWV/PRA/PRV/PWA/PWV`;
  the stateful `INTFIRED/OUTFIRED/INFIRED/ENTERROM/EXITROM`; Z88/TSConf/PD765
  oddities.
- Functions: `PEEK(x)`, `PEEKW(x)`, `NOT(x)`.
- Operators: `= <> < > <= >=`, `AND OR XOR`, `+ - * /`, `& | ^`, parentheses
  `()[]{}`. Case-insensitive (`and`, `PC=0038h` accepted [T2]). `==`, `!=`,
  `&&`, `||`, `!` are **rejected** [T2], [T4].
- Numbers: decimal, or hex with `H` suffix; a hex literal starting with a
  letter needs a leading `0` (`0FFH`).
- DeZog's `convertCondition()` produces exactly this dialect from its own
  C-like syntax (`==`→`=`, `!=`→`<>`, `&&`→` AND `, `||`→` OR `, `!`→`NOT`,
  `0x12BF`→`12BFH`, labels → decimal addresses & 0xFFFF, `b@(…)`/`w@(…)` →
  `peek(…)`/`peekw(…)`) — dezog:zesaruxremote.ts:1005-1044.
- What DeZog actually sends (dezog:zesaruxremote.ts:1056-1130, :640-650,
  :846-850):
  1. `PC=0abcdh` — a plain source breakpoint;
  2. `PC=0abcdh and SEG<n>=<bank>` — a *long* address on the Next, `<bank>` being
     the 8K page or `8000h+k` for ROM (`0x8000 + (bank & 3)`); on 128K it is
     ` and ROM=<0|1>` for `addr <= 3FFF` or ` and RAM=<bank>` for `addr >= C000`;
  3. `PC=… and (<user condition>)` — ASSERTION and conditional source breakpoints;
  4. `SP>=<sp>` — step-over of CALL/RST (no PC term at all);
  5. `PC=PEEKW(SP-2) AND SP>=<sp>` — step-out.

### 1.7 History, stack, coverage, timing, Next-specific

| Fact | Evidence |
|---|---|
| `cpu-history`: `enabled yes`, `set-max-size N`, `clear`, `started yes`, `ignrephalt yes`, `ignrepldxr yes` at init (only `enabled` has errors suppressed); `get <i>` per entry, `i=0` newest; DeZog fetches `spotCount` (default 10) entries after **every** step or stop | dezog:zesaruxcpuhistory.ts:93-101, :115; cpuhistory.ts:189-193; settings.ts:1086-1087 |
| `cpu-history get 0` reply: `PC=167f SP=ffdf AF=40bb BC=243b HL=4008 DE=4009 IX=ffff IY=fff3 AF'=ffff BC'=ffff HL'=ffff DE'=ffff I=00 R=64 IM1 IFF-- (PC)=d65838f6 (SP)=018c MMU=00000000000a000b0004000500000001 ` (trailing space); DeZog reads `(PC)=` four opcode bytes, `(SP)=` the word at SP, and `MMU=` via the same slot decoder (assert if absent) | [T3]; dezog:zesaruxcpuhistory.ts:393-426; decodezesaruxdata.ts:296-298 |
| `cpu-history get 500` (past the end) → `ERROR: index out of range`; DeZog maps an `error` prefix to "no more history" | [T1]; dezog:zesaruxcpuhistory.ts:115-119 |
| `extended-stack enabled yes` → `''`; `get n [index]` → lines `0000H default` (type words: `default`, `push`, `call`, `rst`, `maskable_interrupt`, …); DeZog keeps `call`/`rst`/`*interrupt*`. `enabled yes` fails while ZEsarUX has a menu open (`Error. Can not enter cpu step mode…` [T1]), which is why DeZog first sends `enabled no` with errors suppressed | [T3]; dezog:zesaruxremote.ts:319-321, :458-477, :831-843 |
| `get-stack-backtrace [n]`: `0532H 045FH 0A14H 01A9H 0000H ` (default 5, trailing space). Not used by DeZog (it reads the stack with `read-memory`) | [T1]; dezog:remotebase.ts:746-770 |
| `cpu-code-coverage enabled yes` / `clear` at init, then `get` + `clear` after **every** step/continue; `get` reply: `0136 ` (4 hex + space per address). `codeCoverageEnabled` defaults to **true** for `zrcp` | [T3]; dezog:zesaruxremote.ts:308-315, :763-794; settings.ts:1097-1106 |
| `get-tstates`: T-states within the current frame (`49` after a step from reset); `get-tstates-partial`: cumulative since `reset-tstates-partial`, printed as **nine zero-padded decimal digits** (`000000017`); DeZog uses the pair to show T-states per step | [T3]; dezog:zesaruxremote.ts:733-746 |
| `get-cpu-frequency` → `3494400` (decimal Hz); `get-version` → `12.0`; `get-current-machine` → `ZX Spectrum Next` — DeZog lower-cases it and tests `includes("tbblue")\|\|includes("zx spectrum next")`, `"128k"`, `"48k"`, `"16k"`; ≥ `10.3` required via `semver.coerce` | [T1]; dezog:zesaruxremote.ts:38, :200-216, :247-279 |
| `tbblue-get-register N` → `00H` (two upper-case hex digits + `H`); out of range → `ERROR. Out of range`; DeZog reads the first two chars | [T1]; dezog:zesaruxremote.ts:1415-1426 |
| `tbblue-get-sprite i [n]` → one line per sprite `0A 14 1E 28 \n` (4 bytes; 5 when byte 3 bit 6 is set) then a blank line; `tbblue-get-pattern i 4\|8 [n]` → 256 (or 128) `XX ` per line; `tbblue-get-palette ula\|layer2\|sprite first\|second i [n]` → 3-hex-digit 9-bit RGB `000 005 140 …`; `tbblue-get-clipwindow ula\|layer2\|sprite\|tilemap` → `0 255 0 191 ` | [T1], [T4]; dezog:zesaruxremote.ts:1434-1545 |
| `hard-reset-cpu` / `reset-cpu` reply empty; both leave PC=0000 in cpu-step mode | [T3] |
| DeZog connect sequence: `close-all-menus`, `about`, `get-version`, `set-debug-settings <0\|32>`, `hard-reset-cpu` (if `resetOnLaunch`, default true), `enter-cpu-step`, wait `loadDelay`, `smartload "<file>"` / `load-binary "<file>" <addr> 0`, `get-current-machine`, breakpoint init, coverage, history, `extended-stack enabled no` then `yes` | dezog:zesaruxremote.ts:195-322, :1588-1600 |
| DeZog disconnect: `\n`, `cpu-history enabled no`, `cpu-code-coverage enabled no`, `extended-stack enabled no`, `clear-membreakpoints`, `disable-breakpoints`, `exit-cpu-step`, `quit`; ZEsarUX answers `quit` with `Sayonara baby\n` and closes | dezog:zesaruxsocket.ts:713-725; [T4] |

### 1.8 Where the brief was wrong or imprecise

- The prompt is `command> ` **only outside** cpu-step mode; DeZog reads the
  suffix after `command` as the server's state (`@cpu-step> `). Both must be
  reproduced exactly.
- "`help <cmd>`" is real and is how the syntax above was obtained, but the
  ZRCP *version* DeZog requires is a ZEsarUX version (`≥ 10.3`), not a protocol
  number — there is no protocol version command.
- Default port 10000 is correct (settings.ts:584-585; `--remoteprotocol-port`
  default 10000).
- DeZog's first commands on connect are `close-all-menus` and `about`, not
  `get-version` — and `close-all-menus` is **not** in the `help` list DeZog
  itself carries in a comment (zesaruxsocket.ts:11-127) but exists in 12.0's
  `ls` output [T1].

---

## 2. Command table

Classes: **S** served (mapped onto a CAP), **D** declined by design (answered
with an honest error, reason given), **U** unsupported (answered
`Error. Unsupported command in jnext: <name>` — never silently accepted). Every
reply reproduces ZEsarUX's byte layout of §1 unless a "Divergence" is stated.

### 2.1 Session and information

| Command | Class | Reply (exact) | Backend |
|---|---|---|---|
| `about` | S | `jnext ZRCP remote command protocol\n` | — (Divergence: text names jnext; DeZog ignores the reply) |
| `get-version` | S | `12.0-jnext-<jnext version>\n` — `semver.coerce` yields `12.0.0` ≥ `10.3` and `< 12.1`, so DeZog never sends `set-breakpointpasscount` (which 12.0 lacks, [T1]) | `JNEXT_VERSION_STRING` (jnext:CMakeLists.txt:5, `build/generated/version.h`) |
| `get-buildnumber` | S | the jnext git describe / build id | — |
| `get-current-machine` | S | `ZX Spectrum Next` / `ZX Spectrum 48k` / `ZX Spectrum 128k` / `ZX Spectrum +3` / `Pentagon` (DeZog derives `MemoryModelZxNextTwoRom` / `Zx48k` / `Zx128k` / Unknown / Unknown) | `CAP-TIME-01 machine_timing()` + machine type |
| `get-cpu-frequency` | S | decimal Hz = `master_cycles_per_frame × fps / cpu_divisor` (3 500 000 at 3.5 MHz on the Next's 28 MHz master; NR 0x07 speed changes it) | `CAP-INS-19 machine()` (fps, cpu_divisor — REQ-zrcp-07) |
| `get-cpu-core-name`, `get-os` | S | `jnext-fuse-z80`; `GNU/Linux` / `Windows` / `macOS` | — |
| `noop`, `close-all-menus` | S | empty reply | — |
| `help [cmd]`, `ls` | S | generated from the adapter's own table — lists **only served and declined** commands, with the class in the description | — |
| `set-cr` | S | sets the session's `\r\n` flag | — |
| `quit` / `exit` / `logout` | S | `Sayonara baby\n`, close, detach | `CAP-SES-01 detach` |
| `exit-emulator` | D | `Error. Unsupported command in jnext: exit-emulator` — a remote client must not be able to terminate the user's emulator session | — |
| `get-debug-settings` / `set-debug-settings n` | S / **S with a reservation** | stores and echoes the bitmask; bits 0-4 are ZEsarUX presentation settings for its own console and change nothing in jnext's replies (documented in `help`); **bit 5 (step over interrupt)** → `Error. Unsupported in jnext: step-over-interrupt (bit 5)`; DeZog sends `32` only with `skipInterrupt: true` (default false, settings.ts:1067-1068) | — |
| `get-io-ports`, `get-ui-io-ports`, `set-ui-io-ports`, `get-machines`, `set-machine`, `get-video-driver`, `get-audio-buffer-info`, `get-ocr`, `get-text-overlay`, `print-footer`, `set-text-brightness`, `set-window-zoom`, `set-verbose-level`, `speech-*`, `ayplayer`, `zeng-is-master`, `esxdoshandler-get-open-files`, `dump-nested-functions`, `dump-scanline-buffer`, `hexdump-internal`, `save-binary-internal`, `get-visualmem-*`, `cpu-transaction-log`, `cpu-panic`, `debug-analyze-command`, `get-breakpoints-optimized`, `find-label`, `load-source-code`, `assemble`, `view-basic`, `ifrom-press-button`, `kartusho-press-button`, `tsconf-*`, `zxevo-get-nvram`, `get-memory-zones`, `get-current-memory-zone`, `set-memory-zone`, `get-paging-state` | U | `Error. Unsupported command in jnext: <name>` | — |

`get-memory-zones` / `set-memory-zone` are ZEsarUX's way of addressing
physical memory (zone 0 = 2 MB RAM, 14 = sprite patterns, 17 = copper, 31 =
sprite attributes [T1]). The backend has `MemSpace::Page` (CAP-INS-02), so
these *could* be served later; they are U in v1 because no client uses them
and the zone numbering is ZEsarUX-internal.

### 2.2 Execution control

| Command | Class | Semantics in jnext | Backend |
|---|---|---|---|
| `enter-cpu-step` | S | pause; prompt becomes `command@cpu-step> `; idempotent | `CAP-CTL-01 pause()` |
| `exit-cpu-step` | S | resume; prompt `command> `; if not in step mode: `Error. You are not in step to step mode` | `CAP-CTL-02 run()` (+ `CAP-CTL-11` gate → `Error. Machine state is corrupt after a failed rewind; acknowledge in the GUI or send hard-reset-cpu`) |
| `cpu-step` | S | requires step mode (else ZEsarUX's `Error. You must first enter cpu-step mode`); if another client resumed the machine meanwhile, pause first (§4.4); one instruction; reply = register line + ` TSTATES: n` + `\n` + disasm at the new PC | `CAP-CTL-03 step_into()` (synchronous), `CAP-INS-01`, `CAP-INS-11`, `CAP-TIME-01` |
| `cpu-step-over` | S | ZEsarUX semantics, not jnext's Step Over: RET/JP-family → `step_into`; otherwise `run_to(pc + instruction_length)` and wait for the stop; **bounded**: a stop for any other reason (breakpoint, other client) ends it with that reason. Divergence: ZEsarUX hangs forever on `JR $`; jnext honours a client interrupt (§1.2). The RET/JP classification uses `is_ret_like()` from `src/debug/disasm.h:29` over `peek(Cpu)` — a published value service (backend §6 keeps `disasm.h` published as-is), not a `CAP-INS-11` member (v3 lists only `instruction_length` / `is_call_like`) | `CAP-INS-11 instruction_length`, `disasm.h is_ret_like` over `CAP-INS-02`, `CAP-CTL-06 run_to`, `CAP-CTL-03` |
| `run` | S | requires step mode; emits `Running until a breakpoint, key press or data sent, menu opening or other event\n`, resumes, then answers on the next `Paused` (§4.3) | `CAP-CTL-02`, `CAP-SES-02 Paused` |
| `run <n>` | S | as `run` with an instruction limit; `Returning after <n> opcodes\n` when the limit ends it. A loop of `step_into` (events raised inside a step are delivered at its boundary and land in `pause_reason` — REQ-zrcp-05 ACCEPTED) that also asks `probe_execute(pc)` after each step, because the GH #221 step-off skips an `Execute` match at the resumed-from PC and the loop must end with a `fired` line there. **Budgeted**: the loop runs inside one `pump` only until the pump's `budget_ms` is spent, then parks (`in_run = RunLimit(remaining)`) and continues on the next pump — a `run 1000000` is a few hundred pumps, never one blocked tick; socket data interrupts it like a plain `run`; cap 1 000 000 | `CAP-CTL-03`, `CAP-CTL-13`, `CAP-EVT probe_execute`, `CAP-SES-03` budget |
| `run verbose` | D | `Error. Unsupported in jnext: run verbose` — 10 MB/s of text [T3] over a per-tick pump serves nobody | — |
| `run no-stop-on-data` | D | `Error. Unsupported in jnext: no-stop-on-data` — it makes the connection unrecoverable (§1.2) | — |
| `run update-immediately` | D | same error; a jnext display setting, not a protocol one | — |
| `hard-reset-cpu` | S | `CAP-CTL-12 Hard` — in jnext a hard reset is a **cold boot** (Task 70), and today it is **deferred**: `Emulator::request_hard_reset()` only sets a flag (`src/core/emulator.h:197-208`) that the loop owner polls after the tick's frames and turns into `emulator_cold_boot()` (`src/platform/sdl_app.cpp:406-411`, `src/gui/qt_app.cpp:506-513`, `src/platform/headless_app.cpp:689-692`), which placement-news a fresh `Emulator` and leaves it *running* — only the breakpoint set, the active flag and the mute mask survive; "the transient run/step state (paused, step mode, trace log) is intentionally not restored" (`src/platform/emulator_boot.h:112-124`, `:134-144`). A ZRCP session cannot be served by that: DeZog sends `hard-reset-cpu` **first** with its default `resetOnLaunch: true`, then `enter-cpu-step`, then `smartload` in the same drain (§1.7). **Decision (a)**: the verb completes *synchronously inside `pump`* under the contract of §4.6 — the machine is reconstructed before the reply, stays paused if it was paused, the session's subscriptions and attachment survive, and later commands in the same drain see the new machine — so the reply is `''` and the machine sits at PC=0000 of `nextboot.rom`, exactly like [T3]. REQ-zrcp-15 **ACCEPTED** (backend v4: `CAP-CTL-12` + `CAP-SES-07 set_loop_driver(LoopDriver{cold_boot, load})` / `on_cold_boot_done`); the only error case is a build with no cold-boot driver registered (`RefusedUnavailable` → `Error. Unsupported in jnext: hard-reset-cpu`) | `CAP-CTL-12 Hard`, `CAP-SES-07` |
| `reset-cpu` | S | `CAP-CTL-12 Soft` — `Emulator::soft_reset()` is synchronous (`src/core/emulator.h:189-195`), so no ordering issue | `CAP-CTL-12` |
| `generate-nmi` | S | the physical NMI button = Multiface NMI | `CAP-IN-04 press_nmi(Mf)` |
| `smartload "<file>"` | S | load a `.nex/.sna/.szx/.z80/.tap/.tzx` into the running machine as `--load` does; per ZEsarUX's own rule the adapter enters step mode if not in it and leaves it afterwards. **Which `CAP-CTL-15` path**: the **direct** one — `emulator_apply_load()` → `Emulator::load_nex()` etc. (`src/platform/emulator_boot.h:25-42`, no cold boot) — so a paused caller stays paused **at the new PC** (a NEX's boot-hold frames run on the next `run`). The GUI's other route, cold boot then `ColdBootHooks::schedule_load` N frames later (`emulator_boot.h:243-245`), is *not* what `smartload` takes: on that path a paused caller would sit at PC 0x0000 of `nextboot.rom` with the load still pending, which is not ZEsarUX's `smartload` ("load into the current machine") | `CAP-CTL-15 load(path)`, direct path (REQ-zrcp-12 ACCEPTED) |
| `load-binary "<file>" addr len` | S | read the host file, `poke(Cpu, addr, bytes)`; `len 0` = whole file | `CAP-INS-02 poke` |
| `save-binary "<file>" addr len` | S | `peek(Cpu, …)` to a host file | `CAP-INS-02 peek` |
| `snapshot-save <file>` / `snapshot-load <file>` | S | **in-memory bookmark keyed by the given string** (DeZog appends `.zsf` to its `-state` name; the string is only a key) — the session's bookmark, no file is written; `snapshot-load` of an unknown key → `Error. No snapshot saved under that name in this session`; `help` says so (Divergence: ZEsarUX writes a ZSF file). Disk persistence is JNS via the GUI / `CAP-CAP-04`, not this command | `CAP-CAP-03 bookmark_save(name)` / `bookmark_restore(name)` (REQ-zrcp-13 DECIDED) |
| `send-keys-ascii`, `send-keys-string`, `send-keys-event` | U (v1) | `Error. Unsupported command in jnext: …` — `CAP-IN-01` could serve them; ZEsarUX's key vocabulary differs from jnext's `--delayed-keypress` names and no client sends them. Candidate for v2. | — |
| `get-snapshot` / `put-snapshot` (ZSF over the console) | U | | — |

### 2.3 Inspection

| Command | Class | Notes | Backend |
|---|---|---|---|
| `get-registers` / `gr` | S | §1.4 format byte-for-byte; `MEMPTR` from `Z80Registers::MEMPTR`; `VPS: 0` constant (ZEsarUX's "video pause state", meaningless here — stated in `help`); `MMU=` as §2.3.1 | `CAP-INS-01`, `CAP-INS-03` |
| `set-register NAME=VALUE` | S | names: all 8/16-bit registers incl. `'` forms, `I`, `R`, `PC`, `SP`, `IX`, `IY`, `IFF1`, `IFF2`; **and `IM`** (Divergence: ZEsarUX 12.0 cannot set IM [T4]; jnext can and does — `CAP-INS-01 set_register`); unknown → `Error changing register`; reply = full register line | `CAP-INS-01` |
| `get-memory-pages [verbose]` | S | `RO RO A10 A11 A4 A5 A0 A1 ` — `RO` for a ROM slot, `A<page>` for RAM (`SlotInfo.is_rom/effective`) | `CAP-INS-03` |
| `read-memory [addr] [len]` | S | `peek(Cpu)`; bare form = 64 KB; `len` beyond 64 KB wraps around the address space (ZEsarUX serves it too, [T1]; DeZog never asks for more than 64 KB per chunk) | `CAP-INS-02` |
| `write-memory addr b…` / `write-memory-raw addr HEX` | S | `poke(Cpu)`; ROM silently unchanged (as ZEsarUX); a write that lands in ROM is *reported* in `help` as ignored, not on the wire (DeZog reads back after `write-memory` and shows the mismatch itself) | `CAP-INS-02` |
| `hexdump addr len`, `get-crc32 addr len` | S | formats of §1.4; CRC-32 (IEEE) over `peek(Cpu)` | `CAP-INS-02` |
| `disassemble [addr] [n]` / `d` | S | `  %04X %s` per line, operands upper-case hex without `H` — **jnext's disassembler prints `$4000`-style operands** (`src/debug/disasm.cpp:110-146`; `src/debug/disasm_text.h:44-46` documents the `$XXXX` rule), so the adapter post-processes `$XXXX` → `XXXX`. Column 7 is where DeZog reads the mnemonic (§1.3) — pinned by a test | `CAP-INS-11` |
| `evaluate <expr>` / `e` | S | same compiler as conditions (§3), evaluated once against `CAP-INS` reads; decimal result; parse failure → `Error parsing` | `CAP-INS-01/02/03` |
| `get-tstates` | S | `Time.cycle_in_frame / MachineInfo.cpu_divisor` (v3 keeps the in-frame count in master cycles) | `CAP-INS-07`, `CAP-INS-19` |
| `get-tstates-partial` / `reset-tstates-partial` | S | adapter keeps `base = tstates_total` at reset; prints `%09llu`; "OVERFLOW" never (64-bit) | `CAP-INS-07 tstates_total` (REQ-zrcp-07 CONFIRMED) |
| `get-stack-backtrace [n]` | S | `%04XH ` × n from SP | `CAP-INS-02`, `CAP-INS-01` |
| `extended-stack enabled yes\|no` / `get n [index]` / `clear` | S | `enabled yes` turns on call tracking; `get`: for each word at `index+2i` a `CallFrame` with `sp_at_call == that address` gives `call` / `rst` / `maskable_interrupt` / `non_maskable_interrupt`, otherwise `default` (jnext does not distinguish `push`; DeZog only keys on `call`, `rst`, `*interrupt*`) | `CAP-INS-12 call_stack()` + enable |
| `cpu-history enabled\|started yes\|no`, `is-enabled`, `is-started`, `set-max-size n`, `get-max-size`, `get-size`, `clear`, `get i`, `get-extended i`, `get-pc start n`, `ignrephalt yes\|no`, `ignrepldxr yes\|no` | S | over the TraceLog: entry `i` (0 = newest) formatted as §1.7 with `(PC)=` the 4 opcode bytes, `(SP)=` and `MMU=` from the entry (REQ-zrcp-08 ACCEPTED: `TraceEntry` gains I, R, IM, IFF1/2, the (SP) word and the 8 effective pages; `trace_resize`/`trace_clear` added); `ignrephalt`/`ignrepldxr` are **adapter-side view filters** (skip an entry whose PC equals the previous entry's and whose opcode is `76` / `ED B0` / `ED B8`), not backend state; `restore i` → D (`Error. Unsupported in jnext: cpu-history restore — use the jnext debugger's Step Back`) | `CAP-INS-13` |
| `cpu-code-coverage enabled yes\|no`, `clear`, `get` | S | executed-PC set since `clear`; `get` prints `%04x ` per address in ascending order (`0136 `, [T3]); one bit-set per instruction while enabled, nothing when off | `CAP-INS-20 coverage` (REQ-zrcp-09 ACCEPTED) |
| `get-breakpoints [i] [n]`, `get-breakpointsactions`, `get-membreakpoints [addr] [n]` | S | from the adapter's slot table / range map (§4) | — |
| `tbblue-get-register i` / `tbblue-set-register i v` | S | `%02XH`; set = debugger write (not a `NextRegWrite` event) | `CAP-INS-04 nextreg_peek/write` |
| `tbblue-get-sprite i [n]` / `tbblue-set-sprite i b0 b1 b2 b3 [b4]` | S | 4 or 5 bytes per line per attr3 bit 6; set is a direct engine setter, not a port 0x57 write (no auto-increment, no guest traffic) | `CAP-INS-08` / `set_sprite_attr_raw` (REQ-zrcp-10) |
| `tbblue-get-pattern i 4\|8 [n]` / `tbblue-set-pattern …` | S | 4-bit: 128 bytes from the 8-bit pattern index `i` (VHDL half-pattern addressing) | `CAP-INS-08` pattern RAM / `write_pattern_ram` (REQ-zrcp-10) |
| `tbblue-get-palette ula\|layer2\|sprite first\|second i [n]` / `tbblue-set-palette …` | S | 9-bit `%03X` — jnext stores RGB333 (`palette.h:480-483`) | `CAP-INS-15 palette()` / `set_palette(id, index, rgb333)` (REQ-zrcp-10 ACCEPTED) |
| `tbblue-get-clipwindow L` / `tbblue-set-clipwindow L x1 x2 y1 y2` | S | read from the **live layer state** (`Ula::clip_x1()` etc., `ula.h:206-211`; NR 0x18-0x1C are rotating 4-write registers whose `peek` is only the last byte, so they cannot be the source); set via four `nextreg_write`s of 0x18-0x1B after an 0x1C index reset | `CAP-INS-15 clip_window(Layer)` (REQ-zrcp-11 ALTERNATIVE), `CAP-INS-04` for set |
| `write-port port value` | S | `CAP-INS-05 port_out` — a control action, documented as perturbing | `CAP-INS-05` |
| `get-tstates` / `get-cpu-turbo-speed` | S / U | | |

#### 2.3.1 The `MMU=` projection — where DeZog's model and jnext's disagree

DeZog's Next model has **two** ROMs (`0xFC/0xFD` = ROM0 halves, `0xFE/0xFF` =
ROM1 halves, zxnextmemorymodels.ts:59-64) decoded from `0x8000 + k`. jnext has
four (`Mmu::current_rom_bank()` 0..3, `src/memory/mmu.h:1018`) — and the
adapter reads the bank **from `CAP-INS-03` alone**: a ROM slot is mapped by
`map_rom_physical(0, sram_rom*2)` / `(1, sram_rom*2+1)` (`src/memory/mmu.cpp:546-547`,
`:396-399`), so for a slot with `SlotInfo.is_rom` the bank is
`rom_bank = SlotInfo.effective_page >> 1`. The adapter emits
`0x8000 | ((rom_bank & 1) << 1) | (slot & 1)`, so ROM 3 (48K BASIC, the one a
NEX runs under) reads as DeZog's "ROM1 (ZX Basic)" and ROM 0 (128 editor) as
"ROM0" — the two DeZog names. ROMs 1/2 (+3DOS, +3 syntax) fold onto them. This
is a projection limitation, stated in `help get-registers`, and it is strictly
better than ZEsarUX 12.0's own `0000` for ROM slots [T1]. For a RAM slot the
value is the 8K page. The `SEG<n>=<value>` condition term uses the **same**
encoding (§3.2), so a DeZog long-address breakpoint compares like for like.

Cross-checked with `design-dzrp` (2026-09-26): the DZRP adapter's projection
is deliberately different — DeZog's `cspect` remote uses
`MemoryModelZxNextOneROM` (one ROM: `0xFE` in slot 0, `0xFF` in slot 1) and
DZRP carries the raw NR 0x50-0x57 value, so it reports the `0xFF` sentinel
and DeZog rewrites slot 0 itself. Each adapter serves what *its* DeZog remote
expects; the two must not be made to match. (DZRP additionally cannot carry
ROM `0xFF` in a long address — `bank+1` overflows a byte — which ZRCP's text
format does not suffer.)

### 2.4 Breakpoints

| Command | Class | Semantics | Backend |
|---|---|---|---|
| `enable-breakpoints` / `disable-breakpoints` | S | session master switch (`Error. Already enabled/disabled` as ZEsarUX) | `CAP-EVT set_client_enabled(cid, bool)` (REQ-zrcp-03 ACCEPTED): live = master && client_enabled(owner) && sub.enabled; the GUI's global master is untouched |
| `set-breakpointaction N [action]` | S | empty / `menu` / `break` → Stop (the only action DeZog uses); `prints <s>` / `printregs` / `printe <e>` / `printc` → Log to the client as `log> …` lines (CAP-EVT `Log` verdict, adapter formats); everything else (`call`, `write`, `set-register`, `putv`, `quicksave`, `*-transaction-log`, `save-binary`, `disassemble`, `reset-tstatp`) → `Error. Unsupported breakpoint action in jnext: <a>` | `CAP-EVT` action/verdict |
| `set-breakpoint N [cond]` / `sb` | S | master off → `Error. You must enable breakpoints first` (checked **first**, as ZEsarUX does: `set-breakpoint 0 …` with the master off answers that, not the range error, [T1]); then `1 ≤ N ≤ 100` else `Error. Index out of range` [T2]; empty → clears the slot; compiles the condition (§3): failure → `Error. Error setting breakpoint`, slot unchanged (Divergence: ZEsarUX leaves it `None`); success stores text + compiled form; if the slot is enabled the backend subscription is (re)created | `CAP-EVT subscribe/unsubscribe` |
| `enable-breakpoint N` / `disable-breakpoint N` | S | flips the slot flag; subscription created/removed accordingly (never a backend `set_enabled` on a shared object — each slot owns its subscription) | `CAP-EVT` |
| `set-breakpointpasscount` | U | `Unknown command` — exactly what 12.0 says [T1], and DeZog does not send it below 12.1 | — |
| `set-membreakpoint addr type [items]` | S | updates the 64 KB type map (§4.2), rebuilds the range subscriptions: `MemRead[lo,hi]` for runs of type 1, `MemWrite` for 2, both kinds for 3, action Stop | `CAP-EVT MemRead/MemWrite` range filter |
| `get-membreakpoints [addr] [n]`, `clear-membreakpoints` | S | from the map | — |
| stop reporting | S | on `Paused{reason}` while a `run`/`cpu-step-over`/`run n` is in flight: `Breakpoint(id)` owned by slot N → `Breakpoint fired: <slot text>\n`; `Watch(id)` from the range map → `Breakpoint fired: Memory Breakpoint Write Address: FFFEH\n` (`Read` for a read hit; address and kind from the event payload); `Client(other)`/`User` → no `fired` line (as a data-sent stop — DeZog shows a plain stop); `Magic` → `Breakpoint fired: Magic breakpoint\n`; `Corrupt` → `Breakpoint fired: Machine corrupt after failed rewind\n`. Then registers + ` TSTATES:` + disasm + prompt | `CAP-SES-02`, `CAP-CTL-13` |

### 2.5 What each decline does to DeZog (so the subset is honest *and* usable)

With every REQ answered, only one DeZog-sent command draws an error:

| Declined | DeZog effect | Mitigation |
|---|---|---|
| `set-debug-settings 32` (bit 5, step over interrupt) | only with `skipInterrupt: true` (default false) | error shown once at launch; the session continues; documented |
| `run verbose` / `no-stop-on-data` / `update-immediately`, `cpu-history restore`, non-break/print breakpoint actions, `exit-emulator` | never sent by DeZog | telnet users get the error text |

(`cpu-code-coverage`, `smartload` and `snapshot-*` were pending in v1 and are
served in v2 — CAP-INS-20, CAP-CTL-15, CAP-CAP-03.)

---

## 3. Conditions

### 3.1 The decision

ZEsarUX breakpoints are conditions and nothing else: there is no "address"
field, `PC=nnnn` is just a term, and `SP>=8000H` is a legal, PC-free
breakpoint that DeZog relies on for step-over (§1.6). The backend's `CAP-EVT`
condition is a **predicate callback**, deliberately not an expression language
(`backend.md` §4.3). So the adapter must own a compiler from ZEsarUX's grammar
to a predicate — and there are two ways to get one:

- **(a)** an adapter-private parser for the ZEsarUX dialect over `CAP-INS`
  reads; or
- **(b)** a token-level translation of the ZEsarUX dialect into the DSL's
  expression syntax (#26), compiled by the DSL's expression compiler exposed as
  a library, yielding the same `std::function<bool(const Event&, const
  Debugger&)>` the DSL uses.

**(b) is chosen**, provided `design-dsl` exposes `compile_expr(text) ->
Predicate` with the vocabulary in §3.2 (REQ sent to `design-dsl`, mirrored as
REQ-zrcp-02 to the backend so the ledger sees the dependency). One evaluator,
one grammar of record (the DSL's), and DeZog's own `convertCondition()` is the
proof that the two dialects are a near-bijection (§1.6). (a) is the fallback
if the DSL's expression subset lands later than #280, and the translation
table below is the spec either way.

### 3.2 The honoured subset and its translation

| ZEsarUX | Meaning | Translated to (DSL expression, spellings confirmed by `design-dsl` 2026-09-26) | Backend read |
|---|---|---|---|
| `A B C D E F H L AF BC DE HL IX IY SP PC I R` | registers | same names | `CAP-INS-01` |
| `AF' BC' DE' HL'` | alternate 16-bit | `AF2 BC2 DE2 HL2` (a quote is a string delimiter in the DSL) | `CAP-INS-01` |
| `A' F' B' … L'` | alternate 8-bit | `((AF2 >> 8) & 0xFF)`, `(AF2 & 0xFF)`, … — the DSL has no 8-bit alternates | `CAP-INS-01` |
| `FS FZ FP FV FH FN FC` | flag bits | `SF ZF PF PF HF NF CF` builtins (`FV` = `PF`) | `CAP-INS-01` |
| `IFF1 IFF2` | interrupt FFs | `IFF1 IFF2` (also `IM`, `HALTED` exist) | `CAP-INS-01` |
| `SEG0..SEG7` | MMU slot value **in the §2.3.1 encoding** (ROM → `8000h+k`) | the adapter wraps `page[n]` + `SlotInfo.is_rom` into the encoding (`is_rom ? 0x8000 \| ((page[n] >> 1) & 1) << 1 \| (n & 1) : page[n]`) — not a DSL builtin (the DSL's `mmu[n]` is the raw NR 0x50+n with the 0xFF sentinel, `page[n]` the effective page) | `CAP-INS-03` |
| `ROM` / `RAM` | 128K: ROM index at 0000; bank at C000 | `rom_bank()` is **not** in the DSL v1; the adapter derives ROM as `page[0] >> 1` (§2.3.1 rule) and RAM as `page[6] >> 1`; `paging_ports().7ffd` bit 4 / `1ffd` bit 2 is the equivalent route | `CAP-INS-03` |
| `PEEK(x)` / `PEEKW(x)` | logical memory | `mem[x]` / `mem16[x]` | `CAP-INS-02` |
| `OPCODE1..4` | bytes at PC, MSB-first | `mem[PC]`, `(mem[PC]<<8)|mem[PC+1]`, … | `CAP-INS-02` |
| `= <> < > <= >=` | comparisons | `== != < > <= >=` | — |
| `AND OR NOT(x)` | logical | keywords `and or not` | — |
| `XOR` | logical xor | `(a or b) and not (a and b)` (the DSL has no logical xor) | — |
| `+ - * / & | ^` | arithmetic / bitwise | same (`%`, `~`, `<<`, `>>` also exist) | — |
| `nnnnH`, `0FFh`, decimal | literals | `0xnnnn`, decimal | — |
| `( ) [ ] { }` | grouping | `( )` | — |

Integers are 32-bit signed in the DSL; ZEsarUX's are unsigned 32-bit — for the
16-bit quantities every DeZog condition uses this is invisible, and it is
stated in `help set-breakpoint`. Division by zero: predicate false + logged
(DSL rule). ZEsarUX precedence was not probed; the adapter parenthesises every
binary term it emits, so the DSL's precedence table is the only one in play.

**Declined terms** — `set-breakpoint` answers `Error. Error setting breakpoint`
(ZEsarUX's own text for a bad expression) and `help set-breakpoint` lists
them: `MRA MRV MWA MWV PRA PRV PWA PWV` (last-access variables — ZEsarUX's way
of writing a value-predicated watch; DeZog never sends them and the DSL
expresses that case natively as a `MemWrite` handler), `INTFIRED OUTFIRED
INFIRED ENTERROM EXITROM` (stateful), `EPC COPPERPC HILOWMAPPED PD765PCN`, Z88
`D0..A7`, TSConf `P1..P3 AC ER SR`.

### 3.3 The `PC=` fast path

A condition of the form `PC=<lit>` or `PC=<lit> AND <rest>` (the first term,
after whitespace and case normalisation) becomes an `Execute[lit,lit]`
subscription whose condition is `<rest>` (or none). Anything else becomes
`Execute[0x0000,0xFFFF]` with the whole expression as the condition — the
ZEsarUX "not optimized" case, evaluated at every instruction boundary while
the slot is enabled. That is what DeZog's step-over (`SP>=`) and step-out
(`PC=PEEKW(SP-2) AND SP>=`) cost in ZEsarUX too, and they are armed only for
the duration of one step. `help set-breakpoint` says which form a slot took
(`get-breakpoints-optimized` stays U: nobody reads it).

### 3.4 Coordination points

- `design-dsl` — **ACCEPTED 2026-09-26** (`dsl-frontend.md` §5.5): `compile_expr(text, ctx)`
  → the `CAP-EVT` predicate and a value-returning `eval_expr(text) -> int32`
  for `evaluate`, both library entry points of `src/script/` landing in the
  DSL's WP1; event-free use passes a null `Event` and the compiler rejects
  event-payload names at compile time. Spellings recorded in §3.2. Nothing of
  #280 blocks on it: the fallback parser uses the same table and is deleted
  when `compile_expr` lands.
- `design-backend`: `CAP-EVT` condition signature `(const Event&, const
  Debugger&)` is sufficient — the predicate reads registers/memory/MMU through
  the `Debugger&`. REQ-zrcp-02 **ACCEPTED**: an `Execute[0,0xFFFF]`
  subscription with a predicate is legal and evaluated at every boundary.

---

## 4. Session state kept in the adapter

The backend must not learn any of this. One `ZrcpSession` per accepted
connection (one at a time, §5):

### 4.1 Fields

| State | Type | Set by | Read by |
|---|---|---|---|
| `step_mode` | bool | `enter-/exit-cpu-step`, `smartload`, `snapshot-load` (ZEsarUX rule: enter if needed, restore afterwards) | prompt, `run`, `cpu-step*` gate |
| `in_run` | enum {None, Run, RunLimit(n), StepOver} | `run`, `run n`, `cpu-step-over` | `pump` (any inbound bytes → interrupt), `Paused` handler |
| `cr_mode` | bool | `set-cr` | every write |
| `debug_settings` | uint8 | `set-debug-settings` | `get-debug-settings` |
| `bp_master` | bool | `enable-/disable-breakpoints` | every slot command |
| `slots[1..100]` | `{text, compiled, enabled, action, EventId?}` | `set-breakpoint*`, `enable-/disable-breakpoint` | `get-breakpoints*`, stop reporting (EventId → slot) |
| `mem_types[65536]` | uint8 (0/1/2/3) | `set-membreakpoint`, `clear-membreakpoints` | `get-membreakpoints`; rebuild of range `EventId`s |
| `mem_ranges` | vector<{lo,hi,type,EventId…}> | derived from `mem_types` | stop reporting |
| `tstates_partial_base` | uint64 | `reset-tstates-partial` | `get-tstates-partial` |
| `history` | `{enabled, started, max_size, ign_halt, ign_ldxr}` | `cpu-history …` | `cpu-history get*` (view over `CAP-INS-13`) |
| `extended_stack` | bool | `extended-stack enabled` | `extended-stack get` (turns `CAP-INS-12` tracking on/off) |
| `coverage` | bool (+ backend handle if REQ-zrcp-09 lands) | `cpu-code-coverage enabled` | `get`/`clear` |
| `rx_buf` / `tx_buf` | bytes | pump | pump |
| `ClientId` | from `CAP-SES-01 attach` | connect | every subscription (owner), detach |

### 4.2 Memory-breakpoint map → range subscriptions

`set-membreakpoint addr type items` writes `type` into `mem_types[addr ..
addr+items-1]` (wrapping at 64 KB as ZEsarUX does), then the adapter
recomputes maximal runs of equal non-zero type and diffs them against
`mem_ranges`: unchanged runs keep their `EventId`, removed runs are
unsubscribed, new runs subscribed (`MemRead` for 1, `MemWrite` for 2, both
for 3; action Stop; no condition). DeZog's WPMEM ranges are few and large, so
the diff is trivially cheap and never per-instruction.

### 4.3 The `run` state machine

```
prompt ──run──▶ emit "Running until …\n"; CAP-CTL-02 run(); in_run=Run
   │                                   │
   │            bytes arrive on socket ─┤──▶ CAP-CTL-01 pause(); discard line
   │            Paused{reason} arrives ─┘
   │            Reset{Hard} arrives ───┘  (machine NOT paused; reply built from the event, §4.6 rule 4)
   ▼
 stop reply: [Breakpoint fired: …\n] + registers " TSTATES: n" + "\n  PC DISASM\n" + prompt
```

`Paused` arrives synchronously from the backend (`CAP-SES-02`, on the
emulation thread, inside `run_frame` at an instruction boundary). The
listener only *queues* the stop reply; it is written from the next `pump`
(`backend.md` §5: notifications are flushed in `pump`). The register line is
built at pump time, not at delivery time — the machine is stopped, so the two
are the same values.

### 4.4 Paused-by-someone-else and resumed-by-someone-else

- `Paused{by: other}` while `in_run != None` → the run ends with a plain stop
  reply (§2.4). DeZog shows the stop with no reason; correct.
- `Paused{by: other}` while idle at the prompt → nothing is sent (ZEsarUX has
  no unsolicited messages except `log>`; DeZog has no state for one). The
  session's `step_mode` is **not** changed by it: `step_mode` is what the
  client believes, and the prompt reflects the client's belief, not the
  machine's. `get-registers` still answers live.
- `Resumed{by: other}` while the client is in step mode → the next `cpu-step`,
  `cpu-step-over`, `run n` first calls `CAP-CTL-01 pause()` (idempotent) and
  then proceeds, so a GUI "Run" under a DeZog session cannot make a DeZog step
  execute a frame. `run` simply resumes (already running → `Result` no-op, the
  run reply still arrives on the next pause).
- The GUI sees `Paused{by: zrcp client}` and refreshes as on any breakpoint
  (`backend.md` §4.1) — no ZRCP knowledge on its side.

### 4.5 Disconnect

Socket close, `quit`, or a fatal parse: `CAP-SES-01 detach(cid)` — the
backend removes this client's subscriptions and, if the machine is paused *by
this client*, passes that pause to a remaining client, or resumes the machine
when no other arming client remains (owner question 2 in `backend.md` §13, as
refined by the owner's N1 decision of 2026-10-01 — §11.6; a crashed DeZog must
not leave the machine hung). `step_mode` dies with the session.

### 4.6 What a session observes across a hard reset (cold boot) — REQ-zrcp-15

Today's hard reset is a deferred reconstruction (§2.2 `hard-reset-cpu`): the
`Emulator` object is destroyed and placement-new'd at the same address
(`src/platform/emulator_boot.h:112-124`), the frontend's `ColdBootHooks`
sequence runs around it (`:156-242`), and only the breakpoint set, the active
flag and the mute mask are carried across. For a remote session that would
mean: reply `''` now, the actual boot one tick later, the pause gone, the
client's subscriptions gone unless the backend re-applies them, and every
command DeZog sent in between executed against a machine that is about to be
destroyed.

The contract the adapter designs against — REQ-zrcp-15, **ACCEPTED in full**
by backend v4, rule 4 corrected in v5 (CAP-CTL-12 + `CAP-SES-07
LoopDriver{cold_boot, load}` / `on_cold_boot_begin()` + `on_cold_boot_done()`; `CAP-CTL-15 load()` —
`smartload` — goes through the same loop-owner driver, which changes nothing
above):

1. `CAP-CTL-12 Hard` called from a client (i.e. inside `pump`, which the loop
   owner already calls in the same post-frames slot where it polls
   `take_hard_reset_request()` today) **completes before it returns**: the
   backend invokes the loop owner's registered cold-boot sequence (the
   `ColdBootHooks` driver, `emulator_boot.h:239`) synchronously.
2. The `Debugger` re-binds to the reconstructed `Emulator` (same address, but
   a fresh `DebugState`/`TraceLog`/`CallStack`) and **re-applies every
   attached client's subscriptions and settings** (call-stack tracking,
   trace, coverage) — the client-owned model of `CAP-EVT-09` is what makes this
   mechanical.
3. **Paused stays paused.** If the machine was paused when the verb was
   called, it is paused at PC=0000 of `nextboot.rom` afterwards — which is what
   ZEsarUX does in cpu-step mode ([T3] `hard-reset-cpu` → `PC=0000`) and what
   DeZog's launch sequence relies on. If it was running, it runs.
4. `Reset{Hard}` is delivered to all listeners **before** the verb returns;
   the requesting session gets its `''` reply after that. **A client's
   `reset(Hard)` never pauses a running machine and there is no `Reset`
   pause reason** (backend v5 / arch Rev 3, resolving round-2 N-1): paused
   stays paused (PC 0x0000), running stays running. A session that was in
   `run` when a reset happens (a *guest* NR 0x02 reset, or another client's
   `reset(Hard)`) has its `run` reply **completed by the adapter from the
   `Reset{Hard}` listener event** — adapter policy, not a pause, so no other
   client sees a stop: the reply is the plain stop shape (no `fired` line)
   with the register line and disassembly taken at delivery (the fresh
   machine at PC=0000). The machine is then *running* while the client sits
   at `command@cpu-step> `, which is exactly the §4.4 case "resumed by
   someone else": the next `cpu-step` / `cpu-step-over` / `run n` pauses
   first, and a `run` simply stays attached to the running machine. Stated
   in `help hard-reset-cpu` as the one place a ZRCP register line can
   describe a machine that has since moved on.
5. A guest-initiated hard reset (NR 0x02 bit 1 from Z80 code) keeps today's
   deferred path, but the backend delivers the same `Reset{Hard}` and applies
   rules 2-3 when the loop owner performs it (the loop owner calls
   `on_cold_boot_begin()` before the destroy — so a paused session stays
   paused, pause owner intact — and `on_cold_boot_done()` after; `begin` added
   in B3 by owner decision 2026-09-28), so a session that was in `run` is told
   (rule 4). Ordering within one tick: the loop owner's flag poll
   **precedes** `pump()`, so a guest reset and a client `hard-reset-cpu` in
   the same tick run in that order and the second reboots the fresh machine —
   legal, and the client gets the machine it asked for.
6. With no cold-boot sequence registered (a bare test harness), the verb
   returns `Result::RefusedUnavailable` and the adapter answers
   `Error. Unsupported in jnext: hard-reset-cpu` — never a silent `''`.

Why (a) and not (b) "serve it as a soft reset": DeZog's default launch
(`resetOnLaunch: true`) sends `hard-reset-cpu` once per F5 as a *clean slate*
before `smartload`; a soft reset preserves RAM and would silently ship a
different machine under the loaded program, while a faithful cold boot costs
nothing observable in step mode (the machine is paused at PC=0 until
`smartload`, and a NEX load re-initialises from the warm-start recording
anyway). With the REQ accepted, (b) is gone; the only error the adapter can
answer is the no-driver case, and it is an *error*, not a soft reset in
disguise.

---

## 5. Transport and loop ownership — agreed with `design-gdb` / `design-dzrp`

One model for the three socket adapters (proposal from `design-gdb`,
2026-09-26, accepted with the amendments marked ✚; both amendments were
accepted by `design-gdb` (recorded in `gdb-rsp-frontend.md` §6.2/§8 — a
z88dk `stepi` is 4-5 round trips, so it needs the drain too) and by
`design-dzrp` (compatible with its REQ-dzrp-8/9) the same evening):

1. **Single-threaded, no mutex.** Each adapter is a service object with
   `poll()`; the loop owner (Qt tick `src/gui/qt_app.cpp:474`, SDL loop
   `src/platform/sdl_app.cpp:378`, headless `src/platform/headless_app.cpp:619`)
   calls `Debugger::pump()` (`CAP-SES-03`) once per tick, **running and
   paused**. Today the paused loops skip only `run_frame`
   (`src/platform/frame_sequencer.h:209`), so the tick keeps coming; the
   headless loop needs the `pump(wait)` form from `backend.md` §5.
2. **Sockets** go through the **public** `esp::` seam every protocol adapter
   uses: `esp::make_socket_listener(bind_address)` → `EspListener`
   (`open(port)`, `accept`, `port()`; `src/esp01/include/esp01/esp_socket.h:509-561`)
   yielding an `EspTransport` per accepted connection (`:258`); the include
   directory is `PUBLIC` (`src/esp01/CMakeLists.txt:31`) and the twins are
   already Windows-portable. `esp_socket_platform.h` is private to the esp01
   module (`:10-12`) and is **not** referenced — the v1 text that cited
   `open_listener`/`accept_nonblocking` was wrong (review R-3).
3. **Latency** ✚ — ZRCP needs one amendment: DeZog's queue is strictly
   one-command-in-flight (§1.2), and a single DeZog step is **~15 sequential
   round trips** (`cpu-step`, `get-registers`, `extended-stack get`,
   `cpu-code-coverage get` + `clear`, then `cpu-history get 0..9` — §1.3, §1.7).
   At one command per 20 ms tick that is 300 ms per F11 in the GUI, against
   ~1 ms in ZEsarUX. So `pump` must **drain**: after answering a command,
   wait up to `idle_ms` (≈2 ms) for the next complete line and answer it too,
   until the socket is quiet or a `budget_ms` (≈10 ms, half a tick) is spent
   — and only while the machine is paused, where the tick has nothing else to
   do. Running: `pump(0)` as proposed. Sent as REQ-zrcp-01; `design-gdb`
   benefits identically (a gdb step is several packets).
4. **Arbitration**: ONE pause state, any client may pause/resume, no
   ownership lock; adapters observe `Paused`/`Resumed` edges with the
   originating client and translate (§4.4). Subscriptions are per-client
   (`CAP-EVT-09` owner) so a detach removes only its own. ✚ The ZRCP master
   switch (`enable-/disable-breakpoints`) is per-session, not the backend's
   global master (REQ-zrcp-03) — a DeZog launch must not silently suspend the
   breakpoints a human set in the Qt window.
5. **One client per ZRCP listener.** A second connection is answered
   `Error. Another ZRCP client is connected\n` and closed. ZEsarUX's
   multi-client behaviour was not probed and no client needs it.
6. **CLI** — rows for `src/core/cli_options.h` (arity, `Doc::Documented`,
   metavar, help; the man-page entry goes in `doc/man/jnext.1.md` OPTIONS so
   `make cli-check` stays green). **One port rule for the three
   `--<proto>-port` rows** (review R-5; proposed to `design-dzrp`/`design-gdb`
   2026-09-26 night): *absent = off*; **`0` = an OS-chosen ephemeral port,
   logged at startup as `zrcp: listening on 127.0.0.1:NNNNN`** — which is
   exactly `EspListener::open(0)`'s contract (`esp_socket.h:518-527`) and what a
   CI row needs; no protocol port has a default value; no "0 = off" —
   **confirmed by the owner (2026-09-27, Q8): ports are explicit, the
   listener is off unless `--zrcp-port` is given.** The
   `--debug-listen-address` row below is `design-dzrp`'s text, adopted
   verbatim by all three files (2026-09-26 night):

   ```
   { "--zrcp-port", 1, Doc::Documented, OptId::ZrcpPort,
     "PORT",
     "Serve the ZEsarUX remote command protocol (ZRCP) on TCP PORT\n"
     "so ZRCP clients such as DeZog (\"remoteType\": \"zrcp\") can\n"
     "drive the debugger. 10000 is the port those clients assume.\n"
     "Off unless given; PORT 0 binds an OS-chosen port and logs it.\n"
     "One client at a time." },
   { "--debug-listen-address", 1, Doc::Documented, OptId::DebugListenAddress,
     "ADDR",
     "Bind address for the debugger protocol ports (--dzrp-port,\n"
     "--zrcp-port, --gdb-port). Default 127.0.0.1. A non-loopback\n"
     "address exposes the debugger to the network: none of these\n"
     "protocols has any authentication." },
   ```

   `--debug-listen-address` is shared with DZRP/RSP (one row, whoever lands first; the spelling is `design-dzrp`'s amendment, adopted by `design-gdb` and here).
   Precedent for the shape: `--esp-listen-address` (`cli_options.h:414`).
7. **Headless stop policy** (`CAP-SES-04`): when a ZRCP client is attached,
   a `Stop` must **pause** (and be reported to the client), not exit — the
   client is the thing waiting for it. **Settled by the owner (2026-09-27,
   Q3): a *connected* remote client ⇒ headless `Stop` pauses and notifies;
   none ⇒ exit non-zero.** `zrcp-func` (§6.2) rests on that rule, and the rule
   is now decided, not proposed.

---

## 6. Validation

Three tiers, each stating exactly what it proves.

### 6.1 By hand — `nc localhost 10000`

Proves the byte layout against a human reading of §1: welcome, prompt,
`help`, `enter-cpu-step`, `cpu-step`, `run` + Enter to interrupt,
`set-breakpoint 1 PC=…` + `run` → `Breakpoint fired`. Nothing automated
rests on it; it is the developer's smoke test and the reason the protocol was
chosen first (#280).

### 6.2 Fake client — regression functional row `zrcp-func`

`test/00regression/scripts/zrcp-func.sh` starts `build/gui-release/jnext
--headless --machine 48k --zrcp-port <ephemeral> --inject <fixture>` and
runs a **Python** client (precedent: `esp-server-func.sh`,
`esp-cipdomain-func.sh` already use `python3`) that replays a scripted
conversation and diffs replies **byte-exactly** against expected strings
derived from the [T] transcripts (with jnext-specific fields — version,
machine name — masked). It asserts, in order:

1. welcome + `command> ` prompt; `get-version` coerces to ≥ 10.3;
2. `enter-cpu-step` → prompt suffix; `cpu-step` reply shape (column 7 is the
   mnemonic; ` TSTATES: ` present); `get-registers` labels at the widths DeZog
   reads (§1.4);
3. `set-breakpointaction 1` / `set-breakpoint 1 PC=<addr in fixture>` /
   `enable-breakpoint 1` / `run` → `Running until …` **first**, then
   `Breakpoint fired: PC=…` and PC in the register line equals the address;
4. `run` then a bare `\n` → stop reply without `fired`, and the blank line
   was **not** executed (exactly one prompt follows);
5. `set-membreakpoint <addr>h 2 2` / `run` → `Memory Breakpoint Write Address`
   with the address the fixture writes;
6. a `SP>=` breakpoint ends a `run` past a `CALL` (DeZog's step-over
   mechanism) and `PC=PEEKW(SP-2) AND SP>=` ends one at the `RET` (step-out);
7. `cpu-history get 0` after three steps returns the PC of the third step
   with `(PC)=`, `(SP)=`, `MMU=` present;
8. `read-memory` / `write-memory-raw` round trip; `disassemble` operand
   spelling has no `$`;
9. a declined command answers with an `Error` line and the session survives;
10. `quit` → `Sayonara baby` and the socket closes; the machine resumes
    (headless run continues to `--delayed-automatic-exit-frames` and exits 0).

Row count pinned in `functional_tests.conf` (`expect:` bumps by one). Unit
tier: `zrcp_adapter_test` over the backend's fake `Transport` (`backend.md`
§7) with one row per served command's formatter and one per §3.2 translation,
plus the coverage-on-both-sides rows for slot bounds (0, 1, 100, 101), the
`PC=` fast path vs the general path, and the range-map diff (adjacent runs,
overlapping removal).

### 6.3 Real client — DeZog 3.7.4, `"remoteType": "zrcp"`

Manual, recorded in this file's §6.4 when done (WP-5). **Prerequisite** (review
R-6): the DeZog default launch sends `hard-reset-cpu` before anything else, so
the run with DeZog defaults (`resetOnLaunch: true`) is only meaningful once
REQ-zrcp-15 (§4.6) is accepted and implemented; until then the scenario runs
with `resetOnLaunch: false` and the first assertion below is skipped, and
§2.5's "only one DeZog-sent command draws an error" holds only under that
setting. (REQ-zrcp-15 is now ACCEPTED, so the prerequisite is implementation
order — backend `CAP-SES-07` before WP-3 — not a design gap.) launch.json:
`zrcp: {port: <N>, resetOnLaunch: true}` (the default — assertion 1 is that the
cold boot completes inside the launch and `smartload` lands on the fresh
machine), `load` set to the demo's `.nex`
(`smartload` is served), coverage left at its default (served), `topOfStack`
set from the demo's MAP. Scenario, on `demo/magic_bp_demo` (or any z88dk demo with a
`.map`): F5 attaches (connect sequence of §1.7 completes without a warning
except the two documented ones), source breakpoint hits, F10 over a `call`
(the `SP>=` path), F11 into it, Shift+F11 out (the `PEEKW` path), a WPMEM
range fires, reverse-step 5 instructions (the `cpu-history get` path shows the
right registers and the disassembly aligns with `(PC)=`), `-state save` +
`-state restore` round-trip through the session bookmark, disconnect leaves
jnext running. **Claimed subset
after validation**: everything DeZog's `ZesaruxRemote` sends, with the one
declined option of §2.5 (`set-debug-settings` bit 5) named.

### 6.4 Validation record

_(empty until WP-5 runs)_

### 6.5 Reviewer mutations — pre-diff hypotheses, not the list

These are what the author expects a reviewer to try; the reviewer derives
the real list from the diff (a table built from the author's rows cannot
find a behaviour with no row). Each must be caught by a named row:

| Mutation | Row that must go red |
|---|---|
| prompt emitted as `command> ` in step mode | `zrcp_adapter_test` prompt row; `zrcp-func` item 2 |
| `run` reply prompt sent immediately (before the stop) | `zrcp-func` item 3 (DeZog asserts `Running until` is the whole first reply) |
| interrupting line executed instead of discarded | `zrcp-func` item 4 (two prompts) |
| `fired` line omitted / emitted for a data-sent stop | `zrcp-func` items 3, 4 |
| register line with one space before `F=` / lower-case `IM`/`IFF` swapped | width rows (DeZog offsets) |
| `MMU=` ROM value emitted as the raw `0xFF` sentinel | projection row (expects `8002`/`8003` for ROM 3) |
| `$` left in `disassemble` operands / mnemonic not at column 7 | `zrcp-func` item 8; column-7 row |
| slot range check before master check | order row ([T1] vs [T2] texts) |
| `set-breakpoint` failure clears the slot | slot-unchanged row |
| `PC=` fast path also taken for `PC=x OR y` | fast-path row (must be general) |
| range diff drops an adjacent run / removal of a middle sub-range | range-map rows |
| `cpu-history get 0` returns the OLDEST entry | history-order row (`zrcp-func` item 7) |
| `(PC)=` bytes in host order | byte-order row |
| `run n` runs to completion in one pump | budget row (pump returns with `RunLimit(remaining)`) |
| `hard-reset-cpu` reply before the boot completes / pause lost across it | §6.3 assertion 1; adapter row over a fake cold-boot hook |
| `enable-breakpoints` flips the global master | client-switch row (a second client's subscription still fires) |
| detach leaves the machine paused by the dead client | detach row |

---

## 7. Requirements on the backend — ledger

Sent to `design-backend` 2026-09-26 (format `REQ-zrcp-<n>: <capability> —
<why> — <ZRCP command>`). Status is what the backend recorded in its §12.

| REQ | Capability | Why | ZRCP command | Status (backend v3) |
|---|---|---|---|---|
| REQ-zrcp-01 | `CAP-SES-03 pump(PumpBudget{max_wait_ms, drain_ms, budget_ms})` drains successive commands while paused (answer, wait ≤ drain_ms ≈ 2 for the next complete line, repeat until quiet or budget_ms ≈ 10 spent); `pump(0)` while running | a DeZog step is ~15 sequential round trips; one per 20 ms tick = 300 ms per F11 | all | **ACCEPTED** (also behind REQ-dzrp-9; endorsed by `design-gdb`) |
| REQ-zrcp-02 | `CAP-EVT` predicate on an `Execute[0,0xFFFF]` subscription evaluated at **every** instruction boundary; the compiler is the DSL's `compile_expr` (§3) | ZEsarUX breakpoints are PC-free conditions (`SP>=…`, `PC=PEEKW(SP-2)…`) | `set-breakpoint`, `evaluate` | **ACCEPTED** — cost class "condition-only breakpoint" in backend §8, paid only by a session that set one |
| REQ-zrcp-03 | per-client suspend/resume of *that client's* subscriptions without touching the global master | DeZog's init/quit toggle the master; a human's GUI breakpoints must survive | `enable-breakpoints`, `disable-breakpoints` | **ACCEPTED** → `CAP-EVT set_client_enabled(cid, bool)` |
| REQ-zrcp-04 | `MemRead/MemWrite` range subscriptions with the hit **address and kind** in the `Paused` payload | `Breakpoint fired: Memory Breakpoint Write Address: FFFEH` | `set-membreakpoint`, `run` | **CONFIRMED** → `Paused.reason = Watch{event_id, access, addr}`, `Paused.matched = vector<Hit{event_id, addr, access, value}>` |
| REQ-zrcp-05 | events delivered during `CAP-CTL-03 step_into()`, visible in `pause_reason` | `run n` loop; `cpu-step` onto a watched write | `run n`, `cpu-step` | **ACCEPTED** (delivered at the boundary inside `step_frame_slot`) + new `CAP-EVT probe_execute(pc) -> vector<EventId>` for the loop's post-step check (GH #221 step-off would otherwise hide a match at the resumed-from PC) |
| REQ-zrcp-06 | `RunState.pause_reason` carries the `EventId` / `ClientId` | slot lookup; paused-by-other detection | `run` | **CONFIRMED** |
| REQ-zrcp-07 | monotonic T-states, fps, cpu divisor | `get-tstates-partial`, `get-cpu-frequency` | those | **CONFIRMED** → `CAP-INS-07 tstates_total`, `CAP-INS-19 machine().fps/cpu_divisor` |
| REQ-zrcp-08 | `TraceEntry` + I, R, IM, IFF1, IFF2, (SP) word, 8 MMU effective pages; `trace_resize`, `trace_clear` | DeZog's reverse-step parser asserts on `MMU=`, reads `(SP)=` | `cpu-history *` | **ACCEPTED** (+15 bytes/entry; trace off unless enabled; the (SP) read is inside `InspectionScope`) |
| REQ-zrcp-09 | executed-PC bit set since clear | DeZog enables coverage by default for `zrcp` | `cpu-code-coverage` | **ACCEPTED** → `CAP-INS-20 coverage` (a genuine backend capability, also for the DSL) |
| REQ-zrcp-10 | palette / pattern-RAM / sprite-attribute writes | `tbblue-set-*` | those | **ACCEPTED** → `CAP-INS-15 set_palette`, `CAP-INS-08 set_sprite_attr_raw`, `write_pattern_ram` (direct setters, not port writes) |
| REQ-zrcp-11 | clip-window readback for ULA / L2 / tilemap | `tbblue-get-clipwindow` | that | **ALTERNATIVE** → `CAP-INS-15 clip_window(Layer)` from live layer state (NR shadows are rotating registers and unusable) |
| REQ-zrcp-12 | `load(path)` as `--load` does | DeZog launches via `smartload` | `smartload` | **ACCEPTED** → `CAP-CTL-15 load(path)` (`emulator_apply_load` moves under the backend; paused caller stays paused at the new PC) |
| REQ-zrcp-13 | snapshot save/load semantics | DeZog `-state save/restore` | `snapshot-save/-load` | **DECIDED** → in-memory bookmarks keyed by name: `CAP-CAP-03 bookmark_save(name)` / `bookmark_restore(name)` (the same map DZRP's `CMD_READ/WRITE_STATE` uses); disk = JNS via `CAP-CAP-04` (save only), not this command |
| REQ-zrcp-14 | `CAP-SES-04`: connected remote client ⇒ `Stop` pauses under `--headless` | a client blocked on `run` must get its reply | `run` | **ACCEPTED**; the rule itself settled by the owner 2026-09-27 (Q3) |
| REQ-zrcp-15 | `CAP-CTL-12 Hard` contract across the cold-boot reconstruction (§4.6): synchronous inside `pump` via the loop owner's registered cold-boot sequence; `Debugger` re-binds; attached clients, their subscriptions and settings re-applied; paused stays paused (PC=0000 of `nextboot.rom`); `Reset{Hard}` delivered before return; guest-initiated resets get the same notification/re-apply when the loop owner performs them; `RefusedUnavailable` with no sequence registered | review R-2: the verb is a deferred flag today (`emulator.h:197-208`) and the boot wipes pause/subscriptions (`emulator_boot.h:112-124`); DeZog sends it first with `resetOnLaunch: true` | `hard-reset-cpu` | **ACCEPTED in full** (backend v4, verified against the code: the after-tick flag poll `sdl_app.cpp:409` / `qt_app.cpp:510` / `headless_app.cpp:691`, `emulator_boot.h:122-124`, `:133-146`) → `CAP-CTL-12` + `CAP-SES-07 set_loop_driver(LoopDriver{cold_boot, load})` / `on_cold_boot_done()`; points 1-6 recorded verbatim; guest NR 0x02 resets keep the deferred path and the loop owner calls `on_cold_boot_begin()` before the destroy and `on_cold_boot_done()` after, so 2-4 apply (`begin` added in B3, owner decision 2026-09-28). `design-gdb` aligned its `monitor reset hard` to this REQ without filing a duplicate: served only under REQ-zrcp-15 (client sees an `O` line + `OK`, next `g` shows PC=0000 with its `Z0`s intact, no unsolicited `T05` since the client never resumed); `E01` + explanatory `O` line until accepted — the same stance as §2.2 |

Reach-arounds: **0**. Every served command in §2 names its CAP; the only
`Emulator` knowledge in the adapter is the four-ROM → two-ROM projection
(§2.3.1), which reads `SlotInfo` from `CAP-INS-03`.

---

## 8. Work packages for #280 (parallel implementation agents)

| WP | Scope | Depends on | Tests |
|---|---|---|---|
| WP-1 **Session skeleton (consumes transport package T)** | **No listener and no fake of its own**: it consumes the shared transport package **T** of the architecture doc §10 (one listener over `esp::make_socket_listener` / `EspListener` / `EspTransport`, `esp_socket.h:258/509/561`, plus the in-memory fake `Transport` for unit suites — written once, used by DZRP, ZRCP and GDB). ZRCP's own part is only framing and session: `ZrcpSession` line reader/writer over a `Transport`, `set-cr`, welcome, prompt, `help`/`ls` table, `Unknown command`, U-class replies; the `--zrcp-port` row + man page (the `--debug-listen-address` row lands with T); service registration through `CAP-SES-03` | package T, backend `CAP-SES-01/02/03` | `zrcp_adapter_test`: framing rows (welcome, prompt, blank line, unknown, alias, extra args, `set-cr`) |
| WP-2 **Inspection formatters** | `get-registers`/`set-register` (incl. `MMU=` projection), `read-/write-memory*`, `hexdump`, `get-crc32`, `disassemble` (operand `$` stripping, column 7), `get-memory-pages`, `get-stack-backtrace`, `get-tstates*`, `get-cpu-frequency`, `get-current-machine`, `tbblue-get-*` | `CAP-INS-01..04/08/11/15`, `CAP-TIME-01`, REQ-zrcp-07 | one row per formatter against [T] bytes; register-line width rows (DeZog offsets) |
| WP-3 **Control + run state machine** | `enter-/exit-cpu-step`, `cpu-step`, `cpu-step-over` (ZEsarUX semantics), `run`, `run n` (pump-budgeted), interrupt-by-data, `Paused`/`Resumed`/`Reset` handling (§4.3-4.6), stop reply, `hard-reset-cpu` (per §4.6) / `reset-cpu`, `generate-nmi` | `CAP-CTL-01..06/12/13`, `CAP-SES-02`, REQ-zrcp-05/06/**15** | `zrcp_adapter_test` wiring rows: the machine actually stops (the #203 shape); `zrcp-func` items 1-4 |
| WP-4 **Breakpoints and conditions** | slot table, master switch (per-client), actions (Stop/Log), `set-membreakpoint` map + range diff, condition translation → DSL compiler (or the fallback parser), `PC=` fast path, `evaluate`, `get-breakpoints*`, `get-membreakpoints` | `CAP-EVT`, REQ-zrcp-02/03/04; `design-dsl` compiler | translation rows (each §3.2 line, both directions of every operator), slot bounds 0/1/100/101, fast-path vs general, range diff; `zrcp-func` items 5-6 |
| WP-5 **History, stack, coverage, load** | `cpu-history *` view + filters, `extended-stack *`, `cpu-code-coverage *`, `smartload`/`load-binary`/`save-binary`, `snapshot-*` as bookmarks; the DeZog validation run and §6.4 | `CAP-INS-12/13/20`, `CAP-CTL-15`, `CAP-CAP-03` | history-format rows (`(PC)=` byte order, `(SP)=`, filters), extended-stack typing rows; `zrcp-func` item 7; the DeZog session record |
| WP-6 **Fixture + docs** | commit a trimmed, reviewed subset of the [T] transcripts as `test/fixtures/zrcp/` (the expected-bytes source for `zrcp-func` and the adapter test); user-guide page "Debugging with DeZog over ZRCP" (launch.json, the two settings of §2.5); developer-guide paragraph | WP-1..5 | `docs-check` |

WP-2/3/4/5 are independent of each other once WP-1's session skeleton
exists; WP-4 is the only one gated on another design (`design-dsl`).

---

## 9. Owner decisions (review of 2026-09-27) — nothing open

1. **`hard-reset-cpu` is a faithful cold boot (Q9: yes).** Served as the
   synchronous reconstruction of §4.6 (REQ-zrcp-15). DeZog's default
   `resetOnLaunch: true` therefore works; the user guide / man page still
   **documents `resetOnLaunch: false` as the setting to use when jnext was
   started with `--load`** (the program is already loaded and a cold boot
   before `smartload` only costs the boot), per the owner.
2. **Version string (Q10: yes).** `get-version` answers `12.0-jnext-<ver>`:
   `semver.coerce` reads `12.0.0` (≥ 10.3, < 12.1), the suffix says what it
   is, and it is the only shape that is both truthful and accepted by DeZog.
3. **Unsolicited `log>` lines** for `Log`-action events while the client is
   idle at the prompt: not raised as a separate question in the owner review;
   the design's default stands as decided — send them whenever they happen,
   which is what ZEsarUX does and what DeZog's parser handles anywhere
   (dezog:zesaruxsocket.ts:502-524).
4. **Headless stop policy (Q3: exception adopted).** While a remote client
   is connected, a headless `Stop` **pauses and notifies** instead of exiting
   non-zero (`CAP-SES-04`, set by the loop owner). `zrcp-func` (§6.2) and
   every other `--headless` protocol regression row rest on it, and it is now
   settled.
5. **Ports (Q8).** Explicit: off unless `--zrcp-port` is given; `0` =
   OS-chosen, logged (§5.6).

Noted from the same review, no design change here: the owner allows machine
**mutation** from DSL scripts (the ZRCP condition translation of §3 only
*reads*, so `compile_expr` is unaffected), and DZRP moves to protocol
version 2.2.0 (DZRP-only).

---

## 10. Deliberate divergences from ZEsarUX 12.0 (all stated in `help`)

| ZEsarUX | jnext ZRCP | Why |
|---|---|---|
| `cpu-step-over` on `JR $` never returns | interruptible by socket data | a hung connection is a worse failure than a different stop |
| condition text canonicalised on echo (`PC=38H`) | echoed as given, trimmed | cosmetic; no client parses it; ZEsarUX's pretty-printer is not worth reproducing |
| `set-register IM=n` → `Error changing register` | works | jnext can |
| `MMU=0000` for ROM slots on the Next | `8000h+k` (§2.3.1) | DeZog's decoder expects it; ZEsarUX's value is its own bug |
| a bad condition leaves the slot `None` | slot unchanged | a typo must not silently delete a working breakpoint |
| `run verbose`, `no-stop-on-data`, `update-immediately` | declined | §2.2 |
| `hard-reset-cpu` is instantaneous | is a cold boot, completed synchronously inside the reply (§4.6) | Task 70 semantics; REQ-zrcp-15 |
| stray `\r` in the "not in cpu-step mode" error | plain `\n` | ZEsarUX artefact |
| multiple simultaneous clients (unprobed) | one | nobody needs more; arbitration lives in the backend anyway |

---

## 11. Implementation record — milestone 1 (WP-1 session, WP-2 formatters, WP-3 control / run)

Code: `src/remote/zrcp/zrcp_server.{h,cpp}` (the session, THE command table, the
run state machine) and `src/remote/zrcp/zrcp_format.{h,cpp}` (pure formatters:
register line, `MMU=` projection, flags, disassembly line, hexdump, CRC-32,
memory pages, machine name, numbers). Registered by `DebugServers::start`
(`src/platform/debug_servers.*`) in all three loop owners, beside DZRP;
`--zrcp-port` in `cli_options.h` / `main.cpp` / `EmulatorConfig::zrcp_port`.
Suite: `zrcp_adapter_test` (111 rows after review round 1, `gate: none`). Regression: `zrcp-func`
(§6.2 items 1-4 + DZRP beside it, headless), `zrcp-sdl-func`, `zrcp-qt-func`
(the other two loop owners), and `debug-listen-address-func` extended.

### 11.1 Served in milestone 1 — 47 of §2's 67

- **WP-1 (13):** `about`, `close-all-menus`, `get-buildnumber`,
  `get-cpu-core-name`, `get-debug-settings|gds`, `get-os`, `get-version`,
  `help|?`, `ls`, `noop`, `quit|exit|logout`, `set-cr`, `set-debug-settings|sds`.
  Declined: `exit-emulator`. Unsupported, each by name (57): every other name of
  ZEsarUX 12.0's `ls`, ZEsarUX's aliases included (`a`, `zo`, …).
- **WP-2 (26):** `get-registers|gr`, `set-register|sr`, `read-memory`,
  `write-memory|wm`, `write-memory-raw`, `hexdump|h`, `get-crc32`,
  `disassemble|d`, `get-memory-pages|gmp`, `get-stack-backtrace`, `get-tstates`,
  `get-tstates-partial`, `reset-tstates-partial`, `get-cpu-frequency`,
  `get-current-machine|gcm`, `tbblue-get-{register,sprite,pattern,palette,clipwindow}`,
  `tbblue-set-{register,sprite,pattern,palette,clipwindow}`, `write-port`.
- **WP-3 (8):** `enter-cpu-step|encs`, `exit-cpu-step|ecs`, `cpu-step|cs`,
  `cpu-step-over|cso`, `run|r` (+ *n*), `hard-reset-cpu`, `reset-cpu`,
  `generate-nmi`.
- **Pending: none.** Milestone 2 served WP-4's twelve (§11.7) and milestone 3
  WP-5's eight (§11.9): all of §2's 67. Row `ZRCP-TAB-01` pins the table
  against ZEsarUX's 125 names.

### 11.2 The run state machine as built

`in_run` ∈ {None, Run, RunLimit, StepOver}. While it is not None no line is
executed: any received byte stops the machine (`pause()` if it is still
running) and the line it belongs to is discarded up to its newline — also when
the rest of that line arrives later. The stop reply is **level-triggered** on
`state().paused`, checked at the top of `on_service` (a stop in the frames
before this pump) and in `on_notify` (a stop caused inside this pump's drain,
e.g. another client's pause): `[Breakpoint fired: …\n]` + register line +
` TSTATES: n` + `\n` + the disassembly at PC + the prompt. `fired` is named for
`Magic` and `Corrupt` in this milestone (a slot's breakpoint and memory
breakpoint are WP-4's); a step, a run-to target, data sent, another client's
pause or breakpoint have none. `run n` is a loop of `step_into`, sliced by the
adapter's clock (2 ms) and returning `Serviced` after each slice, so the pump's
drain budget decides how many slices a tick runs; it ends at the count
(`Returning after n opcodes`), on a step whose own event stopped it
(`pause_reason` is then not `Step` — REQ-zrcp-05, fixed in the backend, §11.4),
on landing at a PC `probe_execute` says is armed, or on data. `cpu-step-over`
is `run_to(pc + len)` (asynchronous) except for a RET or JP, which is a plain
step. A `Reset{Hard}` while in flight makes the reply OWED (plain stop, the
fresh machine's registers, the machine not paused): it goes out at the next
callback — this pump's `on_notify` at the latest — through the same finish
path as any stop, so a line sent before it is an interrupt, a parked `run n`
steps the new machine no further, and this session's transients are removed
outside the backend's fan-out. `quit`, a hang-up or a reset of the connection
detach (SES-01).

### 11.3 Deviations and precisions, each with its reason

1. **`set-cr` adds the CR *before* each LF (CRLF), per session**, as §1.1 and
   §4.1 say. ZEsarUX actually adds it *after* (`\n\r`,
   `escribir_socket_add_cr()` in `network.c`), only on some of its writes (not
   the prompt), and as a process-global flag that outlives the connection.
   CRLF is what a Windows telnet wants; the transcripts' form is an artefact
   (finding 1).
2. **The corruption refusal text** is `Error. Machine state is corrupt after a
   failed rewind; acknowledge it in the jnext debugger`, not §2.2's "…
   acknowledge in the GUI or send hard-reset-cpu": CTL-12 `Hard` passes the
   same CTL-11 gate (`reset_hard()` → `execute_gate()`), so the second half of
   the advice would itself be refused (finding 2).
3. **`tbblue-set-*` and `write-port` are served with WP-2.** §2 classes them S,
   but §8 gives them to no work package; they are the write halves of WP-2's
   formatters, as `set-register` and `write-memory` are (finding 4).
4. **`get-buildnumber` answers the jnext version**: the tree generates no build
   id (`version.h` carries the version alone), and a build-time stamp would
   make builds non-reproducible.
5. **`get-current-machine` never answers `Pentagon`**: jnext has no Pentagon
   machine type (`MachineType` is Next / 48K / 128K / +3; `--machine` offers the
   same four).
6. **`§6.2` item 3 in `zrcp-func` uses the magic breakpoint**, a §2.4 named
   stop (`Breakpoint fired: Magic breakpoint`), because `set-breakpoint` is
   WP-4, gated on S's `compile_expr`. The slot form of item 3 lands with WP-4.
7. **`run n` slices by the adapter's own clock** (2 ms, injectable for tests)
   and returns `Serviced` per slice: a `Service` never sees `PumpBudget`, so
   "until the pump's `budget_ms` is spent" is realised as "the pump keeps
   calling while its budget lasts".
8. **`cpu-step-over`'s plain-step set** is ZEsarUX's `si_cpu_step_over_jpret()`
   list plus `JP NZ` (C2, which that list forgets), `JP (IX)`/`JP (IY)`, and
   RETI/RETN (through `is_ret_like()`, as §2.2 says): none has a next
   instruction to run to.
9. **Numbers are strict:** decimal or `H`-suffixed hex, and (review round 1)
   in range: an address past FFFFH and a register value wider than its
   register (`PC=10000H`, `A=100H`, `IFF1=2`, `IM=3`) are refused, never
   truncated as ZEsarUX does. ZEsarUX's `atoi()`
   reads `0x38` or a typo as 0; jnext answers `Error. Invalid number: <tok>`
   (or the command's own ZEsarUX error where it has one). Labels are not
   resolved, and `set-register`'s value is a number, not an expression, until
   WP-4 brings the expression compiler.
10. **Bounds:** a command line ≤ 512 KiB (`Error. Command line too long`, then
    discarded to its newline); `read-memory` / `hexdump` / `get-crc32` ≤ 1 MiB;
    `disassemble` ≤ 65536 lines; `get-stack-backtrace` ≤ 32768 words;
    `tbblue-get-*` items ≤ one bank's worth; `run n` ≤ 1 000 000 (§2.2). Each
    over-bound request answers an error; ZEsarUX would try to produce it.
11. **Malformed writes write nothing:** a byte > 255, an odd or non-hex
    `write-memory-raw` string, a 10-bit colour, six sprite bytes are refused
    before the first byte lands (ZEsarUX truncates, or writes garbage as 0).
12. **`tbblue-*` are served on every machine type** — jnext's hardware is always
    the Next core; ZEsarUX answers `ERROR. Machine is not TBBlue` off TBBlue.
13. **`tbblue-set-sprite i b0 … [b4]`** writes 1-5 bytes from byte 0 and keeps
    the rest (ZEsarUX wraps its index at 4 and can never write a fifth byte).
14. **`get-tstates-partial` restarts at a hard reset** — the new machine's
    monotonic T-state counter restarts (ZEsarUX's partial counter survives its
    reset). Stated in `help get-tstates-partial`.
15. **`get-debug-settings` starts at 1**, ZEsarUX's own initial value ([T1]); §4.1
    does not say.
16. **`help <cmd>`** of an unsupported command answers the unsupported error;
    of an unknown one ZEsarUX's `No help for that command`.
17. **Decision, review round 1 — `MMU=` and `get-memory-pages` per machine.**
    §2.3.1 designed only the Next's eight 8 K slots, but `get-current-machine`
    answers `ZX Spectrum 128k` on a 128K session, and DeZog then decodes `MMU=`
    with its 128K decoder: the FIRST FOUR fields as 16 K slots
    (`decodezesaruxdata.ts`, `>= 0x8000` -> ROM `8 + (v & 1)`). So on the 48K,
    128K and +3 jnext now emits ZEsarUX's own legacy projection
    (`mem128.c`, `debug_paginas_memoria_mapeadas[0..3]`): four 16 K segments,
    `0x8000 + ROM image` or the RAM bank, then four `0000` where ZEsarUX leaves
    its array unset; and `get-memory-pages` spells ZEsarUX's legacy segments
    (`RO<rom> RA<bank>` on 128K/+3, `ROM RAM` on 48K). A segment is read from
    its first 8 K slot. The Next keeps §2.3.1. Rows FMT-11/12, REG-01/06/07,
    PG-01.
18. **`hard-reset-cpu` whose boot fails** answers the same
    `Error. Unsupported in jnext: hard-reset-cpu` as the no-driver case: the
    backend returns `RefusedUnavailable` for both.

### 11.4 Backend defect found and fixed on this branch (with its rows)

**REQ-zrcp-05 was not honoured by `step_into()`.** It armed `Step` *after* the
step, and `arm()` clears the event-stop latch — so a watch the stepped
instruction hit, or the magic opcode it executed, was reported as a plain
`Step` (and the magic / legacy-watch latches of the stop being left survived
the step). `step_into()` now arms before the step, clears the previous stop's
evidence, and lets the step's own event be the reason
(`src/debug/debugger_control.cpp`). No header change. Rows: `CTL-03-04/05`
(backend suite, 1287 → 1289) and `ZRCP-RUN-13/14`.

### 11.5 Findings

1. §1.1's `set-cr` direction (deviation 1), and the "stray `\r`" of [T2]'s
   `run` error is that same flag carried across connections — transcripts 1
   and 2 were one ZEsarUX process — not an artefact of that message.
2. §2.2's corruption advice (deviation 2).
3. **For WP-4:** `probe_execute(pc)` ignores conditions, and the frozen header
   returns `bool` where backend.md §4.3 says `vector<EventId>`. A PC-free
   condition breakpoint (`Execute[0,FFFF]` + predicate) will therefore end
   every `run n` after one step, and the loop cannot tell WHICH subscription
   it landed on (a slot's `fired` text needs it). It also ignores the
   subscription's ACTION: a `Log`-only `Execute` subscription (a DSL logger)
   ends a `run n` exactly as a `Stop` would. WP-4 has to evaluate its own
   conditions and actions in the loop or ask for the header change.
   **Resolved in WP-4** (§11.8): the header change landed, conditions are
   evaluated, and the adapter judges the action.
4. §8 assigns no WP to `tbblue-set-*` / `write-port` (deviation 3).
5. The census holds: ZEsarUX 12.0's `ls` is 125 names, §2's 67 S + 1 D + 57 U
   (`ZRCP-TAB-01`).
6. Under DeZog's `set-debug-settings 0`, ZEsarUX's own `cpu-step` prints only
   the disassembly line (debug-settings bit 0); jnext always prints the
   register line too, which §2.1's "bits 0-4 change nothing" already implies.
   DeZog re-reads the registers after a step, so nothing depends on it.
7. **Review round 1.** (a) §2.3.1 covered only the Next projection; the
   legacy one is decision 17 above. (b) Package G's `poke(Cpu)` contract ("what
   landed; `RefusedReadOnly` if none") is on `main` now; the adapter's
   all-ROM write is still ZEsarUX's silent success, and row MEM-03 now pins it
   (mutant R34 caught after the merge). (c) For the owner (backend contract,
   not adapter): `step_into` attributes the pause to the stepping client, so a
   ZRCP client that steps a machine another client paused, then quits or
   crashes, releases that pause on its detach (CAP-SES-01 as written). DeZog's
   own disconnect sends `exit-cpu-step` first; a telnet or crashed client does
   not. **Decided by the owner (N1, 2026-10-01) and implemented on this branch
   — §11.6.**

### 11.6 Pause ownership on detach (owner decision N1, 2026-10-01)

The owner's words: "The pause should belong to the one remaining." Implemented
in the backend (`src/debug/debugger_session.cpp` `detach()`,
`debugger_control.cpp` `step_into()`, a private `Impl::pause_origin`), so it
holds for every client — ZRCP, DZRP, GDB RSP and Qt alike, since each adapter
only calls `detach()`. No public header changed; the frozen `debugger.h`
comment on `detach()` ("there is no 'last client' condition") is now stale and
is reported for the owner rather than edited. The rule (`backend.md`
CAP-SES-01):

- A detaching client's own pause is **released only when no other arming
  client remains**. Otherwise it passes, and the machine stays paused.
- **The heir** is the client the machine was paused by before the leaver
  stepped it, if that client is still attached; `step_into()` on a paused
  machine keeps that origin across any number of steps, and every other
  control verb ends it. Failing that, the **earliest-attached** remaining
  client (lowest id).
- **Observers do not count** (REQ-qt-32): the Qt GUI keeps one for its whole
  life, and counting it would make every GUI-session detach keep the machine
  paused — the hung machine the release exists to prevent.
- A pending cold-boot capture owned by the leaver follows the same rule.
- Internal step / run targets are still dropped (they are subscriptions).
- The Qt window's close resumes the machine itself before its detach
  (`DebuggerManager::detach_backend()`), a user action this rule does not
  touch.

Rows: backend `N1-01..14` and `CTL-12-47c` (new); `SES-01-12`, `SES-01-22`,
`CTL-12-47`, `REENT-24` and `OBS-08` **rewritten**, because each asserted the
old release with another arming client attached (`REENT-24` and `SES-01-22`
now use an observer bystander, which keeps what they pin: the release itself);
ZRCP `ZRCP-SES-09/10` through the real adapter.

**Owner-confirmed (2026-10-01), all four points of the hand-back:** (1) an
observer never inherits a pause and never holds one back from release; (2) the
fallback heir is the earliest-attached remaining arming client; (3) only
`step_into` keeps the original pauser — so a client that single-steps an
UNOWNED pause (the magic breakpoint; `Corrupt` refuses the step anyway) takes
ownership of it, and its detach then hands it on or releases it like any pause
of its own; (4) the comment-only fix to the frozen `debugger.h` `detach()`
text is approved — **not applied in this build**: the edit was refused by the
session's permission policy for a shared frozen header, and is reported for the
owner to apply or re-authorise (z-m2-report.md).

### 11.7 Milestone 2 — WP-4, breakpoints and conditions

**Served (12):** `set-breakpoint|sb`, `set-breakpointaction|sba`,
`enable-breakpoint|eb`, `disable-breakpoint|db`, `enable-breakpoints`,
`disable-breakpoints`, `get-breakpoints|gb`, `get-breakpointsactions|gba`,
`set-membreakpoint`, `get-membreakpoints`, `clear-membreakpoints`,
`evaluate|e`. 59 served, 1 declined, 57 unsupported, 8 pending (`ZRCP-TAB-01`).

**How a condition becomes a predicate** (`src/remote/zrcp/zrcp_condition.*`):
the text is tokenised exactly as ZEsarUX 12.0's `exp_par_exp_to_tokens` does
(operand / operator alternation, signs, `H` / `%` / `'c'` numbers, `( [ {`
brackets, case-insensitive names); the canonical text `get-breakpoints` lists
and `Breakpoint fired:` echoes is ZEsarUX's `exp_par_tokens_to_exp` over those
tokens; the tokens are then split the way `exp_par_evaluate_token` evaluates
them and every split is emitted as a fully bracketed DSL expression, which
`script::compile_expr` compiles in the `Execute` scope. Nothing in the adapter
evaluates an expression except the native terms below.

**Deviations and precisions (milestone 2):**

1. **ZEsarUX's grouping, not a precedence table.** §3.2 said the adapter
   "parenthesises every binary term it emits, so the DSL's precedence table is
   the only one in play". The source says otherwise: ZEsarUX splits at the
   FIRST top-level logical operator, then comparison, then `+ -`, then the
   other arithmetic, right-nested — `9-3-1` is 7, `2*3&1` is 2, `0 AND 0 OR 1`
   is 0 (`ZRCP-CND-04`). The translation reproduces that grouping.
2. **"On Change", as ZEsarUX's default — for PC-free conditions.** ZEsarUX
   fires a condition only on a false→true transition
   (`debug_breakpoints_cond_behaviour` = 1, `debug.c`
   `cpu_core_loop_debug_check_breakpoints`; `--brkp-always` turns it off). A
   PC-free slot does the same: its predicate keeps the value of its last
   evaluation, is evaluated at every boundary of a free run, and in a `run n` —
   where the GH #221 step-off means the backend evaluates nothing — by the
   landing check (`slot_edge_at`); (re)arming starts it false, as
   `debug_set_breakpoint` does. A fast-path (`PC=<n> …`) slot fires at every
   arrival: the `PC=` term was false at the previous instruction, so On Change
   and Always agree, except for an instruction that jumps to itself (`JR $`),
   where ZEsarUX fires once and jnext on every pass after the step-off — the
   one residual difference, stated in `help set-breakpoint`. (Milestone 2 first
   shipped "Always" for both; review round 1 rejected it: a PC-free condition
   that stays true stopped every `run` after one instruction, and a print
   action on one sent ~10^6 lines a second. Rows `ZRCP-BP-22..24`, `-26/27`.)
3. **The master switch is the adapter's.** §2.4 mapped `enable-/disable-
   breakpoints` to CAP-EVT `set_client_enabled`. That switch also suspends the
   client's TRANSIENT subscriptions (`event_table.cpp`: live = enabled ∧
   (transient ∨ master) ∧ client), so with breakpoints off `cpu-step-over`
   could never end. Instead a slot's subscription exists exactly while
   breakpoints are on, the slot is enabled and it has a condition, and the
   memory-breakpoint ranges likewise (`ZRCP-BP-10`, `ZRCP-MBP-07`).
4. **`set-breakpoint` enables the slot**, as `debug_set_breakpoint` does
   (DeZog's `enable-breakpoint` after it is then a no-op). A refused condition
   leaves the slot as it was (§2.4's divergence, `ZRCP-BP-06`).
5. **Actions.** Empty / `menu` / `break` stop; `prints`, `printregs`,
   `printe`, `printc` do not stop and send their output to the session as a
   `log> …` line (ZEsarUX prints it on its own console); everything else is
   refused at `set-breakpointaction` with `Error. Unsupported breakpoint action
   in jnext: <word>` (`ZRCP-BP-13/14`).
6. **Declined names** are refused at `set-breakpoint` (and by `evaluate`):
   the last-access variables, `TSTATES*`, `SCANLINE`, the stateful `*FIRED` /
   `ENTERROM` / `EXITROM`, `HILOWMAPPED`, `PD765PCN`, `USP`, `EPC`,
   `COPPERPC`, the 68000 / TSConf registers, and the functions `FPEEK IN ABS
   BYTE WORD OPM*` (`ZRCP-CND-09`). Binary (`101%`) and character (`'A'`)
   literals, which §3.2 did not list, are honoured: they cost nothing.
7. **The fired line names this session's breakpoint** even when another
   client's breakpoint was the backend's first `Stop` on the same instruction
   (`ZRCP-BP-11`, `ZRCP-MBP-08`) — a PC-free slot only if it fired (its edge)
   at that instruction (`ZRCP-BP-26`); another client's breakpoint alone gives
   a plain stop (`ZRCP-BP-12`).
8. **Indexes and addresses** keep milestone 1's strict number rule
   (`parse_number`) where ZEsarUX uses `atoi` / `parse_string_to_number`.

### 11.8 The DSL library and ZRCP's condition subset (§11 item 8, measured)

**Answer: the library covers the honoured subset with no fallback parser
(no second evaluator), except for `SEG0..7`, `ROM` and `RAM`, which it cannot
express and the adapter evaluates natively.**

Evidence, every row driving `compile_expr` / `eval_expr` through `evaluate`
or a breakpoint:

- registers, the alternates (8-bit ones as `(AF2 >> 8) & 255` …), the flags
  (`FS…FC` → `SF ZF PF PF HF NF CF`), `IFF1/2` — `ZRCP-CND-05`;
- every operator both ways, `XOR` as `((a)!=0) != ((b)!=0)`, ZEsarUX's x/0 =
  65535 as `((b)==0)*65535 + ((b)!=0)*((a)/((b)+((b)==0)))` — `ZRCP-CND-06`;
- `PEEK` / `PEEKW` / `OPCODE1..4` as `mem[…]` with the address mod 64K —
  `ZRCP-CND-07`; numbers — `ZRCP-CND-08`; grouping — `ZRCP-CND-04`;
- DeZog's conditions end to end — `ZRCP-BP-04/07/09`, `ZRCP-MBP-01/02`, and
  the regression row `zrcp-bp-func` against a live jnext.

What the library could not do, and what was done instead:

- **Is a slot ROM?** `SEGn` / `ROM` / `RAM` are ZEsarUX's
  `debug_paginas_memoria_mapeadas[]`, the `MMU=` projection, whose ROM
  encoding depends on `SlotInfo.is_rom`. The DSL has `mmu[n]` (the raw NextREG,
  `0xFF` sentinel) and `page[n]` (the effective page); neither says ROM — the
  sentinel is not the rule (`mmu.cpp`: a verbatim `0xE0..0xFE` write also maps
  ROM). So they are NATIVE terms: honoured as `<var><op><number>` in the
  top-level AND chain (DeZog's `PC=… AND SEGn=…`, `… AND ROM=…`, `… AND
  RAM=…`), evaluated from the same `mapped_page()` the register line prints,
  and refused anywhere else (`ZRCP-CND-11/12`). A DSL builtin for "slot is
  ROM", or for the projection itself, would remove them — a request for
  package S, not made here.
- **ZEsarUX's grouping** is reproduced by the translation (deviation 1); the
  DSL needs only its brackets.

**`probe_execute -> vector<EventId>` (owner-approved 2026-09-29, landed in
review round 1).** The frozen-header change (`530dd22d3`, applied on the owner's
instruction after the session's permission policy refused it to the author)
returns the id of every live `Execute` subscription covering the PC whose
condition holds now, evaluated against the `Execute` event `execute_gate()`
would build, under an `InspectionScope`, recording nothing; a legacy
`BreakpointSet` PC breakpoint adds an `EVENT_NONE` entry; another kind, and a
spent `once` re-enabled, are not listed (`EVT-PROBE-08..13`).
`run n`'s landing check is the only place a breakpoint is evaluated inside a
`run n` (each step's own `Execute` match is skipped by the GH #221 step-off),
and it asks:

- this session's slots, with their own predicates — On Change for a PC-free
  one, which a stateless probe cannot judge: a stop slot ends the run with its
  fired line, a print slot prints and the run steps on
  (`ZRCP-BP-15/16/19/24`);
- `probe_execute`, for everyone else: an `EVENT_NONE` entry (a legacy PC
  breakpoint), or another client's static-`Stop` subscription with no handler,
  ends the run; this session's own ids are left to the slots
  (`ZRCP-BP-21/25/28/29/30`). A `Log` / `Continue` one would not stop a free
  `run`, so it does not end a `run n`; a HANDLER's verdict overrides the static
  action (`events.h` `Handler`) and is known only by running it, which a pure
  probe may not, so a handler subscription is not counted either (review round
  2). Every jnext frontend's breakpoint — DZRP, GDB, the Qt GUI — is a static
  `Stop`; the residual is a handler (a future DSL rule) that would return `Stop`
  at the landing PC, which a `run n` passes.

The On-Change state is keyed to the boundary (the event's master cycle): a
second evaluation of the same slot's condition at the same cycle — a
`probe_execute`, the landing check — returns the cached result and advances
nothing (`ZRCP-BP-31`). `probe_execute` honours the condition contract's
`!replay_mode` rule as delivery does: in replay it lists nothing and runs no
condition (`EVT-PROBE-15`); a page qualifier is matched against the page mapped
at the PC's own slot (`EVT-PROBE-14`). Review round 1 found the first, interim cut skipped
  other clients wherever a slot of this session covered the PC — which a
  PC-free slot does everywhere — and could not evaluate their conditions; both
  are gone.

### 11.9 Milestone 3 — WP-5, history / stack / coverage / load

**Served (8):** `cpu-history`, `cpu-code-coverage`, `extended-stack`,
`smartload|sl`, `load-binary`, `save-binary`, `snapshot-save`,
`snapshot-load`. 67 served, 1 declined, 57 unsupported (`ZRCP-TAB-01`).
`get-pc` with a negative count answers ZEsarUX's `Error. Can't be negative`,
and a negative index is honoured as ZEsarUX's `parse_string_to_number` does.
Oracles: ZEsarUX 12.0's `remote.c` (`remote_cpu_history`,
`remote_extended_stack`, `remote_cpu_code_coverage`, the file and snapshot
handlers), `debug.c` (`cpu_history_legacy_regs_bin_to_string`, the index rule,
`push_value_types_strings`, the repeated-HALT/LDxR rule), `utils.c`
(`load_binary_file`, `save_binary_file`, `util_parse_commands_argvc_comillas`);
DeZog's `zesaruxremote.ts` / `zesaruxcpuhistory.ts` (init, every-step `get`,
`extended-stack get` typing, coverage `get` + `clear`, `smartload "…"`,
`load-binary "…" <decimal> 0`, `snapshot-save <path>.zsf`).

**Deviations and precisions:**

1. **The history is jnext's trace log** (CAP-INS-13). `get i` (0 = newest),
   `get-pc`, `get-size`, the index errors and the line are ZEsarUX's, byte for
   byte, with the memory-access list empty (jnext records none — the trailing
   space stays). `started` is recorded but the history records while it is
   `enabled`. **`clear` and `set-max-size` act on the session's VIEW, never on
   the machine's trace** (review round 1): that trace is the one jnext's Step
   Back and rewind read, and DeZog sends both on every launch, so clearing or
   resizing it would cut another user's Step Back for the rest of the
   process. `clear` records the newest trace entry as the view's base (only
   later entries are shown; if the base has left the ring, every entry is
   newer); `set-max-size` (1..1000000) caps the view at the newest n. So the
   history holds at most what the machine's trace holds — 10000 entries by
   default — whatever `set-max-size` says; `get-max-size` reports the size
   set. `ignrephalt` / `ignrepldxr` are also a VIEW, ZEsarUX's rule (the
   first of a run of HALTs / LDIR / LDDR is kept) applied when read, so they
   also apply to entries recorded before they were set.
2. **`MMU=` in a history entry** is the get-registers projection
   (`mapped_page`), built from the entry's eight effective pages. The entry
   does not record whether a slot was ROM (`TraceEntry` has no such field),
   so a slot is taken as ROM when it is ROM now and still holds the same page
   — exact unless the program switched a slot between ROM and RAM since the
   entry. **Frozen-header need, reported, not made:** a per-slot ROM flag (a
   byte mask) in `TraceEntry`, filled by the recorder.
3. **Declined:** `cpu-history get-extended` (it adds paging-port values the
   entry does not hold — the same header need) and `restore` (use the jnext
   debugger's Step Back), each by name with the reason.
4. **Machine-wide switches.** The trace, the coverage and call tracking are
   the machine's, not the session's. A session's `enabled yes` switches the
   backend on only if it was off, and its `enabled no` — or its end — turns off
   only what it turned on; `Already enabled` / `Already disabled` follow the
   session's own state (`ZRCP-HIS-09`, `ZRCP-XST-02`).
5. **`extended-stack get`** reads each word from memory now and types it from
   jnext's call tracking: a frame whose return address is at that word gives
   `call`, `rst`, `maskable_interrupt` or `non_maskable_interrupt`; anything
   else is `default` (jnext does not tell `push` from `default`; DeZog keys
   only on the other four). `clear` answers empty and does nothing: the
   tracker forgets a frame when its return pops it, and the backend publishes
   no call-stack clear.
6. **`snapshot-save` / `snapshot-load`** are this session's in-memory
   bookmarks (CAP-CAP-03, REQ-zrcp-13 DECIDED): the name is only a key, at
   most 8, gone with the session. `snapshot-save` refuses mid-frame
   (`RefuseMidFrame`), for DZRP's reason: DeZog does not re-read the
   registers after a save, so an advance would leave its cached PC behind.
   A stop at a breakpoint is mid-frame; a pause while running is not.
7. **`smartload`** is CAP-CTL-15 `load()`, jnext's `--load` table (`.nex`,
   `.sna`, `.szx`, `.z80`, `.jns`, `.tap`, `.tzx`, `.wav`, `.rzx`); a paused
   machine stays paused at the new PC. Any refusal answers ZEsarUX's one
   text, `Error. Unknown file format` (the reason is logged).
8. **`load-binary` / `save-binary`** work on the CPU view (writes to ROM land
   nowhere; addresses wrap at FFFFH); `save-binary` length 0 is 64 KB; an
   address must fit 16 bits (milestone 1's rule). `save-binary` answers a
   failure `ERROR loading file`, ZEsarUX's own text, and so does a
   `load-binary` of something that is not a readable file (a directory). A
   write the backend REFUSES — an RZX session, a corrupt machine — is said,
   as `write-memory` says it: `Error. load-binary refused: <reason> (<n>
   bytes loaded)` (review round 1; a write to ROM stays silent).

Rows: `ZRCP-FMT-14`, `ZRCP-HIS-01..13`, `ZRCP-XST-01..04`, `ZRCP-COV-01`,
`ZRCP-LOAD-01..05`, `ZRCP-SNAP-01..03`; the regression row `zrcp-hist-func` (§6.2 item 7 and the
rest of WP-5 against a live jnext: a `.sna` smartloaded, history, coverage,
extended stack, load/save-binary, a snapshot round trip).

