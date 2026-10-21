// The one translation unit that holds crtemu's implementation. Third-party code,
// so its warnings are not ours to fix; the hooks it calls are in crt_gl_loader.h.

#ifdef JNEXT_HAVE_CRT_GL

#include "gui/crt_gl_loader.h"

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
#define CRTEMU_GL_GET_PROC(name) crt_gl_get_proc(name)
#define CRTEMU_SCREEN_FBO crt_gl_screen_fbo()
#define CRTEMU_FILL_VIEWPORT   // the overlay is already placed and shaped to the picture
#define CRTEMU_REPORT_SHADER_ERRORS
#define CRTEMU_REPORT_ERROR(str) crt_gl_report(str)
#include "crtemu/crtemu.h"

void crt_gl_free_struct(crtemu_t* crt) { CRTEMU_FREE( crt->memctx, crt ); }

#endif  // JNEXT_HAVE_CRT_GL
