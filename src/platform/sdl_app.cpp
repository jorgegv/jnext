#include "sdl_app.h"
#include "platform/host_key_wiring.h"   // GH #268
#include "platform/emulator_boot.h"
#include "platform/cli_capture.h"
#include "platform/cli_delay.h"
#include "platform/auto_exit.h"
#include "platform/recording_info.h"   // GH #26 WP6
#include "platform/rzx_startup.h"
#include "platform/frame_sequencer.h"   // RENDER_INTERVAL_MS, shared with QtApp
#include "platform/render_policy.h"
#include "platform/pointer_capture.h"   // GH #307 release_chord
#include "core/emulator_config.h"
#include "core/log.h"
#include <cmath>

bool SdlApp::init(int argc, char* argv[]) {
    // SDL3 SDL_Init returns bool (true = success); SDL2 returned 0 on
    // success and a negative on failure, so the SENSE of this test is
    // inverted, not just the spelling.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        Log::platform()->error("SDL_Init: {}", SDL_GetError());
        return false;
    }
    // GH #311 test hook: JNEXT_TEST_VIRTUAL_JOYSTICKS attaches SDL virtual
    // joysticks (and hides physical ones) so the regression suite can drive
    // the real binary without hardware. Inert when the variable is unset.
    GamepadHost::attach_test_devices_from_env();
    if (!display_.init(NATIVE_W, NATIVE_H, DISPLAY_H)) return false;
    // Task 47 (--silent): never open an SDL audio device. SdlAudio stays
    // un-initialized, so SdlAudio::queued_ms() returns -1 and
    // push_from_mixer() is a no-op — audio_pacing::frames_for_tick(-1)
    // falls back to wall-clock pacing (1 frame/tick), the same path taken
    // when audio init genuinely fails below.
    if (!(config_set_ && config_.silent)) {
        if (!audio_.init()) {
            Log::platform()->warn("Audio init failed — continuing without sound");
        }
        // GH #155 — the band's estimate envelope is the opened device's buffer.
        pacing_band_.envelope_ms = audio_.pacing_envelope_ms();
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
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) set_mouse_captured(true);
            return;
        }
        mouse_dispatcher_->handle_sdl_event(e);
    };

    // Build the joystick host adapter (G42 closure — JOY-WIRE-02/03/04).
    // SDL emits SDL_EVENT_GAMEPAD_* events only for gamepads that
    // have been explicitly opened via SDL_OpenGamepad — the
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
    // GH #311 — the assigned controllers follow the sources (SDL-only: set by
    // --joy1-device / --joy2-device; there is no config file here).
    emulator_.on_joystick_device_changed = [this](int slot, const JoyDeviceRef& ref) {
        if (gamepad_host_) gamepad_host_->set_device(slot, ref);
    };
    emulator_.set_joystick_source(0, config_.joy_source[0]);
    emulator_.set_joystick_source(1, config_.joy_source[1]);
    emulator_.set_joystick_device(0, config_.joy_device[0]);
    emulator_.set_joystick_device(1, config_.joy_device[1]);
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

    // Route SDL key events into emulator keyboard matrix; intercept host
    // shortcuts. Both this and the focus-loss binding go in through ONE call
    // (GH #268): a frontend must not be able to end up with keys wired and the
    // "the keyboard went elsewhere" signal forgotten, which is the failure the
    // Qt side shipped with and no test saw. See platform/host_key_wiring.h.
    wire_host_keys(input_, key_router_, [this](SDL_Scancode sc, bool pressed) {
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
                if ((m & SDL_KMOD_CTRL) && (m & SDL_KMOD_ALT)) {
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
    });

    // GH #276 B4 — THE HOSTED DEBUGGER BACKEND (the B4 plan's §6.1), exactly as
    // in HeadlessApp::init(): built on the initialised machine, kept across every
    // cold boot, no client attached — so nothing is armed and the run is the run
    // without it. SES-04's `ExitNonZero`: the SDL frontend has no pause path at
    // all. (No `ExitRequested` listener yet — a listener needs an attached
    // client, which would arm every instruction; B4 report, M2 part 1, O5.)
    debugger_ = std::make_unique<jnext::dbg::Debugger>(emulator_);
    debugger_->set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
    jnext::dbg::LoopDriver driver;
    driver.cold_boot = [this]() {
        boot_machine(std::string());
        return true;   // emulator_frontend_cold_boot() returns void (SES-07)
    };
    driver.load = [this](const std::string& path) {
        return emulator_apply_load(emulator_, path, tape_realtime_);
    };
    debugger_->set_loop_driver(driver);
    host_probe_ = HostProbe::from_env(emulator_, *debugger_);   // GH #276 B5
    // GH #12 (WP-5) — the socket debugger servers, on this loop's pump.
    if (!debug_servers_.start(*debugger_, config_)) return false;
    // GH #26 WP5 — Alt+1..Alt+8 are the script host keys (host_key_wiring.h),
    // as in the Qt window: the backend exists now, the Router was bound above.
    wire_script_keys(key_router_, *debugger_);
    // GH #26 WP4 — the scripts, after the servers and before the machine runs:
    // a script that does not load is a startup failure (exit 1, §6.5).
    {
        jnext::script::ScriptHostOptions so;
        so.map_file = config_.map_file;
        so.scripts  = config_.script_files;
        so.keys     = config_.script_keys;
        so.record_file = config_.record_script_file;   // GH #26 WP6
        so.exits    = true;
        script_host_.set_recording_info([this]() { return recording_info_of(emulator_); });
        if (!script_host_.start(*debugger_, so)) return false;
    }

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

// GH #276 B4 — CTL-12 rule 5: a boot the loop owner decides on (F1, a guest
// NR 0x02 hard reset, a NEX load request) is bracketed by the hosted backend's
// begin/done, so its reconstruct contract runs as for a client's `reset(Hard)`.
void SdlApp::cold_boot(const std::string& load_file) {
    debugger_->on_cold_boot_begin();
    boot_machine(load_file);
    debugger_->on_cold_boot_done();
}

void SdlApp::boot_machine(const std::string& load_file) {
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
        emulator_.on_joystick_device_changed = [this](int slot, const JoyDeviceRef& ref) {
            if (gamepad_host_) gamepad_host_->set_device(slot, ref);
        };
        emulator_.set_joystick_source(0, cfg.joy_source[0]);
        emulator_.set_joystick_source(1, cfg.joy_source[1]);
        emulator_.set_joystick_device(0, cfg.joy_device[0]);
        emulator_.set_joystick_device(1, cfg.joy_device[1]);
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
    // A recording boots the machine it was made on, which stays selected for
    // later boots (emulator_cold_boot()).
    hooks.keep_machine = [this](MachineType type) { config_.type = type; };
    emulator_frontend_cold_boot(emulator_, config_set_ ? config_ : EmulatorConfig{},
                                load_file, hooks);
}

void SdlApp::set_delayed_screenshot(const std::string& file, int delay_frames,
                                    uint8_t layer_mask) {
    screenshot_file_ = file;
    screenshot_countdown_ = cli::Delay::frames(delay_frames);
    screenshot_layers_ = layer_mask;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} frame(s) (layers: {})",
                           file, delay_frames,
                           Renderer::layer_mask_to_string(layer_mask));
}

