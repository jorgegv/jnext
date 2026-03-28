#include "debugger/debugger_window.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/video_panel.h"
#include "debugger/sprite_panel.h"
#include "debugger/copper_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/audio_panel.h"
#include "debugger/watch_panel.h"
#include "debugger/breakpoint_panel.h"
#include "core/emulator.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"

#include "debugger/debugger_manager.h"

#include <QCloseEvent>
#include <QToolBar>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QDataStream>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>

DebuggerWindow::DebuggerWindow(Emulator* emulator, QWidget* parent)
    : QMainWindow(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("JNEXT Debugger"));
    create_panels();

    setMinimumWidth(1170);
    resize(1170, 900);

    // Restore saved size (not position — position is controlled by the main window).
    QSettings settings("JNEXT", "Debugger");
    QByteArray saved_size = settings.value("debugger/size").toByteArray();
    if (!saved_size.isEmpty()) {
        QDataStream ds(saved_size);
        int w, h;
        ds >> w >> h;
        if (w >= 1170 && h > 100)
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

    // Create the menu bar with debug actions.
    create_menus();

    // Create the debug controls toolbar at the bottom.
    auto* toolbar = new QToolBar(tr("Debug Controls"), this);
    toolbar->setMovable(false);

    // Continue and Break on the left
    auto* continue_btn = new QPushButton(tr("F5: Continue"), this);
    connect(continue_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run);
    toolbar->addWidget(continue_btn);

    auto* break_btn = new QPushButton(tr("F9: Break"), this);
    connect(break_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_pause);
    toolbar->addWidget(break_btn);

    // Spacer to push step buttons to the right
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    auto* step_into_btn = new QPushButton(tr("F6: Single Step"), this);
    connect(step_into_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_into);
    toolbar->addWidget(step_into_btn);

    auto* step_over_btn = new QPushButton(tr("F7: Step Over"), this);
    connect(step_over_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_over);
    toolbar->addWidget(step_over_btn);

    auto* step_out_btn = new QPushButton(tr("F8: Step Out"), this);
    connect(step_out_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_out);
    toolbar->addWidget(step_out_btn);

    addToolBar(Qt::BottomToolBarArea, toolbar);
}

void DebuggerWindow::create_menus() {
    QMenuBar* bar = menuBar();

    // --- Debug menu ---
    QMenu* debug_menu = bar->addMenu(tr("&Debug"));

    run_action_ = debug_menu->addAction(tr("Run / &Continue"));
    run_action_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(run_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_run);

    pause_action_ = debug_menu->addAction(tr("Pause / &Break"));
    pause_action_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(pause_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_pause);

    debug_menu->addSeparator();

    step_into_action_ = debug_menu->addAction(tr("&Single Step"));
    step_into_action_->setShortcut(QKeySequence(Qt::Key_F6));
    connect(step_into_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_into);

    step_over_action_ = debug_menu->addAction(tr("Step &Over"));
    step_over_action_->setShortcut(QKeySequence(Qt::Key_F7));
    connect(step_over_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_over);

    step_out_action_ = debug_menu->addAction(tr("Step Ou&t"));
    step_out_action_->setShortcut(QKeySequence(Qt::Key_F8));
    connect(step_out_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_out);

    debug_menu->addSeparator();

    // Trace submenu
    QMenu* trace_menu = debug_menu->addMenu(tr("&Trace"));

    QAction* trace_enable = trace_menu->addAction(tr("&Enable Trace"));
    trace_enable->setCheckable(true);
    connect(trace_enable, &QAction::triggered, this, [this](bool checked) {
        if (emulator_)
            emulator_->trace_log().set_enabled(checked);
    });

    QAction* clear_trace = trace_menu->addAction(tr("&Clear Trace"));
    connect(clear_trace, &QAction::triggered, this, [this]() {
        if (emulator_)
            emulator_->trace_log().clear();
    });

    QAction* export_trace = trace_menu->addAction(tr("E&xport Trace..."));
    connect(export_trace, &QAction::triggered, this, [this]() {
        if (!emulator_) return;
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export Trace Log"), QString(),
            tr("Text Files (*.txt);;All Files (*)"));
        if (!path.isEmpty()) {
            bool ok = emulator_->trace_log().export_to_file(path.toStdString());
            if (!ok) {
                QMessageBox::warning(this, tr("Export Failed"),
                    tr("Could not write trace log to:\n%1").arg(path));
            }
        }
    });

    // --- Map menu ---
    QMenu* map_menu = bar->addMenu(tr("&Map"));

    QMenu* load_map_menu = map_menu->addMenu(tr("&Load MAP File"));
    QAction* z88dk_action = load_map_menu->addAction(tr("&Z88DK Format..."));
    connect(z88dk_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_load_map_z88dk);
    QAction* simple_action = load_map_menu->addAction(tr("&Simple Format (48K ROM)..."));
    connect(simple_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_load_map_simple);

    // --- Breakpoints menu ---
    QMenu* bp_menu = bar->addMenu(tr("&Breakpoints"));

    QAction* add_read_bp = bp_menu->addAction(tr("Add &Read Breakpoint..."));
    connect(add_read_bp, &QAction::triggered, this, [this]() {
        show_add_data_bp_dialog(WatchType::READ);
    });

    QAction* add_write_bp = bp_menu->addAction(tr("Add &Write Breakpoint..."));
    connect(add_write_bp, &QAction::triggered, this, [this]() {
        show_add_data_bp_dialog(WatchType::WRITE);
    });

    QAction* add_rw_bp = bp_menu->addAction(tr("Add Read/&Write Breakpoint..."));
    connect(add_rw_bp, &QAction::triggered, this, [this]() {
        show_add_data_bp_dialog(WatchType::READ_WRITE);
    });

    bp_menu->addSeparator();

    QAction* clear_all_bp = bp_menu->addAction(tr("&Clear All Breakpoints"));
    connect(clear_all_bp, &QAction::triggered, this, [this]() {
        emulator_->debug_state().breakpoints().clear_all_pc();
        emulator_->debug_state().breakpoints().clear_all_watchpoints();
        if (disasm_panel_) disasm_panel_->refresh();
    });

    // --- Watches menu ---
    QMenu* watches_menu = bar->addMenu(tr("&Watches"));

    QAction* add_watch = watches_menu->addAction(tr("&Add Watch..."));
    connect(add_watch, &QAction::triggered, this, [this]() {
        if (watch_panel_)
            watch_panel_->on_add_watch();
    });
}

