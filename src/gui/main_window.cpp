#include "gui/main_window.h"
#include "gui/emulator_widget.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#endif

#include <QKeyEvent>
#include <QCloseEvent>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QStyle>

// ---------------------------------------------------------------------------
// Qt::Key -> SDL_Scancode mapping
// ---------------------------------------------------------------------------

namespace {

SDL_Scancode qt_key_to_sdl(int key) {
    switch (key) {
        // Letters
        case Qt::Key_A: return SDL_SCANCODE_A;
        case Qt::Key_B: return SDL_SCANCODE_B;
        case Qt::Key_C: return SDL_SCANCODE_C;
        case Qt::Key_D: return SDL_SCANCODE_D;
        case Qt::Key_E: return SDL_SCANCODE_E;
        case Qt::Key_F: return SDL_SCANCODE_F;
        case Qt::Key_G: return SDL_SCANCODE_G;
        case Qt::Key_H: return SDL_SCANCODE_H;
        case Qt::Key_I: return SDL_SCANCODE_I;
        case Qt::Key_J: return SDL_SCANCODE_J;
        case Qt::Key_K: return SDL_SCANCODE_K;
        case Qt::Key_L: return SDL_SCANCODE_L;
        case Qt::Key_M: return SDL_SCANCODE_M;
        case Qt::Key_N: return SDL_SCANCODE_N;
        case Qt::Key_O: return SDL_SCANCODE_O;
        case Qt::Key_P: return SDL_SCANCODE_P;
        case Qt::Key_Q: return SDL_SCANCODE_Q;
        case Qt::Key_R: return SDL_SCANCODE_R;
        case Qt::Key_S: return SDL_SCANCODE_S;
        case Qt::Key_T: return SDL_SCANCODE_T;
        case Qt::Key_U: return SDL_SCANCODE_U;
        case Qt::Key_V: return SDL_SCANCODE_V;
        case Qt::Key_W: return SDL_SCANCODE_W;
        case Qt::Key_X: return SDL_SCANCODE_X;
        case Qt::Key_Y: return SDL_SCANCODE_Y;
        case Qt::Key_Z: return SDL_SCANCODE_Z;

        // Digits
        case Qt::Key_0: return SDL_SCANCODE_0;
        case Qt::Key_1: return SDL_SCANCODE_1;
        case Qt::Key_2: return SDL_SCANCODE_2;
        case Qt::Key_3: return SDL_SCANCODE_3;
        case Qt::Key_4: return SDL_SCANCODE_4;
        case Qt::Key_5: return SDL_SCANCODE_5;
        case Qt::Key_6: return SDL_SCANCODE_6;
        case Qt::Key_7: return SDL_SCANCODE_7;
        case Qt::Key_8: return SDL_SCANCODE_8;
        case Qt::Key_9: return SDL_SCANCODE_9;

        // Modifiers
        case Qt::Key_Shift:   return SDL_SCANCODE_LSHIFT;
        case Qt::Key_Control: return SDL_SCANCODE_LCTRL;
        case Qt::Key_Alt:     return SDL_SCANCODE_LALT;

        // Special keys
        case Qt::Key_Return: return SDL_SCANCODE_RETURN;
        case Qt::Key_Enter:  return SDL_SCANCODE_KP_ENTER;
        case Qt::Key_Space:  return SDL_SCANCODE_SPACE;
        case Qt::Key_Backspace: return SDL_SCANCODE_BACKSPACE;
        case Qt::Key_Escape: return SDL_SCANCODE_ESCAPE;

        // Arrow keys (mapped to ZX cursor keys via compound table)
        case Qt::Key_Up:    return SDL_SCANCODE_UP;
        case Qt::Key_Down:  return SDL_SCANCODE_DOWN;
        case Qt::Key_Left:  return SDL_SCANCODE_LEFT;
        case Qt::Key_Right: return SDL_SCANCODE_RIGHT;

        // Function keys
        case Qt::Key_F1:  return SDL_SCANCODE_F1;
        case Qt::Key_F2:  return SDL_SCANCODE_F2;
        case Qt::Key_F3:  return SDL_SCANCODE_F3;
        case Qt::Key_F4:  return SDL_SCANCODE_F4;
        case Qt::Key_F5:  return SDL_SCANCODE_F5;
        case Qt::Key_F6:  return SDL_SCANCODE_F6;
        case Qt::Key_F7:  return SDL_SCANCODE_F7;
        case Qt::Key_F8:  return SDL_SCANCODE_F8;
        case Qt::Key_F9:  return SDL_SCANCODE_F9;
        case Qt::Key_F10: return SDL_SCANCODE_F10;
        case Qt::Key_F11: return SDL_SCANCODE_F11;
        case Qt::Key_F12: return SDL_SCANCODE_F12;

        default: return SDL_SCANCODE_UNKNOWN;
    }
}

const char* speed_name(int idx) {
    switch (idx) {
        case 0: return "3.5 MHz";
        case 1: return "7 MHz";
        case 2: return "14 MHz";
        case 3: return "28 MHz";
        default: return "?";
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("JNEXT \u2014 ZX Spectrum Next Emulator");

    // Central widget: the emulator display (fixed-size, pixel-perfect).
    emulator_widget_ = new EmulatorWidget(this);
    setCentralWidget(emulator_widget_);

    // Build UI elements.
    create_menus();
    create_toolbar();
    create_statusbar();

    // Ensure the window receives key events even when focus is on a child widget.
    setFocusPolicy(Qt::StrongFocus);

    // When the emulator widget detects the real DPR on its first frame,
    // it re-applies its scale and emits scale_changed — we re-fix the
    // window size to match.
    connect(emulator_widget_, &EmulatorWidget::scale_changed, this, [this]() {
        if (!is_fullscreen_)
            apply_fixed_window_size();
    });

    // Apply default scale — the widget sizes itself, then we fix the window around it.
    set_scale(current_scale_);
}

void MainWindow::set_emulator(Emulator* emu) {
    emulator_ = emu;
#ifdef ENABLE_DEBUGGER
    if (!debugger_mgr_ && emu) {
        debugger_mgr_ = new DebuggerManager(this, emu, this);
        // Debugger starts disabled — main window stays fixed-size.
    }
#endif
}

void MainWindow::toggle_fullscreen() {
    if (is_fullscreen_) {
        // Restore chrome.
        menuBar()->show();
        for (QToolBar* tb : findChildren<QToolBar*>()) tb->show();
        statusBar()->show();

        showNormal();
        is_fullscreen_ = false;
        emulator_widget_->set_fullscreen_mode(false);
        // Restore fixed windowed size.
        apply_fixed_window_size();
    } else {
        // Hide chrome for true fullscreen.
        menuBar()->hide();
        for (QToolBar* tb : findChildren<QToolBar*>()) tb->hide();
        statusBar()->hide();

        // Remove fixed-size constraint so the window can go fullscreen.
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        // Let the widget expand in fullscreen.
        emulator_widget_->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        emulator_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        emulator_widget_->set_fullscreen_mode(true);
        showFullScreen();
        is_fullscreen_ = true;
    }

    // Sync the fullscreen menu action state.
    if (fullscreen_action_)
        fullscreen_action_->setChecked(is_fullscreen_);
}

void MainWindow::set_scale(int factor) {
    if (factor < EmulatorWidget::MIN_SCALE) factor = EmulatorWidget::MIN_SCALE;
    if (factor > EmulatorWidget::MAX_SCALE) factor = EmulatorWidget::MAX_SCALE;
    current_scale_ = factor;

    // Size the widget to an exact integer multiple.
    emulator_widget_->set_scale(factor);

    if (!is_fullscreen_) {
        apply_fixed_window_size();
    }

    // Sync the scale menu action group.
    if (scale_group_) {
        for (QAction* a : scale_group_->actions()) {
            if (a->data().toInt() == factor) {
                a->setChecked(true);
                break;
            }
        }
    }
}

void MainWindow::apply_fixed_window_size() {
    // The widget has a fixed size; let Qt compute the minimum window size
    // that fits the widget + chrome (menu bar, toolbar, status bar).
    // Then lock the window to exactly that size.
    emulator_widget_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    adjustSize();
    setFixedSize(size());
}

void MainWindow::cycle_scale() {
    if (is_fullscreen_) return;
    // 2 -> 3 -> 4 -> 2
    int next = current_scale_ + 1;
    if (next > EmulatorWidget::MAX_SCALE) next = EmulatorWidget::MIN_SCALE;
    set_scale(next);
}

void MainWindow::set_crt_filter(bool enabled) {
    emulator_widget_->set_crt_filter(enabled);

    if (crt_filter_action_)
        crt_filter_action_->setChecked(enabled);
}

bool MainWindow::crt_filter() const {
    return emulator_widget_->crt_filter();
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

void MainWindow::create_menus() {
    // --- File menu ---
    QMenu* file_menu = menuBar()->addMenu(tr("&File"));

    QAction* load_nex = file_menu->addAction(tr("Load &NEX File..."));
    load_nex->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    connect(load_nex, &QAction::triggered, this, &MainWindow::on_load_nex);

    QAction* mount_sd = file_menu->addAction(tr("&Mount SD Card Image..."));
    connect(mount_sd, &QAction::triggered, this, &MainWindow::on_mount_sd);

    file_menu->addSeparator();

    record_start_action_ = file_menu->addAction(tr("Start &Recording..."));
    record_start_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
    connect(record_start_action_, &QAction::triggered, this, &MainWindow::on_record_start);

    record_stop_action_ = file_menu->addAction(tr("Sto&p Recording"));
    record_stop_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F6));
    record_stop_action_->setEnabled(false);
    connect(record_stop_action_, &QAction::triggered, this, &MainWindow::on_record_stop);

    file_menu->addSeparator();

    QAction* quit = file_menu->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    // --- Machine menu ---
    QMenu* machine_menu = menuBar()->addMenu(tr("&Machine"));

    QAction* reset = machine_menu->addAction(tr("&Reset"));
    reset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(reset, &QAction::triggered, this, &MainWindow::on_reset);

    machine_menu->addSeparator();

    // Machine type submenu
    QMenu* type_menu = machine_menu->addMenu(tr("Machine &Type"));
    machine_type_group_ = new QActionGroup(this);
    machine_type_group_->setExclusive(true);

    struct TypeEntry { const char* label; MachineType type; };
    TypeEntry type_entries[] = {
        {"ZX Spectrum &48K",  MachineType::ZX48K},
        {"ZX Spectrum &128K", MachineType::ZX128K},
        {"ZX Spectrum +&3",   MachineType::ZX_PLUS3},
        {"&Pentagon",         MachineType::PENTAGON},
        {"ZX &Next",          MachineType::ZXN_ISSUE2},
    };
    for (auto& e : type_entries) {
        QAction* a = type_menu->addAction(tr(e.label));
        a->setCheckable(true);
        a->setData(static_cast<int>(e.type));
        machine_type_group_->addAction(a);
        if (e.type == MachineType::ZXN_ISSUE2) a->setChecked(true);
        connect(a, &QAction::triggered, this, [this, t = e.type]() {
            on_machine_type(t);
        });
    }

    machine_menu->addSeparator();

    QMenu* speed_menu = machine_menu->addMenu(tr("CPU &Speed"));
    speed_group_ = new QActionGroup(this);
    speed_group_->setExclusive(true);

    const char* speed_labels[] = {"3.5 MHz", "7 MHz", "14 MHz", "28 MHz"};
    for (int i = 0; i < 4; ++i) {
        QAction* a = speed_menu->addAction(tr(speed_labels[i]));
        a->setCheckable(true);
        a->setData(i);
        speed_group_->addAction(a);
        if (i == 0) a->setChecked(true);  // default: 3.5 MHz
        connect(a, &QAction::triggered, this, [this, i]() { on_cpu_speed(i); });
    }

    // --- Tape menu ---
    QMenu* tape_menu = menuBar()->addMenu(tr("&Tape"));

    QAction* tape_open = tape_menu->addAction(tr("&Open Tape File..."));
    tape_open->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(tape_open, &QAction::triggered, this, &MainWindow::on_tape_open);

    tape_eject_action_ = tape_menu->addAction(tr("&Eject Tape"));
    tape_eject_action_->setEnabled(false);
    connect(tape_eject_action_, &QAction::triggered, this, &MainWindow::on_tape_eject);

    tape_rewind_action_ = tape_menu->addAction(tr("&Rewind"));
    tape_rewind_action_->setEnabled(false);
    connect(tape_rewind_action_, &QAction::triggered, this, &MainWindow::on_tape_rewind);

    tape_menu->addSeparator();

    tape_fast_action_ = tape_menu->addAction(tr("&Fast Load"));
    tape_fast_action_->setCheckable(true);
    tape_fast_action_->setChecked(true);  // fast load is default
    connect(tape_fast_action_, &QAction::triggered, this, &MainWindow::on_tape_fast_load);

    // --- Debug menu ---
    QMenu* debug_menu = menuBar()->addMenu(tr("&Debug"));

    magic_bp_action_ = debug_menu->addAction(tr("Magic &Breakpoint"));
    magic_bp_action_->setCheckable(true);
    magic_bp_action_->setToolTip(tr("Enable magic breakpoints (ED FF / DD 01)"));
    if (emulator_) magic_bp_action_->setChecked(emulator_->config().magic_breakpoint);
    connect(magic_bp_action_, &QAction::triggered, this, [this](bool checked) {
        if (!emulator_) return;
        EmulatorConfig cfg = emulator_->config();
        cfg.magic_breakpoint = checked;
        emulator_->init(cfg);
    });

    // --- View menu ---
    QMenu* view_menu = menuBar()->addMenu(tr("&View"));

    scale_group_ = new QActionGroup(this);
    scale_group_->setExclusive(true);
    for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s) {
        QAction* a = view_menu->addAction(tr("Scale %1x").arg(s));
        a->setCheckable(true);
        a->setData(s);
        scale_group_->addAction(a);
        if (s == current_scale_) a->setChecked(true);
        connect(a, &QAction::triggered, this, [this, s]() { on_scale(s); });
    }

