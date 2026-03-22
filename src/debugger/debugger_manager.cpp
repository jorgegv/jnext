#include "debugger/debugger_manager.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "core/emulator.h"
#include "debug/debug_state.h"
#include "debug/disasm.h"

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QAction>
#include <QStyle>

DebuggerManager::DebuggerManager(QMainWindow* main_window, Emulator* emulator, QObject* parent)
    : QObject(parent)
    , main_window_(main_window)
    , emulator_(emulator)
{
    // Activate the debug state so breakpoint checks run in the hot loop.
    emulator_->debug_state().set_active(true);

    create_debug_menu();
    create_debug_toolbar();
    create_panels();

    // Start in running state.
    was_paused_ = false;
    update_actions();
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

void DebuggerManager::create_debug_menu() {
    // Insert Debug menu before the Help menu.
    QMenuBar* bar = main_window_->menuBar();
    QMenu* debug_menu = new QMenu(QObject::tr("&Debug"), bar);

    // Find the Help menu to insert before it.
    QAction* before_action = nullptr;
    for (QAction* a : bar->actions()) {
        if (a->text().contains("Help", Qt::CaseInsensitive)) {
            before_action = a;
            break;
        }
    }
    bar->insertMenu(before_action, debug_menu);

    run_action_ = debug_menu->addAction(QObject::tr("&Run / Continue"));
    run_action_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(run_action_, &QAction::triggered, this, &DebuggerManager::on_run);

    pause_action_ = debug_menu->addAction(QObject::tr("&Pause"));
    pause_action_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(pause_action_, &QAction::triggered, this, &DebuggerManager::on_pause);

    debug_menu->addSeparator();

    step_into_action_ = debug_menu->addAction(QObject::tr("Step &Into"));
    step_into_action_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(step_into_action_, &QAction::triggered, this, &DebuggerManager::on_step_into);

    step_over_action_ = debug_menu->addAction(QObject::tr("Step &Over"));
    step_over_action_->setShortcut(QKeySequence(Qt::Key_F10));
    connect(step_over_action_, &QAction::triggered, this, &DebuggerManager::on_step_over);

    step_out_action_ = debug_menu->addAction(QObject::tr("Step Ou&t"));
    step_out_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    connect(step_out_action_, &QAction::triggered, this, &DebuggerManager::on_step_out);
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void DebuggerManager::create_debug_toolbar() {
    debug_toolbar_ = main_window_->addToolBar(QObject::tr("Debug"));
    debug_toolbar_->setMovable(false);

    QAction* run_btn = debug_toolbar_->addAction(
        main_window_->style()->standardIcon(QStyle::SP_MediaPlay), QObject::tr("Run"));
    connect(run_btn, &QAction::triggered, this, &DebuggerManager::on_run);

    QAction* pause_btn = debug_toolbar_->addAction(
        main_window_->style()->standardIcon(QStyle::SP_MediaPause), QObject::tr("Pause"));
    connect(pause_btn, &QAction::triggered, this, &DebuggerManager::on_pause);

    debug_toolbar_->addSeparator();

    QAction* step_into_btn = debug_toolbar_->addAction(QObject::tr("Into"));
    connect(step_into_btn, &QAction::triggered, this, &DebuggerManager::on_step_into);

    QAction* step_over_btn = debug_toolbar_->addAction(QObject::tr("Over"));
    connect(step_over_btn, &QAction::triggered, this, &DebuggerManager::on_step_over);

    QAction* step_out_btn = debug_toolbar_->addAction(QObject::tr("Out"));
    connect(step_out_btn, &QAction::triggered, this, &DebuggerManager::on_step_out);
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

void DebuggerManager::create_panels() {
    // CPU registers panel (right dock area)
    cpu_panel_ = new CpuPanel(emulator_);
    cpu_dock_ = new QDockWidget(QObject::tr("CPU Registers"), main_window_);
    cpu_dock_->setWidget(cpu_panel_);
    cpu_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    main_window_->addDockWidget(Qt::RightDockWidgetArea, cpu_dock_);

    // Disassembly panel (right dock area, below CPU)
    disasm_panel_ = new DisasmPanel(emulator_);
    disasm_dock_ = new QDockWidget(QObject::tr("Disassembly"), main_window_);
    disasm_dock_->setWidget(disasm_panel_);
    disasm_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    main_window_->addDockWidget(Qt::RightDockWidgetArea, disasm_dock_);

    // Memory panel (bottom dock area)
    memory_panel_ = new MemoryPanel(emulator_);
    memory_dock_ = new QDockWidget(QObject::tr("Memory"), main_window_);
    memory_dock_->setWidget(memory_panel_);
    memory_dock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    main_window_->addDockWidget(Qt::BottomDockWidgetArea, memory_dock_);

    // Connect disasm panel signals
    connect(disasm_panel_, &DisasmPanel::run_to_requested, this, [this](uint16_t addr) {
        emulator_->debug_state().run_to(addr);
        was_paused_ = false;
        emit resumed();
        update_actions();
    });
}

// ---------------------------------------------------------------------------
// Debug control slots
// ---------------------------------------------------------------------------

void DebuggerManager::on_run() {
    emulator_->debug_state().resume();
    was_paused_ = false;
    emit resumed();
    update_actions();
}

void DebuggerManager::on_pause() {
    emulator_->debug_state().pause();
    was_paused_ = true;
    emit paused();
    refresh_panels();
    update_actions();
}

void DebuggerManager::on_step_into() {
    // Ensure paused first.
    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    emulator_->execute_single_instruction();

    // Re-pause after the single step.
    emulator_->debug_state().pause();
    was_paused_ = true;
    emit paused();
    refresh_panels();
    update_actions();
}

void DebuggerManager::on_step_over() {
    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    auto regs = emulator_->cpu().get_registers();
    uint16_t pc = regs.PC;

    // Read memory via the MMU for disassembly.
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
        // Not a call — just step into.
        on_step_into();
    }
}

void DebuggerManager::on_step_out() {
    if (!emulator_->debug_state().paused()) {
        emulator_->debug_state().pause();
    }

    auto regs = emulator_->cpu().get_registers();
    emulator_->debug_state().step_out(regs.SP);
    was_paused_ = false;
    emit resumed();
    update_actions();
}

// ---------------------------------------------------------------------------
// Panel refresh
// ---------------------------------------------------------------------------

void DebuggerManager::refresh_panels() {
    if (emulator_->debug_state().paused()) {
        // Always refresh when paused.
        if (cpu_panel_) cpu_panel_->refresh();
        if (disasm_panel_) disasm_panel_->refresh();
        if (memory_panel_) memory_panel_->refresh();
    } else {
        // Throttle refresh during running to ~4Hz.
        ++refresh_counter_;
        if (refresh_counter_ >= REFRESH_INTERVAL) {
            refresh_counter_ = 0;
            if (cpu_panel_) cpu_panel_->refresh();
            if (disasm_panel_) disasm_panel_->refresh();
<<<<<<< HEAD
=======
            if (memory_panel_) memory_panel_->refresh();
>>>>>>> worktree-agent-a90eb90e
        }
    }
}

void DebuggerManager::check_breakpoint_hit() {
    bool is_paused = emulator_->debug_state().paused();

    // Detect transition from running to paused (breakpoint hit during run_frame).
    if (is_paused && !was_paused_) {
        was_paused_ = true;
        emit paused();
        refresh_panels();
        update_actions();
    }
}

// ---------------------------------------------------------------------------
// Action state management
// ---------------------------------------------------------------------------

void DebuggerManager::update_actions() {
    bool is_paused = emulator_->debug_state().paused();

    if (run_action_)       run_action_->setEnabled(is_paused);
    if (pause_action_)     pause_action_->setEnabled(!is_paused);
    if (step_into_action_) step_into_action_->setEnabled(is_paused);
    if (step_over_action_) step_over_action_->setEnabled(is_paused);
    if (step_out_action_)  step_out_action_->setEnabled(is_paused);
}
