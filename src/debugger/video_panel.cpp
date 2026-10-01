#include "debugger/video_panel.h"
#include "debug/debugger.h"

#include <QShowEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QTabWidget>
#include <QRadioButton>
#include <QPaintEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// PaletteSwatchWidget — 32 std-ULA palette entries in a horizontal row.
// G102: shows the full std-ULA encoder range (0x00..0x1F = 4 sub-cycles
// × 8 colours = ink, paper, ink-bright, paper-bright sub-bands of the
// 256-entry × 2-bank ULA palette per VHDL zxula.vhd:543-553).
// ---------------------------------------------------------------------------

class PaletteSwatchWidget : public QWidget {
public:
    static constexpr int N = 32;

    explicit PaletteSwatchWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(CELL + 1);
        std::fill(std::begin(colours_), std::end(colours_), 0xFF000000u);
    }

    void set_colours(const uint32_t colours[N]) {
        std::copy(colours, colours + N, colours_);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(Qt::gray);
        int cell = width() / N;
        if (cell < 1) cell = 1;
        for (int i = 0; i < N; ++i) {
            uint32_t argb = colours_[i];
            QColor c(static_cast<int>((argb >> 16) & 0xFF),
                     static_cast<int>((argb >>  8) & 0xFF),
                     static_cast<int>( argb        & 0xFF));
            p.fillRect(i * cell, 0, cell, CELL, c);
            p.drawRect( i * cell, 0, cell, CELL);
        }
    }

private:
    static constexpr int CELL = 20;
    uint32_t colours_[N]{};
};

// ---------------------------------------------------------------------------
// RasterDiagramWidget — the beam position on a map of the whole frame (GH #22).
//
// Three nested rectangles, all in RAW frame-counter space, all with their
// bounds taken from the live VideoTiming (never from constants of its own):
//
//   blanking  the whole frame minus [c_max_hblank+1, c_max_vblank+1)
//             (VHDL zxula_timing.vhd:348-357)
//   paper     raw hc [c_min_hactive+1, c_min_hactive+256] x
//             raw vc [c_min_vactive,   c_min_vactive+191]
//             — `border_active` is `i_phc(8) or border_active_v`
//             (zxula.vhd:414-415), and phc reads 0 at raw hc c_min_hactive+1
//             (zxula.vhd:43-46), which is where the one-pixel offset comes from
//   border    whatever the other two leave
//
// The regions really are rectangles in this space, so this is the same
// classification RasterState makes, drawn rather than named — not a second
// model of it.
// ---------------------------------------------------------------------------

class RasterDiagramWidget : public QWidget {
public:
    static constexpr int W = 200;
    static constexpr int H = 136;

    explicit RasterDiagramWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(W, H);
        setToolTip(QObject::tr(
            "Beam position on the frame.  Light = paper (256x192), "
            "mid = border, dark = blanking.\n"
            "Updated only while the emulator is paused."));
    }

    /// Per-machine frame geometry, straight from VideoTiming.
    void set_frame(int ticks_per_line, int lines_per_frame,
                   int max_hblank, int max_vblank,
                   int min_hactive, int min_vactive)
    {
        ticks_per_line_  = ticks_per_line;
        lines_per_frame_ = lines_per_frame;
        max_hblank_      = max_hblank;
        max_vblank_      = max_vblank;
        min_hactive_     = min_hactive;
        min_vactive_     = min_vactive;
        update();
    }

    /// Beam position in raw frame counters; `valid` is false while running,
    /// when the panel has no meaningful position to show.
    void set_beam(int raw_hc, int raw_vc, bool valid)
    {
        beam_hc_    = raw_hc;
        beam_vc_    = raw_vc;
        beam_valid_ = valid;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const double sx = static_cast<double>(W) / ticks_per_line_;
        const double sy = static_cast<double>(H) / lines_per_frame_;

        // Blanking fills the whole frame; the visible area is painted over it.
        p.fillRect(0, 0, W, H, QColor(0x1B, 0x1B, 0x22));

        auto rect_for = [&](int hc0, int hc1, int vc0, int vc1) {
            const int x0 = static_cast<int>(hc0 * sx);
            const int y0 = static_cast<int>(vc0 * sy);
            const int x1 = static_cast<int>((hc1 + 1) * sx);
            const int y1 = static_cast<int>((vc1 + 1) * sy);
            return QRect(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0));
        };

        p.fillRect(rect_for(max_hblank_ + 1, ticks_per_line_ - 1,
                            max_vblank_ + 1, lines_per_frame_ - 1),
                   QColor(0x4A, 0x5A, 0x78));                       // border
        p.fillRect(rect_for(min_hactive_ + 1, min_hactive_ + 256,
                            min_vactive_,     min_vactive_ + 191),
                   QColor(0xC8, 0xD4, 0xE8));                       // paper

        p.setPen(QColor(0x80, 0x80, 0x80));
        p.drawRect(0, 0, W - 1, H - 1);

        if (!beam_valid_) return;

        // The scanline the beam is on, then the beam itself.
        const int by = static_cast<int>(beam_vc_ * sy);
        const int bx = static_cast<int>(beam_hc_ * sx);
        p.setPen(QColor(0xFF, 0x40, 0x40, 0x80));
        p.drawLine(0, by, W - 1, by);
        p.setPen(QColor(0xFF, 0x20, 0x20));
        p.drawLine(bx, std::max(0, by - 4), bx, std::min(H - 1, by + 4));
        p.fillRect(QRect(bx - 1, by - 1, 3, 3), QColor(0xFF, 0x20, 0x20));
    }

