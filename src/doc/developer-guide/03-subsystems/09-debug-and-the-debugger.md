# 3.9 Debug and the debugger

The debugger is the developer-facing half of jnext: a way to stop the machine,
look at everything inside it, change some of it, start it again — and, when
rewind is on, run it backwards. It is not a separate program talking to the
emulator over a wire. It lives in the same process and the same thread as the
emulation, so a panel reads machine state by calling straight into `Emulator`
rather than marshalling it across a boundary, and the run loop consults the
debugger's state once per instruction, before the fetch. That single
consultation point is what makes stepping exact and breakpoints cheap.

The dependency runs one way and stops halfway. The emulator core owns and
consults the debug *backend* — it is an ordinary member, present in every
build — while the Qt *UI* sits above both and can be compiled out entirely.

## Two directories, and the split is the design

**`src/debug/`** (target `jnext_debug`) is the backend: disassembler,
breakpoint and watchpoint sets, execution-control state, instruction trace log,
call-stack tracker, symbol table, rewind ring buffer, and the raster-state
derivation (`raster_state.*`) behind the Video panel's beam/fetch indicator. It
has **no Qt dependency at all**. **`src/debugger/`** (target `jnext_debugger`) is the Qt 6 UI and
nothing else — panels, menus, the debugger window.

Three things fall out of that. The backend is testable without a GUI:
`rewind_test` and `resume_guard_test` link it with no Qt anywhere, while the
`debugger_*` suites are the ones that need a Qt build. The emulator can own
debug state permanently without dragging Qt into the core. And because the
backend is present in *every* build, `--magic-breakpoint` and `--trace` are
plain CLI flags rather than GUI-only features.

One caveat about "pure": `jnext_debug` does link SDL3, because
`rewind_buffer.cpp` includes `core/emulator.h`, which reaches `input/keyboard.h`
and thence `SDL.h`. The rule the split enforces is *no GUI toolkit*, not *no
dependencies*.

### The four published headers

