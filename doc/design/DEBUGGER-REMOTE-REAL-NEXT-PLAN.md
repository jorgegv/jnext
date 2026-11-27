# Remote debugging of another jnext and of a real Next: execution plan (epic #486)

Status: **DRAFT for owner review** (2026-11-17; round 2, 2026-11-18: the independent review's
findings and the owner's inputs of 2026-11-17 folded in; round 3, 2026-11-18: the owner's answers
to Q1, Q4-Q8 and Q10 folded in, §13, and the tasks grouped into 18 sub-issues). Nothing in this
plan is started. When the owner approves it, the sub-issues of the task list are opened under
#486, on the epic branch `epic486-remote-target`; each sub-issue branches off it and merges back
into it, and the epic reaches `main` in one merge (CLAUDE.md, merge protocol step 1).

Scope is the owner's, from #486's UPDATE blocks, and is not re-opened here:

- **2026-11-10 decision.** In scope: (1) a target interface in jnext's debugger, saying what the
  connected target can do so that panels it cannot serve are greyed out, plus a DZRP client,
  tested against a local jnext over loopback; (2) the same client against a real Next running
  dezogif_ng, as the proof of concept, with a custom shim decided only after it. Abandoned: DZRP
  extensions, an own protocol (JRDB), and the debugger as a client of other emulators and
  debuggers (ZEsarUX over ZRCP, GDB targets).
- **2026-11-11.** Source-level debugging on a remote target over plain DZRP: line stepping by
  temporary breakpoints, the call stack rebuilt by walking the stack, C variables from registers
  and memory, a build-match check at connect time; ROM breakpoints, watchpoints, coverage,
  rewind and anything cycle-exact greyed out on hardware.
- The replayable bug-report bundle is #492, not this epic.

---

## Task list

One heading per sub-issue (owner, 2026-11-18: "group related tasks in the same subissue if
possible, considering parallelization"). The task lines under a heading are that sub-issue's
checklist, one line per task, with stable IDs; the detail is in §4 to §10, and the
"Sub-issues and parallelization" section below says what can run at once. Tightly coupled or
strictly sequential tasks on the same files share a sub-issue; tasks that can run on separate
branches in parallel are kept apart. The owner-run tasks (R0.5, R4.6) and the decision gate (R5)
are sub-issues of their own; R6 is one placeholder sub-issue, opened only if R5.2 says yes.

### Phase R0 and R1: foundations and the target interface

#### Sub-issue 1: DZRP client core (framing, session, TCP link)

- [x] **R0.2** DZRP client framing: command encoder and response/notification parser in `dzrp_frame.*`, cross-checked against the server's parser
- [x] **R0.3** DZRP client session core (`DzrpClient`): CMD_INIT/CLOSE, sequence numbers 1..15, pipelining window, timeouts, NTF_PAUSE/NTF_LOG, profiles, send gate, wait hook, session token
- [x] **R0.4** Client links: outbound TCP over `esp::make_socket_transport`, an in-memory link pair for the suites, and the `test/dzrp_client_probe` driver R0.5 runs

#### Sub-issue 2: early hardware checkpoint on the KS2 (board issue 4, the Kickstarter 2 board) (owner)

- [x] **R0.5** Owner-run on the KS2 (board issue 4) over dezogif_ng's WiFi ROM: connect while stopped, CMD_INIT, registers, memory, NextREGs, then one unsupported command to confirm the silent reset (Q10); confirms §2.4 before R1-R4 build on it (record: `doc/testing/REMOTE-TARGET-VALIDATION.md` §1; §2.4 confirmed; new: `CMD_INIT` rewrites the slots, and a session starts with no debuggee, D24)

#### Sub-issue 3: the target seam and the capability model

- [x] **R0.1** Target-seam inventory: classify every `Debugger` verb (machine / host-side / local-only); write the owner-approved `debug/target.h` and facade additions (Q1)
- [ ] **R1.1** `Target` interface and `LocalTarget`: the machine half of `Debugger::Impl` behind it, behaviour-neutral, bench A/B
- [x] **R1.2** Capability model: `Debugger::capabilities()`, a reason per missing capability, register and run-state validity

#### Sub-issue 4: Qt greying and window retargeting

- [ ] **R1.3** Qt greying: actions, panel placeholders and unknown registers driven by capabilities; fake-capability Qt suite
- [ ] **R1.4** Debugger window retargeting: one window, Local or Remote target (Q8), title and status bar, caches reset on a switch

### Phase R2: remote jnext over DZRP

#### Sub-issue 5: `DzrpTarget` for a remote jnext

- [ ] **R2.1** `DzrpTarget` inspection: registers, CPU memory, banks, slots, NextREGs, sprites; stop-time snapshot cache with pipelined refresh
- [ ] **R2.2** Run control and execute breakpoints (normal mode, CMD 40/41); conditions, handlers and hit counts evaluated by the client
- [ ] **R2.3** Watchpoints over CMD 42/43 as `Mem` subscriptions
- [ ] **R2.7** Loopback parity suite for the R2 capabilities (inspection, run control, execute breakpoints, watchpoints): the remote facade over an in-process `DzrpServer` against `LocalTarget`; sub-issues 6, 9, 10, 11 and 13 add their rows to the same suite

#### Sub-issue 6: instruction stepping by temporary breakpoints

- [ ] **R2.4** One successor function, oracle-tested against jnext's Z80 core, and Step Into / Over / Out / Run to Cursor on a remote target

#### Sub-issue 7: connect UI, `--remote-target` and Preferences

- [ ] **R2.5** Connect and disconnect in the GUI: Debug > Remote Target dialog, errors, lost link, reconnect
- [ ] **R2.6** CLI and Preferences: `--remote-target`, `remote.*` keys in `src/platform/config_keys.h` (`KeyId` + `KEYS[]`), man page, `make cli-check`

#### Sub-issue 8: remote jnext end to end: functional rows and docs

- [ ] **R2.8** Functional rows: `remote-jnext-func` (test probe over TCP) and `remote-qt-func` (GUI started with `--remote-target`)
- [ ] **R2.9** Docs: user guide page, developer guide chapter, FEATURES.md

### Phase R3: source-level debugging on a remote target

#### Sub-issue 9: remote program, sidecars and build match

- [ ] **R3.1** Program and sidecars for a remote target; fetch identity from the MMU slots
- [ ] **R3.2** Build-match check at connect and after a load: target code bytes against the program file

#### Sub-issue 10: source line stepping on a remote target

- [ ] **R3.3** Source line stepping (Into, Over, Out) by exit-point breakpoints

#### Sub-issue 11: stack walking and C variables on a remote target

- [ ] **R3.4** Call stack by stack walking (assembler return addresses, C frame pointers); Step Out by return address
- [ ] **R3.5** C variables on a remote target; bulk reads where the target advertises them
- [ ] **R3.6** Tests and docs for remote source-level debugging (needs sub-issues 8 and 10 too)

### Phase R4: remote Next with dezogif_ng (proof of concept)

#### Sub-issue 12: dezogif_ng profile and simple-mode breakpoints

- [ ] **R4.1** dezogif_ng profile: identity rule, static command set, never-unsupported and running-state send rules, IM and IFF unknown, reserved banks, bank views greyed (Q7)
- [ ] **R4.2** Simple-mode breakpoints (CMD 13/14): set before continue, restore at stop, continue off a breakpoint, forbidden addresses, edits while running

#### Sub-issue 13: program upload

- [ ] **R4.3** Program upload, part of the PoC (Q5): a NEX onto the target (CMD_WRITE_BANK or WRITE_BANK_MEM, SET_SLOT, SET_REGISTER)

#### Sub-issue 14: emulated dezogif_ng rig rows and real-Next docs

- [ ] **R4.5** Emulated-rig rows: the vendored dezogif_ng ROM fixtures (Q4) in jnext, WiFi over the emulated ESP-01
- [ ] **R4.7** Docs: "Debugging a real Next" user guide page, man page, developer guide

#### Sub-issue 15: hardware validation on the KS2 (owner)

- [ ] **R4.6** Hardware validation on the owner's KS2 (board issue 4) over WiFi, with a latency record (owner)

(Sub-issue 16, the serial link, R4.4, was dropped by the owner on 2026-11-27, D25; the sub-issue and task numbers are not reused.)

### Phase R5: proof-of-concept verdict (owner decision gate)

#### Sub-issue 17: PoC verdict

- [ ] **R5.1** PoC report: what works, what is missing, measured costs, the shim options
- [ ] **R5.2** Owner decision: a custom shim or not, and its scope. No shim code before it

### Phase R6: custom shim (only if R5.2 says yes)

#### Sub-issue 18: custom shim (placeholder)

- [ ] **R6.1** Import dezogif_ng's sources under jnext's GPL, MIT notices kept, licensing record
- [ ] **R6.2** Build the shim in the tree, reproducibly, versioned in lockstep with jnext
- [ ] **R6.3** The standard DZRP 2.2.0 commands the shim lacks (24, 25/26, 28, 40-43 where feasible)

---

## Sub-issues and parallelization

Each sub-issue branches off the epic branch `epic486-remote-target` and merges back into it.
"Depends on" means the branch cannot be finished (and usually not started) before those are
merged into the epic branch; a note says where part of the work can start earlier.

| Sub-issue | Tasks | Depends on | Can run in parallel with |
|---|---|---|---|
| 1 DZRP client core | R0.2, R0.3, R0.4 | none | none (but see the note on R0.1 below) |
| 2 Hardware checkpoint (owner) | R0.5 | 1 | none (but see the note on R0.1 below) |
| 3 Target seam and capabilities | R0.1, R1.1, R1.2 | 2 (R1.1 waits for R0.5; R0.1 may start at once) | none |
| 4 Qt greying and retargeting | R1.3, R1.4 | 3 | 5, 6, 9, 10, 12, 13 |
| 5 `DzrpTarget` for a remote jnext | R2.1, R2.2, R2.3, R2.7 | 1, 3 | 4 |
| 6 Stepping by temporary breakpoints | R2.4 | 3, 5 (the pure successor function and its oracle suite may start after 3) | 4, 7, 9 |
| 7 Connect UI, CLI, Preferences | R2.5, R2.6 | 4, 5 | 6, 9, 10, 12, 13 |
| 8 Remote jnext end to end | R2.8, R2.9 | 6, 7 | 9, 10, 12, 13 |
| 9 Remote program, sidecars, build match | R3.1, R3.2 | 5 | 4, 6, 7, 8, 12 |
| 10 Source line stepping | R3.3 | 6, 9 | 4, 7, 8, 12, 13, 14 |
| 11 Stack walking, C variables, source docs | R3.4, R3.5, R3.6 | 8, 9, 10 (R3.4 and R3.5 need only 9; R3.6 needs 8 and 10) | 12, 13, 14 |
| 12 dezogif_ng profile, simple-mode breakpoints | R4.1, R4.2 | 5, 6 | 4, 7, 8, 9, 10, 11 |
| 13 Program upload | R4.3 | 9, 12 | 4, 7, 8, 10, 11 |
| 14 Emulated rig rows, real-Next docs | R4.5, R4.7 | 8, 12, 13 | 10, 11 |
| 15 Hardware validation (owner) | R4.6 | 11, 14 | none |
| 17 PoC verdict (owner gate) | R5.1, R5.2 | 15 | none |
| 18 Custom shim (placeholder) | R6.1, R6.2, R6.3 | 17, and R5.2 saying yes | none |

The parallel column is symmetric and lists no sub-issue that is a direct or indirect
dependency or dependant of the row. Work inside a sub-issue that can start before its
dependencies land is noted in the "Depends on" column: R0.1 (sub-issue 3) needs nothing and can
run beside sub-issues 1 and 2; the successor function of sub-issue 6 needs only sub-issue 3;
R3.4 and R3.5 (sub-issue 11) need only sub-issue 9.

**Critical path:** 1 -> 2 -> 3 -> 5 -> 6 -> 12 -> 13 -> 14 -> 15 -> 17. Two side chains join it
and must not fall behind: 3 -> 4 -> 7 -> 8 (sub-issue 14 needs the probe rows of 8), and
5 -> 9 -> 10 -> 11, with 11 also after 8 (sub-issue 15 validates source stepping and C variables on the KS2). The two
owner-run steps (2 and 15) are on the path, so their timing is the owner's to plan; sub-issue
3's R0.1 inventory and sub-issue 6's successor function are the work that can be done ahead of
them. The host runs at most three builds or test runs at a time, so at most three branches are in active build and test together.

---

## 1. Sources and evidence

Every claim about code cites a path and a symbol in the tree named; line numbers are as of the
date above.

| Source | Where | State read |
|---|---|---|
| jnext | this repository | `main` @ `ad916bc6d` (v1.3.5) |
| dezogif_ng | `~/src/spectrum/dezogif_ng` | `eb58524`, build `0026`, MIT since `4f988b9` |
| maziac's dezogif (upstream of dezogif_ng) | the same checkout, remote `upstream` | fork point `dee4255` (`fbb3629` "Version 2.2.1"); `upstream/main` @ `71c7ad0` (2026-09-10, `v2.3.0-rc2`) |
| DeZog (source, not the bundle) | `~/src/spectrum/dezog.jorgegv` | fork branch `zxnext-socket-transport` @ `6b46a0c1`; `upstream/main` @ `0de07af6` (3.8.0-rc7) |
| DZRP specification | DeZog `design/DeZogProtocol.md` | `upstream/main` @ `0de07af6`, DZRP 2.2.0 |
| Existing designs | `doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md`, `doc/design/debug-subsystem/dzrp-frontend.md`, `doc/design/SOURCE-LEVEL-DEBUGGING.md` | as on `main` |

dezogif_ng was found and read; nothing below about it is from memory. Statements about it that
rest on its own documentation rather than its source are marked **(doc)**; statements that
nobody has measured are marked **(unmeasured)**.

---

## 2. What exists today

### 2.1 The debugger: one facade over one emulator

- `jnext::dbg::Debugger` (`src/debug/debugger.h`) is the one facade every frontend holds. Its
  header says it is FROZEN: "additions are by owner decision only" (banner, lines 12-14).
- All of its state is `Debugger::Impl` (`src/debug/debugger_impl.h`), which holds
  `Emulator& emu`. Each loop owner builds exactly one: `QtApp` (`src/gui/qt_app.cpp:322`),
  `SdlApp` (`src/platform/sdl_app.cpp:242`), `HeadlessApp` (`src/platform/headless_app.cpp:57`).
- The Qt panels hold `jnext::dbg::Debugger*` and never `Emulator*` (`src/debugger/*_panel.h`;
  `video_panel.*` and `debugger_window.*` name `Emulator` only in comments and in one menu
  label, "&Attach to Emulator Window", `debugger_window.cpp:983`). Across
  `src/debugger/` they call **99 distinct facade methods** (grep of `dbg_->`, `dbg.`,
  `backend().` call sites). The most used are host-side: `source_map` (28 sites),
  `source_page` (16), `symbols` (13); the machine reads are `state` (19), `mmu_slots` (11),
  `registers` (10), `memory_reader` (8), `nextreg_peek` (6).
- **The host-side half already reads the machine mostly through the facade.** In
  `src/debug/debugger_cvalues.cpp` there is no `emu.` use at all (C values are built on
  `peek()` and `registers()`); `debugger_cframes.cpp` has one (`emu.call_stack()`), and its
  banner already describes a no-tracker path: "Without tracker frames the entry bytes of frame
  0 and the saved frame pointers of the older frames supply it."
- **Where the host-side half reaches the emulator directly** (what R1.1 must move behind the
  seam): `Debugger::source_step()` (`src/debug/debugger_source.cpp`, the forward loop) calls
  `impl_->emu.debugger_step()` up to `SOURCE_STEP_LIMIT` times and takes call depth from
  `impl_->emu.call_stack()`; the fetch identity comes from `emu.predict_fetch()`; source
  history reads `emu.trace_log()`; time is `emu.clock()`.
- Panels refresh on `Paused` and on a ~4 Hz throttle while running; `DebuggerManager`
  (`src/debugger/debugger_manager.h`) pulls `state()` every tick
  (`DebuggerManager::check_breakpoint_hit()`) and opens the window on any pause it has not
  shown.
- Single-threaded: everything runs on the emulation thread, inside the loop owner's tick and
  `Debugger::pump()` (architecture §5). A remote server is a `dbg::Service` the pump drains.

### 2.2 The DZRP server, and why it is not a client

- `jnext::remote::dzrp::DzrpServer` (`src/remote/dzrp/dzrp_server.h`) answers **DZRP 2.2.0**,
  machine ZXNEXT, name `"jnext v<version>"` (`dzrp_server.cpp`, `cmd_init`). Its command table
  `DzrpServer::COMMANDS[]` (`dzrp_server.cpp:165-198`) serves 1-12, 15-21, 23-28, 39-43, 50-51;
  **not** 13/14 (`CMD_SET_BREAKPOINTS`, `CMD_RESTORE_MEM`, the hardware dialect) and not 22
  (`CMD_EXEC_ASM`). So a jnext target speaks DZRP's **normal mode** (breakpoints owned by the
  remote, CMD 40/41).
