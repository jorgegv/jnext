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

DebuggerManager::DebuggerManager(QMainWindow* main_window, Emulator* emulator, QObject* parent)
    : QObject(parent)
    , main_window_(main_window)
    , emulator_(emulator)
{
    // Start with debugger DISABLED — no performance impact.
    emulator_->debug_state().set_active(false);

    // Watch main window move/resize to keep debugger sticky.
    main_window_->installEventFilter(this);

    create_debug_menu();
    create_debug_toolbar();

    was_paused_ = false;
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

        // Set disasm panel paused state based on current emulator state.
        if (debugger_window_->disasm_panel()) {
            debugger_window_->disasm_panel()->set_paused(
                emulator_->debug_state().paused());
        }

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
// Menu
// ---------------------------------------------------------------------------

void DebuggerManager::create_debug_menu() {
    QMenuBar* bar = main_window_->menuBar();

    // Find the existing Debug menu (created by MainWindow with trace items).
    QMenu* debug_menu = nullptr;
    for (QAction* a : bar->actions()) {
        if (a->text().contains("Debug", Qt::CaseInsensitive) && a->menu()) {
            debug_menu = a->menu();
            break;
        }
    }

    // If no existing Debug menu, create one before Help.
    if (!debug_menu) {
        debug_menu = new QMenu(QObject::tr("&Debug"), bar);
        QAction* before_action = nullptr;
        for (QAction* a : bar->actions()) {
            if (a->text().contains("Help", Qt::CaseInsensitive)) {
                before_action = a;
                break;
            }
        }
        bar->insertMenu(before_action, debug_menu);
    }

    // Insert debugger actions at the top of the menu (before existing trace items).
    QAction* first_action = debug_menu->actions().isEmpty() ? nullptr : debug_menu->actions().first();

    // Enable/Disable Debugger (checkable toggle)
    enable_action_ = new QAction(QObject::tr("Enable &Debugger"), debug_menu);
    enable_action_->setCheckable(true);
    enable_action_->setChecked(false);
    enable_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(enable_action_, &QAction::triggered, this, [this](bool checked) {
        set_enabled(checked);
    });
    debug_menu->insertAction(first_action, enable_action_);

    debug_menu->insertSeparator(first_action);

    run_action_ = new QAction(QObject::tr("&Run / Continue"), debug_menu);
    run_action_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(run_action_, &QAction::triggered, this, &DebuggerManager::on_run);
    debug_menu->insertAction(first_action, run_action_);

    pause_action_ = new QAction(QObject::tr("&Pause"), debug_menu);
    pause_action_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(pause_action_, &QAction::triggered, this, &DebuggerManager::on_pause);
    debug_menu->insertAction(first_action, pause_action_);

    debug_menu->insertSeparator(first_action);

    step_into_action_ = new QAction(QObject::tr("Step &Into"), debug_menu);
    step_into_action_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(step_into_action_, &QAction::triggered, this, &DebuggerManager::on_step_into);
    debug_menu->insertAction(first_action, step_into_action_);

    step_over_action_ = new QAction(QObject::tr("Step &Over"), debug_menu);
    step_over_action_->setShortcut(QKeySequence(Qt::Key_F10));
    connect(step_over_action_, &QAction::triggered, this, &DebuggerManager::on_step_over);
    debug_menu->insertAction(first_action, step_over_action_);

    step_out_action_ = new QAction(QObject::tr("Step Ou&t"), debug_menu);
    step_out_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    connect(step_out_action_, &QAction::triggered, this, &DebuggerManager::on_step_out);
    debug_menu->insertAction(first_action, step_out_action_);

    // Separator before trace submenu
    debug_menu->insertSeparator(first_action);
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void DebuggerManager::create_debug_toolbar() {
    debug_toolbar_ = main_window_->addToolBar(QObject::tr("Debug"));
    debug_toolbar_->setMovable(false);

    // Debugger enable toggle button
    QAction* dbg_toggle = debug_toolbar_->addAction(
        main_window_->style()->standardIcon(QStyle::SP_ComputerIcon),
        QObject::tr("Debugger"));
    dbg_toggle->setCheckable(true);
    dbg_toggle->setChecked(false);
    dbg_toggle->setToolTip(QObject::tr("Enable/Disable Debugger (Ctrl+D)"));
    connect(dbg_toggle, &QAction::triggered, this, [this](bool checked) {
        set_enabled(checked);
    });
    // Keep in sync with enable_action_
    connect(this, &DebuggerManager::enabled_changed, dbg_toggle, &QAction::setChecked);

    debug_toolbar_->addSeparator();

    QAction* run_btn = debug_toolbar_->addAction(
        main_window_->style()->standardIcon(QStyle::SP_MediaPlay), QObject::tr("Run"));
    connect(run_btn, &QAction::triggered, this, &DebuggerManager::on_run);

    QAction* pause_btn = debug_toolbar_->addAction(
        main_window_->style()->standardIcon(QStyle::SP_MediaPause), QObject::tr("Pause"));
    connect(pause_btn, &QAction::triggered, this, &DebuggerManager::on_pause);

}

// ---------------------------------------------------------------------------
// Debug control slots
// ---------------------------------------------------------------------------

void DebuggerManager::on_run() {
    if (!enabled_) return;
    emulator_->debug_state().resume();
    was_paused_ = false;
    if (debugger_window_ && debugger_window_->disasm_panel())
        debugger_window_->disasm_panel()->set_paused(false);
    emit resumed();
    update_actions();
}

void DebuggerManager::on_pause() {
    if (!enabled_) return;
    emulator_->debug_state().pause();
    was_paused_ = true;
    if (debugger_window_ && debugger_window_->disasm_panel())
        debugger_window_->disasm_panel()->set_paused(true);
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
    if (debugger_window_ && debugger_window_->disasm_panel())
        debugger_window_->disasm_panel()->set_paused(true);
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
        if (debugger_window_ && debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(false);
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
    if (debugger_window_ && debugger_window_->disasm_panel())
        debugger_window_->disasm_panel()->set_paused(false);
    emit resumed();
    update_actions();
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
        if (debugger_window_ && debugger_window_->disasm_panel())
            debugger_window_->disasm_panel()->set_paused(true);
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
    bool is_paused = enabled_ && emulator_->debug_state().paused();

    // Debug control actions only available when debugger is enabled.
    if (run_action_)       run_action_->setEnabled(enabled_ && is_paused);
    if (pause_action_)     pause_action_->setEnabled(enabled_ && !is_paused);
    if (step_into_action_) step_into_action_->setEnabled(enabled_ && is_paused);
    if (step_over_action_) step_over_action_->setEnabled(enabled_ && is_paused);
    if (step_out_action_)  step_out_action_->setEnabled(enabled_ && is_paused);
}
