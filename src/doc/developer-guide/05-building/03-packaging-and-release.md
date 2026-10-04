# 5.3 Packaging and release

`doc/RELEASE-PROTOCOL.md` is the authority for everything on this page. What
follows describes the shape of the system so that the protocol makes sense when
you read it; it is not a replacement for it.

## The package targets

| Target | Produces |
|---|---|
| `make package-src` | source tarball plus `jnext-<ver>-src.zip`, with submodule content vendored |
| `make package-rpm` | `.rpm` via CPack, in `build/rpm-release/` |
| `make package-deb` | `.deb` via CPack, in `build/deb-release/` |
| `make package-win` | Windows x64 Qt6 `.zip` — MinGW cross-build, Qt6/SDL3 DLLs and the `qwindows` plugin bundled |
| `make package-win-qt5` / `package-win32-qt5` | the legacy Qt5 Windows zips, 64- and 32-bit, which keep a lower Windows floor than the Qt6 build |
| `make package-win-sdl` / `package-win32-sdl` | SDL-only Windows zips — repo-internal validation legs, not published |
| `make package-flatpak` | Flatpak bundle, in `build/flatpak-release/` |
| `make package-macos` | macOS `.dmg` — Darwin only; prints a SKIP and exits cleanly elsewhere |
| `make package-test` | builds every package except macOS and **asserts its contents** |
| `make package-contract-test` | the packaging-script contract suites only — hermetic, about 4 s, no toolchain |

The source tarball is deliberately not a `git archive`. That would produce an
*empty* `third_party/spdlog` directory, which fails CMake configure the moment
anyone tries to build from it; `package-src` instead runs
`packaging/make-dist-tarball.sh`, which vendors the submodule content properly.