    view_menu->addSeparator();

    fullscreen_action_ = view_menu->addAction(tr("&Fullscreen"));
    // F11 toggles fullscreen; when debugger is enabled, F11 is intercepted
    // for Step Into and Ctrl+F11 is used for fullscreen instead (handled in keyPressEvent).
    fullscreen_action_->setShortcut(QKeySequence(Qt::Key_F11));
    fullscreen_action_->setCheckable(true);
    connect(fullscreen_action_, &QAction::triggered, this, &MainWindow::on_fullscreen);

    view_menu->addSeparator();

    crt_filter_action_ = view_menu->addAction(tr("CRT &Filter"));
    crt_filter_action_->setCheckable(true);
    connect(crt_filter_action_, &QAction::triggered, this, [this](bool checked) {
        emulator_widget_->set_crt_filter(checked);
    });

#ifdef ENABLE_DEBUGGER
    view_menu->addSeparator();
    debugger_action_ = view_menu->addAction(tr("&Debugger"));
    debugger_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    debugger_action_->setCheckable(true);
#endif

    // --- Help menu ---
    QMenu* help_menu = menuBar()->addMenu(tr("&Help"));

    QAction* about = help_menu->addAction(tr("&About JNEXT..."));
    connect(about, &QAction::triggered, this, &MainWindow::on_about);
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void MainWindow::create_toolbar() {
    QToolBar* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);

