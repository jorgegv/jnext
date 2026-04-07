#pragma once

#include <string>
#include <cstdint>
#include <memory>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "platform/screenshot.h"

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

    /// Schedule a screenshot after `delay_seconds` seconds.
    void set_delayed_screenshot(const std::string& file, int delay_seconds);

    /// Schedule automatic exit after `delay_seconds` seconds.
    void set_delayed_exit(int delay_seconds);

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

private:
    void on_frame_tick();
    void on_status_tick();

    Emulator emulator_;

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

    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
};
