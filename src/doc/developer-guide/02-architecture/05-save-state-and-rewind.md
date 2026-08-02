# 2.5 Save state, rewind and determinism

## What rewind is

JNEXT can run the emulated machine **backwards**. In the debugger you can press
Shift+F7 to undo the instruction that just executed, Shift+F6 to jump to the
start of the previous frame, or drag a slider to any frame still held in the
buffer — and the entire machine goes back to exactly how it was at that point.
Not just the registers: memory, the MMU mapping, the video registers, the
peripherals, everything. You land there paused, with the emulator window
redrawn to match, and F5 carries on from there. For debugging a crash this
inverts the usual workflow, because you can step *away* from the wreckage
towards its cause instead of restarting and trying to catch the moment on the
way past.

It is opt-in, via `--rewind-buffer-size N` on the command line or
**Debug ▸ Rewind ▸ Enable Rewind** in the debugger, because it works by
snapshotting the whole machine at every frame boundary and those snapshots are
large. The user-facing description lives in the user guide, under
*Debugger ▸ Backward execution (rewind)*.

That is the feature the rest of this page implements. Everything hangs off one
requirement: the ability to put the entire machine into a byte buffer and get
it back out again. Save states, snapshots and rewind all use the same
mechanism, and that mechanism is a convention rather than an inheritance
hierarchy.

## The serialisation convention

There is **no `Saveable` base class**. `src/core/saveable.h`, despite the name,
defines only two concrete helpers:

- `StateWriter` writes into a pre-allocated buffer. Constructed with a null
  pointer it runs in *measure mode* instead, writing nothing and simply
  counting the bytes it would have written.
- `StateReader` reads back from a const buffer.

Both are bounds-disciplined by construction. A write past capacity is
suppressed, a read past the end is zero-filled, and either latches a sticky
flag — `overflow()` and `out_of_bounds()` respectively. The position counter
keeps advancing in both cases, so a diagnostic still reports the offset the
code *intended*, which is the number you need to find the culprit. No overrun
can corrupt memory.

Each subsystem then simply *has* the two methods:

```cpp
void save_state(StateWriter& w) const;
void load_state(StateReader& r);        // or bool, where it can fail
```

`Emulator::save_state()` (`emulator.cpp:9228`) calls them in a fixed order —
core, video, peripherals, audio, then its own private fields — and writes a
**sentinel** after every block: a `u32` holding `kStateSentinelMagic ^
ordinal`, with a shared counter keeping the two sides in lockstep.
`load_state()` verifies the same sequence, and on the first mismatch it logs an
error naming the subsystem that went wrong and returns false, rather than
feeding desynchronised bytes into everything downstream and producing a machine
that is subtly wrong in an unrelated place.

A failed load leaves the machine **torn**, and JNEXT says so rather than
pretending otherwise. `last_state_error()` holds the name of the offending
subsystem until a reset re-establishes the machine cleanly, and
`state_error_generation()` is a monotonic counter so the GUI can distinguish a
fresh incident from one the user has already acknowledged. Acknowledging does
not clear the breadcrumb, for the obvious reason that acknowledging does not
heal the desynchronisation.

A comment above `save_state()` lists what is deliberately *not* serialised, and
the omissions all have reasons: the ROM buffer and boot ROM are loaded from
disk and never change; the contention model is rebuilt from config; the port
dispatch table is a set of lambdas that `init()` rewires anyway; and the mixer,
the debugger's transient state, the trace and call stack, tape position, the
recorders, the SD card and the framebuffer are either host-side or
reconstructible.

## The rewind ring

`RewindBuffer` (`src/debug/rewind_buffer.h`) is a ring of fixed-width slots
backed by a single anonymous mapping, obtained from `anon_mem::alloc` in
`src/core/anon_mem.h`. The mapping matters: 500 frames of a snapshot around
2.3 MB is over a gigabyte of address space, and an anonymous mapping lets those
pages fault in lazily as they are first used instead of being eagerly zeroed up
front. The slot size is decided once, at construction, by running `save_state`
in measure mode:

