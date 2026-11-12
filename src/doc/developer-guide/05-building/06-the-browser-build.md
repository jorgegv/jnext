# 5.6 The browser build

JNEXT also runs in a browser tab, built for WebAssembly. This page is the entry
point to everything that builds, tests and ships it. What the page's author sees
is in the user guide, chapter 10; this page is for the person who changes the
emulator.

## What it is

The browser build is the SDL-only configuration (no Qt, no debugger GUI) compiled
by a pinned Emscripten SDK. One tree, `build/wasm-core`, links two modules:

- `jnext` (`jnext.js` and `jnext.wasm`): the node build. `test/wasm-core-check.sh`
  (WCORE-01 to WCORE-09) and `make regression-wasm` run it under node. The unit suites
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
| `CMakeLists.txt` | `if(NOT EMSCRIPTEN)` around `src/remote` | No debugger protocol servers: a page has no sockets to serve on. |
| `CMakeLists.txt` | `if(NOT EMSCRIPTEN)` around `src/script` | No script host. |
| `CMakeLists.txt` | `if(EMSCRIPTEN)` after the `jnext` target | The node `jnext` links with `-sNODERAWFS=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1`. |
| `CMakeLists.txt` | `if(EMSCRIPTEN)` around `jnext_web` | Defines the `jnext_web` target: `main.cpp`, `browser_frontend.cpp` and `web/web_controller.cpp`; `OUTPUT_NAME` `jnext-<version>`; `JNEXT_BROWSER=1`; the web link options (`-sENVIRONMENT=web`, `-sMODULARIZE=1`, `-sEXPORT_NAME=createJnextModule`, `-sINVOKE_RUN=0`, `-sEXIT_RUNTIME=0`, the exported runtime methods and functions, `-lidbfs.js`); the two `--extern-post-js` files, `web/jnext-image.js` then the configured `jnext_api.js`; and a `configure_file` of `jnext_api.js.in`, of each example page and of `jnext-widget.js.in`. |
| `src/core/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` around `find_package(PNG)` | No libpng package search: the port target above stands in. |
| `src/core/CMakeLists.txt` | `elseif(EMSCRIPTEN)` in the network-library choice | No network library: `sdcard_provisioner_net_web.cpp` is self-contained (a portable SHA-256, no download). |
| `src/platform/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` (twice) | `jnext_platform` does not link `jnext_remote` or `jnext_script`; and the sources `browser_crt.cpp` and `crtemu_browser_impl.cpp` (the CRT effect) are left out of every other build. |
| `src/esp01/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` (twice) and `if(NOT WIN32 AND NOT EMSCRIPTEN)` (twice) | The ESP-01 suites (`esp_at_test`, `esp_socket_test`, `esp_sigpipe_test`) and their `add_test` lines are not built. |
| `test/CMakeLists.txt` | `if(EMSCRIPTEN)` | The test executables link with `-sNODERAWFS=1 -sENVIRONMENT=node`, an 8 MB stack, `-sSTACK_OVERFLOW_CHECK=2`, `-sEXIT_RUNTIME=1`, and at `-O1`. |
| `test/CMakeLists.txt` | `if(NOT EMSCRIPTEN)` and `if(NOT WIN32 AND NOT EMSCRIPTEN)`, many | The suites whose subject the browser build compiles out or cannot run (processes, threads, sockets). One arm each; they are declared `# os: native` (or `posix`, `linux`) in `test/unit-tests.conf`, see [4.2](../04-testing/02-declared-suites-and-pinned-counts.md). |

Source files carry the same two symbols. The list below is the output of
`git grep -c '__EMSCRIPTEN__\|JNEXT_BROWSER' -- src` and what each arm does:

- `src/main.cpp`: the refusals (`cli::browser_refusal`, `config_keys::browser_refusal`,
  and for `jnext_web` `browser_page.h`); no NextPi; the SD-card download declined
  without reading stdin; extraction of a zipped `--sdcard` and its removal at exit; handing the run to `browser_frontend::run`
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
- `src/platform/debug_servers.cpp` and `debug_servers.h`: no protocol servers (the three servers are neither declared nor started).
- `src/core/nextpi_provisioner.cpp`: compiled out.
- `src/core/video_recorder.cpp`: no process to run ffmpeg, so the call fails as a
  missing ffmpeg does.