    QAction* reset_btn = toolbar->addAction(
        style()->standardIcon(QStyle::SP_BrowserReload), tr("Reset"));
    connect(reset_btn, &QAction::triggered, this, &MainWindow::on_reset);

    QAction* load_btn = toolbar->addAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Load"));
    connect(load_btn, &QAction::triggered, this, &MainWindow::on_load_nex);

}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void MainWindow::create_statusbar() {
    fps_label_ = new QLabel(tr("FPS: --"));
    fps_label_->setMinimumWidth(100);

    speed_label_ = new QLabel(tr("3.5 MHz"));
    speed_label_->setMinimumWidth(100);
    speed_label_->setAlignment(Qt::AlignCenter);

    machine_label_ = new QLabel(tr("ZX Next"));
    machine_label_->setMinimumWidth(100);
    machine_label_->setAlignment(Qt::AlignRight);

    tape_label_ = new QLabel(tr("Tape: none"));
    tape_label_->setMinimumWidth(120);
    tape_label_->setAlignment(Qt::AlignCenter);

    statusBar()->addWidget(fps_label_, 1);
    statusBar()->addWidget(speed_label_, 1);
    statusBar()->addWidget(tape_label_, 1);
    statusBar()->addPermanentWidget(machine_label_);
}

// ---------------------------------------------------------------------------
// Status update
// ---------------------------------------------------------------------------

