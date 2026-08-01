# 2.5 Save state, rewind and determinism

Rewinding needs the whole machine in a byte buffer. So does anything else that
wants to put the machine back where it was. JNEXT has one mechanism for that,
and it is a convention rather than an inheritance hierarchy.

## The serialisation convention

There is **no `Saveable` base class**. `src/core/saveable.h` — despite the name
— defines only two concrete helpers:

- `StateWriter` writes into a pre-allocated buffer, or, constructed with a null
  pointer, runs in *measure mode* and just counts the bytes it would write.
- `StateReader` reads from a const buffer.

Both are bounds-disciplined: a write past capacity is suppressed, a read past
the end is zero-filled, a sticky flag is latched (`overflow()` /
`out_of_bounds()`), and the position counter keeps advancing so diagnostics
still show the intended offset. No overrun can corrupt memory.

Each subsystem simply *has* the two methods:

```cpp
void save_state(StateWriter& w) const;
void load_state(StateReader& r);        // or bool, where it can fail
```

`Emulator::save_state()` (`emulator.cpp:9228`) calls them in a fixed order —
core, video, peripherals, audio, then its own private fields — and writes a
**sentinel** after every block: a `u32` of `kStateSentinelMagic ^ ordinal`,
with a shared counter that keeps both sides in lockstep. `load_state()` checks
the same sequence and, on the first mismatch, logs an error naming the
subsystem and returns false rather than feeding desynced bytes into everything
downstream.

A failed load leaves the machine **torn**, and says so: `last_state_error()`
holds the subsystem name until a reset re-establishes the machine cleanly, and
`state_error_generation()` is a monotonic counter so the GUI can tell a fresh
incident from one the user already acknowledged. Acknowledging does not clear
the breadcrumb, because acknowledging does not heal the desync.

A comment above `save_state()` lists what is deliberately *not* serialised: the
ROM buffer and boot ROM (loaded from disk, never change), the contention model
(rebuilt from config), the port dispatch table (lambdas, rewired by `init()`),
the mixer, the debugger's transient state, the trace and call stack, tape
position, recorders, the SD card, and the framebuffer.

## The rewind ring

`RewindBuffer` (`src/debug/rewind_buffer.h`) is a ring of fixed-width slots
backed by a single anonymous mapping (`anon_mem::alloc`, `src/core/anon_mem.h`),
so pages fault in lazily instead of eagerly zeroing what may be a gigabyte —
500 frames of a ~2.3 MB snapshot is over a gigabyte of address space. Slot size
is decided once, at construction, by running `save_state` in measure mode:

```cpp
StateWriter measure;  save_state(measure);
size_t snap_bytes = measure.position();
```

Snapshots are taken in `Emulator::begin_new_frame()` — at a frame boundary,
which is precisely why the scheduler queue does not need serialising: it is
empty there.

**This is why every field must be constant-width.** `take_snapshot()` refuses
to publish a slot whose `save_state` did not write exactly `snapshot_bytes`,
which is the correct behaviour but was silent for a long time: three fields
were written count-prefixed and variable-length, so a single `OUT (0xFF),A` or
one received UART byte widened the stream and every snapshot from then on was
quietly dropped. The header of `rewind_buffer.h` carries the audited list of
runtime-variable state and the rule that anything added to it must be
fixed-width.

Three entry points use the ring, and all of them funnel through `load_state()`:

| Call | What it does |
|---|---|
| `rewind_to_cycle(target)` | Restore the nearest snapshot at or before `target`, then fast-forward to the exact cycle in **replay mode**. |
| `step_back(n)` | Use the `TraceLog` to find the start cycle of the instruction *n* back, then `rewind_to_cycle` it. Fails loudly if the trace is off — which is why enabling rewind also enables the trace. |
| `rewind_to_frame(num)` | Restore a specific frame's snapshot. |

`replay_mode_` suppresses audio mixing and rendering during the fast-forward,
so a rewind of several seconds neither screeches nor flickers; the frame is
re-rendered once at the end. If the restore fails verification, the rewind is
aborted and the machine is *paused* rather than reported as successful.

Host-side state that shadows emulated state — the joystick and mouse
dispatchers, which live in the platform layer and are not `Emulator` members —
is re-seeded through the `on_input_state_restored` callback fired at the end of
every successful load. Without it the next live host event would stomp the
restore.

## Determinism

Automated runs have to produce the same bytes on a fast desktop and a loaded
CI runner. Four things make that true, and they are all in the emulator rather
than in the harness.

**Count frames, not seconds.** `--delayed-screenshot-frames`,
`--delayed-automatic-exit-frames`, `--delayed-keypress-frames` and
`--delayed-snapshot-frames` are counted in *emulated* frames, and override
their wall-clock seconds forms when both are given. `HeadlessApp::run()` even
converts a seconds-form keypress using the machine's actual refresh rate
(`video_timing().refresh_60hz() ? 60 : 50`) rather than assuming 50 Hz.

**Pin the clock.** `--rtc` sets `EmulatorConfig::rtc_fixed_tm`, which reaches
`I2cRtc::set_fixed_time()`; the DS1307 then answers from a frozen time instead
of the host clock, so anything that draws a date — the NextZXOS menu, most
obviously — is stable.

**Headless never reads the saved configuration.** `AppConfig` is loaded only
on the non-headless Qt path, so a developer's preferences cannot change what
an automated run does. CLI values win over saved ones everywhere else.

**A missing capture is an error, not a silent pass.** If a requested
`--delayed-screenshot` or `--delayed-snapshot` was never written — the exit
bound fired first, or the debugger stayed paused so no frame was rendered for
it — the process logs an error and exits non-zero.

One deliberate asymmetry: the frontend render hint that lets the Qt GUI skip
compositing frames nobody will see is never set headless, and is overridden
whenever recording or the debugger is active, so headless output is
bit-identical to a fully-rendered run. See
[4.3 The regression suite](../04-testing/03-the-regression-suite.md) for how
this is used in practice.
