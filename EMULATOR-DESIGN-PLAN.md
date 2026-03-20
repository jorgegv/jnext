# ZX Spectrum Next Emulator — High-Level Design Plan

> **VHDL source is authoritative.** When hardware behavior is ambiguous, consult the FPGA source at `../ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.

---

## Table of Contents

- [Table of Contents](#table-of-contents)
- [1. Goals and Scope](#1-goals-and-scope)
  - [Target](#target)
  - [Accuracy Model (chosen: **Line-accurate hybrid**)](#accuracy-model-chosen-line-accurate-hybrid)
  - [Machine targets (in priority order)](#machine-targets-in-priority-order)
- [2. Technology Choices](#2-technology-choices)
  - [2.1 Debugger UI Library Comparison](#21-debugger-ui-library-comparison)
- [3. Architecture Overview](#3-architecture-overview)
- [4. Project Structure](#4-project-structure)
- [5. Module Specifications](#5-module-specifications)
  - [5.1 Clock \& Scheduler](#51-clock--scheduler)
  - [5.2 CPU — Z80N](#52-cpu--z80n)
  - [5.3 Memory — MMU \& RAM](#53-memory--mmu--ram)
  - [5.4 Video Pipeline](#54-video-pipeline)
  - [5.5 Audio Engine](#55-audio-engine)
  - [5.6 I/O Port Dispatch](#56-io-port-dispatch)
  - [5.7 Peripheral Modules](#57-peripheral-modules)
  - [5.8 Input Subsystem](#58-input-subsystem)
  - [5.9 SDL Frontend](#59-sdl-frontend)
  - [5.10 Debug \& Introspection](#510-debug--introspection)
- [6. Emulation Accuracy Model](#6-emulation-accuracy-model)
  - [Pre-computed contention LUT](#pre-computed-contention-lut)
  - [Scanline execution model](#scanline-execution-model)
  - [Copper accuracy](#copper-accuracy)
- [7. Build System](#7-build-system)
- [8. Testing Strategy](#8-testing-strategy)
  - [Unit tests (per module)](#unit-tests-per-module)
  - [Integration tests](#integration-tests)
  - [Reference comparison](#reference-comparison)
- [9. Implementation Roadmap](#9-implementation-roadmap)
  - [Phase 1 — Skeleton \& CPU (target: boots to BASIC) ✓ COMPLETE](#phase-1--skeleton--cpu-target-boots-to-basic--complete)
  - [Phase 2 — Full ULA Video ✓ COMPLETE](#phase-2--full-ula-video--complete)
  - [Phase 2.5 — Logging \& Debugging](#phase-25--logging--debugging)
  - [Phase 3 — Extended Video (Layer 2 + Sprites + Tilemap)](#phase-3--extended-video-layer-2--sprites--tilemap)
  - [Phase 3.5 — Program Loading (NEX + raw binary)](#phase-35--program-loading-nex--raw-binary)
  - [Phase 4 — Audio](#phase-4--audio)
  - [Phase 5 — Peripherals \& Full I/O](#phase-5--peripherals--full-io)
  - [Phase 6 — Native UI \& Usability](#phase-6--native-ui--usability)
  - [Phase 7 — Debugger Window](#phase-7--debugger-window)
  - [Phase 8 — Polish \& Accuracy](#phase-8--polish--accuracy)
- [10. Key Pitfalls and Mitigations](#10-key-pitfalls-and-mitigations)

---

## 1. Goals and Scope

### Target

A real-time, cross-platform (Linux / macOS / Windows) software emulator of the ZX Spectrum Next computer, written in C++17.

### Accuracy Model (chosen: **Line-accurate hybrid**)

| Layer             | Granularity                                  | Rationale                               |
|-------------------|----------------------------------------------|-----------------------------------------|
| CPU               | Per-instruction with pre-computed contention | Sufficient for >99% of software         |
| ULA / Video       | Per-scanline                                 | Visible raster effects at line boundary |
| Copper            | Per-cycle within scanline                    | Palette changes must be cycle-precise   |
| Sprites / Layer 2 | Per-scanline compositing                     | Correct overlap and transparency        |
| Audio             | Per-sample (1/44100 s) accumulated per line  | Eliminates audible aliasing             |
| Interrupts        | Checked at start of each scanline            | Matches hardware firing at vc=1         |
| DMA               | Burst per logical operation                  | Sufficient for all standard uses        |

Full cycle-accurate mode (28 MHz reference) is a future optional build flag.

### Machine targets (in priority order)

1. Issue 2 / Spartan 6 — primary reference (matches CLAUDE.md)
2. 48K legacy mode
3. 128K/+3 legacy mode
4. Pentagon timing mode

---

## 2. Technology Choices

| Component          | Choice                                                     | Rationale                                                      |
|--------------------|------------------------------------------------------------|----------------------------------------------------------------|
| **Language**       | C++17                                                      | Lambdas, `std::variant`, structured bindings; no overhead      |
| **Build**          | CMake ≥ 3.16                                               | Universal; vcpkg integration; cross-platform                   |
| **Z80 core**       | **[FUSE Z80](https://fuse-emulator.sourceforge.net/)** (extracted from FUSE 1.6.0) | GPLv2; battle-tested, accurate; opcode files vendored in `third_party/fuse-z80/` with shim layer; isolated behind `Z80Cpu` wrapper |
| **AY/YM2149**      | AYemu (C library) or inline custom                         | AYemu is compact and accurate; easy to integrate               |
| **Display/Input**  | SDL2 (SDL3 migration later)                                | Mature, cross-platform, hardware-accelerated renderer          |
| **Audio output**   | SDL_AudioStream (SDL2 ≥ 2.0.18)                            | Flexible buffer; avoids callback latency                       |
| **Debugger UI**    | Qt 6 *(recommended)* or wxWidgets — see §2.1               | Native OS look-and-feel; proper widgets, not SDL-drawn text    |
| **Logging**        | spdlog                                                     | MIT licence; named loggers per subsystem; inline level check; fmt-style API; `SPDLOG_ACTIVE_LEVEL` strips trace/debug in release builds |
| **Testing**        | Google Test or Catch2                                      | Standard; integrates with CMake CTest                          |
| **Packaging**      | vcpkg or Conan                                             | Handles SDL2 + GUI library + dependencies on all platforms     |

### 2.1 Debugger UI Library Comparison

The debugger must present a **native application window** with real buttons, toolbars, splitter panes, tables, and text fields — not an SDL canvas with a hand-drawn UI. The three main candidates:

| Criterion              | **Qt 6**                          | **wxWidgets**                      | **GTK+ 4**                        |
|------------------------|-----------------------------------|------------------------------------|-----------------------------------|
| Look & feel            | Native on all platforms           | Native on all platforms            | Native on Linux; emulated on Win/macOS |
| CMake integration      | Excellent (`find_package(Qt6)`)   | Good (`find_package(wxWidgets)`)   | Good (pkg-config / vcpkg)         |
| C++ API quality        | Excellent (signals/slots, MOC)    | Good (event tables, macros)        | Fair (C API with C++ wrappers)    |
| License                | LGPL 3 / GPL 3 / Commercial       | wxWindows Licence (permissive)     | LGPL 2.1                          |
| Widget richness        | Very high (dock widgets, etc.)    | High                               | High                              |
| vcpkg support          | Yes                               | Yes                                | Yes (Linux-only package usually)  |
| Embedded SDL texture   | `QOpenGLWidget` or `QWindow`      | `wxGLCanvas`                       | `GtkGLArea`                       |
| Recommended for        | Full-featured debugger IDE feel   | Simpler/lighter, FOSS-friendly     | Linux-first projects              |

**Recommendation: Qt 6.**
- Best-in-class dockable panel system (`QDockWidget`) maps naturally to CPU / Memory / Video panels
- `QOpenGLWidget` or `QWindow::fromWinId` can embed the SDL emulator output inside the Qt window, giving a single unified window if desired
- Strong CMake support with `Qt6::Widgets`, `Qt6::OpenGL`
- LGPL licence allows use in non-GPL projects as long as the Qt library is dynamically linked

**Runner-up: wxWidgets** — if LGPL/Qt licence is a concern; API is more verbose but fully permissive.

---

## 3. Architecture Overview

```
┌────────────────────────────────────────────────────────────┐
│                        SDL Frontend                        │
│  Window · Renderer · Audio · Input Events · OSD · Menu     │
└──────────────────────┬─────────────────────────────────────┘
                       │ framebuffer ptr / audio samples / input state
