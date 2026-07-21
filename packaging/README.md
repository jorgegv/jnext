# Packaging jnext (Task 67)

Maintainer guide for producing distributable jnext packages. All packaging
inputs live under this directory; user-facing docs stay in the top-level
`README.md`/`USAGE.md`, which this file does not duplicate.

## App ID

The desktop file, AppStream metainfo and icon use the reverse-DNS id
**`io.github.zxjogv.jnext`** (the git author on this repo is `ZXjogv`). If
the project ever settles on a different GitHub org/user for releases,
rename the three `packaging/assets/io.github.zxjogv.jnext.*` files and the
Flatpak manifest's `app-id`/filename to match — nothing else depends on the
exact string.

## What CMake already does

`CMakeLists.txt` gained `install()` rules (Task 67) that are always present
(they don't gate any build option): the `jnext` binary, the `.desktop`
launcher, the AppStream metainfo, a scalable SVG + 512×512 PNG icon under
`hicolor`, `LICENSE`/`README.md`/`ChangeLog`/`USAGE.md` under `share/doc/jnext`,
and the man page at `share/man/man1/jnext.1` (pre-generated and committed, so
installing needs no doc toolchain — the formats compress it themselves, hence
the `jnext.1*` glob in the RPM `%files`). `include(CPack)`
at the end of the same file wires up TGZ/DEB/RPM package generation from any
configured build directory — no separate CPack config file.

The `packaging/assets/` icons are all derived from one artwork (source bundle
`jnext-icons.zip`, kept in `packaging/assets/` for reference):
`io.github.zxjogv.jnext.{svg,png}` (Linux
freedesktop), `jnext.ico` (embedded into `jnext.exe` on Windows via `jnext.rc`
+ windres), and `jnext.icns` (macOS `.app`/`.dmg`, via `CPACK_BUNDLE_ICON` —
unverified on the Linux dev host).

```sh
cmake -B build -DENABLE_QT_UI=ON -DENABLE_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cd build && cpack -G TGZ        # or: cpack -G "TGZ;DEB;RPM"
```

Verified on this dev host (Fedora 44): **TGZ, RPM and DEB all produced a
package** (`jnext-<ver>-Linux.{tar.gz,rpm,deb}`). The DEB generator prints
"CPACK_DEBIAN_PACKAGE_DEPENDS not set, the package will have no
dependencies" — a known CPack-on-non-Debian-host limitation
(`dpkg-shlibdeps` integration is finicky outside a real Debian/Ubuntu
build); the `packaging/debian/` source package below computes dependencies
correctly via `${shlibs:Depends}` and is the one to actually ship.

`cmake --install` respects `DESTDIR`/`CMAKE_INSTALL_PREFIX` normally:

```sh
DESTDIR=/tmp/stage cmake --install build
```

## Building each package

The root `Makefile` wraps every packaging path in a `make package-*` target
(each configures its own git-ignored build dir, so they don't disturb
`build/`):

| Target                 | Produces                          | Tooling needed                                  | Verified on the Linux dev host? |
|------------------------|-----------------------------------|-------------------------------------------------|---------------------------------|
| `make package-src`     | source tarball (`build/dist/v<ver>.tar.gz`) — vendors submodule content | git                     | Yes                             |
| `make package-rpm`     | `.rpm` (via CPack, in `build/package-rpm/`), named `jnext-<ver>-<rel>.<arch>.rpm` | `cpack` + `rpmbuild` | Yes                     |
| `make package-deb`     | `.deb` (via CPack, in `build/package-deb/`), named `jnext_<ver>_<arch>.deb` | `cpack` + `dpkg`     | Yes (deps weak off-Debian, see above) |
| `make gui-release-win` | Windows `jnext.exe` + its runtime DLLs bundled beside it in `build/gui-release-win/` (runnable in place) | Fedora MinGW cross toolchain (see below)        | **Yes** (with the MinGW packages installed) |
| `make package-win`     | Windows `.zip` (`jnext-<ver>-windows-x64.zip` in `build/gui-release-win/`) — exe + bundled Qt6/SDL2/SDL3 DLLs + Qt plugins | Fedora MinGW cross toolchain (see below)        | **Yes** (with the MinGW packages installed) |
| `make package-flatpak` | Flatpak bundle (`build/flatpak/`) | `flatpak-builder` + `org.kde.Sdk//6.10`          | Manifest validates; **full build needs `org.kde.Sdk` installed** (a large runtime) — not present here |
| `make package-macos`   | macOS `.dmg` — a self-contained `jnext.app`, verified with `otool` | a Mac / the GitHub Actions macos runner         | **No** — the target prints a SKIP and exits cleanly on non-Darwin |

`make package-test` (`test/packaging/packaging-test.sh`) runs every package
target above except macOS and asserts each produces a correctly-named artifact
containing the `jnext` binary. It is **tooling-guarded**: a package whose build
tool is absent SKIPs rather than FAILs, so the same test runs meaningfully on
any dev box. On this host src/rpm/deb/win PASS and flatpak SKIPs (manifest
validated; the full `flatpak-builder` run needs `org.kde.Sdk`). Every target
that cannot run detects the missing tooling/platform and exits with a clear
message (what to install, or that a Mac/CI runner is required) instead of a
cryptic mid-build failure.

### Windows cross-build (Fedora MinGW)

`make package-win` cross-compiles with Fedora's MinGW wrapper `mingw64-cmake`.
(The embedded `nextboot.rom` is generated as a portable C array by
`src/core/embed_rom.cmake`, so there is no objcopy/object-format dependency on
any target.) Install the full toolchain + libraries first:

```sh
sudo dnf install mingw64-gcc mingw64-gcc-c++ mingw64-qt6-qtbase \
  mingw64-sdl2-compat mingw64-curl mingw64-openssl mingw64-zlib \
  mingw64-libpng mingw64-winpthreads
```

`mingw64-filesystem` supplies `mingw64-cmake`; the native `qt6-qtbase-devel`
supplies the host `moc`/`rcc`/`uic` used during the cross-build;
`mingw64-qt6-qttools` is optional.

The cross-build produces a **console-subsystem** `jnext.exe` (so `--headless`/
`--version` stdout works). Making it link required a few portability fixes:
`src/core/anon_mem.h` (VirtualAlloc vs `mmap` for the rewind/profiler buffers),
gating Linux-only `sched_getcpu`, `std::filesystem` instead of POSIX `mkdir`,
and linking `Qt6::EntryPointPrivate` on Windows to supply the `WinMain`→`main`
bridge.

**DLL bundling** is handled by `packaging/windows/bundle-dlls.sh`. `jnext.exe`
cannot start on a stock Windows box on its own — it needs the Qt6/SDL2 runtime
DLLs and, critically, the `platforms/qwindows.dll` Qt platform plugin (without
it Qt aborts with "no Qt platform plugin could be initialized"). The script
resolves the transitive DLL closure of the exe and the shipped Qt plugins from
`/usr/x86_64-w64-mingw32/sys-root/mingw/bin` (skipping the DLLs Windows itself
provides), adds the jnext-built `libspdlog.dll`, copies the Qt plugins into
`platforms/`/`styles/`/`imageformats/` with a `qt.conf`, and **fails the build**
if any required non-system DLL is missing. It also handles one runtime-loaded
dependency objdump cannot see: Fedora's `mingw64-sdl2-compat` `SDL2.dll` is a
shim that `LoadLibrary`s `SDL3.dll` at runtime, so `SDL3.dll` is not in any
import table — the script detects the shim and bundles `SDL3.dll` too (without
it the exe dies with "Failed loading SDL3 library"). `gui-release-win` runs it
against `build/gui-release-win/` (exe runnable in place); `package-win` runs it
into a clean, correctly-named staging dir and zips that (not CPack `-G ZIP`,
whose `/usr` install prefix would give a broken `usr/bin/jnext.exe` layout on
Windows).

## Runtime dependencies — how they were derived

Not guessed: `ldd build/gui-release/jnext` was run on this host and the
**direct** (non-transitive) linked libraries were cross-referenced against
the `find_package()` calls in `CMakeLists.txt` / `src/*/CMakeLists.txt`:

| Library     | Where it's required                                             |
|-------------|-------------------------------------------------------------------|
| SDL2        | root `CMakeLists.txt`, `src/platform`, `src/gui`, `src/input`     |
| Qt6 Widgets (pulls Gui, Core) | `src/gui`, `src/debugger` (`ENABLE_QT_UI`/`ENABLE_DEBUGGER`) |
| libcurl     | root `CMakeLists.txt` — SD-card image download                   |
| OpenSSL (libcrypto) | root `CMakeLists.txt` — SHA-256 integrity check of the SD image |
| zlib        | root `CMakeLists.txt` — SZX/RZX compression                      |
| libpng      | `src/platform` — PNG screenshots                                 |

`ffmpeg` is **not** linked — `src/core/video_recorder.cpp` shells out to it
via `system("ffmpeg -version …")` and a piped `ffmpeg -y …` command for the
`--record` feature. It is therefore a `Recommends`/soft dependency in both
the RPM spec and the Debian control file, never a hard `Requires`.

The transitive closure (Qt platform plugins, fontconfig, X11, ICU, etc.) is
left to each packaging format's native dependency resolver — `rpm`'s
auto-`Requires` generator (confirmed via `rpm -qp --requires` on the built
RPM) and Debian's `dpkg-shlibdeps` (`${shlibs:Depends}` in `debian/control`)
— rather than hand-listed, since that closure is Qt-version- and
distro-specific and the tooling gets it right automatically.

## RPM (Fedora)

Support policy (per project CLAUDE.md): Fedora current + previous stable
(44 and 43 at the time of writing).

`packaging/rpm/jnext.spec` builds from a source tarball via `cmake`. It is a
real, from-source spec (not merely a CPack wrapper), with explicit
`BuildRequires` matching the table above plus `cmake`/`gcc-c++`/`git`, and
validates the desktop file + metainfo at `%install` time.

**Submodule gotcha (found while verifying this spec):** `third_party/spdlog`
is a git submodule; a plain `git archive` tarball of the source tree
contains an *empty* `third_party/spdlog/` directory (a gitlink, no content),
which fails CMake configure with "does not contain a CMakeLists.txt file".
Use `packaging/make-dist-tarball.sh` to produce a correctly-vendored source
tarball (equivalent to `git clone --recursive`) before building the RPM:

```sh
packaging/make-dist-tarball.sh ~/rpmbuild/SOURCES   # writes v<version>.tar.gz
rpmbuild -bb packaging/rpm/jnext.spec
```

This was verified end-to-end on this host: `rpmbuild -bb` compiled the full
Qt6 GUI + debugger and produced an installable `jnext` RPM whose
auto-generated `Requires:` matched the table above (Qt6Core/Gui/Widgets,
SDL2, libcurl, libcrypto, libpng, zlib, GL libs, …).

## Debian / Ubuntu

Support policy: Debian Stable + OldStable, Ubuntu current + LTS.

`packaging/debian/` (`control`, `rules`, `changelog`, `copyright`, and
`source/format`) is a standard `debhelper-compat (= 13)`, `dh --buildsystem
cmake` source package. Debian tooling expects `debian/` at the repository
root, so symlink (or copy) it there before building:

```sh
ln -s packaging/debian debian
dpkg-buildpackage -us -uc -b
rm debian   # remove the symlink afterwards; keep the tree clean
```

`debian/rules` passes `-DENABLE_QT_UI=ON -DENABLE_TESTS=OFF
-DCMAKE_BUILD_TYPE=Release` to the CMake configure step, matching how the
project actually builds its release binary (`make gui-release`).
`${shlibs:Depends}` in `debian/control` lets `dpkg-shlibdeps` compute the
real runtime dependency list from the built binary at package time.

## Flatpak

`packaging/flatpak/io.github.zxjogv.jnext.yml` targets the **KDE runtime**
(`org.kde.Platform`/`org.kde.Sdk`) rather than `org.freedesktop.Platform`,
because jnext's Qt6 GUI needs `Qt6::Widgets` and the KDE runtime ships a
full Qt6 stack — `org.freedesktop.Platform` would need Qt6 built as an
extra module, which the KDE runtime avoids. SDL2 is not part of that
runtime and is built from source as a `cmake-ninja` module; jnext itself is
built the same way from a `git` source (so its own submodules are pulled by
CMakeLists.txt's existing `git submodule update --init --recursive` logic,
same submodule gotcha as the RPM case above — a `git` flatpak source has a
real `.git`, an `archive`/tarball source would not).

The manifest carries the **real** SDL2 tarball `sha256` and pins
`runtime-version: '6.10'` (the KDE runtime branch installed here). It validates
with `flatpak-builder --show-manifest`. A full `flatpak-builder` run additionally
needs `org.kde.Sdk//6.10` installed (a large runtime) and the `flathub` remote
enabled — neither is set up on this dev host, so `make package-flatpak` /
`make package-test` validate the manifest and SKIP the actual build here. To
build it for real:

```sh
flatpak install flathub org.kde.Sdk//6.10 org.kde.Platform//6.10
flatpak-builder --user --install build-dir packaging/flatpak/io.github.zxjogv.jnext.yml
```

Bump both the SDL2 pin (URL + `sha256`) and the `runtime-version` when a newer
SDL2 release or KDE runtime branch is targeted.

## Windows / macOS

**Windows** is cross-built through the project's own `make package-win` target
— a MinGW cross-build run in a Fedora container — so CI produces byte-for-byte
the same artifact a developer builds locally, with the Qt6/SDL2/SDL3 runtime
DLLs + the `qwindows` plugin bundled by `packaging/windows/bundle-dlls.sh`. The
exact recipe (package list + `make package-win`) was proven in a `fedora:44`
container and the produced `jnext.exe` runs under wine; only a real GitHub
Actions run remains unexercised.

**macOS** has no build host in this environment — the `macos` job builds it
natively on `macos-latest` through the project's own `make package-macos`
target, so CI runs the same recipe a Mac developer would.

The package is a **relocatable `jnext.app`** at the root of the `.dmg`. It has
to be: until v0.98.72 the `.dmg` held a bare executable under `usr/bin`, still
carrying the absolute `/opt/homebrew/...` install names it linked against, and
it aborted in dyld on the first Mac that was not the build machine
([GH #46](https://github.com/jorgegv/jnext/issues/46)). `macdeployqt` now copies
the Qt frameworks, the Cocoa platform plugin and the non-Qt Homebrew dylibs into
`Contents/Frameworks` and rewrites the install names to `@executable_path`.

**That deployment is not trusted — it is checked.**
`packaging/macos/verify-bundle.sh` walks every Mach-O in the bundle with
`otool -L` and fails if any dependency points outside it (`/opt/homebrew`,
`/usr/local`, `/opt/local`, any other absolute path that is not `/usr/lib` or
`/System`). It runs twice: at install/staging time from `CMakeLists.txt`, and
again by `make verify-macos-dmg` against the `.dmg` **as mounted**, which also
launches the bundled binary. A failure fails the job, so no `.dmg` is uploaded
and the release simply carries no macOS package — the right outcome when the
alternative is one that aborts at launch.

The gate's own decision logic is tested on Linux against stubbed `otool`/`file`
(`test/packaging/verify-bundle-test.sh`, run by `make package-test`), because
the thing it guards cannot be built here.

## CI: `.github/workflows/release.yml`

One tag-triggered workflow — a `gate` job reads `releases.yaml` (from the tag's
own commit) and only lets the build + publish run for **listed** tags. See
[doc/RELEASE-PROTOCOL.md](../doc/RELEASE-PROTOCOL.md) for the full gated-release
policy. The per-OS build jobs, when they run:

| Job       | Runner                        | Build                                                 | Package(s)                     | Verified locally? |
|-----------|-------------------------------|-------------------------------------------------------|--------------------------------|--------------------|
| `deb`     | `ubuntu-latest` + `ubuntu:24.04`/`ubuntu:26.04` (matrix) | apt deps + CPack (in each pinned container) | 2× DEB (CPack), one per LTS, named `jnext_<ver>_ubuntu<rel>_amd64.deb` | Yes — both built + `apt install`ed in their own containers; the 24.04 deps carry the t64 names, 26.04's differ |
| `rpm`     | `ubuntu-latest` + `fedora:44` | dnf deps + CPack (in a Fedora container)             | RPM (CPack)                    | Yes — built in a `fedora:44` container; deps are Fedora-native (`libcurl.so.4()(64bit)`, not the Ubuntu `CURL_OPENSSL_4` node) |
| `src`     | `ubuntu-latest`               | `make package-src` (submodule-aware)                 | `jnext-<ver>-src.zip`          | Yes |
| `windows` | `ubuntu-latest` + `fedora:44` | `make package-win` (MinGW cross-build + DLL bundling) | ZIP (`build/gui-release-win/`) | Yes — same recipe proven in a `fedora:44` container; exe runs under wine |
| `flatpak` | `ubuntu-latest` + KDE 6.10     | `flatpak-builder` (org.kde.Sdk//6.10)                 | `.flatpak` bundle              | No — `continue-on-error`; not yet run on a GitHub runner |
| `macos`   | `macos-latest`                | `make package-macos` (Homebrew + CPack + `macdeployqt`) | DragNDrop `.dmg` (`build/package-macos/`) | No — no macOS runner locally (`continue-on-error`); the bundle's self-containment is asserted in-job by `verify-bundle.sh` |

This workflow is separate from `ci.yml` (which runs the test suite on push/PR).
`release.yml` does not run tests — it builds packages and, for tags listed in
`releases.yaml`, publishes them as a GitHub Release.
