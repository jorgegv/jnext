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
#include "debugger/mmu_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/callstack_panel.h"
#include "core/emulator.h"
#include "core/rzx_player.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"
#include "debug/rewind_buffer.h"

#include "debugger/debugger_manager.h"

#include <QCloseEvent>
#include <QToolBar>
#include <QMenuBar>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
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
#include <QSlider>
#include <QStatusBar>
#include <QSpinBox>

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
    // Layout: F2:Trace(ball) | Export Trace | --> | Continue | Single Step | Step Over | Step Out | Break |
    auto* toolbar = new QToolBar(tr("Debug Controls"), this);
    toolbar->setMovable(false);

    // F2: Trace On/Off toggle with integrated ball icon
    trace_toggle_btn_ = new QPushButton(tr("F2: Trace"), this);
    update_trace_indicator();
    connect(trace_toggle_btn_, &QPushButton::clicked, this, [this]() {
        if (!emulator_) return;
        bool new_state = !emulator_->trace_log().enabled();
        emulator_->trace_log().set_enabled(new_state);
        if (trace_enable_action_)
            trace_enable_action_->setChecked(new_state);
        update_trace_indicator();
    });
    toolbar->addWidget(trace_toggle_btn_);

    // F3: Export Trace
    auto* export_trace_btn = new QPushButton(tr("F3: Export Trace"), this);
    connect(export_trace_btn, &QPushButton::clicked, this, [this]() {
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
    toolbar->addWidget(export_trace_btn);

    // Spacer to push execution controls to the right
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // Execution controls on the right
    auto* continue_btn = new QPushButton(tr("F5: Continue"), this);
    connect(continue_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run);
    toolbar->addWidget(continue_btn);

    auto* frame_back_btn = new QPushButton(tr("|\u25C4 Frame Back"), this);
    frame_back_btn->setToolTip(tr("Rewind to previous frame (Shift+F6)"));
    connect(frame_back_btn, &QPushButton::clicked, this, [this]() {
        if (debugger_mgr_ && emulator_ && emulator_->rewind_buffer()
                && !emulator_->rewind_buffer()->empty()) {
            uint32_t prev = emulator_->frame_num() > 0
                ? emulator_->frame_num() - 1 : 0;
            debugger_mgr_->on_rewind_to_frame(prev);
        }
    });
    toolbar->addWidget(frame_back_btn);

    auto* step_back_btn = new QPushButton(tr("\u25C4 Step Back"), this);
    step_back_btn->setToolTip(tr("Step back one instruction (Shift+F7)"));
    connect(step_back_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_back);
    toolbar->addWidget(step_back_btn);

    auto* step_into_btn = new QPushButton(tr("F6: Single Step"), this);
    connect(step_into_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_into);
    toolbar->addWidget(step_into_btn);

    auto* step_over_btn = new QPushButton(tr("F7: Step Over"), this);
    connect(step_over_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_over);
    toolbar->addWidget(step_over_btn);

    auto* step_out_btn = new QPushButton(tr("F8: Step Out"), this);
    connect(step_out_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_out);
    toolbar->addWidget(step_out_btn);

    auto* run_to_eosl_btn = new QPushButton(tr("Run to EOSL"), this);
    run_to_eosl_btn->setToolTip(tr("Run to End of Scan Line"));
    connect(run_to_eosl_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run_to_eosl);
    toolbar->addWidget(run_to_eosl_btn);

    auto* run_to_eof_btn = new QPushButton(tr("Run to EOF"), this);
    run_to_eof_btn->setToolTip(tr("Run to End of Frame"));
    connect(run_to_eof_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run_to_eof);
    toolbar->addWidget(run_to_eof_btn);

    auto* break_btn = new QPushButton(tr("F9: Break"), this);
    connect(break_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_pause);
    toolbar->addWidget(break_btn);

    addToolBar(Qt::BottomToolBarArea, toolbar);

    // --- Rewind slider toolbar (second bottom toolbar, initially hidden) ---
    rewind_toolbar_ = new QToolBar(tr("Rewind"), this);
    rewind_toolbar_->setMovable(false);

    auto* rewind_label = new QLabel(tr("  \u23EE Rewind — Frame: "), this);
    rewind_toolbar_->addWidget(rewind_label);

    rewind_slider_ = new QSlider(Qt::Horizontal, this);
    rewind_slider_->setMinimumWidth(300);
    rewind_slider_->setToolTip(tr("Drag to select a frame to rewind to"));
    connect(rewind_slider_, &QSlider::sliderPressed, this, [this]() {
        rewind_slider_dragging_ = true;
    });
    connect(rewind_slider_, &QSlider::sliderMoved, this, [this](int value) {
        if (rewind_frame_label_)
            rewind_frame_label_->setText(tr("Frame %1").arg(value));
    });
    connect(rewind_slider_, &QSlider::sliderReleased, this, [this]() {
        rewind_slider_dragging_ = false;
        // Trigger rewind immediately on release (no need to press Jump Here separately).
        if (debugger_mgr_)
            debugger_mgr_->on_rewind_to_frame(
                static_cast<uint32_t>(rewind_slider_->value()));
    });
    rewind_toolbar_->addWidget(rewind_slider_);

    rewind_frame_label_ = new QLabel(tr("---"), this);
    rewind_frame_label_->setMinimumWidth(120);
    rewind_toolbar_->addWidget(rewind_frame_label_);

    rewind_jump_btn_ = new QPushButton(tr("Jump Here"), this);
    connect(rewind_jump_btn_, &QPushButton::clicked, this, [this]() {
        if (!debugger_mgr_ || !rewind_slider_) return;
        uint32_t frame = static_cast<uint32_t>(rewind_slider_->value());
        debugger_mgr_->on_rewind_to_frame(frame);
    });
    rewind_toolbar_->addWidget(rewind_jump_btn_);

    rewind_toolbar_->setVisible(false);
    addToolBar(Qt::BottomToolBarArea, rewind_toolbar_);
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

    QAction* frame_back_action = debug_menu->addAction(tr("|< &Frame Back"));
    frame_back_action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
    connect(frame_back_action, &QAction::triggered, this, [this]() {
        if (debugger_mgr_ && emulator_ && emulator_->rewind_buffer()
                && !emulator_->rewind_buffer()->empty()) {
            uint32_t prev = emulator_->frame_num() > 0
                ? emulator_->frame_num() - 1 : 0;
            debugger_mgr_->on_rewind_to_frame(prev);
        }
    });

    step_back_action_ = debug_menu->addAction(tr("Step &Back"));
    step_back_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F7));
    connect(step_back_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_back);

    debug_menu->addSeparator();

    QAction* run_to_eof_action = debug_menu->addAction(tr("Run to End of &Frame"));
    connect(run_to_eof_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_run_to_eof);

    QAction* run_to_eosl_action = debug_menu->addAction(tr("Run to End of Scan &Line"));
    connect(run_to_eosl_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_run_to_eosl);

    debug_menu->addSeparator();

    // Trace submenu
    QMenu* trace_menu = debug_menu->addMenu(tr("&Trace"));

    trace_enable_action_ = trace_menu->addAction(tr("&Enable Trace"));
    trace_enable_action_->setCheckable(true);
    trace_enable_action_->setShortcut(QKeySequence(Qt::Key_F2));
    connect(trace_enable_action_, &QAction::triggered, this, [this](bool checked) {
        if (emulator_) {
            emulator_->trace_log().set_enabled(checked);
            update_trace_indicator();
        }
    });

    QAction* clear_trace = trace_menu->addAction(tr("&Clear Trace"));
    connect(clear_trace, &QAction::triggered, this, [this]() {
        if (emulator_)
            emulator_->trace_log().clear();
    });

    QAction* export_trace = trace_menu->addAction(tr("E&xport Trace..."));
    export_trace->setShortcut(QKeySequence(Qt::Key_F3));
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

    // Rewind submenu
    debug_menu->addSeparator();
    QMenu* rewind_menu = debug_menu->addMenu(tr("&Rewind"));

    rewind_enable_action_ = rewind_menu->addAction(tr("&Enable Rewind"));
    rewind_enable_action_->setCheckable(true);
    rewind_enable_action_->setChecked(emulator_ && emulator_->rewind_buffer() != nullptr);
    connect(rewind_enable_action_, &QAction::triggered, this, [this](bool checked) {
        if (!emulator_) return;
        if (checked) {
            if (!emulator_->rewind_buffer()) {
                // No buffer allocated — explain how to enable
                QMessageBox::information(this, tr("Enable Rewind"),
                    tr("Rewind requires pre-allocated memory.\n"
                       "Restart the emulator with:\n\n"
                       "  --rewind-buffer-size N\n\n"
                       "where N is the number of frames to store (default 500)."));
                rewind_enable_action_->setChecked(false);
                return;
            }
            emulator_->set_rewind_enabled(true);
        } else {
            emulator_->set_rewind_enabled(false);
        }
    });

    QAction* rewind_size_action = rewind_menu->addAction(tr("Rewind &Buffer Size..."));
    connect(rewind_size_action, &QAction::triggered, this, &DebuggerWindow::show_rewind_buffer_size_dialog);

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

    // Step Back is only available when paused, rewind buffer has snapshots,
    // and RZX playback is not active.
    bool can_rewind = is_paused
        && emulator_
        && emulator_->rewind_buffer()
        && !emulator_->rewind_buffer()->empty()
        && !emulator_->rzx_player().is_playing();
    if (step_back_action_) step_back_action_->setEnabled(can_rewind);
    if (rewind_jump_btn_)  rewind_jump_btn_->setEnabled(can_rewind);
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

