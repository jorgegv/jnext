# 5.6 The browser build

JNEXT also runs in a browser tab, built for WebAssembly. This page is the entry
point to everything that builds, tests and ships it. What the page's author sees
is in the user guide, chapter 8; this page is for the person who changes the
emulator.

## What it is

The browser build is the SDL-only configuration (no Qt, no debugger GUI) compiled
by a pinned Emscripten SDK. One tree, `build/wasm-core`, links two modules:

- `jnext` (`jnext.js` and `jnext.wasm`): the node build. `test/wasm-core-check.sh`
  (WCORE-01 to WCORE-12) and `make regression-wasm` run it under node. The unit suites
  are separate modules, built in `build/wasm-unit-test`.
- `jnext_web` (`jnext-<version>.js` and `.wasm`): the build a page loads, with the
  example pages (`plain.html`, `itch-host.html`, `itch-game.html`, `widget.html`) and
  the ready-made widget (`jnext-widget.js`) configured beside it.

Two preprocessor symbols mark browser-only code. `__EMSCRIPTEN__` is defined in
both modules. `JNEXT_BROWSER` is a compile definition of `jnext_web` alone: it marks
what only a page needs (the `requestAnimationFrame` loop, the page's refusals). The
two trees and the way they are built are in [5.2](02-build-configurations.md); the
minimal SD-card image the browser tests use is in [5.3](03-packaging-and-release.md).

## The CMake arms

Each row is one arm that tests for Emscripten, named as it is written. The list is
the output of `git grep -n EMSCRIPTEN` over the CMake files.

| file | condition | what it does |
|---|---|---|
| `CMakeLists.txt` | `elseif(EMSCRIPTEN)` in the target-OS chain | `JNEXT_TARGET_OS` is `emscripten`, which the unit-test harness reads for `# os:`. |
| `CMakeLists.txt` | `if(EMSCRIPTEN)` after the Windows API floor | Refuses `ENABLE_QT_UI` and `ENABLE_DEBUGGER` with a `FATAL_ERROR`; adds `-fwasm-exceptions` to every translation unit and the link; adds `-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=2MB`. |
| `CMakeLists.txt` | `if(EMSCRIPTEN)` before `find_package(ZLIB)` | Defines `ZLIB::ZLIB` and `PNG::PNG` as INTERFACE targets carrying `-sUSE_ZLIB=1` and `-sUSE_LIBPNG=1`: the sysroot's default libpng is the SJLJ variant, which does not link with wasm exceptions. |
| `CMakeLists.txt` | `if(NOT WIN32 AND NOT EMSCRIPTEN)` | Looks for CURL and OpenSSL only on POSIX hosts: the browser build links neither. |
| `CMakeLists.txt` | `if(EMSCRIPTEN)` after the `jnext` target | The node `jnext` links with `-sNODERAWFS=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1`. |
| `CMakeLists.txt` | `if(EMSCRIPTEN)` around `jnext_web` | Defines the `jnext_web` target: `main.cpp`, `browser_frontend.cpp` and `web/web_controller.cpp`; `OUTPUT_NAME` `jnext-<version>`; `JNEXT_BROWSER=1`; the web link options (`-sENVIRONMENT=web`, `-sMODULARIZE=1`, `-sEXPORT_NAME=createJnextModule`, `-sINVOKE_RUN=0`, `-sEXIT_RUNTIME=0`, the exported runtime methods and functions, `-lidbfs.js`); the three `--extern-post-js` files, `web/jnext-image.js`, `web/jnext-touch-keyboard.js`, then the configured `jnext_api.js`; and a `configure_file` of `jnext_api.js.in`, of each example page and of `jnext-widget.js.in`. |
| `src/core/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` around `find_package(PNG)` | No libpng package search: the port target above stands in. |
| `src/core/CMakeLists.txt` | `elseif(EMSCRIPTEN)` in the network-library choice | No network library: `sdcard_provisioner_net_web.cpp` is self-contained (a portable SHA-256, no download). |
| `src/platform/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` | The sources `browser_crt.cpp` and `crtemu_browser_impl.cpp` (the CRT effect) are left out of every other build. `jnext_platform` links `jnext_remote` and `jnext_script` in every build: the debugger protocol servers and the script library are compiled into the browser build (the script host is not, by source arms below). |
| `src/esp01/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` (twice) and `if(NOT WIN32 AND NOT EMSCRIPTEN)` (twice) | The ESP-01 suites (`esp_at_test`, `esp_socket_test`, `esp_sigpipe_test`) and their `add_test` lines are not built. |
| `test/CMakeLists.txt` | `if(EMSCRIPTEN)` | The test executables link with `-sNODERAWFS=1 -sENVIRONMENT=node`, an 8 MB stack, `-sSTACK_OVERFLOW_CHECK=2`, `-sEXIT_RUNTIME=1`, and at `-O1`. |
| `test/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` and `if(NOT WIN32 AND NOT EMSCRIPTEN)`, many | The suites whose subject the browser build compiles out or cannot run (processes, threads, sockets). One arm each; they are declared `# os: native` (or `posix`, `linux`) in `test/unit-tests.conf`, see [4.2](../04-testing/02-declared-suites-and-pinned-counts.md). |

Source files carry the same two symbols. The list below is the output of
`git grep -c '__EMSCRIPTEN__\|JNEXT_BROWSER' -- src` and what each arm does:

- `src/main.cpp`: the refusals (`cli::browser_refusal`, `config_keys::browser_refusal`,
  and for `jnext_web` `browser_page.h`); `--debug-channel`, refused by every build but `jnext_web` (`cli::page_only_refusal`); no NextPi; the SD-card download declined
  without reading stdin; extraction of a zipped `--sdcard` and its removal at exit; a `.zip` page file, accepted only when its program is a NEX (extracted, with its directory, before the program-format refusal would apply, which a refused archive still prints); handing the run to `browser_frontend::run`
  and keeping the application and the logging alive after `main()` returns.
- `src/platform/sdl_app.cpp` and `sdl_app.h`: no script host; the stepped warm-start
  recording in `SdlApp::tick()` with its progress report; `release_mouse_capture()`, for
  when the browser ends the pointer lock itself; `take_audio_fill_stats()`, the bench's
  audio fill-events seam.
- `src/platform/headless_app.cpp` and `headless_app.h`: no script host.
- `src/platform/sdl_display.cpp` and `sdl_display.h`: the canvas is created as an OpenGL window and the
  page owns its size (window scale does nothing); `set_crt()` and the CRT draw (below).
- `src/platform/browser_crt.cpp`, `browser_crt.h` and `crtemu_browser_impl.cpp`: the CRT
  effect, whole files.
- `src/platform/debug_servers.cpp` and `debug_servers.h`: the three socket servers (`--dzrp-port`, `--gdb-port`, `--zrcp-port`) are neither declared nor started, so `esp::make_socket_listener` is never referenced from this file in the browser build (the symbol is still compiled in, through esp01 and `remote::Server::open(address, port)`); the three channel servers are built in every build; and `frame_loop_budget()` returns `PumpBudget{0, 0, 10}` for a paused machine with a client attached, where the native build waits 2 ms for the next command.
- `src/platform/web/web_controller.cpp`: the exports of the page's debug channels, below.
- `src/core/nextpi_provisioner.cpp`: compiled out.
- `src/core/video_recorder.cpp`: no process to run ffmpeg, so the call fails as a
  missing ffmpeg does.
- `src/core/sdcard_provisioner_net_web.cpp` and `sdcard_provisioner_net_curl.cpp`: the
  two halves of the provisioner's network and hash backend, each whole-file
  conditional.

`src/platform/browser_page.h` has no arm of its own: `main.cpp` includes it only under
`JNEXT_BROWSER`, and it holds the page's own refusals (`--headless`, `--rzx-record`, program
formats).

