#include "gui/main_window.h"
#include "version.h"
#include "gui/emulator_widget.h"
#include "gui/host_chords.h"
#include "gui/preferences_dialog.h"
#include "gui/preferences_apply_policy.h"
#include "gui/pok_dialog.h"           // GH #381
#include "gui/qt_ctrl_is_command.h"   // GH #365
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/video_recorder.h"
#include "core/sna_saver.h"
#include "core/szx_saver.h"
#include "core/nex_saver.h"
#include "core/nex_loader.h"          // GH #228 — probe_version + policy
#include "core/program_archive.h"
#include "platform/emulator_boot.h"   // GH #228 — emulator_load_routes_to_nex
#include "platform/pok_trainers.h"    // GH #381
#include "core/rzx.h"                 // rzx::playable, before a cold boot plays a file
#include "core/log.h"
#include <stdexcept>
#include "core/screenshot.h"
#include "core/quick_snapshot.h"
#include "core/jns_snapshot.h"
#include "peripheral/esp_host_policy.h"
#include "input/mouse_dispatcher.h"
#include "platform/pointer_capture.h"
#include "platform/window_title.h"      // GH #155 — the title names the version
#include "gui/mac_cursor_warp.h"      // GH #303 (macOS only)
#include "platform/speed_report.h"
#include "qt/menu_bar_alt_nav_qt.h"   // GH #268
#include "toolbar_icons/toolbar_icons.h"
#include "debug/debugger.h"            // GH #278 WP6 — the Magic Breakpoint item (CTL-14)
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "qt/debug_keymap_qt.h"
#endif
#include <ctime>

#include <QKeyEvent>
#include <QFocusEvent>
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
#include <QPushButton>
#include <QLabel>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QInputDialog>
#include <QTimer>
#include <QFileInfo>

// ---------------------------------------------------------------------------
// Qt::Key -> SDL_Scancode mapping
// ---------------------------------------------------------------------------

namespace {

QString release_hint_text(pointer_capture::ReleaseMethod m) {
    return QString::fromLatin1(pointer_capture::release_hint(m, kQtCtrlIsCommand));
}

pointer_capture::HostKey qt_host_key(int key) {
    switch (key) {
        case Qt::Key_Control: return pointer_capture::HostKey::Ctrl;
        case Qt::Key_Alt:     return pointer_capture::HostKey::Alt;
        case Qt::Key_Escape:  return pointer_capture::HostKey::Esc;
        default:              return pointer_capture::HostKey::Other;
    }
}

pointer_capture::HostButton qt_host_button(Qt::MouseButton b) {
    switch (b) {
        case Qt::LeftButton:   return pointer_capture::HostButton::Left;
        case Qt::MiddleButton: return pointer_capture::HostButton::Middle;
        case Qt::RightButton:  return pointer_capture::HostButton::Right;
        default:               return pointer_capture::HostButton::Other;
    }
}

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
    setWindowTitle(QString::fromUtf8(JNEXT_WINDOW_TITLE));
    base_window_title_ = windowTitle();

    // Task 66 \u2014 load persisted GUI preferences. This is the production
    // AppConfig ctor (~/.jnext/jnext.conf); MainWindow is only ever
    // constructed by the real (non-headless) GUI path (QtApp::init()), so
    // headless/unit-test runs never touch the user's real config file. The
    // regression suite additionally isolates $JNEXT_CONFIG_DIR so its handful
    // of non-headless invocations stay deterministic (test/00regression/
    // regression.sh).
    app_config_.load();
#ifdef ENABLE_DEBUGGER
    // GH #312 \u2014 fold an older Debugger.conf into jnext.conf once, even if
    // the debugger is never opened this run.
    DebuggerWindow::migrate_legacy_config();
#endif

    // GH #1 \u2014 every [debugger_keys] entry the config layer refused, said out
    // loud. A bad binding must never look like an accepted one: the action
    // falls back to its default (or to unbound, for a conflict) and the reason
    // is on the log AND at the top of Preferences > Debugger Keys.
    for (const auto& issue : app_config_.debug_key_issues()) {
        Log::platform()->error(
            "Config [debugger_keys] {}={}: {}",
            issue.action_id, issue.text, issue.reason);
    }
    debug_keys_ = app_config_.data().debug_keys;

    // Central widget: the emulator display (fixed-size, pixel-perfect).
    emulator_widget_ = new EmulatorWidget(this);
    setCentralWidget(emulator_widget_);

    // Build UI elements.
    create_menus();
    create_toolbar();
    create_statusbar();

    // GH #268 — a bare Alt tap must not hand the menu bar the keyboard.
    //
    // QMenuBar arms itself on the Alt SHORTCUT-OVERRIDE and, on the matching
    // Alt key-up with nothing pressed in between, calls setKeyboardMode(true)
    // -> setFocus(Qt::MenuBarFocusReason) (qmenubar.cpp:252,1463). That runs in
    // a qApp-level event filter, BEFORE this window's handlers and regardless
    // of whether they accept the event, so keyPressEvent cannot defend against
    // it: measured on the product, the focus-out lands between the Alt key-down
    // and the Alt key-up. The next letter that happens to be a top-level
    // mnemonic (F/M/I/A/B/V/N/H) is then eaten by the menu bar and opens that
    // popup, with nothing on screen having warned the user that focus had left
    // the emulator. That is the reported symptom.
    //
    // jnext is exposed to this far more than an ordinary application because
    // Alt is BOTH its host-shortcut namespace (see create_menus()) AND a guest
    // modifier — Alt+E is EDIT, Alt+G is GRAPH, Alt+C is CAPS LOCK
    // (keyboard.cpp:248-250) — so pressing Alt is an ordinary part of typing
    // here, and letting go of it without a letter is an ordinary slip.
    //
    // The mechanism, the Qt line numbers and the exact scope of the hint are
    // documented once, at the shared helper. It does NOT gate the mnemonic
    // itself: Alt+F still opens the File menu. H268-03 pins that. The debugger
    // window installs the same style for the same reason.
    jnext::disable_alt_menu_navigation(menuBar(), this);

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

