# 1.2 Repository layout

The point of this page is to turn a symptom into a path. A bug report about
sprite scaling, a feature request for a new tape format, a failing screenshot —
each of them lives somewhere specific, and the directory tree is regular enough
that knowing the rule is usually faster than grepping.

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
| `.claude/` | The AI-assisted development apparatus: reusable agent definitions, task recipes, git hooks that enforce the branch rules, and process notes. Not part of the product, but it is where the project's conventions are made executable. |
| `.prompts/` | The daily task files, one per working day, kept as part of the project's record. Not part of the product. |

The root also carries `CMakeLists.txt`, a large self-documenting `Makefile`
(run `make` with no target and it lists every target it has), `version.yaml`
(the single source of truth for the version), `releases.yaml` (the allowlist
that gates public GitHub releases), `CLAUDE.md` (the project conventions), and
the two mkdocs configs — `mkdocs.yml` for the user guide, `mkdocs-devguide.yml`
for this one.

## Inside `src/`

Most of these directories hold an emulated subsystem, and
[chapter 3](../03-subsystems/index.md) opens with a table mapping each one back
to the VHDL it was derived from. The rest — the frontends, the debugger UI, the
profiler — surround the emulation rather than being part of it.

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

Three of those names describe features rather than hardware, and are worth
knowing before you meet them in the code.

The **phantom typist** in `src/input/` is what makes `--load game.tap` just
work. Starting a tape means typing something first — `LOAD ""` at a 48K BASIC
prompt, `ENTER` on the 128K and +3 loader menus — and the moment that prompt
becomes ready differs by machine. Rather than guessing with a fixed delay, the
typist watches the guest scanning its keyboard and starts typing once the ROM's
input loop is demonstrably running.

**`AttributeMux`** in `src/memory/` exists for multicolour demos. The ULA
re-reads a character cell's attribute byte on every one of its eight scanlines,
so a program that rewrites attributes between those reads gets eight different
colours down a single cell — the technique Nirvana-class engines are built on.
Since JNEXT composites the frame at the end, those writes have to be logged and
replayed per scanline to survive at all.

The **rewind ring buffer** in `src/debug/` is the machinery behind the
debugger's backwards execution: step back one instruction, jump back a frame,
or drag a slider to a frame in recent history. It works by snapshotting the
entire machine at every frame boundary into a ring of fixed-size slots, then
restoring the nearest snapshot and replaying forward to the point you asked
for. That is why the state-serialisation contract in `core/saveable.h` reaches
into every subsystem: a rewind is only ever as faithful as the least complete
`save_state()` in the machine.

## Which directories may see SDL and Qt

There is one architectural boundary in this tree that is worth stating
explicitly, because breaking it is easy and the consequence arrives late: **the
emulation core knows about neither SDL nor Qt**. `src/core`, `src/cpu`,
`src/memory`, `src/video`, `src/audio`, `src/port` and `src/peripheral` contain
no `#include` of either — only prose comments naming them. That is precisely
what makes the same `Emulator` object usable from an SDL window, from a Qt
widget and from a headless loop with no display at all, and therefore what
makes the unit suites and the regression suite possible.

Qt is confined to `src/gui` and `src/debugger`. SDL is owned by `src/platform`
— and, to be honest about it, by four headers in `src/input`: `keyboard.h`,
`gamepad_host.h`, `joystick_dispatcher.h` and `mouse_dispatcher.h` all include
`<SDL2/SDL.h>`. The keyboard case is structural rather than accidental.
`Keyboard::set_key()` takes an `SDL_Scancode`, which makes SDL's scancode enum
the project's vocabulary for host keys; the Qt frontend translates its own key
events into that vocabulary rather than introducing a second one. Traffic in
the other direction stays clean: callbacks such as `on_input_state_restored`
and `on_joystick_source_changed` carry no SDL or Qt type in their signature.
