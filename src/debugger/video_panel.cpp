#include "debugger/video_panel.h"
#include "core/emulator.h"
#include "port/nextreg.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/layer2.h"
#include "video/sprites.h"
#include "video/tilemap.h"
#include "memory/ram.h"
#include "debug/debug_state.h"

#include <QShowEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QTabWidget>
#include <QRadioButton>
#include <QPaintEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <algorithm>

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
// VideoLayerView
// ---------------------------------------------------------------------------

// Checkerboard tile size for transparent pixel indication.
static constexpr int CHECK_SZ = 8;

// Dark background colour for "not yet rendered" rows.
static constexpr uint32_t UNRENDERED_ARGB   = 0xFF111111;
// Light checkerboard for transparent areas — matches typical image editor style.
static constexpr uint32_t CHECKER_DARK_ARGB = 0xFFAAAAAA;
static constexpr uint32_t CHECKER_LITE_ARGB = 0xFFCCCCCC;

static void fill_checker(uint32_t* dst, int row)
{
    for (int x = 0; x < 320; ++x) {
        bool dark = (((row / CHECK_SZ) ^ (x / CHECK_SZ)) & 1) != 0;
        dst[x] = dark ? CHECKER_DARK_ARGB : CHECKER_LITE_ARGB;
    }
}

VideoLayerView::VideoLayerView(Layer layer, const char* title,
                               Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , layer_(layer)
    , title_(QString::fromLatin1(title))
    , emulator_(emulator)
    , image_(NATIVE_W, NATIVE_H, QImage::Format_ARGB32)
{
    image_.fill(UNRENDERED_ARGB);
    // Don't call setFixedSize here — the widget's DPR is unknown until it is
    // placed on a screen.  showEvent() will call setFixedSize(sizeHint()) once
    // the real DPR is known.
}

QSize VideoLayerView::sizeHint() const
{
    // Use the widget's own DPR if it has been assigned to a screen (> 0),
    // otherwise fall back to the primary screen.
    const qreal dpr = (devicePixelRatioF() > 0.0)
                    ? devicePixelRatioF()
                    : (QGuiApplication::primaryScreen()
                       ? QGuiApplication::primaryScreen()->devicePixelRatio()
                       : 1.0);
    return QSize(qRound(NATIVE_W * DISPLAY_SCALE / dpr) + 2 * MARGIN,
                 TITLE_H + qRound(NATIVE_H * DISPLAY_SCALE / dpr) + 2 * MARGIN);
}

void VideoLayerView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Now the widget's DPR is known — lock size to exact 2× native + margins.
    setFixedSize(sizeHint());
}

void VideoLayerView::setLayer(Layer layer)
{
    if (layer_ == layer) return;
    layer_ = layer;
    last_vc_ = -2;  // force re-render
}

void VideoLayerView::refresh(int vc)
{
    // Only skip re-render for the dim running placeholder (vc < 0).
    // When paused (vc >= 0), always re-render: registers (scroll, palette,
    // tile data) may have changed even if the scanline position is the same
    // (e.g. EOF → EOF stepping stays at vc=255 across frames).
    if (vc < 0 && last_vc_ < 0) return;
    last_vc_ = vc;
    render_to_image(vc);
    update();
}

void VideoLayerView::render_to_image(int vc)
{
    if (!emulator_) {
        image_.fill(UNRENDERED_ARGB);
        return;
    }

    // When running (vc < 0), show a dim placeholder.
    if (vc < 0) {
        image_.fill(UNRENDERED_ARGB);
        return;
    }

    Emulator& emu = *emulator_;

    for (int row = 0; row < 256; ++row) {
        uint32_t* dst = reinterpret_cast<uint32_t*>(image_.scanLine(row));

        if (row > vc) {
            // Scanline not yet reached in this frame.
            for (int x = 0; x < 320; ++x)
                dst[x] = UNRENDERED_ARGB;
            continue;
        }

        // Pre-fill with checkerboard so transparent areas are visible.
        fill_checker(dst, row);

        switch (layer_) {
            case Layer::ULA_PRIMARY:
                emu.renderer().ula().render_scanline(dst, row, emu.mmu());
                break;

            case Layer::ULA_SHADOW:
                emu.renderer().ula().render_scanline_screen1(dst, row, emu.mmu());
                break;

            case Layer::LAYER2_ACTIVE:
                emu.layer2().render_scanline_debug(
                    dst, row, emu.ram(), emu.palette(),
                    emu.layer2().active_bank());
                break;

            case Layer::LAYER2_SHADOW:
                emu.layer2().render_scanline_debug(
                    dst, row, emu.ram(), emu.palette(),
                    emu.layer2().shadow_bank());
                break;

            case Layer::SPRITES:
                emu.sprites().render_scanline_debug(dst, row, emu.palette());
                break;

            case Layer::TILEMAP: {
                static bool ula_over[320];
                std::fill(std::begin(ula_over), std::end(ula_over), false);
                emu.tilemap().render_scanline_debug(
                    dst, ula_over, row, emu.ram(), emu.palette());
                break;
            }
        }
    }
}

