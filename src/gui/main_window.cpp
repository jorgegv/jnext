#include "gui/main_window.h"
#include "version.h"
#include "gui/emulator_widget.h"
#include "gui/preferences_dialog.h"
#include "gui/preferences_apply_policy.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/video_recorder.h"
#include "core/sna_saver.h"
#include "core/szx_saver.h"
#include "core/nex_saver.h"
#include "core/log.h"
#include "platform/screenshot.h"
#include "peripheral/esp_host_policy.h"
#include "input/mouse_dispatcher.h"
#include "platform/pointer_capture.h"
#include "platform/speed_report.h"
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
#include <QFileInfo>

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

        // Task 77 — host keys for the Spectrum compound keys wired in
        // Keyboard::init_map(). Without these the mappings are unreachable
        // from the Qt GUI (this table is the only route to set_key()).
        case Qt::Key_Tab:        return SDL_SCANCODE_TAB;         // EXTEND MODE
        case Qt::Key_Backtab:    return SDL_SCANCODE_TAB;         // Shift+Tab
        case Qt::Key_QuoteLeft:  return SDL_SCANCODE_GRAVE;       // TRUE/INV VIDEO
        case Qt::Key_Apostrophe: return SDL_SCANCODE_APOSTROPHE;  // '"' (SS+P)
        case Qt::Key_Semicolon:  return SDL_SCANCODE_SEMICOLON;   // ';' (SS+O)
        case Qt::Key_Period:     return SDL_SCANCODE_PERIOD;      // '.' (SS+M)
        case Qt::Key_Comma:      return SDL_SCANCODE_COMMA;       // ',' (SS+N)

        // Issue #115 — host keys the Next's own PS/2 keymap binds but which
        // this table dropped, so they never reached Keyboard::set_key() from
        // the GUI at all. Matrix derivation is in Keyboard::init_map().
        case Qt::Key_CapsLock:   return SDL_SCANCODE_CAPSLOCK;    // CAPS LOCK
        case Qt::Key_Backslash:  return SDL_SCANCODE_BACKSLASH;   // INV VIDEO
        case Qt::Key_Slash:      return SDL_SCANCODE_SLASH;       // '/' (SS+V)
        case Qt::Key_Minus:      return SDL_SCANCODE_MINUS;       // '-' (SS+J)
        case Qt::Key_Equal:      return SDL_SCANCODE_EQUALS;      // '=' (SS+L)

        // Issue #123 — the SHIFTED form of every key above.
        //
        // QKeyEvent::key() is the LOGICAL key, so Shift+';' arrives as
        // Qt::Key_Colon and Shift+'2' as Qt::Key_At — neither of which this
        // table held, so the keystroke was dropped here and Keyboard never saw
        // it. That cost the guest 19 keys, including Caps Shift + 1..0, i.e.
        // EDIT, CAPS LOCK, TRUE/INV VIDEO, the cursor compounds, GRAPH and
        // DELETE as typed on a 48K.
        //
        // The real machine has no such problem because its PS/2 keymap is
        // indexed by PHYSICAL scancode and the shift keys are an independent
        // overlay: LShift/RShift only raise capshift_count (keymaps.vhd:83,
        // ps2_keyb.vhd:196-198), they do not select a different keymap entry.
        // SDL scancodes mean the same thing, so each shifted logical key below
        // resolves to the SAME scancode as its unshifted sibling and the host
        // Shift key asserts Caps Shift on its own — exactly the hardware
        // behaviour.
        //
        // The pairings are MEASURED, not inferred: real X11 key events through
        // Qt 6.11 on a US layout report each pair below with an identical
        // QKeyEvent::nativeScanCode() on both members.
        //
        // KNOWN LIMIT, stated so a reader does not over-read this: these are
        // the ASCII shifted glyphs of a US/UK layout. A layout whose glyph is
        // outside that set — Spanish 'ñ' on the ';' key, or the ISO key left
        // of Z that types '<'/'>' — still produces a logical key with no entry
        // here and is still dropped. Fixing THAT needs
        // QKeyEvent::nativeScanCode() plus a per-platform physical-key table
        // (evdev / Windows scancode / macOS virtual key); recorded in
        // doc/design/TASK-115-HOST-SHORTCUT-INVENTORY.md §10.
        case Qt::Key_Exclam:      return SDL_SCANCODE_1;          // Shift+1 '!'
        case Qt::Key_At:          return SDL_SCANCODE_2;          // Shift+2 '@'
        case Qt::Key_NumberSign:  return SDL_SCANCODE_3;          // Shift+3 '#'
        case Qt::Key_Dollar:      return SDL_SCANCODE_4;          // Shift+4 '$'
        case Qt::Key_Percent:     return SDL_SCANCODE_5;          // Shift+5 '%'
        case Qt::Key_AsciiCircum: return SDL_SCANCODE_6;          // Shift+6 '^'
        case Qt::Key_Ampersand:   return SDL_SCANCODE_7;          // Shift+7 '&'
        case Qt::Key_Asterisk:    return SDL_SCANCODE_8;          // Shift+8 '*'
        case Qt::Key_ParenLeft:   return SDL_SCANCODE_9;          // Shift+9 '('
        case Qt::Key_ParenRight:  return SDL_SCANCODE_0;          // Shift+0 ')'
        case Qt::Key_Underscore:  return SDL_SCANCODE_MINUS;      // Shift+- '_'
        case Qt::Key_Plus:        return SDL_SCANCODE_EQUALS;     // Shift+= '+'
        case Qt::Key_Bar:         return SDL_SCANCODE_BACKSLASH;  // Shift+\ '|'
        case Qt::Key_Colon:       return SDL_SCANCODE_SEMICOLON;  // Shift+; ':'
        case Qt::Key_QuoteDbl:    return SDL_SCANCODE_APOSTROPHE; // Shift+' '"'
        case Qt::Key_AsciiTilde:  return SDL_SCANCODE_GRAVE;      // Shift+` '~'
        case Qt::Key_Less:        return SDL_SCANCODE_COMMA;      // Shift+, '<'
        case Qt::Key_Greater:     return SDL_SCANCODE_PERIOD;     // Shift+. '>'
        case Qt::Key_Question:    return SDL_SCANCODE_SLASH;      // Shift+/ '?'

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
    base_window_title_ = windowTitle();

    // Task 66 \u2014 load persisted GUI preferences. This is the production
    // AppConfig ctor (~/.jnext/jnext.conf); MainWindow is only ever
    // constructed by the real (non-headless) GUI path (QtApp::init()), so
    // headless/unit-test runs never touch the user's real config file. The
    // regression suite additionally isolates $JNEXT_CONFIG_DIR so its handful
    // of non-headless invocations stay deterministic (test/00regression/
    // regression.sh).
    app_config_.load();

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
        // The host cursor is hidden only while captured (set_mouse_captured),
        // where it would otherwise double up with the emulator-rendered
        // Kempston cursor. Uncaptured it stays visible: the pointer belongs to
        // the desktop then, and you need to see it to aim at the viewport to
        // click and capture.
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
        // Task 60c — after a rewind / save-load restore, the mouse dispatcher's
        // cumulative shadow (wheel/button) must be re-seeded from the restored
        // KempstonMouse. Task 79 added the SDL gamepad host on the same restore
        // hook, so QtApp now owns emu->on_input_state_restored and fans it out
        // to BOTH (gamepad host + this window's mouse via
        // resync_input_dispatchers()). See QtApp::wire_gamepad_and_sources().
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