    // The shader could not run (no usable OpenGL): the widget has already
    // fallen back to the software display; make the menu say so, and tell the user.
    // Soft scanlines were asked for scanlines: they continue as the software Scanlines.
    connect(emulator_widget_, &EmulatorWidget::crt_shader_failed, this,
            [this](const QString& why) {
        const bool soft = emulator_widget_->crt_shader_style() == crt_shader::Style::Lite;
        if (soft) set_crt_filter(true);
        sync_crt_actions();
        auto* box = new QMessageBox(QMessageBox::Warning, tr("CRT Effect"),
            soft ? tr("The CRT effect cannot run on this computer: %1.\n\n"
                      "The display continues with the software Scanlines.").arg(why)
                 : tr("The CRT effect cannot run on this computer: %1.\n\n"
                      "The display continues without it.").arg(why),
            QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();   // not exec(): the emulator must keep running behind it
    });

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
MainWindow::~MainWindow() {
    // GH #306 — the pause client must not outlive this window on a backend
    // that does, the same contract as the DebuggerManager destructor.
    if (debugger_ && pause_client_ != jnext::dbg::CLIENT_NONE)
        debugger_->detach(pause_client_);
}

void MainWindow::set_emulator(Emulator* emu) {
#ifdef ENABLE_DEBUGGER
    // GH #278 WP2 — the DebuggerManager built below adapts the loop owner's
    // debugger backend, which set_debugger() must have supplied first. Without
    // it this window would come up with NO debugger at all — no Debug toolbar
    // button, View > Debugger dead, the debugger keys unforwarded — and nothing
    // would say so. That is a wiring error in the caller (QtApp, a test
    // fixture, docshot), so it is REFUSED here, loudly and before anything is
    // bound, rather than tolerated as a debugger-less window.
    if (emu && !debugger_mgr_ && !debugger_)
        throw std::logic_error(
            "MainWindow::set_emulator(): no debugger backend - call "
            "set_debugger() with the loop owner's jnext::dbg::Debugger first");
#endif
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
    sync_machine_type_display();
    // GH #381 - a rebuilt machine is a different program: what the POK session
    // recorded to undo its trainers belongs to the one that is gone. The dialog,
    // if open, shows what memory holds now.
    if (pok_) {
        pok_->forget_applied();
        if (pok_dialog_) pok_dialog_->refresh();
    }
    // GH #278 WP6 — the Magic Breakpoint item shows whether the magic
    // breakpoint is armed (CTL-14), e.g. by --magic-breakpoint, on every
    // machine bound here (a cold boot binds a new one). Its old read of the
    // config sat in create_menus(), which runs before any machine exists, so
    // the item always opened unchecked.
    if (magic_bp_action_ && debugger_ && emu)
        magic_bp_action_->setChecked(debugger_->magic_breakpoint());
    // GH #89 — Tape > Start / Stop Saving show a --tape-save at once, not at
    // the first status tick.
    update_tape_status();
#ifdef ENABLE_DEBUGGER
    if (!debugger_mgr_ && emu) {
        debugger_mgr_ = new DebuggerManager(this, *debugger_, this);
        debugger_mgr_->set_script_host(script_host_);   // GH #26 WP5
        // Debugger starts disabled — main window stays fixed-size.
        // GH #1 — hand it the user's key bindings straight away: the window is
        // built now, and a keymap pushed only when it is first SHOWN would
        // leave the emulator window forwarding keys the debugger does not have.
        push_debug_keymap();
    }
#endif
}

// ---------------------------------------------------------------------------
// GH #306 — Pause from the emulator window
// ---------------------------------------------------------------------------

void MainWindow::ensure_pause_client() {
    if (pause_client_ != jnext::dbg::CLIENT_NONE || !debugger_) return;
    // An OBSERVER: it arms nothing, so pausing from this window costs no
    // per-instruction tracking and leaves --persistent-breakpoints' default
    // (PBPUI-03) alone. Attached on the first press, not at startup, so a
    // session that never pauses has no extra client.
    const auto r = debugger_->attach(jnext::dbg::ClientInfo{
        "Qt emulator window", jnext::dbg::ClientKind::Gui, /*observer=*/true});
    if (!r) return;
    pause_client_ = r.value;
#ifdef ENABLE_DEBUGGER
    if (debugger_mgr_) debugger_mgr_->set_emulator_window_pause_client(pause_client_);
#endif
}

void MainWindow::toggle_pause() {
    if (debugger_) {
        const bool paused = debugger_->state().paused;
#ifdef ENABLE_DEBUGGER
        if (debugger_mgr_ && debugger_mgr_->is_enabled()) {
            // The window is open: the same verbs as its F5 / F9 keys, so the
            // corruption modal, the step-off and the pause-edge presentation
            // all apply.
            if (paused) debugger_mgr_->on_run(); else debugger_mgr_->on_pause();
        } else
#endif
        {
            ensure_pause_client();
            if (pause_client_ != jnext::dbg::CLIENT_NONE) {
                // The Result is not acted on: sync_pause_state() below shows
                // the truth whatever the backend answered.
                if (paused) debugger_->run(pause_client_);
                else        debugger_->pause(pause_client_);
            }
        }
    }
    sync_pause_state();
}

void MainWindow::sync_pause_state() {
    const bool p = debugger_ && debugger_->state().paused;
    if (pause_action_) pause_action_->setChecked(p);
    if (paused_label_) paused_label_->setVisible(p);
}

void MainWindow::set_script_host(jnext::script::ScriptHost* host) {
    script_host_ = host;
#ifdef ENABLE_DEBUGGER
    if (debugger_mgr_) debugger_mgr_->set_script_host(host);
#endif
}

void MainWindow::sync_machine_type_display() {
    if (!emulator_) return;
    const MachineType type = emulator_->config().type;
    if (machine_label_) machine_label_->setText(tr(machine_type_str(type)));
    if (machine_type_group_) {
        for (QAction* a : machine_type_group_->actions()) {
            if (a->data().toInt() == static_cast<int>(type)) {
                a->setChecked(true);
                break;
            }
        }
    }
}

void MainWindow::resync_input_dispatchers() {
    if (mouse_dispatcher_) mouse_dispatcher_->resync();
}

void MainWindow::persist_joy_config() {
    for (int i = 0; i < 2; ++i) {
        app_config_.data().joy_source[i] = emulator_->joystick_source(i);
        app_config_.data().joy_device[i] =
            QString::fromStdString(emulator_->joystick_device(i).id);
        app_config_.data().joy_device_name[i] =
            QString::fromStdString(emulator_->joystick_device(i).name);
    }
    app_config_.save();
}

void MainWindow::on_joy_choice_selected(int connector, const JoyChoice& choice) {
    if (!emulator_) return;
    // The emulator enforces the one-cursor-connector and one-controller-per-
    // connector rules (it may reset the other connector), so read the
    // effective state back for both the menu and the persisted config.
    switch (choice.kind) {
    case JoyChoice::Kind::Auto:
        emulator_->set_joystick_source(connector, JoySource::Sdl);
        emulator_->set_joystick_device(connector, JoyDeviceRef{});
        break;
    case JoyChoice::Kind::Device: {
        std::string name = choice.label;
        if (joy_device_provider_) {
            for (const auto& d : joy_device_provider_())
                if (d.id == choice.id) name = d.name;
        }
        emulator_->set_joystick_source(connector, JoySource::Sdl);
        emulator_->set_joystick_device(connector, JoyDeviceRef{ choice.id, name });
        break;
    }
    case JoyChoice::Kind::Missing:
        break;   // already the assignment; the entry only displays it
    case JoyChoice::Kind::Keys:
        emulator_->set_joystick_source(connector, JoySource::CursorKeys);
        emulator_->set_joystick_device(connector, JoyDeviceRef{});
        break;
    case JoyChoice::Kind::None:
        emulator_->set_joystick_source(connector, JoySource::None);
        emulator_->set_joystick_device(connector, JoyDeviceRef{});
        break;
    }
    sync_joy_source_menu();
    persist_joy_config();
}

void MainWindow::sync_joy_source_menu() {
    // Without an emulator (the window before set_emulator()) the menu shows the
    // defaults: Automatic on both connectors.
    const std::vector<JoyDeviceInfo> devices =
        joy_device_provider_ ? joy_device_provider_() : std::vector<JoyDeviceInfo>{};
    for (int conn = 0; conn < 2; ++conn) {
        QMenu* sub = joy_menu_[conn];
        if (!sub) continue;
        // This runs from inside a triggered action's slot, so the old actions
        // are retired with deleteLater() rather than deleted under the signal.
        for (QAction* old : sub->actions()) {
            sub->removeAction(old);
            old->deleteLater();
        }
        if (joy_group_[conn]) joy_group_[conn]->deleteLater();
        joy_group_[conn] = new QActionGroup(sub);
        joy_group_[conn]->setExclusive(true);

        const auto choices = joy_choices(
            conn, devices, emulator_ ? emulator_->joystick_source(conn) : JoySource::Sdl,
            emulator_ ? emulator_->joystick_device(conn) : JoyDeviceRef{});
        for (const JoyChoice& ch : choices) {
            if (ch.kind == JoyChoice::Kind::Keys) sub->addSeparator();
            QString text;
            switch (ch.kind) {
            case JoyChoice::Kind::Auto: text = tr("&Automatic (first free controller)"); break;
            case JoyChoice::Kind::Keys: text = tr("Cursor &Keys + Space"); break;
            // No mnemonic on None: the pinned menu shape (main_window_accel_test
            // MA-05) counts two per connector submenu, as before this menu listed
            // controllers; a mnemonic here would change that count.
            case JoyChoice::Kind::None: text = tr("None"); break;
            default:
                // A controller's own name: escape '&' so it is not read as a mnemonic.
                text = QString::fromStdString(ch.label).replace("&", "&&");
                break;
            }
            QAction* a = sub->addAction(text);
            a->setCheckable(true);
            joy_group_[conn]->addAction(a);
            if (!ch.id.empty()) a->setStatusTip(QString::fromStdString(ch.id));
            a->setChecked(ch.checked);
            connect(a, &QAction::triggered, this, [this, conn, ch](bool) {
                on_joy_choice_selected(conn, ch);
            });
        }
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
        // GH #242 — give the viewport its fixed size back BEFORE the window is
        // sized around it. Entering fullscreen releases the constraint by
        // passing QWIDGETSIZE_MAX to setFixedSize(), and Qt reads that as "no
        // constraint at all" (QWidgetPrivate::setMinimumSize_helper maps a
        // QWIDGETSIZE_MAX minimum to 0) — which is exactly why it works as a
        // release, but it also means the viewport comes back with no size of
        // its own, and EmulatorWidget implements no sizeHint(). Without this
        // the layout had nothing to size itself from and the window collapsed
        // to the chrome's own minimum on every fullscreen exit, staying wrong
        // until the next scale change put the constraint back.
        emulator_widget_->set_scale(current_scale_);
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
    // The widget has a fixed size; lock the window to exactly the size the
    // layout wants for it plus the chrome (menu bar, toolbar, status bar).
    //
    // GH #236 — sizeHint(), NOT adjustSize(). QWidget::adjustSize() is
    // documented to clamp a WINDOW to two thirds of the screen, and that clamp
    // is what shipped a 2x window 1280x960 instead of 1280x1109 on the
    // reporter's 2560x1440 display (960 == 1440*2/3; at 3x it came out
    // 1706x960 == 2560*2/3 x 1440*2/3, which is what identified it).
    //
    // The clamp is normally invisible because adjustSize() also activates the
    // layout, and QLayout::SetDefaultConstraint then re-imposes the layout's
    // minimum on the window, which resize() clamps the too-small size back up
    // to. The setMinimumSize(0, 0) below clears that minimum, so the rescue
    // only happens when the activation actually runs — and it does not when
    // the layout is already clean.
    //
    // Which is the startup path exactly. main.cpp applies the saved scale
    // before the event loop starts (correct: the layout is still dirty), and
    // then this runs AGAIN on a mapped window with the scale already at its
    // final value — from EmulatorWidget's devicePixelRatio one-shot via
    // scale_changed, and again from QtApp::init's 100 ms re-apply. Re-applying
    // an unchanged scale re-sets the viewport to the fixed size it already
    // has, and a fixed-size child's updateGeometry() does NOT invalidate its
    // parent layout (QWidgetPrivate::updateGeometry_helper skips that when
    // minw==maxw && minh==maxh). Nothing dirties the layout, activate() is a
    // no-op, and the clamped size gets locked in — while a scale change made
    // from the menu, which does dirty the layout, was always correct. Hence
    // the report's "started at 2x" vs "switched to 2x" mismatch.
    //
    // sizeHint() is that same layout size with no screen clamp applied, so it
    // is right whichever way we got here. A window bigger than the screen is
    // then possible (3x on a 1440-tall display), but that is what picking 3x
    // means and is already what the menu path does.
    emulator_widget_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    emulator_widget_->updateGeometry();
    setFixedSize(sizeHint());
}

void MainWindow::cycle_scale() {
    if (is_fullscreen_) return;
    // 2 -> 3 -> 4 -> 2
    int next = current_scale_ + 1;
    if (next > EmulatorWidget::MAX_SCALE) next = EmulatorWidget::MIN_SCALE;
    set_scale(next);
}

void MainWindow::set_crt_filter(bool enabled) {
    // The scanline filter and the OpenGL shader are mutually exclusive.
    if (enabled && emulator_widget_->crt_shader()) set_crt_shader(false);
    emulator_widget_->set_crt_filter(enabled);

    sync_crt_actions();
}

void MainWindow::sync_crt_actions() {
    const bool shader = emulator_widget_->crt_shader();
    const auto style = emulator_widget_->crt_shader_style();
    if (crt_filter_action_) crt_filter_action_->setChecked(emulator_widget_->crt_filter());
    if (crt_soft_action_) crt_soft_action_->setChecked(shader && style == crt_shader::Style::Lite);
    if (crt_tv_action_) crt_tv_action_->setChecked(shader && style == crt_shader::Style::Tv);
    if (crt_pc_action_) crt_pc_action_->setChecked(shader && style == crt_shader::Style::Pc);
}

bool MainWindow::crt_filter() const {
    return emulator_widget_->crt_filter();
}

bool MainWindow::set_crt_shader(bool enabled) {
    const bool ok = emulator_widget_->set_crt_shader(enabled);
    if (ok && enabled) set_crt_filter(false);   // exclusive with the scanline filter
    sync_crt_actions();
    return ok;
}

bool MainWindow::crt_shader() const {
    return emulator_widget_->crt_shader();
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
    // Reserved today: Alt+Q/O/S/K/R/T/D/P/U, Alt+Shift+F5 and Alt+Shift+F9 (shortcuts) and Alt+F/M/I/A/B/V/N/H
    // (mnemonics). Alt+E/G/C/` stay free because the GUEST uses them
    // (keyboard.cpp:163,172-174).
    //
    // -----------------------------------------------------------------------
    // Issue #217 — the SECOND namespace, which the paragraph above does not
    // cover and which shipped three collisions unnoticed.
    //
    // The items inside ONE popup share a namespace of their own, live only
    // while that popup is open. Qt's answer to a repeat there is the same
    // ambiguity: with "Load &NEX File..." and "Save S&napshot..." both on N,
    // pressing N with the File menu open moved between them instead of
    // choosing one. Three such pairs were reported (File N, File M, View F)
    // and a fourth was found by the guard written for them (Machine S, on
    // "&Soft Reset" / "CPU &Speed").
    //
    // Replacement letters were picked to collide with NOTHING — not inside
    // their own popup, not with a menubar title, not with a shortcut above,
    // not with the guest's Alt+E/G/C — which is why two of them sit on an
    // awkward letter ("Record MPEG&4 Video...", "CP&U Speed"). An awkward
    // mnemonic beats a stolen one.
    //
    // main_window_accel_test (rows MA-01..05) now harvests every mnemonic and
    // every key sequence from the live window and fails on a repeat in any of
    // the three namespaces, so the next one cannot ship. MA-05 pins the SHAPE
    // of the menu tree: adding or removing an entry here means updating its
    // numbers, deliberately.
    // -----------------------------------------------------------------------

    // --- File menu ---
    QMenu* file_menu = menuBar()->addMenu(tr("&File"));

    // #217: mnemonic moved off N (shared with "Save S&napshot...") onto L, the
    // first letter of the verb and of the toolbar button that does the same
    // thing. L is free everywhere: no menubar title, no shortcut, not a guest
    // Alt key, and unused in this popup.
    QAction* load_nex = file_menu->addAction(tr("&Load NEX File..."));
    load_nex->setShortcut(QKeySequence(Qt::ALT | Qt::Key_O));
    connect(load_nex, &QAction::triggered, this, &MainWindow::on_load_nex);

    // GH #93 — a live card change, for NextZXOS's REMOUNT ("Remove/insert SD
    // and press Y"). I and E are free in this popup.
    QAction* insert_sd = file_menu->addAction(tr("&Insert SD Card Image..."));
    connect(insert_sd, &QAction::triggered, this, &MainWindow::on_insert_sd);
    QAction* eject_sd = file_menu->addAction(tr("&Eject SD Card"));
    connect(eject_sd, &QAction::triggered, this, [this]() { handle_sd_card_path(QString()); });

    file_menu->addSeparator();

    // #217: this and the old "&Mount SD Card Image..." both claimed M. Mount kept
    // it (GH #93 renamed it Insert) — nobody hunts for video recording under
    // "MPEG4". Every letter of "Record MPEG4
    // Video" that is free everywhere else is either already used in this popup
    // (P is "Sto&p MPEG4 Recording") or is the digit; V would read better but
    // Alt+V is the View menu. So the 4 of MPEG4, underlined in place.
    record_start_action_ = file_menu->addAction(tr("Record MPEG&4 Video..."));
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
    connect(rzx_play, &QAction::triggered, this, &MainWindow::on_rzx_play);

    QAction* rzx_record = file_menu->addAction(tr("Record R&ZX..."));
    connect(rzx_record, &QAction::triggered, this, &MainWindow::on_rzx_record);

    QAction* rzx_stop = file_menu->addAction(tr("Stop RZX Recordin&g"));
    connect(rzx_stop, &QAction::triggered, this, &MainWindow::handle_rzx_stop);

    file_menu->addSeparator();

    screenshot_action_ = file_menu->addAction(tr("Save &Screenshot..."));
    screenshot_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_S));
    connect(screenshot_action_, &QAction::triggered, this, [this]() {
        if (!emulator_) return;
        // GH #18 — two formats, chosen by the extension, exactly as
        // --delayed-screenshot chooses. The filter is the discoverable half
        // of that: a user who never reads the man page still sees .scr here.
        const QString png_filter = tr("PNG Images (*.png)");
        const QString scr_filter = tr("ZX Screen Dumps (*.scr)");
        QString selected = png_filter;
        QString path = QFileDialog::getSaveFileName(
            this, tr("Save Screenshot"), app_config_.data().screenshot_dir,
            png_filter + QStringLiteral(";;") + scr_filter
                + QStringLiteral(";;") + tr("All Files (*)"),
            &selected);
        if (path.isEmpty()) return;
        // Only supply an extension when the user typed none: a name that
        // already ends .png or .scr must keep the format it names, whichever
        // filter row happened to be selected.
        if (!path.endsWith(".png", Qt::CaseInsensitive)
            && !path.endsWith(".scr", Qt::CaseInsensitive)) {
            path += (selected == scr_filter) ? QStringLiteral(".scr")
                                             : QStringLiteral(".png");
        }
        // Task 66 — remember the containing directory for the next dialog.
        app_config_.data().screenshot_dir = QFileInfo(path).absolutePath();
        app_config_.save();
        write_screenshot(path);
    });

    // GH #19 — the same capture with no dialog at all. Alt+K: Alt+S is this
    // dialog and Alt+Shift+S is Save Snapshot, Alt+Q/O/R/T/D/P are taken, the
    // menu bar holds Alt+F/M/I/A/B/V/N/H and the GUEST owns Alt+E/G/C
    // (keyboard.cpp:163,172-174). K is free in every one of those namespaces
    // and is the label's own mnemonic, so the menu entry and the shortcut are
    // the same letter rather than two unrelated choices.
    quick_screenshot_action_ = file_menu->addAction(tr("Quic&k Screenshot"));
    quick_screenshot_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_K));
    connect(quick_screenshot_action_, &QAction::triggered,
            this, &MainWindow::on_quick_screenshot);
    // Same two fixes as the quick snapshot actions below (GH #321): reachable
    // with the menu bar hidden, and one capture per press.
    quick_screenshot_action_->setAutoRepeat(false);
    addAction(quick_screenshot_action_);

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

    // GH #321 — Quick Save / Quick Load: no dialog, fixed folder. No '&'
    // mnemonic (every letter is taken in some Alt namespace, #217). The chords
    // live HERE; the SDL frontend's twin is platform/quick_snapshot_chord.h.
    quick_save_action_ = file_menu->addAction(tr("Quick Save Snapshot"));
    quick_save_action_->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_F5));
    connect(quick_save_action_, &QAction::triggered, this, &MainWindow::on_quick_save);
    quick_load_action_ = file_menu->addAction(tr("Quick Load Snapshot"));
    quick_load_action_->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_F9));
    connect(quick_load_action_, &QAction::triggered, this, &MainWindow::on_quick_load);
    // A held chord must act once, not once per autorepeat (35 saves in 2 s).
    quick_save_action_->setAutoRepeat(false);
    quick_load_action_->setAutoRepeat(false);
    // Also on the window itself, as pause_action_ is: with the menu bar hidden
    // (fullscreen) a menu action's shortcut never matches, the key falls to
    // keyPressEvent, and Alt+Shift+F9 would fire the Multiface NMI.
    addAction(quick_save_action_);
    addAction(quick_load_action_);

    file_menu->addSeparator();

    quit_action_ = file_menu->addAction(tr("&Quit"));
    // GH #380 — Alt+F4 is a SECOND shortcut next to Alt+Q, so it quits whether or
    // not the mouse is captured. Windows takes a plain Alt+F4 itself (Qt's
    // qwindowskeymapper.cpp passes it on) and closes the window; where nothing
    // does, the key reaches this window, and keyPressEvent() sends every other
    // Alt-held F4 here too. Alt+Q stays the primary: host hotkeys live on Alt (#115).
    quit_action_->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Q), QKeySequence(Qt::ALT | Qt::Key_F4)});
    // A menu draws only shortcut() — the first — unless the text carries its own
    // accelerator column after a tab (QMenu::initStyleOption), so the label names
    // both, taken from the bindings. Consumers that compare this action's text
    // look at the part before the tab (host_chords.cpp, the suites' lookups).
    quit_action_->setText(tr("&Quit") + QLatin1Char('\t') +
                          QKeySequence::listToString(quit_action_->shortcuts(),
                                                     QKeySequence::NativeText));
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
    connect(quit_action_, &QAction::triggered, this, [this]() {
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

    // GH #306 — freeze/resume the machine from the emulator window, without
    // opening the debugger. P is Preferences (Alt+P, #259), so the shortcut is
    // Alt+U (free in every Alt namespace) and the popup mnemonic is A (the
    // popup's P/S/T/U/E are taken). The SAME QAction goes on the Main toolbar
    // (one checkmark) and on the window, so the shortcut also works with the
    // menu bar hidden in fullscreen.
    // GH #322 — the Pause key is a second shortcut (the conventional emulator
    // pause key, as in FUSE): an owner-approved exception to "host keys are
    // Alt+key". Harmless: on a Next, PS/2 Pause/Break only resets the PS/2
    // module (keymaps.vhd:104, ps2_keyb.vhd:245), jnext models no PS/2, and
    // qt_key_to_sdl never forwarded the key. Alt+U stays first: QMenu shows
    // only the primary shortcut and Mac keyboards have no Pause key.
    pause_action_ = machine_menu->addAction(tr("P&ause"));
    pause_action_->setCheckable(true);
    pause_action_->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_U),
                                 QKeySequence(Qt::Key_Pause)});
    // A held key re-fired the action (measured ~20 toggles in 1.5 s).
    pause_action_->setAutoRepeat(false);
    // Its icon (pause, resume while checked) is bound in create_toolbar().
    pause_action_->setToolTip(tr("Pause / resume emulation (Alt+U or Pause)"));
    connect(pause_action_, &QAction::triggered, this, [this]() { toggle_pause(); });
    addAction(pause_action_);

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
        {"&Pentagon 128K",    MachineType::PENTAGON},
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

    // #217, the collision the guard found rather than the issue: this and
    // "&Soft Reset" above both claimed S in the Machine popup. Soft Reset keeps
    // S (its own first letter, and #45 named it); CPU Speed takes the U of the
    // acronym, which is free everywhere.
    QMenu* speed_menu = machine_menu->addMenu(tr("CP&U Speed"));
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

    // GH #381 - the trainers of a .pok file, applied to the running machine.
    // R: the Machine popup's P/S/A/T/U/E are taken. Deliberately no shortcut, no
    // data() and not checkable: preferences_apply_test finds the machine actions
    // by data() plus checkable.
    machine_menu->addSeparator();
    QAction* pok_trainers = machine_menu->addAction(tr("POK T&rainers..."));
    connect(pok_trainers, &QAction::triggered, this, &MainWindow::on_pok_trainers);

    // --- Input menu (Task 79) ---
    // Per-connector host input source. Each Next joystick connector can be
    // driven by a chosen (or the first free) controller, by the host cursor keys + Space,
    // or by nothing (only one connector may use the cursor keys at a time). Changes apply
    // live and persist to ~/.jnext/jnext.conf.
    QMenu* input_menu = menuBar()->addMenu(tr("&Input"));
    for (int conn = 0; conn < 2; ++conn) {
        joy_menu_[conn] = input_menu->addMenu(conn == 0 ? tr("Joy &1 Source (port 0x1F)")
                                                        : tr("Joy &2 Source (port 0x37)"));
    }
    // Filled with the defaults now; sync_joy_source_menu() refills them from the
    // emulator and the host's device list once those exist.
    sync_joy_source_menu();

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
    // releases by default (Alt+Cmd on macOS, where Qt reads Command as Ctrl);
    // GH #365 makes the gesture configurable.
    update_capture_status_tip();
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

    // GH #89 — what --tape-save does, from the menu. No shortcut, like the
    // rest of this menu: a host Alt shortcut takes a key from the guest, and
    // saving is not frequent. Their enabled state follows the emulator
    // (update_tape_status()), so a --tape-save start shows Stop enabled.
    tape_menu->addSeparator();
    tape_save_start_action_ = tape_menu->addAction(tr("Start &Saving..."));
    tape_save_start_action_->setStatusTip(
        tr("Save to a TAP, TZX or WAV file: SAVEs and the tape output are appended"));
    connect(tape_save_start_action_, &QAction::triggered, this, &MainWindow::on_tape_save_start);
    tape_save_stop_action_ = tape_menu->addAction(tr("Stop Sa&ving"));
    tape_save_stop_action_->setEnabled(false);
    connect(tape_save_stop_action_, &QAction::triggered, this, &MainWindow::on_tape_save_stop);

    // --- Debug menu ---
    // Mnemonic is Alt+B ("De&bug"), NOT Alt+D: Alt+D is the View > Debugger
    // shortcut (#115). Alt+D was ALSO the reason keyboard.cpp could not give
    // the guest Alt+D — that constraint is unchanged, only its owner moved.
    QMenu* debug_menu = menuBar()->addMenu(tr("De&bug"));

    magic_bp_action_ = debug_menu->addAction(tr("Magic &Breakpoint"));
    magic_bp_action_->setCheckable(true);
    magic_bp_action_->setToolTip(tr("Enable magic breakpoints (ED FF / DD 01)"));
    // Its checked state is the machine's, synced by set_emulator(): this runs
    // in the constructor, before any machine is bound (GH #278 WP6).
    connect(magic_bp_action_, &QAction::triggered, this, [this](bool checked) {
        if (!debugger_) return;
        // Arm/disarm on the running machine, through the backend (CTL-14). This
        // used to re-run init() with the flag changed, which re-initialised a
        // booted NextZXOS machine into 48K BASIC and never disarmed the hook
        // (GH #239).
        debugger_->set_magic_breakpoint(checked);
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

    // #217: F belonged to "&Fullscreen" above (its own first letter, and the
    // entry users reach for far more often). C is the guest's Alt+C, R is the
    // Power Reset shortcut, T the Open Tape one — so the L of "Filter", free
    // everywhere.
    crt_filter_action_ = view_menu->addAction(tr("CRT Scan&lines"));
    crt_filter_action_->setCheckable(true);
    connect(crt_filter_action_, &QAction::triggered, this, [this](bool checked) {
        set_crt_filter(checked);
    });

    // The OpenGL shader's three looks, only in a build that has it. The entry above is
    // the Scanlines mode (the software overlay, with or without OpenGL), so
    // View carries one choice: off, scanlines, soft scanlines, TV or PC monitor. No mnemonics, like the
    // Scale entries: the main window's Alt-letter space is full (see the header of
    // test/gui/main_window_accel_test.cpp) and MA-05 pins how many the menu tree has.
    if (EmulatorWidget::crt_shader_built_in()) {
        auto add_look = [this, view_menu](const QString& text, crt_shader::Style style) {
            QAction* a = view_menu->addAction(text);
            a->setCheckable(true);
            connect(a, &QAction::triggered, this, [this, style](bool checked) {
                if (checked) emulator_widget_->set_crt_shader_style(style);
                set_crt_shader(checked);
            });
            return a;
        };
        crt_soft_action_ = add_look(tr("CRT Soft Scanlines"), crt_shader::Style::Lite);
        crt_tv_action_ = add_look(tr("CRT TV"), crt_shader::Style::Tv);
        crt_pc_action_ = add_look(tr("CRT PC Monitor"), crt_shader::Style::Pc);
        crt_soft_action_->setToolTip(tr("Soft scanlines only, drawn by the GPU"));
        crt_tv_action_->setToolTip(tr("Curved glass, bloom, scanlines and shadow mask, drawn by the GPU"));
    }

#ifdef ENABLE_DEBUGGER
    view_menu->addSeparator();
    debugger_action_ = view_menu->addAction(tr("&Debugger"));
    debugger_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_D));
    debugger_action_->setCheckable(true);

    // GH #215: the Debug menu used to hold Magic Breakpoint and nothing else,
    // so a user who opened the menu named Debug looking for the debugger found
    // an unrelated toggle and concluded it was not in the menus. Put the SAME
    // QAction there too — not a copy — so both routes toggle one action and
    // show one checkmark. It has to happen here rather than in the Debug block
    // above: that menu is built first, before this action exists. View keeps
    // the entry the design plan's Phase 7.5 asked for; nothing moves.
    debug_menu->addSeparator();
    debug_menu->addAction(debugger_action_);
