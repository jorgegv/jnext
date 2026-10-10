#include "debugger/debugger_manager.h"
#include "debugger/script_panel.h"
#include "debugger/debugger_window.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/watch_panel.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/breakpoint_model.h"
#include "debugger/stack_panel.h"
#include "debugger/callstack_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/audio_panel.h"
#include "debugger/memory_panel.h"

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QStyle>
#include <QTimer>
#include <QEvent>
#include <QPainter>
#include <QPixmap>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>

DebuggerManager::DebuggerManager(QMainWindow* main_window, jnext::dbg::Debugger& dbg,
                                 QObject* parent)
    : QObject(parent)
    , main_window_(main_window)
    , dbg_(dbg)
{
    // Start with debugger DISABLED — no performance impact: no ARMING client
    // is attached until the window opens (attach_backend()). The breakpoints'
    // owner is attached now, for the manager's lifetime, and arms nothing
    // (REQ-qt-32; GH #278 WP4c).
    bp_model_ = new BreakpointModel(dbg_, this);

    // Watch main window move/resize to keep debugger sticky.
    main_window_->installEventFilter(this);

    // Find the "Debugger" action in the View menu (created by MainWindow).
    QMenuBar* bar = main_window_->menuBar();
    for (QAction* menu_action : bar->actions()) {
        if (menu_action->menu()) {
            for (QAction* a : menu_action->menu()->actions()) {
                if (a->text().contains("Debugger", Qt::CaseInsensitive) && a->isCheckable()) {
                    enable_action_ = a;
                    break;
                }
            }
        }
        if (enable_action_) break;
    }

    if (enable_action_) {
        connect(enable_action_, &QAction::triggered, this, [this](bool checked) {
            set_enabled(checked);
        });
    }

    create_debug_toolbar();
}

DebuggerManager::~DebuggerManager() {
    // A manager destroyed with its window open (the app tearing down without a
    // close) must not leave a client attached to a backend that outlives it.
    detach_backend();
}

// ---------------------------------------------------------------------------
// The attach policy and the pause-state presentation
// ---------------------------------------------------------------------------

void DebuggerManager::attach_backend() {
    if (client_ != jnext::dbg::CLIENT_NONE) return;
    // SES-01 — the attach arms the machine (§5: armed = attached || persistent)
    // and switches the step machinery on (§4.1 `attached`); SES-05 — the live
    // raster switches the render hint and the raster walk on. Together they are
    // exactly what `DebugState::set_active(true)` switched on before WP2.
    const auto r = dbg_.attach(jnext::dbg::ClientInfo{"Qt GUI", jnext::dbg::ClientKind::Gui});
    client_ = r.value;
    dbg_.set_listener(client_, this);
    dbg_.set_live_raster(client_, true);
    // INS-12 — call-stack tracking while the window is open, as before; through
    // the backend it is also re-applied across a cold boot (CTL-12 rule 2), where
    // the direct `call_stack().set_enabled()` it replaces was silently lost.
    dbg_.set_call_stack_enabled(true);
    set_panels_client();
}

void DebuggerManager::detach_backend() {
    if (client_ == jnext::dbg::CLIENT_NONE) return;
    dbg_.set_call_stack_enabled(false);
    // SES-01 — removes this client's subscriptions (a Step Over or Run to Here
    // target still in flight goes with the window) and its live-raster request.
    // It resumes nothing here: set_enabled(false) has already resumed a paused
    // machine, whoever paused it, before it gets here.
    dbg_.set_listener(client_, nullptr);
    dbg_.detach(client_);
    client_ = jnext::dbg::CLIENT_NONE;
    set_panels_client();
}

void DebuggerManager::set_panels_paused(bool paused) {
    if (!debugger_window_) return;
    if (debugger_window_->disasm_panel())
        debugger_window_->disasm_panel()->set_paused(paused);
    if (debugger_window_->cpu_panel())
        debugger_window_->cpu_panel()->set_paused(paused);
    if (debugger_window_->stack_panel())
        debugger_window_->stack_panel()->set_paused(paused);
    if (debugger_window_->callstack_panel())
        debugger_window_->callstack_panel()->set_paused(paused);
}

