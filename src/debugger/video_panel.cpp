#include "debugger/video_panel.h"
#include "core/emulator.h"
#include "port/nextreg.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/layer2.h"
#include "video/sprites.h"
#include "video/tilemap.h"
#include "memory/ram.h"
#include "debug/debug_state.h"

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
// VideoLayerView
// ---------------------------------------------------------------------------

// Checkerboard tile size for transparent pixel indication.
static constexpr int CHECK_SZ = 8;

// Dark background colour for "not yet rendered" rows.
static constexpr uint32_t UNRENDERED_ARGB   = 0xFF111111;
// Light checkerboard for transparent areas — matches typical image editor style.
static constexpr uint32_t CHECKER_DARK_ARGB = 0xFFAAAAAA;
static constexpr uint32_t CHECKER_LITE_ARGB = 0xFFCCCCCC;

static void fill_checker(uint32_t* dst, int row, int width)
{
    for (int x = 0; x < width; ++x) {
        bool dark = (((row / CHECK_SZ) ^ (x / CHECK_SZ)) & 1) != 0;
        dst[x] = dark ? CHECKER_DARK_ARGB : CHECKER_LITE_ARGB;
    }
}

// Repaint the checkerboard over every zero-alpha cell.  Used after the ULA
// clip pass, which turns clipped-away cells TRANSPARENT (alpha 0); the panel's
// convention is that transparent areas show the checkerboard.
static void restore_checker_where_transparent(uint32_t* dst, int row, int width)
{
    for (int x = 0; x < width; ++x) {
        if ((dst[x] & 0xFF000000u) != 0) continue;
        const bool dark = (((row / CHECK_SZ) ^ (x / CHECK_SZ)) & 1) != 0;
        dst[x] = dark ? CHECKER_DARK_ARGB : CHECKER_LITE_ARGB;
    }
}

// ---------------------------------------------------------------------------
// Per-scanline state replay (mirrors Renderer::render_frame)
// ---------------------------------------------------------------------------
//
// Every video subsystem keeps a per-frame change log of the register writes
// the Z80 / Copper made mid-frame, tagged with the framebuffer row they landed
// on.  `Renderer::render_frame` rewinds each log to the frame baseline and
// replays it line by line, so row N is composited with the register state that
// was live when the raster crossed row N.  That is what produces raster splits
// — beast.nex's Layer 2 parallax bands, its per-line palette gradient,
// parallax.nex's DMA-multiplexed sprites, tilemap scroll splits.
//
// The panel used to render every row with the END-OF-PAUSE live register
// state, so all of those effects collapsed to a single flat value and the
// panel showed something the compositor never draws.  The compositor is the
// oracle for "what should this layer look like", so the panel replays the
// frame exactly the same way.
//
// This is state-preserving.  Each subsystem's live register state is, by
// construction, equal to the last entry in its change log (every write both
// mutates the live register and appends a log entry).  So rewind → apply rows
// 0..FB_HEIGHT-1 → flush-remaining walks the cursor to the end of the log and
// leaves every live register exactly where it started; the render cursors are
// reset by the next render_frame's rewind anyway.  It is the same round trip
// render_frame performs once per frame — no more, no less.  We only ever do it
// while the emulator is PAUSED (refresh() early-returns for vc < 0), so there
// is no concurrent emulation to perturb.

static void replay_rewind(Emulator& emu)
{
    emu.palette().rewind_to_baseline();
    emu.layer2().rewind_to_baseline();
    emu.sprites().rewind_to_baseline();
    emu.ula().rewind_to_baseline();            // port 0xFF Timex screen-mode
    emu.ula().rewind_scroll_to_baseline();     // ULA scroll
    emu.ula().palsel_rewind_to_baseline();     // ULA active-palette selector
    emu.tilemap().rewind_nr6b_to_baseline();   // NR 0x6B
}

static void replay_line(Emulator& emu, int row)
{
    emu.palette().apply_changes_for_line(row);
    emu.layer2().apply_changes_for_line(row);
    emu.sprites().apply_changes_for_line(row);
    emu.ula().apply_changes_for_line(row);
    emu.ula().apply_scroll_changes_for_line(row);
    emu.ula().palsel_apply_changes_for_line(row);
    emu.tilemap().apply_nr6b_changes_for_line(row);
}

