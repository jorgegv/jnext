#pragma once

#include <string>
#include <cstdint>
#include <memory>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "platform/screenshot.h"
#include "video/renderer.h"

class QApplication;
class QTimer;
class MainWindow;
class SdlAudio;

/// Qt6-based application shell for the emulator.
///
/// Replaces SdlApp when ENABLE_QT_UI is defined.  SDL is still used for
/// audio output; the display and input are handled by Qt.
class QtApp {
public:
    QtApp();
    ~QtApp();

    // Non-copyable.
    QtApp(const QtApp&) = delete;
    QtApp& operator=(const QtApp&) = delete;

    /// Set emulator config before calling init().
    void set_config(const EmulatorConfig& cfg) { config_ = cfg; config_set_ = true; }

    /// Schedule a binary injection after `delay_frames` frames.
    void set_pending_inject(const std::string& file, uint16_t org,
                            uint16_t pc, int delay_frames);

    /// Schedule a file load (e.g. .nex) after `delay_frames` frames.
    void set_pending_load(const std::string& file, int delay_frames);

    void set_tape_realtime(bool realtime) { tape_realtime_ = realtime; }
    void set_rzx_play(const std::string& file) { rzx_play_file_ = file; }
    void set_rzx_record(const std::string& file) { rzx_record_file_ = file; }

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
    /// --delayed-screenshot was never written (the debugger stayed paused, so
    /// no frame was ever rendered for it, and the emulator exited first).
    int exit_code() const { return exit_code_; }

    /// Set emulator speed multiplier (e.g. 0.5, 1.0, 2.0, 4.0).
    /// Adjusts the frame timer interval accordingly.
    void set_speed_multiplier(double multiplier);
    double speed_multiplier() const { return speed_multiplier_; }

    /// Initialize Qt, SDL audio, emulator, and create the main window.
    bool init(int argc, char* argv[]);

    /// Enter the Qt event loop (blocks until the window is closed).
    int run();

    /// Clean up SDL audio and Qt resources.
    void shutdown();

    Emulator& emulator() { return emulator_; }

    /// Task 66 — access to the main window for applying saved GUI
    /// preferences after init(). Valid only after a successful init().
    MainWindow* main_window() { return main_window_; }

    /// Task 70 — power-on cold boot: reconstruct the emulator in place and
    /// re-run the proven startup init() path. `load_file` empty = clean
    /// NextZXOS boot (Reset button / F1 / a program's NR 0x02 hard reset);
    /// non-empty = boot as if launched with --load <file> (menu file load).
    void cold_boot(const std::string& load_file = std::string());

private:
    void on_frame_tick();
    void on_status_tick();

    Emulator emulator_;

    // QApplication holds a reference to argc (and may write through it), so the
    // storage must outlive it — init()'s own parameters do not.
    int    qt_argc_ = 0;
    char** qt_argv_ = nullptr;

    // Ownership managed manually to control init/shutdown order.
    QApplication* qapp_        = nullptr;
    MainWindow*   main_window_ = nullptr;
    QTimer*       frame_timer_ = nullptr;
    QTimer*       status_timer_ = nullptr;
    std::unique_ptr<SdlAudio> audio_;

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
    int         screenshot_countdown_ = -1;  // in frames; -1 = no pending
    uint8_t     screenshot_layers_ = Renderer::LAYER_ALL;
    // Set once we have warned that the capture is being held back because no
    // frame is rendering (debugger paused). Keeps the warning off every tick.
    bool        screenshot_deferred_warned_ = false;

    // Process exit status. Non-zero when a requested screenshot was never
    // taken (see shutdown()); main.cpp returns it.
    int         exit_code_ = 0;

    // Pending --delayed-automatic-exit state
    int         exit_countdown_ = -1;  // in frames; -1 = no pending

    // Emulator config
    EmulatorConfig config_;
    bool           config_set_ = false;
    bool           tape_realtime_ = false;
    std::string    rzx_play_file_;
    std::string    rzx_record_file_;

    // FPS tracking
    int frame_count_ = 0;

    // Emulator speed multiplier (1.0 = real-time 50 Hz)
    double speed_multiplier_ = 1.0;

    // Task 27 C6 — wall-clock throttle for the compositor at speed > 1x.
    // Monotonic ms timestamp (std::chrono::steady_clock in qt_app.cpp) of the
    // last tick whose frames were rendered. Initialised to 0, NOT INT64_MIN:
    // steady_clock counts from boot, so `now - 0` is host uptime (>> 20 ms)
    // and the first tick always renders. An INT64_MIN sentinel overflows the
    // `now - last` subtraction (UB, observed negative), which made EVERY
    // frame skip forever — caught by render-skip-turbo-func's engagement
    // count (601 skips in 601 frames, i.e. zero renders).
    int64_t last_render_ms_ = 0;

    static constexpr int NATIVE_W = 640;
    static constexpr int NATIVE_H = 256;
    /// Vertical 2× — square-pixel display height (Phase 7 wires the pipeline).
    static constexpr int DISPLAY_H = NATIVE_H * 2;
};
