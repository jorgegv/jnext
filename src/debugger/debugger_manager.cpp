#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "core/emulator.h"
#include "debug/debug_state.h"
#include "debug/disasm.h"

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QStyle>
#include <QTimer>
#include <QEvent>
#include <QFileDialog>
#include <QMessageBox>

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

void DebuggerManager::set_enabled(bool enabled) {
    if (enabled_ == enabled)
        return;

    enabled_ = enabled;

    if (enabled) {
        // Activate debug checks in the hot loop.
        emulator_->debug_state().set_active(true);

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

        // Refresh panels immediately.
        debugger_window_->refresh_panels();
    } else {
        // Resume if paused, then deactivate.
        if (emulator_->debug_state().paused()) {
            emulator_->debug_state().resume();
            was_paused_ = false;
            emit resumed();
        }

        emulator_->debug_state().set_active(false);

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

    // Wire disasm panel "run to" signal.
    if (auto* dp = debugger_window_->disasm_panel()) {
        connect(dp, &DisasmPanel::run_to_requested, this, [this](uint16_t addr) {
            emulator_->debug_state().run_to(addr);
            was_paused_ = false;
            emit resumed();
            update_actions();
        });
    }
}

// ---------------------------------------------------------------------------
// Toolbar (minimal: just a toggle button in the main window)
// ---------------------------------------------------------------------------

void DebuggerManager::create_debug_toolbar() {
    debug_toolbar_ = main_window_->addToolBar(QObject::tr("Debug"));
    debug_toolbar_->setMovable(false);

    QAction* dbg_toggle = debug_toolbar_->addAction(
        main_window_->style()->standardIcon(QStyle::SP_MessageBoxWarning),
        QObject::tr("Debug"));
    dbg_toggle->setCheckable(true);
    dbg_toggle->setChecked(false);
    dbg_toggle->setToolTip(QObject::tr("Toggle Debugger (Ctrl+D)"));
    connect(dbg_toggle, &QAction::triggered, this, [this](bool checked) {
        set_enabled(checked);
    });
    connect(this, &DebuggerManager::enabled_changed, dbg_toggle, &QAction::setChecked);
}

// ---------------------------------------------------------------------------
// Debug control slots
// ---------------------------------------------------------------------------

void DebuggerManager::on_run() {
    if (!enabled_) return;
    emulator_->debug_state().resume();
    was_paused_ = false;
    emit resumed();
    update_actions();
}

void DebuggerManager::on_pause() {
    if (!enabled_) return;
    emulator_->debug_state().pause();
    was_paused_ = true;
    emit paused();
    if (debugger_window_) {
        debugger_window_->activate_follow_pc();
        debugger_window_->refresh_panels();
    }
    update_actions();
}

void DebuggerManager::on_step_into() {
    if (!enabled_) return;

    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    emulator_->execute_single_instruction();

    emulator_->debug_state().pause();
    was_paused_ = true;
    emit paused();
    if (debugger_window_) {
        debugger_window_->activate_follow_pc();
        debugger_window_->refresh_panels();
    }
    update_actions();
}

void DebuggerManager::on_step_over() {
    if (!enabled_) return;

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
        emit resumed();
        update_actions();
    } else {
        on_step_into();
    }
}

void DebuggerManager::on_step_out() {
    if (!enabled_) return;

    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    auto regs = emulator_->cpu().get_registers();
    emulator_->debug_state().step_out(regs.SP);
    was_paused_ = false;
    emit resumed();
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

// ---------------------------------------------------------------------------
// Panel refresh
// ---------------------------------------------------------------------------

void DebuggerManager::refresh_panels() {
    if (!enabled_ || !debugger_window_)
        return;

    if (emulator_->debug_state().paused()) {
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
    if (!enabled_)
        return;

    bool is_paused = emulator_->debug_state().paused();

    // Detect transition from running to paused (breakpoint hit during run_frame).
    if (is_paused && !was_paused_) {
        was_paused_ = true;
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
