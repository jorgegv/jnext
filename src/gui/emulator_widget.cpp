#include "gui/emulator_widget.h"
#include <QPainter>
#include <cstring>

EmulatorWidget::EmulatorWidget(QWidget* parent)
    : QWidget(parent)
{
    // Dark gray background when no frame has been rendered yet.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x30, 0x30, 0x30));
    setPalette(pal);
}

void EmulatorWidget::update_frame(const uint32_t* framebuffer, int w, int h) {
    if (!framebuffer || w <= 0 || h <= 0) return;

    // QImage::Format_ARGB32 matches the emulator's ARGB8888 layout.
    // We must copy the data because the framebuffer pointer is owned by the emulator
    // and may be overwritten on the next frame.
    if (image_.width() != w || image_.height() != h) {
        image_ = QImage(w, h, QImage::Format_ARGB32);
    }
    std::memcpy(image_.bits(), framebuffer, static_cast<size_t>(w) * h * 4);
    update();  // schedule repaint
}

void EmulatorWidget::paintEvent(QPaintEvent* /*event*/) {
    if (image_.isNull()) return;

    QPainter painter(this);

    // Integer scaling: find the largest integer scale that fits.
    int scale_x = width() / image_.width();
    int scale_y = height() / image_.height();
    int scale = std::max(1, std::min(scale_x, scale_y));

    int dst_w = image_.width() * scale;
    int dst_h = image_.height() * scale;

    // Center within the widget.
    int x = (width() - dst_w) / 2;
    int y = (height() - dst_h) / 2;

    // No interpolation — crisp pixel scaling.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(x, y, dst_w, dst_h), image_);
}