┌──────────────────────▼─────────────────────────────────────┐
│                     Emulator Core                          │
│                                                            │
│  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐   │
│  │  Clock /    │  │   CPU       │  │  Video Renderer   │   │
│  │  Scheduler  │◄─│   Z80N      │  │  ULA+L2+TM+SPR    │   │
│  └──────┬──────┘  └──────┬──────┘  └────────┬──────────┘   │
│         │                │                  │              │
│  ┌──────▼──────┐  ┌──────▼──────┐  ┌────────▼──────────┐   │
│  │ Port        │  │  Memory     │  │  Audio Mixer      │   │
│  │ Dispatch    │◄─│  MMU / RAM  │  │  AY×3 + DAC + BEP │   │
│  └──────┬──────┘  └─────────────┘  └───────────────────┘   │
│         │                                                  │
│  ┌──────▼──────────────────────────────────────────────┐   │
│  │               Peripheral Modules                    │   │
│  │  DivMMC · CTC · UART · SPI · I2C · DMA · Copper     │   │
│  │  IM2 · Multiface · NextREG file                     │   │
│  └─────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────┘
```

**Key design rules:**
- The emulator **core has no SDL dependency** — pure C++ with no platform headers.
- SDL lives exclusively in `src/platform/`.
- All subsystems communicate via **direct method calls** (no message bus) for performance.
- The emulator exposes a single `run_frame()` call to the host loop.

---

## 4. Project Structure

```
zxnext-emulator/
├── CMakeLists.txt
├── EMULATOR-DESIGN-PLAN.md        ← this file
├── docs/
│   ├── TIMING.md                  reference: exact ULA counter values per machine
│   ├── PORT_MAP.md                reference: all I/O ports
│   └── NEXTREG_REFERENCE.md       reference: all NextREG registers
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── emulator.h / .cpp      top-level machine class, run_frame()
│   │   ├── clock.h / .cpp         28 MHz master clock + derived enables
│   │   └── scheduler.h / .cpp     priority-queue event scheduler
│   ├── cpu/
│   │   ├── z80_cpu.h / .cpp       Z80 core wrapper (FUSE Z80 backend; swap-friendly interface)
│   │   ├── z80n_ext.h / .cpp      26 Z80N extension opcodes
│   │   └── im2.h / .cpp           IM2 interrupt controller + daisy-chain
│   ├── memory/
│   │   ├── mmu.h / .cpp           8×8K slot mapping, fast dispatch tables
│   │   ├── ram.h / .cpp           768K / 1792K RAM backing store
│   │   ├── rom.h / .cpp           4×16K ROM banks + Alt ROM
│   │   └── contention.h / .cpp    per-machine pre-computed contention LUTs
│   ├── video/
│   │   ├── timing.h / .cpp        raster counters (hc/vc), blanking, sync
│   │   ├── ula.h / .cpp           ULA: pixel+attr fetch, screen modes, border
│   │   ├── lores.h / .cpp         LoRes 128×96 mode
│   │   ├── layer2.h / .cpp        Layer 2 bitmap (256×192/320×256/640×256)
│   │   ├── tilemap.h / .cpp       Tilemap 40×32 / 80×32
│   │   ├── sprites.h / .cpp       Sprite engine (≤128 sprites, 16×16)
│   │   ├── palette.h / .cpp       ULA / Layer2 / Sprite palettes
│   │   └── renderer.h / .cpp      layer compositing, framebuffer output
│   ├── audio/
│   │   ├── ay_chip.h / .cpp       AY-3-8910 / YM2149 (one instance)
│   │   ├── dac.h / .cpp           4× 8-bit DAC (Specdrum/Soundrive)
│   │   ├── beeper.h / .cpp        ULA EAR/MIC beeper
│   │   └── mixer.h / .cpp         stereo mix + sample accumulation
│   ├── port/
│   │   ├── port_dispatch.h / .cpp fast I/O port routing
│   │   └── nextreg.h / .cpp       NextREG file (0x243B/0x253B + all registers)
│   ├── peripheral/
│   │   ├── copper.h / .cpp        Copper co-processor
│   │   ├── dma.h / .cpp           Z80-DMA + ZXN-DMA
│   │   ├── divmmc.h / .cpp        DivMMC + SD card image
│   │   ├── ctc.h / .cpp           CTC 4-channel timer
│   │   ├── uart.h / .cpp          2× UART
│   │   ├── spi.h / .cpp           SPI master
│   │   └── i2c.h / .cpp           I2C bit-bang
│   ├── input/
│   │   ├── keyboard.h / .cpp      ZX keyboard matrix (8×5)
│   │   ├── joystick.h / .cpp      Sinclair / Kempston / MD joystick
│   │   └── mouse.h / .cpp         Kempston mouse
│   ├── platform/                  ← ONLY place with SDL headers
│   │   ├── sdl_app.h / .cpp       SDL init, main loop, frame pacer
│   │   ├── sdl_display.h / .cpp   window, renderer, texture pipeline
│   │   ├── sdl_audio.h / .cpp     SDL_AudioStream integration
│   │   ├── sdl_input.h / .cpp     SDL scancode → keyboard/joystick
│   ├── debug/                     ← pure C++, no GUI dependency
│   │   ├── disasm.h / .cpp        Z80+Z80N disassembler
│   │   ├── breakpoints.h / .cpp   execution breakpoints (PC, read/write watchpoints)
│   │   └── trace.h / .cpp         cycle trace log (circular buffer or file)
│   └── debugger/                  ← Qt 6 UI; gated by ENABLE_DEBUGGER
│       ├── CMakeLists.txt         links Qt6::Widgets + Qt6::OpenGL
│       ├── debugger_app.h / .cpp  QApplication entry, main window
│       ├── main_window.h / .cpp   QMainWindow with QDockWidgets
│       ├── cpu_panel.h / .cpp     QWidget: registers, step/run/pause buttons
│       ├── disasm_panel.h / .cpp  QWidget: scrollable disassembly list
│       ├── memory_panel.h / .cpp  QWidget: hex editor (QTableView or custom)
│       ├── video_panel.h / .cpp   QWidget: raster diagram, layer toggles, palette
│       ├── sprite_panel.h / .cpp  QWidget: 128-slot sprite viewer
│       ├── audio_panel.h / .cpp   QWidget: live waveforms, AY register table
│       ├── copper_panel.h / .cpp  QWidget: instruction list, live PC highlight
│       ├── nextreg_panel.h / .cpp QWidget: NextREG table (editable)
│       └── emulator_view.h / .cpp QOpenGLWidget embedding the SDL framebuffer
├── third_party/
│   ├── fuse-z80/                  (vendored — FUSE Z80 core, GPLv2 licence)
│   └── ayemu/                     (git submodule)
├── test/
│   ├── unit/                      per-module unit tests
│   └── integration/               full boot + known ROM tests
└── roms/
    └── .gitkeep                   (ROMs not redistributed)
