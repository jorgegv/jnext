#include "sdl_app.h"
#include "core/emulator_config.h"
#include "core/log.h"

bool SdlApp::init(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        Log::platform()->error("SDL_Init: {}", SDL_GetError());
        return false;
    }
    if (!display_.init("JNEXT — ZX Spectrum Next Emulator", NATIVE_W, NATIVE_H)) return false;
    if (!audio_.init()) {
        Log::platform()->warn("Audio init failed — continuing without sound");
    }

    // Initialise the emulator with config (use set_config() before init(), or defaults).
    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
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
            if (sc == SDL_SCANCODE_ESCAPE && display_.is_fullscreen()) {
                display_.toggle_fullscreen();
                return;
            }
            if (sc == SDL_SCANCODE_F2) {
                int next_scale = display_.get_scale() + 1; // 2→3→4→2
                if (next_scale > 4) next_scale = 2;
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

void SdlApp::set_pending_load(const std::string& file, int delay_frames) {
    load_file_ = file;
    load_countdown_ = delay_frames;
    Log::platform()->info("--load: will load '{}' after {} frame(s)", file, delay_frames);
}

void SdlApp::set_delayed_screenshot(const std::string& file, int delay_seconds) {
    screenshot_file_ = file;
    screenshot_countdown_ = delay_seconds * 50;  // 50 fps
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} second(s)",
                           file, delay_seconds);
}

void SdlApp::set_delayed_exit(int delay_seconds) {
    exit_countdown_ = delay_seconds * 50;  // 50 fps
    Log::platform()->info("--delayed-automatic-exit: will exit after {} second(s)",
                           delay_seconds);
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

        // Apply pending load when countdown reaches zero.
        if (load_countdown_ == 0) {
            emulator_.load_nex(load_file_);
            load_countdown_ = -1;  // done
        } else if (load_countdown_ > 0) {
            --load_countdown_;
        }

        emulator_.run_frame();

        // Push audio samples accumulated during this frame to SDL.
        audio_.push_from_mixer(emulator_.mixer());

        const uint32_t* fb = emulator_.get_framebuffer();
        const int fb_w = emulator_.get_framebuffer_width();
        const int fb_h = emulator_.get_framebuffer_height();
        display_.upload_frame(fb, fb_w, fb_h);
        display_.present();

        // Delayed screenshot: take after countdown expires.
        if (screenshot_countdown_ == 0) {
            save_screenshot_png(screenshot_file_, fb, fb_w, fb_h);
            screenshot_countdown_ = -1;  // done
        } else if (screenshot_countdown_ > 0) {
            --screenshot_countdown_;
        }

        // Delayed automatic exit.
        if (exit_countdown_ == 0) {
            Log::platform()->info("automatic exit triggered");
            running_ = false;
        } else if (exit_countdown_ > 0) {
            --exit_countdown_;
        }

        // Frame pacing: target 50 Hz
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
    }
}

void SdlApp::shutdown() {
    audio_.shutdown();
    display_.shutdown();
    SDL_Quit();
}
