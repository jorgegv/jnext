#pragma once

#include <QMainWindow>
#include <QSettings>
#include "debug/breakpoints.h"

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

    /// Position this window to the right of the given main window.
    void position_next_to(QWidget* main_win);

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
};