void MainWindow::resync_input_dispatchers() {
    if (mouse_dispatcher_) mouse_dispatcher_->resync();
}

void MainWindow::on_joy_source_selected(int connector, JoySource src) {
    if (!emulator_) return;
    // set_joystick_source enforces the one-cursor-connector rule (it may flip
    // the other connector back to SDL), so read the effective state back for
    // both the menu checkmarks and the persisted config.
    emulator_->set_joystick_source(connector, src);
    sync_joy_source_menu();
    app_config_.data().joy_source[0] = emulator_->joystick_source(0);
    app_config_.data().joy_source[1] = emulator_->joystick_source(1);
    app_config_.save();
}

void MainWindow::sync_joy_source_menu() {
    if (!emulator_) return;
    for (int conn = 0; conn < 2; ++conn) {
        const JoySource s = emulator_->joystick_source(conn);
        const int idx = (s == JoySource::CursorKeys) ? 1 : 0;
        if (joy_source_action_[conn][idx])
            joy_source_action_[conn][idx]->setChecked(true);
    }
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
    // -----------------------------------------------------------------------
    // Issue #115 part 2 — host hotkeys live on Alt, never on Ctrl.
    //
    // Ctrl IS the guest's Symbol Shift (keymaps.vhd:84; jnext binds LCTRL/RCTRL
    // to matrix {7,1} in keyboard.cpp:124-125), so every Ctrl+<letter> host
    // shortcut silently ate a Symbol Shift sequence NextBASIC needs constantly:
    // SS+O is ';', SS+D is STEP, SS+R is '<', SS+T is '>', SS+Q is '<=',
    // SS+S is '|'. Qt's shortcut map outranks keyPressEvent, so the guest never
    // saw them at all. All six moved to Alt+<letter>.
    //
    // Alt is therefore the HOST namespace and Ctrl is the GUEST's. That is a
    // deliberate divergence from real hardware, which maps Left Alt to EXTEND
    // MODE and Right Alt to GRAPH (keymaps.vhd:85-94): jnext follows the
    // FUSE/ZEsarUX convention instead (EXTEND on Tab, keyboard.cpp:154) and
    // gives up ever binding the Alt keys as guest shift keys. Deliberate, not
    // an oversight — see doc/design/TASK-115-HOST-SHORTCUT-INVENTORY.md §8.
    //
    // Consequence for anyone adding a menu or a shortcut here: Alt+<letter> is
    // ONE namespace shared by the QAction shortcuts below AND by the top-level
    // menubar mnemonics ('&' in addMenu). A letter used twice is an AMBIGUOUS
    // Qt shortcut — QAction::event() only prints a warning and does nothing, so
    // BOTH bindings break. host_hotkey_test pins the two sets disjoint.
    // Reserved today: Alt+Q/O/S/R/T/D (shortcuts) and Alt+F/M/I/A/B/V/N/H
    // (mnemonics). Alt+E/G/C/` stay free because the GUEST uses them
    // (keyboard.cpp:163,172-174).
    // -----------------------------------------------------------------------

    // --- File menu ---
    QMenu* file_menu = menuBar()->addMenu(tr("&File"));

    QAction* load_nex = file_menu->addAction(tr("Load &NEX File..."));
    load_nex->setShortcut(QKeySequence(Qt::ALT | Qt::Key_O));
    connect(load_nex, &QAction::triggered, this, &MainWindow::on_load_nex);

    QAction* mount_sd = file_menu->addAction(tr("&Mount SD Card Image..."));
    connect(mount_sd, &QAction::triggered, this, &MainWindow::on_mount_sd);

    file_menu->addSeparator();

    record_start_action_ = file_menu->addAction(tr("Record &MPEG4 Video..."));
    record_start_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
    connect(record_start_action_, &QAction::triggered, this, &MainWindow::on_record_start);
    // Packaging Task 67 follow-up: grey the option out entirely when ffmpeg
    // is missing, instead of only failing after the user picks a save path
    // (on_record_start's existing check stays as a fallback in case ffmpeg
    // is installed/removed while jnext is running).
    if (!VideoRecorder::ffmpeg_available()) {
        record_start_action_->setEnabled(false);
        // Deliberately platform-neutral: jnext ships on Linux, macOS and
        // Windows, and this text used to name dnf and apt only — a macOS user
        // was told to run a Fedora command.
        record_start_action_->setToolTip(tr("FFmpeg is required for video recording but was not found in PATH.\n"
                                             "Install FFmpeg, make sure it is on your PATH, and restart jnext.\n"
                                             "See https://ffmpeg.org/download.html"));
        record_start_action_->setStatusTip(record_start_action_->toolTip());
    }

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
    screenshot_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_S));
    connect(screenshot_action_, &QAction::triggered, this, [this]() {
        if (!emulator_) return;
        QString path = QFileDialog::getSaveFileName(
            this, tr("Save Screenshot"), app_config_.data().screenshot_dir,
            tr("PNG Images (*.png);;All Files (*)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(".png", Qt::CaseInsensitive))
            path += ".png";
        // Task 66 — remember the containing directory for the next dialog.
        app_config_.data().screenshot_dir = QFileInfo(path).absolutePath();
        app_config_.save();
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
    // BOOT-SNAPSAVE-01 + BOOT-SNAPSAVE-04). Shift+S keeps the standard
    // "Save As" muscle memory and sits next to Alt+S for the screenshot.
    //
    // Issue #130 — this was Ctrl+Shift+S, the last residual of the #115
    // migration (which moved only the six PLAIN Ctrl+<letter> chords). Ctrl is
    // the guest's Symbol Shift and Shift is its Caps Shift, so Ctrl+Shift+S
    // was a legitimate guest keystroke — CS+SS+S — that the Qt shortcut map
    // swallowed before keyPressEvent ever ran. Moved onto Alt, where every
    // other host chord lives.
    //
    // Alt+Shift+S collides with nothing: the live host set is Alt+F/M/I/A/B/V/
    // N/H (menubar mnemonics, all PLAIN Alt+letter) plus Alt+O/S/Q/R/T/D,
    // Ctrl+F5, Ctrl+F6, F4, F11 and Preferences. Its modifier set differs from
    // Alt+S's, so Qt treats the two as distinct sequences, not as one
    // ambiguous binding — pinned by host_hotkey_test H115-33.
    QAction* save_snapshot = file_menu->addAction(tr("Save S&napshot..."));
    save_snapshot->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_S));
    connect(save_snapshot, &QAction::triggered, this, &MainWindow::on_save_snapshot);

    file_menu->addSeparator();

    QAction* quit = file_menu->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Q));
    // Issue #131 — go through close() so closeEvent() actually RUNS. This used
    // to connect straight to QApplication::quit(), which skips closeEvent
    // entirely, so File > Quit and closing the window with [X] did different
    // things: one action, two behaviours, decided only by how it was invoked.
    //
    // WHAT WAS ACTUALLY LOST, measured rather than assumed. The debugger
    // teardown was skipped, and with it DebuggerWindow::save_geometry() — which
    // runs from DebuggerManager::set_enabled(false) (debugger_manager.cpp:153)
    // and from DebuggerWindow::closeEvent (debugger_window.cpp:196), and from
    // nowhere else that a quit reaches. A user who resized or moved the
    // debugger window and then chose File > Quit lost that layout, while [X]
    // kept it.
    //
    // The RECORDING was NOT in fact lost, and this comment says so rather than
    // repeating the issue's guess: main.cpp:870-873 stops any still-active
    // recording once run() returns, and ~VideoRecorder() (video_recorder.cpp:
    // 32-35) stops it again if anything ever got past that. Verified
    // end-to-end on the pre-fix binary: Alt+Q during --record still produced a
    // valid MP4. closeEvent's stop_recording() is the first of three, and
    // making Quit run it restores the [X] ordering; it does not rescue a file
    // that was being dropped.
    //
    // close() returns false only if closeEvent() ignored the event.
    // MainWindow::closeEvent() never does — it contains no ignore() call — and
    // it cannot put a question to the user either: stop_recording() shows no
    // dialog, and the debugger teardown deliberately passes
    // prompt_on_corrupt=false (Task 60f, pinned by debugger_quit_gate_test
    // QG-01/QG-04). So Quit always quits today. The guard exists so that IF a
    // veto is ever added — an "unsaved changes?" prompt, say — declining it
    // cancels the quit instead of the app dying anyway. It can BLOCK, briefly:
    // stopping a recording runs the ffmpeg mux synchronously. That is the same
    // wait the [X] path has always had, and it finishes.
    //
    // qApp->quit() stays explicit rather than being left to
    // quitOnLastWindowClosed: that fallback depends on no OTHER top-level
    // window being open, which is a property of the whole application, not of
    // this action.
    connect(quit, &QAction::triggered, this, [this]() {
        if (close()) qApp->quit();
    });

    // --- Machine menu ---
    QMenu* machine_menu = menuBar()->addMenu(tr("&Machine"));

    // Issue #45 — two distinct reset controls, mirroring real Next hardware:
    // Power Reset = power off/on cold boot (full boot chain), Soft Reset =
    // the front-panel reset button (back to NextZXOS, no firmware reload).
    // R deliberately stays on Power Reset (it has always meant the cold boot),
    // now as Alt+R per #115; F4 matches the host soft-reset hotkey
    // (zxnext.vhd:6370).
    QAction* reset = machine_menu->addAction(tr("&Power Reset"));
    reset->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
    connect(reset, &QAction::triggered, this, &MainWindow::on_reset);

    QAction* soft_reset = machine_menu->addAction(tr("&Soft Reset"));
    soft_reset->setShortcut(QKeySequence(Qt::Key_F4));
    connect(soft_reset, &QAction::triggered, this, &MainWindow::on_soft_reset);

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

    // --- Input menu (Task 79) ---
    // Per-connector host input source. Each Next joystick connector can be
    // driven by an autodetected SDL gamepad or by the host cursor keys + Space
    // (only one connector may use the cursor keys at a time). Changes apply
    // live and persist to ~/.jnext/jnext.conf.
    QMenu* input_menu = menuBar()->addMenu(tr("&Input"));
    struct JoyEntry { const char* label; JoySource src; };
    const JoyEntry joy_entries[2] = {
        { "&SDL Gamepad",        JoySource::Sdl },
        { "Cursor &Keys + Space", JoySource::CursorKeys },
    };
    for (int conn = 0; conn < 2; ++conn) {
        QMenu* sub = input_menu->addMenu(conn == 0 ? tr("Joy &1 Source (port 0x1F)")
                                                   : tr("Joy &2 Source (port 0x37)"));
        auto* group = new QActionGroup(this);
        group->setExclusive(true);
        for (int e = 0; e < 2; ++e) {
            QAction* a = sub->addAction(tr(joy_entries[e].label));
            a->setCheckable(true);
            group->addAction(a);
            joy_source_action_[conn][e] = a;
            const JoySource src = joy_entries[e].src;
            connect(a, &QAction::triggered, this, [this, conn, src](bool checked) {
                if (checked) on_joy_source_selected(conn, src);
            });
        }
    }
    // Default state before set_emulator() syncs from the effective sources.
    joy_source_action_[0][0]->setChecked(true);
    joy_source_action_[1][0]->setChecked(true);

    // Kempston mouse pointer capture (issue #37). Off by default: capturing
    // uninvited would take the pointer away from a user who only wanted to
    // reach the menus. Clicking the viewport also captures.
    input_menu->addSeparator();
    capture_mouse_action_ = input_menu->addAction(tr("Capture &Mouse"));
    capture_mouse_action_->setCheckable(true);
    // Deliberately NO keyboard shortcut. Every plain Ctrl+<key> is a real ZX
    // sequence — Ctrl maps to Symbol Shift (issue #115), so Ctrl+M IS SS+M —
    // and a host shortcut would swallow it before the guest ever saw it.
    // Capture is by clicking the viewport (or this menu item); Ctrl+Alt
    // releases.
    capture_mouse_action_->setStatusTip(
        tr("Confine the host pointer so the Kempston mouse can move freely "
           "(click the screen to capture, Ctrl+Alt to release)"));
    connect(capture_mouse_action_, &QAction::triggered, this,
            [this](bool on) { set_mouse_captured(on); });

    // --- Tape menu ---
    // Mnemonic is Alt+A ("T&ape"), NOT Alt+T: Alt+T is the Open Tape File
    // shortcut below, and one letter cannot serve both (#115 — an ambiguous
    // Qt shortcut activates neither).
    QMenu* tape_menu = menuBar()->addMenu(tr("T&ape"));

    QAction* tape_open = tape_menu->addAction(tr("&Open Tape File..."));
    tape_open->setShortcut(QKeySequence(Qt::ALT | Qt::Key_T));
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
    // Mnemonic is Alt+B ("De&bug"), NOT Alt+D: Alt+D is the View > Debugger
    // shortcut (#115). Alt+D was ALSO the reason keyboard.cpp could not give
    // the guest Alt+D — that constraint is unchanged, only its owner moved.
    QMenu* debug_menu = menuBar()->addMenu(tr("De&bug"));

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
    // F11 toggles fullscreen, unconditionally — the debugger's step shortcuts
    // are F6/F7/F8, so F11 is never intercepted. This matters since Task 77:
    // Esc became the ZX BREAK key, making F11 the ONLY way out of fullscreen.
    // (A previous comment here claimed F11 was taken by Step Into and that
    // fullscreen moved to Ctrl+F11; neither was true of the code.)
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
    debugger_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_D));
    debugger_action_->setCheckable(true);
