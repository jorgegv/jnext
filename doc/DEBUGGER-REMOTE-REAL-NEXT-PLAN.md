# Remote debugging of another jnext and of a real Next: execution plan (epic #486)

Status: **DRAFT for owner review** (2026-11-17). Nothing in this plan is started. When the
owner approves it, its tasks become the sub-issues of #486, on the epic branch
`epic486-remote-target`; each sub-issue branches off it and merges back into it, and the epic
reaches `main` in one merge (CLAUDE.md, merge protocol step 1).

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

Each line is sized to be one sub-issue. IDs are stable; the detail is in §4 to §10.

### Phase R0: foundations (no user-visible change)

- [ ] **R0.1** Target-seam inventory: classify every `Debugger` verb (machine / host-side / local-only); propose `debug/target.h` and the facade additions for owner approval
- [ ] **R0.2** DZRP client framing: command encoder and response/notification parser in `dzrp_frame.*`, cross-checked against the server's parser
- [ ] **R0.3** DZRP client session core (`DzrpClient`): CMD_INIT/CLOSE, sequence numbers 1..15, pipelining window, timeouts, NTF_PAUSE dispatch, remote profile detection
- [ ] **R0.4** Client links: outbound TCP over `esp::make_socket_transport`, and an in-memory link pair for the suites

### Phase R1: the target interface in the backend

- [ ] **R1.1** `Target` interface and `LocalTarget`: the machine half of `Debugger::Impl` behind it, behaviour-neutral, bench A/B
- [ ] **R1.2** Capability model: `Debugger::capabilities()`, a reason per missing capability, register and run-state validity
- [ ] **R1.3** Qt greying: actions, panel placeholders and unknown registers driven by capabilities; fake-capability Qt suite
- [ ] **R1.4** Debugger window retargeting: one window, Local or Remote target, title and status bar, caches reset on a switch

### Phase R2: remote jnext over DZRP

- [ ] **R2.1** `DzrpTarget` inspection: registers, CPU memory, banks, slots, NextREGs, sprites; stop-time snapshot cache with pipelined refresh
- [ ] **R2.2** Run control and execute breakpoints (normal mode, CMD 40/41); conditions, handlers and hit counts evaluated by the client
- [ ] **R2.3** Watchpoints over CMD 42/43 as `Mem` subscriptions
- [ ] **R2.4** Instruction stepping by temporary breakpoints: one successor function, oracle-tested against jnext's Z80 core
- [ ] **R2.5** Connect and disconnect in the GUI: Debug > Remote Target dialog, errors, lost link, reconnect
- [ ] **R2.6** CLI and Preferences: `--remote-target`, `remote.*` keys in `config_keys.cpp`, man page, `make cli-check`
- [ ] **R2.7** Loopback parity suite: the remote facade over an in-process `DzrpServer` against `LocalTarget`, same programs, same answers
- [ ] **R2.8** Functional rows: `remote-jnext-func` (test probe over TCP) and `remote-qt-func` (GUI started with `--remote-target`)
- [ ] **R2.9** Docs: user guide page, developer guide chapter, FEATURES.md

### Phase R3: source-level debugging on a remote target

- [ ] **R3.1** Program and sidecars for a remote target; fetch identity from the MMU slots
- [ ] **R3.2** Build-match check at connect and after a load: target code bytes against the program file
- [ ] **R3.3** Source line stepping (Into, Over, Out) by exit-point breakpoints
- [ ] **R3.4** Call stack by stack walking (assembler return addresses, C frame pointers); Step Out by return address
- [ ] **R3.5** C variables on a remote target; bulk reads where the target advertises them
- [ ] **R3.6** Tests and docs for remote source-level debugging

### Phase R4: remote Next with dezogif_ng (proof of concept)

- [ ] **R4.1** dezogif_ng profile: static command set, never-unsupported guard, IM and IFF unknown, reserved banks
- [ ] **R4.2** Simple-mode breakpoints (CMD 13/14): set before continue, restore at stop, continue off a breakpoint, forbidden addresses
- [ ] **R4.3** Program upload: a NEX onto the target (CMD_WRITE_BANK or WRITE_BANK_MEM, SET_SLOT, SET_REGISTER) (owner question Q5)
- [ ] **R4.4** Serial link: host serial port (POSIX termios, Win32 COM), 921600 8N1, the 0xA5 preamble (owner question Q3)
- [ ] **R4.5** Emulated-rig rows: the real dezogif_ng ROM in jnext, WiFi over the emulated ESP-01 and UART over the joy-port pty (owner question Q4)
- [ ] **R4.6** Hardware validation on the owner's KS2, with a latency record (owner)
- [ ] **R4.7** Docs: "Debugging a real Next" user guide page, man page, developer guide

### Phase R5: proof-of-concept verdict (owner decision gate)

- [ ] **R5.1** PoC report: what works, what is missing, measured costs, the shim options
- [ ] **R5.2** Owner decision: a custom shim or not, and its scope. No shim code before it

### Phase R6: custom shim (only if R5.2 says yes; placeholders)