void DebuggerManager::set_panels_client() {
    if (!debugger_window_) return;
    if (auto* p = debugger_window_->nextreg_panel()) p->set_client(client_);
    if (auto* p = debugger_window_->audio_panel())   p->set_client(client_);
    if (auto* p = debugger_window_->memory_panel())  p->set_client(client_);
}

void DebuggerManager::apply_pause_state(bool paused) {
    shown_paused_ = paused;
    set_panels_paused(paused);
    if (paused) {
        emit this->paused();
        if (debugger_window_) {
            debugger_window_->activate_follow_pc();
            debugger_window_->refresh_panels();
        }
    } else {
        emit resumed();
    }
    update_actions();
}

bool DebuggerManager::eventFilter(QObject* obj, QEvent* event) {
    if (obj == main_window_) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            reposition_debugger_window();
        }
    }
    return QObject::eventFilter(obj, event);
}

void DebuggerManager::reposition_debugger_window() {
    if (!debugger_window_ || !debugger_window_->isVisible())
        return;
    debugger_window_->position_next_to(main_window_);
}

// ---------------------------------------------------------------------------
// Enable / Disable
// ---------------------------------------------------------------------------

bool DebuggerManager::set_enabled(bool enabled, bool prompt_on_corrupt) {
    if (enabled_ == enabled)
        return true;   // already in the requested state

    enabled_ = enabled;

    if (enabled) {
        // Activate debug checks in the hot loop.
        attach_backend();

        // Create the debugger window lazily.
        ensure_window();
        debugger_window_->show();
        debugger_window_->raise();
        debugger_window_->activateWindow();

        // Position after show — some window managers ignore move() before show().
        // Use a short timer to let the WM finish placing the window first.
        debugger_window_->position_next_to(main_window_);
        QTimer::singleShot(50, this, [this]() {
            if (debugger_window_ && debugger_window_->isVisible())
                debugger_window_->position_next_to(main_window_);
        });

        // Seed the panel paused state from the machine's. Deliberately NOT
        // apply_pause_state(): a pause already in force gets the full
        // pause-edge sequence from the next check_breakpoint_hit(), exactly as
        // before (shown_paused_ is false while the window is closed).
        set_panels_paused(dbg_.state().paused);

        // Refresh panels immediately.
        debugger_window_->refresh_panels();
    } else {
        // Resume if paused, then deactivate.
        if (dbg_.state().paused) {
            // Task 60e: disabling the debugger auto-resumes — route it through
            // the same corruption gate. If the user declines, abort the disable
            // and keep the debugger enabled + paused so they can reset. enabled_
            // was already flipped to false above (and Qt flips the checkable
            // action to unchecked BEFORE emitting triggered()), so restore both
            // the state and every UI affordance, then report the decline so
            // callers (both closeEvents) don't hide/close a still-live debugger.
            //
            // Task 60f: the app-quit path passes prompt_on_corrupt=false — the
            // machine is being destroyed, so the gate does not apply and the
            // disable proceeds unconditionally (never returns false).
            if (prompt_on_corrupt && !confirm_resume_if_corrupt()) {
                enabled_ = true;
                if (enable_action_)
                    enable_action_->setChecked(true);
                update_actions();
                emit enabled_changed(true);   // re-check the toolbar Debug button
                return false;
            }
            if (!prompt_on_corrupt) {
                // Task 60f — the app-quit path is not gated (see the header):
                // the machine is about to be destroyed. The backend's run()
                // refuses an unacknowledged corruption (CTL-11), so the quit
                // path acknowledges it without asking — the resume it has always
                // done still happens, and nothing survives to be protected.
                if (const auto inc = dbg_.resume_blocked_by_corruption())
                    dbg_.acknowledge_corruption(inc->generation);
            }
            // Whoever paused it — this window, a breakpoint, the magic
            // breakpoint, another client: closing the debugger resumes the
            // machine, as it always has.
            dbg_.run(client_);
            shown_paused_ = false;
            emit resumed();
        }
        shown_paused_ = false;

        // GH #278 WP4c — nothing else to clear. The legacy `active()` bit this
        // used to clear (set by the magic-breakpoint hook and, before WP3, by
        // the rewind paths) is retired: a magic stop now holds the machine only
        // until the resume above releases it (DebugState's magic hold), so the
        // detach leaves the machine unarmed — PBPUI-09.
        detach_backend();

        if (debugger_window_) {
            debugger_window_->save_position();
            debugger_window_->hide();
        }
    }

    // Sync the menu checkmark.
    if (enable_action_)
        enable_action_->setChecked(enabled);

    update_actions();
    emit enabled_changed(enabled);
    return true;
}

