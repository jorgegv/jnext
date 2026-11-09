# 5.2 Build configurations

The `make` targets already pass the right options for you, so this table
matters mainly when you invoke CMake directly.

| Option | Default | Meaning |
|---|---|---|
| `ENABLE_QT_UI` | **OFF** | Build the Qt6 native UI. `make gui-*` turns it on; a plain `cmake` without it gives the SDL frontend |
| `ENABLE_DEBUGGER` | **ON** | Include the Qt debugger UI. It opens from the Qt main window, so it is only reachable when `ENABLE_QT_UI` is also on |
| `ENABLE_TESTS` | ON | Build the unit-test binaries |
| `USE_CCACHE` | ON | Use ccache as the compiler launcher when it is found; a no-op if it is not |
| `JNEXT_ENABLE_LTO` | ON | LTO/IPO for Release builds. Flipped OFF in exactly one place — see below |
| `JNEXT_FORCE_QT5` | OFF | Build the GUI and debugger against Qt 5 instead of Qt 6 (the legacy Windows legs only) |
| `CYCLE_ACCURATE` | OFF | 28 MHz cycle-accurate mode |
| `STATIC_BUILD` | OFF | Link statically; needs static SDL3/Qt6 builds, which distribution packages are not |
| `MACOS_APP_BUNDLE` | OFF | Build a relocatable `jnext.app` bundle (macOS packaging only) |
| `GIT_SUBMODULE` | ON | Run `git submodule update --init --recursive` at configure time |

## The combinations are gated, not assumed

`ENABLE_QT_UI` and `ENABLE_DEBUGGER` between them give four combinations, and
only one of those is the one you build by hand. `make build-matrix` builds all
four and fails if any of them breaks.

That target exists because of a specific and rather subtle failure mode. CMake
derives static-link *order* from the dependency graph you declare, which means a
target that under-declares its dependencies still links successfully whenever
some other library happens to pull them in at a usable position. The bug is
real but invisible, and it shipped twice: `jnext_core` never declared its
subsystem libraries and `jnext_platform` never declared `jnext_core`, and in
both cases the default combination — both options ON — hid the problem, so the
link only broke for someone building without the debugger. Link rot of this kind
appears at build time and only in the combinations the default build does not
exercise, so the only way to find it is to build them all. The matrix keeps
going after a failure and reports every broken combination rather than stopping
at the first, because knowing whether three are broken or only one is the
difference between a single missing edge and a wrong graph.

Building is not the same as running, though, and for a long time it was all the
matrix did: every configuration was proved to link, and only the default one ever
had a suite run in it, so a suite could stay red in a supported configuration
indefinitely — which had happened. Two configurations now run their
suites: the default Qt+debugger build through `make unit-test`, and the SDL-only
build through `make unit-test-sdl`. The other two, Qt without the debugger and
SDL with it, are not used in practice and remain build-only. CI runs both
targets.

The configurations are about *what* is built; the suites also run on three
operating systems and under node. The SDL-only set runs natively on Linux
(`make unit-test-sdl`), natively on macOS (the same target, with Homebrew's GNU
tools first on `PATH`), and as MinGW-built Windows executables under wine
(`make unit-test-win`), and as WebAssembly modules under node
(`make unit-test-wasm`). Which suites each OS owes is declared by `# os:` in
`test/unit-tests.conf` ([4.2](../04-testing/02-declared-suites-and-pinned-counts.md)).
The Qt and debugger suites run on Linux only, and the regression suite, the
documentation gates and the traceability matrix are Linux-only by design: they
gate committed artifacts that are byte-checked against the `fedora:44`
toolchain.

## Where each target builds

There is one scheme, `build/<variant>-<config>`, and everything lives under
`build/`: `build/sdl-release`, `build/gui-release`, `build/win-release`,
`build/rpm-release`, `build/deb-release`, `build/mac-release`,
`build/flatpak-release`, and so on.

