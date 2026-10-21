#include "gui/emulator_widget.h"
#include "core/log.h"
#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>

#ifdef JNEXT_HAVE_CRT_GL
#include "gui/crt_gl_overlay.h"
#endif

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

void EmulatorWidget::update_frame(const uint32_t* framebuffer, int w, int h,
                                  bool new_content) {
    if (!framebuffer || w <= 0 || h <= 0) return;

    // On the first frame the widget is visible and devicePixelRatioF()
    // returns the real compositor DPR.  Re-apply scale so the widget
    // size and pre-scaled image match the actual physical pixel count.
    if (!dpr_valid_) {
        dpr_valid_ = true;
        set_scale(scale_);
        emit scale_changed();
    }

#ifdef JNEXT_HAVE_CRT_GL
    // The CRT shader draws the frame itself: no copy into native_, no prescale.
    if (crt_overlay_) {
        // native_ is not touched: the overlay holds the newest frame, and
        // drop_crt_overlay() rebuilds native_ from it when the software path resumes.
        crt_overlay_->submit(framebuffer, w, h);
        if (fullscreen_mode_) layout_crt_overlay();   // prescale() re-derives this every frame too
        if (new_content) frame_pending_ = true;
        return;
    }
#endif

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
    //
    // A stale re-push (new_content == false) must not arm the flag — otherwise
    // a paused frontend, which re-pushes the same framebuffer every tick,
    // inflates present_count_ with frames that showed nothing new, and a
    // reporting window straddling pause->resume then under-reports genuinely
    // lost frames. Note this only ever LEAVES the flag alone: a frame pushed
    // earlier and not yet painted stays pending, so it is still counted when
    // its paint finally arrives.
    if (new_content) frame_pending_ = true;

    // Unconditional, and identical for both values of new_content: the
    // repaint request is presentation, and this diagnostic does not alter it.
    update();  // schedule repaint
}

void EmulatorWidget::flush_pending_present() {
    // See the header: deliver the repaint request update() posted, through the
    // same path the event loop would take. frame_pending_ is the "a new frame
    // has not been painted" flag paintEvent() clears.
    ++flush_stats_.checked;
    if (!frame_pending_) return;
    ++flush_stats_.painted;
    QCoreApplication::sendPostedEvents(window(), QEvent::UpdateRequest);
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
        sync_crt_overlay();   // the scanline mode is the shader's Lite style where there is GL
        update();
    }
}

void EmulatorWidget::set_fullscreen_mode(bool fs) {
    fullscreen_mode_ = fs;
    fs_offset_ = QPoint(0, 0);
    layout_crt_overlay();
}

bool EmulatorWidget::set_crt_shader(bool enabled) {
#ifdef JNEXT_HAVE_CRT_GL
    crt_shader_ = enabled;
    if (enabled) gl_unavailable_ = false;   // an explicit request tries again
    sync_crt_overlay();
    return true;
#else
    (void)enabled;
    crt_shader_ = false;
    return false;
#endif
}

void EmulatorWidget::set_crt_shader_style(crt_shader::Style style) {
    if (crt_style_ == style) return;
    crt_style_ = style;
    sync_crt_overlay();   // the style picks crtemu's shader set at creation: rebuild
}

