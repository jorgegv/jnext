#include "sdl_app.h"
#include "core/emulator_config.h"
#include "core/log.h"

bool SdlApp::init(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        Log::platform()->error("SDL_Init: {}", SDL_GetError());
        return false;
    }
    if (!display_.init("JNEXT — ZX Spectrum Next Emulator", NATIVE_W, NATIVE_H)) return false;

    // Initialise the emulator with default config.
    EmulatorConfig cfg;
    if (!emulator_.init(cfg)) {
        Log::platform()->error("Emulator init failed");
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
                int next_scale = (display_.get_scale() % 4) + 1; // 1→2→3→4→1
                display_.set_scale(next_scale);
                return;
            }
        }
        emulator_.keyboard().set_key(sc, pressed);
    };

    running_ = true;
    return true;
}

void SdlApp::set_pending_inject(const std::string& file, uint16_t org,
                                uint16_t pc, int delay_frames) {
    inject_file_ = file;
    inject_org_  = org;
    inject_pc_   = pc;
    inject_countdown_ = delay_frames;
    Log::platform()->info("--inject: will load '{}' at {:#06x} (PC={:#06x}) after {} frame(s)",
                           file, org, pc, delay_frames);
}

void SdlApp::run() {
    while (running_) {
        uint32_t frame_start = SDL_GetTicks();

        if (!input_.poll()) break;

        // Apply pending inject when countdown reaches zero.
        if (inject_countdown_ == 0) {
            emulator_.inject_binary(inject_file_, inject_org_, inject_pc_);
            inject_countdown_ = -1;  // done
        } else if (inject_countdown_ > 0) {
            --inject_countdown_;
        }

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
