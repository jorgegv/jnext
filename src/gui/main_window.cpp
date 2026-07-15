#include "gui/main_window.h"
#include "version.h"
#include "gui/emulator_widget.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/video_recorder.h"
#include "core/sna_saver.h"
#include "core/szx_saver.h"
#include "core/nex_saver.h"
#include "core/log.h"
#include "platform/screenshot.h"
#include "input/mouse_dispatcher.h"
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#endif

#include <QKeyEvent>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QFile>
#include <QFileDialog>
#include <QIODevice>
#include <QMessageBox>
#include <QLabel>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QInputDialog>
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

    // Enable mouse tracking so motion events flow continuously even with no
    // buttons held — Kempston mouse drivers (Art Studio Next, mouse demos)
    // sample motion every poll, not just while dragging. Both the window
    // and the central emulator viewport need tracking on; events bubble
    // up to MainWindow's overrides via Qt's event propagation. (G43.)
    setMouseTracking(true);
    if (emulator_widget_) {
        emulator_widget_->setMouseTracking(true);
        // Hide the host cursor when it's over the emulator viewport. The
        // emulator-rendered Kempston-mouse cursor (e.g. trainyard, Art
        // Studio Next) doubles up with the host cursor otherwise. Qt
        // restores the normal cursor automatically as soon as the mouse
        // leaves the widget (so menus / toolbars stay clickable).
        emulator_widget_->setCursor(Qt::BlankCursor);
    }

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

// Defined out-of-line so unique_ptr<MouseDispatcher> can be declared with
// only a forward declaration in main_window.h. The header for
// MouseDispatcher is included at the top of this translation unit, so
// the compiler has the full type here.
MainWindow::~MainWindow() = default;