void MainWindow::update_status(double fps, int cpu_speed_idx) {
    if (fps_label_)
        fps_label_->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));

    const char* spd = speed_name(cpu_speed_idx);
    if (speed_label_)
        speed_label_->setText(QString(spd));

    // Update the speed radio button to match actual hardware state.
    if (speed_group_) {
        for (QAction* a : speed_group_->actions()) {
            if (a->data().toInt() == cpu_speed_idx) {
                a->setChecked(true);
                break;
            }
        }
    }

    // Update tape status display
    update_tape_status();
}

// ---------------------------------------------------------------------------
// Menu action handlers
// ---------------------------------------------------------------------------

void MainWindow::on_load_nex() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Load Program"), QString(),
        tr("Spectrum Files (*.nex *.sna *.szx *.tap *.tzx *.wav);;NEX Files (*.nex);;SNA Snapshots (*.sna);;SZX Snapshots (*.szx);;TAP Files (*.tap);;TZX Files (*.tzx);;WAV Files (*.wav);;All Files (*)"));
    if (!path.isEmpty()) {
        if (emulator_) {
            if (path.toLower().endsWith(".tap")) {
                emulator_->load_tap(path.toStdString());
            } else if (path.toLower().endsWith(".tzx")) {
                emulator_->load_tzx(path.toStdString());
            } else if (path.toLower().endsWith(".sna")) {
                emulator_->load_sna(path.toStdString());
            } else if (path.toLower().endsWith(".szx")) {
                emulator_->load_szx(path.toStdString());
            } else if (path.toLower().endsWith(".wav")) {
                emulator_->load_wav(path.toStdString());
            } else {
                emulator_->load_nex(path.toStdString());
            }
        }
        emit load_nex_requested(path);
    }
}