```

---

## 5. Module Specifications

### 5.1 Clock & Scheduler

**Clock** (`src/core/clock.h`)
- 28 MHz master tick counter (`uint64_t cycle_`)
- Derived clock-enable signals: CPU (÷8/4/2/1), pixel (÷4 = 7 MHz), PSG enable
- CPU speed set via NextREG `0x07`

**Scheduler** (`src/core/scheduler.h`)
- Min-heap priority queue ordered by `uint64_t cycle_timestamp`
- Event types: `SCANLINE`, `VSYNC`, `CPU_INT`, `CTC_TICK`, `DMA_BURST`, `AUDIO_SAMPLE`, `INPUT_POLL`
- `run_frame()` drains events up to `frame_end_cycle`; CPU executes between events

```
run_frame():
  while current_cycle < frame_end_cycle:
    execute CPU instruction → advance cycle by T-states + contention
    drain scheduler events at current_cycle
    update Copper at current raster position
    if scanline boundary: render scanline, accumulate audio, check interrupts
```

---

### 5.2 CPU — Z80N

**Z80 core wrapper** (`src/cpu/z80_cpu.h`)
- **Chosen backend: FUSE Z80** (`third_party/fuse-z80/`, vendored from FUSE 1.6.0, GPLv2)
- Opcode files extracted with a shim layer (`fuse_z80_shim.h`) that stubs all FUSE emulator dependencies
- The wrapper provides `fuse_z80_readbyte/writebyte/readport/writeport` callbacks that dispatch to `Mmu::read/write` and `PortDispatch::read/write`
- The rest of the emulator only sees `Z80Cpu` — FUSE internals are never included outside `z80_cpu.cpp`
- To swap the backend in the future: rewrite only `z80_cpu.cpp`; all call sites remain unchanged
- Contention is currently a no-op in the FUSE shim (tstates += base time only); the emulator handles contention at a higher level
- `WAIT_n` modeled as additional T-states returned
- Z80N `ED`-prefix opcodes intercepted before FUSE sees them; dispatched to `z80n_ext.cpp`

**Z80N extensions** (`src/cpu/z80n_ext.h`)
- 26 new instructions (e.g., `SWAPNIB`, `MIRROR`, `PIXELAD`, `PIXELDN`, `NEXTREG nn,n`, `NEXTREG nn,A`, `TEST n`, `BSLA/BSRA/BSRL/BSRF/BRLC DE,B`, `MUL D,E`, `ADD HL,A`, `ADD DE,A`, `ADD BC,A`, `OUTINB`, `LDPIRX`, `LDIRX`, `LDDX`, `LDDRX`, `LDIRSCALE`, `PUSH nn`, `POP nn`, `LOOP`)
- Decoded after catching `ED` prefix byte not matched by standard Z80

**IM2 controller** (`src/cpu/im2.h`)
- 14 interrupt levels (CTC×4, ULA frame, line IRQ, UART×2, DivMMC, …)
- Priority daisy-chain: lowest active level wins the vector byte
- RETI detection: watch for `ED 4D` opcode sequence during M1 cycles
- Frame interrupt: fires at `vc=1, hc=0` (machine-type dependent offset)

---

### 5.3 Memory — MMU & RAM

**MMU** (`src/memory/mmu.h`)
- 8 slots × 8K = 64K CPU address space
- Each slot holds a 16-bit page number (physical 8K page index into RAM or ROM)
- `read_dispatch_[8]` / `write_dispatch_[8]`: precomputed `uint8_t*` pointers + size masks — updated whenever a page mapping changes
- Hot path fully inline:
  ```cpp
  inline uint8_t read(uint16_t addr) {
      return read_dispatch_[addr >> 13].ptr[addr & 0x1FFF];
  }
  ```
- Traditional 128K ports (`0x7FFD`, `0xDFFD`, `0x1FFD`) translate to MMU page writes
- Layer 2 mapping (NextREG `0x123B`) overlays MMU for read or write

**RAM** (`src/memory/ram.h`)
- Flat `std::vector<uint8_t>` of 768 KB (expandable to 1792 KB)
- 16K banks / 8K pages as simple index arithmetic

**ROM** (`src/memory/rom.h`)
- 4 × 16K slots loaded from `.rom` files at startup
- Alt ROM support via NextREG `0x8C`
- ROM slots map to read-only dispatch entries (write is a no-op)

**Contention** (`src/memory/contention.h`)
- Pre-computed LUT per machine type: `uint8_t lut[vc_max][hc_max]`
- Indexed by `(vc, hc)` at time of memory access → wait states (0–6)
- Contention applies only to 0x4000–0x7FFF (screen RAM) and port 0xFD family
- LUT is filled once at machine-type selection; zero cost at runtime

---

### 5.4 Video Pipeline

**Video timing** (`src/video/timing.h`)
- `hc` (0–447 or 0–455) and `vc` (0–319 depending on machine) counters in 7 MHz domain
- Sub-pixel `sc` (0–3) in 28 MHz domain
- Constants for each machine: active display start/end, blank start/end, sync pulses
- `cycle_to_raster(cycle, &x, &y)` — converts master cycle to raster position

**ULA** (`src/video/ula.h`)
- Pixel fetch: reads pixel byte + attribute byte from VRAM at `hc mod 16` intervals
- Shift register: 8 pixels shifted out per 8 hc ticks
- Floating bus: caches last fetched byte; returned on undriven reads of port `0xFF`
- Screen modes: Standard, Alternate (0x6000), Hi-colour, Hi-res (Timex), LoRes

**Layer 2** (`src/video/layer2.h`)
- Resolutions: 256×192 (8-bit), 320×256 (8-bit or 4-bit), 640×256 (4-bit)
- Pixel address computed from scroll registers + bank select
- Fetched from RAM backing store ahead of scanline render

**Tilemap** (`src/video/tilemap.h`)
- 40×32 or 80×32 grid; each cell = 1 byte (tile index) + optional attribute
- Pattern memory: 8×8 glyph bitmaps from RAM
- Hardware X/Y scroll via NextREG `0x2F`/`0x30`

**Sprites** (`src/video/sprites.h`)
- 128 sprites; per-sprite: X, Y (10-bit), pattern index, palette offset, flags (visible, rotate/mirror, scale ×1/2/4/8, 4-bit/8-bit colour, link)
- Pattern RAM: 16 KB (128 × 256 bytes for 8-bit, 128 × 128 bytes for 4-bit)
- Rendered per scanline: iterate all 128 sprites, check Y overlap, blit pattern row
- Collision detection: flag set in NextREG `0x1E` when sprites overlap

**Palette** (`src/video/palette.h`)
- RGB333 (9-bit) internal; convert to RGB565 for SDL texture output
- ULA palette: 16 entries (8 colours × normal/bright)
- Layer 2 / Sprite palette: 256 entries
- ULA+ extended palette: 64 entries (NextREG `0x40`–`0x4F`)

**Renderer / Compositor** (`src/video/renderer.h`)
- Scanline buffer: `uint16_t scanline[640]` (RGB565)
- Per-scanline pass: ULA → LoRes → Layer2 → Tilemap → Sprites in priority order
- Priority order controlled by NextREG `0x15`
- Transparency: colour index 0 / E3 transparent flag skips write
- Output: 320×256 (or machine-native) framebuffer uploaded to SDL texture each frame

---

### 5.5 Audio Engine

**AY chip** (`src/audio/ay_chip.h`)
- One instance per AY (3 total for TurboSound); controlled via ports `0xFFFD`/`0xBFFD`
- 16 registers: tone periods (A/B/C), noise, mixer, volume, envelope
- Runs at `28 MHz / prescaler` clock-enable rate
- Outputs: 3 channel values → mixed stereo by `Mixer`

**DAC** (`src/audio/dac.h`)
- 4 channels (A, B, C, D): written via various ports (`0x1F`, `0x0F`, `0x4F`, `0xDF` etc.)
- A+D → left; B+C → right; summed with AY output

**Beeper** (`src/audio/beeper.h`)
- Toggled by port `0xFE` bit 4 (EAR out) and bit 3 (MIC)
- Generates square-wave; accumulated per sample period

**Mixer** (`src/audio/mixer.h`)
- Accumulates per-scanline samples from AY×3 + DAC + Beeper
- Outputs stereo `int16_t` pairs at 44100 Hz into a ring buffer
- Ring buffer fed to `sdl_audio.cpp` via `SDL_PutAudioStreamData`

---

### 5.6 I/O Port Dispatch

**Port dispatch** (`src/port/port_dispatch.h`)
- Primary table: `std::array<Handler, 256>` for low-byte (fast path for common ports)
- Extended table: `std::unordered_map<uint16_t, Handler>` for full 16-bit port decode
- ZX Spectrum ports are decoded by address line masking (not full 16-bit match) — implement mask-match registration: `register(mask, value, read_fn, write_fn)`

Key port registrations:

| Port (mask)       | Handler                                   |
|-------------------|-------------------------------------------|
| `0x00FF = 0xFE`   | ULA: keyboard rows / border / beeper      |
| `0x00FF = 0xFF`   | Timex screen mode                         |
| `0xC002 = 0xC000` | AY data (`0xBFFD`)                        |
| `0xC002 = 0x8000` | AY register select (`0xFFFD`)             |
| `0xE002 = 0x0000` | 128K bank (`0x7FFD`)                      |
| `0xF002 = 0x1000` | +3 extended (`0x1FFD`)                    |
| `0x00FF = 0xE3`   | DivMMC control                            |
| `0x00FF = 0x3B`   | NextREG family (`0x243B`, `0x253B`, etc.) |

**NextREG file** (`src/port/nextreg.h`)
- Array of 256 registers; write routes to appropriate subsystem handler
- Read returns cached value
- `nextreg_write(reg, val)` dispatches to: video, audio, memory, copper, IM2, etc.

---

### 5.7 Peripheral Modules

**Copper** (`src/peripheral/copper.h`)
- 1K × 16-bit instruction RAM (ports `0x303B`, `0x60B`, `0x61B`)
- Two instruction types:
  - `WAIT hpos, vpos` — stall until `vc == vpos && hc >= hpos`
  - `MOVE nextreg, value` — write to NextREG (bypasses normal CPU path)
- Execute loop runs per-scanline: scan from PC until WAIT for a future line
- Apply all buffered MOVE writes at line boundary before rendering

**DMA** (`src/peripheral/dma.h`)
- Port `0x0B` (Z80-DMA compat) and `0x6B` (ZXN burst mode)
- State machine: `IDLE → BUS_REQUEST → TRANSFERRING → DONE`
- Bus arbitration: CPU stalls after current instruction completes; DMA runs burst
- Address modes: increment / decrement / fixed for both source and destination

**DivMMC** (`src/peripheral/divmmc.h`)
- Port `0xE3`: `conmem` (bit 7), `mapram` (bit 6), bank select (bits 3:0)
- Auto-map trigger addresses: `0x0000`, `0x0008`, `0x0038`, `0x0066`, `0x4C02`
- SD card simulation: read `.img` file; implement SPI byte protocol via SPI module

**CTC** (`src/peripheral/ctc.h`)
- 4 channels; ports `0x183B`–`0x1B3B`
- Timer mode: reload counter on underflow, generate interrupt
- Counter mode: count external triggers
- Daisy-chain ZC/TO output of channel N feeds trigger of channel N+1

**UART** (`src/peripheral/uart.h`)
- 2 channels (ESP / Pi)
- 512-byte RX FIFO, 64-byte TX FIFO
- Host bridge: can connect to TCP socket or stdin/stdout for ESP emulation

**SPI** (`src/peripheral/spi.h`)
- CS via port `0xE7`; data via port `0xEB`
- Delegates byte exchanges to SD card or flash emulation backend

**I2C** (`src/peripheral/i2c.h`)
- Bit-banged via ports `0x103B` (SCL) / `0x113B` (SDA)
- RTC (DS1307) emulated using host system clock

---

### 5.8 Input Subsystem

**Keyboard** (`src/input/keyboard.h`)
- 8-row × 5-column matrix; read via port `0xFE` (upper 8 bits select row)
- SDL scancode → matrix position lookup table
- Programmable function key mapping via NextREG

**Joystick** (`src/input/joystick.h`)
- Sinclair (ports `0x7FFE`, `0xEFFE`), Kempston (`0x001F`), MD 6-button
- SDL_GameController API; configurable button mapping

**Mouse** (`src/input/mouse.h`)
- Kempston mouse protocol: ports `0xFADF`, `0xFBDF`, `0xFFDF`
- Relative motion accumulated from SDL mouse events

---

### 5.9 SDL Frontend

**Application** (`src/platform/sdl_app.h`)
- `init()` → `SDL_Init`, create window + renderer, open audio, enumerate controllers
- `run()` → main loop: `SDL_PollEvent` → translate input → `emulator.run_frame()` → upload texture → `SDL_RenderPresent` → frame pace
- `shutdown()` → clean SDL teardown

**Display** (`src/platform/sdl_display.h`)
- `SDL_CreateTexture` with `SDL_PIXELFORMAT_RGB565`, `SDL_TEXTUREACCESS_STREAMING`
- Integer scaling: compute largest integer scale fitting window; center output
- `SDL_RenderSetLogicalSize(renderer, native_w, native_h)` for aspect ratio
- Fullscreen: `SDL_WINDOW_FULLSCREEN_DESKTOP` toggle on `F11`
- Optional CRT scanline overlay: second pass with semi-transparent horizontal lines

**Audio** (`src/platform/sdl_audio.h`)
- Open `SDL_AudioStream` at 44100 Hz, stereo, `AUDIO_S16`
- Per-frame: call `SDL_PutAudioStreamData(stream, mixer_buf, samples * 4)`
- SDL pulls from stream in hardware callback; automatic resampling handles rate drift
- Target buffer: ~2 frames (40 ms) to absorb jitter without audible latency

**Input** (`src/platform/sdl_input.h`)
- `SDL_PollEvent` loop; dispatch:
  - `SDL_KEYDOWN/UP` → `Keyboard::set_key(scancode, pressed)`
  - `SDL_CONTROLLERBUTTONDOWN/UP` → `Joystick::set_button`
  - `SDL_MOUSEMOTION` → `Mouse::move`
  - `SDL_QUIT` → application exit

**Native UI** (Phase 6 — Qt 6)
- In Phase 6 the raw SDL window is replaced by a Qt 6 `QMainWindow`
- Menu bar, toolbar, and status bar provide all emulator controls via native OS widgets
- The emulator framebuffer is displayed inside a `QOpenGLWidget` central widget
- SDL continues to handle audio output and low-level input; video output is redirected to the Qt widget
- OSD information (FPS, speed, machine mode) shown in the Qt status bar instead of a bitmap overlay

---

### 5.10 Debug & Introspection

The debug subsystem is split into two independent layers:

**Backend — pure C++, no GUI dependency** (`src/debug/`)

- `disasm.h` — disassembles Z80 + Z80N opcodes from a memory read callback; returns a `DisasmLine` struct (address, bytes, mnemonic string)
- `breakpoints.h` — PC breakpoints and read/write/I/O watchpoints; checked inside `Z80Cpu::execute_instruction()` via a hot-path inline test; fires a `BreakpointCallback` to pause the emulator
- `trace.h` — per-instruction state log (PC, AF, BC, DE, HL, SP, cycle count); circular in-memory buffer or streaming to file; zero overhead when disabled

**Frontend — Qt 6 GUI** (`src/debugger/`)

The debugger runs as a **separate process or a separate thread** that communicates with the emulator core via a thin `DebuggerInterface` API (read registers, read/write memory, set/clear breakpoints, step, run, pause). This keeps the emulator's hot loop free from Qt event processing.

`DebuggerInterface` (`src/debug/debugger_interface.h`) — the bridge:
```cpp
class DebuggerInterface {
public:
    // Control
    void pause();
    void resume();