void DebuggerWindow::update_trace_indicator() {
    if (!trace_toggle_btn_) return;
    bool active = emulator_ && emulator_->trace_log().enabled();
    QColor color = active ? QColor(0x00, 0xC0, 0x00) : QColor(0xC0, 0x00, 0x00);

    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(QPen(QColor(0x40, 0x40, 0x40), 1));
    p.drawEllipse(1, 1, 12, 12);
    p.end();

    trace_toggle_btn_->setIcon(QIcon(pix));
    trace_toggle_btn_->setIconSize(QSize(14, 14));

    if (trace_enable_action_)
        trace_enable_action_->setChecked(active);
}

void DebuggerWindow::update_rewind_ui() {
    if (!emulator_) return;

    auto* rb = emulator_->rewind_buffer();
    bool has_rewind = rb && !rb->empty() && rb->depth() > 1;

    // Show/hide the rewind toolbar
    if (rewind_toolbar_)
        rewind_toolbar_->setVisible(has_rewind);

    bool is_paused = emulator_->debug_state().paused();
    if (has_rewind && !rewind_slider_dragging_) {
        // Always update the range; only move the thumb when running (not paused).
        // When paused, the user controls the slider position.
        if (rewind_slider_) {
            rewind_slider_->blockSignals(true);
            rewind_slider_->setRange(
                static_cast<int>(rb->oldest_frame_num()),
                static_cast<int>(rb->newest_frame_num()));
            if (!is_paused)
                rewind_slider_->setValue(static_cast<int>(emulator_->frame_num()));
            rewind_slider_->blockSignals(false);
        }
        if (rewind_frame_label_) {
            rewind_frame_label_->setText(
                tr("Frame %1 / %2")
                    .arg(emulator_->frame_num())
                    .arg(rb->newest_frame_num()));
        }
    }

    // Status bar indicator
    if (rb && !rb->empty()) {
        bool is_rewound = emulator_->frame_num() < rb->newest_frame_num();
        if (is_rewound) {
            statusBar()->showMessage(
                tr("\u23EE Rewound: frame %1 of %2  (F5 / Continue to resume)")
                    .arg(emulator_->frame_num())
                    .arg(rb->newest_frame_num()));
        } else {
            size_t mb = (rb->depth() * rb->snapshot_bytes() + 524288) / 1048576;
            statusBar()->showMessage(
                tr("\u23EE Rewind: %1 frames / %2 MB")
                    .arg(rb->depth())
                    .arg(mb));
        }
    } else {
        statusBar()->clearMessage();
    }

    // Sync menu checkmark
    if (rewind_enable_action_)
        rewind_enable_action_->setChecked(rb != nullptr && emulator_->rewind_enabled());
}

