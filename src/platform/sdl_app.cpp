#include "sdl_app.h"
#include "core/emulator_config.h"
#include "core/log.h"

bool SdlApp::init(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        Log::platform()->error("SDL_Init: {}", SDL_GetError());
        return false;
    }
    if (!display_.init("JNEXT — ZX Spectrum Next Emulator",
                       NATIVE_W, NATIVE_H, DISPLAY_H)) return false;
    if (!audio_.init()) {
        Log::platform()->warn("Audio init failed — continuing without sound");
    }

    // Initialise the emulator with config (use set_config() before init(), or defaults).
    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
    if (!emulator_.init(cfg)) {
        Log::platform()->error("Emulator init failed");
        return false;
    }

    // Build the Kempston mouse host adapter now that emulator_ is initialised.
    mouse_dispatcher_ = std::make_unique<MouseDispatcher>(emulator_.mouse());
    input_.on_mouse = [this](const SDL_Event& e) {
        mouse_dispatcher_->handle_sdl_event(e);
    };

    // Build the joystick host adapter (G42 closure — JOY-WIRE-02/03/04).
    // SDL emits SDL_CONTROLLER* events only for SDL_GameControllers that
    // have been explicitly opened via SDL_GameControllerOpen — the
    // CONTROLLERDEVICEADDED handler below opens up to two devices and
    // routes them to slots 0 / 1.
    joystick_dispatcher_ = std::make_unique<JoystickDispatcher>(emulator_.joystick());
    input_.on_controller = [this](const SDL_Event& e) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            // e.cdevice.which is the device-index here (NOT the
            // instance-id). Open it, then resolve the instance-id and
            // map it to the first free connector slot.
            const int dev_idx = e.cdevice.which;
            if (SDL_IsGameController(dev_idx)) {
                int slot = -1;
                if (controllers_[0] == nullptr)      slot = 0;
                else if (controllers_[1] == nullptr) slot = 1;
                if (slot >= 0) {
                    controllers_[slot] = SDL_GameControllerOpen(dev_idx);
                    if (controllers_[slot]) {
                        SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[slot]);
                        if (js) {
                            const SDL_JoystickID iid = SDL_JoystickInstanceID(js);
                            joystick_dispatcher_->map_instance_to_slot(iid, slot);
                        }
                    }
                }
            }
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            // e.cdevice.which is the instance-id here.
            const SDL_JoystickID iid = e.cdevice.which;
            for (int s = 0; s < 2; ++s) {
                if (controllers_[s]) {
                    SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[s]);
                    if (js && SDL_JoystickInstanceID(js) == iid) {
                        joystick_dispatcher_->map_instance_to_slot(iid, -1);
                        SDL_GameControllerClose(controllers_[s]);
                        controllers_[s] = nullptr;
                        break;
                    }
                }
            }
        } else {
            joystick_dispatcher_->handle_sdl_event(e);
        }
    };

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
            // G152 host hotkeys (F1 hard reset / F4 soft reset / F9 MF NMI
            // / F10 DivMMC NMI). Mirror MainWindow keyPressEvent.
            if (sc == SDL_SCANCODE_F1)  { emulator_.on_hotkey_f1_hard_reset();   return; }
            if (sc == SDL_SCANCODE_F4)  { emulator_.on_hotkey_f4_soft_reset();   return; }
            if (sc == SDL_SCANCODE_F9)  { emulator_.on_hotkey_f9_mf_nmi();       return; }
            if (sc == SDL_SCANCODE_F10) { emulator_.on_hotkey_f10_divmmc_nmi();  return; }
            // G147 host hotkeys F3/F5/F6/F7/F8 → EmuFnKeys FSM. Side-effect
            // callbacks installed by Emulator (NR 0x05/0x07/0x09 toggles).
            if (sc == SDL_SCANCODE_F3)  { emulator_.emu_fnkeys().simulate_mf_fkey_press(3); return; }
            if (sc == SDL_SCANCODE_F5)  { emulator_.emu_fnkeys().simulate_mf_fkey_press(5); return; }
            if (sc == SDL_SCANCODE_F6)  { emulator_.emu_fnkeys().simulate_mf_fkey_press(6); return; }
            if (sc == SDL_SCANCODE_F7)  { emulator_.emu_fnkeys().simulate_mf_fkey_press(7); return; }
            if (sc == SDL_SCANCODE_F8)  { emulator_.emu_fnkeys().simulate_mf_fkey_press(8); return; }
        } else {
            // Consume release so host hotkeys never bleed into the ZX matrix.
            switch (sc) {
            case SDL_SCANCODE_F1: case SDL_SCANCODE_F2: case SDL_SCANCODE_F3:
            case SDL_SCANCODE_F4: case SDL_SCANCODE_F5: case SDL_SCANCODE_F6:
            case SDL_SCANCODE_F7: case SDL_SCANCODE_F8: case SDL_SCANCODE_F9:
            case SDL_SCANCODE_F10: case SDL_SCANCODE_F11:
                return;
            default:
                break;
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

void SdlApp::set_delayed_screenshot(const std::string& file, int delay_frames) {
    screenshot_file_ = file;
    screenshot_countdown_ = delay_frames;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} frame(s)",
                           file, delay_frames);
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
    // Close any open game-controllers (G42).
    for (int s = 0; s < 2; ++s) {
        if (controllers_[s]) {
            SDL_GameControllerClose(controllers_[s]);
            controllers_[s] = nullptr;
        }
    }
    audio_.shutdown();
    display_.shutdown();
    SDL_Quit();
}
