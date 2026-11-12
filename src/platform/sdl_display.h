#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <string>

#include "gui/crt_shader_policy.h"

#ifdef __EMSCRIPTEN__
class BrowserCrt;
#endif

class SdlDisplay {
public:
    SdlDisplay();
    ~SdlDisplay();

    /// Initialise the SDL window + renderer. The window is titled
    /// JNEXT_WINDOW_TITLE (platform/window_title.h), which carries the version.
    ///
    /// `native_w × native_h` is the IN-MEMORY framebuffer size (640×256
    /// post-G104). `display_h` is the post-vertical-2× logical viewport
    /// height (512). Window size is `native_w × display_h × scale`; the
    /// renderer's logical size matches the window's logical viewport so
    /// the `native_w × native_h` texture gets stretched 1× horizontally
    /// and 2× vertically, producing square-pixel 4:3 output (G104 Phase 7).
    bool init(int native_w, int native_h, int display_h);
    void upload_frame(const uint32_t* pixels, int w, int h);
    void present();

    /// The browser build's CRT effect (call once, after init()): `filter` and `shader` are the
    /// startup.crt_filter and startup.crt_shader settings, `style` a crt_shader::Style. Returns
    /// what the display shows (crt_shader::resolve); when the shader was asked and cannot run,
    /// `why` says so and the effect is the fallback. Elsewhere the shader never runs. The effect
    /// is fixed for the run: SDL draws nothing while the shader does.
    crt_shader::Effect set_crt(bool filter, bool shader, crt_shader::Style style, std::string& why);
    void toggle_fullscreen();
    bool is_fullscreen() const { return fullscreen_; }
    void set_scale(int scale);

    /// GH #428 — the window needs the current frame drawn again although the
    /// emulator wrote no new one (exposed, resized, fullscreen, scale).
    /// Set by SdlInput's window events and by toggle_fullscreen()/set_scale();
    /// SdlApp::tick() consumes it with take_redraw_request().
    void request_redraw() { redraw_requested_ = true; }
    bool take_redraw_request() { const bool r = redraw_requested_; redraw_requested_ = false; return r; }
    int  get_scale() const { return scale_; }
    void shutdown();

    SDL_Window*   window()   const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
    int native_w_ = 0, native_h_ = 0;
    int display_h_ = 0;  ///< Logical display height (NATIVE_H × 2 = 512).
    int scale_    = 2;
    bool fullscreen_ = false;
    bool redraw_requested_ = false;
    crt_shader::Effect crt_effect_ = crt_shader::Effect::Off;
#ifdef __EMSCRIPTEN__
    std::unique_ptr<BrowserCrt> crt_;
#endif
};
