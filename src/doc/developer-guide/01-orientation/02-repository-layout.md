# 1.2 Repository layout

## Top level

| Path | What it is |
|---|---|
| `src/` | All emulator source. One subdirectory per subsystem — see below. |
| `test/` | Every test: unit suites (one directory per subsystem), the regression suite under `test/00regression/`, the declared-suite manifests, the harness and its self-test, and the traceability tooling. |
| `doc/` | Documentation. `doc/design/` holds design plans (including the roadmap), `doc/testing/` the per-subsystem test plans and the generated traceability matrix, `doc/man/` the man-page source, `doc/user-guide/` and `doc/developer-guide/` the **rendered and committed** guides. |
| `demo/` | z88dk test programs, one directory per feature (`copper_demo`, `tilemap_demo`, `lores_demo`, …), built to `.nex`/`.tap` by `demo/Makefile`. Most regression screenshots are of these. |
| `packaging/` | Everything that turns a build into a package: `rpm/`, `debian/`, `flatpak/`, `macos/`, `windows/`, the AppStream metadata in `assets/`, and `sync-version.sh`, the one script that knows every file hard-coding the version. |
| `third_party/` | Vendored dependencies: `fuse-z80/` (the Z80 core, GPLv2-or-later), `spdlog/` (logging, submodule), `zot/` (TZX playback, MIT), `fatfs/` (FAT filesystem, used for SD-image work). |
| `tools/` | Developer utilities that are not part of the build: SD-image fixers, CSpect/DZRP differential-debugging helpers, profiler post-processing, the user-guide server. |
| `roms/` | `nextboot.rom` and nothing else. The 8 KB FPGA boot ROM is embedded into the binary at build time; every other ROM is read out of the SD-card image at runtime. Git-ignored beyond that file. |
| `cmake/` | `GenerateVersion.cmake` — derives the version from `version.yaml`. |
| `data/` | `48rom.map`, a symbol map for the 48K ROM used when disassembling. |
| `docker/` | Dockerfiles for the Linux and Windows cross-builds. |
| `.github/` | CI and release workflows, plus the issue templates. |
| `.prompts/` | Daily working notes. Not part of the product. |

The root also carries `CMakeLists.txt`, a large self-documenting `Makefile`
(run `make` with no target to list every target), `version.yaml` (the single
source of truth for the version), `releases.yaml` (the allowlist gating public
GitHub releases), and the two mkdocs configs — `mkdocs.yml` for the user guide,
`mkdocs-devguide.yml` for this one.

## Inside `src/`

| Directory | Responsibility |
|---|---|
| `core/` | `Emulator` — the top-level machine that owns every subsystem — plus `Clock`, `Scheduler`, `EmulatorConfig`, the CLI option table (`cli_options.h`), the logging wrapper (`log.h`), the state-serialisation primitives (`saveable.h`), all the file loaders and savers (NEX, SNA, SZX, Z80, TAP, TZX, WAV, RZX), the host-side FAT32 reader that extracts ROMs from the SD image, the SD-card provisioner, and the video recorder. |
| `cpu/` | `Z80Cpu`, the wrapper around the vendored FUSE core; the Z80N extension opcodes (`z80n_ext` — 31 of them, not the 26 the roadmap still says); and the IM2 interrupt controller with its client mixin. |
| `memory/` | `Mmu` (8 × 8 K slots, the `MemoryInterface` implementation), `Ram`, `Rom`, the `ContentionModel`, and `AttributeMux` — the per-scanline replay of mid-frame attribute writes. |
| `video/` | The layers and the compositor: `Ula`, `Lores`, `Layer2`, `Tilemap`, `SpriteEngine`, `PaletteManager`, `VideoTiming` (raster counters), and `Renderer`, which composites them. |
| `audio/` | `AyChip` and the `TurboSound` triple wrapper, `Dac`, `Beeper`, `I2s`, and the `Mixer` that sums them; plus the WAV recorder and the DAC trace recorder. |
| `port/` | `PortDispatch` (mask/value I/O decode) and `NextReg`, the NextREG register file. |
| `peripheral/` | Copper, CTC, DMA, DivMMC, Multiface, the NMI source pipeline, SPI, the SD card device, I²C with its DS1307 RTC, the UART, and the adapters that bind the emulated ESP-01 to UART 0. |
| `input/` | The ZX keyboard matrix, joysticks, Kempston mouse, MD6 connector, membrane stick, NR 0x0B I/O mode, the F-key state machine, the phantom typist that types `LOAD ""` for you, and the host-side joystick/mouse dispatchers. |
| `platform/` | The SDL frontend (`sdl_app`, `sdl_display`, `sdl_audio`, `sdl_input`), the headless frontend (`headless_app`), PNG screenshots, the cold-boot helper, and the small header-only policies for frame pacing and render skipping. |
| `gui/` | The Qt 6 frontend: `QtApp`, `MainWindow`, `EmulatorWidget`, the preferences dialog and the saved-configuration store. |
| `debugger/` | The Qt 6 debugger window and its panels — CPU, disassembly, memory, MMU, stack, call stack, watches, breakpoints, video, sprites, copper, NextREG, audio. Compiled only when `ENABLE_DEBUGGER=ON`. |
| `debug/` | The debugger *backend*, pure C++ with no GUI dependency: disassembler, breakpoint set, `DebugState`, trace log, call stack, symbol table, and the rewind ring buffer. |
| `esp01/` | The emulated ESP-01 WiFi module — AT-command engine, socket layer, worker thread — built as its own library with its own tests. |
| `profiler/` | The per-physical-address T-state profiler behind `--profile`. |
| `doc/` | Markdown sources for the user guide and this developer guide, plus the Graphviz diagram sources. |
| `save/` | A CMake target that currently has no sources; the serialisation primitives it was meant to hold live in `core/saveable.h`. |

## Which directories may see SDL and Qt

The rule the code actually keeps is that **the emulation core knows about
neither**. `src/core`, `src/cpu`, `src/memory`, `src/video`, `src/audio`,
`src/port` and `src/peripheral` contain no `#include` of SDL or Qt at all —
only prose comments naming them. That is what makes the same `Emulator` usable
from a window, from a Qt widget and from a headless loop.

Qt is confined to `src/gui` and `src/debugger`. SDL is owned by
`src/platform` — and, honestly, by four headers in `src/input`:
`keyboard.h`, `gamepad_host.h`, `joystick_dispatcher.h` and
`mouse_dispatcher.h` all include `<SDL2/SDL.h>`. The keyboard case is
structural rather than incidental: `Keyboard::set_key()` takes an
`SDL_Scancode`, so SDL's scancode enum is the project's host-key vocabulary
and the Qt frontend translates its own key events into it. Callbacks crossing
back the other way — `on_input_state_restored`, `on_joystick_source_changed` —
carry no SDL or Qt type in their signature.