void EmulatorWidget::sync_crt_overlay() {
#ifdef JNEXT_HAVE_CRT_GL
    // Which look is wanted: the shader's TV/PC style; or, for the scanline mode,
    // the shader's own scanlines-only (Lite) style. If GL turned out to be
    // unusable the scanline mode falls back to the software overlay (paintEvent).
    bool want = false;
    crt_shader::Style style = crt_style_;
    if (crt_shader_) {
        want = true;
    } else if (crt_filter_ && !gl_unavailable_) {
        want = true;
        style = crt_shader::Style::Lite;
    }

    if (crt_overlay_ && (!want || crt_overlay_->style() != style)) drop_crt_overlay();
    if (want && !crt_overlay_) {
        crt_overlay_ = new CrtGlOverlay(this, style);
        connect(crt_overlay_, &CrtGlOverlay::presented,
                this, &EmulatorWidget::note_overlay_presented);
        // Queued: failed() can be raised from inside the overlay's own
        // initializeGL()/paintEvent(), which must return before it is deleted.
        // By the time it is delivered the user may have switched the effect off
        // (or on again, into a NEW overlay): only the overlay that raised it counts.
        connect(crt_overlay_, &CrtGlOverlay::failed, this,
                [this, raised_by = QPointer<CrtGlOverlay>(crt_overlay_)](const QString& why) {
                    if (!raised_by || raised_by != crt_overlay_) return;
                    drop_crt_overlay();
                    gl_unavailable_ = true;
                    if (crt_shader_) {
                        crt_shader_ = false;
                        Log::platform()->warn("CRT shader unavailable, using the software display: {}",
                                              why.toStdString());
                        emit crt_shader_failed(why);
                    } else {
                        // The scanline mode keeps its look: the software overlay takes over.
                        Log::platform()->warn("CRT shader unavailable, drawing software scanlines: {}",
                                              why.toStdString());
                        update();
                    }
                }, Qt::QueuedConnection);
        layout_crt_overlay();
        // Paused, or stopped in the debugger: no frame is coming, so show the last one.
        if (!native_.isNull())
            crt_overlay_->submit(reinterpret_cast<const uint32_t*>(native_.constBits()),
                                 native_.width(), native_.height());
        crt_overlay_->show();
    }
#endif
}

void EmulatorWidget::drop_crt_overlay() {
#ifdef JNEXT_HAVE_CRT_GL
    if (!crt_overlay_) return;
    // Take the newest frame back (native_ is not updated while an overlay runs).
    std::vector<uint32_t> last;
    if (crt_overlay_->copy_frame_argb(last)) {
        const QSize sz = crt_overlay_->frame_size();
        if (native_.width() != sz.width() || native_.height() != sz.height())
            native_ = QImage(sz.width(), sz.height(), QImage::Format_ARGB32);
        std::memcpy(native_.bits(), last.data(), last.size() * 4);
    }
    // Deleted now, not deleteLater(): its destructor needs its context, and a
    // later deletion could land after the parent's own teardown.
    delete crt_overlay_;
    crt_overlay_ = nullptr;
    // Repaint from it now.
    if (!native_.isNull()) prescale();
    update();
#endif
}

void EmulatorWidget::note_overlay_presented() {
    if (frame_pending_) {
        frame_pending_ = false;
        ++present_count_;
    }
}

void EmulatorWidget::layout_crt_overlay() {
#ifdef JNEXT_HAVE_CRT_GL
    if (!crt_overlay_) return;
    if (!fullscreen_mode_) {
        crt_overlay_->setGeometry(rect());
        return;
    }
    // Same rectangle prescale() gives the software picture in fullscreen.
    const qreal dpr = devicePixelRatioF();
    const int pw = qRound(width() * dpr);
    const int ph = qRound(height() * dpr);
    const int s = crt_shader::fullscreen_scale(pw, ph, NATIVE_W, DISPLAY_H);
    const int tw = NATIVE_W * s;
    const int th = DISPLAY_H * s;
    crt_overlay_->setGeometry(qRound((pw - tw) / (2.0 * dpr)), qRound((ph - th) / (2.0 * dpr)),
                              qRound(tw / dpr), qRound(th / dpr));
#endif
}

void EmulatorWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layout_crt_overlay();
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
        const int fs_scale = crt_shader::fullscreen_scale(pw, ph, NATIVE_W, DISPLAY_H);
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
    // The CRT shader overlay covers the picture and does its own accounting.
    if (crt_overlay_) return;
    if (scaled_.isNull()) return;

    // Task 63 — count this as a PRESENT only if it is serving a frame not yet
    // shown. See frame_pending_ in update_frame().
    const bool serving_new_frame = frame_pending_;
    if (frame_pending_) {
        frame_pending_ = false;
        ++present_count_;
    }

    // Task 63 tick-stats — time the paint only when it serves a NEW frame;
    // spontaneous re-blits are excluded (see take_paint_stats() in the header).
    const auto paint_t0 = std::chrono::steady_clock::now();

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

    if (serving_new_frame) {
        tick_stats::add_sample(
            paint_stats_,
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - paint_t0).count());
    }
}