private:
    int  ticks_per_line_  = 456;
    int  lines_per_frame_ = 311;
    int  max_hblank_      = 95;
    int  max_vblank_      = 7;
    int  min_hactive_     = 136;
    int  min_vactive_     = 64;
    int  beam_hc_         = 0;
    int  beam_vc_         = 0;
    bool beam_valid_      = false;
};

// ---------------------------------------------------------------------------
// VideoLayerView
// ---------------------------------------------------------------------------

// Checkerboard tile size for transparent pixel indication.
static constexpr int CHECK_SZ = 8;

// Every view's width: the canonical framebuffer width the backend renders.
static constexpr int RENDER_W = static_cast<int>(jnext::dbg::RENDER_WIDTH);

// Dark background colour for "not yet rendered" rows.
static constexpr uint32_t UNRENDERED_ARGB   = 0xFF111111;
// Light checkerboard for transparent areas — matches typical image editor style.
static constexpr uint32_t CHECKER_DARK_ARGB = 0xFFAAAAAA;
static constexpr uint32_t CHECKER_LITE_ARGB = 0xFFCCCCCC;

// Paint the checkerboard over every zero-alpha cell: the panel's convention is
// that transparent areas show the checkerboard.  The backend draws a view with
// alpha 0 wherever it is transparent — a cell the layer does not paint, one
// its clip window removes (the ULA's NR 0x1A clip, applied by the compositor
// stage; the tilemap's NR 0x1B clip, which writes 0 itself) — so this one pass
// covers every view alike (GH #278 WP4d, design-qt §3.7).
static void restore_checker_where_transparent(uint32_t* dst, int row, int width)
{
    for (int x = 0; x < width; ++x) {
        if ((dst[x] & 0xFF000000u) != 0) continue;
        const bool dark = (((row / CHECK_SZ) ^ (x / CHECK_SZ)) & 1) != 0;
        dst[x] = dark ? CHECKER_DARK_ARGB : CHECKER_LITE_ARGB;
    }
}

// The per-scanline state replay that makes each view show the register state
// the raster saw on each row (raster splits, per-line palettes, scroll splits)
// lives with the render in the backend now: `jnext::dbg::Debugger::render_layer`
// (`src/debug/debugger_render.cpp`, GH #278 WP4d).

