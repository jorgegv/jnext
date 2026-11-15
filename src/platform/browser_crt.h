#pragma once
#ifdef __EMSCRIPTEN__

// The CRT effect of the browser build: crtemu (third_party/crtemu), the shader the Qt window
// runs, drawn on SDL's own WebGL 1 context, since a canvas holds exactly one. It replaces the
// SDL draw of the frame while it runs; the rules (which effect, what falls back to what) are
// crt_shader::resolve() (gui/crt_shader_policy.h), shared with the Qt window.

#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gui/crt_shader_policy.h"

struct crtemu_t;

class BrowserCrt {
public:
    /// Build the effect on the current WebGL context (the renderer's, so call it after
    /// SDL_CreateRenderer). Null, with `why` set, when the context cannot run it.
    static std::unique_ptr<BrowserCrt> create(SDL_Renderer* renderer, crt_shader::Style style,
                                              std::string& why);
    /// Destroys crtemu's GL objects: the context must still be alive.
    ~BrowserCrt();
    BrowserCrt(const BrowserCrt&) = delete;
    BrowserCrt& operator=(const BrowserCrt&) = delete;

    /// Draw one ARGB32 frame, in the rectangle SDL's letterbox gives the unfiltered one.
    void present(SDL_Renderer* renderer, const uint32_t* argb, int w, int h);

private:
    BrowserCrt() = default;
    crtemu_t* crt_ = nullptr;
    int64_t t0_us_ = 0;
    std::vector<uint32_t> abgr_;
};

#endif
