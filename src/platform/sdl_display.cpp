#include "sdl_display.h"
#include <cstdio>

bool SdlDisplay::init(const char* title, int native_w, int native_h) {
    native_w_ = native_w;
    native_h_ = native_h;

    window_ = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        native_w * scale_, native_h * scale_,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!window_) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }

    SDL_RenderSetLogicalSize(renderer_, native_w, native_h);

    texture_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        native_w, native_h);
    if (!texture_) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

void SdlDisplay::upload_frame(const uint32_t* pixels, int w, int h) {
    SDL_UpdateTexture(texture_, nullptr, pixels, w * sizeof(uint32_t));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
}

void SdlDisplay::present() {
    SDL_RenderPresent(renderer_);
}

void SdlDisplay::toggle_fullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_,
        fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void SdlDisplay::set_scale(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 3) scale = 3;
    scale_ = scale;
    // Only resize if not in fullscreen; fullscreen ignores scale.
    if (!fullscreen_) {
        SDL_SetWindowSize(window_, native_w_ * scale_, native_h_ * scale_);
    }
}

void SdlDisplay::shutdown() {
    if (texture_)  { SDL_DestroyTexture(texture_);   texture_  = nullptr; }
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_);     window_   = nullptr; }
}
