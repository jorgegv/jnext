#pragma once

#include <QMainWindow>
#include <functional>
#include <SDL2/SDL.h>

class EmulatorWidget;
enum class ScaleMode;

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

    /// Toggle between windowed and fullscreen mode.
    void toggle_fullscreen();

    /// Set integer scale factor (1-4) and resize window accordingly.
    /// Only effective in windowed mode.
    void set_scale(int factor);

    /// Cycle through scale factors 1x -> 2x -> 3x -> 4x -> 1x.
    /// Only effective in windowed mode.
    void cycle_scale();

    /// Viewport setting accessors for menu integration (Agent C).
    void set_scale_mode(ScaleMode mode);
    ScaleMode scale_mode() const;

    void set_crt_filter(bool enabled);
    bool crt_filter() const;

    bool is_fullscreen() const { return is_fullscreen_; }
    int current_scale() const { return current_scale_; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void handle_key(QKeyEvent* event, bool pressed);

    EmulatorWidget* emulator_widget_ = nullptr;
    KeyCallback key_callback_;

    bool is_fullscreen_ = false;
    int current_scale_ = 2;  ///< Default 2x scale (640x512).

    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
};