#endif

    // --- Settings menu (Task 66) ---
    // Mnemonic is Alt+N ("Setti&ngs"), NOT Alt+S: Alt+S is the Save
    // Screenshot shortcut (#115). E/G/C are also off-limits — the guest owns
    // Alt+E/G/C (keyboard.cpp:163,172-174) — which leaves N.
    QMenu* settings_menu = menuBar()->addMenu(tr("Setti&ngs"));

    QAction* preferences = settings_menu->addAction(tr("&Preferences..."));
    preferences->setShortcut(QKeySequence::Preferences);
    connect(preferences, &QAction::triggered, this, &MainWindow::on_open_preferences);

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
        style()->standardIcon(QStyle::SP_BrowserReload), tr("Power Reset"));
    connect(reset_btn, &QAction::triggered, this, &MainWindow::on_reset);

    QAction* soft_reset_btn = toolbar->addAction(
        style()->standardIcon(QStyle::SP_DialogResetButton), tr("Soft Reset"));
    connect(soft_reset_btn, &QAction::triggered, this, &MainWindow::on_soft_reset);

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
    fps_label_ = new QLabel(tr("FPS: -- emu / -- shown"));
    fps_label_->setMinimumWidth(190);
    // Task 63 (issue #9) — the two numbers mean different things and the
    // status bar used to show only the first, labelled ambiguously "FPS".
    fps_label_->setToolTip(tr(
        "emu:   emulated frames per second (how fast the machine is simulated)\n"
        "shown: frames per second that actually reached the screen\n\n"
        "'shown' below 'emu' means frames are not being presented — deliberately "
        "(audio catch-up, speed > 1x) or otherwise. Run with "
        "--log-level platform=info for the full cadence breakdown."));

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

    // GH #25 — hidden until the emulator reports an ESP, so the status bar is
    // byte-identical for every run that has not enabled one (the default).
    esp_label_ = new QLabel(tr("ESP: on"));
    esp_label_->setMinimumWidth(150);
    esp_label_->setAlignment(Qt::AlignCenter);
    esp_label_->setVisible(false);

    statusBar()->addWidget(fps_label_, 1);
    statusBar()->addWidget(speed_label_, 1);
    statusBar()->addWidget(emu_speed_label_, 1);
    statusBar()->addWidget(tape_label_, 1);
    statusBar()->addWidget(esp_label_, 1);
    statusBar()->addPermanentWidget(machine_label_);
}