// Which layers are enabled, and in what priority order — read through the NextREG
// READ HANDLERS, never from the raw register cache.
//
// `NextReg::cached()` returns regs_[reg]: the last byte someone wrote to that NextREG
// number, and nothing else. But a layer enable is not owned by its NextREG. Layer 2's
// enable is a single FF that BOTH NR 0x69 bit 7 and port 0x123B bit 1 latch (VHDL
// zxnext.vhd:3916, 3924-3925), and port 0x123B is how programs actually turn Layer 2
// on — it never touches regs_[0x69]. That is precisely why NR 0x69 has a read handler
// composing the live value from Layer2/Mmu/port_ff_reg; emulator.cpp:2779 says so in
// as many words ("The bare regs_[0x69] would only echo the last NR 0x69 write and miss
// port 0x123B / 0x7FFD / 0xFF mid-stream changes").
//
// The panel read the raw cache and so reported beast.nex — which enables Layer 2 via
// port 0x123B — as having Layer 2 OFF, while the Layer 2 view right next to it showed
// the demo's graphics. Raw NR 0x69 = 0x00; true value = 0xC0 (Task 40).
//
// All four of these registers have read handlers (0x15 recomposes priority and
// sprite_en from the renderer, 0x68 bit 3 from the ULA, 0x6B from the live tilemap
// control), so reading any of them from the cache is a bug waiting to happen.
//
// peek(), not read(): the same value, but read() is the *Z80's* read — it emits a trace
// line, and this panel refreshes several times a second with the guest running, so it
// would inject phantom NextREG reads into the log used to diagnose NextREG traffic.
// (None of these four handlers mutates state, so read() would be safe here — but the
// debugger has its own read, and this is it.)
// GH #22 — the raster/ULA-fetch answer the panel shows, without a QWidget.
//
// Deliberately derived from the master clock (Debugger::raster(), which equals
// Emulator::paused_hc()/paused_vc() while paused), NOT from
// VideoTiming::pos(): the latter's counters are only advanced when a debugger
// is attached (emulator.cpp, "Task 27 C10 ... purely the debug observable"),
// while the clock is what every other raster consumer (NR 0x1E/0x1F, the
// Copper, contention) also uses.
//
// The ULA mode inputs are the live registers, because they decide WHAT is
// being fetched: port 0xFF bits 2:0 select the Timex modes whose "attribute"
// slots fetch a second bitmap plane instead, and the shadow-screen bit forces
// screen_mode to "000" (zxula.vhd:191, :238-239, :248-249).
RasterState video_panel_raster_state(const jnext::dbg::Debugger& dbg)
{
    // The backend's INS-06 derivation is this one, with the same four inputs.
    return dbg.raster();
}

void video_panel_layer_state(const jnext::dbg::Debugger& dbg, bool active_out[4],
                             int& priority_out)
{
    const uint8_t reg15 = dbg.nextreg_peek(0x15);
    const uint8_t reg68 = dbg.nextreg_peek(0x68);
    const uint8_t reg69 = dbg.nextreg_peek(0x69);
    const uint8_t reg6b = dbg.nextreg_peek(0x6B);

    active_out[0] = !(reg68 & 0x80);   // ULA     (bit 7 = DISABLE)
    active_out[1] = !!(reg69 & 0x80);  // Layer 2
    active_out[2] = !!(reg6b & 0x80);  // Tilemap
    active_out[3] = !!(reg15 & 0x01);  // Sprites
    priority_out  = (reg15 >> 2) & 0x07;
}

VideoLayerView::VideoLayerView(Layer layer, const char* title,
                               const jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , layer_(layer)
    , title_(QString::fromLatin1(title))
    , dbg_(dbg)
    , image_(RENDER_W, NATIVE_H, QImage::Format_ARGB32)
{
    image_.fill(UNRENDERED_ARGB);
    // Don't call setFixedSize here — the widget's DPR is unknown until it is
    // placed on a screen.  showEvent() will call setFixedSize(sizeHint()) once
    // the real DPR is known.
}

QSize VideoLayerView::sizeHint() const
{
    // Use the widget's own DPR if it has been assigned to a screen (> 0),
    // otherwise fall back to the primary screen.
    const qreal dpr = (devicePixelRatioF() > 0.0)
                    ? devicePixelRatioF()
                    : (QGuiApplication::primaryScreen()
                       ? QGuiApplication::primaryScreen()->devicePixelRatio()
                       : 1.0);
    return QSize(qRound(NATIVE_W * DISPLAY_SCALE / dpr) + 2 * MARGIN,
                 TITLE_H + qRound(NATIVE_H * DISPLAY_SCALE / dpr) + 2 * MARGIN);
}

void VideoLayerView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Now the widget's DPR is known — lock size to exact 2× native + margins.
    setFixedSize(sizeHint());
}

void VideoLayerView::setLayer(Layer layer)
{
    if (layer_ == layer) return;
    layer_ = layer;
    last_vc_ = -2;  // force re-render
}

