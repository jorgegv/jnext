#pragma once

#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>

class Emulator;

/// Debugger panel showing raster position, layer controls, and ULA palette.
class VideoPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current video state.
    void refresh();

    QSize sizeHint() const override { return QSize(250, 400); }

private:
    void create_ui();

    Emulator* emulator_;

    // Raster position labels
    QLabel* hc_label_ = nullptr;
    QLabel* vc_label_ = nullptr;

    // Layer visibility checkboxes (display-only for now)
    QCheckBox* ula_check_ = nullptr;
    QCheckBox* l2_check_ = nullptr;
    QCheckBox* tm_check_ = nullptr;
    QCheckBox* spr_check_ = nullptr;

    // Layer priority dropdown
    QComboBox* priority_combo_ = nullptr;

    // Palette swatch widget (custom-painted)
    QWidget* palette_widget_ = nullptr;
};