void DebuggerManager::set_keymap(const jnext::dbgkeys::Keymap& km) {
    keymap_ = km;
    if (debugger_window_)
        debugger_window_->set_keymap(km);
}

void DebuggerManager::ensure_window() {
    if (debugger_window_)
        return;

    debugger_window_ = new DebuggerWindow(dbg_, nullptr);
    debugger_window_->set_debugger_manager(this);
    // GH #1 — BEFORE anything else touches the window. The window is created
    // lazily (the first time the debugger is enabled, from the menu, a magic
    // breakpoint or anywhere else), so a rebind made while it did not exist
    // has to be applied here or it is silently lost. Measured, not reasoned
    // about: rebinding and then opening the debugger for the first time left
    // the toolbar on the old keys.
    debugger_window_->set_keymap(keymap_);

    // Closing the debugger window disables the debugger.
    connect(debugger_window_, &DebuggerWindow::window_closed, this, [this]() {
        set_enabled(false);
    });

    // Wire disasm panel "run to" signal and set symbol/watch pointers.
    if (auto* dp = debugger_window_->disasm_panel()) {
        connect(dp, &DisasmPanel::run_to_requested, this, [this](uint16_t addr) {
            // Task 60e: "Run to Here" resumes execution — gate it too.
            if (!confirm_resume_if_corrupt()) return;
            // CTL-06 — a transient Execute at addr, then run.
            if (dbg_.run_to(client_, addr) != jnext::dbg::Result::Ok) return;
            apply_pause_state(false);
        });

        // GH #278 WP6 — the ONE symbol table is the backend's (CAP-SYM): the
        // panels read it, the Map menu loads into it.
        dp->set_symbol_table(&dbg_.symbols());
        if (debugger_window_->watch_panel())
            dp->set_watch_panel(debugger_window_->watch_panel());
    }

    // Wire call stack panel with symbol table.
    if (auto* cs = debugger_window_->callstack_panel()) {
        cs->set_symbol_table(&dbg_.symbols());
    }

    // Wire breakpoint panel with symbol table. It needs no pointer to the
    // disassembly: both panels follow the GUI's BreakpointModel (GH #220; GH
    // #278 WP4c).
    if (auto* bp = debugger_window_->breakpoint_panel()) {
        bp->set_symbol_table(&dbg_.symbols());
        bp->set_model(bp_model_);
    }
    if (auto* dp = debugger_window_->disasm_panel())
        dp->set_breakpoint_model(bp_model_);
    // GH #278 WP4c — a watch is a peek through the backend (§3.5).
    if (auto* wp = debugger_window_->watch_panel())
        wp->set_backend(&dbg_);
    // GH #278 WP4b — whose client the panels' writes are (the window is built
    // after the attach that opens it).
    set_panels_client();
}

// ---------------------------------------------------------------------------
// Toolbar (minimal: just a toggle button in the main window)
// ---------------------------------------------------------------------------