Every package's `jnext` is a **PGO build**, the same as `make gui-release`
([5.5](05-performance-and-optimisation.md#profile-guided-optimisation)): the package
targets call `tools/pgo-build.sh`, which configures an instrumented twin of the
package's tree, trains it and rebuilds the tree with the profile. Each package
build therefore needs the SD image (provisioned by download when missing), and
some platforms need more:

- the Windows legs are Release builds with LTO, like the Linux packages,
  and are trained under **wine** with no display, the GUI run on Qt's
  offscreen platform (its plugin is bundled next to the instrumented exe
  only). No wine is a hard error; `WIN_PGO=0` (or `make win-release-non-pgo`)
  builds without PGO.
  The wine prefix is refreshed (`wineboot -u`) on every build, so a prefix
  made while the host wine was broken does not stay broken. After the
  optimised build, `tools/pgo-build.sh` builds `fuse_z80_test.exe` in the
  same tree and runs it under wine, and the build fails unless all 1356 FUSE
  cases pass. It is the same check as the Flatpak's, for the same reason: a
  different toolchain plus LTO.
  The repository-internal SDL-only Windows legs are not PGO builds, but
  are Release builds with LTO too;
- the **Flatpak** trains inside the build sandbox: the distribution zip is a
  manifest source, jnext's own provisioner turns it into the SD image there,
  and the FUSE Z80 suite must pass against the PGO-built CPU core before the
  bundle is installed — the KDE SDK's gcc is the one that miscompiles jnext
  under LTO, so its output gets that extra check;
- **macOS** uses clang's instrumentation (`-fprofile-instr-generate`, merged
  with `llvm-profdata` from Xcode). There a stale profile is an error but a
  translation unit with no profile at all is not detected per unit.

`package-test` matters more than its name suggests. It does not merely build
each package: it then looks inside and checks what is there — that the rpm and
deb carry `bin/jnext`, that the source zip carries the vendored submodule, that
the Windows zip carries its DLLs and its platform plugin, and that `jnext.exe`
is a GUI-subsystem binary with no stray console window attached. Each row
checks the artifact named for the current `version.yaml` version, after
deleting earlier ones: a build directory keeps every previous version's
artifacts, and a row that took the first match once passed on the previous
version's leftover. Nothing
actually invoked it, which is how a permanently-failing packaging row managed to
survive 46 tags unnoticed. It now runs as its own parallel CI job,
and its hermetic half, `package-contract-test`, is a prerequisite of
`make unit-test`.

## `version.yaml` is the single source of truth

No other file carries a copy of the version: everything that needs it reads
`version.yaml` when it is built.

- CMake reads it into `PROJECT_VERSION`, so the binary and every CPack-generated
  package carry the right version, and the `package-*` recipes read it to name
  their artifacts.
- Both mkdocs configs load the hook `src/doc/version_hook.py`, which sets
  `extra.doc_release` (the "This version" in each guide's header) from it at
  render time, and stops the build when it is missing or malformed rather than
  rendering a wrong version.
- The native rpm spec takes it as `rpmbuild --define "jnext_version X.Y.Z"` and
  refuses to parse without it; its top `%changelog` entry is generated from the
  same value, so it always matches `Version:`.
- The native Debian package's `debian/changelog` is written at build time by
  `packaging/gen-debian-changelog.sh`, whose top entry is the version and whose
  tail is the committed `changelog.history`.
- The Flatpak manifest builds from the local checkout and carries no version.

So a bump edits nothing by hand except `version.yaml`. What it must still
commit is done by `packaging/sync-version.sh`, which the `bump-*` targets call:
it re-renders the two committed guides, whose every page shows the version, and
for a **public release only** adds the AppStream `<release>` entry to
`packaging/assets/io.github.zxjogv.jnext.metainfo.xml`. That entry stays
committed because it carries the release date, which `releases.yaml` does not
record and a source-tarball build has no git history to look up.

**Do not add a hard-coded copy of the version anywhere: read `version.yaml` at
build time.** The contract suite `test/packaging/sync-version-test.sh`, part of
`make package-contract-test` and so of `make unit-test`, fails when a copy of
the current version appears in a packaging, CI, CMake or mkdocs file, and also
pins the behaviour of the script, the hook, the spec and the changelog
generator.

## Bumping, and the public/private distinction

There is a distinction here that is easy to miss because most projects do not
make it. **A git tag is not the same thing as a public release.** Every merge
to `main` gets its own `vX.Y.Z` tag, and the great majority of those are private
history markers that exist only so a change can be pointed at later. Only a
curated subset ever becomes a public GitHub Release, and the way a tag joins
that subset is by being listed in `releases.yaml` — an explicit allowlist file
in the repository. The release workflow builds nothing for a tag that is not in
it. So the interesting question at bump time is not "which number" but "is this
one public", and the tooling asks you exactly that.

All three of `bump-patch`, `bump-minor` and `bump-major` behave the same way.
They refuse to run on a dirty working tree; compute the new version; **prompt
`Add vX.Y.Z to releases.yaml (build a public GitHub Release)? [y/N]`**, with No
as the default; write `version.yaml`; run `sync-version.sh`; and then stage,
commit `chore: bump version to <ver>`, and create the tag. Every step is
`&&`-chained, so a failed sync commits nothing and tags nothing.

Two consequences of that flow are worth knowing in advance. The prompt only
appears on a terminal, so a non-interactive shell answers `n` and a scripted
bump is always private. And when you do answer `y`, the tag is added to
`releases.yaml` *before* the tagging step, which means the tag's own commit is
the one that lists it — necessary because the release workflow reads that file
from the tag's commit and would otherwise never see the entry.

`make publish-release` pushes the branch and then the newest tag **alone**.
The single-tag push is not fussiness: GitHub fires tag events only when three or
fewer tags arrive at once, and pushing more than that creates *zero* events, so
nothing builds at all. The target also refuses outright when the newest tag is
public but has no ChangeLog entry.

## The ChangeLog rule

**Version headers in the ChangeLog correspond to public releases only** — the
tags listed in `releases.yaml` — and never to intermediate private patch tags.
Everything since the last public release accumulates under a single
`## Unreleased` header, which is renamed to the version and the date when a
release is actually cut.

A public bump must have the ChangeLog updated *before* the bump commit, so that
the released tag carries its own entry rather than pointing forward at one. That
entry is differential: it describes what changed since the previous public
release, not the whole history. Four sections, one line each, 10-20 words. If a
bullet wraps, it is too long, and the fix is to cut it rather than to reflow it.
`doc/RELEASE-PROTOCOL.md` §2.1 has the reasoning and the counter-example that
motivated the rule.

A public bump also requires the documentation to be checked against the running
product, which is a genuinely different thing from the automatic staleness
gates — see [5.4](04-continuous-integration.md) and
[4.5](../04-testing/05-documentation-and-cli-gates.md) for what those gates do
and do not prove.
