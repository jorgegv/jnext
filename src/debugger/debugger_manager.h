#pragma once

#include <QObject>
#include <QAction>
#include <QToolBar>
#include <memory>

#include "debug/debug_keymap.h"
#include "debug/debugger.h"

class BreakpointModel;
namespace jnext { namespace script { class ScriptHost; } }
class QMainWindow;
class DebuggerWindow;

/// Manages the debugger enable/disable state, debug menu actions, toolbar,
/// and the separate debugger window with all panels.
/// Created by MainWindow when ENABLE_DEBUGGER is defined.
///
/// GH #278 WP2 — the Qt ADAPTER over the debugger backend (`jnext::dbg::Debugger`,
/// `debug/debugger.h`). The control verbs — run, pause, step into/over/out,
/// run to here, run to end of frame / scanline — are the backend's, and since
/// WP3 so are step back and rewind to frame (CTL-09/10); so are the pause state
/// the adapter reads, and the corruption observables the resume modal and the
/// rewind warning ask about (CTL-11). What stays here is Qt: the modal, the window, the
/// panels' paused/running presentation, the Qt signals.
///
/// THE ATTACH POLICY (qt-frontend.md §4 as built; manager decisions
/// 2026-09-29): the adapter is TWO backend clients.
///   * The WINDOW's client, attached exactly while the debugger window is open
///     — by `set_enabled(true)`, detached by `set_enabled(false)` — which
///     requests the live raster for as long and to which every verb — and
///     every NextREG / Audio panel write (WP4b) — is attributed. Not for the
///     process lifetime: an attached client ARMS the
///     machine, and a window that armed breakpoints while closed would change
///     GH #219's default (`debugger_persistent_bp_test` PBPUI-03).
///   * The GUI's BREAKPOINTS' owner, a non-arming OBSERVER client (REQ-qt-32)
///     held by the BreakpointModel for the manager's lifetime (GH #278 WP4c),
///     so the user's breakpoints outlive the window without arming anything.
/// A pause the window did not cause still opens it: the pause state is PULLED
/// from the backend on every tick, whoever is attached.
///
/// The window hands the backend to its panels; since WP4a-d eleven of the 13
/// read through it (the Breakpoints panel through the manager's
/// BreakpointModel), and the Disassembly and Memory panels still take the
/// `Emulator*` until WP5 moves them. The window reaches the backend through
/// backend() for its rewind, trace and action controls (WP3).
class DebuggerManager : public QObject {
    Q_OBJECT
public:
    /// `dbg` is the loop owner's backend (`QtApp::debugger()`) — the ONE
    /// `Debugger` of that machine; the adapter never constructs its own, and
    /// reaches the machine through nothing else (GH #278 WP7). It must outlive
    /// the manager.
    DebuggerManager(QMainWindow* main_window, jnext::dbg::Debugger& dbg,
                    QObject* parent = nullptr);
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

    /// GH #278 WP3 — the backend this adapter drives, for the DebuggerWindow's
    /// own controls (rewind toolbar and menu, trace menu, action greying),
    /// which read it rather than the Emulator.
    jnext::dbg::Debugger& backend() const { return dbg_; }

    /// GH #278 WP4c — the GUI's breakpoints, as backend subscriptions owned by
    /// the observer client. The Breakpoints panel, the disassembly and the
    /// window's Breakpoints menu all edit this one model.
    BreakpointModel& breakpoints() const { return *bp_model_; }

    /// GH #26 WP5 — the loop owner's script host (`QtApp`'s, the one
    /// `--script` loads into), for the Script tab. Null = no scripting.
    void set_script_host(jnext::script::ScriptHost* host);
    jnext::script::ScriptHost* script_host() const { return script_host_; }

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

    /// GH #278 WP4b/WP5 — the three panels that WRITE through the backend
    /// (NextREG, Audio, Memory) attribute their writes to the window's client: `client_`, which
    /// is CLIENT_NONE while the window is closed. Pushed on every attach and
    /// detach, and when the window is built.
    void set_panels_client();

    /// THE ONE pause-state transition of the window — the eleven copies of the
    /// panel sequence every verb used to carry. Paused: the four panels
    /// frozen, `paused()`, the disassembly re-centred on PC, a full refresh,
    /// the paused actions. Running: the four panels released, `resumed()`, the
    /// running actions. Records what the window now shows, which is what
    /// check_breakpoint_hit() compares the backend's state against.
    void apply_pause_state(bool paused);

    /// Task 60e: a step back or rewind whose restore tore the machine (the
    /// backend's `RefusedCorrupt`) is surfaced to the user — status bar and a
    /// modal naming the subsystem from the backend's incident (CTL-11). Called
    /// ONLY for `RefusedCorrupt`: a benign refusal (`RefusedUnavailable` —
    /// empty buffer, trace off, frame out of range — or `RefusedRzx`) never
    /// reaches it. `op` names the operation for the message.
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

    /// GH #278 WP4c — owned (a QObject child); holds the observer client.
    BreakpointModel* bp_model_ = nullptr;

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
    jnext::script::ScriptHost* script_host_ = nullptr;   // GH #26 WP5
};
