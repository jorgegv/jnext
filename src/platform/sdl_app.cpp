#include "sdl_app.h"
#include "platform/emulator_boot.h"
#include "platform/render_policy.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include <cmath>

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
        // Uncaptured the pointer belongs to the desktop; clicking the window
        // is how you hand it to the guest (same gesture as the Qt frontend).
        if (!mouse_captured_) {
            if (e.type == SDL_MOUSEBUTTONDOWN) set_mouse_captured(true);
            return;
        }
        mouse_dispatcher_->handle_sdl_event(e);
    };

    // Build the joystick host adapter (G42 closure — JOY-WIRE-02/03/04).
    // SDL emits SDL_CONTROLLER* events only for SDL_GameControllers that
    // have been explicitly opened via SDL_GameControllerOpen — the
    // CONTROLLERDEVICEADDED handler below opens up to two devices and
    // routes them to slots 0 / 1.
    gamepad_host_ = std::make_unique<GamepadHost>(emulator_.joystick());

    // Task 60c — after any state restore (rewind / snapshot load) re-seed both
    // host dispatchers' shadows from the restored canonical Joystick /
    // KempstonMouse, so the next live controller/mouse event does not push a
    // stale shadow back over the restore. Fired from Emulator::load_state.
    emulator_.on_input_state_restored = [this]() {
        if (mouse_dispatcher_) mouse_dispatcher_->resync();
        if (gamepad_host_)     gamepad_host_->resync();
    };

    // Task 79 — cursor-keys-as-joystick + per-connector source wiring. The
    // Keyboard routes arrows/Space into the dispatcher when a connector is
    // cursor-key-sourced; the Emulator pushes any source change to the
    // dispatcher's per-slot gate. refresh_joystick_sources() applies the
    // CLI-resolved sources (config_.joy_source) once everything is wired.
    emulator_.keyboard().set_joystick_dispatcher(&gamepad_host_->dispatcher());
    emulator_.on_joystick_source_changed = [this](int slot, JoySource src) {
        if (gamepad_host_) gamepad_host_->set_source(slot, src);
    };
    emulator_.set_joystick_source(0, config_.joy_source[0]);
    emulator_.set_joystick_source(1, config_.joy_source[1]);
    emulator_.refresh_joystick_sources();

    // Task 83 — adopt pads already plugged in (SDL only emits DEVICEADDED for
    // later arrivals). Runs after the sources are applied so a CursorKeys
    // connector is skipped.
    gamepad_host_->enumerate_existing_devices();

    input_.on_controller = [this](const SDL_Event& e) {
        if (gamepad_host_) gamepad_host_->handle_event(e);
    };

    input_.on_quit = [this]() { running_ = false; };

    // Issue #122 — bind the key router to this machine's Keyboard BEFORE the
    // callback below can fire (SDL_PollEvent only runs inside run(), so the
    // window is empty in practice; ordering it this way keeps it empty by
    // construction). Also re-run on every cold boot — see cold_boot() — which
    // is what attach()'s latch reset is for: a release still held back refers
    // to a key of a machine that no longer exists (rows RT-08a/b/c, RT-09a/b).
    key_router_.attach(emulator_.keyboard());

    // Route SDL key events into emulator keyboard matrix; intercept host shortcuts.
    input_.on_key  = [this](SDL_Scancode sc, bool pressed) {
        if (pressed) {
            // Ctrl+Alt releases the pointer — the VirtualBox/VMware
            // convention, and the same combo the Qt frontend uses. It is
            // checked on the modifier keys themselves rather than bound to a
            // letter: every plain Ctrl+<key> is a real ZX sequence (Ctrl is
            // Symbol Shift — issue #115), so a letter shortcut would steal it
            // from the guest.
            if (mouse_captured_ &&
                (sc == SDL_SCANCODE_LALT  || sc == SDL_SCANCODE_RALT ||
                 sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL)) {
                const SDL_Keymod m = SDL_GetModState();
                if ((m & KMOD_CTRL) && (m & KMOD_ALT)) {
                    set_mouse_captured(false);
                    // Not consumed — see the Qt handler: swallowing the Alt
                    // key-down would desync Keyboard's host Alt-modifier state.
                }
            }
            if (sc == SDL_SCANCODE_F11) {
                display_.toggle_fullscreen();
                return;
            }
            // Task 77 — Esc is the ZX BREAK key (Caps Shift + Space) and is
            // NO LONGER a fullscreen toggle; F11 above is the only one.
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
        // Issue #122 — through the issue-#120 minimum-hold latch, NOT straight
        // into the matrix. SDL_PollEvent() and the frame loop are the same
        // run() iteration, so a press and its release delivered in one poll
        // batch used to set the matrix bit and clear it again with no
        // run_frame() in between: the guest never saw the key. A press is still
        // applied at once; only a release that no frame has had the chance to
        // see is held back, and on_tick_end() in run() discharges it.
        //
        // The dispatch itself lives in host_key_latch::Router (unit-tested
        // there, rows RT-*): this callback must stay a plain forward, exactly
        // as in QtApp, so the logic the guest depends on is not once again
        // reachable only through a live SDL window.
        key_router_.on_host_key(sc, pressed);
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

    // Issue #40 — the sequence itself lives in platform/emulator_boot.h and is
    // shared with QtApp; this supplies only the SDL-specific steps. Placement-
    // new keeps &emulator_ stable; the host adapters below re-bind to the
    // (stable-address) sub-objects and the emulator-side callbacks.
    ColdBootHooks hooks;
    hooks.rewire_host = [this](const EmulatorConfig& cfg) {
        // Re-run the emulator-bound wiring init() does at startup.
        mouse_dispatcher_ = std::make_unique<MouseDispatcher>(emulator_.mouse());
        gamepad_host_     = std::make_unique<GamepadHost>(emulator_.joystick());
        // Issue #122 — re-bind the key router to the reconstructed Keyboard,
        // mirroring QtApp::wire_gamepad_and_sources. attach() also clears any
        // release the latch is holding: it names a key of the machine that has
        // just been destroyed, and the new Keyboard starts all-released.
        key_router_.attach(emulator_.keyboard());
        emulator_.on_input_state_restored = [this]() {
            if (mouse_dispatcher_) mouse_dispatcher_->resync();
            if (gamepad_host_)     gamepad_host_->resync();
        };
        // Task 79 — re-apply the per-connector source wiring (the reconstructed
        // Emulator/Keyboard start at defaults).
        emulator_.keyboard().set_joystick_dispatcher(&gamepad_host_->dispatcher());
        emulator_.on_joystick_source_changed = [this](int slot, JoySource src) {
            if (gamepad_host_) gamepad_host_->set_source(slot, src);
        };
        emulator_.set_joystick_source(0, cfg.joy_source[0]);
        emulator_.set_joystick_source(1, cfg.joy_source[1]);
        emulator_.refresh_joystick_sources();

        // Task 83 — adopt pads already plugged in (SDL only emits DEVICEADDED
        // for later arrivals). Runs after the sources are applied so a
        // CursorKeys connector is skipped.
        gamepad_host_->enumerate_existing_devices();
    };
    hooks.cancel_pending_work = [this]() {
        inject_countdown_ = -1;
        load_countdown_   = -1;
    };
    hooks.schedule_load = [this](const std::string& file, int delay_frames) {
        set_pending_load(file, delay_frames);
    };
    emulator_frontend_cold_boot(emulator_, config_set_ ? config_ : EmulatorConfig{},
                                load_file, hooks);
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
            // Shared format dispatch (incl. .rzx) — see platform/emulator_boot.h.
            // SDL has no --tape-realtime toggle; fast tape load (false).
            if (!emulator_apply_load(emulator_, load_file_, /*tape_realtime=*/false)) {
                Log::platform()->error("load: failed to load '{}'", load_file_);
            }
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
        // Issue #35 — a WhenSlowPrefer::Video catch-up runs one frame here and
        // skips the end-of-iteration sleep below, so the second frame the sound
        // card wants gets its own iteration (and its own present) instead of
        // being superseded inside this one.
        bool next_tick_asap = false;
        {
            const audio_pacing::TickPlan plan =
                emulator_.fastload_active()
                    ? audio_pacing::TickPlan{1, false}
                    : audio_pacing::plan_for(
                          audio_pacing::frames_for_tick(pacing_band_, audio_.queued_ms()),
                          when_slow_prefer_);
            const int frames = plan.frames;
            next_tick_asap   = plan.next_tick_asap;
            const bool screenshot_due = (screenshot_countdown_ == 0);
            for (int i = 0; i < frames; i++) {
                // Superseded-composite skip (issue #9): the display presents
                // once, below, so only the tick's last frame composites
                // (render_policy.h). Fastload runs 1 frame per loop pass here
                // (its pacing comes from skipping the delay), so it always
                // composites; a due screenshot forces the whole tick.
                emulator_.set_render_enabled(
                    render_policy::composite_frame_in_tick(i, frames,
                                                           screenshot_due));
                emulator_.run_frame();
                ++frames_rendered;
            }
            // The hint is per-frame within this tick; restore the default.
            emulator_.set_render_enabled(true);
        }

        // Issue #122 — end of the tick's frames: any key whose release was held
        // back has now been seen by the guest and may come up. The gate on
        // frames_rendered lives in Router::on_tick_end and is pinned by rows
        // RT-04a/b/c: a tick the audio pacer gave zero frames emulated nothing,
        // so the hold has not been honoured and must not be discharged.
        //
        // Placed HERE, not after present() where QtApp's equivalent sits, for
        // one structural reason: SDL checks its cold-boot requests AFTER the
        // frames where Qt checks them before (frame_sequencer.h:402-403), and
        // those checks `continue`. Discharging further down would therefore be
        // skipped on a tick that did emulate frames. The outcome is the same
        // either way — cold_boot() re-attaches, which resets the latch — but
        // this placement keeps "frames ran => the hold is discharged" true
        // unconditionally instead of true-by-consequence.
        key_router_.on_tick_end(frames_rendered);

        if (std::string load_file = emulator_.take_nex_load_request(); !load_file.empty()) {
            cold_boot(load_file);
            continue;
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
        //
        // The same sleep is skipped for a single iteration when the audio pacer
        // asked to catch up and the user prefers video (issue #35): the extra
        // frame runs in the NEXT iteration, where it is composited and shown,
        // rather than inside this one, where it would be superseded.
        if (!fastload && !next_tick_asap) {
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

    // Close any open game-controllers (G42): GamepadHost owns their lifecycle
    // now (Task 79), so destroying it here (before SDL_Quit) closes them.
    gamepad_host_.reset();
    audio_.shutdown();
    display_.shutdown();
    SDL_Quit();
}

// ---------------------------------------------------------------------------
// Pointer capture (issue #37).
//
// SDL relative mode hides the cursor, confines it to the window and reports
// unbounded xrel/yrel — exactly what a relative Kempston mouse needs, and what
// MainWindow has to emulate by warping. Guard the call so a failure (some
// platforms/back-ends refuse) leaves the flag false rather than pretending the
// pointer is captured while it is still free.
// ---------------------------------------------------------------------------
void SdlApp::set_mouse_captured(bool on)
{
    if (on == mouse_captured_) return;
    if (SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE) != 0) {
        // Enable failing means the pointer is NOT captured, so leave the flag
        // false rather than claim it. Disable failing is worse — SDL still
        // holds the pointer — but clearing the flag anyway at least stops
        // feeding the guest, and says so loudly.
        if (on) {
            Log::platform()->warn("Mouse capture failed: {}", SDL_GetError());
            return;
        }
        Log::platform()->error("Mouse release failed, pointer may stay grabbed: {}",
                               SDL_GetError());
    }
    mouse_captured_ = on;
    if (!on && mouse_dispatcher_) {
        // Same stuck-button hazard as the Qt frontend: buttons are only
        // forwarded while captured, so one held as capture ends would never
        // have its release delivered.
        mouse_dispatcher_->reset();
    }
    Log::platform()->info("Mouse {} ({})", on ? "captured" : "released",
                          on ? "Ctrl+Alt to release" : "click the window to capture");
}
