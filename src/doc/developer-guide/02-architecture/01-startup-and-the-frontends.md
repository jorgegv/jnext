# 2.1 Startup and the frontends

Everything that happens before the first emulated instruction lives in
`src/main.cpp`. It parses the command line, resolves an SD-card image, builds
an `EmulatorConfig`, picks one of the three frontends, hands it the config and
runs it. There is no second entry point: the GUI, the SDL frontend and the
headless runner all reach the machine through that same function, which is what
keeps them from drifting apart.

## Parsing: the option table

Arguments are dispatched from `cli::OPTIONS` in `src/core/cli_options.h`, a
`constexpr` array whose rows are `{ name, arity, doc-status, id }`. The parsing
loop looks each argument up with `cli::find()`, consumes as many following
values as the row's `arity` says, and switches on `opt->id`.

Holding the flag set as *data* rather than as a chain of string comparisons is
not a style preference — it buys three separate guarantees:

- The parser physically cannot accept a flag that is not in the table. And
  because `CMakeLists.txt` sets `-Werror=switch`, a row added to the table with
  no matching arm in the `OptId` switch is a compile error rather than a flag
  that silently does nothing.
- `cli_options_test` can enumerate the table and diff it against the OPTIONS
  section of the man page in both directions. An implemented flag that nobody
  documented and a documented flag that nobody implemented are therefore
  equally hard failures of `make cli-check`.
- Where an exception is genuinely wanted, it is declared on the row itself
  instead of being hidden in the checker. There is exactly one: `--sd-card`,
  marked `Doc::UndocumentedAlias`, a back-compatible spelling that keeps
  working but is deliberately not advertised.

A numeric option value is parsed by `cli::parse_int` or `cli::parse_hex16`
(both in `cli_options.h`), never by a bare `std::stoi` or `std::stoul`: those
throw on `x`, which aborts the process, and read `5x` as 5. A value that is not
a whole number in the option's range is a usage error: `main.cpp` prints the
option and the offending value and exits 1.

