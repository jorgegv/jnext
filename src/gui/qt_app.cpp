#include "gui/qt_app.h"
#include "gui/main_window.h"
#include "gui/emulator_widget.h"
#include "platform/emulator_boot.h"
#include "platform/sdl_audio.h"
#include "core/log.h"
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_manager.h"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <new>
#include <QApplication>
#include <QTimer>
#include <SDL2/SDL.h>

QtApp::QtApp() = default;
QtApp::~QtApp() = default;

namespace {
/// Host monotonic clock, integer microseconds (the frame-deadline domain).
int64_t steady_now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

int64_t QtApp::effective_frame_period_us() const
{
    // Exact emulated frame period scaled by the user speed multiplier, in
    // integer microseconds (quantisation <= 0.5 us/frame, < 0.003% — noise
    // next to the 1.1-1.3% whole-ms rounding error this replaces).
    return static_cast<int64_t>(
        std::llround(emulator_.frame_period_ms() * 1000.0 / speed_multiplier_));
}

void QtApp::log_frame_pacing(int64_t period_us)
{
    // Rate-change info line — issue-#9 reporters are instructed to look for
    // this. Emitted ONLY when the effective period changes (refresh-rate
    // switch via NR 0x05 bit 2, or a speed change), never per tick.
    if (period_us == last_paced_period_us_) return;
    last_paced_period_us_ = period_us;
    Log::platform()->info(
        "frame pacing: {:.2f} Hz video refresh -> {:.3f} ms deadline scheduler",
        1000.0 / emulator_.frame_period_ms(),
        static_cast<double>(period_us) / 1000.0);
}

void QtApp::rebase_frame_timer()
{
    if (!frame_timer_) return;
    // Explicit re-anchor (timer start, cold boot, speed change): any old
    // deadline is meaningless, so restart the schedule at now + period.
    // setInterval on a live timer restarts its period from the moment of the
    // call — exactly what anchoring at `now` requires.
    const int64_t period_us = effective_frame_period_us();
    frame_timer_->setInterval(frame_deadline_.rebase(steady_now_us(), period_us));
    log_frame_pacing(period_us);
}

void QtApp::reschedule_frame_timer()
{
    if (!frame_timer_) return;
    // Advance the absolute deadline by exactly one (fractional) period and
    // point the repeating timer at it. Computed against NOW — i.e. at the
    // END of the tick's work — because setInterval restarts the timer's
    // period from the moment of the call; the returned interval therefore
    // alternates (17/18 ms for the 17.198 ms Next-60 period) and the
    // long-run rate is exact (issue #9: the old fixed round() interval ran
    // 1.1-1.3% fast). Passing the period each call makes a runtime 50/60 Hz
    // switch (NR 0x05 bit 2) or speed change glide in with no extra path.
    // Only touch the timer when the value differs: setInterval with an
    // unchanged value would still restart the period from now — harmless,
    // but pointless churn; when the value differs (most ticks, by design)
    // the restart-from-now is precisely the semantic the deadline math
    // assumes.
    const int64_t period_us = effective_frame_period_us();
    const int interval = frame_deadline_.next_interval_ms(steady_now_us(), period_us);
    if (interval != frame_timer_->interval()) {
        frame_timer_->setInterval(interval);
    }
    log_frame_pacing(period_us);
}

void QtApp::set_pending_inject(const std::string& file, uint16_t org,
                               uint16_t pc, int delay_frames) {
    inject_file_ = file;
    inject_org_  = org;
    inject_pc_   = pc;
    inject_countdown_ = delay_frames;
    Log::platform()->info("--inject: will load '{}' at {:#06x} (PC={:#06x}) after {} frame(s)",
                           file, org, pc, delay_frames);
}

void QtApp::set_pending_load(const std::string& file, int delay_frames) {
    load_file_ = file;
    load_countdown_ = delay_frames;
    Log::platform()->info("--load: will load '{}' after {} frame(s)", file, delay_frames);
}

void QtApp::set_delayed_screenshot(const std::string& file, int delay_frames,
                                   uint8_t layer_mask) {
    screenshot_file_ = file;
    screenshot_countdown_ = delay_frames;
    screenshot_layers_ = layer_mask;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} frame(s) (layers: {})",
                           file, delay_frames,
                           Renderer::layer_mask_to_string(layer_mask));
}