void VideoLayerView::paintEvent(QPaintEvent*)
{
    // Ensure size reflects the current DPR.  showEvent() sets this, but the very
    // first paint can fire before the layout has applied the new fixed size.
    // Returning here causes Qt to immediately schedule a correctly-sized repaint.
    const QSize needed = sizeHint();
    if (size() != needed) {
        setFixedSize(needed);
        return;
    }

    QPainter p(this);

    // Pre-scale the 320×256 source image to fill the physical content area.
    // Use the actual widget dimensions so the image always fills the available
    // space regardless of screen DPR (avoids clipping on fractional-DPR screens).
    const qreal dpr    = devicePixelRatioF();
    const int   phys_w = qRound((width()  - 2 * MARGIN) * dpr);
    const int   phys_h = qRound((height() - TITLE_H - 2 * MARGIN) * dpr);

    QImage scaled(phys_w, phys_h, QImage::Format_ARGB32);
    for (int sy = 0; sy < phys_h; ++sy) {
        const auto* src = reinterpret_cast<const uint32_t*>(
            image_.scanLine(sy * NATIVE_H / phys_h));
        auto* dst = reinterpret_cast<uint32_t*>(scaled.scanLine(sy));
        for (int sx = 0; sx < phys_w; ++sx)
            dst[sx] = src[sx * NATIVE_W / phys_w];
    }
    // Tag with DPR so Qt maps each physical pixel 1:1 to the screen.
    scaled.setDevicePixelRatio(dpr);

    // Draw at (MARGIN, TITLE_H + MARGIN); the image's logical size is
    // phys_w/dpr × phys_h/dpr which exactly fits the content area.
    p.drawImage(QPoint(MARGIN, TITLE_H + MARGIN), scaled);

    // Title above the image.
    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);
    p.setPen(Qt::lightGray);
    p.drawText(QRect(0, 0, width(), TITLE_H),
               Qt::AlignLeft | Qt::AlignVCenter, title_);

    // Red scanline indicator when paused.
    // Spans the full widget width so it's visible in the margins even when
    // image content is red or the scanline is at the very bottom.
    if (last_vc_ >= 0) {
        // Logical y: top of image + (vc * logical image height / 256)
        const int log_img_h = qRound(phys_h / dpr);
        const int y_line    = TITLE_H + MARGIN + last_vc_ * log_img_h / NATIVE_H;
        p.setPen(QPen(Qt::red, 1));
        p.drawLine(0, y_line, width() - 1, y_line);
    }
}

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

    // ── Layer sub-panels ─────────────────────────────────────────────────────

    layer_tabs_ = new QTabWidget(this);
    layer_tabs_->setTabPosition(QTabWidget::North);

    // All tabs share the same layout: a fixed-size VideoLayerView at the top,
    // then a fixed-height row for radio buttons at the bottom.  Because the
    // view has a fixed size (2× native + margins), all screens are identical
    // in position and size across every tab.
    static constexpr int CTRL_ROW_H = 26;

    auto make_layer_tab = [&](const char* tab_title,
                               VideoLayerView::Layer layer,
                               const char* view_title,
                               const char* rb1_text, QRadioButton** rb1_out,
                               const char* rb2_text, QRadioButton** rb2_out)
        -> VideoLayerView*
    {
        auto* tab  = new QWidget();
        auto* vbox = new QVBoxLayout(tab);
        vbox->setContentsMargins(12, 4, 12, 4);
        vbox->setSpacing(2);

        // Fixed-size screen view, centred so left/right margins are equal.
        auto* view = new VideoLayerView(layer, view_title, emulator_, tab);
        vbox->addWidget(view, 0, Qt::AlignHCenter);

        // Fixed-height control row below the screen.
        auto* ctrl = new QWidget(tab);
        ctrl->setFixedHeight(CTRL_ROW_H);
        auto* rb_row = new QHBoxLayout(ctrl);
        rb_row->setContentsMargins(4, 0, 4, 0);
        rb_row->setSpacing(12);

        if (rb1_text && rb2_text) {
            auto* rb1 = new QRadioButton(tr(rb1_text), ctrl);
            auto* rb2 = new QRadioButton(tr(rb2_text), ctrl);
            rb1->setChecked(true);
            rb_row->addWidget(rb1);
            rb_row->addWidget(rb2);
            if (rb1_out) *rb1_out = rb1;
            if (rb2_out) *rb2_out = rb2;
        }
        rb_row->addStretch();

        vbox->addWidget(ctrl);

        layer_tabs_->addTab(tab, tr(tab_title));
        return view;
    };

    // ULA tab — single view, radio buttons select primary/shadow.
    ula_view_ = make_layer_tab(
        "ULA",
        VideoLayerView::Layer::ULA_PRIMARY, "ULA screen (bank 5/7)",
        "Primary (bank 5)", &ula_rb_primary_,
        "Shadow (bank 7)",  &ula_rb_shadow_);

    connect(ula_rb_primary_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            ula_view_->setLayer(VideoLayerView::Layer::ULA_PRIMARY);
            refresh();
        }
    });
    connect(ula_rb_shadow_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            ula_view_->setLayer(VideoLayerView::Layer::ULA_SHADOW);
            refresh();
        }
    });

    // Layer 2 tab — single view, radio buttons select active/shadow bank.
    l2_view_ = make_layer_tab(
        "Layer2",
        VideoLayerView::Layer::LAYER2_ACTIVE, "Layer 2",
        "Active bank",  &l2_rb_active_,
        "Shadow bank",  &l2_rb_shadow_);

    connect(l2_rb_active_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            l2_view_->setLayer(VideoLayerView::Layer::LAYER2_ACTIVE);
            refresh();
        }
    });
    connect(l2_rb_shadow_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            l2_view_->setLayer(VideoLayerView::Layer::LAYER2_SHADOW);
            refresh();
        }
    });

    // Sprites tab — single view, no radio buttons.
    sprites_view_ = make_layer_tab(
        "Sprites",
        VideoLayerView::Layer::SPRITES, "Sprites",
        nullptr, nullptr, nullptr, nullptr);

    // TileMap tab — single view, no radio buttons.
    tilemap_view_ = make_layer_tab(
        "TileMap",
        VideoLayerView::Layer::TILEMAP, "TileMap",
        nullptr, nullptr, nullptr, nullptr);

    // When the user switches tabs, invalidate so the new tab renders immediately.
    connect(layer_tabs_, &QTabWidget::currentChanged, this, [this](int) {
        for (VideoLayerView* v : {ula_view_, l2_view_,
                                   sprites_view_, tilemap_view_}) {
            if (v) v->invalidate();
        }
        refresh();
    });

    layout->addWidget(layer_tabs_);
    layout->addStretch();
}

void VideoPanel::refresh()
{
    if (!emulator_) return;

    // ── Raster position (only when paused) ───────────────────────────────────
    // Raw VC is 0..LINES_PER_FRAME-1 (0..319); clamp to FB_HEIGHT-1 (0..255)
    // since only those scanlines are visible and rendered in the layer views.
    int vc = -1;
    if (emulator_->debug_state().paused()) {
        vc = std::min(emulator_->paused_vc(), Renderer::FB_HEIGHT - 1);
        vc_label_->setText(QString::asprintf("%3d", vc));
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

    // ── Layer sub-panel views — only refresh the visible tab ─────────────────
    switch (layer_tabs_->currentIndex()) {
        case 0:  if (ula_view_)      ula_view_->refresh(vc);      break;
        case 1:  if (l2_view_)       l2_view_->refresh(vc);       break;
        case 2:  if (sprites_view_)  sprites_view_->refresh(vc);  break;
        case 3:  if (tilemap_view_)  tilemap_view_->refresh(vc);  break;
        default: break;
    }
}