void DebuggerWindow::show_rewind_buffer_size_dialog() {
    if (!emulator_) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Rewind Buffer Size"));
    dlg.setMinimumWidth(360);

    auto* form = new QFormLayout(&dlg);

    auto* spin = new QSpinBox(&dlg);
    spin->setRange(0, 2000);
    spin->setSuffix(tr(" frames"));
    int current_frames = emulator_->rewind_buffer()
        ? static_cast<int>(emulator_->rewind_buffer()->depth()) : 0;
    // Show the max capacity (depth() is current fill, not capacity)
    // Use snapshot_bytes to estimate from allocated memory
    // Since we don't expose max_frames, use config value as best estimate
    spin->setValue(current_frames > 0 ? current_frames : 500);

    size_t snap_bytes = emulator_->rewind_buffer()
        ? emulator_->rewind_buffer()->snapshot_bytes() : 0;
    QString info;
    if (snap_bytes > 0) {
        size_t est_mb = (static_cast<uint64_t>(spin->value()) * snap_bytes + 524288) / 1048576;
        info = tr("~%1 MB at %2 frames").arg(est_mb).arg(spin->value());
    } else {
        info = tr("Rewind is currently disabled. Enter frame count to enable.");
    }
    auto* info_label = new QLabel(info, &dlg);
    info_label->setWordWrap(true);

    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [&](int v) {
        if (snap_bytes > 0) {
            size_t est_mb = (static_cast<uint64_t>(v) * snap_bytes + 524288) / 1048576;
            info_label->setText(tr("~%1 MB at %2 frames").arg(est_mb).arg(v));
        }
    });

    form->addRow(tr("Buffer size:"), spin);
    form->addRow(info_label);

    auto* note = new QLabel(
        tr("Note: resizing clears all existing snapshots."), &dlg);
    note->setWordWrap(true);
    form->addRow(note);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    int new_frames = spin->value();
    emulator_->resize_rewind_buffer(new_frames);
    update_rewind_ui();
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
    tab_widget_->setTabPosition(QTabWidget::North);
    tab_widget_->addTab(video_panel_, tr("Video"));
    tab_widget_->addTab(sprite_panel_, tr("Sprites"));
    tab_widget_->addTab(copper_panel_, tr("Copper"));
    tab_widget_->addTab(nextreg_panel_, tr("NextREG"));
    tab_widget_->addTab(audio_panel_, tr("Audio"));

    tab_widget_->setMinimumWidth(380);

    stack_panel_ = new StackPanel(emulator_);
    callstack_panel_ = new CallStackPanel(emulator_);
    breakpoint_panel_ = new BreakpointPanel(emulator_);

    mmu_panel_ = new MmuPanel(emulator_);

    auto* cpu_box = make_group(tr("CPU Registers"), cpu_panel_);
    auto* mmu_box = make_group(tr("MMU"), mmu_panel_);
    auto* disasm_box = make_group(tr("Disassembly"), disasm_panel_);

    // Right column: CPU Registers (top half) + MMU (bottom half)
    auto* right_col = new QSplitter(Qt::Vertical);
    right_col->addWidget(cpu_box);
    right_col->addWidget(mmu_box);
    right_col->setStretchFactor(0, 1);
    right_col->setStretchFactor(1, 1);
    right_col->setHandleWidth(1);
    right_col->setChildrenCollapsible(false);

    auto* right_widget = new QWidget();
    auto* right_layout = new QHBoxLayout(right_widget);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(2);
    right_layout->addWidget(disasm_box, 1);  // disasm gets stretch
    right_layout->addWidget(right_col, 0);   // CPU+MMU on the right, narrow

    // --- Top: tabs (left) | CPU+disasm (right) ---
    top_splitter_ = new QSplitter(Qt::Horizontal);
    top_splitter_->addWidget(tab_widget_);
    top_splitter_->addWidget(right_widget);
    top_splitter_->setStretchFactor(0, 3);  // left tabs get more space
    top_splitter_->setStretchFactor(1, 2);  // right side (disasm+CPU)
    top_splitter_->setSizes({450, 300});
    top_splitter_->setHandleWidth(1);
    top_splitter_->setChildrenCollapsible(false);

    // --- Bottom: memory tabs (left) | watches+breakpoints tabs (right) ---
    auto* memory_tab_widget = new QTabWidget();
    memory_tab_widget->setTabPosition(QTabWidget::North);
    memory_tab_widget->addTab(stack_panel_, tr("Stack"));
    memory_tab_widget->addTab(callstack_panel_, tr("Call Stack"));
    memory_tab_widget->addTab(memory_panel_, tr("Memory"));

    auto* bottom_tab_widget = new QTabWidget();
    bottom_tab_widget->setTabPosition(QTabWidget::North);
    bottom_tab_widget->addTab(watch_panel_, tr("Watches"));
    bottom_tab_widget->addTab(breakpoint_panel_, tr("Breakpoints"));

    auto* bottom_splitter = new QSplitter(Qt::Horizontal);
    bottom_splitter->addWidget(memory_tab_widget);
    bottom_splitter->addWidget(bottom_tab_widget);
    bottom_splitter->setStretchFactor(0, 1);
    bottom_splitter->setStretchFactor(1, 1);
    bottom_splitter->setSizes({600, 400});
    bottom_splitter->setHandleWidth(1);
    bottom_splitter->setChildrenCollapsible(false);

    // --- Main: top area | bottom (memory + watches) ---
    main_splitter_ = new QSplitter(Qt::Vertical);
    main_splitter_->addWidget(top_splitter_);
    main_splitter_->addWidget(bottom_splitter);
    main_splitter_->setStretchFactor(0, 1);  // top gets stretch
    main_splitter_->setStretchFactor(1, 0);  // bottom stays at preferred size
    main_splitter_->setHandleWidth(1);
    main_splitter_->setChildrenCollapsible(false);

    setCentralWidget(main_splitter_);
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
    if (mmu_panel_) mmu_panel_->refresh();
    if (stack_panel_) stack_panel_->refresh();
    if (callstack_panel_) callstack_panel_->refresh();
    if (breakpoint_panel_) breakpoint_panel_->refresh();
    update_rewind_ui();
}
