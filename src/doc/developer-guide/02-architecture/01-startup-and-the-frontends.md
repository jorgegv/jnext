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

## Choosing a frontend

The choice comes down to a single runtime condition wrapped around a single
compile-time arm:

```cpp
if (headless) {
    HeadlessApp app;  result = configure_and_run(app);
} else {
#ifdef ENABLE_QT_UI
    QtApp app;        result = configure_and_run(app);
#else
    SdlApp app;       result = configure_and_run(app);
#endif
}
```

`configure_and_run` is a generic lambda, and that is what lets a single body
drive three classes that share no base class. They implement the same informal
interface — `set_config`, `init`, `run`, `shutdown`, `emulator`, `exit_code` —
and the handful of steps that genuinely belong to only one of them are guarded
with `if constexpr` on the deduced type. `--delayed-snapshot` wiring is
`HeadlessApp`-only that way, and some GUI plumbing is `QtApp`-only.

- **`QtApp`** (`src/gui/qt_app.h`) owns a `QApplication` and a `MainWindow`
  whose central widget is an `EmulatorWidget`. A `QTimer` drives the machine,
  paced from `Emulator::frame_period_ms()` and the current speed multiplier.
  SDL is still linked into this build, but only to get audio out.
- **`SdlApp`** (`src/platform/sdl_app.h`) owns an `SdlDisplay`, an `SdlInput`
  and an `SdlAudio`, and runs its own event and pacing loop.
- **`HeadlessApp`** (`src/platform/headless_app.h`) owns nothing but the
  `Emulator`. Its `run()` is a bare `run_frame()` loop with no sleep in it.

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
human present — which is how the project's own screenshot regression suite
drives the emulator. The delays are counted in emulated frames rather than
wall-clock seconds precisely so that a loaded machine produces the same bytes
as an idle one. And a capture that was requested but never taken makes the
process exit non-zero rather than quietly writing nothing, so a broken test run
cannot pass by producing no output. See [2.5](05-save-state-and-rewind.md) and
[4.3 The regression suite](../04-testing/03-the-regression-suite.md).
