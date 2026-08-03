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

    /// Benchmark mode (Task 27 T1): run exactly `frames` frames uncapped,
    /// then print ONE machine-parseable `BENCH ...` line plus a human
    /// summary line to stdout and exit. `workload` is a free-form label
    /// (the loaded file's basename, or "boot-<machine>") echoed in the
    /// BENCH line. Measurement-only: nothing in the emulation path changes.
    void set_benchmark(int frames, const std::string& workload);

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
    /// `key` is a case-insensitive key name (Task 57 extended it beyond
    /// single chars so NextZXOS shell interactions can be scripted):
    ///   - a single character: '0'-'9', 'a'-'z', or the punctuation
    ///     '.' ',' ';' ':' (mapped to their SYMBOL SHIFT compound)
    ///   - a named key: "enter"/"return", "space", "up"/"down"/"left"/
    ///     "right" (cursors = CAPS SHIFT + 7/6/5/8, same table as the
    ///     PC-arrow compound map in src/input/keyboard.cpp)
    ///   - a compound "sym+<char>" or "caps+<char>" (e.g. "sym+m" = '.')
    /// Returns false (and schedules nothing) if the name is not
    /// recognised — the caller must fail loudly, never drop a key.
    /// delay_frames is in emulated frames. Repeatable: each call queues
    /// one independent keypress.
    bool set_delayed_keypress(const std::string& key, int delay_frames);

    /// Same as set_delayed_keypress, but in emulated seconds. Conversion to
    /// frames is deferred to run() so the actual machine framerate
    /// (50 Hz PAL vs 60 Hz NTSC, per video_timing().refresh_60hz()) is
    /// used. Headless runs at max speed, so wallclock is irrelevant —
    /// "seconds" here means emulated-machine seconds.
    bool set_delayed_keypress_seconds(const std::string& key, int delay_seconds);

    /// Schedule a hardware NMI BUTTON press after a delay, so an NMI
    /// handler can be exercised headlessly (GH #209). `button` is
    /// case-insensitive and names which button to press, spelled as the
    /// label on a real Next's case. Two of its three buttons raise an
    /// NMI, and they have separate enable gates, so there is no default:
    ///   - "nmi"   (aliases "mf", "m1")  : the NMI button   (Multiface, host F9)
    ///   - "drive" (alias  "divmmc")     : the DRIVE button (DivMMC,    host F10)
    /// RESET is the third button and is not an NMI source, so it is not
    /// a name here. Returns false (and schedules nothing) if the name is
    /// not recognised — including the empty string — because the caller
    /// must fail loudly rather than drop a press.
    ///
    /// The press goes through `Emulator::on_hotkey_f9_mf_nmi()` /
    /// `on_hotkey_f10_divmmc_nmi()`, the SAME seam the GUI and SDL
    /// front-ends use for F9/F10, so every NR 0x06 / NR 0x83 / CONMEM /
    /// config-mode gate and the whole arbitration chain are exercised
    /// exactly as on hardware. It is deliberately NOT a synthetic
    /// `request_nmi()` on the CPU, which would bypass all of that and
    /// prove nothing about the button path.
    ///
    /// One press = one strobe, which is what the hardware does, not a
    /// convenience: `hotkey_m1` / `hotkey_drive` are one-cycle edge
    /// pulses (zxnext.vhd:6348-6349), and the arbitration latch is
    /// guarded by `elsif nmi_activated = '0'` (zxnext.vhd:2106), so a
    /// button HELD across many frames still yields exactly ONE NMI
    /// until the in-flight one retires via RETN / automap-hold / the
    /// Multiface exit port sequence. A hold-for-N-frames press would
    /// therefore be indistinguishable from this one on the Next, while
    /// on real Multiface hardware a hold longer than ~1 s is not an NMI
    /// at all (emu_fnkeys.vhd:114-157 turns it into the M1+key combo).
    /// Repeatable: each call queues one independent press.
    bool set_delayed_nmi(const std::string& button, int delay_frames);

    /// Same as set_delayed_nmi, but in emulated seconds. Conversion to
    /// frames is deferred to run() so the actual machine framerate is
    /// used — identical treatment to set_delayed_keypress_seconds.
    bool set_delayed_nmi_seconds(const std::string& button, int delay_seconds);

    /// Which of the two NMI buttons a --delayed-nmi press targets.
    /// Public because the name→enum parser is a free function in the
    /// .cpp, alongside the keypress name parser.
    enum class NmiButtonName { Mf, DivMmc };

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

    // --benchmark state (Task 27 T1). 0 = disabled.
    int         benchmark_frames_ = 0;
    std::string benchmark_workload_;
    /// Print the BENCH + human summary lines for a finished benchmark run.
    void print_benchmark_result(double wall_seconds);

    // Non-zero when a requested screenshot was never taken (see shutdown()).
    int         exit_code_ = 0;

    EmulatorConfig config_;
    bool           config_set_ = false;
    bool           running_ = false;
    bool           tape_realtime_ = false;
    std::string    rzx_play_file_;
    std::string    rzx_record_file_;

    // Pending --delayed-keypress state. Key names are parsed to matrix
    // positions up-front (set_delayed_keypress rejects unknown names);
    // row2/col2 = -1 when there is no shift component.
    struct DelayedKey {
        std::string name;   // original key name (for logging)
        int row1, col1;     // primary matrix position
        int row2, col2;     // compound second position, or -1/-1
        int countdown;      // in frames
    };
    std::vector<DelayedKey> delayed_keys_;

    // Pending seconds-form keys awaiting framerate-aware conversion in run().
    struct PendingSecondsKey {
        DelayedKey key;     // countdown unused until conversion
        int  delay_seconds;
    };
    std::vector<PendingSecondsKey> pending_seconds_keys_;

    // Pending --delayed-nmi state (GH #209). Which of the two buttons
    // was named is resolved at schedule time (unknown names are
    // rejected there, never dropped at fire time).
    struct DelayedNmi {
        std::string   name;     // original button name (for logging)
        NmiButtonName button;
        int           countdown;  // in frames
    };
    std::vector<DelayedNmi> delayed_nmis_;

    // Pending seconds-form NMI presses awaiting conversion in run().
    struct PendingSecondsNmi {
        DelayedNmi nmi;     // countdown unused until conversion
        int  delay_seconds;
    };
    std::vector<PendingSecondsNmi> pending_seconds_nmis_;

    // In-memory framebuffer size (canonical 640×256 post-G104). Headless
    // never opens a window; these constants are declarative for symmetry
    // with sdl_app / qt_app and any future headless-rendering hooks. The
    // delayed-screenshot path emits a 640×512 PNG via vertical 2×
    // doubling inside save_screenshot_png.
    static constexpr int NATIVE_W = 640;
    static constexpr int NATIVE_H = 256;
    static constexpr int DISPLAY_H = NATIVE_H * 2;
};
