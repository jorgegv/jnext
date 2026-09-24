# SDL2 → SDL3 Migration — Design Note

> **Status: IMPLEMENTED (2026-09-24). GH [#57](https://github.com/jorgegv/jnext/issues/57).**
> The note below is the design as written *before* the work; it is kept as
> written so the analysis and the reasoning stay readable. **What actually
> changed, and where this note turned out to be wrong, is recorded in §9 at the
> end — read that alongside any section here.** In particular §5.5's blocker
> and §8's Q1 are RESOLVED, not open: the owner withdrew the deferral the same
> day, on the grounds that this project already builds a dependency from source
> (the Flatpak SDL2 module) and already bundles libraries for two platforms.
>
> The original SDL2 choice is recorded at `doc/design/EMULATOR-DESIGN-PLAN.md:97`
> ("SDL2 (SDL3 migration later)"). That document is a frozen historical artifact
> and is NOT edited by this work.

The motivation is settled in the issue and is not re-argued here: every supported
platform resolves `SDL2` to **sdl2-compat**, so jnext already runs on SDL3 through
a shim — paying its costs, getting none of its API. This note establishes what the
migration actually touches, what it proves, and what it cannot decide without the
owner.

---

## Decision summary (read this first)

| # | Question | Blocking? |
|---|----------|-----------|
| **Q1** | **Ubuntu 24.04 LTS has no `libsdl3-dev`. What happens to its `.deb`?** | **YES — hard blocker** |
| Q2 | Single cut vs staged migration | No — recommendation given |
| Q3 | Windows SDL-only `SDL2main` → SDL3 header-only `SDL_main.h` | No — verifiable by cross-build |

Everything else in this note is settled by evidence gathered in this tree, in the
installed SDL3 3.4.16 headers, or by measurement. Full detail in §8.

---

## 1. Complete inventory of jnext's SDL surface

### 1.1 The "core is SDL-free" claim — verified, and it is NOT true as stated

`doc/design/EMULATOR-DESIGN-PLAN.md` §3 states *"The emulator **core has no SDL
dependency** — pure C++ with no platform headers"* and *"SDL lives exclusively in
`src/platform/`"*. Checked rather than assumed:

```
git grep -n 'SDL' -- src/core src/audio src/video src/cpu src/memory \
                     src/port src/peripheral src/debug src/debugger src/esp01
```

Every hit in those trees is a **comment**. There is not one SDL *call* in the
emulator core. That half of the claim holds.

The second half does not. SDL is a **type** dependency of the core:

- `src/input/keyboard.h:4` — `#include <SDL2/SDL.h>`
- `src/input/keyboard.h:39` — `void set_key(SDL_Scancode sc, bool pressed);`
- `src/core/emulator.h` includes `input/keyboard.h`

so `<SDL2/SDL.h>` is a transitive include of `emulator.h`. The consequence is
already documented in-tree, at `src/debug/CMakeLists.txt:8-12`:

> `rewind_buffer.cpp` includes `core/emulator.h` → `input/keyboard.h` →
> `<SDL2/SDL.h>`, so this lib needs SDL2's include directories.

and `src/debug/CMakeLists.txt:12` links `jnext_debug` against `SDL2::SDL2` purely
to get those headers. **`src/input/` and `src/debug/` are the two places the rule
is violated**, and both must be migrated even though neither calls SDL.

This is a finding, not a blocker: `SDL_Scancode` is an `int`-like enum and the
migration is a rename (`<SDL3/SDL.h>`, `SDL3::SDL3`). Decoupling `Keyboard` from
SDL entirely is a **separate**, larger refactor and is explicitly **out of scope**
here — mentioning it, not doing it.

### 1.2 Per-subsystem call sites

Distinct SDL identifiers in `src` (excluding `src/doc`): **197 distinct symbols,
527 occurrences**. By owner:

| Subsystem | Files | Role | Migration weight |
|---|---|---|---|
| `src/platform/sdl_audio.{h,cpp}` | 2 | Audio device + callback | **High** — §2 |
| `src/platform/sdl_display.{h,cpp}` | 2 | Window / renderer / texture | Medium — §3.4 |
| `src/platform/sdl_input.{h,cpp}` | 2 | Event pump | Medium — §3.2 |
| `src/platform/sdl_app.{h,cpp}` | 2 | SDL-only frontend loop | Low |
| `src/input/keyboard.{h,cpp}` | 2 | `SDL_Scancode` matrix map (81 refs) | Low — pure rename |
| `src/input/gamepad_host.{h,cpp}` | 2 | Joystick/gamepad lifecycle | **High** — §3.3 |
| `src/input/joystick_dispatcher.{h,cpp}` | 2 | Button/axis/hat → Next bits | Medium — constant renames |
| `src/input/mouse_dispatcher.{h,cpp}` | 2 | Kempston mouse | Low |
| `src/gui/qt_app.{h,cpp}` | 2 | SDL init + event drain under Qt | Low |
| `src/gui/main_window.{h,cpp}` | 2 | Qt→SDL key/button translation (93 scancodes) | Low — pure rename |
| `src/debug/CMakeLists.txt` | 1 | Transitive header dep (§1.1) | Trivial |

Tests carrying SDL symbols: `test/input/input_test.cpp` (175),
`test/input/input_integration_test.cpp` (46), `test/gui/shifted_keys_test.cpp` (24),
`test/gui/host_hotkey_test.cpp` (16), `test/gui/esc_break_test.cpp` (7),
`test/platform/host_key_latch_test.cpp` (4), `test/port/port_test.cpp` (4).
These are constant/enum renames, not logic changes.

### 1.3 What SDL is actually FOR, per frontend

The brief asks this, and the answer is narrower than the file count suggests.

**Qt GUI build (`ENABLE_QT_UI=ON`, the shipped default)** — Qt owns the window,
the framebuffer widget, the event loop and the keyboard. SDL's *entire* remaining
job is:

1. **Audio output** — `SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)` at
   `src/gui/qt_app.cpp:203`, and the whole of `sdl_audio.cpp`.
2. **Gamepads** — `qt_app.cpp:489-490` drains `SDL_PollEvent` once per frame tick
   and feeds only controller events to `GamepadHost`; everything else is dropped.
3. **A scancode vocabulary** — `main_window.cpp:54 qt_key_to_sdl()` converts Qt
   keys into `SDL_Scancode` because `Keyboard::set_key` speaks that type (§1.1).
   No SDL code runs for this; it is a shared enum, nothing more.

So in the shipped binary SDL is **an audio backend and a joystick backend**. No
window, no renderer, no texture, no SDL keyboard.

**SDL-only build (`ENABLE_QT_UI=OFF`, GH #108)** — additionally owns the window,
renderer, texture, fullscreen, event loop and keyboard (`sdl_display.cpp`,
`sdl_input.cpp`, `sdl_app.cpp`). This is a repo-internal validation leg plus the
Windows-7-floor variant; it is where the `SDL_CreateWindow`/`SDL_CreateRenderer`
deltas of §3.4 bite, and nowhere else.

---

## 2. Audio — the high-risk part, and it is smaller than the issue assumed

### 2.1 jnext does not use `SDL_AudioStream`

The issue's headline risk is *"`SDL_AudioStream` semantics changed in SDL3"*.
**jnext does not use `SDL_AudioStream` at all.** The single occurrence of that
identifier in `src` is a comment at `src/platform/sdl_audio.cpp:24` explaining why
one is *not* needed.

jnext uses the **SDL2 device-callback** model, adopted deliberately for GH #208
(`src/platform/sdl_audio.h:9-32`). The complete SDL audio surface is seven calls:

| Call | Site | SDL3 replacement |
|---|---|---|
| `SDL_OpenAudioDevice(nullptr,0,&want,&have,0)` | `sdl_audio.cpp:36` | `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, get_cb, this)` |
| `want.callback` (device callback) | `sdl_audio.cpp:32` | `SDL_AudioStreamCallback` (get-callback) |
| `SDL_PauseAudioDevice(dev, 0)` | `sdl_audio.cpp:45` | `SDL_ResumeAudioStreamDevice(stream)` |
| `SDL_LockAudioDevice` | `:78, :96, :106` | `SDL_LockAudioStream` |
| `SDL_UnlockAudioDevice` | `:80, :98, :108` | `SDL_UnlockAudioStream` |
| `SDL_CloseAudioDevice` | `:188` | `SDL_DestroyAudioStream` (also closes the device) |
| `SDL_GetCurrentAudioDriver` | `:52` | unchanged |

Everything that makes jnext's audio *correct* — the ring, the hold policy, the
fill statistics, the pacing band — lives in `src/platform/audio_fill.h` and
`src/platform/audio_pacing.h`, which are **pure C++ with no SDL** (their unit
suites `audio_fill_test` and `audio_pacing_test` link neither SDL nor the core).
The migration does not touch one line of policy.

### 2.2 The model maps exactly, and the locking maps 1:1

The SDL2 device callback writes into a raw buffer it must fill completely. The
SDL3 get-callback is told how many bytes are wanted and *puts* them into the
stream. `/usr/include/SDL3/SDL_audio.h:1891-1895` settles the units:

> The callback's `additional_amount` argument is roughly how many bytes of
> _unconverted_ data (in the stream's input format) is needed by the caller

Our input format is S16 stereo, so `pairs = additional_amount / 4`. The threading
contract is the important part — `SDL_audio.h:1868-1871`:

> This callback may run from any thread […] you should use `SDL_LockAudioStream`
> to serialize access; **this lock will be held before your callback is called**,
> so your callback does not need to manage the lock explicitly.

That is *precisely* the SDL2 `SDL_LockAudioDevice` contract that
`sdl_audio.h:28-32` documents. **The threading comment carries over verbatim with
one word changed.** The producer keeps locking; the callback keeps not locking.

`SDL_OpenAudioDeviceStream` is the right entry point rather than
device+stream+bind, and its own doc says so (`SDL_audio.h:2005`: *"This is also
intended to be a clean means to migrate apps from SDL2"*). It also **begins
paused** (`:2009-2011`), matching the existing `SDL_PauseAudioDevice(dev,0)` step.

### 2.3 What could regress — and the measurement, not the assertion

Three real hazards, all measured rather than argued. A green test suite proves
nothing about audio here (`feedback_green_tests_are_not_verification`), so the
experiment drives **jnext's real `audio_fill.h`** under both models with an
identical producer and a deliberate 300 ms starvation, capturing through the disk
driver. Harness: `ab.cpp`, built twice (`-lSDL2` = today's sdl2-compat stack,
`-DUSE_SDL3` = the proposal). Producer emits a never-zero ramp (1000..1199) so any
zero in the output is SDL-injected by construction.

```
SDL2 (sdl2-compat, TODAY)
  calls=100  req_bytes=409600  real_pairs=88028  fill_pairs=14372  fill_events=12
  frames=102400  INTERIOR zeros = 0   longest constant run = 12573 frames (285.1 ms)
  min=1000  max=1199

SDL3 (PROPOSED)
  calls=100  req_bytes=409600  real_pairs=88170  fill_pairs=14230  fill_events=9
  frames=102400  INTERIOR zeros = 0   longest constant run = 12715 frames (288.3 ms)
  min=1000  max=1199   max_stream_queued = 4096 bytes
```

**Hazard 1 — different request chunking would move the hold boundaries.**
Refuted: both models made **100 callbacks totalling exactly 409600 bytes**, in
4096-byte (1024-frame) chunks. Identical cadence.

**Hazard 2 — SDL3 delivering silence on shortfall (the #7/#208 click-train).**
Refuted: **zero interior zeros in both**, and `min=1000 / max=1199` in both — the
device never saw a sample outside the producer's own range. The 300 ms starvation
produced a DC hold of 285.1 ms (SDL2) vs 288.3 ms (SDL3): the same plateau, and
the ~3 ms / 3-event spread is run-to-run wall-clock jitter between two separate
real-time captures, not a model difference. **The GH #208 guarantee holds
identically under SDL3.**

**Hazard 3 — the pacing band silently shifting.** This is the subtle one.
`queued_ms()` (`sdl_audio.cpp:84`) reads *our ring*, and `audio_pacing.h`'s band
constants are calibrated against that reading. Under SDL3 there is now a second
buffer — the stream — between our ring and the device, which under SDL2 did not
exist as an app-visible object. If the stream pre-buffered ahead, `queued_ms()`
would under-report and the pacer would over-produce.

Measured: `max_stream_queued = 4096 bytes` — **exactly one device chunk**, never
more, because the get-callback is invoked with only what is needed to satisfy the
immediate request. That is the precise analogue of SDL2's internal device buffer
(`want.samples = 1024`, `sdl_audio.cpp:31`), which `queued_ms()` never counted
either. **Conclusion: `queued_ms()` must stay ring-only and the
`audio_pacing` constants carry over unchanged.** Adding `SDL_GetAudioStreamQueued()`
to the reading would *double-count* the device buffer and shift the band — a
plausible-looking "improvement" that would be a regression. This is recorded here
so nobody adds it later.

### 2.4 How equivalence would be proved on the real emulator

The harness above isolates the policy. On the full binary, the project's own
technique applies (`technique_audio_capture_and_spectral_compare`): capture
jnext's real output through the disk driver before and after the change, same
programme, same machine, and compare — interior-zero count (the #208
discriminator), band shares, and spectral shape. `check-audio-underruns.py` is the
codified form and already runs as `audio-underrun-func`. **Note the env-var trap
in §6.2 first** — run naively, that test would go SKIP rather than FAIL.

---

## 3. API deltas that actually affect this code

Not a generic SDL3 changelog — only what this tree calls. Verified against the
installed SDL3 3.4.16 headers.

### 3.1 Init

| SDL2 | SDL3 | Note |
|---|---|---|
| `SDL_Init(...) < 0` is failure | `bool SDL_Init(SDL_InitFlags)` — `false` is failure | `SDL_init.h:238`. **Inverted sense** — `qt_app.cpp:203` and `sdl_app.cpp` must both flip, and a missed flip fails *closed* (reports failure on success), so it is loud. |
| `SDL_INIT_GAMECONTROLLER` | `SDL_INIT_GAMEPAD` | `SDL_init.h:84` |

### 3.2 Events and keyboard

| SDL2 | SDL3 |
|---|---|
| `SDL_QUIT`, `SDL_KEYDOWN/UP` | `SDL_EVENT_QUIT`, `SDL_EVENT_KEY_DOWN/UP` |
| `SDL_MOUSEMOTION`, `SDL_MOUSEBUTTONDOWN/UP`, `SDL_MOUSEWHEEL` | `SDL_EVENT_MOUSE_MOTION`, `SDL_EVENT_MOUSE_BUTTON_DOWN/UP`, `SDL_EVENT_MOUSE_WHEEL` |
| `SDL_CONTROLLER*` | `SDL_EVENT_GAMEPAD_*` |
| `SDL_JOY*` | `SDL_EVENT_JOYSTICK_*` |
| `e.key.keysym.scancode` | `e.key.scancode` — the `keysym` struct is **gone** |
| `const Uint8* SDL_GetKeyboardState` | `const bool* SDL_GetKeyboardState` (`SDL_keyboard.h:159`) |
| `SDL_NUM_SCANCODES` | `SDL_SCANCODE_COUNT` (`SDL_scancode.h:425`, still 512) |
| `Uint32 SDL_GetTicks()` | `Uint64 SDL_GetTicks()` (`SDL_timer.h:201`) |

Sites: `sdl_input.cpp:3-56` (the whole switch), `keyboard.cpp:18,22,28,36`
(four `SDL_NUM_SCANCODES` array bounds), `sdl_app.h:156` and
`main_window.cpp` (`uint32_t last_present_ms_` vs `Uint64` ticks — widen or cast
deliberately; a silent truncation still works for deltas but is sloppy).

The `SDL_SCANCODE_*` names themselves are **unchanged** — all 93 in
`main_window.cpp` and 81 in `keyboard.cpp` migrate by include path alone. Same for
`SDL_BUTTON_LEFT/MIDDLE/RIGHT` and the `SDL_HAT_*` masks.

### 3.3 Joystick / gamepad — the one genuine logic change

SDL2's device-index/instance-id duality is **gone**; SDL3 is instance-id
throughout. Verified: `SDL_joystick.h:230,248,395`, `SDL_gamepad.h:555,753`,
and `SDL_events.h:596-602` — `SDL_JoyDeviceEvent::which` is documented as
*"The joystick instance id"*, where SDL2 delivered a **device index** on ADDED.

| SDL2 | SDL3 |
|---|---|
| `SDL_NumJoysticks()` + index loop | `SDL_JoystickID* SDL_GetJoysticks(int*)` |
| `SDL_JoystickNameForIndex(idx)` | `SDL_GetJoystickNameForID(iid)` |
| `SDL_JoystickGetDeviceInstanceID(idx)` | *(gone — you already have the id)* |
| `SDL_IsGameController(idx)` | `SDL_IsGamepad(iid)` |
| `SDL_GameControllerOpen(idx)` / `SDL_JoystickOpen(idx)` | `SDL_OpenGamepad(iid)` / `SDL_OpenJoystick(iid)` |
| `SDL_JoystickInstanceID(js)` | `SDL_GetJoystickID(js)` |
| `SDL_GameControllerGetJoystick` | `SDL_GetGamepadJoystick` |
| `SDL_CONTROLLER_BUTTON_A/B/X/Y` | `SDL_GAMEPAD_BUTTON_SOUTH/EAST/WEST/NORTH` |

Affected: `gamepad_host.cpp:50-116` (`open_device(int dev_idx)` becomes
`open_device(SDL_JoystickID)`; `enumerate_existing_devices()` becomes a
`SDL_GetJoysticks` walk; `gamepad_host.cpp:128-130`'s *"e.jdevice.which is the
device-index here (NOT the instance-id)"* comment inverts) and
`joystick_dispatcher.cpp:60-87` (button constant renames).

**A sentinel hazard to get right.** `SDL_JoystickID` is `Uint32` in SDL3
(`SDL_joystick.h:106`) with **0 = invalid**; SDL2's was signed with `-1`.
`gamepad_host.cpp:58-59` tests `if (iid >= 0)` — always true for a `Uint32` — and
`joystick_dispatcher.h:334` stores `int32_t instance_id = -1` as "not mapped".
Both must move to `0`. This is exactly the class of truthy-sentinel defect that
`technique_force_platform_gated_cmake_branch` caught in #46; it is silent if
missed, so it needs a test row that asserts an unmapped slot rejects instance 0.

The **behavioural** upside: the SDL2 dedupe dance in `gamepad_host.cpp:55-67`
(open a device, then ask what instance id it *would* have, to detect a duplicate
ADDED) disappears — the event already carries the id.

### 3.4 Video / window — SDL-only frontend only

| SDL2 | SDL3 | Site |
|---|---|---|
| `SDL_CreateWindow(title,x,y,w,h,flags)` | `SDL_CreateWindow(title,w,h,flags)` + `SDL_SetWindowPosition` (`SDL_video.h:1184`) | `sdl_display.cpp:12-15` |
| `SDL_WINDOW_SHOWN` | *(gone — shown by default)* | `sdl_display.cpp:15` |
| `SDL_CreateRenderer(win,-1,ACCELERATED\|PRESENTVSYNC)` | `SDL_CreateRenderer(win,nullptr)` (`SDL_render.h:273`) **+ `SDL_SetRenderVSync(r,1)`** (`:2784`) | `sdl_display.cpp:21-22` |
| `SDL_RenderSetLogicalSize` | `SDL_SetRenderLogicalPresentation(r,w,h,mode)` (`:1576`) | `sdl_display.cpp:31` |
| `SDL_RenderCopy` | `SDL_RenderTexture` (`:2278`, takes `SDL_FRect`) | `sdl_display.cpp:47` |
| `SDL_SetWindowFullscreen(w, FULLSCREEN_DESKTOP\|0)` | `SDL_SetWindowFullscreen(w, bool)` (`SDL_video.h:2360`) | `sdl_display.cpp:56-57` |
| `SDL_SetRelativeMouseMode(bool)` | `SDL_SetWindowRelativeMouseMode(win, bool)` (`SDL_mouse.h:476`) | `sdl_app.cpp` |

**Vsync needs deliberate care, not a mechanical swap.** Dropping
`SDL_RENDERER_PRESENTVSYNC` into `SDL_SetRenderVSync(r,1)` is the literal
translation, but `sdl_app.h:69-71` documents a live interaction: above 100% speed
the frontend presents at most every `RENDER_INTERVAL_MS` precisely *because* "a
vsynced present cannot cap the speed at the display's refresh". The migration must
keep vsync ON (same default) and re-verify `sdl-render-func` and the `--speed`
rows; it must not quietly become vsync-off, which would change frame pacing on the
SDL-only leg.

`SDL_SetRenderLogicalPresentation` needs a mode argument with no SDL2 analogue.
`SDL_LOGICAL_PRESENTATION_LETTERBOX` reproduces `SDL_RenderSetLogicalSize`'s
documented behaviour, and letterboxing is exactly what `sdl_display.h:28-30`
says the call is for. This is a judgement, so it gets a screenshot-regression
check rather than an assertion.

---

## 4. Build system

`find_package(SDL2 REQUIRED)` appears four times — `CMakeLists.txt:150`,
`src/platform/CMakeLists.txt:7`, `src/input/CMakeLists.txt:9`,
`src/gui/CMakeLists.txt:4` — and `SDL2::SDL2` is linked in five places
(`CMakeLists.txt:395/404`, `src/platform:18`, `src/input:10`, `src/gui:21`,
`src/debug:12`).

Fedora 44 ships the CMake package as **`SDL3::SDL3-shared`** plus
`SDL3::Headers` (`/usr/lib64/cmake/SDL3/SDL3sharedTargets.cmake:68`,
`SDL3headersTargets.cmake:68`) and `sdl3.pc`. Upstream also defines an
`SDL3::SDL3` alias; the guard already used for static builds
(`CMakeLists.txt:391-396`, `if(STATIC_BUILD AND TARGET SDL2::SDL2-static)`)
generalises to pick whichever target exists, and that pattern is already in the
tree so it is not a new idiom.

**Configurations that must each be built and verified:**

| Config | Target | Why it is distinct |
|---|---|---|
| Default dev | `build/` | RelWithDebInfo, Qt UI + debugger |
| `make gui-release` | `build/gui-release` | the binary regression runs against |
| `make sdl-release` | `build/sdl-release` | `ENABLE_QT_UI=OFF` — the only leg using window/renderer/events |
| `make qt5-guard-build` | — | Qt5 dual-maintenance guard, CI job `qt5-guard` (`ci.yml:295-325`) |
| `STATIC_BUILD=ON` | — | needs `SDL3::SDL3-static`; `SDL3-static` is packaged on Fedora 44 |
| `make package-win` / `package-win-sdl` / `package-win32-*` | MinGW x64 + i686 | §5.4 |

**`SDL2main` is the one real build-system unknown.** `CMakeLists.txt:432-440`
links `SDL2::SDL2main` for the **Windows SDL-only** build, because `SDL.h` renames
`main()` to `SDL_main()` there and `SDL2main`'s `WinMain` bridges it. SDL3 has
**no `libSDL3main`** — `SDL_main.h` is a single-header library
(`/usr/include/SDL3/SDL_main.h:44`) and there is no `libSDL3main.*` on disk. The
SDL3 form is to `#include <SDL3/SDL_main.h>` in `main.cpp`, or to define
`SDL_MAIN_HANDLED` (`SDL_main.h:77`) and keep a plain `main`. Both are defensible;
the interaction with `CMakeLists.txt:462`'s existing note about Qt's
`main=qMain` define winning the ordering makes this worth cross-building and
inspecting rather than reasoning about. See §8 Q3.

---

## 5. Packaging — two special cases delete, one gets simpler, one blocks

### 5.1 Fedora RPM — trivial

`packaging/rpm/jnext.spec:14`: `BuildRequires: SDL2-devel` → `SDL3-devel`.
Verified available: `SDL3-devel-3.4.16-1.fc44` (and `SDL3-static` for the static
leg). Runtime dependency is autodetected by rpmbuild.

### 5.2 Flatpak — the SDL2 module **deletes entirely**, better than expected

`packaging/flatpak/io.github.zxjogv.jnext.yml:21-36` builds SDL2 from source as
its own module with a pinned tarball + sha256, because *"SDL2 is not part of the
KDE runtime"* (`:22`).

That is no longer true for SDL3. Checked the installed runtimes directly:

- `org.kde.Platform//6.10` ships `libSDL3.so.0` (3.2.30) — runtime satisfied.
- `org.kde.Sdk//6.10` ships `libSDL3.so`, `include/SDL3/`,
  `lib/x86_64-linux-gnu/cmake/SDL3/` and `sdl3.pc` — build satisfied.

So the `sdl2` module (and its pinned URL + sha256, a recurring maintenance item)
is **deleted, not replaced**. No from-source build, nothing to re-pin.

`:22-24`'s comment about tracking the dev host's `find_package` version goes with
it. **The `jnext` module's deliberate absence of `builddir: true` stays untouched**
— `CMakeLists.txt:230-232` records that it is the project's only end-to-end
witness of an in-source build. Note that the deleted `sdl2` module is where
`builddir: true` *was* needed (`:27-29`), so removing it also removes the
contrast the comment at `CMakeLists.txt:232` draws ("unlike the sdl2 module above
it"); that comment needs a one-line update, not a behaviour change.

### 5.3 macOS — `bundle-dlopen-deps.sh` deletes entirely

`packaging/macos/bundle-dlopen-deps.sh` exists for exactly one reason, stated in
its own header (`:12-24`): Homebrew's `sdl2` is sdl2-compat, which `dlopen`s
libSDL3 by leaf name, so `otool -L` shows nothing, macdeployqt copies nothing, and
the shipped `.app` aborted at launch (GH #46). Linking SDL3 directly puts
`libSDL3.dylib` in an `LC_LOAD_DYLIB` entry, where `macdeployqt` and
`complete-closure.sh` find it like any other library.

**The whole script goes**, along with its invocation at `CMakeLists.txt:604-611`
and CI's `brew install … sdl2` → `sdl3` (`ci.yml:450`, `release.yml:355`). The
in-tree instruction `brew install sdl3` at `bundle-dlopen-deps.sh:91` is
itself the evidence the formula exists.

`verify-bundle.sh` is **unaffected and still valuable** — it walks every Mach-O
for unresolved references, and now simply sees SDL3 as a normal linked dep.

### 5.4 Windows MinGW — the special case deletes

`packaging/windows/bundle-dlls.sh:163-178` detects the sdl2-compat shim by
grepping `SDL2.dll` for the string `SDL3.dll` and force-queues `sdl3.dll` because
`LoadLibrary` leaves no PE import entry. With a direct SDL3 link, `SDL3.dll` is a
real import and `resolve_queue` finds it. **Lines 163-178 delete.**

Toolchain verified present: `mingw64-SDL3-3.4.16-1.fc44` and
`mingw32-SDL3-3.4.16-1.fc44` (plus `-static` variants) — so the x64 and i686 legs
and `package-win32-*` are all covered. `ci.yml:388,392` and `release.yml:214,218`
swap `mingw{64,32}-sdl2-compat` → `mingw{64,32}-SDL3`.

### 5.5 Debian / Ubuntu — **THE BLOCKER**

`packaging/debian/control:9` build-depends on `libsdl2-dev`. The replacement is
`libsdl3-dev`. But `release.yml:92-96` builds the `.deb` in a **pinned matrix of
`ubuntu:24.04` and `ubuntu:26.04` containers**, for the documented reason that
`dpkg-shlibdeps` bakes in the host distro's library package names
(`release.yml:78-88`).

Queried Launchpad for `libsdl3` per series:

| Ubuntu series | `libsdl3` |
|---|---|
| 24.04 noble (**LTS, built by CI**) | **NONE** |
| 24.10 oracular | **NONE** |
| 25.04 plucky | 3.2.8 |
| 25.10 questing | 3.2.20 |
| 26.04 resolute (**LTS, built by CI**) | 3.4.2 |
| 26.10 stonking | 3.4.14 |

**SDL3 does not exist in Ubuntu 24.04 LTS at all.** The 24.04 `.deb` leg cannot be
migrated; it can only be dropped, or kept on a different footing. This is a
user-visible support-policy decision and is §8 Q1.

Debian proper is fine — `libsdl3` is in the archive.

---

## 6. CI

`.github/workflows/ci.yml` runs in `container: fedora:44` and every `run:` is a
plain make target. Nothing in this migration needs a CI-only step; the changes are
dependency names in the `dnf install` lines and one env-var rename in a test
script (which is a *local* fix that CI inherits, exactly as the rule requires).

### 6.1 Dependency swaps

| File:line | From | To |
|---|---|---|
| `ci.yml:136`, `ci.yml:307`, `ci.yml:382` | `SDL2-devel` | `SDL3-devel` |
| `ci.yml:388`, `ci.yml:392` | `mingw64-sdl2-compat`, `mingw32-sdl2-compat` | `mingw64-SDL3`, `mingw32-SDL3` |
| `ci.yml:450`, `release.yml:355` | `brew install … sdl2` | `… sdl3` |
| `release.yml:148`, `:214`, `:218` | Fedora + MinGW names | as above |
| `release.yml:106` | `libsdl2-dev` | `libsdl3-dev` (subject to Q1) |

All verified installable on Fedora 44 except the Ubuntu 24.04 case (§5.5).

### 6.2 The env-var trap — a test that would go SKIP, not FAIL

**This is the highest-value finding in this note after the audio measurement.**

16 test scripts set SDL driver env vars. SDL3 renamed them. Tested empirically
against the installed SDL3 3.4.16 rather than assumed:

| Env var | Honoured by SDL3? | Evidence |
|---|---|---|
| `SDL_AUDIODRIVER` | **YES** (legacy name kept) | ran a probe: `SDL_AUDIODRIVER=disk` → `audio driver = disk` |
| `SDL_VIDEODRIVER` | **YES** (legacy name kept) | ran a probe: `SDL_VIDEODRIVER=dummy` → `video driver = dummy`, `=x11` → `x11` |
| `SDL_DISKAUDIOFILE` | **NO** | absent from `libSDL3.so.0`; probe wrote **no file at all**, and no stray default file either |
| `SDL_DISKAUDIODELAY` | **NO** | absent; SDL3's analogue is `SDL_AUDIO_DISK_TIMESCALE` — **different semantics** (a rate multiplier, not a delay) |

So 14 of the 16 scripts need **no change**. Two do:

- `test/00regression/scripts/audio-underrun-func.sh:44`
- `test/00regression/scripts/silent-func.sh:34,40`

both `SDL_DISKAUDIOFILE=` → `SDL_AUDIO_DISK_OUTPUT_FILE=`.

**Why this matters more than a rename.** `audio-underrun-func.sh:52-53` reads:

```sh
if [[ ! -s "$raw_file" ]]; then
    skip_row " (no audio captured; no SDL audio backend?)"
```

Left unrenamed, the capture file is never written, the row **SKIPs**, and the
suite stays green — the project's **only** audio-underrun regression test would
silently stop testing during the single change most likely to break audio. That is
precisely `feedback_green_tests_are_not_verification`. The rename is mandatory,
and the migration should additionally confirm the row reports **PASS**, not SKIP,
rather than trusting the totals.

The `SDL_DISKAUDIODELAY` change is not used in-tree today, but
`technique_audio_capture_and_spectral_compare` warns that setting it to 0 makes
underrun bugs invisible. That warning needs updating to the new name and
semantics, or the next investigation will reach for a variable that no longer
exists.

### 6.3 Test-manifest consequences

Deleting the two packaging special cases removes their tests:
`test/packaging/bundle-dlopen-deps-test.sh` (rows BD-01/02/03 assert the GH #46
fix) and the SDL3 assertions inside `packaging-test.sh` / `verify-bundle-test.sh`.
These run via `make packaging-test` (`Makefile:1529`), not `unit-tests.conf`, so
`test/unit-tests.conf`'s pinned 112 suites are unaffected — but the deletions are
**test removals** and by project rule need an independent reviewer
(`feedback_reviewer_for_test_removal`). Deleting a test whose bug class the
migration *structurally eliminates* is correct; it still gets reviewed.

---

## 7. Migration strategy and rollback

### 7.1 Single cut, not staged — and no shim of our own

**Recommendation: a single cut on one branch.** Staging is not available in any
useful form:

- SDL2 and SDL3 headers define conflicting symbols; a build cannot link both.
- The surface is not separable by subsystem. `keyboard.h`'s `SDL_Scancode`
  (§1.1) is a transitive dependency of `jnext_core`, `jnext_debug`, both
  frontends and seven test suites. The moment that include flips, everything
  flips.

**A compatibility shim of our own is not warranted, and building one would be the
error this migration exists to undo.** jnext would be re-implementing sdl2-compat
— the exact layer whose costs (invisible `dlopen`, two hand-written packaging
special cases, GH #46's shipped-and-broken `.app`) motivate the change. The whole
return on this work is *deleting* an indirection, not moving it in-tree.

The one place a small internal abstraction *is* defensible is `Keyboard`'s
`SDL_Scancode` (§1.1) — a jnext-owned key enum would decouple core from SDL
permanently. That is a **separate refactor**, larger than this migration, and
should not be smuggled into it.

### 7.2 Commit series

1. Design note (this file).
2. Build system: `find_package(SDL3)`, link targets, `src/debug` include fix.
3. `src/input/` — keyboard scancodes, gamepad_host instance-id model, dispatcher
   constants, **plus the sentinel fix of §3.3 with a test row**.
4. `src/platform/` — audio (§2), display (§3.4), input pump (§3.2).
5. `src/gui/` — init flags, event drain, Qt→SDL translation; and the About box at
   `main_window.cpp:1332` which reads *"Written in C++17 with Qt 6 and SDL2."*
   (user-visible string).
6. Tests: constant renames + the two env-var fixes of §6.2.
7. Packaging: rpm, flatpak (delete module), macOS (delete script + CMake hook),
   Windows (delete lines 163-178), debian (pending Q1).
8. CI + docs. `src/doc/developer-guide/` mentions SDL2 in **7 places across 7
   files** (the user guide: none). Those sources are staleness-gated by
   `docs-devguide-check`, so editing them requires `make docs-devguide` and
   committing the re-rendered `doc/developer-guide/` in the same change.

### 7.3 Proving it, not asserting it

- **Audio**: the §2.3 A/B on the real `audio_fill` policy, plus a full-binary
  before/after disk capture compared per
  `technique_audio_capture_and_spectral_compare`. Confirm `audio-underrun-func`
  reports **PASS**, not SKIP (§6.2).
- **Video**: the screenshot regression is the oracle. **No reference screenshot
  may be regenerated** — a migration that needs new references has changed
  rendering, which is a defect, not an intended outcome.
- **Input**: `input_test` (175 SDL refs) and `input_integration_test` exercise the
  dispatcher; the instance-id sentinel needs a *new* row (§3.3) because no
  existing row can fail on it.
- **Platform legs that cannot be run here**: Windows and macOS are cross-build +
  artifact-inspection only. `technique_force_platform_gated_cmake_branch` applies
  — force the `if(APPLE)`/`if(WIN32)` branches on Linux to prove they are not
  silently truthy `-NOTFOUND`, which is how #46's real defect hid.
- **Full triplet**: unit 8027/8027 (112 suites) · FUSE 1356/1356 · regression
  140/140, with `JNEXT_TEST_JOBS=4` and `/proc/loadavg` read first.

### 7.4 Rollback

The branch is a single revert. The risk that cannot be reverted by git is a
**reference screenshot regenerated to match a rendering change** — hence the
absolute rule in §7.3. Nothing else in this migration writes state that outlives
a checkout.

---

## 8. Open questions requiring an owner decision

### Q1 — Ubuntu 24.04 LTS has no `libsdl3-dev`. What happens to its `.deb`? **(BLOCKING)**

`release.yml:92-96` ships a `.deb` built in an `ubuntu:24.04` container. SDL3 does
not exist in 24.04 (noble) or 24.10 (oracular); it first appears in 25.04 (§5.5).
24.04 is an LTS supported to 2029. Options:

| Option | Consequence |
|---|---|
| **(a) Drop the 24.04 leg; ship 26.04 only** | A supported LTS loses its native package. 24.04 users fall back to Flatpak (which works — §5.2) or build from source. Simplest, honest, and the release notes must say so. |
| (b) Build SDL3 from source inside the 24.04 container | Breaks the "CI runs plain make targets" rule, and produces a `.deb` whose `dpkg-shlibdeps` cannot express its own SDL dependency — an uninstallable or silently-broken package. **Recommend against.** |
| (c) Keep 24.04 on SDL2 | Requires maintaining both backends forever. Defeats the entire purpose of #57. **Recommend against.** |
| (d) Defer #57 until 24.04 leaves support | Costs nothing now, keeps the sdl2-compat tax and both packaging special cases until 2029. |

**My recommendation: (a).** The Flatpak already covers 24.04 users with a fully
supported path (KDE runtime ships SDL3, §5.2), and the migration's concrete wins —
deleting `bundle-dlopen-deps.sh`, deleting `bundle-dlls.sh:163-178`, deleting the
Flatpak SDL module, and removing the GH #46 bug class — are worth one LTS `.deb`.
But this changes a **shipped artifact**, so it is yours, not mine.

### Q2 — Single cut, or staged? *(recommendation given; confirm)*

§7.1 argues a single cut is the only technically available option and that an
in-tree shim would re-create the layer being removed. The consequence to accept
is a large single branch: ~30 source files, 527 SDL identifier occurrences, 7
test files, 16 regression scripts, 4 packaging targets, 2 CI workflows. **Recommend: single cut**, with
the readable commit series of §7.2 so review is per-subsystem even though the
branch lands at once. Confirm you are content with a branch that size before I
start.

### Q3 — Windows SDL-only `main()` entry: which SDL3 form? *(not blocking)*

`CMakeLists.txt:432-440` links `SDL2::SDL2main` for the Windows `ENABLE_QT_UI=OFF`
build. SDL3 has no `libSDL3main`; the choices are `#include <SDL3/SDL_main.h>` in
`main.cpp`, or `SDL_MAIN_HANDLED` + a plain `main`. Given the existing note at
`CMakeLists.txt:462` about include ordering versus Qt's `main=qMain`, I would
**take `SDL_MAIN_HANDLED` + plain `main`**: it removes the rename entirely, so the
Qt and SDL-only legs stop differing on how `main` is spelled. I can cross-build
and inspect both, so this needs no decision from you unless you have a preference
— flagging it because it touches the GH #108 Windows-7-floor leg, which has
burned this project before (the >2 MB stack frame, still open in
`EMULATOR-DESIGN-PLAN.md` §11).

---

## Appendix — verification environment

All package/API facts in this note were checked against the dev host on
2026-09-23, not from memory:

- `SDL3-devel-3.4.16-1.fc44`, `SDL3-static`, `mingw64-SDL3-3.4.16-1.fc44`,
  `mingw32-SDL3-3.4.16-1.fc44` — all installed or available on Fedora 44.
- SDL3 headers read at `/usr/include/SDL3/` (3.4.16, per `SDL_version.h:47-65`).
- `org.kde.Platform//6.10` and `org.kde.Sdk//6.10` inspected on disk.
- Ubuntu series data from the Launchpad published-sources API.
- The audio A/B (§2.3) and the env-var probes (§6.2) are runnable programs, built
  against this tree's `src/platform/audio_fill.h`.

---

## 9. Implementation outcome (2026-09-24)

Written after the migration landed. Everything above is the design as it stood
beforehand; this section is what the work actually found. Where the two
disagree, this one is right.

### 9.1 What the design note got right

- The audio model maps exactly. `SDL_OpenAudioDeviceStream` +
  `SDL_AudioStreamCallback` replaced the SDL2 device callback, the lock moved
  from the device to the stream with the same mutual-exclusion contract, and
  **not one line of `audio_fill.h` / `audio_pacing.h` changed**. §2.3's
  conclusion that `queued_ms()` must stay ring-only is carried into the code as
  a comment at the function, so the "improvement" it warns against is refused
  at the point someone would make it.
- The joystick instance-id sentinel was real and silent. Both sites named in
  §3.3 were wrong under a mechanical type swap.
- The env-var trap was real. `SDL_DISKAUDIOFILE` is gone;
  `SDL_AUDIO_DISK_OUTPUT_FILE` is the name.
- The two packaging special cases deleted, exactly as predicted, and so did the
  Flatpak `sdl2` module.
- `SDL_AUDIODRIVER` / `SDL_VIDEODRIVER` are still honoured, so 14 of the 16
  regression scripts needed no change.

### 9.2 What it got wrong or missed

1. **Q1 was not the blocker it claimed.** §5.5 called Ubuntu 24.04's missing
   `libsdl3` a hard blocker and §8 recommended dropping the artifact. Both were
   wrong, for a reason the note itself contains: the Flatpak manifest had been
   building SDL2 from a pinned, checksummed tarball for years. The answer is
   `packaging/build-sdl3.sh`, driven by a new `make sdl3-vendor` target that
   `package-deb` depends on. It self-skips when the distro provides SDL3, so
   `make package-deb` is byte-identical on both LTS legs and on a developer's
   machine.
   The note's objection that `dpkg-shlibdeps` "cannot express the dependency"
   was also answered rather than accepted: linking SDL3 **statically** means
   there is no bundled `.so`, no rpath and nothing for `dpkg-shlibdeps` to
   resolve — only SDL3's own link-time dependencies, which map to real Ubuntu
   packages. No CMake special case was needed, because SDL3's own
   `SDL3Config.cmake` points the `SDL3::SDL3` alias at whichever library the
   prefix was built with.

2. **The mouse event fields changed type, and the note did not mention it.**
   §3.2 lists the keyboard and tick changes but not the mouse. SDL3 delivers
   `SDL_MouseMotionEvent::xrel/yrel` and `SDL_MouseWheelEvent::y` as `float`
   where SDL2 gave integers, and the right answer differs per field:
   - **Wheel** has an integer twin, `integer_y`, which is SDL's accumulation of
     fractional detents into whole ones — i.e. exactly SDL2's `y`. Reading the
     float would truncate every sub-detent scroll of a high-resolution wheel to
     zero. (`integer_x/y` were added in SDL 3.2.12; every SDL3 jnext targets is
     newer, and an older header fails to compile rather than mis-scrolling.)
   - **Motion** has no integer twin and the deltas really can be sub-unit, so
     `MouseDispatcher::handle_sdl_event` carries the remainder forward between
     events. Truncating each one alone would drop a slow drag entirely.
   Rows `MOUSE-SDL3-WHEEL` and `MOUSE-SDL3-MOTION` pin the two apart; neither
   can be satisfied by the other's implementation.

3. **`SDL_SetRenderVSync` can fail, and that must not be fatal.** §3.4 said
   vsync "needs deliberate care", which was right, but not why: in SDL3 it is a
   separate call that returns a status, and drivers without a refresh to sync
   to — the `dummy` video driver the regression suite renders under, and the
   software renderer generally — legitimately refuse it. It is requested (so
   the default is ON, as `SDL_RENDERER_PRESENTVSYNC` gave us, which
   `sdl_app.h`'s >100 %-speed present throttle depends on) and a refusal is
   logged, not fatal.

4. **Q3 answers itself.** §4 and §8 Q3 treated the Windows `main()` entry as an
   open question between `SDL_main.h` and `SDL_MAIN_HANDLED`. It is not a
   choice any more, because **SDL3's `SDL.h` does not include `SDL_main.h` at
   all** — so there is no rename to disarm and no include-order race with Qt's
   `main=qMain`. The Qt build simply never sees the header; the Windows
   SDL-only build includes it explicitly, in one translation unit, under
   `#if defined(_WIN32) && !defined(ENABLE_QT_UI)`. The configuration is now
   stated rather than inferred from include order, which is strictly better
   than what SDL2 forced.

5. **`SDL_oldnames.h` is the rename oracle.** Not a defect in the note, but
   worth recording: SDL3 ships a header that turns every SDL2 spelling into a
   compile error naming its SDL3 replacement
   (`SDL_CONTROLLER_BUTTON_A_renamed_SDL_GAMEPAD_BUTTON_SOUTH`). The rename
   half of this migration was therefore mechanically checked by the compiler,
   not by a table someone wrote out.

6. **The audio-underrun row got a stronger fix than a rename.** §6.2 asked for
   `SDL_DISKAUDIOFILE` → `SDL_AUDIO_DISK_OUTPUT_FILE` and for the migration to
   confirm the row reports PASS. Both done — but the rename alone leaves the
   trap armed for the *next* change. The missing-capture branch in
   `audio-underrun-func.sh` and `silent-func.sh` is now a **`fail_row`, not a
   `skip_row`**. There is no honest reason for the capture to be absent: the
   `disk` driver is compiled in unconditionally and needs no sound server, no
   device and no permissions. The remaining skips in those scripts
   (`xvfb-run`, `python3`, ImageMagick) are genuine host-tool absences and stay.

### 9.3 Test removals, and why

Three removals, all of things the migration structurally eliminated rather than
merely disabled — each replaced by an in-place note saying what was there and
why it went, so nobody re-derives the bug class from scratch:

- `packaging/macos/bundle-dlopen-deps.sh` and its contract suite
  (`bundle-dlopen-deps-test.sh`, rows BD-01..04). The script existed solely
  because Homebrew's `sdl2` was sdl2-compat and `dlopen`ed libSDL3 by leaf
  name. Linking SDL3 directly makes it an ordinary `LC_LOAD_DYLIB` entry.
- `verify-bundle.sh`'s matching dlopen rule, and its two pinning rows
  (`VB-15`, `VB-16`). The rule could no longer fire on anything.
- `packaging/windows/bundle-dlls.sh` lines detecting the same shim and
  force-queueing `SDL3.dll`. It is a real PE import now.

Against those, the Windows packaging rows gained an assertion they did not have:
every Windows zip must contain `SDL3.dll` **and must not contain `SDL2.dll`** —
so a build that silently resolved back to `mingw-sdl2-compat` fails the package
test instead of shipping.

**And one of those removals shipped broken, which is the lesson worth keeping.**
`test/packaging/packaging-selftest.sh` lists the contract sub-tests by filename
in a `SUBTESTS` array and pins their pass counts (`"Pass: 7"` / `"Pass: 6"`).
Deleting `bundle-dlopen-deps-test.sh` invalidated all three, so
`make packaging-selftest` — the **first prerequisite of `make package-test`**,
which is what CI's packaging job runs — failed at 1/3 and aborted that job
before it built a single artifact. **The required triplet cannot see this**:
`packaging-selftest` is not part of `make unit-test`, the FUSE suite or the
regression suite, so all three stayed green over a broken CI job. Caught in
review, not by any gate.

The prose above discussed this removal at length and never mentioned the file
that referenced it. **Deleting a test means grepping the tree for its filename
first** — `git grep bundle-dlopen-deps` would have found the one live
reference in seconds — and then running the suite that owns it, not only the
triplet.

---

## 10. Review round 1 (2026-09-24) — findings and what changed

The migration itself came through review essentially unscathed; nearly the whole
surface was re-verified by execution, including an independent reproduction of
the Ubuntu 24.04 container build and the Windows `objdump`. Four things changed.

### 10.1 A broken CI job the required triplet could not see (BLOCKER)

`make packaging-selftest` went 3/3 → 1/3 and `make package-test` aborted on it.
Cause and lesson are recorded in §9.3 above, at the point where the removal is
justified, rather than here — that is where someone deleting the next test will
be reading. Fixed: the roster and both pinned counts. `make package-test` now
runs end to end (20 pass, 1 pre-existing skip), which also exercised
`package-flatpak` for the first time and so confirmed by execution that the
deleted `sdl2` module really is unnecessary under the KDE 6.10 runtime.

### 10.2 The id-0 guards pinned nothing individually (MAJOR)

`JRAW-29/30` asserted the outcome, and an outcome test cannot see this table's
real hazard. Removing **either** id-0 guard alone left all 342 rows green;
only removing both failed anything. So a later "simplification" deleting one on
the theory that the other covers it would have gone undetected.

Measuring it explains why, and the explanation changed the fix. The invalid id
and the free marker are the same value (0), **and a free entry's slot is -1** —
so an entry corrupted into "free AND connector N" still answers "unmapped" to
every public query. The corruption is real; its consequences are not observable
until some later change starts trusting the slot field. Two hand-written guards
at two call sites could therefore never be pinned behaviourally, however many
outcome rows were added.

What landed instead:

- The two duplicated guards become **one shared `entry_matches()`** used by both
  readers, so the id-0/free disambiguation is written once rather than copied.
- A white-box accessor, `device_map_free_entries_are_clean()`, asserts the
  **invariant** the treatment maintains — a free entry carries no connector.
- `JRAW-31` (write half), `JRAW-32` (read half + no collateral damage on a live
  mapping), and an extension to `JRAW-30` (the unmap path) assert it directly.

Mutation results, run individually rather than assumed:

| mutation | result |
|---|---|
| remove `map_instance_to_slot`'s id-0 rejection | **JRAW-31 + JRAW-32 fail** |
| unmap clears the id but leaves a stale slot | **JRAW-30 fails** |
| remove `entry_matches`' `!= 0` term | *no row fails — and no behaviour changes* |

The third is stated rather than papered over. That term is **provably**
unobservable while the invariant holds, because matching a free entry still
returns slot -1. It is kept as the safety net that makes a *broken* invariant
non-catastrophic, and the invariant rows are the tripwire that fires the moment
it breaks — which is the realistic defect it guards against, and which the
second mutation above shows is now caught. Neither is dressed up as the other.

The comment claiming the write-side rejection was "the ONLY place the two
meanings are kept apart" was false and is replaced by a description of the real
two-part arrangement.

### 10.3 A stale constant name (MINOR)

`host_key_latch.h` still named `SDL_NUM_SCANCODES` in a comment. The hard-coded
512 is correct — `SDL_SCANCODE_COUNT` is still 512 in 3.4.16, so the constant
was renamed, not renumbered, and the comment now says exactly that.

### 10.4 The vendored `.deb` audio dependency is Depends, not Recommends

Owner decision on the reviewer's evidence, reversing §9.2's first answer.
`dpkg -i` + `apt-get install -f` honours Depends but **not** Recommends, so the
package installed with zero audio backends and jnext ran silently mute — and
`dpkg -i` is what most "download the .deb" instructions say. The
keep-it-soft argument does not apply to a package that already hard-Depends on
the entire Qt6 GUI stack: there is no minimal or headless install being spared.

Written as **alternatives**, `libpipewire-0.3-0 | libpulse0 | libasound2t64`:
any one backend is sufficient, and a conjunction would drag PulseAudio onto a
PipeWire desktop and vice versa.

Two things verified in a container rather than assumed, both of which could
have been silent regressions:

- `CPACK_DEBIAN_PACKAGE_DEPENDS` **appends** to the `SHLIBDEPS`-generated list
  rather than replacing it. The shipped `Depends:` carries the alternatives
  *and* libc6, the three Qt6 libraries, libcurl, libpng, libssl, libstdc++ and
  zlib. Had it replaced, the package would have declared almost none of its
  real dependencies.
- The `dpkg -i` path now installs an audio backend and leaves
  `Status: install ok installed`, with `jnext --version` running.

The non-vendored legs are untouched: the block is gated on
`JNEXT_SDL3_VENDORED`, and the Fedora-built `.deb` carries no injected audio
dependency.
