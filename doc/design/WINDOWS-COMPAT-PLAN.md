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

## 4. 32-bit (i686) SDL leg: **BUILT + audited Win7-clean** (§7 Phase C, 2026-07-26)

> **Repo-internal validation target, not a published artifact** (owner
> decision 2026-07-26, see §7 Phase C): the published 32-bit leg will be
> i686-Qt5 after Phase A merges. Everything below — toolchain, bundling,
> floor audit, wine/LFS evidence — validates i686 itself and carries over.

Toolchain (owner-installed): `mingw32-gcc`/`-gcc-c++` 16.1.1,
`mingw32-sdl2-compat`, `mingw32-SDL3`, `mingw32-zlib`, `mingw32-libpng`,
`mingw32-winpthreads` (curl/openssl not needed since §7 Phase B).
Targets: `make win32-sdl-release` / `make package-win32-sdl`
(`mingw32-cmake`, `ENABLE_QT_UI=OFF -DENABLE_DEBUGGER=OFF`) →
`jnext-<ver>-windows-x86-sdl.zip`. The `_WIN32_WINNT=0x0601` compile pin,
the 16 MB stack reserve, the GUI subsystem + SDL2main link and the UTF-8
manifest all fire for i686 too (same arch-agnostic CMake blocks).
`bundle-dlls.sh` now reads the exe's PE COFF Machine field and resolves DLLs
from the matching sysroot (`/usr/i686-w64-mingw32/...` for `014c`), so one
script serves both packages.

- **Bundle**: the exact i686 twins of the x64 SDL set — 8 DLLs
  (`libgcc_s_dw2-1.dll` in place of x64's SEH libgcc; SDL2/SDL3,
  libstdc++-6, libwinpthread-1, libpng16-16, zlib1, libspdlog), no Qt,
  no curl/OpenSSL.
- **Floor audit** (`pe-floor-audit.sh` over the whole staged bundle):
  **exit 0 — zero post-Win7 imports** in all 9 files. PE headers
  MajorOSystemVersion 4 / MajorSubsystemVersion 4 (i686 GNU ld defaults —
  NT4-era, not a limiter). The audit's objdump parsing was positively
  verified on i686 PE (345 import symbols parsed from jnext.exe, 395 from
  SDL3.dll; the flag path still fires on mingw64 Qt6Gui.dll). i686 msvcrt
  helpers like `___chkstk_ms` are libgcc-internal, not imports — absent
  from the tables, as expected.
- **Large-file proof (the classic i686 LFS risk), under wine 11 (fresh
  prefix, 32-bit PE run natively)**: `--version` OK; 48K headless boot
  screenshot OK; **full NextZXOS boot to the menu against the canonical
  1 GB image — pixel-identical (0 differing pixels) to the checked-in x64
  Linux regression reference** (`boot-nextzxos-menu`), exercising 1 GB of
  FAT32 seeks through both the host-side ROM extractor and the runtime
  SPI/SD `std::fstream` path. Additionally the **full provisioning path**
  ran on i686 via the `$JNEXT_SDCARD_DISTRO_URL` loopback seam: WinHTTP
  downloaded the 1 GB fixture zip, the BCrypt SHA-256 sidecar **matched
  host `sha256sum` exactly**, the extracted raw was byte-identical
  (`cmp`), and FatFs repatched + mounted the image for a 48K boot.
- **Qt5-i686 side note** (pending §7 Phase A): fedora's `mingw32-qt5-qtbase`
  5.15.18 `Qt5Gui.dll` audits **clean** at the curated-list level (exit 0) —
  consistent with Qt 5.15's Win7 support claim; a Qt5-i686 leg would reuse
  the same `win32` plumbing if Phase A gates GO.
- Remaining unknowns: only what wine cannot prove — real-Windows DLL
  availability floors and TLS behaviour need real 32-bit Windows hardware
  (§5 caveat; owner).

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

**Phase C — 32-bit packages. i686 SDL validation infrastructure DONE
2026-07-26** (branch `fix/108-win32`; valid regardless of the Phase A Qt5
gate). **Owner product decision 2026-07-26: SDL-only Windows packages are
DISCARDED as published artifacts** — the final published lineup is x64-Qt6
(Win10), x64-Qt5 (Win7, full GUI) and **i686-Qt5** (Win7, full GUI;
`jnext-<ver>-windows-x86-qt5.zip`, built after Phase A's Qt5 guards merge).
The win32-sdl work therefore ships as **repo-internal infrastructure only**:
it is the 32-bit toolchain/LFS validation and the plumbing the i686-Qt5 leg
will reuse. Delivered: `make win32-sdl-release` + `make package-win32-sdl`
(`jnext-<ver>-windows-x86-sdl.zip`, internal — deliberately NOT in
release.yml's published artifacts); PE-architecture detection in
`bundle-dlls.sh` (i686/x86_64 sysroot from the exe's COFF Machine field);
a `package-win32-sdl` row in `test/packaging/packaging-test.sh` (same
assertions as the x64 SDL row), kept green in CI by the mingw32 package set
in ci.yml's package job. Evidence in §4: floor audit exit 0 (Win7-clean),
wine smoke incl. the 1 GB LFS proof (NextZXOS menu pixel-identical to the
x64 reference) and a full loopback WinHTTP+BCrypt provisioning run on i686 —
all validating the i686 target independently of the GUI toolkit.
**Outstanding for Phase C**: the i686-Qt5 leg once Phase A merges
(mingw32-qt5 5.15.18 Qt5Gui audits clean — §4 side note), and real 32-bit
Windows hardware verification (owner).

**Verification at every phase:** `pe-floor-audit.sh` on every produced bundle,
`make package-test` structural rows for every new target, wine smoke (win7/win8
profiles) as code-path checks — with the §5 caveat that only real hardware
proves DLL availability floors.
