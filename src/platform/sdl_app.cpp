#include "sdl_app.h"
#include "core/emulator_config.h"
#include <cstdio>

bool SdlApp::init(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!display_.init("ZX Spectrum Next", NATIVE_W, NATIVE_H)) return false;

    // Initialise the emulator with default config.
    EmulatorConfig cfg;
    if (!emulator_.init(cfg)) {
        fprintf(stderr, "[SdlApp] Emulator init failed\n");
        return false;
    }

    input_.on_quit = [this]() { running_ = false; };
    // Route SDL key events into emulator keyboard matrix; intercept host shortcuts.
    input_.on_key  = [this](SDL_Scancode sc, bool pressed) {
        if (pressed) {
            if (sc == SDL_SCANCODE_F11) {
                display_.toggle_fullscreen();
                return;
            }
            if (sc == SDL_SCANCODE_F2) {
                int next_scale = (display_.get_scale() % 3) + 1; // 1→2→3→1
                display_.set_scale(next_scale);
                return;
            }
        }
        emulator_.keyboard().set_key(sc, pressed);
    };

    running_ = true;
    return true;
}

void SdlApp::run() {
    while (running_) {
        uint32_t frame_start = SDL_GetTicks();

        if (!input_.poll()) break;

        emulator_.run_frame();

        const uint32_t* fb = emulator_.get_framebuffer();
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
