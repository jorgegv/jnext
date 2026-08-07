# 5.1 Make targets

The place to start is `make` with no arguments, which prints every target
together with its description:

```console
$ make

Available targets:

  sdl-debug        Configure and build the SDL-only frontend in Debug mode ...
  ...
```

There are 69 documented targets at the time of writing. The sections below
group them into families rather than listing them one by one, because `make`
is the authority here and the exact number changes.

## The self-documenting convention

The convention is a single rule: a `# ` comment line **immediately above a
target** *is* that target's description. The default target is a small awk
program over `$(MAKEFILE_LIST)` that pairs each such comment with the target
underneath it. Nothing else is maintained — there is no second list of targets
to keep in sync — so a target that exists is a target `make` shows.

There is one sharp edge. The awk keeps only the **last** `# ` line before a
target, so a second comment line silently replaces the description rather than
extending it. That is not a hypothetical: eight targets, `unit-test` and
`regression` among them, advertised a sentence fragment for as long as nobody
happened to re-read the listing. The rule is therefore enforced rather than
remembered. `make lint-makefile-help` fails if any target's description would
be discarded, and it is a prerequisite of both `make unit-test` and
`make regression`; it costs about 8 ms of awk over one file, so it fails long
before anything expensive has started.

That gives you the rule to follow when you add a target: **one `# ` line above
it, and every word of rationale inside the recipe as `@#` comment lines.** The
Makefile is full of those, and they are where the reasoning actually lives.

## Build

`sdl-*` builds the SDL-only frontend and `gui-*` builds the Qt6 GUI, each in a
`-debug` and a `-release` flavour. Every build variant also has a `-run` target,
which builds and then runs, and a `-clean` target; `make clean` removes all of
them along with `build/`.

The `win-*` family cross-compiles Windows executables with the MinGW toolchain.
`win-release` is the x64 Qt6 build; `win-qt5-release` and `win32-qt5-release`
are the legacy Qt5 legs, 64- and 32-bit, which keep a lower Windows floor than
the Qt6 build can offer; and `win-sdl-release` / `win32-sdl-release` are
SDL-only legs used to validate the cross-build rather than to publish anything.
Separately, `qt5-guard-build` compiles the GUI against native Linux Qt5, purely
to keep that combination building.

## Test

`make unit-test` builds `build/` and runs every declared subsystem suite, and
`make regression` runs the screenshot and functional suite headless. Between
them they pull in a set of structural gates as prerequisites —
`lint-assertions`, `lint-makefile-help`, `cli-check`, `docs-check`,
`traceability-check` and friends — which is deliberate: it puts those checks in
your inner loop instead of leaving them for CI to discover. Alongside them,
`make harness-selftest` proves that the test harness itself fails loudly when
faults are injected into it, `make build-matrix` builds every combination of the
frontend options, and `make unit-test-dashboard` runs the unit tests and
refreshes the committed per-subsystem status table.

Chapter [4. Testing](../04-testing/index.md) covers what each of these actually
proves.

## Documentation

Each documentation source generates a committed output, and each output has a
staleness check guarding it:

| Target | Renders | Guard |
|---|---|---|
| `docs-man` | `doc/man/jnext.1` + `USAGE.md` from `doc/man/jnext.1.md` | `docs-man-check` |
| `docs-userguide` | `doc/user-guide` from `src/doc/user-guide` | `docs-userguide-check` |
| `docs-devguide` | `doc/developer-guide` from `src/doc/developer-guide` | `docs-devguide-check` |
| `docs-devguide-diagrams` | the guide's committed SVGs from Graphviz `.dot` sources | (same check) |

`make docs-check` runs all three guards and aggregates their results, so one
stale document never hides another behind it. It is a prerequisite of both test
targets. For reading rather than checking, `make read-userguide` and
`make read-devguide` serve the rendered sites on localhost.

## Packaging and version

`package-src`, `package-rpm`, `package-deb`, `package-flatpak`, `package-macos`
and the five `package-win*` variants each build one distributable.
`package-test` builds them all except macOS and then asserts their contents,
while `package-contract-test` runs only the hermetic script contract suites.
`version` prints the current version, and `bump-patch` / `bump-minor` /
`bump-major` bump it, commit, and tag. [5.3](03-packaging-and-release.md) covers
what those actually do.
