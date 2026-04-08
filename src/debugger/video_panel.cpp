#include "debugger/video_panel.h"
#include "core/emulator.h"
#include "port/nextreg.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "debug/debug_state.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QPainter>
#include <QString>

// ---------------------------------------------------------------------------
// PaletteSwatchWidget — 16 ULA colours in a horizontal row.
// ---------------------------------------------------------------------------

class PaletteSwatchWidget : public QWidget {
public:
    explicit PaletteSwatchWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(CELL + 1);
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
        int cell = width() / 16;
        if (cell < 1) cell = 1;
        for (int i = 0; i < 16; ++i) {
            uint32_t argb = colours_[i];
            QColor c(static_cast<int>((argb >> 16) & 0xFF),
                     static_cast<int>((argb >>  8) & 0xFF),
                     static_cast<int>( argb        & 0xFF));
            p.fillRect(i * cell, 0, cell, CELL, c);
            p.drawRect( i * cell, 0, cell, CELL);
        }
    }

private:
    static constexpr int CELL = 20;
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

void VideoPanel::create_ui()
{
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    QFont mono_bold = mono;
    mono_bold.setBold(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    // Helper: bold heading label — all padded to same width with monospace font.
    auto make_bold = [&](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text, this);
        lbl->setFont(mono_bold);
        return lbl;
    };

    // Helper: regular value label.
    auto make_val = [&](const QString& text = "---") -> QLabel* {
        auto* lbl = new QLabel(text, this);
        lbl->setFont(mono);
        return lbl;
    };

    // All headings padded to 13 chars ("ULA Palette: " is the longest).
    // With monospace font this aligns the content column across all rows.

    // ── Raster ───────────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("Raster:      "));
        row->addWidget(make_bold("HC:"));
        hc_label_ = make_val("---");
        hc_label_->setFixedWidth(hc_label_->fontMetrics().horizontalAdvance("000") + 4);
        row->addWidget(hc_label_);
        row->addSpacing(12);
        row->addWidget(make_bold("VC:"));
        vc_label_ = make_val("---");
        vc_label_->setFixedWidth(vc_label_->fontMetrics().horizontalAdvance("000") + 4);
        row->addWidget(vc_label_);
        row->addStretch();
        layout->addLayout(row);
    }

    // ── Layers ───────────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("Layers:      "));
        static const char* kLayerNames[4] = { "ULA", "Layer2", "Tilemap", "Sprites" };
        for (int i = 0; i < 4; ++i) {
            layer_flags_[i] = make_val(kLayerNames[i]);
            row->addWidget(layer_flags_[i]);
            if (i < 3) row->addSpacing(8);
        }
        row->addStretch();
        layout->addLayout(row);
    }

    // ── Layer Priority ───────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("Priority:    "));
        static const char* kPrioNames[6] = { "SLU", "LSU", "SUL", "LUS", "USL", "ULS" };
        for (int i = 0; i < 6; ++i) {
            prio_flags_[i] = make_val(kPrioNames[i]);
            row->addWidget(prio_flags_[i]);
            if (i < 5) row->addSpacing(8);
        }
        row->addStretch();
        layout->addLayout(row);
    }

    // ── ULA Palette ──────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("ULA Palette: "));
        palette_widget_ = new PaletteSwatchWidget(this);
        row->addWidget(palette_widget_, 1);
        layout->addLayout(row);
    }

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #D0D0D0;");
    layout->addWidget(line);

    layout->addStretch();
}

void VideoPanel::refresh()
{
    if (!emulator_) return;

    // ── Raster position (only when paused) ───────────────────────────────────
    if (emulator_->debug_state().paused()) {
        vc_label_->setText(QString::asprintf("%3d", emulator_->paused_vc()));
        hc_label_->setText(QString::asprintf("%3d", emulator_->paused_hc()));
    } else {
        vc_label_->setText("---");
        hc_label_->setText("---");
    }

    // ── Layer state ──────────────────────────────────────────────────────────
    uint8_t reg15 = emulator_->nextreg().cached(0x15);
    uint8_t reg68 = emulator_->nextreg().cached(0x68);
    uint8_t reg69 = emulator_->nextreg().cached(0x69);
    uint8_t reg6b = emulator_->nextreg().cached(0x6B);

    bool layer_active[4] = {
        !(reg68 & 0x80),   // ULA
        !!(reg69 & 0x80),  // Layer 2
        !!(reg6b & 0x80),  // Tilemap
        !!(reg15 & 0x01),  // Sprites
    };

    auto set_flag = [](QLabel* lbl, bool active) {
        if (active)
            lbl->setStyleSheet("QLabel { font-weight: bold; color: #00AA00; }");
        else
            lbl->setStyleSheet("QLabel { color: #888888; }");
    };

    for (int i = 0; i < 4; ++i)
        set_flag(layer_flags_[i], layer_active[i]);

    // ── Layer priority ───────────────────────────────────────────────────────
    int priority = (reg15 >> 2) & 0x07;
    for (int i = 0; i < 6; ++i)
        set_flag(prio_flags_[i], i == priority);

    // ── ULA Palette ──────────────────────────────────────────────────────────
    uint32_t colours[16];
    for (int i = 0; i < 16; ++i)
        colours[i] = emulator_->palette().ula_colour(static_cast<uint8_t>(i));
    static_cast<PaletteSwatchWidget*>(palette_widget_)->set_colours(colours);
}
