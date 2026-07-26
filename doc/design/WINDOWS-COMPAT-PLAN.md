# Windows 7/8 Compatibility Audit & Plan (GH #108)

Audit date 2026-07-25, v0.99.27, fedora 44 MinGW cross toolchain
(mingw64-gcc 16.1.1, mingw64-qt6-qtbase 6.11.1, mingw64-sdl2-compat 2.32.70,
mingw64-SDL3 3.4.12). All floor claims below are **static-import evidence**
read from the exact files `make package-win` ships, via
`packaging/windows/pe-floor-audit.sh` (objdump on the PE import tables) — not
vendor support statements. Re-run any time with:

```sh
bash packaging/windows/pe-floor-audit.sh \
    build/win-release/dist/jnext-*/jnext.exe build/win-release/dist/jnext-*/*.dll \
    build/win-release/dist/jnext-*/platforms/*.dll
```

A regular (non-delay) PE import of a DLL or symbol absent from an OS makes the
importing DLL **fail to load there** — no code path can dodge it. That is what
"hard floor" means below.

## 1. Audited floor of the shipped win64 (Qt) package: **Windows 10 (1703)**

PE headers are not the limiter (all binaries: MajorOSVersion 4.0,
MajorSubsystemVersion 5.02 — XP-x64-era, GNU ld defaults). The CRT is
**msvcrt.dll**, not UCRT — no UCRT-redist prerequisite anywhere. The floor
comes entirely from fedora's Qt 6.11 DLLs:

| File | Blocking static imports | First OS with them |
|------|------------------------|--------------------|
| `Qt6Gui.dll` | `d3d12.dll` (regular import, delay-import dir is empty) | Windows 10 |
| `Qt6Gui.dll` | `SystemParametersInfoForDpi` ← user32 | Win10 1607 |
| `Qt6Core.dll` | `SetThreadDescription` ← kernel32 | Win10 1607 |
| `Qt6Core.dll` | `WaitOnAddress`/`WakeByAddress*` ← `api-ms-win-core-synch-l1-2-0.dll` | Windows 8 |
| `Qt6Widgets.dll` | `GetSystemMetricsForDpi`, `SystemParametersInfoForDpi` ← user32 | Win10 1607 |
| `platforms/qwindows.dll` | 9 user32 DPI-context APIs incl. `SetProcessDpiAwarenessContext` | Win10 1703 |
| `platforms/qwindows.dll` | `GetDpiForMonitor` ← shcore.dll | Windows 8.1 |
| ~~`libcrypto-3-x64.dll`~~ | ~~`PathCchRemoveFileSpec` ← `api-ms-win-core-path-l1-1-0.dll`~~ (gone — §7 Phase B removed curl/OpenSSL from all Windows bundles) | ~~Windows 8~~ |

Verdict: the Qt package **cannot run below Windows 10 1703**, and every
blocker is inside fedora-built Qt6 DLLs we do not compile. Nothing jnext-side
can lower this; the Qt leg stays as-is (its D3D imports are load-time only —
jnext renders via QImage software pre-scaling and never initializes a Qt RHI,
but the import alone blocks loading on older OSes).

`jnext.exe`'s own import set is clean: kernel32/msvcrt/shell32 plus the
bundled DLLs — no post-Win7 API. The mingw runtime (libstdc++/libgcc/
libwinpthread), SDL2/SDL3, zlib/libpng/spdlog and the whole curl chain except
libcrypto also audit clean to Win7 level (full 490-symbol system-import set
reviewed, not just the curated list).

## 2. SDL-only variant `make package-win-sdl`: floor **Windows 7** (import-audit evidence; §7 Phase B)

`ENABLE_QT_UI=OFF -DENABLE_DEBUGGER=OFF` + MinGW cross-build (the debugger
option defaults ON and its `src/debugger` does `find_package(Qt6 REQUIRED)` at
configure time — OFF keeps the build genuinely Qt-free; the exe is identical
either way) → `jnext-<ver>-windows-x64-sdl.zip`
(8 DLLs, no Qt, no curl/OpenSSL). Audit result (2026-07-26, post-Phase B):
`pe-floor-audit.sh` over the full bundle reports **ZERO post-Win7 imports**
(exit 0) — jnext.exe + SDL2/SDL3 + libgcc/libstdc++/libwinpthread +
libpng16/zlib1/libspdlog all clean.

