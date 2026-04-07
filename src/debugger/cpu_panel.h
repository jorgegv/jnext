#pragma once

#include <QWidget>
#include <QLabel>

class Emulator;

/// CPU registers, flags, and MMU panel.
class CpuPanel : public QWidget {
    Q_OBJECT
public:
    explicit CpuPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current CPU state and MMU.
    void refresh();

    /// Set paused state — when not paused, panel shows gray overlay and skips updates.
    void set_paused(bool paused);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void create_ui();

    Emulator* emulator_;
    bool paused_ = false;

    // Register value labels (hex display)
    QLabel* reg_af_ = nullptr;
    QLabel* reg_bc_ = nullptr;
    QLabel* reg_de_ = nullptr;
    QLabel* reg_hl_ = nullptr;
    QLabel* reg_af2_ = nullptr;
    QLabel* reg_bc2_ = nullptr;
    QLabel* reg_de2_ = nullptr;
    QLabel* reg_hl2_ = nullptr;
    QLabel* reg_ix_ = nullptr;
    QLabel* reg_iy_ = nullptr;
    QLabel* reg_sp_ = nullptr;
    QLabel* reg_pc_ = nullptr;
    QLabel* reg_i_ = nullptr;
    QLabel* reg_r_ = nullptr;
    QLabel* reg_iff_ = nullptr;
    QLabel* reg_im_ = nullptr;
    QLabel* reg_halted_ = nullptr;
    QLabel* ula_screen_ = nullptr;

    // Individual flag labels
    QLabel* flag_s_ = nullptr;
    QLabel* flag_z_ = nullptr;
    QLabel* flag_h_ = nullptr;
    QLabel* flag_pv_ = nullptr;
    QLabel* flag_n_ = nullptr;
    QLabel* flag_c_ = nullptr;

};
