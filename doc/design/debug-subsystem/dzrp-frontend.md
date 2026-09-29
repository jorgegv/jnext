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
> - v1.2 (2026-09-26 night): after the independent protocol review
>   (`scratchpad/reviews/protocols.md`, verdict REJECT): R-1 — an empty or
>   malformed `CMD_WRITE_STATE` is refused BEFORE the machine is touched, and
>   bookmarks go through the backend's named map (CAP-CAP-03) with a token on
>   the wire; V-STATE / dzrp-state-func / Q2 rewritten to what DeZog actually
>   does. R-4 — drain-while-paused adopted, `pump(PumpBudget{…})`, step-latency
>   arithmetic restated with and without the drain (4-6 round trips per DeZog
>   step). R-5 — one port rule (absent = off, `0` = ephemeral, logged) and one
>   `--debug-listen-address` help text agreed with design-zrcp/design-gdb.
>   N-3 stale line citations regenerated; N-4 watchpoint banks reported, never
>   auto-continued (REQ-dzrp-11 filed); N-5 stop policy is the loop owner's;
>   N-6 served count corrected to 26; N-7 matrix cells reported to the backend.
> - v1.3 (2026-09-26 night, round-2 review APPROVE, `protocols-r2.md` notes):
>   headless "remote connected ⇒ Pause" marked OWNER-PENDING (all eight
>   functional rows depend on it); WP-1 consumes the shared transport package
>   T instead of writing a listener; DeZog's own `TimeWait` throttle recorded
>   beside V-LAT; `esp_socket.h:518-527`; a `Reset{Hard}` row added to the
>   `NTF_PAUSE` mapping — settled the same night: a hard reset never pauses a
>   running machine, so the adapter sends nothing; bookmarks survive a
>   reconstruct, cross-machine restores are refused before `load_state`.
> - v1.4 (2026-09-27, owner note "DZRP must be 2.2.0"): re-verified against
>   upstream DeZog `main` @ `0de07af6` (3.8.0-rc7). jnext answers **2.2.0**;
>   `CMD_GET_SUPPORTED_COMMANDS` (24), `CMD_READ/WRITE_BANK_MEM` (25/26) and
>   `CMD_ENABLE_BREAK_ON_INTERRUPT` (39) served; `CMD_WRITE_BANK` (5) and
>   `CMD_SET_BORDER` (12) kept as legacy for 2.0/2.1 clients but not advertised;
>   `NTF_LOG` not emitted; seq 1-15 accepted alongside 1-255; `CMD_PAUSE`
>   notifies only when it stopped something. F2/F3/F4/F7 corrected for 3.8's
>   subset-driven remotes (watchpoints and bookmarks become DeZog-reachable).
>   Owner answers Q3/Q5/Q6/Q8 recorded (§10). Served 30 / declined 5 +
>   unsupported 3 / 0 reach-arounds.
> - v1.5 (2026-09-27, round-4 review `protocols-r4.md`, verdict REJECT):
>   R-1 — bank 0xFF reads now use the backend's settled `Rom{index}` (a 16 KB
>   ROM image 0..3; SRAM pages 2i/2i+1 on the Next) through `SlotInfo.space` +
>   `space_offset` (REQ-qt-31); the "no new capability" claim withdrawn and
>   REQ-dzrp-12 filed and ACCEPTED.
>   R-2 — `CMD_ENABLE_BREAK_ON_INTERRUPT` is advertised and served but
>   reachable today only from a foreign client (rc7 never calls its sender);
>   V-BRKINT marked not executable against rc7. N-1..N-7 folded: ROM index
>   only while slot 0 is ROM (else paging-derived or refused), V-BANKMEM says
>   `remoteType: dzrp` and adds an out-of-page offset, counts "nine", §7.1
>   heading covers both builds, §10 lists Q2 and Q4 as the open items, row 24
>   spells the 7 advertised bytes and why clearing 5/12 is safe, DeZog's
>   swapped state-gating quirk noted.
> - v1.6 (2026-09-27): owner decisions on the last two open items — Q2 =
>   REFUSE a mid-frame `CMD_READ_STATE` (the design as written), Q4 = the
>   `cspect_dzrp.py` H1-H3 fixes land inside #12's WP-6. §10 retitled "Owner
>   decisions — nothing open"; the "owner may prefer advance" wording removed.
> - v1.7 (2026-09-29, milestone 1 — WP-1 + WP-2 implemented, branch
>   `gh12-dzrp`): §12 records what was built, every deviation from this design
>   with its reason, three backend defects fixed on the branch, and the design
>   findings (T's fit, one backend gap). Sections 0-11 are unchanged: where
>   the implementation differs, §12 says so and why.

Every claim below carries a `file:line` citation. Sources and their versions:

| Source | Where | Version |
|---|---|---|
| DZRP spec | upstream `main:design/DeZogProtocol.md` (935 lines; saved to the scratchpad as `dzrp_spec_upstream.md`) — **2.2.0** (`:134-149`); the local checkout's copy ends at 2.1.0 | **spec:N** = line in the UPSTREAM file unless marked `spec21:N` (the local 2.1.0 copy) |
| DeZog client (the truth where the spec is vague) | `/home/jorgegv/src/spectrum/dezog.jorgegv/src/remotes/` — local checkout = DeZog **3.7.4** (DZRP 2.0/2.1 client); **upstream `main` @ `0de07af6` = 3.8.0-rc7** (DZRP 2.2.0 client), read via `git show upstream/main:…` after `git fetch upstream` on 2026-09-27 | 3.7.4 is what the marketplace ships (published 2026-07-30); 3.8.0 has no release tag yet. **up:N** below = a line in the upstream file |
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

## Work packages — the tracker for this package

Mirrors this package's row in [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
§10.1, which stays authoritative: if the two ever disagree, §10.1 wins and this
table is stale. It exists because §10.1 states each package's sequence as one
long table cell, which is unreadable as a plan and impossible to track against.

Status values: `todo` · `in progress` · `in review` · **`done`** (independently
reviewed and APPROVED). The whole package lands on **one branch** and merges
whole, so `done` here means the sub-item is approved, not merged.

| WP | Branch `gh12-dzrp` (issue #12) | Status |
|---|---|---|
| **WP-1** | framing over the shared transport (T) | in review |
| **WP-2** | session / registers / memory | in review |
| **WP-3** | breakpoints / continue / notify | todo |
| **WP-4** | tier 2 (the commands only an emulator can serve) | todo |
| **WP-5** | loop owners + CLI | todo |
| **WP-6** | validation — **including the `tools/cspect_dzrp/cspect_dzrp.py` H1-H3 fixes** (owner decision §1.3 item 25: part of this package, not a separate change), and the V-LAT paused-cadence measurement (§11 item 6) | todo |
| **WP-7** | docs | todo |

WP-3, WP-4 and WP-5 may run in parallel after WP-2. Depends on: B0 (landed), B, T.

**Reuse, not rewrite** (owner, 2026-09-27): `dezogif_ng` — the owner's own DZRP
client for real hardware, a local sibling checkout — carries a full conformance
suite. **Copy the needed bits in; do NOT add it as a submodule.** `test/dzrp/dzrp.py`
(framing, every command id including tier 2, `TcpTransport`), `conformance.py`,
and the per-scenario clients that map onto contracts this design states
independently: `queued-commands.py` ↔ SES-03's drain-while-paused,
`split-command.py` ↔ frame reassembly, `orphan-notify.py` /
`abandoned-send-client.py` ↔ SES-01's "a crashed DeZog must not leave the machine
hung". It negotiates **2.1.0** (`dzrp.py:360`) while jnext answers **2.2.0**, so
the version becomes a parameter. GPLv3 both sides, so it is licence-clean. Detail
on #12.

Every sub-item is reviewed by an agent or person that did NOT write it, and the
branch does not merge until the full §10.3 gate is green on the tip.

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

**F1. Which DZRP version to answer: 2.2.0.** The current spec is 2.2.0
(spec:134-149). DeZog 3.8 requires it: every socket/serial remote derives from
`DzrpTransportRemote` with `DZRP_VERSION = [2, 2, 0]` (up
`dzrptransportremote.ts:60`). The check is unchanged: *major equal AND
remote.minor ≥ client.minor* (up `:631-632`; 3.7.4 `buf:392-393`). So a
remote answering 2.1.0 is **refused by DeZog 3.8** ("Required version is 2.2
or higher"), while **2.2.0 satisfies both** 3.7.4 (`[2,0,0]`, 2 ≥ 0) and 3.8.
The CSpect plugin still answers 2.0.0 (`REVIEW.md` N5) — it will fail
against 3.8 until updated; jnext must not copy it. 2.2.0 carries obligations
beyond the number: `CMD_GET_SUPPORTED_COMMANDS` "MUST be supported by any
remote that supports DZRP >= 2.2.0" (spec:730), and DeZog 3.8 sends it
immediately after `CMD_INIT` (up `dzrpremote.ts:229-237`
`handleSupportedCommands`). **2.2.0 delta, verified against the upstream
spec and client:**

| Change (spec:134-149) | Wire | jnext |
|---|---|---|
| `CMD_GET_SUPPORTED_COMMANDS` (24) added | reply = bitfield, 1-32 bytes LE, bit *n* of the field = command *n* (byte *j* bit *b* → id 8j+b; spec:710-731; decoded at up `dzrptransportremote.ts:1119-1127` and `:123-140`) | served, adapter-only (§2 row 24); DeZog disables every unsupported command client-side by replacing its method with a thrower (up `:147-160`) — the honest-refusal mechanism 2.1 lacked |
| `CMD_READ_BANK_MEM` (25) / `CMD_WRITE_BANK_MEM` (26) added | `bank, offset u16, size u16` / `bank, offset u16, data` (spec:735-775; up `:853-891`); DeZog uses them for the loaders (`.sna/.z80/.nex`, up `dzrpremote.ts:1660-1783`) and for bank-qualified memory views | served over CAP-INS-02 `Page{bank}` / `Rom` (§2 rows 25/26, §5.2) |
| `CMD_ENABLE_BREAK_ON_INTERRUPT` (39) added | `0/1` (spec:776-789); advertised support sets `supportsBreakOnInterrupt` (up `:194`) | served over the backend's `IntAck` event kind (§2 row 39, §3.3) |
| Sequence numbers 1-**15**; response byte bits 4-7 unused (spec:212-262) | DeZog 3.8 wraps at 15 (up `:247-252`) and compares the echoed byte exactly (up `:493-496`) | accept 1-255 (2.0/2.1 clients) and 1-15 alike; echo the byte verbatim (§4.2.5) |
| "normal" vs "simple" mode (spec:920-935) | DeZog picks *simple* (13/14) only when 40 is unsupported (up `:94-98`) | jnext advertises 40/41 → normal mode; 13/14 unsupported |
| `CMD_WRITE_BANK` (5) **removed** | loaders now send 26 with offset 0 | kept as legacy for 2.0/2.1 clients, bit 5 clear in the bitfield (§2 row 5) |
| `CMD_SET_BORDER` (12) **removed** ("use CMD_WRITE_PORT") | loaders now send `WRITE_PORT(0xFE, colour)` (up `dzrpremote.ts:1672,1731,1775`) | kept as legacy, bit 12 clear (§2 row 12) |
| `NTF_LOG` (2) added | remote → DeZog format string + data (spec:893-918); "only meant for debugging purposes"; shown only if the user enables a log channel (3.8 CHANGELOG) | not emitted (§2, §9) |
| `CMD_PAUSE` semantics spelled out | "if the program is not running when received, nothing happens"; NTF only when a program is stopped, and AFTER the response (spec:396-414) | §2 row 7 follows it |
| `CMD_RESTORE_MEM` restores only if the byte is still 0xC7 | simple-mode detail | unsupported command, no effect |
| `MemoryModelZxNext` uses **0xFF for ROM in both slots** (3.8 CHANGELOG "Internally used 0xFF for ROM instead of 0xFE"; up `zxnextmemorymodels.ts:69-104`); the 3.7.4 slot-0 rewrite 0xFF→0xFE is gone (up `dzrpremote.ts:304-311`) | `GET_REGISTERS` unchanged | §5.3 updated |

Everything else — framing, `GET_REGISTERS` layout, `CONTINUE` (still 11 bytes,
up `:708-727`), `NTF_PAUSE` layout, breakpoints 40/41, watchpoints 42/43, state
50/51, sprites, ports — is byte-identical between 2.1 and 2.2 (diff of the two
spec files; only the seq range text changed in those sections).

**F2. Which DeZog remote type reaches jnext.** Two client generations:

*DeZog 3.7.4 (marketplace)* has exactly two DZRP-over-socket remote types
(`remotefactory.ts:26-35`):

| remoteType | breakpoint dialect | refuses **client-side**, before any packet | notes |
|---|---|---|---|
| `cspect` (port 11000 default `settings.ts:604`) | `CMD_ADD_BREAKPOINT`/`REMOVE` (40/41), remote owns bp state | watchpoints (cspect:162-175), state save/restore (cspect:180-185), `supportsWPMEM=false` (cspect:28) | sprites, TBBlue, ports, `EXEC_ASM` all sent |
| `zxnext` with `hostname` (fork PR #186 only in 3.7.x) | `CMD_SET_BREAKPOINTS`/`RESTORE_MEM` (13/14) | sprites, state, watchpoints; `IM` decoded as NaN | built for opcode-patching stubs |

*DeZog 3.8 (upstream `main`)* rebuilt all of them on one
`DzrpTransportRemote` (up `src/remotes/dzrptransport/`): `cspect` (up
`cspectremote.ts`, 44 lines, refuses only ZX81 loads; port 11000), the new
generic **`dzrp`** (up `dzrpgenericremote.ts`; "can support all DZRP
commands"; socket default `localhost:14000`, up `settings.ts:933-946`;
documented "Experimental. Use to connect new DZRP remotes", Usage.md:223), and
`zxnext` (dezogif, socket or serial, up `remotefactory.ts:31-37` — the fork's
socket variant was merged). **Nothing is refused client-side any more: the
subset is whatever `CMD_GET_SUPPORTED_COMMANDS` reports**, and the mode is
chosen from it (up `:94-98`).

**Decision: jnext presents as a normal-mode remote** — `remoteType:
"cspect"` on 3.7.4 (port from `--dzrp-port`, DeZog's default 11000), and
`remoteType: "dzrp"` (or `cspect`) on 3.8 (`dzrp`'s default port is 14000;
`--dzrp-port` has no default, so the user sets both to the same number). It
keeps breakpoint state where an emulator keeps it (no opcode patching to hide
from `READ_MEM`/disassembly — the hazard `DEZOG-BREAKPOINTS-DESIGN.md:36-54`
documents), and it is what `tools/cspect_dzrp/cspect_dzrp.py` already speaks.
Commands 13/14 are therefore **unsupported-reported**, not tier 2 (§2), and
bit 40 set in the bitfield is what keeps a 3.8 client in normal mode.

**F3. Who can reach the tier-2 commands.** With **3.7.4**, no released
remote sends watchpoints or state over a wire: the compatibility table
(spec21:87-119) marks `ADD_WATCHPOINT`/`READ_STATE` only for `zsim`
(in-process) and MAME (gdb), and both socket remotes refuse them client-side
(F2). With **3.8** that changes: `supportsWPMEM` follows bit 42 (up
`dzrptransportremote.ts:190`), `-state save/restore` is allowed iff bits 50/51
are set (up `:197-206`), and the consistency rules require the pairs 42/43,
50/51 and 40/41 to be advertised together (up `:209-216`). So the tier-2
commands become **DeZog-reachable from 3.8 on**, and remain reachable today by
non-DeZog clients — jnext's own `cspect_dzrp.py`, ZX Basic Studio (#12's
reporter), dezogif_ng's `test/dzrp/dzrp.py`. The validation plan (§7) says
which client covers which row; the man page says "DeZog 3.8 or later" for
watchpoints and bookmarks.

**F4. What DeZog actually sends on connect.** `onConnect` (3.7.4
remote:230-275; 3.8 up `dzrpremote.ts:229-275`): `CMD_INIT` → **(3.8 only)
`CMD_GET_SUPPORTED_COMMANDS`** → `load()` — only if `launch.json` names a
`load` file: `.sna`/`.z80` = bank writes ×N (`WRITE_BANK` on 3.7.4,
`WRITE_BANK_MEM` offset 0 on 3.8), `SET_SLOT`×8, border (`SET_BORDER` on
3.7.4, `WRITE_PORT 0xFE` on 3.8), `SET_REGISTER`×15, `WRITE_PORT 0x7FFD`
(128K), `INTERRUPT_ON_OFF` (remote:1600-1706; up `:1655-1760`); `.nex` =
border, bank writes, `SET_SLOT`×8, `SET_REGISTER`×2 (remote:1712-1741; up
`:1770-1795`) — then the memory model is instantiated from the `CMD_INIT`
machine-type byte. After `initialized`, the debug adapter reads registers
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
(§3.4). The byte overflows for bank 0xFF (§5.3) — a protocol limit that 3.8
widens: its model uses 0xFF for ROM in **both** slots 0 and 1 (F1 table), so
no ROM breakpoint at all fits the byte any more.

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
**But the refusal is invisible to a DeZog user** (review R-1): `stateSave`
gzips whatever came back and writes it with no length check (remote:1749-1756),
so `-state save` after a breakpoint silently writes an *empty* state file, and
`-state restore` later sends that emptiness back as a **0-byte
`CMD_WRITE_STATE`** (remote:1764-1770). The adapter must refuse that payload
before it reaches `load_state_bytes` — a corruption latch there would leave
the session with every `CONTINUE` refused (§3.2) and no way to acknowledge from
DeZog. §6 does exactly that.

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
| 1 | `CMD_INIT` | cspect, zxnext, dzrp | T1 | CAP-SES-01 attach, CAP-CTL-01 pause | Cmd: version(3, big-endian) + name\0 (spec:277-281). Reply: err, **2.2.0** (F1), machine **ZXNEXT=4** always (§5.1), `"jnext v<version>\0"` (spec:283-291). The client's version is recorded: a 2.0/2.1 client (3.7.4) is served the legacy rows 5/12 and 1-255 seqs; a 2.2 client gets the bitfield next. Attaches the client, **pauses the machine** (F4), records the client's DZRP version. A second connection while one is open is accepted and immediately closed with a log line (one client, as the CSpect plugin: `README.md` "No multi-client support"). |
| 2 | `CMD_CLOSE` | cspect, zxnext | T1 | CAP-SES-01 detach | Reply seq only. Detach: the backend removes this client's subscriptions and resumes if the machine is paused *by this client* (CAP-SES-01). DeZog's `cspect` remote sends `CMD_PAUSE` before `CLOSE` (cspect:114), so a graceful close leaves the machine **running**; a dropped socket is treated as `CLOSE`. |
| 3 | `CMD_GET_REGISTERS` | cspect, zxnext | T1 | CAP-INS-01, CAP-INS-03 | Reply after seq: PC SP AF BC DE HL IX IY AF' BC' DE' HL' (12 LE words), R, I, IM, reserved(0), Nslots=8, 8 slot bytes = 37 bytes (spec:296-319; parsed at buf:421-441). Slot byte = NR 0x50+s visible page, **0xFF for ROM** (`mmu.h:53`; DeZog rewrites slot 0's 0xFF to 0xFE itself, remote:295-302). IFF1/2 are not on the wire (spec). |
| 4 | `CMD_SET_REGISTER` | cspect, zxnext, load | T1 | CAP-INS-01 | Cmd: reg index (spec:327 numbering: 0=PC … 11=HL', 13=IM, 14=F … 35=I; 12 unused) + u16 LE; 8-bit targets take the low byte (spec:328). Unknown index → seq-only reply + warn log. WP-2 verifies DeZog's `Z80_REG` enum equals the spec table. |
| 5 | `CMD_WRITE_BANK` | load (3.7.4 only — **removed in 2.2.0**, spec:147) | T1 legacy | CAP-INS-02 `Page{}` | Served for 2.0/2.1 clients; bit 5 is CLEAR in the `GET_SUPPORTED_COMMANDS` bitfield (a 2.2 client must use row 26). Cmd: bank(1) + 8192 bytes (spec21:337-354). Bank is the MMU page number 0..223 (`MemoryModelZxNextOneROM`, `zxnextmemorymodels.ts:192-256`); the backend routes it (REQ-dzrp-5: +0x20 in Next mode `mmu.h:1387-1390`, page 0x0E → `bank7_bram` `mmu.h:1392-1401`). Reply: err + string (spec:357-362): `1 "bank out of range"` for >223 / 0xFE / 0xFF, `1 "length must be 8192"` otherwise. DeZog throws the string (buf:626-631) — the honest refusal reaches the user. |
| 6 | `CMD_CONTINUE` | cspect, zxnext | T1 | CAP-EVT Execute ×2 with `Transient` (REQ-dzrp-3), CAP-CTL-02 | §3.2. Reply seq only, **immediately, before the machine runs** (spec:399; CSpect does the same, `REVIEW.md`). Payload ≥5 bytes (F5). |
| 7 | `CMD_PAUSE` | cspect, dzrp, zxnext-socket | T1 | CAP-CTL-01 | Reply seq only; then **one** `NTF_PAUSE` reason 1, sent AFTER the response, **only if the command actually stopped a running machine** — 2.2.0 spells it out: "if for some reason the program is not running when received, nothing happens" and the NTF is sent "if a program is stopped" (spec:396-414). Already paused → response only. (3.7.4 ignores a stray NTF anyway, buf:228, so the rule is safe for both.) Because commands run between frames (§4.2) the pause lands at a frame boundary. |
| 8 | `CMD_READ_MEM` | cspect, zxnext | T1 | CAP-INS-02 `Cpu` peek | Cmd: reserved(1), addr u16, size u16 (spec:421-426; the reserved byte is mandatory, `README.md`). CPU view through the live mapping, **side-effect free** (`Mmu::peek`, backend finding F1 — `Mmu::read` latches the +3 floating-bus byte). size 0 → empty reply; DeZog splits a 64K read into two 32K (buf:583-592). |
| 9 | `CMD_WRITE_MEM` | cspect, zxnext, load | T1 | CAP-INS-02 `Cpu` poke | As the CPU would write: ROM-mapped bytes are dropped (`mmu.h:505`), Layer-2 write-over applies. Reply seq only; a dropped byte is logged at debug level (DZRP has no error field here). |
| 10 | `CMD_SET_SLOT` | load, console | T1 | CAP-INS-03 | Cmd: slot, bank (spec:456-474). Through the NR 0x50+slot write path so the write is what the guest's own `NEXTREG` would do; 0xFF (and 0xFE on slot 0, spec:473) = ROM. Reply err byte: 1 for slot >7 or bank 224..0xFD. |
| 11 | `CMD_GET_TBBLUE_REG` | cspect, zxnext | T1 | CAP-INS-04 peek | `NextReg::peek` (`nextreg.h:53-62`): no read handler side effects, no phantom log lines. Reply: value. |
| 12 | `CMD_SET_BORDER` | load (3.7.4 only — **removed in 2.2.0**, spec:148; 3.8 sends `WRITE_PORT(0xFE, colour)` instead, up `dzrpremote.ts:1672`) | T1 legacy | CAP-INS-18 `set_border` (REQ-dzrp-6) | Served for 2.0/2.1 clients; bit 12 CLEAR in the bitfield. Bits 2:0 (spec21:498-503). `Ula::set_border` (`ula.h:180`), not a port write. A 3.8 client's `WRITE_PORT 0xFE` is a real OUT (row 21): border set, EAR/MIC driven low — DeZog's choice, documented. |
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
| 23 | `CMD_INTERRUPT_ON_OFF` | load | T1 | CAP-INS-01 | IFF1 = IFF2 = flag (spec:697-708; sent on every .sna/.z80 load, remote:1649,1705; up `:1700,1759`). |
| 24 | `CMD_GET_SUPPORTED_COMMANDS` | dzrp, cspect, zxnext (3.8, right after `INIT`) | T1 | — (adapter-only) | 2.2.0, MUST (spec:710-731). Reply: 7 bytes LE, bit *n* = command *n* served. **Set:** 1 2 3 4 6 7 8 9 10 11 15 16 17 18 19 20 21 23 24 25 26 39 40 41 42 43 50 51 (28 bits) — **on the wire: `DE 8F BF 07 80 0F 0C`** (the unit row's expected bytes). **Clear:** 5 and 12 (removed in 2.2.0, still served for older clients), 13 14 22 (unsupported). Clearing 5/12 is safe because 3.8's `DZRP` enum no longer contains them (up `dzrpremote.ts:41,48` commented out), so `disableUnsupportedCommands` (up `:147-160`, which throws "Methode … does not exist" for an enum entry without a sender) never iterates them (N-6). DeZog turns every other clear bit into a client-side thrower, so an unsupported feature fails with a named error instead of a timeout — this is the 2.2 form of "reported, never silent". The pairs 40/41, 42/43, 50/51 are advertised together (up `:209-216` refuses an inconsistent field). DeZog's own gating of `-state` is swapped (up `:197-206`: `stateSave` disabled when 51 is clear, `stateRestore` when 50 is) — harmless here since both are set (N-7). |
| 25 | `CMD_READ_BANK_MEM` | dzrp, cspect (3.8: bank-qualified memory views, `-md … bank=`) | T1 | CAP-INS-02 `Page{bank}` peek; CAP-INS-03 `SlotInfo.space` → CAP-INS-02 `Rom{index}` for 0xFF | Cmd: bank, offset u16, size u16 (spec:735-754; DeZog splits 64K into 2×32K, up `:853-875`). bank 0..223 → `peek(Page{bank}, offset, size)`; **offset + size must stay inside 8 KB** — bytes past the page are not served (reply the bytes that fit; a wholly out-of-range request gets an empty reply + warn). bank 0xFF (3.8's ROM id, one 16 KB bank spanning both slots — `zxnextmemorymodels.ts:64-104`, `bankSize: 0x4000`, slot 1 `bankOffset: 0x2000`) → offset 0x0000-0x1FFF is read through `SlotInfo(0).space`, 0x2000-0x3FFF through `SlotInfo(1).space` (settled by REQ-dzrp-12 / REQ-qt-31: `Rom{index}` names a **16 KB ROM image** 0..3, addresses 0..0x3FFF, read-only — on a ROM-in-SRAM machine SRAM pages 2·index / 2·index+1; a ROM slot's `SlotInfo.space` is `Rom{effective_page >> 1}` with `space_offset = (effective_page & 1)·0x2000` selecting its 8 KB half, so the adapter reads `peek(space, space_offset + (offset & 0x1FFF), n)` and composes nothing itself), **only while that slot `is_rom`**; when RAM is paged at 0x0000 (NR 0x50 ≠ 0xFF, or port 0xEFF7 bit 3, `mmu.cpp:536-538`) the ROM select `sel` is derived from CAP-INS-03 `paging_ports()` (7FFD b4 \| 1FFD b2) and the read is `peek(Rom{sel}, offset, n)`; if even that is unavailable the reply is empty + warn (N-1). |
| 26 | `CMD_WRITE_BANK_MEM` | load (3.8), dzrp console | T1 | CAP-INS-02 `Page{bank}` poke | Cmd: bank, offset u16, data (spec:756-775). Same range rule; ROM bank → nothing written, warn log (the command has no error field, unlike the removed `WRITE_BANK`). Loaders send offset 0 with 8192 bytes (up `dzrpremote.ts:1660-1661`). |
| 39 | `CMD_ENABLE_BREAK_ON_INTERRUPT` | **none in 3.8.0-rc7** — the sender `sendDzrpCmdEnableBreakOnInterrupt` (up `dzrptransportremote.ts:1132-1135`) has no caller; the UI (`exceptionbreakpoints.ts:86-88, 299-301`) calls `Remote.enableBreakOnInterrupt`, which no DZRP class overrides and which returns `false` ("Only supported by zsim", `remotebase.ts:1115-1117`), so enabling the option prints "Break on interrupt: disabled." and puts nothing on the wire. Reachable today only from a foreign client (`cspect_dzrp.py`) | T1 | CAP-EVT `IntAck` subscription, `Stop`, owner=client | 2.2.0 (spec:776-789). Served and **advertised** (bit 39): the spec defines it, a later DeZog will wire it, and advertising costs only that 3.8 shows the "Break on Interrupt" exception option (`funcSupported`, up `:194`) — which then does nothing, DeZog's defect, not ours. 1 → subscribe `IntAck{Stop}` (the accepted-maskable-interrupt seam, `backend.md` §4.3, `emulator.cpp:1114`); 0 → unsubscribe. NMI is not an "interrupt" here. Stop → `NTF_PAUSE` reason 255, address PC, string `"Break on interrupt."` (§3.3). No DeZog-coverage claim in the man page (R-2). |
| 40 | `CMD_ADD_BREAKPOINT` | cspect, dzrp | T1 | CAP-EVT Execute[a,a] Stop, owner=client, **no predicate** | §3.1. Cmd: addr u16, bank+1, condition\0 (spec:738-743). The condition string is **ignored** (decision 2). Reply u16 id; 0 = refused (spec:750; DeZog marks the bp unverified, remote:1378-1379). |
| 41 | `CMD_REMOVE_BREAKPOINT` | cspect | T1 | CAP-EVT unsubscribe | By id. Unknown id → seq-only reply + warn. |
| 42 | `CMD_ADD_WATCHPOINT` | none released (F3) | T2 | CAP-EVT MemRead/MemWrite [lo, lo+size-1] Stop | Cmd: addr, bank+1, size u16, access bit0 read / bit1 write (spec:771-777). Ranges are what the backend's #279 work adds (backend §4.3/§8); the old per-address `add_watchpoint` would have needed `size` entries scanned per access. Bank: §5.4. Reply err: 0 ok, 1 refused (size 0, both access bits clear, or wraps past 0xFFFF — "wrap around is ignored" by DeZog too, remote:669). |
| 43 | `CMD_REMOVE_WATCHPOINT` | none released | T2 | CAP-EVT unsubscribe | Matched by the exact (addr, bank, size, access) tuple the adapter recorded at add time (DZRP watchpoints have no id, spec:104). |
| 50 | `CMD_READ_STATE` | none released (F3) | T2 | CAP-CAP-03 `bookmark_save(name)` over CAP-ST-01 (+REQ-dzrp-10) | §6. Reply: a **bookmark token** (`"JNXB"` + name), not the multi-megabyte stream — "arbitrary data, the format is up to the remote" (spec:817); **zero length** when mid-frame (spec:819). |
| 51 | `CMD_WRITE_STATE` | none released | T2 | CAP-CAP-03 `bookmark_restore(name)` over CAP-ST-02 | §6. Payload validated **before the machine is touched**: empty, wrong magic, or unknown token → reply seq only, then `NTF_PAUSE` reason 255 "no state to restore" — no `load_state_bytes` call, no corruption latch (R-1). A valid token restores; a sentinel failure inside the restore is the one case that latches (CAP-CTL-11) and is reported the same way with its subsystem name. |
| — | `NTF_PAUSE` (1) | ← to client | T1 | CAP-SES-02 Paused, CAP-INS-17 events_fired_since | §3.3. Frame: len, seq 0, `1`, reason, addr u16, bank+1, string\0 (spec:884-891; parsed buf:225-245, up `:452-475`). Exactly one per `CMD_CONTINUE`, plus one per `CMD_PAUSE` that stopped something (row 7). |
| — | `NTF_LOG` (2) | ← to client (2.2.0) | DECL | — | Not emitted. The spec marks it "only meant for debugging purposes. Don't use it in production code" (spec:914-918) and DeZog 3.8 shows it only when the user enables the "DeZog DZRP Log notifications" channel. It is the one channel that could make the R-1 refusal visible (§6, Q2); recorded as an option the owner may switch on later, not designed in. |
| — | unknown command id (incl. any id not in the bitfield) | — | UNS | — | Seq-only reply + warn log "unsupported DZRP command N". The frame is length-prefixed, so it is always consumed exactly (dezogif_ng issue #7's lesson, `DEZOG-BREAKPOINTS-DESIGN.md:84-85`). |

**Counts.** 33 command ids (the 31 of 2.2.0 plus the two 2.2.0 removed) + 2
notifications. Served: **30 commands** — 26 T1 (rows 1-12, 15-21, 23-26, 39,
40, 41; row 15 `LOOPBACK` and row 24 are adapter-only and map to no CAP; rows
5 and 12 are legacy, served but not advertised) + 4 T2 (42, 43, 50, 51).
Declined-by-design: the condition string (in row 40), reverse debugging (no
command to decline — §9), the six backend step verbs CTL-03..08 (DZRP has no
step; §3.2), IO watchpoints (no wire form), and `NTF_LOG` (not emitted).
Unsupported-reported: 13, 14, 22 (bits clear; seq-only reply if sent anyway).

**Every mutating command** (rows 4, 5, 9, 10, 12, 21, 23, 26, 51 and the
breakpoint/watchpoint rows) is logged by the backend as one CAP-SES-06
`MUTATE … by dzrp` line per command — one line for an 8 KB `WRITE_BANK_MEM`,
not one per byte — so a DeZog `load` leaves a readable trace in jnext's log
(`backend.md` v4). The adapter adds no logging of its own for those.

**What each client can reach, honestly:** DeZog 3.7.4 — rows 1-4, 6-12, 16-21,
23, 40, 41 (5/12/23 only with a `launch.json` `load`); rows 42/43/50/51 only
from a non-DeZog client (F3). DeZog 3.8 — everything advertised in row 24,
including 42/43/50/51 (F3) and the new 24/25/26/39; never 5/12 (removed). §7
validates each group against the client that actually sends it. The man page
claims DeZog coverage only for what a released DeZog sends: never for row 39
(R-2), and "DeZog 3.8 or later" for 42/43/50/51.

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
- The GH #221 step-off arm (`debug_state.h:180-206`, `:300-301`)
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
  one one-shot, `breakpoints.h:194-197`); then `run()`. Reply seq only
  *before* anything runs (§4.2 guarantees it).
- On the next stop (any cause) the backend removes both transients; the adapter
  forgets their ids. DeZog removes nothing — "they will be removed automatically
  after the command is finished" (spec:401).
- The adapter **never** calls CAP-CTL-03..08. In particular not `step_over()`
  (the GUI's call-like heuristic, `debugger_manager.cpp:413-428`) — DeZog has
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
| this client's `IntAck` subscription (row 39) | 255 | PC (the handler entry, after the acknowledge) | `"Break on interrupt."` — DeZog's own text for its internal `BREAK_INTERRUPT` (base:33 "zsim only", remote:869-871); 255 + string is the spec-defined way to say it over the wire |
| anything else: `User` (GUI pause), another client's breakpoint, `Magic`, `Script(id)`, `Corrupt`, `Step`/`RunTo` from the GUI | 255 | PC | human-readable, e.g. `"paused by GUI"`, `"magic breakpoint"`, the script's stop text |
| `Reset{Hard}` (guest NR 0x02 bit 1, the GUI's F1, or another client's `reset(Hard)`) while this client has a `CONTINUE` outstanding | **nothing is sent** | — | — |

The `Reset{Hard}` row is the decided CAP-CTL-12 contract (round 2): a hard
reset **never pauses a running machine and there is no `Reset` pause
reason** — `Reset{Hard}` is a listener event only. DZRP's notification is a
*pause* notification, so the adapter sends nothing: DeZog keeps waiting on
its outstanding `CONTINUE`, the machine reboots through `nextboot.rom`, the
backend re-applies this client's subscriptions — the two transient temp
breakpoints included — on the reconstructed machine (CAP-SES-07), and the
next stop (a breakpoint, a temp address, or a `CMD_PAUSE`) produces the
ordinary `NTF_PAUSE`. The adapter's own bookkeeping (`continue_outstanding`,
id maps, tokens) is untouched by the event. DZRP has no reset command, so
CTL-12 is never called from this adapter.

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
  same. The adapter sets **no** stop policy: CAP-SES-04 is the loop owner's.
  The rule that `--headless` switches from `ExitNonZero` to `Pause` + notify
  while a remote client is connected is the backend's *proposal* on top of the
  owner's #279 headless rule and is **OWNER-PENDING** (`backend.md` §13.2,
  arch doc §12 Q3; default = the exception applies). **All nine DZRP
  functional rows in §7.2 run `--headless` and rest on it** — **DECIDED by
  the owner 2026-09-27: the exception applies** (remote attached ⇒ headless
  Stop pauses + notifies), so the rows stand as written. Nothing about
  headless-vs-GUI lives in the adapter.
- `CMD_CLOSE` or socket EOF/RST → `detach(cid)`: the backend drops the client's
  subscriptions and resumes if the pause was this client's (CAP-SES-01). The
  listening socket stays open for the next session (the CSpect plugin closes
  its listener after one accept, `REVIEW.md` "listener accepts ONE" — jnext
  deliberately does not: a headless CI run may attach, detach, re-attach).
- One client at a time (row 1).

### 4.2 Loop ownership — the single-threaded model, shared with ZRCP and RSP

Agreed with `design-gdb` (its REQ-gdb-13/14/15) and `backend.md` §5:

1. **No thread.** The adapter is a service object with `poll()`; the backend's
   `pump(PumpBudget{max_wait_ms, drain_ms, budget_ms}) -> ServiceHint
   {remote_attached, paused}` (CAP-SES-03, `backend.md` v3) calls it. The three loop owners call `pump`
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
   (`qt_app.cpp:667`). `Paused` is delivered synchronously inside `run_frame()`
   (CAP-SES-02) and queued; the adapter writes it in that `pump`; commands read
   there apply to the next tick's frames.
   **Drain while paused** (REQ-zrcp-01, accepted into CAP-SES-03; adopted here,
   review R-4): while the machine is paused, after answering one command the
   pump waits up to `drain_ms` (~2 ms) for the next complete command and
   answers it too, until the socket is quiet or `budget_ms` (~10 ms) is spent.
   Only a command that needs the machine to *run* (`CMD_CONTINUE`) ends the
   drain; every inspection command is answered inside it.
   **Step-latency arithmetic, restated.** One DeZog step over DZRP is 4-6
   sequential round trips (remote:1105-1152, 1039-1087; `remotebase.ts:775-795`,
   `:1545-1557`): `CMD_CONTINUE` → `NTF_PAUSE` → `CMD_GET_REGISTERS` →
   `CMD_READ_MEM` (call stack, ≥1) → `CMD_READ_MEM` (4 bytes at PC for
   `calcStepBp`; +1 at SP for a `RET`) → next `CMD_CONTINUE`. Without the
   drain each of those is one tick: **4-6 ticks = 80-120 ms per step** at
   50 Hz. With the drain the inspection commands are answered in one pump:
   **≈ 1 tick + ≤ 10 ms per step**, provided the Node client answers each
   reply within `drain_ms` on loopback (plausible, unmeasured — V-LAT).
   Latency budget, stated: command → effect ≤ 1 tick; stop → `NTF` ≤ same
   tick; vs sub-millisecond on the CSpect plugin's own thread. Step Out over
   a long loop is where the remaining tick is felt (one `CONTINUE` round trip
   per instruction). REQ-dzrp-9 (needs-prototype, API accepted): the loop
   owner MAY shorten its cadence while `ServiceHint{remote_attached &&
   paused}` (headless poll ≤2 ms; Qt/SDL re-arm the tick timer at ~2 ms);
   whether that is switched on is decided by V-LAT (§7.1), measured **in the
   drained configuration** — the drain removes the inspection round trips,
   the cadence change would remove the tick.
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
   (a `CMD_WRITE_STATE` may still carry a large payload from a foreign client;
   ours is a token, §6); command seq must be 1..255 — 2.2.0 narrowed the
   client's range to 1..15 with response bits 4-7 "unused" (spec:212-262), and
   DeZog compares the echoed byte exactly (up `:493-496`), so the adapter
   **echoes the seq byte verbatim** whatever its value, never masks it, and
   accepts both ranges (3.7.4 still sends 1..255). Seq 0, length over cap, or a truncated stream after
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
  "unless given; PORT 0 binds an OS-chosen port and logs it.\n"
  "One client at a time." },
{ "--debug-listen-address", 1, Doc::Documented, OptId::DebugListenAddress,
  "ADDR",
  "Bind address for the debugger protocol ports (--dzrp-port,\n"
  "--zrcp-port, --gdb-port). Default 127.0.0.1. A non-loopback\n"
  "address exposes the debugger to the network: none of these\n"
  "protocols has any authentication." },
```

**The one port rule, shared by the three `--<proto>-port` rows** (review R-5;
adopted verbatim by design-zrcp and design-gdb 2026-09-26 night): absent =
off; `0` = bind an OS-chosen ephemeral port and log it at startup as
`dzrp: listening on 127.0.0.1:NNNNN` — the contract `EspListener::open(0)`
already has (`esp_socket.h:518-527`); any other value = that port. There is
no "0 = off" (it would duplicate "absent"). The `--debug-listen-address` row
above is the single shared row, text identical in the three files; whichever
protocol lands first adds it, the others reuse it.

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

DZRP 8K bank N = MMU page N as NR 0x50-0x57 spell it — for `WRITE_BANK`,
`READ/WRITE_BANK_MEM` (offset within the 8 KB page; the spec's `offset u16`
allows more, jnext serves only what lies inside the page) and long addresses.
Bank **0xFF** in `READ_BANK_MEM` is DeZog 3.8's one 16 KB ROM bank (F1):
offset 0x0000-0x1FFF is the ROM half in slot 0, 0x2000-0x3FFF the half in
slot 1 (`zxnextmemorymodels.ts:64-104`). Each half is read through the
`MemSpace` the backend reports for that slot — CAP-INS-03 `SlotInfo.space` +
`space_offset`: for a ROM slot `Rom{effective_page >> 1}` and
`(effective_page & 1)·0x2000`, where `Rom{index}` is a **16 KB ROM image**
(0..3, addresses 0..0x3FFF, read-only) that on a ROM-in-SRAM machine is SRAM
pages 2·index / 2·index+1 — outside `Page{}`'s NR number space
(`mmu.cpp:396-402, 546-547`; `emulator.cpp:6829` `set_rom_in_sram(true)`) —
so the adapter composes no index of its own (R-1; REQ-dzrp-12 ACCEPTED, the
same rule REQ-qt-31 settled). The half is served only while that slot
`is_rom`; with RAM paged there the ROM select comes from `paging_ports()`
(7FFD bit 4, 1FFD bit 2 → `Rom{sel}` at the DZRP offset), else empty + warn
(N-1). Which ROM a bank-qualified DeZog view *means* is pinned by V-BANKMEM
(§7.1). `WRITE_BANK_MEM` to 0xFE/0xFF writes nothing (warn log;
`poke(Rom)` is `RefusedReadOnly`). jnext's *physical*
store index differs (`to_sram_page` adds 0x20 in Next mode, `mmu.h:1387-1390`;
MMU page 0x0E is a dedicated BRAM, `mmu.h:1392-1401`). The adapter never
computes that: `MemSpace::Page{N}` is indexed by MMU page and the backend
routes (REQ-dzrp-5). Banks 224..253 are refused (`WRITE_BANK` error string);
0xFE/0xFF are ROM and refused for writing.

### 5.3 Long addresses and the 0xFF overflow

`bank+1` is one byte (spec:264-270). DeZog 3.7.4's model has ROM as 0xFE
(slot 0) and 0xFF (slot 1); **3.8's has 0xFF in both** (F1). 0xFF+1 does not
fit: DeZog sends `(0x100) & 0xFF = 0` — a 64K breakpoint (buf:514) — and keys
its own map by `0x1000000 | addr`; the adapter can only ever report bank byte
0 or the slot's page+1, neither of which DeZog's lookup (remote:697-703; up
`:727-733`) matches. So **breakpoints in ROM cannot be matched by DeZog
through DZRP** — slot 1 with 3.7.4, both slots with 3.8. Not jnext's to fix;
recorded in the man page as a protocol limit. (Slot-0 ROM works with 3.7.4
only, via 0xFE+1 = 0xFF.)

### 5.4 Watchpoint banks

`CMD_ADD_WATCHPOINT` carries `bank+1`. Bank byte ≠ 0 → `Mem[lo, lo+size-1,
page = bank]` (REQ-dzrp-11, **accepted**: the `page` qualifier is AND-ed with
the logical range and tested only after the range matched, exactly as
`Execute.page`), so a bank-qualified watch fires only in that bank; bank byte
0 → the logical range alone. On a hit the adapter **reports the bank byte of
the accessed address's slot**; it never filters and never auto-continues (the
auto-continue withdrawn in §3.1 would produce the same Paused/Resumed flicker
for every other client; review N-4). That is DZRP's own model: "when a
watchpoint is hit the watch address is returned" and the client decides
(`breakpoints.md:132-133`); DeZog does exactly that filtering client-side
(remote:661-686 `getWatchpointsByAddress` compares the wp's bank with the
current slot) — and no released DeZog remote sends the command anyway (F3).
(Before REQ-dzrp-11 the backend's `Mem` filter was "logical range **or**
physical-page set", which could not express "this range in this bank"; the
qualifier closes that for foreign clients — DeZog needed nothing.) `remove`
matches the recorded (addr, bank, size, access) tuple.

---

## 6. State bookmarks (`CMD_READ_STATE` / `CMD_WRITE_STATE`)

**Mechanism: the backend's named in-memory bookmark map** (CAP-CAP-03
`bookmark_save(name)` / `bookmark_restore(name)` over CAP-ST-01/02;
`backend.md` v3 already says DZRP and ZRCP's `snapshot-save/-load` share it).
On the wire the payload is a **token**, not the state: `"JNXB"` + a name the
adapter allocates (`dzrp-<cid>-<n>`). The spec allows it — "arbitrary data,
the format is up to the remote" (spec:817) — and it is what the semantics
already were: the stream is in-process only, no versioning, session lifetime
(CAP-ST-02 note), so the bytes DeZog would have filed on disk were never
restorable across a jnext restart either. Sending the token instead saves a
multi-megabyte transfer plus a gzip in the Node client per `-state save`.
The map is **per client, bounded at 8 bookmarks** (each a full
`save_state` snapshot, `RewindBuffer::snapshot_bytes()`), and **a client's
bookmarks die with its detach** (`backend.md` CAP-CAP-03, decided on REQ-dzrp
adoption); a 9th `READ_STATE` gets `Result::RefusedUnavailable` → zero-length
reply + log line, never a silent eviction. Bookmarks **survive a hard
reconstruct** (they are backend state, not `Emulator` state), and a restore
into a machine whose type or snapshot width differs (a `--machine` change
across a cold boot) is refused `RefusedUnavailable` *before* `load_state` —
the same NTF 255 "no state to restore" path, no latch. Each bookmark is a
rewind-slot-sized snapshot: tens of MB per client at the bound of 8, which is
why the bound exists.
(Rejected alternative: raw `save_state_bytes` on the wire — same lifetime,
more bytes, and no cheap way to recognise a bad payload; see R-1 below.)

**Frame boundary rule (F9).** `READ_STATE` saves a bookmark **only when no
frame is in flight** (`at_frame_boundary()`, REQ-dzrp-10 — accepted:
`save_state_bytes(Mode::RefuseMidFrame)` → `Result::NotAtFrameBoundary`);
otherwise a **zero-length** reply (spec:819) plus a log line saying why and
when it works. Because every command runs between frames (§4.2), the machine
*is* at a boundary after `CMD_PAUSE` and after `CMD_INIT` — so a DeZog user
gets a bookmark after a manual pause, not after a breakpoint. Advancing
instead (the GUI's own rule) would leave DeZog's cached PC behind the machine
and turn its next step into a runaway (F9).

**What DeZog then does with a refusal (review R-1, verified):** nothing
visible. `stateSave` gzips the empty reply and writes it (remote:1749-1756);
a later `-state restore` ungzips it and sends a **0-byte `CMD_WRITE_STATE`**
(remote:1764-1770). The owner weighed that invisibility against the runaway
step an advance would cause and **decided for refusal** (2026-09-27, §10
Q2); the adapter-side payload validation below is what makes the resulting
empty `-state restore` harmless.

**`WRITE_STATE` — validate before touching the machine.** The adapter checks
the payload itself: empty, shorter than the header, wrong magic, or a name it
did not issue (or one the backend no longer holds) → reply seq only, then
`NTF_PAUSE` reason 255, address PC, string `"no state to restore"` (with
`"(save was refused mid-frame)"` appended when the adapter's last
`READ_STATE` for this client was the zero-length one). **`bookmark_restore`
is not called; nothing is latched; the session continues.** DeZog then
re-reads registers (remote:1772-1773) and shows the unchanged machine. Only a
valid token reaches `bookmark_restore`; a sentinel failure inside *that*
restore is the one path that latches corruption (CAP-CTL-11) — reported the
same way with the subsystem name, after which `CONTINUE` is refused per §3.2
until acknowledged from the GUI (DZRP cannot express an acknowledgment).
After a successful restore the machine is at a frame boundary, paused.

**Not offered**: nothing of CAP-ST-03/04 (rewind). DZRP has no verb; decision 5.

---

## 7. Validation plan

Three layers, each naming what it proves.

### 7.1 Against the real client — DeZog 3.7.4 (marketplace) and 3.8 (upstream `main`) in VS Code

Two client builds: the marketplace **3.7.4** (`~/.vscode/extensions/maziac.dezog-3.7.4`)
with `"remoteType": "cspect", "cspect": {"port": 11000}`, and **3.8** built
from upstream `main` in the Extension Development Host (the fork's proven
rig, `DEZOG-BREAKPOINTS-DESIGN.md:266-277`) with `"remoteType": "dzrp",
"dzrp": {"port": 11000}`; a demo built by `make -C demo` with its `.map`;
jnext started with `--dzrp-port 11000 --load demo/<x>.nex`. Rows say which
build they apply to; the protocol document records both versions' results
separately (a 3.8 result is provisional until 3.8.0 is released). Manual checklist, each item a row in the WP-6 test
protocol document, result recorded with the DeZog version:

| Row | Exercise | Proves |
|---|---|---|
| V-INIT | Connect (3.7.4 `cspect`; 3.8 `dzrp` and `cspect`); VS Code shows *stopped/entry*; Registers pane populated; MMU slots shown; 3.8: the DeZog log shows no "Unsupported command" line for anything jnext advertises | rows 1, 3, 24; F1, F4 |
| V-SUPP | 3.8: `-dbg cmd_exec_asm 0` and a `.p` load → named "not supported by the remote" errors, no timeout; 3.7.4: `-state save` refused client-side as before | row 24; bits 13/14/22 clear |
| V-BANKMEM | 3.8 with **`remoteType: "dzrp"`** (with `cspect`, `CSpectRemote.sendDzrpCmdReadBankMem` turns a bank-0xFF read into a plain `READ_MEM` whenever the ROM is paged in, up `cspectremote.ts:23-35`, so it never exercises row 25's ROM path — N-2): memory view `-md <addr> bank=<n>` on a bank not currently mapped shows that bank; `bank=255` at 0x0000 and at 0x2000 shows the two ROM halves; then `-md 0x3000 bank=<n>` for an 8 KB bank — DeZog passes the typed address as the offset unbounded (`memorycommands.ts:218`), so this hits the "serve what fits, else empty + warn" rule; record what the view displays (N-3). Repeat `bank=255` with RAM paged at 0x0000 (port 0xEFF7 bit 3) to exercise the paging-derived path | rows 25/26; N-1..N-3 |
| V-BRKINT | **Not executable against 3.8.0-rc7** (R-2: no code path sends 39; enabling the option prints "Break on interrupt: disabled."). Record that outcome as the row's result and rely on `dzrp-brkint-func` (§7.2) for the coverage; re-run when a DeZog release wires the sender | row 39 |
| V-WP | 3.8: WPMEM watchpoint on a variable, Continue → stops with the write address; 3.7.4: not reachable (F3) | rows 42/43 |
| V-BP | Set an editor breakpoint from the `.map`, Continue, hits; Continue again, hits again (GH #221 path); remove, does not hit | rows 40, 41, 6; §3.1 |
| V-BANK | Breakpoint in a banked page, hit only when that bank is mapped | §3.1 long addresses, REQ-dzrp-7 |
| V-STEP | Step Into through `CALL`, `RET`, `JR cc` taken and not taken, `RST 08`; Step Over a `CALL`; Step Out of a routine | §3.2, F8 (a user bp at the step target must not run away) |
| V-COND | Conditional breakpoint `A == 3` in a loop: stops exactly once | decision 2: adapter reports unconditional hits, DeZog filters |
| V-LOG | LOGPOINT: text printed, machine does not stay stopped | row 40 with a log-only bp |
| V-PAUSE | Continue, then Pause: stops, registers refresh | row 7 |
| V-MEM | Memory view, disassembly view, edit a byte, edit a register | rows 8, 9, 4 |
| V-NR/SPR | `-nextreg` / sprite viewer / sprite palette / pattern viewer | rows 11, 16-19 |
| V-LOAD | `launch.json` `load` of a `.sna` and of a `.nex`, on 3.7.4 (rows 5, 12) and on 3.8 (rows 26, 21 with port 0xFE) | rows 5, 10, 12, 21, 23, 26 (F4 sequence) |
| V-STATE | 3.8 (3.7.4 refuses client-side, F3): `-state save` after Pause, change a register, `-state restore` → machine back, registers refreshed. Then: breakpoint hit, `-state save` → DeZog shows **nothing** (an empty file is written — verify its size), `-state restore` → registers unchanged, jnext log shows `no state to restore`, and **Continue still works** (no corruption latch) | §6, R-1 |
| V-GUI | With the Qt debugger open: DZRP pause opens/refreshes the window; GUI Run then DeZog Continue | §4.4 |
| V-LAT | Step Out of a 1000-iteration `DJNZ` routine, wall-clock timed, in `build/gui-release` at 100 %, **with the drain enabled** (`drain_ms` 2, `budget_ms` 10); record per-step cost (expect ≈ 1 tick + ≤10 ms); repeat with the paused-cadence shortening prototyped (REQ-dzrp-9). **Read the result against DeZog's own throttle:** every DZRP continue-resolve handler calls `timeWait.waitAtInterval()` on a `TimeWait(1000, 200, 100)` (remote:889, :1113; `misc/timewait.ts:8-57`) — after the first second of a step-out loop DeZog sleeps 100 ms every 200 ms, i.e. it caps itself at ~50 % duty. Measure the first second separately from the rest; a flat result past 1 s says nothing about the tick cadence. | REQ-dzrp-9 decision; R-4 arithmetic; r2 N-5 |
| V-CLOSE | Stop the session: machine resumes; reconnect works without restarting jnext | row 2, §4.1 |

### 7.2 Against jnext's own client — `tools/cspect_dzrp/cspect_dzrp.py` as a harness

It already speaks the wire to CSpect (independent client implementation; wire
audit in `REVIEW.md`) and covers the commands DeZog cannot reach (F3). New
regression rows (functional, `test/00regression/functional_tests.conf`,
`expect:` bumped deliberately), each starting jnext headless with
`--dzrp-port 0` (ephemeral, bound port logged — the one port rule of §4.3,
`esp_socket.h:518-527`) and waiting for the listener's log line with the
ready-file idiom `test/00regression/scripts/esp-server-func.sh` already uses:

| Row | Script drives | Proves |
|---|---|---|
| dzrp-loopback-func | `CMD_LOOPBACK` 1..8192 bytes, both length conventions, seq wrap 255→1 | §4.2.5 framing |
| dzrp-init-regs-func | `INIT` as a 2.0.0 client and as a 2.2.0 client → reply version 2.2.0, machine 4; `GET_SUPPORTED_COMMANDS` → 7 bytes with exactly the row-24 set (bits 5, 12, 13, 14, 22 clear); `GET_REGISTERS` 37 bytes, slots `FF FF 0A 0B 04 05 00 01` on a 48K-style map; seq 1..255 and 1..15 both echoed verbatim | rows 1, 3, 24; F1 |
| dzrp-bp-continue-func | add bp, continue, `NTF_PAUSE` reason 2 with bank byte; continue with temp bp at the same address → reason 0 | §3.1, F7, F8 |
| dzrp-watch-func | range watch write on a 256-byte area, program writes the last byte and one past → one NTF reason 4 with the right address | rows 42, 43, REQ-dzrp-1, edge coverage both sides |
| dzrp-state-func | pause → `READ_STATE` returns a `JNXB` token; poke a register; `WRITE_STATE(token)` → registers equal the saved ones; continue, bp hit → `READ_STATE` empty; `WRITE_STATE` with 0 bytes, with garbage, and with an unissued token → seq-only reply + NTF 255 "no state to restore", registers unchanged, `CONTINUE` still runs (no latch) | §6, R-1 |
| dzrp-bank-func | `WRITE_BANK 14` (legacy) and `WRITE_BANK_MEM 14, offset 0x100` then `SET_SLOT 6,14` and `READ_MEM 0xC000` → the bytes (bank-7 BRAM routing); `READ_BANK_MEM 14, 0x1FF0, 0x20` → 16 bytes (page edge); `WRITE_BANK 250` → error string; `READ_BANK_MEM 255, 0, 16` → the mapped ROM's first bytes | rows 5, 25, 26; REQ-dzrp-5 |
| dzrp-brkint-func | `ENABLE_BREAK_ON_INTERRUPT 1`, continue → NTF 255 "Break on interrupt." at the IM1/IM2 handler entry within one frame; `0` → no NTF for 5 frames | row 39 |
| dzrp-unsupported-func | `CMD_SET_BREAKPOINTS`, `CMD_EXEC_ASM`, id 99 → seq-only replies, warn lines in the log, connection alive; `CMD_SET_BORDER` and `CMD_WRITE_BANK` from a 2.2.0 client → still served (log at debug) | "reported, never silent"; legacy rows |
| dzrp-close-resume-func | pause, `CLOSE` → frame counter advances again; reconnect | row 2 |

(Nine rows; `expect:` in `functional_tests.conf` goes up by nine.)

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
  → dzrp-state row must fail; pass an empty `WRITE_STATE` through to
  `bookmark_restore` → the no-latch row must fail; skip the post-frame flush →
  a row that counts ticks between stop and NTF must fail; disable the drain →
  a row that counts pumps per 5-command inspection burst must fail.

---

## 8. REQ ledger (to `design-backend`), with dispositions

Sent 2026-09-26 evening as `REQ-dzrp-<n>: <capability> — <why> — <command>`.
Status is updated in place when the backend answers.

| REQ | Capability asked | Why (cited) | DZRP command | Status |
|---|---|---|---|---|
| 1 | `Paused` / fired `Event` carries the accessed address and read-vs-write kind for watch stops | NTF reason 3/4 + address (spec:843-844); v1 `Paused` has only `pc` | 42, NTF | **ACCEPTED** — `Paused{by, reason, pc, cycle}`, `reason = Watch{event_id, access ∈ {Read, Write}, addr}` (also `Breakpoint{id}`, `Step`, `RunTo{id}`, `User{cid}`, `Magic`, `Corrupt`, `Script{id, text}`) |
| 2 | `events_fired_since(seq)` lists every matching subscription at a boundary, not only the winning verdict | temp-first rule (`breakpoints.md:124-129`, F8) | 6, NTF | **ACCEPTED** — `Paused.matched: vector<EventId>` (transients included); temp-beats-user is adapter policy over that list |
| 3 | adapter-creatable transient subscriptions: two Execute-once per resume, master-switch-exempt, auto-removed at the next stop, hidden from the GUI list | DeZog sends up to two (buf:484-490); `BreakpointSet` has one one-shot (`breakpoints.h:194-197`) | 6 | **ACCEPTED** — `Transient` flag on any subscription, any number, auto-removed at the next stop, hidden unless `include_transient`; the GUI's step-over/run-to one-shot becomes the same mechanism |
| 4 | raw sprite data behind CAP-INS-08: 5 attribute bytes ×128, 16 KB pattern RAM, sprite palette RGB333 per bank | `sprites.h:477-478`, `palette.h:482` are private; `SpriteInfo` is lossy | 16, 18, 19 | **ACCEPTED** — `sprite_attr_raw(i)`, `pattern_ram()`, `sprite_palette_rgb333(bank, idx)`; accessor additions on `SpriteEngine`/`PaletteManager` are an implementation item |
| 5 | `MemSpace::Page` indexed by MMU page 0..223, backend routes (+0x20, bank-7 BRAM), ROM pages refused with a reason | `mmu.h:1387-1401` | 5 | **ACCEPTED** — `Page` = NR 0x50-0x57 page space; 0xFE/0xFF → `Result::InvalidPage` + reason string; `Rom{n}` stays a separate space |
| 6 | `set_border(c)` as a debugger write to `Ula::set_border` | `ula.h:180`; port 0xFE would drive EAR/MIC; sent on every load (remote:1621,1718) | 12 | **ACCEPTED** — new **CAP-INS-18** `set_border(colour)`; no port traffic, no event |
| 7 | Execute filter with an optional physical-page qualifier | long-address breakpoints (spec:736-753); ALTERNATIVE: adapter auto-continue (§3.1) | 40 | **ACCEPTED (primary form)** — `Execute{lo, hi, page?}` fires when PC ∈ [lo,hi] AND effective page at slot(PC) == page; evaluated only after the address matched. The adapter fallback in §3.1 is not needed |
| 8 | notification flush in the same tick as the stop (post-frame `pump`/`flush`) | a DeZog step is 4-6 sequential round trips (§4.2.3); without same-tick notification each adds a tick | NTF | **ACCEPTED** — `pump(PumpBudget)` is called AFTER the tick's frame batch (the `post_frames` slot, `qt_app.cpp:667`) and **drains while paused** (REQ-zrcp-01, adopted here); a stop in this tick's frames is notified in this tick; inspection commands are answered inside the drain |
| 9 | (needs-prototype) `pump` reports "remote attached and paused" so the loop owner can tick faster while paused | with the drain a step is ≈1 tick + ≤10 ms; the cadence change would remove the tick; decide on V-LAT measured in the drained configuration | 6 | **NEEDS-PROTOTYPE** (API accepted) — `pump()` returns `ServiceHint{remote_attached, paused}`; the loop owner MAY shorten its cadence while paused with a remote attached (headless poll ≤2 ms; Qt/SDL re-arm the tick timer at ~2 ms). Decided on V-LAT (§7.1) against real DeZog step-out loops, drain on |
| 10 | `at_frame_boundary()` query + `save_state_bytes(RefuseMidFrame)` | F9: DeZog does not refresh registers after READ_STATE (remote:1749-1756) | 50 | **ACCEPTED** — `at_frame_boundary()` (= `!frame_in_progress()`, `emulator.h:191`); `save_state_bytes(Mode::AdvanceToBoundary \| Mode::RefuseMidFrame)` → `Result::NotAtFrameBoundary`; DZRP uses `RefuseMidFrame` and answers zero-length. v1.2: the bytes stay in the backend's CAP-CAP-03 map; the wire carries a token (§6) |
| 11 | `Mem` filter with an optional physical-page qualifier AND-ed with the logical range (as Execute's `page`), so a bank-qualified watch fires only for the named bank | DZRP `CMD_ADD_WATCHPOINT` carries `bank+1` (spec:771-777); v3's `Mem` filter was range OR page-set (`backend.md` §4.3) | 42 | **ACCEPTED** (filed after review N-4) — `Mem{lo, hi, page?}`, same shape and cost placement as `Execute.page`; no auto-continue needed |
| 12 | `Rom{index}` defined on a `rom_in_sram_` machine (Next), read-only, plus a `SlotInfo` field that tells a client which space (and offset) reads a ROM slot, so no client derives the index itself (the same gap REQ-qt-31 closes) | round-4 review R-1: v6 text defined `Rom` "for machines whose ROM is not in SRAM", while on `--machine next` the ROM IS in SRAM (`emulator.cpp:6829`) and `Page{}` cannot reach SRAM pages 0-7 (`mmu.h:1387-1390`) — 3.8's `READ_BANK_MEM 0xFF` had no space to land in | 25 | **ACCEPTED** (verified `emulator.cpp:6829`, `mmu.cpp:396-402, 546-547`) — `Rom{index}` = a 16 KB ROM image 0..3 (addresses 0..0x3FFF) uniformly: SRAM pages 2·index/2·index+1 on a ROM-in-SRAM machine, the `Rom` object's image on 48K/128K/+3; `SlotInfo` gains `space` + `space_offset` (RAM → `Page{nr_page}`, 0; ROM → `Rom{effective_page >> 1}`, `(effective_page & 1)·0x2000`); matrix DZRP `Rom = S` |

Not REQs, recorded: `run()` refusal → NTF 255 (§3.2); `pause()` from `pump` is
at a frame boundary by construction (§4.2); GUI resume cannot be told to DeZog
(§4.4); machine type always ZXNEXT (§5.1); watchpoint bank filtered
adapter-side (§5.4).

**MAPPED (v1.4, DZRP 2.2.0): 30 commands served (26 T1 + 4 T2; 28 CAP-mapped,
`LOOPBACK` and `GET_SUPPORTED_COMMANDS` are adapter-only; rows 5/12 legacy),
5 declined (condition string; reverse — no wire verb; step verbs CTL-03..08;
IO watchpoints; `NTF_LOG`), 3 unsupported-reported (13, 14, 22), 0 REQs open
(11 accepted incl. **REQ-dzrp-12**, filed after round 4 to pin `Rom{index}`
on a ROM-in-SRAM machine; 1 needs-prototype — rows 25/26 ride CAP-INS-02
`Page`/`Rom` via CAP-INS-03 `SlotInfo.space`/`space_offset`, row 39 rides the
`IntAck` kind), 0 reach-arounds.**
Capability-matrix corrections sent to design-backend (review N-7): the DZRP
cells for CAP-CTL-12 (reset — no DZRP command) and CAP-INS-19 (`machine()` —
§5.1 always answers ZXNEXT and never reads it) are **not used**; the DZRP cell
for CAP-CAP-03 is **used** (§6 adopts the named map).

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
- **IO watchpoints** (`WatchType::IO_READ/IO_WRITE`, `breakpoints.h:9`) — no
  DZRP form.
- **A "resumed" notification** — none; §4.4.
- **Refusing a `CONTINUE`** — reply is seq-only; §3.2's NTF-255 idiom.
- **Bank 0xFF in a long address** — byte overflow, §5.3.
- **`CMD_EXEC_ASM`** — deferred, §2 row 22.

---

## 10. Owner decisions — nothing open

**Answered by the owner (2026-09-27):** headless stop policy = the exception
(a remote attached ⇒ headless Stop pauses + notifies) — §4.1's OWNER-PENDING
is now DECIDED and the nine functional rows stand; a remote pause **does**
open the Qt debugger window — Q1 below answered *yes*, no Qt change;
subscriptions and a pause die with the client — §4.1's detach rule stands;
ports are explicit, off unless given — Q3 below answered as designed. DZRP
version = 2.2.0 (F1). **Q2 and Q4 answered 2026-09-27 (below). Nothing is
open.** Q1-Q4 are kept as the record of what was asked and how it was
decided.

1. **Q1 — ANSWERED (owner, 2026-09-27: yes).** A remote (DZRP) pause opens
   the local Qt debugger window through the unchanged GH #219 path
   (`debugger_manager.cpp:682-693`). No Qt change.
2. **Q2 — ANSWERED (owner, 2026-09-27: REFUSE).** A mid-frame
   `CMD_READ_STATE` gets the zero-length reply (spec:819) — the design as
   written in §6. The alternative, advancing to the frame boundary as the GUI
   snapshot rule does, was rejected for DZRP because DeZog does not re-read
   registers after `READ_STATE` (F9) and its next step would run from a PC
   the machine had left. The refusal is invisible in DeZog (R-1: it writes an
   empty state file); the adapter-side validation of `CMD_WRITE_STATE` (§6)
   makes the later empty `-state restore` harmless — no backend call, no
   corruption latch, session continues. `NTF_LOG` stays off (§2).
3. **Q3 — ANSWERED (owner, 2026-09-27: ports explicit, off unless given).**
   No default for `--dzrp-port`; DeZog's own defaults (11000 `cspect`, 14000
   `dzrp`) are documented, not assumed.
4. **Q4 — ANSWERED (owner, 2026-09-27: inside #12).** The `cspect_dzrp.py`
   fixes (REVIEW.md H1/H2/H3 — the stale `PAUSE`-notification race and the
   two lock gaps) land **inside #12's validation package (WP-6)**, not as a
   separate tools change; WP-6 already reads that way.

---

## 11. Work packages for #12 (parallelisable; each on its own branch + worktree)

| WP | Scope | Depends on | Files | Review focus |
|---|---|---|---|---|
| WP-1 framing + session skeleton | **Consumes the shared transport package T** (arch doc §10: the listener over `esp::make_socket_listener` and the in-memory byte-pipe fake `Transport`, written once for DZRP/ZRCP/GDB) — writes **no** listener and no fake. Delivers `dzrp_frame.{h,cpp}` (parser/encoder over T's `Transport`, both length conventions, 16 MiB cap, seq rules, chunk timeout); `CMD_LOOPBACK`; `dzrp_adapter_test` framing rows on T's fake | package T, backend `pump` (CAP-SES-03) | new `src/remote/dzrp/`, `test/remote/`, `test/unit-tests.conf`, `test/CMakeLists.txt` | framing mutation rows; never blocks; Windows twin builds (`make package-win`) |
| WP-2 adapter core + session | `DzrpServer`: attach/detach, `INIT` (answers 2.2.0, records the client's version) / `CLOSE`, `GET_SUPPORTED_COMMANDS` (the row-24 bitfield, one table shared with the dispatcher so a served command cannot be unadvertised), `GET/SET_REGISTERS` (verify `Z80_REG` enum = spec table), `READ/WRITE_MEM`, `READ/WRITE_BANK_MEM` (page-bounded; ROM via `Rom{}`), `SET_SLOT`, `GET_TBBLUE_REG`, legacy `SET_BORDER`/`WRITE_BANK`, ports, `INTERRUPT_ON_OFF`, unknown-command path | backend CAP-INS-01..05, SES-01/02, REQ-6 | `src/remote/dzrp/dzrp_server.{h,cpp}` | side-effect-free reads (a +3-mode `READ_MEM` row that `p3_floating_bus_dat_` is unchanged); refusal paths |
| WP-3 breakpoints, continue, notify | rows 6, 7 (NTF only when it stopped something), 39 (`IntAck`), 40, 41, `NTF_PAUSE`, temp-first rule, bank byte, exactly-once | REQ-1/2/3/7/8 | same | F7/F8 rows; step-off (GH #221) through `run()`; post-frame flush timing row |
| WP-4 tier 2 | watchpoints (42/43, reported with bank, no filtering), state (50/51 over the CAP-CAP-03 map with the `JNXB` token; payload validated before any backend call), `WRITE_BANK` (5), sprites (16-19) | REQ-1/4/5/10/11, backend range watches (#279 work) | same | edge rows both sides of a range; `READ_STATE` refusal; empty/garbage/unissued `WRITE_STATE` → no latch; bank-7 BRAM routing |
| WP-5 loop owners + CLI | `pump` calls in `frame_sequencer`, SDL loop, headless loop (paused branch: no `run_frame`, no frame-countdown decrement, `pump(wait)`); `--dzrp-port`, `--debug-listen-address` rows + `main.cpp` cases + `EmulatorConfig`; man page OPTIONS + a DZRP section in `doc/man/jnext.1.md` (`make docs-man`), user guide chapter 6 subsection (`make docs-userguide`) | WP-1/2 | `src/platform/*`, `src/gui/qt_app.cpp`, `src/core/cli_options.h`, `src/main.cpp`, `doc/man/jnext.1.md` | `make cli-check`, `docs-check`; headless paused loop does not spin (CPU time row) |
| WP-6 validation | `cspect_dzrp.py` extensions + REVIEW.md H1 fix; the nine functional rows (§7.2) + `expect:` bump; the DeZog manual protocol (§7.1) executed and recorded in `doc/testing/DZRP-VALIDATION.md` with the DeZog version and V-LAT numbers | WP-1..5 | `tools/cspect_dzrp/`, `test/00regression/` | every functional row uses `timeout --foreground --kill-after=5s`; no `trap` in row scripts |
| WP-7 docs | developer guide page for `src/remote/dzrp` (what is served, the two remote types, the ROM-bank limit, the loop model); `FEATURES.md` line; ChangeLog *Unreleased* line at merge time | WP-5 | `src/doc/developer-guide/`, `FEATURES.md` | `docs-devguide-check` |

Order: package T (its own branch, shared) → WP-1 → WP-2 → {WP-3, WP-4, WP-5
in parallel} → WP-6 → WP-7. Each WP
gets an independent reviewer per CLAUDE.md; the full triplet plus
`make unit-test-sdl` before every merge.

---

## 12. Implementation record — milestone 1 (WP-1 framing, WP-2 session / registers / memory)

Code: `src/remote/dzrp/dzrp_frame.{h,cpp}` (WP-1) and
`src/remote/dzrp/dzrp_server.{h,cpp}` (`DzrpServer`, WP-1 + WP-2), inside
target `jnext_remote`. Suite: `test/remote/dzrp_adapter_test.cpp`
(`gate: none`, both configurations). The adapter is a `remote::Protocol`
that owns its `remote::Server`; the loop owner (WP-5) opens it and
`add_service()`s it — nothing in this milestone touches a loop owner, the CLI,
`src/gui/` or `src/platform/`.

### 12.1 Served in milestone 1

`CMD_INIT` (1), `CMD_CLOSE` (2), `CMD_GET_REGISTERS` (3), `CMD_SET_REGISTER`
(4), `CMD_WRITE_BANK` (5, legacy), `CMD_READ_MEM` (8), `CMD_WRITE_MEM` (9),
`CMD_SET_SLOT` (10), `CMD_GET_TBBLUE_REG` (11), `CMD_SET_BORDER` (12,
legacy), `CMD_LOOPBACK` (15), `CMD_READ_PORT` (20), `CMD_WRITE_PORT` (21),
`CMD_INTERRUPT_ON_OFF` (23), `CMD_GET_SUPPORTED_COMMANDS` (24),
`CMD_READ_BANK_MEM` (25), `CMD_WRITE_BANK_MEM` (26). Everything else takes the
unknown-command path until its WP lands. **The bitfield is therefore
`1E 8F B0 07` in this milestone**; it grows to §2 row 24's `DE 8F BF 07 80 0F
0C` as WP-3 (6, 7, 39, 40, 41) and WP-4 (16-19, 42, 43, 50, 51) add their rows
to the one table — it is computed from that table, never written down
(`DZRP-SUP-01` pins today's value; `DZRP-SUP-02` sends every id 0..255 and
checks "unsupported" is said exactly for the clear bits, legacy 5/12 excepted).

### 12.2 Deviations and additions, each with its reason

1. **`CMD_READ_BANK_MEM` bank 0xFF with RAM paged into a ROM slot: no
   paging-derived fallback — empty reply + warn** (§2 row 25 / §5.2 / N-1 say
   derive `sel` from `paging_ports()` as "7FFD b4 | 1FFD b2"). The ROM select
   is machine-specific — `Mmu::current_sram_rom()`: 48K → 0; +3 → two bits
   *with the NR 0x8C alt-ROM locks*; 128K and the **Next → one bit (7FFD
   b4) with the locks** — so the formula names the wrong image on the Next
   whenever 1FFD b2 is set, and under any NR 0x8C lock on every machine. The
   backend publishes no "ROM select" query, and composing one in the adapter
   is exactly what REQ-qt-31 / REQ-dzrp-12 forbid. Serving the wrong ROM
   silently is worse than an honest empty reply. A half is served only while
   its slot `is_rom` (`DZRP-BANK-09`). Proposal for the owner in §12.4.
2. **Commands before `CMD_INIT`** (the design is silent): machine commands are
   refused — seq-only reply + warn `<CMD> before CMD_INIT — refused` — because
   every mutation must be attributed (`ClientId by`) and the client is
   attached by `CMD_INIT`. The machine-free ones (`INIT`, `CLOSE`,
   `LOOPBACK`, `GET_SUPPORTED_COMMANDS`) are served. A `session` column in
   the command table carries it (`DZRP-SES-11`).
3. **A payload shorter than the command's fixed length** (silent in the
   design): seq-only reply + warn `malformed <CMD>`, nothing executed
   (`DZRP-MAL-01`); the three commands whose reply has an error field report
   error 1 in it instead — `CMD_INIT` (full reply shape, nothing attached,
   `DZRP-SES-10`), `CMD_WRITE_BANK` ("length must be 8192", also for an
   over-long payload) and `CMD_SET_SLOT`. Bytes past a command's fields are
   consumed and ignored (the frame is length-delimited).
4. **`CMD_INIT` never calls `pause()` on a machine that is already paused**
   (a precision of §4.1's "stays paused"): the backend's `pause()`
   re-attributes a standing pause to the caller ("last verb wins"), and DZRP's
   later detach would then release a pause that was never its own — a GUI
   pause, or the unowned `Magic` stop no detach may clear (`DZRP-SES-08`). A
   repeated `CMD_INIT` (conformance C6 sends five) renegotiates on the client
   it has; it never attaches twice (`DZRP-SES-04`).
5. **`CMD_CLOSE` keeps the connection** (§2 row 2 says "detach", and DZRP is
   silent on the transport): the session returns to its pre-`INIT` state and a
   new `CMD_INIT` on the same connection is served — DeZog's own stress list
   sends `CLOSE` then `INIT` (conformance C15; `DZRP-SES-05`).
6. **`CMD_LOOPBACK` over 8192 bytes** (the spec's maximum; the design gives no
   over-limit behaviour): declined in-band with a seq-only reply + warn, and
   the connection serves on (conformance C18; `DZRP-FR-04`).
7. **Refusals of commands that have no error field** (`READ_PORT`,
   `WRITE_PORT`, `SET_REGISTER`, `WRITE_MEM`, `WRITE_BANK_MEM`, `SET_BORDER`,
   `INTERRUPT_ON_OFF` — today only the backend's RZX wall and the page bound
   refuse): the ordinary reply, *short* for `READ_PORT` (never a made-up
   value), plus a warn line naming the backend `Result`. `CMD_WRITE_BANK`'s
   error string for a backend refusal is `result_name()` (e.g.
   `refused_rzx`).
8. **The chunk timeout runs from the last byte that advanced the frame**, as
   DeZog's own "timeout between data chunks" does — a slow link that keeps
   delivering is never cut off (`DZRP-FR-14`); 5 s of silence mid-frame is a
   protocol error (`DZRP-FR-13`).
9. **`CMD_SET_REGISTER` number 12**: the spec says "unused"; DeZog's `Z80_REG`
   enum (verified identical to the spec table everywhere else, 3.7.4
   `z80registers.ts:14-25` and upstream 3.8.0-rc7) names it `IR` (I<<8 | R).
   DeZog's register panes never offer IR for editing, so only the raw debug
   console can send it; it takes §2 row 4's unknown-index path (warn,
   nothing written; `DZRP-REG-04`).
10. **`CMD_WRITE_MEM`'s "dropped byte is logged at debug level"** is a
    read-back: the bytes that do not read back as written are counted and
    logged at debug ("ROM, or a write-only overlay"), because `peek`/`poke`
    report no per-byte fate (`DZRP-MEM-09`).
11. **Nothing was copied from `dezogif_ng`** (§ "Reuse, not rewrite"): its
    `test/dzrp/` clients drive a live remote over TCP, which this milestone's
    in-memory fake cannot host and which needs WP-5's `--dzrp-port`. What was
    reused is the TEST DESIGN — C2 (length convention by violation), C4/C5
    (loopback and its sizes), C6 (seq echo), C9 (framed on the length), C15
    (`CLOSE` then `INIT` on one connection), C18 (oversize loopback declined,
    remote serves on) — restated as `DZRP-FR-*` / `DZRP-SES-*` rows, with
    provenance (`/home/jorgegv/src/spectrum/dezogif_ng/test/dzrp/conformance.py`
    @ `709ae7d77d444e0e14d88d5c6114b2a7c18e2be6`) in the suite's header. The
    copy, with the version made a parameter, belongs to WP-6.

### 12.3 Backend defects found and fixed on this branch (each with its row)

Implementation files only — no frozen header changed.

- **B-1 `set_mmu_slot(slot 0/1, 0xFF)` UNMAPPED the slot** (reads 0xFF,
  writes dropped): it called `Mmu::set_page()`, where the guest's `NEXTREG
  0x50,0xFF` re-engages legacy ROM paging (the NR 0x50-0x57 write handler,
  `zxnext.vhd:4611-4612, :3052`). DeZog sends `SET_SLOT` 0/1 = 0xFF on every
  `.sna/.z80/.nex` load, so a DeZog load would have left the ROM unmapped.
  Now `set_mmu_slot` runs the NR 0x50+slot write handler — exactly §2 row
  10's "through the NR 0x50+slot write path". `DZRP-SLOT-02/03`.
- **B-2 `port_out`'s `MUTATE` line printed the value in decimal after `0x`**
  (`std::to_string`: 0x15 logged as "0x21"). `DZRP-PORT-05`.
- **B-3 `set_border` lacked §4.2a's RZX wall** — the one mutation verb without
  it. `DZRP-BRD-02`.

### 12.4 Design findings

- **T fits DZRP with no reach-around.** Everything goes through
  `Connection::read/write/close` and the four `Protocol` callbacks; no socket
  and no `Emulator` is touched. `on_service` executes at most one command
  (the parser holds one frame and reads only `wanted()` bytes, so the rest of
  a pipelined burst stays in T's buffer, where `max_input` backpressures the
  peer — `DZRP-FR-15`), returns `Serviced` for it so the SES-03 drain answers
  a queued chain in one pump while paused (`DZRP-FR-20`), and T's "every
  pass, new bytes or not" is what the chunk timeout needs. One consequence,
  not a defect: a frame larger than `max_input` (1 MiB) arrives at most 1 MiB
  per pass, and a pass that completes no command ends the paused drain, so a
  16 MiB foreign `CMD_WRITE_STATE` takes ≥16 ticks. DeZog's is a token (§6);
  WP-4/WP-6 may raise `max_input` for DZRP if that is ever measured to
  matter.
- **Backend gap (frozen header → owner): no query for the ROM a RAM-paged ROM
  slot would serve.** It is what bank 0xFF needs when RAM is paged at 0x0000
  (12.2 item 1). Proposal: one INS-03 read, e.g. `uint8_t rom_select()
  const` (the `Rom{}` index legacy paging selects, `Mmu::current_sram_rom()`
  with the NR 0x8C locks folded in), or equivalently a `SlotInfo` field for
  slots 0/1. Not needed by any released DeZog (its model only asks for bank
  0xFF while GET_REGISTERS says ROM is paged); needed for full row-25 fidelity.
- `ClientInfo` cannot be changed after `attach()`, so a repeated `CMD_INIT`
  under a different program name keeps the first name in the backend's
  client list (the log line shows the new one). Cosmetic; no change proposed.
