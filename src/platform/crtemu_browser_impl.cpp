// The one translation unit that holds crtemu's implementation for the browser build. Third-party
// code, so its warnings are not ours to fix. The GL entry points come from the host loader
// (the same mode the Qt window runs, see third_party/crtemu/README.jnext.md): the context is SDL's.

#ifdef __EMSCRIPTEN__

#include <SDL3/SDL.h>
#include "core/log.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#define CRTEMU_IMPLEMENTATION
#define CRTEMU_GL_GET_PROC(name) reinterpret_cast<void*>(SDL_GL_GetProcAddress(name))
#define CRTEMU_SCREEN_FBO 0
#define CRTEMU_FILL_VIEWPORT   // BrowserCrt::present() sizes the viewport to the picture's own rectangle
#define CRTEMU_REPORT_SHADER_ERRORS
#define CRTEMU_REPORT_ERROR(str) Log::platform()->error("crtemu: {}", str)
#include "crtemu/crtemu.h"

#endif  // __EMSCRIPTEN__