void SdlApp::set_delayed_screenshot_seconds(const std::string& file, int delay_seconds,
                                    uint8_t layer_mask) {
    screenshot_file_ = file;
    screenshot_countdown_ = cli::Delay::seconds(delay_seconds);
    screenshot_layers_ = layer_mask;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} emulated second(s) "
                           "(layers: {})",
                           file, delay_seconds,
                           Renderer::layer_mask_to_string(layer_mask));
}

void SdlApp::set_speed_percent(int percent) {
    double multiplier = percent / 100.0;
    if (multiplier < 0.1) multiplier = 0.1;
    if (multiplier > 10.0) multiplier = 10.0;
    speed_multiplier_ = multiplier;
    Log::platform()->info("Emulator speed: {}x", multiplier);
}

void SdlApp::set_delayed_exit(int delay_frames) {
    exit_countdown_ = cli::Delay::frames(delay_frames);
    Log::platform()->info("--delayed-automatic-exit: will exit after {} frame(s)",
                           delay_frames);
}

void SdlApp::set_delayed_exit_seconds(int delay_seconds) {
    exit_countdown_ = cli::Delay::seconds(delay_seconds);
    Log::platform()->info("--delayed-automatic-exit: will exit after {} emulated second(s)",
                           delay_seconds);
}

