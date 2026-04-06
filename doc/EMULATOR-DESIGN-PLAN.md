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
  - [Phase 3.5 — Program Loading (NEX + raw binary) ✓ COMPLETE](#phase-35--program-loading-nex--raw-binary--complete)
  - [Phase 4 — Audio ✓ COMPLETE](#phase-4--audio--complete)
  - [Phase 5 — Peripherals \& Full I/O ✓ COMPLETE](#phase-5--peripherals--full-io--complete)
  - [Phase 6 — Native UI \& Usability ✓ COMPLETE](#phase-6--native-ui--usability--complete)
  - [Phase 7 — Debugger Window ✓ COMPLETE](#phase-7--debugger-window--complete)
  - [Phase 7.5 - Debugger enhancements](#phase-75---debugger-enhancements)
  - [Phase 7.8 — Polish \& Accuracy](#phase-78--polish--accuracy)
  - [Phase 8  More enhancements](#phase-8--more-enhancements)
  - [Phase 9 - CI, Quality and Release](#phase-9---ci-quality-and-release)
  - [Phase 10 — NextZXOS Boot (v1.1)](#phase-10--nextzxos-boot-v11)
  - [Phase 11 - New functions](#phase-11---new-functions)
- [10. Key Pitfalls and Mitigations](#10-key-pitfalls-and-mitigations)
- [11. Pending Issues](#11-pending-issues)

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
- [x] Window scaling 1x/2x/3x/4x selectable by user via F2 key
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
- [x] Create Z88DK test programs for different functions (store them in `demo` directory):
  - [x] Layer2 and sprites
  - [x] Tilemap
  - [x] Copper
  - [x] Palette
- [x] Verify all works ok:
  - [x] Layer 2
  - [x] Sprites
  - [x] Tilemap
  - [x] Copper
  - [x] Palette
- [x] **Milestone**: Next-specific games render correctly

### Phase 3.5 — Program Loading (NEX + raw binary) ✓ COMPLETE

- [x] `--inject FILE` CLI flag with `--inject-org ADDR` (default `0x8000`) and `--inject-pc ADDR` (default = org): loads a raw binary into RAM and jumps to it; useful for quick testing of z88dk-compiled programs without tape/snapshot support
- [x] Create a demo with sprite and Layer2 rendering that allows testing the emulator and Phase 3 functionality
- [x] NEX file loader: parse the NEX V1.0/V1.1/V1.2 header (512 bytes), load memory pages (16K banks) into RAM, set PC to entry point, configure Layer 2 screen/palette / border / entry bank from header fields — `NexLoader` class in `src/core/nex_loader.h/.cpp`
- [x] `--load FILE` CLI flag: auto-detects file format by extension (`.nex`) and loads accordingly; extensible for future `.sna`/`.z80`/`.tap` support
- [x] Wire file loading into the emulator UI (File menu in Phase 6) — Load NEX via menu or Ctrl+O
- [x] **Milestone**: Can load and run `.nex` files from CLI and GUI

### Phase 4 — Audio ✓ COMPLETE

- [x] AY-3-8910 × 3 (TurboSound) — YM2149 emulation with 3 tone generators, noise LFSR (17-bit), envelope (16 shapes), AY/YM volume tables; TurboSound wrapper with per-chip stereo/mono mixing and panning; all verified against VHDL ym2149.vhd and turbosound.vhd
- [x] DAC × 4 — Soundrive 4-channel 8-bit DAC (A+B→L, C+D→R); Soundrive Mode 1+2 ports, Specdrum, Covox aliases; verified against VHDL soundrive.vhd
- [x] Beeper — EAR/MIC from port 0xFE bits 4/3 with VHDL-matched volume scaling (EAR=256, MIC=32)
- [x] Audio mixer → SDL_AudioStream — mixer combines all sources with audio_mixer.vhd scaling (13-bit internal); ring buffer at 44100 Hz stereo int16; SDL_AudioStream bridge with overflow protection
- [x] NextREG audio control — 0x06 (PSG mode AY/YM), 0x08 (stereo mode/DAC enable/TurboSound enable), 0x09 (per-chip mono flags)
- [x] PSG ticking at 1.75 MHz (28 MHz / 16), sample generation via Bresenham accumulator
- [x] CTC (drives some AY timing) — implemented in Phase 5 (4-channel counter/timer, VHDL-verified)
- [x] Create Z88DK test programs for different functions (store them in `demo` directory):
  - [x] YM2149
  - [x] DAC
  - [x] Beeper
- [x] Verify all works ok:
  - [x] YM2149
  - [-] DAC - **deferred**
  - [x] Beeper
- [x] **Milestone**: Music and sound effects audible

### Phase 5 — Peripherals & Full I/O ✓ COMPLETE

- [x] NextREG file (core registers) — raster position reads (0x1E/0x1F), line interrupt (0x22/0x23), interrupt control (0xC0-0xCA), ULA control (0x68), fallback colour (0x4A), DivMMC automap (0xB8-0xBB), Alt ROM (0x8C), reset (0x02), machine type (0x03), sprite attrs (0x35-0x39); read handler mechanism added
- [x] NextREG clip windows (0x18-0x1C) — rotating 4-write cycle for Layer2/Sprite/ULA/Tilemap clip windows; clip control register (read indices / reset); fixed incorrect sprite clip wiring
- [x] NextREG file (remaining registers) — expansion bus (0x80-0x8F), Pi GPIO (0x90-0xA9), Multiface/joystick (0x0A-0x0B) — intentionally stubbed (cached only); no emulation effect, only relevant to physical hardware
- [x] DivMMC — 8K ROM + 128K RAM overlay, auto-map triggers on M1, port 0xE3 control; configurable entry points via NextREG 0xB8-0xBB; VHDL-verified
- [x] DivMMC SD card `.img` mounting — SdCardDevice SPI backend with CMD0/CMD8/CMD17/CMD24/CMD55+ACMD41/CMD58; SDHC byte addressing; `--sd-card` and `--divmmc-rom` CLI options
- [x] UART — dual-channel with 512/64-byte FIFOs, 17-bit prescaler, loopback mode; VHDL-verified
- [x] SPI — byte-level master with SpiDevice virtual interface; VHDL-verified
- [x] I2C — bit-bang protocol decoder + DS1307 RTC using host clock; VHDL-verified
- [x] DMA — Z80-DMA register protocol (14-state FSM) + ZXN burst mode, bus arbitration; VHDL-verified
- [x] CTC — 4-channel counter/timer, VHDL state machine, prescaler 16/256, daisy-chain; VHDL-verified
- [x] IM2 controller (all 14 levels) — wired with CTC, DMA, UART interrupt callbacks
- [x] Z80N extension opcodes — all 26 opcodes implemented (SWAPNIB, MIRROR, TEST, barrel shifts, MUL, ADD rr,A, ADD rr,nn, NEXTREG nn/A, PIXELDN, PIXELAD, SETAE, OUTINB, LDIX, LDDX, LDIRX, LDDRX, LDPIRX, LDIRSCALE, LOOP); VHDL-verified T-states and behavior
- [x] All peripherals integrated into emulator core: port dispatch, IM2 callbacks, DMA bus stall, CTC/UART ticking, DivMMC MMU overlay
- [ ] **Milestone (DEFERRED to v1.1)**: NextZXOS boots from SD image — root cause analyzed (DivMMC/config page conflict), write-recording approach identified; see `doc/nextzxos-boot-investigation.md`

### Phase 6 — Native UI & Usability ✓ COMPLETE

- [x] Qt 6 `QMainWindow` as the main emulator window (replaces raw SDL window) — `src/gui/main_window.h/.cpp`, enabled via `-DENABLE_QT_UI=ON`
- [x] Menu bar: **File** (Load NEX, Mount SD image, Quit), **Machine** (Reset, CPU speed submenu), **View** (2×/3×/4× scaling, Fullscreen, CRT filter), **Help** (About)
- [x] Toolbar: **Reset** + **Load** buttons with standard icons
- [x] Emulator viewport: `EmulatorWidget` (QWidget) with QImage-based ARGB8888 rendering, software pre-scaled nearest-neighbour at exact integer multiples of 320×256
- [x] Fullscreen mode: true fullscreen (chrome hidden) via F11; ESC or F11 to exit; aspect ratio preserved with letterbox black bars at largest integer scale
- [x] Status bar: FPS counter (1s timer), CPU speed (from NextREG 0x07), machine mode label
- [x] CRT scanline filter: semi-transparent dark lines overlay in EmulatorWidget::paintEvent(), toggled from View menu; respects image bounds in fullscreen
- [x] SDL remains for audio output only; keyboard input via Qt key events mapped to SDL scancodes
- [x] Additional restriction: the final emulator file must be linked statically and have no dynamic library dependencies — `STATIC_BUILD` CMake option added; requires static Qt6/SDL2 builds (system packages are dynamic-only)
- [x] Hi-DPI pixel-perfect rendering: DPR-aware widget sizing deferred to first visible frame; software pre-scaling eliminates all QPainter/GPU artifacts
- [x] Window non-resizable; scale changed via View menu (2×/3×/4×) or F2 key cycle
- [x] ESC exits fullscreen in both GUI and SDL builds
- [x] **Milestone**: Native application window with menu bar, toolbar, and fullscreen toggle

### Phase 7 — Debugger Window ✓ COMPLETE

Extends the Phase 6 Qt 6 main window with **dockable debugger panels** providing full introspection into the running emulator. Uses real OS widgets (buttons, toolbars, tables, splitters). Architecture: single-threaded (no mutex needed), dock widgets in existing MainWindow, pause = skip `run_frame()`.

**Infrastructure**
- [x] `DebugState` + `BreakpointSet` in `src/debug/` — pause/resume/step state, PC breakpoints, one-shot breakpoints for step-over/run-to-cursor
- [x] `DebuggerManager` in `src/debugger/` — creates all panels, Debug menu (F5/F9/F10/F11/Shift+F11), debug toolbar
- [x] `QDockWidget` panels in existing `QMainWindow`; each panel independently floatable/resizable
- [x] `ENABLE_DEBUGGER` CMake flag; when OFF the debugger sources are not compiled, zero overhead
- [x] `execute_single_instruction()` method on `Emulator` for single-step operations
- [x] Breakpoint check in `run_frame()` hot loop — breaks mid-frame and pauses

**Debug backend** (`src/debug/`, pure C++)
- [x] Z80 + Z80N disassembler (`disasm.h`) — all prefixes (CB/DD/FD/ED/DDCB/FDCB) + 26 Z80N extensions
- [x] `BreakpointSet` — PC breakpoints, read/write/IO watchpoints, one-shot breakpoints
- [x] `TraceLog` — circular buffer of pre-execution state (10K entries), export to file

**CPU panel** (`QDockWidget`)
- [x] `QGridLayout` showing all Z80 registers live (AF, BC, DE, HL, AF'/BC'/DE'/HL', IX, IY, SP, PC, I, R, IFF1/2, IM)
- [x] Flags row (S, Z, H, PV, N, C) with active highlight
- [x] Halted indicator
- [x] Step controls: Step Into (F11), Step Over (F10), Step Out (Shift+F11), Run to Cursor, Run (F5), Pause (F9)

**Disassembly panel** (`QDockWidget`)
- [x] Custom-painted scrollable disassembly (32 visible lines)
- [x] Current PC row highlighted in yellow
- [x] Click gutter to set/clear breakpoint (red dot)
- [x] "Follow PC" toggle; manual address navigation via address bar
- [x] Context menu: Toggle Breakpoint, Run to Here

**Memory panel** (`QDockWidget`)
- [x] Custom-painted hex editor: address | hex bytes (8+8) | ASCII
- [x] MMU page selector (CPU View + Slot 0-7 with live page numbers)
- [x] Highlighted regions: SP row (orange), VRAM (cyan), attributes (yellow)
- [x] Inline hex editing with auto-advance

**Video panel** (`QDockWidget`)
- [x] Raster position display (vc from NextREG 0x1E/0x1F)
- [x] Layer toggle checkboxes (ULA/Layer2/Tilemap/Sprites)
- [x] Layer priority dropdown (6 modes from NextREG 0x15)
- [x] ULA palette swatch grid (16 colours)

**Sprite panel** (`QDockWidget`)
- [x] QTableWidget 128 rows (Index, X, Y, Pattern, Palette, Visible, Mirror, Rotate)

**Audio panel** (`QDockWidget`)
- [x] AY register table (16 rows × 3 chips, hex values)
- [x] Source mute checkboxes (AY#0/1/2, DAC, Beeper)
- [x] TurboSound/AY-YM/stereo mode info labels

**Copper panel** (`QDockWidget`)
- [x] Decoded instruction table (WAIT/MOVE/NOP/HALT) centered on current PC
- [x] Current PC highlighted
- [x] Running state and mode display

**NextREG panel** (`QDockWidget`)
- [x] 256-row table (address, name, hex value, binary value)
- [x] Editable hex value column (commits via `nextreg().write()`)
- [x] ~70 known register names

**Trace log**
- [x] Debug menu: Enable Trace (checkable), Clear Trace, Export Trace...
- [x] Circular buffer recording pre-execution state per instruction
- [x] Export to text file

**Build integration**
- [x] `src/debug/CMakeLists.txt` — pure C++ static lib
- [x] `src/debugger/CMakeLists.txt` — Qt6::Widgets, AUTOMOC ON
- [x] All 3 build configs verified: Qt+Debugger, Qt-only, SDL-only
- [x] **Milestone**: Can set a breakpoint, single-step through ROM, inspect and edit any memory page and any NextREG live, with all panels dockable

### Phase 7.5 - Debugger enhancements

- [x] Remove all references to Debugger in main emulator window. Just leave a "Debug" button with an icon of a Bug, and a "Debugger" option in the "View" menu. The Debug button and the Debugger option open the full debugger window.
- [x] When running freely (i.e. not step by step), the Disassembler panel should not follow PC. It should be grayed out and just don't update. The ASM panel should only follow PC when running in step mode (for efficiency reasons)
- [x] The debugger should be able to load a MAP file (e.g. from Z88DK), and extract a symbol table from it. The ASM panel then should show the symbolic name instead of the symbol address, when showing an instruction with an immediate 16-bit value matching one of the symbols in the symbol table.
- [x] The debugger window must have its own menu bar and title. Debugger options should be moved to this bar.
- [x] Additional panel: Watches. It should be at the bottom of the debugger window. The memory panel is quite wide. Make the memory panel use only half the window width, and leave the other half for the Watch panel.
- [x] Defined watches should allow showing the byte (8bit), word (16bit), long(32bit) at the watch adddress.
- [x] Adittional menu options:
  - [x] Map -> Load MAP file -> Z88DK format (for the moment, more formats in the future)
  - [x] Watches -> Add watch -> Ask for address/symbol from the Map file if there is a match
  - [x] In the ASM panel:
    - [x] A symbol should allow to set a watch on its address
    - [x] A 16-bit immediate should allow to set a watch on the address it shows
    - [x] A 16-bit register should allow to set a watch on the address it currently contains
- [x] When running in "Run to here" mode, the ASm panel should also not be updated for performance reasons
- [x] Add Data breakpoints:
  - [x] Enhance break points to be Read/Write/Execute (execute being the regular code breakpoints)
  - [x] Add options in the context menu for Data breakpoints for the same cases as for Watches (16-bit immediates or 16-bit register contents)
  - [x] Add a  Breakpoints submenu at the top menubar and also make the breakpoint options available there

### Phase 7.8 — Polish & Accuracy

- [x] Machine-type selection: 48K / 128K / +3 / Pentagon / Next via `--machine-type` CLI + Qt menu; ROMs from `--roms-directory` (default `/usr/share/fuse`); fixed port 0x7FFD decode mask; +3 port 0x1FFD paging with 4-ROM selection
- [x] Add a Keyboard mapping from PC cursors -> ZX cursors (arrow keys → Caps Shift + 5/6/7/8 via compound key table)
- [x] Run FUSE Z80 opcode test suite: 1340/1356 pass (98.8%); 16 failures are undocumented Z80 behaviors (BIT n,(HL) YF flag, SCF flags 3/5, DD/FD prefix chains, DJNZ loop test)
- [x] Create test programs for Next features (Z88dk NEX): floating bus, L2 320x256, L2 640x256, sprite scaling — baseline verified matches ZesarUX
- [x] Implement new features:
  - [x] Floating bus emulation (48K/128K modes only — returns pixel/attribute bytes based on ULA fetch timing within each 8T cycle)
  - [x] Pentagon timing mode (448 ticks/line, 320 lines, zero contention, Pentagon ROMs 128p-0/1)
  - [x] Wire contention delays into CPU memory reads — per-access contention via callback in FUSE Z80 readbyte/writebyte; 128K contended bank tracking (banks 1,3,5,7 at 0xC000)
  - [x] Contention for all modes: 48K/128K/+3 (standard pattern), Pentagon/Next (zero contention) — per-slot flags, +3 wider window (VHDL hc_adj[3:1]==0), +3 banks>=4 rule, +3 special paging support
  - [x] Layer 2 320×256 and 640×256 modes — column-major addressing (x*256+y), 320x256@8bpp, 640x256@4bpp (2px/byte), NextREG 0x69 Layer 2 enable, default RRRGGGBB palette init
  - [x] Sprite scaling ×2/×4/×8 — per-sprite X/Y via extended byte 4, non-over-border clip fix (clip_y2 default 0xBF matching VHDL)
  - [x] Sprite anchoring — anchor/relative composite sprites (type 0 + type 1), offset rotation/mirror/scale, pattern & palette offset modes
- [x] File format loading: TAP
  - [x] Custom TAP parser (no external library) — block parsing, checksum verification, header logging
  - [x] Fast TAP loading via ROM LD-BYTES trap at 0x0556 (48K ROM)
  - [x] Auto-type LOAD "" via keyboard matrix injection with debounce-safe timing
  - [x] CLI support: `--load file.tap` with 100-frame boot delay for BASIC initialization
  - [x] Qt GUI: file dialog accepts .tap files alongside .nex
  - [x] Regression test: tap-demo (15 tests total, all passing)
  - [x] TAPE menu with controls: Open, Eject, Rewind, Fast Load toggle
  - [x] Real-time tape loading via EAR bit simulation (--tape-realtime CLI flag, Tape menu toggle)
  - [x] Tape EAR input routed to audio mixer — loading sounds audible during real-time playback
  - [x] Per-scanline border colour updates — authentic red/cyan pilot and yellow/blue data stripes
- [x] File format loading: TZX
  - [x] Uses ZOT library by antirez (MIT license, third_party/zot/) for TZX parsing and real-time playback
  - [x] Supports blocks: 0x10 (standard), 0x11 (turbo), 0x12-0x15 (pure tone/pulse/data/direct), 0x20-0x25 (pause/groups/loops), 0x2A/0x2B (stop/signal), 0x30/0x32 (metadata)
  - [x] Fast-load mode via ROM trap at 0x0556 (extracts standard/turbo data blocks)
  - [x] Real-time playback with EAR bit + audio (same --tape-realtime flag as TAP)
  - [x] GUI: file dialogs accept .tzx, Tape menu works with TZX files
  - [x] CLI: --load file.tzx with auto-detection by extension
- [ ] File format loading: WAV
- [ ] File format loading: SNA, SZX - These should be easy, they are similar to NEX
- [x] Automated regression test suite — `--headless` mode, `demo/Makefile` (NEX+TAP), `test/regression.sh` with 12 screenshot + FUSE Z80 tests, reference image generation
- [ ] Performance profiling and optimization

### Phase 8  More enhancements

- [ ] Emulator:
  - [ ] Magic Breakpoint: a special instruction that when run in real hardware or emulators that do not support it, it runs a NOP. But if hit in the emulator (and MAGIC BP is active), is triggers a break to the debugger (equivalent to INT3 on Intel x86) - See @MAGIC-BREAKPOINT.md document for description. Support both magic-bp opcodes (ZesarUX and CSpect)
  - [ ] Save video with/without audio - Format: MP4+H264, using FFMPEG - See @VIDEO-RECORDING.md document for description.
  - [ ] RZX file input and playback (Open RZX file, Start Playing, Pause, Stop Playing)
  - [ ] RZX recording to file (Record to RZX file, Start Recording, Pause Recording, Stop Recording)
  - [ ] Magic Port:
    - [ ] A "magic" 16-bit port that can be used for debugging: each time an out is made to that port, the byte sent is traced in STDERR
    - [ ] Port can be selected at will, ideally one that is not normally used should be selected
    - [ ] Magic Port mode: ascii,hex,dec,line. "line" mode is special: output is buffered unti a CR or LF is sent, then a whole line is sent to STDERR

- [ ] Debugger:
  - [ ] Use tabbed panels for everything
  - [ ] Registers panel:
    - [ ] Convert to tabbed panel
    - [ ] Add IFF1/IFF2/R registers, IM Mode, ULA active screen (bank 5/7) 
    - [ ] Add a NEXTREGS panel
  - [ ] Add a MMU panel: current bank layout (128 mode, banks 0-3), current page layout (Next MMU mode, pages 0-7)
  - [ ] Video panel:
    - [ ] add a ULA screen panel: two screens with contents of both screens (normal, shadow), with current frame video contents updated to the scanline, red line indicating the current scanline
    - [ ] add a Layer2 screen panel: two screens with contents of both Layer2 screens (normal, shadow) updated to the scanline and red line indicating the current scanline
    - [ ] add a Sprite screen panel: contents of Sprite rendering updated to the scanline and red line indicating the current scanline
    - [ ] add a TileMap screen panel: contents of TileMap rendering updated to the scanline and red line indicating the current scanline
  - [ ] Call stack panel:
    - [ ] Monitors CALL, RST instructions
    - [ ] Monitors Interrupts
    - [ ] Integrates with symbol list
    - [ ] Shows list of nested functions calls, top->down (recent->older)
  - [ ] Stack panel:
    - [ ] View of 16-bit values, top->down: address (hex), 16-bit value hex+dec, Hibyte value hex+dec, Lobyte value hex+dec
    - [ ] Potentially integrated with Call Stack panel?
  - [ ] Control buttons: 
    - [ ] New button "Run to EOF" (popup hint with "Run to End of Frame")
    - [ ] New button "-> Run to EOSL" (popup hint with "Run to End of Scan Line")
  - [ ] Source level debugging with Z88DK .LIS files (assess independently - potentially complex)
  - [ ] Backwards execution: Have a circular buffer that stores the complete CPU state and memory changes (including the bank) up to a maximum size, and allow "rewinding" up to a certain number of instructions. Should be toggleable from the debugger and via command line, with a configurable maximum number of instructions.

- [ ] General UI:
  - [ ] Emulator speed: text (manually input %), plus 0.5x,1x,2x,4x
  - [ ] Save PNG screenshot
  - [ ] CLI: add options for all the previous functionalities where it makes sense:
    - --magic-port-enable
    - --magic-port-mode
    - --magic-breakpoint-enable
    - --rewind-buffer-size <num-instructions>
    - --rzx-play <rzx-file>
    - --rzx-record <rzx-file>
    - --record-video <output-file>
    - --record-video-with-audio <output-file>
- [ ] **Milestone**: v0.9 release (NEX loading, 48K/128K/+3 BASIC, debugger, all video/audio)

### Phase 9 - CI, Quality and Release

- [ ] Generate full testing plan:
  - [ ] Unit test plan, per module
  - [ ] Integration test plan, between modules
  - [ ] Functional test plan (~demos)
- [ ] CI golden-output visual regression tests
- [ ] General code refactor and tidy up (/simplify)
- [ ] Replacement of magic number with named constants where possible
- [ ] Global analysis of code, module by module, ensure alignment with VHDL source, document each module for easy reference for CLAUDE
- [ ] Generation of Windows version
- [ ] Generation of MacOS version
- [ ] Documentation
  - [ ] Update README for repo and source code users
  - [ ] Create DEVELOPMENT documentation
  - [ ] Create USAGE document and man page for users
- [ ] Create static executables by downloading QT and SDL sources and building them
- [ ] Create exhaustive CI plan for Github and automated release

### Phase 10 — NextZXOS Boot (v1.1)

- [ ] Evaluate other "shortcut" options: divMMC ROM interception similar to TAP loading (CSpect does this?)
- [ ] Implement config page write recording + soft_reset replay approach
- [ ] Reset DivMMC automap state properly during soft_reset
- [ ] Verify replayed ROM data correctness
- [ ] Handle ULA screen RAM bank overlap during replay
- [ ] End-to-end test with screenshot comparison against reference image
- [ ] **Milestone**: v1.0 release (NextZXOS boots from SD image, NEX loading, 48K/128K/+3 BASIC, debugger, all video/audio) - Lots of bugs ironed out. 

### Phase 11 - New functions

- [ ] Z80 file format loading
- [ ] SZX file format loading
- [ ] DSK file format loading - Emulation of disk controller?

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

## 11. Pending Issues

Issues deferred to post-release for further debugging.

| Issue | Detail | Status |
|-------|--------|--------|
| **DAC audio output buzzing** | Soundrive DAC demo produces a continuous buzz alongside the expected tones, even with interrupts disabled, 28 MHz CPU speed, and a pure-assembly playback loop with deterministic timing. Tested on both JNext and ZEsarUX with the same result. Needs investigation into whether the issue is in the demo code, the emulator's DAC/mixer sampling, or fundamental to software-driven DAC playback without hardware-level sample timing. DAC is rarely used by Next software, so this is low priority. | Open |
| **Debugger window sticky positioning** | Debug sticky positioning of debugger window to emulator window. The debugger window should stay attached to the right side of the emulator window and move together when dragged. Currently the debugger window position is saved/restored via QSettings but does not track the emulator window in real time. | Open |
| **TZX Direct Recording (DeciLoad)** | TZX 0x15 blocks with DeciLoad 12k8 turbo format (77 T-states/sample) fail in real-time mode. BASIC loader loads fine but custom loader can't read direct recording data. FUSE handles the same file correctly. See [doc/DECILOAD-TZX-LOADING.md](DECILOAD-TZX-LOADING.md) for full investigation. Test file: `test/tzx/Xevious_ZX0_DeciLoad12k8.tzx`. | Open |

---

*This document is the living design specification. Sections will be refined as implementation proceeds. All timing constants and register definitions should be cross-checked against the FPGA source before coding.*
