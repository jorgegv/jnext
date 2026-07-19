#include "gui/qt_app.h"
#include "gui/main_window.h"
#include "gui/emulator_widget.h"
#include "platform/emulator_boot.h"
#include "platform/render_policy.h"
#include "platform/sdl_audio.h"
#include "core/log.h"
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_manager.h"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>
#include <QApplication>
#include <QGuiApplication>
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
    frame_timer_->setInterval(seq_.rebase(steady_now_us(), period_us));
    log_frame_pacing(period_us);
}

void QtApp::reschedule_frame_timer(int interval)
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
    if (interval != frame_timer_->interval()) {
        frame_timer_->setInterval(interval);
    }
    log_frame_pacing(effective_frame_period_us());
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

    // Task 63 (issue #9) — identify the display path once. Which Qt platform
    // plugin is live (xcb vs wayland vs offscreen) changes how timer callbacks
    // and paints are delivered, and reporters cannot reliably answer it
    // themselves (jon263 asked for exactly this).
    Log::platform()->info("Qt platform plugin: {}",
                          QGuiApplication::platformName().toStdString());

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

    // Issue #40 — a machine-type change (Machine menu or Preferences) is a
    // power cycle routed through the SAME cold boot. The new type is written
    // into config_ so it also survives every LATER boot (F1 / Reset / a menu
    // load), exactly as if it had been passed on the command line.
    main_window_->set_reboot_callback([this](MachineType type) {
        config_.type = type;
        config_set_  = true;
        cold_boot();
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

    // Issue #40 — the sequence itself lives in platform/emulator_boot.h and is
    // shared with SdlApp; this supplies only the Qt-specific steps. Placement-
    // new keeps &emulator_ stable, so main_window_'s pointer, the debugger and
    // the mouse dispatcher (all bound to the stable address / its sub-objects)
    // stay valid; only the emulator-side wiring needs re-binding.
    ColdBootHooks hooks;
    hooks.rewire_host = [this](const EmulatorConfig& cfg) {
        // The reconstructed emulator/keyboard start at defaults: rebind the
        // window pointer, then re-create + re-wire + re-enumerate the gamepad
        // host and the per-connector input sources.
        if (main_window_) main_window_->set_emulator(&emulator_);
        wire_gamepad_and_sources(cfg);
    };
    hooks.cancel_pending_work = [this]() {
        inject_countdown_ = -1;
        load_countdown_   = -1;
    };
    hooks.schedule_load = [this](const std::string& file, int delay_frames) {
        set_pending_load(file, delay_frames);
    };
    hooks.on_booted = [this]() {
        // Re-anchor the deadline schedule to the (possibly changed) refresh
        // rate — a cold boot is a restart, so rebase rather than glide.
        rebase_frame_timer();
    };
    emulator_frontend_cold_boot(emulator_, config_set_ ? config_ : EmulatorConfig{},
                                load_file, hooks);
}

int64_t QtApp::TickEffects::now_us() const { return steady_now_us(); }

int QtApp::TickEffects::queued_ms() const { return app.audio_->queued_ms(); }

void QtApp::on_frame_tick() {
    // Task 91 — the tick's order and its cross-tick state live in
    // frame_sequencer::Sequencer, under unit test (frame_sequencer_test).
    // This is the whole of the Qt-side frame tick.
    TickEffects fx{*this};
    seq_.tick(fx);
}

// --- TickEffects: the frontend work the sequencer drives -------------------
// Each method below is a direct call into an existing subsystem. There are no
// ordering decisions here and no state: both moved into the sequencer.

bool QtApp::TickEffects::pre_frames() {
    QtApp& a = app;

    // Task 79 — Qt owns the event loop, so SDL never runs its own; drain the
    // SDL event queue each frame and feed controller add/remove/button/axis
    // events to the gamepad host (all other SDL events are ignored — SDL is
    // audio + gamepads only in the GUI build).
    if (a.gamepad_host_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            a.gamepad_host_->handle_event(e);
        }
    }

    // M_EXECCMD requests use the same cold-boot path as menu loads.
    if (std::string load_file = a.emulator_.take_nex_load_request(); !load_file.empty()) {
        a.cold_boot(load_file);
        return false;
    }

    // Task 70 — a hard reset (Reset button / F1 / a program's NR 0x02 bit 1)
    // is a power-on cold boot and cannot run inside run_frame(); it is
    // performed here between frames. cold_boot() reconstructs the emulator, so
    // abandon the tick and let the next one run the fresh machine.
    if (a.emulator_.take_hard_reset_request()) {
        a.cold_boot();
        return false;
    }

    // Apply pending inject when countdown reaches zero.
    if (a.inject_countdown_ == 0) {
        a.emulator_.inject_binary(a.inject_file_, a.inject_org_, a.inject_pc_);
        a.inject_countdown_ = -1;
    } else if (a.inject_countdown_ > 0) {
        --a.inject_countdown_;
    }

    // Apply pending load when countdown reaches zero (auto-detect format).
    if (a.load_countdown_ == 0) {
        // Shared format dispatch (incl. .rzx) — see platform/emulator_boot.h.
        if (!emulator_apply_load(a.emulator_, a.load_file_, a.tape_realtime_)) {
            Log::platform()->error("load: failed to load '{}'", a.load_file_);
        }
        a.load_countdown_ = -1;
    } else if (a.load_countdown_ > 0) {
        --a.load_countdown_;
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
    if (a.screenshot_countdown_ == 0)
        a.emulator_.renderer().set_layer_mask(a.screenshot_layers_);

    return true;
}

void QtApp::TickEffects::run_frame(bool composite) {
    app.emulator_.set_render_enabled(composite);
    app.emulator_.run_frame();
    ++app.frame_count_;
}

void QtApp::TickEffects::push_audio(bool hold_on_underrun) {
    app.audio_->push_from_mixer(app.emulator_.mixer(), hold_on_underrun);
}

void QtApp::TickEffects::present(bool carries_new_content) {
    // Update the display widget with the in-memory framebuffer (640×256).
    // The widget vertically doubles to 640×512 internally during prescale.
    app.main_window_->emulator_widget()->update_frame(
        app.emulator_.get_framebuffer(),
        app.emulator_.get_framebuffer_width(), app.emulator_.get_framebuffer_height(),
        carries_new_content);
}

void QtApp::TickEffects::post_frames(int frames_rendered) {
    QtApp& a = app;

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
    if (a.screenshot_countdown_ == 0) {
        if (frames_rendered > 0) {
            // The framebuffer holds a frame composited with the mask armed
            // above — capture it. If the write fails (missing directory, no
            // permission, disk full) the PNG the user asked for does not
            // exist, which is the same failure as never taking it at all:
            // error + non-zero exit, per QtApp::shutdown()'s contract.
            // save_screenshot_png() has already logged WHY.
            if (!save_screenshot_png(a.screenshot_file_, a.emulator_.get_framebuffer(),
                                     a.emulator_.get_framebuffer_width(),
                                     a.emulator_.get_framebuffer_height())) {
                Log::platform()->error(
                    "--delayed-screenshot: FAILED to write '{}' (layers: {}); "
                    "see the error above. Exiting non-zero.",
                    a.screenshot_file_, Renderer::layer_mask_to_string(a.screenshot_layers_));
                a.exit_code_ = 1;
            }
            a.emulator_.renderer().set_layer_mask(Renderer::LAYER_ALL);
            a.screenshot_countdown_ = -1;  // done
            a.screenshot_deferred_warned_ = false;
        } else {
            // The debugger is paused: run_frame() never ran, so the framebuffer
            // is a stale frame composited with LAYER_ALL. Writing it would
            // silently ignore the layer selection the user explicitly asked
            // for. Hold the countdown at 0 and capture on the first tick that
            // actually renders — but say so, once, rather than sitting mute.
            if (!a.screenshot_deferred_warned_) {
                Log::platform()->warn(
                    "--delayed-screenshot: due now, but the debugger is paused so no "
                    "frame is being rendered; deferring the capture (layers: {}). "
                    "Resume to take it.",
                    Renderer::layer_mask_to_string(a.screenshot_layers_));
                a.screenshot_deferred_warned_ = true;
            }
            a.emulator_.renderer().set_layer_mask(Renderer::LAYER_ALL);
        }
    } else if (a.screenshot_countdown_ > 0) {
        --a.screenshot_countdown_;
    }

    // Delayed automatic exit.
    if (a.exit_countdown_ == 0) {
        Log::platform()->info("automatic exit triggered");
        a.qapp_->quit();
        a.exit_countdown_ = -1;  // done
    } else if (a.exit_countdown_ > 0) {
        --a.exit_countdown_;
    }

#ifdef ENABLE_DEBUGGER
    if (auto* mgr = a.main_window_->debugger_manager()) {
        mgr->check_breakpoint_hit();
        mgr->refresh_panels();
    }
#endif
}

void QtApp::TickEffects::set_timer_interval(int ms) {
    app.reschedule_frame_timer(ms);
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
        seq_.cadence().presented = pc - last_present_count_;
        last_present_count_ = pc;
    }
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const present_cadence::Report cad =
        present_cadence::summarize(seq_.cadence(), now_ms - last_status_ms_);
    last_status_ms_ = now_ms;
    seq_.reset_cadence();

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
            seq_.audio_doubles(), seq_.audio_skips());
    }
    seq_.reset_audio_counters();

    // Task 63 (issue #9) — tick-delivery diagnostics: WHERE the time goes.
    // Same window as the cadence line above (drained by the same 1 s timer),
    // same reportable gate. Drain the widget's paint samples into the window
    // first so one report carries all four mechanisms; then reset. All
    // semantics live in src/platform/tick_stats.h — this is formatting only.
    if (main_window_->emulator_widget()) {
        seq_.stats().paint_us = main_window_->emulator_widget()->take_paint_stats();
    }
    const tick_stats::Report ts = tick_stats::summarize(seq_.stats());
    seq_.reset_stats();
    if (cad.reportable) {
        // µs stats -> "avg/min/max ms"; a stat with no samples prints n/a
        // (the unit lives inside the helper so "n/a" never grows a suffix).
        const auto ms3 = [](const tick_stats::StatReport& s) -> std::string {
            if (!s.valid) return "n/a";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.1f/%.1f/%.1fms",
                          s.mean / 1000.0, s.min / 1000.0, s.max / 1000.0);
            return buf;
        };
        // queue samples are already ms integers.
        const auto qms = [](const tick_stats::StatReport& s) -> std::string {
            if (!s.valid) return "n/a";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.1f/%lld/%lldms", s.mean,
                          static_cast<long long>(s.min),
                          static_cast<long long>(s.max));
            return buf;
        };
        Log::platform()->debug(
            "ticks: n={} interval avg/min/max={} late(>{:.1f}ms)={} "
            "handler avg/min/max={} emu avg/min/max={} "
            "queue avg/min/max={} paint avg/min/max={}",
            ts.ticks, ms3(ts.interval_us), ts.late_threshold_us / 1000.0,
            ts.late, ms3(ts.handler_us), ms3(ts.emu_us), qms(ts.queue_ms),
            ms3(ts.paint_us));
    }

    // Read current CPU speed from NextREG 0x07 (bits 1:0).
    int speed_idx = emulator_.nextreg().cached(0x07) & 0x03;

    main_window_->update_status(fps, static_cast<double>(cad.presented),
                                speed_idx, speed_multiplier_);
}
