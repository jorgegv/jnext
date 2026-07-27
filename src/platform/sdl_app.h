#pragma once
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include "sdl_display.h"
#include "sdl_input.h"
#include "sdl_audio.h"
#include "screenshot.h"
#include "host_key_latch.h"
#include "core/emulator.h"
#include "video/renderer.h"
#include "input/gamepad_host.h"
#include "input/mouse_dispatcher.h"

class SdlApp {
public:
    bool init(int argc, char* argv[]);
    void run();
    void shutdown();

    Emulator& emulator() { return emulator_; }

    /// Set emulator config before calling init().
    void set_config(const EmulatorConfig& cfg) { config_ = cfg; config_set_ = true; }

    /// Task 70 — power-on cold boot: reconstruct the emulator in place and
    /// re-run the proven startup init() path (F1 / a program's NR 0x02 hard
    /// reset). `load_file` empty = clean boot.
    void cold_boot(const std::string& load_file = std::string());

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
    // Smoothed-band state for the audio pacer (Task 63) — one per audio
    // device, owned here per the audio_pacing.h contract (never a global).
    audio_pacing::BandState pacing_band_;
    Emulator   emulator_;
    bool       running_ = false;

    // Kempston mouse host adapter — wires SDL_MOUSE* events into the
    // emulator's KempstonMouse. Created in init() once emulator_ is
    // bound. unique_ptr so we can defer construction until then.
    std::unique_ptr<MouseDispatcher> mouse_dispatcher_;

    /// Pointer capture (issue #37). A Kempston mouse is a RELATIVE device, so
    /// the host pointer must be confined or it hits the window edge and the
    /// guest pointer stops dead. SDL's relative mode does the hiding,
    /// confining and unbounded xrel/yrel for us — the Qt frontend has to
    /// hand-roll the equivalent. Same semantics as there: click the window to
    /// capture, Ctrl+Alt to release. Uncaptured, no mouse event is forwarded.
    bool mouse_captured_ = false;
    void set_mouse_captured(bool on);

    // Joystick / gamepad host adapter — wires SDL_CONTROLLER* events into
    // the emulator's Joystick and owns the SDL_GameController lifecycle for
    // the two Next pad headers. Created in init() once emulator_ is bound.
    // Closes G42 (JOY-WIRE-02/03/04); Task 79 moved the controller
    // open/close/route logic into the shared GamepadHost.
    std::unique_ptr<GamepadHost> gamepad_host_;

    /// Issue #122 — host key events go through the issue-#120 minimum-hold
    /// latch instead of straight into the matrix. SDL_PollEvent() and the frame
    /// loop are the SAME loop iteration (run()), so a key event can only land
    /// BETWEEN two run_frame() calls — the identical sampling window that made
    /// the Qt frontend drop keystrokes, and for the identical reason.
    ///
    /// BOTH the policy and the DISPATCH live in platform/host_key_latch.h, and
    /// both are unit-tested (host_key_latch_test, rows HK-* and RT-*). SdlApp
    /// supplies only the three call sites: on_host_key() in the input_.on_key
    /// callback, on_tick_end() after the frame loop in run(), and attach() in
    /// init() plus cold_boot()'s rewire_host hook.
    host_key_latch::Router<Keyboard, SDL_Scancode> key_router_;

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