// ---------------------------------------------------------------------------
// Status update
// ---------------------------------------------------------------------------

void MainWindow::update_status(double fps, double presented_fps,
                               int cpu_speed_idx, double emu_speed,
                               double frame_period_ms, double measured_fps) {
    // Task 63 (issue #9) — show BOTH figures, each labelled. Reporting only
    // the emulated rate under the bare name "FPS" is what made every user
    // report of judder ("but it says 59 fps") impossible to interpret.
    if (fps_label_)
        fps_label_->setText(QString("FPS: %1 emu / %2 shown")
                                .arg(fps, 0, 'f', 1)
                                .arg(presented_fps, 0, 'f', 1));

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

    // Show the emulator speed. Issue #120 — this cell used to render the
    // REQUESTED multiplier and nothing else, so it read "100%" on a host
    // managing 30 fps just as it did on one keeping up perfectly. A reporter
    // seeing "100%" beside a low frame rate reasonably concluded the two
    // instruments disagreed; in truth only one of them measured anything.
    //
    // Now the requested figure is still what the cell normally shows (it is the
    // setting, and on a healthy host it is also the truth), but a GENUINE
    // shortfall is appended in parentheses. See speed_report.h for why the
    // achieved figure is a percentage of the MACHINE'S OWN refresh — the Next's
    // "50 Hz" is really 49.36 Hz, so a healthy jnext shows "FPS: 49.4" and must
    // still score 100%.
    const speed_report::Report sr = speed_report::summarize(
        measured_fps >= 0.0 ? measured_fps : fps, frame_period_ms, emu_speed);
    if (emu_speed_label_) {
        emu_speed_label_->setText(
            sr.shortfall ? QString("%1% (%2%)").arg(sr.requested_pct).arg(sr.achieved_pct)
                         : QString("%1%").arg(sr.requested_pct));
        QString tip = tr("Requested emulation speed: %1%").arg(sr.requested_pct);
        if (frame_period_ms > 0.0) {
            tip += tr("\n100%% = %1 fps (this machine's true refresh rate)")
                       .arg(1000.0 / frame_period_ms, 0, 'f', 2);
        }
        if (sr.achieved_valid) {
            tip += tr("\nActually achieved: %1%").arg(sr.achieved_pct);
        }
        if (sr.shortfall) {
            tip += tr("\n\nThe host is not keeping up. The figure in brackets is "
                      "what it managed.");
        }
        emu_speed_label_->setToolTip(tip);
    }

    // Update tape status display
    update_tape_status();

    // GH #25 — ESP connection visibility.
    update_esp_status();
}

// ---------------------------------------------------------------------------
// Menu action handlers
// ---------------------------------------------------------------------------

