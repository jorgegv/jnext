#pragma once

#include <QDeadlineTimer>
#include <QMainWindow>
#include <QSettings>
#include "debug/debug_keymap.h"
#include "debugger/window_attach.h"

class BreakpointModel;
class DebuggerManager;
namespace jnext { namespace dbg { class Debugger; } }
class CpuPanel;
class DisasmPanel;
class MemoryPanel;
class VideoPanel;
class SpritePanel;
class CopperPanel;
class ScriptPanel;
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
    /// `dbg` — the debugger backend the panels read through (GH #278 WP4a-d,
    /// WP5: every panel).
    explicit DebuggerWindow(jnext::dbg::Debugger& dbg, QWidget* parent = nullptr);

    /// GH #312: merge a legacy <config-dir>/Debugger.conf into jnext.conf
    /// [debugger] and remove it. Idempotent; true only if it migrated.
    static bool migrate_legacy_config();

    void refresh_panels();

    /// Show why a rewind (Step Back, Frame Back, the slider) was refused, in
    /// this window's status bar, for kRewindRefusalMs; the rewind status line
    /// update_rewind_ui() keeps there does not overwrite it meanwhile.
    void show_rewind_refusal(const QString& msg);
    static constexpr int kRewindRefusalMs = 10000;
    /// A new rewind verb is running: a refusal still on show describes the
    /// previous one, so it goes, and the rewind status line takes the status
    /// bar back.
    void clear_rewind_refusal();

    /// Wire up the debugger manager and create menus/toolbar.
    void set_debugger_manager(DebuggerManager* mgr);

    /// Save window position to QSettings (called before hide/close).
    void save_position();

    /// GH #1 — install the user's debugger key bindings. Safe to call before
    /// or after set_debugger_manager(), and safe to call again whenever
    /// Preferences changes them: it re-derives every shortcut AND every
    /// toolbar label from scratch, so a rebind can never leave a button
    /// advertising a key that no longer works.
    void set_keymap(const jnext::dbgkeys::Keymap& km);

    const jnext::dbgkeys::Keymap& keymap() const { return keymap_; }

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
    NextRegPanel* nextreg_panel() { return nextreg_panel_; }
    AudioPanel* audio_panel() { return audio_panel_; }
    MemoryPanel* memory_panel() { return memory_panel_; }
    /// GH #26 WP5 — the Script tab.
    ScriptPanel* script_panel() { return script_panel_; }
    /// GH #26 WP5 — the eight script host-key actions (Alt+1..Alt+8), index
    /// 0..7 for keys 1..8. Not menu items: window-wide shortcuts, the
    /// `run_to_cursor_action_` pattern. Null before set_debugger_manager().
    QAction* script_key_action(int index) const {
        return index >= 0 && index < 8 ? script_key_actions_[index] : nullptr;
    }