`--set SECTION.KEY=VALUE` (GH #417) is one more row of the table, and its value
is checked at parse time against a second table, `config_keys::KEYS` in
`src/platform/config_keys.h`: the keys of `jnext.conf`. A key with a dedicated
flag (`--cpu-speed`, `--scale`, `--crt`, `--silent`, `--machine`, ...) and the
same key given with `--set` land in the **same variables**, in argument order,
so the later of the two wins. Where they take effect differs by frontend, and
the row of the table says where:

- **headless and the SDL-only build** read no `jnext.conf`; the command-line
  value is the whole setting. The CPU speed is programmed with the NR 0x07
  write `apply_startup_config()` does for the Qt window, right after
  `app.init()`. A key the frontend has no use for prints
  `warning: <spelling> has no effect ...` and the run goes on.
- **the Qt window** merges the Qt-only keys (`merge_cli_assignments`) and the
  CPU speed into its own copy of the saved values, then hands that copy to
  `MainWindow::apply_startup_config()`. The debugger keys of the run go to
  `set_session_debug_keys()` after `dbgkeys::apply_overrides()` has refused a
  clash, and `debugger.attached` to `DebuggerManager::set_attach_override()`.
  None of it reaches `MainWindow::app_config_`, the object that is saved.

Two argument shapes bypass the table lookup, both on purpose.
`--log-level=VALUE` is the one inline-value form and is matched by prefix
before the lookup happens, because scripts using that spelling predate the
table. And a bare argument that does not begin with `-` is taken as the program
to load, so `jnext game.tap` does what you would expect. Anything that *does*
begin with `-` and is not in the table is an error rather than a filename,
because a mistyped flag must never be quietly swallowed as one.

## Before the frontend exists

Two things happen in the gap between parsing and constructing a frontend.

First, in a Qt build on a non-headless run, `AppConfig` is loaded from
`~/.jnext/jnext.conf`, early enough that a saved SD-card path can seed the
resolution step below. **CLI values always win**: a saved value is consulted
only where the corresponding flag was absent. Headless runs do not read the
configuration file at all, because a preference saved on a developer's machine
must never be able to change the outcome of an automated run.

Second, the SD-card image is resolved by `sdcard::provision_sd_card()` in
`src/core/sdcard_provisioner.h`. It tries an explicit `--sdcard` first, then
the image cached under `~/.jnext/sdcard/`, and failing both it offers to
download and patch the canonical distribution image. Headless asks on the
terminal; the GUI spins up a temporary `QApplication` for the dialogs and tears
it down again before `QtApp` builds its own. If no image can be resolved,
startup fails outright — every ROM except the FPGA boot ROM is read out of that
image, exactly as on real hardware, so there is no machine to run without one.
A failed download is reported with the address it was fetching, a reason in plain
words and what happens next: in a dialog in the Qt GUI (a NextPi failure too, also
with `--nextpi`, which then exits) and on stderr and in the log otherwise. A fetch that
is slow or stalls says so the same way: a dialog, or lines on stderr and in the log.

## Choosing a frontend

The choice comes down to a single runtime condition wrapped around a single
compile-time arm:

```cpp
if (headless) {
    auto app_ptr = std::make_unique<HeadlessApp>();  // ... set-up on *app_ptr
    result = configure_and_run(*app_ptr);
} else {
#ifdef ENABLE_QT_UI
    auto app = std::make_unique<QtApp>();  result = configure_and_run(*app);
#else
    auto app = std::make_unique<SdlApp>(); result = configure_and_run(*app);
#endif
}
```

The frontends are heap-allocated on purpose: each holds the 1.17 MB `Emulator`
by value, so as automatic variables they were `main()`'s stack frame (GH #308).

`configure_and_run` is a generic lambda, and that is what lets a single body
drive three classes that share no base class. They implement the same informal
interface — `set_config`, `init`, `run`, `shutdown`, `emulator`, `exit_code` —
and the handful of steps that genuinely belong to only one of them are guarded
with `if constexpr` on the deduced type. `--delayed-snapshot` wiring is
`HeadlessApp`-only that way, `--delayed-pok-frames` is `HeadlessApp`- and
`SdlApp`-only (the Qt window refuses it in `main()`), and some GUI plumbing is
`QtApp`-only.

Most of the `set_*` calls in that body come *after* `init()`, because they
only make sense once the machine exists. So a frontend must act on what they
request in `run()` or in its frame loop, and never read the value back inside
`init()`: `QtApp` once read `--rzx-play` and `--rzx-record` at the end of
`init()`, always found them empty, and silently played and recorded nothing.
The command-line RZX requests are now started by one shared helper,
`emulator_start_rzx()` in `src/platform/rzx_startup.h`, which all three
frontends call at the top of `run()`; `rzx-frontends-func` checks every
spelling of them in every frontend. Its twin `emulator_finish_rzx()`, called
from every `shutdown()`, writes a recording still running and reports whether
the command-line one reached the disk — at exit or at any earlier point, since
`Emulator::stop_rzx_recording()` latches a failed path
(`Emulator::rzx_output_failed()`), as `VideoRecorder::output_failed()` does for
`--record` — so a lost recording exits non-zero (`rzx-record-status-func`).
`QtApp` calls it before an automatic exit's `quit()` too: `quit()` closes the
main window, and `MainWindow::closeEvent()` reports a recording it has to stop
in a modal dialog that an unattended run would wait on forever.

**Quit signals (GH #364).** SDL3's `SDL_Init` installs a persistent
SIGINT/SIGTERM handler that only queues `SDL_EVENT_QUIT`; `QtApp` never reads
that event, so the Qt GUI swallowed both signals, and the SDL frontend exited
with status 0. `quit_signal` (`src/platform/quit_signal.h`) takes the signals
over for every frontend: `main()` calls `quit_signal::install()` before any
frontend's `init()`, hence before `SDL_Init`, and SDL then installs no handler of
its own. The handler stores the signal number in a lock-free atomic (a second
quit signal gets the default action at once: the same one by `SA_RESETHAND`, the
other one because the handler resets it; a signal that was
`SIG_IGN` at start stays ignored). Each frame loop polls `pending()` once per
iteration and leaves through its normal teardown: `QtApp`'s `post_frames()` does
the unattended quit (finish the RZX recording, `set_unattended`, `quit()`),
`SdlApp::run()` and `HeadlessApp::run()` break out of their loops. The end of
`main()`, after every teardown, calls `quit_signal::reraise_if_pending()`, so
the process dies by the signal (status 128+N, `WIFSIGNALED`) and never reads as
a success. Windows is exempt: the helper is a no-op there, because no SIGTERM
can be delivered to a Windows process from outside and Ctrl+C reaches the GUI
exe only through the console it borrows. Tests: `quit_signal_test`
(QSIG-01..14), `quit-signal-func` (all three frontends, asserting the process
was terminated BY the signal, plus headless held paused by a DZRP client) and
`quit-signal-rzx-func` (Qt with a failing RZX write, Linux only).

- **`QtApp`** (`src/gui/qt_app.h`) owns a `QApplication` and a `MainWindow`
  whose central widget is an `EmulatorWidget`. A `QTimer` drives the machine,
  paced from `Emulator::frame_period_ms()` and the current speed multiplier.
  SDL is still linked into this build, but only to get audio out. Right after
  the `QApplication` exists it calls `DesktopColorScheme::install()` (light and
  dark, below).
- **`SdlApp`** (`src/platform/sdl_app.h`) owns an `SdlDisplay`, an `SdlInput`
  and an `SdlAudio`, and runs its own event and pacing loop. Its speed
  multiplier (`--speed`, `SdlApp::set_speed_percent()`) scales the whole-ms
  sleep, switches off audio pacing away from 1x and, above it, presents at most
  every `frame_sequencer::RENDER_INTERVAL_MS` — the same three things
  `QtApp` does, done inline because this loop does not use the sequencer.
- **`HeadlessApp`** (`src/platform/headless_app.h`) owns nothing but the
  `Emulator`. Its `run()` is a bare `run_frame()` loop with no sleep in it.

## Light and dark (GH #363)

The Qt frontend follows the desktop's light/dark preference, at start and
live. The decision is the pure `src/gui/color_scheme_policy.h`; the glue is
`src/gui/desktop_color_scheme.{h,cpp}` (its D-Bus signal receiver is
`desktop_color_scheme_portal.*`, a header of its own so that its meta-object
code stays out of the PGO-profiled sources), installed by `QtApp::init()` and by the
SD-card provisioning dialogs' temporary `QApplication`.

- **Inputs**, in precedence order: `JNEXT_COLOR_SCHEME` (`dark`, `light`,
  `system`; documented in the man page); on Linux, the XDG desktop portal's
  `org.freedesktop.appearance` `color-scheme` (1 dark, 2 light, 0 none), read
  over QtDBus with `ReadOne` (falling back to `Read`) and watched through its
  `SettingChanged` signal; otherwise nothing, and Qt's own detection stands.
  Qt misses the portal value under GNOME (it reports Light with the desktop on
  *prefer-dark*), which is the reason jnext asks itself. QtDBus is linked on
  Linux Qt 6 builds only (`JNEXT_HAVE_QTDBUS`); it is part of qtbase.
- **Applying** a request is `QStyleHints::setColorScheme()` (Qt 6.8+). Some
  platform themes report the new scheme and keep their old palette (Qt's
  `gtk3` theme under X11, measured), so after the theme has had its event-loop
  turn, a palette that still does not match gets Fusion's palette for that
  scheme (`DesktopColorScheme::fusion_palette()`: identical to Qt 6.11's
  Fusion palette in every role and group for both schemes when compared on the
  development host; `color_scheme_test` CS-09 re-checks the light one). Below Qt 6.8 the palette is
  the only lever. Qt 5 builds do none of this.
- **What follows the palette.** The toolbar icons re-render on
  `QEvent::PaletteChange` (`toolbar_icons::bind`). The debugger panels' own
  colours (table tints, separators, the HALTED and flag labels) go through
  `src/qt/palette_tint_qt.h`: the colour a panel names, unchanged, on a light
  palette, and its dark counterpart (lightness mirrored around the dark Base)
  otherwise. Two Qt behaviours shape it, both measured on 6.11: an application
  palette change reaches only the application and the top-level widgets as
  `ApplicationPaletteChange`, and a widget with a style sheet keeps, with its
  whole subtree, the palette it had when the sheet was applied. So
  `palette_tint::follow()` re-sets every followed sheet from one application
  listener after each change, and `DebuggerWindow::changeEvent()` re-runs
  `refresh_panels()` (queued) so table cells re-colour without a step.
  `debugger_panels_test` QDARK-01..10 pin it, one row per converted site (the
  Preferences label is `color_scheme_test` CS-27).
- **Tests.** `color_scheme_test` pins the policy (CS-01..08), the glue fed
  directly (CS-09..17), and the real D-Bus path (CS-18..22) against a mock
  `org.freedesktop.portal.Settings` that the suite serves itself on a private
  `dbus-daemon` with no service activation: `ReadOne`, the version-1 `Read`,
  `install()` reading it, and the checked `SettingChanged` subscription. CS-23/24
  are source checks that `QtApp::init()` and the SD-card dialogs install the
  follower; CS-25/26 exercise both arms of the Qt 6.8 guard on one build
  (`set_scheme_request_enabled(false)` runs the palette-only arm a Qt 6.4 build
  takes).

## Compile-time arms

| Option | Default | Effect |
|---|---|---|
| `ENABLE_QT_UI` | `OFF` in `CMakeLists.txt`; **`ON`** in every `make` target that builds a usable binary | Compiles `src/gui`, links `jnext_gui`, selects `QtApp` over `SdlApp`. |
| `ENABLE_DEBUGGER` | `ON` | Compiles `src/debugger` (the Qt panels). Turning it off removes the debugger UI entirely; `src/debug`, the pure-C++ backend it sits on, is always built. |
| `ENABLE_TESTS` | `ON` | Builds the unit suites and registers them with CTest. |

The mismatch on the first row catches people out and is worth knowing: a raw
`cmake` invocation with no options produces an SDL-only binary, which is why
every Makefile target that is meant to yield a usable build passes
`-DENABLE_QT_UI=ON` explicitly. See
[5.2 Build configurations](../05-building/02-build-configurations.md).

## What headless actually skips

`--headless` is a runtime flag understood by either build, not a separate
binary. It opens no window and no audio device, registers no host input
dispatchers, and never sleeps, so the frame loop advances as fast as the host
can manage. Everything else is the same machine doing the same work: the same
`Emulator`, the same rendering, and the same audio synthesis unless `--silent`
is given. That equivalence is what makes headless useful for measurement, and
it is why `--benchmark` is headless-only — run under a GUI frontend it would be
measuring the frame timer rather than the emulator.

Headless is also where JNEXT's deterministic automation lives. The
`--delayed-screenshot`, `--delayed-snapshot`, `--delayed-keypress` and
`--delayed-automatic-exit` family lets a script boot the machine, wait a
defined amount of emulated time, press keys and capture the screen without any
human present (`--delayed-snapshot`, `--delayed-keypress` and `--delayed-nmi`
are headless-only: only `HeadlessApp` reads them, so `main.cpp` refuses them
without `--headless` rather than let a windowed frontend ignore them — the same
up-front refusal it gives an option that only qualifies one that is absent,
such as `--inject-pc` without `--inject`) — which is how the project's own screenshot regression suite
drives the emulator. The delays are counted in emulated frames rather than
wall-clock seconds precisely so that a loaded machine produces the same bytes
as an idle one. And a capture that was requested but never taken makes the
process exit non-zero rather than quietly writing nothing, so a broken test run
cannot pass by producing no output. The same holds for any other work the
command line deferred — a `--load` or `--inject` still waiting, a keypress, an
NMI, a POK trainer file, a recording, a serial stream or ESP outage edge still to
come — when the
automatic exit fires first: each frontend asks `auto_exit_finds_no_deferred_work()`
(`src/platform/auto_exit.h`) at that moment. See [2.5](05-save-state-and-rewind.md) and
[4.3 The regression suite](../04-testing/03-the-regression-suite.md).