void DebuggerWindow::update_actions(bool is_paused) {
    if (run_action_)       run_action_->setEnabled(is_paused);
    if (pause_action_)     pause_action_->setEnabled(!is_paused);
    if (step_into_action_) step_into_action_->setEnabled(is_paused);
    if (step_over_action_) step_over_action_->setEnabled(is_paused);
    if (step_out_action_)  step_out_action_->setEnabled(is_paused);
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
    watch_panel_ = new WatchPanel(emulator_);

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

    // --- Tab widget for left-side panels (tabs at bottom) ---
    tab_widget_ = new QTabWidget();
    tab_widget_->setTabPosition(QTabWidget::South);
    tab_widget_->addTab(video_panel_, tr("Video"));
    tab_widget_->addTab(sprite_panel_, tr("Sprites"));
    tab_widget_->addTab(copper_panel_, tr("Copper"));
    tab_widget_->addTab(nextreg_panel_, tr("NextREG"));
    tab_widget_->addTab(audio_panel_, tr("Audio"));

    breakpoint_panel_ = new BreakpointPanel(emulator_);
    tab_widget_->addTab(breakpoint_panel_, tr("Breakpoints"));
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

    // --- Bottom: memory (left) | watches (right) ---
    auto* watch_box = make_group(tr("Watches"), watch_panel_);

    auto* bottom_splitter = new QSplitter(Qt::Horizontal);
    bottom_splitter->addWidget(memory_box);
    bottom_splitter->addWidget(watch_box);
    bottom_splitter->setStretchFactor(0, 1);
    bottom_splitter->setStretchFactor(1, 1);
    bottom_splitter->setSizes({600, 400});
    bottom_splitter->setStyleSheet("QSplitter::handle { background: #C0C0C0; }");

    // --- Main: top area | bottom (memory + watches) ---
    main_splitter_ = new QSplitter(Qt::Vertical);
    main_splitter_->addWidget(top_splitter_);
    main_splitter_->addWidget(bottom_splitter);
    main_splitter_->setStretchFactor(0, 1);  // top gets stretch
    main_splitter_->setStretchFactor(1, 0);  // bottom stays at preferred size

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

void DebuggerWindow::show_add_data_bp_dialog(WatchType type) {
    QDialog dlg(this);
    QString type_name;
    switch (type) {
        case WatchType::READ:       type_name = "Read"; break;
        case WatchType::WRITE:      type_name = "Write"; break;
        case WatchType::READ_WRITE: type_name = "Read/Write"; break;
        default:                    type_name = "Data"; break;
    }
    dlg.setWindowTitle(tr("Add %1 Breakpoint").arg(type_name));
    dlg.setMinimumWidth(400);

    auto* form = new QFormLayout(&dlg);
    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText("e.g. 4000 or $4000");
    form->addRow(tr("Address (hex):"), addr_edit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QString addr_text = addr_edit->text().trimmed();
    if (addr_text.startsWith('$')) addr_text = addr_text.mid(1);
    if (addr_text.startsWith("0x", Qt::CaseInsensitive)) addr_text = addr_text.mid(2);

    bool ok = false;
    uint16_t addr = static_cast<uint16_t>(addr_text.toUInt(&ok, 16));
    if (!ok) return;

    emulator_->debug_state().breakpoints().add_watchpoint(addr, type);
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
    if (watch_panel_) watch_panel_->refresh();
    if (breakpoint_panel_) breakpoint_panel_->refresh();
}
