#pragma once

#include <QWidget>
#include <QImage>
#include <cstdint>

/// Emulator display widget — renders the emulator framebuffer as a QImage.
///
/// Uses integer scaling centered within the widget area.
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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
};
