# JNEXT in the browser: detailed plan (epic #14)

Status: **DRAFT for owner review** (2026-11-06; the owner's decisions of
2026-11-06 are folded in, §3). Nothing in this plan is started. When the
owner approves it, its stages become the sub-issues of #14.

Sources:

- epic #14 and its comments (requirements, licensing, hosting);
- the forum conversation with Shrek (mb-maniax.cz), posted on #14 for reference;
- Shrek's WebAssembly port of jnext 1.1.55: `jnext.patch` (52 files, base
  198b302), his web layer (`web/*.cpp`), `build.ps1`, his README and his live
  page, all analysed critically against current `main` (v1.2.0);
- the code of `main` as of 2026-11-06.

Every file:line below refers to `main` unless it says otherwise.

---

## 1. Goal

A visitor opens a web page and plays a ZX Spectrum Next program in it, with
nothing to install. A developer puts a game on their page (itch.io included)
with a few lines of HTML and JavaScript, as RVMPlayer does for the +3 and CPC.

The browser build is the same emulator as the desktop one: the same core, the
same accuracy, the same tests. It is not a fork and not a second emulator.

### 1.1 Use cases

| id | who | what |
|---|---|---|
| U1 | game author | embeds `jnext-<version>.js` and `game.nex` in a page; the game runs in a canvas of a given size |
| U2 | game author | the same inside an itch.io game page (a cross-origin iframe) |
| U3 | visitor | opens such a page, clicks, plays with keyboard and gamepad, with sound |
| U4 | visitor | comes back later: the emulator starts fast, without downloading the system image again |
| U5 | JNEXT maintainer | publishes the web build with every public release, versioned, built by CI |

Out of scope for the first version (owner can move any of these in, see §3):
the debugger, mobile touch controls, CRT effects, video/RZX recording,
NextPi, ESP Wi-Fi, the remote debugger servers, and saving the guest's writes
across page reloads.

---

## 2. Requirements

### 2.1 From the epic

| id | requirement | source |
|---|---|---|
| R1 | Embed a JS file plus a `.nex` in a page and play in the browser | dcrespo3d (issue body) |
| R2 | RVMPlayer-like: one call, parameters, WebAssembly + WebGL | dcrespo3d |
| R3 | Works in itch.io game pages (cross-origin iframe) | dcrespo3d |
| R4 | Viewport size parameter (w, h) | dcrespo3d |
| R5 | File parameter: `.nex` (maybe `.tap`) | dcrespo3d |
| R6 | No debugger in the first version | dcrespo3d, jorgegv |
| R7 | Start as fast as possible; no big download on every start | dcrespo3d |
| R8 | Don't hit specnext.com on every start: cache on the client | dcrespo3d |
| R9 | NextZXOS may be redistributed only "in its entirety"; unlicensed parts are closed source | janko-jj, quoting the tbblue LICENSE.md |
| R10 | Minimal NextZXOS boot image, downloaded on first use and cached in the browser | jorgegv 2026-07-29, 2026-09-26 |
| R11 | Phoebus Dokos will host a minimal NextZXOS image; it **must** be hosted on SpecNext, nowhere else; who prepares it is open | jorgegv 2026-11-05 |
| R12 | Does `--load file.nex --esxdos-stub-root` avoid the SD image? | vmorilla |

R12, answered: not on `main`. `main.cpp:1806-1838` refuses to start without an
image, and a `.nex` runs on a warm-start recording of a real NextZXOS boot of
that image. The core itself copes with an empty `sd_card_image`
(`emulator.cpp:6861`); only `main.cpp` refuses. This plan keeps the faithful
path (§4.4), so the answer stays no.

Rejected suggestions, with the reason:

- *Packing the ROMs or the image into the wasm* (dcrespo3d): forbidden by R9
  and R11.
- *Obfuscating or encrypting the assets* (dcrespo3d), *usable but not
  downloadable* (Duefectu): not enforceable in a browser (anything a page
  fetches is visible in its developer tools), and not a licence mechanism.
- *localStorage is only 5 MB* (dcrespo3d): true, but IndexedDB and the Cache
  API hold far more; Shrek keeps a ~6 MB card in IndexedDB.