## At run time

The browser frontend is a second target of the same tree, `jnext_web`: the same
`main.cpp`, compiled with `JNEXT_BROWSER`, plus `browser_frontend.cpp`, linked
for the web only (`-sENVIRONMENT=web`, a modularised factory named
`createJnextModule`, no automatic `main`, the runtime kept alive by the loop).
The page creates the module, copies its files into the in-memory file system
(`FS`), sets the SDL hints in `ENV` and calls `callMain` with the same argument
vector the command line takes.

*The loop.* `main()` parses, provisions and configures, then hands the run to
`browser_frontend::run`, which installs a `requestAnimationFrame` loop
(`emscripten_set_main_loop_arg` with fps 0 and `simulate_infinite_loop` 0) and
returns, so `main()` returns normally and the loop keeps the runtime alive. The
`simulate_infinite_loop` form was measured and rejected: it unwinds `main()`
with a JavaScript exception, C++ cleanup pads run `main()`'s destructors on it
under wasm exceptions, and the page then crashes in the next frame. Everything
that outlives `main()` is therefore not a local of it: the application is
released to the loop, the two recorders are `static` (the emulator's callbacks
hold references to them; they are never destroyed), logging is not shut down
while the loop lives (`spdlog::shutdown()` drops every registered logger, and one
first used inside the loop would be built afresh, without its `--log-level`
and, for the peripheral loggers that fall back to `spdlog::stderr_color_mt`,
without `--log-file`), and the post-run tail of a run (recorders, `shutdown()`,
the exit status) is a function, `finish`, that the loop calls once a tick ends
the run. Its result reaches the page through `Module.onExit`. A `callMain` that
returns with no loop installed (a refused flag) has no `onExit`: its return
value is the status.

*The JavaScript API.* `jnext.mount()` is `web/jnext_api.js.in`, configured with
the version and linked into `jnext-<version>.js` as a third `--extern-post-js`,
after `web/jnext-image.js` and `web/jnext-touch-keyboard.js` (which it calls). It makes one module instance per
mount: the core keeps process-wide state, so one instance holds one emulator. It
knows no flag spelling. The whole options object goes to `jnext_web_argv`, which
runs `web_api::build_argv` over the `web_api::WEB_OPTIONS` table: each row names a
`cli::OptId` or a `config_keys::KeyId`, so a renamed flag is a compile error, and
the man page's browser section is diffed against the table and the refusal
functions by `web_api_test` (run by `make cli-check`). The controller
(`web_controller.cpp`, a source of `jnext_web` only) holds the exported calls
(`jnext_web_pause`, `jnext_web_reset`, `jnext_web_load`, `jnext_web_save_snapshot`,
`jnext_web_nmi` (the Multiface NMI, `player.nmi()`; refused under `--no-sdcard`, which has no ROM to service it), `jnext_web_rzx_record`, `jnext_web_rzx_stop`, `jnext_web_set_volume`, `jnext_web_quit`) and, through `SdlApp::set_on_load_applied` and `after_tick`, tells
the page when the program is in the machine and a frame has followed (the `ready`
event, `Module.jnextEvent`). `jnext_web_load_applied`, `jnext_web_gain_db`,
`jnext_web_frame_period_ms` and `jnext_web_cpu_mhz` (the frame period and the
effective CPU speed, read by the browser bench) are test seams, not API. Pause is the browser loop's pause (`emscripten_pause_main_loop`),
because the backend's pause needs an armed machine, which costs a check per
instruction. Several players on a page share one IndexedDB database for the
warm-start recording.