#endif

    // --- Settings menu (Task 66) ---
    // Mnemonic is Alt+N ("Setti&ngs"), NOT Alt+S: Alt+S is the Save
    // Screenshot shortcut (#115). E/G/C are also off-limits — the guest owns
    // Alt+E/G/C (keyboard.cpp:163,172-174) — which leaves N.
    QMenu* settings_menu = menuBar()->addMenu(tr("Setti&ngs"));

    // GH #259: an explicit Alt+P, not QKeySequence::Preferences. That standard
    // key is whatever the Qt platform table says, and on Linux it said the
    // Key_Settings multimedia key up to Qt 6.11.1 and NOTHING from Qt 6.11.2 —
    // so the action silently lost its only shortcut. Alt is the host
    // namespace (#115), and P is free in all of it: no menubar mnemonic, no
    // other shortcut, not one of the guest's Alt+E/G/C.
    QAction* preferences = settings_menu->addAction(tr("&Preferences..."));
    preferences->setShortcut(QKeySequence(Qt::ALT | Qt::Key_P));
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

    // Every icon is from the monochrome SVG set (doc/design/ICONSET-DESIGN.md),
    // rendered in the toolbar's palette and again on every palette change.
    // Icon-only buttons: each action's tooltip is its text unless set below.
    QAction* reset_btn = toolbar->addAction(tr("Power Reset"));
    toolbar_icons::bind(reset_btn, toolbar, QStringLiteral("power"));
    connect(reset_btn, &QAction::triggered, this, &MainWindow::on_reset);

    QAction* soft_reset_btn = toolbar->addAction(tr("Soft Reset"));
    toolbar_icons::bind(soft_reset_btn, toolbar, QStringLiteral("soft-reset"));
    connect(soft_reset_btn, &QAction::triggered, this, &MainWindow::on_soft_reset);

    if (pause_action_) {   // GH #306 — the same action as Machine > Pause
        toolbar->addAction(pause_action_);
        // Checked = paused: the button then shows what a click does, resume.
        toolbar_icons::bind(pause_action_, toolbar, QStringLiteral("pause"),
                            QStringLiteral("resume"));
    }

    QAction* load_btn = toolbar->addAction(tr("Load"));
    toolbar_icons::bind(load_btn, toolbar, QStringLiteral("load"));
    connect(load_btn, &QAction::triggered, this, &MainWindow::on_load_nex);

    toolbar->addSeparator();

    // Screenshot toolbar button — triggers the same action as File > Save Screenshot.
    if (screenshot_action_) {
        QAction* screenshot_btn = toolbar->addAction(tr("Screenshot"));
        toolbar_icons::bind(screenshot_btn, toolbar, QStringLiteral("screenshot"));
        connect(screenshot_btn, &QAction::triggered, screenshot_action_, &QAction::trigger);
    }

    toolbar->addSeparator();

    // Multiface NMI toolbar button — mirrors the F9 hotkey path.
    QAction* nmi_btn = toolbar->addAction(tr("NMI"));
    toolbar_icons::bind(nmi_btn, toolbar, QStringLiteral("nmi"));
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
        "'shown' below 'emu' means frames are not being presented - deliberately "
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

    // GH #306 — hidden until the backend reports a pause (sync_pause_state()),
    // so a session that never pauses has the same status bar as before.
    paused_label_ = new QLabel(tr("Paused"));
    paused_label_->setMinimumWidth(60);
    paused_label_->setAlignment(Qt::AlignCenter);
    paused_label_->setVisible(false);

    statusBar()->addWidget(fps_label_, 1);
    statusBar()->addWidget(speed_label_, 1);
    statusBar()->addWidget(emu_speed_label_, 1);
    statusBar()->addWidget(tape_label_, 1);
    statusBar()->addWidget(esp_label_, 1);
    statusBar()->addWidget(paused_label_, 1);
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
        load_filter());
    if (!path.isEmpty()) {
        // Task 66 — remember the containing directory for the next dialog.
        app_config_.data().last_load_dir = QFileInfo(path).absolutePath();
        app_config_.save();
        handle_load_path(path);
    }
}

