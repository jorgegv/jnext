#pragma once
#include <string>
#include <cstdint>
#include "screenshot.h"
#include "core/emulator.h"
#include "core/emulator_config.h"

/// Headless application shell — no display, no audio, no input.
/// Runs the emulator as fast as possible for automated testing.
class HeadlessApp {
public:
    bool init(int argc, char* argv[]);
    void run();
    void shutdown();

    Emulator& emulator() { return emulator_; }

    void set_config(const EmulatorConfig& cfg) { config_ = cfg; config_set_ = true; }

    void set_pending_inject(const std::string& file, uint16_t org,
                            uint16_t pc, int delay_frames);
    void set_pending_load(const std::string& file, int delay_frames);
    void set_delayed_screenshot(const std::string& file, int delay_seconds);
    void set_delayed_exit(int delay_seconds);

private:
    Emulator emulator_;

    // Pending --inject state
    std::string inject_file_;
    uint16_t    inject_org_ = 0;
    uint16_t    inject_pc_  = 0;
    int         inject_countdown_ = -1;

    // Pending --load state
    std::string load_file_;
    int         load_countdown_ = -1;

    // Pending --delayed-screenshot state
    std::string screenshot_file_;
    int         screenshot_countdown_ = -1;

    // Pending --delayed-automatic-exit state
    int         exit_countdown_ = -1;

    EmulatorConfig config_;
    bool           config_set_ = false;
    bool           running_ = false;

    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
};