### 2.2 Project constraints (CLAUDE.md, PULL-REQUEST-PROTOCOL.md)

| id | constraint |
|---|---|
| C1 | All ROMs come from the SD image; only `nextboot.rom` is embedded |
| C2 | Every setting has a CLI way (`cli_options.h`, `--set`): the JS parameters map onto them |
| C3 | CI runs the exact make targets a local run uses; a SKIP fails the gate |
| C4 | No new dependency without owner approval; one topic per PR; tests with every change; existing tests not modified without owner approval |
| C5 | Program text is ASCII (`lint-ascii-text`) |
| C6 | No hard-coded version: artifact names come from `version.yaml` |
| C7 | The warm-start recording is never shipped (it contains NextZXOS and DivMMC ROM content): it is recorded in the visitor's browser |
| C8 | Child processes run under `LANG=C` (moot in a browser: there are none) |

---

## 3. Decisions (owner, 2026-11-06)

| # | question | decision |
|---|---|---|
| D1 | **CORS on SpecNext.** Today `www.specnext.com` sends no `Access-Control-Allow-Origin` (checked 2026-11-06, also by Shrek in October), so a page on any other origin cannot fetch the image. | The owner asks Phoebus Dokos, with the image request: `Access-Control-Allow-Origin: *` on the image URL, a stable **versioned** URL, a published sha256 and size. Without it the only fallback is "the visitor picks the file by hand" (Shrek's current page). This is the critical path of the epic (S0). |
| D2 | **The image recipe** | **The boot set only**: TBBLUE.FW, MACHINES, NEXTZXOS and SYS, plus the licences (about 2.4 MB of clusters on the CSpect image). No DOT, APPS, games, demos or docs. Correctly clustered, so the browser needs no `patch_image_fat32` step; small enough to hold in memory. **We make it** (owner, 2026-11-06), with a small Bash script that turns the distribution image into the minimal one (§4.4, S0), and send Phoebus the script and a tested image with D1. |
| D3 | A "direct NEX without NextZXOS" mode (Shrek's default: a 48K ROM file injected into the ROM area) | **No.** It breaks C1, runs programs on a synthetic machine that needs invented register values (Shrek forces NR 0x4A and MAXBNK), and duplicates what the warm start gives: the NextZXOS boot is a one-time cost per visitor. |
| D4 | Formats in v1 | **`.nex`, `.tap` and `.tzx`.** Snapshots and the other desktop formats later, one sub-issue each. |
| D5 | Guest writes surviving a reload (saves, high scores) | **Not in v1**: guest writes live in memory for the session. A follow-on stage (S11). |
| D6 | Hosting of the browser build | **GitHub release assets only**: each public release carries the files; authors host them on their own pages (itch.io uploads them with the game). **Two files** (`jnext-<version>.js` + `.wasm`), not `-sSINGLE_FILE` (base64 costs +33% and prevents streaming compilation). |
| D7 | Frontend: SDL3's Emscripten port, or a thin C API with a JS frontend (Shrek's way) | **SDL3 port**, with the SDL frontend's loop turned into a per-frame `tick()` (S4). It reuses the tested `src/platform` logic (input latching, pacing, audio fill) instead of rewriting it in JS. A spike opens S5; if the port proves immature, the thin C API is the fallback, brought back to the owner as a decision. |
| D8 | New dependencies | **Approved**: a pinned Emscripten SDK (with its SDL3, zlib and libpng ports) and Playwright + Chromium for the browser smoke test, both pinned (version + sha256) and provisioned into `~/.cache/jnext/tools` like the `z88dk-gdb` client. |
| D9 | Performance floor | **50 fps over the whole regression NEX set, at 3.5 and at 28 MHz**, in the dev host's Chrome and Firefox **and** in Chrome with a **2x CPU throttle** (standing for a modest laptop). Estimated 20-34 ms per frame at 2x for the heaviest programs, so this is borderline: #452 and profiling are likely needed, and S10 carries the work until it holds. (A 4x throttle was considered and rejected: 40-70 ms per frame estimated.) |
| D10 | Mobile and touch in v1 | **No.** It may run on a phone with a hardware keyboard, unsupported. A touch keyboard is a follow-on. |
| D11 | CRT effect on the web | **No in v1**: the shader is GLSL 1.20 on a Qt GL 2.1 context (`src/gui/crt_gl_overlay.cpp:43-45`); a WebGL port is a follow-on. |
| D12 | How Shrek's work enters JNEXT | **We reimplement everything** from his ideas, with our tests; none of his code is merged. Shrek is credited (CREDITS, ChangeLog), and asked for the material only he has (§5). No attribution comments in the code. |
| D13 | Android and the VS Code debug server (hooks in his patch: `JNEXT_ANDROID_DIR`, `JNEXT_SERVER_DIR`) | **Out of scope.** If wanted later, their own epics, with their code in-tree. |

Side issues found in Shrek's patch were filed separately (§9), with the owner's
approvals for the existing rows they may change.

## 4. Architecture

### 4.1 Build target

A new CMake configuration, built with a pinned Emscripten SDK:

- `ENABLE_QT_UI=OFF`, `ENABLE_DEBUGGER=OFF` (the SDL-only configuration,
  already one of the four supported and unit-tested ones);
- `EMSCRIPTEN` arms in CMake where `main` assumes a desktop, instead of
  Shrek's source-list `FILTER` hack and force-included `web_compat.h`:

| piece | today on `main` | browser build |
|---|---|---|
| libcurl, OpenSSL | `find_package` unless `WIN32` (`CMakeLists.txt:215-218`), linked in `src/core/CMakeLists.txt:88-89` | not used; a third provisioner backend `sdcard_provisioner_net_web.cpp`: a portable SHA-256 (FIPS 180-4) and "download unavailable" (downloads happen in JS, §4.4). It must pass the existing SHA256-01* rows (`test/sdcard_provisioner/sdcard_provisioner_test.cpp:2184-2188`). Shrek's `sdcard_net_stub.cpp` is the model. |
| zlib, libpng | required (`CMakeLists.txt:208`; `src/core/CMakeLists.txt:75,80`) | Emscripten ports |
| SDL3 | `find_package(SDL3 REQUIRED)` (`CMakeLists.txt:195`) | Emscripten SDL3 port (D7) |
| spdlog, nlohmann-json | vendored, header-only | unchanged |
| NextPi QEMU (`posix_spawn`, `pi_qemu.cpp:291-294`), ffmpeg (`system()`, `video_recorder.cpp:56`) | built | compiled out; the flags are refused with a message |
| ESP sockets, ZRCP, DZRP, GDB listeners (`src/remote/transport.cpp:164`), ICMP ping | built | compiled out or refused at the CLI: browser sockets cannot `listen()` |
| `std::thread` (ESP, Pi audio, QEMU: `esp_threaded.cpp:52`, `pi_audio.cpp:192`, `pi_qemu.cpp:513`) | built | unreachable: gated with the features above. The core is single-threaded, so the build needs no pthreads, no SharedArrayBuffer and no COOP/COEP headers (which itch.io pages and a cross-origin image would make hard) |
| `jnext_remote`, `jnext_script` linked by `jnext_platform` (`src/platform/CMakeLists.txt:22,24`) | built | not linked in the browser build (size) |
| C++ exceptions (23 `try` blocks outside Qt; throwing `std::filesystem` and `nlohmann::json`) | on | `-fwasm-exceptions` (Emscripten disables catching by default, and a throw would abort); its size and speed cost is measured in S1 |

Portability fixes `main` needs anyway, found in his patch:

- `src/memory/ram.cpp:32` calls `std::fill` without `#include <algorithm>`
  (only `<cstring>`): a libc++ build error. The one-line fix, as in his patch.
- `PAGE_SIZE` collides with a libc macro under Emscripten (`nex_loader.h:467`,
  `szx_loader.cpp:150`, `szx_saver.h:201`): rename the constants instead of
  `#undef`ing the macro.

### 4.2 Main loop

`SdlApp::run()` is a blocking `while (running_)` with `SDL_Delay`
(`sdl_app.cpp:468-816`); a browser needs control back after every frame.

Plan: move the loop body into `SdlApp::tick()` (S4). The native build keeps
its `while` loop calling `tick()`, with no behaviour change, proven by the full
native gate (`host_key_latch_test` drives the real `SdlInput::poll()`). The
browser calls `tick()` from `emscripten_set_main_loop` (or SDL3's
`SDL_MAIN_USE_CALLBACKS`). Pacing stays in the existing `frame_sequencer.h`,
`frame_deadline.h` and `audio_pacing.h`, so the browser gets the same pacing
the desktop has, not a reimplementation in JS (Shrek's `jnx_run_frame` plus
his JS loop).

ASYNCIFY (no refactor) was considered and rejected: bigger, slower, and it
hides the loop structure the browser needs.

### 4.3 Video, audio, input

- **Video**: SDL_Renderer streaming texture (`sdl_display.cpp:28,57,71`) on
  WebGL. Canvas size from the JS parameters, letterboxed (the framebuffer is
  640x256, shown 5:4).
- **Audio**: SDL's push model (`sdl_audio.cpp:99`) on Web Audio. Browsers
  allow sound only after a user gesture: the page shows a "click to start"
  overlay, which is also where the first download and warm start report
  progress.
- **Input**: keyboard bound to the canvas (focus, `tabindex`); all keys
  released on blur (otherwise keys stick); the Alt host hotkeys kept where the
  browser lets them through; Ctrl+W/T, F5, F11 are the browser's. Gamepads
  through SDL's joystick layer (to be verified in S5: browsers expose a pad
  only after a button press). Kempston mouse through pointer lock.

