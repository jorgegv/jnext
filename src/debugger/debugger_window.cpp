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

#include <QCloseEvent>
#include <QToolBar>
#include <QPushButton>
#include <QSettings>
#include <QDataStream>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QGroupBox>

DebuggerWindow::DebuggerWindow(Emulator* emulator, QWidget* parent)
    : QMainWindow(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("JNEXT Debugger"));
    create_panels();

    setMinimumWidth(780);
    resize(780, 900);

    // Restore saved size (not position — position is controlled by the main window).
    QSettings settings("JNEXT", "Debugger");
    QByteArray saved_size = settings.value("debugger/size").toByteArray();
    if (!saved_size.isEmpty()) {
        QDataStream ds(saved_size);
        int w, h;
        ds >> w >> h;
        if (w >= 780 && h > 100)
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
    // --- Create panels ---
    cpu_panel_ = new CpuPanel(emulator_);
    disasm_panel_ = new DisasmPanel(emulator_);
    memory_panel_ = new MemoryPanel(emulator_);
    memory_panel_->setMinimumHeight(320);
    video_panel_ = new VideoPanel(emulator_);
    sprite_panel_ = new SpritePanel(emulator_);
    copper_panel_ = new CopperPanel(emulator_);
    nextreg_panel_ = new NextRegPanel(emulator_);
    audio_panel_ = new AudioPanel(emulator_);

    // --- Helper: wrap a widget in a titled QGroupBox ---
    auto make_group = [](const QString& title, QWidget* content) -> QGroupBox* {
        auto* box = new QGroupBox(title);
        box->setStyleSheet(
            "QGroupBox { font-weight: bold; border: 1px solid #C0C0C0;"
            "  margin-top: 18px; padding-top: 8px; }"
            "QGroupBox::title { subcontrol-origin: margin;"
            "  subcontrol-position: top left; padding: 3px 8px;"
            "  background: #D8D8D8; }");
        auto* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->addWidget(content);
        return box;
    };

    // --- Tab widget for left-side panels (each wrapped in titled group) ---
    tab_widget_ = new QTabWidget();
    tab_widget_->addTab(make_group(tr("Video"), video_panel_), tr("Video"));
    tab_widget_->addTab(make_group(tr("Sprites"), sprite_panel_), tr("Sprites"));
    tab_widget_->addTab(make_group(tr("Copper"), copper_panel_), tr("Copper"));
    tab_widget_->addTab(make_group(tr("NextREG"), nextreg_panel_), tr("NextREG"));
    tab_widget_->addTab(make_group(tr("Audio"), audio_panel_), tr("Audio"));
    tab_widget_->setMinimumWidth(380);

    auto* cpu_box = make_group(tr("CPU Registers"), cpu_panel_);
    auto* disasm_box = make_group(tr("Disassembly"), disasm_panel_);
    auto* memory_box = make_group(tr("Memory"), memory_panel_);

    auto* right_widget = new QWidget();
    auto* right_layout = new QVBoxLayout(right_widget);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(2);
    right_layout->addWidget(cpu_box);
    right_layout->addWidget(disasm_box, 1);  // disasm gets stretch

    // --- Top: tabs (left) | CPU+disasm (right) ---
    top_splitter_ = new QSplitter(Qt::Horizontal);
    top_splitter_->addWidget(tab_widget_);
    top_splitter_->addWidget(right_widget);
    top_splitter_->setStretchFactor(0, 1);  // tabs stretch
    top_splitter_->setStretchFactor(1, 1);  // right side stretches equally
    top_splitter_->setSizes({350, 350});

    // --- Main: top area | memory (bottom) ---
    main_splitter_ = new QSplitter(Qt::Vertical);
    main_splitter_->addWidget(top_splitter_);
    main_splitter_->addWidget(memory_box);
    main_splitter_->setStretchFactor(0, 1);  // top gets stretch
    main_splitter_->setStretchFactor(1, 0);  // memory stays at preferred size

    setCentralWidget(main_splitter_);

    // Style: light separator handles
    main_splitter_->setStyleSheet(
        "QSplitter::handle { background: #C0C0C0; }"
    );
    top_splitter_->setStyleSheet(
        "QSplitter::handle { background: #C0C0C0; }"
    );
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
