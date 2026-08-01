# 5.1 Make targets

Run `make` with no arguments. It prints every target with its description:

```console
$ make

Available targets:

  sdl-debug        Configure and build the SDL-only frontend in Debug mode ...
  ...
```

There are 69 documented targets at the time of writing, and the list below
groups them rather than enumerating them — `make` is the authority, and the
number changes.

## The self-documenting convention

A `# ` comment line **immediately above a target** *is* that target's
description. The default target is a small awk program over `$(MAKEFILE_LIST)`
that pairs each such comment with the target underneath it. Nothing else is
maintained: there is no second list of targets to keep in sync, so a target
that exists is a target `make` shows.

The awk keeps only the **last** `# ` line before a target, which means a second
comment line silently replaces the description. That is not hypothetical —
eight targets, `unit-test` and `regression` among them, once advertised a
sentence fragment for as long as nobody re-read the listing. So the rule is
enforced rather than remembered: `make lint-makefile-help` fails if any
target's description would be discarded, and it is a prerequisite of both
`make unit-test` and `make regression`. It costs about 8 ms of awk over one
file, so it fails before anything expensive starts.

The corollary, when you add a target: **one `# ` line above it; every word of
rationale goes inside the recipe as `@#` comment lines.** The Makefile is full
of those, and they are where the reasoning lives.

## Build

`sdl-*` builds the SDL-only frontend, `gui-*` the Qt6 GUI, each in `-debug`
and `-release`. Every build variant also has a `-run` target (build, then run)
and a `-clean` target. `make clean` removes all of them plus `build/`.

The `win-*` family cross-compiles Windows executables with Fedora's MinGW
toolchain: `win-release` (x64, Qt6), `win-qt5-release` and `win32-qt5-release`
(the Windows 7-compatible Qt5 legs, 64- and 32-bit), and the SDL-only
`win-sdl-release` / `win32-sdl-release`, which are repo-internal validation
legs rather than published packages. `qt5-guard-build` builds the GUI against
native Linux Qt5 to keep that combination compiling.

## Test

`make unit-test` builds `build/` and runs every declared subsystem suite;
`make regression` runs the screenshot and functional suite headless. Between
them they pull in a set of structural gates as prerequisites —
`lint-assertions`, `lint-makefile-help`, `cli-check`, `docs-check`,
`traceability-check` and friends — so those run in your inner loop rather than
only in CI. `make harness-selftest` proves the harness itself fails loudly on
injected faults, and `make build-matrix` builds every frontend option
combination. `make unit-test-dashboard` runs the unit tests and refreshes the
committed per-subsystem status table.

Chapter [4. Testing](../04-testing/index.md) covers what each of these
actually proves.

## Documentation

Sources generate committed outputs, and a staleness check guards each:

| Target | Renders | Guard |
|---|---|---|
| `docs-man` | `doc/man/jnext.1` + `USAGE.md` from `doc/man/jnext.1.md` | `docs-man-check` |
| `docs-userguide` | `doc/user-guide` from `src/doc/user-guide` | `docs-userguide-check` |
| `docs-devguide` | `doc/developer-guide` from `src/doc/developer-guide` | `docs-devguide-check` |
| `docs-devguide-diagrams` | the guide's committed SVGs from Graphviz `.dot` sources | (same check) |

`make docs-check` runs all three guards and aggregates their results, so one
stale document never hides another. It is a prerequisite of both test targets.
`make read-userguide` and `make read-devguide` serve the rendered site on
localhost for reading.

## Packaging and version

`package-src`, `package-rpm`, `package-deb`, `package-flatpak`,
`package-macos` and the five `package-win*` variants each build one
distributable; `package-test` builds them all (except macOS) and asserts their
contents; `package-contract-test` runs only the hermetic script contract suites.
`version` prints the current version, and `bump-patch` / `bump-minor` /
`bump-major` bump it, commit and tag. [5.3](03-packaging-and-release.md)
covers those.
