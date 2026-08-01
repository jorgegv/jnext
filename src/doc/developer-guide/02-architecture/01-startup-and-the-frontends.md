# 2.1 Startup and the frontends

Everything before the first emulated instruction happens in `src/main.cpp`:
parse the command line, resolve an SD-card image, build an `EmulatorConfig`,
pick a frontend, hand it the config, run it. There is no other entry point —
all three frontends go through the same function.

## Parsing: the option table

Arguments are dispatched from `cli::OPTIONS` in `src/core/cli_options.h`, a
`constexpr` array of `{ name, arity, doc-status, id }`. The loop looks each
argument up with `cli::find()`, consumes `arity` values, and switches on
`opt->id`.

Making the flag set *data* rather than an `if` chain buys three things at once:

- The parser cannot accept a flag that is not in the table, and
  `-Werror=switch` on the `OptId` switch (set in `CMakeLists.txt`) makes a
  table entry with no parser arm a compile error.
- `cli_options_test` can enumerate the table and diff it against the man page's
  OPTIONS section in both directions, so an undocumented flag and a documented
  non-flag are equally hard failures of `make cli-check`.
- Deliberate exceptions are declared on the row itself. There is exactly one —
  `--sd-card`, marked `Doc::UndocumentedAlias`, a back-compat spelling that is
  kept working but not advertised.

Two shapes sit outside the table lookup, both on purpose. `--log-level=VALUE`
is the single inline-value form, handled by prefix before lookup because
scripts predate the table. And a bare argument that does not start with `-` is
taken as the program to load, so `jnext game.tap` works; anything starting
with `-` is an unknown option and an error, because a mistyped flag must never
be swallowed as a filename.

## Before the frontend exists

Two things happen between parsing and construction.

In a Qt build and a non-headless run, `AppConfig` is loaded from
`~/.jnext/jnext.conf` early enough to seed the SD-card path. **CLI values
always win**: the saved value is only consulted where the flag was absent.
Headless runs never read the config at all — a saved preference must not be
able to change what an automated run does.

Then the SD-card image is resolved by `sdcard::provision_sd_card()`
(`src/core/sdcard_provisioner.h`), in order: an explicit `--sdcard`, the
cached image under `~/.jnext/sdcard/`, or an offer to download and patch the
canonical distribution image. Headless gets CLI prompts, the GUI gets a
temporary `QApplication` and dialogs — torn down before `QtApp` builds its
own. Failing to resolve an image is fatal: the SD card is where every ROM
except the FPGA boot ROM comes from, exactly as on real hardware.

## Choosing a frontend

The choice is one runtime condition and one compile-time arm:

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

`configure_and_run` is a generic lambda, which is what lets one body serve
three unrelated classes: they share an informal interface (`set_config`,
`init`, `run`, `shutdown`, `emulator()`, `exit_code()`), and the few
frontend-specific steps are guarded with `if constexpr` on the deduced type —
`--delayed-snapshot` is `HeadlessApp`-only, some GUI wiring is `QtApp`-only.

- **`QtApp`** (`src/gui/qt_app.h`) owns a `QApplication`, a `MainWindow` with
  an `EmulatorWidget` central widget, and a `QTimer` that calls `run_frame()`
  paced from `Emulator::frame_period_ms()` and the speed multiplier. SDL is
  still linked, but only for audio output.
- **`SdlApp`** (`src/platform/sdl_app.h`) owns an `SdlDisplay`, `SdlInput` and
  `SdlAudio` and runs its own event/pace loop.
- **`HeadlessApp`** (`src/platform/headless_app.h`) owns nothing but the
  `Emulator`. Its `run()` is a bare `run_frame()` loop with no sleep.

## Compile-time arms

| Option | Default | Effect |
|---|---|---|
| `ENABLE_QT_UI` | `OFF` in `CMakeLists.txt`; **`ON`** in every `make` target that builds a usable binary | Compiles `src/gui`, links `jnext_gui`, selects `QtApp` over `SdlApp`. |
| `ENABLE_DEBUGGER` | `ON` | Compiles `src/debugger` (Qt panels). Turning it off removes the debugger UI entirely; `src/debug`, the pure-C++ backend, is always built. |
| `ENABLE_TESTS` | `ON` | Builds the unit suites and registers them with CTest. |

The mismatch on the first row is worth knowing about: a raw `cmake` with no
flags produces an SDL-only binary, which is why the Makefile targets pass
`-DENABLE_QT_UI=ON` explicitly. See
[5.2 Build configurations](../05-building/02-build-configurations.md).

## What headless actually skips

`--headless` is a runtime flag in either build, not a separate binary. It
opens no window and no audio device, registers no host input dispatchers, and
never sleeps — the frame loop runs at whatever speed the host manages.
Everything else is identical: the same `Emulator`, the same rendering, the
same audio synthesis (unless `--silent`). That matters for measurement, and
`--benchmark` is headless-only for exactly that reason: in a GUI frontend it
would measure the frame timer.

Headless is also where the deterministic automation lives — `--delayed-screenshot`,
`--delayed-snapshot`, `--delayed-keypress`, `--delayed-automatic-exit` — all
counted in emulated frames rather than wall-clock seconds. A requested capture
that was never taken makes the process exit non-zero rather than quietly
writing nothing. See [2.5](05-save-state-and-rewind.md) and
[4.3 The regression suite](../04-testing/03-the-regression-suite.md).
