#pragma once

// The three hooks the patched third_party/crtemu/crtemu.h calls (see
// third_party/crtemu/README.jnext.md). Declared here, defined in
// crt_gl_overlay.cpp, so that crtemu_impl.cpp — the one translation unit that
// holds the library's implementation — needs nothing from Qt's headers.

#ifdef JNEXT_HAVE_CRT_GL

/// Resolve a GL entry point in the current QOpenGLContext; null if it has none.
void* crt_gl_get_proc(const char* name);
/// The framebuffer crtemu must treat as "the screen" (the widget's own FBO).
unsigned crt_gl_screen_fbo();
/// A shader compile/link log line from crtemu.
void crt_gl_report(const char* text);
/// Free crtemu's own allocation only (no GL calls): for a context that is already gone.
struct crtemu_t;
void crt_gl_free_struct(crtemu_t* crt);

#endif