- [ ] **R6.1** Import dezogif_ng's sources under jnext's GPL, MIT notices kept, licensing record
- [ ] **R6.2** Build the shim in the tree, reproducibly, versioned in lockstep with jnext
- [ ] **R6.3** The standard DZRP 2.2.0 commands the shim lacks (24, 25/26, 28, 40-43 where feasible)

---

## 1. Sources and evidence

Every claim about code cites a path and a symbol in the tree named; line numbers are as of the
date above.

| Source | Where | State read |
|---|---|---|
| jnext | this repository | `main` @ `ad916bc6d` (v1.3.5) |
| dezogif_ng | `~/src/spectrum/dezogif_ng` | `eb58524`, build `0026`, MIT since `4f988b9` |
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
  `video_panel.cpp` and `debugger_window.cpp` mention `Emulator` only in comments). Across
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
  (`cli_options.h:588`). dezogif_ng's own bench (`test/run-dzrp-stub.sh`) runs headless jnext
  with the WiFi ROM installed as the Multiface ROM, raises the NMI with `--delayed-nmi`
  (`cli_options.h:782`) and talks DZRP to `127.0.0.1:11000`.
- `--sdcard-file-add FILE --sdcard-file-dest PATH` (`cli_options.h:300`) copies a file onto a
  card and exits (`main.cpp`, the `sdcard_file_add_count > 0` arm returns after the copy), so
  installing a ROM on a scratch card needs no mtools.
- `test/dzrp/` holds dezogif_ng's Python DZRP client and conformance suite, copied at
  `709ae7d` (`doc/testing/DZRP-VALIDATION.md`).

### 2.4 dezogif_ng, read from its source

| Fact | Evidence |
|---|---|
| It is the Multiface ROM (`enNextMf.rom`), entered by the NMI button or, for a PC-initiated Pause, by a Copper-raised NMI | README; `doc/ZXNEXT-REMOTE-DEBUG-STUB.md` §2, §4.3 |
| Answers **DZRP 2.1.0**, machine 4, name `"dezogif "` + version | `src/commands.asm:26-35`, `cmd_init.response` |
| Serves commands **1 to 23** only through its jump table, plus 40 (answers id 0 = refused) and 41 (acknowledged) | `commands.asm` `cmd_jump_table` (`:41-65`), `get_cmd_pointer.not_supported` (`:117-126`), `cmd_add_breakpoint` |
| **No** 24 (`GET_SUPPORTED_COMMANDS`), 25/26 (bank memory), 27/28, 39, 42/43 (watchpoints), 50/51 (state) | same; `:74-77` "not supported, and silent" |
| **An unsupported command ends the session**: `cmd_not_supported` sends no reply and jumps to `drain_main`, which falls into `main`, which "RE-INITIALISE[S] THE DEBUGGEE'S SAVED STATE" | `commands.asm` `cmd_not_supported`; `src/main.asm:117-147` |
| Breakpoints are client-owned `RST 0` patches: CMD 13 sets a list and returns the original bytes, CMD 14 restores them (only where the byte is still 0xC7, spec) | `cmd_set_breakpoints`, `cmd_restore_mem`; `doc/DEZOG-BREAKPOINTS-DESIGN.md` §1, §3 |
| CMD_CONTINUE takes up to two temporary breakpoints, patched by the stub | `cmd_continue` |
| `CMD_PAUSE` only acknowledges; the stop itself comes from the Copper-NMI poll while running | `cmd_pause` banner |
| Sprite attributes and patterns (18/19) are "answered but empty"; palette and clip (16/17) are served | `cmd_jump_table` comments `:60-61` |
| Reserves 8K banks 91 (loopback), 92 (temp), 94 (MAIN, refused by CMD_WRITE_BANK); runs MAIN in slot 7 and uses slot 6 as a swap window while stopped | `src/constants.asm` `LOOPBACK_BANK`, `TMP_BANK`, `MAIN_BANK`, `MAIN_SLOT`, `SWAP_SLOT`; `cmd_write_bank` banner |
| Reports slot 7 from its backup and slot 0 as ROM while stopped | `cmd_get_registers`; `SLOT_BACKUP` struct comment |
| Patches the 48K ROM into the Alt ROM (bytes 0x0000-0x0007 and 0x0066-0x0073) so ROM code can be stepped | `src/altrom.asm` `copy_modify_altrom` |
| UART build: 921600 baud on the joy port, each message preceded by 0xA5 | `constants.asm` `BAUDRATE`; `transport_uart.asm` `TRANSPORT_MESSAGE_START` |
| WiFi build: TCP server on port 11000 via the ESP-01, link up to 460800 baud, about 6.6 ms round trip and 20 KB/s | README (doc) |
| PC Pause works over WiFi, and over the cable on joy port 2 only; the serial break "has not run on hardware" | README (doc); dezogif_ng #43 |
| A program that uses the Copper loses the async break unless it carries the two instructions itself; the "C" key turns the break off | README; `doc/ASYNCHRONOUS-BREAK-DESIGN.md` §0.1 (doc) |
| Needs core 03.01.10 or later (stackless NMI) and a Multiface-enabled config | README (doc) |

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
- DZRP 2.2.0 makes `CMD_GET_SUPPORTED_COMMANDS` mandatory **for remotes that answer 2.2.0**;
  a 2.1.0 remote such as dezogif_ng has no way to say what it supports.

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
                                            |  link: TCP (R0.4) | serial (R4.4) | memory pipe (tests)
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
- **Profiles, not probing** (D4). `CMD_INIT`'s reply names the remote. `"jnext v..."` at 2.2.0
  gets the command set from `CMD_GET_SUPPORTED_COMMANDS`; `"dezogif ..."` at 2.1.0 gets a fixed
  set (§2.4), and the client never sends a command outside it, because one unsupported command
  resets the stub's session. Any other remote is refused with a message naming it (D14).
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
  proposed `src/debug/target.h` (published, std headers only, so `test/lint-debug-headers.sh`
  keeps passing) plus the facade additions: a constructor or factory over a `Target`,
  `capabilities()`, `target_info()` (kind, remote name and version, link text) and the register
  validity mask.