`build/` itself is the canonical development tree, holding `build/jnext` plus
every test binary. `make unit-test-build` configures it, and it does so
deliberately with `ENABLE_QT_UI=ON` and `ENABLE_DEBUGGER=ON` rather than
inheriting whatever the defaults happen to be. The reason is that `ENABLE_QT_UI`
defaults OFF, so an unconfigured `build/` would silently produce an SDL binary
with no main window — and the next person to check something in the GUI would
find no window at all and reasonably conclude their own change had broken it.
For the same reason the target *refuses* to build on a `build/` that someone
has configured by hand with either flag off, rather than handing back a binary
that is not what it claims to be. If you hit that refusal, `make clean` and
retry.

`build/wasm-core` is the WebAssembly build (`make wasm-core`): Release, the
Emscripten toolchain file of the SDK that `make emsdk` provides, and only the
SDL-only configuration, so CMake refuses it with `ENABLE_QT_UI` or
`ENABLE_DEBUGGER` on. `make wasm-core` builds the `jnext` and `sha256_test` targets
only. `jnext` there is the node build: it links with `-sNODERAWFS=1
-sENVIRONMENT=node -sEXIT_RUNTIME=1`, so it sees the host filesystem (what
`make regression-wasm` needs), and its size is not the browser's, which is
`jnext_web`'s. The
configuration differs from the native SDL-only one in a few places: no
curl/OpenSSL (the SD-card provisioner's web twin,
`sdcard_provisioner_net_web.cpp`, has a portable SHA-256 and never downloads),
no `jnext_remote` or `jnext_script`, NextPi compiled out, no process spawning,
`-fwasm-exceptions` in every translation unit and the link, a 2 MB stack and
growable memory, and the flags and `--set` keys that need a process, a socket
or the script host refused with a message (`cli::browser_refusal`). zlib and
libpng come from the SDK's ports (`-sUSE_ZLIB=1`, `-sUSE_LIBPNG=1`), which pick
the libpng variant that matches the exception model.

`build/wasm-unit-test` (`make unit-test-wasm-build`) is the same configuration
with `JNEXT_ENABLE_LTO=OFF` and every target built, the test executables
included; the Windows test tree is the precedent (the suites need no cross-TU
inlining, and an LTO link of the whole core per suite would cost about a hundred
times as much). Its test executables link with an 8 MB stack as the Windows ones
do, with `-sSTACK_OVERFLOW_CHECK=2` (a release module has no stack guard, so an
overflow would silently overwrite static data) and `-sEXIT_RUNTIME=1` (static
destructors, `atexit` and a stdio flush at exit, as natively) and a link at `-O1`
(the objects stay `-O3`; skipping the final wasm-opt pass turns a link of up to two
minutes into one second with no measurable cost in run time), all in
`test/CMakeLists.txt`; `jnext` keeps its own link options.

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
(`browser_pacing.h`): the next due time is the previous one plus the tick's own
cost plus its sleep, and a callback that arrives before it runs no tick. A grid
rather than "now plus the sleep" keeps 50 ticks a second on 60, 120 and 144 Hz
displays alike (the latter lands every tick on the second animation frame, 30 a
second). A stall (a hidden tab) resyncs as `frame_deadline.h` describes: one
tick at once, then the grid, never a burst. During a fast load a tick that asks
for no sleep may repeat inside one callback, bounded by an 8 ms budget. A first
NEX load on an empty file system records the warm-start state in one callback
(about 14 s under load): the page is unresponsive until it ends.

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

## Dev build versus release build

With no `-DCMAKE_BUILD_TYPE` given, CMake here defaults to **RelWithDebInfo**
instead of to the empty build type. An unoptimised `build/jnext` measured 5.8×
slower than Release, which made it a trap for anyone benchmarking anything.
RelWithDebInfo additionally keeps frame pointers, so `perf record -g` produces
usable call graphs from it. Release does not keep them: frame pointers were
measured at a 5.8% cost there, and that is an unacceptable permanent tax on the
binary users actually run.

