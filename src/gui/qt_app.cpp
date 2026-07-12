#include "gui/qt_app.h"
#include "gui/main_window.h"
#include "gui/emulator_widget.h"
#include "platform/sdl_audio.h"
#include "core/log.h"
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_manager.h"
#endif

#include <cctype>
#include <QApplication>
#include <QTimer>
#include <SDL2/SDL.h>

QtApp::QtApp() = default;
QtApp::~QtApp() = default;

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

    // Adjust frame timer: base interval is 20ms (50 Hz real-time).
    // Higher multiplier = shorter interval = faster emulation.
    int interval_ms = static_cast<int>(20.0 / multiplier);
    if (interval_ms < 1) interval_ms = 1;

    if (frame_timer_) {
        frame_timer_->setInterval(interval_ms);
    }
    Log::platform()->info("Emulator speed: {}x (timer {}ms)", multiplier, interval_ms);
}

void QtApp::set_delayed_exit(int delay_seconds) {
    exit_countdown_ = delay_seconds * 50;  // 50 fps
    Log::platform()->info("--delayed-automatic-exit: will exit after {} second(s)",
                           delay_seconds);
}

bool QtApp::init(int argc, char* argv[]) {
    // Initialize SDL for audio only (no video, no window).
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        Log::platform()->error("SDL_Init(AUDIO): {}", SDL_GetError());
        return false;
    }

    // Create QApplication (must exist before any QWidget).
    // Note: Hi-DPI scaling is left enabled (Qt6 default on Wayland).
    // EmulatorWidget accounts for devicePixelRatio itself.
    qapp_ = new QApplication(argc, argv);

    // Initialize SDL audio.
    audio_ = std::make_unique<SdlAudio>();
    if (!audio_->init()) {
        Log::platform()->warn("Audio init failed - continuing without sound");
    }

    // Initialize the emulator core.
    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
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

    // Route emulator speed changes from the menu to the frame timer.
    main_window_->set_speed_callback([this](double multiplier) {
        set_speed_multiplier(multiplier);
    });

    main_window_->show();

    // Re-apply scale after the window is mapped so devicePixelRatio is correct.
    // The 100ms delay ensures the window manager has finished placing the window
    // and the real DPR is available (critical on Wayland/Hi-DPI).
    QTimer::singleShot(100, main_window_, [this]() {
        main_window_->set_scale(main_window_->current_scale());
    });

    // Set up a 20ms timer (50 Hz) to drive emulator frames.
    frame_timer_ = new QTimer(main_window_);
    frame_timer_->setTimerType(Qt::PreciseTimer);
    QObject::connect(frame_timer_, &QTimer::timeout, [this]() { on_frame_tick(); });
    frame_timer_->start(20);

    // Set up a 1-second timer for status bar updates (FPS counter).
    status_timer_ = new QTimer(main_window_);
    QObject::connect(status_timer_, &QTimer::timeout, [this]() { on_status_tick(); });
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

    audio_.reset();
    SDL_Quit();

    delete main_window_;
    main_window_ = nullptr;

    delete qapp_;
    qapp_ = nullptr;
}

void QtApp::on_frame_tick() {
    // Apply pending inject when countdown reaches zero.
    if (inject_countdown_ == 0) {
        emulator_.inject_binary(inject_file_, inject_org_, inject_pc_);
        inject_countdown_ = -1;
    } else if (inject_countdown_ > 0) {
        --inject_countdown_;
    }

    // Apply pending load when countdown reaches zero (auto-detect format).
    if (load_countdown_ == 0) {
        std::string ext;
        auto dot = load_file_.rfind('.');
        if (dot != std::string::npos) {
            ext = load_file_.substr(dot);
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == ".tap") {
            emulator_.load_tap(load_file_, !tape_realtime_);
        } else if (ext == ".tzx") {
            emulator_.load_tzx(load_file_, !tape_realtime_);
        } else if (ext == ".sna") {
            emulator_.load_sna(load_file_);
        } else if (ext == ".szx") {
            emulator_.load_szx(load_file_);
        } else if (ext == ".wav") {
            emulator_.load_wav(load_file_);
        } else {
            emulator_.load_nex(load_file_);
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

    // Update the display widget with the in-memory framebuffer (640×256).
    // The widget vertically doubles to 640×512 internally during prescale.
    main_window_->emulator_widget()->update_frame(
        emulator_.get_framebuffer(),
        emulator_.get_framebuffer_width(), emulator_.get_framebuffer_height());

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
}

void QtApp::on_status_tick() {
    if (!main_window_) return;

    double fps = static_cast<double>(frame_count_);
    frame_count_ = 0;

    // Read current CPU speed from NextREG 0x07 (bits 1:0).
    int speed_idx = emulator_.nextreg().cached(0x07) & 0x03;

    main_window_->update_status(fps, speed_idx, speed_multiplier_);
}
