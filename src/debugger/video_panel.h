#pragma once

#include <QWidget>
#include <QLabel>

class Emulator;

/// Debugger panel showing raster position, layer state, priority and ULA palette.
class VideoPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current video state.
    void refresh();

    QSize sizeHint() const override { return QSize(420, 200); }

private:
    void create_ui();

    Emulator* emulator_;

    // Raster position
    QLabel* hc_label_       = nullptr;
    QLabel* vc_label_       = nullptr;

    // Layer flags (0=ULA, 1=Layer2, 2=Tilemap, 3=Sprites)
    QLabel* layer_flags_[4] = {};

    // Layer priority flags (0=SLU .. 5=ULS)
    QLabel* prio_flags_[6]  = {};

    // Palette swatch
    QWidget* palette_widget_ = nullptr;
};