- The framing is half a pair: `dzrp::FrameParser` (`src/remote/dzrp/dzrp_frame.h`) parses
  **commands** and `encode_response()` builds **responses**. A client needs the other half.
  The header records the trap: the two length conventions differ (a command's length counts
  the payload only, a response's counts from the sequence byte).
- The transport is server-only: `remote::Server` (`src/remote/transport.h`) owns a listener and
  admits one client. Outbound connections exist elsewhere: `esp::EspTransport::begin_connect()`
  through `esp::make_socket_transport()` (`src/esp01/include/esp01/esp_socket.h:258`, `:467`),
  non-blocking, name lookup inside `poll()`, Windows-twinned, used by the ESP-01 emulation.
  Its `AddressPolicy` (`:155`) denies loopback by default, which a debugger client must not.
- No host serial-port code exists. The only termios use is the pty end of the joy-port cable
  (`src/peripheral/joy_uart_link.cpp`, POSIX-only).

### 2.3 Benches jnext already has for a Next-side stub

- `JoyUartLink` (`src/peripheral/joy_uart_link.h`, GH #252) is the PC end of a joy-port serial
  cable; `--joy-uart-pty` (`src/core/cli_options.h:446`) allocates a pty "the form a serial
  client such as DeZog opens".
- The ESP-01 emulation serves `AT+CIPSERVER` on a host socket, bound by `--esp-listen-address`
  (`cli_options.h:588`). dezogif_ng's own bench (`test/run-dzrp-stub.sh`) installs the WiFi ROM
  as the Multiface ROM on a card copy with `mcopy` (`:428`), runs headless jnext, raises the
  NMI with `--delayed-nmi-frames` (`:482`; `cli_options.h:789`) and talks DZRP to
  `127.0.0.1:11000`.
- `--sdcard-file-add FILE --sdcard-file-dest PATH` (`cli_options.h:300`) copies a file onto a
  card and exits (`main.cpp`, the `sdcard_file_add_count > 0` arm returns after the copy), so
  R4.5 can install the ROM with jnext's own mechanism instead of mtools.
- `test/dzrp/` holds dezogif_ng's Python DZRP client and conformance suite, copied at
  `709ae7d` (`doc/testing/DZRP-VALIDATION.md`).

### 2.4 dezogif_ng, read from its source

| Fact | Evidence |
|---|---|
| It is the Multiface ROM (`enNextMf.rom`), entered by the NMI button or, for a PC-initiated Pause, by a Copper-raised NMI | README; `doc/ZXNEXT-REMOTE-DEBUG-STUB.md` §2, §4.3 |
| Answers **DZRP 2.1.0**, machine 4, name **`"dezogif v2.2.1"`**: `PRG_VERSION` is upstream's version string, which the fork "has never moved" | `src/commands.asm:26-35`, `cmd_init.response`; `src/data_const.asm:11-19` |
| Serves commands **1 to 23** only through its jump table, plus 40 (answers id 0 = refused) and 41 (acknowledged) | `commands.asm` `cmd_jump_table` (`:41-65`), `get_cmd_pointer.not_supported` (`:117-126`), `cmd_add_breakpoint` |
| **No** 24 (`GET_SUPPORTED_COMMANDS`), 25/26 (bank memory), 27/28, 39, 42/43 (watchpoints), 50/51 (state) | same; `:74-77` "not supported, and silent" |
| **An unsupported command ends the session**: `cmd_not_supported` sends no reply and jumps to `drain_main`, which falls into `main`, which "RE-INITIALISE[S] THE DEBUGGEE'S SAVED STATE": only `prgm_state` (to `PRGM_IDLE`), the clock speed, the interrupt state, the layer 2 port and the slot 0 backup; PC, SP and the general registers are left as they were | `commands.asm` `cmd_not_supported`; `src/main.asm:117-147` |
| Breakpoints are client-owned `RST 0` patches: CMD 13 sets a list and returns the original bytes, CMD 14 restores them (only where the byte is still 0xC7, spec) | `cmd_set_breakpoints`, `cmd_restore_mem`; `doc/DEZOG-BREAKPOINTS-DESIGN.md` §1, §3 |
| CMD_CONTINUE takes up to two temporary breakpoints, patched by the stub | `cmd_continue` |
| `CMD_PAUSE` only acknowledges; the stop itself comes from the Copper-NMI poll while running | `cmd_pause` banner |
| **While the program runs, any received byte is a break or nothing.** With the async break armed, the per-frame poll breaks on ANY byte (reason `MANUAL_BREAK`; it cannot tell a `CMD_PAUSE` from the ESP-01's unsolicited `CONNECT`/`CLOSED` lines), and only then is the command read; with the break off (a Copper-using program without the two instructions, the "C" key) nothing reads the link and the command times out | `doc/ASYNCHRONOUS-BREAK-DESIGN.md` "BREAK ON ANY BYTE"; README (doc) |
| Sprite attributes and patterns (18/19) are "answered but empty"; palette and clip (16/17) are served | `cmd_jump_table` comments `:60-61` |
| Reserves 8K banks 91 (loopback), 92 (temp), 94 (MAIN, refused by CMD_WRITE_BANK); runs MAIN in slot 7 and uses slot 6 as a swap window while stopped | `src/constants.asm` `LOOPBACK_BANK`, `TMP_BANK`, `MAIN_BANK`, `MAIN_SLOT`, `SWAP_SLOT`; `cmd_write_bank` banner |
| Reports slot 7 from its backup and slot 0 as ROM while stopped | `cmd_get_registers`; `SLOT_BACKUP` struct comment |
| Patches the 48K ROM into the Alt ROM (bytes 0x0000-0x0007 and 0x0066-0x0073) so ROM code can be stepped | `src/altrom.asm` `copy_modify_altrom` |
| **`CMD_INIT` writes the machine's slots** (confirmed on the KS2, R0.5): `slot_backup.slot0` = ROM, `slot_backup.slot7` = 1, and NR 0x51-0x56 = ROM, 10, 11, 4, 5, 0 (the ZX128 defaults), also on a re-attach to a live debuggee. The slots a client reads right after `CMD_INIT` are that table, not the interrupted program's mapping. The client-visible slot 7 (the one `CMD_READ_MEM` pages in for 0xE000-0xFFFF) is the backup value 1; NR 0x57 reads 0x5E (`MAIN_BANK`), the stub's own slot 7 | `commands.asm:150-166`, `:712-720`; `constants.asm:15,17`; `REMOTE-TARGET-VALIDATION.md` §1.2, §1.3 A |
| **A session starts with no debuggee** (D24). Registers are saved only by an NMI over a RUNNING debuggee (`.break_into_debuggee` then `save_registers`) and by `dbg_enter` (an RST 0 breakpoint), and written by `CMD_SET_REGISTER`; with `prgm_state` `PRGM_IDLE` the NMI re-initialises the stub or returns at once. So the registers read after a FRESH entry from NextZXOS are the zero initialisers of `backup.*` (observed on the KS2), but after a previous session (`CMD_CLOSE`, an error through `drain_main`) they are that session's stale values: zero is not a property of every session, so "no debuggee" is a client-side per-session state, never inferred from register values. `CMD_INIT` sets `PRGM_LOADING`; a `CMD_CONTINUE` before SP and PC are set would resume whatever `backup.*` holds (source only, not tried on hardware) | `mf_rom.asm:386-423`; `mf.asm:105-112`; `breakpoints.asm:144-146`; `data.asm:68-91`; `main.asm:142-156`; `commands.asm` `cmd_init`, `cmd_continue`, `cmd_close`; `backup.asm:58-145`; `REMOTE-TARGET-VALIDATION.md` §1.3 B |
| UART build: 921600 baud on the joy port, each message preceded by 0xA5 | `constants.asm` `BAUDRATE`; `transport_uart.asm` `TRANSPORT_MESSAGE_START` |
| WiFi build: TCP server on port 11000 via the ESP-01, link up to 460800 baud, about 6.6 ms round trip and 20 KB/s | README (doc) |
| PC Pause works over WiFi, and over the cable on joy port 2 only; the serial break "has not run on hardware" | README (doc); dezogif_ng #43 |
| A program that uses the Copper loses the async break unless it carries the two instructions itself; the "C" key turns the break off | README; `doc/ASYNCHRONOUS-BREAK-DESIGN.md` §0.1 (doc) |
| Needs core 03.01.10 or later (stackless NMI) and a Multiface-enabled config | README (doc) |

### 2.4a maziac's dezogif, the upstream: what `CMD_INIT` cannot tell apart

| Fact | Evidence |
|---|---|
| At the fork point the upstream answers **byte-identically** to dezogif_ng: DZRP 2.1.0, machine 4, `"dezogif v2.2.1"` | `git show dee4255:src/commands.asm` (`DZRP_VERSION` 2.1.0); `dee4255:src/data_const.asm` `PRG_VERSION` |
| But it routes **`CMD_PAUSE` (7)**, 18 and 19 to `cmd_not_supported`, so on that stub a Pause resets the session exactly as an unknown command does on dezogif_ng | `dee4255:src/commands.asm`, `.pause: defw cmd_not_supported ; 7, not supported on a ZX Next` |
| The upstream today (`71c7ad0`) answers **DZRP 2.2.0**, `"dezogif v2.3.0-rc2"`, and serves `CMD_GET_SUPPORTED_COMMANDS`; its bitfield is 1-4, 6-17, 20-24 (no 5, 18/19, 25/26, 28, 39-43) | `upstream/main:src/commands.asm:27-29`, `:65`, `cmd_get_supported_commands` |
| It emits `NTF_LOG` (notification 2) | `upstream/main:src/dbg_send_log.asm`, `SEND_NTF_LOG` in `cmd_close` |
| **Neither upstream version has a TCP transport**: both are UART-only (`src/uart.asm`; no `transport_esp.asm`). The WiFi transport is dezogif_ng's addition | `git ls-tree dee4255 src/`, `git ls-tree upstream/main src/`; dezogif_ng README "What this fork adds" |
| DeZog 3.8 requires a 2.2 remote | DeZog `upstream/main:src/remotes/dzrptransport/dzrptransportremote.ts:60`, `:630-635` |

So `CMD_INIT` alone cannot tell dezogif_ng from the upstream at the fork point, and the
difference matters (Pause). The **link** can: a 2.1.0 `"dezogif ..."` reached over TCP can only
be dezogif_ng's WiFi build. jnext supports no cable (owner, 2026-11-27, D25), so every
`"dezogif ..."` that is not 2.1.0 over TCP is refused. §3 turns this into the identity rule.

DeZog's client for this stub (`ZxNextSerialRemote`, `src/remotes/dzrpbuffer/zxnextserialremote.ts`)
adds the rules a client must keep: no breakpoint in ROM (bank 0xFE/0xFF) nor at 0x0000-0x0007
or 0x0066-0x0073 (`checkBreakpoint`); RST 08 is stepped as one instruction and a DJNZ's second
temporary breakpoint at the PC is dropped (`calcStepBp`); continuing from a breakpoint is two
continues, first stepping off with the hit address left unpatched (`sendDzrpCmdContinue`); IM
cannot be read back (`Z80RegistersZxNextDecoder` returns NaN).

### 2.5 What DZRP cannot carry, for any remote

- `CMD_GET_REGISTERS` has no IFF1, IFF2, HALT state, MEMPTR or Q (spec, `CMD_GET_REGISTERS`).
- There is no reset, no load, no time (cycle, frame, raster), no trace, no history, no
  coverage, no Copper and no audio command.
- `CMD_CONTINUE` carries at most **two** temporary breakpoints; its step-over and step-out
  "alternate commands" are specified but "At the moment there are no plans to implement" them.
- DZRP 2.2.0 makes `CMD_GET_SUPPORTED_COMMANDS` mandatory **for remotes that answer 2.2.0**,
  so probing with it is safe there (jnext); a 2.1.0 remote such as
  dezogif_ng has no way to say what it supports, and probing it destroys its session (§2.4).

---

## 3. Architecture

```
  Qt debugger window (panels, actions)          holds ONE Debugger* at a time (R1.4)
        |                                   |
  Debugger (local)                     Debugger (remote)            host-side half, ONE code:
   symbols, source map, C values,       the same class              symbols, sources, C frames,
   conditions, stepping strategy                                    conditions, step strategy
        |  Target (R1.1)                    |  Target
  LocalTarget -> Emulator              DzrpTarget (jnext_remote)
                                            |  DzrpClient (R0.3)
                                            |  link: TCP (R0.4) | memory pipe (tests)
                                       remote jnext (DzrpServer)  |  real Next (dezogif_ng)
```

- **The seam is below the facade** (decision D1). `Debugger` keeps its published API and gains
  a constructor over a `Target`; the machine half of `Debugger::Impl` moves behind
  `Target`, with `LocalTarget` holding today's `Emulator` code unchanged. A remote session is a
  **second `Debugger` instance** over a `DzrpTarget`; the local one keeps serving the local
  emulator, its servers and its scripts.
- **The host-side half runs in jnext for every target** (D2): symbols, the source map, C
  variables and frames, breakpoint conditions and handlers, and the stepping strategy read the
  target only through `Target`'s primitives. That is the 2026-11-11 design: the target supplies
  registers, memory, banking, address breakpoints and continue/pause.
- **Capabilities are the target's answer, not the GUI's guess** (R1.2). `Debugger::capabilities()`
  returns a bitmask plus a reason string per missing capability; every verb a target cannot
  serve returns `Result::Unsupported` (already in `result.h`), never a silent no-op.
- **Synchronous reads over a slow link** (D3). Panels read synchronously, as today. `DzrpTarget`
  keeps a **stop-time snapshot**: at every stop it fetches, pipelined in one burst, the
  registers and slots, the memory windows the panels show first (around PC, around SP, the
  watch and C-frame ranges) and the NextREGs the MMU panel needs. A read outside the snapshot
  is a bounded blocking fetch at 256-byte granularity, cached until the next resume. A write
  goes through at once and updates the cache. While the target runs, the snapshot is stale and
  marked so; a jnext target also answers reads while running, dezogif_ng cannot (the stub is
  not executing).
- **Pumping.** `DzrpTarget` is the remote `Debugger`'s `dbg::Service`: the Qt adapter calls the
  remote facade's `pump()` from the same tick it already runs, so NTF_PAUSE arrives as a
  `Paused` the window reacts to exactly as to a local stop. No thread (architecture §5's
  reasons hold).
- **Waiting without a thread: the wait hook** (D17). A blocking fetch waits in a loop of
  `ClientLink::poll()` up to the response timeout, and every iteration calls a **wait hook**
  that the owner of the remote facade installs on `DzrpClient`: the LOCAL facade's
  `Debugger::pump(PumpBudget{})`. This is what makes a single-threaded process able to talk to
  a server it hosts itself: an in-process `DzrpServer` is a `Service` of the local facade and
  is drained only by its `pump()` (`src/remote/transport.h`, `Server`), so without the hook the
  loopback suite (R2.7) would wait for a reply that cannot come. In the GUI the same hook keeps
  the local jnext's own servers (a DeZog attached to it, say) served during the wait. The hook
  never runs frames and is never called from inside `run_frame()` or a LOCAL event delivery (a
  panel refresh is neither), so architecture §5's no-reentrancy rule holds; a wait started
  from inside the hook is refused. The connect and `CMD_INIT` phase is asynchronous (one step
  per tick) and never a blocking wait.
- **No session with oneself** (D17). The client's `CMD_INIT` program name carries a
  per-process random session token; a `DzrpServer` that receives a token of its own process
  answers error 1, and the client reports "this is this jnext's own DZRP server". Such a
  session has no use, and it would make every remote stop a stop of the machine running the
  window. The unit suites inject distinct tokens so the in-process loopback stays possible.
- **The local emulator stalls during a blocking wait.** One thread: while a fetch waits, the
  local emulator runs no frames. The stop-time burst exists to make that rare; each wait is
  bounded by the response timeout (§12).
- **Profiles, chosen from `CMD_INIT` and the link** (D4, D14, D19, D25). The client never sends a
  command outside its profile's set, because one unsupported command resets a dezogif_ng
  session (§2.4).

  | `CMD_INIT` answer | Link | Profile | Command set | Breakpoints |
  |---|---|---|---|---|
  | `"jnext v..."`, 2.2.0 or later | any | jnext | from `CMD_GET_SUPPORTED_COMMANDS` | normal (40/41) |
  | `"dezogif ..."`, 2.1.0 | TCP | dezogif_ng (only its WiFi build has TCP, §2.4a) | fixed, §2.4 | simple (13/14) |
  | any other `"dezogif ..."` (2.2.0 or later, or 2.1.0 over a link that is not TCP) | any | refused: the reason names the WiFi dezogif_ng stub as the supported one | none | n/a |
  | anything else | any | refused, naming the remote | none | n/a |

  Only the WiFi dezogif_ng is supported (owner, 2026-11-27, D25). When upstream dezogif gains a
  WiFi transport, a profile is added then. Every profile accepts `NTF_LOG` and logs it.
- **Nothing but Pause while a dezogif program runs** (D18). From §2.4: on the dezogif profile
  the send gate allows only `CMD_PAUSE` while the program runs; everything else waits for the stop, or is done inside an absorbed pause (R4.2).
- **Two breakpoint dialects** (D5), chosen by the profile and never mixed in one session (spec,
  "Modes"): normal mode (40/41, the remote owns the list) for jnext; simple mode (13/14, the
  client owns the list and patches around every continue) for dezogif_ng.

---

## 4. Phase R0: foundations

Goal: the client side of the protocol exists, is tested against the server's own code, and
changes nothing a user sees.

### R0.1 Target-seam inventory and header proposal

- **Design.** Classify each public method of `Debugger` (`src/debug/debugger.h`) as **machine**
  (dispatched to `Target`: registers, peek/poke, slots, NextREG, ports, run/pause/step,
  subscriptions, sprites, palette), **host-side** (unchanged: symbols, annotations, source map,
  C values, `source_step` strategy, exports), or **local-only** (refuses with `Unsupported` on a
  remote: rewind, trace, coverage, `render_layer`, copper, AY, input injection, capture, bookmarks,
  `load` through the loop driver, `reset`). Output: a table appended to this document and the
  proposed `src/debug/target.h` (published, std headers only, and added to `HEADERS` in
  `test/lint-debug-headers.sh:79-84` so the include-graph lint gates it like the other four)
  plus the facade additions: a constructor or factory over a `Target`,
  `capabilities()`, `target_info()` (kind, remote name and version, link text) and the register
  validity mask.
- **Files.** This document; `src/debug/target.h` (declarations only); `test/lint-debug-headers.sh`;
  `doc/design/debug-subsystem/b0-cap-traceability.md` gets the new CAP rows.
- **Tests.** None beyond compiling the header in the lint (`lint-debug-headers` row).
- **Acceptance.** Every method of `debugger.h` appears once in the table with its class and a
  reason, and the additions written match the set the owner approved (Q1, 2026-11-18: a
  constructor or factory over a `Target`, `capabilities()`, `why_unavailable()`,
  `target_info()`, the register validity mask, and `src/debug/target.h`); anything beyond that
  set goes back to the owner first.
- **Depends on.** Nothing (the inventory can start at once). Blocks R1.1.

### R0.2 Client framing

- **Design.** Add to `src/remote/dzrp/dzrp_frame.{h,cpp}` the inverse pair: `encode_command(seq,
  id, payload)` and an incremental `ResponseParser` that yields responses (seq 1..15 in the low
  four bits) and notifications (seq 0, first byte the id), with the same rules
  `FrameParser` enforces (16 MiB cap, chunk timeout from the last byte that advanced the frame).
  A `swallow_until(0xA5)` pre-stage for a serial dezogif profile was built here and then
  removed with that profile (D25); the WiFi build has no preamble.
- **Tests.** New suite `dzrp_client_test`, rows `DZC-01..`: round trips through the
  server's `FrameParser` and `encode_response()` (both directions, every length edge: 0, 1,
  2^16, the cap), truncated and stalled frames, a notification interleaved between responses,
  a leading 0xA5 or 0x00 being a length byte. Declared in `test/unit-tests.conf` with its count.
- **Acceptance.** The server's parser accepts every command the encoder builds and the client
  parser accepts every response the server's encoder builds, byte for byte.
- **Depends on.** Nothing.

### R0.3 Client session core

- **Design.** `jnext::remote::dzrp::DzrpClient` (`src/remote/dzrp/dzrp_client.{h,cpp}`): owns
  a link, sends `CMD_INIT` (version 2.2.0, name `"jnext v<version>"`), parses the reply into a
  `RemoteInfo{name, version, machine}` and selects a profile from §3's table (D4, D19:
  the answer and the link type). The name it sends carries the process's session token (D17).
  Keeps a pipelining window (configurable; default 15 for jnext, 1 for the dezogif_ng profile until
  R4.6 measures otherwise), a per-command timeout, a queue of NTF_PAUSE notifications, and
  logs `NTF_LOG`. **Every send goes through one gate** that refuses (1) a command outside the
  profile's set (the never-unsupported rule) and (2) on a dezogif profile, any command but
  `CMD_PAUSE` while the program runs (D18); a refusal is a client bug logged at error level.
  Blocking waits call the wait hook (D17) on every poll iteration.
- **Files.** `src/remote/dzrp/dzrp_client.{h,cpp}`; `src/remote/dzrp/dzrp_server.cpp`
  (`DzrpServer::cmd_init`: read the client name, and if it carries a token of this process
  answer error 1 before anything is attached or paused, as the malformed-payload path already
  does); a revision-log entry in `doc/design/debug-subsystem/dzrp-frontend.md` for that server
  change; the developer guide's DZRP server chapter
  (`src/doc/developer-guide/03-subsystems/10-the-dzrp-server.md`).
- **Tests.** `dzrp_client_test` rows over a scripted peer: version gate, each profile, unknown
  remote refused, sequence wrap 15 -> 1, a reply with a wrong sequence number (protocol error,
  link closed), timeout, NTF_PAUSE between a command and its reply, CMD_CLOSE on teardown. Gate
  rows: with the dezogif_ng profile, asking for command 24, 25 or 42 sends nothing; with a dezogif profile and the
  program running, any command but 7 sends nothing. Identity rows: each line of §3's table,
  including a 2.1.0 `"dezogif v2.2.1"` answer giving dezogif_ng over TCP and refused over a serial-kind link, and a 2.2.0 one refused over TCP, both with the remote's name and version and the WiFi dezogif_ng reason.
  Wait-hook rows: a blocking read to an in-process `DzrpServer` over the memory pair completes
  with the hook installed and times out without it; a wait started inside the hook is refused.
  Self-connection row: a server receiving its own process's token answers error 1.
- **Acceptance.** All rows green; against a real `DzrpServer` over the memory pipe (R0.4) the
  session opens and closes and the server's log shows one attach and one detach.
- **Depends on.** R0.2, R0.4.

### R0.4 Client links

- **Design.** A `ClientLink` interface (`src/remote/client_link.h`): `open`, `read`, `write`,
  `poll(wait_ms)`, `state`, `describe()`. Two implementations: `TcpLink` over
  `esp::make_socket_transport()` with a client `AddressPolicy` (loopback and private ranges
  allowed: the user names the host), `MemoryLinkPair` for the suites (two ends of an in-memory
  pipe, the second end handed to a `remote::Server` through `FakeListener`,
  `src/remote/fake_transport.h`). No new socket code (D13).
  A link never pumps its peer: in-process peers are served by R0.3's wait hook (D17).
  This task also ships `test/dzrp_client_probe`, the test-only driver R0.5 runs on the KS2 and
  R2.8's `remote_target_probe` grows from; it has one test-only switch,
  `--send-unsupported ID`, that bypasses the send gate for exactly one command (R0.5, Q10), and
  the product client has no such path.
- **Tests.** `remote_transport_test` gains `RLK-` rows: connect refused, connect timeout, peer
  close, partial writes, the memory pair against `remote::Server`. Windows: the TCP rows run in
  `make unit-test-win` (the esp twin is already there).
- **Acceptance.** Rows green in `make unit-test`, `unit-test-sdl`, `unit-test-win`.
- **Depends on.** Nothing.

### R0.5 Early hardware checkpoint (owner)

- **Goal.** Before R1 to R4 build on §2.4's source-derived facts, check the ones a session
  rests on against the real stub on the owner's Next: a **KS2, board issue 4**, running
  dezogif_ng's WiFi ROM (`make mf-rom-wifi`) over its ESP-01, the PoC link the owner chose (Q3).
- **Design.** The test-only driver of R0.4 (`test/dzrp_client_probe`). **Connect only while
  the stub is stopped**: press the NMI button first, so the debugger's screen is up; a TCP
  connect while the program runs makes the ESP-01 print `CONNECT`, which the async break takes
  as a break (§2.4). Then `tcp:<Next IP>:11000`, `CMD_INIT`, `CMD_GET_REGISTERS`, `CMD_READ_MEM`
  of a few ranges (around PC and SP, one in each slot), `CMD_GET_TBBLUE_REG` of NR 0x00 / 0x01 /
  0x50-0x57, all from the dezogif_ng set, and record the results. **Last, and once** (owner,
  Q10, 2026-11-18): send ONE unsupported command, `CMD_GET_SUPPORTED_COMMANDS` (24), the one D4
  forbids, with `--send-unsupported 24`; expect no reply within the response timeout and the
  stub back on its idle screen (`main` re-initialised); then reconnect and send `CMD_INIT` to
  confirm the stub serves a fresh session. Nothing else outside the set is ever sent.
- **Record.** A first section of `doc/testing/REMOTE-TARGET-VALIDATION.md` (R4.6 completes it):
  board (KS2, issue 4), core version (NR 0x01 and the boot screen), dezogif_ng build number (its
  banner), the `CMD_INIT` reply bytes, the register and memory values against what the Next's
  screen shows, and the round-trip time of each command.
- **Acceptance.** The record exists and states, for each of: the `CMD_INIT` answer (DZRP 2.1.0,
  `"dezogif v2.2.1"`), the slot reporting of `CMD_GET_REGISTERS`, `CMD_READ_MEM` returning the
  debuggee's bytes, and the one unsupported command (no reply, session reset, a fresh session
  served afterwards), whether the hardware agrees with §2.4. Any disagreement is folded into §2.4
  and the affected tasks before R1.1 starts.