void MainWindow::on_load_nex() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Load Program"), app_config_.data().last_load_dir,
        tr("Spectrum Files (*.nex *.sna *.szx *.z80 *.tap *.tzx *.wav *.rzx);;NEX Files (*.nex);;SNA Snapshots (*.sna);;SZX Snapshots (*.szx);;Z80 Snapshots (*.z80);;TAP Files (*.tap);;TZX Files (*.tzx);;WAV Files (*.wav);;RZX Recordings (*.rzx);;All Files (*)"));
    if (!path.isEmpty()) {
        // Task 66 — remember the containing directory for the next dialog.
        app_config_.data().last_load_dir = QFileInfo(path).absolutePath();
        app_config_.save();
        // Task 70 — a menu file-load is a full cold boot as if the file had
        // been passed with --load at startup (reset the Next to a known state,
        // then load and run). QtApp performs the reconstruct+init+load; the
        // per-format loader dispatch lives in the shared startup path.
        if (load_file_callback_) {
            load_file_callback_(path.toStdString());
        }
        emit load_nex_requested(path);
    }
}

void MainWindow::on_mount_sd() {
    QString start_dir = QFileInfo(app_config_.data().sd_card_path).absolutePath();
    QString path = QFileDialog::getOpenFileName(
        this, tr("Mount SD Card Image"), start_dir,
        tr("Disk Images (*.img *.bin);;All Files (*)"));
    if (!path.isEmpty()) {
        // SD card mounting requires restart; just store and inform user.
        // Task 66 — also remember it as the default --sdcard for future
        // launches that don't pass --sdcard explicitly.
        app_config_.data().sd_card_path = path;
        app_config_.save();
        QMessageBox::information(this, tr("SD Card"),
            tr("SD card image selected:\n%1\n\n"
               "Restart the emulator with --sdcard to use this image "
               "(or just restart — it is now the default).").arg(path));
        emit sd_card_selected(path);
    }
}

void MainWindow::on_reset() {
    // Task 70 — the Reset button/menu models the physical reset button: a
    // power-on cold boot. It cannot run inside run_frame(), so request it and
    // let QtApp perform the reconstruct+init between frames (same as F1 and a
    // program's own NR 0x02 hard reset).
    if (emulator_) {
        emulator_->request_hard_reset();
    }
}

void MainWindow::on_soft_reset() {
    // Issue #45 — Soft Reset = the front-panel soft reset, same dispatcher as
    // the host F4 hotkey. Honours the VHDL config-mode gate (no-op while the
    // firmware holds config mode, zxnext.vhd:6370). Also a no-op after a
    // direct --load / File > Load: the firmware never ran there, so config
    // mode is still held — matching hardware, where that state cannot exist.
    if (emulator_) {
        emulator_->on_hotkey_f4_soft_reset();
    }
}

void MainWindow::on_machine_type(MachineType type) {
    if (!emulator_) return;

    // Issue #40 — a machine type that is already running needs nothing done to
    // it. This used to reinit unconditionally, which is what let an unrelated
    // Preferences Apply nuke the machine.
    //
    // DELIBERATE UX CHANGE, slightly beyond the issue's literal ask: this also
    // makes the Machine menu a no-op when you pick the machine you are already
    // running (it used to reboot). Chosen over special-casing the menu because
    // "select 48K while running 48K" is not a request to restart — Machine >
    // Power Reset (and F1) is, and it remains the way to do it. Keeping the
    // reboot here would also mean the menu and Preferences disagree about what
    // selecting an unchanged machine type means. Pinned by PA-05/PA-06.
    if (!preferences_restart_required(emulator_->config().type, type)) {
        machine_label_->setText(tr(machine_type_str(type)));
        return;
    }

    // A real change is a power cycle, and it goes through the frontend's ONE
    // canonical cold-boot path (reconstruct + init + re-wire the host), not a
    // bare Emulator::init() from here.
    if (!reboot_callback_) {
        Log::platform()->error(
            "Machine type change to {} ignored: no reboot callback installed "
            "(the frontend owns the cold-boot path).", machine_type_str(type));
        return;
    }
    reboot_callback_(type);
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
        this, tr("Open Tape File"), app_config_.data().last_load_dir,
        tr("Tape Files (*.tap *.tzx *.wav);;TAP Files (*.tap);;TZX Files (*.tzx);;WAV Files (*.wav);;All Files (*)"));
    if (path.isEmpty() || !emulator_) return;

    app_config_.data().last_load_dir = QFileInfo(path).absolutePath();
    app_config_.save();

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

