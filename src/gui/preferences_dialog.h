#pragma once

#include <QDialog>

#include "gui/app_config.h"

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;

/// Preferences dialog (Task 66 — Configurability).
///
/// Two tabs: **Startup** (machine type, CPU speed, emulator speed, window
/// scale, CRT filter, start muted, tape fast-load) and **Paths** (last load
/// directory, default SD card image, screenshot directory). No Input tab:
/// there is no existing runtime keyboard/joystick-remapping setting to bind
/// one to (redefinable keys is a separate, unimplemented Phase 11 item).
///
/// The dialog only edits an in-memory copy; OK/Apply emit apply_requested()
/// with the edited data so the caller (MainWindow) decides how to persist it
/// (AppConfig::save()) and which fields to live-apply to the running machine.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const AppConfigData& current, QWidget* parent = nullptr);

signals:
    /// Emitted on OK (then the dialog closes) and on Apply (dialog stays open).
    void apply_requested(const AppConfigData& cfg);

private:
    QWidget* build_startup_tab();
    QWidget* build_paths_tab();
    AppConfigData collect() const;
    void browse_directory(QLineEdit* target);
    void browse_sd_image(QLineEdit* target);

    QComboBox* machine_combo_    = nullptr;
    QComboBox* cpu_speed_combo_  = nullptr;
    QSpinBox*  emu_speed_spin_   = nullptr;
    QComboBox* scale_combo_      = nullptr;
    QCheckBox* crt_check_        = nullptr;
    QCheckBox* silent_check_     = nullptr;
    QCheckBox* tape_fast_check_  = nullptr;

    QLineEdit* last_load_dir_edit_  = nullptr;
    QLineEdit* sd_card_path_edit_   = nullptr;
    QLineEdit* screenshot_dir_edit_ = nullptr;
};
