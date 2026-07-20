#pragma once

#include <QMainWindow>
#include <QSettings>
#include "debug/breakpoints.h"
#include "debugger/window_attach.h"

class Emulator;
class DebuggerManager;
class CpuPanel;
class DisasmPanel;
class MemoryPanel;
class VideoPanel;
class SpritePanel;
class CopperPanel;
class NextRegPanel;
class AudioPanel;
class WatchPanel;
class BreakpointPanel;
class MmuPanel;
class StackPanel;
class CallStackPanel;
class QPushButton;
class QSplitter;
class QTabWidget;
class QSlider;
class QLabel;
class QToolBar;

/// Separate window that hosts all debugger panels.
/// Closing this window disables the debugger and resumes emulation.
class DebuggerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DebuggerWindow(Emulator* emulator, QWidget* parent = nullptr);

    void refresh_panels();

    /// Wire up the debugger manager and create menus/toolbar.
    void set_debugger_manager(DebuggerManager* mgr);

    /// Save window position to QSettings (called before hide/close).
    void save_position();

    /// Re-attach this window to the right-hand edge of the given main window.
    /// Honours the Window > "Attach to Emulator Window" toggle, stands down
    /// while the main window is fullscreen, and does nothing at all on a
    /// window system that forbids a client positioning its own toplevels
    /// (Wayland) — see src/debugger/window_attach.h. Safe to call on every
    /// move/resize of the main window.
    void position_next_to(QWidget* main_win);

    /// Is the debugger currently set to follow the emulator window?
    bool attach_enabled() const { return attach_enabled_; }

    /// Activate follow-PC in the disassembly panel.
    void activate_follow_pc();

    /// Update debug action enabled state based on pause state.
    void update_actions(bool is_paused);

    // Panel accessors for signal wiring
    CpuPanel* cpu_panel() { return cpu_panel_; }
    DisasmPanel* disasm_panel() { return disasm_panel_; }
    WatchPanel* watch_panel() { return watch_panel_; }
    BreakpointPanel* breakpoint_panel() { return breakpoint_panel_; }
    StackPanel* stack_panel() { return stack_panel_; }
    CallStackPanel* callstack_panel() { return callstack_panel_; }

signals:
    void window_closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void create_panels();
    void create_menus();
    void save_geometry();
    void restore_geometry();
    void set_attach_enabled(bool on);
    /// Stop attaching after the window system repeatedly ignored our moves,
    /// and tell the user — visibly, and recoverably.
    void give_up_on_attachment();
    void show_add_data_bp_dialog(WatchType type);
    void show_rewind_buffer_size_dialog();
    void update_trace_indicator();
    void update_rewind_ui();

    Emulator* emulator_;
    DebuggerManager* debugger_mgr_ = nullptr;

    // Layout
    QSplitter* main_splitter_ = nullptr;
    QSplitter* top_splitter_ = nullptr;
    QTabWidget* tab_widget_ = nullptr;

    // Panels
    CpuPanel* cpu_panel_ = nullptr;
    DisasmPanel* disasm_panel_ = nullptr;
    MemoryPanel* memory_panel_ = nullptr;
    VideoPanel* video_panel_ = nullptr;
    SpritePanel* sprite_panel_ = nullptr;
    CopperPanel* copper_panel_ = nullptr;
    NextRegPanel* nextreg_panel_ = nullptr;
    AudioPanel* audio_panel_ = nullptr;
    WatchPanel* watch_panel_ = nullptr;
    BreakpointPanel* breakpoint_panel_ = nullptr;
    MmuPanel* mmu_panel_ = nullptr;
    StackPanel* stack_panel_ = nullptr;
    CallStackPanel* callstack_panel_ = nullptr;

    // Issue #39 — window attachment. `attach_enabled_` is the user's toggle,
    // persisted alongside the window size. `attach_supported_` says whether the
    // window system honours a client-issued move: seeded from the platform name
    // at construction (false on Wayland) and then CORRECTED BY MEASUREMENT — a
    // move that is repeatedly ignored latches it false, because the platform
    // name alone proved not to be trustworthy (an XWayland "xcb" session drops
    // moves exactly like native Wayland). When false, no move is attempted,
    // since it would be silently dropped.
    //
    // The two are independent on purpose. A MEASURED give-up clears
    // `attach_supported_` and leaves the menu item ENABLED (re-ticking it
    // retries from a clean slate) and leaves `attach_enabled_` — the persisted
    // preference — untouched. Only a platform that cannot position windows at
    // all disables the item, because there a retry is guaranteed to fail.
    bool     attach_enabled_ = true;
    bool     attach_supported_ = true;
    // Set when the PLATFORM NAME says positioning is impossible (Wayland).
    // Distinct from attach_supported_: a platform-blocked backend can never
    // work, so its menu item stays permanently disabled, whereas a merely
    // MEASURED give-up is recoverable by re-enabling the toggle.
    bool     attach_platform_blocked_ = false;
    QAction* attach_action_ = nullptr;
    // Sequences the deferred landing checks and counts consecutive misses.
    // Pure logic, unit-tested in test/debugger/window_attach_test.cpp.
    jnext::AttachMoveTracker attach_tracker_;

    // Trace toolbar state
    QPushButton* trace_toggle_btn_ = nullptr;
    QAction* trace_enable_action_ = nullptr;

    // Menu bar actions (owned by this window)
    QAction* run_action_ = nullptr;
    QAction* pause_action_ = nullptr;
    QAction* step_into_action_ = nullptr;
    QAction* step_over_action_ = nullptr;
    QAction* step_out_action_ = nullptr;
    QAction* step_back_action_ = nullptr;
    QAction* rewind_enable_action_ = nullptr;

    // Rewind toolbar (second bottom toolbar, shown when rewind buffer has data)
    QToolBar* rewind_toolbar_ = nullptr;
    QSlider*  rewind_slider_ = nullptr;
    QLabel*   rewind_frame_label_ = nullptr;
    QPushButton* rewind_jump_btn_ = nullptr;
    bool      rewind_slider_dragging_ = false;

    // Frame count used by the live Enable Rewind toggle (Task 27 A1b).
    // Remembers the last size applied via the Rewind Buffer Size... dialog
    // within this session; defaults to the pre-A1 CLI default of 500.
    int       last_rewind_frames_ = 500;
};