void MainWindow::on_mount_sd() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Mount SD Card Image"), QString(),
        tr("Disk Images (*.img *.bin);;All Files (*)"));
    if (!path.isEmpty()) {
        // SD card mounting requires restart; just store and inform user.
        QMessageBox::information(this, tr("SD Card"),
            tr("SD card image selected:\n%1\n\n"
               "Restart the emulator with --sd-card to use this image.").arg(path));
        emit sd_card_selected(path);
    }
}

void MainWindow::on_reset() {
    if (emulator_) {
        emulator_->reset();
    }
}

void MainWindow::on_machine_type(MachineType type) {
    if (!emulator_) return;
    // Machine type change requires full reinit — update config and reset
    EmulatorConfig cfg = emulator_->config();
    cfg.type = type;
    emulator_->init(cfg);
    machine_label_->setText(tr(machine_type_str(type)));
}

void MainWindow::on_cpu_speed(int speed_idx) {
    if (emulator_ && speed_idx >= 0 && speed_idx <= 3) {
        emulator_->nextreg().write(0x07, static_cast<uint8_t>(speed_idx));
    }
}

void MainWindow::on_scale(int factor) {
    set_scale(factor);
    emit scale_requested(factor);
}

void MainWindow::on_fullscreen(bool checked) {
    // Sync toggle_fullscreen state with menu action.
    if (checked != is_fullscreen_)
        toggle_fullscreen();
}