`src/debug/` also holds four headers that are a *contract* rather than code:
`debugger.h`, `events.h`, `inspect.h` and `result.h`. They declare
`jnext::dbg::Debugger` — one frontend-agnostic facade over control, inspection,
mutation, events, time, input injection, capture, bookmarks, symbols and
sessions — together with its value types (`Result`, `Expected<T>`, `Event`,
`Subscription`, `MemSpace`, `RunState`, `Listener`, `Service`). They are the frozen
interface of epic [#276](https://github.com/jorgegv/jnext/issues/276), landed
first and alone so that the Qt refactor, three protocol servers (DZRP, ZRCP, GDB
RSP) and the scripting DSL can all be written against one agreed shape. The design is
`doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md`; the map from each of its
capability ids to each declaration is
`doc/design/debug-subsystem/b0-cap-traceability.md`.

Two properties are worth knowing before the implementation exists, and both are
**gated** rather than merely documented — the first review of these headers found
them asserting things nothing checked.

`Emulator` is only *forward-declared*, and none of the four reaches
`core/emulator.h`, `src/platform/`, Qt, SDL, `memory/mmu.h`, `video/renderer.h`,
`video/palette.h`, `video/timing.h`, `debug/debug_state.h` or
`debug/breakpoints.h` — the point of the epic is that a frontend stops holding an
`Emulator*`. **`test/lint-debug-headers.sh`** proves it: for each published header
it preprocesses a one-line translation unit and matches that forbidden set
against the `-M` dependency list, so a forbidden header pulled in three levels
down is caught like a direct include. The patterns see each header's path
relative to the include root, never the directory the run happens to live in. It
is row 5 of the regression preflight, and `make harness-selftest`'s HS-57a/b
prove it stays wired and that its verdict still turns that row red; HS-57c that
the verdict does not depend on the temp or source path.

**`src/debug/debug_types_check.cpp`** is what makes the headers compile at all —
a translation unit of nothing but `static_assert`s, built in all four
configurations. It pins the three backend-owned enums that mirror something else
(`StepMode` against the internal `::StepMode`, `PaletteId` against `::PaletteId`,
the screenshot layer mask against `Renderer::LAYER_*`) value by value; every
`Result` enumerator's number, because the order is a contract adapters map by
index; each of `Listener`'s seven methods individually, since
`is_abstract` on the class passes even when one method gains a body; and
`MachineInfo`'s two clock domains against `MachineTiming`, names and types and the
eight-master-cycles-per-T-state relation.

One idiom in those headers is worth recognising, because it is the only C++
mechanism that does the job: `EventKind`, `Layer`, `RegId` and `ClipLayer` each
end in a `Count` sentinel, and every count is derived from it. Deriving a count
from the last real enumerator is blind to an *append* — the enumerator keeps its
value, the count keeps its number, and a new kind ships with no mask bit and no
switch arm.

### What is behind the facade today

The bodies arrive in five sub-packages on one branch, and three of them are in.

**B1 — control and inspection over the existing primitives**, with no change to
the hot path. `src/debug/debugger.cpp` holds construction, the mutation log,
symbols and the state/rewind verbs; `debugger_control.cpp` the CAP-CTL verbs;
`debugger_inspect.cpp` the CAP-INS read and write surface; `debugger_input.cpp`
the level half of input injection. All of the state lives in a `struct Impl`
behind one `unique_ptr` (`debugger_impl.h`, internal), so the later sub-packages
add their own state — the event table, the client list, the bookmarks — without
editing a header five frontends compile against.

**B2 — the event pipeline**, which is the first part of the epic that touches
the hot path at all. It is described in its own section below.

**B3 — the session**: clients, listeners, the service list that `pump()` drives,
the stop policy, `live_raster` / `attached`, and the cold-boot reconstruct
contract. Also described in its own section below.

The control verbs are the bodies of `DebuggerManager`'s slots with the Qt taken
out: the same `DebugState` calls in the same order, the same GH #207 / #221 /
#223 behaviour, the same two target computations for "run to end of frame" and
"run to end of scanline". What the Qt version did *around* them — four panel
`set_paused()` calls, `emit paused()`, `update_actions()` — is a frontend
reacting to a transition, and becomes a pushed notification later.

Two things had to be fixed in the emulator for the inspection surface to be
honest, and both are worth knowing:

**`Mmu::peek()`** is a non-perturbing read of the live CPU map. `Mmu::read()` is
the *guest's* read: it captures the byte into the +3 floating-bus latch on every
contended access and it raises the data-breakpoint latch on a READ watchpoint.
A debugger read must do neither — and not merely because the panels happen to
run outside `GuestExecutionScope`, since a script handler runs *inside* it. So
`peek()` wraps `read()` under `DebugState::InspectionScope` and puts the
floating-bus byte back. It wraps rather than copies, so the overlay arbitration
(boot ROM, Multiface, DivMMC, Layer 2, alt-ROM, config mode) cannot drift
between the two.

**The frame counter** now advances on every frame boundary. It used to be
incremented as an argument of `take_snapshot()`, so without
`--rewind-buffer-size` it stayed at 0 for the whole run and every consumer asking
"which frame is this?" got the same answer forever. The counter is
*post*-incremented, so during frame K it reads K+1: the backend reports
`frame_num() - 1`, which is the tag the rewind slot for that frame carries and
the frame `--delayed-keypress-frames N` lands on. A rewind lands on a frame
start the ring has already counted (`Emulator::at_restored_frame_start()`), so
`run_to_frame()` takes its base from that flag too, and the session's
`FrameEnded` detector watches the frame's *start cycle* rather than the
counter: the first frame run again after a rewind ends without the counter
moving (GH #278).

A verb whose machinery belongs to a later package is defined in ONE file,
`debugger_pending.cpp`, and returns `Result::Unsupported` — never a silent
no-op. Keeping them together means "what is not implemented yet" is something you
can count rather than a claim in a comment; the file's banner lists them by
owning package. Since B4 it holds one: `render_layer` (INS-14), whose move out
of the Qt video panel is package Q's.

`debugger_backend_test` is the backend's suite, headless and Qt-free: a wiring
row per control verb (arm it through the facade, run, assert the machine stopped
where the verb promises — PC, cycle, pause reason), a *control* row per verb that
the same program runs straight past, and the non-perturbation and frame-counter
rows above.

Everything else the rest of this chapter describes — `DebugState` consulted per
instruction, `BreakpointSet`, `DebuggerManager` driving the panels — is still how
the debugger works today; the frontends have not been moved onto the facade yet.

### The event pipeline (B2)

Today's event vocabulary is "a PC breakpoint, a watchpoint, one one-shot, and
nothing is conditional". B2 replaces it with the fourteen kinds of §4.3 of the
architecture document, and the shape of the replacement is one sentence:

> A site inside an instruction **latches**; a boundary with the machine stopped
> **delivers**.

```
site  ->  cheap filter  ->  LATCH into the ring          (inside an instruction)
----------------------------------------------------------------------------
boundary  ->  drain  ->  build Event  ->  per-subscription filter
          ->  Condition  ->  Handler  ->  verdict  ->  Stop / Log / Continue
                                                        (machine stopped)
```

No user code runs inside `Mmu::write`, the CPU or a device tick. That is what
makes "an inspection read is side-effect free" a property of the code rather
than of the caller's discipline.

**The subscription table.** `EventTable` (`src/debug/event_table.h`, internal)
holds the subscriptions, the 512-entry latch ring and the INS-17 delivery
history. It lives in `Debugger::Impl`, and `DebugState` holds a POINTER to it —
which is how the eight `Mmu` watchpoint sites, `PortDispatch`, `NextReg::write`,
`Copper::execute` and `Dma::execute_burst` all reach it without any of them
seeing an `Emulator*` or a `Debugger*`.

Each entry caches one bool, `live` = `enabled && (transient || master) &&
client_enabled(owner)`, recomputed whenever anything changes and never
per instruction. `transient` is exempt from the master switch, which is what
keeps Step Over working on a machine whose breakpoints the user has all
suspended.

**The hot path is one byte.** The eight `Mmu` sites used to open with
`debug_state_ && watchpoints_live() && has_any_watchpoints()` and then scan a
`vector<Watchpoint>` linearly on every access. They now open with
`debug_state_ && watchpoints_live() && rd_watch_armed(addr)`, where the third
term is a per-8-KB-slot mask byte: one load, a shift and a test. Everything
behind it — the legacy `has_watchpoint()` scan, the precise range match and the
ring append — is out of line in `Mmu::watch_read_` / `watch_write_`.

The mask bytes live on `BreakpointSet`, not on `DebugState` where §6.1 of the
design puts them, and the reason is lifetime rather than taste. They have TWO
contributors — that class's own live watchpoints and the `EventTable`'s `Mem`
subscriptions — so the two have to be pre-ORed somewhere; and putting them on
`DebugState` means `DebugState` has to learn about every mutation of the
breakpoint set, i.e. register a `BreakpointSet` observer. `BreakpointSet` is
copied out and moved back by `emulator_cold_boot()`, and it carries its
observers with it, so such an observer would come back pointing at the destroyed
`Emulator`'s `DebugState`. Where they are, the legacy half is recomputed by
`rebuild_live_()` — which every mutator already calls — so there is no
notification to forget.

The mask is strictly NARROWER than the bool it replaced: an I/O watchpoint
contributes to a separate `port_watch_armed()` flag and to no memory slot at
all, where before it opened the memory gate on every access and then failed the
scan.

**Delivery, and where each kind lands in time.** `Execute` is the only kind
delivered BEFORE the instruction runs — that is what lets a handler write PC
and redirect. Everything else is delivered at an instruction boundary:

| Latched at | Delivered | Late by |
|---|---|---|
| `Mmu` read/write site | the raising instruction's own boundary | nothing |
| `PortDispatch` (reads AFTER dispatch, so the value is the one the guest got) | same | nothing |
| `Dma::execute_burst` (Start / Byte / End) | the boundary of the slot the burst ran in | nothing |
| `NextReg::write`, CPU writer | the NEXT boundary | ≤1 instruction |
| `NextReg::write`, Copper writer | the NEXT boundary | ≤1 instruction |
| `Copper::execute` (Move / Wait / Halt) | the NEXT boundary | ≤1 instruction |
| `on_scanline` | the NEXT boundary | ≤1 instruction |
| `end_of_frame` | that frame edge, before its auto-type tick (B4) | nothing |

The `≤1 instruction` entries are all the same fact: the boundary drain runs
BEFORE `tick_devices_after_instruction()`, and that is where the Copper and the
deferred CPU NextREG queue run. Moving the drain behind the device cluster would
change the GH #265 early-return contract for every data breakpoint, so the delay
is accepted and stated instead.

**DMA is not one of them**, and the first version of this table said it was. A
burst runs from `dma_.execute_burst()` inside `step_one_instruction()`, before
that slot's drain — `tick_devices_after_instruction()` contains no `dma_.` call
at all — so a `Dma` event, and the `Mem`/`Port` events of its own bytes, are
delivered at that slot's own boundary like the CPU's accesses. A DMA NextREG
write is the one DMA-adjacent case that IS late, because it goes through the same
deferred CPU queue as any other NR write.

**`cycle` is slot-granular for a site-latched kind.** The master clock ticks once
per instruction slot, so every event one instruction raises shares that slot's
start cycle — two `Mem{Write}`s from a single `LD (nn),HL` are indistinguishable
by `cycle`. `Scanline` is the exception and is exact: the site hands the latch the
line's own boundary cycle, and the stamper derives `vc`/`hc` from whichever cycle
it is given. Its `cvc` comes from the line NUMBER rather than from a cycle,
because `cvc` steps at raw `hc == hc_ula_zero_raw_hc()` and not at raw `hc` 0
(GH #257) — sampling it at the boundary would report the previous `hc_ula` line
and put a one-line error in the user-visible filter.

**The whole of a delivery runs under one `DebugState::InspectionScope`**, and
that is not optional. A delivery happens inside `run_frame()`'s
`GuestExecutionScope`, where `watchpoints_live()` is true — so a handler that
pokes an address it is watching would latch a watch on itself. `poke(Cpu)` takes
a second scope of its own, so the property holds for any caller on any path.

**`Mmu::write` latches before the overlay arbitration.** The watch check is at the
TOP of the function, before the Multiface / DivMMC / Layer 2 / alt-ROM /
config-mode cascade and before the `read_only_` drop, so a guest write into ROM
that lands nowhere still raises `Mem{Write}` — with `prev == value`, and nothing
in the payload saying the write was dropped. That is what a pre-B2 WRITE
watchpoint did, and it is what a user watching "who writes here" wants.

**A debugger write is not an event.** `NextReg::write` is the ONE hook for every
NextREG writer, and it is gated on `DebugState::guest_access()`: a panel's
`nextreg().write()` and a script's `nextreg_write` both run with that false, so
neither fires a `NextRegWrite` on itself. A Copper MOVE is additionally excluded
from that hook, because the Copper's own site already latches it and §4.3
requires ONE ring entry fanned out at the drain to both `Copper{Move}` and
`NextRegWrite{source=Copper}` — never two.

**The ring is bounded, and says so.** 512 entries, derived in §4.3 from the
Copper's per-master-cycle cadence. On overflow it keeps the FIRST N entries in
order, counts the rest, and marks every delivery of that boundary
`overflowed{dropped}` — a subscriber whose own event survived still has to know
the boundary was lossy. It is a tested path, not a defensive comment:
`EventTable::shrink_ring_for_test()` shrinks the ring and a Copper MOVE burst is
driven over it on purpose (`EVT-OVF-*`).

**The no-subscriber cost.** `Copper::execute` runs once per master cycle and is
8-12 % of the `copper-demo` / `beast` profiles, and `Dma::execute_burst` runs
once per byte, so neither reads the table at its site: each carries plain bools
that the backend sets from `subscribe()` — and there is **one per sub-kind**, not
one per engine. A `Copper{Halt}`-only subscriber must not accumulate a `Move`
entry per master cycle, and ring space is observable, so
`EventTable::has_copper_sub_kind()` / `has_dma_sub_kind()` are what
`gates_changed()` reads. (The first version armed the whole Copper engine from
`has_kind(Copper) || has_kind(NextRegWrite)` and left `has_copper_sub_kind()`
with no caller at all.)

The one thing EVERY user pays is a single 16-bit store per instruction,
`debug_slot_pc_ = pc_pre_exec` — the pre-execution PC a latch cannot recover once
`cpu_.execute()` has moved on. So "the no-subscriber cost is none" is properly
"one store, measured as noise": `test/bench/ab-hotlatch.sh` bounds it below the
run-to-run spread.

**The measurement lives in the tree.** `test/bench/bench.sh` measures one binary
at a time and cannot arm a watch, so `test/bench/ab-hotlatch.sh` (`make
bench-hotlatch`) runs the interleaved A/B §6.2 prescribes over the
`JNEXT_BENCH_WATCH` fixture in `headless_app.cpp`, and prints each variant's own
hit count so a run states whether the watch actually fired.

**`pause_reason` (CTL-13) needs evidence that survives the stop.** Nothing in
the tree used to record WHY the machine stopped: `DebugState::pause()` clears
the step mode, and the hot loop consumes the data-breakpoint latch in the same
breath as the pause. So there are now three records — the backend's armed-verb
reason, `DebugState::note_watch_stop()` / `note_magic_stop()` for the two stops
the machine causes, and the drain's own latch for a subscription stop — and
`Debugger::state()` reads them in a documented order: `Corrupt` first (CTL-11
makes it the thing that refuses every resume), then the armed verb, then the
subscription, then Magic, then a legacy watch, then a legacy PC breakpoint.

**Step Over and Run to Here** no longer use `BreakpointSet`'s single one-shot:
they arm a transient `Execute` subscription, which §4.3 makes unlimited in
number (DeZog needs two temporary breakpoints per `CMD_CONTINUE`), exempt from
the master switch, and auto-removed at the next stop. `run()` deliberately does
NOT drop them — a continue that cleared what the same operation had just armed
could not work — so the drop happens at a stop the backend causes, or on an
explicit `Debugger::pause()`, which is the transition `resume()`'s
`clear_oneshot()` was really standing in for.

**That evidence must not outlive the machine it describes.** Every restore and
every reset replaces the machine wholesale, so a `pause_reason` left over from
before it names an instruction that, on the machine now in memory, never ran.
There is exactly one place that reconciliation happens:
`Emulator::debug_after_machine_transition_()`, called from `load_state()` (which
every restore routes through — `load_state_bytes`, `step_back`,
`rewind_to_frame`, `run_back_to_cycle`, a `.jns` load's closing round trip) and
from the end of `init()` (which is `soft_reset()`). It clears `DebugState`'s stop
records, the pending Stop, the latch ring, and — through
`DebugState::set_machine_replaced_hook()`, because nothing below a frontend may
hold a `Debugger*` — the backend's own armed verb and `EventTable::hits_`. It
then re-derives the eight slot pages, since the restore rewrote the page map the
§6 masks are computed from.

Two details are load-bearing. The ring is **kept** for a reset and discarded for
a restore: `soft_reset()` latches its own `Reset` event before calling `init()`,
so that the event carries the pre-reset cycle, and discarding the ring there
would throw away the event that reports the transition. And the reconciliation
runs from a scope guard at the **top** of `load_state()`, not as its last
statement: `load_state()` has some thirty sentinel early-returns and the first
thing it does is load the clock, so a torn restore is a machine transition too —
`state()`'s Corrupt-first precedence merely hides the stale reason until
`acknowledge_corruption()` drops the mask.

### The session (B3)

The backend does not own a thread and does not own the frame loop. A **loop
owner** — `QtApp`'s timer tick, the SDL loop, `HeadlessApp::run()` — owns both,
and B3 is the seam between them.

**Clients.** `attach(ClientInfo)` returns a `ClientId`; every verb that mutates
or transitions takes one as its first argument, because §4.1 requires each
transition to be broadcast with the client that caused it. The client list holds
each client's listener, its `live_raster` request and its bookmarks (B4) —
*outside* `Emulator`, which is what lets all of it survive a machine
reconstruct.

`detach(cid)` removes that client's subscriptions — and every other record
keyed by its id (its per-client event switch, its unflushed capture failures:
ids are never reused, so anything left behind would be kept for ever) — and,
**iff the machine is paused by this client**, resumes it. A pause by another client survives, and an
*unowned* pause is never released by any detach however many clients come and
go: `PauseReason::Magic` and `PauseReason::Corrupt` carry `by == CLIENT_NONE`
because neither is anyone's verb. There is no "last client" rule — the Qt
adapter is attached for the process lifetime, so a remote is never the last one,
and the point of the rule is that a crashed DeZog must not leave the machine
hung.

**`attached()` is the OR of two contributors, for now.** `DebugState::active()`
is what "a frontend is driving this machine" means on today's tree: the Qt
debugger window sets it when it opens, and the magic-breakpoint hook sets it when
the opcode executes. Neither is a backend client yet. So `attached()` is
`live_clients > 0 || DebugState::active()`, and the client term is its **own
bit** on `DebugState` (`clients_attached_`) rather than a second writer of
`active_` — because a `detach()` of the last client would otherwise clear a flag
the Qt window owns, and nothing in `DebugState` can tell the two owners apart.
`refresh_gates_()` ORs the three (`active_ || clients_attached_ ||
persistent_`), so the identity `armed() == attached() || persistent()` holds
whichever contributor is set, and `SuspendScope` clears all three — its promise
is "disarms breakpoints", and that is only true if it clears every contributor.

**The two flags that replace `active()` in the hot path.** §4.1 splits what
`active()` used to switch on: `attached` gates the *step machinery* (Step Out's
per-instruction test, the `STEP_BACK` / `RUN_BACK_TO_CYCLE` step modes), and
`live_raster` — per client, ORed — gates only the *render-every-frame hint* and
the per-instruction `VideoTiming::advance()` walk. `refresh_gates_()`
precomputes both into `DebugState` bits (`attached_ = active_ ||
clients_attached_`, `raster_live_ = active_ || live_raster_`), so each hot-path
reader still pays one bool load, and `active_` stays a term of both until the Qt
window becomes a client. Before the split a machine driven only by a remote
client never finished a Step Out. `Debugger::attached()` and `live_raster()` read
those same bits back rather than re-deriving them, so the answer a client gets
and the gate the hot loop obeys cannot disagree.

**Listeners.** Seven pushes, all pure virtual (a silently ignored notification is
what a default empty override invites): `Paused`, `Resumed`, `Reset`,
`FrameEnded`, `SubscriptionsChanged`, `ExitRequested`, `Log`. They are
synchronous, on the emulation thread, and must do no UI work — the Qt listener
records and acts on its own tick.

`Paused` / `Resumed` / `FrameEnded` / `SubscriptionsChanged` are **not pushed at
each transition site**. There are seven ways out of paused and a dozen into it,
and a push at each is the two-lists failure. Instead one function compares
`paused()`, `DebugState::resume_generation()`, the raw frame counter
(`Emulator::frame_num()` — not the clamped frame tag, which reads 0 both before
anything has run and after frame 0 ends) and `EventTable::revision()` against
what was last pushed, and it is called from
`pump()` — the slot §4.8 specifies, so "a stop in this tick's frames is notified
in this tick's pump". The resume generation is what makes a
stop-resume-stop between two pumps two pushes rather than none: `paused` is true
at both ends. `Reset` is the exception and is pushed synchronously by the verb,
because CTL-12 requires it to reach every listener *before* the verb returns —
that is how an adapter whose client is blocked in a `run` completes the reply.

**`pump(PumpBudget)`** is the loop owner's once-per-tick service call, made after
the tick's frame batch. It drains the registered `Service`s, flushes their
notifications and syncs the pushes. The drain has two arms and they differ in
kind, not degree: while **running**, each service is asked for at most one
command whatever the budget says, because the loop owner needs its thread back
for the next frame; while **paused** it keeps answering while the peer keeps
talking, bounded by `budget_ms` — a DeZog ZRCP step is ~15 sequential round
trips, which at one per tick would be 300 ms. "Paused" is the machine's state
*after each command*, not at entry: a `run` in the chain hands the loop owner
its frames back at once, and a `pause` arriving while running lets the reads
behind it be answered in the same pump. `budget_ms == 0` therefore means
*one* command, not "unbounded". The budgets are the one place in the backend that
reads a wall clock, and legitimately: §4.8 calls them host service parameters,
and nothing in the emulated timeline depends on any of them.

`pump()` refuses to run from inside an event delivery, and refuses rather than
asserting — an `assert` compiles away in the build where a frontend bug would
ship, and re-entering the drain would deliver a boundary's events twice.

**The stop policy** (`StopPolicy::Pause` | `ExitNonZero`) is the loop owner's to
set, never an adapter's: Qt is `Pause`, SDL and `--headless` are `ExitNonZero`,
because the SDL frontend has no pause path at all and a headless run is a CI
verdict. `stop_policy()` returns **what was set** — a setting that reads back as
something else is a trap for whoever wrote it — and the §4.8 override
(`ExitNonZero` becomes `Pause` while a remote client is *connected*, so a client
blocked on `run` gets its stop reply) lives at the one place the policy is
consumed. The exit code with no script to name one is **3**: never 2, which both
harnesses use for a harness fault, and 1 stays "jnext could not run".

An explicit `pause()` is a stop that drops the transient subscriptions but is
**not** an `Action::Stop`, so it never requests an exit. One function serves both
with a parameter, so the two arms stay next to each other.

### The cold-boot reconstruct contract (CTL-12)

A hard reset is modelled as a power-on cold boot the *frontend* performs:
`emulator_frontend_cold_boot()` destroys the `Emulator` and placement-news a new
one at the same address, then re-runs `init()`. `&emu` stays valid, which is what
lets a `Debugger` live across it — but every sub-object is new, and in particular
the `DebugState` is. A surviving `Debugger` is then **silently disconnected**:
every subscription still exists and lists as live, and not one can ever fire.
Nothing in the frontend can detect it.

So the backend re-applies, from one function, whatever route landed the new
machine — `reset(Hard)`, `load()`, or the loop owner's `on_cold_boot_done()`
after a guest NR 0x02 reset:

1. the three publications the constructor makes (`set_event_table`,
   `set_event_hooks`, `set_machine_replaced_hook`) — the fourth hook on
   `DebugState`, the latch stamper, is the Emulator's own and `init()`
   re-installs it, which is also why the destructor leaves it alone;
2. the eight-page seed — `on_slot_remapped()` early-returns while the table is
   null, so every `rebuild_ptr()` during the new `init()` was discarded, which
   makes a page-qualified `Mem` filter wrong in *both* directions;
3. `gates_changed()`, the only writer of the event-mask half of the hot-path
   gate across the boot — the platform's `BreakpointSet` copy drops that half
   before it is restored, so the rebuilt machine's event gate stays closed until
   the backend re-opens it from the live subscription table;
4. the latch ring — it lives on `Debugger::Impl`, so it *survives* the
   reconstruct while everything in it describes a machine that is gone;
5. `arm(Kind::None)`, because `init()` fired
   `debug_after_machine_transition_()` while the hook was still null. `None` and
   not `User`: `state()`'s precedence falls *through* `None` to the legacy
   PC-breakpoint check and matches `User` immediately, so `User` would silently
   swallow a breakpoint at the landing address;
6. the pause, iff the machine was paused — "paused stays paused, running stays
   running", and there is no `Reset` pause reason, so a client's `reset(Hard)`
   never pauses a running machine. The pause keeps its *owner*: all three routes
   share one capture taken before the machine goes (the pause in force and whose
   it is), and a pause a client owned comes back as `User{that client}` — held
   for it — so SES-01's detach can still release it. Re-applied bare, it would
   read as the unowned fallback, which no detach releases: a remote that
   hard-reset a paused machine and then crashed would leave it hung. An unowned
   pause stays unowned (the `None` fall-through of item 5);
7. the enable flags a client set through a verb — call-stack tracking, the
   trace and `persistent_breakpoints`, which live on the `Emulator` and are reset
   by the reconstruct. The backend keeps its own record of each request
   (`Impl::want_*`, empty until a client sets it) and re-applies only what was
   asked for, so a machine nobody configured comes back with its defaults and a
   config-set `--persistent-breakpoints` is not clobbered. It never reads the
   dead machine, which is why the guest path can do it too. The Qt panels still
   switch call-stack tracking and the trace directly on the `Emulator` until
   package Q, so those are not captured;
8. the per-client state, which is only the arm bit and the `live_raster` OR: the
   subscriptions, switches and symbol table live on `Impl` and never went
   anywhere;
9. on a machine that was actually rebuilt, a fresh CTL-11 corruption guard: its
   acknowledgement is keyed to the `Emulator`'s `state_error_generation()`,
   which the rebuilt machine restarts at 0, so a kept acknowledgement would
   pre-acknowledge the new machine's own first corruption;
10. the notification edge detector, re-based on the rebuilt machine: its resume
   generation and frame counter restart at 0 while the detector's baseline lives
   on `Impl`. Every route first *flushes* the old machine's pending edges (a
   stop, a resume, the frames since the last pump), so re-basing loses nothing;
   the one exception is a `done` with no `begin`, whose machine is gone before
   the backend hears of it. `last_paused` is not re-based: it is what listeners
   were last told, so paused-to-paused pushes nothing and paused-to-running
   pushes `Resumed`;
11. (B4) the INS-20 coverage sink — the only thing of coverage's the machine
   held, a pointer into the backend's bit set; the set recorded before the boot
   is kept, because "since clear" is not "since boot";
12. (B4) the queued screenshots' hold on the machine: the head's layer mask on
   the new `Renderer` and the force-render bit on the new `DebugState`, with every
   capture's wait re-based on the rebuilt machine's rendered-frame counter, which
   restarted at 0. Bookmarks need nothing: they live on the client rows.

**No verb that drives the machine runs from inside a delivery.** A handler runs
with `run_frame()` — or the pre-instruction gate inside it — still on the stack,
and the drain walking the latch ring and building the boundary's `matched[]`. So
every verb that would EXECUTE the machine (the step verbs, a `save_state_bytes()`
that has to advance to a frame boundary), CHANGE ITS RUN STATE (`pause`, `run`,
`step_out`, the `run_to` family — each re-arms the stop evidence, which would
rewrite the very stop the handler is part of), REWIND or RESTORE it (`step_back`,
`rewind_to_frame`, `load_state_bytes`, `bookmark_restore`), or RESET or REPLACE
it (`reset` of either kind, `load`, `on_cold_boot_done`, and the one NextREG
write that resets — NR 0x02 with the soft bit, through `nextreg_write` or
`port_out`) refuses there with `Unsupported`, through one helper. The three
verbs that save the machine — `save_state_bytes`, `bookmark_save`,
`save_snapshot` — refuse only when they would have to *advance* to a frame
boundary, through the one frame-boundary helper they share. A handler stops the machine by returning
`Action::Stop`. Mutations stay allowed (§4.2a), `raise_host_event` is designed to
nest, and `detach` is a session verb — its release of the departing client's own
pause goes through `run()`'s body rather than the refused public verb.

Then the `Reset{Hard}` event is latched (after the ring discard, or it would go
with the stale entries) and `Reset{Hard}` is pushed to every listener before the
verb returns.

`reset(Hard)` runs the loop owner's sequence **synchronously**, through the
`LoopDriver` closure registered by `set_loop_driver()` — the sequence lives in
`src/platform/`, above the backend, so a closure is the only way the backend can
reach it. With no closure registered the verb refuses with
`RefusedUnavailable`. `load()` re-applies **unconditionally** — idempotent on a
load that replaced nothing — because the loop owner's closure may load in place
(`emulator_apply_load()`), cold-boot first (the Qt menu route), or re-`init()`
in place (`load_rzx` with an embedded snapshot). What it does need to know is
whether the machine was *reconstructed*, because a reconstructing load is a cold
boot and owes every other client the `Reset{Hard}` push and event, exactly as
`reset(Hard)` does; a load that did not reconstruct pushes nothing. The backend
tells the two apart from its own publication: `DebugState::events_` points at
`Impl::events` from the constructor on, and only a brand-new `DebugState` — a
reconstruct — can make it point anywhere else.

**The guest path is a pair of notifications.** A guest NR 0x02 hard reset is
performed by the loop owner, not by the backend, and by the time
`on_cold_boot_done()` runs the paused machine is gone — so the loop owner calls
`on_cold_boot_begin()` immediately *before* it destroys the machine, and the
backend takes the same capture there that `reset(Hard)` takes before its driver.
`on_cold_boot_begin()` is the one declaration added to the frozen header after
B0 (owner decision). The pairing is pinned state by state: `begin` then `done`
keeps the pause and its owner; `done` without a `begin` re-applies the rebuilt
machine's own state, unowned; a second `begin` replaces the first; a
`reset(Hard)` or `load()` in between discards a pending capture; a detach of the
capture's owner releases the pause it recorded; and neither call needs a driver
or refuses on a corrupt machine.

**The single-owner rule, split.** §4.1 CTL-12 says the platform-side
`BreakpointSet` / `active()` save-and-restore in `emulator_cold_boot()` becomes
"a second owner of the same state" once the backend re-applies subscriptions.
Measured, only one part of what it carries is backend state — the event-mask
half of the hot-path gate — and B3 retired that half: `emulator_cold_boot()`
zeroes it on its copy, and the backend's `gates_changed()` is its single owner.
The rest — the *Qt panels'* breakpoint model, the observers that travel on its
copy (the only reason `BreakpointPanel` and `DisasmPanel` stay subscribed; each
registers once in its constructor), and `saved_active`, which keeps an open
debugger window armed — has no other owner before package Q, so retiring it now
would lose a user's breakpoints on every hard reset, unsubscribe two panels and
leave an open window unarmed. Package Q retires it when the panels become
clients. The reasoning is recorded at the site.

### Input, capture, bookmarks and coverage (B4)

**Pulses append.** `press_key(name | matrix position, hold_frames)` (IN-01)
queues a pulse on `Keyboard`'s auto-type queue, and `queue_auto_type()` itself
now APPENDS for every producer — the phantom typist, the two tape `LOAD ""`
sites, `--delayed-keypress` and the backend. Replacing, it let a second
producer in the same frame clobber the first and stranded a held key down. An
append behind an entry in flight leaves that entry's counters alone (a held key
does not restart its hold); onto an *idle* queue the counters reset exactly as
the replacing version did, so a producer that finds nothing queued sees no
change. The 16-entry cap covers the union of what every producer queued, and a
pulse that does not fit is refused with the count that was queued (0).

**The injection edge (REQ-dsl-20).** Every pulse and every level set issued
during frame N — from a handler, from a remote command in a pump, from a CLI
countdown — lands at the END of frame N and is visible from frame N+1. For the
levels (`set_key`, `set_extended_key`) that means a queue: they no longer touch
the matrix at once, they are queued on `Keyboard` and applied first thing in
the edge's `tick_auto_type()`, so a handler mid-frame cannot change what the
rest of its own frame reads. And `end_of_frame()` now delivers the `Frame`
event BEFORE that tick (B2 had it after), so an `on frame N` handler's pulse is
pressed by frame N's own tick — the frame `--delayed-keypress-frames N` gives.
The price is that the window between that drain and the end of the tick is not
the state `run_frame()` hands back (the tick has not run), so the backend does
not treat it as a frame boundary: a save from a `Frame` handler answers
`NotAtFrameBoundary`, or is refused if it would advance. A restore drops queued
levels, like the pulses it replaces; a reconstruct drops both with the old
`Keyboard`. `set_joystick` and `press_nmi` stay immediate — the contract names
IN-01 and IN-02 only.

**Hosted by every loop owner.** `HeadlessApp`, `SdlApp` and `QtApp` each build
one `Debugger` in `init()` and keep it for the process: they register the
`LoopDriver` (a client's `reset(Hard)` runs the loop owner's own boot; `load()`
its load dispatch), bracket every cold boot THEY decide on — a guest NR 0x02
hard reset, a NEX load request, F1, a menu load — with `on_cold_boot_begin()` /
`on_cold_boot_done()`, and call `pump()` once per tick after the frames (and,
in SDL and headless, after the cold-boot polls; Qt pumps in `post_frames`). With
no client attached this arms nothing, so a run with it is bit-identical to a run
without it (rows HOST-01..05, the last three through the real `HeadlessApp`).
`QtApp::debugger()` is the instance package Q's `DebuggerManager` is to use.

Because none of those calls changes an unattached run, nothing a normal run does
can show one missing. The `JNEXT_HOST_PROBE` fixture (`src/platform/host_probe.h`,
env-gated, zero-cost unset, deliberately not a CLI flag) makes them observable:
a client that runs inside `pump()` as a `Service`, pauses the machine, raises the
guest hard-reset request, reports whether the loop owner's cold boot came back
with `Reset{Hard}` pushed and the pause still its own, resumes, and then asks for
`reset(Hard)` through the registered driver — one `HOSTPROBE` log line each. The
regression rows `sdl-host-probe-func` and `qt-host-probe-func` read those lines
for `SdlApp` and `QtApp`; row HOST-07 runs the same probe through `HeadlessApp`.

**The CLI `--delayed-*` flags keep their own countdowns.** Each loop owner counts
LOOP TICKS for every `--delayed-*` flag, as before — a tick count survives a
cold boot and keeps counting while the machine is paused, which is what keeps
`--delayed-automatic-exit` a hard bound; a `Frame` tag does neither. Only the
ACTIONS go through the backend: `press_key`, `press_nmi` (which now calls the
F9/F10 hotkey functions themselves, gates included), `save_snapshot`, and
`screenshot()` — queued when the count reaches zero, written by the tick's pump,
its outcome read back with `flush_captures()`. The CLI-facing messages and exit
codes are the loop owners' and did not change.

**Screenshots are deferred to the next rendered frame.** `screenshot()` only
queues. `pump()` — the loop owner's post-frames slot — writes every capture
whose frame has been rendered since it was armed (`Emulator::rendered_frames()`
moved), before any command of that pump can touch the machine. While a capture
waits, its layer mask is armed on the renderer and a force-render bit makes sure
the next frame IS rendered, whatever the frontend's render-skip hint says. A
paused machine renders nothing, so the capture is held, with one warning.
`flush_captures(by)` — the one declaration B4 added to the frozen header, by
owner decision — is the exit bound: `NoFrame` if any of `by`'s captures is still
pending (they are dropped), `RefusedUnavailable` if one failed to write since
the last call, else `Ok`. A capture survives its requester's detach and every
machine rebuild; once its requester has detached, its outcome is still logged
but no longer recorded for a client that is gone. The
PNG and `.SCR` writers moved from `src/platform/` to `src/core/screenshot.*` so
the backend, which sits below the platform layer, can call them.

**Saves share one rule.** `save_state_bytes`, `bookmark_save` and
`save_snapshot` reach the frame boundary through one helper: at a boundary they
save; mid-frame, `RefuseMidFrame` refuses and `AdvanceToBoundary` runs the frame
out under `SuspendScope` and says so in a `MUTATE clock … by <client>` line.
`save_snapshot` always advances (the `--delayed-snapshot` rule) and writes the
file by extension through `save_snapshot_file()` in `src/core/`. Bookmarks are
per client, at most 8 (a 9th new name is refused; re-saving a name replaces it),
freed at detach, and survive a hard reconstruct; a restore into a machine of
another type or snapshot width is refused before `load_state` runs, so nothing
is latched.

**Coverage and the trace.** INS-20 coverage is recorded in
`step_one_instruction()`, the one body `run_frame()`, the debugger's Step and
`execute_single_instruction()` share — not in `run_frame()`'s armed block,
which a Step never passes through — and only for a slot that actually fetched
the opcode at PC (an NMI or INT acknowledge does not run it). Switched off it is
one pointer test per instruction. Each trace entry now carries I, R, IM, IFF1,
IFF2, the word at SP (read with `peek()`, so the trace moves no watch and no +3
floating-bus latch) and the eight MMU pages.

## What `ENABLE_DEBUGGER=OFF` removes

`ENABLE_DEBUGGER` (default `ON`) gates **only the Qt UI**. With it off,
`jnext_debugger` is neither compiled nor linked and every use site in
`src/gui/` sits inside an `#ifdef`. `jnext_debug` is linked unconditionally,
and `Emulator::debug_state_` is an ordinary member either way.

That is deliberate, because the hot loop's cost is not "is the debugger
compiled in" but "is it *active*". `DebugState::active_` starts false and turns
true only when the UI enables the debugger or a magic breakpoint fires.

There are **two** booleans, and the split is load-bearing (GH #219).
`active_` means *the debugger is driving the machine*: it gates the step modes
(`OUT`, `STEP_BACK`, `RUN_BACK_TO_CYCLE`), the "render every frame" hint that
keeps the panels showing a live framebuffer, and `video_timing_.advance()`,
which maintains raster counters nothing but a human inspector ever reads.
`armed_` — `active_ || persistent_` — is the narrower *are breakpoints live*,
and it is what the per-instruction breakpoint test hangs off. `persistent_`
comes from `--persistent-breakpoints` via `EmulatorConfig`, and is what lets
breakpoints survive closing the debugger window without switching the rest of
that machinery back on. The call-stack pre/post hooks sit behind their own
`enabled()` flag.

`armed_` is a cached bool recomputed by the two setters rather than an
expression, so the default configuration executes exactly the load-and-branch
the single `active()` gate used to.

So "the debugger costs nothing when closed" is a claim about a predictable
branch, not about conditional compilation.

### A debugger read is not a guest access

There is a **third** boolean, and it exists because `Mmu::read()` is not the
CPU's read. The Watches, Memory, Stack and Disassembly panels all
inspect guest memory through the same `Mmu::read()` the CPU uses, so before
this gate existed a READ watchpoint on any address a panel happened to display
was latched by the panel's own refresh — at roughly 4 Hz while the machine ran
— and the next Run or Step stopped one instruction later at an unrelated
address. The snapshot savers did the same on every File > Save Snapshot: all
three sweep RAM through a temporary slot-7 window at `$E000`, and the 48K SNA
saver additionally pushes PC at `SP-2`.

`watchpoints_live_` is `armed_` **and** `guest_access_`, and `guest_access_` is
false unless the emulator has declared that it is executing — the RAII
`DebugState::GuestExecutionScope`, taken by exactly three functions:
`Emulator::run_frame()`, `step_frame_slot()` and
`execute_single_instruction()`. The watchpoint checks are therefore triple-
gated on pointer non-null, `watchpoints_live()` and — since B2 — the per-slot
mask byte `rd_watch_armed()` / `wr_watch_armed()`, which replaced
`has_any_watchpoints()` there (see "The event pipeline (B2)" above).

The point of putting the gate there rather than around panel refresh is that
it does not depend on the caller. A panel added tomorrow cannot fire a
watchpoint whatever it calls, because it cannot make `guest_access_` true; and
`MemoryPanel` in particular reads from its `paintEvent`, not from `refresh()`,
so a scope around the refresh call would have missed it anyway.

Its counterpart `DebugState::InspectionScope` covers the two debugger readers
that necessarily run *inside* execution: the trace log captures four opcode
bytes at `PC` and the call-stack tracker three, whatever the instruction's real
length, so both read data bytes past a short instruction that the CPU never
fetches. Both are switched on by the debugger, so both hurt exactly the user
who had it open.

The hot path pays nothing for any of this: `Mmu::read` and `Mmu::write` compile
to the same 395 and 285 instructions they did before, every difference being a
structure offset.

## Execution control

`DebugState` holds `paused_`, a `StepMode`, and a `BreakpointSet`. The run loop
consults it once per instruction, before the fetch:

| Mode | Set by | How it terminates |
|---|---|---|
| `NONE` + `paused_` | `pause()` | `run_frame()` returns immediately |
| PC breakpoint | `BreakpointSet::add_pc` | `should_break(pc)` matches |
| `INTO` | `step_into()` | loop pauses on the next iteration |
| `OVER` | `step_over(next_pc)`, or the backend's `step_over_subscribed()` | one-shot breakpoint at `next_pc`; through the backend, a transient `Execute` subscription there |
| `OUT` | `step_out(sp)` | `check_step_out()` matches, after the instruction |
| `RUN_TO_CYCLE` | `run_to_cycle()` | master clock reaches the target |
| `STEP_BACK` / `RUN_BACK_TO_CYCLE` | `step_back()`, `run_back_to_cycle()` | handled before the loop starts, by rewinding |
| watchpoint | `add_watchpoint` | `Mmu` latches `data_bp_hit` (guest accesses only); checked after the instruction |

### Enabling and disabling breakpoints

`BreakpointSet` keeps two things, not one. The **model** — `pc_all_`
(`addr -> enabled`) and `wp_all_` (each `Watchpoint` carrying its own
`enabled`) — is what the Breakpoints panel lists. The **live cache** —
`pc_live_` and `wp_live_`, the same container types the class had before any of
this existed — holds only what can actually fire, and is what `has_pc()`,
`has_watchpoint()`, `has_io_watchpoint()` and `has_any_watchpoints()` read.
`rebuild_live_()` recomputes the cache, and is called only by mutators; nothing
on the hot path ever rebuilds or filters.

That split is what makes a disabled breakpoint cost *nothing* rather than
merely little. The hot path's structures are unchanged and their contents are a
subset of the model, so a disabled breakpoint is not hashed, not compared and
not iterated. Disable the only watchpoint and its bit leaves the per-slot mask
`rebuild_live_()` maintains, so the eight `Mmu` watchpoint sites and
`PortDispatch` short-circuit exactly as on a machine that never had one.
(`has_any_watchpoints()` is still there and still true of the model; since B2 it
is no longer what the sites read.)

The **master switch** (`set_master_enabled()`) is the whole of
`rebuild_live_()`'s first line: with it off, the cache is left empty and the
model is not read at all. It therefore cannot consume a per-breakpoint flag,
which is what makes the off/on round trip exact by construction rather than by
a save-and-restore that has to be kept correct. One-shots sit outside the model
entirely, so Step Over, Step Out and Run to Here still work while every
breakpoint is suspended.

Two queries exist where there used to be one, and they are not
interchangeable: `has_pc()` is *live* (will this stop the CPU) and is what the
run loop asks; `pc_exists()` / `pc_enabled()` are the *model* and are what the
panel's checkbox and the gutter's marker ask. The gutter draws a filled dot for
live and a hollow ring for a breakpoint that exists but is suspended, and the
gutter's click-to-toggle asks `pc_exists()` — asking `has_pc()` there would
read a disabled breakpoint as absent and stack a second one on top of it.

Every one of those modes leaves `paused_` through `DebugState::unpause_()`,
which also arms a one-instruction **step-off**. The loop consumes it
immediately before `should_break()`, and that first test of a resumed run is
the only one it suppresses. It has to exist because the breakpoint check sits
*before* the fetch: without it, resuming with PC still on a breakpoint
re-matched the unchanged address and the machine re-paused having executed
nothing — issue #221, and it pinned Step Over, Step Out, Run to Cursor and Run
to EOF in place exactly as it pinned F5.

The arm suppresses the resumed-from address without storing one, and the
property that makes that sound is a **precondition**, not an accident of the
call sites: `unpause_()` raises the arm only on a real paused → running edge.
Given that, the machine was stopped between the arm and its consumption, so the
PC the gate sees is necessarily the PC the resume was issued at. Without the
edge check the guarantee is simply false — a resume issued on an
already-running machine arms against a PC that keeps moving, and the arm lands
on a later, unrelated breakpoint and swallows it. The UI reaches that state by
ordinary use: the debugger toolbar's Continue button is a plain `QPushButton`
with no enable gating, and `MainWindow` forwards the Run binding to `on_run()`
whenever the debugger is enabled, regardless of whether the machine is paused. The arm is also dropped by `refresh_gates_()` when
breakpoints go dead, because the consumer stops running there while PC does
not.

Step Over is not a special CPU mode. `DebuggerManager::on_step_over()` asks the
disassembler whether the current instruction `is_call_like()` — `CALL nn`,
`CALL cc,nn`, `RST n`, `DJNZ` — and if it is, sets a one-shot breakpoint at
`PC + instruction_length()` and resumes; otherwise it degrades to Step Into.
Run to Cursor is the same one-shot mechanism with a user-chosen address, and
Run to End of Frame and End of Scanline are `run_to_cycle()` with a computed
target.

Step Out is the one mode whose termination is decided *after* the instruction
rather than before it. `step_out()` records SP at the moment F8 was pressed;
`check_step_out()` is then called from the shared per-instruction body once per
instruction and ends the step when three things hold together: the instruction
popped exactly one return address (`sp_after == sp_before + 2`, which is what
tells a taken `RET cc` from an untaken one), its opcode was a return form
(`RET`, `RET cc`, `RETI`/`RETN` including the undocumented `ED` aliases), and
the pop unwound the stack *strictly past* the armed SP. That last condition is
what makes a nested call's own `RET` — and an interrupt handler's `RETI` —
return *into* the routine being stepped out of rather than end the step.

It is called from `step_one_instruction()`, not from `run_frame()`'s loop, so
free-running and single-stepping cannot disagree about where a step out ends.
That call did not exist at all until issue #203 was fixed: the mode was written
and never read, and F8 had the observable behaviour of Run.

Its position inside that body is load-bearing in both directions, and the
reason is the general one for any debugger hook here — **it may only read
memory the CPU itself read**. `Mmu::read()` is not inert: inside a
`GuestExecutionScope` it fires read watchpoints, and it latches the +3
floating bus whatever the scope. Reading the opcode speculatively
before the instruction runs is therefore unsafe, because `Z80Cpu::execute()`
has *three* early returns that complete a step without ever fetching at `PC` —
an accepted NMI, an accepted `INT`, and the esxdos shim. In those slots the
debugger's read is the only touch of that address, and with a watchpoint on it
the phantom hit ends the step at the interrupt vector instead of the routine's
return.

So the CPU is **asked** rather than deduced: `fetched_opcode_last_execute()`
is false through all three early returns and true only once the fetch has
happened, and the read is gated on it. Deducing it from `SP` movement does not
work, and the reason is worth knowing before inventing a fourth shim: NMI and
`INT` push, so they *do* move `SP` the wrong way and an arithmetic test catches
them — but the esxdos shim deliberately fakes a return's own `+2`, and is
indistinguishable from a real `RET` by arithmetic alone.

The decision also sits *before* the deferred RETN overlay clear, because a
`RETN` leaving a DivMMC-mapped routine unmaps it there, and a later read would
see the underlying page rather than the `ED 45` the CPU fetched.

Single-stepping goes through `Emulator::execute_single_instruction()`, which
shares its per-instruction body verbatim with the free-running loop
(`step_one_instruction()`), specifically so that the two paths cannot drift.
Stepping must *observe* the emulation and never alter it — see
[2.2 The emulator core](../02-architecture/02-the-emulator-core.md) for why
`run_frame()` refuses to re-begin a frame that is already in progress.

That function is the *raw* one-slot primitive, though, and the debugger does
not use it directly: `DebuggerManager::on_step_into()` calls
`Emulator::debugger_step()`. The difference is the frame boundary, which is
easy to overlook — while the debugger holds the machine the frontends stop
calling `run_frame()` altogether, so a step is the machine's only driver and
inherits its frame loop as well as its inner one. `step_frame_slot()` begins a
frame when none is in flight and calls the shared `end_of_frame()` once the
clock reaches the frame's last cycle. Without it, everything scheduled per
frame — the ULA frame interrupt above all — stops being scheduled the moment
the debugger pauses, and a `HALT`ed CPU can never be woken (GH #207). A step
issued at a `HALT` consequently runs the halt out rather than stepping one of
its internal NOP slots, bounded to two frames: the CPU leaves the halt only on
an accepted interrupt or NMI, and stepping a slot in which nothing observable
can change is not a step.

The primitive keeps its frame-agnostic behaviour deliberately. Much of the
test tree uses it to advance a machine whose frames the test drives itself,
and several suites depend on a step not touching frame state at all.

Two consequences of the halt-run are worth knowing. A Step that ends on a
watchpoint **consumes** `data_bp_hit_`, exactly as `run_frame()` does — the
halt-run loop reads that flag, so a latch left set would make every later Step
collapse back to a single NOP slot. And **Step and Step Back stop being
inverses across a halt**: one Step can execute ~10 000 internal NOP slots,
enough to saturate the circular trace buffer, while Step Back still undoes N
raw instructions. Nothing corrupts — the rewind buffer's own frame snapshots
are taken normally — but the two controls are counting different things, so
stepping *back* out of a halt is not one press.

## The key bindings are data (GH #1)

No shortcut in the debugger window is written at its call site. The inventory
is a table in `src/debug/debug_keymap.cpp` — twelve actions, each with a
config-file id, a label and a compiled-in default — and
`DebuggerWindow::apply_keymap()` is the single place that pushes a
`jnext::dbgkeys::Keymap` onto the `QAction`s. It also rewrites every toolbar
caption and tooltip that quotes a key, so `F5: Continue` is *generated* from
the binding rather than typed next to it; with the default map it produces
exactly the strings the toolbar carried before the mechanism existed.

Three properties of the layout are worth understanding before changing it.

**The model is Qt-free and lives in `jnext_debug`.** Not in `src/debugger/`,
because `src/gui/` has to read and write the same table with
`ENABLE_DEBUGGER=OFF`: `AppConfig` must round-trip `[debugger_keys]` in a
debugger-less build, or `PreferencesDialog::collect()` — which rebuilds an
`AppConfigData` from scratch — would wipe a user's bindings the moment they
pressed OK. And not in `src/gui/` either, because `ENABLE_QT_UI=OFF` with
`ENABLE_DEBUGGER=ON` is a real build-matrix combination in which `jnext_gui`
does not exist. The Qt conversions sit in the header-only
`src/debug/debug_keymap_qt.h`, which `jnext_debug` itself never compiles.

**The vocabulary is bounded.** `Key` is an enum of function keys, letters,
digits and sixteen named keys, not a mirror of `Qt::Key`. That is what makes
`parse_combo()` able to refuse a value *by name*, makes `render_combo()`
canonical, and makes the whole grammar testable without a widget.
`validate_combo()` then refuses the combinations that would break something
else: anything without Ctrl/Alt/Meta that is not `F1`–`F12` (Qt's shortcut map
outranks the focused panel, and the memory panel types hex with bare keys),
`Alt`+letter (the menu bar's namespace), and `Ctrl+C`/`Ctrl+A` (GH #21).

**Two claimants in one shortcut map is a conflict; two windows is not.** A
chord the *emulator* window binds — `Ctrl+F5`, `Ctrl+F6`, `F4`, `F11`, the set
`harvest_host_chords()` reads off the real `QAction`s — is accepted with a
warning rather than refused, because `Qt::WindowShortcut` matches against the
ACTIVE window and each window keeps its own binding. Measured both ways in
`debugger_keymap_test`'s DKH group; refusing the class would make `F11` = Step
Into impossible, which is the binding GH #1 asked for.

**Conflicts are refused, not resolved, wherever a human is present.** Qt
classifies two identical sequences as AMBIGUOUS and dispatches them
round-robin, so a clash breaks *both* bindings — the GH #124 defect this window
shipped five times. The Preferences tab therefore rejects a capture that
another action already holds. `build_keymap()` still has to resolve a
hand-edited file, and does so deterministically: explicit beats default,
earlier action beats later, and the loser is left unbound and reported. Every
refusal — unparseable, illegal, unknown id, conflict — becomes a `LoadIssue`
that `MainWindow` logs at error level and the tab lists in red. Nothing is
dropped quietly.

`MainWindow` keeps its own `debug_keys_`, the map that is *in effect*, seeded
after `app_config_.load()` and replaced by `apply_preferences()`. Both the
emulator window's forwarding of the five execution keys and the push into
`DebuggerWindow::set_keymap()` read that member, so the two windows cannot end
up on different maps. The forwarding covers exactly `run`, `pause`,
`step_into`, `step_over` and `step_out`; it matches modifiers exactly, and it
deliberately excludes `trace_toggle`, whose default `F2` is that window's
scale cycler.

## Panels

Thirteen panels, created by `DebuggerWindow::create_panels()`. What each one
introspects:

| Panel | Reads |
|---|---|
| CPU Registers | the Z80 register file, flags, IFF/IM, halt state, active ULA screen |
| MMU | the 8 slot→page map with RAM/ROM type, plus the 128K bank view |
| Disassembly | `src/debug/disasm.*` over `Mmu::read`, with symbol substitution, a breakpoint gutter, and a selection you can copy as assembly |
| Memory | raw bytes, either through the CPU's address space or a chosen MMU slot |
| Stack | words at and above `SP` |
| Call Stack | `src/debug/call_stack.*`, a shadow stack built from SP deltas |
| Watches | byte / word / long at user addresses |
| Breakpoints | the contents of `BreakpointSet` |
| Video | the raster position in all four counter domains plus the ULA fetch phase (`src/debug/raster_state.*`), and each layer rendered separately — composite, ULA primary and shadow, Layer 2 active and shadow, sprites, tilemap, and the NR 0x4A fallback colour |
| Sprites | all 128 sprite attribute slots |
| Copper | the decoded Copper program and its PC |
| NextREG | the whole 256-entry register file, editable |
| Audio | AY registers per chip, and the per-source mute mask |

For what these look like and how to drive them, see chapter 6 of the **user
guide**, *The debugger* — a UI reference written against the running product,
and not repeated here.

The Video panel's raster block is worth a note, because it is the one place a
user sees `hc`/`vc`, `hc_ula`/`vc_ula`, `cvc` and `phc` side by side. Every
number is labelled with the VHDL signal it mirrors, and `cvc` is labelled as
what NR 0x1E/0x1F report — reading that register as a raw frame line is GH #16,
and comparing a 28 MHz count against the 7 MHz `hc_ula` is GH #181. The
derivation lives in `src/debug/raster_state.*` and takes the live `VideoTiming`
by reference, so a machine-timing change moves the readout, the region
classification, the fetch schedule and the frame diagram together; there is
deliberately no second table of raster constants in `src/debugger/`.

Four panels (CPU, Disassembly, Stack, Call Stack) update only while paused.
That is a performance decision as much as a legibility one: reading the
register file every frame while the machine runs produces a blur, at real cost.

Everything else refreshes on `DebuggerManager`'s timer — with one exception.
The **two views of the breakpoint set** (the Breakpoints list and the
disassembly gutter) also **observe it**: `BreakpointSet::add_observer()` takes a
`std::function<void(BreakpointChange)>` — a plain callback, because `src/debug/`
is Qt-free — and every mutator calls it, so a breakpoint appears the instant it
is set rather than on the next tick. The two subscribers differ, deliberately:
the list acts on both change kinds, the gutter only on `PcBreakpoints`, since it
paints the PC half and nothing else. One-shot breakpoints notify nobody — they
are transient, are set on every resume, and no panel draws them. The enable
setters notify the half they change; the master switch notifies **both**, since
it changes every checkbox in the list and every dot in the gutter at once.

The point is where the notification comes *from*. A dozen call sites mutate that
set, and each one used to be responsible for repainting the views itself; twice
a site was added that did not, and the panels lied until the next tick. Emitting
from the mutator means a new call site cannot get it wrong.

## Symbols

`src/debug/symbol_table.*` is a bidirectional address↔name map with two
readers: `load_z88dk_map()` for z88dk linker output, and `load_simple_map()`
for a plain `SYMBOL = $ADDR` list. `DebuggerManager` owns the table, and the
disassembly, breakpoint and watch panels all consume it — which is why a
breakpoint set on a symbol keeps its name in the breakpoint list.

## Copying out of a custom-painted panel

The disassembly panel paints itself, so there is no Qt text widget to inherit a
selection from — it has to be built against the panel's own line model. Two
choices in `src/debugger/disasm_panel.*` are worth knowing before touching it.

The selection is an **address range**, an anchor and a cursor, not a pair of
line indices. `entries_` is rebuilt from scratch by every scroll, every
`refresh()` and every `activate_follow_pc()`, so an index into it survives none
of those while an address survives all of them. That one choice is why the
selection holds still while the view moves under it.

The text is produced by **`src/debug/disasm_text.*`** — pure C++, no Qt — which
re-disassembles the selected range from live memory rather than reusing the
painted lines. That is what lets a selection copy in full after its lines have
scrolled out of the view, and what makes copying work while the panel has
stopped updating. The same file holds `apply_symbols()`, and the painter calls
it too: the issue required the copied text to carry the MAP-file symbolic form,
and one shared rule is what stops the two from drifting. Being Qt-free, all of
it is assertable without a display — `debugger_disasm_copy_test` drives the
real panel with real events and reads the real clipboard.

## Rewind

Rewind lets you run the machine *backwards*: step back an instruction at a
time, jump back a whole frame, or drag a slider to somewhere earlier in the
session and carry on from there. For a Z80 developer that turns the usual
debugging move inside out — instead of guessing where to put a breakpoint,
re-running, and finding you have overshot again, you take the crash and walk
back from it to the instruction that caused it. It is off by default, because
it costs a full machine save every frame plus the memory the ring occupies;
`--rewind-buffer-size N` or the debugger's own toggle turns it on.

The implementation is not an undo log. It is a ring of **whole-machine
snapshots taken at frame boundaries**. `RewindBuffer` allocates one `mmap`
region of `max_frames × snapshot_bytes` up front, so pages fault in lazily
rather than being memset, and `Emulator::run_frame()` writes a slot at the top
of each frame, overwriting the oldest once the ring is full. Stepping back to a
point *inside* a frame means restoring that frame's snapshot and replaying
forward to the target instruction, which is why enabling rewind also
force-enables the trace log: `step_back()` needs the trace to know which cycle
the target instruction started at. The replay runs with audio and video
suppressed, so a long rewind neither screeches nor flickers.

The snapshot itself is produced by the same `save_state` / `load_state`
interface every subsystem implements, described in
[2.5 Save state and rewind](../02-architecture/05-save-state-and-rewind.md).
Two of its properties are load-bearing here:

- **The stream must be fixed-width.** Slot size is measured once at
  construction, by a dry run in measure mode. Any field that serialises a
  runtime length silently widens the stream, and from that moment every
  snapshot is dropped. That is not hypothetical — it shipped once, and a single
  `OUT (0xFF),A` was enough to trigger it. `rewind_buffer.h` carries the
  audited enumeration of every variable-length candidate and what was done
  about each.

- **A failed restore is loud.** A snapshot that does not round-trip is never
  published, a restore that fails verification returns a sentinel instead of
  reporting success, and the emulator latches `last_state_error()`. The UI then
  refuses to resume a machine known to be corrupt without explicit
  confirmation, via the `ResumeGuard` policy in `src/debug/resume_guard.h`.

## Magic breakpoint and magic port

These two are hooks a Z80 programmer puts in their *own* source, and neither
exists on real hardware. They solve the two problems that come up constantly
when the code under test is on the other side of the emulator: stopping at a
place you marked in your source rather than at an address you had to look up,
and getting a value out of a running program without opening a debugger at all
— a `printf` that lands on the host's terminal instead of on the screen the
program is busy drawing.

The **magic breakpoint** is an opcode that pauses the debugger where it
executes. jnext intercepts it in `src/cpu/z80_cpu.cpp` before the FUSE core
sees it, and recognises both community conventions: `ED FF`
(ZEsarUX/Spectaculator) and `DD 01` (CSpect). With `--magic-breakpoint` set,
`Emulator::init` installs an `on_magic_breakpoint` callback that activates and
pauses `DebugState`; the opcode then advances PC by two and costs 8 T-states.
With the flag unset the callback is null and both sequences fall straight
through to normal Z80 decoding — which is the point, because it means the hook
can be left in shipped source instead of being conditionally assembled out.

The **magic port** is not a CPU feature at all. It is an ordinary port handler
registered on a full 16-bit decode (`register_handler(0xFFFF, addr, …)`) whose
write side prints to `stderr` in one of four modes: `hex`, `dec`, `ascii` or
`line`, the last buffering until CR/LF so a whole string arrives as one line.
Reads are not intercepted. Being a normal registration, it obeys exactly the
same dispatch rules as everything else in
[3.6 Peripherals](06-peripherals.md).
