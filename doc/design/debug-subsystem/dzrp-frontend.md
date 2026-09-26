# DZRP frontend — DeZog Remote Protocol adapter (GH #12, epic #276)

> Status: **v1 — design for the frontend feedback cycle**. Owner of this file:
> the DZRP frontend design agent. Read-only siblings: `backend.md`,
> `qt-frontend.md`, and the ZRCP / GDB / DSL files. Requirements to the backend
> are `REQ-dzrp-<n>` (§8); their dispositions are recorded there as they arrive.
>
> **Revision log**
> - v1 (2026-09-26 evening): command table, breakpoint model, transport/loop
>   model, validation plan, REQ ledger, work packages. Mapped onto `backend.md`
>   v1: 22 used, 4 declined, 3 unsupported, 0 reach-arounds.
> - v1.1 (same evening): all ten REQs answered by `design-backend` — 9 ACCEPTED,
>   REQ-9 NEEDS-PROTOTYPE (API accepted, cadence measured on V-LAT). §8 records
>   the dispositions; §3.1/§4.2/§6 updated to the accepted forms (`Transient`
>   flag, `page` qualifier, `Paused{matched[]}`, post-frame `pump`, CAP-INS-18,
>   `save_state_bytes(RefuseMidFrame)`).

Every claim below carries a `file:line` citation. Sources and their versions:

