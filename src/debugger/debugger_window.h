#pragma once

#include <QMainWindow>
#include <QSettings>

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
class QDockWidget;

/// Separate window that hosts all debugger dock panels.
/// Closing this window disables the debugger and resumes emulation.
class DebuggerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DebuggerWindow(Emulator* emulator, QWidget* parent = nullptr);

    void refresh_panels();

    /// Create the debug toolbar with step/run buttons connected to the manager.
    void set_debugger_manager(DebuggerManager* mgr);

    /// Save window position to QSettings (called before hide/close).
    void save_position();

    /// Position this window to the right of the given main window.
    void position_next_to(QWidget* main_win);

    /// Activate follow-PC in the disassembly panel.
    void activate_follow_pc();

    // Panel accessors for signal wiring
    DisasmPanel* disasm_panel() { return disasm_panel_; }

signals:
    void window_closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void create_panels();
    void save_geometry();
    void restore_geometry();

    Emulator* emulator_;
    DebuggerManager* debugger_mgr_ = nullptr;

    // Panels
    CpuPanel* cpu_panel_ = nullptr;
    QDockWidget* cpu_dock_ = nullptr;
    DisasmPanel* disasm_panel_ = nullptr;
    QDockWidget* disasm_dock_ = nullptr;
    MemoryPanel* memory_panel_ = nullptr;
    QDockWidget* memory_dock_ = nullptr;
    VideoPanel* video_panel_ = nullptr;
    QDockWidget* video_dock_ = nullptr;
    SpritePanel* sprite_panel_ = nullptr;
    QDockWidget* sprite_dock_ = nullptr;
    CopperPanel* copper_panel_ = nullptr;
    QDockWidget* copper_dock_ = nullptr;
    NextRegPanel* nextreg_panel_ = nullptr;
    QDockWidget* nextreg_dock_ = nullptr;
    AudioPanel* audio_panel_ = nullptr;
    QDockWidget* audio_dock_ = nullptr;
};