- **Result (2026-11-27, KS2, core 3.02.01, dezogif_ng build 00.26).** `doc/testing/REMOTE-TARGET-VALIDATION.md`
  §1: the `CMD_INIT` answer, the slot reporting, the Alt ROM and 48K ROM reads and the silent
  reset AGREE with §2.4; round trip median 7.4 to 7.5 ms. NOT checked: `CMD_READ_MEM` returning
  a debuggee's bytes, because there was no debuggee (R4.6 checks it after an upload). New and
  folded in (§2.4, §11, R1.2, R2.1, R2.5, R3.2, R4.1, R4.3, R4.5, R4.6, §12, §13 Q11, D24):
  `CMD_INIT` rewrites the slots, and a session starts with no debuggee.
- **Depends on.** R0.3, R0.4. Blocks R1.1 (by owner decision 2026-11-17).

---

## 5. Phase R1: the target interface in the backend

Goal: the local debugger runs on the seam with no change in behaviour or speed, and the GUI
knows how to grey out what a target cannot do.

### R1.1 `Target` and `LocalTarget`

- **Design.** Implement R0.1's header. `Debugger::Impl` holds `Target& target` and, for a local
  target, `Emulator* emu` (null for a remote). Every machine method dispatches through the
  target; every local-only method returns `Unsupported` when `emu` is null. The `source_step`
  forward loop splits into a strategy (host-side) and a primitive ("step one instruction",
  "run with these transient breakpoints") the target provides; `LocalTarget` keeps the exact
  synchronous `debugger_step()` loop, so local behaviour is byte-identical. The hot loop
  (`Emulator::run_frame()`) is not touched: subscriptions still live in the `EventTable`.
- **Files.** `src/debug/target.h`, `src/debug/local_target.{h,cpp}`, `src/debug/debugger*.cpp`,
  `src/debug/debugger_impl.h`.
- **Tests.** Every existing suite unchanged and green in all four configurations
  (`make build-matrix`, `unit-test`, `unit-test-sdl`, `unit-test-win`, `unit-test-wasm`);
  `make bench` A/B on `build/gui-release-non-pgo` within noise; the debugger rows of the
  regression run as targeted rows (`dzrp-*`, `gdb-*`, `zrcp-*`, the Qt debugger rows).
- **Docs.** Developer guide 3.9 (`src/doc/developer-guide/03-subsystems/09-1-the-backend-api.md`):
  the seam and the three method classes.
