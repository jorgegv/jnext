#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QActionGroup>
#include <QPoint>
#include <functional>
#include <memory>
#include <SDL2/SDL.h>
#include "core/emulator_config.h"
#include "gui/app_config.h"
#include "platform/pointer_capture.h"

class Emulator;
class EmulatorWidget;
class MouseDispatcher;
class QTimer;
class QMouseEvent;
class QWheelEvent;
#ifdef ENABLE_DEBUGGER
class DebuggerManager;
#endif

/// Main emulator window — QMainWindow shell with emulator viewport, menu bar,
/// toolbar, and status bar.  Keyboard events are dispatched to a configurable callback.
///
/// The window is non-resizable; scale is changed via the View menu (2x/3x/4x)
/// or F2 key. The EmulatorWidget is always an exact integer multiple of
/// 640×512 (in-memory 640×256 framebuffer with vertical 2× scaling — square
/// pixels for 4:3 CRT-faithful geometry, G104 Phase 7).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Out-of-line so unique_ptr<MouseDispatcher> can use a forward
    // declaration in this header.
    ~MainWindow() override;

    /// Access the central emulator display widget.
    EmulatorWidget* emulator_widget() { return emulator_widget_; }

    /// Set the emulator pointer for direct callbacks.
    /// When ENABLE_DEBUGGER is defined, also creates the DebuggerManager.
    void set_emulator(Emulator* emu);

    /// Task 60c/79 — re-seed this window's host input dispatchers (Kempston
    /// mouse) from the restored emulator state after a rewind / save-load.
    /// Invoked from QtApp's combined on_input_state_restored fan-out.
    void resync_input_dispatchers();

    /// Task 79 — refresh the Input-menu checkmarks from the emulator's
    /// effective per-connector sources. Called by QtApp after it has wired the
    /// sources (startup + cold boot) so the menu reflects the live state.
    void sync_joy_source_menu();

#ifdef ENABLE_DEBUGGER
    DebuggerManager* debugger_manager() { return debugger_mgr_; }
