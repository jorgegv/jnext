#pragma once

#include <QObject>
#include <QAction>
#include <QToolBar>
#include <memory>

#include "debug/symbol_table.h"
#include "debug/resume_guard.h"

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
    void on_run_to_eof();
    void on_run_to_eosl();
    void on_step_back();
    void on_rewind_to_frame(uint32_t frame_num);
    void on_load_map_z88dk();
    void on_load_map_simple();

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

    /// Task 60e: if the emulator flagged a corrupt state after a failed
    /// rewind/step-back (Emulator::last_state_error() non-empty), surface it
    /// to the user (status bar + modal warning). No-op on a benign failure
    /// (empty buffer, trace off, frame out of range) where no restore was
    /// attempted. `op` names the operation for the message.
    void warn_state_corrupt(const QString& op);

    /// Task 60e — THE single choke point every resume/step/execute path must
    /// pass through. Returns true if it is safe to proceed. If the machine is
    /// corrupt (failed rewind, Task 60b) and this incident has not yet been
    /// acknowledged, it shows a modal Yes/No warning (default No): on Yes it
    /// records the acknowledgment and returns true; on No it returns false and
    /// the caller MUST abort (stay paused). Every new corruption incident
    /// re-prompts. When the machine is clean it is a cheap no-op returning
    /// true. The pure policy lives in ResumeGuard so it can be unit-tested.
    bool confirm_resume_if_corrupt();

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

    // Task 60e — per-incident acknowledgment state for the corruption gate.
    ResumeGuard resume_guard_;

    // Refresh throttle
    int refresh_counter_ = 0;
    static constexpr int REFRESH_INTERVAL = 12;

    // Track previous pause state for breakpoint detection
    bool was_paused_ = false;
};
