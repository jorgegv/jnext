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
| `make package-win-qt5` / `package-win32-qt5` | the legacy Qt5 Windows zips, 64- and 32-bit, which keep a lower Windows floor than the Qt6 build: Windows 7 SP1 against the Qt6 zip's Windows 10 1703. The floors were audited once, per package, and the audit is recorded in `doc/design/WINDOWS-COMPAT-PLAN.md` |
| `make package-win-sdl` / `package-win32-sdl` | SDL-only Windows zips — repo-internal validation legs, not published |
| `make package-flatpak` | Flatpak bundle, in `build/flatpak-release/` |
| `make package-macos` | macOS `.dmg` — Darwin only; prints a SKIP and exits cleanly elsewhere |
| `make package-web` | the browser build as `jnext-<ver>-web.zip` in `build/wasm-core/`: the `.js` and `.wasm`, the example pages, the licence texts. Needs the Emscripten SDK (built by `make wasm-web`) |
| `make package-test` | builds every package except macOS, Flatpak and the browser zip and **asserts its contents** (a missing tool fails) |
| `make package-contract-test` | the packaging-script contract suites only — hermetic, about 4 s, no toolchain; it also runs `make web-package-test` |
| `make web-package-test` | the contract suite of `packaging/web/package-web.sh` (WEBPKG-01 to WEBPKG-36), on fake trees, hermetic |

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
  translation unit with no profile at all is only a warning, and the library
  members `jnext` never links are exempted from it via ld64's link map.

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

## The minimal NextZXOS image

The browser build does not fetch the 1 GB distribution SD image. It fetches a
small boot card, a 40 MiB FAT32 volume with 512-byte clusters that holds only
what NextZXOS needs to reach its menu and to warm-start a program. The card is
made from the distribution image by `tools/make-minimal-sdcard-image.sh`:

```bash
tools/make-minimal-sdcard-image.sh ~/.jnext/sdcard/cspect-next-1gb.img ~/tmp/nextzxos-minimal.img
```

The boot set (`TBBLUE.FW`, `MACHINES`, `NEXTZXOS`, `SYS`, `LICENSE.md`,
`README.md`) is the `BOOT_SET` list at the top of the script, copied with the
source's own spelling of each name. jnext's own two patches, the `config.ini`
and the doubled `zx81.rom`, are applied by `tools/fix-sdcard-image.sh`, which
the script calls unchanged. The script refuses to overwrite its destination,
reads the source image only (it places the card with a hard link, so the
destination filesystem must support them: not FAT or exFAT), and refuses a card with too few clusters for
FatFs to take it as FAT32 (more than 65525).

The output is reproducible: a fixed volume serial, `SOURCE_DATE_EPOCH` and
`TZ=UTC` pinned inside the script, and a DOS-to-DOS copy, so the directory
order comes from the source card and not from the builder's filesystem. The
hash is the same only for the same mtools version, because `mformat` writes its
version into the boot sector. The script needs mtools, `sfdisk` (util-linux)
and coreutils; it needs no root and no loop mount.

The output is copyrighted NextZXOS content. It is never committed, attached to
an issue or a package, or uploaded; it is hosted only where the owner decides.

Two tests cover it: `make minimal-image-test` (16 rows, part of `make
unit-test`, run against the cached distribution image) and the
`minimal-sdcard-image-func` regression row, which boots the card to the
NextZXOS menu and warm-starts a NEX, both compared with the full card's
references.

## The browser package

`make package-web` zips the output of `make wasm-web` as
`build/wasm-core/jnext-<ver>-web.zip`, through `packaging/web/package-web.sh`. The
zip has one top directory, `jnext-<ver>-web/`, and 21 files:

- `jnext-<ver>.js` and `jnext-<ver>.wasm`: two files, not one, because a page
  author hosts them and GitHub serves a release asset as `octet-stream`, so the
  asset is the zip; the build does not use `SINGLE_FILE`;
- the three example pages `plain.html`, `itch-host.html` and `itch-game.html`,
  which load nothing but `jnext-<ver>.js`;
- `README.txt` (from `packaging/web/README.txt.in`, with the link to the source
  of exactly that version), `LICENSE` and `CREDITS.md`;
- `licenses/`: the licence texts of the libraries linked into `jnext_web`
  (Emscripten, musl, libc++, libc++abi, libunwind, compiler-rt, SDL3, zlib, libpng
  from the Emscripten SDK; spdlog, nlohmann-json, zot, FatFs from `third_party`).
  The script's header records the link set and when it was measured; a library
  linked later needs a line in the `manifest` function, and no test can see that.

Every name comes from `version.yaml` (through CMake for the build output, through
the script for the rest), and the script takes exact names, never a glob, because
a build directory keeps the previous versions' `jnext-<old>.js`. It refuses a
non-Release tree, a missing or ambiguous licence source, a malformed
`version.yaml` and a missing date source.

The zip is reproducible: the entry time is `SOURCE_DATE_EPOCH`, else the commit
time of `HEAD`, never the current time; the script forces `TZ=UTC` (Info-ZIP
stores local DOS time), file mode 0644, names sorted under `LC_ALL=C`, and
`zip -X -D` (no uid/gid, no directory entries). The same sources give the same
bytes whatever the caller's time zone, umask, locale or file times.

**No NextZXOS image is ever in the package.** It is copyrighted; the page fetches
it from the host the page author names. After writing the zip the script reads it
back with CPython's `zipfile` (`--verify`, which also runs on its own) and deletes
it if anything is wrong: the entry names must equal the list exactly, a name
ending in `.img` or `.mmc` is refused at any depth, no entry may be a symlink,
every entry must equal its source byte for byte, and each file has a size cap read
from the zip's central directory (the `.wasm` at most 16 MiB, against the 40 MiB
minimal card), so an image embedded raw into the module or renamed into the package
is refused. An image deliberately compressed and embedded into the module is out of
scope, and so is a zip whose central directory lies.

It is built on every push: the `browser` leg of the `test` job runs `make
wasm-browser-test`, which has `package-web` as a prerequisite and ends with the row
WEBPKG-BR-01, which serves only the extracted zip and boots `plain.html` from it
(see [5.4](04-continuous-integration.md)); and `release.yml` has a `web` job that
runs the same target. It is not a `package-test` row: that would need a second
Emscripten SDK restore and a second wasm build in the `package` job, for what the
browser leg and `web-package-test` already prove (the Flatpak's SDK is the same
case). `make web-package-test` runs in `make unit-test` through
`package-contract-test`.