*The debugger over a page channel.* A page cannot listen, so the protocol servers
(`DzrpServer`, `ZrcpServer`, `GdbServer`) run over a second `esp::EspListener`, the
`remote::PipeChannel` of `src/remote/pipe_transport.*` ([3.10.1](../03-subsystems/10-1-the-socket-transport.md)),
which the host feeds instead of a kernel. `--debug-channel PROTOCOL` (the page option `debug` emits it)
sets `EmulatorConfig::{dzrp,zrcp,gdb}_channel`, and `DebugServers::start` builds the server over a
channel; `DebugServers::channel(p)` hands the channel to the controller. Nothing pumps on data
arrival: the page's `requestAnimationFrame` tick pumps `Debugger::pump()` once per tick like every loop
owner, so the debugger is served on the frame loop, and a paused player (`emscripten_pause_main_loop`)
or a hidden tab serves nothing until frames resume; the bytes wait in the channel. While wasm runs no
byte can arrive (the page's events wait), so a paused machine's budget has no 2 ms drain: `{0, 0, 10}`
still answers every command already buffered, one pass each, and is bounded by 10 ms.
The exports `jnext_web_debug_open`, `jnext_web_debug_send` and `jnext_web_debug_close` take the
protocol number (`cli::DebugProtocol`: 0 dzrp, 1 zrcp, 2 gdb) and return the `PipeResult` (0 ok, 1 no
channel or no machine, 2 wrong state, 3 input full); the page calls them between two ticks. Output
travels the other way by pull: after each tick `web_controller::after_tick` takes each channel's
output and then its end, and calls the `EM_JS` `jnext_web_debug_out`, which copies the bytes
(`HEAPU8.slice`) and calls `Module.jnextDebug`; calling JavaScript from inside
`EspTransport::send()` was rejected because it re-enters the module while `Server::push()` is on the
stack. `jnext_api.js` delivers `onData` and `onClose` from a microtask, so a page handler never runs
inside a wasm call. A `ws` entry is a link kept by `jnext_api.js` over a channel (messages `open`,
`close` and binary bytes, redialled once a second); the bridge, `tools/web-debug-bridge/jnext-debug-bridge.mjs`,
is plain node with no dependency (the user guide's 8.6 documents its use). `TcpRelay`, its TCP half, admits no new client after a client left until the host confirms the end of that session (`release()`, or 3 s), so the old session's last bytes cannot reach the next client.

*Size.* The debugger (the three protocol servers, the pipe transport and the script library ZRCP compiles its conditions with) adds about 705 KiB to the shipped `.wasm`, 12.7% of it (5693287 to 6415005 bytes), or about 250 KiB compressed with `gzip -9` (1812629 to 2068256 bytes, 14.1%), and about 8 KiB to the `.js` (267679 to 276198 bytes; about 2 KiB compressed, 71548 to 73734 bytes). Measured at version 1.3.41, one Release LTO `wasm-core` build of the tree before the debugger and one of the same tree with it, same version string length. There is no separate debug build: the code does nothing unless a page sets `debug`.

*Pacing.* `SdlApp::tick()` never sleeps; it reports the sleep it wants before the
next tick. A `requestAnimationFrame` callback cannot sleep, so that sleep becomes
the earliest moment the next tick may start, on an absolute grid
(`browser_pacing.h`). The grid advances by the EXACT frame period
(`SdlApp::effective_frame_period_us()`: 20259 us at 49.36 Hz, not the 20 ms the
whole-millisecond sleep adds up to, which ran 1.3% fast and cost the audio pacer a
held picture every second or two), and a callback that arrives before the next due
time runs no tick. A grid rather than "now plus the sleep" keeps 50 ticks a second
on 60, 120 and 144 Hz displays alike (the latter lands every tick on the second
animation frame, 30 a second). A callback runs ONE grid tick, and a second only if a
whole slot was already overdue when the callback started: the browser composites
once per callback, so a second tick in it presents two frames and shows one.
Content slower than the display (49.36 or 58.15 Hz on 60 Hz) never has a whole slot
overdue at a callback's start in steady state; content faster than the display (a
30 Hz display, `--speed 200`) always does and runs its slots, so the rate is
unchanged. A stall (a hidden tab) resyncs as `frame_deadline.h` describes: one tick
at once, then the grid, never a burst. During a fast load a tick that asks for no
sleep may repeat inside one callback, bounded by an 8 ms budget. The audio device
asks SDL for 512 sample frames, which SDL doubles to 1024 (21.3 ms at 48 kHz): SDL's
own default for a Web Audio device is 2 x 1024 (42.7 ms), against the roughly 23 ms
chunk the pacer's guards were calibrated for, so with the default the pacer's own
sawtooth reached them and doubled or held frames; a page's ENV
`SDL_AUDIO_DEVICE_SAMPLE_FRAMES` wins. `jnext_web_pacing_counter` exports the
counters that tell these apart (callbacks by ticks, ticks by frames, fill events;
a test seam, read by the bench). A first
NEX load records the warm-start state in steps of one frame, until 25 ms have
passed in the tick, reporting `Module.jnextProgress` (see the last paragraph below), so
the page keeps repainting. The SDL-only desktop frontend uses the same exact
deadline (`frame_deadline.h`, as the Qt window does).

*Video, input, sound.* The renderer is SDL's OpenGL ES 2 one, on WebGL, with the
same letterbox (640x512 logical) and the renderer's default linear filter. The
page sizes the canvas by CSS; the window is resizable and high-pixel-density, so
the drawing buffer follows the CSS size times the device pixel ratio, and F2
(window scale) does nothing. The window is created as an OpenGL window from the
start: otherwise `SDL_CreateRenderer` destroys and recreates it, and on a HiDPI
display the SDL port sets the canvas CSS size to 1x1 in between, so the new
window ignores the page's CSS. It has no title, so the host page keeps its own
`document.title`. Keys go to the canvas element (the keyboard element hint
defaults to the canvas selector; the page's `ENV` wins), so a page that embeds
the emulator keeps the rest of its keys, and the canvas needs a `tabindex`.
SDL watches the window's blur, not the canvas's: a canvas that loses focus to
another element of the page releases every held key through its own blur
listener. Sound is SDL's Web Audio backend, which runs the audio callback on the
main thread (so it cannot overlap a tick); before a user gesture the audio
context is suspended and SDL drains the stream on a timer, so the pacing keeps
working silently. Browsers will not resume it without a gesture on the page:
start the module from a click. Mouse capture is the pointer lock; when the
browser ends the lock itself (Escape) jnext's capture is released too. A pad
appears after its first button press (browser policy).

*Zipped NEX.* A `.zip` is not refused by extension: `main.cpp` and `jnext_web_load` open it, ask `ProgramArchive::choose()` and accept it only when that is a `.nex` (`archive_program_allowed()`), then `extract_program()` into the MEMFS scratch directory; anything else (no program, several, a TAP, unreadable) prints `program_refusal()` as before.

*Snapshots.* A snapshot rides the desktop's own load path: `program_refusal()` is the only gate, and
`jnext_web_load` hands the file to `SdlApp::cold_boot`, which boots the machine the snapshot names
(and keeps it for later resets, as the desktop frontends do). It never touches the warm start: that is
a NEX-only route (`emulator_load_routes_to_nex` gates the tick's recording, and `load_sna`, `load_szx`,
`load_z80` and `load_jns_file` build or restore the whole machine on their own). `saveSnapshot()` calls
`jnext_web_save_snapshot`, which writes a `.jns` with `Emulator::save_jns_file` to a scratch file of the
in-memory file system; the JavaScript reads it back and deletes it. `startRzxRecording()` and
`stopRzxRecording()` do the same with `jnext_web_rzx_record` (`Emulator::start_rzx_recording`, which refuses and logs
why on a Next, during a playback and so on) and `jnext_web_rzx_stop` (`stop_rzx_recording` when that path is still
recording; a reset or a load has written it already) into `/tmp/jnext-page-<seq>.rzx`.

*The touch keyboard.* `web/jnext-touch-keyboard.js` (`JnextTouchKeyboard.create`) builds the
on-screen keyboard inside the player's box the first time it is shown, with inline styles only
(no stylesheet is added to the page). A touch key is a synthetic DOM `KeyboardEvent`, with only
`code`, dispatched on the canvas: it enters SDL's own Emscripten key handler, so SDL's key and
modifier state, the host-key filter of the SDL callback, the input latch and `Keyboard::set_key`
all treat it as a PC key, and the canvas blur's `SDL_ResetKeyboard()` releases a touch-held key as
it releases a physical one. (A C++ export that pushed an SDL event would bypass SDL's key state.)
No input code of the core changed. The keys never take the focus: the keyboard cancels
`pointerdown`, `mousedown`, `touchstart` and `contextmenu`, and each key tracks its own pointer
(`setPointerCapture`), so several fingers hold several keys. The extended keys of the full layout
are sent as their own host keys, never as shift compounds, so NextReg 0xB0 and 0xB1 report them
as the Next's keyboard does. Caps Shift and Symbol Shift latch (`LATCH_MAX_HOLD_MS`): a lone tap
leaves the shift waiting; the next key sends the waiting shifts' key-downs first, then its own
codes, and its release sends the key-ups, then the shifts', and clears the latches. A code
shared by two presses (the Alt of Edit and Graph) is reference-counted. The mode is the
`--touch-keyboard` flag, resolved in `main()` (`cli::parse_touch_keyboard`,
`browser_frontend::set_touch_keyboard`) and read back by the page through
`jnext_web_touch_keyboard`, because the JavaScript knows no flag of its own; `auto` is the
`(pointer: coarse)` media query at the start. The keyboard takes the bottom of the box and the
canvas shrinks by its height (`FULL_MIN_WIDTH` chooses the layout; a `ResizeObserver` on the box
follows the page), then a window `resize` event makes SDL resize its drawing buffer.

*A WAV on the grid.* A `.wav` is positioned on the emulated T-state clock
(`WavLoader::get_ear_bit`), so the grid, a callback that runs no tick or two, a pause and a
hidden tab move it exactly as far as the machine runs, never in wall time. It never sprints
(`fastload_active()` excludes it) and the core runs it per instruction (batch gate G4), like a
real-time `.tap` or `.tzx`.

*The files a page can collect.* `player.listFiles()`, `readFile()` and `stop()` (`web/jnext_api.js.in`)
hand the page the files the core wrote for an output option. The list is built by the writers, not by
watching the file system: Emscripten's FS tracking delegate exists only in `FS_DEBUG` builds. `core/output_files.h` is
a header-only hook (`begin(path)` when a writer opens its file, `end(path)` when it has finished it for good,
`written(path)` for a file written at once); natively its sink is null, so every call is a test of one
pointer and nothing else (`output_files_test`, OUTF-03). `main()` installs the page's sink first
(`browser_frontend::install_output_sink`): it calls `Module.jnextFile(path, complete)`, which the player
sets (`jnext_web_file`, an `EM_JS` like `jnextEvent`), and the player keeps the list in JavaScript, because a call into
the module after `emscripten_force_exit` is not safe to rely on. The call sites are one place per option, at the
real start and finish of the file:

| option | begin | end |
|---|---|---|
| `--wav-record`, `--wav-record-stems`, `--dac-trace` | `AudioRecorder::start`, `DacTraceRecorder::start` | their `stop()` |
| `--tape-save` | `Emulator::start_tape_save` (TAP and TZX/WAV arms) | `stop_tape_save`, `finish_outputs` |
| `--compositor-trace` | the `fopen` in `Renderer::render_frame` | `Renderer::finish_compositor_trace` (after its frame, or at the end) |
| `--profile-output` | none | `finish_outputs`, after `write_to_file` |
| `--delayed-screenshot` | none | `SdlApp::tick`, when the capture is written |
| `--export-skoolkit`, `--export-ghidra` | none | `run_export`, once for each path in `ReReport::paths` |
| `--bug-report` | none | `write_bug_report_at_exit`, after the write |
| `--audio-log` | `AudioLogStream::open`, after the `fopen` | `AudioLogStream::finish`, after the flush and close (also when it reports an error: what is on disk is final) |
| `--psg-record` | `PsgRecorder::start` (FILE) and the first block of a chip (`-ay1`, `-ay2`) | `PsgRecorder::stop`, after the close of each file (also on a write error) |
| `--log-file` | `main()`, where the sink is built | the page's finish, after flushing every logger |

Not listed, by design: the SD-writes journal, the warm-start recording, quick snapshots, the API's own
`/tmp/jnext-page-*` scratch files, the extracted zip members, the esxDOS stand-in's writes and what a debugger
protocol command saves; none is an output option.

*Two layers keep the card in.* The list is the allow-list: only a file a writer reported can be returned. On top of it the player never lists
or returns anything under its own locations, `/sdcard` (the SD card: copyrighted NextZXOS), `/jnext` (the
configuration directory: the warm-start recording, the card-writes records) and `/media` (the programs
and the copies off the card). The test is on the canonical path, which the page's own file system gives
(`FS.lookupPath`, symlinks followed, so `..`, `.`, a relative name and a link all land on the real path), and it is
made again at each call; the `r + '/'` boundary keeps `/sdcard.dat` ordinary. Both layers are needed:
`--tape-save` to a name that is not `.tzx` or `.wav` appends TAP blocks to an existing file, so `--tape-save
/sdcard/image.img` makes the card a file "jnext wrote", which the allow-list alone would return; a deny-list alone would
return the output of every future writer that keeps firmware-derived state. A new writer left off the list is
then a missing file, which its option's row finds, and never a leak (BOUT-17).
The guard keeps the card image and the player's own files in. It does not filter what an export the page asked for
contains: a `--export-skoolkit` of ROM space writes the ROM the machine ran, exactly as on the desktop (owner decision), and
BOUT-08 compares it with the native run.

*`finish_outputs`.* No destructor runs in a page: the app is released at the end of `main()` and the module
links with `-sEXIT_RUNTIME=0`, and `emscripten_force_exit` does not flush stdio. `Emulator::~Emulator` used to
be the only place a TZX/WAV tape save was closed (its last block, the WAV header) and the CPU profile was
written, so a page lost both, and an unfinished compositor trace and the `--log-file` stayed in stdio's buffers.
`Emulator::finish_outputs()` (idempotent) does those and the destructor calls it; the page's finish
(`main.cpp`) calls it after `app.shutdown()`, flushes the loggers and tells the player the log is complete.
`stop()` uses the same `jnext_web_quit` as `destroy()` and resolves with the exit status once `onExit` has fired,
which is after `finish_outputs`.

*Refused and absent.* The build's own flag and `--set` key refusals apply, and
the page also refuses `--headless` (it would block the tab for ever), `--rzx-record` (a
page records with the player's methods), and any program that is not a `.nex`, `.tap`, `.tzx`, `.jns`, `.sna`, `.szx`, `.z80`, `.wav` or `.rzx` (`browser_page.h`),
each with a message that says "browser page". The browser reserves Ctrl+W, T and
N (and Ctrl+Q in some browsers), so Symbol Shift plus those letters cannot be
typed with Ctrl; the operating system keeps Alt+Tab and Alt+F4. Files the
emulator writes (screenshots, WAV, the audio write log, quick snapshots, RZX recordings, which `stopRzxRecording()` hands to the page) live in the in-memory file
system for the session; the page collects those of the output options with `listFiles()` and `readFile()` (above). On the desktop `--rzx-play` is started by a pre-step at the top of
`SdlApp::run()`, which the page does not call. In a page an RZX playback (`file`, `--load x.rzx`,
`--rzx-play`) is the page's pending load (`main.cpp`) instead: so it fires `ready`, is what
`reset('program')` restarts, and a file that fails rejects `ready`. Two things
the browser link carries but never runs: the ESP-01's worker thread and the
NextPi audio thread sleep in their own thread bodies, and neither is reachable
from `tick()`: the flags that start them are refused, and a thread cannot start
without shared memory.

*The card and the warm start in the browser.* The page makes the SD card itself, from
the official NextZXOS zip (`minimal_card::kDistribution`, [5.3](03-packaging-and-release.md)),
and never takes a card or a digest from the page's author. `web/jnext-image.js` runs the
flow, and the builder (`src/core/minimal_card.cpp`, the same code as the native
`make-minimal-card`) does the work in wasm through the `jnext_card_*` exports of
`card_builder_web.cpp` (`begin`, `feed`, `finish`, `error`, `inflated`, `distribution`, and `dirs`, which returns the canonical form of a page's `card.dirs` or the reason it is refused: the rules live in `minimal_card::canonical_dirs` only):
- *Cache.* The key is `https://card.jnext.invalid/<card sha256>` (the `.invalid` host is
  never fetched) in Cache API `jnext-sdcard-v2`. The entry is stored gzipped (`packed()`,
  a `CompressionStream`: about 1.5 MB for the 40 MiB card, mostly zeros; measured with gzip -6, the page's own level may differ slightly). A hit is read
  through a `DecompressionStream` (`unpacked()`), whose CRC-32 and length check (gzip's
  trailer) is the integrity check: nothing is hashed again, because the key's digest was
  proved when the entry was written (`minimal_card.cpp` refuses a card whose SHA-256 is
  not the pinned one). An entry that fails to decompress is deleted and counts as a miss.
  A browser without both stream classes stores the card raw and hashes it on every hit,
  as before. `jnext-sdcard-v1` (from JNEXT 1.3.0 until the release that introduced v2) held the card raw: the first miss
  in v2 moves a good v1 entry over (one SHA-256, then `packed()`), and the v1 cache is
  deleted on that visit whatever it held. On a hit, the entries of that host that belong to
  OTHER releases are removed (those whose key is neither `<host><card sha256>` nor under it).
  A page with `card.dirs` (user guide 8.4)
  gets a card with no pinned digest, kept under
  `<host><card sha256>/r<recipe>/<the canonical list as JSON, URI-encoded>`: the release's identity,
  the builder recipe (a change that alters only list cards bumps it), and the list. Its
  entry carries the builder's card SHA-256 in an `X-Jnext-Card-Sha256` header, which a gzip hit
  returns without hashing (the same trust as the key) and a raw hit is hashed against; a
  missing or malformed header deletes the entry (a miss). A page that changes its list leaves the
  old list's entry until the next release. `jnext_card_begin` and the tool clear the pinned card
  digest for a non-empty list, and a refusal that names `requested path` is thrown as it is, not
  as a fault of the visitor's file. The seam's `card: {url, sha256}` ignores `dirs`.
- *Probe.* A `HEAD` of `kDistroUrl` (`mode: 'cors'`, no credentials, 15 s bound) says whether
  the page may read the file. It is available when the answer is `ok` and, when `Content-Length` is
  present, equals the pinned size. A TypeError (no `Access-Control-Allow-Origin`, or offline: the
  page cannot tell them apart), an abort or a bad status means not available.
- *Fetch.* `fetch(url)` of the zip streams straight into `feed()`, one chunk at a time
  through one reusable staging buffer: the zip is never held whole in JavaScript or in wasm,
  and never stored. `finish()` verifies the zip digest, builds the card in the in-memory
  file system and checks the card digest; the card is then stored in the Cache.
- *Pick.* When the probe or the fetch fails, or `card.source` is `'pick'`, the API's overlay
  shows the pick panel (the copyright statement, the link to specnext.com, a file button).
  The chosen file's size is compared with the pinned size first, and its bytes go through the
  same builder (`onProgress('build', ...)`). There is no File System Access API or
  drag-and-drop.
- *The internal card.* After the card is ready the layer gives `jnext_web_argv` an internal
  option `$card`, `{path, sha256}`, which becomes `--sdcard PATH --sdcard-sha256 HEX`. A
  page option of that name is refused by the layer, and `web_api_test` proves internal
  names start with `$` and are not in the man page. The page's `card` option
  (`{source: 'auto'|'fetch'|'pick'}`) is a host option and adds nothing to argv.

The card is a plain file in the in-memory filesystem, read one 512-byte block at a time
through a stream, so the SD code needed no block-device seam. The warm-start recording is keyed on the card's digest, because the page
rewrites the card on every visit and its modification time would never match. A zipped
card (reachable only through the local-development seam below) is extracted in `main()` with the
program-archive reader (exactly one `.img` member; the zip's digest stands for
it) and the extracted card is removed when the run ends. The recording is the
only thing kept between visits by default: IndexedDB (IDBFS) is mounted on the warm-start
directory only (`JNEXT_CONFIG_DIR=/jnext`, so `/jnext/warm-start`), and
`warm_start::store()` calls a persist hook, installed by the browser frontend,
that flushes it. Guest writes to the card live in memory and are gone at
reload unless the page asks for `keepWrites` (below). A first NEX load records the NextZXOS machine in steps: `SdlApp::tick()`
calls `Emulator::prepare_warm_start()` for a time budget per tick and reports
`Module.jnextProgress(phase, fraction)` (`'warm-start'` while recording,
`'restore'` for a cached recording; the page adds `'download'`, `'build'` and `'choose'`), so the page can
repaint and show progress instead of freezing for the length of the boot. With no
image the provisioner declines the download without reading stdin (a stdin read
is `window.prompt()` in a browser).

*The CRT effect.* `--crt` and the three `startup.crt_*` keys (the `WEB` frontend bit in
`config_keys.h`; the native SDL-only build still warns that they have no effect) turn on
the desktop's crtemu shader, off by default. The SDL renderer of the page is a WebGL 1
context (SDL's GLES2 driver), and a canvas holds one context, so crtemu draws on SDL's own:
`BrowserCrt` (`platform/browser_crt.cpp`) owns a `crtemu_t` built right after the renderer, in
the same host-loader mode as the Qt overlay (GL entry points from `SDL_GL_GetProcAddress`,
`crtemu_browser_impl.cpp` being the one translation unit with crtemu's implementation, which
carries patch 4: the vertex buffer and attribute are stated again at every present). With the
shader on, `SdlDisplay::upload_frame` calls `BrowserCrt::present` instead of drawing: the
framebuffer is swapped to ABGR into the object's own buffer, the viewport is SDL's letterbox
rectangle (`SDL_GetRenderLogicalPresentationRect`, in output pixels, from the top;
`SDL_GetRenderOutputSize` gives the height: the Current form returns the logical size under a
logical presentation) turned into a GL viewport by `crt_shader::gl_viewport`, every vertex
attribute array but 0 is disabled (SDL's GLES2 renderer enables arrays 0 and 1 and binds a buffer
to neither until its first draw, which it never makes now, and WebGL 1 refuses a draw while an
enabled array has no buffer), and `crtemu_present` draws, its own clear painting the letterbox
bars. SDL's own state needs no care, because it draws nothing while the shader runs. No SDL
present is needed: the page shows the drawing buffer at the end of the frame. The effect is chosen
once, after `SdlApp::init` (`main.cpp`; the rule is
`crt_shader::resolve`, the one the Qt window uses too), and fixed for the run. The software scanlines are 25% black on the odd rows of the
640x512 logical screen, as the Qt overlay's. A shader that cannot be built (no WebGL renderer,
a compile or link failure) leaves the run going: one `CRT effect:` warning, then the
scanlines for the soft style (or when `startup.crt_filter` is also set) and the plain picture
otherwise. `BrowserCrt` is destroyed before the renderer, because crtemu deletes its GL
objects with the context alive. A lost WebGL context is not handled, as for SDL's own
renderer. crtemu's WebGL arm needed two patches in the host-loader mode (8: the wrap enums, 9:
the viewport size), listed in `third_party/crtemu/README.jnext.md`.

*Keeping the guest's writes.* `keepWrites: true` makes the page pass
`--sdcard-writes <path>`, the SD write journal of
[3.8](../03-subsystems/08-media-and-loaders.md), with a file in the in-memory filesystem
(`/jnext/card-writes/<key>.jsdw`) that `web/jnext-image.js` brings in from, and stores back
to, its own IndexedDB database `jnext-card-writes` (store `journals`, one record per key).
The key is the card's SHA-256 and the first 16 hex digits of the SHA-256 of
`location.pathname` (`cardWritesKey`), so one card has one record per page and a changed
card never reuses the old one. A second IDBFS mount was rejected: it would create a database
at mount, and the warm-start database is the only one a plain page makes; the store is
written whole (the file is a few KB) and the database is created only for a page that uses
the option. The option is opt-in: it adds a flag to the command line the API documents.
`jnext_api.js` hands `keepWrites` to the emulator as the file's path (false is dropped) and
rejects a non-boolean itself, before anything is fetched. The core's journal calls a persist
hook after every committed write (`browser_frontend.cpp`, which calls
`Module.jnextPersistCardWrites`); `persistCardWrites` runs one store at a time, and a write
that arrives during one sets a flag that makes the same chain store once more, so the last
write is the one kept. One store also runs after `callMain` returns, because a bind-time
`--sdcard-writes-reset` happens before the hook exists (the file is then absent and the record
is deleted), a best-effort one on `pagehide`, and a final one that `destroy()` awaits before it
resolves. `player.resetCard()` calls `jnext_web_reset_card()` (`web_controller.cpp`), which
runs `Emulator::forget_card_writes()` (the journal's `clear()`, whose hook deletes the record,
and the stand-in's in-memory file, which a cold boot would otherwise carry over) and then the
same restart as `reset('program')`. A NEX loaded with `file` is answered by the esxDOS stand-in (3.8), which keeps one file; the
journal stores it as a second kind of record, when the program closes the file, so it travels in
the same record, needs no other flag or store, and is reset with the card. The record has no
program in its key: a changed NEX on the same page and card reads the old save. The stand-in's
file is not card state, so a save through it leaves the warm-start key alone and the next visit
restores the recording instead of making it again. Files in a host directory a page names with
`--esxdos-stub-root` are in memory only. Two `keepWrites` players of one card on one page share
a key; the last store wins. A visit after the program saved records the warm start again, because the card
the recording was made on is not the one it now boots from.

## How it is tested

| target | tree | what runs | rows or suites | CI leg |
|---|---|---|---|---|
| `make wasm-core` | `build/wasm-core` | `test/wasm-core-check.sh` under the SDK's node, and `sha256_test` | the WCORE rows, SHA256-02..16 | with `regression-wasm` |
| `make unit-test-wasm` | `build/wasm-unit-test` | the SDL-only unit suites under node, through `test/node-run.sh`; suites marked `# os: native` are absent by name | the manifest | `unit-wasm` |
| `make regression-wasm` | `build/wasm-core` | the regression suite's screenshot rows, each pixel-equal to the native reference, plus the functional rows whose `os=` names `emscripten` | `regression_tests.conf` | `regression-wasm` |
| `make wasm-browser-test` | `build/wasm-core` | the browser frontend in the pinned Chromium headless shell | BRW-01 to BRW-35, WBI-PW-01 to WBI-PW-09, BOUT-01 to BOUT-20 (the files JNEXT writes, `files.mjs`: each output option's file, listed and fetched by the page, byte-equal to a native run of the same arguments (`chain_outputs` in `browser-smoke.sh`) except the PNG, compared by pixels, and the three members of the bug-report zip that carry the time, the paths, the card name and the host (`manifest.json`, `settings.json`, `log.txt`; every `state/` member, the screens and `debugger.json` are byte-equal); every row of a run that depends on the stop frame (the WAV, the profile, the RAM dumps) runs at `--speed 99`, one frame in each tick, in the page and natively; the files still being written, `stop()`, and the card guard that refuses the SD card even when `--tape-save` was aimed at it), WEBAPI-01 to WEBAPI-34 (WEBAPI-30 and 31 load a WAV, made at run time from `demo/beeper_demo`'s TAP by `tap2wav.py` beside `tap2tzx.py`, and compare the page with a native run and with `tap-demo-reference.png`; WEBAPI-32 to 34 load a zipped NEX and its data file, made at run time with CPython's `zipfile` around the esxDOS probe NEX, and refuse a zipped TAP), WIDGET-01 to WIDGET-32, BDBG-01 to BDBG-25 (the debugger protocols served by a page, `debug.mjs`: the unchanged native peers `dzrp-peer.py`, `zrcp-peer.py` and `gdb-peer.py` against the page over the JS channel, through a `TcpRelay` that stands in for a webview host, and over a WebSocket to the bridge run as a child process; the bridge's origin check, framing against node's own WebSocket client and the RFC 6455 handshake vector, backpressure and the end of a session after a reply larger than the kernel buffers), BCARD-01 to BCARD-44 (the card flow, the poster, the stored form of the card and `card.dirs`, `card.mjs`: a synthetic distribution through the localhost seam, and BCARD-13 and BCARD-41 on real data; BCARD-22 to 25 and 44, `card-errors.mjs`: the wording of the pick path's refusals, driven with a stub module under node, no browser and no seam), TKB-01 to TKB-17 (the on-screen keyboard, `touch.mjs`: real touch input through the DevTools protocol on a Chromium touch-device emulation, against the spec's own table of the Next keyboard from the FPGA's `keymaps.vhd`, `membrane.vhd` and `zxnext.vhd`, with `demo/key_matrix_demo`, a 48K program that writes the half-rows and NextReg 0xB0/0xB1 to the magic port, and its native run `kmx.log` as oracle), WBW-01 to WBW-17 (the kept writes, `writes.mjs`: the stored record is compared byte for byte with the journal of a native run of the same program, WBW-13 to WBW-17 for the file the esxDOS stand-in keeps), BRZ-01 to BRZ-12 (RZX in a page, `rzx.mjs`: `keys48.rzx` and `s128.rzx` are recorded at run time by the native jnext from our own inputs; a page's replay equals the native replay, and `stopRzxRecording()`'s file replays natively), BSN-01 to BSN-11 (snapshots, `snapshots.mjs`: a `.sna`, `.z80`, `.szx` and `.jns` loaded in the page equal the native run of the same file, and `saveSnapshot()` is read back by the page and by the native jnext), BCRT-01 to BCRT-13 (the CRT effect: `test/browser/crt.mjs`, structure derived from the vendored shaders, not reference images), WBENCH-01 to WBENCH-12 (the browser bench's arithmetic, `test/bench/web/bench-stats-test.mjs`), and WEBPKG-BR-01 to WEBPKG-BR-07, which boot `plain.html` from the extracted `make package-web` zip, check that its four example pages lay out at the device width on a phone (02 to 05), and that `plain.html` and `itch-host.html` fit their player in a phone-wide page (06 and 07) (the `SPECS` of `test/browser/browser-smoke.sh`) | `browser` |

- `make wasm-core` proves the module starts, imports no shared memory, catches an exception,
  refuses what the browser cannot do, and hashes correctly on the portable SHA-256.
- `make unit-test-wasm` and `make regression-wasm` prove the core computes the same
  thing as the native build under the WebAssembly compiler: the second to the pixel.
- `make wasm-browser-test` proves the page: the frame loop and pacing, input and sound
  policy, the card build, cache and warm start, and the JavaScript API, with the
  example pages. The fixture preparation (independent chains of native runs) and the
  specs run in parallel, at most `JNEXT_BROWSER_JOBS` at once (default
  `min(4, max(1, nproc/2))`; `1` is one at a time), longest spec first
  (`test/browser/spec-runner.inc`). Each spec writes only under its own
  `$SMOKE_WORK`; the shared fixtures are read-only while the specs run, and a spec
  tagged `file:count:serial` runs alone afterwards. Details: [5.1](01-make-targets.md), [5.4](04-continuous-integration.md),
  [4.2](../04-testing/02-declared-suites-and-pinned-counts.md) and
  [4.3](../04-testing/03-the-regression-suite.md).
- The widget (`web/jnext-widget.js.in`) is a plain script on top of `jnext.mount()`:
  every `.jnext-widget` element gets a frame, a progress bar, a toolbar and a status
  line, with all CSS injected once under `.jnext-widget` (each part resets its own
  properties with `all: initial`; the API's own overlay keeps its text and click, and
  only its colours are set from the theme variables). The WIDGET rows (`widget.mjs`)
  include the two code blocks of the user guide's section 8.5, extracted from its
  source and run verbatim, so the guide's snippets cannot drift from what is tested.
  `jnext.mount()` itself adds the small "JNext" attribution link (www.jnext.es; the application icon, a string
  made by CMake from `packaging/assets` that a few lines of `mount()` turn into SVG DOM with `createElementNS`: no
  `DOMParser`, no `data:` URI, so Trusted Types and `img-src` policies have nothing to refuse). It is always the
  next sibling of the container: in the flow, sized and offset to the player box (a `ResizeObserver` and the window
  `resize` keep it on the panel's right edge), or, when the parent's layout (a flex row, a grid, an absolutely
  positioned player) would not put it below the player, absolutely positioned under the box's right edge with the
  container's bottom margin reserving the room (in that mode the geometry is checked once per frame, since nothing
  else notices content above the player changing height). It is never inside the container, so the container's `overflow`
  cannot clip it, and it never lies over the picture. The widget and every page share it; `itch-game.html` is a
  flex column so the link keeps its room.
- `crt_shader_policy_test` (CRT-POL-15 to 18: the start-up rule and the GL viewport) and `config_keys_test` (SETK-CRT-01: the `WEB` bit) cover the CRT effect's rules natively.
- Native suites test the browser's policy without a browser: `web_api_test` (the option
  table, the argv and the man page's browser section; `make cli-check` runs it),
  `browser_policy_test` (the frame-callback gate and the page's refusals),
  `warm_start_card_test` and `sha256_test`.
- `remote_pipe_test` (XPT-PIPE-01 to 13, DBGS-CH-01 and 02, DBGS-BUD-01) tests the channel through the production `remote::Server`, and `DebugServers` over it with the bytes the native peers check; it runs natively and under node. `dzrp_adapter_test`, `gdb_rsp_test` and `zrcp_adapter_test` run under node too, which proves the protocol code with a 32-bit `size_t` and `long`.
- `make package-web` builds the browser zip and `make web-package-test` is the hermetic contract suite of its script (WEBPKG-01 to WEBPKG-50, run by `make unit-test`); both are described in [5.3](03-packaging-and-release.md).
- All four wasm targets are in the batch gate ([4.1](../04-testing/01-the-test-triplet.md)).
- The SDK and Playwright are provisioned by `make emsdk` and `make playwright`, pinned by
  sha256, into `~/.cache/jnext/tools`.

What none of this proves: Firefox or Safari, a real GPU, a phone, or itch.io itself.

## The card never enters the tree

NextZXOS is SpecNext's copyrighted software, and only specnext.com hosts it. Four rules
keep it, and every card made from it, out of the repository and off GitHub:

- The tests build the minimal card at run time from the developer's own cached
  distribution, into a scratch directory under `~/tmp` that is deleted when the run ends
  (the suites' own master under `~/.jnext/sdcard/test-suite` is only read, in place). Tests that need a zip use a synthetic one
  made at run time.
- Playwright's trace, HAR and video are off.
- No CI artifact path reaches the card.
- JNEXT never serves it: the page reads the official zip from specnext.com, or the
  visitor's own copy of it. Nobody hosts a card: not JNEXT, not a page author, not
  SpecNext, and a developer's local one is never published.

The minimal card itself is described in [5.3](03-packaging-and-release.md). The user
guide (chapter 8) tells page authors and visitors none of what follows; the
`lint-user-docs-card` check (part of `make docs-check`) fails if a user-facing text
describes making, serving or hosting a card (the developer guide is out of its scope).

## Trying a page locally with a card of your own

This section is local development and testing only, on your own download of the official
distribution. It is not a way to publish anything: a card made this way is copyrighted
NextZXOS content, stays on your machine, and is never hosted or served beyond
`localhost`.

1. Make the card from your download with `make-minimal-card` ([5.3](03-packaging-and-release.md))
   and take its SHA-256 from the last line it prints.
2. Put it next to the page in a scratch directory under `~/tmp` (with
   `jnext-VERSION.js`, `jnext-VERSION.wasm`, `plain.html` and a program, `game.nex`
   here) and serve that directory on localhost: `python3 -m http.server 8000`.
3. Give the page the local test seam before its scripts run. `window.jnextDev` is read once
   at `mount()` and is honoured only when the page's host is `localhost`, `127.0.0.1` or
   `[::1]`; on any other origin it is ignored with one console warning. It is not an option
   and no shipped file sets it. Playwright injects it with `page.addInitScript`; in a
   browser you can set it from a small extra script or the console before `mount()`:

   ```js
   window.jnextDev = { card: { url: 'card-file-name', sha256: '<the card digest>' } };
   ```

4. Open `http://localhost:8000/plain.html?file=game.nex` and click the player. A
   page opened from disk (`file://`) does not work: the browser refuses to load the `.wasm`
   file from a `file://` address and the page's status line reads
   `error: Aborted(both async and sync fetching of the wasm failed ...)`.
5. Delete the scratch directory when you are done.

The seam's fields:

- `card: {url, sha256}`: use that file as the card, skipping the distribution flow. It is the
  older fetch path (its cache entry is gzipped too, and still hashed on every hit: its key is
  the URL, which does not bind the digest): the file may be a `.img` or a zip holding exactly one `.img` (then the digest
  is the zip's), it is checked against the digest on every visit and cached under its URL.
- `distribution: {url, size, sha256, cardSha256}`: a stand-in for the official zip, for the
  tests (a synthetic zip, or a zip of the cached image made at run time). With `cardSha256`
  omitted the pinned card digest is kept.
- `probeTimeoutMs`: the probe's time bound.

The cross-origin tests serve the page from `http://127.0.0.1:<port>`, not from another host
name, so the seam is honoured there too.

Rows: BCARD (`test/browser/card.mjs`: the probe, fetch, pick, cache and seam paths, and the
build of the real distribution when cached) and LUDC (`test/lint-user-docs-card-selftest.sh`:
the user-facing-docs lint and its scope).

## A program on the card

`file: {card: PATH}` runs a NEX that the in-memory card holds, with its data files. `web_api::build_argv`
validates the object (`WAPI-CFILE-*`): no key but `card`, a string, absolute, no empty, `.` or `..` part, ending
`.nex`, not with card source `none`, and inside one canonical `card.dirs` entry by path component (lower
case; a string prefix such as `/GAMES/Next/TX` does not cover `/GAMES/Next/TX-1696`). The page layer
(`jnext_api.js`, `copyProgram`) then calls `jnext_card_copy_program` (`card_builder_web.cpp`), which runs
`card_extract::copy_program_dir` (`src/core/card_extract.cpp`): FatFs mounts the card read-only in effect (it
never writes), finds the NEX case-insensitively and copies its directory, only that subtree, into
`/media/<k>-card/`, up to 48 MiB of file bytes, 4,096 entries (files and directories together: FatFs opens each file by path and rescans its directory, so a wide directory costs quadratic time, and a card can alias one directory under many names) and 32 levels of depth: past any of them the copy is refused. `substitute()` then sets `file` to the copy and adds the internal
`$serveDir: true`, which `build_argv` turns into `--esxdos-stub`; the page layer refuses every option whose
name starts with `$`, and `load({card})`. The copy is all or nothing: a failure removes it and the message names the path
(`the card has no /GAMES/...`). Tests: `MCARD-CP-01..03` (the extractor, on the Builder's synthetic card),
`WAPI-CFILE-01..04`, `BCFILE-01..06` (`test/browser/cardfile.mjs`, real data for the two games against
mtools copies and native runs) and `PAGES-03..05` (the demos of the site).

## The Pages demo site

`make pages-site` (prerequisite `package-web`) runs `packaging/web/pages-site.sh`, which reads
`jnext-<ver>-web.zip` with CPython's `zipfile` and writes `build/pages`: the zip's files minus the
four example pages, plus `index.html`, made from `web/pages/index.html.in` with the version of
`version.yaml`. The page holds one widget with `data-card-dirs="/APPS,/GAMES"` and `data-nmi`, and three demo links, `?demo=nextzxos`, `?demo=warhawk` and `?demo=tx-1696`: a small script sets the widget's title and, for a game, `data-card-file`, so every demo is a page load with one emulator, and all three share one cached card. The page also
loads the site's visitor-statistics script from `stats.jogv.es`; the browser rows answer it with an
empty script, so no test run reaches the statistics server. The
script refuses, and removes the output, when the site is not exactly that list, when any name has
an image, ROM, disk-image, program, recording or archive extension as a dot-separated part (`.img .mmc
.rom .bin .dsk .trd .scl .hdf .vhd .mdr .p .nex .tap .tzx .sna .szx .z80 .jns .rzx .wav .raw .zip .gz
.tgz .bz2 .xz .7z .rar .tar`, any case, any depth, so `x.img.gz` too) or ends in a dot or whitespace,
when a file starts with an archive signature or holds a boot-sector mark at byte 510 (only `jnext-<ver>.wasm`,
with its preamble, is exempt), when a zip entry is a symlink, has a `..` or absolute path, lies outside the top folder
or repeats a name, when a required file is missing, or when the wasm exceeds 16 MiB or the site 24 MiB. The
site never holds NextZXOS, the distribution or a program: the visitor's browser downloads the
distribution from specnext.com. `test/packaging/pages-site-test.sh` (PSITE-01 to 07, hermetic) pins
the script and the workflow wiring; `test/browser/pages.mjs` (PAGES-01 and 02) serves `build/pages`
alone, and `wasm-browser-test` has `pages-site` as a prerequisite.