void MainWindow::on_about() {
    QMessageBox::about(this, tr("About JNEXT"),
        tr("<h3>JNEXT</h3>"
           "<p>ZX Spectrum Next Emulator</p>"
           "<p>A line-accurate emulator of the ZX Spectrum Next computer, "
           "based on the official FPGA VHDL sources.</p>"
           "<p>Written in C++17 with Qt 6 and SDL2.</p>"));
}

// ---------------------------------------------------------------------------
// Tape menu handlers
// ---------------------------------------------------------------------------

void MainWindow::on_tape_open() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open Tape File"), QString(),
        tr("Tape Files (*.tap *.tzx *.wav);;TAP Files (*.tap);;TZX Files (*.tzx);;WAV Files (*.wav);;All Files (*)"));
    if (path.isEmpty() || !emulator_) return;

    if (path.toLower().endsWith(".tzx")) {
        emulator_->load_tzx(path.toStdString());
    } else if (path.toLower().endsWith(".wav")) {
        emulator_->load_wav(path.toStdString());
    } else {
        emulator_->load_tap(path.toStdString());
    }
    update_tape_status();
}

void MainWindow::on_tape_eject() {
    if (!emulator_) return;
    emulator_->tape().eject();
    emulator_->tzx_tape().eject();
    update_tape_status();
}

void MainWindow::on_tape_rewind() {
    if (!emulator_) return;
    if (emulator_->tzx_tape().is_loaded()) {
        emulator_->tzx_tape().rewind();
    } else {
        emulator_->tape().rewind();
    }
    update_tape_status();
}

void MainWindow::on_tape_fast_load(bool checked) {
    if (!emulator_) return;
    emulator_->tape().set_fast_load(checked);
    emulator_->tzx_tape().set_fast_load(checked);
}

void MainWindow::update_tape_status() {
    if (!tape_label_ || !emulator_) return;

    const auto& tap = emulator_->tape();
    const auto& tzx = emulator_->tzx_tape();

    if (tzx.is_loaded()) {
        QString name = QString::fromStdString(tzx.filename());
        tape_label_->setText(tr("Tape: %1").arg(name));
    } else if (tap.is_loaded()) {
        QString name = QString::fromStdString(tap.filename());
        tape_label_->setText(tr("Tape: %1 [%2/%3]")
            .arg(name)
            .arg(tap.current_block())
            .arg(tap.block_count()));
    } else {
        tape_label_->setText(tr("Tape: none"));
    }

    // Enable/disable menu actions
    bool has_tape = tap.is_loaded() || tzx.is_loaded();
    if (tape_eject_action_)  tape_eject_action_->setEnabled(has_tape);
    if (tape_rewind_action_) tape_rewind_action_->setEnabled(has_tape);
}

// ---------------------------------------------------------------------------
// Recording handlers
// ---------------------------------------------------------------------------

