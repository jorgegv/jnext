#pragma once

#include <QWidget>
#include <QImage>
#include <cstdint>

/// Emulator display widget — renders the emulator framebuffer as a QImage.
///
/// The in-memory framebuffer is 640×256; the widget displays it at
/// 640×512 (vertical 2× scaling) at 1× scale_, scaled to integer multiples
/// thereof. One in-memory row paints two display rows for square-pixel
/// 4:3 output. The vertical doubling is performed during prescale via the
/// generic nearest-neighbour mapping `dy * NATIVE_H / target_h` — when
/// target_h = NATIVE_H × 2 each src row gets sampled twice.
class EmulatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit EmulatorWidget(QWidget* parent = nullptr);

    /// Update the displayed frame.  Copies framebuffer data and schedules a repaint.
    /// @param framebuffer  Pointer to w*h uint32_t pixels in ARGB8888 format.
    /// @param w            Framebuffer width in pixels.
    /// @param h            Framebuffer height in pixels.
    void update_frame(const uint32_t* framebuffer, int w, int h);

    /// Set the integer scale factor (2-4).  Resizes the widget to NATIVE_W*s x DISPLAY_H*s.
    void set_scale(int factor);
    int scale() const { return scale_; }

    /// Enable or disable the CRT scanline filter overlay.
    void set_crt_filter(bool enabled);
    bool crt_filter() const { return crt_filter_; }

    /// Set fullscreen mode.  In fullscreen the widget fills the screen but
    /// the framebuffer is rendered at the largest integer scale that fits,
    /// centered with black bars (letterboxing).
    void set_fullscreen_mode(bool fs);
    bool fullscreen_mode() const { return fullscreen_mode_; }

    /// Native framebuffer dimensions (640×256 in-memory, post-G104).
    static constexpr int NATIVE_W = 640;
    static constexpr int NATIVE_H = 256;
    /// Logical display height after vertical 2× scaling — 512. Used for
    /// window-sizing arithmetic so that scale=1 is geometrically square
    /// (640×512). Phase 7 wires the actual vertical doubling at prescale.
    static constexpr int DISPLAY_H = NATIVE_H * 2;

    /// Minimum and maximum supported scale factors.
    /// Post-G104 the in-memory framebuffer is 640×256 displayed at 640×512
    /// (vertical 2× scaling — see DISPLAY_H above).  Scale=1 → 640×512
    /// physical; scale=2 → 1280×1024; scale=3 → 1920×1536.  Pre-G104 the
    /// range was 2..4 to compensate for the 320×256 native size; post-G104
    /// 1..3 covers the equivalent physical-pixel envelope without making
    /// the default window 4× bigger than it used to be.
    static constexpr int MIN_SCALE = 1;
    static constexpr int MAX_SCALE = 3;

signals:
    /// Emitted once, on the first frame, after the widget re-applies its
    /// scale with the correct devicePixelRatio.  MainWindow connects to
    /// this to re-fix its window size.
    void scale_changed();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Software-scale native_ into scaled_ at the current scale factor.
    void prescale();

    QImage native_;   ///< Original framebuffer (640×256 ARGB32).
    QImage scaled_;   ///< Pre-scaled framebuffer (drawn 1:1, no painter scaling).
    int scale_ = MIN_SCALE;
    bool crt_filter_ = false;
    bool fullscreen_mode_ = false;
    bool dpr_valid_ = false;  ///< True once DPR has been verified on-screen.
    QPoint fs_offset_;        ///< Top-left offset for centered image in fullscreen.
};
