#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "screenshot.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "video/renderer.h"

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
    /// Schedule a screenshot after `delay_frames` frames. `layer_mask` is a
    /// Renderer::LayerMask bitmask (--delayed-screenshot-layers); it is armed
    /// on the renderer for exactly the captured frame and disarmed right
    /// after. Renderer::LAYER_ALL (the default) makes that a no-op.
    void set_delayed_screenshot(const std::string& file, int delay_frames,
                                uint8_t layer_mask);
    /// Exit after `delay_frames` frames — the hard bound: it always fires,
    /// even with a --delayed-screenshot / --delayed-snapshot still outstanding
    /// (which then errors and exits non-zero, see shutdown()). main() resolves
    /// --delayed-automatic-exit (seconds) and --delayed-automatic-exit-frames
    /// (frames, wins when both given) into this frame count.
    void set_delayed_exit(int delay_frames);

    /// Schedule a snapshot save after `delay_frames` frames. Format is
    /// chosen by `file`'s extension (.szx / .nex / anything else -> .sna),
    /// same dispatch as MainWindow::on_save_snapshot(). Headless-only
    /// (Task 13b) — enables scripted save/reload round-trip testing
    /// without a GUI. Same "requested but never taken -> non-zero exit"
    /// contract as set_delayed_screenshot().
    void set_delayed_snapshot(const std::string& file, int delay_frames);

    /// Process exit status, valid after shutdown(). Non-zero when a requested
    /// --delayed-screenshot was never written (auto-exit fired first).
    int exit_code() const { return exit_code_; }
    void set_tape_realtime(bool realtime) { tape_realtime_ = realtime; }
    void set_rzx_play(const std::string& file) { rzx_play_file_ = file; }
    void set_rzx_record(const std::string& file) { rzx_record_file_ = file; }

    /// Schedule a keypress after a delay (for menu-driven tests in headless mode).
    /// key is a character like '0'-'9', 'a'-'z', or '\n' / ' ' for ENTER / SPACE.
    /// delay_frames is in emulated frames.
    void set_delayed_keypress(char key, int delay_frames);

    /// Same as set_delayed_keypress, but in emulated seconds. Conversion to
    /// frames is deferred to run() so the actual machine framerate
    /// (50 Hz PAL vs 60 Hz NTSC, per video_timing().refresh_60hz()) is
    /// used. Headless runs at max speed, so wallclock is irrelevant —
    /// "seconds" here means emulated-machine seconds.
    void set_delayed_keypress_seconds(char key, int delay_seconds);

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
    uint8_t     screenshot_layers_ = Renderer::LAYER_ALL;

    // Pending --delayed-automatic-exit state
    int         exit_countdown_ = -1;

    // Pending --delayed-snapshot state
    std::string snapshot_file_;
    int         snapshot_countdown_ = -1;

    // Non-zero when a requested screenshot was never taken (see shutdown()).
    int         exit_code_ = 0;

    EmulatorConfig config_;
    bool           config_set_ = false;
    bool           running_ = false;
    bool           tape_realtime_ = false;
    std::string    rzx_play_file_;
    std::string    rzx_record_file_;

    // Pending --delayed-keypress state
    struct DelayedKey {
        char key;
        int countdown;  // in frames
    };
    std::vector<DelayedKey> delayed_keys_;

    // Pending seconds-form keys awaiting framerate-aware conversion in run().
    struct PendingSecondsKey {
        char key;
        int  delay_seconds;
    };
    std::vector<PendingSecondsKey> pending_seconds_keys_;

    // In-memory framebuffer size (canonical 640×256 post-G104). Headless
    // never opens a window; these constants are declarative for symmetry
    // with sdl_app / qt_app and any future headless-rendering hooks. The
    // delayed-screenshot path emits a 640×512 PNG via vertical 2×
    // doubling inside save_screenshot_png.
    static constexpr int NATIVE_W = 640;
    static constexpr int NATIVE_H = 256;
    static constexpr int DISPLAY_H = NATIVE_H * 2;
};