- **Acceptance.** No test count changes except new rows; no behaviour change; bench within noise.
- **Depends on.** R0.1 (owner-approved header); R0.5 (the hardware checkpoint, owner decision 2026-11-17).
- **Measured (#513 Stage B).** The delivery path is unchanged by construction: `git diff` of
  `src/core src/cpu src/memory src/video src/audio src/peripheral src/port src/platform src/gui
  src/debugger src/remote` against the epic is empty, and `drain_boundary()`, `execute_gate()` and
  `deliver_to_subscribers()` compile at `-O2` to the same instruction stream as before (base and
  branch objects of `debugger_events.cpp` compared with `objdump`: the one difference is the
  displacement of the `emu` load, which is no longer the first member of `Impl`, plus padding).
  The wall-time `make bench` A/B and `ab-hotlatch` runs need a quiet host (load below 1.5) and are
  still to be taken; the R1.1 box is ticked when they are.

### R1.2 Capability model

- **Design.** `enum class Capability` (published, in `target.h`) with one bit per capability of §11
  (two groups in an explicit order, not a row-for-row copy: see the note under section 11), `Debugger::capabilities()`, `Debugger::why_unavailable(Capability) -> std::string`,
  and a `RegisterValidity` mask beside `registers()` (IFF1/IFF2, IM, HALT, MEMPTR/Q), with
  `cycle` and `frame` of `RunState` valid exactly when the `MachineTime` capability is (D5 of
  the #513 plan: `inspect.h` is frozen, so no `RunState` field is added). `LocalTarget` reports
  everything.
- **Tests.** Backend suite rows: local reports all; a fake target reporting none makes every
  machine verb return `Unsupported` (one row per verb class) and never touches memory.
- **Acceptance.** No verb of a remote facade can silently succeed for a capability the target
  lacks.
- **Depends on.** R1.1.
- **No debuggee** (D24, from R0.5; a separate bullet, after Depends, so it does not rewrite the
  Design sentence, which #513 words differently). A dezogif target is "no debuggee" from every
  `CMD_INIT` until its client has set SP, PC and the program's slots in this session ("the
  program's slots": every slot the client means the program to use, set by `CMD_SET_SLOT` after
  this `CMD_INIT`; R4.3's upload sets those of the NEX's bank mapping). In Stage A's terms, with no
  new `Capability` bit (`CAPABILITY_COUNT` unchanged): the target's capability set lacks `Run`,
  `InstructionStep` and `RunToCursor` (all three need `CMD_CONTINUE` and valid registers, R2.4),
  with `why_unavailable` of each = "no program loaded: upload one"; its `RegisterValidity` has
  `Core` cleared, so `registers()` returns zeros marked invalid; the slots stay available (they
  are true) and the MMU panel labels them "CMD_INIT defaults" until the state clears;
  `set_mmu_slot`, memory and NextREG verbs are not gated by it. What #513 does for it (Stage C, R1.2;
  Stage B only built the seam and left `capabilities()` and the validity accessors undefined):
  `register_validity()` passes the target's validity mask through (unknown bits dropped), and
  `capabilities()` is the target's set intersected with what the facade serves (the served set of
  #513's section 3, eight bits), so `why_unavailable(Run)` answers with the target's own reason;
  the panel's reason rule is R1.3's. The reason texts are one per capability, from section 11's
  "When unavailable" column (the #513 inventory's reason column is per verb), so R1.3 shows one text each. The state is the target's own and is never derived from register values.
  `LocalTarget` and remote jnext never report it. Row for it, added to the Tests above: a fake
  reporting "no debuggee" has `Run`, `InstructionStep` and `RunToCursor` unavailable with that
  reason and `Core` invalid, and still serves memory, NextREG reads and `set_mmu_slot`.

### R1.3 Qt greying

- **Design.** Each `QAction` in `DebuggerWindow` (`src/debugger/debugger_window.cpp`, the Debug,
  Source, Trace, Coverage, Rewind, Script, Breakpoints and Watches menus) and each panel names
  the capability it needs. `DebuggerWindow::apply_capabilities()` disables the actions (tooltip
  "Not available on <target>: <reason>") and shows a placeholder page in a panel that cannot be
  served (the reason, not an empty table). The CPU panel shows `?` for an invalid register;
  the Call Stack panel marks a reconstructed stack (R3.4). When `Core` is invalid the CPU panel
  shows the reason of `why_unavailable(Capability::Run)` ("no program loaded: upload one", D24).
- **Tests.** New Qt suite `debugger_capabilities_test` (`?`-gated `qt+dbg`), rows `RTQ-`: for
  each capability, a fake target lacking only it greys exactly the actions and panels the
  table says, and restoring it ungreys them.
- **Docs.** User guide `06-debugger/panels/index.md`: what a greyed panel means.
- **Acceptance.** §11's table and the suite agree row for row.
- **Depends on.** R1.2.

### R1.4 Debugger window retargeting

- **Design.** One debugger window, one target at a time (owner, Q8, 2026-11-18: acceptable). `DebuggerManager` owns an
  optional remote `Debugger` and switches the window between the local and the remote facade;
  the panels' `set_backend()` (or a rebuild of the window) moves them. The title and the status
  bar name the target ("Remote: jnext v1.3.5 at 127.0.0.1:14000"). Switching resets the
  panels' caches and the breakpoint model; breakpoints are per target.
- **Tests.** Qt suite rows: switch to a fake remote and back, panels read the right facade,
  local breakpoints survive a remote session.
- **Acceptance.** No panel keeps a pointer to the old facade after a switch (a row closes the
  remote facade and asserts no access).
- **Depends on.** R1.3.

---

## 6. Phase R2: remote jnext over DZRP

Goal: jnext's debugger drives another jnext over DZRP with every capability DZRP carries, and
CI proves it on loopback. This is also the PoC's client, minus the dezogif profile.

### R2.1 `DzrpTarget` inspection

- **Design.** `jnext::remote::dzrp::DzrpTarget : jnext::dbg::Target` (`src/remote/dzrp/dzrp_target.{h,cpp}`).
  Registers: `CMD_GET_REGISTERS` (also the eight slot banks), `CMD_SET_REGISTER`. CPU memory:
  `CMD_READ_MEM` / `CMD_WRITE_MEM`, or `CMD_READ_MEM_BLOCKS` (28) when advertised, for the
  stop-time burst. Physical pages and ROM: `CMD_READ_BANK_MEM` / `CMD_WRITE_BANK_MEM`
  (25/26, ROM as bank 0xFF). Slots: `CMD_SET_SLOT`. NextREGs: `CMD_GET_TBBLUE_REG` per register
  (pipelined), writes by `CMD_SET_NEXTREGS` (27) when advertised. Sprites: 16-19. Paging ports
  for the MMU panel from NextREGs where the Next exposes them. Snapshot cache per D3. On a
  dezogif target in the "no debuggee" state (R1.2) the burst leaves out the registers (invalid
  anyway) and fetches the slots as usual (they are `CMD_INIT`'s defaults, labelled so).
- **Tests.** New suite `remote_target_test`, created here and extended by R2.2 to R2.4 and
  R2.7 (the in-process setup and wait hook of R2.7's design), rows `RTG-`: each primitive against `LocalTarget`
  on the same machine state; the snapshot burst is one pipelined write (row counts frames
  before the first reply); a write is visible to the next read without a refetch.
- **Acceptance.** Every inspection panel the table marks "remote jnext: yes" shows the same
  values as the local debugger of the target jnext at the same stop.
- **Depends on.** R0.3, R1.2.

### R2.2 Run control and execute breakpoints

- **Design.** `run()` -> `CMD_CONTINUE` with no temporaries; `pause()` -> `CMD_PAUSE`; NTF_PAUSE
  -> `Paused{reason}` (reason 1 manual, 2 breakpoint, 0 step, 255 + text). An `Execute`
  subscription -> `CMD_ADD_BREAKPOINT` with its long address (page -> bank+1), removal ->
  41. Condition, handler, `once`, `Log`/`Continue` actions and hit counts run in the client at
  each stop, against the remote facade (a `Condition` takes `const Debugger&`, `events.h`), and
  a false condition resumes at once: one round trip per hit, stated in the tooltip. Other event
  kinds -> `Unsupported`.
- **Tests.** `RTG-` rows: breakpoint in RAM, page-qualified, and in ROM (a jnext target can;
  it is armed in every ROM bank, because the server maps bank 0xFE/0xFF to `PAGE_ANY`,
  `bank1_to_page()`, `src/remote/dzrp/dzrp_server.cpp:105-124`),
  conditional true/false, `once`, a hit while the GUI of the target jnext also has a breakpoint
  (both clients see their own), detach resumes only a pause the client made (the server's rule,
  `dzrp-close-resume-func`).
- **Acceptance.** The breakpoint panel's state matches `CMD_ADD/REMOVE` traffic at every step.
- **Depends on.** R2.1.

### R2.3 Watchpoints

- **Design.** A `Mem` subscription with an access mask -> `CMD_ADD_WATCHPOINT` (address,
  bank+1, size, access); removal by the exact tuple (the server matches by tuple,
  `DzrpServer::Watch`). NTF_PAUSE reasons 3/4 -> `Watch{access, addr}`.
- **Tests.** `RTG-` rows: read, write, read-write, a range, a banked watch, removal.
- **Acceptance.** As R2.2, for the Watches and Breakpoints panels.
- **Depends on.** R2.2.

### R2.4 Instruction stepping by temporary breakpoints

- **Design.** A pure function `step_successors(regs, reader, mode) -> {addr, addr?}` in
  `src/debug/step_successors.{h,cpp}` (host-side, used by every remote profile): the address
  after the instruction, and the branch target where one exists (JP/JR/DJNZ/CALL cc, RET cc and
  RET from the stack word, `JP (HL)`, `(IX)`, `(IY)` from registers, RST, `RETI/RETN`, the Z80N
  opcodes, `JP (C)` refused as unknowable). Step Into = `CMD_CONTINUE` with both; Step Over =
  only the next address for a call-like instruction (`is_call_like()`, `src/debug/disasm.h:29`);
  Run to Cursor = one temporary; Step Out = a temporary at the return address when R3.4 knows
  it, else DeZog's loop of step-overs until a RET pops above the start SP
  (`dzrpremote.ts` `stepOut`). HALT: stepping it waits for an interrupt (said in the status
  bar). Decision D6: our own function, tested against our own core, not a port of DeZog's
  `calcStepBp`.
- **Tests.** New suite `step_successors_test`, rows `SSC-`: for every opcode of the Z80 and
  Z80N sets (every prefix group the disassembler `src/debug/disasm.cpp` decodes), in states that take and do not take
  each branch, execute one instruction on jnext's CPU core (the FUSE-validated oracle) and
  assert the PC reached is one of the successors. Remote rows in `RTG-` for each step verb.
- **Acceptance.** No opcode reaches a PC outside its successor set; steps on a remote jnext
  land where the local step lands on the same program.
- **Depends on.** R2.2.

### R2.5 Connect and disconnect in the GUI

- **Design.** Debug > Remote Target... opens a dialog: link (TCP), host and port,
  connect timeout, response timeout; Connect, Disconnect. On connect the
  window retargets (R1.4) and pauses the remote (DeZog's convention; the jnext server's
  `CMD_INIT` pauses already, dezogif_ng is stopped by construction, but has no debuggee until its client sets one up, D24). Errors are a status-bar
  message plus the log line. A lost link freezes the panels with a banner and offers
  Reconnect; it never takes the local emulator down.
- **Tests.** Qt suite rows over a `MemoryLinkPair`: connect, refuse (unknown remote), lose the
  link mid-run, reconnect.
- **Docs.** User guide (R2.9).
- **Acceptance.** Closing the remote jnext while connected leaves the local jnext running and
  the window back on a usable state within the response timeout.
- **Depends on.** R1.4, R2.2.

### R2.6 CLI and Preferences

- **Design.** One enabler flag, `--remote-target URI` with `tcp:HOST:PORT`, which opens the debugger window on that target at start. The
  Preferences "Debugger" tab (`src/gui/preferences_dialog.cpp`, `build_debugger_tab()`) gains a
  "Remote target" group holding the dialog's fields. Each field is a
  `KeyId` entry plus a `KEYS[]` row in `src/platform/config_keys.h` (section `remote`): `host`
  (Text), `port` (Int 1..65535), `connect_timeout_ms` (Int), `response_timeout_ms` (Int), all `QT`,
  `needs_debugger = true`, and `flag = nullptr`: `Key::flag` means "a dedicated option with the
  same effect" (`config_keys.h:111`), and `--remote-target` does more than set these keys (it
  also connects at start), so every field is set by `--set remote.KEY=VALUE` and the man page
  says which keys `--remote-target` fills from its URI. The URI grammar is exactly
  `tcp:HOST:PORT`; the program file is a separate key
  (`remote.program`, R3.1). `cli_options.h` gains the
  `--remote-target` entry; headless and the SDL frontend refuse it with a message (there is no
  debugger window to drive).
- **Tests.** `cli_options_test` (via `make cli-check`), `app_config_test` (AC-94/95/96 cover a
  new key automatically), a `--set remote.port=...` row.
- **Docs.** `doc/man/jnext.1.md` Debugging section, regenerated `jnext.1`, `USAGE.md` and the
  user guide's option appendix (`make docs-man`, `make docs-userguide`).
- **Acceptance.** `make cli-check` and `make docs-check` green; every dialog field is reachable
  from the CLI.
- **Depends on.** R2.5.

### R2.7 Loopback parity suite

- **Design.** `remote_target_test` builds, in one process, an `Emulator` with its local
  `Debugger` and a `DzrpServer`, and a remote `Debugger` over a `DzrpTarget` over the
  `MemoryLinkPair`. Each row runs the same scenario through the local facade and through the
  remote one and compares registers, memory, slots, NextREGs, sprites, breakpoints and
  watchpoints hit. Later sub-issues add their own parity rows to this suite: step landings
  (R2.4, sub-issue 6), source resolution and build match (R3.1, R3.2, sub-issue 9), source steps
  (R3.3, sub-issue 10), the rebuilt call stack and C values (R3.4, R3.5, sub-issue 11), and
  upload (R4.3, sub-issue 13). The remote facade's `DzrpClient` gets the local facade's `pump()` as its wait hook
  (§3, D17) and the two ends distinct session tokens, which is what lets one thread wait on a
  server it hosts. The server is the "fake shim" the epic asks for on the jnext side, and it is
  not a fake: it is the shipped server.
- **Tests.** Rows `RTG-`, declared in `test/unit-tests.conf` (Linux, SDL-only and Windows
  configurations: no Qt).
- **Acceptance.** Every R2 capability of §11's "remote jnext" column (inspection: registers,
  memory, page and ROM views, slots, NextREGs, sprites; Run and Pause; execute breakpoints with
  conditions; watchpoints) has at least one parity row. The other "remote jnext: yes" rows get
  theirs from the sub-issues named in the design.
- **Depends on.** R2.1 to R2.3 (sub-issue 5).

### R2.8 Functional rows

- **Design.** A test-only executable `remote_target_probe` (built in `test/`, like
  `fuse_z80_test`; D10) connects a remote facade to a URI and runs a named scenario, printing
  rows. `remote-jnext-func`: start a headless jnext with `--dzrp-port 0` on a demo NEX, read the
  logged port, run the probe (connect, registers, breakpoint, continue, hit, step, disconnect;
  R3.6 adds a source step and a C variable). `remote-qt-func`: `xvfb-run -d` (with `-noreset`, HS-72/HS-89) a GUI jnext with
  `--remote-target tcp:127.0.0.1:PORT` against the same target, assert the "connected to jnext"
  log line and a stop. Both in `test/00regression/functional_tests.conf`, `os=` as the
  harness allows (the probe row runs on linux, macos, windows).
- **Acceptance.** Both rows green as targeted rows (`make regression-rows ROWS=...`).
- **Depends on.** R2.6, R2.7.

### R2.9 Docs for remote jnext

- User guide: new page `src/doc/user-guide/06-debugger/functions/18-debugging-a-remote-target.md`
  (connect, what greys out and why, security: DZRP has no authentication, so the target's
  `--debug-listen-address` decides who can connect).
- Developer guide: new chapter `src/doc/developer-guide/03-subsystems/10-8-remote-targets.md`
  (the seam, the profiles, the cache, the stepping strategy), linked from `10-0`.
- FEATURES.md entry.
- **Acceptance.** `make docs-check` green; the user guide page covers connecting from the
  dialog and from `--remote-target`, every "remote jnext: no" row of §11 with its reason, and
  the security note; the developer guide chapter covers the seam, the profile table of §3, the
  wait hook and the snapshot cache.

---

## 7. Phase R3: source-level debugging on a remote target

Goal: the Source tab, line stepping, the call stack and C variables work on a remote target,
over plain DZRP.

### R3.1 Program and sidecars; fetch identity

- **Design.** The user names the program file of the remote session (dialog field, and the CLI
  through `--set remote.program=FILE`, a new `KeyId` + `KEYS[]` row as in R2.6). The
  existing `Debugger::load_program_sidecars(path)` loads its map, SLD, `.lis`, `.cdb` as for a
  local load. The fetch identity on a remote is the MMU's: `Page{p}` for a RAM slot, `Rom{}` for
  bank 0xFF; DivMMC, Multiface, Alt ROM and Layer 2 overlays are invisible over DZRP and are
  reported as such (`source_page()` returns the slot's page; §11).
- **Tests.** `RSRC-` rows in `remote_target_test`: a z88dk banked program's lines resolve on
  the remote as on the local.
- **Acceptance.** With the program named, the Source tab, symbols and line breakpoints of a
  remote session resolve to the same files, lines and addresses as a local session of the same
  program; with an overlay mapped, the views say "MMU only" instead of showing a wrong line.
- **Depends on.** R2.7.

### R3.2 Build-match check

- **Design.** At connect and after an upload (on a "no debuggee" session, D24, only once that state has cleared, never against `CMD_INIT`'s default mapping), compare the target's bytes with the program
  file: for a NEX, its banks (through `NexLoader`, `src/core/nex_loader.h`) sampled at every
  source-mapped code range; for a bare binary with a map, the bytes the map's code ranges cover.
  A mismatch shows a banner ("the program on the target is not the build these sources
  describe") and disables source features until the user confirms. Reads use the snapshot
  burst, so the check costs a bounded number of round trips.
- **Tests.** `RSRC-` rows: match, one byte changed, wrong bank mapped.
- **Acceptance.** A one-byte difference in any source-mapped code range is reported before
  any line is shown, and a match costs no more round trips than the snapshot burst plus one
  read per mapped bank.
- **Depends on.** R3.1.

### R3.3 Source line stepping

- **Design.** The strategy half of `source_step()` (R1.1) gets a remote primitive. For the
  current statement's code ranges, the **exit set** is: the address after the range, and every
  successor (R2.4) of every instruction in the range that falls outside it; for Over, the
  successors of call-like instructions are their return points; an indirect transfer (`JP
  (HL)`, `RET` whose slot is not provable) puts a breakpoint on the transfer itself, and the
  step continues from there by one instruction step. The exit set becomes the two
  `CMD_CONTINUE` temporaries plus, beyond two, transient breakpoints (normal mode: 40/41 added
  and removed around the continue; simple mode: part of the CMD 13 list). Over checks the SP
  at the stop against the start (recursion re-enters the same line deeper: continue). Out uses
  R3.4's return address. Back and Reverse Continue stay local-only (no trace remotely). D7.
- **Tests.** `RSRC-` rows comparing remote and local landings for Into, Over, Out on the C and
  assembler fixtures `source_debug_test` and `c_value_test` already use; a statement with more than two
  exits; a recursive call; a `JP (HL)` dispatch.
- **Acceptance.** Same landing lines as the local source step on every fixture.
- **Depends on.** R2.4, R3.1.

### R3.4 Call stack by stack walking

- **Design.** At each stop, `call_stack()` on a remote is rebuilt from memory: C frames through
  the existing no-tracker path of `Debugger::c_frames()` (frame pointers and entry bytes); for
  assembler, each stack word `w` above SP whose preceding bytes are a CALL (`w-3`) or RST
  (`w-1`) to code is taken as a return address, bounded by depth and by the stack page. The
  panel labels it "reconstructed". Step Out takes the innermost return address.
- **Tests.** `RSRC-` rows: the reconstructed stack against the local tracker on the same stop,
  for C (both frame styles in `c_frames()`), assembler, and a `banked_call`.
- **Acceptance.** Equal frames on the fixtures; a known heuristic miss (data on the stack that
  looks like a return address) is documented, not hidden.
- **Depends on.** R3.1.

### R3.5 C variables on a remote target

- **Design.** No change to `debugger_cvalues.cpp` (it uses `peek()` and `registers()`); the
  remote cache serves its reads, and the snapshot burst includes the frames' ranges. Bulk reads
  by `CMD_READ_MEM_BLOCKS` when advertised; plain `CMD_READ_MEM` otherwise (dezogif). Writes
  through `poke()` as today.
- **Tests.** `RSRC-` parity rows for locals, globals, structs and arrays on the C fixture.
- **Acceptance.** The Variables panel shows the same rows and values on a remote stop as on the
  local one; on a dezogif profile no bulk command is ever sent (gate row).
- **Depends on.** R3.4.

### R3.6 Tests and docs for remote source debugging

- Functional: extend `remote-jnext-func` (or a sibling `remote-source-func`) with a source
  step and a C variable on a z88dk demo.
- Docs: user guide `06-debugger/functions/14-source-level-debugging.md` and R2.9's new page; the
  developer guide's `03-subsystems/09-5-source-level-debugging/05-breakpoints-steps-history.md` (3.9.5.5) and
  `10-c-frames-and-values.md` (3.9.5.10) gain the remote strategy.
- **Acceptance.** The functional row green as a targeted row; `make docs-check` green; the
  pages say what a remote source step costs (round trips) and what stays local-only (Back,
  Reverse Continue).
- **Depends on.** R2.8 and R2.9 (sub-issue 8: the probe, `remote-jnext-func` and the page it
  extends), R3.3 (sub-issue 10) and R3.5.

---

## 8. Phase R4: remote Next with dezogif_ng (proof of concept)

Goal: the same client debugs a program on a real Next running dezogif_ng, and CI proves the
protocol path against the real stub running in jnext.

### R4.1 dezogif_ng profile

- **Design.** The dezogif_ng profile (`dzrp_client.cpp`), selected by §3's identity rule (2.1.0
  `"dezogif ..."` over TCP). Commands 1-23 except 22, plus nothing above; while the
  program runs, only `CMD_PAUSE` (D18); breakpoints in simple mode (R4.2); IM, IFF1, IFF2, HALT invalid; no watchpoints, no
  bank memory, no state; sprite attributes and patterns unavailable (the stub returns zeros);
  pipelining window 1 until R4.6 measures more; reserved banks 91, 92 and 94 refused for writes
  and marked in the memory views; slot 7 reads come from the stub's backup and are correct for
  the debuggee (`cmd_get_registers`); **"no debuggee" (D24) from every `CMD_INIT`**: the profile
  offers no Run and shows registers invalid (R1.2) until the client has set SP, PC (`CMD_SET_REGISTER`)
  and the program's slots (`CMD_SET_SLOT`) in this session, and the gate refuses `CMD_CONTINUE`
  before that (design change from the round-3 text); physical page and ROM image views greyed in the PoC (owner,
  Q7, 2026-11-18). Capabilities per §11.
- **Tests.** `dzrp_client_test` rows with a scripted dezogif peer (the fake shim for the
  hardware dialect; its fresh connect state is R0.5's: zero registers, slots `FF FF 0A 0B 04 05
  00 01`; rows: Run unavailable and `CMD_CONTINUE` refused before the client sets SP, PC and
  slots, offered after): handshake, the gate refuses every command outside the set (a test that a
  stray 24 would be caught before it is sent, because the real stub would answer nothing and
  reset); the running-state rule; the peer emits `NTF_LOG` and the client logs it.
- **Acceptance.** Against the scripted peer, a full session (connect, the client sets SP, PC and
  slots by `CMD_SET_REGISTER` and `CMD_SET_SLOT`, which clears "no debuggee"; registers, memory, a
  breakpoint, continue, hit, step, Pause, disconnect) sends only commands of the profile's set,
  and nothing but command 7 while running; the capability set matches §11's dezogif_ng column.
- **Scope of the "no debuggee" rule.** The dezogif_ng profile, the only dezogif profile
  (observed on the KS2). A 2.2.0 stub is refused (D25), so Q11 is closed.
- **Depends on.** R2.7.

### R4.2 Simple-mode breakpoints

- **Design.** The client owns the breakpoint list. Before every continue: `CMD_SET_BREAKPOINTS`
  with all enabled execute breakpoints except the one the PC stands on, keeping the returned
  original bytes; at every stop, before any read is served from the cache, `CMD_RESTORE_MEM`
  with those bytes (the disassembly hazard, `DEZOG-BREAKPOINTS-DESIGN.md` §3: a patched 0xC7
  must never reach the disassembler or the step computation). Continuing from a breakpoint is
  two continues: step off with temporaries, then patch it back and continue
  (`ZxNextSerialRemote.sendDzrpCmdContinue`). Refused: ROM banks, 0x0000-0x0007, 0x0066-0x0073
  (`checkBreakpoint`); RST 08 stepped as one instruction; a DJNZ's temporary at the PC dropped
  (`calcStepBp`). A breakpoint added, removed, enabled or disabled while the program runs is NOT sent at once:
  any byte would break the program (async break armed) or go unanswered (break off, §2.4).
  Where the async break works, the edit is done as an **absorbed pause**: `CMD_PAUSE`, wait for
  `NTF_PAUSE`, restore and re-set the list (14, then 13), `CMD_CONTINUE`, and the window does not
  show that stop (the program is stopped for those round trips; said in the tooltip).
  If the async break is off (the "C" key, or a Copper program without the two instructions) the edit is **queued** and applied
  at the next stop, and the breakpoint shows "pending" until then. A NTF_PAUSE
  at a temporary that is also a user breakpoint is reported as a step (DeZog's correction).
- **Tests.** Scripted-peer rows for each rule, and for edits while running: with Pause the
  wire carries exactly 7, then (after `NTF_PAUSE`) 14, 13, 6, and no `Paused` reaches the
  window; without Pause the wire carries nothing until the next stop, then 14 and 13 with the
  queued edit in. Emulated-rig rows (R4.5) for the real stub.
- **Acceptance.** After every stop the cache and the target hold no 0xC7 the program did not
  write (row reads the patched addresses after the restore).
- **Depends on.** R4.1, R2.4.

### R4.3 Program upload

- **Design.** Load a NEX onto the target the way DeZog does: each bank by `CMD_WRITE_BANK`
  (dezogif) or `CMD_WRITE_BANK_MEM` (jnext), slots by `CMD_SET_SLOT`, SP and PC by
  `CMD_SET_REGISTER`, then the session is stopped at the entry point. **Upload is how a dezogif_ng
  session gets a debuggee** (D24): the NMI saves nothing from NextZXOS, so there is nothing to
  attach to. The upload must set every register it relies on (the stub's values are zero or
  another session's) and every slot of the program, because `CMD_INIT` has already overwritten
  slots 1-6 with the ZX128 defaults. SP, PC and the slots set in this session is the event that
  clears "no debuggee" (R1.2, R4.1), so the first `CMD_CONTINUE` follows it. The NEX is parsed by
  `NexLoader` (`src/core/nex_loader.h`). Refuses a NEX that needs bank 94 (MAIN) or 91/92 on
  dezogif. The same uploader serves a jnext target, so "Load program" works on both. Part of
  the PoC (owner, Q5, 2026-11-18).
- **Tests.** `RTG-` rows on the loopback (jnext target): uploaded memory equals a local
  `--load` of the same NEX at its entry point; scripted-peer row for the dezogif command
  sequence.
- **Acceptance.** After an upload the target stops at the NEX's entry point with its banks,
  slots, SP and PC as a local `--load` of the same file sets them, `Run` is available and the
  registers read valid on dezogif_ng, and the build-match check
  (R3.2) passes; a NEX touching banks 91, 92 or 94 is refused before any byte is sent to
  dezogif_ng.
- **Depends on.** R4.1; R3.2 for the build-match part of the acceptance.

### R4.5 Emulated-rig rows

- **Design.** The real dezogif_ng running in jnext, driven by the probe. **The ROMs are vendored
  prebuilt fixtures** (owner, Q4, 2026-11-18), not built from source, so there is no sjasmplus
  dependency: `test/fixtures/dezogif_ng/` holds the WiFi ROM (`enNextMf-wifi.rom`) of one dezogif_ng build, with its `LICENSE`
  and `NOTICE` (MIT, maziac 2020 and jorgegv 2026), a `README` naming the build number and the
  dezogif_ng commit, and a sha256 manifest the row checks before use (a mismatch is a FAIL).
  Updating them is a deliberate fixture change with a new build number. No git submodule. A
  scratch card copy (`private-sd`), the ROM installed by `jnext --sdcard COPY --sdcard-file-add ROM
  --sdcard-file-dest /MACHINES/NEXT/enNextMf.rom --sdcard-file-force`, then headless jnext with
  `--esp --esp-listen-address 127.0.0.1 --delayed-nmi-frames ...` (WiFi ROM) and the probe on
  `tcp:127.0.0.1:11000` (row `remote-dezogif-wifi-func`, tagged `serial` because the guest's
  port is fixed at 11000). That WiFi row is the primary one, matching the owner's PoC link.
  The probe connects while the stub is
  stopped: a TCP connect while the program runs makes the ESP-01 print `CONNECT`, which the
  async break takes as a break (§2.4). Scenario: connect (fresh boot and delayed NMI: rows assert
  R0.5's state: zero registers, slots `FF FF 0A 0B 04 05 00 01`, Run unavailable), upload a demo NEX (R4.3), registers,
  memory and slots, breakpoint, continue, hit, step, a breakpoint edit while running (absorbed
  pause, R4.2), pause of a running program, disconnect. The NextZXOS
  image is the one jnext provisions; no image is committed or uploaded, in any form.
- **Tests.** The WiFi row.
- **Acceptance.** The row green as targeted rows; they fail when the client sends an
  unsupported command (mutation: drop the gate, the stub resets and the row goes red).
- **Depends on.** R4.2, R4.3, R2.8 (the probe).

### R4.6 Hardware validation (owner)

- **Design.** A manual protocol in `doc/testing/REMOTE-TARGET-VALIDATION.md` (the shape of
  `DZRP-VALIDATION.md`, started by R0.5): the R4.5 scenario on the owner's **KS2, board issue 4**,
  over WiFi (dezogif_ng's WiFi ROM on the ESP-01), with a C program uploaded by R4.3 and source
  stepping; first repeat R0.5's connect checks on a stub freshly entered from NextZXOS (NMI first:
  after an earlier session the registers are stale, not zero) as the "before" of the upload, and
  check that `CMD_READ_MEM` returns the uploaded program's bytes (R0.5 could not: no debuggee); record
  round-trip time, stop-refresh time, Step Over and line-step wall time, an absorbed-pause
  breakpoint edit, and the largest pipelining window that holds. Every record names the board
  (KS2, issue 4), the core version and the dezogif_ng build. Results set the dezogif_ng
  profile's window and timeouts.
- **Acceptance.** Every row of §11's dezogif_ng column that says "yes" has a recorded result on
  the KS2 (pass, or a defect folded back into the plan), and the latency figures are in the
  record.

### R4.7 Docs for the real Next

- User guide page "Debugging a real Next" (install dezogif_ng's WiFi ROM, what greys out,
  the Copper and joystick caveats from §2.4); man page for `tcp:` URIs; developer guide: the
  dezogif profile and the simple-mode dance. FEATURES.md entry.
- **Acceptance.** `make docs-check` green; the user guide page covers installing the WiFi ROM,
  connecting, every "no" of §11's dezogif_ng column with its reason, the running-program rule
  (edits cause a short absorbed pause) and the Copper and joystick caveats.

---

## 9. Phase R5: proof-of-concept verdict

- **R5.1** A report appended to this document: what the PoC does on hardware, what is missing
  against §11, measured costs, and the shim options (below).
- **R5.2** The owner decides. Options to put to the owner, all within the 2026-11-10 decision
  (no DZRP extensions, no own protocol): (a) stop at dezogif_ng; (b) bring dezogif_ng to
  DZRP 2.2.0: maziac's upstream already answers 2.2.0 and serves `CMD_GET_SUPPORTED_COMMANDS`
  (`71c7ad0`, §2.4a), so (b) is merging that and adding what neither stub has (25/26 bank
  memory, 28 block reads) in dezogif_ng, the owner's own project; the client then uses them
  through the bitfield (the client then needs a profile for it, D25: today a 2.2.0 dezogif is refused); (c) a jnext shim in this
  repository (R6).

## 10. Phase R6: custom shim (placeholders, gated by R5.2)

- **R6.1** Import: dezogif_ng's sources, distributed under jnext's GPL with the MIT notices of
  maziac (2020) and jorgegv (2026) kept (`LICENSE`, `NOTICE`); a licensing note beside
  `doc/legal/`.
- **R6.2** Build: dezogif_ng's ROM is **sjasmplus** source, and z88dk builds only its `mfselect`
  and `.mfinstall` tools (README "Requirements"), so the issue body's "built with z88dk" holds
  for the tools, not the ROM. Either sjasmplus becomes a pinned build dependency (owner
  approval, PULL-REQUEST-PROTOCOL's no-new-dependency rule) or the sources are ported to
  z88dk's assembler. Reproducible output (dezogif_ng already has `check-reproducible`).
  Version in lockstep with `version.yaml`.
- **R6.3** Features, standard DZRP only: `CMD_GET_SUPPORTED_COMMANDS`, bank memory, block
  reads, normal-mode breakpoints if the 216-byte budget of the WiFi build allows
  (`DEZOG-BREAKPOINTS-DESIGN.md` §2), WiFi, NMI and Copper activation as dezogif_ng has them.

---

## 11. What degrades on a remote target

"Remote jnext" is another jnext over DZRP 2.2.0 (`DzrpServer`). "Remote Next" is a Next running
dezogif_ng build 0026 (DZRP 2.1.0) through the dezogif_ng profile (WiFi, the PoC link); the
link is WiFi only (D25). "Greyed" means the action is disabled with the reason in its
tooltip, or the panel shows a placeholder with the reason (R1.3).

| Capability | Local jnext | Remote jnext | Remote Next (dezogif_ng) | When unavailable |
|---|---|---|---|---|
| Run, Pause | yes | yes (CMD 6/7, NTF_PAUSE) | Run yes, once the client has set SP, PC and slots ("no debuggee" before, D24); Pause only with the async break (WiFi ROM yes), else the NMI button; not for a Copper-using program without the two instructions | Pause stays enabled; on timeout the status bar says "press the NMI button" |
| Step Into / Over / Out (instruction) | exact (`debugger_step`) | by temporary breakpoints, one round trip per step; Out by return address or DeZog's loop | same; not before the client has set SP, PC and slots (D24); not in ROM, not at 0x0000-0x0007 / 0x0066-0x0073; RST 08 steps over | step actions disabled at a forbidden PC, with the reason |
| Run to Cursor | yes | yes | yes, cursor in RAM only, and not before the client has set SP, PC and slots (D24) | disabled on a ROM or forbidden line |
| Run to end of frame / scanline, run to cycle | yes | no (no time in DZRP) | no | greyed: "cycle-exact: local only" |
| Step Back, Frame Back, rewind slider, source Back, Reverse Continue | yes | no | no | Rewind menu and history actions greyed |
| Execute breakpoints | anywhere | anywhere (CMD 40/41); page-qualified in RAM, a ROM breakpoint armed in every ROM bank | RAM only (RST 0 patches, CMD 13/14), patched only while running; an edit while running is an absorbed pause, or queued ("pending") without the async break (R4.2) | the gutter refuses the click with the reason |
| Any command while the program runs | yes | yes | only `CMD_PAUSE`: any other byte would break the program or go unanswered (§2.4, D18) | edits absorbed or queued; reads show the last stop, marked stale |
| Conditions, log actions, `once`, hit counts | evaluated in the emulator | evaluated by the client at each hit, auto-continue (a round trip per hit) | same | enabled; tooltip states the cost |
| Read / write watchpoints | yes | yes (CMD 42/43) | no | Watch-breakpoint actions greyed |
| Port, NextREG-write, interrupt, frame, Copper, DMA events | yes | no | no | not offered (script-only today) |
| Registers PC SP AF BC DE HL IX IY and shadows, I, R | yes | yes | yes once the client has set SP, PC and slots; invalid before ("no debuggee", D24) | CPU panel shows the reason of Run: "no program loaded: upload one" |
| IM | yes | yes | unknown | CPU panel shows `?` |
| IFF1, IFF2, HALT, MEMPTR | yes | unknown (not in DZRP) | unknown | CPU panel shows `?` |
| Register write | yes | yes | yes | n/a |
| Memory read / write, CPU view | yes | yes | yes, while stopped | n/a |
| Memory read while running | yes (4 Hz refresh) | yes, at the target's next tick | no (the stub is not executing) | panels show the last stop, marked stale |
| Physical page and ROM image views | yes | yes (CMD 25/26, ROM bank 0xFF) | no in the PoC (owner, Q7) | the Memory panel's page view greyed |
| Overlays in the fetch identity (DivMMC, Multiface, Alt ROM, Layer 2 read map) | yes | no (DZRP sends the slot's bank only) | no; ROM slots show the stub's patched Alt ROM | source lookup uses the slot's page; Disassembly labels the identity "MMU only" |
| MMU slots | yes | yes | yes (slot 7 from the stub's backup); right after `CMD_INIT` they are its ZX128 defaults, labelled so (§2.4) | n/a |
| NextREG panel | yes | yes, one command per register, pipelined | yes, one round trip per register, refreshed on stop only | n/a |
| Sprite attributes, patterns | yes | yes (CMD 18/19) | no (answered with zeros) | Sprites panel shows palette and clip only |
| Sprite palette and clip | yes | yes | yes | n/a |
| Copper panel | yes | no command | no (write-only on hardware; the stub owns the list) | placeholder |
| Video panel (layers, raster) | yes | no | no | placeholder |
| Audio panel (AY, mute) | yes | no | no | placeholder |
| Trace log | yes | no | no | Trace menu greyed |
| Memory and source coverage | yes | no | no | Coverage menu greyed; no tint |
| Call Stack | tracked every instruction | rebuilt by stack walking at each stop | same | panel labelled "reconstructed" |
| Source tab, line breakpoints, source Into / Over / Out | yes | yes (exit-point breakpoints) | yes, RAM code only | line breakpoints refused on ROM lines |
| C variables, read and write | yes | yes | yes | n/a |
| Symbols, user labels and comments, map loading, skoolkit / ghidra export and import | yes | yes (host-side) | yes | exports omit coverage data |
| Debugger scripts (DSL) | yes | no (scripts run inside the emulator's event delivery) | no | Script menu greyed |
| Magic breakpoint toggle | yes | no (the target's own setting) | no | greyed |
| Load program | yes | upload (R4.3) | upload (R4.3), part of the PoC | n/a once R4.3 lands; greyed before |
| Reset | yes | no (no reset in DZRP) | no | greyed |
| Machine time (T-states, frame, raster position) | yes | no | no | status fields show `?` |
| Build match against the program file | n/a | yes (R3.2) | yes (R3.2) | banner on mismatch |
| Ports (`port_in`, `port_out`) | yes | yes (CMD 20/21) | yes (CMD 20/21, inside the served 1-23) | n/a |
| Input injection (keys, joystick, NMI button) | yes | no | no | Input actions greyed |
| Capture (screenshot, flush) | yes | no | no | Capture actions greyed |
| Machine state (bookmarks, snapshots, state bytes) | yes | no (DZRP 50/51 are not used in R1) | no | Bookmark and snapshot actions greyed |
| CTC and DMA state | yes | no | no | placeholder |
| Paging ports and the NextREG select latch | yes | no | no | fields show `?` |

The six rows from Ports down were added by the R0.1 inventory (section 15): verbs of `Debugger`
that no earlier row covered. Notes on how the table maps onto `Capability` (`src/debug/target.h`):
the enum is NOT a row-for-row copy of the table: it has two groups (what a remote may serve, then
local-only), each in an explicit order fixed by `static_assert`s in `src/debug/debug_types_check.cpp`;
"Run, Pause" is two bits (`Run`, `Pause`); other rows map to one bit or several (Step Back ... and
"Run to end of frame ..." are `Rewind` and `CycleExact`). Rows with NO bit: the register rows
("Registers PC SP AF BC DE HL IX IY and shadows, I, R", IM, IFF1/IFF2/HALT/MEMPTR) are
`RegisterValidity` fields (Core, IM, IFF1, IFF2, Halted, Memptr), not capability bits; "Conditions,
log actions, `once`, hit counts" is never greyed; "Source tab, line breakpoints, source Into / Over /
Out", "C variables, read and write" and "Symbols, user labels and comments, map loading, skoolkit /
ghidra export and import" are host-side and need only the capabilities of the primitives they call;
"Build match" is a host-side check (R3.2). `RunState::cycle` and `RunState::frame`, like `time()`, are
valid exactly when the `MachineTime` capability is.

---

## 12. Risks

| Risk | Consequence | Mitigation |
|---|---|---|
| The frozen header needs additions | R1 cannot start without them | approved by the owner (Q1, 2026-11-18); R0.1 writes exactly the approved set, and anything more goes back to the owner |
| Latency: synchronous panels over a 6.6 ms (WiFi, doc) or 20 ms (a Qt jnext tick) round trip | a slow or stalling window | the stop-time pipelined burst; misses at 256-byte granularity; measure in R4.6 and R2.8; widen the burst before making panels asynchronous |
| One unsupported command resets dezogif_ng's session (verified, `main.asm:117-147`) | a client bug loses the user's session | the send gate (R0.3) and its rows; the emulated-rig mutation row (R4.5) |
| A patched 0xC7 reaches the disassembler | wrong instruction lengths, wrong step breakpoints | restore before the cache is filled (R4.2), asserted by a row |
| Self-modifying code over a breakpoint | CMD 14 leaves a changed byte alone (spec); a breakpoint silently disappears | documented in the user guide; the breakpoint panel re-reads the byte at the next stop |
| A wrong successor sends a step running away | lost stop; on dezogif the program runs free | the opcode-table oracle suite (R2.4); Pause where available |
| `JP (C)` and unknowable indirect transfers | no static successor | stop at the instruction and single-step it |
| Stack-walk heuristic finds data that looks like a return address | a wrong frame in the Call Stack | labelled "reconstructed"; C frames use frame pointers |
| CMD_CONTINUE carries two temporaries only | line steps with more exits | transient breakpoints beyond two (R3.3) |
| The emulated ESP binds the guest's fixed port 11000 | parallel rows collide | the row is tagged `serial` |
| The vendored dezogif_ng ROM fixtures drift from the stub the owner runs | the emulated rig tests a different build from the KS2 | the fixtures' README names the build; R4.6 records the KS2's build; a mismatch is updated as a deliberate fixture change (owner, Q4) |
| A dezogif session has no debuggee until its client sets one up, and `CMD_INIT` rewrites slots 1-6 (R0.5) | zero or stale registers shown as a CPU state; a `CMD_CONTINUE` resuming `backup.*` | the "no debuggee" state (R1.2, D24): Run unavailable, registers invalid, no continue before SP, PC and slots are set (R4.1, R4.3); rows in R4.1 and R4.5 |
| R0.5 sends one unsupported command on purpose | that session on the KS2 is lost | done last and once, by the owner's decision (Q10), with a reconnect afterwards; only the test probe can bypass the gate |
| `CMD_INIT` cannot tell dezogif_ng from the upstream at the fork point (both 2.1.0 `"dezogif v2.2.1"`), and a Pause resets the old upstream's session (§2.4a) | a Pause destroys the session on an old upstream stub | the identity rule (§3, D19, D25): only 2.1.0 over TCP is accepted, and the upstream has no TCP transport, so it is never reached |
| The upstream dezogif is already on DZRP 2.2.0 (`71c7ad0`), and dezogif_ng may follow | a user points the client at a 2.2.0 dezogif | refused with the WiFi dezogif_ng reason (D25); a profile is added when upstream supports WiFi |
| A blocking fetch stalls the local emulator (one thread) | the local machine's frames and sound stop while a remote read waits | the stop-time burst makes misses rare; each wait is bounded by the response timeout (setting, R2.6); the wait hook keeps the local servers served; measured in R2.8 and R4.6 |
| Waiting on a server in the same process | without the wait hook the in-process loopback suite cannot get a reply | the wait hook (D17), with rows that fail without it |
| A session with oneself | every remote stop stops the local machine | refused by the session token (D17) |
| A byte sent while a dezogif program runs | an unwanted break, or no answer | the running-state send rule (D18) in the gate, with rows; the probe connects only while the stub is stopped (the ESP-01's `CONNECT` line is a byte) |
| The program on the Next is not the build the map describes | wrong lines, wrong variables | the build-match check (R3.2) |
| Interrupts and the stub perturb the debuggee (R, speed, Copper list, joystick port) | values differ from an emulator session | stated in §11 and the user guide, from §2.4's evidence |

---

## 13. Open questions for the owner

Answered by the owner on 2026-11-17 and folded in: **Q2** (document location) is removed: plan
documents live under `doc/design/`, and this file was moved there. **Q3** is answered below.
The KS2 (board issue 4) is available for real testing (R0.5, R4.6). Answered on 2026-11-18
(#486 "UPDATE 2026/11/18") and folded in: Q1, Q4, Q5, Q6, Q7, Q8 and Q10. Only Q9 stays open,
for R5. The owner later withdrew an idea of a dezogif_ng git submodule; the plan adds none.

- **Q1. Frozen header. ANSWERED (owner, 2026-11-18): approved**: a constructor or factory over
  a `Target`, `capabilities()`, `why_unavailable()`, `target_info()`, a register validity mask,
  and the new published header `src/debug/target.h` (R0.1).
- **Q3. Link on the KS2. ANSWERED (owner, 2026-11-17): WiFi**, dezogif_ng's WiFi ROM over the
  ESP-01. Serial was later dropped (D25); the emulated-rig rows use the WiFi path.
- **Q4. dezogif_ng in CI. ANSWERED (owner, 2026-11-18): vendor** the prebuilt MIT ROMs as pinned
  test fixtures (build number, sha256, their notice), not built from source; no sjasmplus
  dependency (R4.5, D20).
- **Q5. Upload in the PoC. ANSWERED (owner, 2026-11-18): yes**, uploading the program to the
  Next is part of the PoC (R4.3, sub-issue 13).
- **Q6. Other dezogif stubs and DZRP remotes. ANSWERED (owner, 2026-11-18): keep** the dezogif
  DZRP 2.2.0 profile, labelled untested. **REVERSED by the owner, 2026-11-27 (D25): the
  2.2.0 profile is dropped**; only the WiFi dezogif_ng is supported. Every other remote (CSpect, ZEsarUX's DZRP,
  ...) stays refused.
- **Q7. Bank views on dezogif_ng. ANSWERED (owner, 2026-11-18): greyed out** in the PoC (R4.1,
  §11).
- **Q8. One target per window. ANSWERED (owner, 2026-11-18): acceptable** (R1.4).
- **Q9. The custom shim's limits.** With DZRP extensions abandoned, a jnext shim could only add
  standard DZRP 2.2.0 commands and the stub-side features dezogif_ng already has. And the issue
  says the shim is built with z88dk, while dezogif_ng's ROM is sjasmplus source (z88dk builds
  only its `mfselect` and `.mfinstall` tools). Both shape R5.2;
  no answer is needed before then, but they are recorded here so R5 starts from them.
- **Q10. The silent reset on hardware. ANSWERED (owner, 2026-11-18): yes**, the early hardware
  checkpoint sends dezogif_ng ONE unsupported command, once, to confirm the silent session reset
  on the KS2 (R0.5).
- **Q11. "No debuggee" on the dezogif 2.2 profile. CLOSED (owner, 2026-11-27, D25):** the
  profile is dropped, so the question does not arise; D24 applies to dezogif_ng only.

---

## 14. Decisions taken in this plan

| # | Decision | Why | Rejected alternative |
|---|---|---|---|
| D1 | The target seam is inside `jnext_debug`, below the facade; a remote session is a second `Debugger` over a `DzrpTarget` | panels keep `Debugger*` and the 99 methods they call; the host-side half exists once | an abstract facade (about 150 virtuals, every panel and the host-side logic duplicated or rewritten); proxies at panel level |
| D2 | Symbols, sources, C values, frames, conditions and the stepping strategy run in jnext for every target | the 2026-11-11 design; plain DZRP carries only primitives | moving logic to the target (needs extensions, abandoned) |
| D3 | Stop-time snapshot cache with a pipelined burst; misses are bounded blocking reads | keeps the synchronous facade and the panels unchanged | asynchronous per-panel fetch (every panel rewritten) |
| D4 | Remote profiles from `CMD_INIT` (name and version) and the link (D19); `GET_SUPPORTED_COMMANDS` only for remotes answering 2.2.0 or later; never send a command outside the profile | dezogif_ng answers 2.1.0, has no command 24, and resets on an unsupported command (`commands.asm`, `main.asm:117-147`); 2.2.0 makes 24 mandatory, so probing is safe there, and the upstream dezogif already serves it (`71c7ad0`) | probing with command 24 everywhere, as #486's 2026-11-11 item 3 suggests: correct for 2.2.0 remotes (jnext, the upstream dezogif today), session-destroying on dezogif_ng |
| D5 | Two breakpoint dialects, normal (40/41) for jnext and simple (13/14) for dezogif_ng, never mixed | the jnext server has no 13/14; dezogif_ng refuses 40 (id 0); the spec forbids mixing in a session | implementing 13/14 in the jnext server so one dialect serves both (patching RAM on an emulator, losing ROM breakpoints) |
| D6 | One successor function in `jnext_debug`, tested against jnext's own Z80 core over the whole opcode table | a FUSE-validated oracle exists in the tree | porting DeZog's `calcStepBp` (no oracle; and code is reimplemented, not copied) |
| D7 | Line steps by the statement's exit set, extra exits as transient breakpoints | one round trip per line, as #486 asks | one instruction step per round trip until the line changes (rejected in #486 itself) |
| D8 | Call stack on a remote rebuilt by walking the stack at each stop | the tracker needs every instruction; `c_frames()` already has a no-tracker path | running a tracker on the target |
| D9 | Tests at three levels: in-process loopback against the shipped server, a scripted dezogif peer, and the real dezogif_ng ROM in jnext; hardware by the owner | a fake alone would test our reading of the stub against itself | a fake shim only |
| D10 | A test-only probe executable drives the remote facade in functional rows | tests the shipped client code end to end without new user features | making jnext a DZRP relay (not in scope); driving the GUI with xdotool for protocol rows |
| D11 | TCP is the only link (R0.4); serial dropped (D25) | TCP covers remote jnext and dezogif_ng's WiFi build, which is the owner's PoC link (Q3) | serial inside the PoC, or after it (dropped, D25) |
| D12 | The custom shim is a placeholder phase gated by R5.2, standard DZRP only | the 2026-11-10 decision makes it conditional and abandons extensions | planning a shim protocol now |
| D13 | The TCP link reuses `esp::make_socket_transport()` with a client address policy | non-blocking, Windows-twinned, already tested; `jnext_remote` stays Qt-free | new socket code; Qt sockets |
| D14 | Recognised: jnext and dezogif_ng (2.1.0 `"dezogif"` over TCP); every other remote refused, a non-matching `"dezogif"` with the WiFi dezogif_ng reason (revised by D25) | scope read literally ("clients for other emulators" abandoned) | generic DZRP remotes; the 2.2.0 dezogif bitfield profile of the first version (reversed by the owner, D25) |
| D15 | One enabler flag `--remote-target URI`, every other field through `--set remote.*`; Qt only | CLAUDE.md's GUI-setting rule with the smallest flag surface | one dedicated flag per field |
| D16 | Remote verbs a target cannot serve return `Result::Unsupported` and are greyed with a reason | the facade's rule "every verb that can refuse says so" | hiding panels |
| D17 | Blocking waits call a wait hook (the local facade's `pump()`); connect and `CMD_INIT` are asynchronous; a session with oneself is refused by a per-process session token | single-threaded, an in-process server is drained only by the local `pump()` (`transport.h` `Server`); a self-session has no use | a client thread (architecture §5's reasons against threads hold); asynchronous panels; detecting self by address and port (misses a LAN address of the same host) |
| D18 | On a dezogif profile, nothing but `CMD_PAUSE` is sent while the program runs; breakpoint edits are an absorbed pause, or queued without Pause | any byte breaks the running program or goes unanswered (`ASYNCHRONOUS-BREAK-DESIGN.md`, "BREAK ON ANY BYTE") | DeZog's way, sending `CMD_SET_BREAKPOINTS` at once (an unannounced break with the async break, a timeout without it) |
| D19 | The 2.1.0 `"dezogif"` identity is resolved by the link: TCP = dezogif_ng; any other link is refused (revised by D25; the conservative serial profile and `remote.stub` are gone) | both forks answer 2.1.0 `"dezogif v2.2.1"`, and only dezogif_ng has a TCP transport (§2.4a) | a probe command to tell them apart (every candidate costs a session on one of them); always asking the user |
| D25 | Real Next over WiFi only: the serial and dezogif 2.2.0 profiles, R4.4 (sub-issue 16) and `remote.stub` are dropped (owner, 2026-11-27, GH #581; Q6 reversed, Q11 closed). When upstream dezogif supports WiFi, a profile is added then | "the better option is to only allow the Wifi option, so only dezogif_ng ... Drop the serial support, it's clumsy and less practical when Wifi is available" | Q11 option 1: accept no Run on the 2.2 profile (profile kept, untested); option 2: un-grey a `CMD_SET_SLOT` + `CMD_WRITE_MEM` upload, designed blind; option 3: Run on the client's say-so; keeping the serial link for cable rigs |
| D20 | The emulated-rig rows use vendored prebuilt dezogif_ng ROMs, pinned by build number and sha256 with their MIT notice; no submodule | owner, Q4 (2026-11-18): no sjasmplus dependency, and a test of a binary the owner also runs on the KS2 | building the ROMs from source (a new pinned toolchain); a git submodule (the owner withdrew that idea) |
| D21 | Upload is part of the PoC (the dezogif 2.2 grey-out is void: that profile is dropped, D25) | owner, Q5 | the `SET_SLOT` + `WRITE_MEM` fallback |
| D22 | R0.5 sends one unsupported command (24), last and once, through a test-only gate bypass in the probe | owner, Q10: confirm the silent reset on the KS2 at the cost of one session; the product client keeps no bypass | not exercising it (the round-2 plan) |
| D23 | Tasks are grouped into 18 sub-issues (16 withdrawn by D25): coupled or sequential tasks on the same files together, parallel ones apart; owner-run tasks and the gate on their own; R6 one placeholder | owner, 2026-11-18 ("group related tasks in the same subissue if possible, considering parallelization") | one sub-issue per task (36 issues, many tiny and strictly sequential) |
| D24 | A dezogif session is "no debuggee" from every `CMD_INIT` until its client has set SP, PC and the program's slots (those the client means it to use): Run, instruction steps and Run to Cursor unavailable ("no program loaded: upload one"), registers invalid, slots available and labelled `CMD_INIT` defaults; client-side, never inferred from register values. Applies to the dezogif_ng profile, the only dezogif profile (D25) | R0.5 on the KS2 (`REMOTE-TARGET-VALIDATION.md` §1.3): `CMD_INIT` rewrites the slots, and registers are zero only after a fresh entry from NextZXOS (stale after an earlier session), so only the client can say a program is set up | showing zero or stale registers as the target's state; inferring the state from zeros; greying the slots (the slots are the mapping `CMD_READ_MEM` goes through and the upload needs `CMD_SET_SLOT`) |


---

## 15. Target-seam inventory (R0.1)

Every public declaration of `jnext::dbg::Debugger` (`src/debug/debugger.h`), one row each
(overloads included), classified for the target seam of §3 (decision D1):

- **M, machine**: the facade keeps argument validation, the local-only guards (RZX, inside-delivery)
  and the attribution (MUTATE line, `note_mutation()`); the machine action is a `Target` call.
- **H, host-side**: unchanged; it reads the machine only through the facade (so through the target).
  Internals that touch the local machine are guarded for a non-local facade.
- **L, local-only**: stays on the local `Emulator`. On a non-local facade a refusing verb returns
  `Result::Unsupported` and a direct-value query returns its empty value, without touching the target.

"Gate" is the `Capability` the verb needs (or "local machine": no bit, local only). "Non-local
answer in R1" is what a `Debugger(Target&)` returns in this epic's R1; "R2.x serves it" marks the
verbs the later sub-issues widen. The set a non-local facade serves in R1 is `RegisterWrite`,
`Memory`, `PhysicalMemory`, `MmuSlots`, `NextRegs`, `Ports`, `SpriteData`, `SpritePaletteClip`
(`capabilities()` is the target's set intersected with it). The five rows marked ADDED are the
additions the owner approved (Q1, 2026-11-18); the table has 209 rows for 205 names.

Checked by a script that reads the header's public span and this table (exit 0 = every declaration
once, nothing extra). Corrections found while reading the bodies are in the reason column:
`probe_execute`, `raise_host_event`, `load_map`, `pump`, `live_raster` and `attached` are H but
read the local machine or local `DebugState` (`ds()`) in their bodies, `effective_page` reads
`emu.mmu()`, and `source_location()` (no argument) reads `emu.cpu()` for the PC; the
`load_source_map` / `load_program_sidecars` family arms local verification hooks. Stage B guards
each of those (the compiler lists `emu.` sites but only the definition of `ds()`, so this list is
the guide for `ds()` callers).

<!-- gh513-inventory-begin -->
| Method | Class | Gate | Non-local answer in R1 | Reason |
|---|---|---|---|---|
| `Debugger` (Emulator&) | H | none | local facade; builds and owns a `LocalTarget` | `Debugger(Emulator&)`, signature unchanged; no caller changes |
| `Debugger` (Target&) | H | none | non-local facade (`emu` null) | ADDED (Q1): `Debugger(Target&)`, TGT-01; the target outlives the facade |
| `~Debugger` | H | none | local mirrors torn down only when local | unhooks the machine; the non-local teardown touches no machine |
| `pause` | M | Pause | `Unsupported` (R2.2 serves it) | target primitive; the stop machinery is local `DebugState` |
| `run` | M | Run | `Unsupported` (R2.2) | same |
| `step_into` | M | InstructionStep | `Unsupported` (R2.4) | local path routes its one step through `Target::step_instruction` |
| `step_over` | M | InstructionStep | `Unsupported` (R2.4) | remote: temporary breakpoints by the facade's strategy |
| `step_out` | M | InstructionStep | `Unsupported` (R2.4) | same |
| `run_to` | M | RunToCursor | `Unsupported` (R2.2) | temporary breakpoint plus run |
| `run_to_source_line` | M | RunToCursor | `Unsupported` (R2.2) | resolves the line host-side, then `run_to` |
| `run_to_cycle` | L | CycleExact | `Unsupported` | no time in DZRP |
| `run_to_frame` | L | CycleExact | `Unsupported` | no time in DZRP |
| `run_to_end_of_frame` | L | CycleExact | `Unsupported` | no time in DZRP |
| `run_to_end_of_scanline` | L | CycleExact | `Unsupported` | no time in DZRP |
| `step_back` | L | Rewind | `Unsupported` | rewind ring lives in the local `Emulator` |
| `rewind_to_frame` | L | Rewind | `Unsupported` | same |
| `resume_blocked_by_corruption` | L | local machine | `nullopt` | corruption latch is a local-machine state error |
| `acknowledge_corruption` | L | local machine | `Unsupported` | same |
| `reset` | L | Reset | `Unsupported` | no reset in DZRP; local path is the loop driver |
| `state` | M | none (target run state) | `Target::run_state()`, `pause_reason` None | host composes the pause reason from local stop evidence; a remote stop is `NTF_PAUSE` |
| `magic_breakpoint` | L | MagicBreakpoint | `false` | the target's own setting |
| `set_magic_breakpoint` | L | MagicBreakpoint | `Unsupported` | same |
| `load` | L | Load | `Unsupported` | goes through the local loop driver (upload is R4.3) |
| `program_loads` | H | none | unchanged | host-side counter |
| `note_program_replaced` | H | none | unchanged | host-side; drops source guards |
| `armed` | L | local machine | `false` | arm bits drive local event hooks |
| `set_persistent_breakpoints` | L | local machine | `Unsupported` | local hook policy |
| `persistent_breakpoints` | L | local machine | `false` | local hook policy |
| `registers` | M | none (validity: Core) | `Target::registers()` | CMD_GET_REGISTERS carries Core; the other fields are `RegisterValidity` |
| `set_register` | M | RegisterWrite | dispatched | facade keeps the MUTATE line and argument checks |
| `peek` | M | Memory (Cpu); PhysicalMemory (Page, Rom, DivMmcRam) | dispatched | facade refuses identity-only kinds; local keeps the F1 read-arbiter path |
| `poke` | M | Memory (Cpu); PhysicalMemory (Page) | dispatched | facade keeps RZX and ROM-sentinel refusals and the MUTATE text |
| `mmu_slots` | M | MmuSlots | dispatched | CMD_GET_SLOTS |
| `set_mmu_slot` | M | MmuSlots | dispatched | CMD_SET_SLOT; slot range checked by the facade |
| `paging_ports` | L | PagingLatches | empty value | port latches are not in DZRP |
| `rom_select` | M | PhysicalMemory | dispatched; without the capability `MemSpace::cpu()` | DZRP ROM bank 0xFF names the image |
| `nextreg_peek` | M | NextRegs | dispatched | CMD_GET_TBBLUE_REG, the read path |
| `nextreg_write` | M | NextRegs | dispatched | facade keeps the MUTATE line |
| `nextreg_selected` | L | PagingLatches | empty value | the NR select latch (port 0x243B) is not in DZRP |
| `port_in` | M | Ports | dispatched | CMD_READ_PORT (remote jnext, dezogif_ng 1-23) |
| `port_out` | M | Ports | dispatched | CMD_WRITE_PORT; the soft-reset delivery guard is local-only |
| `raster` | L | Video | empty value | no raster position in DZRP |
| `time` | L | MachineTime | empty value | no time in DZRP |
| `time_epoch` | L | MachineTime | empty value | same |
| `sprites` | M | SpriteData | dispatched; the facade pads to `SPRITE_COUNT` | CMD_GET_SPRITES (dezogif_ng answers zeros) |
| `sprite_attr_raw` | M | SpriteData | dispatched | same |
| `set_sprite_attr_raw` | M | SpriteData | dispatched | same |
| `pattern_ram` | M | SpriteData | dispatched; empty view unless the size is `PATTERN_RAM_BYTES` | CMD_GET_SPRITE_PATTERNS |
| `write_pattern_ram` | M | SpriteData | dispatched | CMD 19 family |
| `sprite_palette_rgb333` | M | SpritePaletteClip | dispatched | DZRP serves the sprite palette only |
| `sprite_clip` | M | SpritePaletteClip | dispatched | DZRP serves the sprite clip |
| `copper` | L | Copper | empty value | no Copper command |
| `ay_registers` | L | Audio | `Unsupported` | no audio command |
| `turbosound_enabled` | L | Audio | empty value | no audio command |
| `ay_mode` | L | Audio | empty value | no audio command |
| `stereo_mode` | L | Audio | empty value | no audio command |
| `audio_mute_mask` | L | Audio | empty value | no audio command |
| `audio_state` | L | Audio | empty value | no audio command |
| `audio_channel_mute_mask` | L | Audio | empty value | no audio command |
| `audio_scope_enabled` | L | Audio | empty value | no audio command |
| `audio_log_enabled` | L | Audio | empty value | no audio command |
| `stems_recording` | L | Audio | empty value | no audio command |
| `psg_recording` | L | Audio | empty value | no audio command |
| `set_audio_mute_mask` | L | Audio | `Unsupported` | no audio command |
| `set_audio_channel_mute_mask` | L | Audio | `Unsupported` | no audio command |
| `clear_audio_drops` | L | Audio | `Unsupported` | no audio command |
| `set_audio_scope_enabled` | L | Audio | `Unsupported` | no audio command |
| `stems_record_start` | L | Audio | `Unsupported` | no audio command |
| `stems_record_stop` | L | Audio | `Unsupported` | no audio command |
| `set_audio_log_enabled` | L | Audio | `Unsupported` | no audio command |
| `audio_log_clear` | L | Audio | `Unsupported` | no audio command |
| `audio_log_resize` | L | Audio | `Unsupported` | no audio command |
| `audio_log_export` | L | Audio | `Unsupported` | no audio command |
| `psg_record_start` | L | Audio | `Unsupported` | no audio command |
| `psg_record_stop` | L | Audio | `Unsupported` | no audio command |
| `audio_scope_samples` | L | Audio | `Unsupported` | no audio command |
| `audio_log_entries` | L | Audio | `Unsupported` | no audio command |
| `ctc_state` | L | CtcDma | empty value | no CTC command |
| `dma_state` | L | CtcDma | empty value | no DMA command |
| `disassemble` | H | Memory (reads) | plain Cpu reads through the target | no fetch prediction without a local machine |
| `instruction_length` | H | Memory (reads) | same | same |
| `is_call_like` | H | Memory (reads) | same | same |
| `memory_reader` | H | Memory (reads) | plain Cpu reads through the target | local keeps the PC-instruction prediction (FetchOverlays) |
| `operand_reader` | H | Memory (reads) | same | local keeps `predict_data_reader` |
| `call_stack` | L | CallStackTracked | empty value | a remote stack is rebuilt by walking (`c_frames`) |
| `call_stack_enabled` | L | CallStackTracked | `false` | tracker lives in the local `Emulator` |
| `set_call_stack_enabled` | L | CallStackTracked | `Unsupported` | same |
| `trace_enabled` | L | Trace | `false` | no trace in DZRP |
| `set_trace_enabled` | L | Trace | `Unsupported` | same |
| `trace_clear` | L | Trace | `Unsupported` | same |
| `trace_resize` | L | Trace | `Unsupported` | same |
| `trace_entries` | L | Trace | `Unsupported` | same |
| `trace_export` | L | Trace | `Unsupported` | same |
| `framebuffer` | L | Video | empty value | no video in DZRP |
| `render_layer` | L | Video | `Unsupported` | same |
| `palette` | L | Video | empty value | DZRP has the sprite palette only |
| `palette_priority` | L | Video | empty value | same |
| `set_palette` | L | Video | `Unsupported` | same |
| `active_ula_palette_bank` | L | Video | empty value | same |
| `ula_screen_regs` | L | Video | empty value | no video in DZRP |
| `clip_window` | L | Video | empty value | no video in DZRP |
| `input_state` | L | Input | empty value | no input command |
| `subscriptions` | H | none | unchanged | host-side event table; empty on a non-local facade |
| `events_fired_since` | H | none | unchanged | host-side latch ring; empty on a non-local facade |
| `set_border` | L | Video | `Unsupported` | CMD_SET_BORDER is removed in DZRP 2.2.0 |
| `machine` | L | MachineTime | empty value | no machine info in DZRP |
| `coverage_enable` | L | Coverage | `Unsupported` | no coverage in DZRP |
| `coverage_enabled` | L | Coverage | `false` | no coverage in DZRP |
| `coverage_clear` | L | Coverage | `Unsupported` | no coverage in DZRP |
| `coverage` | L | Coverage | empty value | no coverage in DZRP |
| `mem_coverage_enable` | L | Coverage | `Unsupported` | no coverage in DZRP |
| `mem_coverage_enabled` | L | Coverage | `false` | no coverage in DZRP |
| `mem_coverage_clear` | L | Coverage | `Unsupported` | no coverage in DZRP |
| `mem_coverage` | L | Coverage | `Unsupported` | no coverage in DZRP |
| `mem_coverage_fetch` | L | Coverage | empty value | no coverage in DZRP |
| `mem_coverage_spaces` | L | Coverage | empty value | no coverage in DZRP |
| `executed_source_lines` | L | Coverage | empty value | no coverage in DZRP |
| `subscribe` | M | by kind: Execute -> ExecuteBreakpoints; Mem -> Watchpoints; Host -> none (host-side); every other kind -> MachineEvents | `Unsupported` for every kind (R2.2/R2.3 widen it) | remote breakpoints and watchpoints are R2; other kinds need machine hooks |
| `unsubscribe` | H | none | unchanged | host-side event table |
| `set_enabled` | H | none | unchanged | same |
| `subscribe_source_line` | M | ExecuteBreakpoints | `Unsupported` (R2.2) | expands to execute subscriptions |
| `unsubscribe_group` | H | none | unchanged | host-side event table |
| `set_group_enabled` | H | none | unchanged | same |
| `master_enabled` | H | none | unchanged | host-side switch; the local mirror into `DebugState` is guarded |
| `set_master_enabled` | H | none | unchanged | same |
| `client_enabled` | H | none | unchanged | host-side switch |
| `set_client_enabled` | H | none | unchanged | same |
| `probe_execute` | H | none | empty vector | host-side table query, but its body stamps the event from the local machine: guarded |
| `raise_host_event` | H | none | `Ok`, nothing delivered | no Host subscriber can exist (`subscribe` refuses); the body reads the local clock: guarded |
| `press_key` (name) | L | Input | `Unsupported` | no input command |
| `press_key` (MatrixKey) | L | Input | `Unsupported` | no input command |
| `set_key` | L | Input | `Unsupported` | no input command |
| `set_extended_key` | L | Input | `Unsupported` | no input command |
| `set_joystick` | L | Input | `Unsupported` | no input command |
| `press_nmi` | L | Input | `Unsupported` | no input command |
| `screenshot` | L | Capture | `Unsupported` | captures read the local renderer |
| `flush_captures` | L | Capture | `Unsupported` | same |
| `ula_screen_dump` | L | Video | empty value | reads local video memory |
| `bookmark_save` | L | MachineState | `Unsupported` | state images are local (DZRP 50/51 unused in R1) |
| `bookmark_restore` | L | MachineState | `Unsupported` | same |
| `bookmarks` | L | MachineState | empty value | same |
| `save_snapshot` | L | MachineState | `Unsupported` | same |
| `at_frame_boundary` | L | MachineState | `false` | frame boundary is a local-machine notion |
| `save_state_bytes` | L | MachineState | `Unsupported` | state images are local |
| `load_state_bytes` | L | MachineState | `Unsupported` | same |
| `rewind_enabled` | L | Rewind | `false` | rewind ring is local |
| `set_rewind_enabled` | L | Rewind | `Unsupported` | same |
| `rewind_range` | L | Rewind | empty value | same |
| `rewind_blocked` | L | Rewind | `Result::Unsupported` inside the optional | same |
| `resize_rewind_buffer` | L | Rewind | `Unsupported` | same |
| `load_map` | H | none | unchanged | host-side symbol table; binds `emu.predict_fetch` into the toolchain symbols (`debugger.cpp:275`): guarded |
| `clear_symbols` | H | none | unchanged | same |
| `lookup` | H | none | unchanged | same |
| `lookup_name` | H | none | unchanged | same |
| `symbols` | H | none | unchanged | same |
| `set_user_label` | H | none | unchanged | host-side annotations |
| `remove_user_label` | H | none | unchanged | same |
| `set_user_comment` | H | none | unchanged | same |
| `annotations` | H | none | unchanged | same |
| `annotations_revision` | H | none | unchanged | same |
| `load_annotations` | H | none | unchanged | same |
| `save_annotations` | H | none | unchanged | same |
| `replace_annotations` | H | none | unchanged | same |
| `toolchain_symbols` | H | none | unchanged | host-side symbol table |
| `export_skoolkit` | H | none | unchanged | host-side export; coverage data omitted |
| `export_ghidra` | H | none | unchanged | same |
| `import_skoolkit` | H | none | unchanged | host-side import |
| `import_ghidra` | H | none | unchanged | same |
| `load_source_map` | H | none | unchanged | host-side; the local verify hooks it arms (`verify_at_first_execution`) are guarded |
| `clear_source_map` | H | none | unchanged | host-side; local guard teardown guarded |
| `source_map` | H | none | unchanged | host-side |
| `source_map_status` | H | none | unchanged | host-side |
| `variables` | H | none | unchanged | host-side variable store |
| `c_frames` | H | CallStackTracked (tracked frames only) | existing no-tracker path | reads the local tracker when present (`debugger_cframes.cpp`) |
| `c_locals` | H | Memory (reads and writes through the facade) | unchanged | reads through the facade |
| `c_globals` | H | Memory (reads and writes through the facade) | unchanged | reads through the facade (same `Env` / `eval_var` path as `c_locals`) |
| `read_c_value` | H | Memory (reads and writes through the facade) | unchanged | reads through the facade (same path) |
| `write_c_value` | H | Memory (reads and writes through the facade) | unchanged | writes through `poke` |
| `write_c_value_row` | H | Memory (reads and writes through the facade) | unchanged | writes through `poke` (same path as `write_c_value`) |
| `c_variables_status` | H | none | unchanged | host-side |
| `c_children` | H | Memory (reads and writes through the facade) | unchanged | reads through the facade |
| `c_children_row` | H | Memory (reads and writes through the facade) | unchanged | reads through the facade (same path as `c_children`) |
| `source_location` (addr) | H | none | unchanged | host-side lookup |
| `source_location` (no argument) | H | none | unchanged | host-side; Stage B reads the PC through `registers()`, not `emu.cpu()` (`debugger_source.cpp:651`) |
| `source_location` (space, addr) | H | none | unchanged | host-side lookup |
| `effective_page` | H | MmuSlots | the slot's `effective_page` | local reads `emu.mmu().get_effective_page` (the slot's page, not the fetch prediction; `debugger_source.cpp:607`): guarded |
| `source_page` | H | MmuSlots | the slot's `space`; without MmuSlots `cpu()` | local keeps `predict_fetch`; plan section 11 "Overlays in the fetch identity" |
| `load_program_sidecars` | H | none | unchanged | host-side; arms local verification windows: guarded |
| `source_step` | M | Into/Over/Out: InstructionStep; Back, ReverseContinue: Rewind (L) | `Unsupported` (R2.4 serves Into/Over/Out; Back and ReverseContinue stay local) | strategy is host-side; the single steps are target primitives |
| `attach` | H | none | unchanged | host-side client list; the local arm bits mirror into `DebugState` guarded |
| `detach` | H | none | unchanged | same |
| `set_listener` | H | none | unchanged | host-side |
| `add_service` | H | none | unchanged | host-side |
| `remove_service` | H | none | unchanged | host-side |
| `pump` | H | none | unchanged | drains services, compacts the event table; its hint and drain arm read local `DebugState` (`ds().paused()`, `debugger_session.cpp:528/576/595`): guarded |
| `stop_policy` | H | none | unchanged | host-side |
| `set_stop_policy` | H | none | unchanged | host-side |
| `set_live_raster` | H | none | unchanged | host-side; the local mirror is guarded |
| `live_raster` | H | none | unchanged | host-side flag, read from local `DebugState` (`ds().live_raster()`, `debugger_session.cpp:627`): guarded, a non-local facade keeps its own value in `Impl` |
| `attached` | H | none | unchanged | host-side flag, read from local `DebugState` (`ds().attached()`, `debugger_session.cpp:641`): guarded, same |
| `log` | H | none | unchanged | host-side listener fan-out |
| `set_loop_driver` | L | local machine | `Unsupported` | the loop owner is a local construct |
| `on_cold_boot_begin` | L | local machine | `Unsupported` | local cold-boot machinery |
| `on_cold_boot_done` | L | local machine | `Unsupported` | same |
| `state_revision` | H | none | unchanged | host-side counter |
| `capabilities` | H | none | `kNonLocalServed` intersected with the target's set | ADDED (Q1), TGT-02; direct value |
| `why_unavailable` | H | none | the section 11 reason for the capability; "" when available | ADDED (Q1), TGT-02 |
| `target_info` | H | none | the target's `TargetInfo`, verbatim | ADDED (Q1), TGT-03 |
| `register_validity` | H | none | the target's mask, bits above `Count` dropped | ADDED (Q1), TGT-04 (the register validity mask) |
<!-- gh513-inventory-end -->