// GH #381 - Machine > POK Trainers...: the dialog of the open file or, with none
// open yet, the picker for one.
void MainWindow::on_pok_trainers() {
    if (pok_ && pok_dialog_) {
        if (pok_dialog_->isVisible()) {
            pok_dialog_->raise();
            pok_dialog_->activateWindow();
        } else {
            pok_dialog_->open();
        }
        return;
    }
    on_pok_open();
}

void MainWindow::on_pok_open() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open POK File"), app_config_.data().last_load_dir,
        tr("POK Trainers (*.pok *.POK);;All Files (*)"));
    if (path.isEmpty()) return;
    app_config_.data().last_load_dir = QFileInfo(path).absolutePath();
    app_config_.save();
    open_pok_file(path);
}

bool MainWindow::open_pok_file(const QString& path) {
    if (!emulator_ || !debugger_) return false;
    jnext::pok::PokFile file;
    std::string error;
    if (!jnext::pok::load_pok_file(path.toStdString(), file, error)) {
        const QString msg = tr("Cannot load POK file: %1").arg(QString::fromStdString(error));
        statusBar()->showMessage(msg, 5000);
        Log::platform()->error("status bar: {}", msg.toStdString());
        if (!unattended_) QMessageBox::warning(this, tr("POK Trainers"), msg);
        return false;
    }
    start_pok_session(std::move(file), path.toStdString(), true);
    return true;
}

// Make `file` the window's POK session; `show` opens the trainers dialog on it.
void MainWindow::start_pok_session(jnext::pok::PokFile file, const std::string& display,
                                   bool show) {
    pok_ = std::make_unique<jnext::pok::Session>(*debugger_, std::move(file), display);
    if (!pok_dialog_) {
        pok_dialog_ = std::make_unique<PokDialog>(this);
        connect(pok_dialog_.get(), &PokDialog::open_file_requested, this, &MainWindow::on_pok_open);
    }
    pok_dialog_->set_session(pok_.get());   // straight away: the old session is gone
    if (!show) return;
    if (pok_dialog_->isVisible()) {
        pok_dialog_->raise();
        pok_dialog_->activateWindow();
    } else {
        pok_dialog_->open();
    }
}

bool MainWindow::pok_dialog_visible() const {
    return pok_dialog_ && pok_dialog_->isVisible();
}