static void replay_restore(Emulator& emu)
{
    emu.palette().flush_remaining_changes();
    emu.layer2().flush_remaining_changes();
    emu.sprites().flush_remaining_changes();
    emu.ula().flush_remaining_changes();
    emu.ula().flush_remaining_scroll_changes();
    emu.ula().palsel_flush_remaining_changes();
    emu.tilemap().flush_remaining_nr6b_changes();
}

VideoLayerView::VideoLayerView(Layer layer, const char* title,
                               Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , layer_(layer)
    , title_(QString::fromLatin1(title))
    , emulator_(emulator)
    , image_(NATIVE_W, NATIVE_H, QImage::Format_ARGB32)
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
    // Only skip re-render for the dim running placeholder (vc < 0).
    // When paused (vc >= 0), always re-render: registers (scroll, palette,
    // tile data) may have changed even if the scanline position is the same
    // (e.g. EOF → EOF stepping stays at vc=255 across frames).
    if (vc < 0 && last_vc_ < 0) return;
    last_vc_ = vc;
    render_to_image(vc);
    update();
}

void VideoLayerView::render_to_image(int vc)
{
    if (!emulator_) {
        image_.fill(UNRENDERED_ARGB);
        return;
    }

    // When running (vc < 0), show a dim placeholder.
    if (vc < 0) {
        image_.fill(UNRENDERED_ARGB);
        return;
    }

    Emulator& emu = *emulator_;

    // Determine the native width for this layer.
    // G104 phase 2: ULA renderer now emits 640 cells natively (canonical
    // framebuffer width). The debugger panel must match or the QImage scanline
    // dst will be overrun on every render call.
    int layer_w = NATIVE_W;
    switch (layer_) {
        case Layer::ULA_PRIMARY:
        case Layer::ULA_SHADOW:
            layer_w = 640;
            break;
        case Layer::LAYER2_ACTIVE:
        case Layer::LAYER2_SHADOW:
            // G104 Phase 3: Layer 2 always emits 640 (pixel-doubled in
            // 256-mode, pixel-doubled in 320-mode, native in 640-mode).
            layer_w = 640;
            break;
        case Layer::TILEMAP:
            // G104 phase 4: tilemap renderer now emits 640 cells in BOTH
            // col-modes (40-col pixel-doubled, 80-col native).  Force
            // layer_w=640 to match — a 320-wide buffer would be overrun by
            // 40-col scenes (heap corruption in the QImage scanline).
            layer_w = 640;
            break;
        case Layer::SPRITES:
            // G104 phase 5: sprite engine now emits 640 cells (internal
            // 320-grid + emit pixel-double).  layer_w must match or the
            // QImage scanline buffer is overrun on every visible sprite —
            // any sprite at logical x writes to dst[2x] AND dst[2x+1],
            // overflowing a 320-wide buffer.
            layer_w = 640;
            break;
        default:
            break;
    }

    // Recreate QImage if the resolution changed.
    if (image_.width() != layer_w || image_.height() != NATIVE_H) {
        image_ = QImage(layer_w, NATIVE_H, QImage::Format_ARGB32);
    }

    // Layer 2 fetches its pixels straight out of physical SRAM, so it needs
    // the same bank transform the compositor applies (renderer.cpp: the
    // mmu.rom_in_sram() argument to Layer2::render_scanline).  On a Next the
    // ROM lives in SRAM and every ZX RAM bank is shifted by +16 16K-banks
    // (VHDL layer2.vhd:172); without this the panel read Layer 2 pixels from
    // banks 0..N instead of 16..N+16 — i.e. from unrelated (usually zeroed)
    // SRAM, which is why the Layer 2 view rendered solid black on every Next
    // program.
    const bool rom_in_sram = emu.mmu().rom_in_sram();

    // Replay the frame line by line, exactly as Renderer::render_frame does
    // (see the replay_* helpers above).  Rows past the paused raster position
    // have not been drawn yet this frame, but we still have to walk the
    // change-log cursors across them so replay_restore() puts every live
    // register back where it was.
    replay_rewind(emu);

    for (int row = 0; row < 256; ++row) {
        replay_line(emu, row);

        uint32_t* dst = reinterpret_cast<uint32_t*>(image_.scanLine(row));

        if (row > vc) {
            std::fill_n(dst, layer_w, UNRENDERED_ARGB);
            continue;
        }

        // Pre-fill with checkerboard so transparent areas are visible.
        fill_checker(dst, row, layer_w);

        switch (layer_) {
            case Layer::ULA_PRIMARY:
            case Layer::ULA_SHADOW:
                // Force the bank: the live render_scanline() follows the
                // port-0x7FFD b3 shadow selector, so with the shadow screen
                // active the "Primary (bank 5)" view used to show bank 7.
                emu.ula().render_scanline_bank(
                    dst, row, emu.mmu(),
                    /*use_bank7=*/layer_ == Layer::ULA_SHADOW);
                // The ULA is the one layer whose clip window (NR 0x1A) is
                // applied by the COMPOSITOR rather than inside its own
                // render_scanline (VHDL zxnext.vhd:7104 — ula_clipped feeds
                // ula_transparent).  Layer 2 / Tilemap / Sprites all clip
                // themselves, so without this the ULA view was the only layer
                // view showing content the compositor suppresses.
                emu.renderer().apply_ula_clip(dst, row);
                // apply_ula_clip zeroes the clipped-away cells.  The ULA
                // renderer itself only ever emits opaque pixels, so a zero
                // alpha here means exactly "clipped away" — repaint the
                // checkerboard there so it reads as transparent, like every
                // other layer view.
                restore_checker_where_transparent(dst, row, layer_w);
                break;

            case Layer::LAYER2_ACTIVE:
                // G104 Phase 3: render_scanline_debug always emits 640.
                // active_bank() is re-read per row — it is itself replayed
                // per scanline (Layer2 bank change-log).
                emu.layer2().render_scanline_debug(
                    dst, row, emu.ram(), emu.palette(),
                    emu.layer2().active_bank(), rom_in_sram);
                break;

            case Layer::LAYER2_SHADOW:
                emu.layer2().render_scanline_debug(
                    dst, row, emu.ram(), emu.palette(),
                    emu.layer2().shadow_bank(), rom_in_sram);
                break;

            case Layer::SPRITES:
                emu.sprites().render_scanline_debug(dst, row, emu.palette());
                break;

            case Layer::TILEMAP: {
                bool ula_over[640];
                std::fill_n(ula_over, layer_w, false);
                // G104 phase 4: tilemap render_scanline_debug always
                // emits 640 (no width parameter).
                emu.tilemap().render_scanline_debug(
                    dst, ula_over, row, emu.ram(), emu.palette());
                break;
            }
        }
    }

    replay_restore(emu);
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

    // Pre-scale the source image (320×256 or 640×256) to fill the physical content area.
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

VideoPanel::VideoPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

int VideoPanel::fb_row_for_vc(int raw_vc, int vblank_top)
{
    // See the declaration in video_panel.h for the G164v2 rationale.
    const int fb_row = raw_vc - vblank_top;
    return std::min(fb_row, Renderer::FB_HEIGHT - 1);
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

    // ── Raster ───────────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(4);
        row->addWidget(make_bold("Raster:      "));
        row->addWidget(make_bold("HC:"));
        hc_label_ = make_val("---");
        hc_label_->setFixedWidth(hc_label_->fontMetrics().horizontalAdvance("000") + 4);
        row->addWidget(hc_label_);
        row->addSpacing(12);
        row->addWidget(make_bold("VC:"));
        vc_label_ = make_val("---");
        vc_label_->setFixedWidth(vc_label_->fontMetrics().horizontalAdvance("000") + 4);
        row->addWidget(vc_label_);
        row->addStretch();
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
        auto* view = new VideoLayerView(layer, view_title, emulator_, tab);
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

    // ULA tab — single view, radio buttons select primary/shadow.
    ula_view_ = make_layer_tab(
        "ULA",
        VideoLayerView::Layer::ULA_PRIMARY, "ULA screen (bank 5/7)",
        "Primary (bank 5)", &ula_rb_primary_,
        "Shadow (bank 7)",  &ula_rb_shadow_);

    connect(ula_rb_primary_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            ula_view_->setLayer(VideoLayerView::Layer::ULA_PRIMARY);
            refresh();
        }
    });
    connect(ula_rb_shadow_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            ula_view_->setLayer(VideoLayerView::Layer::ULA_SHADOW);
            refresh();
        }
    });

    // Layer 2 tab — single view, radio buttons select active/shadow bank.
    l2_view_ = make_layer_tab(
        "Layer2",
        VideoLayerView::Layer::LAYER2_ACTIVE, "Layer 2",
        "Active bank",  &l2_rb_active_,
        "Shadow bank",  &l2_rb_shadow_);

    connect(l2_rb_active_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            l2_view_->setLayer(VideoLayerView::Layer::LAYER2_ACTIVE);
            refresh();
        }
    });
    connect(l2_rb_shadow_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            l2_view_->setLayer(VideoLayerView::Layer::LAYER2_SHADOW);
            refresh();
        }
    });

    // Sprites tab — single view, no radio buttons.
    sprites_view_ = make_layer_tab(
        "Sprites",
        VideoLayerView::Layer::SPRITES, "Sprites",
        nullptr, nullptr, nullptr, nullptr);

    // TileMap tab — single view, no radio buttons.
    tilemap_view_ = make_layer_tab(
        "TileMap",
        VideoLayerView::Layer::TILEMAP, "TileMap",
        nullptr, nullptr, nullptr, nullptr);

    // When the user switches tabs, invalidate so the new tab renders immediately.
    connect(layer_tabs_, &QTabWidget::currentChanged, this, [this](int) {
        for (VideoLayerView* v : {ula_view_, l2_view_,
                                   sprites_view_, tilemap_view_}) {
            if (v) v->invalidate();
        }
        refresh();
    });

    layout->addWidget(layer_tabs_);
    layout->addStretch();
}

