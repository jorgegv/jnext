#pragma once

#include <QObject>
#include <QAction>
#include <QToolBar>
#include <memory>

#include "debug/symbol_table.h"

class Emulator;
class QMainWindow;
class DebuggerWindow;

/// Manages the debugger enable/disable state, debug menu actions, toolbar,
/// and the separate debugger window with all panels.
/// Created by MainWindow when ENABLE_DEBUGGER is defined.
class DebuggerManager : public QObject {
    Q_OBJECT
public:
    explicit DebuggerManager(QMainWindow* main_window, Emulator* emulator, QObject* parent = nullptr);

    /// Is the debugger currently enabled (window visible, breakpoint checks active)?
    bool is_enabled() const { return enabled_; }

    /// Enable or disable the debugger at runtime.
    void set_enabled(bool enabled);

    /// Refresh all visible panels with current emulator state.
    /// Called from on_frame_tick() — does nothing when debugger is disabled.
    void refresh_panels();

    /// Check if debugger has caused a pause (breakpoint hit etc.)
    /// and emit appropriate signals. Does nothing when disabled.
    void check_breakpoint_hit();

    /// Access the debugger window (may be null if not yet created).
    DebuggerWindow* debugger_window_ptr() const { return debugger_window_; }

    /// Access the symbol table.
    SymbolTable& symbol_table() { return symbol_table_; }
    const SymbolTable& symbol_table() const { return symbol_table_; }

    /// Access emulator (for debugger window menu actions).
    Emulator* emulator() const { return emulator_; }

public slots:
    void on_run();
    void on_pause();
    void on_step_into();
    void on_step_over();
    void on_step_out();
    void on_load_map_z88dk();

signals:
    void paused();
    void resumed();
    void enabled_changed(bool enabled);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void create_debug_toolbar();
    void ensure_window();
    void update_actions();
    void reposition_debugger_window();

    QMainWindow* main_window_;
    Emulator* emulator_;

    bool enabled_ = false;

    // The separate debugger window (created lazily on first enable)
    DebuggerWindow* debugger_window_ = nullptr;

    // Enable/disable action (points to View menu's Debugger action)
    QAction* enable_action_ = nullptr;

    QToolBar* debug_toolbar_ = nullptr;

    // Symbol table for loaded MAP files
    SymbolTable symbol_table_;

    // Refresh throttle
    int refresh_counter_ = 0;
    static constexpr int REFRESH_INTERVAL = 12;

    // Track previous pause state for breakpoint detection
    bool was_paused_ = false;
};
