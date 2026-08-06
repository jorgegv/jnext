#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/watch_panel.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/callstack_panel.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "video/renderer.h"
#include "core/clock.h"
#include "debug/debug_state.h"
#include "debug/disasm.h"

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

DebuggerManager::DebuggerManager(QMainWindow* main_window, Emulator* emulator, QObject* parent)
    : QObject(parent)
    , main_window_(main_window)
    , emulator_(emulator)
{
    // Start with debugger DISABLED — no performance impact.
    emulator_->debug_state().set_active(false);

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

    was_paused_ = false;
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
        emulator_->debug_state().set_active(true);
        emulator_->call_stack().set_enabled(true);

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

        // Set panel paused state based on current emulator state.
        bool is_paused = emulator_->debug_state().paused();
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(is_paused);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(is_paused);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(is_paused);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(is_paused);

        // Refresh panels immediately.
        debugger_window_->refresh_panels();
    } else {
        // Resume if paused, then deactivate.
        if (emulator_->debug_state().paused()) {
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
            emulator_->debug_state().resume();
            was_paused_ = false;
            emit resumed();
        }

        emulator_->debug_state().set_active(false);
        emulator_->call_stack().set_enabled(false);

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

void DebuggerManager::ensure_window() {
    if (debugger_window_)
        return;

    debugger_window_ = new DebuggerWindow(emulator_, nullptr);
    debugger_window_->set_debugger_manager(this);

    // Closing the debugger window disables the debugger.
    connect(debugger_window_, &DebuggerWindow::window_closed, this, [this]() {
        set_enabled(false);
    });

    // Wire disasm panel "run to" signal and set symbol/watch pointers.
    if (auto* dp = debugger_window_->disasm_panel()) {
        connect(dp, &DisasmPanel::run_to_requested, this, [this, dp](uint16_t addr) {
            // Task 60e: "Run to Here" resumes execution — gate it too.
            if (!confirm_resume_if_corrupt()) return;
            emulator_->debug_state().run_to(addr);
            was_paused_ = false;
            dp->set_paused(false);
            if (debugger_window_->cpu_panel())
                debugger_window_->cpu_panel()->set_paused(false);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(false);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(false);
            emit resumed();
            update_actions();
        });

        dp->set_symbol_table(&symbol_table_);
        if (debugger_window_->watch_panel())
            dp->set_watch_panel(debugger_window_->watch_panel());
    }

    // Wire call stack panel with symbol table.
    if (auto* cs = debugger_window_->callstack_panel()) {
        cs->set_symbol_table(&symbol_table_);
    }

    // Wire breakpoint panel with symbol table and disasm panel.
    if (auto* bp = debugger_window_->breakpoint_panel()) {
        bp->set_symbol_table(&symbol_table_);
        bp->set_disasm_panel(debugger_window_->disasm_panel());
    }
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
    // Only true corruption sets Emulator::last_state_error(); benign
    // step-back/rewind failures (empty buffer, disabled trace, frame out of
    // range) never attempt a restore and leave it empty.
    if (emulator_->last_state_error().empty())
        return;
    const QString sub = QString::fromStdString(emulator_->last_state_error());
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
    const bool corrupt = !emulator_->last_state_error().empty();
    const uint64_t gen = emulator_->state_error_generation();
    if (!resume_guard_.needs_confirmation(corrupt, gen))
        return true;   // clean, or this incident already acknowledged

    const QString sub = QString::fromStdString(emulator_->last_state_error());
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
    // corruption bumps the generation and re-prompts.
    resume_guard_.record_ack(gen);
    return true;
}

void DebuggerManager::on_run() {
    if (!enabled_) return;
    // Task 60e: THE choke point — never silently resume a torn machine.
    if (!confirm_resume_if_corrupt()) return;

    emulator_->debug_state().resume();
    was_paused_ = false;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(false);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(false);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(false);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(false);
    }
    emit resumed();
    update_actions();
}

void DebuggerManager::on_pause() {
    if (!enabled_) return;
    emulator_->debug_state().pause();
    was_paused_ = true;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(true);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(true);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(true);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(true);
    }
    emit paused();
    if (debugger_window_) {
        debugger_window_->activate_follow_pc();
        debugger_window_->refresh_panels();
    }
    update_actions();
}