void QtApp::set_speed_multiplier(double multiplier) {
    if (multiplier < 0.1) multiplier = 0.1;
    if (multiplier > 10.0) multiplier = 10.0;
    speed_multiplier_ = multiplier;

    // Re-anchor the fractional deadline schedule at the new effective period
    // (video-refresh period / speed — issue #9). A speed change is a
    // deliberate restart, so rebase rather than glide; the per-tick
    // reschedule in on_frame_tick() then keeps the exact rate, and also
    // picks up runtime 50/60 Hz switches without another speed change.
    rebase_frame_timer();
    Log::platform()->info("Emulator speed: {}x", multiplier);
}

void QtApp::set_delayed_exit(int delay_frames) {
    exit_countdown_ = delay_frames;
    Log::platform()->info("--delayed-automatic-exit: will exit after {} frame(s)",
                           delay_frames);
}

void QtApp::wire_gamepad_and_sources(const EmulatorConfig& cfg) {
    // (Re)create the SDL gamepad host bound to the (possibly reconstructed)
    // emulator's Joystick, then wire the Task 79 input plumbing:
    //   - Keyboard routes arrows/Space into the dispatcher for a cursor-key
    //     connector;
    //   - the Emulator pushes any source change to the dispatcher's per-slot
    //     gate via on_joystick_source_changed;
    //   - on_input_state_restored re-seeds the dispatcher shadow after rewind.
    // refresh_joystick_sources() then applies the CLI/config-resolved sources.
    gamepad_host_ = std::make_unique<GamepadHost>(emulator_.joystick());
    emulator_.keyboard().set_joystick_dispatcher(&gamepad_host_->dispatcher());
    emulator_.on_joystick_source_changed = [this](int slot, JoySource src) {
        if (gamepad_host_) gamepad_host_->set_source(slot, src);
    };
    // Single restore hook fans out to BOTH host input owners: the gamepad host
    // (this class) and the window's Kempston mouse dispatcher (MainWindow).
    emulator_.on_input_state_restored = [this]() {
        if (gamepad_host_) gamepad_host_->resync();
        if (main_window_)  main_window_->resync_input_dispatchers();
    };
    emulator_.set_joystick_source(0, cfg.joy_source[0]);
    emulator_.set_joystick_source(1, cfg.joy_source[1]);
    emulator_.refresh_joystick_sources();
    if (main_window_) main_window_->sync_joy_source_menu();

    // Task 83 — adopt pads that are ALREADY plugged in. SDL only emits
    // DEVICEADDED for arrivals after the subsystem is up, so without this a
    // host built while a pad is connected never sees it. That is why the pad
    // died on every cold boot (which rebuilds this host): issue #13 point 3.
    // Runs last, so the per-connector sources above are already applied and a
    // CursorKeys connector is correctly skipped.
    gamepad_host_->enumerate_existing_devices();
}