// GH #392 - the trainers bundled in a loaded archive: made the POK session
// WITHOUT opening the dialog (the program has only just been handed to the cold
// boot) and said once in the status bar. A .pok that does not parse to a
// trainer is logged and not offered.
void MainWindow::offer_archive_pok(const program_archive::ProgramArchive& archive,
                                   const std::string& program, const std::string& archive_path) {
    if (!emulator_ || !debugger_) return;
    const std::string pok = archive.matching_pok(program);
    if (pok.empty()) return;
    constexpr uint64_t kMaxPok = 1024 * 1024;
    std::vector<uint8_t> bytes;
    std::string why;
    if (archive.member_size(pok) > kMaxPok || !archive.read_member(pok, bytes, why)) {
        Log::platform()->info("archive {}: trainers not offered: {}", archive_path,
                              why.empty() ? pok + " is larger than 1 MiB" : why);
        return;
    }
    jnext::pok::PokFile file = jnext::pok::parse_pok(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (file.trainers.empty()) {
        Log::platform()->info("archive {}: {} holds no trainers, not offered", archive_path, pok);
        return;
    }
    start_pok_session(std::move(file), archive_path + " (" + pok + ")", false);
    const QString msg = tr("Trainers for this program: Machine > POK Trainers...");
    statusBar()->showMessage(msg, 8000);
    Log::platform()->info("status bar: {}", msg.toStdString());
}

// GH #228 — the post-picker half of a menu file-load, split out so
// nex_v13_dialog_test can drive the REAL dialog-to-decision wiring (the
// picker is modal and untestable; this is not).
void MainWindow::handle_load_path(const QString& path, bool jns_probed) {
    const std::string file = path.toStdString();

    // GH #392 - a program inside a .zip: pick the member, extract it to a file of
    // its own extension, and run the ordinary route on that file. Every message
    // names "<archive> (<member>)", never the scratch path.
    if (program_archive::is_archive_path(file)) {
        program_archive::ProgramArchive archive;
        std::string why;
        if (!archive.open(file, why)) {
            refuse_jns(path, QString::fromStdString(why));
            return;
        }
        if (archive.programs().empty()) {
            refuse_jns(path, QString::fromStdString(archive.no_program_why()));
            return;
        }
        std::string member = archive.programs()[0];
        if (archive.programs().size() > 1) {
            if (unattended_) {   // no dialog, as every other dialog here
                refuse_jns(path, QString::fromStdString(archive.several_why()));
                return;
            }
            QStringList items;
            for (const auto& m : archive.programs()) items << QString::fromStdString(m);
            bool ok = false;
            const QString pick = QInputDialog::getItem(
                this, tr("Load from Archive"),
                tr("%1 holds several programs. Load which one?").arg(QFileInfo(path).fileName()),
                items, 0, false, &ok);
            if (!ok || pick.isEmpty()) return;   // Cancel: nothing loaded, nothing reset
            member = pick.toStdString();
        }
        const QString shown = path + QStringLiteral(" (") + QString::fromStdString(member) + QStringLiteral(")");
        std::string extracted;
        if (!archive.extract(member, extracted, why)) {
            // extract() prefixes the member; the archive is named by `shown` here.
            const std::string tail = why.rfind(member + " ", 0) == 0 ? why.substr(member.size() + 1) : why;
            refuse_jns(shown, QString::fromStdString(tail));
            return;
        }
        if (handle_program_path(QString::fromStdString(extracted), shown, jns_probed))
            offer_archive_pok(archive, member, file);
        return;
    }
    handle_program_path(path, QString(), jns_probed);
}

bool MainWindow::handle_program_path(const QString& path, const QString& shown,
                                     bool jns_probed) {
    const std::string file = path.toStdString();
    // What the user is told the file is called: the archive member, not the scratch path.
    const QString name = shown.isEmpty() ? path : shown;
    // GH #228 — NEX V1.3 is an experimental format jnext does not
    // officially support: warn, and only proceed on an explicit
    // confirmation, Cancel being the default. The decision itself is the
    // pure seam (emulator_load_routes_to_nex + NexLoader::probe_version +
    // nex_version_needs_v13_optin, all unit-tested in nex_loader_test
    // NEXGATE-*); this dialog is only its consumer — and even a skipped
    // dialog cannot load V1.3 silently, because Emulator::load_nex()
    // enforces the same policy and fails the load.

    // GH #381 - a .pok is a list of trainers for the machine that is running,
    // not a program: it opens the trainers dialog and nothing is reset. Before
    // this it fell to the NEX route below, which cold-booted the session away
    // and only then failed to load it.
    if (jnext::pok::is_pok_path(file)) {
        open_pok_file(path);
        return false;
    }

    // An .rzx is checked BEFORE the cold boot below, so a file that cannot
    // play is refused with the running machine untouched — the same check
    // for File > Open and File > Play RZX Recording, which both come here.
    if (emulator_load_routes_to_rzx(file)) {
        if (emulator_ && emulator_->tape_save_active()) {
            QMessageBox::warning(this, tr("Play RZX Recording"), rzx_tape_save_refusal());
            return false;
        }
        std::string why;
        if (!rzx::playable(file, why)) {
            QMessageBox::warning(this, tr("Play RZX Recording"),
                tr("Cannot play %1: %2.").arg(name, QString::fromStdString(why)));
            return false;
        }
    }

    // GH #321 (R1) — likewise a .jns: refused BEFORE the cold boot, with the
    // running machine untouched.
    if (emulator_ && !jns_probed && jnext::is_jns_path(file)) {
        std::string why;
        if (!emulator_->probe_jns_file(file, why)) {
            refuse_jns(name, QString::fromStdString(why));
            return false;
        }
    }

    // GH #323 — likewise a .sna/.szx/.z80: it runs on the machine it was saved
    // on, so one that machine cannot be built for (a Timex, a Scorpion...) or
    // that is damaged is refused before the cold boot, session untouched.
    if (emulator_load_routes_to_snapshot(file)) {
        std::string why;
        if (!snapshot_file_loadable(file, why)) {
            refuse_jns(name, QString::fromStdString(why));   // status bar, and a dialog unless unattended
            return false;
        }
    }

    bool allow_experimental_nex_v13 = false;
    if (emulator_load_routes_to_nex(file)) {
        uint8_t ver_bcd = 0;
        char    ver[5]  = {0};
        // GH #381 (RF-1) - a .nex, or a file with an extension nothing owns, falls
        // to the NEX loader, which wants the "Next" prologue probe_version() looks
        // for: a file without it cannot load (NexLoader::load refuses the same
        // magic), so say so BEFORE the cold boot throws the session away for it.
        // The reason follows the case: a .nex is just not a NEX file (a saved web
        // page, say), and "its extension is not one JNEXT loads" or a hint about
        // .pok would contradict its name; only the other extensions get those.
        if (!NexLoader::probe_version(file, ver_bcd, ver)) {
            refuse_jns(name, snapshot_file_ext(file) == "nex"
                ? tr("it is not a NEX file (it does not start with the \"Next\" header)")
                : tr("it is not a NEX file and its extension is not one JNEXT loads "
                     "(.nex, .jns, .sna, .szx, .z80, .tap, .tzx, .wav, .rzx, .zip); a .pok "
                     "trainer file opens from Machine > POK Trainers"));
            return false;
        }
        if (nex_version_needs_v13_optin(ver_bcd)) {
            QMessageBox box(QMessageBox::Warning, tr("Experimental NEX V1.3"),
                            tr("'%1' is NEX version %2.\n\n"
                               "NEX V1.3 is an EXPERIMENTAL format and is NOT "
                               "supported in any way: it may load incorrectly, "
                               "misbehave, or not work at all, and no support "
                               "is provided for it.\n\n"
                               "Load it anyway?")
                                .arg(shown.isEmpty() ? QFileInfo(path).fileName() : shown,
                                     QString::fromLatin1(ver)),
                            QMessageBox::NoButton, this);
            QPushButton* proceed =
                box.addButton(tr("Proceed (unsupported)"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(QMessageBox::Cancel);  // Cancel is the default
            box.exec();
            if (box.clickedButton() != proceed) {
                return false;  // Cancel aborts: nothing is loaded, nothing reset
            }
            allow_experimental_nex_v13 = true;
        }
    }

    // Task 70 — a menu file-load is a full cold boot as if the file had
    // been passed with --load at startup (reset the Next to a known state,
    // then load and run). QtApp performs the reconstruct+init+load; the
    // per-format loader dispatch lives in the shared startup path.
    bool handed = false;
    if (load_file_callback_) {
        menu_load_pending_ = file;
        menu_load_shown_ = shown.toStdString();
        load_file_callback_(file, allow_experimental_nex_v13);
        handed = true;
    }
    emit load_nex_requested(path);
    return handed;
}

void MainWindow::load_finished(const std::string& file, bool ok) {
    const bool from_menu = !menu_load_pending_.empty() && file == menu_load_pending_;
    const std::string shown = menu_load_shown_.empty() ? file : menu_load_shown_;
    menu_load_pending_.clear();
    menu_load_shown_.clear();
    update_tape_status();

    // GH #27 S8 (§15.2) — the restore-time PROVENANCE line. A `.jns` can load
    // perfectly well and still have something the user must be told: the SD
    // card drifted, the ROM digests differ, the tape it named is gone.
    //
    // "It must be VISIBLE, not log-only — a user who ignores a mismatch should
    // have had to ignore it." So it goes in the status bar for every load, not
    // only a menu one: a `--load` from the command line into the GUI is the
    // same restore with the same mismatches.
    //
    // 10 s, matching `DebuggerManager::warn_state_corrupt` — the one existing
    // line in this product that reports a restore the user should think about.
    // Warnings are joined rather than shown in turn, because `showMessage`
    // REPLACES: showing three in a loop would leave only the last on screen.
    //
    // GATED ON THE FILE, not merely on the report being non-empty: the report
    // is cleared when a `.jns` load starts and not by any other loader, so
    // loading a `.jns` with warnings and then a `.nex` would otherwise show
    // the `.jns`'s warnings again against the wrong file.
    if (ok && emulator_ && jnext::is_jns_path(file) &&
        !emulator_->last_jns_report().warnings.empty()) {
        QStringList lines;
        for (const std::string& w : emulator_->last_jns_report().warnings) {
            lines << QString::fromStdString(w);
        }
        statusBar()->showMessage(tr("Snapshot restored - %1")
                                     .arg(lines.join(QStringLiteral("; "))),
                                 10000);
    }

    if (ok || !from_menu) return;
    const QString path = QString::fromStdString(shown);
    QTimer::singleShot(0, this, [this, path]() { report_load_failure(path); });
}

void MainWindow::report_load_failure(const QString& path) {
    QMessageBox::warning(this, tr("Load Failed"),
        tr("Could not load:\n%1\n\nThe file is missing, unreadable or not a valid "
           "file of its type; the log has the details.").arg(path));
}

void MainWindow::on_insert_sd() {
    // Start where the card in use lives. Only the directory is offered: the
    // saved default card is Preferences' to change, not a session swap's.
    QString current = emulator_ ? QString::fromStdString(emulator_->config().sd_card_image)
                                : QString();
    if (current.isEmpty()) current = app_config_.data().sd_card_path;
    QString path = QFileDialog::getOpenFileName(
        this, tr("Insert SD Card Image"), QFileInfo(current).absolutePath(),
        tr("Disk Images (*.img *.bin);;All Files (*)"));
    if (!path.isEmpty()) handle_sd_card_path(path);
}

void MainWindow::handle_sd_card_path(const QString& path) {
    if (!emulator_) return;
    // --sdcard-readonly is a property of the session, not of one card: an
    // inserted card is write-protected exactly when the launch card was.
    const std::string why = emulator_->request_sd_card_change(
        {path.toStdString(), emulator_->config().sd_card_readonly});
    if (why.empty()) return;
    QMessageBox::warning(this, tr("SD Card"),
        tr("The SD card cannot be changed now: %1.").arg(QString::fromStdString(why)));
}

void MainWindow::sd_card_change_finished(const QString& image, const QString& error) {
    if (error.isEmpty()) {
        const QString msg = image.isEmpty() ? tr("SD card ejected")
                                            : tr("SD card inserted: %1").arg(image);
        statusBar()->showMessage(msg, 5000);
        Log::platform()->info("status bar: {}", msg.toStdString());   // sdcard-swap-func reads it
        return;
    }
    statusBar()->showMessage(tr("SD card change failed: %1").arg(error), 5000);
    if (unattended_) return;   // logged by the emulator; nobody to answer a dialog
    QTimer::singleShot(0, this, [this, error]() {
        QMessageBox::warning(this, tr("SD Card"),
            tr("The SD card could not be changed: %1.").arg(error));
    });
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
    if (!emulator_) return;
    // The host soft reset ends an RZX recording (Emulator::end_rzx_at_reset
    // writes it first): tell the user, who otherwise believes it still runs.
    const bool        was_recording = emulator_->rzx_recorder().is_recording();
    const std::string rzx_path      = emulator_->rzx_recorder().output_path();
    emulator_->on_hotkey_f4_soft_reset();
    if (was_recording && !emulator_->rzx_recorder().is_recording()) {
        rzx_recording_ended_by_reset(QString::fromStdString(rzx_path),
                                     !emulator_->rzx_output_failed(rzx_path));
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
           "<p>Written in C++17 with Qt 6 and SDL3.</p>"
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
    handle_tape_path(path);
}

void MainWindow::handle_tape_path(const QString& path) {
    if (!emulator_) return;
    // The Tape menu's own toggle decides the mode: "Turn [real time] on with
    // Tape > Fast Load (uncheck it)" (user guide 5.5). WAV is always real time.
    const bool fast = !tape_fast_action_ || tape_fast_action_->isChecked();
    const std::string file = path.toStdString();
    bool ok = false;
    if (path.toLower().endsWith(".tzx")) {
        ok = emulator_->load_tzx(file, fast);
    } else if (path.toLower().endsWith(".wav")) {
        ok = emulator_->load_wav(file);
    } else {
        ok = emulator_->load_tap(file, fast);
    }
    // A refused file leaves the tape that was in (the loaders build the new
    // one aside and only swap it in on success).
    update_tape_status();
    if (!ok) report_load_failure(path);
}

void MainWindow::on_tape_eject() {
    if (!emulator_) return;
    emulator_->tape().eject();
    emulator_->tzx_tape().eject();
    emulator_->wav_tape().eject();
    update_tape_status();
}

void MainWindow::on_tape_rewind() {
    if (!emulator_) return;
    if (emulator_->tzx_tape().is_loaded()) {
        emulator_->tzx_tape().rewind();
    } else if (emulator_->wav_tape().is_loaded()) {
        // A WAV is always playing in real time: rewinding restarts it.
        emulator_->wav_tape().start_playback(emulator_->monotonic_tstates());
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

void MainWindow::on_tape_save_start() {
    if (!emulator_) return;
    QString filter;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save to Tape File"), app_config_.data().last_load_dir,
        tr("TZX Files (*.tzx);;WAV Files (*.wav);;TAP Files (*.tap)"), &filter,
        // Saving appends to an existing file, so there is nothing to confirm.
        QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) return;
    // A name typed without an extension takes the chosen filter's: the
    // extension is what selects the format.
    if (QFileInfo(path).suffix().isEmpty())
        path += filter.startsWith("WAV") ? ".wav" : filter.startsWith("TAP") ? ".tap" : ".tzx";
    handle_tape_save_path(path);
}

void MainWindow::handle_tape_save_path(const QString& path) {
    if (!emulator_) return;
    if (emulator_->rzx_recorder().is_recording() || emulator_->rzx_player().is_playing()) {
        QMessageBox::warning(this, tr("Save to Tape File"),
            tr("Tape saving cannot start while an RZX recording or playback runs: its "
               "SAVE trap skips the ROM routine, which a recording cannot replay."));
        return;
    }
    if (!emulator_->start_tape_save(path.toStdString())) {
        QMessageBox::warning(this, tr("Save to Tape File"),
            tr("Cannot save to %1.\n\nThe file cannot be written, or it already exists "
               "and is not a tape of that format.").arg(path));
    }
    update_tape_status();
}

void MainWindow::on_tape_save_stop() {
    if (!emulator_) return;
    emulator_->stop_tape_save();
    update_tape_status();
}

void MainWindow::update_tape_status() {
    if (!tape_label_ || !emulator_) return;

    // GH #89 — Start / Stop Saving follow the emulator, wherever saving was
    // started (--tape-save, this menu) or stopped.
    const bool saving = emulator_->tape_save_active();
    if (tape_save_start_action_) tape_save_start_action_->setEnabled(!saving);
    if (tape_save_stop_action_) {
        tape_save_stop_action_->setEnabled(saving);
        tape_save_stop_action_->setStatusTip(
            saving ? tr("Finish saving to %1")
                         .arg(QString::fromStdString(emulator_->config().tape_save_file))
                   : QString());
    }

    const auto& tap = emulator_->tape();
    const auto& tzx = emulator_->tzx_tape();
    const auto& wav = emulator_->wav_tape();

    if (tzx.is_loaded()) {
        QString name = QString::fromStdString(tzx.filename());
        tape_label_->setText(tr("Tape: %1").arg(name));
    } else if (wav.is_loaded()) {
        tape_label_->setText(tr("Tape: %1").arg(QString::fromStdString(wav.filename())));
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
    bool has_tape = tap.is_loaded() || tzx.is_loaded() || wav.is_loaded();
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

void MainWindow::on_rzx_record() {
    if (!emulator_) return;
    // Refuse before the picker: asking for a file name only to refuse it
    // afterwards wastes the user's time.
    if (const QString why = rzx_record_refusal(); !why.isEmpty()) {
        QMessageBox::warning(this, tr("RZX Recording"), why);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Record RZX File"), QString(),
        tr("RZX Files (*.rzx);;All Files (*)"));
    if (path.isEmpty()) return;
    handle_rzx_record_path(path);
}

QString MainWindow::rzx_record_refusal() const {
    if (!emulator_) return QString();
    if (emulator_->rzx_recorder().is_recording()) {
        return tr("An RZX recording to %1 is already running.\n\n"
                  "Stop it first (File > Stop RZX Recording).")
            .arg(QString::fromStdString(emulator_->rzx_recorder().output_path()));
    }
    if (emulator_->rzx_player().is_playing()) {
        return tr("An RZX recording is playing. Input cannot be recorded during "
                  "playback, so wait for it to finish.");
    }
    if (emulator_->tape_save_active()) return rzx_tape_save_refusal();
    // GH #274 — the Next is JNEXT's default machine, so this is the refusal a
    // user is most likely to meet. It has to read as "use another machine",
    // never as "recording is broken", and it is shown BEFORE the file picker.
    if (emulator_->config().type == MachineType::ZXN_ISSUE2) {
        return tr("RZX recording is not available on a ZX Spectrum Next.\n\n"
                  "An RZX carries a snapshot of the machine it starts from, and the "
                  "formats it can carry describe a 48K, 128K, Pentagon or +3 Spectrum - none of "
                  "them holds the Next's extra RAM, NextREGs, Layer 2, tilemap, sprites "
                  "or Copper.\n\n"
                  "Switch to Machine > Machine Type > 48K, 128K, Pentagon or +3 and record there. "
                  "Playing a Next recording made by an older JNEXT still works, with a "
                  "warning that it may not replay faithfully.");
    }
    return QString();
}

QString MainWindow::rzx_tape_save_refusal() const {
    return tr("Tape saving is on (--tape-save, or Tape > Start Saving), and it cannot be "
              "combined with RZX recording or playback: its SAVE trap skips the ROM "
              "routine, which a recording cannot replay.\n\nStop it first (Tape > Stop "
              "Saving).");
}

void MainWindow::handle_rzx_record_path(const QString& path) {
    if (!emulator_) return;
    if (const QString why = rzx_record_refusal(); !why.isEmpty()) {
        QMessageBox::warning(this, tr("RZX Recording"), why);
        return;
    }
    std::string reason;
    if (!RzxRecorder::can_write(path.toStdString(), reason) ||
        !emulator_->start_rzx_recording(path.toStdString())) {
        QMessageBox::warning(this, tr("RZX Recording"),
            tr("Cannot record to %1:\n%2")
                .arg(path, reason.empty() ? tr("see the log for details")
                                          : QString::fromStdString(reason)));
        return;
    }
    statusBar()->showMessage(tr("Recording RZX to %1").arg(path), 3000);
}

void MainWindow::handle_rzx_stop() {
    if (!emulator_) return;
    if (!emulator_->rzx_recorder().is_recording()) {
        statusBar()->showMessage(tr("No RZX recording is running"), 3000);
        return;
    }
    const QString path = QString::fromStdString(emulator_->rzx_recorder().output_path());
    if (emulator_->stop_rzx_recording()) {
        statusBar()->showMessage(tr("RZX recording saved to %1").arg(path), 3000);
    } else {
        QMessageBox::warning(this, tr("RZX Recording"),
            tr("The RZX recording could not be written to %1, and is lost.\n\n"
               "See the log for details.").arg(path));
    }
}

void MainWindow::rzx_recording_ended_by_reset(const QString& path, bool written) {
    if (written) {
        statusBar()->showMessage(
            tr("The reset ended the RZX recording; it was saved to %1").arg(path), 5000);
        return;
    }
    statusBar()->showMessage(
        tr("The reset ended the RZX recording, and it could not be written to %1").arg(path),
        5000);
    if (unattended_) {
        Log::platform()->error(
            "RZX: the reset ended the recording to '{}', which could not be written; "
            "unattended run, so no dialog (the exit status reports it)", path.toStdString());
        return;
    }
    Log::platform()->error(
        "RZX: the reset ended the recording to '{}', which could not be written; "
        "reporting it in a dialog", path.toStdString());
    QTimer::singleShot(0, this, [this, path]() {
        QMessageBox::warning(this, tr("RZX Recording"),
            tr("The reset ended the RZX recording, and it could not be written to "
               "%1, so it is lost.\n\nSee the log for details.").arg(path));
    });
}

void MainWindow::on_rzx_play() {
    if (!emulator_) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Play RZX File"), QString(),
        tr("RZX Files (*.rzx);;All Files (*)"));
    if (path.isEmpty()) return;
    handle_rzx_play_path(path);
}

void MainWindow::handle_rzx_play_path(const QString& path) {
    if (!emulator_) return;
    // The SAME route as File > Open of an .rzx — handle_load_path(): the
    // file is checked, then the frontend cold-boots the machine as if it had
    // been given with --load, and plays it. Playing it in place instead
    // replayed the recording on whatever state the running machine carried
    // that its snapshot does not (on the Next, a boot ROM overlay among it),
    // so the same file could replay differently depending on which menu item
    // opened it. The cold boot also ends a running recording by writing it
    // (QtApp::cold_boot() tells the user).
    //
    // The cold boot chooses the loader by extension, so a file without .rzx
    // would be taken for a NEX: refused here instead.
    if (!emulator_load_routes_to_rzx(path.toStdString())) {
        QMessageBox::warning(this, tr("Play RZX Recording"),
            tr("Cannot play %1: an RZX recording must have the .rzx extension.").arg(path));
        return;
    }
    handle_load_path(path);
}

// ---------------------------------------------------------------------------
// Screenshots (GH #18 .SCR format, GH #19 quick capture)
// ---------------------------------------------------------------------------

bool MainWindow::write_screenshot(const QString& path) {
    if (!emulator_) return false;
    // The framebuffer is canonical 640×256 in-memory; the PNG route
    // vertically doubles it to 640×512 with square pixels (G104). The .SCR
    // route ignores the framebuffer entirely and dumps the ULA screen
    // memory instead (GH #18).
    const bool ok = save_screenshot(path.toStdString(),
                                    emulator_->get_framebuffer(),
                                    emulator_->get_framebuffer_width(),
                                    emulator_->get_framebuffer_height(),
                                    emulator_->ula());
    if (ok) {
        statusBar()->showMessage(tr("Screenshot saved: %1").arg(path), 3000);
    } else {
        QMessageBox::warning(this, tr("Screenshot Error"),
            tr("Failed to save screenshot to:\n%1").arg(path));
    }
    return ok;
}

void MainWindow::on_quick_screenshot() {
    if (!emulator_) return;
    const AppConfigData& cfg = app_config_.data();
    // An empty setting means the built-in default rather than the current
    // working directory: a capture with no dialog must still land somewhere
    // the user can find, and "wherever jnext was started from" is not that.
    const QString dir = cfg.quick_screenshot_dir.isEmpty()
        ? QString::fromStdString(default_quick_screenshot_dir())
        : cfg.quick_screenshot_dir;

    const std::string path = auto_screenshot_path(
        dir.toStdString(), cfg.quick_screenshot_format, std::time(nullptr));
    if (path.empty()) {
        // auto_screenshot_path() has already logged the reason (a directory
        // it could not create, or 99 names taken inside one second).
        QMessageBox::warning(this, tr("Screenshot Error"),
            tr("Could not create a screenshot file in:\n%1").arg(dir));
        return;
    }
    write_screenshot(QString::fromStdString(path));
}

// G35: wires SnaSaver/SzxSaver/NexSaver to File > Save Snapshot... —
// closes BOOT-SNAPSAVE-01/02/03/04 in mmu_test. Format is chosen by the
// extension the user picks (or types); defaults to .sna if none/unknown.
QString MainWindow::load_filter() {
    return tr("Spectrum Files (*.nex *.jns *.sna *.szx *.z80 *.tap *.tzx *.wav *.rzx *.zip);;"
              "NEX Files (*.nex);;jnext Snapshots (*.jns);;SNA Snapshots (*.sna);;"
              "SZX Snapshots (*.szx);;Z80 Snapshots (*.z80);;TAP Files (*.tap);;"
              "TZX Files (*.tzx);;WAV Files (*.wav);;RZX Recordings (*.rzx);;"
              "ZIP Archives (*.zip);;POK Trainers (*.pok);;All Files (*)");
}

QString MainWindow::save_filter(bool next_machine) {
    // The LEADING entry is the default the dialog offers, so the order is the
    // recommendation: `.jns` on a Next because nothing else can represent one,
    // `.sna` elsewhere because that is what other emulators read (§15.2).
    return next_machine
        ? tr("jnext snapshot (*.jns);;Spectrum snapshot (*.sna);;"
             "ZX-State snapshot (*.szx);;NEX program (*.nex);;All Files (*)")
        : tr("Spectrum snapshot (*.sna);;jnext snapshot (*.jns);;"
             "ZX-State snapshot (*.szx);;NEX program (*.nex);;All Files (*)");
}

// The `.jns` write path behind both File > Save Snapshot... and Quick Save:
// save_jns_file() (which advances to a frame boundary itself), the warnings,
// the failure dialog and the success line.
bool MainWindow::save_jns_with_feedback(const QString& path, const QString& ok_msg,
                                        int ok_ms) {
    if (!emulator_->save_jns_file(path.toStdString())) {
        const QString why = QString::fromStdString(emulator_->last_jns_error());
        statusBar()->showMessage(tr("Snapshot save failed: %1").arg(why), 5000);
        if (!unattended_)
            QMessageBox::warning(this, tr("Save Snapshot"), why);
        return false;
    }
    for (const std::string& w : emulator_->last_jns_report().warnings) {
        statusBar()->showMessage(QString::fromStdString(w), 8000);
    }
    statusBar()->showMessage(ok_msg, ok_ms);
    Log::platform()->info("status bar: {}", ok_msg.toStdString());
    return true;
}

// GH #321 — File > Quick Save Snapshot (Alt+Shift+F5).
void MainWindow::on_quick_save() {
    if (host_action_callback_) { host_action_callback_(QuickAction::Save); return; }
    quick_save_now();
}

void MainWindow::quick_save_now() {
    if (!emulator_) return;
    const std::string dir = default_quick_snapshot_dir();
    const std::string path = auto_quick_snapshot_path(dir, std::time(nullptr));
    if (path.empty()) {
        const QString msg = tr("Could not create a snapshot file in:\n%1")
                                .arg(QString::fromStdString(dir));
        statusBar()->showMessage(msg, 5000);
        if (!unattended_) QMessageBox::warning(this, tr("Quick Save"), msg);
        return;
    }
    const QString qpath = QString::fromStdString(path);
    if (save_jns_with_feedback(qpath, tr("Quick saved: %1").arg(qpath), 5000)) {
        // --quick-snapshot-keep overrides the Preferences setting for the run.
        const EmulatorConfig& ec = emulator_->config();
        prune_quick_snapshots(dir, ec.quick_snapshot_keep_cli
                                       ? ec.quick_snapshot_keep
                                       : app_config_.data().quick_snapshot_keep);
    }
}

// GH #321 — File > Quick Load Snapshot (Alt+Shift+F9): the newest .jns in the quick
// folder, through the same route as File > Load.
void MainWindow::on_quick_load() {
    if (host_action_callback_) { host_action_callback_(QuickAction::Load); return; }
    quick_load_now();
}

void MainWindow::quick_load_now() {
    if (!emulator_) return;
    const std::string dir = default_quick_snapshot_dir();
    std::string why;
    const std::string path = latest_quick_snapshot(dir, why);
    if (path.empty()) {
        const QString msg = tr("Quick Load: no snapshot in %1")
                                .arg(QString::fromStdString(dir));
        statusBar()->showMessage(msg, 5000);
        Log::platform()->info("status bar: {}", msg.toStdString());
        return;
    }
    // Refuse a bad file BEFORE the cold boot throws the session away. Quick
    // Load asks for no dialog, so this is a status-bar line (and the log).
    if (!emulator_->probe_jns_file(path, why)) {
        const QString msg = tr("Quick Load: refused %1: %2")
                                .arg(QString::fromStdString(path), QString::fromStdString(why));
        statusBar()->showMessage(msg, 8000);
        Log::platform()->error("status bar: {}", msg.toStdString());
        return;
    }
    const QString msg = tr("Quick loading: %1").arg(QString::fromStdString(path));
    statusBar()->showMessage(msg, 5000);
    Log::platform()->info("status bar: {}", msg.toStdString());
    handle_load_path(QString::fromStdString(path), true);   // probed just above
}

void MainWindow::refuse_jns(const QString& path, const QString& why) {
    const QString msg = tr("Cannot load %1: %2.").arg(path, why);
    statusBar()->showMessage(msg, 5000);
    Log::platform()->error("status bar: {}", msg.toStdString());
    if (!unattended_) QMessageBox::warning(this, tr("Load Failed"), msg);
}

void MainWindow::on_save_snapshot() {
    if (!emulator_) return;
    // GH #27 S8 (§15.2) — `.jns` leads the filter on a Next and `.sna` leads
    // it elsewhere, because they are the right default on their own machines:
    // a `.jns` is the only format that can represent a Next at all, and on a
    // 48K/128K/+3 a `.sna` is what other emulators read.
    const bool next_machine =
        emulator_->config().type == MachineType::ZXN_ISSUE2;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Snapshot"), QString(), save_filter(next_machine));
    if (path.isEmpty()) return;
    if (!path.endsWith(".sna", Qt::CaseInsensitive) &&
        !path.endsWith(".szx", Qt::CaseInsensitive) &&
        !path.endsWith(".nex", Qt::CaseInsensitive) &&
        !path.endsWith(".jns", Qt::CaseInsensitive)) {
        // The DEFAULT SUFFIX follows the machine for the same reason the
        // filter order does. It is still suffix-driven rather than
        // filter-row-driven, which is this slot's existing behaviour and is
        // deliberately left alone here.
        path += next_machine ? ".jns" : ".sna";
    }

    // GH #27 S6 (design §10.2 P7, §15.2) — ALWAYS ADVANCE, NEVER REFUSE.
    // A snapshot may only be taken at a frame boundary, and the debugger
    // breaks MID-frame, which is precisely when a developer reaches for this
    // menu item. Complete the in-flight frame through the ordinary path
    // first; the machine is then up to one frame past where the user paused,
    // which the status bar says once rather than leaving it silent.
    if (emulator_->advance_to_frame_boundary()) {
        statusBar()->showMessage(
            tr("Paused mid-frame: advanced to the next frame boundary to "
               "save from."), 4000);
    }

    // `.jns` assembles a manifest, an SD identity and blob declarations rather
    // than producing one flat buffer, so it writes its own file — and it is
    // the only arm here that can report a REASON when it fails.
    if (path.endsWith(".jns", Qt::CaseInsensitive)) {
        save_jns_with_feedback(path, tr("Snapshot saved: %1").arg(path), 3000);
        return;
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
        // GH #274 — .sna is the 48K form only, which cannot represent a Next:
        // SnaSaver::save() refuses it and says why, exactly as the .szx arm
        // above does. Without the reason the user saw only the generic
        // empty-buffer warning below.
        std::string sna_error;
        bytes = SnaSaver::save(*emulator_, &sna_error);
        if (bytes.empty() && !sna_error.empty()) {
            QMessageBox::warning(this, tr("Save Snapshot"),
                QString::fromStdString(sna_error));
            return;
        }
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

    set_mouse_release_method(cfg.mouse_release);   // GH #365
    set_scale(cfg.window_scale);
    // One effect: resolve the exclusivity before touching the widget. The shader
    // (any style, Lite = Soft Scanlines) wins over the software Scanlines. In a build
    // without it a saved Soft Scanlines choice is the nearest thing it has: Scanlines.
    const bool soft = cfg.crt_shader && cfg.crt_shader_style == crt_shader::Style::Lite;
    emulator_widget_->set_crt_shader_style(cfg.crt_shader_style);
    if (cfg.crt_shader && set_crt_shader(true)) {
        // on; also cleared the scanline mode
    } else {
        set_crt_shader(false);
        set_crt_filter(cfg.crt_filter || soft);
    }

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
        tr("Switching to %1 restarts the emulator - anything currently "
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

    // Task 79 / GH #311 — live-apply the per-connector input sources and the
    // assigned controllers. The dialog already enforces the one-cursor and
    // one-controller rules, so these calls never conflict; sync the
    // Input-menu checkmarks to the (now effective) state.
    if (emulator_) {
        emulator_->set_joystick_source(0, cfg.joy_source[0]);
        emulator_->set_joystick_source(1, cfg.joy_source[1]);
        for (int i = 0; i < 2; ++i) {
            emulator_->set_joystick_device(i, JoyDeviceRef{ cfg.joy_device[i].toStdString(),
                                                            cfg.joy_device_name[i].toStdString() });
        }
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

    // NextPi is started (and, the first time, downloaded) by main() before
    // this window exists, and QEMU outlives cold boots on purpose, so it has no
    // live toggle either way: the tick takes effect at the next launch. Said
    // here for the same reason as the ESP line above.
    if (emulator_ && (emulator_->pi_uart() != nullptr) != cfg.nextpi_enabled) {
        Log::platform()->info(
            "Preferences: NextPi {} saved but NOT applied to the running "
            "machine; it takes effect the next time jnext starts.",
            cfg.nextpi_enabled ? "enabled" : "disabled");
    }

    // GH #1 — the debugger key bindings apply LIVE, in both windows. This is
    // the ONLY place that makes them live: the forwarding in keyPressEvent()
    // and the push into the debugger window both read debug_keys_, so an
    // Apply can never leave the two windows on different maps.
    debug_keys_ = cfg.debug_keys;
    push_debug_keymap();

    // cfg.silent has no live setter (the SDL audio device is opened once at
    // QtApp::init() time and MainWindow has no handle to it) — persisted
    // only, applied on next launch. Host output gain is deliberately different:
    // apply_startup_config() updates the mixer immediately.
}

void MainWindow::push_debug_keymap() {
#ifdef ENABLE_DEBUGGER
    if (!debugger_mgr_) return;
    // Through the MANAGER, not straight at the window: the window is created
    // lazily, so a rebind made before the debugger was ever opened must be
    // remembered rather than dropped (GH #1).
    debugger_mgr_->set_keymap(debug_keys_);
#endif
}

void MainWindow::on_open_preferences() {
    // GH #1 review follow-up — hand the dialog the combinations THIS window
    // already answers to, harvested from the real QActions rather than
    // hand-listed, so a menu item added later is picked up with no edit here.
    PreferencesDialog dlg(app_config_.data(), this, app_config_.debug_key_issues(),
                          harvest_host_chords(this),
                          joy_device_provider_ ? joy_device_provider_()
                                               : std::vector<JoyDeviceInfo>{});
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

        // The pointer-release gesture (GH #365; by default Ctrl+Alt, the
        // convention users know from VirtualBox / VMware / QEMU — Alt+Command
        // on macOS, where Qt swaps the keys). Checked before anything else so
        // it works even if the guest is busy. Ctrl+Alt arrives here as "this
        // one down, the other already held" in either order, and is
        // deliberately NOT consumed: Keyboard::set_key() treats Alt as a host
        // modifier (never a ZX key) and swallowing it would leave alt_held_
        // false, so the Alt-compounds (EDIT = Alt+E, GRAPH = Alt+G, CAPS LOCK
        // = Alt+C) would silently resolve to their unmodified meaning until
        // Alt was released and pressed again. Esc IS consumed (press, release).
        const pointer_capture::Verdict rv = release_filter_.on_key(
            qt_host_key(key), true, modifiers & Qt::ControlModifier, modifiers & Qt::AltModifier,
            mouse_captured_);
        if (rv.release) set_mouse_captured(false);
        if (rv.consume) {
            event->accept();
            return;
        }

#ifdef ENABLE_DEBUGGER
        // When the debugger is enabled, intercept its execution-control keys.
        //
        // GH #1 — driven from the USER'S KEYMAP, not from five hard-coded
        // F-keys. Without that a rebind half-applies: the new combination
        // works in the debugger window while the old one keeps working — and
        // keeps being swallowed — here. Exactly FIVE actions are forwarded,
        // the same five this block has always forwarded. Notably not
        // trace_toggle, whose default F2 is this window's scale cycler.
        //
        // Modifiers are matched EXACTLY now; this block used to switch on the
        // key alone, so Shift+F7 in this window triggered Step Over. It no
        // longer does, which is what lets Shift+F7 mean Step Back.
        if (debugger_mgr_ && debugger_mgr_->is_enabled()) {
            using namespace jnext::dbgkeys;
            const Combo pressed = combo_from_event(key, modifiers);
            const Keymap& km = debug_keys_;
            if (pressed.bound()) {
                // GH #223: the debugger owns Run whenever it is enabled,
                // whether or not the machine is paused. on_run() is now a
                // deliberate no-op on a running machine, and the key is STILL
                // consumed — do not "fix" this into a fall-through.
                //
                // A fall-through would be INERT TODAY: it would reach
                // `case Qt::Key_F5` further down and drive the EmuFnKeys FSM,
                // but jnext implements no F5 side effect at all —
                // emu_fnkeys.h:68-69 puts F5/F6 out of scope for G132 (tracked
                // under G147) and fire_mf_side_effects() dispatches F2/F3/F7/F8
                // only — so no NextREG is written. On real HARDWARE F5 is the
                // expansion-bus enable hotkey (zxnext.vhd:6344 -> :2190
                // `nr_80_expbus(7) <= '1'`). Consuming the key keeps "F5 while
                // running does nothing" true by construction rather than by
                // G147 being unfinished, which is what this defends. Owner
                // decision: F5 while running is fully ignored. GH223-03 fails
                // on a fall-through.
                if (pressed == km.combo(Action::Run)) {
                    debugger_mgr_->on_run();
                    event->accept();
                    return;
                }
                if (pressed == km.combo(Action::StepInto)) {
                    debugger_mgr_->on_step_into();
                    event->accept();
                    return;
                }
                if (pressed == km.combo(Action::StepOver)) {
                    debugger_mgr_->on_step_over();
                    event->accept();
                    return;
                }
                if (pressed == km.combo(Action::StepOut)) {
                    debugger_mgr_->on_step_out();
                    event->accept();
                    return;
                }
                if (pressed == km.combo(Action::Pause)) {
                    debugger_mgr_->on_pause();
                    event->accept();
                    return;
                }
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
        // so the F4 case below sees a plain F4 only when no shortcut matched,
        // and the modified presses: Shift/Ctrl+F4 reset nothing, and F4 with
        // Alt held is File > Quit (GH #380). Alt+F1 resets nothing either.
        case Qt::Key_F1:
            if (emulator_ && !modifiers.testFlag(Qt::ShiftModifier)
                          && !modifiers.testFlag(Qt::ControlModifier)
                          && !modifiers.testFlag(Qt::AltModifier)) {
                emulator_->on_hotkey_f1_hard_reset();
                event->accept();
                return;
            }
            break;
        case Qt::Key_F4:
            // GH #380 — any F4 with Alt held is File > Quit, whatever else is held
            // and with the mouse captured or not. An exact Alt+F4 in an active
            // window is taken earlier, by the shortcut map, through the same
            // action; what lands here is the rest: Ctrl+Alt+F4 (AltGr+F4 on
            // Windows), Alt+Shift+F4, Meta+Alt+F4, a plain Alt+F4 with the menu
            // bar hidden (fullscreen) or the window not active. It never reaches
            // the guest, and needs no emulator.
            if (modifiers.testFlag(Qt::AltModifier)) {
                if (quit_action_) quit_action_->trigger();
                event->accept();
                return;
            }
            if (emulator_ && !modifiers.testFlag(Qt::ShiftModifier)
                          && !modifiers.testFlag(Qt::ControlModifier)) {
                on_soft_reset();   // the same dispatcher as Machine > Soft Reset
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
        // GH #365 — the release of an Esc that released the mouse.
        const Qt::KeyboardModifiers mods = event->modifiers();
        if (release_filter_.on_key(qt_host_key(key), false, mods & Qt::ControlModifier,
                                   mods & Qt::AltModifier, mouse_captured_).consume) {
            event->accept();
            return;
        }
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

void MainWindow::focusOutEvent(QFocusEvent* event) {
    // GH #268 — the keyboard has gone somewhere else, so every key-up still
    // owed to us will be delivered THERE. Release whatever the guest is
    // holding now, while we still know what that is.
    //
    // This is the general form of the defect: the popup guard in handle_key()
    // stops a NEW press from being stranded, but by the time a popup opens the
    // user may already be holding keys — and the same is true of Alt+Tab, of a
    // modal dialog, and of a menu opened with the mouse. The worst case is the
    // one that was reported: Alt+F opens the File menu and BOTH the F key-up
    // and the Alt key-up go to the menu, leaving Keyboard::alt_held_ true for
    // the rest of the session. After that every E/G/C resolves to its ALT
    // variant — EDIT / GRAPH / CAPS LOCK (keyboard.cpp:248-250) — so those
    // three letters silently stop appearing while every other letter still
    // types. "press A I got A, press G nothing is shown", exactly.
    release_filter_.reset();   // GH #365 — a swallowed Esc's release goes elsewhere
    if (keyboard_lost_callback_) keyboard_lost_callback_();
    QMainWindow::focusOutEvent(event);
}

void MainWindow::handle_key(QKeyEvent* event, bool pressed) {
    // Ignore auto-repeat (ZX keyboard matrix doesn't auto-repeat).
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    // GH #268 — while a Qt popup owns the keyboard, the guest gets NOTHING.
    //
    // A popup (a menu, a combo drop-down) is a window of its own, so
    // QApplication::notify stops propagating at it — but QMenu::keyPressEvent
    // deliberately FORWARDS an unmatched key-down back to the menu bar
    // (qmenu.cpp), which then propagates up to this window, while the matching
    // key-UP is simply swallowed. Measured on the product: with the Tape menu
    // open, one tap of `g` delivered a key-down here and no key-up at all, so
    // the guest was left holding G and the 48K ROM's own auto-repeat filled the
    // BASIC line with it. Feeding a press whose release cannot arrive is the
    // bug; the user is driving a menu, so dropping it is also what they meant.
    if (QApplication::activePopupWidget()) {
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

// Put the captured pointer back on the viewport centre. macOS cannot use
// QCursor::setPos for this (issue #303 — see gui/mac_cursor_warp.h).
void warp_pointer(const QPoint& p) {
#ifdef Q_OS_MACOS
    mac_cursor::warp(p.x(), p.y());
#else
    QCursor::setPos(p);
#endif
}

} // anonymous namespace

QPoint MainWindow::viewport_centre_global() const {
    const QWidget* w = emulator_widget_ ? static_cast<const QWidget*>(emulator_widget_)
                                        : static_cast<const QWidget*>(this);
    return w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2));
}

int MainWindow::capture_margin() const {
    // Far enough that a stale pre-warp event and a post-warp one cannot be
    // confused (pointer_capture.h), near enough to stay on the viewport.
    const QWidget* w = emulator_widget_ ? static_cast<const QWidget*>(emulator_widget_)
                                        : static_cast<const QWidget*>(this);
    return qMax(8, qMin(w->width(), w->height()) / 4);
}

QString MainWindow::release_hint_text() const {
    return ::release_hint_text(release_filter_.method());
}

void MainWindow::update_capture_status_tip() {
    if (!capture_mouse_action_) return;
    capture_mouse_action_->setStatusTip(
        tr("Confine the host pointer so the Kempston mouse can move freely "
           "(click the screen to capture, %1 to release)")
            .arg(release_hint_text()));
}

void MainWindow::set_mouse_release_method(pointer_capture::ReleaseMethod m) {
    release_filter_.set_method(m);
    update_capture_status_tip();
    // No captured-title refresh: the only callers are startup and Preferences,
    // and a modal dialog has already released the capture (changeEvent).
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
        // macOS and Wayland cannot rely on the warp landing at once (issue
        // #303): macOS always, Wayland by the QPA platform in use.
#ifdef Q_OS_MACOS
        capture_deferred_ = true;
#else
        capture_deferred_ = pointer_capture::deferred_warp_platform(
            QGuiApplication::platformName().toStdString());
#endif
        Log::input()->debug("pointer capture: {} policy (QPA platform '{}')",
                            capture_deferred_ ? "deferred-warp" : "centre",
                            QGuiApplication::platformName().toStdString());
        const QPoint centre = viewport_centre_global();
        if (capture_deferred_) {
            const QPoint at = QCursor::pos();
            if (deferred_policy_.begin(at.x(), at.y(), centre.x(), centre.y(), capture_margin()))
                warp_pointer(centre);
        } else {
            centre_policy_.begin();
            warp_pointer(centre);
        }
        // The status-bar message times out; the title carries the way out for
        // as long as the pointer is actually held.
        setWindowTitle(base_window_title_ + tr(" - %1 to release mouse").arg(release_hint_text()));
        statusBar()->showMessage(tr("Mouse captured - %1 to release").arg(release_hint_text()), 4000);
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
    const pointer_capture::Motion m = capture_deferred_
        ? deferred_policy_.on_motion(global.x(), global.y(), centre.x(), centre.y(),
                                     capture_margin())
        : centre_policy_.on_motion(global.x(), global.y(), centre.x(), centre.y());
    if (m.forward) mouse_dispatcher_->handle_motion(m.dx, m.dy);
    if (m.recentre) warp_pointer(centre);
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
    // GH #365 — a middle click that releases the mouse is consumed whole: it
    // must not reach handle_button(), whose release (uncaptured by then) would
    // never be forwarded and would leave Kempston bit 2 stuck.
    if (release_filter_.on_button(qt_host_button(event->button()), true, mouse_captured_).release) {
        set_mouse_captured(false);
        event->accept();
        return;
    }
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
    // y > 0 = wheel rolled away from user (matches SDL_EVENT_MOUSE_WHEEL convention
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
    // Same for an RZX recording (latched by Emulator::rzx_output_failed(),
    // read by QtApp::shutdown()), and the user is TOLD here, while the window
    // still exists: a recording lost at exit is otherwise one log line.
    if (emulator_ && emulator_->rzx_recorder().is_recording()) handle_rzx_stop();

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
