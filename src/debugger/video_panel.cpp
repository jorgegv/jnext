#include "debugger/video_panel.h"
#include "core/emulator.h"
#include "port/nextreg.h"
#include "video/palette.h"
#include "video/renderer.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QFont>
#include <QPainter>
#include <QString>

// ---------------------------------------------------------------------------
// Custom palette swatch widget — paints 16 ULA colours in a 4x4 grid.
// ---------------------------------------------------------------------------

class PaletteSwatchWidget : public QWidget {
public:
    explicit PaletteSwatchWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(4 * CELL_SIZE + 1, 4 * CELL_SIZE + 1);
        setMaximumSize(4 * CELL_SIZE + 1, 4 * CELL_SIZE + 1);
        std::fill(std::begin(colours_), std::end(colours_), 0xFF000000u);
    }

    void set_colours(const uint32_t colours[16]) {
        std::copy(colours, colours + 16, colours_);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(Qt::gray);
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                int idx = row * 4 + col;
                uint32_t argb = colours_[idx];
                QColor c(static_cast<int>((argb >> 16) & 0xFF),
                         static_cast<int>((argb >> 8) & 0xFF),
                         static_cast<int>(argb & 0xFF));
                p.fillRect(col * CELL_SIZE, row * CELL_SIZE,
                           CELL_SIZE, CELL_SIZE, c);
                p.drawRect(col * CELL_SIZE, row * CELL_SIZE,
                           CELL_SIZE, CELL_SIZE);
            }
        }
    }

private:
    static constexpr int CELL_SIZE = 20;
    uint32_t colours_[16]{};
};

// ---------------------------------------------------------------------------
// VideoPanel
// ---------------------------------------------------------------------------

VideoPanel::VideoPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

void VideoPanel::create_ui() {
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    QFont mono_bold = mono;
    mono_bold.setBold(true);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(8);
    main_layout->setContentsMargins(8, 8, 8, 8);

    // --- Raster position group ---
    auto* raster_group = new QGroupBox(tr("Raster Position"), this);
    auto* raster_layout = new QFormLayout(raster_group);
    raster_layout->setSpacing(4);

    hc_label_ = new QLabel("N/A", raster_group);
    hc_label_->setFont(mono);
    raster_layout->addRow("hc:", hc_label_);

    vc_label_ = new QLabel("---", raster_group);
    vc_label_->setFont(mono);
    raster_layout->addRow("vc:", vc_label_);

    main_layout->addWidget(raster_group);

    // --- Layer toggles group ---
    auto* layers_group = new QGroupBox(tr("Layers"), this);
    auto* layers_layout = new QVBoxLayout(layers_group);
    layers_layout->setSpacing(2);

    ula_check_ = new QCheckBox(tr("ULA"), layers_group);
    ula_check_->setEnabled(false); // display-only
    layers_layout->addWidget(ula_check_);

    l2_check_ = new QCheckBox(tr("Layer 2"), layers_group);
    l2_check_->setEnabled(false);
    layers_layout->addWidget(l2_check_);

    tm_check_ = new QCheckBox(tr("Tilemap"), layers_group);
    tm_check_->setEnabled(false);
    layers_layout->addWidget(tm_check_);

    spr_check_ = new QCheckBox(tr("Sprites"), layers_group);
    spr_check_->setEnabled(false);
    layers_layout->addWidget(spr_check_);

    main_layout->addWidget(layers_group);

    // --- Layer priority dropdown ---
    auto* priority_group = new QGroupBox(tr("Layer Priority"), this);
    auto* priority_layout = new QVBoxLayout(priority_group);

    priority_combo_ = new QComboBox(priority_group);
    priority_combo_->addItem("SLU (Sprites, Layer2, ULA)");
    priority_combo_->addItem("LSU (Layer2, Sprites, ULA)");
    priority_combo_->addItem("SUL (Sprites, ULA, Layer2)");
    priority_combo_->addItem("LUS (Layer2, ULA, Sprites)");
    priority_combo_->addItem("USL (ULA, Sprites, Layer2)");
    priority_combo_->addItem("ULS (ULA, Layer2, Sprites)");
    priority_combo_->setEnabled(false); // read-only for now
    priority_layout->addWidget(priority_combo_);

    main_layout->addWidget(priority_group);

    // --- Palette swatch ---
    auto* palette_group = new QGroupBox(tr("ULA Palette"), this);
    auto* palette_layout = new QVBoxLayout(palette_group);
    palette_widget_ = new PaletteSwatchWidget(palette_group);
    palette_layout->addWidget(palette_widget_);

    main_layout->addWidget(palette_group);

    // Push everything to the top
    main_layout->addStretch();
}

void VideoPanel::refresh() {
    if (!emulator_) return;

    // --- Raster position ---
    // vc from NextREG 0x1E bit 0 (MSB) and 0x1F (LSB)
    uint8_t reg_1e = emulator_->nextreg().cached(0x1E);
    uint8_t reg_1f = emulator_->nextreg().cached(0x1F);
    uint16_t vc = ((reg_1e & 0x01) << 8) | reg_1f;
    vc_label_->setText(QString::asprintf("%d", vc));
    hc_label_->setText("N/A");  // only meaningful when paused mid-scanline

    // --- Layer state from NextREG 0x15 ---
    uint8_t reg15 = emulator_->nextreg().cached(0x15);
    spr_check_->setChecked(reg15 & 0x01);           // bit 0: sprites visible
    // Layer 2 enabled via NextREG 0x69 bit 7 (or port 0x123B)
    uint8_t reg69 = emulator_->nextreg().cached(0x69);
    l2_check_->setChecked(reg69 & 0x80);

    // Tilemap enabled via NextREG 0x6B bit 7
    uint8_t reg6b = emulator_->nextreg().cached(0x6B);
    tm_check_->setChecked(reg6b & 0x80);

    // ULA always enabled unless explicitly disabled via NextREG 0x68 bit 7
    uint8_t reg68 = emulator_->nextreg().cached(0x68);
    ula_check_->setChecked(!(reg68 & 0x80));

    // Layer priority from bits 4:2
    int priority = (reg15 >> 2) & 0x07;
    if (priority < priority_combo_->count())
        priority_combo_->setCurrentIndex(priority);

    // --- Palette swatch ---
    uint32_t colours[16];
    for (int i = 0; i < 16; ++i) {
        colours[i] = emulator_->palette().ula_colour(static_cast<uint8_t>(i));
    }
    static_cast<PaletteSwatchWidget*>(palette_widget_)->set_colours(colours);
}