void VideoLayerView::refresh(int vc)
{
    // Only skip re-render for the dim running placeholder (vc < 0), and only
    // when the placeholder is what is ALREADY shown (last_vc_ == -1). When
    // paused (vc >= 0), always re-render: registers (scroll, palette, tile
    // data) may have changed even if the scanline position is the same (e.g.
    // EOF → EOF stepping stays at vc=255 across frames).
    //
    // GH #278 WP0 — `last_vc_ < 0` also matched invalidate()'s and setLayer()'s
    // -2 ("force re-render"), so a tab switched to while running kept the
    // picture it was last rendered with during a pause.
    if (vc < 0 && last_vc_ == -1) return;
    last_vc_ = vc;
    render_to_image(vc);
    update();
}

void VideoLayerView::render_to_image(int vc)
{
    // Rows the raster has not reached yet this frame — and, when running or
    // with no backend, the whole placeholder — are the "not rendered" colour.
    image_.fill(UNRENDERED_ARGB);
    if (!dbg_ || vc < 0) return;

    // The backend draws rows 0..vc over a 0x00000000 fill (INS-14) and leaves
    // the rows below alone.
    auto* bits = reinterpret_cast<uint32_t*>(image_.bits());
    const size_t stride = static_cast<size_t>(image_.bytesPerLine()) / sizeof(uint32_t);
    if (!jnext::dbg::ok(dbg_->render_layer(layer_, vc, bits, stride))) return;

    for (int row = 0; row <= vc; ++row)
        restore_checker_where_transparent(
            reinterpret_cast<uint32_t*>(image_.scanLine(row)), row, RENDER_W);

    // Name the register value in the title, matching how the other views label
    // themselves.  NR 0x4A through the backend's peek — the value the register
    // holds at the end of the frame so far; when the Copper animates it, the
    // bands in the image are the honest per-row story.
    if (layer_ == Layer::Background) {
        title_ = QString::asprintf("Background colour (NR 0x4A = $%02X)",
                                   dbg_->nextreg_peek(0x4A));
    }

    // The two ULA views pin their bank (that is the point of having both), so the one
    // the ULA is NOT currently reading shows whatever happens to be in that bank —
    // usually junk. Correct, and deeply confusing: beast.nex renders its sky from the
    // SHADOW screen, so the default "Primary (bank 5)" view is a screenful of garbage
    // and looks like a broken panel. Say which bank is live, so the garbage explains
    // itself (Task 40).
    if (layer_ == Layer::UlaPrimary || layer_ == Layer::UlaShadow) {
        const bool showing_bank7 = (layer_ == Layer::UlaShadow);
        const bool live_bank7    = dbg_->ula_screen_regs().active_bank == 7;
        title_ = showing_bank7 ? QStringLiteral("ULA shadow (bank 7)")
                               : QStringLiteral("ULA primary (bank 5)");
        title_ += (showing_bank7 == live_bank7)
                    ? QStringLiteral(" — LIVE: the ULA is reading this bank")
                    : QString(" — NOT live: the ULA is reading bank %1")
                          .arg(live_bank7 ? 7 : 5);
    }
}

void VideoLayerView::paintEvent(QPaintEvent*)
{
    // Ensure size reflects the current DPR.  showEvent() sets this, but the very
    // first paint can fire before the layout has applied the new fixed size.
    // Returning here causes Qt to immediately schedule a correctly-sized repaint.
    const QSize needed = sizeHint();
    if (size() != needed) {
        setFixedSize(needed);
        return;
    }

    QPainter p(this);

    // Pre-scale the source image (640×256) to fill the physical content area.
    const qreal dpr    = devicePixelRatioF();
    const int   phys_w = qRound((width()  - 2 * MARGIN) * dpr);
    const int   phys_h = qRound((height() - TITLE_H - 2 * MARGIN) * dpr);
    const int   img_w  = image_.width();
    const int   img_h  = image_.height();

    QImage scaled(phys_w, phys_h, QImage::Format_ARGB32);
    for (int sy = 0; sy < phys_h; ++sy) {
        const auto* src = reinterpret_cast<const uint32_t*>(
            image_.scanLine(sy * img_h / phys_h));
        auto* dst = reinterpret_cast<uint32_t*>(scaled.scanLine(sy));
        for (int sx = 0; sx < phys_w; ++sx)
            dst[sx] = src[sx * img_w / phys_w];
    }
    // Tag with DPR so Qt maps each physical pixel 1:1 to the screen.
    scaled.setDevicePixelRatio(dpr);

    // Draw at (MARGIN, TITLE_H + MARGIN); the image's logical size is
    // phys_w/dpr × phys_h/dpr which exactly fits the content area.
    p.drawImage(QPoint(MARGIN, TITLE_H + MARGIN), scaled);

    // Title above the image.
    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);
    p.setPen(Qt::lightGray);
    p.drawText(QRect(0, 0, width(), TITLE_H),
               Qt::AlignLeft | Qt::AlignVCenter, title_);

    // Red scanline indicator when paused.
    // Spans the full widget width so it's visible in the margins even when
    // image content is red or the scanline is at the very bottom.
    if (last_vc_ >= 0) {
        // Logical y: top of image + (vc * logical image height / 256)
        const int log_img_h = qRound(phys_h / dpr);
        const int y_line    = TITLE_H + MARGIN + (last_vc_ + 1) * log_img_h / NATIVE_H;
        p.setPen(QPen(Qt::red, 1));
        p.drawLine(0, y_line, width() - 1, y_line);
    }
}

