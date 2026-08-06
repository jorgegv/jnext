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
call-stack tracker, symbol table, rewind ring buffer. It has **no Qt dependency
at all**. **`src/debugger/`** (target `jnext_debugger`) is the Qt 6 UI and
nothing else — panels, menus, the debugger window.

Three things fall out of that. The backend is testable without a GUI:
`rewind_test` and `resume_guard_test` link it with no Qt anywhere, while the
`debugger_*` suites are the ones that need a Qt build. The emulator can own
debug state permanently without dragging Qt into the core. And because the
backend is present in *every* build, `--magic-breakpoint` and `--trace` are
plain CLI flags rather than GUI-only features.

One caveat about "pure": `jnext_debug` does link SDL2, because
`rewind_buffer.cpp` includes `core/emulator.h`, which reaches `input/keyboard.h`
and thence `SDL.h`. The rule the split enforces is *no GUI toolkit*, not *no
dependencies*.

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
and it is what the per-instruction breakpoint test and the watchpoint checks in
`Mmu::read`/`Mmu::write` hang off. `persistent_` comes from
`--persistent-breakpoints` via `EmulatorConfig`, and is what lets breakpoints
survive closing the debugger window without switching the rest of that
machinery back on. The call-stack pre/post hooks sit behind their own
`enabled()` flag. The watchpoint checks are triple-gated — pointer non-null,
`armed()`, *and* `has_any_watchpoints()`.

`armed_` is a cached bool recomputed by the two setters rather than an
expression, so the default configuration executes exactly the load-and-branch
the single `active()` gate used to.

So "the debugger costs nothing when closed" is a claim about a predictable
branch, not about conditional compilation.

## Execution control

`DebugState` holds `paused_`, a `StepMode`, and a `BreakpointSet`. The run loop
consults it once per instruction, before the fetch:

| Mode | Set by | How it terminates |
|---|---|---|
| `NONE` + `paused_` | `pause()` | `run_frame()` returns immediately |
| PC breakpoint | `BreakpointSet::add_pc` | `should_break(pc)` matches |
| `INTO` | `step_into()` | loop pauses on the next iteration |
| `OVER` | `step_over(next_pc)` | one-shot breakpoint at `next_pc` |
| `OUT` | `step_out(sp)` | `check_step_out()` matches, after the instruction |
| `RUN_TO_CYCLE` | `run_to_cycle()` | master clock reaches the target |
| `STEP_BACK` / `RUN_BACK_TO_CYCLE` | `step_back()`, `run_back_to_cycle()` | handled before the loop starts, by rewinding |
| watchpoint | `add_watchpoint` | `Mmu` latches `data_bp_hit`; checked after the instruction |

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
memory the CPU itself read**. `Mmu::read()` is not inert: it fires read
watchpoints and latches the +3 floating bus. Reading the opcode speculatively
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

## Panels

Thirteen panels, created by `DebuggerWindow::create_panels()`. What each one
introspects:

| Panel | Reads |
|---|---|
| CPU Registers | the Z80 register file, flags, IFF/IM, halt state, active ULA screen |
| MMU | the 8 slot→page map with RAM/ROM type, plus the 128K bank view |
| Disassembly | `src/debug/disasm.*` over `Mmu::read`, with symbol substitution and a breakpoint gutter |
| Memory | raw bytes, either through the CPU's address space or a chosen MMU slot |
| Stack | words at and above `SP` |
| Call Stack | `src/debug/call_stack.*`, a shadow stack built from SP deltas |
| Watches | byte / word / long at user addresses |
| Breakpoints | the contents of `BreakpointSet` |
| Video | each layer rendered separately — composite, ULA primary and shadow, Layer 2 active and shadow, sprites, tilemap, and the NR 0x4A fallback colour |
| Sprites | all 128 sprite attribute slots |
| Copper | the decoded Copper program and its PC |
| NextREG | the whole 256-entry register file, editable |
| Audio | AY registers per chip, and the per-source mute mask |

For what these look like and how to drive them, see chapter 6 of the **user
guide**, *The debugger* — a UI reference written against the running product,
and not repeated here.

Four panels (CPU, Disassembly, Stack, Call Stack) update only while paused.
That is a performance decision as much as a legibility one: reading the
register file every frame while the machine runs produces a blur, at real cost.

## Symbols

`src/debug/symbol_table.*` is a bidirectional address↔name map with two
readers: `load_z88dk_map()` for z88dk linker output, and `load_simple_map()`
for a plain `SYMBOL = $ADDR` list. `DebuggerManager` owns the table, and the
disassembly, breakpoint and watch panels all consume it — which is why a
breakpoint set on a symbol keeps its name in the breakpoint list.

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