- **Files.** This document; `src/debug/target.h` (declarations only);
  `doc/design/debug-subsystem/b0-cap-traceability.md` gets the new CAP rows.
- **Tests.** None beyond compiling the header in the lint (`lint-debug-headers` row).
- **Acceptance.** Every method of `debugger.h` appears once in the table with its class and a
  reason; the owner approves the header additions (Q1).
- **Depends on.** Nothing. Blocks R1.1.

### R0.2 Client framing

- **Design.** Add to `src/remote/dzrp/dzrp_frame.{h,cpp}` the inverse pair: `encode_command(seq,
  id, payload)` and an incremental `ResponseParser` that yields responses (seq 1..15 in the low
  four bits) and notifications (seq 0, first byte the id), with the same rules
  `FrameParser` enforces (16 MiB cap, chunk timeout from the last byte that advanced the frame).
  A `swallow_until(0xA5)` pre-stage is a parser option for the serial dezogif profile (R4.4),
  built here so the parser has one shape.
- **Tests.** New suite `dzrp_client_test`, rows `DZC-01..`: round trips through the
  server's `FrameParser` and `encode_response()` (both directions, every length edge: 0, 1,
  2^16, the cap), truncated and stalled frames, a notification interleaved between responses,
  the 0xA5 pre-stage with leading zero runs. Declared in `test/unit-tests.conf` with its count.
- **Acceptance.** The server's parser accepts every command the encoder builds and the client
  parser accepts every response the server's encoder builds, byte for byte.
- **Depends on.** Nothing.

### R0.3 Client session core

- **Design.** `jnext::remote::dzrp::DzrpClient` (`src/remote/dzrp/dzrp_client.{h,cpp}`): owns
  a link, sends `CMD_INIT` (version 2.2.0, name `"jnext v<version>"`), parses the reply into a
  `RemoteInfo{name, version, machine}` and selects a profile (D4): `jnext` (needs >= 2.2.0;
  then `CMD_GET_SUPPORTED_COMMANDS`), `dezogif` (2.1.0 fixed set), anything else refused. Keeps a
  pipelining window (configurable; default 15 for jnext, 1 for dezogif until R4.6 measures
  otherwise), a per-command timeout, and a queue of NTF_PAUSE notifications. **Every send goes
  through one gate that refuses a command outside the profile's set** (the never-unsupported
  rule), and that refusal is a client bug logged at error level.
- **Tests.** `dzrp_client_test` rows over a scripted peer: version gate, each profile, unknown
  remote refused, sequence wrap 15 -> 1, a reply with a wrong sequence number (protocol error,
  link closed), timeout, NTF_PAUSE between a command and its reply, CMD_CLOSE on teardown. A
  row asserts the gate: with the dezogif profile, asking for command 24, 25 or 42 sends nothing.
- **Acceptance.** All rows green; against a real `DzrpServer` over the memory pipe (R0.4) the
  session opens and closes and the server's log shows one attach and one detach.
- **Depends on.** R0.2, R0.4.

### R0.4 Client links

- **Design.** A `ClientLink` interface (`src/remote/client_link.h`): `open`, `read`, `write`,
  `poll(wait_ms)`, `state`, `describe()`. Three implementations: `TcpLink` over
  `esp::make_socket_transport()` with a client `AddressPolicy` (loopback and private ranges
  allowed: the user names the host), `MemoryLinkPair` for the suites (two ends of an in-memory
  pipe, the second end handed to a `remote::Server` through `FakeListener`,
  `src/remote/fake_transport.h`), and later `SerialLink` (R4.4). No new socket code (D13).
- **Tests.** `remote_transport_test` gains `RLK-` rows: connect refused, connect timeout, peer
  close, partial writes, the memory pair against `remote::Server`. Windows: the TCP rows run in
  `make unit-test-win` (the esp twin is already there).
- **Acceptance.** Rows green in `make unit-test`, `unit-test-sdl`, `unit-test-win`.
- **Depends on.** Nothing.

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
- **Depends on.** R0.1 (owner-approved header).

### R1.2 Capability model

- **Design.** `enum class Capability` (published, in `target.h`) with one bit per row of §11's
  table, `Debugger::capabilities()`, `Debugger::why_unavailable(Capability) -> std::string`,
  and a `RegisterValidity` mask beside `registers()` (IFF1/IFF2, IM, HALT, MEMPTR/Q) and a
  validity flag in `RunState` for `cycle` and `frame`. `LocalTarget` reports everything.
