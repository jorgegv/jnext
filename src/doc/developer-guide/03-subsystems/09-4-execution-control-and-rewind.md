# 3.9.4 Execution control and rewind

Below the facade, execution control is `DebugState` (`src/debug/debug_state.*`),
an ordinary member of the `Emulator` in every build. The run loop consults it
once per instruction, before the fetch, and that single consultation point is
what makes stepping exact and breakpoints cheap. This page describes the gates,
the step modes, the legacy breakpoint store the hot loop still obeys, rewind,
and the two hooks a Z80 programmer puts in their own source: the magic
breakpoint and the magic port.

## The gates

`DebugState` caches every hot-path question as a bool, recomputed by
`refresh_gates_()` from the setters and never from the hot path:

| Bit | Is | Gates |
|---|---|---|
| `armed()` | `clients_attached_ \|\| persistent_ \|\| replay_armed_ \|\| magic_hold_` | the per-instruction breakpoint and event tests, the boundary drain |
| `attached()` | an arming client is attached | the step machinery: `OUT`, `STEP_BACK`, `RUN_BACK_TO_CYCLE` |
| `raster_live()` | some client asked for the live raster | the render-every-frame hint and `video_timing_.advance()` |
| `watchpoints_live()` | `armed()` and `guest_access_` | every memory and port watch site |

[3.9.3](09-3-sessions-the-pump-and-reconstruct.md) describes where the arm bits
come from. Unarmed, the default configuration executes one load and one branch
per instruction, so "the debugger costs nothing when closed" is a claim about a
predictable branch, not about conditional compilation.

### A debugger read is not a guest access

`Mmu::read()` is not only the CPU's read. Before the guest-access gate, a READ
watchpoint on any address a panel happened to display was latched by the
panel's own refresh, at roughly 4 Hz while the machine ran, and the next Run or
Step stopped one instruction later at an unrelated address. The snapshot savers
did the same: all three sweep RAM through a temporary slot-7 window at `$E000`,
and the 48K SNA saver also pushes PC at `SP-2`.

So `guest_access_` is false unless the emulator has declared it is executing,
with the RAII `DebugState::GuestExecutionScope`. Exactly three functions take it:
`Emulator::run_frame()`, `step_frame_slot()` and `execute_single_instruction()`.
The watch sites test the pointer, `watchpoints_live()`, and the per-slot mask
byte (`rd_watch_armed()` / `wr_watch_armed()`). A caller added tomorrow cannot
fire a watchpoint whatever it calls, because it cannot make `guest_access_` true.
The gate is in `Mmu`, not around panel refreshes, so it does not depend on the
caller.

Its counterpart `DebugState::InspectionScope` covers the debugger readers that
run *inside* execution. The trace log captures four opcode bytes at PC and the
call-stack tracker three, whatever the instruction's real length, so both read
bytes the CPU never fetches. An event delivery, `Mmu::peek()` and
`poke(MemSpace::cpu())` take the same scope.

`DebugState::SuspendScope` clears every arm bit for its lifetime. A save that
has to advance to a frame boundary runs under it, so the advance does not stop
on a breakpoint.

The hot path pays nothing for any of this: `Mmu::read` and `Mmu::write` compile
to the same instructions they did without it, every difference being a
structure offset.

## The step modes

| Mode | Set by | Ends |
|---|---|---|
| `NONE` and paused | `pause()` | `run_frame()` returns at once |
| `INTO` | `step_into()` | the loop pauses on the next iteration |
| `OVER` | `step_over_subscribed()`, with a transient `Execute` subscription at the next instruction | that subscription's stop |
| `OUT` | `step_out(sp)` | `check_step_out()` matches, after the instruction |
| `RUN_TO_CYCLE` | `run_to_cycle()` | the master clock reaches the target |
| `STEP_BACK`, `RUN_BACK_TO_CYCLE` | `step_back()`, `run_back_to_cycle()` | handled before the loop starts, by rewinding |

