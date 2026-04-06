#include "headless_app.h"
#include "core/log.h"

bool HeadlessApp::init(int argc, char* argv[]) {
    (void)argc; (void)argv;

    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
    if (!emulator_.init(cfg)) {
        Log::platform()->error("Emulator init failed");
        return false;
    }

    running_ = true;
    Log::platform()->info("Headless mode initialized");
    return true;
}

void HeadlessApp::set_pending_inject(const std::string& file, uint16_t org,
                                     uint16_t pc, int delay_frames) {
    inject_file_ = file;
    inject_org_  = org;
    inject_pc_   = pc;
    inject_countdown_ = delay_frames;
    Log::platform()->info("--inject: will load '{}' at {:#06x} (PC={:#06x}) after {} frame(s)",
                           file, org, pc, delay_frames);
}

void HeadlessApp::set_pending_load(const std::string& file, int delay_frames) {
    load_file_ = file;
    load_countdown_ = delay_frames;
    Log::platform()->info("--load: will load '{}' after {} frame(s)", file, delay_frames);
}

void HeadlessApp::set_delayed_screenshot(const std::string& file, int delay_seconds) {
    screenshot_file_ = file;
    screenshot_countdown_ = delay_seconds * 50;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} second(s)",
                           file, delay_seconds);
}

void HeadlessApp::set_delayed_exit(int delay_seconds) {
    exit_countdown_ = delay_seconds * 50;
    Log::platform()->info("--delayed-automatic-exit: will exit after {} second(s)",
                           delay_seconds);
}

void HeadlessApp::run() {
    while (running_) {
        // Apply pending inject.
        if (inject_countdown_ == 0) {
            emulator_.inject_binary(inject_file_, inject_org_, inject_pc_);
            inject_countdown_ = -1;
        } else if (inject_countdown_ > 0) {
            --inject_countdown_;
        }

        // Apply pending load.
        if (load_countdown_ == 0) {
            emulator_.load_nex(load_file_);
            load_countdown_ = -1;
        } else if (load_countdown_ > 0) {
            --load_countdown_;
        }

        emulator_.run_frame();

        // Delayed screenshot.
        if (screenshot_countdown_ == 0) {
            save_screenshot_png(screenshot_file_, emulator_.get_framebuffer(),
                                NATIVE_W, NATIVE_H);
            screenshot_countdown_ = -1;
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
    }
}

void HeadlessApp::shutdown() {
    Log::platform()->info("Headless mode shutdown");
}