bool QtApp::init(int argc, char* argv[]) {
    // Initialize SDL for audio + gamepads (no video, no window). Task 79 adds
    // SDL_INIT_GAMECONTROLLER so autodetected pads can drive the two Next
    // joystick connectors in the GUI; controller events are polled each frame
    // in on_frame_tick() (Qt owns the event loop, so SDL never runs its own).
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        Log::platform()->error("SDL_Init(AUDIO|GAMECONTROLLER): {}", SDL_GetError());
        return false;
    }

    // Create QApplication (must exist before any QWidget).
    // Note: Hi-DPI scaling is left enabled (Qt6 default on Wayland).
    // EmulatorWidget accounts for devicePixelRatio itself.
    //
    // QApplication takes `int &argc` and keeps that reference for its whole
    // life, so it must NOT be bound to init()'s parameter (dead once init()
    // returns). QCoreApplication::arguments() then reads the stale slot and
    // walks argv past its end. The only caller on that path is
    // QXcbSessionManager, so this crashed on X11 sessions with a session
    // manager (issue #8) and never on Wayland or headless.
    qt_argc_ = argc;
    qt_argv_ = argv;
    qapp_ = new QApplication(qt_argc_, qt_argv_);

    // Initialize the emulator core.
    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};

    // Task 47 (--silent): never open an SDL audio device. audio_ stays
    // null, which every downstream `audio_ &&` guard in on_frame_tick()
    // already treats identically to "audio init failed" — the frame timer
    // falls back to wall-clock pacing (audio_pacing::frames_for_tick(-1)
    // returns 1 via the `audio_paced` gate below), same as any host with no
    // audio backend at all.
    if (!cfg.silent) {
        audio_ = std::make_unique<SdlAudio>();
        if (!audio_->init()) {
            Log::platform()->warn("Audio init failed - continuing without sound");
        }
    } else {
        Log::platform()->info("--silent: not opening an audio device");
    }
    if (!emulator_.init(cfg)) {
        Log::platform()->error("Emulator init failed");
        return false;
    }

    // Create the main window.
    main_window_ = new MainWindow();

    // Wire emulator pointer so menus can call into it.
    main_window_->set_emulator(&emulator_);

    // Route keyboard events from the Qt window to the emulator keyboard matrix.
    main_window_->set_key_callback([this](SDL_Scancode sc, bool pressed) {
        emulator_.keyboard().set_key(sc, pressed);
    });

    // Task 79 — SDL gamepad host + per-connector input-source wiring.
    wire_gamepad_and_sources(cfg);

    // Route emulator speed changes from the menu to the frame timer.
    main_window_->set_speed_callback([this](double multiplier) {
        set_speed_multiplier(multiplier);
    });

    // Task 70 — route menu file-loads through a full cold boot (reconstruct +
    // init as if launched with --load <file>).
    main_window_->set_load_file_callback([this](const std::string& file) {
        cold_boot(file);
    });

    main_window_->show();

    // Re-apply scale after the window is mapped so devicePixelRatio is correct.
    // The 100ms delay ensures the window manager has finished placing the window
    // and the real DPR is available (critical on Wayland/Hi-DPI).
    QTimer::singleShot(100, main_window_, [this]() {
        main_window_->set_scale(main_window_->current_scale());
    });

    // Drive emulator frames from a repeating timer paced by the fractional
    // deadline scheduler (issue #9): the interval alternates (e.g. 20/21 ms
    // for the 20.259 ms 50 Hz period) so the long-run rate is the EXACT
    // emulated refresh, not its whole-ms rounding. on_frame_tick() advances
    // the schedule at the end of every tick.
    frame_timer_ = new QTimer(main_window_);
    // Qt::PreciseTimer is REQUIRED: the default CoarseTimer may fire within
    // 5% of the interval (~0.9 ms at 17 ms), which would swallow the
    // 17/18 ms alternation the deadline scheduler relies on.
    frame_timer_->setTimerType(Qt::PreciseTimer);
    QObject::connect(frame_timer_, &QTimer::timeout, [this]() { on_frame_tick(); });
    rebase_frame_timer();     // anchor the schedule + set the first interval
    frame_timer_->start();    // no argument: uses the interval just set

    // Set up a 1-second timer for status bar updates (FPS counter).
    status_timer_ = new QTimer(main_window_);
    QObject::connect(status_timer_, &QTimer::timeout, [this]() { on_status_tick(); });
    // Task 63 — seed the cadence window's start NOW, not on the first tick.
    // Without this the first report covers "since epoch 0" and printed the
    // nonsense `over 0ms`; with it the first window is a genuine measurement
    // from timer start to first tick.
    last_status_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    status_timer_->start(1000);

    // Apply RZX play/record if requested via CLI.
    if (!rzx_play_file_.empty()) {
        emulator_.load_rzx(rzx_play_file_);
    }
    if (!rzx_record_file_.empty()) {
        emulator_.start_rzx_recording(rzx_record_file_);
    }

    return true;
}

