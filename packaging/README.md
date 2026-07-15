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
launcher, the AppStream metainfo, a scalable SVG + 256×256 PNG icon under
`hicolor`, and `LICENSE`/`USAGE.md` under `share/doc/jnext`. `include(CPack)`
at the end of the same file wires up TGZ/DEB/RPM package generation from any
configured build directory — no separate CPack config file.

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

`flatpak-builder` is **not installed on this dev host** — this manifest was
authored and YAML-validated (`python3 -c "import yaml; yaml.safe_load(...)"`)
but not build-tested. Before using it for a real release: fill in the
placeholder `sha256` for the pinned SDL2 tarball, and confirm the
`runtime-version` against whatever KDE runtime branch is current on
Flathub.

```sh
flatpak-builder --user --install build-dir packaging/flatpak/io.github.zxjogv.jnext.yml
```

## Windows / macOS

No Windows or macOS build host is available in this environment, so neither
was built or run locally. `.github/workflows/packaging.yml` (see below) has
a best-effort CI job for each, using the real dependency set (Qt6 + SDL2 +
curl + OpenSSL + zlib + libpng) via `aqt`/vcpkg (Windows) and Homebrew
(macOS), packaged via CPack's `ZIP` and `DragNDrop` generators respectively.
Treat those two jobs as **unverified** until they've actually run on GitHub
Actions.

## CI: `.github/workflows/packaging.yml`

Three jobs, one per OS, all producing packages as workflow artifacts on
`workflow_dispatch` or a `v*` tag push:

| Job       | Runner           | Deps                                   | Package(s)             | Verified locally? |
|-----------|------------------|-----------------------------------------|-------------------------|--------------------|
| `linux`   | `ubuntu-latest`  | apt: SDL2/Qt6/curl/OpenSSL/zlib/libpng  | TGZ + DEB + RPM (CPack) | Yes, same commands proven on Fedora 44 here |
| `windows` | `windows-latest` | aqt (Qt6) + vcpkg (SDL2 + friends)      | ZIP (CPack)             | No — no Windows runner locally |
| `macos`   | `macos-latest`   | Homebrew (Qt6 + SDL2 + friends)         | DragNDrop `.dmg` (CPack)| No — no macOS runner locally |

This workflow is separate from `ci.yml`/`release.yml` (owned elsewhere) —
it only builds and packages, it does not run the test suite.
