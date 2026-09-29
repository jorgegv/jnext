#pragma once

#include <QObject>
#include <QAction>
#include <QToolBar>
#include <memory>

#include "debug/debug_keymap.h"
#include "debug/debugger.h"
#include "debug/symbol_table.h"

class Emulator;
class QMainWindow;
class DebuggerWindow;

/// Manages the debugger enable/disable state, debug menu actions, toolbar,
/// and the separate debugger window with all panels.
/// Created by MainWindow when ENABLE_DEBUGGER is defined.
///
/// GH #278 WP2 — the Qt ADAPTER over the debugger backend (`jnext::dbg::Debugger`,
/// `debug/debugger.h`). The control verbs — run, pause, step into/over/out,
/// run to here, run to end of frame / scanline — are the backend's; so are the
/// pause state the adapter reads, and the corruption observables the resume
/// modal asks about (CTL-11). What stays here is Qt: the modal, the window, the
/// panels' paused/running presentation, the Qt signals.
///
/// THE ATTACH POLICY (qt-frontend.md §4 as built; manager decision 2026-09-29):
/// the adapter is a backend CLIENT exactly while the debugger window is open —
/// attached by `set_enabled(true)`, detached by `set_enabled(false)` — and
/// requests the live raster for as long. Not for the process lifetime: an
/// attached client ARMS the machine, and a window that armed breakpoints while
/// closed would change GH #219's default (`debugger_persistent_bp_test`
/// PBPUI-03). A pause the window did not cause still opens it: the pause state
/// is PULLED from the backend on every tick, whoever is attached.
///
/// The panels (`DebuggerWindow` and its 13) still take the `Emulator*` until
/// WP4/WP7 move them onto the backend; so do the rewind verbs until WP3.
class DebuggerManager : public QObject {
    Q_OBJECT
public:
    /// `dbg` is the loop owner's backend for `emulator` (`QtApp::debugger()`) —
    /// the ONE `Debugger` of that machine; the adapter never constructs its own.
    /// It must outlive the manager.
    DebuggerManager(QMainWindow* main_window, jnext::dbg::Debugger& dbg,
                    Emulator* emulator, QObject* parent = nullptr);
    ~DebuggerManager() override;

    /// Is the debugger currently enabled (window visible, breakpoint checks active)?
    bool is_enabled() const { return enabled_; }

    /// Enable or disable the debugger at runtime. Returns true if the
    /// debugger is in the requested state afterwards; returns FALSE only when
    /// a disable was DECLINED — the machine is corrupt (Task 60b) and the user
    /// refused to resume it (Task 60e), so the debugger stays enabled + paused.
    /// Callers that hide/close UI on disable must honour a false return.
    ///
    /// Task 60f: `prompt_on_corrupt` (default true) applies the Task 60e
    /// resume-gate on the disable/auto-resume path. Pass FALSE only from the
    /// app-quit path (MainWindow::closeEvent): quitting destroys the whole
    /// machine, so there is nothing to protect — the gate must not fire (it
    /// cannot be declined) and the disable always succeeds and returns true.
    bool set_enabled(bool enabled, bool prompt_on_corrupt = true);

    /// Refresh all visible panels with current emulator state.
    /// Called from on_frame_tick() — does nothing when debugger is disabled.
    void refresh_panels();

    /// Once per frame tick, after the loop owner's pump: read the backend's
    /// pause state and bring the window to it. A pause the window has not shown
    /// yet — a breakpoint, a watchpoint, a magic breakpoint, a finished step,
    /// another client's pause — is shown, opening the window first if it is
    /// closed (GH #219; owner Q5 for a remote client); a resume the window did
    /// not cause is shown too.
    void check_breakpoint_hit();

    /// Access the debugger window (may be null if not yet created).
    DebuggerWindow* debugger_window_ptr() const { return debugger_window_; }

    /// GH #1 — the user's debugger key bindings. Held HERE rather than only
    /// pushed at the window, because the window is created lazily: a rebind
    /// made before the debugger has ever been opened would otherwise be lost,
    /// and ensure_window() would build a window on the compiled-in defaults.
    /// Applies immediately when a window already exists.
    void set_keymap(const jnext::dbgkeys::Keymap& km);

    /// Access the symbol table.
    SymbolTable& symbol_table() { return symbol_table_; }
    const SymbolTable& symbol_table() const { return symbol_table_; }

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

    /// Issue #39: re-snap the debugger window to the emulator window's edge.
    /// Called on every main-window Move/Resize, and by the debugger's own
    /// Window > "Attach to Emulator Window" toggle when it is switched back on.
    /// A no-op when the debugger window is hidden, detached, or the platform
    /// forbids self-positioning.
    void reposition_debugger_window();

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

    /// THE ATTACH POLICY, in one place (see the class comment): become a
    /// backend client and request the live raster / stop being one.
    /// Idempotent.
    void attach_backend();
    void detach_backend();

    /// The four paused-only panels (CPU, Disassembly, Stack, Call Stack).
    void set_panels_paused(bool paused);

    /// THE ONE pause-state transition of the window — the eleven copies of the
    /// panel sequence every verb used to carry. Paused: the four panels
    /// frozen, `paused()`, the disassembly re-centred on PC, a full refresh,
    /// the paused actions. Running: the four panels released, `resumed()`, the
    /// running actions. Records what the window now shows, which is what
    /// check_breakpoint_hit() compares the backend's state against.
    void apply_pause_state(bool paused);

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
    /// true. The policy, and the acknowledgment, are the BACKEND'S (CTL-11:
    /// `resume_blocked_by_corruption()` / `acknowledge_corruption()`), so the
    /// modal gates exactly what the backend's verbs would refuse.
    bool confirm_resume_if_corrupt();

    QMainWindow* main_window_;
    jnext::dbg::Debugger& dbg_;
    /// For what WP2 does not move: the DebuggerWindow and its panels (WP4/WP7),
    /// the rewind verbs and their warning (WP3), the raster snapshot before a
    /// paused refresh (WP4d), and the legacy `DebugState::active()` bit the
    /// window still CLEARS on close (see set_enabled()).
    Emulator* emulator_;

    bool enabled_ = false;

    /// The backend client this window is while it is open (CLIENT_NONE while
    /// closed). Every verb is attributed to it.
    jnext::dbg::ClientId client_ = jnext::dbg::CLIENT_NONE;

    // The separate debugger window (created lazily on first enable)
    DebuggerWindow* debugger_window_ = nullptr;
    jnext::dbgkeys::Keymap keymap_;      // GH #1

    // Enable/disable action (points to View menu's Debugger action)
    QAction* enable_action_ = nullptr;

    QToolBar* debug_toolbar_ = nullptr;

    // Symbol table for loaded MAP files
    SymbolTable symbol_table_;

    // Refresh throttle
    int refresh_counter_ = 0;
    static constexpr int REFRESH_INTERVAL = 12;

    /// Which pause state the window last SHOWN (apply_pause_state()). Not a
    /// copy of the machine's state — that is the backend's, read on every tick
    /// — but the window's own: check_breakpoint_hit() applies the machine's
    /// state when the two differ. The enable seeds set the panels without it,
    /// so a pause found already in force when the window opens still gets the
    /// full pause-edge sequence on the next tick, as it always has.
    bool shown_paused_ = false;
};