int QtApp::run() {
    if (!qapp_) return 1;
    return qapp_->exec();
}

void QtApp::shutdown() {
    // A screenshot that was asked for and never taken is a FAILURE, not a
    // footnote. The capture is deferred while no frame is rendering (debugger
    // paused), but --delayed-automatic-exit is a hard bound and must stay one,
    // so exit can arrive with the capture still outstanding. Saying nothing and
    // returning 0 would let a script mistake "no PNG" for success — the exact
    // trap the old unconditional write avoided by emitting a stale frame.
    // Per src/core/log.h, error = "the user asked for something and did not get
    // it". That is precisely this, so: error + non-zero exit.
    if (screenshot_countdown_ >= 0 && !screenshot_file_.empty()) {
        Log::platform()->error(
            "--delayed-screenshot: NO screenshot was written to '{}' (layers: {}). "
            "The capture came due while the debugger was paused, so no frame was "
            "ever rendered for it, and the emulator exited first. Exiting non-zero.",
            screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_));
        exit_code_ = 1;
    }

    // Stop RZX recording if active (writes the file).
    if (emulator_.rzx_recorder().is_recording()) {
        emulator_.stop_rzx_recording();
    }

    if (frame_timer_) {
        frame_timer_->stop();
    }
    if (status_timer_) {
        status_timer_->stop();
    }

    // Task 79 — close SDL_GameControllers before SDL_Quit().
    gamepad_host_.reset();
    audio_.reset();
    SDL_Quit();

    delete main_window_;
    main_window_ = nullptr;

    delete qapp_;
    qapp_ = nullptr;
}

void QtApp::cold_boot(const std::string& load_file) {
    Log::platform()->info("Cold boot (reconstruct + init), load_file='{}'",
                          load_file.empty() ? "(none)" : load_file.c_str());

    // Reconstruct the emulator in place (restores every power-on default so
    // the machine is byte-identical to a fresh startup) and re-run init(),
    // preserving debugger breakpoints. Placement-new keeps &emulator_ stable,
    // so main_window_'s pointer, the debugger, and the mouse dispatcher (all
    // bound to the stable address / its sub-objects) stay valid; only the
    // emulator-side callback needs re-binding below.
    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
    cfg.load_file = load_file;   // empty => clean NextZXOS boot
    // Task 79 — the per-connector input source is a host-side mapping, not
    // machine state, so it must survive a cold boot (Reset / F1 / NEX load)
    // including any live change made via the Input menu. Carry the emulator's
    // CURRENT effective sources through the reconstruction rather than the
    // startup config's.
    cfg.joy_source[0] = emulator_.joystick_source(0);
    cfg.joy_source[1] = emulator_.joystick_source(1);
    emulator_cold_boot(emulator_, cfg);

    // Re-run the post-init wiring init() does at startup: the reconstructed
    // emulator/keyboard start at defaults, so rebind the window pointer and
    // re-create + re-wire the gamepad host and per-connector input sources.
    if (main_window_) main_window_->set_emulator(&emulator_);
    wire_gamepad_and_sources(cfg);

    // Drop any stale pending work, then schedule the load exactly as the CLI
    // startup does (same per-format delay) so a menu load == launching with
    // --load <file>.
    inject_countdown_ = -1;
    load_countdown_   = -1;
    if (!load_file.empty()) {
        set_pending_load(load_file, emulator_load_delay_frames(load_file));
    }

    // Re-anchor the deadline schedule to the (possibly changed) refresh
    // rate — a cold boot is a restart, so rebase rather than glide.
    rebase_frame_timer();
}