### 4.4 The system image and the warm start

The faithful path `main` already uses, moved into the browser:

1. **First visit.** JS fetches the minimal image from its SpecNext URL (D1,
   D2), checks its sha256, and stores it in the Cache API (or IndexedDB).
2. JS hands the image to the emulator before `callMain` (the Emscripten
   filesystem). The provisioner never downloads or asks on stdin in the
   browser: `sdcard::cli_confirm` reads `std::cin`
   (`sdcard_provisioner.cpp:630-634`), which would become `window.prompt()`.
3. `--load game.nex` runs as on the desktop: on a **warm-start recording** of a
   real NextZXOS boot of that image, keyed by the image identity, machine and
   format. The first visit records it (about 500 frames; Shrek reports ~5 s in
   wasm); the page shows progress.
4. The recording is stored in IndexedDB, in the visitor's browser only (C7).
   Later visits restore it (Shrek reports under a second).
5. Guest writes to the card stay in memory for the session (D5).

**The image in memory.** If the hosted image is small (D2), it is a plain file
in the in-memory filesystem and no new code is needed in the SD path. If it is
large (the 1 GB layout), every SD reader (`sd_card`, `fat32_image`,
`fatfs_diskio`, `sd_rom_extractor`) needs a block-device seam with a sparse
in-memory implementation. Shrek's `SdBackingFile` shows the seam is needed,
but its form (a fake `std::fstream` keyed by path in a global registry) is
not ours: it would be an injected `SdBlockDevice` with its own unit rows. The
decision waits on D2.