// ---------------------------------------------------------------------------
// VideoPanel
// ---------------------------------------------------------------------------

VideoPanel::VideoPanel(const jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    create_ui();
}

int VideoPanel::fb_row_for_vc(int raw_vc, int vblank_top)
{
    // See the declaration in video_panel.h for the G164v2 rationale.
    const int fb_row = raw_vc - vblank_top;
    return std::min(fb_row, VideoLayerView::NATIVE_H - 1);
}

void VideoPanel::create_ui()
{
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    QFont mono_bold = mono;
    mono_bold.setBold(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    // Helper: bold heading label — all padded to same width with monospace font.
    auto make_bold = [&](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text, this);
        lbl->setFont(mono_bold);
        return lbl;
    };

    // Helper: regular value label.
    auto make_val = [&](const QString& text = "---") -> QLabel* {
        auto* lbl = new QLabel(text, this);
        lbl->setFont(mono);
        return lbl;
    };

    // ── Raster position + ULA fetch (GH #22) ─────────────────────────────────
    //
    // Four counters with four origins, each line naming the VHDL signal it
    // shows.  The labelling is the feature: reading NR 0x1E/0x1F as a raw
    // frame line is GH #16, and comparing a 28 MHz count against the ULA's
    // 7 MHz hc_ula is GH #181.
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);

        auto* col = new QVBoxLayout();
        col->setSpacing(1);

        auto* head = new QHBoxLayout();
        head->setSpacing(4);
        head->addWidget(make_bold("Raster:"));
        head->addStretch();
        col->addLayout(head);

        raw_label_   = make_val("raw     hc:----  vc:----  VHDL hc / vc (frame)");
        ula_label_   = make_val("ULA     hc:----  vc:----  o_hc_ula / o_vc_ula");
        cvc_label_   = make_val("Copper cvc:----           NR 0x1E/0x1F read THIS");
        pixel_label_ = make_val("Pixel    x:----   y:----  o_phc / o_vc_ula");
        // Named so the panel test can read them back and pin the
        // paused-only contract this block and the tooltip both state.
        raw_label_->setObjectName(QStringLiteral("rasterRaw"));
        ula_label_->setObjectName(QStringLiteral("rasterUla"));
        cvc_label_->setObjectName(QStringLiteral("rasterCvc"));
        pixel_label_->setObjectName(QStringLiteral("rasterPixel"));
        col->addWidget(raw_label_);
        col->addWidget(ula_label_);
        col->addWidget(cvc_label_);
        col->addWidget(pixel_label_);

        auto* state = new QHBoxLayout();
        state->setSpacing(4);
        state->addWidget(make_bold("Region:"));
        region_label_ = make_val("---");
        region_label_->setObjectName(QStringLiteral("rasterRegion"));
        state->addWidget(region_label_);
        state->addSpacing(12);
        state->addWidget(make_bold("ULA fetch:"));
        fetch_label_ = make_val("---");
        fetch_label_->setObjectName(QStringLiteral("rasterFetch"));
        state->addWidget(fetch_label_);
        state->addStretch();
        col->addLayout(state);
        col->addStretch();

        row->addLayout(col, 1);
        raster_diagram_ = new RasterDiagramWidget(this);
        // Named so the panel test can grab it and check the three regions
        // land where the live VideoTiming puts them (GH #22).
        raster_diagram_->setObjectName(QStringLiteral("rasterDiagram"));
        row->addWidget(raster_diagram_, 0, Qt::AlignTop);
        layout->addLayout(row);
    }

    // ── Layers ───────────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("Layers:      "));
        static const char* kLayerNames[4] = { "ULA", "Layer2", "Tilemap", "Sprites" };
        for (int i = 0; i < 4; ++i) {
            layer_flags_[i] = make_val(kLayerNames[i]);
            row->addWidget(layer_flags_[i]);
            if (i < 3) row->addSpacing(8);
        }
        row->addStretch();
        layout->addLayout(row);
    }

    // ── Layer Priority ───────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("Priority:    "));
        static const char* kPrioNames[6] = { "SLU", "LSU", "SUL", "LUS", "USL", "ULS" };
        for (int i = 0; i < 6; ++i) {
            prio_flags_[i] = make_val(kPrioNames[i]);
            row->addWidget(prio_flags_[i]);
            if (i < 5) row->addSpacing(8);
        }
        row->addStretch();
        layout->addLayout(row);
    }

    // ── ULA Palette ──────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("ULA Palette: "));
        palette_widget_ = new PaletteSwatchWidget(this);
        // Named so the panel test can grab it and check the swatch shows the
        // ACTIVE ULA bank's colours (GH #278 WP4d).
        palette_widget_->setObjectName(QStringLiteral("ulaPalette"));
        row->addWidget(palette_widget_, 1);
        layout->addLayout(row);
    }

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #D0D0D0;");
    layout->addWidget(line);

    // ── Layer sub-panels ─────────────────────────────────────────────────────

    layer_tabs_ = new QTabWidget(this);
    layer_tabs_->setTabPosition(QTabWidget::North);

    // All tabs share the same layout: a fixed-size VideoLayerView at the top,
    // then a fixed-height row for radio buttons at the bottom.  Because the
    // view has a fixed size (2× native + margins), all screens are identical
    // in position and size across every tab.
    static constexpr int CTRL_ROW_H = 26;

    auto make_layer_tab = [&](const char* tab_title,
                               VideoLayerView::Layer layer,
                               const char* view_title,
                               const char* rb1_text, QRadioButton** rb1_out,
                               const char* rb2_text, QRadioButton** rb2_out)
        -> VideoLayerView*
    {
        auto* tab  = new QWidget();
        auto* vbox = new QVBoxLayout(tab);
        vbox->setContentsMargins(12, 4, 12, 4);
        vbox->setSpacing(2);

        // Fixed-size screen view, centred so left/right margins are equal.
        auto* view = new VideoLayerView(layer, view_title, dbg_, tab);
        vbox->addWidget(view, 0, Qt::AlignHCenter);

        // Fixed-height control row below the screen.
        auto* ctrl = new QWidget(tab);
        ctrl->setFixedHeight(CTRL_ROW_H);
        auto* rb_row = new QHBoxLayout(ctrl);
        rb_row->setContentsMargins(4, 0, 4, 0);
        rb_row->setSpacing(12);

        if (rb1_text && rb2_text) {
            auto* rb1 = new QRadioButton(tr(rb1_text), ctrl);
            auto* rb2 = new QRadioButton(tr(rb2_text), ctrl);
            rb1->setChecked(true);
            rb_row->addWidget(rb1);
            rb_row->addWidget(rb2);
            if (rb1_out) *rb1_out = rb1;
            if (rb2_out) *rb2_out = rb2;
        }
        rb_row->addStretch();

        vbox->addWidget(ctrl);

        layer_tabs_->addTab(tab, tr(tab_title));
        return view;
    };

    // "All layers" tab — the full composite, i.e. the same image the emulator
    // window shows.  Leftmost and selected by default (Task 36): it is the view
    // that actually explains the screen, since the per-layer views cannot show
    // the NR 0x4A fallback colour (it belongs to no layer).
    composite_view_ = make_layer_tab(
        "All layers",
        VideoLayerView::Layer::Composite, "All layers (composite)",
        nullptr, nullptr, nullptr, nullptr);

    // ULA tab — single view, radio buttons select primary/shadow.
    ula_view_ = make_layer_tab(
        "ULA",
        VideoLayerView::Layer::UlaPrimary, "ULA screen (bank 5/7)",
        "Primary (bank 5)", &ula_rb_primary_,
        "Shadow (bank 7)",  &ula_rb_shadow_);

    connect(ula_rb_primary_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            ula_view_->setLayer(VideoLayerView::Layer::UlaPrimary);
            refresh();
        }
    });
    connect(ula_rb_shadow_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            ula_view_->setLayer(VideoLayerView::Layer::UlaShadow);
            refresh();
        }
    });

    // Layer 2 tab — single view, radio buttons select active/shadow bank.
    l2_view_ = make_layer_tab(
        "Layer2",
        VideoLayerView::Layer::Layer2Active, "Layer 2",
        "Active bank",  &l2_rb_active_,
        "Shadow bank",  &l2_rb_shadow_);

    connect(l2_rb_active_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            l2_view_->setLayer(VideoLayerView::Layer::Layer2Active);
            refresh();
        }
    });
    connect(l2_rb_shadow_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            l2_view_->setLayer(VideoLayerView::Layer::Layer2Shadow);
            refresh();
        }
    });

    // Sprites tab — single view, no radio buttons.
    sprites_view_ = make_layer_tab(
        "Sprites",
        VideoLayerView::Layer::Sprites, "Sprites",
        nullptr, nullptr, nullptr, nullptr);

    // TileMap tab — single view, no radio buttons.
    tilemap_view_ = make_layer_tab(
        "TileMap",
        VideoLayerView::Layer::Tilemap, "TileMap",
        nullptr, nullptr, nullptr, nullptr);

    // Background tab — rightmost.  The NR 0x4A fallback colour: the one thing
    // on screen that belongs to no layer, so no layer view can show it.
    background_view_ = make_layer_tab(
        "Background",
        VideoLayerView::Layer::Background, "Background colour (NR 0x4A)",
        nullptr, nullptr, nullptr, nullptr);

    // When the user switches tabs, invalidate so the new tab renders immediately.
    connect(layer_tabs_, &QTabWidget::currentChanged, this, [this](int) {
        for (VideoLayerView* v : {composite_view_, ula_view_, l2_view_,
                                   sprites_view_, tilemap_view_,
                                   background_view_}) {
            if (v) v->invalidate();
        }
        refresh();
    });

    // "All layers" is the default view.
    layer_tabs_->setCurrentIndex(0);

    layout->addWidget(layer_tabs_);
    layout->addStretch();
}

