# 3.9 Debug and the debugger

The debugger is split across two directories, and the split is the design.

**`src/debug/`** (target `jnext_debug`) is the backend: disassembler, breakpoint
and watchpoint sets, execution-control state, instruction trace log, call-stack
tracker, symbol table, rewind ring buffer. It has **no Qt dependency at all**.
**`src/debugger/`** (target `jnext_debugger`) is the Qt 6 UI and nothing else —
panels, menus, the debugger window.

Three things fall out of that. The backend is testable without a GUI —
`rewind_test` and `resume_guard_test` link it with no Qt anywhere, while the
`debugger_*` suites are the ones that need a Qt build. The emulator can own
debug state permanently without dragging Qt into the core. And the backend is
present in *every* build, which is why `--magic-breakpoint` and `--trace` are
plain CLI flags rather than GUI-only features.

One caveat about "pure": `jnext_debug` does link SDL2, because
`rewind_buffer.cpp` includes `core/emulator.h`, which reaches `input/keyboard.h`
and thence `SDL.h`. The rule the split enforces is *no GUI toolkit*, not *no
dependencies*.

## What `ENABLE_DEBUGGER=OFF` removes

`ENABLE_DEBUGGER` (default `ON`) gates **only the Qt UI**. When it is off,
`jnext_debugger` is not compiled or linked and every use site in `src/gui/` is
inside an `#ifdef`. `jnext_debug` is linked unconditionally, and
`Emulator::debug_state_` is an ordinary member either way.

That is deliberate: the hot loop's cost is not "is the debugger compiled in" but
"is it *active*". `DebugState::active_` starts false and is only set true when
the UI enables the debugger or a magic breakpoint fires. Every debug check in
the run loop hangs off that one boolean — the per-instruction breakpoint test,
the call-stack pre/post hooks (behind their own `enabled()` flag), and
`video_timing_.advance()`, which maintains raster counters nothing but a human
inspector reads. The watchpoint checks in `Mmu::read`/`write` are triple-gated:
pointer non-null, `active()`, *and* `has_any_watchpoints()`.

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
| `RUN_TO_CYCLE` | `run_to_cycle()` | master clock reaches the target |
| `STEP_BACK` / `RUN_BACK_TO_CYCLE` | `step_back()`, `run_back_to_cycle()` | handled before the loop starts, by rewinding |
| watchpoint | `add_watchpoint` | `Mmu` latches `data_bp_hit`; checked after the instruction |

Step Over is not a special CPU mode. `DebuggerManager::on_step_over()` asks the
disassembler whether the current instruction `is_call_like()` — `CALL nn`,
`CALL cc,nn`, `RST n`, `DJNZ` — and if so sets a one-shot breakpoint at
`PC + instruction_length()` and resumes; otherwise it degrades to Step Into.
Run to Cursor is the same one-shot mechanism with a user-chosen address, and
Run to End of Frame / End of Scanline are `run_to_cycle()` with a computed
target.

**`StepMode::OUT` is set by `step_out()` but the run loop never reads it, and
`DebugState::check_step_out()` — the predicate that would detect the returning
`RET`/`RETI`/`RETN` at the right stack depth — has no caller anywhere in the
tree.** Step Out therefore resumes and stops only at a real breakpoint or a
manual break.

Single-stepping goes through `Emulator::execute_single_instruction()`, which
shares its per-instruction body verbatim with the free-running loop
(`step_one_instruction()`), specifically so the two paths cannot drift. Stepping
must *observe* the emulation, never alter it — see
[2.2 The emulator core](../02-architecture/02-the-emulator-core.md) for why
`run_frame()` refuses to re-begin a frame that is already in progress.

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
not repeated here.

Four panels (CPU, Disassembly, Stack, Call Stack) only update while paused.
That is a performance decision: reading the register file every frame while the
machine runs produces a blur at real cost.

## Symbols

`src/debug/symbol_table.*` is a bidirectional address↔name map with two
readers: `load_z88dk_map()` for z88dk linker output and `load_simple_map()` for
a plain `SYMBOL = $ADDR` list. `DebuggerManager` owns the table; the
disassembly, breakpoint and watch panels consume it, which is why a symbolic
breakpoint keeps its name in the list.

## Rewind

Backward execution is a ring of **whole-machine snapshots taken at frame
boundaries**, not an undo log. `RewindBuffer` allocates one `mmap` region of
`max_frames × snapshot_bytes` up front — pages fault in lazily rather than being
memset — and `Emulator::run_frame()` writes a slot at the top of each frame,
overwriting the oldest when full.

The snapshot is produced by the same `save_state`/`load_state` interface every
subsystem implements; see
[2.5 Save state and rewind](../02-architecture/05-save-state-and-rewind.md).
Two properties are load-bearing here:

- **The stream must be fixed-width.** The slot size is measured once at
  construction by a dry run in measure mode, so any field that serialises a
  runtime length silently widens the stream and every snapshot from then on is
  dropped. That is not hypothetical — it shipped once, and a single
  `OUT (0xFF),A` triggered it. `rewind_buffer.h` carries the audited enumeration
  of every variable-length candidate and what was done about each.
- **A failed restore is loud.** A snapshot that does not round-trip is not
  published, a restore failing verification returns a sentinel rather than
  reporting success, and the emulator latches `last_state_error()`. The UI then
  refuses to resume a corrupt machine without explicit confirmation, via the
  `ResumeGuard` policy in `src/debug/resume_guard.h`.

Rewind is off unless `--rewind-buffer-size N` is given or the debugger enables
it, because it costs a full save every frame plus the ring memory. Enabling it
also force-enables the trace log: `step_back()` needs it to find the target
instruction's cycle inside the restored frame, then replays forward to it with
audio and video suppressed.

## Magic breakpoint and magic port

These are the two hooks a Z80 programmer puts in their *own* code, and neither
exists on real hardware.

The **magic breakpoint** is intercepted in `src/cpu/z80_cpu.cpp` before the FUSE
core sees the opcode: `ED FF` (the ZEsarUX/Spectaculator convention) and `DD 01`
(CSpect's). When `--magic-breakpoint` is set, `Emulator::init` installs an
`on_magic_breakpoint` callback that activates and pauses `DebugState`; the
opcode then advances PC by two and costs 8 T-states. With the flag unset the
callback is null and both sequences fall straight through to normal Z80
decoding, so they can be left in shipped source.

The **magic port** is not a CPU feature at all — it is an ordinary port handler
registered on a full 16-bit decode (`register_handler(0xFFFF, addr, …)`) whose
write side prints to `stderr` in one of four modes (`hex`, `dec`, `ascii`,
`line`, the last buffering until CR/LF). Reads are not intercepted. Being a
normal registration, it obeys the same dispatch rules as everything else in
[3.6 Peripherals](06-peripherals.md).
