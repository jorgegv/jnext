# Debugger backend — design (working file)

> Status: **v7 — CONVERGED; owner review folded; review round 4 folded.** All five frontends replied MAPPED against v3 with 0 reach-arounds (qt 40/15, dzrp 30 commands/5+3, zrcp 67/7, gdb 21/24, dsl 35/16); 89 REQs answered; hot-path measured. Assembled into `doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md` (GH
> #277, epic #276). Owner of this file: the backend design agent. Frontend
> agents own one sibling file each (`qt-frontend.md`, `dzrp-frontend.md`,
> `zrcp-frontend.md`, `gdb-frontend.md`, `dsl-frontend.md`) and send
> requirements as `REQ-<frontend>-<n>`; every REQ is recorded in §12 with a
> disposition. The converged design is assembled into
> `doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md`.
>
> **Revision log**
> - B4 implementation notes (2026-09-28, sequencing and precision only — no CAP
>   added or removed; each is argued at its site in the code and in the B4
>   report): **INS-20** is recorded in `step_one_instruction()`, the one body
>   `run_frame()`, the debugger's Step and `execute_single_instruction()` share,
>   not in `run_frame()`'s attached-gated block (a Step never passes through it,
>   so every stepped instruction would be missing), and only for a slot that
>   fetched its opcode; off is one pointer test; switching it off clears it.
>   **REQ-dsl-20** needed the `Frame` drain moved BEFORE `tick_auto_type()` in
>   `end_of_frame()` (B2 had it after, so an `on frame N` pulse landed a frame
>   late); IN-02 level sets are queued on `Keyboard` and applied first in that
>   tick; the drain-to-tick window is not a frame boundary for the save verbs.
>   IN-03/IN-04 stay immediate (the contract names IN-01/IN-02 only; decided
  2026-09-28, B4 O3 — the DSL's "joystick at the next frame boundary" is the DSL
  engine's to honour by queueing its own action, not a backend contract).
>   **CAP-CAP-01** is queued by the verb and written by `pump()`; the PNG/`.SCR`
>   writers moved to `src/core/` (the backend may not reach up into
>   `src/platform/`); `NoFrame` had no carrier in the frozen API — owner decision
  2026-09-28 (O1): `Result flush_captures(ClientId by)` added to `debugger.h`, the
  exit bound that reports a pending (dropped) or failed capture.
>   **CLI `--delayed-*`** (owner decision O2): the loop owners keep their
  loop-tick countdowns; only the actions route through `press_key`, `press_nmi`,
  `screenshot` + `flush_captures`, `save_snapshot`. `press_nmi` now calls the
  F9/F10 hotkey functions themselves (B1 strobed `NmiSource` directly and so
  skipped F10's DivMMC-port gate). The loop owners host one `Debugger` each.
  **CAP-CAP-03** re-saving a held name replaces it, also at the bound; the
>   bound is checked before any advance. **CAP-CAP-04** is synchronous (the
>   `--delayed-snapshot` "always advance" rule) and dispatches through
>   `save_snapshot_file()` in `src/core/`. The ST-01 advance's SES-06 line is
>   `MUTATE clock (<verb> advanced to the frame boundary) <from> -> <to> by <c>`.
> - v1 (2026-09-26 evening): first API draft with capability IDs.
> - v2 (2026-09-26 night): 59 REQs from dsl/gdb/dzrp/qt answered (§12) —
>   additions only, no CAP removed or reshaped: memory access bitmask,
>   Transient subscriptions, physical-page qualifier on Execute, `matched[]`
>   in `Paused`, IntAck/Nmi/Host kinds, `Result::RefusedUnavailable`,
>   `rewind_blocked()`, CAP-CTL-14 magic switch, CAP-INS-18 `set_border`,
>   CAP-INS-19 `machine()`, CAP-CAP-04 `save_snapshot`, CAP-SES-06 `log`,
>   `ExitRequested`, per-client `live_raster`, `pump()` after the frame batch.
>   Finding F2 (frame counter dead without rewind) verified. §8 carries the
>   measured hot-path numbers from two interleaved A/B runs: no-watch cost
>   none; armed-and-hitting 5.0 % today vs 1.4 % with the slot mask.
> - v3 (2026-09-26 night): 14 ZRCP REQs answered — additions only: draining
>   `pump(PumpBudget)`, per-client subscription switch, `probe_execute(pc)`,
>   `Paused.matched` as `Hit{}`s, richer `TraceEntry`, CAP-INS-20 coverage,
>   palette/sprite/pattern debugger writes, `clip_window(Layer)`, CAP-CTL-15
>   `load(path)`, named in-memory bookmarks, headless stop policy decided.
> - v7 (2026-09-27, after review round 4, round-5 notes folded — ring sizing
>   reworded, `space_offset`, un-shifted `ram_` index, NR 0x8C, delivery
>   handlers under `InspectionScope`, REQ-dsl-23 row says 512; `scratchpad/reviews/arch-r4.md` +
>   `dsl-qt-r4.md`): the latch ring is 512 with a derivation that includes the
>   Copper's per-master-cycle cadence and a MOVE as one fanned-out entry, and
>   overflow is a specified, tested behaviour (R-1); `set PC` clears `halted`
>   is an implementation obligation (`z80_cpu.h:135`); the `NextReg::write`
>   hook must be `guest_access`-gated; the gate and drain are off in
>   `replay_mode_`; mutations refused under RZX; the `PC = B` bypass stated;
>   `Copper.Halt` is a new branch at the stall path; `Dma.Start` is one
>   transition; `Mem{Write}` carries `prev`; `Port` carries `source`;
>   `SlotInfo.space` and the `Rom` enumeration settled from the code (§11 item
>   1 closed); tallies fixed (qt 40/15, dsl 35/16).
> - v6 (2026-09-27, owner review 2026-09-27): the thirteen owner questions
>   answered and recorded in §1 (items 9-23), §13 struck; mutation from scripts
>   allowed — §4.2a write contract; Copper MOVE/WAIT/HALT and DMA start/end
>   event kinds with hook citations, no-subscriber cost and a bench row; DZRP
>   2.2.0; Developer Guide gate item and DOC package (architecture doc §10).
> - v5 (2026-09-26, after round 2, `scratchpad/reviews/arch-r2.md`): a client's
>   `reset(Hard)` never pauses a running machine and no `Reset` pause reason
>   exists — a blocked `run` completes from the `Reset{Hard}` event (R2-2);
>   B0 headers are their own branch (R2-1, in the architecture doc); platform-side
>   `BreakpointSet` restore retired in B3 [B3 milestone 2: only its event-mask
>   half; the Qt half is package Q's — see CAP-CTL-12], `ColdBootHooks` at `:179`, flag poll
>   precedes `pump`, APPEND honours the 16-entry cap and the in-flight entry,
>   `load()` routed through the loop driver (no platform include), bookmark
>   cost and reconstruct survival stated, SDL any-stop question widened, dzrp
>   26 in the ledger; REQ-dsl-20 injection ordering recorded. Round 3
>   (`arch-r3.md`, APPROVE): `queue_auto_type` itself becomes append for every
>   producer (phantom typist, tape auto-type, backend).
> - v4 (2026-09-26, after the arch reviewer's REJECT, `scratchpad/reviews/arch.md`):
>   one detach rule (R-1); §8.3 relabelled — the armed rows were a single-address
>   cold-hit scan, not a hot hit (R-2); CPU `NextRegWrite` delivery specified as
>   ≤1 instruction late with the payload captured in the latch (R-3); every
>   citation regenerated from the tree (R-4); two owner questions restored to
>   §13 (R-5); §10-equivalent ownership fixed in the architecture doc (R-6);
>   ring 32 by construction, DMA/`set_matrix_bit` accessors named, CAP-CAP-01
>   and CAP-SES-04 corrected, REQ-dsl-18/19 answered.

Every claim about the code below carries a `file:line` citation into the
worktree at `main @ 974b0ab19`. Claims are marked **[verified]** (read in the
source) or **[inferred]** (a conclusion drawn from what was read). Nothing here
is taken from `doc/design/EMULATOR-DESIGN-PLAN.md` §5.10, whose
`DebuggerInterface` does not exist [verified: `grep -r DebuggerInterface src/`
returns nothing].

---

## Work packages — the tracker for this package

Mirrors this package's row in [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
§10.1, which stays authoritative: if the two ever disagree, §10.1 wins and this
table is stale. It exists because §10.1 states each package's sequence as one
long table cell, which is unreadable as a plan and impossible to track against.

Status values: `todo` · `in progress` · `in review` · **`done`** (independently
reviewed and APPROVED). The whole package lands on **one branch** and merges
whole, so `done` here means the sub-item is approved, not merged.

| WP | Branch `gh276-backend` (issue #286) | Status |
|---|---|---|
| **B1** | facade + control + inspection over the existing primitives, **no hot-path change**: `Mmu::peek()` (F1), the frame counter (F2), the `SymbolTable` instance move, `key_name_to_matrix` move, and the §4 accessor additions (raw sprite/palette forms, public `set_matrix_bit`, the DMA slot flag, `input_state`) | **done** — 3 review rounds, APPROVED 2026-09-27 |
| **B2** | `EventTable` + the **512-entry latch ring** (its overflow a tested path, not a comment) + slot masks + `on_slot_remapped` + the NR / port / IntAck / Nmi / Reset / Frame / Scanline hooks, and the Copper / DMA kinds with their per-engine armed flags. **Bench-gated, including §11 item 3's hot-latch measurement**, which has never been taken | **done** — independently reviewed, APPROVED |
| **B3** | session: clients, listeners, `pump` + `Service` registration, stop policy, `live_raster` / `attached`, the loop driver (SES-07), the reconstruct contract (CTL-12/15), and retiring the platform-side `BreakpointSet` / `active()` restore in `emulator_cold_boot()`. Owns SES-06's own log channel | **done** — independently reviewed, APPROVED |
| **B4** | input (IN-01 APPEND) / capture / bookmarks / coverage / extended `TraceEntry`, and the `--delayed-*` CLI conveniences re-expressed as generated subscriptions in all three loop owners, retiring `QtApp`'s and `HeadlessApp`'s private countdowns. Owns ST-01's SES-06 line for the advance | **done** — independently reviewed, APPROVED |
| **B5** | `debugger_backend_test` as the package's sign-off — the §9 row families end to end (delivery, transient, multi-client, reconstruct, pump, APPEND) | in review |

Depends on: **B0** (#285, landed v1.0.46). Blocks: T, Q, D, Z, G, S.

**Carried forward from B1's review, for every later sub-item:** derive the
mutation list from the **diff**, never from your own row list; pin **both arms**
of every conditional; never read a *derived* quantity where an unambiguous one
is a line away (`frame_tag()` clamps, `frame_num()` does not); and verify the
premise of a justification comment the way you would verify code — three of
B1's defects hid behind a confident sentence.

Every sub-item is reviewed by an agent or person that did NOT write it, and the
branch does not merge until the full §10.3 gate is green on the tip.

---

## 0. Conventions

- **CAP-xxx-nn** — a backend capability. Prefixes: `CTL` control, `INS`
  inspection, `EVT` events/breakpoints, `TIME` deterministic time, `IN` input
  injection, `CAP` capture, `ST` state bookmarks/rewind, `SYM` symbols, `SES`
  session/clients. Adapters map their protocol onto CAP IDs and declare which
  they *decline* (a legitimate outcome — the owner's projection principle).
- The backend is one C++ facade, **`jnext::dbg::Debugger`** (working name),
  plus value types. It is *not* a protocol, *not* a wire format, and carries
  none of DZRP's, ZRCP's, RSP's or the DSL's models.
- "Instruction boundary" = the point in `Emulator::run_frame()` /
  `step_frame_slot()` immediately after `step_one_instruction()` returns and
  before `tick_devices_after_instruction()` [`src/core/emulator.cpp:9392-9414`,
  `:10453-10456`]. Every event the backend delivers is delivered there, with the
  machine stopped — see §4.3 and §5.

---

## 1. Settled owner decisions (recorded, not re-litigated)

Quoted from the issue dumps (`gh276.md`, `gh277.md`, `gh279.md`, `gh12.md`):

1. **No plugin API.** "There will be no plugin API — no C ABI shared library,
   no dlopen/LoadLibrary host, no scripting binding exposed as a plugin
   interface." #279's use cases become **requirements on the DSL's
   vocabulary** (#26). Revisitable only on evidence that the DSL cannot express
   a real use case.
2. **The DSL is the event primitive** ("which is the primitive? — CLOSED in the
   DSL's favour").
3. **#12's protocol is DZRP** (decided in #12's thread with the reporter,
   dcrespo3d and jattree). Socket transport, not serial. Partial server via
   `CMD_INIT` subset negotiation is legitimate.
4. **Governing principle:** "The backend is the union of what jnext can do.
   Each frontend is a projection of that onto what its protocol or its users
   can express." A capability is never shaped by the poorest adapter; no
   adapter may reach around the backend.
5. **Conditions live in the backend; the DZRP adapter declines them** (DeZog
   evaluates conditions client-side and wants an unconditional pause).
6. **Reverse debugging stays in the backend**; DZRP has no verb for it and
   serves none of it. **The upstream DeZog conversation is not a
   prerequisite.**
7. **Deterministic time (frames, T-states) is the unit throughout; never
   wall-clock.**
8. **No functional regression in the Qt GUI** (#278 is judged panel by panel).
9. **The backend needs its own headless tests** — #203 is the precedent.

**Owner review of 2026-09-27** (the §13 questions, answered; numbered 10-24 here = the architecture document's §1.3 items 9-23): (10) two
branches `gh276-headers` + `gh276-backend`; (11) headless script-stop exit
code **3**; (12) headless Stop with a remote connected = pause + notify; (13)
one rule for every stop — SDL stops and headless magic breakpoints exit
non-zero, man-page line, `magic-bp-func` re-pinned; (14) a remote pause opens
the Qt window; (15) detach = subscriptions and own pause die with the client;
(16) the Memory panel slot view becomes a physical-page read/write view as
#278's last work package; (17) ports explicit; (18) ZRCP hard reset = cold
boot; (19) `12.0-jnext-<ver>`; (20) `monitor in/out` kept labelled, `k` =
detach; (21) `--map` feeds the one symbol table and **mutation from scripts
is allowed** (§4.2a); (22) Alt+1..8; (23) DZRP answers 2.2.0; (24) the
finished subsystem is documented exhaustively in the Developer Guide.

---

## 2. The code as it is (measured)

### 2.1 The control surface today

- `DebugState` [`src/debug/debug_state.h:10-327`] holds `paused_`, a
  `StepMode` (`NONE, INTO, OVER, OUT, RUN_TO_CYCLE, STEP_BACK,
  RUN_BACK_TO_CYCLE` [`:6`]), the `BreakpointSet`, the two hot-path gates
  `armed_` / `wp_live_` [`:273-278`, members `:307`, `:317`], the GH #221
  step-off arm [`:300-303`, member `:319`], and the data-breakpoint latch
  `data_bp_hit_` / `data_bp_addr_` [`:252-255`, members `:320-321`].
  It is pure C++ and already the *execution-control* half of a backend.
- The **verbs** that turn those into user-facing behaviour live in a
  `Q_OBJECT`: `DebuggerManager::on_run/on_pause/on_step_into/on_step_over/
  on_step_out/on_run_to_eof/on_run_to_eosl/on_step_back/on_rewind_to_frame`
  [`src/debugger/debugger_manager.h:62-72`]. Their bodies contain the actual
  semantics a headless consumer needs and cannot reach today [verified]:
  - Step Over: `is_call_like()` → one-shot at `PC + instruction_length()`,
    else Step Into [`debugger_manager.cpp:413-448`].
  - Step Into: `Emulator::debugger_step()` (frame-loop-aware, runs a HALT out,
    GH #207) [`:377-411`, `emulator.cpp:10500-10591`].
  - Run to EOF: target = midpoint of the last *visible* raw line
    (`FB_HEIGHT-1 + vblank_top()`), or the same line next frame if already past
    it [`:475-518`]. Run to EOSL: next raw line start, or next frame start past
    the last visible row [`:520-560`].
  - Every resume passes the Task 60e corruption gate
    `confirm_resume_if_corrupt()` (a modal, over the pure `ResumeGuard` policy
    [`src/debug/resume_guard.h`]) [`:297-324`].
  - Step Back / Rewind to Frame call `Emulator::step_back(1)` /
    `rewind_to_frame(n)` directly and refuse under RZX
    (`rzx_blocks_rewind`) [`:562-622`]. A *benign* failure (empty buffer,
    trace off, frame out of range) is silent; only `last_state_error()`
    raises the corruption modal [`:275-295`].
- The **hot loop** consults `DebugState` at three points [verified]:
  before the instruction — `should_break(pc)` after `consume_step_off()`,
  then `StepMode::INTO`, then `RUN_TO_CYCLE` vs `clock_.get()`
  [`emulator.cpp:9301-9343`]; after it — the `data_bp_hit()` early return
  [`:9398-9406`]; and inside the shared body — the GH #203 Step Out predicate,
  gated on `fetched_opcode_last_execute()` [`:9975-9986`]. `STEP_BACK` /
  `RUN_BACK_TO_CYCLE` are consumed *before* the loop, by rewinding
  [`:9251-9260`]. `DebugState::active()` gates the step machinery AND the
  render hint AND the per-instruction `VideoTiming::advance()`
  [`:9251`, `:9916`, `:9552`, `:10024`] — one flag, three unrelated
  consumers (design-qt finding, REQ-qt-01c).
- Magic breakpoint: `cpu_.on_magic_breakpoint` sets `active` and pauses
  [`emulator.cpp:7880-7885`]; `--persistent-breakpoints` sets `persistent_`
  from `EmulatorConfig` [`:1122`].

### 2.2 The inspection surface today

- 14 panel headers hold `Emulator*` (33 occurrences counted with
  `grep -c "Emulator\*" src/debugger/*.h`: video 4, manager 3, window 2, the
  other twelve 2 each) [verified].
- What the panels actually *call* on it (uniq count over
  `src/debugger/*.cpp`): `debug_state()` 55, `mmu()` 20, `rewind_buffer()` 15,
  `trace_log()` 9, `cpu()` 9, `frame_num()` 8, `last_state_error()` 4,
  `video_timing()` 3, `call_stack()` 3, `timing()` 2, `nextreg()` 2,
  `current_frame_cycle()` 2, `clock()` 2, and one each of `ula()`,
  `turbosound()`, `sprites()`, `copper()`, `palette()`, `renderer()`,
  `snapshot_raster()`, `set_audio_mute_mask()`/`audio_mute_mask()`,
  `rzx_player()`/`rzx_recorder()`, `debugger_step()`, `step_back()`,
  `rewind_to_frame()`, `rewind_enabled()`/`set_rewind_enabled()`,
  `resize_rewind_buffer()`, `state_error_generation()` [verified].
  design-qt's `qt-frontend.md` §1 inventories all 93 core accesses; §4.2
  below is derived from both.
- Two panel reads are **not side-effect free** today, which matters because
  observation must not perturb (Task 40):
  - `Mmu::read()` latches the +3 floating-bus byte on every contended read,
    inside *and outside* `GuestExecutionScope`
    [`src/memory/mmu.h:405-406`, `:514-515`] — a Memory-panel refresh in +3
    mode rewrites `p3_floating_bus_dat_`. **[verified; finding F1]**
  - NextREG reads are already handled: panels use `NextReg::peek()`, which
    exists precisely because `read()` runs destructive read handlers and logs
    [`src/port/nextreg.h:52-63`] [verified].
- Watchpoint gating is triple: `debug_state_ && watchpoints_live() &&
  has_any_watchpoints()` before any per-address scan, at eight `Mmu` sites and
  in `PortDispatch::check_io_watchpoint_` [`mmu.h:258-260` and seven more,
  `port_dispatch.cpp:27-36`] [verified]. `watchpoints_live()` is
  `armed_ && guest_access_` [`debug_state.h:275`], so a panel read can never
  raise the latch — that property is kept by construction in §4.3.

### 2.3 The event vocabulary today, and its limits

`BreakpointSet` [`src/debug/breakpoints.h`]: PC breakpoints (`unordered_set`
live cache [`:224`]), watchpoints as a `vector<Watchpoint{addr, type, enabled}>`
[`:11-18`, live cache `:225`] with `READ/WRITE/READ_WRITE/IO_READ/IO_WRITE`
[`:9`], **one** one-shot [`:194-197`, members `:226-227`], a master switch
[`:191`], observers. Per-address, unconditional, linear scan when armed
[`breakpoints.cpp:105-116`]. **No range, no value predicate, no
NextREG-write event, no frame/scanline/cycle event, no physical-page filter**
[verified — #279's description of the gap is accurate]. The latch carries only
the address, not the value or the writer's PC [`debug_state.h:320-321`].

Where the NextREG write paths are, and **when they commit** (this decides
§4.3's `NextRegWrite` delivery): the CPU's port-`0x253B` and `NEXTREG`-opcode
writes are deferred through `enqueue_cpu_nr_write()` while
`defer_cpu_nr_writes_` is set — set at `:9933` and cleared at `:10001`, both
inside `step_one_instruction()` [`emulator.cpp:4746-4770`] — and committed by
`flush_pending_cpu_nr_writes()` at `:10221`, **inside**
`tick_devices_after_instruction()` (`:10193`), i.e. *after* the instruction
boundary of §0; only the GH #272 row-boundary sub-flush (`:11481`) commits some
of them earlier. `enqueue_cpu_nr_write(0xFF, …)` at `:6433` is a fourth
enqueue site (the ULA border/palette shortcut). The Copper writes
synchronously via `Copper::execute(hc, vc, NextReg&)` → `NextReg::write`, with
`active_move_hc()` non-negative only for that call
[`src/peripheral/copper.h:55`, `:131`]; a DMA transfer that targets port
`0x253B` also writes synchronously (`defer_cpu_nr_writes_` is false in a DMA
slot). `NextReg::write_selected` is `write(selected_, val)` [`nextreg.cpp:437`],
so **one hook in `NextReg::write` sees every writer** [verified]; Copper is
identifiable at the hook (`active_move_hc() >= 0`), Cpu vs Dma only from the
slot's DMA flag (see §4.3).

### 2.4 Time and the frame loop

- `frame_num_` [`emulator.h:947`, `:1533`] is incremented at **exactly one
  site**, inside `if (rewind_buffer_ && rewind_enabled_ && !replay_mode_)`
  [`emulator.cpp:8466-8468`], so `Emulator::frame_num()` stays 0 for the
  whole run unless `--rewind-buffer-size` is given. `HeadlessApp` and `QtApp`
  each keep a private frame count for their `--delayed-*` countdowns.
  **[verified; finding F2 — raised by design-dsl, REQ-dsl-16]**. The fix is
  local: increment unconditionally at that site (it is already the once-per-
  logical-frame point, replayed frames included, and already in the snapshot
  stream), tag the rewind slot with the pre-increment value — rewind
  behaviour is unchanged.
- `clock_.get()` is the 28 MHz master cycle; `current_frame_cycle()` is the
  frame start; `timing_.master_cycles_per_line/frame` give the conversions
  [`debugger_manager.cpp:475-560`]; `Emulator::monotonic_tstates()` is frame
  base + live FUSE counter [`emulator.h:500`, `emulator.cpp:7984-7990`] and
  is the right "T-state clock" because `cpu_divisor` changes under NR 0x07
  (design-gdb, REQ-gdb-10).
- A paused frame is **resumed, not restarted** (`frame_in_progress_`)
  [`emulator.cpp:9264-9296`] — Task 40; any control verb design must keep that.
- While paused the frontends stop calling `run_frame()` altogether
  [`src/platform/frame_sequencer.h` step (6): `if (!fx.paused())`;
  `emulator.cpp:10502-10506`], so `debugger_step()` is the machine's only driver.

### 2.5 Input injection and capture today

- `--delayed-keypress[-frames]` resolves a key *name* to matrix positions in
  `key_name_to_matrix()` [`src/platform/headless_app.cpp:216-258`]
  and injects via `Keyboard::queue_auto_type({row1,col1,row2,col2, 5 frames})`
  at a frame countdown [`:557-569`]. `queue_auto_type` **replaces** the queue
  (`auto_queue_ = keys`, [`keyboard.cpp:541`]), so two presses scheduled for
  the same frame keep only the second, and a press issued while one is held
  strands the first key down (design-dsl's review finding; REQ-dsl-18).
  `--delayed-nmi` goes through the same hotkey seam the GUI uses
  (`on_hotkey_f9_mf_nmi/f10_divmmc_nmi`) [`:573-590`].
- `--delayed-screenshot[-frames|-layers]` arms `Renderer::set_layer_mask()` for
  the one frame before capture [`:593-597`] and writes through
  `save_screenshot()` [`src/platform/screenshot.h:53`, `headless_app.cpp:705-725`];
  in the GUI a capture that comes due while the debugger is paused is
  **deferred** with a one-time warning [`qt_app.cpp:622-636`] and fails the
  run only if `--delayed-automatic-exit` arrives first
  (`auto_exit_finds_no_deferred_work`, `:647`); headless writes what is in the
  framebuffer [`headless_app.cpp:704-712`]. `--delayed-snapshot` saves a
  snapshot at a frame boundary [`headless_app.cpp:728-810`].
- Joystick state is settable as raw 12-bit vectors:
  `Joystick::set_joy_left/right(uint16_t)` [`src/input/joystick.h:113-117`].
  Keyboard rows are readable (`Keyboard::read_rows` [`keyboard.h:68`]) but the
  matrix itself is private [`:182-185`].
- The **GUI** implements the same delayed actions separately in
  `QtApp::TickEffects::post_frames()` [`src/gui/qt_app.cpp:581-632`] — two
  implementations of "do X at frame N" already, which §4.5 removes rather than
  adds to.

### 2.6 State and rewind

`Emulator::save_state/load_state` over `StateWriter/StateReader`
[`src/core/saveable.h`, `emulator.h:776-793`]; fixed-width stream; snapshots
only at frame boundaries [`rewind_buffer.h:11-16`]; a mid-frame save advances
to the boundary under `DebugState::SuspendScope` [`debug_state.h:133-162`,
`emulator.cpp:11902-11908`]; `frame_in_progress()` is queryable
[`emulator.h:191`]. Failed restores latch `last_state_error()` and bump
`state_error_generation()` [`emulator.h:895-901`]. `RewindBuffer`
[`src/debug/rewind_buffer.h`] + `Emulator::step_back(n)` / `rewind_to_frame` /
`rewind_to_cycle` [`emulator.h:958-966`]; refused under RZX
(`rzx_blocks_rewind`) [`:972`].

### 2.7 Build configuration today

- `jnext_debug` (`src/debug/`) is linked by `jnext_core` **unconditionally**
  [`src/core/CMakeLists.txt` `target_link_libraries(jnext_core PUBLIC ...
  jnext_debug ...)`], so the backend primitives exist in every configuration
  including SDL-only [verified]. It links SDL3 only because
  `rewind_buffer.cpp` includes `core/emulator.h` [`src/debug/CMakeLists.txt:8-12`].
- `ENABLE_DEBUGGER` gates only `src/debugger/` (Qt panels)
  [`CMakeLists.txt:335-337`, `:495-498`]; `ENABLE_QT_UI` gates `src/gui/`.
- The two Qt headers in `src/debug/`: `debug_keymap_qt.h` (included by
  `src/gui/host_chords.cpp`, `shortcut_capture_button.cpp`, `main_window.cpp`,
  `test/debugger/keymap_test.cpp`) and `menu_bar_alt_nav_qt.h` (included by
  `src/debugger/debugger_window.cpp`, `src/gui/main_window.cpp`) [verified by
  grep]. Both are header-only; `jnext_debug` never compiles them.

### 2.8 Tests that pin today's behaviour

Core (Qt-free): `rewind_test 261`, `resume_guard_test 11`, `step_out_test 50`,
`persistent_bp_test 18`, `io_watchpoint_test 25`, `bp_enable_test 23`,
`resume_step_off_test 19`, `raster_state_test 86`, `snapshot_test 297`.
Qt-gated (`?`): `debugger_video_panel_test 106`, `debugger_audio_panel_test 15`,
`debugger_quit_gate_test 5`, `debugger_persistent_bp_test 5`,
`debugger_inspect_watchpoint_test 18`, `debugger_window_size_test 21`,
`debugger_window_grow_test 4`, `debugger_accel_test 8`, `debugger_keymap_test
34`, `debugger_menu_test 45`, `debugger_disasm_copy_test 33`, `host_hotkey_test
45`, `preferences_apply_test 56` [`test/unit-tests.conf`]. All must keep their
pinned counts across #278 (a count change is a deliberate manifest edit).

The **#203 shape** already exists and is the model for every backend verb:
`step_out_test`'s `STPOUT-W*` rows arm the verb through the same `DebugState`
the debugger uses, drive a real `Emulator` with a RAM program through
`run_frame()`, and assert the machine *actually stops* — "every STPOUT-W row
fails on the pre-fix tree" [`test/debug/step_out_test.cpp:1-20`]. The
predicate-only rows (`STPOUT-P*`) would have stayed green through #203.

---

## 3. Architecture

```
                 ┌──────────────────────────────────────────────────────┐
  frontends      │ Qt panels │ DZRP srv │ ZRCP srv │ GDB RSP srv │ DSL  │
  (adapters)     │ src/debugger  src/remote/…  src/remote/…  src/script │
                 └──────┬─────────┬──────────┬───────────┬─────────┬───┘
                        │  all through ONE facade, no Emulator* below  │
                 ┌──────▼─────────▼──────────▼───────────▼─────────▼───┐
  backend        │ jnext::dbg::Debugger  (src/debug/, target jnext_debug)│
  (this design)  │  ├ control  (§4.1)   ├ inspect (§4.2)  ├ events (§4.3)│
                 │  ├ time (§4.4)  ├ input/capture (§4.5)  ├ state (§4.6)│
                 │  └ session/clients (§4.8)                             │
                 │  internals: DebugState, EventTable (ex BreakpointSet),│
                 │  TraceLog, CallStack, SymbolTable, RewindBuffer, disasm│
                 └──────────────────────────┬────────────────────────────┘
                                            │ hooks (instruction boundary,
                                            │ MMU/port/NR latches, frame edges)
                 ┌──────────────────────────▼────────────────────────────┐
  core           │ Emulator (owns DebugState today; owns the latch state  │
                 │ tomorrow — no Emulator code learns what a frontend is) │
                 └───────────────────────────────────────────────────────┘
```

Principles that fall out of §2 and the settled decisions:

- **One facade, in `jnext_debug`, built in every configuration.** It already
  is (§2.7); the design adds the facade next to the primitives rather than a
  new library, so the SDL-only build gains a debugger backend for free and
  headless tests need no Qt.
- **Emulator keeps the hooks, the backend keeps the policy.** The hot loop
  already consults `DebugState`; it will consult an `EventTable` in the same
  three places (§4.3). Nothing in the emulator learns what a frontend is.
- **Pull for state, push for transitions.** Inspection is synchronous pull
  (§4.2). The backend pushes a fixed set of session transitions to attached
  clients — `Paused`, `Resumed`, `Reset`, `FrameEnded`,
  `SubscriptionsChanged`, `ExitRequested`, `Log` — plus per-subscription event
  deliveries (§4.3). Panels refresh on `Paused` (and on their own throttle
  while running, as today); scripts act *inside* an event delivery; remote
  servers turn `Paused` into a notification packet.
- **Single-threaded stays.** Every backend call runs on the emulation thread.
  Out-of-process clients are pumped by the loop owner (§5).
- **Deterministic time only.** Every `when` in the API is a master cycle, a
  frame number or a raster position (§4.4); no API takes milliseconds.

---

## 4. Backend API

Working C++ sketch; names are proposals, IDs are the contract. `Debugger` is
constructed with an `Emulator&` and is the *only* type frontends hold.

`Result` ∈ { `Ok`, `RefusedRunning`, `RefusedPaused`, `RefusedCorrupt`,
`RefusedRzx`, `RefusedUnavailable` (benign: empty rewind buffer, trace off,
frame out of range — REQ-qt-08b), `RefusedReadOnly`, `InvalidPage`,
`NotAtFrameBoundary`, `NoFrame`, `Unsupported` }. Every verb returns one
instead of silently doing nothing — the GUI's `if (!enabled_) return;` guards
[`debugger_manager.cpp:327`] become adapter-side decisions.

### 4.1 Control — `CAP-CTL`

| ID | Capability | Semantics (from the code) |
|---|---|---|
| CAP-CTL-01 | `pause()` | `DebugState::pause()`; takes effect at the next instruction boundary [`emulator.cpp:9304`]; idempotent. Called from `pump()` (outside `run_frame`) the machine is already at a boundary, so it is effectively synchronous. |
| CAP-CTL-02 | `run()` | Resume; no-op if already running (GH #223 [`debugger_manager.cpp:326-336`]); the GH #221 step-off arm stays in the backend [`debug_state.h:300-303`] — adapters never insert their own temp breakpoints to step off; subject to CAP-CTL-11. |
| CAP-CTL-03 | `step_into()` | `Emulator::debugger_step()` — one instruction, frame-loop aware, runs a HALT out (GH #207). **Synchronous**: returns after the instruction executed, already paused. |
| CAP-CTL-04 | `step_over()` | `is_call_like` → a *transient* `Execute` subscription at next PC and resume; else = CAP-CTL-03. Asynchronous (completes at a later boundary). The DZRP adapter never calls CAP-CTL-03..08: DeZog steps by `CMD_CONTINUE` with its own temp breakpoints, which the adapter serves as transient `Execute` subscriptions + `run()` (design-dzrp). |
| CAP-CTL-05 | `step_out()` | `DebugState::step_out(SP)`; ends per `check_step_out` (GH #203). Asynchronous. |
| CAP-CTL-06 | `run_to(addr)` | Transient `Execute[addr,addr]` and resume (Run to Here; RSP `i<len>` = `run_to(pc+len)`). |
| CAP-CTL-07 | `run_to_cycle(master_cycle)` | `RUN_TO_CYCLE`; the primitive under EOF/EOSL. |
| CAP-CTL-08 | `run_to_end_of_frame()` / `run_to_end_of_scanline()` | The two target computations move verbatim from `debugger_manager.cpp:475-560` into the backend (the *only* place that knowledge lives today, and a script needs them). |
| CAP-CTL-09 | `step_back(n)` | `Emulator::step_back(n)`; synchronous; `RefusedRzx` / `RefusedUnavailable` / `RefusedCorrupt` distinguished. |
| CAP-CTL-10 | `rewind_to_frame(n)` | `Emulator::rewind_to_frame(n)`; same contract. |
| CAP-CTL-11 | resume gate | `resume_blocked_by_corruption() -> optional<CorruptionIncident{subsystem, generation}>` and `acknowledge_corruption(generation)`. The *policy* is `ResumeGuard` (already pure, [`resume_guard.h`]); the modal stays in the Qt adapter. A client that does not acknowledge gets `RefusedCorrupt` (DZRP reports it as NTF_PAUSE reason 255; RSP as `E01`). |
| CAP-CTL-12 | `reset(Hard\|Soft)` | `Soft` = `Emulator::soft_reset()` [`emulator.h:198`], synchronous. **`Hard` is the cold-boot reconstruct contract (REQ-zrcp-15, blocking):** today `request_hard_reset()` only raises a flag [`emulator.h:207`] that each loop owner polls after its tick's frames [`sdl_app.cpp:409`, `qt_app.cpp:510`, `headless_app.cpp:691`] and turns into `emulator_frontend_cold_boot()` [`emulator_boot.h:225-245`], which destroys and placement-news the `Emulator` and deliberately restores nothing transient ("starts fresh and running", [`:122-124`]). The backend makes it **synchronous for a client**: (1) the loop owner registers its cold-boot driver (`ColdBootHooks`, [`emulator_boot.h:179`]) with the backend at start-up (CAP-SES-07); `reset(Hard)` from inside `pump()` — the same post-frames slot the flag poll lives in — runs it before returning, so later commands in the same drain (ZRCP `hard-reset-cpu` → `enter-cpu-step` → `smartload`) see the new machine; (2) the backend re-binds to the reconstructed `Emulator` (same address; fresh `DebugState`/`TraceLog`/`CallStack`) and re-applies every client's subscriptions, switches, `live_raster`/`attached`, call-stack tracking, trace and coverage enables, and the symbol table — the client-owned model makes this mechanical; (3) **paused stays paused, running stays running**: the backend re-applies the pause after the reconstruct (the machine is then at PC 0x0000 of `nextboot.rom`, as ZEsarUX's `hard-reset-cpu` in cpu-step mode); a client's `reset(Hard)` **never pauses a running machine** — there is no `Reset` in `pause_reason`; (4) `Reset{Hard}` reaches every listener before the verb returns; an adapter whose client is blocked in a `run` completes that reply from the `Reset{Hard}` listener event — adapter policy, not a pause, so no other client sees a stop; (5) a **guest-initiated** hard reset (NR 0x02) keeps the deferred path, and the loop owner calls the backend's `on_cold_boot_begin()` immediately before it destroys the machine and `on_cold_boot_done()` after the rebuild, so rules 2-4 apply identically — `begin` captures the pause in force and its owner, exactly what the `reset(Hard)` verb captures before its driver (the frozen header gained `on_cold_boot_begin()` for this, owner decision 2026-09-28: without it rule 3 could not hold here, because by `done` the paused machine is gone); (6) with no driver registered (a bare test harness) → `RefusedUnavailable`. **Ordering within a tick:** every loop owner polls `take_hard_reset_request()` *before* it calls `pump()`, so a guest-initiated reset and a client `reset(Hard)` in the same tick run in that order and the second finds a freshly booted machine (it still runs the driver — a reset of a just-reset machine is legal and observable, not skipped). **Single owner:** once the backend re-applies subscriptions, the platform-side `BreakpointSet`/`active()` save-and-restore in `emulator_cold_boot()` [`emulator_boot.h:133-146`] is a second owner of whatever part of it is backend state, and that part is retired, not left to double-restore. **The retirement is split (B3 milestone 2, sequencing only):** the one part that is backend state before package Q — the event-mask half of `BreakpointSet`'s hot-path gate (`ev_mask_rd_/wr_/port_`) — is **retired in B3** (`emulator_cold_boot()` zeroes it on its copy; the backend's `gates_changed()` is its single owner); the Qt panels' half — their PC breakpoints and watchpoints, the observers that travel on the copy, and `active()` — has no other owner until the panels become clients, and is **retired by package Q** (qt-frontend.md §7, WP2/WP6). The same contract covers CAP-CTL-15 when a `.nex` load routes to the cold boot. |
| CAP-CTL-13 | `state() -> RunState{paused, step_mode, pause_reason, cycle, frame, pc}` | Pull. `pause_reason` ∈ {`User{cid}`, `Breakpoint{event_id}`, `Watch{event_id, access, addr}`, `Step`, `RunTo{event_id}`, `Magic`, `Corrupt`, `Script{event_id, text}`}. |
| CAP-CTL-14 | `magic_breakpoint()` / `set_magic_breakpoint(bool)` | `Emulator::set_magic_breakpoint` [`emulator.cpp:7873-7886`] (REQ-qt-12; the Debug-menu toggle, pinned by `debugger_menu_test` MBP-01/02). |
| CAP-CTL-15 | `load(path) -> Result` | Routed through the **loop owner's registered driver** (CAP-SES-07 `LoopDriver::load`), which today is `emulator_apply_load()` [`src/platform/emulator_boot.h:25`]; the backend never includes `src/platform/` — it sits below that layer (ZRCP `smartload`, REQ-zrcp-12). No driver → `RefusedUnavailable`. When the caller is paused the machine stays paused after the load routine completes, at the new PC; a NEX's boot-hold frames run on the next resume. **Contract (REQ-qt-29):** a load that routes to `emulator_cold_boot()` destroys and reconstructs the `Emulator` in place [`emulator_boot.h:133-146` saves the `BreakpointSet` with its observers and `active()` across `~Emulator()` / placement-`new` / `init()`]; the backend owns every client's subscriptions, enable flags, the master and per-client switches, the attached/live_raster state and the symbol table **outside** `Emulator`, and re-installs its hooks and latch state after the reconstruct, so nothing any client set is lost — a backend row subscribes, loads, and asserts the subscription still fires. A load that **reconstructed** the machine is a cold boot and owes rule 4 of CAP-CTL-12 — `Reset{Hard}` pushed to every listener before the verb returns, and the `Reset{Hard}` event latched; a load that did not reconstruct pushes nothing (B3 milestone 2). |

**Semantics with 0, 1 or N frontends attached** (answering #277):

- **0 attached.** The backend is inert: `armed()` is false unless
  `--persistent-breakpoints` or a magic breakpoint fires; the hot loop pays the
  same load-and-branch it pays today (§8). A magic breakpoint with nobody
  attached pauses the machine exactly as today [`emulator.cpp:7880-7885`]; the
  loop owner's stop policy decides what that means (Qt: open the window —
  `check_breakpoint_hit()` [`debugger_manager.cpp:682-720`]; headless and
  SDL: per CAP-SES-04 — which for a magic breakpoint is a **change** from
  today's behaviour, where a headless magic breakpoint pauses and the run
  continues to `--delayed-automatic-exit` with exit 0 [`emulator.cpp:7880-7885`];
  owner question §13.3).
- **1 attached.** Identical to today.
- **N attached.** One machine, one `DebugState`. **No ownership token and no
  arbitration queue**: any client may pause, resume or step; every transition
  is broadcast to *all* clients with the originating client id
  (`Paused{by: cid}`), so a GUI panel sees "paused by client 2" and refreshes;
  a DeZog session sees a pause it did not ask for as an unconditional pause
  notification (which its protocol already models). "Last verb wins" is what a
  human at the GUI and a remote script both expect; anything richer is
  speculative. Subscriptions are **owned** by the client that created them
  (§4.3) so a disconnect removes its own and nothing else's; all are visible
  to all (CAP-INS-17), editable only by their owner.
- **Remote pause while the GUI is open:** the GUI's frame tick keeps running
  (`fx.paused()` only skips `run_frame`), it receives `Paused`, and behaves as
  it does today on a breakpoint hit. A GUI Run afterwards resumes the machine
  and the remote receives `Resumed{by: gui}` (DZRP has no packet for it —
  documented limit in `dzrp-frontend.md`).
- **Two flags, not one** (REQ-qt-01c): `attached` (≥1 client) gates the step
  machinery that `active()` gates today; `live_raster` (per client, ORed —
  CAP-SES-05) gates only the render-every-frame hint and the per-instruction
  `VideoTiming::advance()` walk. Step Out / Step Back never depend on
  `live_raster`.

### 4.2 Inspection — `CAP-INS`

All reads are **side-effect free by contract** (Task 40): they run outside any
`GuestExecutionScope`, use `NextReg::peek()`, and — new — a `Mmu::peek()` that
resolves the mapping without the floating-bus latch (finding F1, §2.2).

| ID | Capability | Notes |
|---|---|---|
| CAP-INS-01 | `registers() -> Z80Registers`; `set_register(RegId, value)` | `cpu().get_registers()` / set. `RegId` covers the 12 pairs individually, PC, SP, I, R, IFF1, IFF2, IM — a partial `G` (REQ-gdb-5) is per-register calls; no "set all" that clobbers unnamed registers. |
| CAP-INS-02 | `peek(MemSpace, addr, n, out)` / `poke(MemSpace, addr, n, in) -> {count, Result}` | `MemSpace::Cpu` (logical, through the live mapping incl. DivMMC/MF/L2 overlays); `MemSpace::Page{p}` where **p is the NR 0x50-0x57 page number (0..223)** and the backend does the VHDL routing — `to_sram_page` (+0x20 in Next mode) [`mmu.h:1387-1390`], page 0x0E → `bank7_bram` [`:1371`, `:1400`], 0xFE/0xFF refused `InvalidPage` (REQ-dzrp-5); `MemSpace::Rom{index}` — **settled from the code, uniformly** (REQ-qt-31; protocols-r4 R-1; design-dzrp's matching REQ): `index` names a **16 KB ROM image** (0..3, addresses 0..0x3FFF), read-only; on a `rom_in_sram_` machine (Next mode, `set_rom_in_sram(true)` [`emulator.cpp:6829`]) it is SRAM pages `2·index` / `2·index+1` (`map_rom_physical(0, sram_rom*2)` / `(1, sram_rom*2+1)` [`mmu.cpp:546-547`], mapped through `ram_.page_ptr(rom_page)` *without* `to_sram_page` [`:396-402`] — an un-shifted `ram_` page index (0..7 on the Next, never add 0x20) outside `Page{}`'s NR number space); on 48K/128K/+3 it is the `Rom` object's image (`Rom::page_ptr` [`rom.h:23`]); on the Next `Rom{0..3}` is complete because the NR 0x8C alt-ROM overrides are folded into `current_sram_rom()` [`mmu.cpp:540`]; `poke(Rom)` is `RefusedReadOnly`. `poke(Cpu)` is `Mmu::write` outside `GuestExecutionScope`: ROM ignored, per-scanline logs and attribute mux updated, no watchpoint latch, no event (REQ-qt-17b); it returns the count written and `RefusedReadOnly` when the range is read-only (REQ-gdb-6). |
| CAP-INS-03 | `mmu_slots() -> array<SlotInfo{nr_page, effective_page, is_rom, space, space_offset}, 8>`; `set_mmu_slot(slot, page)`; `paging_ports() -> {7ffd, 1ffd, dffd}`; `rom_select() -> MemSpace` | `Mmu::get_effective_page()` / `is_slot_rom()` [`mmu.h:74-78`]; **`space` + `space_offset`** name the backing store of the slot's 8 KB — RAM: `Page{nr_page}`, offset 0; ROM: `Rom{effective_page >> 1}`, offset `(effective_page & 1) · 0x2000` (a ROM slot's `effective_page` is `slots_[s]`, the **un-shifted `ram_` page index** — 0..7 on the Next, never add 0x20 — not an NR page — REQ-qt-31 and protocols-r4 R-1 / design-dzrp; no client composes a `MemSpace` from `effective_page + is_rom`); set via NR 0x50-0x57 (DZRP `CMD_SET_SLOT`) — `set_mmu_slot` runs the NR 0x50+slot WRITE HANDLER, so 0xFF on slot 0/1 re-engages legacy ROM paging exactly as the guest's `NEXTREG` does (GH #12; a bare `Mmu::set_page` left the slot unmapped). **`rom_select()`** (added by package D, owner-approved 2026-09-29) is `Rom{Mmu::current_sram_rom()}` — VHDL `sram_rom` (`zxnext.vhd:2981-3008`), machine type, 7FFD b4 / 1FFD b2 and the NR 0x8C locks folded in: the image slots 0/1 serve whenever they are ROM-mapped, answered while RAM is paged there too (DZRP `CMD_READ_BANK_MEM` bank 0xFF). |
| CAP-INS-04 | `nextreg_peek(reg)`, `nextreg_write(reg, val)`, `nextreg_selected()` | `NextReg::peek/write/selected` [`nextreg.h:18,52-62`]. A backend `nextreg_write` is a *debugger* write: synchronous (handlers run), not deferred, source `Debugger`, no event. |
| CAP-INS-05 | `port_in(port)`, `port_out(port, val)` | DZRP `CMD_READ_PORT/WRITE_PORT`, RSP `monitor in/out`. **Perturbing by nature**; the API says so and the DSL declines it. |
| CAP-INS-06 | `raster() -> RasterState` | `raster_state_at(video_timing(), hc, vc, port_ff, shadow)` [`src/debug/raster_state.h`]; computed from the clock when paused (`snapshot_raster()`), so no per-instruction `VideoTiming::advance()` dependency for clients without `live_raster`. |
| CAP-INS-07 | `time() -> Time{master_cycle, tstates_total, frame, cycle_in_frame, vc_raw, hc_raw}` | `clock_.get()`; `tstates_total = Emulator::monotonic_tstates()` [`emulator.h:500`] (REQ-gdb-10 — monotonic across NR 0x07 divisor changes); `frame` = the F2-fixed counter (§2.4); `current_frame_cycle()`. `frame` is the CURRENT frame's pre-increment tag — the F2 fix is a post-increment at `begin_new_frame` [`emulator.cpp:8467`], so the raw counter reads K+1 during frame K and the backend reports `frame_num_ − 1`, the same number the rewind slot carries; frame 0 is the first `run_frame()` after load. Read raw, `on frame N` would fire one frame early against `--delayed-keypress-frames N`. |
| CAP-INS-08 | `sprites() -> span<SpriteInfo>`; raw forms `sprite_attr_raw(i) -> 5 bytes`, `pattern_ram() -> span<uint8_t,16K>`, `sprite_palette_rgb333(bank, idx)`, `sprite_clip()`; debugger writes `set_sprite_attr_raw(i, bytes)`, `write_pattern_ram(addr, bytes)` (direct engine setters, not port 0x57/0x5B traffic — REQ-zrcp-10) | `SpriteEngine::get_sprite_info()` [`sprites.h:420-435`]; raw forms need accessor additions (`sprites_[]`/`pattern_ram_` private [`:477-478`], `sprite_rgb333_` private [`palette.h:482`]) — REQ-dzrp-4, the `CMD_GET_SPRITES*` family. NR 0x15 via CAP-INS-04. |
| CAP-INS-09 | `copper() -> CopperView{pc, running, mode, program[1024]}` | What `copper_panel.cpp` reads. |
| CAP-INS-10 | `ay_registers(chip) -> array<uint8_t,16>`, `turbosound_enabled()`, `ay_mode()`, `stereo_mode()`, `audio_mute_mask()/set_audio_mute_mask()` | The **live** signals [`turbosound.h:42,51,61`], not NR re-decodes (REQ-qt-22b, pinned by `debugger_audio_panel_test` DAP-02..06). |
| CAP-INS-11 | `disassemble(addr, n_lines, symbols?) -> vector<DisasmLine>`, `instruction_length`, `is_call_like` | `src/debug/disasm.*` over `peek(Cpu)`; `disasm_text::*` for copy formats. |
| CAP-INS-12 | `call_stack() -> span<CallFrame>`; `set_call_stack_enabled(bool)` | `CallStack`; cost is per-instruction, so it is on only while a client asks. |
| CAP-INS-13 | `trace_enabled()`, `set_trace_enabled(bool)`, `trace_clear()`, `trace_resize(n)`, `trace_entries()`, `trace_export(path)` | `TraceLog` [`trace.h:39-43`]. Forced on by rewind (REQ-qt-10b). `TraceEntry` gains I, R, IM, IFF1, IFF2, the `(SP)` word and the 8 MMU effective pages (+15 bytes/entry; DeZog's zrcp reverse-step parser asserts on a missing `MMU=` — REQ-zrcp-08); the `(SP)` read runs inside the existing `InspectionScope` [`emulator.cpp:9851`] so it fires no watch. |
| CAP-INS-14 | `framebuffer() -> {ptr, w, h}`; `render_layer(Layer, vc, uint32_t* dst, stride_px)` | Raw framebuffer [`emulator.h:519-525`]; per-layer full-frame render *from current state* — `render_to_image` + `replay_*` move verbatim out of `video_panel.cpp:394-630` into a Qt-free function; width always 640; rows 0..vc rendered over a 0x00000000 fill so alpha 0 ≡ transparent; rows > vc untouched; the widget keeps `QImage`, checkerboard, DPR, the raster line and titles (design-qt §3.7). **NEEDS-PROTOTYPE**: run the 106 `debugger_video_panel_test` rows against the moved function before the widget changes (§11 item 6). |
| CAP-INS-15 | `palette(PaletteId) -> span<uint16_t>` incl. `PaletteId::UlaActive`; `set_palette(id, index, rgb333)` (debugger write, REQ-zrcp-10); `active_ula_palette_bank()`; `ula_screen_regs() -> {port_ff, shadow(7ffd b3), live_bank7}`; `clip_window(Layer) -> {x1,x2,y1,y2}` from the live layer state ([`ula.h:206-211`] etc. — not the rotating NR 0x18-0x1C shadows, REQ-zrcp-11); the one published `rrrgggbb_to_argb()` | REQ-qt-27b/28: adapters convert RGB333 → toolkit pixels with the backend's function; the backend never returns a toolkit pixel format. |
| CAP-INS-16 | `input_state() -> {matrix[8], ext_keys, joy_left12, joy_right12, port_1f, port_37}` | Needed by the #20 *recorder* (§9): `Keyboard::read_rows` [`keyboard.h:68`] exists, the composed joystick ports and the 12-bit vectors need small accessors. |
| CAP-INS-17 | `subscriptions(include_transient=false) -> span<Subscription>` (the model incl. disabled, with owner); `events_fired_since(seq)` | For the Breakpoints panel and remote list commands; the Paused payload's `matched[]` is the per-stop subset (§4.3). |
| CAP-INS-18 | `set_border(colour)` | `Ula::set_border` [`ula.h:180`] — a debugger write, not `port_out(0xFE)` (which also drives EAR/MIC); DZRP `CMD_SET_BORDER` on every load (REQ-dzrp-6). |
| CAP-INS-20 | `coverage_enable(bool)`, `coverage_clear()`, `coverage() -> bitset<65536>` of PCs executed since clear | One bit-set per instruction inside the `attached`-gated branch; zero cost when off (REQ-zrcp-09 — DeZog enables it by default for zrcp; the DSL may ask "was this ever executed"). |
| CAP-INS-19 | `machine() -> MachineInfo{type, cpu_divisor, tstates_per_line/frame, master_cycles_per_line/frame, lines, fps, hc_max, vc_max, max_hblank, max_vblank, display_origin{hc,vc}, vblank_top}` — both clock domains, under `MachineTiming`'s own names (the architecture doc's INS-19 rename, B0 review B4; this row still said `cycles_per_*` until B4) | `EmulatorConfig::type` + `timing_` + `VideoTiming` geometry the frame diagram draws [`video_panel.cpp:997-1005`] (REQ-qt-25b, REQ-dsl-17). |


### 4.2a Mutation — the write half of `CAP-INS` (owner decision 2026-09-27)

"Side-effect free" above is a property of **observation** only. Writes are a
first-class capability the DSL may now use (its "no poke" rule is withdrawn by
the owner, §1 item 20). The write verbs already in the tables: CAP-INS-01
`set_register` (12 pairs with `F` addressable alone, PC, SP, I, R, IFF1,
IFF2, IM), CAP-INS-02 `poke(Cpu | Page)`, CAP-INS-03 `set_mmu_slot`,
CAP-INS-04 `nextreg_write`, CAP-INS-05 `port_out`, CAP-INS-08 sprite/pattern
writes, CAP-INS-10 `set_audio_mute_mask`, CAP-INS-15 `set_palette`,
CAP-INS-18 `set_border` — the union of what the Qt panels write today
(NextREG [`nextreg_panel.cpp:174`], `Mmu::write` [`memory_panel.cpp:148/153`],
mute mask [`audio_panel.cpp:165`]) and what the protocols asked for; no
new CAP id. **Contract, identical for every write** (REQ-dsl-21..24, REQ-qt WP8
confirmations): *where* — `poke(Cpu)` is `Mmu::write` outside any
`GuestExecutionScope`, the Memory panel's path: live map, **overlays
honoured** (a poke into `0x0000-0x3FFF` with DivMMC mapped goes where the
panel's does), ROM ignored, change logs and attribute mux updated, no latch;
`peek/poke(Page{p})` address the physical page **regardless of any DivMMC /
Multiface / L2 overlay** over a slot, a ROM-class page is `RefusedReadOnly`;
`nextreg_write` runs the register's write handler synchronously; `port_out`
dispatches like a guest `OUT`; `set_register(PC)` clears `halted` — an
obligation on the implementation (the only setter today is `set_registers`
[`z80_cpu.h:135`], nothing clears the `halted` field [`:14`] on a PC write);
*when* — always a delivery point: a frontend command
runs with the machine paused at a boundary; an **`Execute`** handler is
pre-instruction, so its write is seen by the instruction at PC (`set PC = x`
redirects before anything runs — and an `Execute` handler at A setting
`PC = B` bypasses a breakpoint at B for that one instruction, the gate having
run for A); every other kind is post-instruction, the raising instruction has
completed and the write lands before the next one, reaching neither it nor
the device cluster that already ran; deterministic
(same boundary every run); **logged by the backend** for every client as one
CAP-SES-06 `info` line `MUTATE <what> <old> -> <new> by <client>` (old = the
peeked value), so a script's log sees a GUI's or a remote's writes too; not
an event and not CPU-attributed (source `Debugger`: no watch, no
`NextRegWrite`, no `Port` fires, no `source`, no re-entry — a property of
`guest_access_` being false while the write runs [`debug_state.h:275`]: true
for a `pump()` command by construction (outside `run_frame()`), but a script
mutation executes at a delivery **inside** `run_frame()`'s
`GuestExecutionScope` with `wp_live_` true, so the engine runs the **whole
rule body, reads and mutations, under one `DebugState::InspectionScope`**
[`debug_state.h:104-115`], and B2 gates the **new `NextReg::write` hook** on
the same `guest_access()`, or panel and script NextREG writes would fire
`NextRegWrite` on themselves (`SCRIPT-EV-MUT-NOEVENT`); `NextReg::write` traces every caller
identically, so the `MUTATE` line is the only record of a debugger write);
visible to later handlers of the same delivery; **refused with `RefusedRzx`
while an RZX is recording or playing** (a recording cannot carry it, a
playback would diverge — the rewind wall's class); and **rewind-aware**: the next frame-boundary snapshot carries the
mutation, but a rewind target *inside* the mutated frame would replay it
without the script (handlers do not run in `replay_mode_` — and neither does
the pre-instruction gate nor the drain: `rewind_to_cycle` replays with
`set_active(true)` + `run_to_cycle` [`emulator.cpp:12720`], so the
`EventTable` is consulted only when `!replay_mode_`, else an `Execute`
subscription in the span would stop the replay short) and diverge, so the
backend records each mutation's cycle and refuses `step_back` /
`rewind_to_cycle` into a mutated span with `RefusedUnavailable` (frame-
boundary targets stay legal); the backend does **not** re-fire subscriptions
during replay — "script mutation + intra-frame rewind replay is not
deterministic" is a stated wall (design-dsl §4), not a promise.

### 4.3 Events, breakpoints, conditions — `CAP-EVT`

**Decision: the condition engine is a backend concept, and it is a *predicate
callback*, not an expression language.** The backend evaluates conditions at
delivery time (owner decision 5) but does not parse them: a condition is
`std::function<bool(const Event&, const Debugger&)>` compiled by whoever set
it. The DSL compiles its `when` clause to one (and, the closure being the
interpreter's, it may read interpreter variables and snapshots — design-dsl
§5.2); the GUI could attach a value-equality predicate; the DZRP, ZRCP and
RSP adapters never set one (they decline). One evaluation mechanism, the
expression grammar stays in the DSL where #26 defines it, and no interpreter
sits in the hot loop — predicates run only at an instruction boundary for an
event that already matched its cheap filter.

**Vocabulary** (matching CSpect's, as #279 recommends, plus the deterministic-
time kinds #26 needs; kinds added in v2 are marked †):

| Kind | Cheap filter (evaluated where) | Payload (beyond the common fields) |
|---|---|---|
| `Execute` | PC ∈ [lo, hi], optional `page` qualifier: effective page at slot(PC) == page, tested only after the address matched (pre-instruction gate, `should_break` site) — REQ-dzrp-7 | pc |
| `Mem` with `access` bitmask {Read, Write} (REQ-qt-13c, REQ-gdb-8) | logical addr ∈ [lo, hi] **or** physical page ∈ set (MMU sites; §8); a logical range may also carry an optional `page` qualifier AND-ed with it ("this range in this bank", DZRP's `bank+1` watchpoints — REQ-dzrp-11), tested only after the range matched | addr, phys_page, value (written / read), `prev` (the byte before a write — one peek at the latch site, on the hit path only; REQ-dsl-25), pc of the instruction, `source` ∈ {Cpu, Dma} (REQ-dsl-1 amendment; tagged at the boundary drain from the slot's DMA flag [`emulator.cpp:9784`], never at the MMU site) |
| `Port` with `access` {Read, Write} | `(port & mask) == value` (PortDispatch site; the GH #222 low-byte rule is sugar = mask 0x00FF); read value latched *after* dispatch (today `check_io_watchpoint_` runs before it [`port_dispatch.cpp:60`] — a trivial move) | port, value, pc, `source` ∈ {Cpu, Dma} (tagged at the drain from the slot's DMA flag exactly as `Mem`; a DMA byte whose destination is a port is a `Port{Write}` — REQ-dsl-27) |
| `NextRegWrite` | reg ∈ set, `source` ∈ {Cpu, Copper, Dma, Any} (`NextReg::write` hook) | reg, value, `prev` (peeked at the hook, before commit — REQ-dsl-4), source, **and `pc`, `cycle`, `hc`, `vc` captured in the latch at the hook**, not at delivery. Delivered after commit at the **next instruction boundary the drain reaches**: for a Copper or DMA write that is the current instruction's boundary; for a CPU write it is **≤1 instruction late**, because CPU NR writes commit in `flush_pending_cpu_nr_writes()` [`emulator.cpp:10221`], inside the post-instruction device cluster, *after* the boundary drain (§2.3). A `Stop` on a CPU NR write therefore lands one instruction after the writer, with the payload's `pc` naming the writer exactly (the Scanline rule, same reason). Chosen over moving the drain after the device cluster, which would change the GH #265 early-return contract at `:9398` for every data breakpoint. For "NR 0x51 vs MMU0" the script reads NR 0x50 (unaffected) and has both old and new 0x51 |
| `Frame` | every frame, or frame == N (`begin_new_frame`/`end_of_frame`) | frame (the pre-increment tag, = `time().frame`) |
| `Scanline` | cvc == N (or every) — latched at `on_scanline` [`emulator.cpp:11630`] with the line's exact cycle, delivered at the **next** instruction boundary (≤1 instruction late; instruction granularity is the model's unit) | frame, vc, cycle (captured in the latch) |
| `Cycle` | master_cycle >= N, one-shot by nature (pre-instruction gate; same test as `RUN_TO_CYCLE`) | cycle |
| `Reset` | hard / soft (the two reset paths) | kind |
| `IntAck` † | accepted maskable interrupt (`cpu_.on_int_ack` seam [`emulator.cpp:1114`]) — REQ-dsl-7 | vector, im |
| `Nmi` † | accepted NMI (`request_nmi` [`:10386`]) | source (mf / divmmc) |
| `Magic` | the magic-breakpoint opcode (`on_magic_breakpoint`) | pc |
| `Host` | a frontend calls `raise_host_event(name)`; fixed vocabulary `script1`..`script8` (REQ-dsl-8); key bindings are a GH #1 keymap addition after #278 | name |
| `Copper` † — sub-kinds `Move`, `Wait`, `Halt` (REQ-dsl-22) | `Move`: a MOVE executed (the `nextreg.write` at [`copper.cpp:209`], where `active_move_hc` is set); `Wait`: a WAIT satisfied ([`:184-197`], `pc_` advance `:186`); `Halt`: the first stall on the HALT form — `is_halt()` [`copper.cpp:87`] is defined and **uncalled today**; the hook is a new branch on the bare stall path (`// Otherwise stall`, [`:195`]) calling it, with an edge latch (first stall only; REQ-dsl-26). Filter: copper-PC range and/or NR set. Latched at the site — the Copper runs inside `tick_devices_after_instruction()` — delivered at the next instruction boundary (≤1 instruction late, the `NextRegWrite`/`Scanline` rule). A MOVE also raises `NextRegWrite{source=Copper}`; both fire from **one** latch entry fanned out at the drain, `Copper.Move` is the copper-side view | reg, value (`Move`); copper pc; `vpos`, `hpos_threshold` (`Wait`); `hc_ula`, `cvc` as the Copper compares them |
| `Dma` † — sub-kinds `Start`, `Byte`, `End` (REQ-dsl-23) | `Start`: **one definition** — `phase_` enters `START_DMA` while `state_ == TRANSFERRING`: the R6 `0x87` enable [`dma.cpp:573-575`], the R3 `dma_en` path [`:423-424`] and the auto-restart [`:817`] all fall out of it; `cmd_load` [`:677`] is an address reload, not a transition (REQ-dsl-26). **But one definition is not one site** (corrected 2026-09-28 from B2's review): two **mid-transfer re-arbitrations** satisfy the same predicate and begin no transfer, so they must not fire `Start` — `dma_delay_` asserting mid-transfer and dropping back to `START_DMA` to release the bus [VHDL `dma.vhd:420-432`], and the burst-mode prescaler wait expiring and returning through `START_DMA` [VHDL `dma.vhd:451-460`]. B2 excludes both, correctly, and discloses it at the `dma.h` site; `Byte`: each transferred byte inside `execute_burst` [`:699`; I/O write `:783-786`, memory write `:788`]; `End`: block completion at the `on_interrupt` site [`:807-813`] (auto-restart [`:815-819`] = `End` then `Start`). Filter: src/dst range for `Byte`. A DMA byte into a watched **range** still fires `Mem{source=Dma}`; `Dma.Byte` is the transfer-side view, no range needed. `Byte` is armed only while a `Byte` subscription exists; delivered at the boundary of the slot the burst ran in (the 512-entry ring covers a full 16-byte burst with a Read|Write range armed, §4.3) | `Start`/`End`: src, dst, length, direction, mode, bytes; `Byte`: src_addr, dst_addr, value, is_io_src, is_io_dst; cycle |

**No-subscriber cost of the Copper/DMA kinds:** one predicated branch on a
per-engine flag (`Copper::events_armed_`, `Dma::events_armed_`,
`Dma::byte_events_armed_`) set only by `subscribe`/`unsubscribe`, next to the
`should_log()` trace branch each site already has [`copper.cpp:192`, `:212`].
DMA `Start`/`End`: once per transfer, negligible; `Byte`: one branch per
transferred byte, only while armed.
Copper: `Copper::execute` is 8-12 % of the `copper-demo`/`beast` profiles
(Task 27), so B2 adds a `make bench` row on those two workloads with no
subscriber before it merges — expected noise, as for the MMU gate (§8).

Common payload: `cycle, frame, vc, hc, pc` at delivery; `id`, `owner`.

**Subscription** = `{kind, filter, access, condition?, once, transient,
action, enabled, owner}`; `subscribe(...) -> EventId`, `unsubscribe(id)`,
`set_enabled(id, bool)`, master switch (kept, GH #225), and a **per-client
switch** `set_client_enabled(cid, bool)` (REQ-zrcp-03: DeZog toggles
`enable-/disable-breakpoints` per session; a human's Qt rows must survive) —
live = master && client_enabled(owner) && enabled, rebuilt on change, never per
instruction. `probe_execute(pc) -> vector<EventId>` is a pure query ("would an
enabled Execute subscription match here") for adapters whose step loops must
report a breakpoint at the PC a step landed on (REQ-zrcp-05): the pre-instruction
gate cannot report it, because the GH #221 step-off skips exactly that address on
the next resume.

A subscription whose cheap filter matches *everything* — `Execute[0x0000,0xFFFF]`
with a predicate, which is how ZRCP's PC-free condition breakpoints
(`SP>=nnnn`) map (REQ-zrcp-02) — is legal: the predicate then runs at every
instruction boundary, and only the session that set one pays for it. It is the
"condition-only breakpoint" cost class of §8, not measured, off unless used.
- **`once`**: disabled after its first *accepted* firing (predicate true);
  stays listable.
- **`transient`** (REQ-dzrp-3): exempt from the master switch, auto-**removed**
  at the next stop, hidden from the user-model listing; any number of them.
  Step Over, Run to Here and DeZog's two temp breakpoints per `CMD_CONTINUE`
  all use it — one implementation of "temporary breakpoint" replaces today's
  single one-shot [`breakpoints.h:205-209`].
- Today's PC breakpoints and watchpoints become `Execute[addr,addr]` /
  `Mem[addr,addr]` subscriptions with `action=Stop`, no condition, owner = the
  Qt client (REQ-qt-13d), so the Breakpoints panel lists the same model, and
  `BreakpointSet`'s observer contract (GH #220) survives as
  `SubscriptionsChanged{kinds: bitmask}` (REQ-qt-13b: the gutter acts on
  `Execute` only).

**Actions**: `Stop`, `Log`, `Continue`. A delivery calls the subscriber's
handler (if any) with the payload; the handler may return a *verdict* that
overrides the static action (a DSL `on … do … stop end` decides at runtime).
`Stop` → `DebugState::pause()` at that boundary, `pause_reason =
Watch{id, access, addr}` / `Breakpoint{id}` / `Script{id, text}`, PC at the
instruction *after* the access for Mem/Port/NR events — the access has
happened; the "offending instruction" is `pc` in the payload, which is
`pc_pre_exec` [`emulator.cpp:9939`] — or *at* the instruction for `Execute`.
The `Paused` push carries **`matched: vector<Hit{event_id, addr, access,
value}>`** — every subscription that matched at that boundary, transient ones
included, each with its own hit address (REQ-dzrp-2, REQ-zrcp-04); the
verdict applied is the strongest. Events raised during `step_into()` are
delivered at the boundary inside `step_frame_slot()` exactly as in
`run_frame()`, and `pause_reason` reflects them (REQ-zrcp-05). Under `--headless` the loop owner maps
`Stop` to "log the event, exit non-zero" (CAP-SES-04) — the backend does not
know it is headless.

**Ordering and delivery point.** Memory, port, NR, Copper and DMA events
raised *during* an instruction are **latched**, not delivered: the hot-path
site appends `{kind, addr, value, prev, phys_page, pc, cycle}` to a fixed ring
on `DebugState` (replacing the single `data_bp_addr_`) and sets the existing
`data_bp_hit_` bit. At the instruction boundary the backend drains the ring, tags `source`
from the slot's DMA flag — today the local `dma_stalled_cpu_this_step`
[`emulator.cpp:9687`, set `:9784`], which becomes a member the drain can read
(an accessor addition, like INS-08's) — evaluates conditions, calls handlers,
and applies the strongest verdict. One caveat inherited from the code: a
`Stop` verdict takes the GH #265 early return at `:9398`, which skips
`tick_devices_after_instruction()`, so the stopping instruction's own deferred
CPU NR writes stay queued until the resume — pre-existing data-breakpoint
behaviour, kept, and stated so nobody reads "delivered after commit" over it. This is the existing shape (`data_bp_hit()` checked after
`step_one_instruction()` [`emulator.cpp:9398`]) with a value and a page added;
it is also what makes "observation does not perturb" a property of the
*design*: no user code ever runs inside `Mmu::write`, inside the CPU, or
inside a device tick.

**Ring bound — 512, derived; overflow specified (round-4 correction).** The
round-2 and v6 derivations ("32 / 64 by construction") counted only the DMA
slot and the CPU instruction and **omitted the Copper**, which executes once
per master cycle for the whole instruction window
(`tick_copper_for_master_cycles`, [`emulator.cpp:11574-11575`]): a MOVE takes
two cycles [`copper.cpp:161-166`], a satisfied WAIT one, so one boundary can
carry up to `tstates × divisor` Copper latches (a chain of satisfied WAITs) —
a 21-T `LDIR` iteration at divisor 8 is 168 cycles, 84 MOVEs. Two rules follow.
(1) A MOVE is **one** latch entry fanned out at the drain to both
`Copper.Move` and `NextRegWrite{source=Copper}`, never two. (2) The ring is
**512** entries. The ~23-T ordinary slot is an estimate; contention adds up to
6 T per contended cycle [`contention.h:336`], so a contended `LDIR` iteration
can reach ~80 T ≈ 640 master cycles at divisor 8 — a MOVE burst over it
(≤320 entries) still fits, but **a chain of already-satisfied WAITs over a
long contended slot is the overflow path** (one latch per master cycle); the
DMA worst case is 50 (a slot is DMA *or* CPU [`:9687-9790`]; `execute_burst(16)`
[`:9735`]; 16 `Dma.Byte` + 16 writes + 16 reads under a Read|Write range + one
`Start`/`End` pair — the burst loop exits at an auto-restart); the CPU's own
accesses are a handful; `skip_trap_cycles_` [`:7973`] ticks no device. **48
bytes per entry, 24 KB** as built (the earlier "~16 bytes, 8 KB" predated the
per-kind payload enumeration — wrong by 3×; one-off allocation, but 3× the
store traffic on the latch path); 512 covers every MOVE burst, not
every conceivable Copper program, which is why **overflow is a specified,
tested behaviour**, not "unreachable": the first N entries are kept in order with a
dropped count, the drain delivers them and marks the boundary's deliveries
`overflowed{dropped}`, and a backend row shrinks the ring through a test hook
and overflows it with a MOVE burst on purpose, asserting order, count and the
flag.

**Span invariants and cross-register consistency** (#279) need no new event
kind: two `Execute` (or `IntAck` + `Execute`) subscriptions whose handlers read
CAP-INS-01/03 and compare — design-dsl's `snap`/`unsnap`/`changed()` snapshot
stack is script-side state over backend primitives (its §2.5).

**Physical-page filter** is a first-class filter, not a derived one: "a write
to bank N" must match whether bank N is mapped at 0x8000 or 0xC000, and the
MMU knows the page at the write site (`slots_[addr>>13]`) — see §8.

### 4.4 Deterministic time — `CAP-TIME`

| ID | Capability |
|---|---|
| CAP-TIME-01 | `time()` (= CAP-INS-07) and `machine()` (= CAP-INS-19). |
| CAP-TIME-02 | `Frame`, `Scanline`, `Cycle` events (§4.3) — *the* way a script acts **at** an instant: the handler runs at the boundary where the instant was reached, with the machine stopped there, and may `Stop` or `Continue`. |
| CAP-TIME-03 | `run_to_cycle` / `run_to_frame(n)` (control) — the way a client *drives* to an instant. |

"A panel refreshing on pause" is pull after a `Paused` push; "a script acting
at an event" is a handler inside a delivery; "a remote acting at an event" is
`Stop` → `Paused` push → client commands. No wall clock anywhere: the
`--delayed-*-time` (seconds) options stay a *frontend* conversion to frames at
startup, as `HeadlessApp::run()` already does [`headless_app.cpp:353-375`].

### 4.5 Input injection and capture — `CAP-IN`, `CAP-CAP`

Relates to, and *replaces the duplication of*, the delayed-action machinery
(§2.5): `HeadlessApp` and `QtApp` each keep a per-frame countdown list; both
become callers of the same backend primitives, scheduled by a `Frame` event.

| ID | Capability | Built on |
|---|---|---|
| CAP-IN-01 | `press_key(name\|{row,col}[,{row2,col2}], hold_frames)` — a **pulse**, **APPEND** semantics (REQ-dsl-18) | `Keyboard::queue_auto_type` [`keyboard.h:79`] replaces the queue today [`keyboard.cpp:541`]; the backend appends instead (the 4-frame all-released gap `tick_auto_type` inserts between entries stays), so a second pulse while one is held is queued behind it and never strands a key down, and two pulses scheduled for one frame both happen. Two details the append honours: the queue keeps the snapshot-width cap `MAX_AUTO_TYPE_KEYS = 16` [`keyboard.h:195`] with the same loud truncation log, and `press_key` returns `RefusedUnavailable` (plus the count actually queued) when the append would exceed it; and appending never resets `auto_frame_count_` / `auto_gap_` [`keyboard.h:200-201`] for the entry in flight, so a held key does not restart its hold. **`Keyboard::queue_auto_type` itself becomes append, so every producer inherits it** — the backend's `press_key`, the phantom typist (`phantom_typist_.tick_frame()` [`emulator.cpp:9590`] → `keyboard_->queue_auto_type` [`phantom_typist.cpp:170`]) and the two `--load` tape auto-type sites [`emulator.cpp:8022`, `:8076`], all of which REPLACE today; the 16-entry cap applies to the union of what they queue. Append only in the backend path was rejected: a script pulse queued in the same `end_of_frame` would be clobbered when the typist fires one line earlier. The `--delayed-keypress-frames` rows inherit the fix. `key_name_to_matrix()` moves out of `headless_app.cpp` into the backend so every frontend and the DSL share the vocabulary the man page documents. |
| CAP-IN-02 | `set_key(row, col, pressed)` / `set_extended_key(id, pressed)` — level, not pulse | `Keyboard::set_matrix_bit` [`keyboard.h:185`] is **private** today and gains a public debugger-injection entry (accessor addition); `set_extended_key` [`:127`] is public. Replay applies recorded *state* per frame (#20); the DSL's bare `press`/`release` are this, only `press … for n` is IN-01. |
| CAP-IN-03 | `set_joystick(side, bits12)` | `Joystick::set_joy_left/right` [`joystick.h:113-117`]. |
| CAP-IN-04 | `press_nmi(Mf\|Drive)` | `on_hotkey_f9_mf_nmi / f10_divmmc_nmi` (GH #209 seam). |
| CAP-CAP-01 | `screenshot(path, layer_mask, Format::Png\|Scr)`; `flush_captures(by)` (added in B4, owner decision 2026-09-28: the exit bound — `NoFrame` if any of `by`'s captures is still pending, which it drops; `RefusedUnavailable` if any failed to write since the last call; else `Ok`; per client; allowed from a handler) | `save_screenshot` + `Renderer::set_layer_mask`. **Deferred to the next rendered frame** (one rule for every frontend; the GUI's defer-with-warning contract [`qt_app.cpp:622-636`], which headless today does not follow — it writes whatever is in the framebuffer [`headless_app.cpp:704-712`]; the headless change is named here). `NoFrame` is returned only when the deferral is cut off by the exit bound — today's `auto_exit_finds_no_deferred_work` non-zero exit [`qt_app.cpp:647`]. |
| CAP-CAP-02 | screen *memory* capture: `ula_screen_dump()` (`Ula::screen_dump` [`ula.h:608`]) + `peek(Page{n})` for L2 banks / tilemap / pattern RAM | REQ-dsl-13; the byte-diffable unit for #20. |
| CAP-CAP-03 | `bookmark_save(name, Mode) -> Result` / `bookmark_restore(name) -> Result` / `bookmarks(cid)` — named, in-memory, **per client** | §4.6; a map over CAP-ST-01/02 for adapters whose protocol names bookmarks: ZRCP `snapshot-save/-load` (REQ-zrcp-13) and DZRP `CMD_READ/WRITE_STATE` (design-dzrp's post-review choice: the wire carries a token `JNXB<name>`, never the bytes, so a refused save cannot come back as a 0-byte restore). Each bookmark is a full `save_state` snapshot (the rewind slot size, `RewindBuffer::snapshot_bytes()`); **bound: 8 per client** (`RefusedUnavailable` beyond it, oldest never evicted silently) — at the rewind slot size that is of the order of the machine's RAM (768 KB–2 MB) plus subsystem state per bookmark, i.e. **tens of MB per client at the bound**, allocated on first use, never up front; **a client's bookmarks die with its `detach`**. A bookmark **survives a CTL-12 `Hard` reconstruct** (it is backend-owned bytes of the *old* machine, tagged with `machine().type` and the snapshot width at save time); restoring one into the rebuilt machine is legal only when the type and width still match — the backend refuses the mismatch with `RefusedUnavailable` *before* touching `load_state` rather than letting the sentinel check latch corruption. Disk is JNS via CAP-CAP-04, save only. |
| CAP-CAP-04 | `save_snapshot(path)` at the next frame boundary | The `--delayed-snapshot` path [`headless_app.cpp:730-810`] (REQ-dsl-11). |

**Injection ordering (REQ-dsl-20, a CAP-IN contract):** every IN-01 pulse append and IN-02 level set issued during frame N (from a `Frame` handler, a remote command in that tick's `pump`, or a `--delayed-*` countdown) is queued and applied in `end_of_frame` **before** `keyboard_.tick_auto_type()` [`emulator.cpp:9592`], so a pulse issued at the edge of frame N is pressed at that edge and visible to the guest from frame N+1 — the same frame `--delayed-keypress-frames N` lands on today, which queues before `run_frame(N)` [`headless_app.cpp:559-561`]. The other order would shift every `--delayed-keypress-frames` regression row by one frame.

### 4.6 State bookmarks and reverse execution — `CAP-ST`

| ID | Capability | Notes |
|---|---|---|
| CAP-ST-01 | `at_frame_boundary() -> bool`; `save_state_bytes(Mode::AdvanceToBoundary \| Mode::RefuseMidFrame) -> {bytes, Result}` | `Emulator::save_state` into a measured buffer. **Frame-boundary only**: `AdvanceToBoundary` advances under `SuspendScope` first (the #27 S6 rule [`emulator.cpp:11902-11908`]) and reports the cycle saved at; `RefuseMidFrame` returns `NotAtFrameBoundary` for a client that cannot refresh its register cache (DeZog — REQ-dzrp-10; works after PAUSE/INIT, which land at a boundary because `pump()` runs between `run_frame` calls, not after a mid-frame breakpoint). DZRP `CMD_READ_STATE`. |
| CAP-ST-02 | `load_state_bytes(bytes) -> Result` | `Emulator::load_state`; on sentinel failure the machine is corrupt → `last_state_error()` latched, CAP-CTL-11 applies. DZRP `CMD_WRITE_STATE`. **In-process only** (no versioning) — a bookmark lives for the session, exactly DeZog's `-state save/restore`. Persisting one is JNS (#27), not this. |
| CAP-ST-03 | `rewind_enabled()/set_rewind_enabled()`, `rewind_range() -> {oldest_frame, newest_frame, depth, capacity_frames, snapshot_bytes}`, `rewind_blocked() -> optional<string>`, `resize_rewind_buffer(frames)` | Existing `Emulator` accessors [`emulator.h:915-925`]; `snapshot_bytes` for the status bar (REQ-qt-09b); `rewind_blocked` as the pre-click query the toolbar greys on (REQ-qt-09c). |
| CAP-ST-04 | `step_back(n)`, `rewind_to_frame(n)` (= CAP-CTL-09/10) | Offered to every client; served by Qt (and available to the DSL, which declines it in v1 as re-entrant); declined by DZRP (no verb), RSP (`z88dk-gdb` sends no `bc`/`bs` — verified by design-gdb against `debugger_gdb.c`), ZRCP (design-zrcp to decide vs `cpu-history`). |

### 4.7 Symbols — `CAP-SYM`

`load_map(path, Format::Z88dk\|Simple) -> count`, `clear`, `lookup(addr)`,
`lookup_name(name)`, `symbols()`. `SymbolTable` moves from `DebuggerManager`
[`debugger_manager.h:112-113`] into the backend so remote lookups, the DSL's
`@sym`, and the panels share one table. Loading from the GUI stays a Qt file
dialog over CAP-SYM.

### 4.8 Session — `CAP-SES`

| ID | Capability |
|---|---|
| CAP-SES-01 | `attach(ClientInfo{name, kind}) -> ClientId`; `detach(cid)` — removes that client's subscriptions and, **iff the machine is paused *by this client*** (`pause_reason` names it, or a `Stop` on one of its subscriptions), resumes it; a pause by another client survives. **The one rule**, as the five frontends state it (`dzrp-frontend.md` §2 row 2, `zrcp-frontend.md` §4.5, `gdb-rsp-frontend.md` §2 row 19); there is no "last client" condition — the Qt adapter is attached for the process lifetime, so a remote is never the last client, and the point of the rule is that a crashed DeZog must not leave the machine hung. |
| CAP-SES-02 | `set_listener(cid, Listener&)` — `Paused{by, reason, cycle, pc, matched[]}`, `Resumed{by}`, `Reset{kind}`, `FrameEnded{frame}`, `SubscriptionsChanged{kinds}`, `ExitRequested{code}`, `Log{level, text}`. Synchronous callbacks on the emulation thread, inside `pump()` or `run_frame()`; they must return promptly and do no UI work (the Qt listener records and acts on its tick — REQ-qt-15b). |
| CAP-SES-03 | `pump(PumpBudget{max_wait_ms, drain_ms, budget_ms}) -> ServiceHint{remote_attached, paused}` — while paused, after answering a command it waits up to `drain_ms` (~2) for the next complete command and answers it too, until quiet or `budget_ms` (~10) is spent (REQ-zrcp-01: a DeZog zrcp step is ~15 sequential round trips; at one per tick that is 300 ms); `pump(0)` while running. The budgets are **host service parameters** — how long the loop owner lends its thread to socket I/O — never emulation semantics; nothing in the emulated timeline depends on them. Called by the loop owner once per tick **after** the tick's frame batch (the `post_frames` slot where `check_breakpoint_hit()` sits today [`qt_app.cpp:666`]), so a stop in this tick's frames is notified in this tick's pump (REQ-dzrp-8: one tick per DeZog step round trip). Drives registered services (socket adapters). The loop owner *may* shorten its cadence while `paused && remote_attached` (REQ-dzrp-9, NEEDS-PROTOTYPE against real DeZog). §5. |
| CAP-SES-04 | `set_stop_policy(StopPolicy::Pause \| ExitNonZero)` — set by the loop owner: **Qt = `Pause`**; **SDL and `--headless` = `ExitNonZero`** (log the event, exit non-zero; code = the script's explicit `exit` code, else the default — owner question §13.1) **unless a remote client is connected, in which case `Pause` + notify** — a proposal on top of the owner's #279 rule ("stop becoming a logged event plus a non-zero exit under `--headless`"), listed for the owner in §13.2. SDL is `ExitNonZero` because that frontend has no pause: `grep -n pause src/platform/sdl_app.{h,cpp}` hits one audio comment (`:422`), the sequencer's only pause is the debugger's `DebugState` [`frame_sequencer.h:209`], and there is no resume path (REQ-dsl-19). The one place the frontend kind is expressed, and it is expressed by the frontend. |
| CAP-SES-05 | `set_live_raster(cid, bool)` — per client, ORed (REQ-qt-01b); `attached()` — ≥1 client, the gate on the step machinery (REQ-qt-01c). |
| CAP-SES-06 | `log(level, text)` — the backend's message sink; frontends attach a console (Qt), stderr (headless/SDL), or a notification (remote) — REQ-dsl-11. |
| CAP-SES-07 | `set_loop_driver(LoopDriver{cold_boot(cfg), load(path)})` — the loop owner registers the `emulator_frontend_cold_boot()` sequence and the `emulator_apply_load()` dispatch it already owns (both live in `src/platform/`, below which the backend sits, so this is the only way the backend reaches them); `on_cold_boot_begin()` / `on_cold_boot_done()` — the loop owner's notifications immediately before and after a deferred (guest) cold boot (`begin` added by owner decision 2026-09-28, so CAP-CTL-12 rule 3 holds on the guest path). They exist so CAP-CTL-12 `Hard` and CAP-CTL-15 honour the reconstruct contract from any client (REQ-zrcp-15, REQ-qt-29). The stop policy (CAP-SES-04) is likewise the **loop owner's** to set, never an adapter's. |

---

## 5. Threading and the out-of-process model

**Today:** single-threaded, no mutex, deliberately [dev guide §3.9]. The loop
owner is the frontend: `QtApp`'s timer tick through `frame_sequencer`, the
SDL loop [`sdl_app.cpp:378`], or `HeadlessApp::run()` [`headless_app.cpp:503-`].
While paused the Qt/SDL loops keep ticking; the headless loop keeps calling
`run_frame()`, which returns immediately when paused — but only inside
`if (debug_state_.armed())` [`emulator.cpp:9300-9305`]: a paused-but-not-armed
machine runs. The backend therefore defines **`armed = attached ||
persistent`**, so a `pause()` from any attached client is honoured; a headless
run with no client attached is never paused by anything (every `Stop` exits,
CAP-SES-04), so its poll loop never spins without a client to serve.

**Minimum change for a socket frontend:** none to the threading model. A
protocol server is an adapter with a non-blocking listening socket; the loop
owner calls `Debugger::pump(max_wait_ms)` once per tick after the frames
(CAP-SES-03), and the server reads whatever is available, decodes complete
commands, executes them synchronously against the backend on the emulation
thread, and writes replies. Notifications (`Paused` …) are queued by the
listener and flushed in the same `pump`. Consequences:

- **GUI / SDL:** `pump(0)` per tick — never blocks; command latency ≤ one
  tick (20 ms at 50 Hz), which is what the CSpect plugin gives DeZog too.
  design-dzrp asks whether a paused machine with a remote attached should
  tick faster (a DeZog step-out is a loop of `CMD_CONTINUE` round trips);
  the `ServiceHint` lets the loop owner re-arm a ~2 ms timer while paused; the
  need is **measured against real DeZog before it is adopted** (§11 item 7).
- **Headless with a server attached:** the loop calls `pump(wait)` with
  `wait = paused ? ≤50 ms : 0`, turning the busy spin into a `poll()` while
  paused. That is the whole headless change.
- **No reentrancy:** a *command* handler (a protocol server acting on a
  received packet) that calls `step_into()` executes an instruction *inside*
  `pump`, which is inside the tick, outside `run_frame` — exactly where
  `DebuggerManager::on_step_into()` calls `debugger_step()` today. Command
  handlers are never invoked from inside an *event* delivery (§4.3 — event
  handlers, i.e. scripts, DO run inside a delivery, with the machine stopped
  at a boundary; they may not issue control verbs), so `pump` is not called
  from inside `run_frame`; the backend asserts `!in_delivery_` in `pump`. The DSL's "a script observes, it does not drive" rule
  (design-dsl §1) is the same wall from the other side.
- **Why not a thread:** every inspection read would then need the emulator
  locked at an instruction boundary anyway (a mid-instruction register read is
  garbage), which is a condition-variable handshake per command — more
  machinery for no lower latency than a tick. Declined; §11 item 7 records the
  one thing that would change it.

---

## 6. What `src/debug/` becomes — layout and build matrix

**Published** (frontends include these, nothing else; C++17 — `CMakeLists.txt:11` — so the sketches' `span<T>` means a `{const T*, size_t}` pair or a `std::vector<T>` copy, never `std::span`):
`src/debug/debugger.h` (facade, `jnext::dbg::Debugger`), `events.h` (kinds,
filters, `Event`, `Subscription`), `inspect.h` (value types: `Z80Registers`
re-export, `SlotInfo`, `RasterState`, `Time`, `MachineInfo`, `MemSpace`,
`SpriteInfo`, `rrrgggbb_to_argb`…), `result.h`. `symbol_table.h`, `disasm.h`,
`disasm_text.h`, `raster_state.h` stay published as-is (already
frontend-agnostic value services).

**Internal** (`jnext_debug` only; frontends do not include):
`debug_state.*` (execution control; grows the latch ring and the slot masks),
`breakpoints.*` (becomes the `EventTable` behind `Subscription`; keeps its
observer and enable model), `trace.*`, `call_stack.*`, `rewind_buffer.*`,
`resume_guard.h`, `debug_keymap.*` (data model of GH #1 — stays here for the
reason the dev guide gives: `src/gui` reads it with `ENABLE_DEBUGGER=OFF`).

**Moved:** `debug_keymap_qt.h` and `menu_bar_alt_nav_qt.h` → **`src/qt/`**,
a header-only directory with no CMake target (both are header-only today and
`jnext_debug` never compiles them [§2.7]); `${CMAKE_SOURCE_DIR}/src` is
already on every include path. Included by `src/gui` and `src/debugger`
exactly as now. This keeps the dev guide's build-matrix argument intact
(`ENABLE_QT_UI=OFF, ENABLE_DEBUGGER=ON` has no `jnext_gui`) without a Qt
header in the pure layer.

**New frontend directories** (each an adapter, each links `jnext_debug` +
`jnext_core`, none links Qt): `src/remote/dzrp/`, `src/remote/zrcp/`,
`src/remote/gdb/` (one target `jnext_remote`, one shared `--debug-listen-address ADDR`
default `127.0.0.1` plus `--dzrp-port`/`--zrcp-port`/`--gdb-port` — design-gdb's
proposal, adopted for all three; the listener/transport seam is the public
`esp::make_socket_listener` / `EspListener` / `EspTransport` interface
[`src/esp01/include/esp01/esp_socket.h:561/509/258`], not the platform
layer beneath it), `src/script/` (`jnext_script`, the DSL,
`--script FILE`). Built in **every** configuration — they have no toolkit
dependency, and a headless CI run is their main use. No gate option proposed
until a platform cannot build a socket.

**Build-configuration matrix (proposed):**

| Configuration | `jnext_debug` (backend) | `src/debugger` Qt panels | `src/remote` | `src/script` | `src/qt` headers |
|---|---|---|---|---|---|
| Qt + debugger (`build/`, shipped) | yes | yes | yes | yes | used by gui+debugger |
| SDL-only (`build/sdl-unit-test`) | yes | no | yes | yes | not compiled |
| Qt, no debugger | yes | no | yes | yes | used by gui |
| SDL + debugger (build-only) | yes | yes | yes | yes | used by debugger |

---

## 7. Testability

- **Backend suite, headless, no frontend** — `debugger_backend_test` (Qt-free,
  in `test/debug/`): a 48K `Emulator` with a RAM program (the
  `step_out_test`/`rewind_test` idiom), a `Debugger` on it, frames driven
  through `run_frame()` exactly as the frontends drive them. One **wiring row
  per verb and per event kind**: arm it through the facade, run, assert the
  machine stopped where the verb promises (PC, cycle, pause_reason) — and a
  *control* row that the same program runs straight past without the verb.
  That is the #203 shape generalised: "the predicate is right" is not a row;
  "the machine stopped" is.
- **Delivery rows** for CAP-EVT: for each kind, a row that asserts the payload
  (value written, physical page, `source` Cpu vs Copper vs Dma, `prev`) and
  one that asserts a *false* condition does not stop. Range rows sit on both
  edges and one past each edge (the coverage-on-one-side rule). Transient
  rows: two transients per resume, removed at the stop, absent from the
  user list. `matched[]` rows: a user breakpoint and a transient at the same
  address both listed.
- **Multi-client rows**: two attached fake listeners; pause by A → both see
  `Paused{by:A}`; detach A while paused by A → resumed; detach A while paused
  by B → still paused; A's subscriptions gone, B's intact.
- **Non-perturbation rows**: `peek(Cpu)` in +3 mode leaves
  `p3_floating_bus_dat_` unchanged (F1); a Memory-panel-style sweep with a
  READ watch armed fires nothing (already `debugger_inspect_watchpoint_test`).
- **Frame counter row** (F2): `time().frame` advances without
  `--rewind-buffer-size`.
- **Adapters**: each protocol server is constructed over a *fake transport*
  (an in-memory byte pipe implementing the same `Transport` interface the
  socket does) so a unit suite pushes packets and reads replies with no
  network; plus one **real-client** row in the regression suite per protocol:
  DZRP with the project's own `tools/cspect_dzrp/cspect_dzrp.py` client (it
  already speaks the wire to CSpect — an independent implementation of the
  *client* side), ZRCP with scripted `nc` lines, RSP with `z88dk-gdb` from the
  z88dk checkout (design-gdb already validated its target XML against the
  real v2.4 binary: 1023-byte ceiling, `T05`-only dispatch). The frontends'
  own files say what they validate against.
- **Qt panels (#278)**: the existing 13 Qt suites and their pinned counts are
  the sufficiency proof; new rows only where the panel's *source* of data
  changed.
- **Hot path**: `make bench` before/after, §8.

---

## 8. The hot-path constraint — design and measurement

**Constraint:** range watches on every memory write must cost ≈ nothing with
no watch armed and stay cheap with one armed.

### 8.1 Design

The existing gate order is kept *exactly*: `debug_state_ &&
watchpoints_live() && has_any_watchpoints()` [`mmu.h:258-260`]. With no watch
armed that is the whole cost today, and stays the whole cost — the design adds
nothing before it. Behind it, the linear `vector<Watchpoint>` scan
[`breakpoints.cpp:106-116`] is replaced by:

1. `uint8_t rd_slot_mask_`, `wr_slot_mask_` on `DebugState`: bit *s* set iff
   *any* armed range intersects logical slot *s* (8 × 8 KB) **or** any armed
   physical-page set contains the page currently mapped at slot *s*. One
   byte load + shift + bit test per access.
2. Recomputed only when subscriptions change or the MMU remaps a slot — the
   MMU already has a single point where a slot's page changes (the dispatch
   table rebuild); it calls `debug_state_->on_slot_remapped(s, page)`.
3. On a slot-mask hit: precise scan of the (few) armed ranges for that slot,
   then latch `{addr, value, phys_page}` into the ring (§4.3). No condition
   is evaluated here.

**Why not a 1-bit-per-byte 64 KB bitmap:** it costs a load + shift + test
too, but 8 KB of cache pressure per space, and it cannot express "physical
page" without a second map; the slot mask handles both with one byte.

### 8.2 Measurement — method

The reference harness (`make bench`, `test/bench/bench.sh`) measures one
binary at a time, median of 5, and voids a workload whose spread exceeds 5 %.
On this host during the design session (five other agents building and
testing) it voided two of five workloads (`boot-nextzxos` 9.1 %, `beast`
23.1 %, load1 2.18 at start — result kept in the scratchpad, not committed).
Deltas were therefore measured the way `test/bench/c1-ab-e596fa6a.txt` did in
Task 27: **interleaved A/B**, four binaries run back-to-back per pair, same
core (`taskset -c 0`), private reflink SD clone, `--benchmark N` headless,
T-states/s as the metric, medians over pairs, spread per binary. Interleaving
makes a slow drift hit every binary equally; the spread column shows bursts.

Binaries (all Release, `-O2 -DNDEBUG`, same flags as `build/gui-release`):

| Tag | Source | Env |
|---|---|---|
| **B** | `main @ 974b0ab19` unmodified | — |
| **P1U** | prototype: `rd/wr_slot_mask_` + `on_slot_remapped`-less static masks, the mask test inserted **after** the existing triple gate at all 8 MMU sites | — (no watch) |
| **P0A** | baseline code + an env hook that arms ONE `WRITE` watchpoint at `0x0000` and `--persistent-breakpoints` (today's linear scan when armed) | `JNEXT_PROTO_WATCH=0000,w` |
| **P1A** | P1U + the same hook, masks set from the watch | `JNEXT_PROTO_WATCH=0000,w` |

The prototype is a **throwaway**: three files mutated (`mmu.h`,
`debug_state.h`, `emulator.cpp`), built in `build-p0/` and `build-p1/`,
sources restored from a `cp` backup and verified byte-identical
(`cmp`), nothing committed. The mutation script and raw results are in the
session scratchpad (`backend/proto.py`, `ab.sh`, `ab-run*.txt`).

### 8.3 Measurement — results

Two interleaved runs. Run 1 (5 pairs, load1 2.45 → 2.51) is the no-watch
measurement and the armed-cold measurement on the two workloads that never
touch the watched address; its `boot-nextzxos` armed cells were **invalid**
(the firmware copies ROM images into SRAM through config-mode paging at
`0x0000-0x3FFF`, so the `WRITE 0x0000` watch fired, `run_frame()` paused the
machine, and the benchmark loop counted frames that emulated nothing — 13×
"faster"; the harness measuring a paused machine, not a speed-up). Run 2 (7
pairs, load1 2.91 → 3.21) rebuilt the two armed binaries with a hook that
**consumes the latch without pausing** (`JNEXT_PROTO_NOPAUSE`).

**What the armed rows measure — corrected after review.** The armed watch is a
*single-address* `WRITE` watch at `0x0000`, and `has_watchpoint` matches
`wp.addr == addr` exactly [`breakpoints.cpp:105-116`], so on `boot-nextzxos`
it latches **once per ROM-image copy** (the copy's first byte), not once per
write. The armed columns therefore measure the **armed scan on every memory
write at a 28 MHz write rate with a cold hit** — for P0A the linear scan on
every write; for P1A the slot-0 mask bit forcing the precise scan for the
`0x0000-0x1FFF` half of each copy, still without a latch. The frequently-
latching path (ring append per write, `data_bp_hit_` set and drained per
instruction) was exercised a handful of times per run; `NOPAUSE` only cleared
the flag those few times. A true hot-latch measurement needs a *range* watch
covering the copy (`Mem[0x0000,0x3FFF] Write` — the #279 case), which the
throwaway prototype could not express because it reused today's per-address
`BreakpointSet`; it is §11 item 3, measured on the implementation branch.
Deltas are medians of pairs against B; `sp` is that binary's own
(max−min)/median.

| Workload | run | B (T/s) | P1U vs B (no watch) | P0A vs B (today's scan, armed, cold hit) | P1A vs B (mask design, armed, cold hit) |
|---|---|---|---|---|---|
| boot-48k (3.5 MHz, ROM-resident) | 1 | 69.3 M (sp 1.8 %) | **+1.1 %** (sp 3.4 %) | −0.4 % (sp 14.7 %) | −1.3 % (sp 9.1 %) |
| boot-48k | 2 | 68.6 M (sp 27.1 %*) | −0.4 % (sp 5.9 %) | −2.3 % (sp 9.7 %) | −1.0 % (sp 1.8 %) |
| boot-nextzxos (28 MHz) | 1 | 180.3 M (sp 2.7 %) | **+3.6 %** (sp 4.3 %) | *invalid* (paused) | *invalid* (paused) |
| boot-nextzxos, **armed, scan every write, cold hit (28 MHz)** | 2 | 184.8 M (sp 3.9 %) | **+0.8 %** (sp 3.1 %) | **−5.0 %** (sp 2.1 %) | **−1.4 %** (sp 1.6 %) |
| beast (28 MHz, L2 + copper) | 1 | 68.8 M (sp 1.9 %) | **+0.0 %** (sp 2.5 %) | −2.4 % (sp 4.7 %) | −0.8 % (sp 1.8 %) |
| beast | 2 | 68.0 M (sp 3.0 %) | **+0.8 %** (sp 2.8 %) | −1.9 % (sp 9.8 %) | −1.5 % (sp 5.7 %) |

\* one load burst in run 2's `boot-48k` B column (pair 6 ran 25 % slow); the
other columns of that workload are unaffected because each pair is
interleaved, but the B median for that row is the least trustworthy number in
the table.

**Verified claims** (numbers that survive both runs and sit inside the tight
spreads):

1. **No-watch cost of the new gate shape: none.** B → P1U is +1.1 / −0.4 /
   +3.6 / +0.8 / +0.0 / +0.8 % across six workload-runs — noise, centred
   slightly on the favourable side. The design predicted exactly this (the
   first gate is unchanged and the compiler emits nothing new before it); it
   is now measured, not inferred. This is the number #279 and #277 asked for.
2. **Armed, scanning every write at 28 MHz, cold hit: today's linear scan
   costs 5.0 %, the slot mask 1.4 %** on `boot-nextzxos`, the cleanest row
   in the table (spreads 2.1 % and 1.6 %). Both armed columns also pay the
   pre-existing `--persistent-breakpoints` per-instruction `should_break()`
   lookup on an empty set. This is the cost of *having* a watch armed while
   the guest writes memory at full speed and does not hit it — paid only by
   the user who armed one. It says nothing about the latch-and-deliver cost
   when a range watch *does* hit on every write (§11 item 3).
3. **Armed-but-cold** (`beast`, `boot-48k`): both designs are within
   1-2.5 % of B with spreads of the same size; the mask is never worse than
   the scan and the run-2 `beast`/`boot-48k` P1A columns (sp 5.7 % / 1.8 %)
   are the tighter ones. No stronger claim is made for this case.

What the prototype did NOT measure: a range watch that hits on every write
(the latch append per write, the per-instruction drain, condition evaluation
and handler call — no delivery machinery was prototyped, and the per-address
`BreakpointSet` cannot express a range), the physical-page set path (masks
were static), and the `on_slot_remapped` recompute. The last two run off the
hot path (per subscription change / per MMU remap); the first is §11 item 3,
the one remaining hot-path measurement, to be taken on the implementation
branch with `Mem[0x0000,0x3FFF] Write` armed over `boot-nextzxos`.

---

## 9. #20 as a use case of #26 — verdict

**Holds** (design-dsl §7 concurs, independently derived). Replay = a script:
`on frame N do press/release/joystick … end` (CAP-IN-02/03 level semantics),
`on frame M do compare_scr/screenshot … exit … end` (CAP-CAP-01/02 + DSL
`assert`/`exit`). Record = a recorder over `Frame` + `Host` events reading
**CAP-INS-16 `input_state()`** each frame (the one thing nothing exposes
today), emitting the script (`.jds`) and the ULA `.scr` dumps it references;
non-ULA layers go through the composited PNG and the suite's png-diff. RZX
stays untouched (it replays IN results; a keyboard-path test needs input
state). `--delayed-keypress-frames` and `--delayed-screenshot*` remain as CLI
conveniences whose actions are the same backend verbs; their countdowns stay in
the loop owners, counting loop ticks (owner decision 2026-09-28, B4 O2 — a tick
count survives a cold boot and advances while paused; a `Frame` tag does
neither).
**#20 re-scopes to: recorder + `compare_scr` action + CAP-INS-16** — no
separate frontend, no third mechanism.

---

## 10. Explicit non-goals

- No plugin ABI (owner decision). No expression language in the backend
  (the DSL owns it). No wire protocol in the backend. No thread. No
  wall-clock API. No persistence of bookmarks (JNS, #27). No change to how the
  hot loop is structured (Task 40 / GH #207 / GH #203 semantics unchanged).
  No new panels. No DeZog protocol extension. No script-driven stepping or
  machine mutation in the DSL's v1 (its own rule; the backend still offers
  the verbs to other clients).

## 11. Cannot be known without a prototype

1. ~~The exact `MemSpace::Rom` enumeration~~ — **settled from the code**
   (CAP-INS-02): `Rom{index}` = one 16 KB ROM image, SRAM pages `2·index` /
   `2·index+1` on a `rom_in_sram_` machine, the `Rom` object's image
   otherwise; `Page` is the NR page space (REQ-dzrp-5); `SlotInfo.space` +
   `space_offset` carry the slot's backing store.
2. ~~Latch ring size~~ — **resolved by derivation** (§4.3, corrected in
   round 4 to include the Copper's per-master-cycle cadence): 512 entries;
   overflow is a specified, tested path either way.
3. **The hot-latch cost**: a range watch that hits on every write (append per
   write + drain + predicate + handler) — not measured; the prototype's armed
   rows were a cold-hit scan (§8.3). Measure with `Mem[0x0000,0x3FFF] Write`
   over `boot-nextzxos` on branch B before B2 merges.
4. ~~One `NextReg::write` hook for all writers~~ — **resolved from the code**
   (§2.3): `write_selected` → `write`, Copper direct, DMA-via-port
   synchronous; the CPU path commits after the boundary, hence the ≤1-
   instruction delivery rule in §4.3.
5. ~~DSL expressiveness for #279's call trace~~ — **resolved** by design-dsl's
   snapshot stack (`snap`/`unsnap`/`changed`/`depth`), no plugin-shaped need
   surfaced; `crc32(range)` and a hit histogram are the next likely asks.
6. The `render_layer` move (CAP-INS-14): prove the uniform alpha-0
   transparency contract by running the 106 DVP rows against the moved
   function before the widget changes (design-qt §3.7).
7. Paused-state service cadence for DeZog (REQ-dzrp-9): whether a ~2 ms tick
   while paused-with-remote is needed, measured against a real DeZog step-out.

---

## 12. Requirements ledger

Status vocabulary: ACCEPTED (→ CAP id) · REJECTED (reason) · ALTERNATIVE (→
CAP id) · NEEDS-PROTOTYPE · CONFIRMED (a confirmation, not a new capability).

**Totals:** 89 REQs — 77 ACCEPTED, 3 ALTERNATIVE, 7 CONFIRMED, 2
NEEDS-PROTOTYPE, 0 REJECTED. MAPPED against v3 (used /
declined / reach-arounds): qt 40/15/0, dzrp 30 commands (26 tier 1 + 4 tier 2; DZRP 2.2.0 verified against DeZog main @ 0de07af6 — 3.8's subset-driven remotes reach watchpoints and bookmarks, the tier-2 "non-DeZog only" caveat is 3.7.4-only)/(5 declined + 3
unsupported-reported)/0, zrcp 67/(1 command + 6 options)/0, gdb 21/24/0, dsl
35/16/0.

| REQ | Capability | Status | CAP / reason |
|---|---|---|---|
| REQ-dsl-1 | mem WRITE event over logical range, payload addr/value/phys_page/pc/cycle; SOURCE cpu/dma | ACCEPTED | CAP-EVT `Mem{Write}`; source tagged at boundary drain from the slot's DMA flag (`emulator.cpp:9784`) |
| REQ-dsl-2 | mem READ event over range | ACCEPTED | CAP-EVT `Mem{Read}` |
| REQ-dsl-3 | EXECUTE event addr/range, pre-instruction, payload pc + opcode | ACCEPTED / ALTERNATIVE (opcode) | CAP-EVT `Execute`; opcode via `peek(Cpu, pc)` at delivery — a payload opcode would lie in an NMI/INT-accept slot |
| REQ-dsl-4 | NextREG WRITE event before commit, {reg,new,prev,source} | ALTERNATIVE | `NextRegWrite`: `prev` peeked at the hook, delivered after commit at the boundary; source cpu/copper/dma |
| REQ-dsl-5 | PORT read/write events, GH #222 rule + mask/value | ACCEPTED | `Port{Read,Write}`; (mask,value) is the primitive; read value latched after dispatch |
| REQ-dsl-6 | frame/scanline/cycle events + clocks | ACCEPTED | CAP-TIME-02, CAP-INS-06/07; Scanline latched, delivered ≤1 instruction later with the exact cycle |
| REQ-dsl-7 | RESET, INT-accepted, NMI-accepted events | ACCEPTED | `Reset`, `IntAck` (`emulator.cpp:1114` seam), `Nmi` (`:10386` seam) |
| REQ-dsl-8 | host key event | ACCEPTED | `Host`, names `script1`..`script8`; key binding is a GH #1 keymap addition (design-qt) |
| REQ-dsl-9 | conditions + once in the backend | ACCEPTED | §4.3 predicate callback + `once` |
| REQ-dsl-10 | non-perturbing read-only state access | ACCEPTED | CAP-INS-01/02/03/04/06, CAP-SYM; F1 → `Mmu::peek` |
| REQ-dsl-11 | actions: stop(reason)/log/exit/screenshot/snapshot/input | ACCEPTED | `Stop` → `Script{id,text}`; CAP-SES-06 `log`; `ExitRequested`; CAP-CAP-01; CAP-CAP-04; CAP-IN-01/02/03 |
| REQ-dsl-12 | stop at end of instruction, payload pc = pre-exec PC | ACCEPTED | §4.3 |
| REQ-dsl-13 | screen bytes: `Ula::screen_dump` + physical pages | ACCEPTED | CAP-CAP-02 |
| REQ-dsl-14 | per-frame input state for the recorder | ACCEPTED | CAP-INS-16 (+ composed ports 0x1F/0x37) |
| REQ-dsl-15 | cost statement measured | ACCEPTED | §8 |
| REQ-dsl-16 | real frame counter (`frame_num_` only counts with rewind) | ACCEPTED, **verified** (`emulator.cpp:8467`) | finding F2; CAP-INS-07 `frame`; unconditional increment at the same site |
| REQ-dsl-17 | machine type readable from a callback | ACCEPTED | CAP-INS-19 `machine()` |
| REQ-gdb-1 | `pause()` sync/idempotent | ACCEPTED | CAP-CTL-01 |
| REQ-gdb-2 | `resume()` with backend-side step-off | ACCEPTED | CAP-CTL-02 (GH #221 arm stays) |
| REQ-gdb-3 | `step_into` = `debugger_step` semantics | ACCEPTED | CAP-CTL-03 |
| REQ-gdb-4 | run until PC == next_pc (`i<len>`) | ACCEPTED | CAP-CTL-06 `run_to` |
| REQ-gdb-5 | registers get; per-register set (partial `G`) | ACCEPTED | CAP-INS-01 `set_register(RegId)` |
| REQ-gdb-6 | bulk logical memory r/w, write failure on ROM reported | ACCEPTED | CAP-INS-02 `poke` returns count + `RefusedReadOnly` |
| REQ-gdb-7 | PC breakpoints add/remove/exists | ACCEPTED | `Execute[addr,addr]`, CAP-INS-17 |
| REQ-gdb-8 | range watchpoints w/r/access with faulting address | ACCEPTED | `Mem` range with `access` bitmask; payload addr |
| REQ-gdb-9 | stop reason query | ACCEPTED | CAP-CTL-13 `pause_reason` |
| REQ-gdb-10 | monotonic T-state counter | ACCEPTED (corrected) | CAP-INS-07 `tstates_total` = `Emulator::monotonic_tstates()` (`emulator.h:500`) |
| REQ-gdb-11 | nextreg peek/write, mmu slots, physical page read, symbols | ACCEPTED | CAP-INS-04/03/02, CAP-SYM |
| REQ-gdb-12 | breakpoint ownership per client, merged visibility | ACCEPTED | subscription `owner`, CAP-INS-17 |
| REQ-gdb-13 | per-tick poll hook, running and paused, single thread | ACCEPTED | CAP-SES-03 `pump` |
| REQ-gdb-14 | paused/resumed edge with cause, same thread | ACCEPTED | CAP-SES-02 |
| REQ-gdb-15 | run state query | ACCEPTED | CAP-CTL-13 |
| REQ-dzrp-1 | Paused carries accessed address + access kind | ACCEPTED | CAP-CTL-13 / CAP-SES-02 `Watch{id, access, addr}` |
| REQ-dzrp-2 | every matched subscription listed at a stop | ACCEPTED | `Paused.matched[]`, CAP-INS-17 `events_fired_since` |
| REQ-dzrp-3 | adapter-creatable transient subscriptions, ≥2 per resume | ACCEPTED | `transient` flag (§4.3) |
| REQ-dzrp-4 | raw sprite attrs / pattern RAM / RGB333 palette / clip | ACCEPTED | CAP-INS-08 raw forms (accessor additions on SpriteEngine/PaletteManager) |
| REQ-dzrp-5 | `Page` index = NR page space with VHDL routing; ROM sentinels refused | ACCEPTED | CAP-INS-02 |
| REQ-dzrp-6 | `set_border` as a debugger write | ACCEPTED | CAP-INS-18 |
| REQ-dzrp-7 | Execute filter with physical-page qualifier | ACCEPTED | `Execute.page` (§4.3) |
| REQ-dzrp-8 | notification flush in the same tick as the stop | ACCEPTED | CAP-SES-03 `pump` after the frame batch |
| REQ-dzrp-9 | faster service cadence while paused with a remote | NEEDS-PROTOTYPE (API accepted) | `ServiceHint`; policy measured against real DeZog (§11.7) |
| REQ-dzrp-10 | bookmark must not silently advance the machine | ACCEPTED | CAP-ST-01 `at_frame_boundary()`, `Mode::RefuseMidFrame` |
| REQ-qt-01..28 (served) | attach, pause/run/step verbs, EOF/EOSL, rewind, trace, corruption, breakpoint model, symbols, transitions, registers, memory, MMU, nextreg, sprites, copper, AY, mute, call stack, raster, ULA regs, palette, render, disasm | ACCEPTED | CAP-SES-01/05, CAP-CTL-01..11, CAP-ST-03, CAP-INS-01..15, CAP-EVT, CAP-SYM, CAP-SES-02 (design-qt §3.2) |
| REQ-qt-01b | per-client `set_live_raster(cid, bool)` | ACCEPTED | CAP-SES-05 |
| REQ-qt-01c | `active()` also gates the step machinery — must not tie to live_raster | ACCEPTED (finding) | `attached` vs `live_raster` (§4.1) |
| REQ-qt-08b | benign refusal distinct from corrupt | ACCEPTED | `Result::RefusedUnavailable` |
| REQ-qt-09b | `snapshot_bytes()` | ACCEPTED | CAP-ST-03 `rewind_range()` |
| REQ-qt-09c | `rewind_blocked()` pre-query | ACCEPTED | CAP-ST-03 |
| REQ-qt-10b | `trace_enabled()`, `trace_clear()` | ACCEPTED | CAP-INS-13 |
| REQ-qt-12 | magic breakpoint get/set | ACCEPTED | CAP-CTL-14 |
| REQ-qt-13b | `SubscriptionsChanged` carries the kind | ACCEPTED | CAP-SES-02 `{kinds}` |
| REQ-qt-13c | READ_WRITE as one row | ACCEPTED | `Mem.access` bitmask |
| REQ-qt-13d | GUI subscription shape; panel lists all owners | CONFIRMED | §4.3; read-only for other owners |
| REQ-qt-15b | listener records only; UI on the tick | noted | CAP-SES-02 contract |
| REQ-qt-17b | `poke(Cpu)` ≡ `Mmu::write` minus watchpoints | CONFIRMED | CAP-INS-02 |
| REQ-qt-22b | live TurboSound/AY/stereo signals | ACCEPTED | CAP-INS-10 |
| REQ-qt-25b | raster geometry | ACCEPTED | CAP-INS-19 |
| REQ-qt-27b | active ULA palette bank + one RGB333→ARGB function | ACCEPTED | CAP-INS-15 |
| REQ-qt-28 | `render_layer` split | NEEDS-PROTOTYPE (split agreed) | CAP-INS-14; §11.6 |
| REQ-zrcp-01 | draining `pump` while paused | ACCEPTED | CAP-SES-03 `PumpBudget` |
| REQ-zrcp-02 | predicate on `Execute[0,0xFFFF]` at every boundary | ACCEPTED | §4.3 (cost class documented; only the session that sets one pays) |
| REQ-zrcp-03 | per-client subscription switch | ACCEPTED | `set_client_enabled(cid, bool)` |
| REQ-zrcp-04 | hit address + kind in `Paused` | CONFIRMED | `Watch{id, access, addr}`, `matched: Hit{}` |
| REQ-zrcp-05 | events delivered during `step_into`; breakpoint at the landed PC reportable | ACCEPTED | §4.3; new `probe_execute(pc)` |
| REQ-zrcp-06 | `pause_reason` carries EventId / ClientId | CONFIRMED | CAP-CTL-13 |
| REQ-zrcp-07 | monotonic T-states, fps, divisor | CONFIRMED | CAP-INS-07/19 |
| REQ-zrcp-08 | richer `TraceEntry` + `trace_resize`/`trace_clear` | ACCEPTED | CAP-INS-13 |
| REQ-zrcp-09 | execute coverage | ACCEPTED | CAP-INS-20 |
| REQ-zrcp-10 | palette / sprite / pattern-RAM debugger writes | ACCEPTED | CAP-INS-15, CAP-INS-08 |
| REQ-zrcp-11 | clip-window readback | ALTERNATIVE | CAP-INS-15 `clip_window(Layer)` from live state, not NR shadows |
| REQ-zrcp-12 | `load(path)` | ACCEPTED | CAP-CTL-15 |
| REQ-zrcp-13 | snapshot-save/-load | DECIDED | named in-memory bookmarks = CAP-CAP-03 / CAP-ST-01/02 |
| REQ-zrcp-14 | headless Stop pauses when a remote is connected | ACCEPTED as the design's proposal | CAP-SES-04; owner question §13.2 (it extends the owner's #279 headless rule) |
| REQ-dsl-18 | `press_key` pulses must append, not replace | ACCEPTED, verified (`keyboard.cpp:541`) | CAP-IN-01 APPEND semantics |
| REQ-dsl-19 | SDL frontend has no pause → SDL = ExitNonZero | ACCEPTED, verified (`sdl_app.cpp`: no pause path) | CAP-SES-04 |
| REQ-qt-29 | `load(path)` preserves every client's subscriptions and attach state across a cold-boot reconstruct | ACCEPTED, verified (`emulator_boot.h:133-146`) | CAP-CTL-15 contract + a backend row |
| REQ-dzrp-11 | `Mem` range AND physical-page qualifier | ACCEPTED | `Mem.page`, same shape as `Execute.page` |
| REQ-dsl-20 | frame-edge injection applied before `tick_auto_type()` | ACCEPTED, verified (`emulator.cpp:9592`; `headless_app.cpp:559-561`) | CAP-IN ordering contract (§4.5) |
| REQ-dsl-21 | mutation from a delivery: RegId incl. F/IFF/IM/PC (clears halted); Execute = pre-instruction, others post; no events, not CPU-attributed; poke = the panel's `Mmu::write` path; backend-logged `MUTATE`; rewind wall | ACCEPTED | §4.2a |
| REQ-dsl-22 | `Copper{Move,Wait,Halt}` with filters, latched, both fire with `NextRegWrite` | ACCEPTED, verified (`copper.cpp:86-87`, `:184-197`, `:209`) | CAP-EVT `Copper` |
| REQ-dsl-23 | `Dma{Start,Byte,End}` with a `Byte` range filter, armed only when subscribed; overflow policy | ACCEPTED, verified (`dma.cpp:572/677/699/783-788/807-819`) | CAP-EVT `Dma`; ring 512 (§4.3) |
| REQ-dsl-24 | `set_audio_mute_mask` is a logged mutation | ACCEPTED | §4.2a |
| REQ-qt-30 (reworded, round 4) | `peek/poke(Page)` overlay-independence for RAM slots; ROM-slot bytes come from `MemSpace::Rom` | CONFIRMED | CAP-INS-02, §4.2a |
| REQ-qt-31 | `SlotInfo` carries the `MemSpace` that reads the slot's backing store | ACCEPTED, verified (`mmu.h:74-77`, `mmu.cpp:396-402`) | CAP-INS-03 `space` + `space_offset`; §11 item 1 closed |
| REQ-dzrp-12 (protocols-r4 R-1) | `MemSpace::Rom` undefined for the Next's ROM-in-SRAM pages | ACCEPTED, verified (`emulator.cpp:6829`, `mmu.cpp:546-547`) | CAP-INS-02 `Rom{index}` = 16 KB image, SRAM pages `2·index`/`2·index+1`; `SlotInfo.space` |
| REQ-dsl-25 | `prev` in the `Mem{Write}` payload | ACCEPTED | CAP-EVT `Mem` |
| REQ-dsl-26 | `Copper.Halt` needs a new call + edge latch; `Dma.Start` is one transition | ACCEPTED, verified (`copper.cpp:87` uncalled, `:195`; `dma.cpp:423-424`, `:573-575`, `:817`) | CAP-EVT `Copper`/`Dma` rows |
| REQ-dsl-27 | `source ∈ {Cpu, Dma}` on `Port` events | ACCEPTED | CAP-EVT `Port` |
| REQ-zrcp-15 | `reset(Hard)` synchronous for a client; re-bind + re-apply; paused stays paused; `Reset{Hard}` before return; guest reset same rules; refused without a driver | ACCEPTED, verified (flag poll after the tick in all three loop owners; `emulator_boot.h:122-124`) | CAP-CTL-12 contract, CAP-SES-07 |

## 13. Open questions for the owner (only genuine ones)

**None.** All five below were answered by the owner on 2026-09-27 — see §1
(items 10-24) and the architecture document §1.3. Kept for the record.

1. **Headless exit code for a script `stop` with no explicit `exit`.** Never
   2 (both harnesses use 2 for a harness fault). Backend default proposed:
   1, the same as a failed `--load`; design-dsl proposes a dedicated 3 so a
   row can tell a script verdict from a jnext failure by code alone. The
   DSL's explicit `exit <code>` exists either way.
2. **Headless stop policy while a remote client is connected.** The owner's
   #279 rule is "stop becomes a logged event plus a non-zero exit under
   `--headless`". CAP-SES-04 proposes an exception: while a DZRP/ZRCP/RSP
   client is connected, `Stop` pauses and notifies it instead (a client
   blocked on `run` must get its stop reply; three frontends' regression rows
   depend on it). Default: the exception. Alternative: exit non-zero always,
   and the servers' headless rows become GUI/SDL rows.
3. **Any stop in the SDL frontend, and magic breakpoints under `--headless`.** The SDL frontend has no pause path, so a `--persistent-breakpoints` PC hit there is a dead end today (the machine pauses, stale frames present, nothing can resume); CAP-SES-04 makes every such stop a logged event + non-zero exit — say so for the whole class, not only magic. For magic specifically: today a headless magic
   breakpoint pauses the machine and the run continues to
   `--delayed-automatic-exit` with exit 0 [`emulator.cpp:7880-7885`;
   `test/00regression/scripts/magic-bp-func.sh` relies on the exit bound and
   `|| true`, greps the log]. Routing it through CAP-SES-04 makes it a logged
   event plus a non-zero exit — a CLI contract change that needs a man-page
   line under `--magic-breakpoint`; `magic-bp-func` keeps passing only because
   it greps the log, and its `expect:` should be re-pinned. Default: adopt the
   change (one rule for every stop). Alternative: magic keeps today's
   pause-and-continue-to-exit behaviour in headless.
4. **Client ownership on detach.** Default (the rule in CAP-SES-01): a
   client's subscriptions die with it and a pause *it* caused is released.
   Alternative: everything persists until explicitly cleared (a crashed DeZog
   then leaves the machine paused with its breakpoints armed).
5. **Memory panel "slot view" semantics** (design-qt): "Slot 3 (page 0A)"
   reads CPU addresses `0x6000-0x7FFF` through the live map, not the physical
   page. Pin as-is for #278; decide separately whether it becomes a
   `MemSpace::Page` read now that one exists.