- `src/core/sdcard_provisioner_net_web.cpp` and `sdcard_provisioner_net_curl.cpp`: the
  two halves of the provisioner's network and hash backend, each whole-file
  conditional.

`src/platform/browser_page.h` has no arm of its own: `main.cpp` includes it only under
`JNEXT_BROWSER`, and it holds the page's own refusals (`--headless`, RZX, program
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
the version and linked into `jnext-<version>.js` as a second `--extern-post-js`,
after `web/jnext-image.js` (which it calls). It makes one module instance per
mount: the core keeps process-wide state, so one instance holds one emulator. It
knows no flag spelling. The whole options object goes to `jnext_web_argv`, which
runs `web_api::build_argv` over the `web_api::WEB_OPTIONS` table: each row names a
`cli::OptId` or a `config_keys::KeyId`, so a renamed flag is a compile error, and
the man page's browser section is diffed against the table and the refusal
functions by `web_api_test` (run by `make cli-check`). The controller
(`web_controller.cpp`, a source of `jnext_web` only) holds the exported calls
(`jnext_web_pause`, `jnext_web_reset`, `jnext_web_load`, `jnext_web_set_volume`,
`jnext_web_quit`) and, through `SdlApp::set_on_load_applied` and `after_tick`, tells
the page when the program is in the machine and a frame has followed (the `ready`
event, `Module.jnextEvent`). `jnext_web_load_applied`, `jnext_web_gain_db`,
`jnext_web_frame_period_ms` and `jnext_web_cpu_mhz` (the frame period and the
effective CPU speed, read by the browser bench) are test seams, not API. Pause is the browser loop's pause (`emscripten_pause_main_loop`),
because the backend's pause needs an armed machine, which costs a check per
instruction. Several players on a page share one IndexedDB database for the
warm-start recording.

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

*Refused and absent.* The build's own flag and `--set` key refusals apply, and
the page also refuses `--headless` (it would block the tab for ever), the RZX
flags, and any program that is not a `.nex`, `.tap` or `.tzx` (`browser_page.h`),
each with a message that says "browser page". The browser reserves Ctrl+W, T and
N (and Ctrl+Q in some browsers), so Symbol Shift plus those letters cannot be
typed with Ctrl; the operating system keeps Alt+Tab and Alt+F4. Files the
emulator writes (screenshots, WAV, quick snapshots) live in the in-memory file
system for the session. `--rzx-play` is started by a pre-step at the top of
`SdlApp::run()`, which the page does not call (`--rzx-record` starts from `tick()`);
a later RZX implementation must move that pre-step into something the loop can call
before it enables RZX. Two things
the browser link carries but never runs: the ESP-01's worker thread and the
NextPi audio thread sleep in their own thread bodies, and neither is reachable
from `tick()`: the flags that start them are refused, and a thread cannot start
without shared memory.

*The card and the warm start in the browser.* `web/jnext-image.js` gets the SD
card into the page: it fetches the image cross-origin (`mode: 'cors'`, no
credentials), checks its SHA-256 against the digest the page was given on every
visit, and keeps the verified bytes in the Cache API (keyed by URL; an entry that
no longer hashes right is dropped and fetched again). The card is a plain file in
the in-memory filesystem, read one 512-byte block at a time through a stream, so
the SD code needed no block-device seam. The page then passes
`--sdcard FILE --sdcard-sha256 HEX`: the warm-start recording is keyed on that
digest, because the page rewrites the card on every visit and its modification
time would never match. A zipped image is extracted in `main()` with the
program-archive reader (exactly one `.img` member; the zip's digest stands for
it) and the extracted card is removed when the run ends. The recording is the
one thing kept between visits: IndexedDB (IDBFS) is mounted on the warm-start
directory only (`JNEXT_CONFIG_DIR=/jnext`, so `/jnext/warm-start`), and
`warm_start::store()` calls a persist hook, installed by the browser frontend,
that flushes it. Guest writes to the card live in memory and are gone at
reload. A first NEX load records the NextZXOS machine in steps: `SdlApp::tick()`
calls `Emulator::prepare_warm_start()` for a time budget per tick and reports
`Module.jnextProgress(phase, fraction)` (`'warm-start'` while recording,
`'restore'` for a cached recording; the page adds `'download'`), so the page can
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