**The image identity** on the desktop is size + mtime; in the browser it is
the sha256 the JS checked (one comparison, no hashing a large file).

The image can also arrive zipped: `main` already reads zip archives
(`program_archive.h`, `save/zip_interchange.h`), so no new dependency.

**Making the minimal image** (S0, D2). `tools/make-minimal-sdcard-image.sh
<distribution.img> <minimal.img>` turns the image as SpecNext publishes it into
the minimal one. It is a minimal Bash script (`set -euo pipefail`), with no
root and no loop mount, using only Linux tools:

- the mtools `tools/fix-sdcard-image.sh` already needs (`minfo`, `mdir`,
  `mformat`, `mmd`, `mcopy`);
- `sfdisk` (util-linux) for the partition table, `truncate` and `sha256sum`.

It works in this order:

1. **Read the source in place**, read-only, through the mtools `@@offset`
   syntax; the offset comes from the source's own partition table (`sfdisk`).
   It runs on the distribution image directly: mtools reads the
   under-clustered FAT32 that tbblue rejects.
2. **Make the empty card**: a file of a fixed size (about 40 MB) with one
   partition, type 0x0c, at sector 63, the layout of every shipped image
   (`fix-sdcard-image.sh` assumes it). FAT32 with 512-byte clusters, so the
   card has at least 65,525 clusters.
