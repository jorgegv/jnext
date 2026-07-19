#pragma once

#include <string>
#include <cstdint>
#include <memory>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "input/gamepad_host.h"
#include "platform/frame_deadline.h"
#include "platform/present_cadence.h"
#include "platform/screenshot.h"
#include "platform/tick_stats.h"
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

    // Task 63 (issue #9) — the pre-existing frame-tick work, unchanged. Called
    // by on_frame_tick(), which is now a thin instrumented wrapper (interval /
    // queue-depth / handler-duration sampling around this body). All the
    // accumulation logic lives in src/platform/tick_stats.h where the unit
    // suite can pin it; this wiring is reachable by no test and stays trivial.
    void frame_tick_body();

    // Task 79 — (re)create the gamepad host and wire the per-connector input
    // sources (cursor-keys routing + SDL pad gating) to the current emulator.
    // Shared by init() and cold_boot() (which reconstructs the emulator).
    void wire_gamepad_and_sources(const EmulatorConfig& cfg);

    // Task 63 (issue #9) — fractional frame-deadline pacing. See
    // src/platform/frame_deadline.h for the policy; these are the thin
    // Qt-side wrappers (deliberately trivial: on_frame_tick wiring has no
    // test harness, so ALL scheduling logic lives in the tested header).
    /// Effective frame period (video refresh scaled by the speed
    /// multiplier), integer microseconds.
    int64_t effective_frame_period_us() const;
    /// Re-anchor the deadline schedule at now and restart the timer's
    /// period (timer start, cold boot, speed change).
    void rebase_frame_timer();
    /// End-of-tick: advance the deadline by one exact period and point the
    /// repeating timer at it (glides across 50/60 Hz and speed changes).
    void reschedule_frame_timer();
    /// Emit the "frame pacing" info line — only when the period changed.
    void log_frame_pacing(int64_t period_us);

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

    // Task 79 — SDL gamepad host (autodetected pads → Joy 1 / Joy 2) plus the
    // per-connector cursor-key routing. Created in init() once the emulator is
    // bound; SDL controller events are polled in on_frame_tick().
    std::unique_ptr<GamepadHost> gamepad_host_;

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

    // FPS tracking — EMULATED frames (run_frame() calls) per status window.
    int frame_count_ = 0;

    // Task 63 (issue #9) present-cadence diagnostics. See
    // src/platform/present_cadence.h for what each figure means and why the
    // previous instrument (dropped == audio double-step count) was useless.
    present_cadence::Counters cadence_;
    uint64_t last_present_count_ = 0;  ///< EmulatorWidget::present_count() at last drain
    int64_t  last_status_ms_ = 0;      ///< steady_clock ms at last drain (real window)
    int      audio_doubles_ = 0;       ///< ticks that ran 2+ frames (audio catch-up)
    int      audio_skips_ = 0;         ///< ticks that ran 0 frames (audio ahead)

    // Task 63 (issue #9) tick-delivery diagnostics. See
    // src/platform/tick_stats.h for what each figure means and the four
    // mechanisms ((a) late timer callbacks, (b) slow emulation, (c) slow
    // paint, (d) audio-queue starvation) the per-second `ticks:` line
    // discriminates.
    tick_stats::Counters tick_stats_;
    int64_t last_tick_us_ = -1;  ///< steady_clock µs at previous tick entry;
                                 ///  -1 = no tick yet. Persists across window
                                 ///  drains, so only the process's FIRST tick
                                 ///  contributes no interval sample.

    // Emulator speed multiplier (1.0 = real-time 50 Hz)
    double speed_multiplier_ = 1.0;

    // Task 63 (issue #9) — fractional frame-deadline scheduler driving
    // frame_timer_'s interval. The long-run tick rate equals the EXACT
    // emulated frame period (17.198 ms at Next-60) instead of its whole-ms
    // rounding (17 ms = 1.1% fast); see src/platform/frame_deadline.h.
    frame_deadline::Scheduler frame_deadline_;
    // Last effective period handed to the scheduler; the "frame pacing" info
    // line is emitted only when this changes (refresh-rate or speed change,
    // never per tick).
    int64_t last_paced_period_us_ = -1;

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