- **Tests.** Backend suite rows: local reports all; a fake target reporting none makes every
  machine verb return `Unsupported` (one row per verb class) and never touches memory.
- **Acceptance.** No verb of a remote facade can silently succeed for a capability the target
  lacks.
- **Depends on.** R1.1.

### R1.3 Qt greying

- **Design.** Each `QAction` in `DebuggerWindow` (`src/debugger/debugger_window.cpp`, the Debug,
  Source, Trace, Coverage, Rewind, Script, Breakpoints and Watches menus) and each panel names
  the capability it needs. `DebuggerWindow::apply_capabilities()` disables the actions (tooltip
  "Not available on <target>: <reason>") and shows a placeholder page in a panel that cannot be
  served (the reason, not an empty table). The CPU panel shows `?` for an invalid register;
  the Call Stack panel marks a reconstructed stack (R3.4).
- **Tests.** New Qt suite `debugger_capabilities_test` (`?`-gated `qt+dbg`), rows `RTQ-`: for
  each capability, a fake target lacking only it greys exactly the actions and panels the
  table says, and restoring it ungreys them.
- **Docs.** User guide `06-debugger/panels/index.md`: what a greyed panel means.
- **Acceptance.** §11's table and the suite agree row for row.
- **Depends on.** R1.2.

### R1.4 Debugger window retargeting

- **Design.** One debugger window, one target at a time (owner question Q8). `DebuggerManager` owns an
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
  for the MMU panel from NextREGs where the Next exposes them. Snapshot cache per D3.
- **Tests.** In `remote_target_test` (R2.7) rows `RTG-`: each primitive against `LocalTarget`
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
- **Tests.** `RTG-` rows: breakpoint in RAM and in ROM (a jnext target can), page-qualified,
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

- **Design.** Debug > Remote Target... opens a dialog: link (TCP, later serial), host and port
  (or device and baud), connect timeout, response timeout; Connect, Disconnect. On connect the
  window retargets (R1.4) and pauses the remote (DeZog's convention; the jnext server's
  `CMD_INIT` pauses already, dezogif_ng is stopped by construction). Errors are a status-bar
  message plus the log line. A lost link freezes the panels with a banner and offers
  Reconnect; it never takes the local emulator down.
- **Tests.** Qt suite rows over a `MemoryLinkPair`: connect, refuse (unknown remote), lose the
  link mid-run, reconnect.
- **Docs.** User guide (R2.9).
- **Acceptance.** Closing the remote jnext while connected leaves the local jnext running and
  the window back on a usable state within the response timeout.
- **Depends on.** R1.4, R2.2.

### R2.6 CLI and Preferences

- **Design.** One enabler flag, `--remote-target URI` with `tcp:HOST:PORT` (and from R4.4
  `serial:DEVICE[@BAUD]`), which opens the debugger window on that target at start. The
  Preferences "Debugger" tab (`src/gui/preferences_dialog.cpp`, `build_debugger_tab()`) gains a
  "Remote target" group holding the dialog's fields. One row per field in
  `src/platform/config_keys.h` `KEYS[]` (section `remote`): `link` (Word `tcp|serial`), `host`
  (Text), `port` (Int 1..65535), `serial_device` (Path), `serial_baud` (Int), `connect_timeout_ms`
  (Int), `response_timeout_ms` (Int), all `QT`, `needs_debugger = true`, the first five with
  `flag = "--remote-target"`. `cli_options.h` gains the `--remote-target` entry; headless and the
  SDL frontend refuse it with a message (there is no debugger window to drive).
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
  remote one and compares: registers, memory, slots, breakpoints hit, step landings, C values
  (R3.5). The server is the "fake shim" the epic asks for on the jnext side, and it is not a
  fake: it is the shipped server.
- **Tests.** Rows `RTG-`, declared in `test/unit-tests.conf` (Linux, SDL-only and Windows
  configurations: no Qt).
- **Acceptance.** Every "remote jnext: yes" capability of §11 has at least one parity row.
- **Depends on.** R2.1 to R2.4.

### R2.8 Functional rows

- **Design.** A test-only executable `remote_target_probe` (built in `test/`, like
  `fuse_z80_test`; D10) connects a remote facade to a URI and runs a named scenario, printing
  rows. `remote-jnext-func`: start a headless jnext with `--dzrp-port 0` on a demo NEX, read the
  logged port, run the probe (connect, registers, breakpoint, continue, hit, step, C variable,
  disconnect). `remote-qt-func`: `xvfb-run -d` (with `-noreset`, HS-72/HS-89) a GUI jnext with
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
- FEATURES.md entry. `make docs-check` green.

---

## 7. Phase R3: source-level debugging on a remote target

Goal: the Source tab, line stepping, the call stack and C variables work on a remote target,
over plain DZRP.

### R3.1 Program and sidecars; fetch identity