void QtApp::on_frame_tick() {
    // Task 79 — Qt owns the event loop, so SDL never runs its own; drain the
    // SDL event queue each frame and feed controller add/remove/button/axis
    // events to the gamepad host (all other SDL events are ignored — SDL is
    // audio + gamepads only in the GUI build).
    if (gamepad_host_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            gamepad_host_->handle_event(e);
        }
    }

    // M_EXECCMD requests use the same cold-boot path as menu loads.
    if (std::string load_file = emulator_.take_nex_load_request(); !load_file.empty()) {
        cold_boot(load_file);
        return;
    }

    // Task 70 — a hard reset (Reset button / F1 / a program's NR 0x02 bit 1)
    // is a power-on cold boot and cannot run inside run_frame(); it is
    // performed here between frames. cold_boot() reconstructs the emulator, so
    // return immediately and let the next tick run the fresh machine.
    if (emulator_.take_hard_reset_request()) {
        cold_boot();
        return;
    }

    // Apply pending inject when countdown reaches zero.
    if (inject_countdown_ == 0) {
        emulator_.inject_binary(inject_file_, inject_org_, inject_pc_);
        inject_countdown_ = -1;
    } else if (inject_countdown_ > 0) {
        --inject_countdown_;
    }

    // Apply pending load when countdown reaches zero (auto-detect format).
    if (load_countdown_ == 0) {
        // Shared format dispatch (incl. .rzx) — see platform/emulator_boot.h.
        if (!emulator_apply_load(emulator_, load_file_, tape_realtime_)) {
            Log::platform()->error("load: failed to load '{}'", load_file_);
        }
        load_countdown_ = -1;
    } else if (load_countdown_ > 0) {
        --load_countdown_;
    }

    // --delayed-screenshot-layers: arm the compositor layer mask for the frames
    // rendered in this tick — the last of them is the one the screenshot below
    // captures — and take it down again right after. Default LAYER_ALL makes
    // both calls no-ops, so the unmasked path is bit-identical to before.
    // Trade-off: the mask lives on the one Renderer the GUI also displays, so
    // for that single tick (~20 ms) the on-screen window shows the same masked
    // composite that lands in the PNG. Rendering a second, separate pass for
    // the screenshot would avoid that, but re-running render_frame also
    // re-advances per-frame renderer state (the ULA flash counter), which would
    // make `--delayed-screenshot-layers all` differ from the plain
    // --delayed-screenshot default. One frame of flicker is the cheaper bug.
    if (screenshot_countdown_ == 0)
        emulator_.renderer().set_layer_mask(screenshot_layers_);

    // Frames actually rendered in this tick. The screenshot below is only
    // valid if at least one of them went through the compositor with the mask
    // armed above — a debugger pause skips run_frame() entirely, and then the
    // framebuffer still holds a frame composited with LAYER_ALL. Capturing
    // that would silently hand the user a picture with the wrong layers in it.
    int frames_rendered = 0;

    // Task 27 C6 — compositor throttle at speed > 1x. At --speed 400 the
    // 5 ms QTimer emulates up to 200 frames/s while the display presents
    // ~50-60, so most composited frames are thrown away unseen. Throttle the
    // render hint to ~50 Hz wall-clock: the emulator core still runs every
    // frame, it just skips the end-of-frame compositing pass for frames
    // nobody will consume. Emulator::run_frame() re-forces the render when a
    // consumer the frontend cannot see is active (video recording, debugger).
    // Frontend-side forcing:
    //   * speed <= 1x: never skip — 1x behaviour stays bit-identical
    //     (including the audio-pacing multi-frame bursts and fastload);
    //   * screenshot due this tick (countdown == 0): the captured frame MUST
    //     have gone through the compositor with the layer mask armed above,
    //     so the whole tick renders.
    bool render_this_tick = true;
    if (speed_multiplier_ > 1.0 && screenshot_countdown_ != 0) {
        constexpr int64_t RENDER_INTERVAL_MS = 20;  // ~50 Hz presentation
        const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now_ms - last_render_ms_ < RENDER_INTERVAL_MS) {
            render_this_tick = false;
        } else {
            last_render_ms_ = now_ms;
        }
    }
    emulator_.set_render_enabled(render_this_tick);

    // Run one emulator frame (skip if debugger has paused).
    if (!emulator_.debug_state().paused()) {
        // Pace emulation against the sound card, not the wall clock. The 20 ms
        // QTimer runs the emulator at 50.00 frames/s, but a 48K frame is
        // 1/50.08 s and audio is synthesised on the emulated clock — so we feed
        // the device ~44030 samples per real second while it consumes 44100.
        // That permanent deficit empties the audio queue and SDL then pads the
        // stream with zeros, clicking a few times a second forever (issue #7 /
        // Task 23). Running an extra frame when the queue is low (or skipping
        // one when it is high) locks emulation to the audio device's clock.
        // Only at 1x speed: --speed and fastload deliberately decouple emulated
        // time from wall-clock time, so the emulator's sample rate no longer
        // matches the device's and no pacing band can hold. Audio at speeds
        // other than 1x stays as it always was (the device over- or under-runs
        // and SDL papers over it); fixing that needs resampling, which is out
        // of scope here. The same condition gates the underrun padding below —
        // unpaced, it would fire every tick instead of acting as a rescue.
        const bool audio_paced =
            audio_ && speed_multiplier_ == 1.0 && !emulator_.fastload_active();

        int frames = 1;
        if (audio_paced) {
            frames = audio_pacing::frames_for_tick(audio_->queued_ms());
        }
        for (int i = 0; i < frames; i++) {
            emulator_.run_frame();
            ++frame_count_;
            ++frames_rendered;
        }
        // Task 63 (issue #9) — record how often the audio pacer ran 0 or 2+
        // frames in one tick. The resulting frame losses are EXPECTED, and the
        // cadence report must attribute them as such rather than lumping them
        // in with presents the window system actually swallowed.
        if (frames >= 2)      ++audio_doubles_;
        else if (frames == 0) ++audio_skips_;

        // Task 19 fastload follow-up — when the phantom typist is
        // armed or a fast-load tape is delivering data, sprint
        // through additional emulated frames inside this single Qt
        // timer tick. The 20 ms QTimer interval normally caps the
        // emulator at 50 Hz; during fastload we want flat-out
        // execution like FUSE. We bound the per-tick burst (so the
        // GUI thread still services repaints / events between
        // bursts) and re-check `fastload_active()` each iteration
        // so the loop exits the moment the typist fires + the tape
        // hits `at_end()`. With BURST_LIMIT=200, a typical 48K TAP
        // loads in a single Qt tick (~4 s of emulated time per
        // 20 ms wall-clock, > 200× real-time on a modern host).
        constexpr int FASTLOAD_BURST_LIMIT = 200;
        int burst = 0;
        while (emulator_.fastload_active() &&
               burst < FASTLOAD_BURST_LIMIT &&
               !emulator_.debug_state().paused()) {
            emulator_.run_frame();
            ++frame_count_;
            ++frames_rendered;
            ++burst;
        }

        // Push audio samples to SDL — but skip during fastload to
        // avoid feeding the audio device huge sample chunks
        // generated at >>1× real-time. FUSE does the same via
        // `sound_pause()/sound_unpause()` around fastloading.
        if (audio_ && !emulator_.fastload_active()) {
            audio_->push_from_mixer(emulator_.mixer(), audio_paced);
        }
    } else if (audio_) {
        // Paused in the debugger: the emulator produces no samples, so the
        // device queue would drain and SDL would zero-fill — stepping the
        // output from its resting level to 0, and back again on resume. Two
        // clicks per pause. Keep feeding the device instead: the mixer is
        // empty, so push_from_mixer() only runs its underrun guard, which
        // holds the last level. A steady level is silent; a step is not.
        // (The guard's behaviour with zero production is AP-06 in
        // test/audio/audio_pacing_test.cpp.)
        audio_->push_from_mixer(emulator_.mixer(), /*hold_on_underrun=*/true);
    }

    // Task 27 C6 — the hint is per-tick: restore the default (always render)
    // so any run_frame() caller outside this tick (debugger actions, future
    // paths) is unaffected. The next tick re-decides.
    emulator_.set_render_enabled(true);

    // Update the display widget with the in-memory framebuffer (640×256).
    // The widget vertically doubles to 640×512 internally during prescale.
    // Task 27 C6 — skipped on throttled ticks: the framebuffer was not
    // re-composited, so re-copying/prescaling the identical stale frame into
    // the widget would be pure waste.
    if (render_this_tick) {
        // Task 63 (issue #9) — declare whether this push carries a newly
        // composited frame. `frames_rendered == 0` means no run_frame() ran
        // this tick (debugger paused, or the audio pacer skipping a tick
        // because the device queue is high), so the framebuffer is byte-for-
        // byte what was already shown. Presentation is unaffected either way;
        // only present_count() accounting changes. Without this, a paused
        // frontend re-presenting a static screen every tick inflates
        // `presented`, and a window straddling pause->resume then floors
        // `dropped` at 0 and reports lost=0 while frames really were lost.
        const bool new_content = present_cadence::carries_new_content(frames_rendered);
        main_window_->emulator_widget()->update_frame(
            emulator_.get_framebuffer(),
            emulator_.get_framebuffer_width(), emulator_.get_framebuffer_height(),
            new_content);
    }

    // Task 63 (issue #9) — accumulate this tick into the cadence window.
    // `frames_rendered` is every run_frame() of this tick (pacing burst +
    // fastload burst); only the LAST of them can possibly be presented,
    // because they all composite into the same framebuffer and update_frame()
    // is called once, after the loops. See src/platform/present_cadence.h.
    cadence_.emulated += static_cast<uint64_t>(frames_rendered);
    cadence_.superseded +=
        present_cadence::superseded_in_tick(frames_rendered, render_this_tick);
    cadence_.unrendered +=
        present_cadence::unrendered_in_tick(frames_rendered, render_this_tick);

    // Delayed screenshot: take after countdown expires. The screenshot
    // helper vertically doubles the in-memory 640×256 framebuffer so the
    // emitted PNG is 640×512 (square pixels, G104 Phase 7).
    //
    // Note: screenshot/exit countdowns decrement once per QTimer tick
    // (not once per emulator frame), so during a fastload burst the
    // countdowns still tick at 50 Hz wall-clock. Regression screenshots
    // are taken in headless mode (SDL path), which is unaffected; in
    // GUI use the imprecision is on the order of the burst length
    // (sub-second) and not user-visible.
    if (screenshot_countdown_ == 0) {
        if (frames_rendered > 0) {
            // The framebuffer holds a frame composited with the mask armed
            // above — capture it. If the write fails (missing directory, no
            // permission, disk full) the PNG the user asked for does not
            // exist, which is the same failure as never taking it at all:
            // error + non-zero exit, per QtApp::shutdown()'s contract.
            // save_screenshot_png() has already logged WHY.
            if (!save_screenshot_png(screenshot_file_, emulator_.get_framebuffer(),
                                     emulator_.get_framebuffer_width(),
                                     emulator_.get_framebuffer_height())) {
                Log::platform()->error(
                    "--delayed-screenshot: FAILED to write '{}' (layers: {}); "
                    "see the error above. Exiting non-zero.",
                    screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_));
                exit_code_ = 1;
            }
            emulator_.renderer().set_layer_mask(Renderer::LAYER_ALL);
            screenshot_countdown_ = -1;  // done
            screenshot_deferred_warned_ = false;
        } else {
            // The debugger is paused: run_frame() never ran, so the framebuffer
            // is a stale frame composited with LAYER_ALL. Writing it would
            // silently ignore the layer selection the user explicitly asked
            // for. Hold the countdown at 0 and capture on the first tick that
            // actually renders — but say so, once, rather than sitting mute.
            if (!screenshot_deferred_warned_) {
                Log::platform()->warn(
                    "--delayed-screenshot: due now, but the debugger is paused so no "
                    "frame is being rendered; deferring the capture (layers: {}). "
                    "Resume to take it.",
                    Renderer::layer_mask_to_string(screenshot_layers_));
                screenshot_deferred_warned_ = true;
            }
            emulator_.renderer().set_layer_mask(Renderer::LAYER_ALL);
        }
    } else if (screenshot_countdown_ > 0) {
        --screenshot_countdown_;
    }

    // Delayed automatic exit.
    if (exit_countdown_ == 0) {
        Log::platform()->info("automatic exit triggered");
        qapp_->quit();
        exit_countdown_ = -1;  // done
    } else if (exit_countdown_ > 0) {
        --exit_countdown_;
    }

