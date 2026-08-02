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
| `make package-win` | Windows x64 Qt6 `.zip` — MinGW cross-build, Qt6/SDL2/SDL3 DLLs and the `qwindows` plugin bundled |
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

`package-test` matters more than its name suggests. It does not merely build
each package: it then looks inside and checks what is there — that the rpm and
deb carry `bin/jnext`, that the source zip carries the vendored submodule, that
the Windows zip carries its DLLs and its platform plugin, and that `jnext.exe`
is a GUI-subsystem binary with no stray console window attached. Nothing
actually invoked it, which is how a permanently-failing packaging row managed to
survive 46 tags unnoticed. It now runs as its own parallel CI job,
and its hermetic half, `package-contract-test`, is a prerequisite of
`make unit-test`.

## `version.yaml` is the single source of truth

Everything else derives from it. CMake reads it into `PROJECT_VERSION`, so every
CPack-generated package already carries the right version with no further work
from anyone.

The files that a *person* wrote by hand are the ones that need help, and
`packaging/sync-version.sh` keeps them in lockstep. The `bump-*` targets call it
automatically. It knows about four files:

- `packaging/rpm/jnext.spec` — the `Version:` field plus a matching
  `%changelog` entry, because rpmbuild complains when the top changelog version
  and `Version:` disagree
- `packaging/assets/io.github.zxjogv.jnext.metainfo.xml` — the AppStream
  `<releases>` history, **for public releases only**
- `packaging/debian/changelog`
- `mkdocs.yml` — the version stamped into every page of the user guide, which is
  also why the script re-renders that guide

The Flatpak manifest is deliberately absent from that list: it builds from the
local checkout and carries no version tag to rewrite.

**When you add a file that hard-codes the version, add it to
`sync-version.sh`.** That script is the one place that must know them all. It is
idempotent; it fails loudly when a target file, or the anchor an edit depends
on, is missing — aborting the whole bump is better than committing a half-synced
tree — and it is covered by a contract suite inside `make package-test`.

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
