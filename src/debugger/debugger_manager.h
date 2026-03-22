#pragma once

#include <QObject>
#include <QDockWidget>
#include <QAction>
#include <QToolBar>

class Emulator;
class QMainWindow;
class CpuPanel;
class DisasmPanel;

/// Manages all debugger panels, menus, and toolbar.
/// Created by MainWindow when ENABLE_DEBUGGER is defined.
class DebuggerManager : public QObject {
    Q_OBJECT
public:
    explicit DebuggerManager(QMainWindow* main_window, Emulator* emulator, QObject* parent = nullptr);

    /// Refresh all visible panels with current emulator state.
    /// Called from on_frame_tick() at ~4Hz during run, immediately on step/breakpoint.
    void refresh_panels();

    /// Check if debugger has caused a pause (breakpoint hit etc.)
    /// and emit appropriate signals.
    void check_breakpoint_hit();

public slots:
    void on_run();
    void on_pause();
    void on_step_into();
    void on_step_over();
    void on_step_out();

signals:
    void paused();
    void resumed();

private:
    void create_debug_menu();
    void create_debug_toolbar();
    void create_panels();
    void update_actions(); // enable/disable actions based on pause state

    QMainWindow* main_window_;
    Emulator* emulator_;

    // Actions
    QAction* run_action_ = nullptr;
    QAction* pause_action_ = nullptr;
    QAction* step_into_action_ = nullptr;
    QAction* step_over_action_ = nullptr;
    QAction* step_out_action_ = nullptr;

    QToolBar* debug_toolbar_ = nullptr;

    // Panels
    CpuPanel* cpu_panel_ = nullptr;
    QDockWidget* cpu_dock_ = nullptr;
    DisasmPanel* disasm_panel_ = nullptr;
    QDockWidget* disasm_dock_ = nullptr;

    // Refresh throttle
    int refresh_counter_ = 0;
    static constexpr int REFRESH_INTERVAL = 12; // refresh every 12 frames ~ 4Hz at 50Hz

    // Track previous pause state for breakpoint detection
    bool was_paused_ = false;
};