## How it is tested

| target | tree | what runs | rows or suites | CI leg |
|---|---|---|---|---|
| `make wasm-core` | `build/wasm-core` | `test/wasm-core-check.sh` under the SDK's node, and `sha256_test` | the WCORE rows, SHA256-02..15 | with `regression-wasm` |
| `make unit-test-wasm` | `build/wasm-unit-test` | the SDL-only unit suites under node, through `test/node-run.sh`; suites marked `# os: native` are absent by name | the manifest | `unit-wasm` |
| `make regression-wasm` | `build/wasm-core` | the regression suite's screenshot rows, each pixel-equal to the native reference, plus the functional rows whose `os=` names `emscripten` | `regression_tests.conf` | `regression-wasm` |
| `make wasm-browser-test` | `build/wasm-core` | the browser frontend in the pinned Chromium headless shell | BRW-01 to BRW-33, WBI-PW-01 to WBI-PW-08, WEBAPI-01 to WEBAPI-29, WIDGET-01 to WIDGET-16, WBENCH-01 to WBENCH-12, BCRT-01 to BCRT-13 (the CRT effect: `test/browser/crt.mjs`, structure derived from the vendored shaders, not reference images) (the browser bench's arithmetic, `test/bench/web/bench-stats-test.mjs`), and WEBPKG-BR-01, which boots `plain.html` from the extracted `make package-web` zip (the `SPECS` of `test/browser/browser-smoke.sh`) | `browser` |

- `make wasm-core` proves the module starts, imports no shared memory, catches an exception,
  refuses what the browser cannot do, and hashes correctly on the portable SHA-256.
- `make unit-test-wasm` and `make regression-wasm` prove the core computes the same
  thing as the native build under the WebAssembly compiler: the second to the pixel.
- `make wasm-browser-test` proves the page: the frame loop and pacing, input and sound
  policy, the image download, cache and warm start, and the JavaScript API, with the
  example pages. Details: [5.1](01-make-targets.md), [5.4](04-continuous-integration.md),
  [4.2](../04-testing/02-declared-suites-and-pinned-counts.md) and
  [4.3](../04-testing/03-the-regression-suite.md).
- The widget (`web/jnext-widget.js.in`) is a plain script on top of `jnext.mount()`:
  every `.jnext-widget` element gets a frame, a progress bar, a toolbar and a status
  line, with all CSS injected once under `.jnext-widget` (each part resets its own
  properties with `all: initial`; the API's own overlay keeps its text and click, and
  only its colours are set from the theme variables). The WIDGET rows (`widget.mjs`)
  include the two code blocks of the user guide's section 10.5, extracted from its
  source and run verbatim, so the guide's snippets cannot drift from what is tested.
- `crt_shader_policy_test` (CRT-POL-15 to 18: the start-up rule and the GL viewport) and `config_keys_test` (SETK-CRT-01: the `WEB` bit) cover the CRT effect's rules natively.
- Native suites test the browser's policy without a browser: `web_api_test` (the option
  table, the argv and the man page's browser section; `make cli-check` runs it),
  `browser_policy_test` (the frame-callback gate and the page's refusals),
  `warm_start_card_test` and `sha256_test`.
- `make package-web` builds the browser zip and `make web-package-test` is the hermetic contract suite of its script (WEBPKG-01 to WEBPKG-50, run by `make unit-test`); both are described in [5.3](03-packaging-and-release.md).
- All four wasm targets are in the batch gate ([4.1](../04-testing/01-the-test-triplet.md)).
- The SDK and Playwright are provisioned by `make emsdk` and `make playwright`, pinned by
  sha256, into `~/.cache/jnext/tools`.

What none of this proves: Firefox or Safari, a real GPU, a phone, or itch.io itself.

## The image never enters the tree

The SD-card image is copyrighted content. Four rules keep it out of the repository and
off GitHub:

- The tests build the minimal card at run time, into a scratch directory under `~/tmp`
  that is deleted when the run ends.
- Playwright's trace, HAR and video are off.
- No CI artifact path reaches the card.
- JNEXT never serves it: a page names an image it can reach, and the browser fetches it.

The minimal card itself is described in [5.3](03-packaging-and-release.md).