```cpp
StateWriter measure;  save_state(measure);
size_t snap_bytes = measure.position();
```

Snapshots are taken from `Emulator::begin_new_frame()`, which is to say at a
frame boundary — and that is precisely why the scheduler queue never has to be
serialised, because it is empty at that instant.

**This is also why every serialised field must be constant-width.**
`take_snapshot()` refuses to publish a slot whose `save_state` did not write
exactly `snapshot_bytes`. That refusal is the correct behaviour, but it was
silent for a long time, and the consequence was ugly: three fields were being
written count-prefixed and variable-length, so a single `OUT (0xFF),A` or one
byte arriving on the UART widened the stream, and from that point on every
snapshot was quietly discarded — rewind simply stopped recording, with no
error. The header of `rewind_buffer.h` now carries the audited list of
runtime-variable state along with the rule that anything added to it must be
fixed-width.

Three entry points use the ring, and all of them funnel through `load_state()`:

| Call | What it does |
|---|---|
| `rewind_to_cycle(target)` | Restore the nearest snapshot at or before `target`, then fast-forward to the exact cycle in **replay mode**. |
| `step_back(n)` | Use the `TraceLog` to find the start cycle of the instruction *n* back, then `rewind_to_cycle` it. Fails loudly if the trace is off — which is why enabling rewind also enables the trace. |
| `rewind_to_frame(num)` | Restore a specific frame's snapshot. |

`replay_mode_` suppresses audio mixing and rendering throughout the
fast-forward, so rewinding several seconds neither screeches through the
speakers nor flickers through hundreds of frames; the picture is rendered once,
at the end. And if the restore fails verification, the rewind is aborted and
the machine is left *paused* rather than reported as a success.

One category of state needs special handling: host-side state that shadows
emulated state. The joystick and mouse dispatchers live in the platform layer
and are not `Emulator` members, so a restore cannot reach them directly.
Instead they are re-seeded through the `on_input_state_restored` callback,
fired at the end of every successful load. Without it, the next live host input
event would stomp on the restored machine.

## Determinism

An automated run has to produce the same bytes on a fast desktop and on a
loaded CI runner. Four things make that true, and it is worth noting that all
four live in the emulator rather than in the test harness — a harness cannot
make a non-deterministic emulator reproducible.

**Count frames, not seconds.** `--delayed-screenshot-frames`,
`--delayed-automatic-exit-frames`, `--delayed-keypress-frames` and
`--delayed-snapshot-frames` are all counted in *emulated* frames, and they
override their wall-clock counterparts when both are given. `HeadlessApp::run()`
goes one step further and converts a seconds-form keypress using the machine's
actual refresh rate, `video_timing().refresh_60hz() ? 60 : 50`, rather than
assuming 50 Hz.

**Pin the clock.** `--rtc` sets `EmulatorConfig::rtc_fixed_tm`, which reaches
`I2cRtc::set_fixed_time()`. The emulated DS1307 then answers from a frozen time
instead of the host clock, so anything that draws a date — the NextZXOS menu
being the obvious case — comes out the same on every run.

**Headless never reads the saved configuration.** `AppConfig` is loaded only on
the non-headless Qt path, so a developer's saved preferences cannot change what
an automated run does. Everywhere else, CLI values win over saved ones.

**A missing capture is an error, not a silent pass.** If a requested
`--delayed-screenshot` or `--delayed-snapshot` was never actually written —
because the exit bound fired first, or the debugger stayed paused so no frame
was ever rendered for it — the process logs an error and exits non-zero. A test
that produces no output fails instead of passing.

There is one deliberate asymmetry to be aware of. The frontend render hint that
lets the Qt GUI skip compositing frames nobody will see is never set in
headless mode, and is overridden whenever recording or the debugger is active,
so headless output stays bit-identical to a fully-rendered run. See
[4.3 The regression suite](../04-testing/03-the-regression-suite.md) for how
all of this is used in practice.
