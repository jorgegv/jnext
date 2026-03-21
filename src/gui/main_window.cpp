#include "gui/main_window.h"
#include "gui/emulator_widget.h"
#include <QKeyEvent>
#include <QMenuBar>
#include <QStatusBar>

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

} // anonymous namespace

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("JNEXT \u2014 ZX Spectrum Next Emulator");
    setMinimumSize(NATIVE_W, NATIVE_H);

    // Central widget: the emulator display.
    emulator_widget_ = new EmulatorWidget(this);
    setCentralWidget(emulator_widget_);

    // Empty menu bar (menus will be added by Agent C).
    menuBar();

    // Empty status bar.
    statusBar();

    // Ensure the window receives key events even when focus is on a child widget.
    setFocusPolicy(Qt::StrongFocus);

    // Apply default 2x scale.
    set_scale(current_scale_);
}

void MainWindow::toggle_fullscreen() {
    if (is_fullscreen_) {
        showNormal();
        // Restore windowed size based on current scale factor.
        set_scale(current_scale_);
        is_fullscreen_ = false;
    } else {
        showFullScreen();
        is_fullscreen_ = true;
    }
}

void MainWindow::set_scale(int factor) {
    if (factor < 1) factor = 1;
    if (factor > 4) factor = 4;
    current_scale_ = factor;

    if (!is_fullscreen_) {
        // Resize window to fit the scaled framebuffer plus menu/status bar chrome.
        // We set a fixed size on the central widget temporarily to force the layout,
        // then allow it to expand again.
        int target_w = NATIVE_W * factor;
        int target_h = NATIVE_H * factor;

        // Account for menu bar and status bar height.
        int chrome_h = menuBar()->sizeHint().height() + statusBar()->sizeHint().height();

        resize(target_w, target_h + chrome_h);
    }
}

void MainWindow::cycle_scale() {
    if (is_fullscreen_) return;
    int next = (current_scale_ % 4) + 1;  // 1->2->3->4->1
    set_scale(next);
}

void MainWindow::set_scale_mode(ScaleMode mode) {
    emulator_widget_->set_scale_mode(mode);
}

ScaleMode MainWindow::scale_mode() const {
    return emulator_widget_->scale_mode();
}

void MainWindow::set_crt_filter(bool enabled) {
    emulator_widget_->set_crt_filter(enabled);
}

bool MainWindow::crt_filter() const {
    return emulator_widget_->crt_filter();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Handle emulator UI keys first (F11 fullscreen, F2 scale cycle).
    if (!event->isAutoRepeat()) {
        switch (event->key()) {
        case Qt::Key_F11:
            toggle_fullscreen();
            event->accept();
            return;
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
    // F11 and F2 release events can be silently consumed.
    if (!event->isAutoRepeat()) {
        if (event->key() == Qt::Key_F11 || event->key() == Qt::Key_F2) {
            event->accept();
            return;
        }
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
