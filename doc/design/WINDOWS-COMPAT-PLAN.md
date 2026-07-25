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
| `libcrypto-3-x64.dll` | `PathCchRemoveFileSpec` ← `api-ms-win-core-path-l1-1-0.dll` | Windows 8 |

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

## 2. SDL-only variant `make package-win-sdl`: floor **Windows 8.0**

`ENABLE_QT_UI=OFF` + MinGW cross-build → `jnext-<ver>-windows-x64-sdl.zip`
(16 DLLs, no Qt). Audit result: the **single** remaining post-Win7 import in
the whole bundle is fedora libcrypto-3's `PathCchRemoveFileSpec`
(`api-ms-win-core-path-l1-1-0.dll`, Windows 8+). Everything else is
Win7-clean.

- **Windows 8.0 / 8.1: expected to work** (all imports resolve).
- **Windows 7: blocked** — `libcrypto-3-x64.dll` fails to load, and it is a
  startup (non-delay) dependency of `libcurl-4.dll`, which `jnext.exe` links
  for SD-image self-provisioning (`find_package(CURL REQUIRED)` +
  `OpenSSL::Crypto` in `src/core`). Fixing this would mean either fedora
  rebuilding OpenSSL without PathCch (not ours) or making curl/OpenSSL
  optional in jnext core (a feature amputation — SD download + hash verify —
  needing an owner decision; not done here).

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
  mingw32-gcc-c++ mingw32-sdl2-compat mingw32-curl mingw32-openssl
  mingw32-zlib mingw32-libpng mingw32-winpthreads` and mirror
  `win-sdl-release` with `mingw32-cmake` (the meaningful i686 leg is the
  SDL-only one: i686 Qt6 exists but would carry the same Win10 floor,
  defeating the point of a 32-bit build).
- **Large-file handling is NOT a blocker**: SD image I/O is
  `std::fstream`+`std::streamoff` throughout (`src/peripheral/sd_card.cpp`,
  `src/core/sd_rom_extractor.cpp`), mingw libstdc++ is built with
  `_GLIBCXX_USE_LFS` (64-bit fseeko64 offsets even on i686), and the
  canonical image is 1 GB — under the 2 GiB signed-32 limit anyway.
- Expected i686 floors: same as x86_64 per leg (the Win8 libcrypto PathCch
  import must be re-verified on `mingw32-openssl` after install).
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
| Win7/8 support | **`make package-win-sdl`** (SDL-only, x64): Win 8.0+ by import evidence. |
| True Win7 support | **Blocked** by fedora libcrypto-3's PathCch import; would need curl/OpenSSL made optional (owner decision) — documented, not implemented. |
| 32-bit package | **Deferred to owner** (install mingw32 toolchain); audit shows no code blocker. |
| DirectX floor | Nothing to do jnext-side: SDL3 probes all renderers/audio backends at runtime with software/DirectSound fallbacks; jnext's Qt GUI renders via QImage (no RHI use) — the Qt D3D12 issue is a load-time import, not a rendering requirement. |
