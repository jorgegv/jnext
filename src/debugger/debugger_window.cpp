#include "debugger/debugger_window.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/video_panel.h"
#include "debugger/sprite_panel.h"
#include "debugger/copper_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/audio_panel.h"
#include "core/emulator.h"

#include "debugger/debugger_manager.h"

#include <QDockWidget>
#include <QCloseEvent>
#include <QToolBar>
#include <QPushButton>
#include <QSettings>
#include <QDataStream>

DebuggerWindow::DebuggerWindow(Emulator* emulator, QWidget* parent)
    : QMainWindow(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("JNEXT Debugger"));
    resize(506, 780);

    create_panels();

    // Restore saved size (not position — position is controlled by the main window).
    QSettings settings("JNEXT", "Debugger");
    QByteArray saved_size = settings.value("debugger/size").toByteArray();
    if (!saved_size.isEmpty()) {
        QDataStream ds(saved_size);
        int w, h;
        ds >> w >> h;
        if (w > 100 && h > 100)
            resize(w, h);
    }
}

void DebuggerWindow::closeEvent(QCloseEvent* event) {
    save_geometry();
    emit window_closed();
    event->accept();
}

void DebuggerWindow::set_debugger_manager(DebuggerManager* mgr) {
    debugger_mgr_ = mgr;

    auto* toolbar = new QToolBar(tr("Debug Controls"), this);
    toolbar->setMovable(false);

    auto* continue_btn = new QPushButton(tr("Continue"), this);
    connect(continue_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run);
    toolbar->addWidget(continue_btn);

    auto* break_btn = new QPushButton(tr("Break"), this);
    connect(break_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_pause);
    toolbar->addWidget(break_btn);

    toolbar->addSeparator();

    auto* step_into_btn = new QPushButton(tr("Step Into"), this);
    connect(step_into_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_into);
    toolbar->addWidget(step_into_btn);

    auto* step_over_btn = new QPushButton(tr("Step Over"), this);
    connect(step_over_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_over);
    toolbar->addWidget(step_over_btn);

    auto* step_out_btn = new QPushButton(tr("Step Out"), this);
    connect(step_out_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_out);
    toolbar->addWidget(step_out_btn);

    addToolBar(toolbar);
}

void DebuggerWindow::save_position() {
    save_geometry();
}

void DebuggerWindow::save_geometry() {
    QSettings settings("JNEXT", "Debugger");
    // Only save size; position is controlled by the main window (sticky).
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds << width() << height();
    settings.setValue("debugger/size", data);
}

void DebuggerWindow::restore_geometry() {
    // No-op: size is restored in constructor, position is set by MainWindow.
}

void DebuggerWindow::position_next_to(QWidget* main_win) {
    if (!main_win) return;
    QPoint top_right = main_win->mapToGlobal(QPoint(main_win->width(), 0));
    move(top_right);
}

void DebuggerWindow::create_panels() {
    // Static dock features: no close, no float — bold title label.
    auto static_dock = [](QDockWidget* dock) {
        dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        QFont f = dock->font();
        f.setBold(true);
        dock->setFont(f);
    };

    // Light gray separator lines + darker title bar background.
    setStyleSheet(
        "QMainWindow::separator { background: #C0C0C0; width: 1px; height: 1px;"
        "  margin: 3px; }"
        "QDockWidget::title { background: #D0D0D0; padding: 4px; }"
    );

    // CPU registers panel (right dock area)
    cpu_panel_ = new CpuPanel(emulator_);
    cpu_dock_ = new QDockWidget(tr("CPU Registers"), this);
    cpu_dock_->setWidget(cpu_panel_);
    static_dock(cpu_dock_);
    addDockWidget(Qt::RightDockWidgetArea, cpu_dock_);

    // Disassembly panel (right dock area, below CPU)
    disasm_panel_ = new DisasmPanel(emulator_);
    disasm_dock_ = new QDockWidget(tr("Disassembly"), this);
    disasm_dock_->setWidget(disasm_panel_);
    static_dock(disasm_dock_);
    addDockWidget(Qt::RightDockWidgetArea, disasm_dock_);

    // Memory panel (bottom dock area) — tall enough for 16+ rows
    memory_panel_ = new MemoryPanel(emulator_);
    memory_panel_->setMinimumHeight(320);
    memory_dock_ = new QDockWidget(tr("Memory"), this);
    memory_dock_->setWidget(memory_panel_);
    static_dock(memory_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, memory_dock_);

    // Video panel (left dock area)
    video_panel_ = new VideoPanel(emulator_);
    video_dock_ = new QDockWidget(tr("Video"), this);
    video_dock_->setWidget(video_panel_);
    static_dock(video_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, video_dock_);

    // Sprite panel (left dock area, tabified with video)
    sprite_panel_ = new SpritePanel(emulator_);
    sprite_dock_ = new QDockWidget(tr("Sprites"), this);
    sprite_dock_->setWidget(sprite_panel_);
    static_dock(sprite_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, sprite_dock_);
    tabifyDockWidget(video_dock_, sprite_dock_);

    // Copper panel (left dock area, tabified)
    copper_panel_ = new CopperPanel(emulator_);
    copper_dock_ = new QDockWidget(tr("Copper"), this);
    copper_dock_->setWidget(copper_panel_);
    static_dock(copper_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, copper_dock_);
    tabifyDockWidget(sprite_dock_, copper_dock_);

    // NextREG panel (left dock area, tabified)
    nextreg_panel_ = new NextRegPanel(emulator_);
    nextreg_dock_ = new QDockWidget(tr("NextREG"), this);
    nextreg_dock_->setWidget(nextreg_panel_);
    static_dock(nextreg_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, nextreg_dock_);
    tabifyDockWidget(copper_dock_, nextreg_dock_);

    // Audio panel (left dock area, tabified)
    audio_panel_ = new AudioPanel(emulator_);
    audio_dock_ = new QDockWidget(tr("Audio"), this);
    audio_dock_->setWidget(audio_panel_);
    static_dock(audio_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, audio_dock_);
    tabifyDockWidget(nextreg_dock_, audio_dock_);

    // Raise the video panel tab by default
    video_dock_->raise();
}

void DebuggerWindow::activate_follow_pc() {
    if (disasm_panel_) disasm_panel_->activate_follow_pc();
}

void DebuggerWindow::refresh_panels() {
    if (cpu_panel_) cpu_panel_->refresh();
    if (disasm_panel_) disasm_panel_->refresh();
    if (memory_panel_) memory_panel_->refresh();
    if (video_panel_) video_panel_->refresh();
    if (sprite_panel_) sprite_panel_->refresh();
    if (copper_panel_) copper_panel_->refresh();
    if (nextreg_panel_) nextreg_panel_->refresh();
    if (audio_panel_) audio_panel_->refresh();
}