void VideoPanel::refresh()
{
    if (!dbg_) return;

    // ── Raster position + ULA fetch — paused only (GH #22) ───────────────────
    //
    // paused_vc()/paused_hc() are the RAW frame counters (VHDL hc / vc), taken
    // from the master clock; everything else displayed here is derived from
    // them by RasterState, against the live VideoTiming's per-machine
    // constants.  The panels update only while paused/stepping, deliberately.
    //
    // The layer views, however, index FRAMEBUFFER ROWS, and since G164v2
    // (Task 13) the mapping is
    //
    //     fb_row = raw_vc - VideoTiming::vblank_top()
    //
    // (32 on the NEXT family / 48K / 128K / +3 50 Hz, 48 on Pentagon, 8 on the
    // 60 Hz overrides).  Renderer::render_frame, Emulator::on_scanline and
    // every per-scanline change log already work in fb_row space.  The panel
    // did not: it fed the raw VC straight in as a row index, so on a Next the
    // "already rendered" cut-off and the red raster marker sat 32 rows below
    // the true raster position (and during the top vblank, raw VC 0..31, it
    // claimed rows the raster had not reached yet).
    //
    // fb_row < 0  → raster is still in the top vblank: nothing drawn yet.
    // fb_row is clamped to FB_HEIGHT-1 for the bottom border / bottom vblank.
    int vc = -1;
    {
        // INS-19: the per-machine raster geometry, from the live VideoTiming.
        const jnext::dbg::MachineInfo mi = dbg_->machine();
        auto* diagram = static_cast<RasterDiagramWidget*>(raster_diagram_);
        diagram->set_frame(mi.hc_max + 1, mi.vc_max + 1,
                           mi.max_hblank, mi.max_vblank,
                           mi.display_origin_hc, mi.display_origin_vc);

        if (dbg_->state().paused) {
            const RasterState rs = video_panel_raster_state(*dbg_);
            vc = fb_row_for_vc(rs.raw_vc, mi.vblank_top);

            raw_label_->setText(QString::asprintf(
                "raw     hc:%4d  vc:%4d   VHDL hc / vc (frame)",
                rs.raw_hc, rs.raw_vc));
            ula_label_->setText(QString::asprintf(
                "ULA     hc:%4d  vc:%4d   o_hc_ula / o_vc_ula",
                rs.hc_ula, rs.vc_ula));
            cvc_label_->setText(QString::asprintf(
                "Copper cvc:%4d           NR 0x1E/0x1F read THIS", rs.cvc));
            pixel_label_->setText(QString::asprintf(
                "Pixel    x:%4d   y:%4d   o_phc / o_vc_ula%s",
                rs.phc, rs.vc_ula, rs.in_paper() ? "" : "  (off paper)"));
            region_label_->setText(raster_region_name(rs.region));
            fetch_label_->setText(ula_fetch_name(rs.fetch));
            diagram->set_beam(rs.raw_hc, rs.raw_vc, true);
        } else {
            raw_label_->setText("raw     hc:----  vc:----  VHDL hc / vc (frame)");
            ula_label_->setText("ULA     hc:----  vc:----  o_hc_ula / o_vc_ula");
            cvc_label_->setText("Copper cvc:----           NR 0x1E/0x1F read THIS");
            pixel_label_->setText("Pixel    x:----   y:----  o_phc / o_vc_ula");
            region_label_->setText("---");
            fetch_label_->setText("---");
            diagram->set_beam(0, 0, false);
        }
    }

    // ── Layer state ──────────────────────────────────────────────────────────
    bool layer_active[4];
    int priority;
    video_panel_layer_state(*dbg_, layer_active, priority);

    auto set_flag = [](QLabel* lbl, bool active) {
        if (active)
            lbl->setStyleSheet("QLabel { font-weight: bold; color: #00AA00; }");
        else
            lbl->setStyleSheet("QLabel { color: #888888; }");
    };

    for (int i = 0; i < 4; ++i)
        set_flag(layer_flags_[i], layer_active[i]);

    // ── Layer priority ───────────────────────────────────────────────────────
    for (int i = 0; i < 6; ++i)
        set_flag(prio_flags_[i], i == priority);

    // ── ULA Palette (std-ULA range, 32 entries) ──────────────────────────────
    // G102 — VHDL std-ULA encoder produces ula_pixel in [0, 0x1F]:
    //   0x00..0x07: ink   (BRIGHT=0)
    //   0x08..0x0F: ink   (BRIGHT=1)
    //   0x10..0x17: paper (BRIGHT=0)
    //   0x18..0x1F: paper (BRIGHT=1)
    // Display shows the active ULA palette bank's 32 std-ULA-reachable
    // entries; ULAnext / ULA+ entries (0x20..0xFF) are not shown here.
    // INS-15 `UlaActive` resolves NR 0x43 bit 1; the entries are RGB333, so
    // they go through INS-15's `rgb333_to_argb()`, the palette's own expansion.
    uint32_t colours[PaletteSwatchWidget::N];
    const std::vector<uint16_t> ula =
        dbg_->palette(jnext::dbg::PaletteId::UlaActive);
    for (int i = 0; i < PaletteSwatchWidget::N; ++i) {
        const uint16_t rgb333 = static_cast<size_t>(i) < ula.size()
                                    ? ula[static_cast<size_t>(i)] : 0;
        colours[i] = jnext::dbg::rgb333_to_argb(rgb333);
    }
    static_cast<PaletteSwatchWidget*>(palette_widget_)->set_colours(colours);

    // ── Layer sub-panel views — only refresh the visible tab ─────────────────
    switch (layer_tabs_->currentIndex()) {
        case 0:  if (composite_view_)  composite_view_->refresh(vc);  break;
        case 1:  if (ula_view_)        ula_view_->refresh(vc);        break;
        case 2:  if (l2_view_)         l2_view_->refresh(vc);         break;
        case 3:  if (sprites_view_)    sprites_view_->refresh(vc);    break;
        case 4:  if (tilemap_view_)    tilemap_view_->refresh(vc);    break;
        case 5:  if (background_view_) background_view_->refresh(vc); break;
        default: break;
    }
}
