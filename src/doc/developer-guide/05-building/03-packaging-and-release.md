# 5.3 Packaging and release

`doc/RELEASE-PROTOCOL.md` is the authority for everything on this page. What
follows describes the shape of the system so that the protocol makes sense when
you read it; it is not a replacement for it.

## The package targets

| Target | Produces |
|---|---|
| `make package-src` | source tarball plus `jnext-<ver>-src.zip`, with submodule content vendored |
| `make package-rpm` | `.rpm` via CPack, in `build/rpm-release/`, named with the host's dist tag (`jnext-<v>-1.fc45.x86_64.rpm`) |
| `make package-deb` | `.deb` via CPack, in `build/deb-release/` |
| `make package-win` | Windows x64 Qt6 `.zip` — MinGW cross-build, Qt6/SDL3 DLLs and the `qwindows` plugin bundled |
| `make package-win-qt5` / `package-win32-qt5` | the legacy Qt5 Windows zips, 64- and 32-bit, which keep a lower Windows floor than the Qt6 build: Windows 7 SP1 against the Qt6 zip's Windows 10 1703. The floors were audited once, per package, and the audit is recorded in `doc/design/WINDOWS-COMPAT-PLAN.md` |
| `make package-win-sdl` / `package-win32-sdl` | SDL-only Windows zips — repo-internal validation legs, not published |
| `make package-flatpak` | Flatpak bundle, in `build/flatpak-release/` |
| `make package-macos` | macOS `.dmg` — Darwin only; prints a SKIP and exits cleanly elsewhere. `MACOS_DEPLOYMENT_TARGET=15.0` pins the minimum macOS and gates the `.dmg` on it; `MACPORTS_PREFIX=/opt/local` takes the dependencies from MacPorts (the Intel build) instead of Homebrew |
| `make package-web` | the browser build as `jnext-<ver>-web.zip` in `build/wasm-core/`: the `.js` and `.wasm`, the example pages, the licence texts. Needs the Emscripten SDK (built by `make wasm-web`) |
| `make package-test` | builds every package except macOS, Flatpak and the browser zip and **asserts its contents** (a missing tool fails) |
| `make package-contract-test` | the packaging-script contract suites only — hermetic, about 4 s, no toolchain; it also runs `make web-package-test` |
| `make pages-site` | the Pages demo site in `build/pages` from the web zip, guarded: no image, program or archive ([5.6](06-the-browser-build.md)) |
| `make pages-site-test` | the contract suite of `pages-site.sh` and the Pages workflows (PSITE-01 to PSITE-07), hermetic; part of `package-contract-test` |
| `make web-package-test` | the contract suite of `packaging/web/package-web.sh` (WEBPKG-01 to WEBPKG-50), on fake trees, hermetic |