#ifdef ENABLE_DEBUGGER
    if (auto* mgr = main_window_->debugger_manager()) {
        mgr->check_breakpoint_hit();
        mgr->refresh_panels();
    }
#endif

    // Issue #9 — drive the repeating timer from the fractional deadline
    // scheduler, at the END of the tick so this tick's own work is
    // subtracted from the wait. Follows runtime 50/60 Hz switches (NR 0x05
    // bit 2) and speed changes via the period argument, and emits the
    // "frame pacing" line when the rate changes. The cold-boot early
    // returns above are covered by cold_boot()'s own rebase.
    reschedule_frame_timer();
}

void QtApp::on_status_tick() {
    if (!main_window_) return;

    double fps = static_cast<double>(frame_count_);
    frame_count_ = 0;

    // Task 63 (issue #9) — drain the present-cadence window.
    //
    // The two figures answer different questions and MUST NOT be conflated:
    // `emulated` says how fast the machine is being simulated, `presented`
    // says how much of that the user actually saw. Issue #9 is entirely about
    // the second, and the old instruments only ever reported the first.
    if (main_window_->emulator_widget()) {
        const uint64_t pc = main_window_->emulator_widget()->present_count();
        cadence_.presented = pc - last_present_count_;
        last_present_count_ = pc;
    }
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const present_cadence::Report cad =
        present_cadence::summarize(cadence_, now_ms - last_status_ms_);
    last_status_ms_ = now_ms;
    cadence_ = {};

    // A window with no duration is not a measurement — do not print one.
    // (last_status_ms_ is seeded when the timer starts, so this only guards
    // a clock anomaly, not the ordinary first tick.)
    if (cad.reportable) {
        Log::platform()->debug(
            "cadence: emulated={} presented={} dropped={} "
            "(superseded={} unrendered={} lost={}) over {}ms | "
            "audio-catchup: {} doubles, {} skips",
            cad.emulated, cad.presented, cad.dropped,
            cad.superseded, cad.unrendered, cad.lost, cad.elapsed_ms,
            audio_doubles_, audio_skips_);
    }
    audio_doubles_ = 0;
    audio_skips_   = 0;

    // Read current CPU speed from NextREG 0x07 (bits 1:0).
    int speed_idx = emulator_.nextreg().cached(0x07) & 0x03;

    main_window_->update_status(fps, static_cast<double>(cad.presented),
                                speed_idx, speed_multiplier_);
}
