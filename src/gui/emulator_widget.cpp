#include "gui/emulator_widget.h"
#include <QPainter>
#include <cstring>
#include <algorithm>

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

    if (image_.width() != w || image_.height() != h) {
        image_ = QImage(w, h, QImage::Format_ARGB32);
    }
    std::memcpy(image_.bits(), framebuffer, static_cast<size_t>(w) * h * 4);
    update();  // schedule repaint
}

void EmulatorWidget::set_scale(int factor) {
    factor = std::clamp(factor, MIN_SCALE, MAX_SCALE);
    scale_ = factor;
    setFixedSize(NATIVE_W * factor, NATIVE_H * factor);
}

void EmulatorWidget::set_crt_filter(bool enabled) {
    if (crt_filter_ != enabled) {
        crt_filter_ = enabled;
        update();
    }
}

void EmulatorWidget::paintEvent(QPaintEvent* /*event*/) {
    if (image_.isNull()) return;

    QPainter painter(this);

    // Always nearest-neighbour — the widget is an exact integer multiple.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(0, 0, width(), height()), image_);

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
