#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QActionGroup>
#include <functional>
#include <SDL2/SDL.h>

class Emulator;
class EmulatorWidget;
class QTimer;

/// Main emulator window — QMainWindow shell with emulator viewport, menu bar,
/// toolbar, and status bar.  Keyboard events are dispatched to a configurable callback.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Access the central emulator display widget.
    EmulatorWidget* emulator_widget() { return emulator_widget_; }

    /// Set the emulator pointer for direct callbacks.
    void set_emulator(Emulator* emu) { emulator_ = emu; }

    /// Set the callback for key events.
    /// Signature: (SDL_Scancode scancode, bool pressed).
    using KeyCallback = std::function<void(SDL_Scancode, bool)>;
    void set_key_callback(KeyCallback cb) { key_callback_ = std::move(cb); }

    /// Update status bar information.  Called once per second from the frame timer.
    void update_status(double fps, int cpu_speed_idx);

signals:
    /// Emitted when a scale factor is selected from the View menu.
    void scale_requested(int factor);

    /// Emitted when a NEX file should be loaded.
    void load_nex_requested(const QString& path);

    /// Emitted when an SD card image path is selected (informational; requires restart).
    void sd_card_selected(const QString& path);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void handle_key(QKeyEvent* event, bool pressed);
    void create_menus();
    void create_toolbar();
    void create_statusbar();

    // Menu action slots
    void on_load_nex();
    void on_mount_sd();
    void on_reset();
    void on_cpu_speed(int speed_idx);
    void on_scale(int factor);
    void on_fullscreen(bool checked);
    void on_about();

    EmulatorWidget* emulator_widget_ = nullptr;
    Emulator*       emulator_        = nullptr;
    KeyCallback     key_callback_;

    // Status bar labels
    QLabel* fps_label_     = nullptr;
    QLabel* speed_label_   = nullptr;
    QLabel* machine_label_ = nullptr;

    // Toolbar speed indicator
    QLabel* toolbar_speed_label_ = nullptr;

    // CPU speed action group
    QActionGroup* speed_group_ = nullptr;

    // Scale action group
    QActionGroup* scale_group_ = nullptr;

    // Fullscreen action
    QAction* fullscreen_action_ = nullptr;

    // Scale mode action group
    QActionGroup* scale_mode_group_ = nullptr;

    // CRT filter action
    QAction* crt_filter_action_ = nullptr;
};
