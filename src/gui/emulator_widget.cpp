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

    // Task 63 (issue #9) — a NEW frame is now waiting to be shown. paintEvent()
    // clears this and counts one present. Marking it here (not in paintEvent
    // unconditionally) is what makes `present_count_` mean "distinct emulated
    // frames that reached the screen": an expose/resize repaint that re-blits
    // an image already shown finds the flag clear and is correctly not counted.
    frame_pending_ = true;

    update();  // schedule repaint
}

void EmulatorWidget::set_scale(int factor) {
    factor = std::clamp(factor, MIN_SCALE, MAX_SCALE);
    scale_ = factor;

    // Widget size is in Qt logical pixels.  On Hi-DPI (Wayland, etc.) Qt
    // multiplies by devicePixelRatio to get physical pixels.  We want the
    // *physical* size to be exactly NATIVE_W*factor × DISPLAY_H*factor, so
    // the logical size must be divided by the DPR. DISPLAY_H = NATIVE_H * 2
    // gives the vertical 2× scaling that turns the 640×256 in-memory
    // framebuffer into a square-pixel 640×512 viewport (G104 Phase 7).
    const qreal dpr = devicePixelRatioF();
    const int lw = qRound(NATIVE_W * factor / dpr);
    const int lh = qRound(DISPLAY_H * factor / dpr);
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

void EmulatorWidget::set_fullscreen_mode(bool fs) {
    fullscreen_mode_ = fs;
    fs_offset_ = QPoint(0, 0);
}

void EmulatorWidget::prescale() {
    const qreal dpr = devicePixelRatioF();
    const int pw = qRound(width() * dpr);   // physical width
    const int ph = qRound(height() * dpr);  // physical height
    const int nw = native_.width();
    const int nh = native_.height();

    int target_w, target_h;

    if (fullscreen_mode_) {
        // Square-pixel CRT geometry: use NATIVE_W (640) for the horizontal
        // step and DISPLAY_H (NATIVE_H × 2 = 512) for the vertical step so
        // the integer fullscreen scale lands on a 4:3-correct grid (G104
        // Phase 7). The source image is still NATIVE_H rows tall; the
        // generic nearest-neighbour scaling loop below handles the
        // nh→target_h vertical stretch (typically 2× per fs_scale step).
        const int sx = pw / NATIVE_W;
        const int sy = ph / DISPLAY_H;
        const int fs_scale = std::max(1, std::min(sx, sy));
        target_w = NATIVE_W * fs_scale;
        target_h = DISPLAY_H * fs_scale;
        // Offset to center the image (in logical pixels for paintEvent).
        fs_offset_ = QPoint(qRound((pw - target_w) / (2.0 * dpr)),
                            qRound((ph - target_h) / (2.0 * dpr)));
    } else {
        // In windowed mode setFixedSize() already gives us a physical
        // NATIVE_W × DISPLAY_H × scale viewport, so target_w/target_h match
        // pw/ph. The generic nh→target_h mapping below performs the
        // vertical 2× doubling implicitly (e.g. nh=256, target_h=512 →
        // each src row sampled twice).
        target_w = pw;
        target_h = ph;
        fs_offset_ = QPoint(0, 0);
    }

    if (scaled_.width() != target_w || scaled_.height() != target_h) {
        scaled_ = QImage(target_w, target_h, QImage::Format_ARGB32);
    }
    // Tag with DPR so Qt maps image pixels 1:1 to physical screen pixels.
    scaled_.setDevicePixelRatio(dpr);

    // Nearest-neighbour scale from native → target dimensions. The
    // dy * nh / target_h mapping naturally vertically doubles when
    // target_h = 2 * nh: rows 0,1 of the destination both sample src row 0,
    // rows 2,3 sample src row 1, etc. Horizontal scaling is unchanged.
    if (target_w <= 0 || target_h <= 0 || nw <= 0 || nh <= 0) return;
    for (int dy = 0; dy < target_h; ++dy) {
        const auto* src = reinterpret_cast<const uint32_t*>(
            native_.scanLine(dy * nh / target_h));
        auto* dst = reinterpret_cast<uint32_t*>(scaled_.scanLine(dy));
        for (int dx = 0; dx < target_w; ++dx) {
            dst[dx] = src[dx * nw / target_w];
        }
    }
}

void EmulatorWidget::paintEvent(QPaintEvent* /*event*/) {
    if (scaled_.isNull()) return;

    // Task 63 — count this as a PRESENT only if it is serving a frame not yet
    // shown. See frame_pending_ in update_frame().
    if (frame_pending_) {
        frame_pending_ = false;
        ++present_count_;
    }

    QPainter painter(this);

    // Draw the pre-scaled image — its devicePixelRatio matches the widget's,
    // so Qt performs a 1:1 physical-pixel blit with no further scaling.
    // In fullscreen mode, the image is centered with black bars around it.
    painter.drawImage(fs_offset_, scaled_);

    // CRT scanline filter: semi-transparent dark horizontal lines every other row.
    // In fullscreen mode, only draw over the image area, not the black bars.
    if (crt_filter_) {
        const qreal dpr = devicePixelRatioF();
        const int img_w = qRound(scaled_.width() / dpr);
        const int img_h = qRound(scaled_.height() / dpr);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QColor line_colour(0, 0, 0, 64);  // 25% opacity black
        painter.setPen(Qt::NoPen);
        painter.setBrush(line_colour);
        for (int y = 1; y < img_h; y += 2) {
            painter.drawRect(fs_offset_.x(), fs_offset_.y() + y, img_w, 1);
        }
    }
}
