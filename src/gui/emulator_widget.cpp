#include "gui/emulator_widget.h"
#include <QPainter>
#include <cstring>
#include <algorithm>
#include <cmath>

EmulatorWidget::EmulatorWidget(QWidget* parent)
    : QWidget(parent)
{
    // Black background.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x00, 0x00, 0x00));
    setPalette(pal);

    // Fixed size policy — the widget dictates its own size.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    set_scale(scale_);
}

void EmulatorWidget::update_frame(const uint32_t* framebuffer, int w, int h) {
    if (!framebuffer || w <= 0 || h <= 0) return;

    // On the first frame the widget is visible and devicePixelRatioF()
    // returns the real compositor DPR.  Re-apply scale so the widget
    // size and pre-scaled image match the actual physical pixel count.
    if (!dpr_valid_) {
        dpr_valid_ = true;
        set_scale(scale_);
        emit scale_changed();
    }

    if (native_.width() != w || native_.height() != h) {
        native_ = QImage(w, h, QImage::Format_ARGB32);
    }
    std::memcpy(native_.bits(), framebuffer, static_cast<size_t>(w) * h * 4);

    // Pre-scale in software to the widget's physical pixel dimensions.
    prescale();

    update();  // schedule repaint
}

void EmulatorWidget::set_scale(int factor) {
    factor = std::clamp(factor, MIN_SCALE, MAX_SCALE);
    scale_ = factor;

    // Widget size is in Qt logical pixels.  On Hi-DPI (Wayland, etc.) Qt
    // multiplies by devicePixelRatio to get physical pixels.  We want the
    // *physical* size to be exactly NATIVE_W*factor × NATIVE_H*factor, so
    // the logical size must be divided by the DPR.
    const qreal dpr = devicePixelRatioF();
    const int lw = qRound(NATIVE_W * factor / dpr);
    const int lh = qRound(NATIVE_H * factor / dpr);
    setFixedSize(lw, lh);

    // Re-scale the cached image for the new factor.
    if (!native_.isNull()) {
        prescale();
    }
}

void EmulatorWidget::set_crt_filter(bool enabled) {
    if (crt_filter_ != enabled) {
        crt_filter_ = enabled;
        update();
    }
}

void EmulatorWidget::prescale() {
    // Target: fill the widget's physical pixel buffer exactly.
    const qreal dpr = devicePixelRatioF();
    const int pw = qRound(width() * dpr);   // physical width
    const int ph = qRound(height() * dpr);  // physical height
    const int nw = native_.width();
    const int nh = native_.height();

    if (scaled_.width() != pw || scaled_.height() != ph) {
        scaled_ = QImage(pw, ph, QImage::Format_ARGB32);
    }
    // Tag with DPR so Qt maps image pixels 1:1 to physical screen pixels.
    scaled_.setDevicePixelRatio(dpr);

    // Nearest-neighbour scale from native → physical dimensions.
    for (int dy = 0; dy < ph; ++dy) {
        const auto* src = reinterpret_cast<const uint32_t*>(
            native_.scanLine(dy * nh / ph));
        auto* dst = reinterpret_cast<uint32_t*>(scaled_.scanLine(dy));
        for (int dx = 0; dx < pw; ++dx) {
            dst[dx] = src[dx * nw / pw];
        }
    }
}

void EmulatorWidget::paintEvent(QPaintEvent* /*event*/) {
    if (scaled_.isNull()) return;

    QPainter painter(this);

    // Draw the pre-scaled image — its devicePixelRatio matches the widget's,
    // so Qt performs a 1:1 physical-pixel blit with no further scaling.
    painter.drawImage(0, 0, scaled_);

    // CRT scanline filter: semi-transparent dark horizontal lines every other row.
    if (crt_filter_) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QColor line_colour(0, 0, 0, 64);  // 25% opacity black
        painter.setPen(Qt::NoPen);
        painter.setBrush(line_colour);
        for (int y = 1; y < height(); y += 2) {
            painter.drawRect(0, y, width(), 1);
        }
    }
}
