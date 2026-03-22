#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

class SdlDisplay {
public:
    SdlDisplay() = default;
    ~SdlDisplay() { shutdown(); }

    bool init(const char* title, int native_w, int native_h);
    void upload_frame(const uint32_t* pixels, int w, int h);
    void present();
    void toggle_fullscreen();
    void set_scale(int scale);
    int  get_scale() const { return scale_; }
    void shutdown();

    SDL_Window*   window()   const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
    int native_w_ = 0, native_h_ = 0;
    int scale_    = 2;
    bool fullscreen_ = false;
};
