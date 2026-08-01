# 1.1 What JNEXT is, in one page

JNEXT is a ZX Spectrum Next emulator written in C++17. Its specification is
not a document — it is the VHDL source of the official ZX Spectrum Next FPGA
core. Where hardware behaviour is ambiguous or undocumented, the answer comes
from that source and is pinned there by a test citing the file and line.

## What it emulates

The machine set is small and explicit. `MachineType` in
`src/memory/contention.h:5` has exactly four values —
`ZXN_ISSUE2`, `ZX48K`, `ZX128K`, `ZX_PLUS3` — which are the four things
`--machine` accepts (`next`, `48k`, `128k`, `plus3`). Pentagon is *not* one of
them: it survives only as `MachineTimingMode::TimingPentagon`, a video-timing
mode a guest can select through NextREG 0x03, alongside the 48K, 128K and +3
timings. The two axes are independent after startup, which is why they are
separate enums.

Per-machine geometry lives in `MachineTiming` (`src/core/emulator_config.h:259`),
derived from `zxula_timing.vhd`: 48K is 448 pixels × 312 lines, 128K/+3/Next
are 456 × 311. Everything is counted in 28 MHz master cycles
(`MASTER_CLOCK_HZ`), from which the 7 MHz pixel clock and the four selectable
CPU speeds are derived in `src/core/clock.h`.

## The accuracy model, as implemented

| Layer | Granularity in the code |
|---|---|
| CPU | Per instruction. The FUSE Z80 core (`third_party/fuse-z80/`) executes one instruction; Z80N opcodes are intercepted before FUSE sees them. |
| Memory contention | Per bus cycle. FUSE's read/write callbacks call `ContentionModel::contention_tick()`, wired by `z80_set_contention_runtime()` (`src/cpu/z80_cpu.h`). |
| Copper | Per 28 MHz cycle, across the window each instruction consumed — `Emulator::tick_copper_for_master_cycles()`. |
| Interrupts | Scheduled into a min-heap by master cycle (`src/core/scheduler.h`), drained after every instruction. |
| Audio | Integrated over each instruction's cycle span, not point-sampled — `Emulator::advance_audio()` into `Mixer`. |
| Video | **Composited once, at the end of the frame.** |

That last row is the one nobody expects, and it shapes a large part of the
codebase. `Emulator::run_frame()` does not render as it goes: it schedules one
`SCANLINE` event per line, but those callbacks only *snapshot and tag* display
state (`Emulator::on_scanline`). The picture is produced in a single pass by
`Renderer::render_frame()` after the CPU has finished the frame, walking rows
0..255. Because the live registers then hold the frame's *last* values, every
register a demo changes mid-frame carries a per-scanline change log that the
renderer rewinds and replays row by row. [2.4 The video pipeline](../02-architecture/04-the-video-pipeline.md)
covers that mechanism; it is the single most load-bearing idea in the video
code.

Extreme cycle-exactness is deliberately not a goal — "good enough to develop
games on" is. The known consequences are documented rather than hidden: a
mid-line register write lands from the start of the row it fell in, so an
effect can appear at most one row early and never late.

## What a "frame" is

One call to `Emulator::run_frame()` advances the master clock by
`timing_.master_cycles_per_frame` and leaves a complete 640 × 256 ARGB8888
image in the framebuffer the frontend reads with `get_framebuffer()`. Real
time per frame follows the emulated refresh rate (`frame_period_ms()`), so a
60 Hz guest paces at 60 fps rather than a hardcoded 50.

A frame is not necessarily one call, though. The debugger pauses by *returning
from inside* the inner loop, so `frame_in_progress_` records that the next
call must resume the half-executed frame instead of restarting it — restarting
would rewind the Copper and wipe the change logs of a frame still in flight.

## The three frontends

All three drive the same `Emulator` and differ only in what surrounds it.

- **Qt GUI** — `src/gui/qt_app.*` plus `MainWindow`; built when `ENABLE_QT_UI=ON`.
  Qt owns the window, the menus and keyboard input; SDL is kept only for audio
  output. The debugger (`src/debugger/`) hangs off this frontend.
- **SDL-only** — `src/platform/sdl_app.*`; the fallback when `ENABLE_QT_UI=OFF`.
  Window, input and audio all through SDL2.
- **Headless** — `src/platform/headless_app.cpp`; selected at runtime by
  `--headless` in either build. No window, no audio device, no host input;
  runs uncapped and drives everything from frame counters. This is what the
  regression suite uses.

See [2.1 Startup and the frontends](../02-architecture/01-startup-and-the-frontends.md)
for how one of the three is chosen.