    // Step Into: execute exactly one instruction (including entering CALLs/RSTs/traps);
    // always advances PC by one instruction regardless of type.
    void step_into();

    // Step Over: if current instruction is CALL nn, CALL cc,nn, RST n, or DJNZ:
    //   set a temporary "one-shot" breakpoint at PC + instruction_length, then resume.
    //   The emulator runs freely until that breakpoint fires, then pauses again.
    //   For all other instructions, behaves identically to step_into().
    void step_over();

    // Step Out: set a temporary breakpoint that fires on the next RET/RETI/RETN
    //   at the current stack depth (SP == current SP at call time), then resume.
    void step_out();

    void run_to(uint16_t addr);   // temporary breakpoint at addr, then resume

    // Inspection (safe to call from Qt thread via mutex)
    Z80Cpu::Registers get_registers() const;
    uint8_t read_memory(uint16_t addr) const;
    void write_memory(uint16_t addr, uint8_t value);
    uint8_t read_nextreg(uint8_t reg) const;
    void write_nextreg(uint8_t reg, uint8_t value);
    RasterPos get_raster_pos() const;
    Sprite get_sprite(uint8_t idx) const;

    // Breakpoints
    void add_breakpoint(uint16_t pc);
    void add_watchpoint(uint16_t addr, WatchType type);
    void clear_breakpoint(uint16_t pc);