#endif

    /// Set the callback for key events.
    /// Signature: (SDL_Scancode scancode, bool pressed).
    using KeyCallback = std::function<void(SDL_Scancode, bool)>;
    void set_key_callback(KeyCallback cb) { key_callback_ = std::move(cb); }

    /// Toggle between windowed and fullscreen mode.
    void toggle_fullscreen();

    /// Set integer scale factor (2-4) and resize window accordingly.
    /// Only effective in windowed mode.
    void set_scale(int factor);

    /// Cycle through scale factors 2x -> 3x -> 4x -> 2x.
    /// Only effective in windowed mode.
    void cycle_scale();

    void set_crt_filter(bool enabled);
    bool crt_filter() const;

    bool is_fullscreen() const { return is_fullscreen_; }
    int current_scale() const { return current_scale_; }

    /// Set callback for emulator speed changes.
    using SpeedCallback = std::function<void(double)>;
    void set_speed_callback(SpeedCallback cb) { speed_callback_ = std::move(cb); }

    // Task 70 — file load from the menu triggers a full cold boot (as if the
    // file had been passed with --load at startup). The frontend (QtApp)
    // supplies this; MainWindow does not touch the emulator directly for loads.
    using LoadFileCallback = std::function<void(const std::string&)>;
    void set_load_file_callback(LoadFileCallback cb) { load_file_callback_ = std::move(cb); }

    // Issue #40 — a machine-type change is a power cycle, and it must take the
    // ONE canonical cold-boot path (platform/emulator_boot.h) like every other
    // reboot. MainWindow therefore requests it from the frontend instead of
    // calling Emulator::init() behind the frontend's back: a bare init() left
    // stale subsystem state and an unwired gamepad host. Same shape as
    // LoadFileCallback above.
    using RebootCallback = std::function<void(MachineType)>;
    void set_reboot_callback(RebootCallback cb) { reboot_callback_ = std::move(cb); }

    // Issue #40 — the "switching machine restarts the emulator" confirmation.
    // Injectable so apply_preferences() is reachable by tests without a modal
    // Qt dialog blocking forever; when unset it puts up the real QMessageBox.
    // Returns true if the user accepted the restart.
    using ConfirmRestartCallback = std::function<bool(MachineType)>;
    void set_confirm_restart_callback(ConfirmRestartCallback cb) {
        confirm_restart_callback_ = std::move(cb);
    }

    // Live-applies AND persists an edited AppConfigData from the Preferences
    // dialog. Public because it is the unit under test in
    // test/gui/preferences_apply_test.cpp — the behaviour issue #40 is about
    // (an Apply must not restart the machine) lives here, not in the dialog.
    void apply_preferences(const AppConfigData& cfg);

    /// Update status bar information.  Called once per second from the frame timer.
    /// Refresh the status bar. `fps` is the EMULATED frame rate (run_frame()
    /// calls/s); `presented_fps` is the rate at which frames actually reached
    /// the screen. Both are shown — see Task 63 / issue #9.
    ///
    /// `emu_speed` is what the user ASKED for (1.0 = normal) and
    /// `frame_period_ms` is the machine's own frame period; together with `fps`
    /// they yield the speed actually ACHIEVED (issue #120 — the cell used to
    /// show the request alone and read "100%" on a host managing 30 fps).
    void update_status(double fps, double presented_fps, int cpu_speed_idx,
                       double emu_speed = 1.0, double frame_period_ms = 0.0);

    /// Task 66 — apply the subset of saved GUI preferences that have no CLI
    /// competitor and are not already baked into the EmulatorConfig used for
    /// Emulator::init() (machine type and silent are — see main.cpp's CLI/
    /// AppConfig merge). Called once at startup, after set_emulator().
    void apply_startup_config(const AppConfigData& cfg);

    /// Access to the live, on-disk-backed preferences (Preferences dialog
    /// reads/writes through this).
    AppConfig& app_config() { return app_config_; }

