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
class QSplitter;
class QTabWidget;

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
    DisasmPanel* disasm_panel() { return disasm_panel_; }
    WatchPanel* watch_panel() { return watch_panel_; }

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

    // Menu bar actions (owned by this window)
    QAction* run_action_ = nullptr;
    QAction* pause_action_ = nullptr;
    QAction* step_into_action_ = nullptr;
    QAction* step_over_action_ = nullptr;
    QAction* step_out_action_ = nullptr;
};