Hence the rule that follows from those two facts: **any performance measurement
or benchmark uses a Release build**, never `build/jnext` — and which Release
build depends on the question. To compare two code changes, use
`build/gui-release-non-pgo/jnext` (`make gui-release-non-pgo`; `make bench`
uses it): its speed does not move when a PGO profile is retrained. To ask how
fast the shipped binary is, use `build/gui-release/jnext`, the PGO build
(`make bench BENCH_BUILD=gui-release`). When you do need to profile a release
binary, use `perf record --call-graph dwarf`.
[5.5](05-performance-and-optimisation.md#measuring-a-change) describes how to
measure a change.

LTO is enabled for **Release only**, through
`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`, and it is guarded by
`check_ipo_supported()` so that a toolchain without working LTO simply builds as
it did before. It earns its keep here because the emulator is split into
per-subsystem static libraries — eighteen in the shipped build with the Qt
GUI and the debugger, fifteen in the SDL-only one — and that split makes every hot
cross-library call — the CPU into `Mmu::read`, the CPU into `PortDispatch` —
un-inlinable at compile time.

`JNEXT_ENABLE_LTO=OFF` exists for exactly one build, the Flatpak one: the
`org.kde.Sdk` GCC miscompiles the QApplication init path under whole-program
LTO and segfaults at GUI launch. That was bisected to the LTO *process* in that
toolchain rather than to any single translation unit or transform, which is why
nothing short of disabling it helps. Every other build keeps LTO on —
including the Windows executables, which is why every `win-*-release` target
passes `-DCMAKE_BUILD_TYPE=Release` explicitly (`WIN_BUILD_TYPE` in the
Makefile). They used to pass no build type, fell back to
RelWithDebInfo, and shipped without LTO.

## Profile-guided optimisation

`make gui-release` — and every package, see
[5.3](03-packaging-and-release.md) — is a gcc **PGO** build: an
instrumented twin in `build/gui-release-pgo-gen` runs a short training set
(`tools/pgo-train.sh`), and `build/gui-release` is then compiled with the
profile it wrote to `build/gui-release-pgo-profile`. `cmake/JnextPgo.cmake`
adds the flags under the CMake option `JNEXT_PGO` (`GENERATE` or `USE`, with
the profile in `JNEXT_PGO_DIR`). Training needs the NextZXOS SD image, is
skipped when nothing that decides the profile changed, and a missing or
mismatched profile fails the build.
[5.5 Performance and optimisation](05-performance-and-optimisation.md#profile-guided-optimisation)
describes the pipeline, the training set, the gates and the measured gains.

## The stack-frame gate

No production function may need a stack frame over 256 KiB:
`-Werror=frame-larger-than=262144` on GCC, `-Wframe-larger-than=262144
-Werror=frame-larger-than` on Clang. It exists because Windows runs on
MinGW's default 2 MB main-thread reserve (jnext sets no `--stack`), and
`main()`'s frame once outgrew it, by a margin that depended on GCC's
stack-slot sharing: only the cross build saw it. `CMakeLists.txt` applies the
flag to `jnext` and to every `STATIC_LIBRARY` target under `src/` (derived from
the directory tree, not listed), at **both** compile and link: under LTO the
compile-time check is blind and only the link sees real frames. Test
executables are excluded on purpose: their fixtures hold `Emulator`s on the
stack and are never built for Windows. Under Clang LTO both halves are blind
(measured with clang 22 on Linux; ld64 untested), so only GCC enforces there. The runtime witness is `startup-stack-func`, which boots all three
frontends on a 1 MiB stack.

## ccache

`CMakeLists.txt` finds ccache and uses it as `CMAKE_{C,CXX}_COMPILER_LAUNCHER`.
This is what makes the project's mandatory clean-rebuild discipline affordable:
a clean rebuild of `gui-release` plus `build/` drops from roughly 65 s to 8 s on
a warm cache. Reverting a change and rebuilding — which is the core reviewer
move, and one this project asks for often — is a pure cache hit, because the
source is byte-identical to a state that has already been compiled once.

Give it room, once per machine; the 5 GB default thrashes on a tree this size:

```console
$ ccache -M 20G
```

That is a user-level setting rather than repository state, so it has to be
re-applied on any new machine. ccache never hits on the PGO build's optimised
tree; [5.5](05-performance-and-optimisation.md#ccache) explains why.
