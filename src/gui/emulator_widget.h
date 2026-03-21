#pragma once

#include <QWidget>
#include <QImage>
#include <cstdint>

/// Scale mode for the emulator display widget.
enum class ScaleMode {
    Integer,    ///< Largest integer scale factor that fits (crisp pixels).
    Stretch,    ///< Fill entire widget area (may distort aspect ratio).
    AspectFit   ///< Maintain 4:3 aspect ratio, letterbox/pillarbox as needed.
};

/// Emulator display widget — renders the emulator framebuffer as a QImage.
///
/// Supports integer scaling, stretch, and 4:3 aspect-fit modes.
/// The framebuffer is expected to be ARGB8888 format (matching Emulator::get_framebuffer()).
class EmulatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit EmulatorWidget(QWidget* parent = nullptr);

    /// Update the displayed frame.  Copies framebuffer data and schedules a repaint.
    /// @param framebuffer  Pointer to w*h uint32_t pixels in ARGB8888 format.
    /// @param w            Framebuffer width in pixels.
    /// @param h            Framebuffer height in pixels.
    void update_frame(const uint32_t* framebuffer, int w, int h);

    /// Set the scaling mode.
    void set_scale_mode(ScaleMode mode);
    ScaleMode scale_mode() const { return scale_mode_; }

    /// Enable or disable the CRT scanline filter overlay.
    void set_crt_filter(bool enabled);
    bool crt_filter() const { return crt_filter_; }

    /// Native framebuffer dimensions (320x256 for ZX Next).
    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;

    /// Target display aspect ratio (4:3 TV).
    static constexpr double TARGET_ASPECT = 4.0 / 3.0;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Compute the destination rectangle for the current scale mode and widget size.
    QRect compute_dest_rect() const;

    QImage image_;
    ScaleMode scale_mode_ = ScaleMode::AspectFit;
    bool crt_filter_ = false;
};
