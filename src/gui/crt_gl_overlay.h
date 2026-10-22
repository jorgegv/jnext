#pragma once

// The OpenGL CRT display filter (doc/design/CRT-GL-FILTER.md).
//
// A QOpenGLWidget that sits on top of EmulatorWidget and draws the emulator
// frame through crtemu (third_party/crtemu). It exists ONLY while the filter is
// switched on: EmulatorWidget creates it on demand and deletes it on switch-off,
// so with the filter off there is no widget, no GL context, no shader and no
// per-frame work — the display path is byte-for-byte the one that existed before.
//
// Compiled only when CMake found Qt6 OpenGLWidgets (JNEXT_HAVE_CRT_GL).

#ifdef JNEXT_HAVE_CRT_GL

#include <QOpenGLWidget>
#include <QSize>
#include <QString>
#include <cstdint>
#include <vector>

#include "gui/crt_shader_policy.h"

struct crtemu_t;

class CrtGlOverlay : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit CrtGlOverlay(QWidget* parent, crt_shader::Style style);
    ~CrtGlOverlay() override;

    /// Take the latest ARGB32 framebuffer (w*h words) and schedule a repaint.
    /// Keeps only the newest frame: if two arrive before a paint, the older is dropped.
    void submit(const uint32_t* argb, int w, int h);

    /// True once crtemu is built and has drawn (or is able to draw) frames.
    bool ready() const { return crt_ != nullptr; }

    /// Size of the newest frame handed to submit() (0x0 before the first).
    QSize frame_size() const { return QSize(frame_w_, frame_h_); }
    crt_shader::Style style() const { return style_; }

    /// Copy the newest frame back out as ARGB32 (w*h words); false if none yet.
    bool copy_frame_argb(std::vector<uint32_t>& out) const;

signals:
    /// A frame reached the screen (crtemu_present returned). The owner counts it
    /// exactly as it counts a paintEvent of the software path.
    void presented();

    /// The filter cannot run here (no context, GLSL 1.20 rejected, ...). The owner
    /// must drop the overlay and fall back to the software display.
    void failed(const QString& reason);

protected:
    void initializeGL() override;
    void paintGL() override;
    void paintEvent(QPaintEvent* event) override;

private:
    /// Destroy crtemu while its context is current. Called when the context is about
    /// to go (Qt may recreate it on window re-creation or reparenting) and from the
    /// destructor; safe to call twice.
    void release_crtemu();

    QMetaObject::Connection ctx_conn_;   ///< aboutToBeDestroyed -> release_crtemu
    crtemu_t* crt_ = nullptr;
    crt_shader::Style style_;
    std::vector<uint32_t> abgr_;   ///< newest frame, already in crtemu's byte order
    int frame_w_ = 0;
    int frame_h_ = 0;
    bool failure_reported_ = false;
    int64_t t0_us_ = 0;            ///< crtemu animates (flicker, noise) against this clock
};

#endif  // JNEXT_HAVE_CRT_GL
