#include "sdl_app.h"
#include <cstdio>

bool SdlApp::init(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!display_.init("ZX Spectrum Next", NATIVE_W, NATIVE_H)) return false;

    input_.on_quit = [this]() { running_ = false; };
    input_.on_key  = [](SDL_Scancode sc, bool pressed) {
        // TODO: route to Keyboard::set_key(sc, pressed)
        (void)sc; (void)pressed;
    };

    running_ = true;
    return true;
}

void SdlApp::run() {
    // Placeholder framebuffer — all black
    static uint16_t fb[NATIVE_W * NATIVE_H] = {};

    while (running_) {
        uint32_t frame_start = SDL_GetTicks();

        if (!input_.poll()) break;

        // TODO: emulator_.run_frame();
        // TODO: copy emulator framebuffer to fb

        display_.upload_frame(fb, NATIVE_W, NATIVE_H);
        display_.present();

        // Frame pacing: target 50 Hz
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
    }
}

void SdlApp::shutdown() {
    display_.shutdown();
    SDL_Quit();
}