- **Windows 7 SP1 and later: expected to work by import evidence** (real-Win7
  hardware verification still pending — see §5's wine caveat).
- History: before Phase B the floor was Windows 8.0 — the one blocker was
  fedora libcrypto-3's `PathCchRemoveFileSpec`
  (`api-ms-win-core-path-l1-1-0.dll`, Win8+), a startup dependency of
  `libcurl-4.dll`, linked for SD-image self-provisioning. Phase B (2026-07-26)
  deleted the whole chain: on Windows the provisioner now uses OS-native
  WinHTTP (download) + BCrypt/CNG (SHA-256) in
  `src/core/sdcard_provisioner_net_win.cpp`, and the Windows build links
  neither curl nor OpenSSL. Eight DLLs left every Windows bundle (libcurl,
  libcrypto, libssl, libssh2, libidn2, libpsl, libunistring, iconv);
  `test/packaging/packaging-test.sh` asserts they never come back. Every
  WinHTTP/BCrypt symbol imported (11 + 7, verified against the built exe's
  import table) is Win7 SP1-available; TLS 1.2 is opted in explicitly via
  `WINHTTP_OPTION_SECURE_PROTOCOLS` (on an unpatched Win7 without KB3140245
  the option call fails and the OS default applies).

Runtime graphics/audio of the SDL leg degrade gracefully by design (verified
against SDL3.dll: d3d9/d3d11/d3d12/vulkan/opengl renderers and
WASAPI/DirectSound audio are all `LoadLibrary`-probed with fallbacks — none is
a static import; Win10 DPI APIs are `GetProcAddress`-probed). Fedora's
`SDL2.dll` is the sdl2-compat shim and runtime-loads `SDL3.dll`
(bundle-dlls.sh already handles that).

Trade-off vs the Qt package: no menus, no preferences dialog, no Qt debugger
GUI — the SDL frontend is keyboard/CLI-driven (same core, headless and
regression-identical). Documented in `packaging/README.md`.

### The WinMain trap (why the SDL exe needs SDL2main)

jnext.exe links as a GUI-subsystem binary, so the MinGW CRT startup calls
`WinMain()`. On Windows SDL.h renames jnext's `main` to `SDL_main`; the Qt
build survives because Qt's `main=qMain` define wins and
`Qt6::EntryPointPrivate` bridges WinMain→qMain. A hand-rolled
`WinMain → main()` bridge in the SDL build resolves `main` to the **CRT's own
GUI-startup main()**, which calls WinMain again — an infinite pre-main loop
with zero output (observed under wine, +relay trace). Correct fix (done):
link `SDL2::SDL2main`, whose WinMain calls `SDL_main()`.

## 3. Deliberate API floor in the build

`CMakeLists.txt` pins `_WIN32_WINNT=0x0601` / `WINVER=0x0601` (Windows 7) for
the `WIN32 AND NOT ENABLE_QT_UI` build, so any future jnext code using a
post-Win7 API fails to **compile** instead of silently raising the import
floor. The Qt build keeps the mingw header default (0x0A00) — its bundled Qt
has a Win10 floor regardless.

## 4. 32-bit (i686) verdict: feasible, blocked only by toolchain install

- fedora 44 ships the full i686 cross stack: `mingw32-gcc`/`-gcc-c++`,
  `mingw32-qt6-qtbase` 6.11.1, `mingw32-sdl2-compat`, `mingw32-SDL3`,
  `mingw32-curl`, `mingw32-openssl`, `mingw32-zlib`, `mingw32-libpng`,
  `mingw32-winpthreads`. **None is installed on the dev host**, and this
  branch does not install system packages — so no `package-win32` target was
  built or claimed. Owner action: `sudo dnf install mingw32-gcc
  mingw32-gcc-c++ mingw32-sdl2-compat
  mingw32-zlib mingw32-libpng mingw32-winpthreads` (curl/openssl no longer
  needed since §7 Phase B — Windows uses WinHTTP/BCrypt) and mirror
  `win-sdl-release` with `mingw32-cmake` (the meaningful i686 leg is the
  SDL-only one: i686 Qt6 exists but would carry the same Win10 floor,
  defeating the point of a 32-bit build).
- **Large-file handling is NOT a blocker**: SD image I/O is
  `std::fstream`+`std::streamoff` throughout (`src/peripheral/sd_card.cpp`,
  `src/core/sd_rom_extractor.cpp`), mingw libstdc++ is built with
  `_GLIBCXX_USE_LFS` (64-bit fseeko64 offsets even on i686), and the
  canonical image is 1 GB — under the 2 GiB signed-32 limit anyway.
- Expected i686 floors: same as x86_64 per leg (the old Win8 libcrypto PathCch
  concern is moot — §7 Phase B removed OpenSSL from Windows builds entirely).
- Residual unknowns until an i686 build actually runs: none identified by
  code audit, but the claim "builds and passes smoke on i686" is deliberately
  **not** made here.

## 5. Verification method (repeatable)

1. **Import audit**: `packaging/windows/pe-floor-audit.sh <exe> <dlls...>` —
   flags Win8/8.1/Win10-only symbols and DLLs missing on Win7. Curated list,
   full-match; an unflagged file means "no known post-Win7 import", not a
   Win7 guarantee. Exit 1 when anything is flagged.
2. **Wine smoke** (wine 11.0 on the dev host): fresh `WINEPREFIX`, run
   `jnext.exe --version` and a `--headless --machine 48k
   --delayed-screenshot` boot against a throwaway copy of the SD image;
   repeat with `winecfg -v win8` and `-v win7`. Both packages passed all
   three profiles (identical 48K BASIC screenshots).
   **Limitation, stated loudly**: wine provides its own implementations of
   d3d12/apisets regardless of the version knob, so wine can validate code
   paths and version checks but **cannot** prove the DLL-availability floors.
   Those rest on the import audit; final proof of the Win8 claim needs a real
   Windows 8.x machine (owner).
3. **Structural packaging checks**: `make package-win-sdl` itself fails if
   Qt files leak into the SDL bundle or SDL2/SDL3 are missing;
   `test/packaging/packaging-test.sh` gained a `package-win-sdl` row
   (zip contents: exe + SDL2 + SDL3, no Qt6*, no platforms/) run by
   `make package-test` in CI.

## 6. Decisions

| Item | Decision |
|------|----------|
| Lower the Qt package below Win10 | **WONT** — floor is inside fedora's Qt6 binaries (d3d12 et al.), not reachable from jnext. |
| Win7/8 support | **`make package-win-sdl`** (SDL-only, x64): Win 7 SP1+ by import evidence (was 8.0 before Phase B). |
| True Win7 support | ~~Blocked~~ ~~Planned~~ **DONE (Phase B, 2026-07-26)** — curl/OpenSSL replaced on Windows with OS-native WinHTTP + BCrypt behind the same provisioner interface; PathCch blocker deleted, `pe-floor-audit.sh` reports zero post-Win7 imports for the SDL bundle. Real-Win7-hardware confirmation pending (owner). |
| 32-bit package | **Approved by owner 2026-07-26**, contingent on §7 Phases A/B (§7 Phase C); audit shows no code blocker. |
| DirectX floor | Nothing to do jnext-side: SDL3 probes all renderers/audio backends at runtime with software/DirectSound fallbacks; jnext's Qt GUI renders via QImage (no RHI use) — the Qt D3D12 issue is a load-time import, not a rendering requirement. |

## 7. Roadmap (owner-agreed 2026-07-26)

Owner decisions driving this section: Qt5 is evaluated FIRST for the legacy legs
(full GUI on Win7/8 beats SDL-only if the port cost is sane); SDL-only is the
accepted fallback if Qt5 does not pan out; the Windows download path moves to
OS-native APIs instead of dropping the feature; 32-bit ships once the above are
settled; real-Win8-hardware verification is deferred until it matters.
Explicitly REJECTED: downgrading the whole product to Qt5 to avoid dual
maintenance — Qt5 (5.15 KDE Patch Collection) is upstream-EOL, the Flatpak/rpm/deb
legs live on Qt6 runtimes, and Qt6's Wayland/HiDPI behaviour is what the emulator
widget was tuned against. Qt6 stays the primary GUI everywhere; Qt5 (if adopted)
is scoped to the legacy Windows packages only, kept green by a CI job that builds
the same plain make target a local run uses.

**Phase A — Qt5 feasibility spike (decision gate).** Fedora 44 ships
`mingw64-qt5-qtbase` AND `mingw32-qt5-qtbase` at 5.15.18 (KDE Patch Collection);
Qt 5.15 supports Windows 7 SP1+ and predates the D3D12/Win10-DPI imports that
give Qt6 its floor. Deliverables: `#if QT_VERSION` port assessment of src/gui +
src/debugger (count the delta), a trial `win-qt5-release` build, and a
`pe-floor-audit.sh` run over the resulting bundle proving the Win7 claim.
Gate: small delta → Qt5 full-GUI legacy legs (64-bit now, 32-bit in Phase C);
large delta → SDL-only fallback stands (owner-accepted).

### Phase A findings (2026-07-26, branch `fix/108-qt5-spike`) — verdict: **GO**

Full Qt6-only usage inventory of `src/gui` + `src/debugger` (the only Qt-using
trees — verified by tree-wide grep: no Qt in `src/platform`, `src/main.cpp`
(only `gui/qt_app.h`, itself Qt-free), or `test/`; `app_config_test` links Qt
transitively via `jnext_gui`). All 66 distinct `<QXxx>` headers used exist in
Qt 5.15. Initially a source-level assessment (no Qt5 devel on the host); after
the owner installed the Qt5 toolchains it was **implemented and proven** — see
"Phase A execution results" below.

**Qt6-only source usages needing `#if QT_VERSION` guards — 4 lines, 2 files:**

| Site | Qt6-only call | Qt5.15 replacement |
|------|---------------|--------------------|
| `src/debugger/disasm_panel.cpp:365` | `QMouseEvent::position()` | `localPos()` (or portable `pos()` — already `int`-cast) |
| `src/debugger/disasm_panel.cpp:368` | `QMouseEvent::position()` | same |
| `src/gui/main_window.cpp:1605` | `QMouseEvent::globalPosition().toPoint()` | `globalPos()` |
| `src/gui/main_window.cpp:1637` | `QMouseEvent::globalPosition().toPoint()` | `globalPos()` |

`position()`/`globalPosition()` are Qt6 `QSinglePointEvent` API; Qt 5.15 has
neither. Guard form: `#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)` so the Qt6
compile path stays textually identical (avoids Qt6 deprecation noise that a
portable `pos()`/`globalPos()` rewrite would emit).

**Additive Qt5-only parity — 1 file, ~4 guarded lines:** `src/gui/qt_app.cpp`
must set `Qt::AA_EnableHighDpiScaling` + `AA_UseHighDpiPixmaps` *before* the
`QApplication` ctor (Qt5 HiDPI is opt-in; both are default/no-op in Qt6).

**CMake — 3 sites:** `src/gui/CMakeLists.txt:1,14` and
`src/debugger/CMakeLists.txt:1,10` (dual `find_package(QT NAMES Qt6 Qt5)` +
`Qt${QT_VERSION_MAJOR}::Widgets`), and root `CMakeLists.txt:291-292` (WIN32
entry point: `Qt6::EntryPointPrivate` → `Qt5::WinMain`; fedora ships it as
`libqt5main.a` in `mingw64-qt5-qtbase`). The APPLE macdeployqt block is
untouched — the Qt5 leg is Windows-only. **Total delta: ~25 lines across 6
files.**

**Verified Qt5.15-clean (no change needed):** `QKeySequence(Qt::CTRL |
Qt::Key_X)` (int promotion — the 5.15-recommended form),
`QFontDatabase::systemFont` (static since 5.2), `horizontalAdvance` (5.11),
`QWidget::screen()` (5.14), every dual-overload signal connect already wrapped
in `QOverload<int>::of` (`QSpinBox::valueChanged`,
`QComboBox::currentIndexChanged` ×3), `angleDelta()`,
`QContextMenuEvent::globalPos()`, `QMouseEvent::pos()`
(`memory_panel.cpp:400`), `QSignalBlocker` (5.3), `QTimer` PreciseTimer +
context-lambda `singleShot` (5.4), `devicePixelRatioF()` (5.6), `QDataStream`
of QSize/QPoint (wire format identical across 5/6). **Hunted, zero hits:**
QRegExp/QRegularExpression, QKeyCombination, QDesktopWidget, QOpenGL*,
QStringView/QStringRef, QTextCodec, qputenv, QVariant::typeId, enterEvent
overrides, checkStateChanged, module-scoped `<QtGui/...>` includes, QWindow /
native-interface use, QtConcurrent/QThread, QtNetwork (curl is used instead).

**Toolchain evidence (dnf repoquery, fedora 44):** `mingw64-qt5-qtbase` 5.15.18
ships `Qt5Config.cmake`/`Qt5WidgetsConfig.cmake` (sys-root cmake dir),
`Qt5Core/Gui/Widgets.dll`, `qt5/plugins/platforms/qwindows.dll` and
`libqt5main.a`; `mingw64-qt5-qmake` ships the cross host tools AUTOMOC needs
(`x86_64-w64-mingw32-{moc,rcc,uic}-qt5`). The package's auto-generated
`mingw64(*.dll)` requires contain **no d3d12/dcomp** — d3d11/dxgi only, both
present on Win7 RTM — consistent with Qt 5.15's official Win7 SP1+ support.
Not final proof: the floor claim still needs `pe-floor-audit.sh` on a real
`win-qt5-release` bundle.

**Risk spots (none gate-blocking):**
1. WinMain/SDL_main/qMain interplay (§2's trap class): `Qt5::WinMain` injects
   `QT_NEEDS_QMAIN` → `main=qMain`, believed equivalent to
   `Qt6::EntryPointPrivate`'s SDL-rename disarm — verify under wine on the
   trial build before claiming it.
2. AUTOMOC must resolve `Qt5::moc` to the cross moc from `mingw64-qt5-qmake`
   — check at first configure.
3. `bundle-dlls.sh` + `packaging-test.sh` structural rows are written for
   `Qt6*.dll`; a `win-qt5` leg needs its own DLL list (`Qt5*.dll`, plugin path
   `lib/qt5/plugins/platforms`).
4. Qt5 HiDPI behaves differently even with the attributes (integer-ish
   scaling); acceptable for the Win7-era target (DPI 96 dominant).

**Owner install list:**
- MinGW Qt5 trial build (64-bit): `mingw64-qt5-qtbase mingw64-qt5-qtbase-devel
  mingw64-qt5-qmake`
- Linux Qt5 validation build (compile-proves this assessment):
  `qt5-qtbase-devel`
- Phase C 32-bit (if this leg ships): `mingw32-qt5-qtbase
  mingw32-qt5-qtbase-devel mingw32-qt5-qmake`

**Gate verdict: GO.** The delta is ~25 lines / 6 files, every Qt6-only usage is
cleanly guardable, there is no architectural Qt6 coupling (no RHI, no QOpenGL,
no Qt6-only widgets or signals), and the fedora mingw Qt5 stack provides
everything the trial build needs including the WinMain bridge.

### Phase A execution results (2026-07-26, same branch) — **GO CONFIRMED, Win7 floor by import evidence**

The assessment above was implemented and proven after the owner installed the
Qt5 toolchains (`qt5-qtbase-devel`, `mingw64-qt5-qtbase{,-devel}`,
`mingw64-qt5-qmake`; mingw32 stack staged for Phase C).

**Delta as implemented (3 commits):**
- `src/debugger/disasm_panel.cpp` — one `#if QT_VERSION` guard (`position()` /
  `localPos()` hoisted into a `QPointF evpos`, both uses).
- `src/gui/main_window.cpp` — `mouse_global_point()` helper in the existing
  anonymous namespace (`globalPosition().toPoint()` / `globalPos()`), used at
  both call sites.
- `src/gui/qt_app.cpp` — Qt5-only `AA_EnableHighDpiScaling` +
  `AA_UseHighDpiPixmaps` before the QApplication ctor.
- CMake mechanism: root option **`JNEXT_FORCE_QT5`** (default OFF) → Qt major
  decided ONCE at root into `JNEXT_QT_MAJOR`: forced 5 by the option, else
  `find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)` — **Qt6 always
  wins when installed**; Qt5 is only a fallback on a Qt6-less host.
  `src/gui`/`src/debugger` consume `Qt${JNEXT_QT_MAJOR}`. The Win7
  `_WIN32_WINNT=0x0601` pin now also covers `WIN32 AND JNEXT_FORCE_QT5`.
- `Makefile` — `win-qt5-release` target (mirrors `win-release`, full GUI +
  debugger, `JNEXT_FORCE_QT5=ON`); `bundle-dlls.sh` detects the Qt major from
  the exe imports and picks `lib/qt5/plugins` + `qwindowsvistastyle.dll`.

**The §2 WinMain trap fired, Qt5-flavoured, exactly as predicted** — and is
fixed in CMake, not by hand-rolled code: fedora's `Qt5::WinMain`
(`libqt5main.a`, WinMain → C++-mangled `qMain(int,char**)`) ships **no**
interface compile definitions and no link-order helper, so (a) plain linking
left WinMain unreferenced when the archive was scanned → `undefined reference
to WinMain` from `crtexewin.o`, and (b) nothing renamed `main.cpp`'s `main`.
The fix replicates Qt6's own mechanics: `target_compile_definitions(jnext
PRIVATE QT_NEEDS_QMAIN)` (qwindowdefs.h's `#define main qMain` lands after
SDL.h's rename in the TU, so it wins — identical include-order mechanics to
the working Qt6 build) plus `target_link_libraries(jnext PRIVATE mingw32
Qt5::WinMain)` — the same `-lmingw32`-before-the-entry-archive ordering Qt6
encodes in its `EntryPointMinGW32` helper target.