// GH #25 — see the declaration in main_window.h for why this is a persistent
// status cell and not a transient message or a dialog.
void MainWindow::update_esp_status() {
    if (!esp_label_) return;

    const EspConnectionLog* log = emulator_ ? emulator_->esp_events() : nullptr;

    // A null log means the ESP is disabled — which is the default, and every
    // run before this branch existed. The cell then does not exist at all, so
    // its PRESENCE is itself the "the guest can reach the network" indicator.
    const bool want_visible = (log != nullptr);
    if (want_visible != esp_cell_visible_) {
        esp_cell_visible_ = want_visible;
        esp_label_->setVisible(want_visible);
    }
    if (!want_visible) {
        esp_seen_id_ = 0;
        return;
    }

    // The change test is (instance id, sequence), NOT the sequence alone. A
    // cold boot with the ESP still enabled builds a FRESH log whose sequence
    // restarts at 0, and the replacement routinely lands on the freed block —
    // so neither the address nor the sequence on its own distinguishes "3 new
    // events" from "the 3 events I already rendered", and the cell would keep
    // showing pre-reboot text. The pair is strictly increasing across a
    // rebuild, so there is no coincidence left to hit.
    const std::uint64_t id  = log->instance_id();
    const std::uint64_t seq = log->sequence();
    if (id == esp_seen_id_ && seq == esp_seen_seq_) return;   // nothing new
    esp_seen_id_  = id;
    esp_seen_seq_ = seq;

    if (seq == 0) {
        // Enabled, nothing attempted yet. Naming the restriction here is the
        // whole point: "idle" alone would not tell the user whether their
        // allowlist actually loaded.
        const auto& allow = emulator_->esp_allowed_hosts();
        esp_label_->setText(allow.empty() ? tr("ESP: on (any host)")
                                          : tr("ESP: on (%1 allowed)").arg(allow.size()));
        esp_label_->setStyleSheet(QString());
        QString tip = tr("The emulated ESP-01 WiFi module is enabled: the running "
                         "program can open outbound TCP connections.\n"
                         "Loopback, link-local and cloud-metadata addresses are "
                         "always refused.\n");
        if (allow.empty()) {
            tip += tr("No --esp-allow given, so any other host may be named.");
        } else {
            tip += tr("Allowed hosts:");
            for (const std::string& host : allow)
                tip += QStringLiteral("\n  ") + QString::fromStdString(host);
        }
        esp_label_->setToolTip(tip);
        return;
    }

    const std::vector<EspEvent> events = log->snapshot();
    if (events.empty()) return;

    // The cell shows the LATEST event and keeps showing it. A refusal or a
    // transport fault is red and stays red until something else happens, so a
    // user who looks away does not lose it.
    const EspEvent& last = events.back();
    QString target = QString::fromStdString(last.host);
    if (last.port != 0) target += QStringLiteral(":") + QString::number(last.port);

    QString text;
    bool    alarming = false;
    switch (last.kind) {
        case EspEvent::Kind::Refused:
            text = tr("ESP: REFUSED %1").arg(target);
            alarming = true;
            break;
        case EspEvent::Kind::Opened:
            text = tr("ESP: %1").arg(target);
            break;
        case EspEvent::Kind::Failed:
            text = tr("ESP: failed %1").arg(target);
            break;
        case EspEvent::Kind::Closed:
            text = tr("ESP: closed %1").arg(target);
            break;
        case EspEvent::Kind::TransportFault:
            text = tr("ESP: FAULT");
            alarming = true;
            break;
    }
    esp_label_->setText(text);
    esp_label_->setStyleSheet(alarming ? QStringLiteral("color: red; font-weight: bold;")
                                       : QString());

    // The history goes in the tooltip: the cell can only show one event, and
    // "it flashed something red a minute ago" has to remain answerable.
    QString tip = tr("Emulated ESP-01 connection history (most recent last):");
    for (const EspEvent& e : events) {
        tip += QStringLiteral("\n  ") + QString::fromLatin1(esp_event_text(e.kind));
        if (!e.host.empty()) {
            tip += QStringLiteral(" ") + QString::fromStdString(e.host);
            if (e.port != 0) tip += QStringLiteral(":") + QString::number(e.port);
        }
        if (!e.detail.empty())
            tip += QStringLiteral(" (") + QString::fromStdString(e.detail) + QStringLiteral(")");
    }
    esp_label_->setToolTip(tip);
}

// ---------------------------------------------------------------------------
// Recording handlers
// ---------------------------------------------------------------------------

