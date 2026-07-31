#pragma once

#include <QDialog>

#include "gui/app_config.h"

class QComboBox;
class QSpinBox;
class QSlider;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

/// Preferences dialog (Task 66 — Configurability).
///
/// Five tabs: **Startup** (machine type, CPU speed, emulator speed, the
/// issue-#35 too-slow degradation policy, window scale, CRT filter, start
/// muted, tape fast-load), **Input** (Task 79 —
/// per-connector Joy 1 / Joy 2 host source: SDL gamepad vs cursor keys),
/// **Audio** (host output gain), **Network** (GH #25 — the emulated ESP-01
/// and its hostname allowlist), and **Paths** (last load directory, default
/// SD card image, screenshot directory).
///
/// The dialog only edits an in-memory copy; OK/Apply emit apply_requested()
/// with the edited data so the caller (MainWindow) decides how to persist it
/// (AppConfig::save()) and which fields to live-apply to the running machine.
///
/// EVERY PERSISTED FIELD MUST HAVE A CONTROL HERE. collect() builds a fresh
/// AppConfigData, so a field this dialog does not read back is silently reset
/// to its default the moment the user presses OK. That is exactly what
/// happened to the two ESP fields while they had no page (GH #25): a
/// hand-edited `[esp]` section was wiped by opening Preferences and pressing
/// OK, with nothing said. The Network tab closes that hole for them.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const AppConfigData& current, QWidget* parent = nullptr);

signals:
    /// Emitted on OK (then the dialog closes) and on Apply (dialog stays open).
    void apply_requested(const AppConfigData& cfg);

private:
    QWidget* build_startup_tab();
    QWidget* build_input_tab();
    QWidget* build_audio_tab();
    QWidget* build_network_tab();
    QWidget* build_paths_tab();
    AppConfigData collect() const;
    void browse_directory(QLineEdit* target);
    void browse_sd_image(QLineEdit* target);

    QComboBox* machine_combo_    = nullptr;
    QComboBox* cpu_speed_combo_  = nullptr;
    QSpinBox*  emu_speed_spin_   = nullptr;
    QComboBox* when_slow_combo_  = nullptr;   // issue #35
    QComboBox* scale_combo_      = nullptr;
    QCheckBox* crt_check_        = nullptr;
    QCheckBox* silent_check_     = nullptr;
    QCheckBox* tape_fast_check_  = nullptr;

    QComboBox* joy1_source_combo_ = nullptr;   // Task 79
    QComboBox* joy2_source_combo_ = nullptr;

    QSlider* audio_gain_slider_ = nullptr;
    QSlider* audio_beeper_gain_slider_ = nullptr;
    QSlider* audio_ay_gain_slider_[3] = {nullptr, nullptr, nullptr};
    QSlider* audio_dac_gain_slider_ = nullptr;

    QCheckBox*      esp_enabled_check_ = nullptr;   // GH #25
    QPlainTextEdit* esp_hosts_edit_    = nullptr;

    QLineEdit* last_load_dir_edit_  = nullptr;
    QLineEdit* sd_card_path_edit_   = nullptr;
    QLineEdit* screenshot_dir_edit_ = nullptr;
};