    // Signals (emitted to Qt via Qt::QueuedConnection)
    std::function<void()> on_breakpoint_hit;
    std::function<void()> on_frame_complete;
};
```

---

---

## 6. Emulation Accuracy Model

### Pre-computed contention LUT

```
For each machine type, fill lut[vc][hc] = wait_states:
- 48K: contend 0x4000–0x7FFF, hc ∈ [128, 383], vc ∈ [64, 255]
  contention pattern: hc mod 8 → {6,5,4,3,2,1,0,0} wait states
- 128K/+3: same range + port 0xFD family during specific hc window
- Pentagon: no contention
```

### Scanline execution model

```
for vc = 0 to frame_height - 1:
  check_pending_interrupts(vc)           // IM2 + frame IRQ at vc==1
  execute_cpu_instructions_for_line()    // ~228 T-states @ 3.5MHz per line
  copper_execute_line(vc)                // apply buffered NextREG writes
  render_scanline(vc)                    // ULA→L2→TM→SPR composite
  accumulate_audio_samples()             // ~882 samples/line @ 44100Hz (50Hz frame)
```

### Copper accuracy

Copper is the only subsystem run with sub-scanline granularity. Within `execute_cpu_instructions_for_line()`, after each instruction, the Copper PC advances through any `WAIT` instructions satisfied by the current `(hc, vc)` position and fires `MOVE` writes immediately. This gives palette-change precision within a scanline.

---

## 7. Build System

```cmake
# Root CMakeLists.txt skeleton
cmake_minimum_required(VERSION 3.16)
project(zxnext VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

option(ENABLE_TESTS    "Build unit tests"       ON)
option(ENABLE_DEBUGGER "Include Qt debugger UI" OFF)
option(CYCLE_ACCURATE  "28 MHz cycle-accurate"  OFF)

find_package(SDL2 REQUIRED)
if(ENABLE_DEBUGGER)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL)
endif()

