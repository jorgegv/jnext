#pragma once

#include <QWidget>
#include <QImage>
#include <cstdint>

/// Emulator display widget — renders the emulator framebuffer as a QImage.
///
/// The widget is always sized to an exact integer multiple of the native
/// framebuffer (320x256), guaranteeing pixel-perfect rendering with
/// nearest-neighbour scaling.
class EmulatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit EmulatorWidget(QWidget* parent = nullptr);

    /// Update the displayed frame.  Copies framebuffer data and schedules a repaint.
    /// @param framebuffer  Pointer to w*h uint32_t pixels in ARGB8888 format.
    /// @param w            Framebuffer width in pixels.
    /// @param h            Framebuffer height in pixels.
    void update_frame(const uint32_t* framebuffer, int w, int h);

    /// Set the integer scale factor (2-4).  Resizes the widget to NATIVE_W*s x NATIVE_H*s.
    void set_scale(int factor);
    int scale() const { return scale_; }

    /// Enable or disable the CRT scanline filter overlay.
    void set_crt_filter(bool enabled);
    bool crt_filter() const { return crt_filter_; }

    /// Native framebuffer dimensions (320x256 for ZX Next).
    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;

    /// Minimum and maximum supported scale factors.
    static constexpr int MIN_SCALE = 2;
    static constexpr int MAX_SCALE = 4;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Software-scale native_ into scaled_ at the current scale factor.
    void prescale();

    QImage native_;   ///< Original framebuffer (320x256 ARGB32).
    QImage scaled_;   ///< Pre-scaled framebuffer (drawn 1:1, no painter scaling).
    int scale_ = MIN_SCALE;
    bool crt_filter_ = false;
};