void VideoPanel::refresh()
{
    if (!emulator_) return;

    // ── Raster position (only when paused) ───────────────────────────────────
    //
    // paused_vc() is the RAW vertical counter (0..lines_per_frame-1) — that is
    // what the HC/VC readout must show, since it is the hardware raster
    // counter.  The layer views, however, index FRAMEBUFFER ROWS, and since
    // G164v2 (Task 13) the mapping is
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
    if (emulator_->debug_state().paused()) {
        const int raw_vc = emulator_->paused_vc();
        vc = fb_row_for_vc(raw_vc, emulator_->video_timing().vblank_top());
        vc_label_->setText(QString::asprintf("%3d", raw_vc));
        hc_label_->setText(QString::asprintf("%3d", emulator_->paused_hc()));
    } else {
        vc_label_->setText("---");
        hc_label_->setText("---");
    }

    // ── Layer state ──────────────────────────────────────────────────────────
    uint8_t reg15 = emulator_->nextreg().cached(0x15);
    uint8_t reg68 = emulator_->nextreg().cached(0x68);
    uint8_t reg69 = emulator_->nextreg().cached(0x69);
    uint8_t reg6b = emulator_->nextreg().cached(0x6B);

    bool layer_active[4] = {
        !(reg68 & 0x80),   // ULA
        !!(reg69 & 0x80),  // Layer 2
        !!(reg6b & 0x80),  // Tilemap
        !!(reg15 & 0x01),  // Sprites
    };

    auto set_flag = [](QLabel* lbl, bool active) {
        if (active)
            lbl->setStyleSheet("QLabel { font-weight: bold; color: #00AA00; }");
        else
            lbl->setStyleSheet("QLabel { color: #888888; }");
    };

    for (int i = 0; i < 4; ++i)
        set_flag(layer_flags_[i], layer_active[i]);

    // ── Layer priority ───────────────────────────────────────────────────────
    int priority = (reg15 >> 2) & 0x07;
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
    uint32_t colours[PaletteSwatchWidget::N];
    const bool active_bank = emulator_->ula().get_active_ula_palette();
    for (int i = 0; i < PaletteSwatchWidget::N; ++i)
        colours[i] = emulator_->palette().ula_colour(active_bank,
                                                     static_cast<uint8_t>(i));
    static_cast<PaletteSwatchWidget*>(palette_widget_)->set_colours(colours);

    // ── Layer sub-panel views — only refresh the visible tab ─────────────────
    switch (layer_tabs_->currentIndex()) {
        case 0:  if (ula_view_)      ula_view_->refresh(vc);      break;
        case 1:  if (l2_view_)       l2_view_->refresh(vc);       break;
        case 2:  if (sprites_view_)  sprites_view_->refresh(vc);  break;
        case 3:  if (tilemap_view_)  tilemap_view_->refresh(vc);  break;
        default: break;
    }
}
