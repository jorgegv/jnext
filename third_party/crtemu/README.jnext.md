# crtemu (vendored)

Cathode-ray-tube emulation shader, single header, by Mattias Gustavsson.
Upstream: see `doc/REFERENCES.md` ("crtemu"). Licence: dual MIT / Unlicense (the text
is at the end of `crtemu.h`), both GPLv3-compatible.

Version: v0.1 header, taken verbatim from a known-good copy, then patched as below.

## JNEXT patches (all marked `JNEXT PATCH` in the header)

Upstream binds GL on non-Windows through `<GL/glew.h>` plus SDL2's `SDL_opengl.h`, and on
Windows through `LoadLibrary("opengl32.dll")`. JNEXT has neither GLEW nor SDL2 in the
Qt path, and a `QOpenGLWidget` already owns a context, so:

1. `CRTEMU_GL_GET_PROC("glName")` — when the host defines it, every GL entry point is
   resolved through it (we pass `QOpenGLContext::getProcAddress`), on every platform.
   The three upstream loaders are untouched and still selected when it is undefined.
2. `CRTEMU_SCREEN_FBO` — upstream rebinds framebuffer `0` as "the screen" in 13 places.
   A `QOpenGLWidget` renders into its own FBO, so that name is a host expression.
   Defaults to `0`, so an unpatched build behaves exactly as upstream.

3. `bind, then upload` — upstream called `BufferData` and only then `BindBuffer`, which
   uploads into whatever buffer was bound. Corrected at both sites (a no-op for a host
   whose binding happened to be right).
4. Re-state the vertex buffer binding and attribute pointer at the start of every
   `crtemu_present()` (host-loader mode). Upstream sets them once in `crtemu_create()`
   and relies on them surviving; under Qt they do not, and the next draw crashes the
   driver (measured: EXC_BAD_ACCESS in GLEngine on macOS).
5. `CRTEMU_FILL_VIEWPORT` — upstream letterboxes the output to 4:3 inside the viewport.
   JNEXT's picture is 640x512 and the overlay is already placed to that shape, so the
   host asks for the whole viewport. Undefined: upstream behaviour.

6. Curvature margin: the TV and PC shaders' `uv *= 1.1` becomes `uv *= 1.152`, so the
   curved glass leaves a thin black surround instead of the picture touching the
   screen edge at the middle of each side. Corners curve away as before.
7. No picture crop. Upstream's TV and PC fragment shaders sample the texture through
   compensation for the artwork of their own TV frame: `tc * vec2(1.025, 0.92) +
   vec2(-0.0125, 0.04)` (TV; keeps about 88% of the rows and nearly all columns) or
   `tc * 1.156 - 0.078` (PC; a 15.6% zoom), then `scale = 0.04` (a 2% shrink of the
   window on every side) and a `vec2(0.003, -0.001)` shift. On JNEXT's 640x256 frame that
   cut about 15 rows off the top and the bottom while barely touching the sides (measured
   with a frame whose outermost 2 px lines were four different colours). All four are
   removed: a texture coordinate of 0..1 is the whole picture. `crt_shader_policy_test`
   CRT-POL-14 fails if they come back.

The shader sources are `#version 120` (GLSL 1.20): the host must request a 2.1 /
compatibility context (`src/gui/crt_gl_overlay.cpp` does).

Do not edit `crtemu.h` further without recording it here.
