# 5.3 Packaging and release

`doc/RELEASE-PROTOCOL.md` is the authority for everything on this page. What
follows is the shape of the system, not a replacement for it.

## The package targets

| Target | Produces |
|---|---|
| `make package-src` | source tarball plus `jnext-<ver>-src.zip`, with submodule content vendored |
| `make package-rpm` | `.rpm` via CPack, in `build/rpm-release/` |
| `make package-deb` | `.deb` via CPack, in `build/deb-release/` |
| `make package-win` | Windows x64 Qt6 `.zip` — MinGW cross-build, Qt6/SDL2/SDL3 DLLs and the `qwindows` plugin bundled |
| `make package-win-qt5` / `package-win32-qt5` | the Windows 7-compatible Qt5 zips, 64- and 32-bit |
| `make package-win-sdl` / `package-win32-sdl` | SDL-only Windows zips — repo-internal validation legs, not published |
| `make package-flatpak` | Flatpak bundle, in `build/flatpak-release/` |
| `make package-macos` | macOS `.dmg` — Darwin only; prints a SKIP and exits cleanly elsewhere |
| `make package-test` | builds every package except macOS and **asserts its contents** |
| `make package-contract-test` | the packaging-script contract suites only — hermetic, about 4 s, no toolchain |

The source tarball is not a `git archive`: that produces an *empty*
`third_party/spdlog` directory, which fails CMake configure. `package-src` runs
`packaging/make-dist-tarball.sh`, which vendors the submodule content properly.

`package-test` matters more than it looks. It builds each package and then
checks what is inside — that the rpm and deb carry `bin/jnext`, that the source
zip carries the vendored submodule, that the Windows zip carries its DLLs and
its platform plugin, and that `jnext.exe` is a GUI-subsystem binary with no
stray console window. For a long time nothing invoked it, which is how a
permanently-failing packaging row survived 46 tags. It now runs as its own
parallel CI job, and its hermetic half (`package-contract-test`) is a
prerequisite of `make unit-test`.

## `version.yaml` is the single source of truth

Everything else derives from it. CMake reads it into `PROJECT_VERSION`, so
every CPack-generated package already carries the right version with no further
work.

The files a *person* wrote by hand are kept in lockstep by
`packaging/sync-version.sh`, which the `bump-*` targets call automatically. It
knows about four:

- `packaging/rpm/jnext.spec` — the `Version:` field plus a matching
  `%changelog` entry (rpmbuild complains if the top changelog version and
  `Version:` disagree)
- `packaging/assets/io.github.zxjogv.jnext.metainfo.xml` — the AppStream
  `<releases>` history, **for public releases only**
- `packaging/debian/changelog`
- `mkdocs.yml` — the version stamped into every page of the user guide, which
  is why the script re-renders that guide too

The Flatpak manifest is deliberately not in the list: it builds from the local
checkout and carries no version tag to rewrite.

**When you add a file that hard-codes the version, add it to
`sync-version.sh`.** That script is the one place that must know them all. It
is idempotent, it fails loudly if a target file or the anchor an edit depends
on is missing — better to abort the whole bump than to commit a half-synced
tree — and it is covered by a contract suite inside `make package-test`.

## Bumping, and the public/private distinction

**A git tag is not the same as a public release.** Every merge to `main` gets
its own `vX.Y.Z` tag, and those are local history markers. Only the curated
subset listed in `releases.yaml` becomes a public GitHub Release.

All three of `bump-patch`, `bump-minor` and `bump-major` behave the same way.
They refuse to run on a dirty working tree; compute the new version; **prompt
`Add vX.Y.Z to releases.yaml (build a public GitHub Release)? [y/N]`**, default
No; write `version.yaml`; run `sync-version.sh`; then stage, commit
`chore: bump version to <ver>` and create the tag. Every step is `&&`-chained,
so a failed sync commits and tags nothing.

Two consequences worth knowing. The prompt only appears on a terminal — a
non-interactive shell answers `n`, so a scripted bump is always private.
And when you answer `y`, the tag is added to `releases.yaml` *before* tagging,
so the tag's own commit lists itself; the release workflow reads that file from
the tag's commit, and would otherwise never see it.

`make publish-release` pushes the branch and then the newest tag **alone**.
Single-tag pushes are not fussiness: GitHub fires tag events only for three or
fewer tags pushed at once, and pushing more creates *zero* events, so nothing
builds. The target also refuses outright if the newest tag is public but has no
ChangeLog entry.

## The ChangeLog rule

**Version headers in the ChangeLog correspond to public releases only** — the
tags in `releases.yaml` — never to intermediate private patch tags. Everything
since the last public release accumulates under a single `## Unreleased` header,
which is renamed to the version and date when a release is actually cut.

A public bump must have the ChangeLog updated *before* the bump commit, so the
released tag carries its own entry, and that entry is differential: what changed
since the previous public release, not the whole history. Four sections, one
line each, 10-20 words. If a bullet wraps, it is too long — cut it rather than
reflowing it. `doc/RELEASE-PROTOCOL.md` §2.1 has the reasoning and the
counter-example that motivated the rule.

A public bump also requires the documentation to be checked against the running
product, which is a separate thing from the automatic staleness gates —
see [5.4](04-continuous-integration.md) and
[4.5](../04-testing/05-documentation-and-cli-gates.md) for what those gates do
and do not prove.
