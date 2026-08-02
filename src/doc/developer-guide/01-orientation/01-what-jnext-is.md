# 1.1 What JNEXT is, in one page

JNEXT is a ZX Spectrum Next emulator written in C++17. Its specification is not
prose but a circuit description: the VHDL source of the official ZX Spectrum
Next FPGA core. Where hardware behaviour is ambiguous or undocumented, the
answer comes from that source, and is pinned there by a test citing the file
and the line it came from.

## What it emulates

The machine set is small and explicit. `MachineType` in
`src/memory/contention.h:5` has exactly four values — `ZXN_ISSUE2`, `ZX48K`,
`ZX128K` and `ZX_PLUS3` — and those are the four things `--machine` accepts
(`next`, `48k`, `128k`, `plus3`).

Pentagon is deliberately not among them, and the reason is worth understanding
because it explains why there are two enums where you might expect one. On real
Next hardware a guest can ask for a different *video timing* without becoming a
different machine, by writing NextREG 0x03. JNEXT models that faithfully:
Pentagon survives only as `MachineTimingMode::TimingPentagon`, one of the
timing modes selectable at run time alongside the 48K, 128K and +3 timings,
while the machine type is fixed at startup. The two axes are independent once
the emulator is running, so they are separate types.

Per-machine geometry lives in `MachineTiming` (`src/core/emulator_config.h:259`)
and is derived from `zxula_timing.vhd`: a 48K frame is 448 pixels × 312 lines,
while 128K, +3 and Next frames are 456 × 311. Underneath all of it, everything
is counted in 28 MHz master cycles (`MASTER_CLOCK_HZ`) — the same master clock
the FPGA uses — and `src/core/clock.h` derives the 7 MHz pixel clock and the
four selectable CPU speeds from it by division.

## The accuracy model, as implemented

| Layer | Granularity in the code |
|---|---|
| CPU | Per instruction. The FUSE Z80 core (`third_party/fuse-z80/`) executes one instruction; Z80N opcodes are intercepted before FUSE sees them. |
| Memory contention | Per bus cycle. FUSE's read/write callbacks call `ContentionModel::contention_tick()`, wired by `z80_set_contention_runtime()` (`src/cpu/z80_cpu.h`). |
| Copper | Per 28 MHz cycle, across the window each instruction consumed — `Emulator::tick_copper_for_master_cycles()`. |
| Interrupts | Scheduled into a min-heap by master cycle (`src/core/scheduler.h`), drained after every instruction. |
| Audio | Integrated over each instruction's cycle span, not point-sampled — `Emulator::advance_audio()` into `Mixer`. |
| Video | **Composited once, at the end of the frame.** |

Two rows there deserve unpacking before you read any of the code.

The **Copper** is a Next-specific piece of hardware that a developer coming
from other 8-bit machines will not have met. It is a tiny co-processor with its
own 1 K instruction RAM and exactly two instructions: `WAIT` until the raster
reaches a given screen position, and `MOVE` a value into a hardware register.
Because its writes bypass the CPU entirely, a guest can repaint the palette or
flip a layer at an exact point on an exact scanline without spending any Z80
time on it — which is how a great many Next demo effects are built. That is why
it gets its own row above, and why that row is finer-grained than "per
instruction": JNEXT advances it across every 28 MHz cycle the instruction
consumed, rather than once when the instruction ends.

**Video is composited once, at the end of the frame**, and that is the row
nobody expects. It shapes a large part of the codebase. `Emulator::run_frame()`
does not draw as it goes: it schedules one `SCANLINE` event per line, but those
callbacks only *snapshot and tag* display state (`Emulator::on_scanline`). The
picture itself is produced afterwards, in a single pass by
`Renderer::render_frame()` walking rows 0..255 once the CPU has finished the
frame.

That creates an obvious problem, and its solution is the single most
load-bearing idea in the video code. By the time the renderer runs, the live
hardware registers hold the frame's *last* values — so a demo that changed the
palette halfway down the screen would have its change applied to the whole
frame. To avoid that, every register that guests are known to change mid-frame
carries a per-scanline change log, which the renderer rewinds and replays row by
row as it descends the screen. [2.4 The video pipeline](../02-architecture/04-the-video-pipeline.md)
covers the mechanism in full.

Extreme cycle-exactness is deliberately not a goal; "good enough to develop
games on" is. The consequences of that choice are documented rather than
hidden. The main one follows directly from the change log above: a register
write that happens part-way through a line is recorded against that whole line,
so an effect can appear at most one row early, and never late.

## What a "frame" is

One call to `Emulator::run_frame()` advances the master clock by
`timing_.master_cycles_per_frame` and leaves a complete 640 × 256 ARGB8888
image in the framebuffer, which the frontend collects with `get_framebuffer()`.
Real time per frame follows the *emulated* refresh rate rather than a hardcoded
50 Hz — `frame_period_ms()` computes it from the machine's own frame length in
master cycles, so a guest that has switched to 60 Hz paces at 60 fps.

A frame is not necessarily one call, though. The debugger pauses by returning
from inside the inner loop, part-way through a frame, so `frame_in_progress_`
records that the next call must resume the half-executed frame rather than
start a new one. Restarting would rewind the Copper and wipe the change logs of
a frame that is still in flight — which is exactly the state a developer is
looking at when they hit a breakpoint mid-screen.

## The three frontends

All three drive the same `Emulator` and differ only in what surrounds it.

- **Qt GUI** — `src/gui/qt_app.*` plus `MainWindow`, built when
  `ENABLE_QT_UI=ON`. Qt owns the window, the menus and keyboard input; SDL is
  kept only for audio output. The debugger (`src/debugger/`) hangs off this
  frontend.
- **SDL-only** — `src/platform/sdl_app.*`, the fallback when `ENABLE_QT_UI=OFF`.
  Window, input and audio all go through SDL2.
- **Headless** — `src/platform/headless_app.cpp`, selected at run time by
  `--headless` in either build. It opens no window, no audio device and no host
  input, runs uncapped instead of paced to real time, and schedules everything
  it does — loading a file, injecting a keypress, taking a screenshot, exiting
  — off frame counters rather than the wall clock. That is what makes a
  headless run repeatable on a fast host and a slow one alike, and it is the
  mode the regression suite runs in.

See [2.1 Startup and the frontends](../02-architecture/01-startup-and-the-frontends.md)
for how one of the three is chosen.
