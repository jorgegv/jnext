#include "gui/emulator_widget.h"
#include <QPainter>
#include <cstring>
#include <algorithm>

EmulatorWidget::EmulatorWidget(QWidget* parent)
    : QWidget(parent)
{
    // Dark gray background when no frame has been rendered yet.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x00, 0x00, 0x00));
    setPalette(pal);

    // Minimum size: native framebuffer dimensions.
    setMinimumSize(NATIVE_W, NATIVE_H);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void EmulatorWidget::update_frame(const uint32_t* framebuffer, int w, int h) {
    if (!framebuffer || w <= 0 || h <= 0) return;

    // QImage::Format_ARGB32 matches the emulator's ARGB8888 layout.
    if (image_.width() != w || image_.height() != h) {
        image_ = QImage(w, h, QImage::Format_ARGB32);
    }
    std::memcpy(image_.bits(), framebuffer, static_cast<size_t>(w) * h * 4);
    update();  // schedule repaint
}

void EmulatorWidget::set_scale_mode(ScaleMode mode) {
    if (scale_mode_ != mode) {
        scale_mode_ = mode;
        update();
    }
}

void EmulatorWidget::set_crt_filter(bool enabled) {
    if (crt_filter_ != enabled) {
        crt_filter_ = enabled;
        update();
    }
}

QRect EmulatorWidget::compute_dest_rect() const {
    int w = width();
    int h = height();
    int iw = image_.width();
    int ih = image_.height();

    switch (scale_mode_) {
    case ScaleMode::Integer: {
        // Largest integer scale factor that fits.
        int sx = w / iw;
        int sy = h / ih;
        int scale = std::max(1, std::min(sx, sy));
        int dw = iw * scale;
        int dh = ih * scale;
        return QRect((w - dw) / 2, (h - dh) / 2, dw, dh);
    }

    case ScaleMode::Stretch:
        return QRect(0, 0, w, h);

    case ScaleMode::AspectFit:
    default: {
        // Maintain 4:3 aspect ratio (the ZX Next output on a TV).
        double widget_aspect = static_cast<double>(w) / h;
        int dw, dh;
        if (widget_aspect > TARGET_ASPECT) {
            // Widget is wider than 4:3 — pillarbox.
            dh = h;
            dw = static_cast<int>(h * TARGET_ASPECT + 0.5);
        } else {
            // Widget is taller than 4:3 — letterbox.
            dw = w;
            dh = static_cast<int>(w / TARGET_ASPECT + 0.5);
        }
        return QRect((w - dw) / 2, (h - dh) / 2, dw, dh);
    }
    }
}

void EmulatorWidget::paintEvent(QPaintEvent* /*event*/) {
    if (image_.isNull()) return;

    QPainter painter(this);
    QRect dest = compute_dest_rect();

    // Use nearest-neighbour for Integer mode (crisp pixels), bilinear for others.
    bool smooth = (scale_mode_ != ScaleMode::Integer);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    painter.drawImage(dest, image_);

    // CRT scanline filter: semi-transparent dark horizontal lines every other row.
    if (crt_filter_) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QColor line_colour(0, 0, 0, 64);  // 25% opacity black
        int y0 = dest.top();
        int y1 = dest.bottom();
        int x0 = dest.left();
        int line_w = dest.width();

        // Draw lines at every other output pixel row within the dest rect.
        // Use a pen width of 1 for single-pixel lines.
        painter.setPen(Qt::NoPen);
        painter.setBrush(line_colour);
        for (int y = y0 + 1; y <= y1; y += 2) {
            painter.drawRect(x0, y, line_w, 1);
        }
    }
}
