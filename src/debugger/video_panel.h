#pragma once

#include <QWidget>
#include <QLabel>
#include <QImage>

#include "debug/inspect.h"
#include "debug/raster_state.h"

namespace jnext { namespace dbg { class Debugger; } }
class QTabWidget;
class QRadioButton;

// ---------------------------------------------------------------------------
// VideoLayerView — displays a single video layer (640×256) for the debugger.
//
// GH #278 WP4d: the picture comes from the debugger backend's INS-14
// `render_layer()`, which draws the view's rows 0..vc with alpha 0 wherever the
// view is transparent. What stays here is presentation: the QImage, the
// "not rendered yet" rows and the running placeholder, the checkerboard under
// every transparent cell, the DPR scaling, the red raster line and the title.
// ---------------------------------------------------------------------------

class VideoLayerView : public QWidget {
    Q_OBJECT
public:
    /// The eight views — the backend's own enum (`debug/inspect.h`), which is
    /// where the render lives now.
    using Layer = jnext::dbg::Layer;

    /// @param dbg  the backend to render through; null shows the placeholder.
    VideoLayerView(Layer layer, const char* title,
                   const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Switch which layer/screen is displayed and force a re-render.
    void setLayer(Layer layer);

    /// Re-render the layer for the given scanline position.
    /// @param vc  Current vertical counter (0-255).  Pass -1 when running
    ///            (shows dim placeholder).
    void refresh(int vc);

    /// Force re-render on the next refresh() call (e.g. after a tab switch).
    /// -2, never -1: -1 means "the running placeholder is shown", the one
    /// state refresh() may skip.
    void invalidate() { last_vc_ = -2; }

    /// The rendered layer image (test seam — see test/debugger/video_panel_test.cpp).
    /// Width is 640 (`jnext::dbg::RENDER_WIDTH`) for every layer; height is NATIVE_H.
    const QImage& image() const { return image_; }

    /// The title painted above the image (test seam, like image()).
    const QString& title() const { return title_; }

    /// Layout constants shared by all VideoLayerView instances.
    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
    static constexpr int DISPLAY_SCALE = 2;   ///< same scale as emulator viewport default
    static constexpr int TITLE_H  = 14;
    static constexpr int MARGIN   = 12;       ///< margin on all sides around the image

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void render_to_image(int vc);

    Layer                        layer_;
    QString                      title_;
    const jnext::dbg::Debugger*  dbg_;
    QImage                       image_;     ///< RENDER_WIDTH × NATIVE_H, ARGB32
    int                          last_vc_ = -2;
};

// ---------------------------------------------------------------------------
// VideoPanel — debugger panel showing raster position, layer state,
//              priority, ULA palette, and per-layer sub-panel views.
// ---------------------------------------------------------------------------

/// Which layers are enabled (ULA, Layer 2, Tilemap, Sprites) and the NR 0x15 layer
/// priority, read through the backend's NextREG peek (the read handlers' value,
/// never the raw register cache) — see the definition for why that distinction
/// is load-bearing (Task 40).
/// Exposed so the panel's answer can be tested without a QWidget.
void video_panel_layer_state(const jnext::dbg::Debugger& dbg, bool active_out[4],
                             int& priority_out);

/// The raster state the Video panel displays (GH #22): the backend's INS-06
/// `raster()`.
///
/// Derived from the emulator's PAUSED raster snapshot — `paused_hc()` /
/// `paused_vc()`, the clock-derived pair — and the live ULA mode registers.
/// It deliberately does NOT read `VideoTiming::pos()`: those counters are only
/// advanced when a debugger is attached (emulator.cpp, "Task 27 C10"), so they
/// are a debug observable rather than the authoritative position.  VideoTiming
/// is used only for its per-machine CONSTANTS (line/frame length, the
/// active-display origin, the blanking limits and the NR 0x64 copper offset),
/// which is what keeps this indicator on the emulator's own timing model
/// instead of a second hand-written table.
///
/// Exposed so the panel's answer can be tested without a QWidget.
RasterState video_panel_raster_state(const jnext::dbg::Debugger& dbg);

class VideoPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4d).
    explicit VideoPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Update display with current video state.
    void refresh();

    /// Convert the raw raster vertical counter into the framebuffer row the
    /// layer views index.
    ///
    /// G164v2 / Task 13: `fb_row = raw_vc - vblank_top`, where vblank_top is
    /// the number of raw-VC lines above the framebuffer's top border — 32 on
    /// the NEXT family / 48K / 128K / +3 50 Hz, 48 on Pentagon, 8 on the 60 Hz
    /// overrides (VideoTiming::vblank_top()).  Renderer::render_frame,
    /// Emulator::on_scanline and every per-scanline change log already index
    /// by fb_row; the panel must too.
    ///
    /// Returns a value < 0 while the raster is still in the top vblank (no row
    /// has been drawn yet), and is clamped to FB_HEIGHT-1 at the bottom.
    static int fb_row_for_vc(int raw_vc, int vblank_top);

    QSize sizeHint() const override { return QSize(600, 600); }

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;

    // Raster position + ULA fetch indicator (GH #22).  Each line names the
    // VHDL counter it shows: the four have four different origins, and the
    // panel is the place that has to say which is which.
    QLabel*  raw_label_       = nullptr;   ///< VHDL hc / vc (frame counters)
    QLabel*  ula_label_       = nullptr;   ///< VHDL o_hc_ula / o_vc_ula
    QLabel*  cvc_label_       = nullptr;   ///< VHDL o_vc_cu — NR 0x1E/0x1F
    QLabel*  pixel_label_     = nullptr;   ///< VHDL o_phc / o_vc_ula, paper only
    QLabel*  region_label_    = nullptr;   ///< Blanking / Border / Paper
    QLabel*  fetch_label_     = nullptr;   ///< Idle / Bitmap / Attribute
    QWidget* raster_diagram_  = nullptr;   ///< beam position on a frame map

    // Layer flags (0=ULA, 1=Layer2, 2=Tilemap, 3=Sprites)
    QLabel* layer_flags_[4] = {};

    // Layer priority flags (0=SLU .. 5=ULS)
    QLabel* prio_flags_[6]  = {};

    // Palette swatch
    QWidget* palette_widget_ = nullptr;

    // Sub-panel layer views (one per tab)
    QTabWidget*      layer_tabs_    = nullptr;
    VideoLayerView*  composite_view_ = nullptr; ///< "All layers" tab (leftmost, default)
    VideoLayerView*  ula_view_      = nullptr;  ///< ULA tab (primary or shadow)
    VideoLayerView*  l2_view_       = nullptr;  ///< Layer2 tab (active or shadow)
    VideoLayerView*  sprites_view_  = nullptr;
    VideoLayerView*  tilemap_view_  = nullptr;
    VideoLayerView*  background_view_ = nullptr;  ///< "Background" tab (rightmost)

    // Screen-select radio buttons
    QRadioButton*    ula_rb_primary_  = nullptr;
    QRadioButton*    ula_rb_shadow_   = nullptr;
    QRadioButton*    l2_rb_active_    = nullptr;
    QRadioButton*    l2_rb_shadow_    = nullptr;
};
