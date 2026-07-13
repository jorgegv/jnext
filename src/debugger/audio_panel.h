#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>

class Emulator;

/// Debugger panel showing AY register state and audio source controls.
class AudioPanel : public QWidget {
    Q_OBJECT
public:
    explicit AudioPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current audio state.
    void refresh();

    QSize sizeHint() const override { return QSize(400, 600); }
    QSize minimumSizeHint() const override { return QSize(350, 580); }

private:
    void create_ui();

    /// Collect the five source checkboxes into an AudioMute mask (unchecked =
    /// muted) and push it to the emulator. Called on every toggle.
    void apply_source_mutes();

    Emulator* emulator_;
    QTableWidget* ay_table_ = nullptr;

    // Source checkboxes: CHECKED = audible, UNCHECKED = muted.
    QCheckBox* mute_ay0_ = nullptr;
    QCheckBox* mute_ay1_ = nullptr;
    QCheckBox* mute_ay2_ = nullptr;
    QCheckBox* mute_dac_ = nullptr;
    QCheckBox* mute_beeper_ = nullptr;

    // Info labels
    QLabel* turbosound_label_ = nullptr;
    QLabel* ay_ym_label_ = nullptr;
    QLabel* stereo_label_ = nullptr;
};