void MainWindow::on_record_start() {
    if (!emulator_) return;

    if (!VideoRecorder::ffmpeg_available()) {
        QMessageBox::warning(this, tr("Recording Error"),
            tr("FFmpeg is not installed or not found in PATH.\n\n"
               "Video recording needs the ffmpeg command available on your "
               "PATH.\n\n"
               "Downloads and per-platform instructions:\n"
               "  https://ffmpeg.org/download.html"));
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
// Preferences (Task 66 — Configurability)
// ---------------------------------------------------------------------------

void MainWindow::apply_startup_config(const AppConfigData& cfg) {
    // Machine type and silent are deliberately NOT applied here: main.cpp
    // already merged them into the EmulatorConfig used for Emulator::init()
    // (CLI wins when given), so re-applying the raw AppConfigData value here
    // would re-open the machine-type/silent precedence bug — this method
    // only sees the persisted value, not whether the CLI overrode it.
    if (emulator_) {
        emulator_->nextreg().write(0x07, static_cast<uint8_t>(cfg.cpu_speed));
        emulator_->tape().set_fast_load(cfg.tape_fast_load);
        emulator_->tzx_tape().set_fast_load(cfg.tape_fast_load);
        emulator_->mixer().set_output_gain_db(cfg.audio_gain_db);
        emulator_->mixer().set_beeper_gain_db(cfg.audio_gain_beeper_db);
        for (int chip = 0; chip < 3; ++chip)
            emulator_->mixer().set_ay_gain_db(chip, cfg.audio_gain_ay_db[chip]);
        emulator_->mixer().set_dac_gain_db(cfg.audio_gain_dac_db);
    }
    if (tape_fast_action_) tape_fast_action_->setChecked(cfg.tape_fast_load);

    set_scale(cfg.window_scale);
    set_crt_filter(cfg.crt_filter);

    if (speed_callback_) speed_callback_(cfg.emulator_speed_percent / 100.0);

    // Issue #35 — applies live: the sequencer reads the policy on the next
    // tick and there is no schedule or band state to re-anchor, so nothing
    // here needs a restart or a reboot.
    if (when_slow_prefer_callback_) when_slow_prefer_callback_(cfg.when_slow_prefer);

    // Sync the CPU-speed / emulator-speed menu radio groups to match.
    if (speed_group_) {
        for (QAction* a : speed_group_->actions()) {
            if (a->data().toInt() == static_cast<int>(cfg.cpu_speed)) {
                a->setChecked(true);
                break;
            }
        }
    }
    if (emu_speed_group_) {
        bool matched = false;
        for (QAction* a : emu_speed_group_->actions()) {
            if (std::abs(a->data().toDouble() - cfg.emulator_speed_percent / 100.0) < 0.01) {
                a->setChecked(true);
                matched = true;
                break;
            }
        }
        if (!matched && emu_speed_group_->checkedAction())
            emu_speed_group_->checkedAction()->setChecked(false);
    }
}

bool MainWindow::confirm_machine_restart_dialog(MachineType requested) {
    const auto answer = QMessageBox::question(
        this, tr("Change Machine Type"),
        tr("Switching to %1 restarts the emulator — anything currently "
           "running will be lost.\n\nRestart now?\n\n"
           "(Choosing No keeps the machine running and applies the new "
           "machine type the next time jnext starts.)")
            .arg(tr(machine_type_str(requested))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

void MainWindow::apply_preferences(const AppConfigData& cfg) {
    // Issue #40 — "Apply" applies settings to the RUNNING machine. Everything
    // in this dialog can be pushed live except the machine type, which is a
    // power cycle. So: only act on the machine type when it actually differs
    // from what is running, and when it does, ask first — the user came here
    // to change a joypad source, not to lose what they were running.
    //
    // This is deliberately asymmetric with the Machine menu, which reboots
    // without asking: picking "48K" from a menu called Machine IS the explicit
    // request. Here the machine type is one field among many on a tab the user
    // may not even have touched, so a silent reboot is a surprise.
    //
    // Declining leaves the machine running and the new type persisted (the
    // dialog saved it before calling us), so it takes effect on next launch.
    const bool restart_required =
        emulator_ && preferences_restart_required(emulator_->config().type,
                                                  cfg.machine_type);
    bool confirmed = false;
    if (restart_required) {
        confirmed = confirm_restart_callback_
                        ? confirm_restart_callback_(cfg.machine_type)
                        : confirm_machine_restart_dialog(cfg.machine_type);
    }

    // GH #25 — the ESP settings go to the frontend BEFORE anything below can
    // reboot, and the order is load-bearing rather than incidental. The
    // callback writes the values into the EmulatorConfig that every cold boot
    // is built from; the machine-type branch below performs a cold boot. Doing
    // it the other way round means an Apply that changes the machine type AND
    // the ESP in one visit reboots on the OLD ESP config, so the change the
    // user just made is stranded until some later reset they were never told
    // about — while both the tab's own note and the man page promise the next
    // hard reset. Pinned by PA-14.
    //
    // Unlike every other setting here, this one has no live setter at all:
    // Emulator::setup_esp() builds the module once at init() and deliberately
    // keeps it across a soft reset, so there is nothing to toggle on a running
    // machine. All this can do is make the NEXT cold boot honour the change
    // instead of stranding it until the next launch.
    if (esp_config_callback_) esp_config_callback_(cfg.esp_enabled, cfg.esp_allowed_hosts);

    switch (preferences_apply_outcome(restart_required, confirmed)) {
    case PreferencesApplyOutcome::RebootThenApplyLive:
        on_machine_type(cfg.machine_type);
        break;
    case PreferencesApplyOutcome::DeferMachineType:
        Log::platform()->info(
            "Preferences: machine type {} saved but NOT applied (restart "
            "declined); it takes effect on next launch.",
            machine_type_str(cfg.machine_type));
        break;
    case PreferencesApplyOutcome::ApplyLiveOnly:
        break;
    }

    // Every other setting applies live and silently, in every outcome above —
    // declining the restart must not also discard the joypad change that the
    // user actually came here to make.
    apply_startup_config(cfg);

    // Task 79 — live-apply the per-connector input sources. The dialog already
    // enforces the one-cursor rule, so these two calls never conflict; sync the
    // Input-menu checkmarks to the (now effective) state.
    if (emulator_) {
        emulator_->set_joystick_source(0, cfg.joy_source[0]);
        emulator_->set_joystick_source(1, cfg.joy_source[1]);
        sync_joy_source_menu();
    }

    // GH #25 — the ESP forward itself happened above, before any reboot could.
    // What is left is telling the user, and this is deliberately AFTER the
    // switch so it reads the machine that is running NOW: if the branch above
    // did cold-boot, the change is already in effect and there is nothing to
    // report. A setting that has taken effect and one that is merely saved must
    // not look alike — the status-bar ESP cell tracks the RUNNING module, and
    // this line is what explains a cell that has not changed.
    if (emulator_ && emulator_->esp_enabled() != cfg.esp_enabled) {
        Log::platform()->info(
            "Preferences: ESP-01 {} saved but NOT applied to the running "
            "machine; it takes effect on the next Power Reset (F1) or launch.",
            cfg.esp_enabled ? "enabled" : "disabled");
    }

    // cfg.silent has no live setter (the SDL audio device is opened once at
    // QtApp::init() time and MainWindow has no handle to it) — persisted
    // only, applied on next launch. Host output gain is deliberately different:
    // apply_startup_config() updates the mixer immediately.
}

void MainWindow::on_open_preferences() {
    PreferencesDialog dlg(app_config_.data(), this);
    connect(&dlg, &PreferencesDialog::apply_requested, this, [this](const AppConfigData& cfg) {
        app_config_.data() = cfg;
        app_config_.save();
        apply_preferences(cfg);
    });
    dlg.exec();
}

// ---------------------------------------------------------------------------
// Key event handling
// ---------------------------------------------------------------------------

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        int key = event->key();
        Qt::KeyboardModifiers modifiers = event->modifiers();

        // Ctrl+Alt releases the pointer — the convention users already know
        // from VirtualBox / VMware / QEMU. Checked before anything else so it
        // works even if the guest is busy. Either order of the two keys
        // arrives here as "this one down, the other already held".
        if (mouse_captured_ &&
            ((key == Qt::Key_Alt     && (modifiers & Qt::ControlModifier)) ||
             (key == Qt::Key_Control && (modifiers & Qt::AltModifier)))) {
            set_mouse_captured(false);
            // Deliberately NOT consumed: fall through to normal key handling.
            // Keyboard::set_key() treats Alt as a host modifier (never a ZX
            // key) and swallowing it here would leave alt_held_ false, so the
            // Alt-compounds (EDIT = Alt+E, GRAPH = Alt+G, CAPS LOCK = Alt+C)
            // would silently resolve to their unmodified meaning until Alt was
            // released and pressed again. Ctrl was already forwarded on its own
            // key-down, so falling through changes nothing for it.
        }

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
        // Task 77 — Esc is the ZX BREAK key (Caps Shift + Space) and is NO
        // LONGER a fullscreen toggle; F11 above is the only one. It therefore
        // has no case here at all: it falls straight through to handle_key().
        case Qt::Key_F2:
            cycle_scale();
            event->accept();
            return;

        // Host hotkeys -> Emulator NMI / reset paths (G152). VHDL
        // zxnext.vhd:6340-6371: F1=hotkey_hard_reset, F4=hotkey_soft_reset,
        // F9=hotkey_m1 (Multiface), F10=hotkey_drive (DivMMC). Routed
        // through Emulator::on_hotkey_f*() dispatchers so the GUI never
        // touches NmiSource directly. Note (#45): plain F4 is normally
        // consumed by the Machine > Soft Reset QAction shortcut before it
        // reaches this handler (same situation as F11/fullscreen_action_),
        // so the F4 case below only sees modified presses (Shift/Ctrl+F4).
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
        if (key == Qt::Key_F11 || key == Qt::Key_F2) {
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

bool MainWindow::event(QEvent* ev) {
    // Task 77 — Tab is the host key for ZX EXTEND MODE (Caps Shift + Symbol
    // Shift), but QWidget::event() consumes Tab/Backtab for focus nav BEFORE
    // keyPressEvent() is ever reached, which would leave the mapping dead in
    // the GUI. Intercept it here and hand it to the normal key path.
    //
    // Nothing is lost: the main window's focus chain is the emulator viewport
    // alone, so there is no meaningful Tab traversal to preserve. Dialogs and
    // the debugger window are separate top-level widgets and are unaffected.
    const QEvent::Type t = ev->type();
    if (t == QEvent::KeyPress || t == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            handle_key(ke, t == QEvent::KeyPress);
            return true;
        }
    }
    return QMainWindow::event(ev);
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

// Qt5/Qt6 (GH #108 Phase A): QMouseEvent::globalPosition() is Qt6
// QSinglePointEvent API; Qt 5.15 spells the same screen-global point
// globalPos() (already a QPoint).
QPoint mouse_global_point(const QMouseEvent* e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->globalPosition().toPoint();
#else
    return e->globalPos();
#endif
}

} // anonymous namespace

QPoint MainWindow::viewport_centre_global() const {
    const QWidget* w = emulator_widget_ ? static_cast<const QWidget*>(emulator_widget_)
                                        : static_cast<const QWidget*>(this);
    return w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2));
}

void MainWindow::set_mouse_captured(bool on) {
    if (on == mouse_captured_) return;
    mouse_captured_ = on;

    if (on) {
        if (emulator_widget_) emulator_widget_->setCursor(Qt::BlankCursor);
        grabMouse(Qt::BlankCursor);
        // Park the pointer on the centre to measure from. Capture can start
        // with the pointer anywhere (menu item, or a click near an edge), and
        // a motion event queued at that old position may still be delivered
        // after the warp — as one enormous delta. Drop the first one.
        capture_policy_.begin();
        QCursor::setPos(viewport_centre_global());
        // The status-bar message times out; the title carries the way out for
        // as long as the pointer is actually held.
        setWindowTitle(base_window_title_ + tr(" - Ctrl+Alt to release mouse"));
        statusBar()->showMessage(tr("Mouse captured — Ctrl+Alt to release"), 4000);
    } else {
        releaseMouse();
        // Drop any latched button/wheel state. Buttons are only forwarded
        // while captured, so a button still held as capture ends would never
        // have its release delivered — the guest would see it pressed for
        // ever. Easy to hit: alt-tab mid-drag (that path auto-releases too).
        if (mouse_dispatcher_) mouse_dispatcher_->reset();
        if (emulator_widget_) emulator_widget_->unsetCursor();
        setWindowTitle(base_window_title_);
        statusBar()->showMessage(tr("Mouse released"), 2000);
    }
    if (capture_mouse_action_ && capture_mouse_action_->isChecked() != on) {
        capture_mouse_action_->setChecked(on);
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    // Uncaptured, the pointer belongs to the desktop: forward nothing, so
    // menus and window controls behave normally.
    if (!mouse_dispatcher_ || !mouse_captured_) {
        QMainWindow::mouseMoveEvent(event);
        return;
    }
    // A Kempston mouse reports RELATIVE motion, so forward the raw host delta
    // measured from the centre, then put the pointer back on the centre. The
    // host pointer therefore never reaches a screen edge and the guest can
    // travel without limit — the fix for issue #37, where motion was derived
    // from the pointer's absolute position inside the window and so was
    // bounded by the window and anchored to wherever the pointer entered.
    const QPoint centre = viewport_centre_global();
    const QPoint global = mouse_global_point(event);

    // Decision lives in pure code (platform/pointer_capture.h) so it is
    // reachable by tests; this handler is not.
    const pointer_capture::Motion m =
        capture_policy_.on_motion(global.x(), global.y(), centre.x(), centre.y());
    if (m.forward) mouse_dispatcher_->handle_motion(m.dx, m.dy);
    if (m.recentre) QCursor::setPos(centre);
    event->accept();
}

void MainWindow::changeEvent(QEvent* event) {
    // Never hold the pointer hostage: losing focus (alt-tab, another window)
    // hands it straight back to the desktop.
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        set_mouse_captured(false);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (!mouse_dispatcher_) {
        QMainWindow::mousePressEvent(event);
        return;
    }
    // Clicking the viewport is how you take the mouse without going to the
    // menu. That click arms the capture and is NOT delivered to the guest —
    // the user was aiming at a window, not at the program.
    if (!mouse_captured_) {
        const bool on_viewport =
            emulator_widget_ &&
            emulator_widget_->rect().contains(
                emulator_widget_->mapFromGlobal(mouse_global_point(event)));
        if (on_viewport) {
            set_mouse_captured(true);
            event->accept();
            return;
        }
        QMainWindow::mousePressEvent(event);
        return;
    }

    const uint8_t sdl_btn = qt_button_to_sdl(event->button());
    if (sdl_btn) {
        mouse_dispatcher_->handle_button(sdl_btn, true);
    }
    event->accept();
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (!mouse_dispatcher_ || !mouse_captured_) {
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
    if (!mouse_dispatcher_ || !mouse_captured_) {
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
    // Stop any active recording before closing. The result is not lost:
    // a failed stop is latched per output path (GH #86 — see
    // VideoRecorder::output_failed(), read by main.cpp after run()).
    if (emulator_ && emulator_->video_recorder().is_recording()) {
        emulator_->stop_recording();
    }

#ifdef ENABLE_DEBUGGER
    if (debugger_mgr_) {
        // Task 60f: the app is quitting — the whole machine is about to be
        // destroyed, so the Task 60e corruption resume-gate does NOT apply
        // here (quitting protects nothing, and a prompt that could be declined
        // would leave a hidden main window + an orphaned, still-open debugger
        // window that keeps lastWindowClosed from firing — the process would
        // never terminate). Disable the debugger WITHOUT the resume prompt
        // (prompt_on_corrupt=false → always succeeds) and close its window so
        // the last top-level window is gone and the app actually quits.
        //
        // Live-session gating is unchanged: F5/Run/Step/RunTo*, the Debug-menu
        // toggle and the debugger window [X] all still gate (prompt defaults
        // true) — only this quit path opts out.
        //
        // Issue #139 — THESE TWO STATEMENTS MUST STAY IN THIS ORDER. Closing
        // the debugger window emits window_closed(), whose slot is
        // set_enabled(false) with the DEFAULT prompt_on_corrupt=true
        // (debugger_manager.cpp:175-177), and DebuggerWindow::closeEvent
        // (debugger_window.cpp:185-198) calls event->ignore() if that disable
        // is declined. Disabling FIRST makes the slot early-return on
        // "already in the requested state" (debugger_manager.cpp:85-86), so no
        // prompt is possible and the close is always accepted. Swapped, a
        // corrupt machine gets the resume modal on the quit path and a vetoed
        // debugger-window close — the Task 60f hang. Pinned by
        // quit_cleanup_test Q139-01/Q139-02, which fail on the swap.
        debugger_mgr_->set_enabled(false, /*prompt_on_corrupt=*/false);
        if (auto* dbg_win = debugger_mgr_->debugger_window_ptr())
            dbg_win->close();
    }
#endif
    QMainWindow::closeEvent(event);
}