signals:
    void window_closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void create_panels();
    void create_menus();
    void save_geometry();
    void restore_geometry();
    /// GH #114 — open a DEFAULT-sized window at the size where nothing has to
    /// scroll, when the screen has room for it. Runs off the first show, since
    /// it measures how much panel area the scroll area is currently hiding.
    void grow_default_size_to_natural();
    void set_attach_enabled(bool on);
    /// Stop attaching after the window system repeatedly ignored our moves,
    /// and tell the user — visibly, and recoverably.
    void give_up_on_attachment();
    /// `type` is a BreakpointModel::Type: Read, Write or ReadWrite.
    void show_add_data_bp_dialog(int type);
    /// GH #215 — Execute is the ordinary PC breakpoint, reached first; it has
    /// its own dialog title and menu entry.
    void show_add_exec_bp_dialog();
    /// Modal "Address (hex):" prompt shared by both of the above. Returns false
    /// when the user cancels or types something that is not hex.
    bool prompt_bp_address(const QString& title, uint16_t& addr);
    void show_rewind_buffer_size_dialog();
    void update_trace_indicator();
    void update_rewind_ui();
    /// GH #278 — the rewind UI's frame numbering; see debugger_window.cpp.
    uint32_t rewind_position() const;
    void frame_back();
    /// GH #278 WP3 — the backend the manager adapts: the rewind toolbar and
    /// menu, the trace menu and the action greying read it, not the Emulator.
    /// Null only before set_debugger_manager().
    jnext::dbg::Debugger* backend() const;
    /// GH #278 WP4c — the GUI's breakpoints (the manager's), for the
    /// Breakpoints menu. Null only before set_debugger_manager().
    BreakpointModel* breakpoint_model() const;
    /// Export Trace (button and menu): the file dialog, the backend's INS-13
    /// trace_export(), and "Export Failed" when it refuses.
    void export_trace();
    /// GH #1 — push keymap_ onto the actions and the toolbar text.
    void apply_keymap();

    jnext::dbg::Debugger& dbg_;
    DebuggerManager* debugger_mgr_ = nullptr;

    // GH #114 — false once a size restored from the config file is in use, so
    // grow_default_size_to_natural() leaves a deliberately small window alone.
    bool size_is_default_ = true;
    // GH #114 — the opening fit runs once per window, not on every re-show,
    // and re-measures a bounded number of times (see grow_default_size_to_natural).
    bool initial_fit_done_ = false;
    int  initial_fit_passes_ = 0;
    static constexpr int kMaxInitialFitPasses = 4;

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
    ScriptPanel* script_panel_ = nullptr;   // GH #26 WP5
    QAction* script_key_actions_[8] = {};    // GH #26 WP5

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

    // GH #1 — the user's key bindings. Defaults until set_keymap() says
    // otherwise, so a DebuggerWindow built without a config behaves exactly
    // as it did before this existed.
    jnext::dbgkeys::Keymap keymap_;

    // Menu bar actions (owned by this window)
    QAction* run_action_ = nullptr;
    QAction* pause_action_ = nullptr;
    QAction* step_into_action_ = nullptr;
    QAction* step_over_action_ = nullptr;
    QAction* step_out_action_ = nullptr;
    QAction* step_back_action_ = nullptr;
    QAction* frame_back_action_ = nullptr;
    QAction* run_to_eof_action_ = nullptr;
    QAction* run_to_eosl_action_ = nullptr;
    QAction* trace_export_action_ = nullptr;
    /// GH #1 — "Run to Cursor" as a bindable window command. Deliberately NOT
    /// in any menu: the disassembly panel's Enter key and its "Run to Here"
    /// context entry are how it is discovered, and adding a menu item would
    /// change the menu shape that debugger_accel_test pins. Unbound by
    /// default, so it does nothing until the user binds it.
    QAction* run_to_cursor_action_ = nullptr;
    QAction* rewind_enable_action_ = nullptr;

    // GH #1 — toolbar buttons whose captions or tooltips quote a key. They are
    // members because a rebind has to rewrite them; a button that still says
    // "F5: Continue" after F5 stopped being Continue is a lie the user cannot
    // see through.
    QPushButton* export_trace_btn_ = nullptr;
    QPushButton* continue_btn_     = nullptr;
    QPushButton* frame_back_btn_   = nullptr;
    QPushButton* step_back_btn_    = nullptr;
    QPushButton* step_into_btn_    = nullptr;
    QPushButton* step_over_btn_    = nullptr;
    QPushButton* step_out_btn_     = nullptr;
    QPushButton* run_to_eosl_btn_  = nullptr;
    QPushButton* run_to_eof_btn_   = nullptr;
    QPushButton* break_btn_        = nullptr;

    // Rewind toolbar (second bottom toolbar, shown when rewind buffer has data)
    QToolBar* rewind_toolbar_ = nullptr;
    QSlider*  rewind_slider_ = nullptr;
    QLabel*   rewind_frame_label_ = nullptr;
    QPushButton* rewind_jump_btn_ = nullptr;
    bool      rewind_slider_dragging_ = false;
    /// While a rewind refusal is on show (show_rewind_refusal()), the rewind
    /// status line leaves the status bar alone.
    QDeadlineTimer rewind_refusal_until_{0};

    // Frame count used by the live Enable Rewind toggle (Task 27 A1b).
    // Remembers the last size applied via the Rewind Buffer Size... dialog
    // within this session; defaults to the pre-A1 CLI default of 500.
    int       last_rewind_frames_ = 500;
};