void MainWindow::on_record_start() {
    if (!emulator_) return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Record Video"), QString(),
        tr("MP4 Video (*.mp4);;All Files (*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".mp4", Qt::CaseInsensitive))
        path += ".mp4";

    if (!emulator_->start_recording(path.toStdString())) {
        QMessageBox::warning(this, tr("Recording Error"),
            tr("Failed to start recording.\n"
               "Make sure FFmpeg is installed and available in PATH."));
        return;
    }

    record_start_action_->setEnabled(false);
    record_stop_action_->setEnabled(true);
    statusBar()->showMessage(tr("Recording..."), 0);
}

void MainWindow::on_record_stop() {
    if (!emulator_) return;

    statusBar()->showMessage(tr("Encoding video..."), 0);
    QApplication::processEvents();  // show message before blocking encode

    bool ok = emulator_->stop_recording();

    record_start_action_->setEnabled(true);
    record_stop_action_->setEnabled(false);

    if (ok) {
        statusBar()->showMessage(tr("Recording saved"), 3000);
    } else {
        QMessageBox::warning(this, tr("Recording Error"),
            tr("FFmpeg encoding failed. Check console output for details."));
        statusBar()->clearMessage();
    }
}

// ---------------------------------------------------------------------------
// Key event handling
// ---------------------------------------------------------------------------

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        int key = event->key();
        Qt::KeyboardModifiers modifiers = event->modifiers();

#ifdef ENABLE_DEBUGGER
        // When debugger is enabled, intercept debug shortcuts.
        if (debugger_mgr_ && debugger_mgr_->is_enabled()) {
            if (key == Qt::Key_F5) {
                debugger_mgr_->on_run();
                event->accept();
                return;
            }
            if (key == Qt::Key_F6) {
                debugger_mgr_->on_step_into();
                event->accept();
                return;
            }
            if (key == Qt::Key_F7) {
                debugger_mgr_->on_step_over();
                event->accept();
                return;
            }
            if (key == Qt::Key_F8) {
                debugger_mgr_->on_step_out();
                event->accept();
                return;
            }
            if (key == Qt::Key_F9) {
                debugger_mgr_->on_pause();
                event->accept();
                return;
            }
        }
#endif

        switch (key) {
        case Qt::Key_F11:
            toggle_fullscreen();
            event->accept();
            return;
        case Qt::Key_Escape:
            if (is_fullscreen_) {
                toggle_fullscreen();
                event->accept();
                return;
            }
            break;
        case Qt::Key_F2:
            cycle_scale();
            event->accept();
            return;
        default:
            break;
        }
    }

    handle_key(event, true);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    // Consume release events for keys we handle in keyPressEvent.
    if (!event->isAutoRepeat()) {
        int key = event->key();
        if (key == Qt::Key_F11 || key == Qt::Key_F2 || key == Qt::Key_Escape) {
            event->accept();
            return;
        }
#ifdef ENABLE_DEBUGGER
        if (debugger_mgr_ && debugger_mgr_->is_enabled()) {
            if (key == Qt::Key_F5 || key == Qt::Key_F6 || key == Qt::Key_F7 ||
                key == Qt::Key_F8 || key == Qt::Key_F9) {
                event->accept();
                return;
            }
        }
#endif
    }

    handle_key(event, false);
}

void MainWindow::handle_key(QKeyEvent* event, bool pressed) {
    // Ignore auto-repeat (ZX keyboard matrix doesn't auto-repeat).
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    SDL_Scancode sc = qt_key_to_sdl(event->key());
    if (sc != SDL_SCANCODE_UNKNOWN && key_callback_) {
        key_callback_(sc, pressed);
        event->accept();
    } else {
        QMainWindow::keyPressEvent(static_cast<QKeyEvent*>(event));
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Stop any active recording before closing.
    if (emulator_ && emulator_->video_recorder().is_recording()) {
        emulator_->stop_recording();
    }

#ifdef ENABLE_DEBUGGER
    if (debugger_mgr_) {
        debugger_mgr_->set_enabled(false);
        auto* dbg_win = debugger_mgr_->debugger_window_ptr();
        if (dbg_win)
            dbg_win->close();
    }
#endif
    QMainWindow::closeEvent(event);
}
