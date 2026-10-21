# OpenGL CRT Display Filter

## Use case

The Spectrum was designed for a cathode-ray tube, and its pictures — hard-edged
pixels, attribute colour, dither tricks — were drawn for one. The scanline gaps,
phosphor mask, bloom and slightly curved glass are part of how the artwork was meant
to be seen, and for many people part of why it feels familiar.

The CRT Filter jnext had was a flat 25 %-black line over every other display row:
no bloom, no mask, no curvature. Users who want the look of a real set want a real
CRT emulation, and want it switched off, at no cost, when they do not.

## Functionality

The **CRT effect** is one choice, in the View menu and in Preferences: **off**,
**Scanlines**, **TV** or **PC monitor**. TV and PC monitor draw the emulator picture
through a GPU CRT emulation; Scanlines replaces the old 25 %-black line overlay with the
same shader's scanlines-only (Lite) look, so there is one implementation of CRT drawing
rather than two.

| Mode | Look | Implementation |
|---|---|---|
| Scanlines | scanlines only (with a light bloom) | shader Lite style; the old software overlay where there is no OpenGL (a build without it, or a driver that failed) |
| TV | curved glass, bloom, scanlines, phosphor mask, slight noise and flicker | shader TV style |
| PC monitor | flatter, scanlines and mask, little curvature | shader PC style |

The curved styles show the whole picture, border rows included, inside the glass with a
thin black surround (crtemu patches 6 and 7). Upstream's TV and PC shaders crop the picture
(see patch 7): on this 640x256 frame that lost about 15 rows top and bottom and almost
nothing at the sides, which is how it was noticed.

* **Off by default.**
* **Off is free per frame.** With the shader off there is no overlay widget, no OpenGL
  context, no shader program and no per-frame work (the build still links three more
  libraries, loaded at start-up; see Build). The display path is the one that
  existed before the feature, plus one null-pointer test in `update_frame()` and one
  in `paintEvent()`.
* **On never leaves a dead picture.** If the machine cannot provide OpenGL (no
  context, GLSL 1.20 refused), the widget drops the overlay, returns to the software
  display, switches the setting off, and tells the user why once. A failure under the
  Scanlines mode is silent: the software overlay takes over (and GL is not retried on
  every toggle).
* The modes are **mutually exclusive**: Preferences offers one pop-up, the View menu
  entries (CRT Scanlines, CRT TV, CRT PC Monitor) uncheck one another and none
  checked is off. Persisted as `[startup] crt_filter` (Scanlines; the key that already
  existed, so an old config keeps working), `crt_shader` and `crt_shader_style`; a config
  with both set starts with the shader only, and a shader saved in the Lite style is the
  Scanlines mode.
* It is a **display filter only**. It reads the frame the emulator already
  produced, changes nothing the guest can observe, and does not touch screenshots
  or recordings, which are taken from the emulator's own framebuffer.
* The picture occupies the rectangle the unfiltered picture does, in
  windowed and in fullscreen mode (the integer-scale rule of G104 Phase 7); at a
  fractional device pixel ratio the two can differ by one physical pixel, because
  widget geometry is in whole logical pixels.

Not in scope: user-supplied or adjustable shader parameters, a CLI flag, the SDL-only
frontend (it has no CRT filter of any kind today), Qt 5 builds. Note that the 640x256
framebuffer is the core's clipped window onto a taller natural frame (`video/timing.h`);
rows outside it never reach any display filter.

## Design

### Why this library, and why it is not a plain dependency

The output is not OpenGL today: the Qt GUI paints a software `QImage` through
`QPainter`, and the SDL frontend uses `SDL_Renderer`. A GPU shader therefore needs a
GL context from somewhere. [crtemu](REFERENCES.md) is a single C header (MIT /
Unlicense, GPLv3-compatible) of about 1,900 lines that implements exactly one thing —
the CRT emulation as a pair of fragment-shader passes plus a bloom blur — and takes
a pixel buffer. RetroArch and MAME shaders are slang/GLSL presets that need a
render-graph runtime with multi-pass scheduling and LUT texture loading, which is a
larger dependency than the whole of jnext's display code.

crtemu is **vendored** in `third_party/crtemu/`, not fetched, and **patched** (all
marked `JNEXT PATCH`, listed in `third_party/crtemu/README.jnext.md`):

1. A host-supplied GL loader (`CRTEMU_GL_GET_PROC`) replaces upstream's three:
   GLEW + SDL2's `SDL_opengl.h` on Unix, `LoadLibrary`/`wglGetProcAddress` on Windows.
   jnext has neither GLEW nor SDL2 in the Qt path; Qt already resolves GL.
2. `CRTEMU_SCREEN_FBO`: upstream rebinds framebuffer 0 as "the screen" 13 times; a
   `QOpenGLWidget` renders into its own FBO.
3. Bind-then-upload for the vertex buffer (upstream uploads into whatever is bound).
4. The vertex buffer binding and attribute pointer are re-stated at the start of every
   `crtemu_present()`. Upstream sets them once in `crtemu_create()` and relies on them
   surviving; under Qt they do not, and the next draw crashed the driver
   (`EXC_BAD_ACCESS` in `GLEngine`, measured on macOS) until this was done.
