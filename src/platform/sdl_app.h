#pragma once
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include "sdl_display.h"
#include "sdl_input.h"
#include "sdl_audio.h"
#include "screenshot.h"
#include "core/emulator.h"
#include "video/renderer.h"
#include "input/joystick_dispatcher.h"
#include "input/mouse_dispatcher.h"

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

    /// Schedule a screenshot after `delay_frames` frames. `layer_mask` is a
    /// Renderer::LayerMask bitmask (--delayed-screenshot-layers), armed on the
    /// renderer for the captured frame only. Renderer::LAYER_ALL = no-op.
    void set_delayed_screenshot(const std::string& file, int delay_frames,
                                uint8_t layer_mask);

    /// Schedule automatic exit after `delay_frames` frames. main() resolves
    /// --delayed-automatic-exit (seconds) and --delayed-automatic-exit-frames
    /// (frames, wins when both given) into this frame count.
    void set_delayed_exit(int delay_frames);

    /// Process exit status, valid after shutdown(). Non-zero when a requested
    /// --delayed-screenshot was never written.
    int exit_code() const { return exit_code_; }

    void set_tape_realtime(bool) {}
    void set_rzx_play(const std::string&) {}
    void set_rzx_record(const std::string&) {}

private:
    SdlDisplay display_;
    SdlInput   input_;
    SdlAudio   audio_;
    Emulator   emulator_;
    bool       running_ = false;

    // Kempston mouse host adapter — wires SDL_MOUSE* events into the
    // emulator's KempstonMouse. Created in init() once emulator_ is
    // bound. unique_ptr so we can defer construction until then.
    std::unique_ptr<MouseDispatcher> mouse_dispatcher_;

    // Joystick / gamepad host adapter — wires SDL_CONTROLLER* events into
    // the emulator's Joystick. Created in init() once emulator_ is bound.
    // Closes G42 (JOY-WIRE-02/03/04). Tracks open SDL_GameController
    // handles so the events get dispatched (SDL only emits CONTROLLER*
    // events for opened controllers).
    std::unique_ptr<JoystickDispatcher> joystick_dispatcher_;
    SDL_GameController* controllers_[2] = { nullptr, nullptr };

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
    uint8_t     screenshot_layers_ = Renderer::LAYER_ALL;

    // Non-zero when a requested screenshot was never taken (see shutdown()).
    int         exit_code_ = 0;

    // Pending --delayed-automatic-exit state
    int         exit_countdown_ = -1;  // in frames; -1 = no pending

    // Last frame-pacing period logged (ms); logs only on a 50/60 Hz change.
    uint32_t    last_frame_ms_ = 0;

    // Emulator config (set via set_config() before init())
    EmulatorConfig config_;
    bool           config_set_ = false;

    static constexpr int NATIVE_W = 640;
    static constexpr int NATIVE_H = 256;
    /// Vertical 2× — square-pixel display height (Phase 7 wires the pipeline).
    static constexpr int DISPLAY_H = NATIVE_H * 2;
    // Frame pacing follows the emulated video refresh via
    // Emulator::frame_period_ms() (~20 ms at 50 Hz, ~17 ms at 60 Hz — issue #9).
};