3. **Copy the boot set**, which is one list at the top of the script:
   `TBBLUE.FW`, `MACHINES`, `NEXTZXOS`, `SYS`, `LICENSE.md` and `README.md`,
   plus any empty directory the boot turns out to need (`TMP`). The boot test
   settles the list.
4. **Apply jnext's two patches** by running `tools/fix-sdcard-image.sh` on the
   small card: the `config.ini` defaults and the `zx81.rom` doubling. This is
   the patch set jnext applies to the image it downloads (recipe 2). The
   script reuses that code rather than repeating it, and the card needs no
   re-clustering.
5. **Fail loud** if a listed path is missing from the source, if the card has
   fewer than 65,525 clusters, or if the destination already exists (the
   script never overwrites it).
6. **Print** the size and the sha256 for Phoebus to publish.

**The output is reproducible**: the same source gives a byte-identical card, so
the published hash means something. The volume serial and the label are fixed
(`mformat -N`), and the timestamps are the source's (`mcopy -m`). Whether
mtools also stamps directory entries with the copy time is unverified; S0
checks it, and fixes it if it does.

### 4.5 JavaScript API

A sketch, to be settled in S7. Versioned script `jnext-<version>.js` (+ `.wasm`), one call per canvas, every
option mapping to a CLI flag or a `--set` key (C2), so the man page and
`make cli-check` stay the single source of truth:

```js
const player = await jnext.mount(document.getElementById('game'), {
  file:   { url: 'game.nex' },          // --load (.nex, .tap, .tzx)
  width:  640, height: 512,             // canvas size; letterboxed
  machine: 'next',                      // --machine
  image:  { url: '<SpecNext URL>', sha256: '<published hash>' },
  muted:  false,                        // --set audio...
  set:    { 'startup.cpu_speed': '28' },// any --set key
  args:   [],                           // any other CLI flag
  onProgress: (phase, fraction) => {},  // 'download' | 'warm-start' | 'restore'
  onReady: () => {},
  onError: (message) => {},
});
player.pause(); player.resume(); player.reset();
player.load('other.nex'); player.setVolume(0.5); player.destroy();
```

Built with `MODULARIZE` so several instances can live on one page. Shrek's
`web/CMakeLists.txt` link options (`MODULARIZE`, `EXPORT_NAME`,
`INVOKE_RUN=0`, `ALLOW_MEMORY_GROWTH`, `ENVIRONMENT`) are the starting point.

### 4.6 Size and speed (estimates, measured in S1 and S10)

- **Size**: Shrek's wasm (1.1.55, no SDL code, no libpng, no exceptions) is
  1.70 MB raw, 0.55 MB brotli. With libpng, the SDL3 port and wasm exceptions:
  2.5-4 MB raw, 0.8-1.3 MB brotli.
- **Speed**: native Release runs the heavy workloads at about 7 ms per frame
  (`test/bench/baseline-2d2e86c7e.txt`). At 1.5-2.5x slower in wasm, that is
  10-17 ms of the 20 ms budget on a fast desktop. Shrek reports busy-wait
  programs at ~15 ms on an i7-8700K. Tight but feasible; #452 helps HALT-based
  programs, mostly at 28 MHz.

---

## 5. Shrek's contribution: what we take, and how

His work is real and careful, and some of it is better than `main`. None of it
merges as it is:

- the patch does not apply: 1,907 commits behind; `git apply --check` fails on
  9 of its 52 files (`emulator.cpp/.h`, `z80_cpu.cpp`, `sd_card.cpp`,
  `unit-tests.conf`, ...);
- it was not green on its own base: `compositor_test` UCLIP-02/03/04 fail
  (broken by his clip change), `debugger_backend_test` reports 1456 rows
  against a manifest of 1453, and `docs-check` would fail (generated man page
  and guides not re-rendered);