5. `CRTEMU_FILL_VIEWPORT`: upstream letterboxes to 4:3 inside the viewport; jnext's
   picture is 640x512 and is already placed.
6. Curvature margin: `uv *= 1.1` becomes `1.152` in the TV and PC shaders.
7. The picture crops in the TV and PC shaders are removed (details in `README.jnext.md`).

### Structure

```
EmulatorWidget (QWidget, software path, unchanged)
  `-- CrtGlOverlay (QOpenGLWidget)   exists only while the shader is on
        `-- crtemu_t                 owned by it; created in initializeGL()
```

* `src/gui/crt_shader_policy.h` — pure rules, no Qt and no GL: the style numbers
  (equal to crtemu's `crtemu_type_t`, asserted), the fullscreen integer-scale rule
  (now also used by `EmulatorWidget::prescale()`, so the two cannot drift), and the
  ARGB32 -> GL_RGBA byte-order swap.
* `src/gui/crt_gl_overlay.{h,cpp}` — the `QOpenGLWidget`: requests a 2.1 /
  compatibility context (crtemu's shaders are GLSL 1.20; on macOS this selects the
  legacy profile, because a 3.2+ core context rejects `#version 120`), keeps the
  newest submitted frame, calls `crtemu_present()` in `paintGL()`, transparent to
  mouse and focus so input still reaches the emulator widget beneath.
* `src/gui/crtemu_impl.cpp` — the one translation unit holding crtemu's
  implementation, built with the third-party warnings silenced.
* `EmulatorWidget` creates the overlay on `set_crt_shader(true)` and deletes it on
  `false`. While it exists `update_frame()` hands the framebuffer straight to it (no
  copy, no software prescale) and `paintEvent()` returns early; on removal the
  software path repaints from the last frame. The overlay counts a presented frame
  through the same `present_count()` gate the software path uses.
* A context that cannot be created is discovered only at the first paint (Qt logs
  "Failed to create context" and never calls `initializeGL()`), so the overlay reports
  it from `paintEvent()`; the owner drops it through a queued connection (it cannot
  be deleted from inside its own paint), emits `crt_shader_failed`, and `MainWindow`
  unchecks the menu entry and opens a non-modal message.

### Build

`-DENABLE_CRT_GL=ON` (default) enables it when Qt 6 and its `OpenGLWidgets` module
are found. Otherwise — or with `OFF` — the three sources are not compiled, no macro is
defined, and the build, the menus and the Preferences dialog are exactly what they
were: no entry, no setting UI. Qt 5 builds (the legacy Windows legs) never have it.
A build that cannot show the setting still hands the saved values back from
Preferences untouched, so a config shared between two builds does not lose them.

Linking `QtOpenGLWidgets`, `QtOpenGL` and the platform OpenGL library makes the binary
load three more libraries at start-up even when the shader is never used. That is a
load-time cost, not a per-frame one; `-DENABLE_CRT_GL=OFF` removes it.

### Menu mnemonics

The main window's Alt-letter space is full (see the header of
`test/gui/main_window_accel_test.cpp`) and `MA-05` pins how many mnemonics the menu
tree carries. The renamed Scanlines entry keeps the mnemonic the old CRT Filter entry had (L); the
TV and PC entries have none, like the Scale entries in the same popup.

## Testing

* `test/gui/crt_shader_policy_test.cpp` (14 rows, no Qt, no GL; part of the SDL-only
  set): the style numbers against crtemu's own enum, out-of-range fallback, the
  fullscreen scale on several screens and degenerate sizes, and the byte-level pixel
  conversion (the red/blue swap that would otherwise ship invisibly wrong).
* `test/gui/crt_shader_widget_test.cpp` (29 rows, offscreen QPA): off is the default
  and builds nothing; the software path still presents frames; on-then-off leaves
  nothing behind; switching on either runs or falls back with exactly one message, and an injected overlay failure (live, and stale after a toggle) behaves as specified
  (the offscreen platform has no GPU, so on a CI host this is the fallback arm — the
  test prints which outcome it saw); settings defaults, round trip, garbage and older
  configs; the Preferences dialog hands saved values back in any build and collects
  new ones; the View entry exists exactly when the build has the shader, with no
  mnemonic; applying a configuration switches it.
* `tools/crtshot` (not part of the build or the tests; `cmake --build build --target crtshot`)
  takes one picture per mode of the same emulator frame, on a real window with a real
  GPU: `./build/crtshot --prefix crt-boot` for the NextZXOS welcome screen (600 frames),
  `--load FILE --frames N` for a program. The user guide's pictures come from it.
* **What the tests cannot see is the picture.** The shader's pixels were checked by
  running the real widget on the native macOS platform, with the shader on in each of
  the three styles and off, rendering a test card and reading the result back:
  colours correct (red and blue the right way round), scanlines, mask, bloom and, for
  TV and PC, curvature; the Lite style fills the viewport. Re-do this by eye on
  Linux and Windows before a release: `QOpenGLWidget` composition differs per
  platform and no host here has been exercised other than macOS.