void DebuggerManager::on_step_into() {
    if (!enabled_) return;
    // Task 60e: single-stepping executes a real instruction against the
    // (possibly torn) CPU/MMU — gate it like every other execute path.
    if (!confirm_resume_if_corrupt()) return;

    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    // GH #207 — debugger_step(), not the raw execute_single_instruction()
    // primitive: while we hold the machine nothing else calls run_frame(), so
    // the Step has to turn frames over too, and a Step at a HALT has to run
    // the halt out (the CPU leaves it only on an accepted interrupt).
    emulator_->debugger_step();

    emulator_->debug_state().pause();
    was_paused_ = true;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(true);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(true);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(true);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(true);
    }
    emit paused();
    if (debugger_window_) {
        debugger_window_->activate_follow_pc();
        debugger_window_->refresh_panels();
    }
    update_actions();
}

void DebuggerManager::on_step_over() {
    if (!enabled_) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    auto regs = emulator_->cpu().get_registers();
    uint16_t pc = regs.PC;

    auto read_fn = [this](uint16_t addr) -> uint8_t {
        return emulator_->mmu().read(addr);
    };

    if (is_call_like(pc, read_fn)) {
        int len = instruction_length(pc, read_fn);
        uint16_t next_pc = static_cast<uint16_t>(pc + len);
        emulator_->debug_state().step_over(next_pc);
        was_paused_ = false;
        if (debugger_window_) {
            if (debugger_window_->disasm_panel())
                debugger_window_->disasm_panel()->set_paused(false);
            if (debugger_window_->cpu_panel())
                debugger_window_->cpu_panel()->set_paused(false);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(false);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(false);
        }
        emit resumed();
        update_actions();
    } else {
        on_step_into();
    }
}

void DebuggerManager::on_step_out() {
    if (!enabled_) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    auto regs = emulator_->cpu().get_registers();
    emulator_->debug_state().step_out(regs.SP);
    was_paused_ = false;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(false);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(false);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(false);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(false);
    }
    emit resumed();
    update_actions();
}

void DebuggerManager::on_run_to_eof() {
    if (!enabled_) return;
    if (!emulator_->debug_state().paused()) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    // Target the midpoint of the last visible scanline, so that when the CPU
    // stops every framebuffer row has been rendered and the video panel shows
    // a complete picture.  If we are already past that point in the current
    // frame (e.g. stepping from blanking), target the same scanline in the
    // next frame.
    //
    // The last visible row is framebuffer row FB_HEIGHT-1, and since G164v2
    // (Task 13) `fb_row = raw_vc - vblank_top()` — so the RAW VC we must run
    // to is FB_HEIGHT-1 + vblank_top() (287 on the NEXT family, 303 on
    // Pentagon, 263 on the 60 Hz overrides), not the bare FB_HEIGHT-1 = 255
    // this used to target.  Stopping at raw VC 255 left the bottom 32
    // framebuffer rows undrawn, so "Run to EOF" never actually reached the end
    // of the frame.
    uint64_t frame_start = emulator_->current_frame_cycle();
    const auto& t = emulator_->timing();
    const uint64_t last_vis_vc =
        static_cast<uint64_t>(Renderer::FB_HEIGHT - 1
                              + emulator_->video_timing().vblank_top());
    const uint64_t last_vis_mid = last_vis_vc * t.master_cycles_per_line
                                  + t.master_cycles_per_line / 2;
    uint64_t target = frame_start + last_vis_mid;
    if (emulator_->clock().get() >= target)
        target += t.master_cycles_per_frame;

    emulator_->debug_state().run_to_cycle(target);
    was_paused_ = false;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(false);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(false);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(false);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(false);
    }
    emit resumed();
    update_actions();
}

void DebuggerManager::on_run_to_eosl() {
    if (!enabled_) return;
    if (!emulator_->debug_state().paused()) return;
    if (!confirm_resume_if_corrupt()) return;   // Task 60e

    // Calculate the cycle at the end of the current scanline.
    uint64_t frame_start = emulator_->current_frame_cycle();
    uint64_t elapsed = emulator_->clock().get() - frame_start;
    // Round up to the next scanline boundary.
    const auto& t = emulator_->timing();
    uint64_t next_line_start = ((elapsed / t.master_cycles_per_line) + 1) * t.master_cycles_per_line;
    uint64_t next_line_vc    = next_line_start / t.master_cycles_per_line;
    // Past the last visible row → jump straight to the next frame start.
    //
    // `next_line_vc` is a RAW VC; the visible framebuffer occupies raw VC
    // [vblank_top(), vblank_top()+FB_HEIGHT).  Comparing the raw VC directly
    // against FB_HEIGHT (as this used to) misclassified raw VC 256..287 — the
    // bottom border, framebuffer rows 224..255 — as blanking, so "Run to
    // EOSL" skipped to the next frame instead of the next scanline for the
    // last 32 visible rows.  G164v2 / Task 13.
    const int next_fb_row = static_cast<int>(next_line_vc)
                            - emulator_->video_timing().vblank_top();
    uint64_t target = (next_fb_row >= Renderer::FB_HEIGHT)
                    ? frame_start + t.master_cycles_per_frame
                    : frame_start + next_line_start;

    emulator_->debug_state().run_to_cycle(target);
    was_paused_ = false;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(false);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(false);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(false);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(false);
    }
    emit resumed();
    update_actions();
}

