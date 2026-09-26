# Debug subsystem architecture — backend and frontends (epic #276)

> Status: **design converged, not implemented.** Deliverable of
> [#277](https://github.com/jorgegv/jnext/issues/277), gate on
> [#278](https://github.com/jorgegv/jnext/issues/278) (Qt refactor),
> [#12](https://github.com/jorgegv/jnext/issues/12) (DZRP),
> [#280](https://github.com/jorgegv/jnext/issues/280) (ZRCP),
> [#281](https://github.com/jorgegv/jnext/issues/281) (GDB RSP),
> [#26](https://github.com/jorgegv/jnext/issues/26) (DSL, carrying
> [#279](https://github.com/jorgegv/jnext/issues/279)'s requirements) and
> [#20](https://github.com/jorgegv/jnext/issues/20) (record/replay, re-scoped
> in §8). **Needs the independent design review #277 requires before any
> implementation starts.** §12 lists the questions only the owner can answer.
>
> **Revision 1** (2026-09-26). Produced by a design feedback cycle: one backend
> design (`debug-subsystem/backend.md`, v1 → v3) and five frontend designs
> written concurrently against it (`debug-subsystem/qt-frontend.md`,
> `dzrp-frontend.md`, `zrcp-frontend.md`, `gdb-rsp-frontend.md`,
> `dsl-frontend.md`). 73 requirements were sent by the frontends and
> dispositioned (62 accepted, 3 alternative, 6 confirmed, 2 needs-prototype, 0
> rejected); every frontend then mapped onto backend v3 with **zero
> reach-arounds** — qt 38 capabilities used / 17 declined, DZRP 22 / 4+3,
> ZRCP 67 / 7, GDB 21 / 24, DSL 28 / 21. The per-agent files stay as
> appendices and working notes; this document stands alone.
>
> Every claim about the code cites `file:line` in `main @ 974b0ab19`
> (v1.0.44). Numbers are measured and say how (§6). Claims are marked
> **[verified]** (read in the source or measured) or **[inferred]**.

---

## Table of contents

- [1. Goals, non-goals, settled decisions](#1-goals-non-goals-settled-decisions)
- [2. The code as it is](#2-the-code-as-it-is)
- [3. Architecture and build configuration](#3-architecture-and-build-configuration)
- [4. Backend API](#4-backend-api)
- [5. Threading and the out-of-process model](#5-threading-and-the-out-of-process-model)
- [6. The hot path, measured](#6-the-hot-path-measured)
- [7. Frontend projections and the capability matrix](#7-frontend-projections-and-the-capability-matrix)
- [8. The #20 verdict](#8-the-20-verdict)
- [9. Testing strategy](#9-testing-strategy)
- [10. Implementation plan](#10-implementation-plan)
- [11. Out of scope, and what needs a prototype](#11-out-of-scope-and-what-needs-a-prototype)
- [12. Open questions for the owner](#12-open-questions-for-the-owner)

---

## 1. Goals, non-goals, settled decisions

### 1.1 Goals

One frontend-agnostic debugger backend in the emulator process, with an API
sufficient for six consumers: the existing Qt GUI (13 panels + the video
layer view), a DZRP server (DeZog / ZX Basic Studio / `tools/cspect_dzrp`), a
ZRCP server (DeZog's `zesarux` remote, telnet), a GDB-RSP server
(`z88dk-gdb`), the scripting DSL (the event primitive, carrying #279's
bug-hunting vocabulary), and a record/replay recorder. The Qt refactor (#278)
is the first consumer and the proof that nothing observable changes; the
three protocols and the DSL are the sufficiency test the owner set: "three
protocols designed independently by other people all map onto this API"
without reaching around it.

### 1.2 Non-goals

No plugin ABI. No expression language, wire protocol or toolkit type in the
backend. No thread. No wall-clock API. No persistence of state bookmarks
(that is JNS, #27). No change to the frame loop's Task 40 / GH #207 / GH #203
semantics. No new GUI panels. No DeZog protocol extension.

### 1.3 Settled owner decisions (quoted; not re-litigated here)

From the issue records of 2026-09-26:

1. *"There will be no plugin API — no C ABI shared library, no
   dlopen/LoadLibrary host, no scripting binding exposed as a plugin
   interface."* #279's use cases are requirements on the DSL's vocabulary.
   Revisitable only on evidence the DSL cannot express a real use case.
2. *"'Which is the primitive?' is CLOSED in the DSL's favour."*
3. *"#12's protocol is DECIDED, and it is DZRP."* Socket, not serial; a
   partial server negotiated via `CMD_INIT` is legitimate.
4. *"The backend is the union of what jnext can do. Each frontend is a
   projection of that onto what its protocol or its users can express."*
   *"A capability must never be shaped by the poorest adapter that consumes
   it. Equally, no adapter may reach around the backend."*
5. *"Conditions: the backend supports them; the DZRP adapter declines to use
   them."*
6. *"Reverse debugging follows the same lead: we offer the functionality and
   the client decides whether to use it."* *"The upstream DeZog conversation
   is no longer a prerequisite."*
7. *"Deterministic time is the unit, never wall-clock — frames and
   T-states."*
8. *"No functional regression in the GUI."* *"The backend needs its own
   tests, runnable headless, independent of any frontend."*

---

## 2. The code as it is

Measured against the tree, not against `doc/design/EMULATOR-DESIGN-PLAN.md`
§5.10, whose `DebuggerInterface` does not exist [verified: no such symbol in
`src/`].

### 2.1 Control lives in a `Q_OBJECT`

`DebugState` [`src/debug/debug_state.h:259-576`] is already the pure
execution-control half: `paused_`, a `StepMode` [`:255`], the
`BreakpointSet`, the cached hot-path gates `armed_` / `wp_live_` [`:522-530`],
the GH #221 step-off arm [`:549-552`], the data-breakpoint latch [`:569-570`].
But the **verbs** — the behaviour a headless consumer needs — are slots of
`DebuggerManager` [`src/debugger/debugger_manager.h:62-72`]: Step Over's
`is_call_like` → one-shot at `PC + instruction_length` else Step Into
[`debugger_manager.cpp:413-473`]; Step Into = `Emulator::debugger_step()`
(frame-loop aware, runs a HALT out, GH #207) [`emulator.cpp:10496-10597`];
Run to End of Frame / End of Scanline as two raster-arithmetic targets fed to
`run_to_cycle` [`debugger_manager.cpp:475-560`]; the Task 60e corruption gate
as a modal over the pure `ResumeGuard` [`src/debug/resume_guard.h`]. The hot
loop consults `DebugState` at three points [verified]: before the instruction
(`consume_step_off` → `should_break(pc)` → `INTO` → `RUN_TO_CYCLE`)
[`emulator.cpp:9301-9343`], after it (`data_bp_hit()`) [`:9398-9406`], and
inside the shared body for Step Out, gated on
`fetched_opcode_last_execute()` [`:9975-9986`]; rewind modes are consumed
before the loop [`:9251-9260`].

One flag, three unrelated consumers: `DebugState::active()` gates the step
machinery [`:9251`, `:9916`], the render-every-frame hint [`:9552`] **and**
the per-instruction `VideoTiming::advance()` walk [`:10023`] [verified;
design-qt finding]. A client that wants breakpoints and steps but not a live
raster readout cannot have that today.

### 2.2 Inspection is `Emulator*`

33 textual `Emulator*` in 15 headers of `src/debugger/` (13 panel classes × 2,
`VideoLayerView` × 2, `DebuggerWindow` × 2, `DebuggerManager` × 3) [verified;
the brief's "14 panels" is 13 panels + the layer sub-widget]. Through them the
`.cpp` files reach 21 distinct core objects and 16 `Emulator` members
(design-qt §1 inventories all 93 accesses). Two GUI-held pieces of debugger
state the core knows nothing about: the Watches list
[`watch_panel.h:46-52`] and the symbol table [`debugger_manager.h:112-113`].

Two reads perturb what they observe (the Task 40 class):

- **F1.** `Mmu::read()` latches the +3 floating-bus byte on every contended
  read, inside *and outside* `GuestExecutionScope`
  [`src/memory/mmu.h:405-406`, `:514-515`]; a Memory-panel refresh in +3 mode
  rewrites `p3_floating_bus_dat_`. **[verified]**
- NextREG is already handled: panels use `NextReg::peek()`, which exists
  because `read()` runs destructive handlers and logs
  [`src/port/nextreg.h:52-63`] [verified].

### 2.3 Events: per-address, unconditional, one one-shot

`BreakpointSet` [`src/debug/breakpoints.h`]: PC set, `vector<Watchpoint{addr,
type, enabled}>` [`:21-30`], **one** one-shot [`:205-209`], a master switch,
observers (GH #220). Watch matching is a linear scan when armed
[`breakpoints.cpp:106-116`]. No range, no value predicate, no NextREG-write
event, no frame/scanline/cycle event, no physical-page filter, and the latch
carries only the address [`debug_state.h:569-570`] [verified — #279's
description of the gap is accurate]. NextREG writes reach `NextReg::write`
from three callers: port `0x253B` / the `NEXTREG` opcode via the deferred
queue [`emulator.cpp:4746-4770`, flushed at `:10221`] and the Copper with
`active_move_hc() >= 0` only during its call [`src/peripheral/copper.h:120-131`].

### 2.4 Time

- **F2.** `Emulator::frame_num_` is incremented at exactly one site, inside
  `if (rewind_buffer_ && rewind_enabled_ && !replay_mode_)`
  [`emulator.cpp:8466-8468`], so `frame_num()` [`emulator.h:947`] is **0 for
  the whole run unless `--rewind-buffer-size` is given**; `HeadlessApp` and
  `QtApp` each keep a private frame count for `--delayed-*`. **[verified;
  raised by design-dsl]**
- `Emulator::monotonic_tstates()` (frame base + live FUSE counter)
  [`emulator.h:500`, `emulator.cpp:7984-7990`] is the right T-state clock: the
  CPU divisor changes under NR 0x07, so a quotient of master cycles is not
  monotonic across a speed change [design-gdb].
- A paused frame is resumed, not restarted [`emulator.cpp:9264-9296`]; while
  paused the frontends stop calling `run_frame()` altogether
  [`src/platform/frame_sequencer.h` step (6)], so `debugger_step()` is the
  machine's only driver [`emulator.cpp:10502-10506`].

### 2.5 Input injection and capture exist twice

`--delayed-keypress[-frames]` resolves a key name in
`HeadlessApp::key_name_to_matrix()` [`src/platform/headless_app.cpp:209-258`]
and injects via `Keyboard::queue_auto_type` [`:557-569`]; `--delayed-nmi` uses
the GUI's hotkey seam [`:579-590`]; `--delayed-screenshot*` arms
`Renderer::set_layer_mask()` for one frame [`:593-597`] and writes through
`save_screenshot()` [`src/platform/screenshot.h:53`]; a capture due while
paused is an error and a non-zero exit [`src/gui/qt_app.cpp:623-632`].
`QtApp::TickEffects::post_frames()` [`qt_app.cpp:581-632`] implements the same
countdowns a second time. Joystick state is settable as 12-bit vectors
[`src/input/joystick.h:113-117`]; the keyboard matrix is private
[`keyboard.h:182-185`].

### 2.6 State and rewind

`save_state/load_state` over a fixed-width stream [`src/core/saveable.h`,
`emulator.h:776-793`]; snapshots only at frame boundaries
[`rewind_buffer.h:11-16`]; a mid-frame save advances under
`DebugState::SuspendScope` [`debug_state.h:370-412`,
`emulator.cpp:11902-11908`]; `frame_in_progress()` [`emulator.h:191`]; failed
restores latch `last_state_error()` / `state_error_generation()`
[`emulator.h:895-901`]; `step_back` / `rewind_to_frame` / `rewind_to_cycle`
[`:958-966`] refuse under RZX [`:972`]. A benign rewind failure (empty buffer,
trace off, frame out of range) is silent in the GUI today; only corruption
raises the modal [`debugger_manager.cpp:275-295`].

### 2.7 Build

`jnext_debug` (`src/debug/`) is linked by `jnext_core` unconditionally
[`src/core/CMakeLists.txt`], so the backend primitives exist in every
configuration including SDL-only [verified]; `ENABLE_DEBUGGER` gates only the
Qt panels [`CMakeLists.txt:335-337`, `:495-498`]. Two Qt headers sit in the
pure layer: `src/debug/debug_keymap_qt.h` (used by `src/gui/host_chords.cpp`,
`shortcut_capture_button.cpp`, `main_window.cpp`,
`test/debugger/keymap_test.cpp`) and `menu_bar_alt_nav_qt.h`
(`src/debugger/debugger_window.cpp`, `src/gui/main_window.cpp`) [verified];
both header-only, never compiled into `jnext_debug`.

### 2.8 Tests that pin today's behaviour

Qt-free: `rewind_test 261`, `resume_guard_test 11`, `step_out_test 50`,
`persistent_bp_test 18`, `io_watchpoint_test 25`, `bp_enable_test 23`,
`resume_step_off_test 19`, `raster_state_test 86`, `snapshot_test 297`.
Qt-gated: `debugger_video_panel_test 106`, `debugger_audio_panel_test 15`,
`debugger_quit_gate_test 5`, `debugger_persistent_bp_test 5`,
`debugger_inspect_watchpoint_test 18`, `debugger_window_size_test 21`,
`debugger_window_grow_test 4`, `debugger_accel_test 8`, `debugger_keymap_test
34`, `debugger_menu_test 45`, `debugger_disasm_copy_test 33`,
`host_hotkey_test 45`, `preferences_apply_test 56` [`test/unit-tests.conf`].
The **#203 shape** exists: `step_out_test`'s `STPOUT-W*` rows arm the verb
through the same `DebugState` the debugger uses, drive a real `Emulator`
through `run_frame()`, and assert the machine *stops*; "every STPOUT-W row
fails on the pre-fix tree" [`test/debug/step_out_test.cpp:1-20`].

---

## 3. Architecture and build configuration

```
                ┌────────────┬───────────┬───────────┬──────────────┬───────────┐
  frontends     │ Qt panels  │ DZRP srv  │ ZRCP srv  │ GDB RSP srv  │ DSL +     │
  (adapters)    │ src/debugger  src/remote/dzrp  /zrcp  /gdb        │ recorder  │
                │            │           │           │              │ src/script│
                └─────┬──────┴─────┬─────┴─────┬─────┴──────┬───────┴─────┬─────┘
                      │  every access through ONE facade; no Emulator* below   │
                ┌─────▼────────────▼───────────▼────────────▼─────────────▼─────┐
  backend       │ jnext::dbg::Debugger            src/debug/  (target jnext_debug)│
                │  control · inspection · events/conditions · time · input/capture│
                │  bookmarks/rewind · symbols · session (clients, listeners, pump)│
                │  internals: DebugState, EventTable (ex BreakpointSet), TraceLog,│
                │  CallStack, SymbolTable, RewindBuffer, disasm, raster_state     │
                └────────────────────────────────┬────────────────────────────────┘
                                                 │ hooks: instruction boundary,
                                                 │ MMU/port/NR latches, frame edges
                ┌────────────────────────────────▼────────────────────────────────┐
  core          │ Emulator — keeps the hook sites and the latch; learns nothing    │
                │ about any frontend                                              │
                └─────────────────────────────────────────────────────────────────┘
  loop owners   QtApp tick · SdlApp loop · HeadlessApp loop  → run_frame(), pump()
```

Principles:

- **One facade in `jnext_debug`, built in every configuration.** The
  backend is added next to the primitives that already exist there, so the
  SDL-only build gains a debugger backend and headless tests need no Qt.
- **Emulator keeps the hooks, the backend keeps the policy.** The hot loop
  consults an `EventTable` at the same three points it consults `DebugState`
  today.
- **Pull for state, push for transitions.** Inspection is synchronous pull.
  The backend pushes `Paused`, `Resumed`, `Reset`, `FrameEnded`,
  `SubscriptionsChanged`, `ExitRequested`, `Log` to attached clients, plus
  per-subscription event deliveries. Panels refresh on `Paused` (and on their
  own throttle while running, as today); scripts act *inside* a delivery;
  remote servers turn `Paused` into a packet.
- **Single-threaded stays** (§5). **Deterministic time only** (§4.4).
- **Protocol models stay in the adapters.** RSP's flat 64 K view and register
  packing, ZRCP's cpu-step mode and 100 breakpoint slots, DZRP's temp-
  breakpoint stepping and bank bytes, the DSL's grammar — none of it is in
  `src/debug/`. The backend's own model is `MemSpace{Cpu, Page, Rom}`, a
  register struct, typed events with cheap filters and predicate callbacks,
  and client-owned subscriptions.

### 3.1 Layout

| Path | Target | Contents |
|---|---|---|
| `src/debug/debugger.h`, `events.h`, `inspect.h`, `result.h` | `jnext_debug` | **published** facade and value types (§4) |
| `src/debug/symbol_table.h`, `disasm.h`, `disasm_text.h`, `raster_state.h` | `jnext_debug` | published as today |
| `src/debug/debug_state.*`, `breakpoints.*` (→ `EventTable`), `trace.*`, `call_stack.*`, `rewind_buffer.*`, `resume_guard.h`, `debug_keymap.*` | `jnext_debug` | **internal** (frontends do not include them); `debug_keymap.*` stays here because `src/gui` reads it with `ENABLE_DEBUGGER=OFF` |
| `src/qt/debug_keymap_qt.h`, `src/qt/menu_bar_alt_nav_qt.h` | none (header-only) | the two Qt headers, moved; included by `src/gui` and `src/debugger` as now; `${CMAKE_SOURCE_DIR}/src` is already on every include path |
| `src/debugger/` | `jnext_debugger` (Qt) | the panels; `DebuggerManager` becomes a thin Qt adapter (design-qt §5.2) |
| `src/remote/dzrp/`, `src/remote/zrcp/`, `src/remote/gdb/` | `jnext_remote` | the three protocol servers over one non-blocking transport (the public `esp::make_socket_listener` / `EspListener` / `EspTransport` seam, `src/esp01/src/esp_socket.h:561/509/258`, Windows-twinned already) |
| `src/script/` | `jnext_script` | lexer/parser/evaluator/engine of the DSL, the expression compiler exported as a library, the recorder |

CLI (all gated by `make cli-check`): `--debug-listen-address ADDR` (default
`127.0.0.1`, after `--esp-listen-address`), `--dzrp-port N`, `--zrcp-port N`,
`--gdb-port N` (absent = off), `--script FILE`, `--script-key <frame> <n>`,
`--map FILE` (one symbol table for the GUI, the DSL and the servers).

### 3.2 Build-configuration matrix

| Configuration | `jnext_debug` | Qt panels | `jnext_remote` | `jnext_script` | `src/qt` headers |
|---|---|---|---|---|---|
| Qt + debugger (`build/`, shipped) | yes | yes | yes | yes | gui + debugger |
| SDL-only (`build/sdl-unit-test`) | yes | no | yes | yes | not compiled |
| Qt, no debugger | yes | no | yes | yes | gui |
| SDL + debugger (build-only) | yes | yes | yes | yes | debugger |

`jnext_remote` and `jnext_script` have no toolkit dependency and are built
everywhere; no gate option is proposed until a platform cannot build a
socket. `make build-matrix` catches the header move breaking the
`QT_UI=OFF, DEBUGGER=ON` link.

---

## 4. Backend API

`jnext::dbg::Debugger` is constructed with an `Emulator&` and is the only type
a frontend holds. Names are proposals; the **CAP ids are the contract** the
five frontends mapped against. Every verb returns a `Result` ∈ {`Ok`,
`RefusedRunning`, `RefusedPaused`, `RefusedCorrupt`, `RefusedRzx`,
`RefusedUnavailable` (benign: empty rewind buffer, trace off, frame out of
range), `RefusedReadOnly`, `InvalidPage`, `NotAtFrameBoundary`, `NoFrame`,
`Unsupported`} — never a silent no-op.

### 4.1 Control — `CAP-CTL`

| ID | Capability | Semantics |
|---|---|---|
| CTL-01 | `pause()` | at the next instruction boundary; idempotent; from `pump()` the machine is already at a boundary |
| CTL-02 | `run()` | no-op if running (GH #223); the GH #221 step-off arm stays in the backend, adapters never insert their own step-off breakpoints; subject to CTL-11 |
| CTL-03 | `step_into()` | `Emulator::debugger_step()` — frame-loop aware, runs a HALT out (GH #207); **synchronous** |
| CTL-04 | `step_over()` | `is_call_like` → transient `Execute` at next PC + `run()`, else CTL-03; asynchronous |
| CTL-05 | `step_out()` | `DebugState::step_out(SP)`, ends per `check_step_out` (GH #203); asynchronous |
| CTL-06 | `run_to(addr)` | transient `Execute[addr,addr]` + `run()` (Run to Here; RSP `i<len>` = `run_to(pc+len)`) |
| CTL-07 | `run_to_cycle(master_cycle)` | the primitive under CTL-08 |
| CTL-08 | `run_to_end_of_frame()` / `run_to_end_of_scanline()` | the two target computations move verbatim from `debugger_manager.cpp:475-560` |
| CTL-09 / CTL-10 | `step_back(n)` / `rewind_to_frame(n)` | synchronous; `RefusedRzx` / `RefusedUnavailable` / `RefusedCorrupt` distinguished |
| CTL-11 | `resume_blocked_by_corruption() -> optional<CorruptionIncident{subsystem, generation}>`, `acknowledge_corruption(gen)` | the `ResumeGuard` policy; the modal stays in Qt; an unacknowledged remote gets `RefusedCorrupt` |
| CTL-12 | `reset(Hard\|Soft)` | `request_hard_reset()` / `soft_reset()` |
| CTL-13 | `state() -> RunState{paused, step_mode, pause_reason, cycle, frame, pc}` | `pause_reason` ∈ {`User{cid}`, `Breakpoint{id}`, `Watch{id, access, addr}`, `Step`, `RunTo{id}`, `Magic`, `Corrupt`, `Script{id, text}`} |
| CTL-14 | `magic_breakpoint()` / `set_magic_breakpoint(bool)` | `Emulator::set_magic_breakpoint` [`emulator.cpp:7873`] |
| CTL-15 | `load(path)` | the frontend-agnostic dispatch `emulator_apply_load()` [`src/platform/emulator_boot.h:25`]; a paused caller stays paused at the new PC |

**Semantics with 0, 1 or N frontends.** 0: inert — `armed()` false unless
`--persistent-breakpoints` or a magic breakpoint; the hot loop pays what it
pays today (§6). 1: identical to today. N: one machine, one `DebugState`,
**no ownership token, no arbitration queue** — any client may pause, resume
or step, every transition is broadcast with the originating client id, "last
verb wins". Subscriptions are owned by their creating client (a detach
removes its own), listed to all, editable only by their owner. A remote pause
while the GUI is open reaches the GUI as `Paused{by: cid}` and it behaves as
on a breakpoint; a GUI Run afterwards reaches the remote as `Resumed{by}`
(DZRP has no packet for that — documented limit). Two flags replace
`active()`: `attached` (≥1 client) gates the step machinery; `live_raster`
(per client, ORed) gates only the render hint and the raster walk.

### 4.2 Inspection — `CAP-INS` (side-effect free by contract)

| ID | Capability |
|---|---|
| INS-01 | `registers()`, `set_register(RegId, v)` per register (12 pairs, PC, SP, I, R, IFF1, IFF2, IM) — no "set all" |
| INS-02 | `peek/poke(MemSpace, addr, n, buf)`: `Cpu` (live mapping, overlays included, **no floating-bus latch** — F1 → `Mmu::peek()`); `Page{p}` with **p = the NR 0x50-0x57 page number (0..223)**, backend does the VHDL routing (`to_sram_page` [`mmu.h:1387`], 0x0E → `bank7_bram` [`:1371`, `:1400`], 0xFE/0xFF → `InvalidPage`); `Rom{i}` for ROM images not in SRAM. `poke(Cpu)` = `Mmu::write` outside `GuestExecutionScope` (ROM ignored, scanline logs and attribute mux updated, no latch, no event), returns count + `RefusedReadOnly` |
| INS-03 | `mmu_slots() -> SlotInfo{nr_page, effective_page, is_rom}[8]` [`mmu.h:74-78`], `set_mmu_slot(s, p)`, `paging_ports()` |
| INS-04 | `nextreg_peek`, `nextreg_write` (synchronous, source Debugger, no event), `nextreg_selected` |
| INS-05 | `port_in`, `port_out` — perturbing by nature, said so; the DSL declines it |
| INS-06 | `raster() -> RasterState` (`src/debug/raster_state.h`, computed from the clock when paused) |
| INS-07 | `time() -> {master_cycle, tstates_total = monotonic_tstates(), frame (F2-fixed), cycle_in_frame, vc_raw, hc_raw}` |
| INS-08 | `sprites()` (`SpriteInfo` ×128), raw `sprite_attr_raw(i)` (5 bytes), `pattern_ram()` (16 KB), `sprite_palette_rgb333(bank, i)`, `sprite_clip()`; debugger writes `set_sprite_attr_raw`, `write_pattern_ram` (engine setters, not port traffic) |
| INS-09 | `copper() -> {pc, running, mode, program[1024]}` |
| INS-10 | `ay_registers(chip)`, `turbosound_enabled()`, `ay_mode()`, `stereo_mode()` (live signals [`turbosound.h:42,51,61`]), `audio_mute_mask` get/set |
| INS-11 | `disassemble(addr, n, symbols?)`, `instruction_length`, `is_call_like` over `peek(Cpu)`; `disasm_text::*` |
| INS-12 | `call_stack()`, `set_call_stack_enabled(bool)` |
| INS-13 | `trace_enabled/set_trace_enabled/trace_clear/trace_resize/trace_entries/trace_export`; `TraceEntry` gains I, R, IM, IFF1/2, `(SP)`, 8 MMU pages (+15 B/entry, read inside `InspectionScope`) |
| INS-14 | `framebuffer()`; `render_layer(Layer, vc, uint32_t* dst, stride)` — `render_to_image` + `replay_*` moved verbatim out of `video_panel.cpp:394-630` into a Qt-free function; width 640; rows 0..vc over a 0x00000000 fill (alpha 0 ≡ transparent); **needs-prototype** (§11) |
| INS-15 | `palette(PaletteId)` incl. `UlaActive`, `set_palette(id, i, rgb333)`, `active_ula_palette_bank()`, `ula_screen_regs()`, `clip_window(Layer)` from live layer state (not the rotating NR 0x18-0x1C shadows), the one published `rrrgggbb_to_argb()` |
| INS-16 | `input_state() -> {matrix[8], ext_keys, joy_left12, joy_right12, port_1f, port_37}` — for the #20 recorder |
| INS-17 | `subscriptions(include_transient)` (the model incl. disabled, with owner), `events_fired_since(seq)` |
| INS-18 | `set_border(colour)` → `Ula::set_border` [`ula.h:180`] (DZRP sends it on every load) |
| INS-19 | `machine() -> {type, cpu_divisor, cycles_per_line/frame, lines, fps, hc_max, vc_max, max_hblank, max_vblank, display_origin, vblank_top}` |
| INS-20 | `coverage_enable/clear/coverage() -> bitset<65536>` of executed PCs; one bit-set per instruction inside the attached-gated branch, zero cost when off |

### 4.3 Events, breakpoints, conditions — `CAP-EVT`

**The condition engine is a backend concept, and it is a predicate callback,
not an expression language.** A condition is
`std::function<bool(const Event&, const Debugger&)>` compiled by whoever set
it — the DSL compiles its `when` clause (and, the closure being the
interpreter's, it may read script variables and snapshots); ZRCP token-
translates ZEsarUX's dialect into the DSL's compiler exported as a library;
DZRP, RSP and the Qt GUI set none. One evaluation mechanism, the grammar in
the DSL where #26 defines it, no interpreter in the hot loop: predicates run
only at an instruction boundary for an event that already matched its cheap
filter.

| Kind | Cheap filter (where) | Payload beyond the common `{cycle, frame, vc, hc, pc, id, owner}` |
|---|---|---|
| `Execute` | PC ∈ [lo,hi], optional `page` qualifier (effective page at slot(PC)), tested after the address matched (pre-instruction gate) | — |
| `Mem`, `access` ⊆ {Read, Write} | logical addr ∈ [lo,hi] **or** physical page ∈ set (MMU sites, §6) | addr, phys_page, value, pc (= `pc_pre_exec`), `source` ∈ {Cpu, Dma} (tagged at the boundary drain from the slot's DMA flag [`emulator.cpp:9784`]) |
| `Port`, `access` | `(port & mask) == value` (GH #222's low-byte rule = mask 0x00FF); read value latched after dispatch | port, value, pc |
| `NextRegWrite` | reg ∈ set, `source` ∈ {Cpu, Copper, Dma, Any} (`NextReg::write` hook) | reg, value, `prev` (peeked at the hook), source, hc/vc; delivered **after** commit |
| `Frame` | every / frame == N | frame |
| `Scanline` | cvc == N — latched at `on_scanline` with the line's exact cycle, delivered at the next boundary (≤1 instruction late) | frame, vc, cycle |
| `Cycle` | master_cycle ≥ N, one-shot by nature | cycle |
| `Reset` | hard / soft | kind |
| `IntAck` / `Nmi` | the accept seams [`emulator.cpp:1114`, `:10386`] | vector, im / source |
| `Magic` | the magic opcode | pc |
| `Host` | `raise_host_event(name)`, `script1`..`script8`; key bindings are a GH #1 keymap addition | name |

**Subscription** = `{kind, filter, access, condition?, once, transient,
action, enabled, owner}`; `subscribe → EventId`, `unsubscribe`,
`set_enabled(id)`, the master switch (GH #225), a **per-client switch**
`set_client_enabled(cid)` (live = master ∧ client ∧ own flag, rebuilt on
change, never per instruction), `probe_execute(pc)` (a pure "would an
`Execute` match here" query for step loops — the GH #221 step-off skips
exactly the landed-on address on the next resume). `once` disables after the
first accepted firing; `transient` is exempt from the master switch,
auto-removed at the next stop, hidden from the user list, unlimited in number
— Step Over, Run to Here and DeZog's two temp breakpoints per `CMD_CONTINUE`
all use it, replacing today's single one-shot. Today's PC breakpoints and
watchpoints become single-address `Execute` / `Mem` subscriptions owned by the
Qt client; `BreakpointSet`'s observer contract survives as
`SubscriptionsChanged{kinds}`.

**Actions** `Stop` / `Log` / `Continue`; a handler's verdict may override the
static action. `Stop` pauses at the boundary with `pause_reason` set and the
`Paused` push carrying **`matched: vector<Hit{event_id, addr, access,
value}>`** — every subscription that matched, transient ones included (the
DZRP adapter's "temp beats user" rule is adapter policy over that list).
Under `--headless` the loop owner maps `Stop` per CAP-SES-04.

**Delivery point and non-perturbation.** Memory, port and NR events raised
*during* an instruction are **latched** at the site (`{kind, addr, value,
phys_page}` into a 16-entry ring on `DebugState`, replacing the single
`data_bp_addr_`) and **delivered at the instruction boundary** — the existing
`data_bp_hit()` shape [`emulator.cpp:9398`] with value and page added. No user
code runs inside `Mmu::write`, the CPU or a device tick; the machine is
stopped when a handler runs; every inspection read is side-effect free. A DMA
burst slot may overflow the ring (recorded as `overflowed`, §11). Span
invariants and cross-register checks (#279) need no new kind: entry/exit
`Execute` (or `IntAck`) handlers over INS-01/03 — the DSL's `snap`/`unsnap`/
`changed()` stack.

### 4.4 Deterministic time — `CAP-TIME`

`time()` and `machine()` (TIME-01); `Frame`/`Scanline`/`Cycle` events
(TIME-02) are how a script acts **at** an instant — the handler runs at the
boundary where the instant was reached, with the machine stopped there;
`run_to_cycle`/`run_to_frame` (TIME-03) are how a client drives to one. A
panel refreshing on pause is pull after a `Paused` push; a remote acting at
an event is `Stop` → `Paused` → commands. The `--delayed-*-time` (seconds)
options stay a frontend conversion to frames at startup
[`headless_app.cpp:353-375`].

### 4.5 Input injection and capture — `CAP-IN`, `CAP-CAP`

Replaces the duplication in `HeadlessApp` and `QtApp` (§2.5): both become
callers of the same primitives, scheduled by `Frame` events.

| ID | Capability |
|---|---|
| IN-01 | `press_key(name\|{row,col}[,{row2,col2}], hold_frames)` — `Keyboard::queue_auto_type`; `key_name_to_matrix()` moves into the backend so every frontend and the DSL share the man page's vocabulary |
| IN-02 | `set_key(row, col, pressed)`, `set_extended_key(id, pressed)` — level, for replay of recorded state |
| IN-03 | `set_joystick(side, bits12)` |
| IN-04 | `press_nmi(Mf\|Drive)` — the GH #209 hotkey seam |
| CAP-01 | `screenshot(path, layer_mask, Png\|Scr)` — next rendered frame; `NoFrame` when paused mid-frame (today's contract) |
| CAP-02 | screen memory: `ula_screen_dump()` (`Ula::screen_dump` [`ula.h:608`]) + `peek(Page)` for L2 / tilemap / pattern RAM |
| CAP-03 | `bookmark_save(name)` / `bookmark_restore(name)` — named, in memory (§4.6) |
| CAP-04 | `save_snapshot(path)` at the next frame boundary — the `--delayed-snapshot` path |

### 4.6 State bookmarks and reverse execution — `CAP-ST`

| ID | Capability |
|---|---|
| ST-01 | `at_frame_boundary()`; `save_state_bytes(Mode::AdvanceToBoundary \| RefuseMidFrame)` — frame-boundary only; `AdvanceToBoundary` runs the #27 S6 `SuspendScope` advance; `RefuseMidFrame` → `NotAtFrameBoundary` for a client that cannot refresh its register cache (DeZog) |
| ST-02 | `load_state_bytes(bytes)` — in-process only, no versioning; failure latches corruption → CTL-11. DZRP `CMD_READ/WRITE_STATE`, ZRCP `snapshot-save/-load` and CAP-03 are the same map; disk is JNS |
| ST-03 | `rewind_enabled` get/set, `rewind_range() -> {oldest, newest, depth, capacity, snapshot_bytes}`, `rewind_blocked() -> optional<reason>` (pre-click greying), `resize_rewind_buffer(n)` |
| ST-04 | `step_back(n)`, `rewind_to_frame(n)` — offered to all; served by Qt; declined by the DSL (v1), unreachable for DZRP and RSP (no verb — `z88dk-gdb` sends no `bc`/`bs`, verified against `debugger_gdb.c`), unreachable for ZRCP (`cpu-history` is a trace *view*, INS-13) |

### 4.7 Symbols — `CAP-SYM`

`load_map(path, Z88dk\|Simple)`, `clear`, `lookup(addr)`, `lookup_name`,
`symbols()`. `SymbolTable` moves from `DebuggerManager` into the backend: one
table for the panels, the DSL's `@name`, the servers' lookups and `--map`.

### 4.8 Session — `CAP-SES`

| ID | Capability |
|---|---|
| SES-01 | `attach(ClientInfo) -> ClientId`, `detach(cid)` — removes the client's subscriptions; if it was the last client and the machine is paused *by it*, resumes (a crashed DeZog must not leave the machine hung; a pause by another client survives) |
| SES-02 | `set_listener(cid, Listener&)` — `Paused{by, reason, cycle, pc, matched[]}`, `Resumed{by}`, `Reset{kind}`, `FrameEnded{frame}`, `SubscriptionsChanged{kinds}`, `ExitRequested{code}`, `Log{level, text}`; synchronous, on the emulation thread, must return promptly (the Qt listener records and acts on its tick) |
| SES-03 | `pump(PumpBudget{max_wait_ms, drain_ms, budget_ms}) -> ServiceHint{remote_attached, paused}` — called by the loop owner once per tick **after** the frame batch (where `check_breakpoint_hit()` sits today [`qt_app.cpp:666`]); while paused it drains queued commands (a DeZog zrcp step is ~15 sequential round trips); `pump(0)` while running |
| SES-04 | `set_stop_policy(Pause \| ExitNonZero)` — GUI/SDL `Pause`; `--headless` `ExitNonZero` **unless a remote client is connected, then `Pause` + notify** (decided by design, three frontends concur; overrulable, §12) |
| SES-05 | `set_live_raster(cid, bool)` (ORed), `attached()` |
| SES-06 | `log(level, text)` — the backend's message sink |

---

## 5. Threading and the out-of-process model

Single-threaded, no mutex, as today [`src/doc/developer-guide/03-subsystems/09-debug-and-the-debugger.md`].
The loop owner is the frontend: `QtApp`'s timer tick through
`frame_sequencer`, the SDL loop [`sdl_app.cpp:378`], `HeadlessApp::run()`
[`headless_app.cpp:503`]. While paused the Qt/SDL loops keep ticking; the
headless loop busy-spins on a `run_frame()` that returns at once
[`emulator.cpp:9304-9305`].

**The minimum change a socket frontend needs is none to the threading
model.** A server is an adapter over a non-blocking listener; the loop owner
calls `pump()` once per tick after the frames; the server reads what is
available, executes complete commands synchronously against the backend on
the emulation thread, writes replies, and flushes queued notifications.
Consequences:

- GUI / SDL: `pump(0)` per tick, ≤ one tick (20 ms at 50 Hz) command latency
  — what the CSpect plugin gives DeZog; one tick per DeZog `CMD_CONTINUE`
  round trip; chained commands (ZRCP's ~15 per step, z88dk's 4-5 per `stepi`)
  are drained inside one `pump` while paused.
- Headless with a server: `pump(wait)` with `wait = paused ? ≤50 ms : 0`
  turns the spin into a `poll()`. That is the whole headless change.
- **No reentrancy.** A command handler that calls `step_into()` executes an
  instruction inside `pump`, outside `run_frame` — where
  `DebuggerManager::on_step_into()` calls `debugger_step()` today. Handlers
  are never invoked from inside a delivery, `pump` is never called from inside
  `run_frame` (asserted). The DSL's "a script observes, it does not drive" is
  the same wall from the other side.
- **Why not a thread:** every inspection read would need the emulator locked
  at an instruction boundary anyway (a mid-instruction register read is
  garbage) — a condition-variable handshake per command for no lower latency
  than a tick. The one thing that would reopen this is a client needing
  sub-tick response *while the machine runs*; none of the five claims it.
  Whether a ~2 ms re-armed tick while paused-with-remote is needed for DeZog
  step-out loops is measured, not assumed (§11).

---

## 6. The hot path, measured

**Constraint** (#279, #277): range watches on every memory write must cost
≈ nothing with no watch armed and stay cheap with one armed.

### 6.1 Design

The existing gate order is kept exactly — `debug_state_ &&
watchpoints_live() && has_any_watchpoints()` [`mmu.h:258-260`], the whole
no-watch cost today and tomorrow. Behind it the linear scan is replaced by
two bytes on `DebugState`, `rd_slot_mask_` / `wr_slot_mask_`: bit *s* set iff
any armed range intersects logical 8 KB slot *s* **or** any armed physical-
page set contains the page currently mapped there; recomputed only when
subscriptions change or the MMU remaps a slot (`on_slot_remapped`); on a hit,
the precise scan of that slot's few ranges, then the latch. One byte load +
shift + test per access. A 64 K-bit bitmap was rejected: same instruction
count, 8 KB of cache pressure per space, and no physical-page expression.

### 6.2 Method

`make bench` measures one binary at a time and voids a workload above 5 %
spread; on this host during the design session (five agents building) it
voided two of five workloads (load1 2.18 at start; result kept in the
scratchpad, not committed). Deltas were therefore measured as Task 27's
`test/bench/c1-ab-e596fa6a.txt` did: **interleaved A/B**, four binaries per
pair on `taskset -c 0`, private reflink SD clone, `--benchmark N` headless,
T-states/s, medians over pairs, per-binary spread. All Release, `-O2
-DNDEBUG`, the `build/gui-release` flags.

| Tag | Binary | Env |
|---|---|---|
| B | `974b0ab19` unmodified | — |
| P1U | prototype: slot masks tested **after** the existing triple gate at all 8 MMU sites | none |
| P0A | baseline code + an env hook arming ONE `WRITE` watch at `0x0000` with `--persistent-breakpoints` (today's linear scan, armed) | `JNEXT_PROTO_WATCH=0000,w` |
| P1A | P1U + the same hook, masks set from the watch | same |

Throwaway prototype: three files mutated (`mmu.h`, `debug_state.h`,
`emulator.cpp`), built in `build-p0/` and `build-p1/`, sources restored from a
`cp` backup and verified byte-identical with `cmp`, build dirs deleted,
nothing committed. Run 2's armed binaries additionally consumed the latch
without pausing (`JNEXT_PROTO_NOPAUSE`), because on `boot-nextzxos` the
firmware's ROM copy into SRAM **writes `0x0000`**: in run 1 the watch fired,
`run_frame()` paused, and the benchmark counted frames that emulated nothing
(13× "faster" — a paused machine, not a speed-up; those cells are marked
invalid). With the no-pause hook, `boot-nextzxos` armed is the **hot-hit**
case (the watch latches on every write of a 16 KB ROM copy) and the other
two workloads stay armed-cold.

### 6.3 Results

| Workload | run (pairs, load1 start→end) | B | P1U vs B (no watch) | P0A vs B (scan, armed) | P1A vs B (mask, armed) |
|---|---|---|---|---|---|
| boot-48k (3.5 MHz, ROM-resident) | 1 (5, 2.45→2.51) | 69.3 M (sp 1.8 %) | **+1.1 %** (3.4 %) | −0.4 % (14.7 %) | −1.3 % (9.1 %) |
| boot-48k | 2 (7, 2.91→3.21) | 68.6 M (sp 27.1 %*) | −0.4 % (5.9 %) | −2.3 % (9.7 %) | −1.0 % (1.8 %) |
| boot-nextzxos (28 MHz) | 1 | 180.3 M (2.7 %) | **+3.6 %** (4.3 %) | *invalid* | *invalid* |
| boot-nextzxos, **hot hit** | 2 | 184.8 M (3.9 %) | **+0.8 %** (3.1 %) | **−5.0 %** (2.1 %) | **−1.4 %** (1.6 %) |
| beast (28 MHz, L2 + copper) | 1 | 68.8 M (1.9 %) | **+0.0 %** (2.5 %) | −2.4 % (4.7 %) | −0.8 % (1.8 %) |
| beast | 2 | 68.0 M (3.0 %) | **+0.8 %** (2.8 %) | −1.9 % (9.8 %) | −1.5 % (5.7 %) |

\* one load burst in run 2's `boot-48k` B column (pair 6 ran 25 % slow); the
interleaving protects the other columns, but that B median is the least
trustworthy number in the table.

**Verified:**

1. **No-watch cost of the new gate shape: none.** B → P1U is +1.1 / −0.4 /
   +3.6 / +0.8 / +0.0 / +0.8 % over six workload-runs — noise, centred on the
   favourable side. The design predicted it (the first gate is unchanged, the
   compiler emits nothing new before it); it is now measured.
2. **Armed and hitting: today's scan costs 5.0 %, the slot mask 1.4 %** on
   `boot-nextzxos`, the cleanest row (spreads 2.1 % / 1.6 %). Both armed
   columns also pay the pre-existing `--persistent-breakpoints`
   per-instruction `should_break()` lookup, so 1.4 % is an upper bound on the
   mask design with one armed, frequently-hitting watch — paid only by the
   user who armed it.
3. **Armed-but-cold** (`beast`, `boot-48k`): both designs within 1-2.5 % of B
   with spreads of that size; the mask is never worse. No stronger claim.

**Not measured** (all off the hot path; §11): the boundary drain with
predicate evaluation and handler call, the physical-page path (masks were
static), `on_slot_remapped`, and the "condition-only breakpoint" class
(`Execute[0,0xFFFF]` with a predicate, ZRCP's PC-free conditions — a
per-instruction predicate that only the session which set one pays for).

---

## 7. Frontend projections and the capability matrix

Each subsection is the projection its designer recorded; the full
command/packet tables, wire findings and validation transcripts are in the
per-frontend files under `debug-subsystem/`.

### 7.1 Qt GUI — #278 (`qt-frontend.md`)

- **Projection:** 93 core accesses inventoried; 91 map onto a CAP, 2 stay
  GUI-side by design (the Watches display list — a watch is a peek, not an
  event, agreed with the DSL — and the layer-state derivation from four
  peeks). `DebuggerManager` becomes a thin Qt adapter attached for the process
  lifetime (so a magic-breakpoint, persistent-breakpoint or remote pause
  reaches it with the window closed, today's GH #219 path) that toggles
  `live_raster` on window show/hide; its listener records transitions and
  does UI work on the tick. Four panels refresh only on pause, the rest on
  the ~4 Hz throttle, exactly as today.
- **Sufficiency findings that changed the backend:** `active()` also gates
  the step machinery (→ `attached` vs `live_raster`); benign rewind failures
  must not raise the corruption modal (→ `RefusedUnavailable`); pre-click
  greying needs `rewind_blocked()`; the audio panel is pinned against
  re-decoding NR bytes (→ live TurboSound signals); the frame diagram needs
  raster geometry (→ `machine()`); READ_WRITE is one row (→ `access`
  bitmask); the gutter re-disassembles on PC changes only (→
  `SubscriptionsChanged{kinds}`).
- **Declined (17):** everything #278 adds no feature for — conditions,
  ranges, `once`, `Log`/`Continue`, the deterministic-time kinds, `Host`,
  `set_register`, `set_mmu_slot`, `port_in/out`, `Page`/`Rom` spaces (the
  Memory panel's slot view reads the CPU map today — pinned for identity,
  §12), input injection, capture, bookmarks, coverage, `load`. `pump(0)` and
  the `Pause` stop policy are used by `QtApp` as the loop owner.
- **The `render_layer` split** (INS-14): backend function, Qt keeps `QImage`,
  checkerboard, DPR, the raster line and the titles; the uniform alpha-0
  contract is proved by running the 106 DVP rows against the moved function
  before the widget changes.

### 7.2 DZRP — #12 (`dzrp-frontend.md`)

- **Findings that fix the design:** jnext answers DZRP **2.1.0** and presents
  as DeZog's **`cspect` remote type** (the only released socket path; the
  `zxnext` socket remote is a fork-only opcode-patching dialect, so
  `CMD_SET_BREAKPOINTS`/`RESTORE_MEM` are unsupported-reported). **No released
  DeZog remote sends watchpoints or state over a wire** — those "tier 2"
  commands are served but reached only by non-DeZog clients
  (`tools/cspect_dzrp/cspect_dzrp.py`, ZX Basic Studio, `dezogif_ng`'s
  harness); the man page must not claim DeZog coverage for them. DeZog
  assumes the remote is stopped after `CMD_INIT`, so **`CMD_INIT` pauses**.
- **Projection:** 22 commands served over CTL-01/02/12, INS-01..05/08/17/18,
  `Execute`/`Mem` subscriptions, ST-01/02, SES-01..03. DeZog steps by
  `CMD_CONTINUE` with up to two temp breakpoints → transient `Execute`
  subscriptions + `run()`; **the adapter never calls the backend's step
  verbs.** Long-address breakpoints → the `Execute.page` qualifier. `matched[]`
  gives the "temp beats user" reporting rule. `CMD_READ_STATE` uses
  `RefuseMidFrame` (DeZog does not re-read registers afterwards; an advance
  would make its next step compute temp breakpoints from a stale PC).
- **Declined by design (4):** the condition string in `CMD_ADD_BREAKPOINT`;
  reverse debugging (no verb); step verbs (no command); IO watchpoints (no
  wire form). **Unsupported, reported (3):** 13, 14, `CMD_EXEC_ASM`.
  Documented limits: no "resumed" notification; a refused `CONTINUE` is
  reported as `NTF_PAUSE` reason 255.
- **Validation:** DeZog 3.7.4 as the real client (manual protocol recorded
  in `doc/testing/DZRP-VALIDATION.md` with V-LAT latency numbers);
  `cspect_dzrp.py` as an independent client-side implementation driving
  eight functional rows; a fake-transport unit suite.

### 7.3 ZRCP — #280 (`zrcp-frontend.md`)

- **Protocol as measured** (ZEsarUX 12.0, DeZog 3.7.4): telnet-style lines,
  `command> ` / `command@cpu-step> ` prompts; `run` answers only when the
  machine stops and **any inbound byte stops it**; breakpoints are **pure
  conditions with no address field** (`SP>=nnnn`, `PC=PEEKW(SP-2) AND …`); a
  DeZog step is ~15 sequential round trips.
- **Projection:** 67 command names served (every one DeZog sends, plus
  telnet conveniences) over CTL-01/02/03/06/12/13/15, INS-01..05/07/08/11/
  12/13/15/19/20, EVT with predicates (ZEsarUX's dialect token-translated into
  the DSL's `compile_expr` library; `PC=nnnn` becomes an `Execute[a,a]` fast
  path), per-client `set_client_enabled` (DeZog toggles enable-/disable-
  breakpoints per session), `probe_execute` (bounded `run n` loops),
  `matched[]` (the "fired" line's address), IN-04, CAP-03 bookmarks keyed by
  name, SES-01..04. cpu-step mode, the prompt, the 100 slots, the memory-
  breakpoint map and the partial T-state base are adapter state.
- **Declined (1 command + 6 options):** `exit-emulator`; `run verbose /
  no-stop-on-data / update-immediately`; `cpu-history restore`; `set-debug-
  settings` bit 5; breakpoint actions other than break/print. 57 of ZEsarUX's
  125 commands reported `Unknown command`, each decline's DeZog effect listed.
  Deliberate divergences (e.g. `hard-reset-cpu` is a cold boot; `MMU=` for
  ROM slots follows DeZog's decoder, not ZEsarUX's bug) stated in `help`.
- **Validation:** `nc` by hand; a Python fake client as the `zrcp-func`
  regression row diffing bytes against captured ZEsarUX transcripts; DeZog
  3.7.4 `"remoteType": "zrcp"` end to end.

### 7.4 GDB RSP — #281 (`gdb-rsp-frontend.md`)

- **Premise re-verified:** distro gdb has no z80 (`Undefined item: "z80"`);
  the client is `z88dk-gdb` (`-h -p -x`), it refuses to connect without
  `qXfer:features:read+` and derives the register packing from the served
  `target.xml`. **Verified against the real v2.4 binary:** the XML must be
  < 1023 bytes (a 1094-byte one segfaults the client's `strcpy`), the served
  document is 600 bytes (`af bc de hl af' bc' de' hl' ix iy sp pc clockl
  clockh`, 14 × 16-bit LE in `g`), stop replies must be `T05…` (the client
  dispatches only on `T`); I/R/IFF/IM are deliberately **not** in the XML
  because the client's `G` zero-fills every register it does not know.
- **Projection:** 21 CAPs — CTL-01/02/03/06/12/13, INS-01 (per-register
  set), INS-02 `Cpu` (the flat 64 K view is *the* memory RSP can express),
  INS-03/04/05/07/11/17, `Execute` (`Z0/Z1`), `Mem` ranges (`Z2/Z3/Z4` — never
  sent by z88dk-gdb, served for real gdb), owner tagging (`D`), SYM, SES-01..03.
  `qRcmd` (`monitor`) is the only place Next-specific state is exposed.
  `i<len>` (z88dk's non-standard step-over) → `run_to(pc+len)`.
  `clockl/clockh` ← `tstates_total`. Unsupported packets get the empty reply
  `$#00`, RSP's own "unsupported".
- **Declined (24):** conditions (never advertised), the other step verbs,
  the corruption modal (`E01`), ST-01..04 (no `bc`/`bs` from the client —
  the only packets it emits are `qSupported, qXfer:features:read, ?, g, G,
  m, M, Z0, z0, s, i<n>, c, D, qRcmd, 0x03`), the Next-specific inspection
  outside `monitor`, IN/CAP, TIME-02/03, SES-04/05.
- **Validation:** design-time stub transcripts against the real client; the
  `gdb-z88dk-func` regression row; `gdb_rsp_test` over a fake transport; the
  z88dk wiki listing as a post-release PR.

### 7.5 The DSL — #26 carrying #279 (`dsl-frontend.md`)

- **Language:** `.jds`, line-oriented; `rule ::= [[disabled] NAME:] on event
  [once] [when expr] do actions end`; events `execute / read / write /
  io_read / io_write / nextreg / frame / scanline / cycle / interrupt / nmi /
  reset / hostkey N / stop`; actions `log, stop, assert, exit, dump_*, snap /
  unsnap / dump_diff, enable / disable, screenshot, save_snapshot,
  compare_scr, press / release / joystick, set, if`; expressions over
  registers, `mem[]`, `mem16[]`, `phys[page, off]`, `nextreg[]`, `mmu[]`,
  `stack[]`, `@symbol`, payload names (`ADDR VALUE PREV PAGE PORT REG SOURCE
  KEY REASON`), clocks (`FRAME CYCLE TSTATES` and the four raster counters).
  A script **observes, it does not drive**: no step/run/rewind, no poke —
  mutation of the machine is the one thing the language cannot say.
- **#279's acceptance workload is expressible:** a write to a MAP-delimited
  code range (logs the writing PC and the physical page); NR 0x51 vs MMU0 via
  `on nextreg 0x51 when …` reading `nextreg[0x50]` and `PREV`; interrupt-
  handler entry/exit invariants with an indented call trace via a **named
  snapshot stack** (`snap`/`unsnap`/`changed(NAME, regs|mmu|iff1|stack0)`/
  `depth`) — ChaseTheBug's `FunctionStackEntry` + `EqualRegs` as a language
  feature; `MemPoint=addr,value` via `when VALUE == …`; arm/disarm from host
  keys. No plugin-shaped need surfaced; `crc32(range)` and a hit histogram
  are the next likely asks.
- **Projection:** 28 CAPs — every event kind but `Magic`, predicates, `once`,
  INS-01/02/03/04/06/07/16/19 (read halves only), TIME-01/02, IN-01/02/03,
  CAP-01/02/04, SYM, SES-01/02/04/06. **Declined (21):** every control verb,
  every mutation verb, INS-05/08..15/17/18/20, bookmarks/rewind (re-entrant
  with delivery inside `run_frame`), `pump`, `live_raster`.
- **`compile_expr` / `eval_expr` are exported as a library** so ZRCP owns no
  second parser.
- **Headless:** `stop` → logged event + non-zero exit unless a remote client
  is connected; explicit `exit N` returns N; the default code is an owner
  question (§12).

### 7.6 Capability matrix

S = served · D = declined by design (the adapter could, and chooses not to) ·
U = unreachable by protocol (no verb/packet/grammar for it) · — = not
applicable to that consumer.

| Capability | Qt (#278) | DZRP (#12) | ZRCP (#280) | GDB (#281) | DSL (#26) |
|---|---|---|---|---|---|
| CTL-01 pause / CTL-02 run | S | S | S | S | S (pause as the `Stop` verdict) / D |
| CTL-03 step into | S | U | S | S | D |
| CTL-04 step over / CTL-05 step out | S | U (temp bps + run) | S (ZEsarUX semantics via CTL-06 + predicates) | U (client computes `i<len>`) | D |
| CTL-06 run_to | S | U (transient subs) | S | S | D |
| CTL-07 run_to_cycle | D (internal to 08) | U | U | U | D |
| CTL-08 run to EOF / EOSL | S | U | U | U | D |
| CTL-09/10 step back, rewind to frame (= ST-04) | S | U | U | U | D |
| CTL-11 corruption gate | S (modal) | S (NTF 255) | S (error line) | S (`E01`) | — |
| CTL-12 reset | D (Machine menu) | S | S | S (`monitor reset`) | D |
| CTL-13 state / pause_reason | S | S | S | S | S (`REASON`) |
| CTL-14 magic switch | S | U | U | U | D |
| CTL-15 load | D | U | S (`smartload`) | U | D |
| INS-01 registers get / set | S / D | S / S | S / S | S / S | S / D |
| INS-02 peek Cpu / Page / Rom; poke | S / D / D; S | S / S / U; S | S / S / U; S | S / S (monitor) / U; S | S / S / D; D |
| INS-03 MMU slots get / set | S / D | S / S | S / S | S / U | S / D |
| INS-04 NextREG peek / write | S / S | S / U | S / S | S (monitor) / S (monitor) | S / D |
| INS-05 ports | D | S | S | S (monitor, labelled perturbing) | D |
| INS-06 raster | S | U | U | U | S |
| INS-07 time | S | U | S | S (`clockl/clockh`) | S |
| INS-08 sprites (+ raw, writes) | S (attrs only) | S | S | U | D |
| INS-09 copper | S | U | U | U | D |
| INS-10 AY / TurboSound / mute | S | U | U | U | D |
| INS-11 disassembly | S | U | S | U | D |
| INS-12 call stack | S | U | S (`extended-stack`) | U | D |
| INS-13 trace | S (export) | U | S (`cpu-history`) | U | D |
| INS-14 framebuffer / render_layer | D / S | U | U | U | D (screenshot via CAP-01) |
| INS-15 palettes / clip / ULA regs | S | S (sprite palette) | S | U | D |
| INS-16 input state | D (recorder-only) | U | U | U | S (recorder) |
| INS-17 subscription list | S | S (idempotence) | S | S (`z` idempotence) | D |
| INS-18 set_border | D | S | S | U | D |
| INS-19 machine() | S | S (`CMD_INIT` type) | S | U | S |
| INS-20 coverage | D | U | S | U | D (later `executed()`) |
| EVT Execute (single / range / page) | S / D / D | S / U / S | S / S / U | S / U / U | S / S / S |
| EVT Mem ranges, access bitmask | single-address only | S | S | S | S |
| EVT Port | single-address only | U | U | U | S |
| EVT NextRegWrite | D | U | U | U | S |
| EVT Frame / Scanline / Cycle | D | U | U | U | S |
| EVT Reset / IntAck / Nmi / Magic / Host | D | U | U | U | S / S / S / D / S |
| EVT conditions | D | **D** (client-side) | S (translated) | D (not advertised) | S |
| EVT once / transient / per-client switch / probe_execute | D / S (internal) / D / D | D / S / D / D | S / D / S / S | D / D / D / D | S / D / D / D |
| TIME-02 act at an instant | D | U | U | U | S |
| IN-01..04 input | D | U | U / U / U / S (`generate-nmi`) | U | S / S / S / D |
| CAP-01 screenshot / CAP-02 screen bytes / CAP-04 snapshot | D | U | U | U | S |
| CAP-03 / ST-01 / ST-02 bookmarks | D | S | S (`snapshot-save/-load`) | U | D |
| ST-03 rewind buffer control | S | U | U | U | D |
| SYM | S | D (client-side) | S | S (`monitor sym`) | S |
| SES-01..03 attach / listener / pump | S (QtApp pumps) | S | S | S | S / S / D |
| SES-04 stop policy | S (Pause) | (Pause while connected) | (Pause while connected) | (Pause while connected) | S (ExitNonZero via the loop owner) |
| SES-05 live_raster | S | D | D | D | D |
| SES-06 log | S (console) | D | S (`log>` lines) | D | S |

Reading the matrix: no capability is used by nobody except the two written
for the recorder (INS-16) and for ZRCP (INS-20), each with a second plausible
consumer named; the DSL reaches every event kind and every read; DZRP's
column is the owner's projection principle made concrete — conditions
declined, reverse unreachable, the rest served.

---

## 8. The #20 verdict

**#20 is a use case of #26.** Independently derived by the backend and the
DSL designs, and consistent with the owner's read:

- **Replay is a script.** `on frame N do press "fire" / release / joystick 1
  0x… end` applies recorded input *state* at frame boundaries (IN-02/03,
  level semantics); `on frame M do compare_scr "ref-M.scr" "…" end` (CAP-02)
  and `screenshot` (CAP-01) assert or regenerate; `exit` gives the CI code.
- **Record is the one new piece:** a recorder over `Frame` + `Host` events
  reading INS-16 `input_state()` each frame (the one thing nothing exposes
  today), dumping ULA `.scr` on a hotkey, emitting the `.jds` and its
  header (`MACHINE`, program file). It is a small frontend-side utility, not
  a protocol.
- **Screen memory:** the ULA `.scr` is the byte-diffable unit; non-ULA layers
  go through the composited PNG and the suite's existing png-diff. RZX stays
  untouched — it replays IN results, and a keyboard-path test needs input
  state.
- **No third mechanism:** `--delayed-keypress-frames`, `--delayed-nmi`,
  `--delayed-screenshot*`, `--delayed-snapshot` remain as flags implemented on
  the same primitives (generated one-line subscriptions), and `QtApp`'s second
  copy of the countdowns goes away.

**Re-scope #20 to:** the recorder (`src/script/recorder.*`, Debug menu
"Record Script…", capture hotkey), the `compare_scr` action, INS-16, and the
conversion of the two parked DAPR interactive rows. Everything else it asked
for is #26.

---

## 9. Testing strategy

- **Backend suite, headless, no frontend** (`test/debug/debugger_backend_test`,
  Qt-free): a 48K `Emulator` with a RAM program, a `Debugger` over it, frames
  driven through `run_frame()` as the frontends drive them. **One wiring row
  per verb and per event kind** — arm through the facade, run, assert the
  machine stopped where promised (PC, cycle, `pause_reason`) — plus a control
  row that runs straight past without the verb. That is the #203 shape
  generalised: "the predicate is right" is not a row; "the machine stopped"
  is. Delivery rows assert payloads (value, physical page, `source`, `prev`)
  and that a false predicate does not stop; range rows sit on both edges and
  one past each; transient rows (two per resume, removed at the stop, absent
  from the list); `matched[]` rows; multi-client rows (pause by A seen by
  both; detach A paused-by-A resumes; detach A paused-by-B stays paused, A's
  subscriptions gone); F1 (a `peek(Cpu)` sweep in +3 mode leaves
  `p3_floating_bus_dat_` unchanged) and F2 (`time().frame` advances without
  rewind) rows.
- **Adapters:** each server over a fake in-memory `Transport` (unit suites
  `dzrp_adapter_test`, `zrcp_adapter_test`, `gdb_rsp_test`, Qt-free) plus a
  real-client regression row each: `cspect_dzrp.py` (an independent client
  implementation) for DZRP, a Python fake client against captured ZEsarUX
  transcripts for ZRCP (`zrcp-func`), `z88dk-gdb` for RSP (`gdb-z88dk-func`),
  and manual DeZog 3.7.4 sessions recorded with version and latency numbers.
  Reviewer mutations are derived from the diff (serve `S05` for `T05`; a
  1023-byte XML; `G` clobbering I/R; a stop reply on a foreign `Paused`; …).
- **DSL:** `script_parse_test`, `script_eval_test`, `script_events_test`, six
  `script-*-func` rows over demo programs with buggy twins, and a mutation
  table per acceptance script (deliver the NR event before commit; register
  the `MemPoint` rule without a predicate; drop the `InspectionScope` around
  script reads; apply `press` mid-frame).
- **Qt (#278):** the 13 Qt suites and their pinned counts are the identity
  test; design-qt §6.2 lists the gaps to close on the *current* tree first
  (green on both trees by construction), and §6.3 the reviewer mutations
  (e.g. move `debug_keymap_qt.h` back → the lint row). Gate: `grep -l
  'core/emulator.h' src/debugger/*.cpp` empty.
- **Hot path:** `make bench` before/after every backend branch that touches
  `mmu.h` or `debug_state.h`; the §6 A/B method when the box is loaded.
- **Manifest discipline:** every new suite and every changed row count is a
  deliberate `test/unit-tests.conf` / `functional_tests.conf` edit; CLI rows
  through `make cli-check`; docs through `docs-check`.

---

## 10. Implementation plan

### 10.1 Work packages and frozen interfaces

The interface every package codes against is **§4 of this document** (the
CAP tables), published as `src/debug/debugger.h`, `events.h`, `inspect.h`,
`result.h`. Within a package, sub-items are independent files and may be
written by parallel agents; each gets its own independent reviewer.

| WP | Branch (one per sub-issue) | Content | Depends on |
|---|---|---|---|
| **B** backend | `gh276-backend` (§12 Q1: a new sub-issue, or stage 1 of #278) | B1 facade + control + inspection over the existing primitives (no hot-path change), `Mmu::peek()` (F1), the frame counter (F2), `SymbolTable` move, `key_name_to_matrix` move; B2 `EventTable` + latch ring + slot masks + `on_slot_remapped` + NR/port/IntAck/Nmi/Reset/Frame/Scanline hooks (bench-gated); B3 session: clients, listeners, `pump`, stop policy, `live_raster`/`attached`; B4 input/capture/bookmarks/coverage/extended `TraceEntry`; B5 `debugger_backend_test`; B6 the header move to `src/qt/` + `make build-matrix` | this design's review |
| **Q** #278 | `gh278-qt` | design-qt WP0 (close the identity gaps on the current tree) → WP1 header move (may precede B) → WP2 `DebuggerManager` verbs → WP3 rewind/trace/corruption → WP4a-d panels (parallel) → WP5 memory panel → WP6 symbols/magic → WP7 reach-around grep = 0 | B |
| **D** #12 | `gh12-dzrp` | design-dzrp WP-1 transport+framing → WP-2 session/registers/memory → {WP-3 breakpoints/continue/notify, WP-4 tier 2, WP-5 loop owners + CLI} → WP-6 validation → WP-7 docs | B (B3 for pump) |
| **Z** #280 | `gh280-zrcp` | design-zrcp WP-1 transport+session → {WP-2 formatters, WP-3 control/run, WP-4 breakpoints+conditions (needs S1), WP-5 history/coverage/load} → WP-6 fixtures+docs | B; WP-4 on S1 |
| **G** #281 | `gh281-gdb-rsp` | design-gdb WP-1 codec → WP-2 target description + packing → WP-3 server → WP-4 wiring/CLI → WP-5 acceptance row + user guide → WP-6 wiki listing (post-release) | B |
| **S** #26 (+#279) | `gh26-dsl` | design-dsl WP1 lexer/parser/`compile_expr` library → WP2 evaluator + snapshot stacks → WP3 engine over subscriptions, stop/exit policy → WP4 CLI + man page → WP5 GUI (Script tab, Alt+1..8) → WP6 recorder (= #20) → WP7 demos + `script-*-func` rows → WP8 docs | B; WP5 on Q |
| **R** #20 | folded into S (WP6) after re-scope | recorder, `compare_scr`, the two DAPR rows | S |

Shared infrastructure the three servers use (transport over the `esp::`
seam, `--debug-listen-address`) is written once in **D** (first server) and
reused by Z and G; if Z or G lands first it carries it instead.

### 10.2 Dependency graph and order

```
   design (#277) ── review ──► B backend ──┬──► Q  #278 Qt refactor (sufficiency proof)
                                           ├──► D  #12  DZRP  ─┐ shared transport, first to land carries it
                                           ├──► Z  #280 ZRCP  ─┤ (Z WP-4 also needs S WP1)
                                           ├──► G  #281 GDB   ─┘
                                           └──► S  #26  DSL (+#279) ──► S WP6 = #20 recorder
                                                                         (S WP5 GUI needs Q)
```

Recommended order: **B**, then **Q** immediately (it is the proof and the
cheapest point to find what B got wrong — any insufficiency is a finding on
this document, not a side channel); D, Z, G and S can start in parallel the
moment B is on `main`, four agents at most (owner's concurrency limit). Q
WP1 (the header move) is independent of B and may land first.

### 10.3 Per-branch merge gate (the standing protocol)

Dedicated branch + worktree under `~/tmp/worktrees/<name>`; a multi-stage
issue lives on **one** branch until done; `make clean && make gui-release`,
`make unit-test`, `make unit-test-sdl`, FUSE 1356/1356, `JNEXT_TEST_JOBS=4
make regression`, `make build-matrix` where a build gate moved; **independent
review** in its own worktree, binary verdict; `make bench` for any branch
touching the hot path; the manager merges one branch at a time and runs `make
bump-patch`; never push without authorisation.

---

## 11. Out of scope, and what needs a prototype

**Out of scope** (§1.2 restated with owners): plugin ABI (closed); DeZog
protocol extension (optional, later, unblocked); script-driven stepping or
machine mutation (the DSL's own v1 rule); a debugger thread; ZEsarUX's full
command surface; real-gdb z80 target compatibility beyond not being rejected
at connect (§7.4); the z88dk wiki listing (post-release PR).

**Cannot be known without a prototype** (each names the branch that measures
it):

1. The exact `MemSpace::Rom` enumeration on 48K/128K/+3 (ROM object vs
   `rom_in_sram_`) — B1.
2. Whether a 16-entry latch ring suffices for a DMA-burst slot — B2.
3. The hot-hit cost **with delivery** (drain + predicate + handler); the
   prototype measured the latch only (§6) — B2, `make bench`.
4. Whether one `NextReg::write` hook sees Cpu/Copper/Dma writers on all
   three call paths, including the deferred-CPU flush — B2.
5. The `render_layer` move: the 106 DVP rows against the moved function
   before the widget changes — Q WP4d.
6. Paused-state service cadence for DeZog step-out loops (a ~2 ms re-armed
   tick while paused-with-remote, or not) — D WP-6's V-LAT measurement.
7. Upstream-master `z88dk-gdb` `monitor` handling (designed from source, run
   only against v2.4) — G WP-5.
8. Whether the DSL's `compile_expr` library covers ZRCP's honoured condition
   subset without a fallback parser — Z WP-4.

---

## 12. Open questions for the owner

Decisions already taken are not repeated. Each question states the design's
default so silence is not a blocker.

1. **Where does the backend implementation live?** Recommended: a new
   sub-issue of #276 on its own branch (`gh276-backend`), so #12/#280/#281/
   #26 can start the moment it lands and #278 stays the proof. Alternative:
   stage 1 of #278's single branch, which serialises the four frontends
   behind the whole Qt refactor.
2. **Headless exit code for a script `stop` with no explicit `exit`.** Never
   2 (a harness fault in both harnesses). Default 1 (same as a failed
   `--load`); design-dsl recommends a dedicated 3 so a row can tell "the
   script caught something" from "jnext could not run" by code alone.
3. **Should a remote (DZRP/ZRCP/RSP) pause open the local Qt debugger
   window?** Today's GH #219 path opens it on any pause with the window shut;
   default: yes, unchanged; alternative: a one-line `by`-check in the Qt
   adapter.
4. **Client ownership on detach.** Default: a client's subscriptions die
   with it and its own pause is released. Alternative: everything persists
   until explicitly cleared.
5. **Memory panel "slot view".** "Slot 3 (page 0A)" reads CPU addresses
   through the live map, not the physical page. Pinned as-is for #278;
   should it become a `MemSpace::Page` read afterwards?
6. **Port defaults.** All three servers off unless `--*-port N` is given
   (DeZog assumes 11000 for `cspect`, 10000 for `zrcp`; z88dk has no
   convention). Keep explicit?
7. **ZRCP `hard-reset-cpu` = cold boot** (Task 70 semantics, seconds of
   emulated time under DeZog's default `resetOnLaunch: true`). Default:
   faithful, and document `resetOnLaunch: false` when `--load` is used.
8. **ZRCP `get-version` string** `12.0-jnext-<ver>` — the only shape DeZog's
   semver gate accepts that is also truthful about the served subset.
9. **GDB `monitor in/out`** (the only perturbing monitor commands): keep,
   labelled (default), or drop. **GDB `k`:** detach (default) or exit jnext.
10. **DSL `--map`** also loads the debugger's symbol table (one table, not
    two) — default yes. **DSL mutation ban** — confirm no `poke`/`set_reg`,
    or allow a loudly-logged one a CI row can forbid with a flag.
11. **Alt+1..Alt+8 as the DSL host-key namespace** in both windows — design-qt
    finds no collision; confirm.

---

*Appendices (working notes, not normative on their own):*
`debug-subsystem/backend.md` (the CAP contract as negotiated, with the 73-row
requirements ledger), `qt-frontend.md`, `dzrp-frontend.md`,
`zrcp-frontend.md`, `gdb-rsp-frontend.md`, `dsl-frontend.md`.