| Source | Where | Version |
|---|---|---|
| DZRP spec | `/home/jorgegv/src/spectrum/dezog.jorgegv/design/DeZogProtocol.md` | history ends at **2.1.0** (`:128-134`); the doc carries no explicit "current version" line |
| DeZog client (the truth where the spec is vague) | `/home/jorgegv/src/spectrum/dezog.jorgegv/src/remotes/` | DeZog **3.7.4** (`package.json`); fork branch `zxnext-socket-transport`, upstream `main` @ `9702cd2e` |
| CSpect DeZog plugin (oracle, binary only) | `/home/jorgegv/src/spectrum/CSpect3_1_0_0/DeZogPlugin.dll`; behaviour as audited in `tools/cspect_dzrp/REVIEW.md` | plugin answers DZRP **2.0.0** (`REVIEW.md` N5) |
| dezogif_ng (owner's stub, constrained end) | `/home/jorgegv/src/spectrum/dezogif_ng/` | `doc/DEZOG-BREAKPOINTS-DESIGN.md`, `doc/legacy/Design.md`, `.claude/skills/dzrp/SKILL.md`, `test/dzrp/dzrp.py` |
| jnext | worktree `gh276-design` @ `main 974b0ab19` | `src/debug/*`, `src/core/emulator.{h,cpp}`, `src/memory/mmu.*`, `src/port/*`, `src/video/sprites.h`, `src/video/palette.h`, `src/platform/*`, `src/gui/qt_app.cpp`, `src/core/cli_options.h` |
| backend design | `doc/design/debug-subsystem/backend.md` | v1 mapped; v3 confirmed (no CAP this adapter uses changed shape) |

Abbreviations: **spec:N** = `DeZogProtocol.md` line N; **remote:N** =
`src/remotes/dzrp/dzrpremote.ts`; **buf:N** =
`src/remotes/dzrpbuffer/dzrpbufferremote.ts`; **cspect:N** =
`src/remotes/dzrpbuffer/cspectremote.ts`; **zxnext:N** =
`src/remotes/dzrpbuffer/zxnextserialremote.ts`; **base:N** =
`src/remotes/remotebase.ts`.

---

## 0. Settled decisions this design starts from (not re-litigated)

From `gh12.md` / `gh276.md` / `gh277.md`, quoted where it matters:

1. **DZRP is the protocol** for #12 (owner + reporter + dcrespo3d + jattree).
2. **Division of labour is DeZog's, not ours.** Instruction-length calculation,
   the original opcode under a breakpoint, the 1-2 temporary breakpoints to
   step off a breakpoint, and **condition evaluation** live in DeZog. "A
   conditional breakpoint is an unconditional pause as far as the remote is
   concerned." The adapter **declines** the backend's predicate support.
3. **Socket transport**, presented like the CSpect plugin.
4. **A partial server is legitimate**; unsupported commands are *reported*
   unsupported, never silently accepted.
5. **DZRP has no history/trace/reverse command.** The adapter serves nothing
   there; the upstream DeZog conversation is *not* a prerequisite.
6. `CMD_READ_STATE`/`CMD_WRITE_STATE` is a **bookmark** (`-state save/restore`),
   not time travel.
7. The backend is the union of what jnext can do; this adapter is a projection
   of it onto what the wire can carry, and **never reaches around it**.

---

## 1. Corrections and findings — where the brief and the sources disagree

Each of these changed a design decision; the brief called itself a hypothesis.

**F1. Which DZRP version to answer.** The spec's history stops at 2.1.0
(spec:128-134; 2.1.0 added `READ_PORT`, `WRITE_PORT`, `EXEC_ASM`,
`INTERRUPT_ON_OFF`). The client's base class requires `[2,0,0]` (remote:135);
the `zxnext` remote overrides to `[2,1,0]` (zxnext:81); the `cspect` remote
inherits 2.0.0. The check is *major equal AND remote.minor ≥ client.minor*
(buf:392-393). **jnext answers `2.1.0`**, which satisfies both remote types;
the CSpect plugin answers 2.0.0 (`REVIEW.md` N5) and would fail a `zxnext`
client's check — one more reason jnext should not copy CSpect's answer.

**F2. Which DeZog remote type reaches jnext, and what that fixes.** DeZog has
exactly two DZRP-over-socket remote types (`remotefactory.ts:26-35`):

| remoteType | breakpoint dialect | refuses **client-side**, before any packet | notes |
|---|---|---|---|
| `cspect` (stock DeZog, port 11000 default `settings.ts:604`) | `CMD_ADD_BREAKPOINT`/`REMOVE` (40/41), remote owns bp state | watchpoints (cspect:162-175), state save/restore (cspect:180-185), `supportsWPMEM=false` (cspect:28) | sprites, TBBlue, ports, `EXEC_ASM` all sent |
| `zxnext` with `hostname` (**fork only** — `git log upstream/main -- zxnextsocketremote.ts` is empty; it is PR maziac/DeZog#186, `DEZOG-BREAKPOINTS-DESIGN.md:170-177`) | `CMD_SET_BREAKPOINTS`/`RESTORE_MEM` (13/14): set every bp before each continue, restore after (zxnext:362-432); 40/41 handled *inside the client* (zxnext:498-507) | sprites (zxnext:654,664), state (zxnext:671-675), watchpoints (zxnext:692-706); `IM` decoded as NaN (`z80registerszxnextdecoder.ts`) | built for opcode-patching stubs |

**Decision: jnext presents as a `cspect`-type remote** (`"remoteType":
"cspect"`, port from `--dzrp-port`). It is the only released path, it keeps
breakpoint state where an emulator keeps it (no opcode patching to hide from
`READ_MEM`/disassembly — the hazard `DEZOG-BREAKPOINTS-DESIGN.md:36-54`
documents), and it is what `tools/cspect_dzrp/cspect_dzrp.py` already speaks.
Commands 13/14 are therefore **unsupported-reported**, not tier 2 (§2).

**F3. No released DeZog remote sends watchpoints or state over a wire.** The
compatibility table (spec:87-119) marks `ADD_WATCHPOINT`/`READ_STATE` only for
`zsim` (in-process) and MAME (translated to gdb). Both socket remotes refuse
them client-side (F2). So the "tier 2, emulator-only" commands the brief
highlights **are served but reached only by non-DeZog clients** — jnext's own
`cspect_dzrp.py` harness, ZX Basic Studio (#12's reporter), dezogif_ng's
`test/dzrp/dzrp.py`. They are still worth serving (the protocol defines them
and the backend has them for free), but the validation plan (§7) cannot claim
DeZog coverage for them, and the man page must not either.

**F4. What DeZog actually sends on connect.** `onConnect` (remote:230-275):
`CMD_INIT` → `load()` — only if `launch.json` names a `load` file: `.sna`/`.z80`
= `WRITE_BANK`×N, `SET_SLOT`×8, `SET_BORDER`, `SET_REGISTER`×15, `WRITE_PORT
0x7FFD` (128K), `INTERRUPT_ON_OFF` (remote:1600-1706); `.nex` = `SET_BORDER`,
`WRITE_BANK`×N, `SET_SLOT`×8, `SET_REGISTER`×2 (remote:1712-1741) — then the
memory model is instantiated from the `CMD_INIT` machine-type byte
(remote:242-263). After `initialized`, the debug adapter reads registers
(`debugadapter.ts:754`) and reports **`stopped`/`entry`** to VS Code
(`design/startup.md:26`). Nothing in that sequence pauses the remote: DeZog
*assumes* it is stopped. Hence **`CMD_INIT` pauses the machine** (§4.1).

**F5. `CMD_CONTINUE` is 11 bytes from DeZog, 5 from the CSpect reference.**
DeZog sends `bp1en, bp1(2), bp2en, bp2(2), alt=0, 4 unused` (buf:484-490);
the CSpect plugin reads only the first five (`REVIEW.md` wire audit, "5
bytes"). The server accepts any payload ≥ 5 bytes and reads the alternate
byte only when present. Alternate commands 1/2 are "not planned" (spec:390)
and never sent (buf:487): a non-zero value is logged and treated as 0 —
spec-conformant ("the remote *might* execute the alternate command", spec:388).

**F6. `CMD_GET_SPRITE_PATTERNS` is 2 bytes on the wire, not 4.** The spec
says `index u16, count u16` (spec:644-648); DeZog sends `[index, count]` as
two bytes (buf:735). The server accepts both lengths (2 → bytes, 4 → LE words).

**F7. The `NTF_PAUSE` bank byte is load-bearing.** DeZog keys its breakpoint
map by *long* address (`(bank+1)<<16 | addr`, remote:729-739) and looks a hit
up by long then 64K address (remote:697-703). In DeZog 3.x "long addresses
are used consistently, even for non-banked systems" (`MemoryPaging.md:69`), so
a `NTF_PAUSE` carrying bank byte 0 matches **nothing**, `condition` stays
undefined, and DeZog silently sends another `CMD_CONTINUE` (remote:1053-1059).
The adapter therefore reports `bank+1` of the page mapped at the PC's slot
(§3.4). The byte overflows for bank 0xFF (§5.3) — a protocol limit.

**F8. The temp-breakpoint priority rule is the client's correctness, not
style.** `design/breakpoints.md:124-129`: when a stop satisfies both a
`CMD_CONTINUE` temp breakpoint and a user breakpoint at the same address, the
remote must report reason **0** (temp) — reporting 2 (breakpoint) lets DeZog
evaluate the user bp's condition, find it false, and *continue*, so the step
runs away. The `zxnext` remote patches this client-side (zxnext:396-398); the
`cspect` remote does not. The adapter implements the rule (§3.3).

**F9. `-state save` mid-frame would corrupt DeZog's register cache.** jnext
snapshots only at frame boundaries (`src/debug/rewind_buffer.h:11-16`); the
GUI rule for a mid-frame save is "always advance, never refuse"
(`src/core/emulator.h:160-183`, GH #27 S6). But DeZog does **not** re-read
registers after `CMD_READ_STATE` (remote:1749-1756; only `stateRestore` does,
:1772), and its next step computes temp breakpoints from the cached PC
(`remotebase.ts:1545-1557`) — from a PC the machine has left. Advancing is
therefore a runaway step waiting to happen. §6 refuses mid-frame instead
(spec-sanctioned: a zero-length reply "means that it was not possible",
spec:819), and §4.2 shows why a manual pause always *is* at a frame boundary.

**F10. jnext already hosts DeZog serial sessions — orthogonally.** With
`--joy-uart-pty` (`cli_options.h`, GH #252) a *guest-side* stub (dezogif_ng)
speaks DZRP to DeZog's `zxnext` serial remote through the emulated joystick
UART. That is dezogif_ng's bench, not this adapter: here jnext itself is the
remote, no stub runs in the guest, and nothing in the guest is perturbed.
The two coexist; the man page says which is which.

---

## 2. The DZRP command table

Tiers: **T1** served, nearly free from existing code; **T2** served, only an
emulator can; **DECL** declined by design (owner decision cited); **UNS**
unsupported, reported as such. "DeZog sends" says which released path sends
it (`cspect` remote, `zxnext` remote, console = only via the `-dbg cmd_*`
debug console `remote:314-626`, load = only while loading a .sna/.z80/.nex).
CAP ids are `backend.md` v1.

| # | Command | DeZog sends | Tier | Backend CAP | Wire and behaviour (cited) |
|---|---|---|---|---|---|
| 1 | `CMD_INIT` | cspect, zxnext | T1 | CAP-SES-01 attach, CAP-CTL-01 pause | Cmd: version(3, big-endian) + name\0 (spec:252-256). Reply: err, **2.1.0**, machine **ZXNEXT=4** always (§5.1), `"jnext v<version>\0"` (spec:259-266). Attaches the client, **pauses the machine** (F4), records the client's DZRP version. A second connection while one is open is accepted and immediately closed with a log line (one client, as the CSpect plugin: `README.md` "No multi-client support"). |
| 2 | `CMD_CLOSE` | cspect, zxnext | T1 | CAP-SES-01 detach | Reply seq only. Detach: the backend removes this client's subscriptions and resumes if the machine is paused *by this client* (CAP-SES-01). DeZog's `cspect` remote sends `CMD_PAUSE` before `CLOSE` (cspect:114), so a graceful close leaves the machine **running**; a dropped socket is treated as `CLOSE`. |
| 3 | `CMD_GET_REGISTERS` | cspect, zxnext | T1 | CAP-INS-01, CAP-INS-03 | Reply after seq: PC SP AF BC DE HL IX IY AF' BC' DE' HL' (12 LE words), R, I, IM, reserved(0), Nslots=8, 8 slot bytes = 37 bytes (spec:296-319; parsed at buf:421-441). Slot byte = NR 0x50+s visible page, **0xFF for ROM** (`mmu.h:53`; DeZog rewrites slot 0's 0xFF to 0xFE itself, remote:295-302). IFF1/2 are not on the wire (spec). |
| 4 | `CMD_SET_REGISTER` | cspect, zxnext, load | T1 | CAP-INS-01 | Cmd: reg index (spec:327 numbering: 0=PC … 11=HL', 13=IM, 14=F … 35=I; 12 unused) + u16 LE; 8-bit targets take the low byte (spec:328). Unknown index → seq-only reply + warn log. WP-2 verifies DeZog's `Z80_REG` enum equals the spec table. |
| 5 | `CMD_WRITE_BANK` | load | T1 | CAP-INS-02 `Page{}` | Cmd: bank(1) + 8192 bytes (spec:337-354). Bank is the MMU page number 0..223 (`MemoryModelZxNextOneROM`, `zxnextmemorymodels.ts:192-256`); the backend routes it (REQ-dzrp-5: +0x20 in Next mode `mmu.h:1387-1390`, page 0x0E → `bank7_bram` `mmu.h:1392-1401`). Reply: err + string (spec:357-362): `1 "bank out of range"` for >223 / 0xFE / 0xFF, `1 "length must be 8192"` otherwise. DeZog throws the string (buf:626-631) — the honest refusal reaches the user. |
| 6 | `CMD_CONTINUE` | cspect, zxnext | T1 | CAP-EVT Execute ×2 with `Transient` (REQ-dzrp-3), CAP-CTL-02 | §3.2. Reply seq only, **immediately, before the machine runs** (spec:399; CSpect does the same, `REVIEW.md`). Payload ≥5 bytes (F5). |
| 7 | `CMD_PAUSE` | cspect, zxnext-socket (zxnext-serial refuses client-side, zxnext:644) | T1 | CAP-CTL-01 | Reply seq only, then **exactly one** `NTF_PAUSE` reason 1 (spec:34-40 case A) — also when already paused (DeZog ignores a notification with no continue outstanding, buf:228; a foreign client gets a uniform answer). Because commands run between frames (§4.2) the pause lands at a frame boundary. |
| 8 | `CMD_READ_MEM` | cspect, zxnext | T1 | CAP-INS-02 `Cpu` peek | Cmd: reserved(1), addr u16, size u16 (spec:421-426; the reserved byte is mandatory, `README.md`). CPU view through the live mapping, **side-effect free** (`Mmu::peek`, backend finding F1 — `Mmu::read` latches the +3 floating-bus byte). size 0 → empty reply; DeZog splits a 64K read into two 32K (buf:583-592). |
| 9 | `CMD_WRITE_MEM` | cspect, zxnext, load | T1 | CAP-INS-02 `Cpu` poke | As the CPU would write: ROM-mapped bytes are dropped (`mmu.h:505`), Layer-2 write-over applies. Reply seq only; a dropped byte is logged at debug level (DZRP has no error field here). |
| 10 | `CMD_SET_SLOT` | load, console | T1 | CAP-INS-03 | Cmd: slot, bank (spec:456-474). Through the NR 0x50+slot write path so the write is what the guest's own `NEXTREG` would do; 0xFF (and 0xFE on slot 0, spec:473) = ROM. Reply err byte: 1 for slot >7 or bank 224..0xFD. |
| 11 | `CMD_GET_TBBLUE_REG` | cspect, zxnext | T1 | CAP-INS-04 peek | `NextReg::peek` (`nextreg.h:318-327`): no read handler side effects, no phantom log lines. Reply: value. |
| 12 | `CMD_SET_BORDER` | load | T1 | CAP-INS-18 `set_border` (REQ-dzrp-6) | Bits 2:0 (spec:498-503). `Ula::set_border` (`ula.h:180`), **not** a port 0xFE write, which would also drive EAR/MIC. |
| 13 | `CMD_SET_BREAKPOINTS` | zxnext only | UNS | — | The opcode-patching dialect (spec:535-538 "only used by the ZX Next"). Not the remote type jnext presents as (F2). Reply seq only + warn log naming the command and the configuration to use (`remoteType: cspect`). |
| 14 | `CMD_RESTORE_MEM` | zxnext only | UNS | — | As 13. Never writes the payload back (that would clobber self-modifying code, since nothing was patched). |
| 15 | `CMD_LOOPBACK` | console (`test start`, remote:539-594) | T1 | — (adapter only) | Echo, N ≤ 8192 (spec:588). First thing to bring up: proves framing without touching the machine (`SKILL.md` smoke-test order). |
| 16 | `CMD_GET_SPRITES_PALETTE` | cspect | T1 | CAP-INS-08 (+REQ-dzrp-4) | Cmd: palette 0/1. Reply 512 bytes: per entry LE `RRRGGGBB, 0000000B` (spec:604; decoded buf:692-700). From `sprite_rgb333_[bank][i]` (`palette.h:482`, private today). |
| 17 | `CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL` | cspect | T1 | CAP-INS-08, CAP-INS-04 | x1, x2, y1, y2 (`sprites.h:100-103`) + NR 0x15 via peek (read handler `emulator.cpp:2702`) (spec:615-623). |
| 18 | `CMD_GET_SPRITES` | cspect | T1 | CAP-INS-08 (+REQ-dzrp-4) | Cmd: index, count (spec:629-633). Reply 5 raw attribute bytes per sprite (spec:635-639; DeZog asserts `count*5 == length`, buf:711). The raw bytes are `SpriteAttr::byte0..4` (`sprites.h:441-446`, private); `SpriteInfo` is decoded and lossy (drops N6, anchor bits). index+count > 128 → clamp count, reply the clamped length. |
| 19 | `CMD_GET_SPRITE_PATTERNS` | cspect | T1 | CAP-INS-08 (+REQ-dzrp-4) | 2- or 4-byte cmd (F6). Reply `count*256` bytes from `pattern_ram_` (`sprites.h:478`), index 0..63, count clamped to 64-index (DeZog asserts the length, buf:737). |
| 20 | `CMD_READ_PORT` | console | T1 | CAP-INS-05 | A real IN: runs IO observers and read handlers (`port_dispatch.h`), i.e. **perturbing by nature** (backend CAP-INS-05 says so). Documented; it is what the command means. IO watchpoints cannot fire (outside `GuestExecutionScope`, `debug_state.h:42-60`). |
| 21 | `CMD_WRITE_PORT` | load (0x7FFD), console | T1 | CAP-INS-05 | A real OUT. Same note. |
| 22 | `CMD_EXEC_ASM` | console | UNS (deferred) | — | "Executed in the debugger context … does not change anything in the debugged program" (spec:702) needs a scratch execution context jnext does not have. Reply seq only + warn log; DeZog's console prints `error: undefined` — visible, not silent. Reconsider only if a client other than the console asks. |
| 23 | `CMD_INTERRUPT_ON_OFF` | load | T1 | CAP-INS-01 | IFF1 = IFF2 = flag (spec:721-726; sent on every .sna/.z80 load, remote:1649,1705). |
| 40 | `CMD_ADD_BREAKPOINT` | cspect | T1 | CAP-EVT Execute[a,a] Stop, owner=client, **no predicate** | §3.1. Cmd: addr u16, bank+1, condition\0 (spec:738-743). The condition string is **ignored** (decision 2). Reply u16 id; 0 = refused (spec:750; DeZog marks the bp unverified, remote:1378-1379). |
| 41 | `CMD_REMOVE_BREAKPOINT` | cspect | T1 | CAP-EVT unsubscribe | By id. Unknown id → seq-only reply + warn. |
| 42 | `CMD_ADD_WATCHPOINT` | none released (F3) | T2 | CAP-EVT MemRead/MemWrite [lo, lo+size-1] Stop | Cmd: addr, bank+1, size u16, access bit0 read / bit1 write (spec:771-777). Ranges are what the backend's #279 work adds (backend §4.3/§8); the old per-address `add_watchpoint` would have needed `size` entries scanned per access. Bank: §5.4. Reply err: 0 ok, 1 refused (size 0, both access bits clear, or wraps past 0xFFFF — "wrap around is ignored" by DeZog too, remote:669). |
| 43 | `CMD_REMOVE_WATCHPOINT` | none released | T2 | CAP-EVT unsubscribe | Matched by the exact (addr, bank, size, access) tuple the adapter recorded at add time (DZRP watchpoints have no id, spec:104). |
| 50 | `CMD_READ_STATE` | none released (F3) | T2 | CAP-ST-01 (+REQ-dzrp-10) | §6. Reply: the `save_state` byte stream (in-process format, "up to the remote", spec:817), or **zero length** when mid-frame (spec:819). |
| 51 | `CMD_WRITE_STATE` | none released | T2 | CAP-ST-02 | §6. Reply seq only; a failed restore (sentinel mismatch → `RefusedCorrupt`) is logged and followed by `NTF_PAUSE` reason 255 "state restore failed: machine corrupt" so the client learns it. |
| — | `NTF_PAUSE` | ← to client | T1 | CAP-SES-02 Paused, CAP-INS-17 events_fired_since | §3.3. Frame: len, seq 0, `1`, reason, addr u16, bank+1, string\0 (spec:838-846; parsed buf:225-245). Exactly one per `CMD_CONTINUE`, plus one per `CMD_PAUSE` (row 7). |
| — | unknown command id | — | UNS | — | Seq-only reply + warn log "unsupported DZRP command N". The frame is length-prefixed, so it is always consumed exactly (dezogif_ng issue #7's lesson, `DEZOG-BREAKPOINTS-DESIGN.md:84-85`). |

**Counts.** 29 commands + 1 notification. Served: **22** (19 T1 + 3 T2 pairs
counted per command: 42, 43, 50, 51 = T2; 15 adapter-only). Declined-by-design:
the condition string (in row 40), reverse debugging (no command to decline —
recorded in §9 as "no wire"), the six backend step verbs CTL-03..08 (DZRP has
no step; §3.2), IO watchpoints (no wire form). Unsupported-reported: 13, 14, 22.

**What DeZog 3.7.4 can reach, honestly:** rows 1-4, 6-12, 16-21, 23, 40, 41 —
and 5/12/23 only when `launch.json` has a `load`. Rows 42/43/50/51 are reachable
only by a non-DeZog client (F3). §7 validates each group against the client
that actually sends it.

---

## 3. Breakpoints and stepping, as DeZog expects them

### 3.1 User breakpoints (`CMD_ADD_BREAKPOINT` / `REMOVE`)

- Adapter state: `map<u16 id, {addr, bank+1, EventId}>`, ids allocated 1..65535
  per session (0 is "refused"), never reused within a session.
- Each add = one backend `Execute[addr, addr]` subscription, `action=Stop`,
  `owner=this client`, **no condition**. Two DeZog breakpoints at one address
  (a LOGPOINT plus a user bp — `breakpoints.md:102-123` says why ids exist) are
  two adapter ids and two backend subscriptions; removing one leaves the other.
  If the backend coalesces by address (REQ-dzrp-3's answer decides), the
  adapter refcounts instead. Either way the wire behaviour is identical.
- Bank byte ≠ 0: a **long** breakpoint → `Execute{addr, addr, page = bank}`
  (REQ-dzrp-7, accepted): fires only when the effective page at the PC's slot
  is `bank`, evaluated after the address matched so it costs nothing otherwise.
  (The rejected alternative — a 64K subscription plus an adapter auto-continue
  on a wrong-bank hit, the fallback DeZog itself performs, `MemoryPaging.md:
  93-96` — would have shown every other client a Paused/Resumed flicker.)
- Bank byte 0: 64K breakpoint, fires in any bank.
- The GH #221 step-off arm (`debug_state.h:180-206`, `debug_state.cpp:300-303`)
  already gives "resume from a breakpoint does not re-hit it": every
  `CMD_CONTINUE` goes through the backend's `run()`, i.e. `unpause_()`. No
  adapter logic, no opcode storage — exactly the split decision 2 asks for.

### 3.2 `CMD_CONTINUE` and stepping

DZRP has **no step command**. DeZog implements Step Into / Step Over / Step Out
as `CMD_CONTINUE` with up to two 64K temporary breakpoints it computed itself
(`remotebase.ts:1545-1635` `calcStepBp`: next PC, branch target, return
address from `(SP)`, RST 08 special-casing); Step Out is a client-side loop of
step-overs checking SP (remote:1170-1248; `breakpoints.md:32-60`). So:

- `CMD_CONTINUE(bp1en, bp1, bp2en, bp2, alt, …)` → for each enabled temp
  address: one `Execute[a,a]` subscription with the **`Transient`** flag
  (exempt from the master switch, auto-removed at the next stop, hidden from
  the user-model listing — REQ-dzrp-3, accepted; today `BreakpointSet` holds
  one one-shot, `breakpoints.h:520-524`); then `run()`. Reply seq only
  *before* anything runs (§4.2 guarantees it).
- On the next stop (any cause) the backend removes both transients; the adapter
  forgets their ids. DeZog removes nothing — "they will be removed automatically
  after the command is finished" (spec:401).
- The adapter **never** calls CAP-CTL-03..08. In particular not `step_over()`
  (the GUI's call-like heuristic, `debugger_manager.cpp:421-433`) — DeZog has
  its own, and two heuristics would fight (decision 2). Consequence, documented:
  DeZog stepping at a `HALT` shows PC not moving, as on CSpect; the GH #207
  halt run-out is a GUI verb.
- `run()` refused (`Result::RefusedCorrupt`, CAP-CTL-11; or `RefusedRzx` is not
  possible for run): the `CONTINUE` reply cannot carry it, so the adapter
  replies, then sends `NTF_PAUSE` reason 255, address PC, string
  `"resume refused: <reason>"`. DeZog shows the string as the break reason
  (remote:873-874) and stays paused — the honest outcome.
- `run()` on an already-running machine is a no-op (GH #223, CAP-CTL-02): the
  temp subscriptions are still installed, which is what a client that lost
  track of a GUI-originated resume needs (§4.4).

### 3.3 `NTF_PAUSE` — reason, address, bank

Sent from the post-frame flush (§4.2) whenever the backend reports `Paused`
while this client has a `CMD_CONTINUE` outstanding, and once after every
`CMD_PAUSE`. Fields:

| Cause (backend `Paused.reason` + `Paused.matched[]` of `Hit{event_id, addr, access, value}`, REQ-dzrp-1/2; backend v3) | reason byte | address | string |
|---|---|---|---|
| one of this client's **transient** subscriptions fired (checked **first**, F8) | 0 | PC | `""` |
| `Client(this cid)` — our own `CMD_PAUSE` | 1 | PC | `""` |
| `Breakpoint(id)` owned by this client (and no transient fired) | 2 | PC | `""` |
| `Watch{id, access=Read, addr}` | 3 | `addr` | `""` |
| `Watch{id, access=Write, addr}` | 4 | `addr` | `""` |
| anything else: `User` (GUI pause), another client's breakpoint, `Magic`, `Script(id)`, `Corrupt`, `Step`/`RunTo` from the GUI | 255 | PC | human-readable, e.g. `"paused by GUI"`, `"magic breakpoint"`, the script's stop text |

Bank byte: `bank+1` of the page currently mapped at `address >> 13` (F7),
with the ROM caveat of §5.3. String is always null-terminated, at least one
byte (spec:846). Exactly-once per `CONTINUE`: the adapter keeps
`continue_outstanding` and drops a second `Paused` edge (e.g. GUI pause after
our breakpoint already stopped it — DeZog would ignore it anyway, buf:228).

### 3.4 What the GUI sees

DZRP user breakpoints and watchpoints are ordinary client-owned subscriptions:
listed in the Breakpoints panel, read-only, marked (qt-frontend.md §3.5,
backend §4.1 "all are visible to all"). Transients are not listed
(REQ-dzrp-3). A GUI edit of a DZRP-owned entry is not offered.

---

## 4. Session, transport and loop ownership

### 4.1 Session

- `CMD_INIT` → `attach(ClientInfo{"dzrp", peer})` → `pause()` (F4). If the
  machine was already paused by someone else it stays paused; the reply is the
  same. The adapter asks for `StopPolicy::Pause` (CAP-SES-04) so a headless run
  with a debugger attached pauses instead of exiting on a stop.
- `CMD_CLOSE` or socket EOF/RST → `detach(cid)`: the backend drops the client's
  subscriptions and resumes if the pause was this client's (CAP-SES-01). The
  listening socket stays open for the next session (the CSpect plugin closes
  its listener after one accept, `REVIEW.md` "listener accepts ONE" — jnext
  deliberately does not: a headless CI run may attach, detach, re-attach).
- One client at a time (row 1).

### 4.2 Loop ownership — the single-threaded model, shared with ZRCP and RSP

Agreed with `design-gdb` (its REQ-gdb-13/14/15) and `backend.md` §5:

1. **No thread.** The adapter is a service object with `poll()`; the backend's
   `pump(max_wait_ms)` (CAP-SES-03) calls it. The three loop owners call `pump`
   once per tick: `QtApp` via `frame_sequencer` (`src/platform/frame_sequencer.h`),
   the SDL loop (`src/platform/sdl_app.cpp:280`), `HeadlessApp::run()`
   (`src/platform/headless_app.cpp:504`). While paused the Qt/SDL ticks keep
   running; the headless loop, which today calls `run_frame()` regardless and
   busy-spins on a paused machine (`emulator.cpp:9304-9305`), gains one
   `if (paused) pump(wait) else …` branch.
2. **Every command executes between two `run_frame()` calls**, on the emulation
   thread, synchronously. That is what makes `CMD_PAUSE` land at a frame
   boundary (§6 depends on it), what makes the `CONTINUE` reply precede any
   execution, and what makes `Mmu::peek`/register reads coherent (a frame
   boundary, never mid-instruction — backend §5 "why not a thread").
3. **Notifications flush after the frame batch of the same tick** (REQ-dzrp-8,
   accepted): `pump()` is called by the loop owner AFTER the tick's frames, in
   the `post_frames` slot where `check_breakpoint_hit()` sits today
   (`qt_app.cpp:666`). `Paused` is delivered synchronously inside `run_frame()`
   (CAP-SES-02) and queued; the adapter writes it in that `pump`; commands read
   there apply to the next tick's frames. Without that, a step is two ticks. Latency budget, stated: command → effect ≤ 1 tick; stop → `NTF`
   ≤ same tick; **one DeZog step ≈ 1 tick (20 ms at 50 Hz)**, vs sub-millisecond
   on the CSpect plugin's own thread. Step Out over a long loop is where that
   is felt (one round trip per instruction). REQ-dzrp-9 (needs-prototype, API
   accepted): `pump()` returns `ServiceHint{remote_attached, paused}` and the
   loop owner MAY shorten its cadence while paused with a remote attached
   (headless poll ≤2 ms; Qt/SDL re-arm the tick timer at ~2 ms); whether that
   is switched on is decided on a measurement with real DeZog (§7.1, V-LAT).
4. **Sockets:** the public `esp::EspListener` / `esp::EspTransport` seam
   (`src/esp01/include/esp01/esp_socket.h:509-561`, `make_socket_listener`):
   non-blocking, POSIX + Winsock, `poll()` never blocks, no exceptions,
   238-row suite. Bound to `--debug-listen-address` (default `127.0.0.1` —
   the same security decision as `--esp-listen-address`, ESP design §13.4).
   `esp_socket_platform.h` is private by its own comment (`:10-12`) and is not
   used directly. If sharing `src/esp01` from `src/remote` proves awkward at
   build time, the lift is `esp::net` → `src/net/`; still no new socket code.
5. **Framing** (spec:189-233, both length conventions — a command's length
   counts the payload only, a response's counts from the seq byte; dezogif_ng's
   `dzrp.py:1-46` records the silent hang that assuming symmetry costs):
   incremental parser over the transport's byte stream; frame cap **16 MiB**
   (a `CMD_WRITE_STATE` carries the full snapshot, > 2 MB of RAM); command seq
   must be 1..255 — seq 0, length over cap, or a truncated stream after
   `chunk_timeout` (5 s, matching DeZog's `socketTimeout`, `settings.ts:606`)
   is a protocol error: log, close the connection (the CSpect plugin does the
   same, `REVIEW.md` "Shutdown on protocol errors"). Responses echo the seq.
   Pipelined commands from a foreign client are processed in order; DeZog never
   pipelines (spec:10).

### 4.3 CLI rows (for `src/core/cli_options.h`; `make cli-check` gates the man page)

```cpp
{ "--dzrp-port", 1, Doc::Documented, OptId::DzrpPort,
  "PORT",
  "Serve the DeZog Remote Protocol (DZRP) on TCP PORT so DeZog\n"
  "(remoteType \"cspect\") or any DZRP client can drive the\n"
  "debugger. DeZog's default for that remote is 11000. Off\n"
  "unless given. One client at a time." },
{ "--debug-listen-address", 1, Doc::Documented, OptId::DebugListenAddress,
  "ADDR",
  "Bind address for --dzrp-port (and the other debugger\n"
  "protocol ports). Default 127.0.0.1; a non-loopback address\n"
  "exposes the debugger to the network with no authentication." },
```

Both work in every frontend (GUI, SDL, `--headless`) and every build
configuration (backend §6: `src/remote` links no toolkit). No `--dzrp-wait`:
`CMD_INIT` pauses (F4), and a headless run that must not execute anything
before the client attaches uses `--persistent-breakpoints` plus a breakpoint,
or simply starts paused via the DSL — not a fourth mechanism. `--delayed-
automatic-exit-frames` counts *frames*, so a client holding the machine paused
never advances it; the wall-clock `--delayed-automatic-exit` is the bound to
use with a debugger attached (man page note).

### 4.4 Arbitration with the Qt debugger

Backend §4.1: one `DebugState`, any client may pause/resume, last verb wins,
every transition broadcast with `by`. Consequences for DeZog, all documented
rather than engineered around:

- A GUI pause while DeZog is running → `NTF_PAUSE` reason 255 "paused by GUI"
  (only if a `CONTINUE` is outstanding, else DeZog ignores it — buf:228).
- A GUI Run while DeZog believes the machine is paused → nothing can be sent
  (DZRP has no "resumed" notification); DeZog shows stale registers until its
  next command, and its next `CMD_CONTINUE` installs temp breakpoints on a
  running machine (`run()` no-op, GH #223). Same as any DZRP remote a human
  can also drive.
- A DZRP pause in GUI mode opens the Qt debugger window through the unchanged
  GH #219 auto-enable path (`debugger_manager.cpp:682-693`; qt-frontend.md §5.2).
  Owner question Q1 (§10).

---

## 5. Memory model on the wire

### 5.1 Machine type

`CMD_INIT` reports **`ZXNEXT (4)` for every `--machine`**. jnext's MMU is the
Next's in every mode — 8 × 8K slots with NR 0x50-0x57 visible, ROM as the
0xFF sentinel (`mmu.h:49-78`), 48K/128K/+3 being the Next's legacy paging
(`mmu.h:651-786`). DeZog then instantiates `MemoryModelZxNextOneROM`
(remote:255-258): slots 0/1 accept 0..223 or ROM (0xFE/0xFF), slots 2-7 accept
0..223 (`zxnextmemorymodels.ts:192-256`). Reporting `ZX128K` for `--machine
128k` would force a 4 × 16K slot model whose bank numbers the wire would have
to translate for no gain; CSpect "will always return ZXNEXT" (spec:265) and so
does jnext.

### 5.2 Banks

DZRP 8K bank N = MMU page N as NR 0x50-0x57 spell it. jnext's *physical*
store index differs (`to_sram_page` adds 0x20 in Next mode, `mmu.h:1387-1390`;
MMU page 0x0E is a dedicated BRAM, `mmu.h:1392-1401`). The adapter never
computes that: `MemSpace::Page{N}` is indexed by MMU page and the backend
routes (REQ-dzrp-5). Banks 224..253 are refused (`WRITE_BANK` error string);
0xFE/0xFF are ROM and refused for writing.

### 5.3 Long addresses and the 0xFF overflow

`bank+1` is one byte (spec:238-243). DeZog's model has ROM as 0xFE (slot 0)
and 0xFF (slot 1): 0xFF+1 does not fit. For a breakpoint in slot-1 ROM DeZog
sends `(0x100) & 0xFF = 0` — a 64K breakpoint (buf:514) — and keys its own map
by `0x1000000 | addr`; the adapter can only ever report bank byte 0 or the
slot's page+1, neither of which DeZog's lookup (remote:697-703) matches. So
**breakpoints at 0x2000-0x3FFF while ROM is mapped there cannot be matched by
DeZog** through DZRP. Not jnext's to fix; recorded in the man page as a
protocol limit. Slot-0 ROM (0xFE → 0xFF) works.

### 5.4 Watchpoint banks

`CMD_ADD_WATCHPOINT` carries `bank+1`. DeZog filters bank client-side on a hit
(remote:661-686 `getWatchpointsByAddress` compares the wp's bank with the
current slot) and no released DeZog remote sends the command (F3). The adapter
installs the logical range and, for a non-zero bank, applies the same slot
check as §3.1's fallback on a hit; `remove` matches the recorded tuple. Not a
REQ: correct without backend support, and the only client that can send it
today is ours.

---

## 6. State bookmarks (`CMD_READ_STATE` / `CMD_WRITE_STATE`)

Payload = `Emulator::save_state` bytes (CAP-ST-01), "format up to the remote"
(spec:817); DeZog gzips and stores it (remote:1749-1756). In-process only, no
versioning, session lifetime — exactly `-state save/restore` (CAP-ST-02 note).

**Frame boundary rule (F9).** `READ_STATE` returns the snapshot **only when no
frame is in flight** (`Emulator::frame_in_progress()`, `emulator.h:184-186`);
otherwise a **zero-length** reply (spec:819) plus a log line saying why and
when it works. Because every command runs between frames (§4.2), the machine
*is* at a boundary after `CMD_PAUSE` and after `CMD_INIT` — so a DeZog user
gets a bookmark after a manual pause, not after a breakpoint. Advancing
instead (the GUI's own rule) would leave DeZog's cached PC behind the machine
and turn its next step into a runaway (F9); REQ-dzrp-10 (accepted) gives
`at_frame_boundary()` and `save_state_bytes(Mode::RefuseMidFrame)` →
`Result::NotAtFrameBoundary`, which the adapter answers with the zero-length
reply. Owner question Q2 remains a policy question, not a capability gap.

**`WRITE_STATE`**: `load_state_bytes` (CAP-ST-02); the machine lands at a frame
boundary, paused; DeZog re-reads registers and the call stack afterwards
(remote:1772-1773). A sentinel failure latches corruption (CAP-CTL-11): the
adapter replies, then `NTF_PAUSE` reason 255 "state restore failed: <subsystem>";
the next `CONTINUE` is refused per §3.2 until the corruption is acknowledged —
which DZRP cannot express, so a DeZog user must acknowledge from the GUI or
restart. Documented.

**Not offered**: nothing of CAP-ST-03/04 (rewind). DZRP has no verb; decision 5.

---

## 7. Validation plan

Three layers, each naming what it proves.

### 7.1 Against the real client — DeZog 3.7.4 in VS Code (`~/.vscode/extensions/maziac.dezog-3.7.4`)

`launch.json`: `"remoteType": "cspect", "cspect": {"port": 11000}`, a demo
built by `make -C demo` with its `.map`, jnext started with `--dzrp-port 11000
--load demo/<x>.nex`. Manual checklist, each item a row in the WP-6 test
protocol document, result recorded with the DeZog version:

| Row | Exercise | Proves |
|---|---|---|
| V-INIT | Connect; VS Code shows *stopped/entry*; Registers pane populated; MMU slots shown | rows 1, 3; F4 |
| V-BP | Set an editor breakpoint from the `.map`, Continue, hits; Continue again, hits again (GH #221 path); remove, does not hit | rows 40, 41, 6; §3.1 |
| V-BANK | Breakpoint in a banked page, hit only when that bank is mapped | §3.1 long addresses, REQ-dzrp-7 |
| V-STEP | Step Into through `CALL`, `RET`, `JR cc` taken and not taken, `RST 08`; Step Over a `CALL`; Step Out of a routine | §3.2, F8 (a user bp at the step target must not run away) |
| V-COND | Conditional breakpoint `A == 3` in a loop: stops exactly once | decision 2: adapter reports unconditional hits, DeZog filters |
| V-LOG | LOGPOINT: text printed, machine does not stay stopped | row 40 with a log-only bp |
| V-PAUSE | Continue, then Pause: stops, registers refresh | row 7 |
| V-MEM | Memory view, disassembly view, edit a byte, edit a register | rows 8, 9, 4 |
| V-NR/SPR | `-nextreg` / sprite viewer / sprite palette / pattern viewer | rows 11, 16-19 |
| V-LOAD | `launch.json` `load` of a `.sna` and of a `.nex` | rows 5, 10, 12, 21, 23 (F4 sequence) |
| V-STATE | `-state save` after Pause → works; after a breakpoint → console shows the refusal; `-state restore` → machine back, registers refreshed | §6 |
| V-GUI | With the Qt debugger open: DZRP pause opens/refreshes the window; GUI Run then DeZog Continue | §4.4 |
| V-LAT | Step Out of a 1000-iteration `DJNZ` routine, wall-clock timed, in `build/gui-release` at 100 % | REQ-dzrp-9 decision |
| V-CLOSE | Stop the session: machine resumes; reconnect works without restarting jnext | row 2, §4.1 |

### 7.2 Against jnext's own client — `tools/cspect_dzrp/cspect_dzrp.py` as a harness

It already speaks the wire to CSpect (independent client implementation; wire
audit in `REVIEW.md`) and covers the commands DeZog cannot reach (F3). New
regression rows (functional, `test/00regression/functional_tests.conf`,
`expect:` bumped deliberately), each starting jnext headless with
`--dzrp-port 0`-style ephemeral port printed in the log (the ESP listener has
the same "port 0 → report what was bound" contract, `esp_socket.h:518-527`):

| Row | Script drives | Proves |
|---|---|---|
| dzrp-loopback-func | `CMD_LOOPBACK` 1..8192 bytes, both length conventions, seq wrap 255→1 | §4.2.5 framing |
| dzrp-init-regs-func | `INIT` → version 2.1.0, machine 4; `GET_REGISTERS` 37 bytes, slots `FF FF 0A 0B 04 05 00 01` on a 48K-style map | rows 1, 3 |
| dzrp-bp-continue-func | add bp, continue, `NTF_PAUSE` reason 2 with bank byte; continue with temp bp at the same address → reason 0 | §3.1, F7, F8 |
| dzrp-watch-func | range watch write on a 256-byte area, program writes the last byte and one past → one NTF reason 4 with the right address | rows 42, 43, REQ-dzrp-1, edge coverage both sides |
| dzrp-state-func | pause → `READ_STATE` non-empty; continue, bp hit → `READ_STATE` empty; `WRITE_STATE` → registers equal the saved ones | §6 |
| dzrp-bank-func | `WRITE_BANK 14` then `SET_SLOT 6,14` and `READ_MEM 0xC000` → the bytes (bank-7 BRAM routing); `WRITE_BANK 250` → error string | REQ-dzrp-5 |
| dzrp-unsupported-func | `CMD_SET_BREAKPOINTS`, `CMD_EXEC_ASM`, id 99 → seq-only replies, warn lines in the log, connection alive | "reported, never silent" |
| dzrp-close-resume-func | pause, `CLOSE` → frame counter advances again; reconnect | row 2 |

`cspect_dzrp.py` gains watchpoint/state/bank/sprite methods (its README lists
them as "not exposed yet"), and its `REVIEW.md` H1 (stale notification in
`step_over_byte`) is fixed first — a harness with a known race is not a
harness. The FIX is a tools change reviewed independently.

### 7.3 Fake-transport unit suite — `dzrp_adapter_test` (Qt-free, `test/remote/`)

The adapter is constructed over an in-memory byte-pipe `Transport` (backend
§7) on a real `Emulator` + `Debugger` with a RAM program, frames driven through
`run_frame()` exactly as the loop owners do. Declared in `test/unit-tests.conf`
under `gate: none` with its pinned count; rows are literal IDs `DZRP-xx-nn`,
each asserting **bytes on the wire and machine state**, never only the
adapter's internal map:

- framing: both length conventions; split delivery one byte at a time; seq 0
  command → connection closed; oversize length → closed; seq echo.
- per command: a request/expected-reply pair from the spec tables (the spec
  line is the row's citation), plus the refusal path where one exists.
- continue/pause/notify: reply precedes execution (the machine's cycle counter
  is unchanged when the reply is on the pipe); temp-vs-user priority (F8); one
  NTF per CONTINUE; NTF after PAUSE when already paused; bank byte per slot.
- session: second connection refused; EOF = detach = resume when paused by us
  / stays paused when paused by another client (two fake listeners).
- **mutation checks for the reviewer** (derived from the diff, not the row
  list): flip the temp-first rule → V-STEP-style row must fail; drop the bank
  byte → the bank row must fail; make `READ_STATE` advance instead of refuse
  → dzrp-state row must fail; skip the post-frame flush → a row that counts
  ticks between stop and NTF must fail.

---

## 8. REQ ledger (to `design-backend`), with dispositions

Sent 2026-09-26 evening as `REQ-dzrp-<n>: <capability> — <why> — <command>`.
Status is updated in place when the backend answers.

| REQ | Capability asked | Why (cited) | DZRP command | Status |
|---|---|---|---|---|
| 1 | `Paused` / fired `Event` carries the accessed address and read-vs-write kind for watch stops | NTF reason 3/4 + address (spec:843-844); v1 `Paused` has only `pc` | 42, NTF | **ACCEPTED** — `Paused{by, reason, pc, cycle}`, `reason = Watch{event_id, access ∈ {Read, Write}, addr}` (also `Breakpoint{id}`, `Step`, `RunTo{id}`, `User{cid}`, `Magic`, `Corrupt`, `Script{id, text}`) |
| 2 | `events_fired_since(seq)` lists every matching subscription at a boundary, not only the winning verdict | temp-first rule (`breakpoints.md:124-129`, F8) | 6, NTF | **ACCEPTED** — `Paused.matched: vector<EventId>` (transients included); temp-beats-user is adapter policy over that list |
| 3 | adapter-creatable transient subscriptions: two Execute-once per resume, master-switch-exempt, auto-removed at the next stop, hidden from the GUI list | DeZog sends up to two (buf:484-490); `BreakpointSet` has one one-shot (`breakpoints.h:520-524`) | 6 | **ACCEPTED** — `Transient` flag on any subscription, any number, auto-removed at the next stop, hidden unless `include_transient`; the GUI's step-over/run-to one-shot becomes the same mechanism |
| 4 | raw sprite data behind CAP-INS-08: 5 attribute bytes ×128, 16 KB pattern RAM, sprite palette RGB333 per bank | `sprites.h:477-478`, `palette.h:482` are private; `SpriteInfo` is lossy | 16, 18, 19 | **ACCEPTED** — `sprite_attr_raw(i)`, `pattern_ram()`, `sprite_palette_rgb333(bank, idx)`; accessor additions on `SpriteEngine`/`PaletteManager` are an implementation item |
| 5 | `MemSpace::Page` indexed by MMU page 0..223, backend routes (+0x20, bank-7 BRAM), ROM pages refused with a reason | `mmu.h:1387-1401` | 5 | **ACCEPTED** — `Page` = NR 0x50-0x57 page space; 0xFE/0xFF → `Result::InvalidPage` + reason string; `Rom{n}` stays a separate space |
| 6 | `set_border(c)` as a debugger write to `Ula::set_border` | `ula.h:180`; port 0xFE would drive EAR/MIC; sent on every load (remote:1621,1718) | 12 | **ACCEPTED** — new **CAP-INS-18** `set_border(colour)`; no port traffic, no event |
| 7 | Execute filter with an optional physical-page qualifier | long-address breakpoints (spec:736-753); ALTERNATIVE: adapter auto-continue (§3.1) | 40 | **ACCEPTED (primary form)** — `Execute{lo, hi, page?}` fires when PC ∈ [lo,hi] AND effective page at slot(PC) == page; evaluated only after the address matched. The adapter fallback in §3.1 is not needed |
| 8 | notification flush in the same tick as the stop (post-frame `pump`/`flush`) | one DeZog step = one round trip; else 2 ticks/step | NTF | **ACCEPTED** — `pump()` is called AFTER the tick's frame batch (the `post_frames` slot, `qt_app.cpp:666`); a stop in this tick's frames is notified in this tick; commands received there apply to the next tick's frames |
| 9 | (needs-prototype) `pump` reports "remote attached and paused" so the loop owner can tick faster while paused | step latency vs the CSpect plugin thread; decide on V-LAT | 6 | **NEEDS-PROTOTYPE** (API accepted) — `pump()` returns `ServiceHint{remote_attached, paused}`; the loop owner MAY shorten its cadence while paused with a remote attached (headless poll ≤2 ms; Qt/SDL re-arm the tick timer at ~2 ms). Whether it is needed is decided on V-LAT (§7.1) against real DeZog step-out loops |
| 10 | `at_frame_boundary()` query + `save_state_bytes(RefuseMidFrame)` | F9: DeZog does not refresh registers after READ_STATE (remote:1749-1756) | 50 | **ACCEPTED** — `at_frame_boundary()` (= `!frame_in_progress()`, `emulator.h:191`); `save_state_bytes(Mode::AdvanceToBoundary \| Mode::RefuseMidFrame)` → `Result::NotAtFrameBoundary`; DZRP uses `RefuseMidFrame` and answers zero-length |

Not REQs, recorded: `run()` refusal → NTF 255 (§3.2); `pause()` from `pump` is
at a frame boundary by construction (§4.2); GUI resume cannot be told to DeZog
(§4.4); machine type always ZXNEXT (§5.1); watchpoint bank filtered
adapter-side (§5.4).

**MAPPED: 22 used, 4 declined (condition string; reverse — no wire; step
verbs CTL-03..08; IO watchpoints), 3 unsupported-reported (13, 14, 22), 0
REQs open (9 accepted, 1 needs-prototype), 0 reach-arounds.** Backend v2 will
carry the accepted forms; nothing further is needed from the backend unless v2
changes a CAP this adapter uses.

Cross-frontend agreements (recorded so the backend gets one transport REQ):
- `design-gdb` (REQ-gdb-13/14/15): shared service/`pump` model, one pause
  state / last verb wins, per-client breakpoint ownership, `--<proto>-port N`
  per protocol + shared `--debug-listen-address` — agreed with the three
  amendments in §4.2 (public `Esp*` seam, post-frame flush, paused cadence).
- `design-dsl` (REQ-dsl-9): predicate slot optional — confirmed; a script
  `stop` reaches DeZog as reason 255 + its string, and only while a `CONTINUE`
  is outstanding (buf:228).
- `design-qt`: transients hidden; client-owned rows listed read-only; remote
  pause opens the window via GH #219 unless the owner decides otherwise (Q1);
  DZRP has no step verb.

---

## 9. What DZRP cannot carry (recorded, not designed around)

- **Reverse debugging / rewind** (CAP-ST-03/04). No history, trace or reverse
  command exists (owner's own correction, `gh12.md`); DeZog's reverse mode is
  a PC-side ring of registers+stack. The adapter serves nothing; if DeZog gains
  a verb, only this file changes. An upstream extension is optional and later.
- **Conditions in the remote** — the protocol *has* the field (spec:743) and
  the client deliberately does not use it (`breakpoints.md:63-75`). Declined.
- **IO watchpoints** (`WatchType::IO_READ/IO_WRITE`, `breakpoints.h:336`) — no
  DZRP form.
- **A "resumed" notification** — none; §4.4.
- **Refusing a `CONTINUE`** — reply is seq-only; §3.2's NTF-255 idiom.
- **Bank 0xFF in a long address** — byte overflow, §5.3.
- **`CMD_EXEC_ASM`** — deferred, §2 row 22.

---

## 10. Open questions for the owner

1. **Q1 — Should a remote (DZRP) pause open the local Qt debugger window?**
   Today's GH #219 path opens it for any pause with the window closed
   (`debugger_manager.cpp:682-693`). Convenient for a developer using both;
   surprising for a headless-minded GUI session. Default in this design: yes
   (no change to the Qt path); the alternative is a one-line `by`-check in the
   Qt adapter.
2. **Q2 — `CMD_READ_STATE` mid-frame: refuse (this design) or advance (the GUI
   snapshot rule)?** Refusal is spec-sanctioned and safe for DeZog (F9);
   advancing follows the owner's "always advance, never refuse" but can only be
   safe for a client that re-reads registers — which DeZog does not.
3. **Q3 — Port number policy.** No default (explicit `--dzrp-port`), 11000
   documented as DeZog's `cspect` default. An alternative is defaulting to
   11000 when `--dzrp-port` is given without a value, which the option table
   cannot express (fixed arity). Keep explicit?
4. **Q4 — `cspect_dzrp.py` fixes** (REVIEW.md H1/H2/H3) as part of #12's
   harness work, or separately first?

---

## 11. Work packages for #12 (parallelisable; each on its own branch + worktree)

| WP | Scope | Depends on | Files | Review focus |
|---|---|---|---|---|
| WP-1 transport + framing | `src/remote/dzrp/dzrp_transport.{h,cpp}` (listener over `esp::make_socket_listener`, byte-pipe fake for tests), `dzrp_frame.{h,cpp}` (parser/encoder, both length conventions, caps, seq rules); `CMD_LOOPBACK`; `dzrp_adapter_test` framing rows | backend `pump` (CAP-SES-03) | new `src/remote/`, `test/remote/`, `test/unit-tests.conf`, `test/CMakeLists.txt` | framing mutation rows; never blocks; Windows twin builds (`make package-win`) |
| WP-2 adapter core + session | `DzrpServer`: attach/detach, `INIT`/`CLOSE`, `GET/SET_REGISTERS` (verify `Z80_REG` enum = spec table), `READ/WRITE_MEM`, `SET_SLOT`, `GET_TBBLUE_REG`, `SET_BORDER`, ports, `INTERRUPT_ON_OFF`, unknown-command path | backend CAP-INS-01..05, SES-01/02, REQ-6 | `src/remote/dzrp/dzrp_server.{h,cpp}` | side-effect-free reads (a +3-mode `READ_MEM` row that `p3_floating_bus_dat_` is unchanged); refusal paths |
| WP-3 breakpoints, continue, notify | rows 6, 7, 40, 41, `NTF_PAUSE`, temp-first rule, bank byte, exactly-once | REQ-1/2/3/7/8 | same | F7/F8 rows; step-off (GH #221) through `run()`; post-frame flush timing row |
| WP-4 tier 2 | watchpoints (42/43), state (50/51), `WRITE_BANK` (5), sprites (16-19) | REQ-1/4/5/10, backend range watches (#279 work) | same | edge rows both sides of a range; `READ_STATE` refusal; bank-7 BRAM routing |
| WP-5 loop owners + CLI | `pump` calls in `frame_sequencer`, SDL loop, headless loop (paused branch: no `run_frame`, no frame-countdown decrement, `pump(wait)`); `--dzrp-port`, `--debug-listen-address` rows + `main.cpp` cases + `EmulatorConfig`; man page OPTIONS + a DZRP section in `doc/man/jnext.1.md` (`make docs-man`), user guide chapter 6 subsection (`make docs-userguide`) | WP-1/2 | `src/platform/*`, `src/gui/qt_app.cpp`, `src/core/cli_options.h`, `src/main.cpp`, `doc/man/jnext.1.md` | `make cli-check`, `docs-check`; headless paused loop does not spin (CPU time row) |
| WP-6 validation | `cspect_dzrp.py` extensions + REVIEW.md H1 fix; the eight functional rows (§7.2) + `expect:` bump; the DeZog manual protocol (§7.1) executed and recorded in `doc/testing/DZRP-VALIDATION.md` with the DeZog version and V-LAT numbers | WP-1..5 | `tools/cspect_dzrp/`, `test/00regression/` | every functional row uses `timeout --foreground --kill-after=5s`; no `trap` in row scripts |
| WP-7 docs | developer guide page for `src/remote/dzrp` (what is served, the two remote types, the ROM-bank limit, the loop model); `FEATURES.md` line; ChangeLog *Unreleased* line at merge time | WP-5 | `src/doc/developer-guide/`, `FEATURES.md` | `docs-devguide-check` |

Order: WP-1 → WP-2 → {WP-3, WP-4, WP-5 in parallel} → WP-6 → WP-7. Each WP
gets an independent reviewer per CLAUDE.md; the full triplet plus
`make unit-test-sdl` before every merge.
