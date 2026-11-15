#ifdef __EMSCRIPTEN__

#include "platform/browser_crt.h"
#include "core/log.h"

#include <GLES2/gl2.h>
#include <chrono>

#include "crtemu/crtemu.h"

namespace {

int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

std::unique_ptr<BrowserCrt> BrowserCrt::create(SDL_Renderer* renderer, crt_shader::Style style,
                                               std::string& why) {
    const char* name = renderer ? SDL_GetRendererName(renderer) : nullptr;
    if (!name || SDL_strcmp(name, "opengles2") != 0) {
        why = "no WebGL renderer";
        return nullptr;
    }
    std::unique_ptr<BrowserCrt> b(new BrowserCrt());
    b->crt_ = crtemu_create(static_cast<crtemu_type_t>(style), nullptr);
    if (!b->crt_) {
        why = "the effect could not be built on this graphics driver";
        return nullptr;
    }
    // crtemu's optional bezel texture is bound to a sampler every frame: give it a 1x1 texture,
    // then switch the bezel off again (a null frame clears use_frame), as the Qt overlay does.
    const uint32_t clear = 0;
    crtemu_frame(b->crt_, const_cast<uint32_t*>(&clear), 1, 1);
    crtemu_frame(b->crt_, nullptr, 1, 1);
    b->t0_us_ = now_us();
    return b;
}

BrowserCrt::~BrowserCrt() {
    if (crt_) crtemu_destroy(crt_);
}

void BrowserCrt::present(SDL_Renderer* renderer, const uint32_t* argb, int w, int h) {
    if (!argb || w <= 0 || h <= 0) return;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    if (abgr_.size() != n) abgr_.resize(n);
    crt_shader::argb_to_abgr(argb, abgr_.data(), n);

    int out_w = 0, out_h = 0;
    SDL_GetRenderOutputSize(renderer, &out_w, &out_h);   // the pixels, not the logical size
    SDL_FRect lr{ 0.f, 0.f, static_cast<float>(out_w), static_cast<float>(out_h) };
    SDL_GetRenderLogicalPresentationRect(renderer, &lr);
    const crt_shader::GlRect vp = crt_shader::gl_viewport(
        static_cast<int>(lr.x), static_cast<int>(lr.y), static_cast<int>(lr.w), static_cast<int>(lr.h), out_h);

    // SDL's GLES2 renderer enables vertex attribute arrays 0 and 1 and binds a buffer to neither
    // until its first draw, which it never makes now: WebGL 1 refuses a draw while an enabled array
    // has no buffer, so disable every array but crtemu's own (0). Then the viewport is SDL's letterbox.
    GLint max_attribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_attribs);
    for (GLint i = 1; i < max_attribs; ++i) glDisableVertexAttribArray(static_cast<GLuint>(i));
    glViewport(vp.x, vp.y, vp.w, vp.h);

    crtemu_present(crt_, static_cast<CRTEMU_U64>(now_us() - t0_us_), abgr_.data(), w, h,
                   crt_shader::PRESENT_MODULATE, crt_shader::PRESENT_BORDER);
}

#endif  // __EMSCRIPTEN__