**Linux-Qt5 compile proof** (`-DJNEXT_FORCE_QT5=ON`, GUI+debugger ON): builds
clean, links `libQt5{Core,Gui,Widgets}.so.5`; `--version` OK; headless 48K
boot screenshot correct; full GUI ran with the frame timer + delayed
screenshot (window on the live session). All screenshots pixel-identical
(sha256 of decoded RGB) to each other.

**MinGW Qt5 trial** (`make win-qt5-release`): builds + links (cross moc from
`mingw64-qt5-qmake` picked up by AUTOMOC automatically); bundle = jnext.exe +
27 DLLs + `platforms/qwindows.dll`, `styles/qwindowsvistastyle.dll`,
`imageformats/{qgif,qico,qjpeg}.dll` + qt.conf.

**Floor audit (`pe-floor-audit.sh` over the ENTIRE bundle — exe + all 25
shipping DLLs + 5 plugin DLLs): exit 0, ZERO flags.** No WIN8/WIN8.1/WIN10
symbol imports, no not-on-Win7 DLLs anywhere (PE headers MajorOSVersion 4.0
throughout; no d3d12, no DPI-context APIs, no PathCch — the curl/OpenSSL
chain is already gone via Phase B). By static-import evidence the Qt5
full-GUI bundle has a **Windows 7 SP1 floor** — subject to the §5 caveat that
only real hardware proves DLL availability.

