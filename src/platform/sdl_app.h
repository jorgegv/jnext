#pragma once
#include <SDL2/SDL.h>
#include <string>
#include "sdl_display.h"
#include "sdl_input.h"
#include "sdl_audio.h"
#include "screenshot.h"
#include "core/emulator.h"

class SdlApp {
public:
    bool init(int argc, char* argv[]);
    void run();
    void shutdown();

    Emulator& emulator() { return emulator_; }

    /// Set emulator config before calling init().
    void set_config(const EmulatorConfig& cfg) { config_ = cfg; config_set_ = true; }

    /// Schedule a binary injection after `delay_frames` frames.
    void set_pending_inject(const std::string& file, uint16_t org,
                            uint16_t pc, int delay_frames);

    /// Schedule a file load (e.g. .nex) after `delay_frames` frames.
    void set_pending_load(const std::string& file, int delay_frames);

    /// Schedule a screenshot after `delay_frames` frames.
    void set_delayed_screenshot(const std::string& file, int delay_frames);

    /// Schedule automatic exit after `delay_seconds` seconds.
    void set_delayed_exit(int delay_seconds);

    void set_tape_realtime(bool) {}
    void set_rzx_play(const std::string&) {}
    void set_rzx_record(const std::string&) {}

private:
    SdlDisplay display_;
    SdlInput   input_;
    SdlAudio   audio_;
    Emulator   emulator_;
    bool       running_ = false;

    // Pending --inject state
    std::string inject_file_;
    uint16_t    inject_org_ = 0;
    uint16_t    inject_pc_  = 0;
    int         inject_countdown_ = -1;  // -1 = no pending inject

    // Pending --load state
    std::string load_file_;
    int         load_countdown_ = -1;    // -1 = no pending load

    // Pending --delayed-screenshot state
    std::string screenshot_file_;
    int         screenshot_countdown_ = -1;  // in frames; -1 = no pending

    // Pending --delayed-automatic-exit state
    int         exit_countdown_ = -1;  // in frames; -1 = no pending

    // Emulator config (set via set_config() before init())
    EmulatorConfig config_;
    bool           config_set_ = false;

    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
    // Target: 50 Hz = 20 ms per frame
    static constexpr uint32_t FRAME_MS = 20;
};