Run to Here is the same transient `Execute` subscription at a chosen address,
through `run_to_subscribed()`. Run to End of Frame and Run to End of Scanline
are `run_to_cycle()` with a computed target.

Transients are unlimited in number, exempt from the master switch and removed at
the next stop the backend causes, or by an explicit `pause()`. `run()`
deliberately does not drop them: a continue that cleared what the same
operation had just armed could not work.

**The step-off.** Every way out of paused goes through `DebugState::unpause_()`,
which arms a one-instruction step-off. The loop consumes it
(`consume_step_off()`) immediately before the breakpoint test, and it
suppresses only the first test of a resumed run. The breakpoint test sits
before the fetch, so without it a resume with PC still on a breakpoint would
re-match the same address and pause again having executed nothing (GH #221).
The `Execute` gate consumes the same arm. `unpause_()` raises it only on a real
paused-to-running edge, so the PC the gate sees is the PC the resume was issued
at. `refresh_gates_()` drops the arm when the machine is disarmed, because the
consumer stops running while PC does not.

**Step Out** is the one mode decided after the instruction rather than before.
`step_out()` records SP. `check_step_out()`, called from the shared
per-instruction body, ends the step when three things hold:

- the instruction popped exactly one return address (`sp_after == sp_before + 2`,
  which tells a taken `RET cc` from an untaken one);
- its opcode was a return form (`RET`, `RET cc`, `RETI`/`RETN` including the
  undocumented `ED` aliases);
- the pop unwound the stack strictly past the armed SP.

The last condition is what makes a nested call's own `RET`, or an interrupt
handler's `RETI`, return into the routine being stepped out of rather than end
the step. It is called from `step_one_instruction()`, not from `run_frame()`'s
loop, so free-running and single-stepping cannot disagree.

Its position is load-bearing: a debugger hook **may only read memory the CPU
itself read**. `Z80Cpu::execute()` has three early returns that complete a step
without fetching at PC: an accepted NMI, an accepted INT and the esxDOS shim.
So the CPU is asked: `fetched_opcode_last_execute()` is false through all three
and true once the fetch happened, and the read is gated on it. Deducing it from
SP does not work: the esxDOS shim fakes a return's own `+2`. The decision also
sits before the deferred RETN overlay clear, because a `RETN` leaving a
DivMMC-mapped routine unmaps it there.

**A step drives the frame loop too.** `Emulator::execute_single_instruction()`
is the raw one-slot primitive, sharing `step_one_instruction()` with the
free-running loop. The backend's `step_into()` runs `Emulator::debugger_step()`
instead. While the debugger holds the machine the frontends stop calling
`run_frame()`, so a step is the machine's only driver. `step_frame_slot()`
begins a frame when none is in flight and calls the shared `end_of_frame()` when
the clock reaches the frame's last cycle. Without it the ULA frame interrupt
would stop being scheduled and a halted CPU could never be woken (GH #207). A
step at a `HALT` therefore runs the halt out, bounded to two frames.

Two consequences follow. A step that ends on a watchpoint consumes the
data-breakpoint latch, as `run_frame()` does. And Step and Step Back are not
inverses across a halt: one Step can execute thousands of internal NOP slots,
enough to wrap the trace buffer, while Step Back still undoes raw instructions.
The primitive keeps its frame-agnostic behaviour deliberately, because many
suites advance a machine whose frames they drive themselves.

## The legacy breakpoint store

`BreakpointSet` (`src/debug/breakpoints.*`) is the core's own PC-breakpoint and
watchpoint store. The hot loop still obeys it (`should_break()` before the
`Execute` gate) and the core suites still drive it, but no frontend writes it:
every frontend's breakpoints are backend subscriptions.

It keeps two things. The **model** (`pc_all_`, `wp_all_`) holds every breakpoint
with its own enabled flag. The **live cache** (`pc_live_`, `wp_live_`) holds
only what can fire, and is what `has_pc()`, `has_watchpoint()` and
`has_io_watchpoint()` read. `rebuild_live_()` recomputes the cache from the
mutators only. The master switch (`set_master_enabled()`) is the whole of its
first line: off, the cache is left empty and the model is not read, so it cannot
consume a per-breakpoint flag and an off/on round trip is exact by construction.
The backend mirrors its own master switch into this one.

## Rewind

Rewind runs the machine backwards: step back an instruction, jump back a frame,
or go back to any earlier point the ring still covers and carry on from there.
It is off by default, because it costs a full machine save every frame plus the
ring's memory; `--rewind-buffer-size N` or the debugger's toggle turns it on.

It is not an undo log. It is a ring of whole-machine snapshots taken at frame
boundaries. `RewindBuffer` (`src/debug/rewind_buffer.*`) allocates one `mmap`
region of `max_frames × snapshot_bytes` up front, so pages fault in lazily, and
`Emulator::run_frame()` writes a slot at the top of each frame. Stepping back to
a point inside a frame restores that frame's snapshot and replays forward to the
target instruction. That is why turning rewind on also turns on the trace log:
`step_back()` needs it to know which cycle the target instruction started at.
The replay runs with audio and video suppressed, under
`DebugState::ReplayArmScope`, and delivers no events.

The snapshot is the `save_state` / `load_state` interface every subsystem
implements, described in [2.5 Save state and rewind](../02-architecture/05-save-state-and-rewind.md).
Two of its properties are load-bearing here:

- **The stream must be fixed-width.** The slot size is measured once, by a dry
  run. A field that serialises a runtime length widens the stream, and from then
  on every snapshot is dropped. `rewind_buffer.h` lists every variable-length
  candidate and what was done about each.
- **A failed restore is loud.** A snapshot that does not round-trip is never
  published, a restore that fails verification returns a sentinel instead of
  success, and the emulator latches `last_state_error()`. Every resume is then
  refused with `RefusedCorrupt` until the incident is acknowledged
  (`acknowledge_corruption()`): the `ResumeGuard` policy in
  `src/debug/resume_guard.h`. The Qt debugger asks the question in a modal.

Rewind is refused with `RefusedRzx` while an RZX records or plays, and with
`RefusedUnavailable` when the ring is empty or the target frame is outside it.

## The magic breakpoint and the magic port

Both are hooks a Z80 programmer puts in their own source; neither exists on
real hardware.

The **magic breakpoint** is an opcode that stops the machine where it executes.
JNEXT intercepts it in `src/cpu/z80_cpu.cpp` before the FUSE core sees it, and
recognises both community conventions: `ED FF` (ZEsarUX, Spectaculator) and
`DD 01` (CSpect). With `--magic-breakpoint` set (or `set_magic_breakpoint(true)`),
`Emulator::init` installs an `on_magic_breakpoint` callback that pauses and
holds the stop (`hold_for_magic_stop()`); the opcode advances PC by two and
costs 8 T-states. The hold exists because the hook fires on a machine nothing
may arm — headless, SDL, a closed debugger window — and the hot loop honours a
pause only inside its armed block. It arms nothing else, and the resume that
ends the stop releases it. The stop's reason is `Magic`, which no client owns
and no detach releases. With the switch off the callback is null and both
sequences decode as ordinary Z80, so the hook can stay in shipped source.

The **magic port** is not a CPU feature. It is an ordinary port handler
registered on a full 16-bit decode (`register_handler(0xFFFF, addr, …)`) whose
write side prints to `stderr` in one of four modes: `hex`, `dec`, `ascii` or
`line`, the last buffering until CR or LF. Reads are not intercepted. It obeys
the same dispatch rules as every other port ([3.6 Peripherals](06-peripherals.md)).

## Tests

`step_out_test`, `resume_step_off_test`, `bp_enable_test`, `io_watchpoint_test`,
`persistent_bp_test`, `resume_guard_test` and `rewind_test` pin the primitives
on this page without a Qt build. `debugger_backend_test`'s `CTL-*` rows drive
the same paths through the facade.