signals:
    /// Emitted when a scale factor is selected from the View menu.
    void scale_requested(int factor);

    /// Emitted when a NEX file should be loaded.
    void load_nex_requested(const QString& path);

    /// Emitted when an SD card image path is selected (informational; requires restart).
    void sd_card_selected(const QString& path);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    /// Task 77 — intercepts Tab/Backtab before Qt's focus-navigation
    /// handling can swallow them, so Tab can act as ZX EXTEND MODE.
    bool event(QEvent* event) override;
    // Kempston-mouse host-event handlers (G43 closure). Each forwards into
    // the MouseDispatcher's transport-agnostic API after translating Qt
    // conventions to SDL ones (button codes, wheel-detent units). The
    // MouseDispatcher is created lazily in set_emulator() once the bound
    // KempstonMouse exists.
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void handle_key(QKeyEvent* event, bool pressed);
    void create_menus();
    void create_toolbar();
    void create_statusbar();

    // Menu action slots
    void on_load_nex();
    void on_mount_sd();
    void on_reset();
    void on_soft_reset();
    void on_machine_type(MachineType type);
    void on_cpu_speed(int speed_idx);
    void on_scale(int factor);
    void on_fullscreen(bool checked);
    void on_about();

    // Tape menu slots
    void on_tape_open();
    void on_tape_eject();
    void on_tape_rewind();
    void on_tape_fast_load(bool checked);
    void update_tape_status();

    // Recording slots
    void on_record_start();
    void on_record_stop();

    // Snapshot save slot (G35: wires SnaSaver to File menu).
    void on_save_snapshot();

    // Preferences dialog (Task 66).
    void on_open_preferences();
    /// The real modal confirmation, used when no ConfirmRestartCallback is
    /// installed (i.e. always, in production).
    bool confirm_machine_restart_dialog(MachineType requested);

    /// Recompute the window's fixed size from the widget + chrome.
    void apply_fixed_window_size();

    EmulatorWidget* emulator_widget_ = nullptr;
    Emulator*       emulator_        = nullptr;
    KeyCallback      key_callback_;
    SpeedCallback    speed_callback_;
    LoadFileCallback load_file_callback_;
    RebootCallback   reboot_callback_;
    ConfirmRestartCallback confirm_restart_callback_;

    // Kempston-mouse host adapter (G43). Mirrors SdlApp's wiring pattern;
    // owned here because MainWindow is the GUI's host event source. Created
    // in set_emulator() once Emulator::mouse() is bound.
    std::unique_ptr<MouseDispatcher> mouse_dispatcher_;

    /// Pointer capture (issue #37). A Kempston mouse is a RELATIVE device:
    /// it reports motion, never position, so the host pointer has to be
    /// confined or it runs into the window edge and the guest pointer stops
    /// dead. While captured we hide the host cursor, warp it back to the
    /// viewport centre after every motion event, and feed the raw delta.
    /// While not captured the mouse belongs to the desktop and nothing is
    /// forwarded, so menus and window controls behave normally.
    bool     mouse_captured_ = false;
    QAction* capture_mouse_action_ = nullptr;
    /// Motion policy for the captured pointer (warp-echo suppression and the
    /// stale first delta). Pure, and unit-tested — see pointer_capture.h.
    pointer_capture::Policy capture_policy_;
    /// Title without any capture suffix, captured at construction.
    QString  base_window_title_;

    void set_mouse_captured(bool on);
    /// Global position of the emulator viewport centre — the point the
    /// pointer is warped back to. Falls back to the window centre when the
    /// viewport is not realised yet.
    QPoint viewport_centre_global() const;

    bool is_fullscreen_ = false;
    int current_scale_ = 1;  ///< Default 1× scale (640×512 viewport — post-G104, was 2× / 1280×1024 pre-G104).

    // Status bar labels
    QLabel* fps_label_     = nullptr;
    QLabel* speed_label_   = nullptr;
    QLabel* machine_label_ = nullptr;

    // Machine type action group
    QActionGroup* machine_type_group_ = nullptr;

    // CPU speed action group
    QActionGroup* speed_group_ = nullptr;

    // Scale action group
    QActionGroup* scale_group_ = nullptr;

    // Fullscreen action
    QAction* fullscreen_action_ = nullptr;

    // CRT filter action
    QAction* crt_filter_action_ = nullptr;

#ifdef ENABLE_DEBUGGER
    DebuggerManager* debugger_mgr_ = nullptr;
#endif

    // Debugger toggle action (in View menu)
    QAction* debugger_action_ = nullptr;

    // Tape menu actions
    QAction* tape_eject_action_  = nullptr;
    QAction* tape_rewind_action_ = nullptr;
    QAction* tape_fast_action_   = nullptr;
    QLabel*  tape_label_         = nullptr;

    // Debug menu actions
    QAction* magic_bp_action_    = nullptr;

    // Recording menu actions
    QAction* record_start_action_ = nullptr;
    QAction* record_stop_action_  = nullptr;

    // Emulator speed action group
    QActionGroup* emu_speed_group_ = nullptr;
    QLabel* emu_speed_label_ = nullptr;

    // Task 79 — Input menu: per-connector source radio actions.
    // joy_source_action_[conn][0] = SDL gamepad, [conn][1] = cursor keys.
    QAction* joy_source_action_[2][2] = { { nullptr, nullptr }, { nullptr, nullptr } };
    // Live-apply + persist a per-connector source change from the Input menu.
    void on_joy_source_selected(int connector, JoySource src);

    // Screenshot action
    QAction* screenshot_action_ = nullptr;

    // Task 66 — persisted GUI preferences (~/.jnext/jnext.conf).
    // Loaded in the constructor; saved whenever the Preferences dialog is
    // applied, and write-through updated by file dialogs that remember a
    // directory (Load Program, Open Tape, Save Screenshot, Mount SD Card).
    AppConfig app_config_;
};