The rpm, deb, Flatpak and macOS packaging builds (including the rpm spec and the
Debian `rules`) configure with `-DJNEXT_WERROR=OFF`; the Windows zips keep the
default, because they are built with the same MinGW toolchains the gate uses.
A warning from a distribution's own compiler therefore prints but never fails
a release; see
[5.2 Build configurations](02-build-configurations.md#compiler-warnings).

The source tarball is deliberately not a `git archive`. That would produce an
*empty* `third_party/spdlog` directory, which fails CMake configure the moment
anyone tries to build from it; `package-src` instead runs
`packaging/make-dist-tarball.sh`, which vendors the submodule content properly.

## Testing the packages

`make regression` tests `build/gui-release`, a PGO build made like the Linux
packages (same source, flags class and PGO method) but a different file: each
package builds its own tree, trains its own profile, and the debs are compiled
by Ubuntu's gcc against Ubuntu's Qt, which no Fedora host can execute. So the
packages' own binaries are tested where they are built, by
`make regression-package PACKAGE=<p>` (`test/regression-package.sh`; it first deletes the package's earlier-version artifacts), which
`release.yml` runs after each package step and before the upload:

| `PACKAGE` | artifact | how the binary under test is obtained |
|-----------|----------|---------------------------------------|
| `rpm`, `deb` | the `.rpm` / `.deb` | extracted (`rpm2cpio`, `dpkg-deb -x`) into `build/regression-package/<p>/`; `regression.sh` runs with `JNEXT=<dir>/usr/bin/jnext` |
| `win`, `win-qt5`, `win32-qt5` | the three published zips | unzipped; only Qt's offscreen platform plugin (the difference between two `bundle-dlls.sh` stagings, with and without it, which must be exactly that one file) is added to that scratch copy, never over a zip member and never a file the zip lacks; `regression.sh --platform` runs with the copy as its Qt tree under wine, whose `PATH` carries no MinGW sysroot, so a DLL missing from the zip fails the run |

The rows are every screenshot row of `regression_tests.conf` plus the 17
functional rows listed once in the script (exit paths and CLI, Qt offscreen
start and exit, snapshots, tape, RZX, audio, NextZXOS reset, esxDOS host
files, and the DZRP, GDB and ZRCP servers). A package run is green only when
the harness exits 0 with no FAIL and no SKIP and its total is exactly the
lints, the SD provision, the screenshots and the functional rows (a filtered
run skips the harness's own completeness check, so the script makes it). It is
never stamped and not part of the batch gate. The Flatpak bundle is the one
shipped artifact this does not cover: it keeps its in-build FUSE gate and
permission gate.

The packaging test (`make package-test`, the CI `package` job) pins the
contract: the harness is handed the unpacked package's own binary, a Windows
scratch copy equals the zip plus the plugin, and no shipped zip carries
`platforms/qoffscreen.dll`.

## Profile-guided packages

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
  builds without PGO. `make regression-win` tests this same PGO tree.
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
  under LTO, so its output gets that extra check. flatpak-builder has no retry of
  its own, so `make flatpak-prefetch` (`packaging/flatpak/prefetch-sources.sh`,
  run by the shared workflow before the builder action) first puts that zip in
  flatpak-builder's download cache (`.flatpak-builder/downloads/<sha256>/`),
  checking the manifest's sha256 and retrying a wrong body up to three times, after
  2, 4 and 8 s; if it still fails, flatpak-builder makes its own attempt as before;
- **macOS** uses clang's instrumentation (`-fprofile-instr-generate`, merged
  with `llvm-profdata` from Xcode). There a stale profile is an error but a
  translation unit with no profile at all is only a warning, and the library
  members `jnext` never links are exempted from it via ld64's link map.
  `tools/pgo-build.sh` also runs the FUSE Z80 suite natively against the
  macOS PGO build's CPU core, on both architectures: AppleClang with ThinLTO
  and PGO is a code generator no other suite exercises.

`package-test` matters more than its name suggests. It does not merely build
each package: it then looks inside and checks what is there — that the rpm and
deb carry `bin/jnext`, that the source zip carries the vendored submodule, that
the Windows zip carries its DLLs and its platform plugin, that the source
tarball and zip, the rpm, the deb and the Windows zips carry the 48K ROM symbol
map `48rom.map` (`share/jnext` in the rpm and deb, the zip root in the Windows
zips; the Flatpak build checks its `share/jnext` too, and `verify-macos-dmg`
checks the macOS bundle's `Contents/Resources`), and that `jnext.exe`
is a GUI-subsystem binary with no stray console window attached. Each row
checks the artifact named for the current `version.yaml` version, after
deleting earlier ones: a build directory keeps every previous version's
artifacts, and a row that took the first match once passed on the previous
version's leftover. Nothing
actually invoked it, which is how a permanently-failing packaging row managed to
survive 46 tags unnoticed. It now runs as its own parallel CI job,
and its hermetic half, `package-contract-test`, is a prerequisite of
`make unit-test`.

## Licence texts

Every package carries a `licenses/` folder with the licence of the third-party
code compiled into jnext. The list is one file, `packaging/licenses.list`
(shipped name, source file); a new vendored library under `third_party/` needs
a line there and a stanza in `packaging/debian/copyright`, and the contract row
`licenses` of `make package-contract-test` fails until both exist. CMake
installs the list under `share/doc/jnext/licenses` (rpm, deb, tarball, Flatpak)
and at the root of the macOS image; the Ubuntu deb that links a vendored SDL3
adds its licence (`sdl3.txt`). The Windows zips get the folder from
`packaging/windows/bundle-dlls.sh`, which also copies the licence files of the
Fedora package that owns each bundled DLL into `licenses/bundled/<package>/`
(Fedora's bzip2, win-iconv and zlib ship none: the texts in
`packaging/windows/license-fallbacks/` stand in) and refuses to bundle when a
DLL has no licence at all. On macOS,
`packaging/macos/bundle-licenses.sh` does the same for the Homebrew libraries
macdeployqt copies into the bundle (`licenses/bundled/<formula>/`, from the
formula's keg) and fails the package when a library's formula has no licence. Homebrew copies only
top-level licence files into a keg, so the Qt and glib kegs get texts from
`packaging/macos/license-fallbacks/`. With `MACPORTS_PREFIX` set (the Intel
build) the owner is the MacPorts port that `port provides` names for the
library, the licences are the `LICENSE*`, `COPYING*` and `LICENCE*` files in
`port contents`, and the same two failures apply: a library no port owns
(for instance one that leaked in from another prefix on the build machine) or
a port with no licence and no fallback stops the package, after every such
library has been named, so one run shows the whole list.

### The macOS architectures and the minimum macOS

There are two macOS packages, built natively, because the dependencies are
single-architecture and a universal binary is not an option: Apple Silicon
(`jnext-<v>-macos-arm64.dmg`, Homebrew) and Intel (`jnext-<v>-macos-x64.dmg`,
MacPorts: Homebrew builds no bottles for Intel Macs any more). The names carry
the architecture because CPack's default would be the same for both and the
release assets would collide. `packaging/macos/install-macports.sh` installs a
pinned MacPorts and the ports from binary archives only, so a port whose
archive is not published yet fails in seconds instead of starting a source
build of Qt.

A bundled library built for a newer macOS than the one running makes dyld
refuse to start the app, so the minimum is explicit and checked.
`MACOS_DEPLOYMENT_TARGET` becomes `CMAKE_OSX_DEPLOYMENT_TARGET` in both PGO
trees and `LSMinimumSystemVersion` in the bundle's `Info.plist`, which makes
the Finder say "requires macOS N or later" on an older release. Then
`verify-bundle.sh` reads the minimum OS version of every Mach-O in the mounted
`.dmg` (`LC_BUILD_VERSION` `minos`, or `LC_VERSION_MIN_MACOSX`). It always
prints the highest one and the file that carries it; with the target set, it
fails the package for any file that needs a newer macOS, for a file whose
minimum it cannot read, and unless `Contents/Info.plist` carries
`LSMinimumSystemVersion` equal to the target (so the Finder refuses an older
macOS instead of dyld killing the app).

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

The browser build does not use the 1 GB distribution SD image. It makes a small boot
card, a 40 MiB FAT32 volume with 512-byte clusters that holds only what NextZXOS needs
to reach its menu and to warm-start a program, from the official NextZXOS distribution
zip. NextZXOS is SpecNext's copyrighted software, and only specnext.com hosts it: the
card is made on the machine that needs it (the visitor's browser; the developer's own
machine for tests) and is never published or hosted, by JNEXT or anyone else.

One C++ implementation makes it, `minimal_card::Builder` (`src/core/minimal_card.h`),
used by the page (compiled to WebAssembly, fed the zip as a stream) and by the native
developer tool `make-minimal-card` (`tools/make-minimal-card/`, built in every native
tree, not installed and not packaged). The tool replaced an earlier mtools shell script: there is no
mtools, `sfdisk`, root or hard link in the way any more, and it runs wherever JNEXT builds.

```bash
make-minimal-card [--unpinned] [--expect-zip-sha256 HEX] [--dirs LIST] <INPUT> <card.img>
```

It is built with the rest of the tree, as `build/tools/make-minimal-card/make-minimal-card`
(likewise in the SDL and Windows trees).

`INPUT` is the distribution zip, the `cspect-next-1gb.img` inside it, or `-` for the zip
on standard input. The kind is read from the content (`PK\3\4` is a zip, an MBR
signature at byte 510 is an image); anything else is refused. On success the last
line is `<card.img>: <bytes> bytes, sha256 <hex>` and the exit status is 0; a refusal
exits 1 with the reason on standard error and leaves the destination untouched or
uncreated; a usage error exits 2. An existing destination is never overwritten.

On your own download of the official zip, for local development and tests:

```bash
make-minimal-card ~/Downloads/sn-emulator-26.11a.zip ~/tmp/nextzxos-minimal.img
curl -s <the zip's URL> | make-minimal-card - ~/tmp/nextzxos-minimal.img
```

The second form is the one manual check against the real file, with no zip on disk. Keep
what you make under `~/tmp` and delete it when you are done: it is copyrighted NextZXOS
content, and it is never committed, attached to an issue or a package, uploaded, served to
anyone, or hosted anywhere.

**Pinned by default.** The distribution this release accepts is identified by
`minimal_card::kDistribution`, right after `kDistroUrl`'s constants in `sdcard_provisioner.cpp` (declared in `minimal_card.h`): the file
name, the zip's size and SHA-256, the inflated image's SHA-256 and the SHA-256 of the
card it must produce, plus a recipe number. The zip or image digest is checked before
anything is written, and the card digest after; a wrong one is refused and no usable output
remains. `--unpinned` skips every pinned digest and prints the card's: it exists for the
synthetic fixtures of the tests and is not for real distributions. `--expect-zip-sha256 HEX`
is test tooling: it replaces the pinned zip digest (and drops the size check) with the digest
of a zip made at run time from the image, and the pinned card digest still applies; the
`minimal-sdcard-image-func` regression row uses it.

**More of the distribution (`--dirs`).** `--dirs /GAMES,/APPS` (the widget's own
syntax: comma-separated, each item trimmed, empty items dropped) is `Expect::dirs`, the
list a page gives as `card.dirs` / `data-card-dirs`. Each path is copied with its subtree, and
its ancestors are created with the source's own names, attributes and times (the other
children of an ancestor are not). `minimal_card::canonical_dirs` is the one place that checks a
list and puts it in canonical form (ASCII lower case, no trailing `/`, sorted), and
`web_api.cpp`, the builder, the tool and (through `jnext_card_dirs`) the page's JavaScript all
call it: a malformed entry, `/`, a boot-set name, a duplicate and a path inside another given
one are refused, naming the entry. A path the distribution does not hold is refused at build,
naming every missing one. A card with extra paths has no pinned card digest (no per-list pin
can exist): the tool and `jnext_card_begin` clear `Expect::card_sha256` for a non-empty list, and
the zip and image pins still apply in full. `/GAMES,/APPS` of 26.11a is accepted; adding `/DEMOS`
is refused with its size.

**One pass.** The builder reads the zip once, from byte 0, in the order it is stored: the
image is its first entry, the inflated image is read by offset, and a directory is parsed
when its whole chain has gone by. A distribution whose layout would need a second pass (a
directory's contents stored before the directory) is refused with a message naming the
cluster; it is not worked around. Its memory is bounded: about 4.0 MB of heap for the 26.11a
distribution (3.7 MB of it kept data), with `/GAMES,/APPS` the wasm heap grew by 21 MB (BCARD-41), under 50 MiB before any refusal (the FAT copy
is capped at 16 MiB and the kept data at 32 MiB), and the card itself is a file (MEMFS in the
browser, not the wasm heap). The 32 MiB bound counts what the builder keeps: each file's SIZE
and each directory's clusters, not the source's clusters (the distribution's are 32 KiB, which
would count `/GAMES` and `/APPS` as 35.09 MiB when they are 27.77 MiB of files and
directories; with the boot set the card counts 31.30 MiB of the 32). Past the bound it goes on counting but stops buffering, so the refusal gives the
exact total and the limit, and a truncated image is reported as "at least N bytes".

The boot set (`TBBLUE.FW`, `MACHINES`, `NEXTZXOS`, `SYS`, `DOT`, `LICENSE.md`, `README.md`)
is copied with the source's own spelling and metadata (`DOT` is there because NextZXOS runs its dot commands from it, and its file browser opens text files, NEX programs and most other types through one; a distribution without it is refused, and `/DOT` in `card.dirs` is refused as part of the boot set), and JNEXT's two patches (the
`config.ini` and the doubled `zx81.rom`) are the functions the SD provisioner uses, not
copies. The output is reproducible and the same bytes on every platform: no clock, the source's directory order, spelling, write times and R/H/S/A attributes, volume serial `4A4E5854`, hidden sectors 63.

**A new distribution.** A release that moves to a new distribution changes `kDistroUrl`
and all of `kDistribution` in one commit: the size, the zip digest and the image digest
from your own download, the card digest from `make-minimal-card` on it, and the recipe
number only if the builder changed what it writes. The tests (MINIMG-01 and the
`minimal-sdcard-image-func` regression row) fail on a partial update.

Tests: `minimal_card_test` (`MCARD-01..16` and `18..41`, on a synthetic distribution built in the test; `MCARD-17`, recipe 3 unchanged, is in `fat32_image_test`; every tree, the wasm one included), the
`make minimal-image-test` shell suite (25 rows, `MINIMG-21..26` for `--dirs`, `MINIMG-28..30` for `/dot` and the Pages list: the tool on the cached image and on a synthetic zip, read back with mtools, sfdisk and python's zipfile) and the `minimal-sdcard-image-func` regression row,
which boots the card to the NextZXOS menu and warm-starts a NEX, both compared with the
full card's references, and the `minimal-sdcard-dot-func` row, which runs `.more readme.md` on the card and compares it with the full card (and with the same card without `/dot`, which must differ).

## The browser package

`make package-web` zips the output of `make wasm-web` as
`build/wasm-core/jnext-<ver>-web.zip`, through `packaging/web/package-web.sh`. The
zip has one top directory, `jnext-<ver>-web/`, and 25 files:

- `jnext-<ver>.js` and `jnext-<ver>.wasm`: two files, not one, because a page
  author hosts them and GitHub serves a release asset as `octet-stream`, so the
  asset is the zip; the build does not use `SINGLE_FILE`;
- the four example pages `plain.html`, `itch-host.html`, `itch-game.html` and
  `widget.html`; the first three load nothing but `jnext-<ver>.js`, `widget.html`
  loads `jnext-widget.js`, the ready-made widget (`web/jnext-widget.js.in`, configured
  with the version like the pages, so that it can load `jnext-<ver>.js` from beside itself);
- `jnext-debug-bridge.mjs`, the reference bridge of the page option `debug`
  (`tools/web-debug-bridge/`, a plain node script for a developer's computer; the Pages demo
  site, which is this zip minus the example pages, carries it too);
- `README.txt` (from `packaging/web/README.txt.in`, with the link to the source
  of exactly that version), `LICENSE` and `CREDITS.md`;
- `licenses/`: the licence texts of the libraries linked into `jnext_web`
  (Emscripten, musl, libc++, libc++abi, libunwind, compiler-rt, SDL3, zlib, libpng
  from the Emscripten SDK; spdlog, nlohmann-json, zot, FatFs, crtemu from `third_party`).
  The script's header records the link set and when it was measured; a library
  linked later needs a line in the `manifest` function, and no test can see that.

Every name comes from `version.yaml` (through CMake for the build output, through
the script for the rest), and the script takes exact names, never a glob, because
a build directory keeps the previous versions' `jnext-<old>.js`. It refuses a
non-Release tree, a missing or ambiguous licence source, a malformed
`version.yaml` and a missing date source.

The packaging is deterministic: the entry time is `SOURCE_DATE_EPOCH`, else the commit
time of `HEAD`, never the current time; the script forces `TZ=UTC` (Info-ZIP
stores local DOS time), file mode 0644, names sorted under `LC_ALL=C`, and
`zip -X -D` (no uid/gid, no directory entries). The same build output gives the same
zip bytes whatever the caller's time zone, umask, locale or file times. The
compiled `.wasm` can differ between build machines, and so can the zip;
identical WebAssembly across machines is not a goal.

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
WEBPKG-BR-01, which serves only the extracted zip and boots `plain.html` from it (WEBPKG-BR-02 to 05 check the viewport of the four example pages, 06 and 07 that plain.html and itch-host.html fit their player in a phone-wide page)
(see [5.4](04-continuous-integration.md)); and `release.yml` has a `web` job that
runs the same target. It is not a `package-test` row: that would need a second
Emscripten SDK restore and a second wasm build in the `package` job, for what the
browser leg and `web-package-test` already prove (the Flatpak's SDK is the same
case). `make web-package-test` runs in `make unit-test` through
`package-contract-test`.
