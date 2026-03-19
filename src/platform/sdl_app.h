#pragma once
#include <SDL2/SDL.h>
#include "sdl_display.h"
#include "sdl_input.h"

class SdlApp {
public:
    bool init(int argc, char* argv[]);
    void run();
    void shutdown();

private:
    SdlDisplay display_;
    SdlInput   input_;
    bool       running_ = false;

    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
    // Target: 50 Hz = 20 ms per frame
    static constexpr uint32_t FRAME_MS = 20;
};