void DebuggerManager::on_step_back() {
    if (!enabled_) return;
    if (!emulator_->rewind_buffer() || emulator_->rewind_buffer()->empty()) return;

    bool ok = emulator_->step_back(1);
    if (!ok) {
        warn_state_corrupt(QObject::tr("Step Back"));
        return;
    }

    was_paused_ = true;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(true);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(true);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(true);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(true);
    }
    emit paused();
    if (debugger_window_) {
        debugger_window_->activate_follow_pc();
        debugger_window_->refresh_panels();
    }
    update_actions();
}

void DebuggerManager::on_rewind_to_frame(uint32_t frame_num) {
    if (!enabled_) return;
    if (!emulator_->rewind_buffer() || emulator_->rewind_buffer()->empty()) return;

    bool ok = emulator_->rewind_to_frame(frame_num);
    if (!ok) {
        warn_state_corrupt(QObject::tr("Rewind To Frame"));
        return;
    }

    was_paused_ = true;
    if (debugger_window_) {
        if (debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(true);
        if (debugger_window_->cpu_panel())
            debugger_window_->cpu_panel()->set_paused(true);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(true);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(true);
    }
    emit paused();
    if (debugger_window_) {
        debugger_window_->activate_follow_pc();
        debugger_window_->refresh_panels();
    }
    update_actions();
}

void DebuggerManager::on_load_map_z88dk() {
    QString path = QFileDialog::getOpenFileName(
        main_window_, QObject::tr("Load Z88DK MAP File"), QString(),
        QObject::tr("MAP Files (*.map);;All Files (*)"));
    if (path.isEmpty())
        return;

    if (symbol_table_.load_z88dk_map(path.toStdString())) {
        QMessageBox::information(main_window_, QObject::tr("MAP Loaded"),
            QObject::tr("Loaded %1 symbols from:\n%2")
                .arg(symbol_table_.size())
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

    int count = symbol_table_.load_simple_map(path.toStdString());
    if (count >= 0) {
        QMessageBox::information(main_window_, QObject::tr("MAP Loaded"),
            QObject::tr("Loaded %1 symbols from:\n%2")
                .arg(symbol_table_.size())
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

    if (emulator_->debug_state().paused()) {
        emulator_->snapshot_raster();
        debugger_window_->refresh_panels();
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
    // Auto-enable debugger when a magic breakpoint (or other external trigger)
    // pauses the emulator while the debugger window is not yet open.
    if (!enabled_ && emulator_->debug_state().paused()) {
        set_enabled(true);
    }

    if (!enabled_)
        return;

    bool is_paused = emulator_->debug_state().paused();

    // Detect transition from running to paused (breakpoint hit during run_frame).
    if (is_paused && !was_paused_) {
        was_paused_ = true;
        if (debugger_window_) {
            if (debugger_window_->disasm_panel())
                debugger_window_->disasm_panel()->set_paused(true);
            if (debugger_window_->cpu_panel())
                debugger_window_->cpu_panel()->set_paused(true);
        if (debugger_window_->stack_panel())
            debugger_window_->stack_panel()->set_paused(true);
        if (debugger_window_->callstack_panel())
            debugger_window_->callstack_panel()->set_paused(true);
        }
        emit paused();
        if (debugger_window_) {
            debugger_window_->activate_follow_pc();
            debugger_window_->refresh_panels();
        }
        update_actions();
    }
}

// ---------------------------------------------------------------------------
// Action state management
// ---------------------------------------------------------------------------

void DebuggerManager::update_actions() {
    if (debugger_window_) {
        bool is_paused = enabled_ && emulator_->debug_state().paused();
        debugger_window_->update_actions(is_paused);
    }
}
