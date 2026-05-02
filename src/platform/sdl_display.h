#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

class SdlDisplay {
public:
    SdlDisplay() = default;
    ~SdlDisplay() { shutdown(); }

    /// Initialise the SDL window + renderer.
    ///
    /// `native_w × native_h` is the IN-MEMORY framebuffer size (640×256
    /// post-G104). `display_h` is the post-vertical-2× logical viewport
    /// height (512). Window size is `native_w × display_h × scale`; the
    /// renderer's logical size matches the window's logical viewport so
    /// the `native_w × native_h` texture gets stretched 1× horizontally
    /// and 2× vertically, producing square-pixel 4:3 output (G104 Phase 7).
    bool init(const char* title, int native_w, int native_h, int display_h);
    void upload_frame(const uint32_t* pixels, int w, int h);
    void present();
    void toggle_fullscreen();
    bool is_fullscreen() const { return fullscreen_; }
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
    int display_h_ = 0;  ///< Logical display height (NATIVE_H × 2 = 512).
    int scale_    = 2;
    bool fullscreen_ = false;
};