**Wine smoke (wine 11.0, fresh prefix): all green.** `--version`, headless
48K boot screenshot, and the full Qt5 GUI (qwindows.dll platform plugin, Qt5
event loop, delayed screenshot) — on the default profile AND `winecfg -v
win7`. Every PNG pixel-identical to the Linux-Qt5 run.

**Qt6 default proven untouched:** `make clean && make gui-release` (selects
"Qt major: 6"), then unit **5688/5688** (76 suites), FUSE **1356/1356**,
regression **106/106** (`JNEXT_TEST_JOBS=4`, logged) — all green on the
default build with zero Qt5 anywhere.

**Shipping surface (added same day, owner-required):**
- `make package-win-qt5` → `jnext-<ver>-windows-x64-qt5.zip` (a PUBLISHED
  release artifact per the owner's lineup), with in-target structural checks
  (Qt5Core + qwindows + SDL2/SDL3 present, no Qt6 leak) and a
  `package-win-qt5` packaging-test row (Qt5 DLLs + qwindows present, NO Qt6,
  no curl/OpenSSL chain — note iconv.dll is legitimate here, Qt5Core imports
  it — and jnext.exe GUI-subsystem). `make package-test`: 16 pass / 0 fail /
  0 skip. The zip's staged dist re-audits floor-clean (exit 0, zero flags).
- `make qt5-guard-build` — Linux Qt5 compile guard (configure with
  `JNEXT_FORCE_QT5=ON`, full GUI+debugger build, `--version` smoke): the
  dual-maintenance enforcement target.
- CI (plain make targets, nothing piped): ci.yml gains a `qt5-guard` job
  (fedora:44, `qt5-qtbase-devel`, runs `make qt5-guard-build`) and its
  packaging job installs `mingw64-qt5-qtbase{,-devel}` + `mingw64-qt5-qmake`
  so the new packaging-test row builds rather than skp_ci_fail-ing;
  release.yml's windows job installs the same trio, runs
  `make package-win-qt5`, and uploads the qt5 zip (the publish job's
  `dist/**/*` then attaches it to the Release).

Remaining before calling the leg done: real-Win7/8-hardware verification of
the audited floor (§5 caveat).

**Phase B — native Windows provisioning path (independent of A). DONE
2026-07-26** (branch `fix/108-native-provisioner`). The curl/OpenSSL surface
(one download + sha256) was split out of `sdcard_provisioner.cpp` into
per-platform backends behind the unchanged provisioner interface:
`sdcard_provisioner_net_curl.cpp` (POSIX, libcurl + OpenSSL EVP, verbatim) and
`sdcard_provisioner_net_win.cpp` (Windows, WinHTTP + BCrypt/CNG — redirects,
HTTP≥400 fail, 30 s connect / 120 s stall timeouts, progress + user abort,
OS-store TLS validation, explicit TLS 1.2 opt-in). Windows builds link
`winhttp`+`bcrypt` (OS components) and neither curl nor OpenSSL
(`find_package(CURL/OpenSSL)` is now POSIX-only). Evidence: SDL bundle 16→8
DLLs (curl chain gone), `pe-floor-audit.sh` exit 0 / zero flags; packaging-test
rows assert the chain stays gone from BOTH Windows zips; wine (fresh prefix)
ran `--version`, a 48K headless boot screenshot, AND a full provisioning
exercise against a loopback HTTP fixture server via the
`$JNEXT_SDCARD_DISTRO_URL` test seam — WinHTTP followed a 302, the BCrypt
sidecar hash matched host `sha256sum`, FatFs repatched the image, and a 404
failed loud with no partial files. Wine cannot prove real-Windows TLS or DLL
floors (§5 caveat) — those await real hardware.

**Phase C — 32-bit packages.** After A+B: `package-win32` mirroring the winning
leg (Qt5 full GUI if A passed, else SDL-only) via `mingw32-cmake`. Local builds
need the owner-installed mingw32 toolchain; the CI container installs its own
packages in the workflow, keeping CI-runs-exact-local-commands intact. Re-run
the floor audit on the i686 bundle before claiming any floor.

**Verification at every phase:** `pe-floor-audit.sh` on every produced bundle,
`make package-test` structural rows for every new target, wine smoke (win7/win8
profiles) as code-path checks — with the §5 caveat that only real hardware
proves DLL availability floors.
