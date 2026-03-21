#include "gui/qt_app.h"
#include "gui/main_window.h"
#include "gui/emulator_widget.h"
#include "platform/sdl_audio.h"
#include "core/log.h"

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

bool QtApp::init(int argc, char* argv[]) {
    // Initialize SDL for audio only (no video, no window).
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        Log::platform()->error("SDL_Init(AUDIO): {}", SDL_GetError());
        return false;
    }

    // Create QApplication (must exist before any QWidget).
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

    // Route keyboard events from the Qt window to the emulator keyboard matrix.
    main_window_->set_key_callback([this](SDL_Scancode sc, bool pressed) {
        emulator_.keyboard().set_key(sc, pressed);
    });

    main_window_->show();

    // Set up a 20ms timer (50 Hz) to drive emulator frames.
    frame_timer_ = new QTimer(main_window_);
    frame_timer_->setTimerType(Qt::PreciseTimer);
    QObject::connect(frame_timer_, &QTimer::timeout, [this]() { on_frame_tick(); });
    frame_timer_->start(20);

    return true;
}

int QtApp::run() {
    if (!qapp_) return 1;
    return qapp_->exec();
}

void QtApp::shutdown() {
    if (frame_timer_) {
        frame_timer_->stop();
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

    // Apply pending load when countdown reaches zero.
    if (load_countdown_ == 0) {
        emulator_.load_nex(load_file_);
        load_countdown_ = -1;
    } else if (load_countdown_ > 0) {
        --load_countdown_;
    }

    // Run one emulator frame.
    emulator_.run_frame();

    // Push audio samples to SDL.
    if (audio_) {
        audio_->push_from_mixer(emulator_.mixer());
    }

    // Update the display widget with the new framebuffer.
    main_window_->emulator_widget()->update_frame(
        emulator_.get_framebuffer(), NATIVE_W, NATIVE_H);
}
