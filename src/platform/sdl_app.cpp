#include "sdl_app.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include <cctype>
#include <cmath>
#include <new>

bool SdlApp::init(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        Log::platform()->error("SDL_Init: {}", SDL_GetError());
        return false;
    }
    if (!display_.init("JNEXT — ZX Spectrum Next Emulator",
                       NATIVE_W, NATIVE_H, DISPLAY_H)) return false;
    // Task 47 (--silent): never open an SDL audio device. SdlAudio stays
    // un-initialized, so SdlAudio::queued_ms() returns -1 and
    // push_from_mixer() is a no-op — audio_pacing::frames_for_tick(-1)
    // falls back to wall-clock pacing (1 frame/tick), the same path taken
    // when audio init genuinely fails below.
    if (!(config_set_ && config_.silent)) {
        if (!audio_.init()) {
            Log::platform()->warn("Audio init failed — continuing without sound");
        }
    } else {
        Log::platform()->info("--silent: not opening an audio device");
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

    // Task 60c — after any state restore (rewind / snapshot load) re-seed both
    // host dispatchers' shadows from the restored canonical Joystick /
    // KempstonMouse, so the next live controller/mouse event does not push a
    // stale shadow back over the restore. Fired from Emulator::load_state.
    emulator_.on_input_state_restored = [this]() {
        if (mouse_dispatcher_)    mouse_dispatcher_->resync();
        if (joystick_dispatcher_) joystick_dispatcher_->resync();
    };

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

void SdlApp::cold_boot(const std::string& load_file) {
    Log::platform()->info("Cold boot (reconstruct + init), load_file='{}'",
                          load_file.empty() ? "(none)" : load_file.c_str());

    // Reconstruct the emulator in place: restores every power-on default so the
    // machine is byte-identical to a fresh startup. Placement-new keeps
    // &emulator_ stable; the host adapters below re-bind to the (stable-address)
    // sub-objects and the emulator-side callback.
    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
    cfg.load_file = load_file;
    emulator_.~Emulator();
    new (&emulator_) Emulator();
    emulator_.init(cfg);

    // Re-run the emulator-bound wiring init() does at startup.
    mouse_dispatcher_    = std::make_unique<MouseDispatcher>(emulator_.mouse());
    joystick_dispatcher_ = std::make_unique<JoystickDispatcher>(emulator_.joystick());
    emulator_.on_input_state_restored = [this]() {
        if (mouse_dispatcher_)    mouse_dispatcher_->resync();
        if (joystick_dispatcher_) joystick_dispatcher_->resync();
    };

    // Schedule the load exactly as the CLI startup does (same per-format delay).
    load_countdown_ = -1;
    if (!load_file.empty()) {
        std::string ext;
        auto dot = load_file.rfind('.');
        if (dot != std::string::npos) {
            ext = load_file.substr(dot);
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        load_file_      = load_file;
        load_countdown_ = (ext == ".tzx" || ext == ".wav") ? 100 : 0;
    }
}

void SdlApp::set_delayed_screenshot(const std::string& file, int delay_frames,
                                    uint8_t layer_mask) {
    screenshot_file_ = file;
    screenshot_countdown_ = delay_frames;
    screenshot_layers_ = layer_mask;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} frame(s) (layers: {})",
                           file, delay_frames,
                           Renderer::layer_mask_to_string(layer_mask));
}