add_subdirectory(src/core)
add_subdirectory(src/cpu)
add_subdirectory(src/memory)
add_subdirectory(src/video)
add_subdirectory(src/audio)
add_subdirectory(src/port)
add_subdirectory(src/peripheral)
add_subdirectory(src/input)
add_subdirectory(src/platform)
if(ENABLE_DEBUGGER)
    add_subdirectory(src/debugger)
endif()

add_executable(zxnext src/main.cpp)
target_link_libraries(zxnext PRIVATE
    zxnext_core zxnext_cpu zxnext_memory
    zxnext_video zxnext_audio zxnext_port
    zxnext_peripheral zxnext_input
    zxnext_platform
    SDL2::SDL2)

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
```

**Platform dependency setup:**
- Linux: `apt install libsdl2-dev` (+ `qt6-base-dev libqt6opengl6-dev` for debugger)
- macOS: `brew install sdl2 qt6`
- Windows: vcpkg `install sdl2:x64-windows qt6:x64-windows`

---

## 8. Testing Strategy

### Unit tests (per module)

| Test file                | Covers                                             |
|--------------------------|----------------------------------------------------|
| `test_mmu.cpp`           | Slot mapping, bank switch, ROM/RAM dispatch        |
| `test_contention.cpp`    | LUT values vs FUSE reference traces                |
| `test_ula_timing.cpp`    | hc/vc counter ranges, display windows, sync pulses |
| `test_ay_chip.cpp`       | Register writes, tone frequency, envelope shapes   |
| `test_port_dispatch.cpp` | Mask-match routing, 0xFE, 0x7FFD, 0x243B           |
| `test_im2.cpp`           | Daisy-chain arbitration, RETI detection            |
| `test_copper.cpp`        | WAIT semantics, MOVE timing                        |
| `test_sprites.cpp`       | Overlap, scaling, 4-bit vs 8-bit                   |

### Integration tests

- **Boot test**: Load 48K ROM, run 10 frames, assert CPU PC is in BASIC interpreter range
- **Visual golden tests**: Render known screens (e.g., BASIC prompt), compare against reference PNG
- **Audio golden tests**: Run AY beep program for 1 second, compare waveform

### Reference comparison

- Use **FUSE test suite** (Z80 opcode tests with cycle counts) to validate Z80 core integration
- Use **CSpect** or **ZEsarUX** as reference emulators for visual/audio output comparison
- Use **timing-sensitive demos** (e.g., those relying on ULA contention) as acceptance criteria

---

## 9. Implementation Roadmap

### Phase 1 — Skeleton & CPU (target: boots to BASIC) ✓ COMPLETE

- [x] CMake project skeleton, CI pipeline (GitHub Actions: Linux + macOS + Windows)
- [x] `Clock` and `Scheduler` stubs
- [x] `Mmu` with 768K RAM + 48K ROM loading + fast dispatch tables
- [x] ~~Add libz80 as `third_party/libz80` git submodule~~ → Replaced by FUSE Z80 core (`third_party/fuse-z80/`)
- [x] Wire FUSE Z80 core into `z80_cpu.cpp` behind the existing `Z80Cpu` wrapper interface
- [x] Connect `MemoryInterface` and `IoInterface` callbacks to `Mmu` and `PortDispatch`
- [x] Port dispatch: `0xFE`, `0x7FFD` only
- [x] SDL window: display raw framebuffer (all black is fine)
- [x] SDL keyboard: map SDL scancodes to ZX keyboard matrix
- [x] **Milestone**: 48K BASIC prompt visible, keyboard input works

### Phase 2 — Full ULA Video ✓ COMPLETE

- [x] Video timing (all machine types: hc/vc counters) — 48K, 128K, Pentagon; sub-pixel sc counter deferred to Phase 8
- [x] ULA pixel + attribute rendering (all screen modes) — standard 48K + Timex hi-colour/hi-res done; LoRes deferred to Phase 3 (NextREG-dependent)
- [x] Border rendering — correct 32+192+32 symmetric layout, full-width rows
- [x] Contention LUT generation (48K, 128K, Pentagon) — all done; FUSE validation deferred to Phase 8
- [x] Frame interrupt (IM2) at correct raster position — fires at vc=1; Im2Controller wired; RETI detection via M1 hook
- [x] SDL texture upload pipeline + integer scaling — ARGB8888, 2× scaling, SDL logical size
- [x] Window scaling 1x/2x/3x selectable by user via F2 key
- [x] **Milestone**: BASIC screen rendered correctly at 50 Hz

### Phase 2.5 — Logging & Debugging

- [x] Integrate spdlog (vendored `third_party/spdlog` v1.17.0 as git submodule)
- [x] Logging wrapper (`src/core/log.h`) creating one named spdlog logger per subsystem (cpu, memory, ula, video, audio, port, nextreg, dma, copper, uart, input, platform, emulator)
- [x] Runtime toggle of debug sections via command-line flags (`--log-level cpu=trace,video=warn`)
- [x] Configurable output: stderr (default; coloured)
- [x] Log levels per section (error, warn, info, debug, trace) — default: info
- [x] Minimal overhead when disabled: runtime check is inline integer compare
- [x] Replace all ad-hoc fprintf(stderr) calls with structured log calls
- [x] Add logging calls to existing subsystems (CPU, MMU, ROM, ULA, port dispatch, NextREG, keyboard)
- [x] **Milestone**: Selective debug output available for any subsystem without recompilation

### Phase 3 — Extended Video (Layer 2 + Sprites + Tilemap)

- [x] Palette subsystem (ULA + Layer2 + Sprite + Tilemap palettes) — PaletteManager with 2×16 ULA, 2×256 L2/Sprite/Tilemap banks; RGB333 internal, ARGB8888 cached; NextREG 0x40/0x41/0x43/0x44/0x14/0x4B/0x4C wired
- [x] Layer 2 bitmap renderer (256×192 @ 8-bit) — reads from physical RAM banks with X/Y scroll wrapping; palette offset; transparency; NextREG 0x12/0x13/0x16/0x17/0x70/0x71 + port 0x123B wired; verified against VHDL layer2.vhd
- [x] Sprite engine (128 sprites, 8-bit colour, scale ×1) — 128 sprites, 16×16, 8-bit and 4-bit colour; VHDL-verified 5-byte attribute format; port 0x303B/0x57/0x5B; pattern RAM 16KB; rotation/mirror; collision detection; clip window; NextREG 0x34/0x75-0x79
- [x] Tilemap (40×32 mode) — 40×32 and 80×32 modes; 4bpp patterns + text mode (1bpp); 512-tile mode; force-attribute; ULA-over-tilemap per-tile priority; X/Y scroll; base address decoding (bank 5/7); NextREG 0x6B/0x6C/0x6E/0x6F/0x2F/0x30/0x31
- [x] Layer compositor with NextREG priority control — per-scanline compositing of ULA+Tilemap/Layer2/Sprites; 6 priority modes (SLU/LSU/SUL/LUS/USL/ULS); all sprite/tilemap/copper port+NextREG handlers wired; copper executes per CPU instruction
- [x] Copper co-processor — 1K×16-bit instruction RAM; WAIT (vc==vpos && hc>=(hpos<<3)+12) and MOVE; HALT/NOP; mode edge detection; 2-cycle MOVE timing; NextREG 0x60/0x61/0x62/0x63 write paths with MSB/LSB pairing
- [ ] **Milestone**: Next-specific games render correctly

### Phase 3.5 — Program Loading (NEX + raw binary)

- [x] `--inject FILE` CLI flag with `--inject-org ADDR` (default `0x8000`) and `--inject-pc ADDR` (default = org): loads a raw binary into RAM and jumps to it; useful for quick testing of z88dk-compiled programs without tape/snapshot support
- [x] Create a demo with sprite and Layer2 rendering that allows testing the emulator and Phase 3 functionality
- [ ] NEX file loader: parse the NEX v1.3 header (512 bytes), load memory pages (16K banks) into RAM, set PC to entry point, configure initial CPU speed / Layer 2 / border from header fields
- [ ] `--load FILE` CLI flag: auto-detects file format by extension (`.nex`) and loads accordingly; extensible for future `.sna`/`.z80`/`.tap` support
- [ ] Wire file loading into the emulator UI (drag-and-drop or File menu in Phase 6)
- [ ] **Milestone**: Can load and run `.nex` files directly from the command line

### Phase 4 — Audio

- [ ] AY-3-8910 × 3 (TurboSound)
- [ ] DAC × 4
- [ ] Beeper
- [ ] Audio mixer → SDL_AudioStream
- [ ] CTC (drives some AY timing)
- [ ] **Milestone**: Music and sound effects audible

### Phase 5 — Peripherals & Full I/O

- [ ] NextREG file (all registers)
- [ ] DivMMC + SD card `.img` mounting
- [ ] UART (loopback / TCP bridge)
- [ ] SPI + I2C stubs
- [ ] DMA (Z80-DMA compat + ZXN burst)
- [ ] IM2 full controller (all 14 levels)
- [ ] Z80N extension opcodes
- [ ] **Milestone**: NextZXOS boots from SD image

### Phase 6 — Native UI & Usability

- [ ] Qt 6 `QMainWindow` as the main emulator window (replaces raw SDL window)
- [ ] Menu bar: **File** (Mount SD image, Quit), **Machine** (Reset, Machine type, CPU speed), **View** (1×/2×/3× scaling, Fullscreen toggle, CRT filter), **Help** (About)
- [ ] Toolbar: **Reset** button (hard reset, like pressing the Next's reset button); additional buttons as needed
- [ ] Emulator viewport: `QOpenGLWidget` or `QWidget` embedding the SDL framebuffer as the central widget
- [ ] Fullscreen mode: toggle between windowed (menu bar + toolbar + viewport) and borderless fullscreen (viewport only); shortcut `F11`
- [ ] Status bar: FPS, CPU speed, current machine mode
- [ ] CRT scanline filter (optional, toggled from View menu)
- [ ] SDL remains for audio output and input event handling; video output rendered into the Qt widget
- [ ] **Milestone**: Native application window with menu bar, toolbar, and fullscreen toggle

### Phase 7 — Debugger Window

Extends the Phase 6 Qt 6 main window with **dockable debugger panels** providing full introspection into the running emulator. Uses real OS widgets (buttons, toolbars, tables, splitters). Requires Phase 6 complete (stable Qt-based emulator).

**Infrastructure**
- [ ] `DebuggerInterface` API bridging emulator core ↔ Qt UI (mutex-protected, `QueuedConnection` signals)
- [ ] `QMainWindow` with `QDockWidget` panels; each panel independently floatable/resizable
- [ ] Toolbar: **Run** / **Pause** / **Step Into** / **Step Over** / **Step Out** / **Run to Cursor** / **Reset** buttons using `QToolBar` + `QAction` with keyboard shortcuts (F5 Run, F9 Pause, F11 Step Into, F10 Step Over, Shift+F11 Step Out)
- [ ] Status bar: current PC, cycle count, raster position, emulation state (Running / Paused / Breakpoint)
- [ ] `ENABLE_DEBUGGER` CMake flag; when OFF the debugger sources are not compiled, zero overhead

**CPU panel** (`QDockWidget`)
- [ ] `QFormLayout` showing all Z80 registers live (AF, BC, DE, HL, IX, IY, SP, PC, I, R, IFF1/2, IM)
- [ ] Z80N extension register display
- [ ] IM2 interrupt state table (14 levels, pending/active/vector)
- [ ] Step control buttons (mirrored from toolbar for convenience):
  - **Step Into** (`F11`): execute one instruction; if it is a CALL/RST, PC moves into the callee
  - **Step Over** (`F10`): if current instruction is `CALL nn`, `CALL cc,nn`, `RST n`, or `DJNZ`: place a one-shot temporary breakpoint at `PC + instruction_length` and resume; execution re-pauses when that address is reached (i.e. after the called routine returns); for all other instructions, behaves as Step Into
  - **Step Out** (`Shift+F11`): set a one-shot breakpoint that fires on the next `RET`/`RETI`/`RETN` where SP equals the current SP value; then resume — pauses on return to caller
  - **Run to Cursor**: set a one-shot breakpoint at the disassembly panel's selected line and resume
- [ ] Breakpoint list (`QTableWidget`): add/remove/enable persistent PC breakpoints and read/write/I/O watchpoints

**Disassembly panel** (`QDockWidget`)
- [ ] `QListView` or custom `QAbstractItemModel` showing disassembled instructions
- [ ] Current PC row highlighted in a distinct colour
- [ ] Click margin to set/clear breakpoint (red dot gutter)
- [ ] "Follow PC" toggle; manual address navigation via address bar (`QLineEdit`)
- [ ] Annotation: known ROM labels shown inline as comments

**Memory panel** (`QDockWidget`)
- [ ] Hex editor widget (`QTableView` with custom delegate): address | hex bytes | ASCII
- [ ] Page selector: MMU slot (0–7) or raw 8K page number (`QSpinBox` / `QComboBox`)
- [ ] Highlighted regions: VRAM cyan, attribute area yellow, SP±16 orange
- [ ] Live inline edit: double-click a byte cell, type new hex value, Enter to commit

**Video / raster panel** (`QDockWidget`)
- [ ] Miniature frame diagram (`QWidget` custom paint): crosshair at live (hc, vc)
- [ ] Layer checkboxes (`QCheckBox`): ULA / LoRes / Layer 2 / Tilemap / Sprites — toggle applied live
- [ ] Palette swatch grid (`QWidget` custom paint): 256 Layer2 + 16 ULA colours
- [ ] Sprite table (`QTableWidget`): 128 rows × columns (index, X, Y, pattern, flags, preview thumbnail)

**Audio panel** (`QDockWidget`)
- [ ] Waveform widgets (`QWidget` custom paint): rolling plot for each AY channel + DAC L/R + Beeper
- [ ] AY register table (`QTableWidget`): 16 rows × 3 chips, editable values
- [ ] Mute `QCheckBox` per source; master volume `QSlider`

**Copper panel** (`QDockWidget`)
- [ ] `QTableView` showing decoded Copper instruction list (address, type, hpos/vpos/nextreg/value)
- [ ] Current Copper PC row highlighted live
- [ ] Enable/disable Copper `QCheckBox`
- [ ] Edit instruction cells directly via table delegate

**NextREG panel** (`QDockWidget`)
- [ ] `QTableWidget` 256 rows: address (hex), name, current value (hex + binary), description
- [ ] Editable value column: type new hex value, Tab to commit → calls `write_nextreg()`

**Emulator screen embed** (optional)
- [ ] `QOpenGLWidget` (`EmulatorView`) can optionally display the emulator framebuffer inside the Qt window, replacing the separate SDL window — controlled by a startup flag

**Build integration**
- [ ] `src/debugger/CMakeLists.txt` uses `find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL)`
- [ ] MOC (Meta-Object Compiler) run automatically via `qt_add_executable` / `set_target_properties(AUTOMOC ON)`
- [ ] vcpkg: `install qt6:x64-linux qt6:x64-osx qt6:x64-windows`
- [ ] **Milestone**: Can set a breakpoint, single-step through ROM, inspect and edit any memory page and any NextREG live, with all panels dockable like a real IDE debugger

### Phase 8 — Polish & Accuracy

- [ ] Run FUSE Z80 opcode test suite; fix any failures
- [ ] Floating bus emulation
- [ ] Pentagon timing mode
- [ ] Layer 2 320×256 and 640×256 modes
- [ ] Sprite scaling ×2/×4/×8
- [ ] Performance profiling and optimization
- [ ] CI golden-output visual regression tests
- [ ] **Milestone**: v1.0 release

---

## 10. Key Pitfalls and Mitigations

| Risk                            | Detail                                                                            | Mitigation                                                                     |
|---------------------------------|-----------------------------------------------------------------------------------|--------------------------------------------------------------------------------|
| **Contention off-by-one**       | Mistiming by 1 T-state breaks cycle-sensitive code                                | Pre-compute LUT from VHDL timing constants; validate against FUSE traces       |
| **Floating bus**                | Port 0xFF reads must return last VRAM fetch byte                                  | Track last fetch byte per scanline position                                    |
| **IM2 RETI detection**          | Must catch `ED 4D` on M1 cycle, not just any execution                            | Hook M1 cycle callback in CPU wrapper                                          |
| **Copper WAIT semantics**       | `hc >= threshold`, not `hc == threshold`; can miss if not checked every cycle     | Check Copper PC advancement after every CPU instruction within line            |
| **Port masking**                | ZX Spectrum decodes ports by address-line masking, not full 16-bit compare        | Use mask/value registration in port dispatch                                   |
| **Layer 2 banking**             | L2 overlay takes priority over MMU for some access modes                          | Apply L2 override check before MMU dispatch                                    |
| **SDL audio stutter**           | Emulator produces exactly 882 samples/frame at 50 Hz; SDL wants continuous stream | Use SDL_AudioStream with 3-frame buffer; accept minor latency                  |
| **Cross-platform pixel format** | SDL texture format varies by platform GPU                                         | Always use `SDL_PIXELFORMAT_RGB565`; let SDL handle conversion                 |
| **DMA mid-instruction**         | DMA can legally take the bus between T-states                                     | In line-accurate mode, stall CPU at instruction boundary; document limitation  |
| **Z80N opcode conflicts**       | Some Z80N opcodes reuse `ED xx` space; must not mis-decode                        | Intercept `ED` prefix in `z80_cpu.cpp` before FUSE core sees it; dispatch to `z80n_ext`; unmapped `ED xx` fall through to FUSE |

---

*This document is the living design specification. Sections will be refined as implementation proceeds. All timing constants and register definitions should be cross-checked against the FPGA source before coding.*
