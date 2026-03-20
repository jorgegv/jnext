#pragma once
#include <SDL2/SDL.h>
#include <string>
#include "sdl_display.h"
#include "sdl_input.h"
#include "core/emulator.h"

class SdlApp {
public:
    bool init(int argc, char* argv[]);
    void run();
    void shutdown();

    Emulator& emulator() { return emulator_; }

    /// Schedule a binary injection after `delay_frames` frames.
    void set_pending_inject(const std::string& file, uint16_t org,
                            uint16_t pc, int delay_frames);

    /// Schedule a file load (e.g. .nex) after `delay_frames` frames.
    void set_pending_load(const std::string& file, int delay_frames);

private:
    SdlDisplay display_;
    SdlInput   input_;
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

    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
    // Target: 50 Hz = 20 ms per frame
    static constexpr uint32_t FRAME_MS = 20;
};