void MainWindow::set_emulator(Emulator* emu) {
    emulator_ = emu;
    // Construct the Kempston mouse host adapter once the emulator (and its
    // KempstonMouse) is bound. Mirrors SdlApp::init() — same ownership
    // pattern, parallel transport (Qt events instead of SDL events).
    // G43 closure for the Qt UI path (default build).
    if (emu) {
        mouse_dispatcher_ = std::make_unique<MouseDispatcher>(emu->mouse());
        // Task 60c — after a rewind / save-load restore, re-seed the mouse
        // dispatcher's cumulative shadow (wheel/button) from the restored
        // KempstonMouse so the next Qt wheel/button event does not stomp the
        // restore. (The Qt path has no joystick dispatcher; gamepad input is
        // SDL-only. See src/input/mouse_dispatcher.cpp::resync.)
        emu->on_input_state_restored = [this]() {
            if (mouse_dispatcher_) {
                mouse_dispatcher_->resync();
            }
        };
    } else {
        mouse_dispatcher_.reset();
    }
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
    emulator_widget_->updateGeometry();
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

    record_start_action_ = file_menu->addAction(tr("Record &MPEG4 Video..."));
    record_start_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
    connect(record_start_action_, &QAction::triggered, this, &MainWindow::on_record_start);

    record_stop_action_ = file_menu->addAction(tr("Sto&p MPEG4 Recording"));
    record_stop_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F6));
    record_stop_action_->setEnabled(false);
    connect(record_stop_action_, &QAction::triggered, this, &MainWindow::on_record_stop);

    file_menu->addSeparator();

    QAction* rzx_play = file_menu->addAction(tr("Play &RZX Recording..."));
    connect(rzx_play, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, tr("Play RZX File"), QString(),
            tr("RZX Files (*.rzx);;All Files (*)"));
        if (!path.isEmpty() && emulator_) {
            emulator_->load_rzx(path.toStdString());
        }
    });

    QAction* rzx_record = file_menu->addAction(tr("Record R&ZX..."));
    connect(rzx_record, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getSaveFileName(
            this, tr("Record RZX File"), QString(),
            tr("RZX Files (*.rzx);;All Files (*)"));
        if (!path.isEmpty() && emulator_) {
            emulator_->start_rzx_recording(path.toStdString());
        }
    });

    QAction* rzx_stop = file_menu->addAction(tr("Stop RZX Recordin&g"));
    connect(rzx_stop, &QAction::triggered, this, [this]() {
        if (emulator_) emulator_->stop_rzx_recording();
    });

    file_menu->addSeparator();

    screenshot_action_ = file_menu->addAction(tr("Save &Screenshot..."));
    screenshot_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    connect(screenshot_action_, &QAction::triggered, this, [this]() {
        if (!emulator_) return;
        QString path = QFileDialog::getSaveFileName(
            this, tr("Save Screenshot"), QString(),
            tr("PNG Images (*.png);;All Files (*)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(".png", Qt::CaseInsensitive))
            path += ".png";
        // The framebuffer is canonical 640×256 in-memory; save_screenshot_png
        // vertically doubles to a 640×512 PNG with square pixels (G104).
        bool ok = save_screenshot_png(path.toStdString(),
                                       emulator_->get_framebuffer(),
                                       emulator_->get_framebuffer_width(),
                                       emulator_->get_framebuffer_height());
        if (ok) {
            statusBar()->showMessage(tr("Screenshot saved: %1").arg(path), 3000);
        } else {
            QMessageBox::warning(this, tr("Screenshot Error"),
                tr("Failed to save screenshot to:\n%1").arg(path));
        }
    });

    // Save Snapshot... — wires SnaSaver to the GUI (G35: closes
    // BOOT-SNAPSAVE-01 + BOOT-SNAPSAVE-04). Ctrl+Shift+S = standard
    // "Save As" muscle memory; complements Ctrl+S for screenshot.
    QAction* save_snapshot = file_menu->addAction(tr("Save S&napshot..."));
    save_snapshot->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(save_snapshot, &QAction::triggered, this, &MainWindow::on_save_snapshot);

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

    machine_menu->addSeparator();

    // Emulator speed (throttle) submenu
    QMenu* emu_speed_menu = machine_menu->addMenu(tr("&Emulator Speed"));
    emu_speed_group_ = new QActionGroup(this);
    emu_speed_group_->setExclusive(true);

    struct EmuSpeedEntry { const char* label; double multiplier; };
    EmuSpeedEntry emu_speeds[] = {
        {"0.5x (50%)",  0.5},
        {"1x (100%)",   1.0},
        {"2x (200%)",   2.0},
        {"4x (400%)",   4.0},
    };
    for (auto& e : emu_speeds) {
        QAction* a = emu_speed_menu->addAction(tr(e.label));
        a->setCheckable(true);
        a->setData(e.multiplier);
        emu_speed_group_->addAction(a);
        if (e.multiplier == 1.0) a->setChecked(true);
        connect(a, &QAction::triggered, this, [this, m = e.multiplier]() {
            if (speed_callback_) speed_callback_(m);
        });
    }

    emu_speed_menu->addSeparator();

    QAction* custom_speed = emu_speed_menu->addAction(tr("&Custom..."));
    connect(custom_speed, &QAction::triggered, this, [this]() {
        bool ok;
        int percent = QInputDialog::getInt(this, tr("Emulator Speed"),
            tr("Speed (%):"), 100, 10, 1000, 10, &ok);
        if (ok) {
            double multiplier = percent / 100.0;
            if (speed_callback_) speed_callback_(multiplier);
            // Uncheck all preset actions (custom speed doesn't match any)
            if (emu_speed_group_) {
                for (QAction* a : emu_speed_group_->actions()) {
                    if (std::abs(a->data().toDouble() - multiplier) < 0.01) {
                        a->setChecked(true);
                        return;
                    }
                }
                // No match — uncheck all
                if (auto* checked = emu_speed_group_->checkedAction())
                    checked->setChecked(false);
            }
        }
    });

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

    toolbar->addSeparator();

    // Screenshot toolbar button — triggers the same action as File > Save Screenshot.
    if (screenshot_action_) {
        QAction* screenshot_btn = toolbar->addAction(
            QIcon::fromTheme("camera-photo"), tr("Screenshot"));
        connect(screenshot_btn, &QAction::triggered, screenshot_action_, &QAction::trigger);
    }

    toolbar->addSeparator();

    // Multiface NMI toolbar button — text-only, mirrors F9 hotkey path.
    QAction* nmi_btn = toolbar->addAction(tr("NMI"));
    nmi_btn->setToolTip(tr("Multiface NMI (F9)"));
    connect(nmi_btn, &QAction::triggered, this, [this]() {
        if (emulator_) {
            emulator_->on_hotkey_f9_mf_nmi();
        }
    });
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

    emu_speed_label_ = new QLabel(tr("1x"));
    emu_speed_label_->setMinimumWidth(60);
    emu_speed_label_->setAlignment(Qt::AlignCenter);

    statusBar()->addWidget(fps_label_, 1);
    statusBar()->addWidget(speed_label_, 1);
    statusBar()->addWidget(emu_speed_label_, 1);
    statusBar()->addWidget(tape_label_, 1);
    statusBar()->addPermanentWidget(machine_label_);
}

// ---------------------------------------------------------------------------
// Status update
// ---------------------------------------------------------------------------

void MainWindow::update_status(double fps, int cpu_speed_idx, double emu_speed) {
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

    // Show emulator speed multiplier.
    if (emu_speed_label_) {
        int pct = static_cast<int>(emu_speed * 100.0 + 0.5);
        emu_speed_label_->setText(QString("%1%").arg(pct));
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
        tr("Spectrum Files (*.nex *.sna *.szx *.z80 *.tap *.tzx *.wav *.rzx);;NEX Files (*.nex);;SNA Snapshots (*.sna);;SZX Snapshots (*.szx);;Z80 Snapshots (*.z80);;TAP Files (*.tap);;TZX Files (*.tzx);;WAV Files (*.wav);;RZX Recordings (*.rzx);;All Files (*)"));
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
            } else if (path.toLower().endsWith(".z80")) {
                emulator_->load_z80(path.toStdString());
            } else if (path.toLower().endsWith(".wav")) {
                emulator_->load_wav(path.toStdString());
            } else if (path.toLower().endsWith(".rzx")) {
                emulator_->load_rzx(path.toStdString());
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
               "Restart the emulator with --sdcard to use this image.").arg(path));
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
        tr("<h3>JNEXT %1</h3>"
           "<p>ZX Spectrum Next Emulator</p>"
           "<p>A line-accurate emulator of the ZX Spectrum Next computer, "
           "based on the official FPGA VHDL sources.</p>"
           "<p>Written in C++17 with Qt 6 and SDL2.</p>"
           "<p>&copy; Copyright 2026 Jorge Gonzalez Villalonga &lt;zx@jogv.es&gt;</p>")
        .arg(JNEXT_VERSION_STRING));
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

    if (!VideoRecorder::ffmpeg_available()) {
        QMessageBox::warning(this, tr("Recording Error"),
            tr("FFmpeg is not installed or not found in PATH.\n\n"
               "Install it with your package manager, e.g.:\n"
               "  sudo dnf install ffmpeg\n"
               "  sudo apt install ffmpeg"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("Record MPEG4 Video"), QString(),
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

// G35: wires SnaSaver/SzxSaver/NexSaver to File > Save Snapshot... —
// closes BOOT-SNAPSAVE-01/02/03/04 in mmu_test. Format is chosen by the
// extension the user picks (or types); defaults to .sna if none/unknown.
void MainWindow::on_save_snapshot() {
    if (!emulator_) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Snapshot"), QString(),
        tr("Spectrum snapshot (*.sna);;ZX-State snapshot (*.szx);;NEX program (*.nex);;All Files (*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".sna", Qt::CaseInsensitive) &&
        !path.endsWith(".szx", Qt::CaseInsensitive) &&
        !path.endsWith(".nex", Qt::CaseInsensitive)) {
        path += ".sna";
    }

    std::vector<uint8_t> bytes;
    if (path.endsWith(".szx", Qt::CaseInsensitive)) {
        // .szx is a classic-Spectrum interchange format: it can only
        // represent 48K/128K/+2A/+3 — see SzxSaver class doc-comment
        // SCOPE. jnext's default machine (Next) is refused outright, with
        // a clear error rather than a truncated or misrepresenting file.
        auto result = SzxSaver::save(*emulator_);
        if (!result.ok) {
            QMessageBox::warning(this, tr("Save Snapshot"),
                QString::fromStdString(result.error));
            return;
        }
        bytes = result.data;
    } else if (path.endsWith(".nex", Qt::CaseInsensitive)) {
        // .nex has its own (looser, 112-bank) ceiling — see NexSaver
        // class doc-comment; NexSaver::save() already logs a warning.
        bytes = NexSaver::save(*emulator_).data;
    } else {
        bytes = SnaSaver::save(*emulator_);
    }
    if (bytes.empty()) {
        QMessageBox::warning(this, tr("Save Snapshot"),
            tr("Snapshot saver returned an empty buffer; nothing written."));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Save Snapshot"),
            tr("Failed to open '%1' for writing").arg(path));
        return;
    }
    qint64 written = f.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<qint64>(bytes.size()));
    f.close();
    if (written != static_cast<qint64>(bytes.size())) {
        QMessageBox::warning(this, tr("Save Snapshot"),
            tr("Short write to '%1' (%2 of %3 bytes)")
                .arg(path).arg(written).arg(bytes.size()));
        return;
    }
    Log::emulator()->info("Snapshot saved to '{}' ({} bytes)",
                          path.toStdString(), bytes.size());
    statusBar()->showMessage(tr("Snapshot saved: %1").arg(path), 3000);
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

        // Host hotkeys -> Emulator NMI / reset paths (G152). VHDL
        // zxnext.vhd:6340-6371: F1=hotkey_hard_reset, F4=hotkey_soft_reset,
        // F9=hotkey_m1 (Multiface), F10=hotkey_drive (DivMMC). Routed
        // through Emulator::on_hotkey_f*() dispatchers so the GUI never
        // touches NmiSource directly.
        case Qt::Key_F1:
            if (emulator_ && !modifiers.testFlag(Qt::ShiftModifier)
                          && !modifiers.testFlag(Qt::ControlModifier)) {
                emulator_->on_hotkey_f1_hard_reset();
                event->accept();
                return;
            }
            break;
        case Qt::Key_F4:
            if (emulator_ && !modifiers.testFlag(Qt::ShiftModifier)
                          && !modifiers.testFlag(Qt::ControlModifier)) {
                emulator_->on_hotkey_f4_soft_reset();
                event->accept();
                return;
            }
            break;
        case Qt::Key_F9:
            // F9 is reserved for the debugger pause when the debugger is
            // active; the ENABLE_DEBUGGER block above already handles
            // that case and returns early. Reaching here means the
            // debugger is OFF and F9 should fire the Multiface NMI.
            if (emulator_) {
                emulator_->on_hotkey_f9_mf_nmi();
                event->accept();
                return;
            }
            break;
        case Qt::Key_F10:
            if (emulator_) {
                emulator_->on_hotkey_f10_divmmc_nmi();
                event->accept();
                return;
            }
            break;

        // Host hotkeys -> EmuFnKeys FSM (G147). VHDL zxnext.vhd:6340-6349
        // emit_F[3,5,6,7,8] are membrane "fnkey strobes" produced by the
        // emu_fnkeys FSM (input/membrane/emu_fnkeys.vhd) when the user
        // taps M1+F3/M1+F5/M1+F6/M1+F7/M1+F8. The FSM gates F3/F8 by
        // NR 0x06 bits 5/7 (handled inside EmuFnKeys). F5/F6 carry the
        // expansion-bus enable / disable strobes; F7 cycles scanlines.
        // Routed through Emulator::emu_fnkeys() which the host installed
        // side-effect callbacks for in emulator.cpp.
        //
        // In production the FSM advances on a per-tick cadence driven
        // by the emulator clock. For host-driven F-key taps we use the
        // self-contained `simulate_mf_fkey_press()` helper that drives
        // the FSM through IDLE → MF_ROW_A11 → MF_ROW_A12 → MF_CHECK →
        // MF_DONE in one host-event boundary. The side-effect callback
        // fires on entry to MF_DONE.
        //
        // F2 is intentionally NOT routed here — it remains the GUI scale
        // cycler (case Qt::Key_F2 above). F1/F4/F9/F10 are owned by G152
        // (reset/NMI). When the debugger is active F5/F6/F7/F8/F9 are
        // claimed earlier in this function and never reach this block.
        case Qt::Key_F3:
            if (emulator_) {
                emulator_->emu_fnkeys().simulate_mf_fkey_press(3);
                event->accept();
                return;
            }
            break;
        case Qt::Key_F5:
            if (emulator_) {
                emulator_->emu_fnkeys().simulate_mf_fkey_press(5);
                event->accept();
                return;
            }
            break;
        case Qt::Key_F6:
            if (emulator_) {
                emulator_->emu_fnkeys().simulate_mf_fkey_press(6);
                event->accept();
                return;
            }
            break;
        case Qt::Key_F7:
            if (emulator_) {
                emulator_->emu_fnkeys().simulate_mf_fkey_press(7);
                event->accept();
                return;
            }
            break;
        case Qt::Key_F8:
            if (emulator_) {
                emulator_->emu_fnkeys().simulate_mf_fkey_press(8);
                event->accept();
                return;
            }
            break;

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
        // G152 host hotkeys (F1/F4/F9/F10) — consume release so the ZX
        // keyboard matrix doesn't see them.
        if (key == Qt::Key_F1 || key == Qt::Key_F4
            || key == Qt::Key_F9 || key == Qt::Key_F10) {
            event->accept();
            return;
        }
        // G147 host hotkeys (F3/F5/F6/F7/F8) — same release-consume rule.
        // The FSM-driven press is fully consumed at keyPressEvent (one-shot
        // simulate_mf_fkey_press); the release just needs to not bleed into
        // the ZX matrix.
        if (key == Qt::Key_F3 || key == Qt::Key_F5
            || key == Qt::Key_F6 || key == Qt::Key_F7 || key == Qt::Key_F8) {
#ifdef ENABLE_DEBUGGER
            // When the debugger is active F5/F6/F7/F8 belong to it — fall
            // through so the debugger's own release handling can run.
            if (!(debugger_mgr_ && debugger_mgr_->is_enabled() &&
                  (key == Qt::Key_F5 || key == Qt::Key_F6 ||
                   key == Qt::Key_F7 || key == Qt::Key_F8))) {
                event->accept();
                return;
            }
#else
            event->accept();
            return;
#endif
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

// ---------------------------------------------------------------------------
// Mouse event handlers — G43 closure for Qt UI path.
//
// Translate Qt events to the SDL conventions the MouseDispatcher expects,
// then forward to its transport-agnostic API. Kempston ports populated:
//   port_fbdf_dat  ← X counter (zxnext.vhd:3546)
//   port_ffdf_dat  ← Y counter (zxnext.vhd:3553)
//   port_fadf_dat  ← wheel | buttons (zxnext.vhd:3560)
// MOUSE-13/14/15 unit tests assert the transport-agnostic surface; this
// path feeds it from the production GUI binary.
// ---------------------------------------------------------------------------

namespace {

// Map a Qt::MouseButton to the SDL_BUTTON_* constant the dispatcher
// already understands. Kempston has only L/M/R, so XButton1/2 are dropped
// (return 0 → dispatcher ignores).
uint8_t qt_button_to_sdl(Qt::MouseButton b) {
    switch (b) {
    case Qt::LeftButton:   return SDL_BUTTON_LEFT;    // 1
    case Qt::MiddleButton: return SDL_BUTTON_MIDDLE;  // 2
    case Qt::RightButton:  return SDL_BUTTON_RIGHT;   // 3
    default:               return 0;
    }
}

} // anonymous namespace

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!mouse_dispatcher_) {
        QMainWindow::mouseMoveEvent(event);
        return;
    }

    // Map the host cursor's global position to the EmulatorWidget viewport
    // and then to a "ZX cursor space" of 320×256 (the Kempston-mouse visible
    // range used by most wide-Next-mode games, e.g. trainyard which clamps
    // to 0..319/0..255 in software). We forward a delta from the previous
    // mapped position to the current one, instead of raw host-pixel deltas,
    // so:
    //   - cursor visual speed matches the host cursor 1:1 regardless of
    //     window scale (320 ZX-px stretched to N widget-px → 1 host-px of
    //     motion = N/320 widget-px of motion = the visible cursor distance);
    //   - when the host cursor leaves at one viewport edge and re-enters at
    //     another, the first new mouse-move event produces a large delta
    //     that pulls the ZX cursor toward the new entry point — boundary
    //     transitions stay "mostly continuous". (The Kempston counter is
    //     8-bit, so very large warps are softened by signed-8-bit wrap in
    //     KempstonMouse::inject_delta; small motions are exact.)
    static constexpr int ZX_CURSOR_W = 320;
    static constexpr int ZX_CURSOR_H = 256;

    const QPoint global = event->globalPosition().toPoint();
    const int ww = emulator_widget_ ? emulator_widget_->width()  : 0;
    const int wh = emulator_widget_ ? emulator_widget_->height() : 0;
    if (emulator_widget_ && ww > 0 && wh > 0) {
        const QPoint local = emulator_widget_->mapFromGlobal(global);
        const int zx_x = static_cast<int>(
            static_cast<long long>(local.x()) * ZX_CURSOR_W / ww);
        const int zx_y = static_cast<int>(
            static_cast<long long>(local.y()) * ZX_CURSOR_H / wh);
        if (have_last_mouse_pos_) {
            const int dx = zx_x - last_zx_x_;
            const int dy = zx_y - last_zx_y_;
            if (dx != 0 || dy != 0) {
                mouse_dispatcher_->handle_motion(dx, dy);
            }
        }
        last_zx_x_ = zx_x;
        last_zx_y_ = zx_y;
    } else if (have_last_mouse_pos_) {
        // Fallback: emulator widget not ready — forward verbatim host deltas.
        const int dx = global.x() - last_mouse_pos_.x();
        const int dy = global.y() - last_mouse_pos_.y();
        if (dx != 0 || dy != 0) {
            mouse_dispatcher_->handle_motion(dx, dy);
        }
    }
    last_mouse_pos_ = global;
    have_last_mouse_pos_ = true;
    event->accept();
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (!mouse_dispatcher_) {
        QMainWindow::mousePressEvent(event);
        return;
    }
    // Make sure the next motion event computes a delta from the press
    // location, not from a stale cursor position from before the user
    // alt-tabbed away. Also seed last_zx_x_/y_ so the position-mapped
    // delta path stays consistent (mouseMoveEvent uses ZX-space deltas
    // rather than raw host deltas).
    const QPoint global = event->globalPosition().toPoint();
    last_mouse_pos_ = global;
    have_last_mouse_pos_ = true;
    if (emulator_widget_) {
        const int ww = emulator_widget_->width();
        const int wh = emulator_widget_->height();
        if (ww > 0 && wh > 0) {
            const QPoint local = emulator_widget_->mapFromGlobal(global);
            last_zx_x_ = static_cast<int>(
                static_cast<long long>(local.x()) * 320 / ww);
            last_zx_y_ = static_cast<int>(
                static_cast<long long>(local.y()) * 256 / wh);
        }
    }

    const uint8_t sdl_btn = qt_button_to_sdl(event->button());
    if (sdl_btn) {
        mouse_dispatcher_->handle_button(sdl_btn, true);
    }
    event->accept();
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (!mouse_dispatcher_) {
        QMainWindow::mouseReleaseEvent(event);
        return;
    }
    const uint8_t sdl_btn = qt_button_to_sdl(event->button());
    if (sdl_btn) {
        mouse_dispatcher_->handle_button(sdl_btn, false);
    }
    event->accept();
}

void MainWindow::wheelEvent(QWheelEvent* event) {
    if (!mouse_dispatcher_) {
        QMainWindow::wheelEvent(event);
        return;
    }
    // Qt6: angleDelta() is in eighths of a degree; one detent = 120 units.
    // Integer-divide so a half-detent (high-resolution wheels) accumulates
    // toward the next tick rather than producing fractional Kempston steps.
    // y > 0 = wheel rolled away from user (matches SDL_MOUSEWHEEL convention
    // the dispatcher already encodes).
    const int qt_y = event->angleDelta().y();
    const int detents = qt_y / 120;
    if (detents != 0) {
        mouse_dispatcher_->handle_wheel(detents);
    }
    event->accept();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Stop any active recording before closing.
    if (emulator_ && emulator_->video_recorder().is_recording()) {
        emulator_->stop_recording();
    }

#ifdef ENABLE_DEBUGGER
    if (debugger_mgr_) {
        // Task 60e: set_enabled(false) prompts (and can be declined) when the
        // machine is corrupt. Only force the debugger window closed if the
        // disable actually happened — otherwise the forced close would
        // re-trigger DebuggerWindow::closeEvent (a SECOND prompt) and hide a
        // still-enabled debugger. The app itself still quits regardless.
        bool disabled = debugger_mgr_->set_enabled(false);
        if (disabled) {
            auto* dbg_win = debugger_mgr_->debugger_window_ptr();
            if (dbg_win)
                dbg_win->close();
        }
    }
#endif
    QMainWindow::closeEvent(event);
}
