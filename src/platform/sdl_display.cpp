#include "sdl_display.h"
#include "core/log.h"

bool SdlDisplay::init(const char* title, int native_w, int native_h, int display_h) {
    native_w_ = native_w;
    native_h_ = native_h;
    display_h_ = display_h;

    // Window sized at NATIVE_W × DISPLAY_H × scale gives square pixels: SDL
    // stretches the NATIVE_W × NATIVE_H texture across the NATIVE_W ×
    // DISPLAY_H logical viewport (G104 Phase 7).
    // SDL3 takes no window position in SDL_CreateWindow; a window is also
    // shown by default (SDL_WINDOW_SHOWN is gone). Centring is a separate,
    // best-effort call — a window manager may place the window itself, which
    // was equally true of SDL2's SDL_WINDOWPOS_CENTERED hint.
    window_ = SDL_CreateWindow(title,
        native_w * scale_, display_h * scale_, 0);
    if (!window_) {
        Log::platform()->error("SDL_CreateWindow: {}", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // nullptr = let SDL pick the best available renderer. SDL3 has no
    // SDL_RENDERER_ACCELERATED flag: the driver list is already ordered
    // accelerated-first and falls back to software on its own.
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        Log::platform()->error("SDL_CreateRenderer: {}", SDL_GetError());
        return false;
    }

    // Vsync ON, as SDL_RENDERER_PRESENTVSYNC gave us under SDL2 — the frame
    // pacing above 100% speed depends on the default being ON (sdl_app.h: a
    // vsynced present cannot cap the speed at the display's refresh, hence
    // the RENDER_INTERVAL_MS throttle). In SDL3 it is a separate call that
    // can FAIL per driver, and some drivers legitimately have no vsync at all
    // (the `dummy` video driver the regression suite renders under, and the
    // software renderer generally). A failure there must not kill the
    // frontend — it is not an error, it is a driver without a refresh to sync
    // to — so it is reported and the renderer is used unsynced, which is what
    // SDL2 also ended up doing when it fell through to a non-vsync driver.
    if (!SDL_SetRenderVSync(renderer_, 1)) {
        Log::platform()->info("SDL_SetRenderVSync(1) unsupported by this "
                              "renderer, presenting unsynced: {}", SDL_GetError());
    }

    // Logical size = NATIVE_W × DISPLAY_H so the renderer letterboxes
    // correctly in fullscreen and the texture stretch ratio matches the
    // window aspect (1×H, 2×V). LETTERBOX is the mode that reproduces
    // SDL2's SDL_RenderSetLogicalSize behaviour, which is what this call is
    // for (see the header).
    SDL_SetRenderLogicalPresentation(renderer_, native_w, display_h,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    texture_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        native_w, native_h);
    if (!texture_) {
        Log::platform()->error("SDL_CreateTexture: {}", SDL_GetError());
        return false;
    }

    return true;
}

void SdlDisplay::upload_frame(const uint32_t* pixels, int w, int h) {
    SDL_UpdateTexture(texture_, nullptr, pixels, w * sizeof(uint32_t));
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
}

void SdlDisplay::present() {
    SDL_RenderPresent(renderer_);
}

void SdlDisplay::toggle_fullscreen() {
    fullscreen_ = !fullscreen_;
    // SDL3 takes a bool and always means borderless-desktop fullscreen (the
    // SDL2 SDL_WINDOW_FULLSCREEN_DESKTOP behaviour); an exclusive-mode
    // fullscreen now needs an explicit SDL_SetWindowFullscreenMode, which
    // jnext has never wanted.
    SDL_SetWindowFullscreen(window_, fullscreen_);
}

void SdlDisplay::set_scale(int scale) {
    if (scale < 2) scale = 2;
    if (scale > 4) scale = 4;
    scale_ = scale;
    // Only resize if not in fullscreen; fullscreen ignores scale. Window is
    // sized at NATIVE_W × DISPLAY_H × scale (square pixels, G104 Phase 7).
    if (!fullscreen_) {
        SDL_SetWindowSize(window_, native_w_ * scale_, display_h_ * scale_);
    }
}

void SdlDisplay::shutdown() {
    if (texture_)  { SDL_DestroyTexture(texture_);   texture_  = nullptr; }
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_);     window_   = nullptr; }
}