void SdlApp::set_delayed_exit(int delay_frames) {
    exit_countdown_ = delay_frames;
    Log::platform()->info("--delayed-automatic-exit: will exit after {} frame(s)",
                           delay_frames);
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

        // Pace emulation against the sound card, not the wall clock: the 20 ms
        // frame delay runs the emulator at 50.00 frames/s while a 48K frame is
        // 1/50.08 s, and audio is synthesised on the emulated clock — so we feed
        // the device ~44030 samples per real second while it consumes 44100. The
        // deficit empties the audio queue and SDL pads the stream with zeros:
        // clicks, a few times a second, forever (issue #7 / Task 23). See
        // audio_pacing.h.
        // --delayed-screenshot-layers: arm the compositor layer mask for the
        // frame(s) rendered in this tick (the last one is what the screenshot
        // below saves), disarmed right after. Default LAYER_ALL = no-op. As in
        // QtApp, the mask lives on the Renderer the window also shows, so the
        // captured frame is displayed masked for that single tick.
        if (screenshot_countdown_ == 0)
            emulator_.renderer().set_layer_mask(screenshot_layers_);

        // Frames actually rendered this tick. frames_for_tick() returns 0 when
        // the audio queue is ahead of the card (audio_pacing.h:56), so this
        // loop can legitimately run zero times — and then the framebuffer still
        // holds the previous frame, composited with LAYER_ALL. Capturing that
        // would silently ignore the user's layer selection.
        int frames_rendered = 0;
        {
            const int frames = emulator_.fastload_active()
                                   ? 1
                                   : audio_pacing::frames_for_tick(audio_.queued_ms());
            for (int i = 0; i < frames; i++) {
                emulator_.run_frame();
                ++frames_rendered;
            }
        }

        // Task 70 — a hard reset (F1 / a program's NR 0x02 bit 1) is a power-on
        // cold boot done here between frames. cold_boot() reconstructs the
        // emulator, so restart the loop with the fresh machine.
        if (emulator_.take_hard_reset_request()) {
            cold_boot(std::string());
            continue;
        }

        // Task 19 fastload follow-up — when the phantom typist is
        // armed or a fast-load tape is in flight, skip pushing audio
        // samples to SDL. The emulator still synthesizes audio into
        // the mixer ring buffer (we drain it below by NOT pushing,
        // letting the buffer self-regulate via the `max_queued` cap
        // in SdlAudio::push_from_mixer); pushing samples while
        // running at 10× wall-clock would either back-pressure the
        // emulator or produce stuttering audio chunks. Mirrors
        // FUSE's `sound_pause()`/`sound_unpause()` around
        // fastloading windows (timer/timer.c).
        const bool fastload = emulator_.fastload_active();
        if (!fastload) {
            // SdlApp has no speed multiplier, so outside fastload it always
            // paces on the audio clock — the underrun hold is safe to enable.
            audio_.push_from_mixer(emulator_.mixer(), /*hold_on_underrun=*/true);
        }

        const uint32_t* fb = emulator_.get_framebuffer();
        const int fb_w = emulator_.get_framebuffer_width();
        const int fb_h = emulator_.get_framebuffer_height();
        display_.upload_frame(fb, fb_w, fb_h);
        display_.present();

        // Delayed screenshot: take after countdown expires.
        if (screenshot_countdown_ == 0) {
            if (frames_rendered > 0) {
                // A failed write means no PNG — same failure as never taking
                // the capture, so same contract as SdlApp::shutdown(): error +
                // non-zero exit. save_screenshot_png() has already logged WHY.
                if (!save_screenshot_png(screenshot_file_, fb, fb_w, fb_h)) {
                    Log::platform()->error(
                        "--delayed-screenshot: FAILED to write '{}' (layers: {}); "
                        "see the error above. Exiting non-zero.",
                        screenshot_file_,
                        Renderer::layer_mask_to_string(screenshot_layers_));
                    exit_code_ = 1;
                }
                emulator_.renderer().set_layer_mask(Renderer::LAYER_ALL);
                screenshot_countdown_ = -1;  // done
            } else {
                // No frame went through the compositor this tick (audio queue
                // ahead of the card). Hold the countdown at 0 and capture on
                // the next tick that actually renders, rather than writing a
                // stale frame with the wrong layers in it.
                emulator_.renderer().set_layer_mask(Renderer::LAYER_ALL);
            }
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

        // Frame pacing: target the emulated video refresh (~20 ms at 50 Hz,
        // ~17 ms at 60 Hz — issue #9; a 60 Hz demo runs at 60 fps, not a
        // hardcoded 50) EXCEPT during fastload — when the phantom typist is
        // armed or a fast-load tape is delivering data, skip the per-frame
        // sleep and immediately run the next frame. Mirrors FUSE
        // timer/timer.c:216 `timer_frame()` fast-path (no `sound_buffer_wait`,
        // no `timer_check`). The moment fastload_active() flips back to false
        // (typist fired AND tape at end), we re-engage pacing on the very next
        // iteration.
        if (!fastload) {
            const uint32_t frame_ms = static_cast<uint32_t>(
                std::lround(emulator_.frame_period_ms()));
            if (frame_ms != last_frame_ms_) {
                last_frame_ms_ = frame_ms;
                Log::platform()->info(
                    "frame pacing: {:.2f} Hz video refresh -> {} ms/frame",
                    1000.0 / emulator_.frame_period_ms(), frame_ms);
            }
            uint32_t elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < frame_ms) SDL_Delay(frame_ms - elapsed);
        }
    }
}

void SdlApp::shutdown() {
    // A requested screenshot that was never written is a failure, not a
    // footnote — same contract as QtApp::shutdown(). Reachable here if the
    // capture was still deferred (no frame rendered on its tick) when
    // --delayed-automatic-exit fired.
    if (screenshot_countdown_ >= 0 && !screenshot_file_.empty()) {
        Log::platform()->error(
            "--delayed-screenshot: NO screenshot was written to '{}' (layers: {}); "
            "the emulator exited with the capture still pending. Exiting non-zero.",
            screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_));
        exit_code_ = 1;
    }

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