- **Design.** The user names the program file of the remote session (dialog field, and the CLI
  through `--remote-target`'s program part or `--set remote.program=FILE`, a new key). The
  existing `Debugger::load_program_sidecars(path)` loads its map, SLD, `.lis`, `.cdb` as for a
  local load. The fetch identity on a remote is the MMU's: `Page{p}` for a RAM slot, `Rom{}` for
  bank 0xFF; DivMMC, Multiface, Alt ROM and Layer 2 overlays are invisible over DZRP and are
  reported as such (`source_page()` returns the slot's page; §11).
- **Tests.** `RSRC-` rows in `remote_target_test`: a z88dk banked program's lines resolve on
  the remote as on the local.
- **Depends on.** R2.7.

### R3.2 Build-match check

- **Design.** At connect and after an upload, compare the target's bytes with the program
  file: for a NEX, its banks (through `NexLoader`, `src/core/nex_loader.h`) sampled at every
  source-mapped code range; for a bare binary with a map, the bytes the map's code ranges cover.
  A mismatch shows a banner ("the program on the target is not the build these sources
  describe") and disables source features until the user confirms. Reads use the snapshot
  burst, so the check costs a bounded number of round trips.
- **Tests.** `RSRC-` rows: match, one byte changed, wrong bank mapped.
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
- **Depends on.** R3.4.

### R3.6 Tests and docs for remote source debugging

- Functional: extend `remote-jnext-func` (or a sibling `remote-source-func`) with a source
  step and a C variable on a z88dk demo.
- Docs: user guide `06-debugger/functions/14-source-level-debugging.md` and the new page; the
  developer guide's `07-source-level-debugging/05-breakpoints-steps-history.md` and
  `10-c-frames-and-values.md` gain the remote strategy.

---

## 8. Phase R4: remote Next with dezogif_ng (proof of concept)

Goal: the same client debugs a program on a real Next running dezogif_ng, and CI proves the
protocol path against the real stub running in jnext.

### R4.1 dezogif_ng profile

- **Design.** The `dezogif` profile (`dzrp_client.cpp`): commands 1-23 except 22, plus nothing
  above; breakpoints in simple mode (R4.2); IM, IFF1, IFF2, HALT invalid; no watchpoints, no
  bank memory, no state; sprite attributes and patterns unavailable (the stub returns zeros);
  pipelining window 1 until R4.6 measures more; reserved banks 91, 92 and 94 refused for writes
  and marked in the memory views; slot 7 reads come from the stub's backup and are correct for
  the debuggee (`cmd_get_registers`). Capabilities per §11.
- **Tests.** `dzrp_client_test` rows with a scripted dezogif peer (the fake shim for the
  hardware dialect): handshake, the gate refuses every command outside the set (a test that a
  stray 24 would be caught before it is sent, because the real stub would answer nothing and
  reset).
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
  (`calcStepBp`). A breakpoint set while running is patched at once and recorded. A NTF_PAUSE
  at a temporary that is also a user breakpoint is reported as a step (DeZog's correction).
- **Tests.** Scripted-peer rows for each rule; emulated-rig rows (R4.5) for the real stub.
- **Acceptance.** After every stop the cache and the target hold no 0xC7 the program did not
  write (row reads the patched addresses after the restore).
- **Depends on.** R4.1, R2.4.

### R4.3 Program upload

- **Design.** Load a NEX onto the target the way DeZog does: each bank by `CMD_WRITE_BANK`
  (dezogif) or `CMD_WRITE_BANK_MEM` (jnext), slots by `CMD_SET_SLOT`, SP and PC by
  `CMD_SET_REGISTER`, then the session is stopped at the entry point. The NEX is parsed by
  `NexLoader` (`src/core/nex_loader.h`). Refuses a NEX that needs bank 94 (MAIN) or 91/92 on
  dezogif. The same uploader serves a jnext target, so "Load program" works on both.
- **Tests.** `RTG-` rows on the loopback (jnext target): uploaded memory equals a local
  `--load` of the same NEX at its entry point; scripted-peer row for the dezogif command
  sequence.
- **Depends on.** R4.1. Owner question Q5 decides whether it is in the PoC.

### R4.4 Serial link

- **Design.** `SerialLink : ClientLink` (`src/remote/serial_link.{h,cpp}` plus a Windows twin):
  POSIX termios raw 8N1, no flow control, arbitrary baud through the platform's custom-rate
  call where the standard set lacks 921600; Win32 `CreateFile("\\\\.\\COMn")` with
  `SetCommState`. The dezogif serial profile enables the 0xA5 pre-stage (R0.2). Pause over the
  cable is offered only when the stub reports joy port 2 (the README's "PC break: ready") and
  is marked unverified on hardware (dezogif_ng #43); otherwise the status bar says to press
  the NMI button. No child process is spawned, so the LANG=C rule does not apply.
- **Tests.** `RLK-` rows on a pty pair (`# os: posix`); the Windows twin compiles in
  `make unit-test-win` and its behaviour is tested only on hardware (stated in the row's
  comment and in §12).
- **Depends on.** R4.1. Owner question Q3 decides whether it is in the PoC.

### R4.5 Emulated-rig rows

- **Design.** The real dezogif_ng running in jnext, driven by the probe: a scratch card copy
  (`private-sd`), the ROM installed by `jnext --sdcard COPY --sdcard-file-add ROM
  --sdcard-file-dest /MACHINES/NEXT/enNextMf.rom --sdcard-file-force`, then headless jnext with
  `--esp --esp-listen-address 127.0.0.1 --delayed-nmi ...` (WiFi ROM) and the probe on
  `tcp:127.0.0.1:11000` (row `remote-dezogif-wifi-func`, tagged `serial` because the guest's
  port is fixed at 11000); and the UART ROM with `--joy-uart-pty`, the probe on the logged pty
  (row `remote-dezogif-uart-func`, `os=linux`). Scenario: connect, registers, memory and slots,
  breakpoint, continue, hit, step, pause of a running program (WiFi), disconnect. The NextZXOS
  image is the one jnext provisions; no image is committed or uploaded, in any form.
- **Tests.** The two rows. Where the ROM comes from is owner question Q4.
- **Acceptance.** Both rows green as targeted rows; they fail when the client sends an
  unsupported command (mutation: drop the gate, the stub resets and the row goes red).
- **Depends on.** R4.2; R4.4 for the UART row.

### R4.6 Hardware validation (owner)

- A manual protocol in `doc/testing/REMOTE-TARGET-VALIDATION.md` (the shape of
  `DZRP-VALIDATION.md`): the R4.5 scenario on the KS2, over WiFi and over the cable as
  available, with a C program and source stepping; record round-trip time, stop-refresh time,
  Step Over and line-step wall time, and the largest pipelining window that holds. Results set
  the dezogif profile's window and timeouts.

### R4.7 Docs for the real Next

- User guide page "Debugging a real Next" (install dezogif_ng, WiFi or cable, what greys out,
  the Copper and joystick caveats from §2.4); man page for `serial:` URIs; developer guide: the
  dezogif profile and the simple-mode dance. FEATURES.md entry.

---

## 9. Phase R5: proof-of-concept verdict

- **R5.1** A report appended to this document: what the PoC does on hardware, what is missing
  against §11, measured costs, and the shim options (below).
- **R5.2** The owner decides. Options to put to the owner, all within the 2026-11-10 decision
  (no DZRP extensions, no own protocol): (a) stop at dezogif_ng; (b) contribute the missing
  standard DZRP 2.2.0 commands (24, 25/26, 28) to dezogif_ng upstream, which the client already
  uses through the bitfield; (c) a jnext shim in this repository (R6).

## 10. Phase R6: custom shim (placeholders, gated by R5.2)

- **R6.1** Import: dezogif_ng's sources, distributed under jnext's GPL with the MIT notices of
  maziac (2020) and jorgegv (2026) kept (`LICENSE`, `NOTICE`); a licensing note beside
  `doc/legal/`.
- **R6.2** Build: dezogif_ng is **sjasmplus** source (`Makefile`, README "Requirements"), not
  z88dk as the issue body says. Either sjasmplus becomes a pinned build dependency (owner
  approval, PULL-REQUEST-PROTOCOL's no-new-dependency rule) or the sources are ported to
  z88dk's assembler. Reproducible output (dezogif_ng already has `check-reproducible`).
  Version in lockstep with `version.yaml`.
- **R6.3** Features, standard DZRP only: `CMD_GET_SUPPORTED_COMMANDS`, bank memory, block
  reads, normal-mode breakpoints if the 216-byte budget of the WiFi build allows
  (`DEZOG-BREAKPOINTS-DESIGN.md` §2), WiFi, NMI and Copper activation as dezogif_ng has them.

---

## 11. What degrades on a remote target

"Remote jnext" is another jnext over DZRP 2.2.0 (`DzrpServer`). "Remote Next" is a Next running
dezogif_ng build 0026 (DZRP 2.1.0). "Greyed" means the action is disabled with the reason in its
tooltip, or the panel shows a placeholder with the reason (R1.3).

| Capability | Local jnext | Remote jnext | Remote Next (dezogif_ng) | When unavailable |
|---|---|---|---|---|
| Run, Pause | yes | yes (CMD 6/7, NTF_PAUSE) | Run yes; Pause only with the async break: WiFi ROM yes, cable on joy port 2 yes but unverified on hardware, else the NMI button; not for a Copper-using program without the two instructions | Pause stays enabled; on timeout the status bar says "press the NMI button" |
| Step Into / Over / Out (instruction) | exact (`debugger_step`) | by temporary breakpoints, one round trip per step; Out by return address or DeZog's loop | same; not in ROM, not at 0x0000-0x0007 / 0x0066-0x0073; RST 08 steps over | step actions disabled at a forbidden PC, with the reason |
| Run to Cursor | yes | yes | yes, cursor in RAM only | disabled on a ROM or forbidden line |
| Run to end of frame / scanline, run to cycle | yes | no (no time in DZRP) | no | greyed: "cycle-exact: local only" |
| Step Back, Frame Back, rewind slider, source Back, Reverse Continue | yes | no | no | Rewind menu and history actions greyed |
| Execute breakpoints | anywhere | anywhere, ROM included, page-qualified (CMD 40/41) | RAM only (RST 0 patches, CMD 13/14), patched only while running | the gutter refuses the click with the reason |
| Conditions, log actions, `once`, hit counts | evaluated in the emulator | evaluated by the client at each hit, auto-continue (a round trip per hit) | same | enabled; tooltip states the cost |
| Read / write watchpoints | yes | yes (CMD 42/43) | no | Watch-breakpoint actions greyed |
| Port, NextREG-write, interrupt, frame, Copper, DMA events | yes | no | no | not offered (script-only today) |
| Registers PC SP AF BC DE HL IX IY and shadows, I, R | yes | yes | yes | n/a |
| IM | yes | yes | unknown | CPU panel shows `?` |
| IFF1, IFF2, HALT, MEMPTR | yes | unknown (not in DZRP) | unknown | CPU panel shows `?` |
| Register write | yes | yes | yes | n/a |
| Memory read / write, CPU view | yes | yes | yes, while stopped | n/a |
| Memory read while running | yes (4 Hz refresh) | yes, at the target's next tick | no (the stub is not executing) | panels show the last stop, marked stale |
| Physical page and ROM image views | yes | yes (CMD 25/26, ROM bank 0xFF) | no in the PoC (Q7) | the Memory panel's page view greyed |
| Overlays in the fetch identity (DivMMC, Multiface, Alt ROM, Layer 2 read map) | yes | no (DZRP sends the slot's bank only) | no; ROM slots show the stub's patched Alt ROM | source lookup uses the slot's page; Disassembly labels the identity "MMU only" |
| MMU slots | yes | yes | yes (slot 7 from the stub's backup) | n/a |
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
| Load program | yes | upload (R4.3) | upload (R4.3) | n/a once R4.3 lands; greyed before |
| Reset | yes | no (no reset in DZRP) | no | greyed |
| Machine time (T-states, frame, raster position) | yes | no | no | status fields show `?` |
| Build match against the program file | n/a | yes (R3.2) | yes (R3.2) | banner on mismatch |

---

## 12. Risks

| Risk | Consequence | Mitigation |
|---|---|---|
| The frozen header needs additions | R1 cannot start without them | R0.1 lists them exactly; owner decision Q1 |
| Latency: synchronous panels over a 6.6 ms (WiFi, doc) or 20 ms (a Qt jnext tick) round trip | a slow or stalling window | the stop-time pipelined burst; misses at 256-byte granularity; measure in R4.6 and R2.8; widen the burst before making panels asynchronous |
| One unsupported command resets dezogif_ng's session (verified, `main.asm:117-147`) | a client bug loses the user's session | the send gate (R0.3) and its rows; the emulated-rig mutation row (R4.5) |
| A patched 0xC7 reaches the disassembler | wrong instruction lengths, wrong step breakpoints | restore before the cache is filled (R4.2), asserted by a row |
| Self-modifying code over a breakpoint | CMD 14 leaves a changed byte alone (spec); a breakpoint silently disappears | documented in the user guide; the breakpoint panel re-reads the byte at the next stop |
| A wrong successor sends a step running away | lost stop; on dezogif the program runs free | the opcode-table oracle suite (R2.4); Pause where available |
| `JP (C)` and unknowable indirect transfers | no static successor | stop at the instruction and single-step it |
| Stack-walk heuristic finds data that looks like a return address | a wrong frame in the Call Stack | labelled "reconstructed"; C frames use frame pointers |
| CMD_CONTINUE carries two temporaries only | line steps with more exits | transient breakpoints beyond two (R3.3) |
| Pause over the cable never tested on hardware (dezogif_ng #43) | Pause may not work on a cable rig | offered only on joy port 2, labelled unverified, NMI fallback message |
| Windows and macOS serial are not testable on this host | a platform-only failure | compile in `unit-test-win`; hardware check by a tester (R4.6) |
| The emulated ESP binds the guest's fixed port 11000 | parallel rows collide | the row is tagged `serial` |
| dezogif_ng's ROM in CI needs sjasmplus or a binary fixture | a new dependency or a binary in the repo | owner question Q4 |
| dezogif_ng and upstream dezogif move to DZRP 2.2.0 | the static profile becomes wrong | profile keyed on name and version; a 2.2.0 dezogif uses the bitfield like jnext |
| The program on the Next is not the build the map describes | wrong lines, wrong variables | the build-match check (R3.2) |
| Interrupts and the stub perturb the debuggee (R, speed, Copper list, joystick port) | values differ from an emulator session | stated in §11 and the user guide, from §2.4's evidence |

---

## 13. Open questions for the owner

1. **Q1. Frozen header.** Approve adding to `src/debug/debugger.h` a constructor (or factory)
   over a `Target`, `capabilities()`, `why_unavailable()`, `target_info()` and a register
   validity mask, plus the new published header `src/debug/target.h`? (R0.1 lists them.)
2. **Q2. Document location.** The brief put this plan in `doc/`; `doc/README.md` files design
   documents under `doc/design/`. Keep it here, or move it there?
3. **Q3. Link on the KS2.** Which link will you test the PoC on: the WiFi ROM over the ESP-01, or
   the cable on the joy port through a USB serial adapter? WiFi needs no serial code (R4.4 can
   follow the PoC); a cable makes R4.4 part of it.
4. **Q4. dezogif_ng in CI.** For the emulated-rig rows (R4.5): vendor the prebuilt MIT ROMs as
   test fixtures (pinned build number and sha256, with their notice), build them from source
   with sjasmplus (a new pinned dependency), or skip those rows and rely on the scripted peer?
5. **Q5. Upload in the PoC.** A dezogif_ng session normally starts with the client pushing the
   program (R4.3). Is that in the PoC, or is attaching to a program already started on the Next
   (NMI into it) enough for it?
6. **Q6. Other DZRP remotes.** The plan recognises only jnext and dezogif_ng and refuses any
   other remote (CSpect, upstream dezogif). Keep that, or accept any 2.2.0 remote through its
   bitfield, labelled untested?
7. **Q7. Bank views on dezogif_ng.** Grey them out in the PoC (plan), or read a page by a
   temporary `CMD_SET_SLOT` swap that the client undoes?
8. **Q8. One target per window.** While the debugger window shows a remote target, the local
   emulator has no debugger window (it keeps running, its servers keep serving). Acceptable, or
   do you want two windows at once?
9. **Q9. The custom shim's limits.** With DZRP extensions abandoned, a jnext shim could only add
   standard DZRP 2.2.0 commands and the stub-side features dezogif_ng already has. And the issue
   says the shim is built with z88dk, while dezogif_ng is sjasmplus source. Both shape R5.2;
   no answer is needed before then, but they are recorded here so R5 starts from them.

---

## 14. Decisions taken in this plan

| # | Decision | Why | Rejected alternative |
|---|---|---|---|
| D1 | The target seam is inside `jnext_debug`, below the facade; a remote session is a second `Debugger` over a `DzrpTarget` | panels keep `Debugger*` and the 99 methods they call; the host-side half exists once | an abstract facade (about 150 virtuals, every panel and the host-side logic duplicated or rewritten); proxies at panel level |
| D2 | Symbols, sources, C values, frames, conditions and the stepping strategy run in jnext for every target | the 2026-11-11 design; plain DZRP carries only primitives | moving logic to the target (needs extensions, abandoned) |
| D3 | Stop-time snapshot cache with a pipelined burst; misses are bounded blocking reads | keeps the synchronous facade and the panels unchanged | asynchronous per-panel fetch (every panel rewritten) |
| D4 | Remote profiles from `CMD_INIT` (name and version); `GET_SUPPORTED_COMMANDS` only for 2.2.0; never send a command outside the profile | dezogif_ng answers 2.1.0, has no command 24, and resets on an unsupported command (`commands.asm`, `main.asm:117-147`) | probing with command 24 everywhere, as #486's 2026-11-11 item 3 suggests: correct for 2.2.0 remotes, session-destroying on dezogif_ng |
| D5 | Two breakpoint dialects, normal (40/41) for jnext and simple (13/14) for dezogif_ng, never mixed | the jnext server has no 13/14; dezogif_ng refuses 40 (id 0); the spec forbids mixing in a session | implementing 13/14 in the jnext server so one dialect serves both (patching RAM on an emulator, losing ROM breakpoints) |
| D6 | One successor function in `jnext_debug`, tested against jnext's own Z80 core over the whole opcode table | a FUSE-validated oracle exists in the tree | porting DeZog's `calcStepBp` (no oracle; and code is reimplemented, not copied) |
| D7 | Line steps by the statement's exit set, extra exits as transient breakpoints | one round trip per line, as #486 asks | one instruction step per round trip until the line changes (rejected in #486 itself) |
| D8 | Call stack on a remote rebuilt by walking the stack at each stop | the tracker needs every instruction; `c_frames()` already has a no-tracker path | running a tracker on the target |
| D9 | Tests at three levels: in-process loopback against the shipped server, a scripted dezogif peer, and the real dezogif_ng ROM in jnext; hardware by the owner | a fake alone would test our reading of the stub against itself | a fake shim only |
| D10 | A test-only probe executable drives the remote facade in functional rows | tests the shipped client code end to end without new user features | making jnext a DZRP relay (not in scope); driving the GUI with xdotool for protocol rows |
| D11 | TCP first (R0.4), serial in R4.4 | TCP covers remote jnext and dezogif_ng's WiFi build | serial first |
| D12 | The custom shim is a placeholder phase gated by R5.2, standard DZRP only | the 2026-11-10 decision makes it conditional and abandons extensions | planning a shim protocol now |
| D13 | The TCP link reuses `esp::make_socket_transport()` with a client address policy | non-blocking, Windows-twinned, already tested; `jnext_remote` stays Qt-free | new socket code; Qt sockets |
| D14 | Only the jnext and dezogif_ng profiles are recognised | scope read literally ("clients for other emulators" abandoned) | generic DZRP remotes (owner question Q6) |
| D15 | One enabler flag `--remote-target URI`, every other field through `--set remote.*`; Qt only | CLAUDE.md's GUI-setting rule with the smallest flag surface | one dedicated flag per field |
| D16 | Remote verbs a target cannot serve return `Result::Unsupported` and are greyed with a reason | the facade's rule "every verb that can refuse says so" | hiding panels |