void DebuggerManager::create_debug_toolbar() {
    debug_toolbar_ = main_window_->addToolBar(QObject::tr("Debug"));
    debug_toolbar_->setMovable(false);

    // Draw a simple bug icon
    QPixmap bug_pix(24, 24);
    bug_pix.fill(Qt::transparent);
    {
        QPainter p(&bug_pix);
        p.setRenderHint(QPainter::Antialiasing);
        // Body (dark green oval)
        p.setBrush(QColor(40, 120, 40));
        p.setPen(Qt::NoPen);
        p.drawEllipse(7, 8, 10, 12);
        // Head
        p.drawEllipse(9, 4, 6, 6);
        // Legs (3 pairs)
        p.setPen(QPen(QColor(40, 120, 40), 1.5));
        p.drawLine(7, 11, 3, 8);   p.drawLine(17, 11, 21, 8);
        p.drawLine(7, 14, 3, 14);  p.drawLine(17, 14, 21, 14);
        p.drawLine(7, 17, 3, 20);  p.drawLine(17, 17, 21, 20);
        // Antennae
        p.drawLine(10, 5, 7, 1);   p.drawLine(14, 5, 17, 1);
    }
    QAction* dbg_toggle = debug_toolbar_->addAction(
        QIcon(bug_pix), QObject::tr("Debug"));
    dbg_toggle->setCheckable(true);
    dbg_toggle->setChecked(false);
    // Alt+D, not Ctrl+D (#115): this button shares the View > Debugger action's
    // binding, which moved off Ctrl because Ctrl is the guest's Symbol Shift.
    // A tooltip naming the old chord would tell the user to press the very
    // sequence the fix hands back to the guest — Ctrl+D types SS+D (STEP).
    // host_hotkey_test H115-28 pins this string against the real shortcut.
    dbg_toggle->setToolTip(QObject::tr("Toggle Debugger (Alt+D)"));
    connect(dbg_toggle, &QAction::triggered, this, [this](bool checked) {
        set_enabled(checked);
    });
    connect(this, &DebuggerManager::enabled_changed, dbg_toggle, &QAction::setChecked);
}

// ---------------------------------------------------------------------------
// Debug control slots
// ---------------------------------------------------------------------------

void DebuggerManager::warn_state_corrupt(const QString& op) {
    // CTL-11 — the incident the failed restore just latched. Called only for
    // the backend's RefusedCorrupt, which it returns only when the restore
    // bumped the corruption generation, so the incident is fresh and
    // unacknowledged; the guard is for a caller that got that wrong.
    const auto incident = dbg_.resume_blocked_by_corruption();
    if (!incident)
        return;
    const QString sub = QString::fromStdString(incident->subsystem);
    if (main_window_->statusBar()) {
        main_window_->statusBar()->showMessage(
            QObject::tr("%1 failed: snapshot restore desynced at '%2' — machine "
                        "state is corrupt; reset to recover").arg(op, sub), 10000);
    }
    QMessageBox::warning(
        main_window_,
        QObject::tr("Rewind Failed"),
        QObject::tr("%1 could not restore the machine snapshot (subsystem '%2' "
                    "desynced).\n\nThe machine is now in a corrupt, partially-"
                    "restored state and has been paused. Resuming may crash or "
                    "behave unpredictably — reset the machine (Machine ▸ Power "
                    "Reset) to recover cleanly.").arg(op, sub));
}