void SdlApp::run() {
    // Command-line RZX play/record — shared with the other two frontends, and
    // applied here in run() for the reason given at emulator_start_rzx().
    // The recording starts later, once the command-line load is in: see
    // emulator_start_rzx_record_when_loaded() in the loop.
    if (!emulator_start_rzx(emulator_, rzx_play_file_, ""))
        exit_code_ = 1;   // a failed RZX load exits non-zero (as headless)

    while (running_) {
        // SDL3's SDL_GetTicks() returns Uint64 (SDL2: Uint32). Only
        // differences are ever taken, so the width is carried through
        // deliberately rather than truncated back.
        uint64_t frame_start = SDL_GetTicks();

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
            if (!emulator_apply_load(emulator_, load_file_, tape_realtime_)) {
                Log::platform()->error("load: failed to load '{}'", load_file_);
                exit_code_ = 1;   // a failed load exits non-zero (as headless)
            }
            load_countdown_ = -1;  // done
        } else if (load_countdown_ > 0) {
            --load_countdown_;
        }

        // --rzx-record, once the load/inject above is in the machine.
        if (!emulator_start_rzx_record_when_loaded(
                emulator_, rzx_record_file_, rzx_record_started_,
                load_countdown_ >= 0 || inject_countdown_ >= 0))
            exit_code_ = 1;

        // Pace emulation against the sound card, not the wall clock: the 20 ms
        // frame delay runs the emulator at 50.00 frames/s while a 48K frame is
        // 1/50.08 s, and audio is synthesised on the emulated clock — so we feed
        // the device ~44030 samples per real second while it consumes 44100. The
        // deficit empties the audio queue and SDL pads the stream with zeros:
        // clicks, a few times a second, forever (issue #7 / Task 23). See
        // audio_pacing.h.
        // --delayed-screenshot: when the countdown comes due, hand the capture
        // to the backend (GH #276 B4, O2; platform/cli_capture.h). It arms the
        // --delayed-screenshot-layers mask now for the frame(s) rendered in this
        // tick, forces them to render, and this tick's pump writes the last of
        // them and takes the mask down — the frame, the mask and the file this
        // loop used to handle itself. As in QtApp, the mask lives on the
        // Renderer the window also shows, so the captured frame is displayed
        // masked for that tick. Queued once, however many ticks it waits.
        if (screenshot_countdown_.due() && !screenshot_queued_) {
            if (queue_cli_screenshot(debugger(), screenshot_file_, screenshot_layers_) ==
                jnext::dbg::Result::Ok)
                screenshot_queued_ = true;
            else
                screenshot_refused_ = true;   // reported below, as a failed write
        }

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
        // --speed away from 100% decouples emulated time from real time: the
        // machine's sample rate no longer matches the sound card's, so the
        // audio pacer cannot hold and the loop paces on the wall clock at the
        // scaled period instead (the sleep below) — exactly as QtApp does.
        const bool speed_scaled = (speed_multiplier_ != 1.0);
        const bool screenshot_due = screenshot_countdown_.due();
        // Above 100%, present at most every RENDER_INTERVAL_MS (QtApp's
        // compositor throttle): the frames in between are never seen, and the
        // renderer is vsynced, so presenting every one would cap the machine
        // at the display's refresh rate. A due screenshot always presents.
        bool present_this_tick = true;
        if (speed_multiplier_ > 1.0 && !screenshot_due) {
            const uint64_t now = SDL_GetTicks();
            if (now - last_present_ms_ < static_cast<uint64_t>(
                                             frame_sequencer::RENDER_INTERVAL_MS))
                present_this_tick = false;
            else
                last_present_ms_ = now;
        }
        {
            const audio_pacing::TickPlan plan =
                (emulator_.fastload_active() || speed_scaled)
                    ? audio_pacing::TickPlan{1, false}
                    : audio_pacing::plan_for(
                          audio_pacing::frames_for_tick(pacing_band_, audio_.queued_ms()),
                          when_slow_prefer_);
            const int frames = plan.frames;
            next_tick_asap   = plan.next_tick_asap;
            for (int i = 0; i < frames; i++) {
                // Superseded-composite skip (issue #9): the display presents
                // once, below, so only the tick's last frame composites
                // (render_policy.h). Fastload runs 1 frame per loop pass here
                // (its pacing comes from skipping the delay), so it always
                // composites; a due screenshot forces the whole tick.
                emulator_.set_render_enabled(
                    present_this_tick &&
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

        // GH #276 B4 — SES-03: the backend's service call, once per tick after
        // the frame batch and after the two cold-boot polls above (CTL-12's
        // ordering — see HeadlessApp::run(), which places it the same way).
        // GH #12 (WP-5) — T's budget: drain a paused remote's commands, never
        // block the tick (platform/debug_servers.h).
        pump_hint_ = debugger_->pump(
            DebugServers::frame_loop_budget(debugger_->state().paused, pump_hint_));

        // GH #26 WP4 — a script's verdict (§6.3), as HeadlessApp: the SDL
        // frontend has no pause, so a script stop is an exit (3).
        if (script_host_.exit_requested()) {
            const int code = script_host_.exit_code();
            Log::platform()->info("script requested exit {}", code);
            if (code != 0 || exit_code_ == 0) exit_code_ = code;
            running_ = false;
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
            // The underrun hold is a rescue for a device the loop keeps fed on
            // the audio clock; unpaced (--speed away from 100%) it would fire
            // every tick instead — QtApp gates it the same way.
            audio_.push_from_mixer(emulator_.mixer(), /*hold_on_underrun=*/!speed_scaled);
        }

        const uint32_t* fb = emulator_.get_framebuffer();
        const int fb_w = emulator_.get_framebuffer_width();
        const int fb_h = emulator_.get_framebuffer_height();
        if (present_this_tick) {
            display_.upload_frame(fb, fb_w, fb_h);
            display_.present();
        }

        // Delayed screenshot: the OUTCOME. This tick's pump (above) has written
        // the capture if a frame was rendered in it; flush_captures() — the
        // backend's exit bound for its CLIENT_NONE captures — says how it
        // ended (GH #276 B4, O2).
        if (screenshot_countdown_.due()) {
            if (screenshot_refused_ || frames_rendered > 0) {
                // A failed write means no file — same failure as never taking
                // the capture, so same contract as SdlApp::shutdown(): error +
                // non-zero exit. save_screenshot_*() has already logged WHY; the
                // format is the extension (GH #18).
                const bool ok = !screenshot_refused_ &&
                                debugger().flush_captures(jnext::dbg::CLIENT_NONE) ==
                                    jnext::dbg::Result::Ok;
                if (!ok) {
                    Log::platform()->error(
                        "--delayed-screenshot: FAILED to write '{}' (layers: {}); "
                        "see the error above. Exiting non-zero.",
                        screenshot_file_,
                        Renderer::layer_mask_to_string(screenshot_layers_));
                    exit_code_ = 1;
                }
                screenshot_countdown_.disarm();  // done
                screenshot_queued_    = false;
                screenshot_refused_   = false;
            }
            // else: no frame went through the compositor this tick (audio queue
            // ahead of the card). The countdown holds at 0 and the capture stays
            // queued — the backend keeps its mask armed and takes it at the next
            // tick that renders, never a stale frame with the wrong layers.
        } else if (screenshot_countdown_.pending()) {
            screenshot_countdown_.tick(emulator_.video_timing().refresh_60hz());
        }

        // Delayed automatic exit. Deferred command-line work it cuts off
        // fails the run (platform/auto_exit.h).
        if (exit_countdown_.due()) {
            Log::platform()->info("automatic exit triggered");
            if (!auto_exit_finds_no_deferred_work(emulator_, {
                    {"--load", load_file_, load_countdown_ >= 0},
                    {"--inject", inject_file_, inject_countdown_ >= 0},
                    {"--rzx-record", rzx_record_file_,
                     !rzx_record_file_.empty() && !rzx_record_started_},
                }))
                exit_code_ = 1;
            if (const std::size_t n = script_host_.unreached_verdicts()) {   // GH #26 WP4 (§7.3)
                Log::platform()->error("SCRIPT: {} deferred actions never ran — exiting 3", n);
                if (exit_code_ == 0) exit_code_ = 3;
            }
            running_ = false;
        } else if (exit_countdown_.pending()) {
            exit_countdown_.tick(emulator_.video_timing().refresh_60hz());
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
            // --speed scales the period (QtApp::effective_frame_period_us()).
            const uint32_t frame_ms = static_cast<uint32_t>(
                std::lround(emulator_.frame_period_ms() / speed_multiplier_));
            if (frame_ms != last_frame_ms_) {
                last_frame_ms_ = frame_ms;
                Log::platform()->info(
                    "frame pacing: {:.2f} Hz video refresh at {}x -> {} ms/frame",
                    1000.0 / emulator_.frame_period_ms(), speed_multiplier_, frame_ms);
            }
            uint64_t elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < frame_ms) SDL_Delay(static_cast<Uint32>(frame_ms - elapsed));
        }
    }
}

void SdlApp::shutdown() {
    // A requested screenshot that was never written is a failure, not a
    // footnote — same contract as QtApp::shutdown(). Reachable here if the
    // capture was still deferred (no frame rendered on its tick) when
    // --delayed-automatic-exit fired.
    if (screenshot_countdown_.armed() && !screenshot_file_.empty()) {
        // GH #276 B4 — a capture already handed to the backend is dropped at
        // this exit bound (flush_captures() → NoFrame), so it cannot land after
        // the verdict.
        if (screenshot_queued_ && debugger_) debugger_->flush_captures(jnext::dbg::CLIENT_NONE);
        Log::platform()->error(
            "--delayed-screenshot: NO screenshot was written to '{}' (layers: {}); "
            "the emulator exited with the capture still pending. Exiting non-zero.",
            screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_));
        exit_code_ = 1;
    }

    // Stop RZX recording if active (writes the file). A command-line recording
    // that did not reach the disk exits non-zero.
    if (!emulator_finish_rzx(emulator_, rzx_record_file_)) exit_code_ = 1;

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
//
// SDL3 made relative mode PER WINDOW (SDL_SetWindowRelativeMouseMode) and
// returns true on success where SDL2's global SDL_SetRelativeMouseMode
// returned 0 — both the argument list and the success sense changed.
// ---------------------------------------------------------------------------
void SdlApp::set_mouse_captured(bool on)
{
    if (on == mouse_captured_) return;
    if (!SDL_SetWindowRelativeMouseMode(display_.window(), on)) {
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
    // SDL's KMOD_CTRL is the physical Control key on every platform (GH #307).
    Log::platform()->info("Mouse {} ({})", on ? "captured" : "released",
                          on ? std::string(pointer_capture::release_chord(false)) + " to release"
                             : std::string("click the window to capture"));
}