- it bundles about 12 unrelated features (PR rule: one topic per PR);
- it changes existing test rows (9 esxDOS rows) and deletes test lines;
- Czech comments and Czech user-facing strings (fails `lint-ascii-text`),
  49 attribution comments, 63 em dashes in code;
- a PowerShell build with an unpinned emsdk and a ROM copied from another
  emulator (C1, C3, C6);
- out-of-tree hooks that pull unreviewed code into our build.

Disposition, item by item:

| item | verdict | where it goes |
|---|---|---|
| HALT fast-forward (`halt_batch_size_`, `execute_halted`) | **Reimplement from the idea.** A/B check on 1.1.55: identical on 45 NEX and 3 synthetic programs. But: host NMI strobe not guarded (a mid-frame F9/F10 press is served up to a scanline late); M1 side effects skipped (DivMMC automap promotion); duplicated contention and 28 MHz wait formulas, already stale on `main`; `execute_single_instruction()` ran a whole frame in one call. Desktop gain at 3.5 MHz about zero (0.98-1.08x measured), real at 28 MHz (2.8x on a synthetic loop) | **#452** (filed, v1.3, platform-independent) |
| Copper WAIT skip (`Copper::idle_cycles`) | **Rework**: ports almost directly (`tick_copper_for_master_cycles` is unchanged since his base), plus an equivalence row | **#452** |
| ULA clip window cleared over the border | **He is right, `main` is wrong.** VHDL `zxula.vhd:562` and `:414-415`: the border is never clipped; `main` clears it (`renderer.cpp:643-681`, from a 2026-04 change matched to ZEsarUX). Fix needs owner approval to change UCLIP-02/03/04, which use the border as their witness | #453 |
| NEX loader zeroes all of bank 5 | **Real bug on `main`.** nexload clears only the attributes (`nexload.asm:319-320`); jnext zeroes 16 KB (`nex_loader.cpp:998-1007`), wiping the NextZXOS sysvars at $5B00-$5FFF even on the warm-start path. His fix is web-only; `main` needs the unconditional, nexload-faithful one | #454 |
| Magic breakpoint `FD 00` (sjasmplus `break` since CSpect 2.19.9.1) | **Rework**: `main` knows only `ED FF` and `DD 01` (`z80_cpu.cpp:1037,1202`); detection must use the fetched prefix chain; man page | #456 |
| Writes to a read-only SD card rejected | **Rework**: right fix, but SD-RO-03 passes with or without it on libstdc++, so it can only be proven on macOS CI | #455 |
| Extra esxDOS host calls (unlink, rename, mkdir, rmdir, truncate, chmod, M_GETERR) | **Reimplement as a feature** (design doc, use case); oracle is NextZXOS 2.09, his probe NEX must come as a fixture; may change 9 existing rows (esx_epath to esx_enotdir): approved only if the probe proves it | #457 |
| Per-scanline "live raster" rendering (IK+ menu flicker) | **Reject**: superseded by `main`'s beam-time replays (ULA, tilemap, LoRes, Layer 2 `SramBeamLog`), which are closer to the VHDL; his version writes device state from the render path | ask Shrek for the IK+ build; a bug if it still flickers on `main` |
| NR 0x4A = 0, MAXBNK = 111 on direct load | **Reject**: invented values (VHDL reset of NR 0x4A is 0xE3, `zxnext.vhd:5014`); D3 removes the need | none |
| `load_nex_in_place`, `esxdos_native_only`, mouse read counter | **Reject** (Android launcher and front-end plumbing) | none |
| `ram.cpp` `#include <algorithm>` | **The same one-line fix** | S1 |
| `sdcard_net_stub.cpp` (SHA-256 + no download) | **Reimplement from the idea**: a portable FIPS 180-4 SHA-256 and "download unavailable", passing the existing rows | S1 |
| `web/CMakeLists.txt` | **Rework** (keep the link options; guards in our CMake) | S1, S5 |
| `web_compat.h` | **Reject**, fix the cause (rename `PAGE_SIZE`) | S1 |
| `jnext_web.cpp` (exported C API, A/B seams) | **Reimplement from the idea** (it bypasses `Rom`/MMU to inject the ROM, reimplements reset and pacing, global raw `new`) | S5/S7 (or D7's fallback) |
| `sd_card_web.cpp` (sparse card, IndexedDB persistence, batched warm-start recording with progress) | **Reimplement from the idea**; building the card from the distro ZIP in the browser is superseded by the SpecNext image | S6, S11 |
| `SdBackingFile`, `ffconf.h` per-platform | **Reject the form**; the seam is reconsidered in S6 (D2) | S6 |
| `build.ps1`, `JNEXT_WEB_DIR` / `JNEXT_ANDROID_DIR` / `JNEXT_SERVER_DIR` hooks | **Reject** (make targets, in-tree code) | S1 |

Asks for Shrek (D12, by email): the IK+ program that flickered; the esxDOS
probe NEX and its NextZXOS 2.09 results (the oracle for #457); his browser
bench numbers per program, so S10 can compare.

---

## 6. Testing and CI

The browser build is tested like Windows is: the same suites, run on the
target.

1. **`make unit-test-wasm`**: the SDL-only unit suites built for node
   (`-sENVIRONMENT=node -sNODERAWFS=1`) and run through a `test/node-run.sh`
   runner, exactly as `test/wine-run.sh` runs `make unit-test-win`. A new
   `JNEXT_TARGET_OS` value (`CMakeLists.txt:168`) and `# os:` lines in
   `test/unit-tests.conf` declare the suites that cannot exist there
   (sockets, threads, processes, symlinks): absent by name, never skipped.
2. **`make regression-wasm`**: the headless `jnext` built for node, reading the
   real cached SD image through NODERAWFS, running the screenshot rows against
   the **same reference PNGs** as native. This proves the wasm core is
   pixel-identical to the desktop one.
3. **Browser smoke test** (D8): Playwright + headless Chromium loads the
   example page with a licence-clean test NEX from `test/00regression/nex`,
   clicks, waits for the first frames and compares the canvas with a
   reference. It also checks the JS API events.
4. **The native gates** stay green at every stage, including
   `make unit-test-sdl` and `make unit-test-win`, since S1 and S4 touch shared
   code.

CI: a pinned emsdk provisioned by `test/provision-emsdk.sh` into
`~/.cache/jnext/tools` (the `z88dk-gdb` pattern), new jobs in the `fedora:44`
container running exactly the make targets above, blocking like the others.
The new targets join the batch gate (CLAUDE.md merge protocol step 2) once
they exist.

---

## 7. Stages (future sub-issues of #14)

One branch per sub-issue off the epic branch `epic14-webassembly`, merged back
into it; the epic reaches `main` in one merge, after its CI run (merge
protocol step 1).

| # | sub-issue | deliverable | acceptance |
|---|---|---|---|
| S0 | Minimal image and hosting | `tools/make-minimal-sdcard-image.sh` (§4.4) and a shell suite in `make unit-test`. Its rows: the cluster count is at least 65,525; the card holds exactly the listed set, each file byte-identical to the source except the two patched ones; two runs give the same sha256; a missing path and an existing destination are refused. A regression row boots the minimal card (built from the cached distribution image) to the NextZXOS menu and runs a test NEX. Developer guide: one paragraph. The script and a tested image go to Phoebus with D1 (URL, CORS, sha256, size) | rows green; the boot screenshot matches the existing NextZXOS menu reference; once the image is hosted, a fetch from another origin succeeds and the hash matches |
| S1 | Toolchain and portable core build | `test/provision-emsdk.sh` (pinned); `EMSCRIPTEN` arms in CMake (§4.1); `sdcard_provisioner_net_web.cpp`; `PAGE_SIZE` renames; `<algorithm>` include; `-fwasm-exceptions`; `make wasm-core` | builds from clean in `fedora:44`; SHA256-01* rows pass; native `unit-test`, `unit-test-sdl`, `unit-test-win` green; wasm size recorded |
| S2 | `make unit-test-wasm` | `test/node-run.sh`, `JNEXT_TARGET_OS` value, `# os:` lines, CI job | every declared suite passes under node, 0 SKIP, absent suites named; CI job green |
| S3 | `make regression-wasm` | headless jnext for node | the screenshot rows match the existing references byte for byte, 0 FAIL, 0 SKIP |
| S4 | SDL frontend loop as `tick()` | refactor, native behaviour unchanged | full native batch gate green, `host_key_latch_test` green |
| S5 | Browser frontend | SDL3 Emscripten port: canvas, WebGL, Web Audio after a click, keyboard focus and blur release, gamepads, pointer lock; a spike first to confirm D7 | smoke test boots a test NEX and a test TAP; canvas matches its reference; keys and sound work after a click |
| S6 | System image in the browser | fetch + sha256 + Cache API; image handed to the FS before `callMain`; warm start recorded once with progress and restored from IndexedDB; block-device seam only if D2 needs it | first visit records with progress; second visit restores (time recorded); new rows for any new SD code |
| S7 | JavaScript API | `jnext.mount()` and the controller (§4.5), options mapped to CLI and `--set`; example page (plain and itch.io-style iframe); user guide chapter and man page section | the examples work; `cli-check` and `docs-check` green |
| S8 | Packaging and release | `make package-web`: a zip with `jnext-<version>.js` + `.wasm`, an example page, LICENSE and a source link; a `release.yml` job attaching it to public releases (D6) | names from `version.yaml`; `sync-version-test` green; the asset is on a public release |
| S9 | Documentation | user guide: embedding JNEXT in a page; developer guide: the browser build and its tests | `docs-check` green |
| S10 | Performance | browser bench page; numbers per program at 3.5 and 28 MHz, unthrottled and at Chrome's 2x CPU throttle; profiling and optimisation until D9 holds (#452 lands separately) | 50 fps on the whole regression NEX set at both speeds, in Chrome and Firefox, and in Chrome at 2x throttle |
| S11 | Follow-ons, each its own sub-issue if wanted | guest writes kept across reloads; snapshots and the other desktop formats; touch keyboard; WebGL CRT shader | per D4, D5, D10, D11 |

S0 runs in parallel with S1-S4 and blocks S6. S1-S3 prove the core before any
browser work; S4 is native-only and can land early. #452 is independent.

---

## 8. Risks

| risk | effect | mitigation |
|---|---|---|
| SpecNext serves no CORS header, or no stable URL | no page on another origin can fetch the image | raise it now (D1); fallback: the visitor picks the file by hand |
| The minimal image is not small | memory and download time; a block-device seam needed | the recipe (D2); the seam in S6 only if needed |
| SDL3's Emscripten port is immature | S5 slips | spike first; fallback to a thin C API (D7) |
| Speed: D9's floor at 2x throttle is borderline | below 50 fps on the heaviest programs | measure early (S1, S3); #452; profiling in S10 until it holds |
| A reached `throw` or `std::thread` aborts the page | crash | wasm exceptions; features with threads compiled out and refused at the CLI |
| Storage partitioning: each embedding site caches its own copy of the image | the first visit on every site downloads it | accepted; documented for authors |
| Browser-reserved keys, focus loss | keys not delivered, or stuck | release on blur; document the reserved keys |
| A third cross-platform gate | CI time and upkeep | pinned toolchain, the same suites as native |
| Licence | JNEXT must never serve NextZXOS, its ROMs or the warm-start state | everything comes from SpecNext or is recorded in the visitor's browser; pages that self-host our JS/wasm carry LICENSE and a source link (GPL) |

---

## 9. Related issues

- **#452** perf: skip idle HALT repeats and unmet Copper WAITs in one step,
  bit-identical (v1.3; from Shrek's HALT and Copper work, platform-independent).
- Filed 2026-11-06 from the review of Shrek's patch (all v1.3, independent of
  this epic):
  - **#453** bug: the ULA clip window is applied to the border (the VHDL never
    clips it); owner approved changing UCLIP-02/03/04.
  - **#454** bug: loading a NEX zeroes all of bank 5 and wipes the NextZXOS
    system variables (nexload clears only the attributes).
  - **#455** bug: writes to a read-only SD card reach the backing stream.
  - **#456** feature: the `FD 00` magic breakpoint (sjasmplus `break`).
  - **#457** feature: esxDOS calls that change files on the host folder;
    owner approved changing 9 rows only if a kept NextZXOS probe proves it.