bool DebuggerManager::confirm_resume_if_corrupt() {
    // CTL-11 — the backend's guard: empty when the machine is clean or this
    // incident is already acknowledged.
    const auto incident = dbg_.resume_blocked_by_corruption();
    if (!incident)
        return true;

    const QString sub = QString::fromStdString(incident->subsystem);
    QMessageBox::StandardButton btn = QMessageBox::warning(
        main_window_,
        QObject::tr("Machine State Corrupt"),
        QObject::tr("The machine state is corrupt after a failed rewind/step-"
                    "back (subsystem '%1' did not restore cleanly).\n\n"
                    "Resuming or stepping may crash or behave unpredictably. "
                    "Reset the machine (Machine ▸ Power Reset) to recover "
                    "cleanly.\n\n"
                    "Proceed anyway?").arg(sub),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (btn != QMessageBox::Yes)
        return false;   // caller must abort

    // Acknowledge THIS incident so we don't re-prompt on every following
    // action — but leave last_state_error() set: acknowledging does not heal
    // the desync, so the breadcrumb survives until an actual reset. A new
    // corruption bumps the generation and re-prompts. Acknowledged IN THE
    // BACKEND, whose verbs would otherwise refuse the resume the user just
    // agreed to (RefusedCorrupt).
    dbg_.acknowledge_corruption(incident->generation);
    return true;
}

void DebuggerManager::on_run() {
    if (!enabled_) return;
    // GH #223: Run on an already-running machine is a no-op, exactly as in
    // on_run_to_eof() / on_run_to_eosl() below. Without this, F5 pressed out of
    // habit at the emulator window reached the resume, which threw away a
    // pending Run to Here / step-over target. Ordered BEFORE
    // confirm_resume_if_corrupt() — same as the siblings — because that call
    // can raise a modal, and prompting the user about a resume we are about to
    // refuse would be a question about nothing. (The backend's run() is a
    // no-op on a running machine too; this guard is the modal's.)
    if (!dbg_.state().paused) return;
    // Task 60e: THE choke point — never silently resume a torn machine.
    if (!confirm_resume_if_corrupt()) return;

    // CTL-02 — the GH #221 step-off is the backend's.
    if (dbg_.run(client_) != jnext::dbg::Result::Ok) return;
    apply_pause_state(false);
}

void DebuggerManager::on_pause() {
    if (!enabled_) return;
    dbg_.pause(client_);   // CTL-01
    apply_pause_state(true);
}

void DebuggerManager::on_step_into() {
    if (!enabled_) return;
    // Task 60e: single-stepping executes a real instruction against the
    // (possibly torn) CPU/MMU — gate it like every other execute path.
    if (!confirm_resume_if_corrupt()) return;

    // CTL-03 — synchronous: pauses a running machine first, runs one
    // instruction through debugger_step() (the frame loop turned over, a HALT
    // run out — GH #207), and leaves the machine paused.
    if (dbg_.step_into(client_) != jnext::dbg::Result::Ok) return;
    apply_pause_state(true);
}

void DebuggerManager::on_step_over() {
    if (!enabled_) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    // CTL-04 — the call-like decision is the backend's: over a CALL / RST /
    // DJNZ it arms a transient Execute at the next instruction and RUNS;
    // anything else is a Step Into and stays paused. Which of the two happened
    // is the machine's state, not something to re-derive here.
    if (dbg_.step_over(client_) != jnext::dbg::Result::Ok) return;
    apply_pause_state(dbg_.state().paused);
}

void DebuggerManager::on_step_out() {
    if (!enabled_) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    // CTL-05 — run until a return pops past the current stack depth (GH #203).
    if (dbg_.step_out(client_) != jnext::dbg::Result::Ok) return;
    apply_pause_state(false);
}

void DebuggerManager::on_run_to_eof() {
    if (!enabled_) return;
    if (!dbg_.state().paused) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    // CTL-08 — the midpoint of the last VISIBLE scanline (raw VC
    // FB_HEIGHT-1 + vblank_top, G164v2), or of the next frame's if already past
    // it; the arithmetic moved to the backend verbatim.
    if (dbg_.run_to_end_of_frame(client_) != jnext::dbg::Result::Ok) return;
    apply_pause_state(false);
}

void DebuggerManager::on_run_to_eosl() {
    if (!enabled_) return;
    if (!dbg_.state().paused) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    // CTL-08 — the next scanline start; past the last visible framebuffer row,
    // the next frame's start (G164v2).
    if (dbg_.run_to_end_of_scanline(client_) != jnext::dbg::Result::Ok) return;
    apply_pause_state(false);
}

void DebuggerManager::on_step_back() {
    if (!enabled_) return;
    // CTL-09 — the three outcomes are the backend's: Ok (the machine is one
    // instruction back, paused), a benign refusal — RefusedRzx (an RZX is
    // recording or playing) or RefusedUnavailable (empty buffer, trace off or
    // empty, or a replay that would cross a debugger change, §4.2a) — whose
    // reason, logged by the backend and recorded by on_log(), goes to the
    // window's status bar — and RefusedCorrupt (the restore tore the machine),
    // the only one that warns with a modal. A refusal still on show from an
    // earlier verb is cleared first: it describes that verb, not this one.
    rewind_refusal_.clear();
    if (debugger_window_) debugger_window_->clear_rewind_refusal();
    const jnext::dbg::Result r = dbg_.step_back(client_, 1);
    if (r == jnext::dbg::Result::RefusedCorrupt) {
        warn_state_corrupt(QObject::tr("Step Back"));
        return;
    }
    if (r != jnext::dbg::Result::Ok) {
        show_rewind_refusal();
        return;
    }

    apply_pause_state(true);
}

void DebuggerManager::on_log(jnext::dbg::LogLevel, const std::string& text) {
    static const std::string kTag = "REWIND REFUSED: ";
    if (text.compare(0, kTag.size(), kTag) != 0) return;
    std::string msg = text.substr(kTag.size());
    // The backend tags a client's line " [client N]"; the status bar is this
    // client's own, so the tag says nothing there.
    if (const auto at = msg.rfind(" [client "); at != std::string::npos) msg.erase(at);
    rewind_refusal_ = msg;
}

void DebuggerManager::show_rewind_refusal() {
    if (rewind_refusal_.empty() || !debugger_window_) return;
    debugger_window_->show_rewind_refusal(QString::fromStdString(rewind_refusal_));
}

void DebuggerManager::on_rewind_to_frame(uint32_t frame_num) {
    if (!enabled_) return;
    // CTL-10 — as on_step_back(): a frame outside the ring is RefusedUnavailable
    // (its reason in the status bar), a torn restore RefusedCorrupt (warned).
    rewind_refusal_.clear();
    if (debugger_window_) debugger_window_->clear_rewind_refusal();
    const jnext::dbg::Result r = dbg_.rewind_to_frame(client_, frame_num);
    if (r == jnext::dbg::Result::RefusedCorrupt) {
        warn_state_corrupt(QObject::tr("Rewind To Frame"));
        return;
    }
    if (r != jnext::dbg::Result::Ok) {
        show_rewind_refusal();
        return;
    }

    apply_pause_state(true);
}

void DebuggerManager::on_load_map_z88dk() {
    QString path = QFileDialog::getOpenFileName(
        main_window_, QObject::tr("Load Z88DK MAP File"), QString(),
        QObject::tr("MAP Files (*.map);;All Files (*)"));
    if (path.isEmpty())
        return;

    // CAP-SYM — the backend's load_map(): Ok with the count, or refused when
    // the file cannot be read (the loader's -1). NOT a bool test of the count
    // (GH #278 WP0): a readable map with no `; addr` symbols loads 0 and is
    // still "MAP Loaded"; a failed read never reports success over the old
    // table.
    if (dbg_.load_map(path.toStdString(), jnext::dbg::MapFormat::Z88dk)) {
        QMessageBox::information(main_window_, QObject::tr("MAP Loaded"),
            QObject::tr("Loaded %1 symbols from:\n%2")
                .arg(dbg_.symbols().size())
                .arg(path));
    } else {
        QMessageBox::warning(main_window_, QObject::tr("Load Failed"),
            QObject::tr("Could not load MAP file:\n%1").arg(path));
    }
}

void DebuggerManager::on_load_map_simple() {
    QString path = QFileDialog::getOpenFileName(
        main_window_, QObject::tr("Load Simple MAP File"), QString(),
        QObject::tr("MAP Files (*.map);;All Files (*)"));
    if (path.isEmpty())
        return;

    if (dbg_.load_map(path.toStdString(), jnext::dbg::MapFormat::Simple)) {
        QMessageBox::information(main_window_, QObject::tr("MAP Loaded"),
            QObject::tr("Loaded %1 symbols from:\n%2")
                .arg(dbg_.symbols().size())
                .arg(path));
    } else {
        QMessageBox::warning(main_window_, QObject::tr("Load Failed"),
            QObject::tr("Could not load MAP file:\n%1").arg(path));
    }
}

// ---------------------------------------------------------------------------
// Panel refresh
// ---------------------------------------------------------------------------

void DebuggerManager::refresh_panels() {
    if (!enabled_ || !debugger_window_)
        return;

    // GH #278 WP0 — while paused the actions are refreshed with the panels.
    // Their enabled state has inputs no verb touches: an RZX recording or
    // playback started from the main window, the rewind buffer freed by Rewind
    // Buffer Size... = 0. Recomputed only by the verbs, Step Back and Jump Here
    // stayed enabled over them until the next verb, and the click was then
    // refused. (While running every such action is off whatever those inputs
    // are, and the verb that resumed has already said so.)
    if (dbg_.state().paused) {
        // The raster the panels show is the backend's (INS-06), which takes a
        // paused machine's snapshot at the query itself (GH #278 WP7).
        debugger_window_->refresh_panels();
        update_actions();
    } else {
        // Throttle refresh during running to ~4Hz.
        ++refresh_counter_;
        if (refresh_counter_ >= REFRESH_INTERVAL) {
            refresh_counter_ = 0;
            debugger_window_->refresh_panels();
        }
    }
}

void DebuggerManager::check_breakpoint_hit() {
    // GH #278 WP4c — another client's breakpoint change, pushed in the pump
    // that just ran, reaches the Breakpoints panel and the gutter now: the
    // model's listener only recorded it (REQ-qt-15b).
    bp_model_->sync();

    // GH #278 WP2 — the pause state is the BACKEND'S (CTL-13 `state()`), pulled
    // here once per tick, after the loop owner's pump (qt-frontend.md §4 as
    // built: no pause epoch exists, and a pull needs none — see there).
    const jnext::dbg::RunState st = dbg_.state();
    const bool paused = st.paused;

    // Auto-enable debugger when a magic breakpoint (or other external trigger)
    // pauses the emulator while the debugger window is not yet open.
    //
    // GH #219 made this the FRONTEND half of --persistent-breakpoints: an
    // ordinary breakpoint can now fire with the window shut, and this is what
    // forces it open on the hit. set_enabled(true) show()s, raise()s and
    // activateWindow()s, so no GUI call is needed anywhere in the core.
    // Pinned by debugger_persistent_bp_test PBPUI-02. Owner Q5: another
    // client's pause opens it too (PBPUI-08). The ONE exception is GH #306: a
    // pause made by the emulator window's own Pause control (matched by client
    // id, so another Gui/observer client still opens it) is exactly what that
    // control exists to do WITHOUT the debugger window.
    const bool own_pause =
        st.pause_reason.kind == jnext::dbg::PauseReason::Kind::User &&
        emulator_window_pause_client_ != jnext::dbg::CLIENT_NONE &&
        st.pause_reason.by == emulator_window_pause_client_;
    if (!enabled_ && paused && !own_pause) {
        set_enabled(true);
    }

    if (!enabled_)
        return;

    // Bring the window to the machine's state when they differ: a pause it has
    // not shown (breakpoint hit during run_frame(), a finished Step Over / Step
    // Out / run-to, another client's pause) gets the pause-edge sequence; a
    // resume it did not cause (another client's run) gets the running one.
    if (paused != shown_paused_)
        apply_pause_state(paused);
}

// ---------------------------------------------------------------------------
// Action state management
// ---------------------------------------------------------------------------

void DebuggerManager::update_actions() {
    if (debugger_window_) {
        bool is_paused = enabled_ && dbg_.state().paused;
        debugger_window_->update_actions(is_paused);
    }
}

void DebuggerManager::set_script_host(jnext::script::ScriptHost* host) {
    script_host_ = host;
    if (debugger_window_ && debugger_window_->script_panel())
        debugger_window_->script_panel()->set_host(host);
}
