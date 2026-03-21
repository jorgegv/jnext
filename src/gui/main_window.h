#pragma once

#include <QMainWindow>
#include <functional>
#include <SDL2/SDL.h>

class EmulatorWidget;

/// Main emulator window — QMainWindow shell with emulator viewport, menu bar,
/// and status bar.  Keyboard events are dispatched to a configurable callback.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Access the central emulator display widget.
    EmulatorWidget* emulator_widget() { return emulator_widget_; }

    /// Set the callback for key events.
    /// Signature: (SDL_Scancode scancode, bool pressed).
    using KeyCallback = std::function<void(SDL_Scancode, bool)>;
    void set_key_callback(KeyCallback cb) { key_callback_ = std::move(cb); }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void handle_key(QKeyEvent* event, bool pressed);

    EmulatorWidget* emulator_widget_ = nullptr;
    KeyCallback key_callback_;
};
