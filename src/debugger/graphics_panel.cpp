#include "debugger/graphics_panel.h"
#include "debug/debugger.h"
#include "debug/gfx_decode.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gfx = jnext::dbg::gfx;
using jnext::dbg::PaletteId;

namespace {

// The Video panel's transparency checkerboard (8 px squares, same two greys).
constexpr int CHECK_SZ = 8;
constexpr uint32_t CHECKER_DARK_ARGB = 0xFFAAAAAA;
constexpr uint32_t CHECKER_LITE_ARGB = 0xFFCCCCCC;

constexpr int SCALE = 3;           // atlas pixels per image pixel
constexpr int PATTERN_COLS = 8;    // 16x16 cells: 128 px wide
constexpr int TILE_COLS = 16;      // 8x8 cells: 128 px wide

// Active palette selects, read through the live NextREG read handlers (NR 0x43 bits 3 and 2 /
// NR 0x6B bit 4: zxnext.vhd:6044-6045, :6101-6102).
bool sprite_second(const jnext::dbg::Debugger* d) { return d->nextreg_peek(0x43) & 0x08; }
bool layer2_second(const jnext::dbg::Debugger* d) { return d->nextreg_peek(0x43) & 0x04; }
bool tilemap_second(const jnext::dbg::Debugger* d) { return d->nextreg_peek(0x6B) & 0x10; }

// Combo index 0 = Active, 1 = First, 2 = Second.
PaletteId sprite_bank(const jnext::dbg::Debugger* d, int choice) {
    const bool second = choice == 0 ? sprite_second(d) : choice == 2;
    return second ? PaletteId::SpriteSecond : PaletteId::SpriteFirst;
}
PaletteId tilemap_bank(const jnext::dbg::Debugger* d, int choice) {
    const bool second = choice == 0 ? tilemap_second(d) : choice == 2;
    return second ? PaletteId::TilemapSecond : PaletteId::TilemapFirst;
}

uint32_t argb_of(const std::vector<uint16_t>& pal, uint8_t index) {
    return jnext::dbg::rgb333_to_argb(index < pal.size() ? pal[index] : 0);
}

QComboBox* make_palette_combo(const char* name, QWidget* parent) {
    auto* c = new QComboBox(parent);
    c->setObjectName(name);
    c->addItems({"Active", "First", "Second"});
    return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// GfxAtlas
// ---------------------------------------------------------------------------

GfxAtlas::GfxAtlas(int scale, QWidget* parent) : QWidget(parent), scale_(scale) {}

void GfxAtlas::set_image(const QImage& img, int cell_px, int count) {
    const bool resized = img.size() != image_.size();
    image_ = img;
    cell_px_ = cell_px;
    count_ = count;
    if (selected_ >= count_) selected_ = -1;
    if (resized) {
        setFixedSize(sizeHint());
        updateGeometry();
    }
    update();
}

void GfxAtlas::set_selected(int idx) {
    selected_ = (idx >= 0 && idx < count_) ? idx : -1;
    update();
}

void GfxAtlas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    for (int y = 0; y < height(); y += CHECK_SZ)
        for (int x = 0; x < width(); x += CHECK_SZ) {
            const bool dark = (((y / CHECK_SZ) ^ (x / CHECK_SZ)) & 1) != 0;
            p.fillRect(x, y, CHECK_SZ, CHECK_SZ, QColor::fromRgba(dark ? CHECKER_DARK_ARGB : CHECKER_LITE_ARGB));
        }
    if (image_.isNull()) return;
    p.drawImage(QRect(QPoint(0, 0), image_.size() * scale_), image_);
    if (selected_ >= 0) {
        const int cols = image_.width() / cell_px_;
        const int cs = cell_px_ * scale_;
        p.setPen(QPen(QColor(0xFF, 0x30, 0x30), 2));
        p.drawRect((selected_ % cols) * cs + 1, (selected_ / cols) * cs + 1, cs - 2, cs - 2);
    }
}

void GfxAtlas::mousePressEvent(QMouseEvent* e) {
    if (image_.isNull() || cell_px_ <= 0) return;
    const int cs = cell_px_ * scale_;
    const int cols = image_.width() / cell_px_;
    const int col = e->pos().x() / cs, row = e->pos().y() / cs;
    const int idx = row * cols + col;
    if (e->pos().x() < 0 || e->pos().y() < 0 || col >= cols || idx >= count_) return;
    set_selected(idx);
    emit cell_clicked(idx);
}

// ---------------------------------------------------------------------------
// GraphicsPanel
// ---------------------------------------------------------------------------

GraphicsPanel::GraphicsPanel(const jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent), dbg_(dbg)
{
    create_ui();
}

void GraphicsPanel::create_ui() {
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    sub_tabs_ = new QTabWidget(this);
    sub_tabs_->setObjectName("gfxSubTabs");
    layout->addWidget(sub_tabs_);

    // --- Patterns ---
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(4, 4, 4, 4);
        auto* row = new QHBoxLayout();
        fmt8_ = new QRadioButton(tr("8-bit"), page);
        fmt8_->setObjectName("gfxPatFmt8");
        fmt4_ = new QRadioButton(tr("4-bit"), page);
        fmt4_->setObjectName("gfxPatFmt4");
        fmt8_->setChecked(true);
        pat_palette_ = make_palette_combo("gfxPatPalette", page);
        pat_offset_ = new QSpinBox(page);
        pat_offset_->setObjectName("gfxPatOffset");
        pat_offset_->setRange(0, 15);
        row->addWidget(fmt8_);
        row->addWidget(fmt4_);
        row->addWidget(new QLabel(tr("Palette:"), page));
        row->addWidget(pat_palette_);
        row->addWidget(new QLabel(tr("Offset:"), page));
        row->addWidget(pat_offset_);
        row->addStretch(1);
        v->addLayout(row);
        pat_atlas_ = new GfxAtlas(SCALE);
        pat_atlas_->setObjectName("gfxPatAtlas");
        auto* scroll = new QScrollArea(page);
        scroll->setWidget(pat_atlas_);
        v->addWidget(scroll, 1);
        pat_info_ = new QLabel(tr("Click a pattern."), page);
        pat_info_->setObjectName("gfxPatInfo");
        pat_info_->setFont(mono);
        v->addWidget(pat_info_);
        sub_tabs_->addTab(page, tr("Patterns"));

        auto redo = [this] { render_patterns(); };
        connect(fmt8_, &QRadioButton::toggled, this, redo);
        connect(pat_palette_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, redo);
        connect(pat_offset_, QOverload<int>::of(&QSpinBox::valueChanged), this, redo);
        connect(pat_atlas_, &GfxAtlas::cell_clicked, this, [this](int) { update_pattern_info(); });
    }

    // --- Tiles ---
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(4, 4, 4, 4);
        auto* row = new QHBoxLayout();
        tile_palette_ = make_palette_combo("gfxTilePalette", page);
        tile_offset_ = new QSpinBox(page);
        tile_offset_->setObjectName("gfxTileOffset");
        tile_offset_->setRange(0, 15);
        row->addWidget(new QLabel(tr("Palette:"), page));
        row->addWidget(tile_palette_);
        row->addWidget(new QLabel(tr("Offset:"), page));
        row->addWidget(tile_offset_);
        row->addStretch(1);
        v->addLayout(row);
        tile_source_ = new QLabel(page);
        tile_source_->setObjectName("gfxTileSource");
        tile_source_->setFont(mono);
        tile_source_->setWordWrap(true);
        v->addWidget(tile_source_);
        tile_atlas_ = new GfxAtlas(SCALE);
        tile_atlas_->setObjectName("gfxTileAtlas");
        auto* scroll = new QScrollArea(page);
        scroll->setWidget(tile_atlas_);
        v->addWidget(scroll, 1);
        tile_info_ = new QLabel(tr("Click a tile."), page);
        tile_info_->setObjectName("gfxTileInfo");
        tile_info_->setFont(mono);
        v->addWidget(tile_info_);
        sub_tabs_->addTab(page, tr("Tiles"));

        auto redo = [this] { render_tiles(); };
        connect(tile_palette_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, redo);
        connect(tile_offset_, QOverload<int>::of(&QSpinBox::valueChanged), this, redo);
        connect(tile_atlas_, &GfxAtlas::cell_clicked, this, [this](int) { update_tile_info(); });
    }

    // --- Palettes ---
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(4, 4, 4, 4);
        pal_bank_ = new QComboBox(page);
        pal_bank_->setObjectName("gfxPalBank");
        struct Bank { const char* name; PaletteId id; };
        const Bank banks[] = {
            {"ULA first", PaletteId::UlaFirst},           {"ULA second", PaletteId::UlaSecond},
            {"Layer 2 first", PaletteId::Layer2First},    {"Layer 2 second", PaletteId::Layer2Second},
            {"Sprites first", PaletteId::SpriteFirst},    {"Sprites second", PaletteId::SpriteSecond},
            {"Tilemap first", PaletteId::TilemapFirst},   {"Tilemap second", PaletteId::TilemapSecond}};
        for (const Bank& b : banks) pal_bank_->addItem(b.name, static_cast<int>(b.id));
        v->addWidget(pal_bank_);
        pal_table_ = new QTableWidget(256, 5, page);
        pal_table_->setObjectName("gfxPalTable");
        pal_table_->setHorizontalHeaderLabels({"#", "Colour", "RGB333", "Hex", "Pri"});
        pal_table_->setFont(mono);
        pal_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        pal_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        pal_table_->verticalHeader()->setVisible(false);
        pal_table_->verticalHeader()->setDefaultSectionSize(20);
        pal_table_->setColumnWidth(0, 40);
        pal_table_->setColumnWidth(1, 60);
        pal_table_->setColumnWidth(2, 80);
        pal_table_->setColumnWidth(3, 60);
        pal_table_->setColumnWidth(4, 40);
        pal_table_->horizontalHeaderItem(4)->setToolTip(
            tr("Stored priority bit (bit 15 of the palette word, NR 0x44 bit 7). Only Layer 2 palettes use it."));
        for (int i = 0; i < 256; ++i) {
            for (int c = 0; c < 5; ++c) {
                auto* item = new QTableWidgetItem();
                item->setTextAlignment(Qt::AlignCenter);
                pal_table_->setItem(i, c, item);
            }
            pal_table_->item(i, 0)->setText(QString::asprintf("%02X", i));
        }
        v->addWidget(pal_table_, 1);
        sub_tabs_->addTab(page, tr("Palettes"));

        connect(pal_bank_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this] { render_palettes(); });
    }

    connect(sub_tabs_, &QTabWidget::currentChanged, this, [this] { render_current(); });
}

void GraphicsPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    render_current();
}

void GraphicsPanel::refresh() {
    // Hidden (another tab of the window is current): nothing is drawn.
    if (!dbg_ || !isVisibleTo(window())) return;
    render_current();
}

void GraphicsPanel::render_current() {
    if (!dbg_ || !isVisibleTo(window())) return;
    switch (sub_tabs_->currentIndex()) {
        case 0: render_patterns(); break;
        case 1: render_tiles();    break;
        case 2: render_palettes(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

void GraphicsPanel::render_patterns() {
    if (!dbg_ || !isVisibleTo(window()) || sub_tabs_->currentIndex() != 0) return;
    ++render_count_;
    const bool four = fmt4_->isChecked();
    const int n = four ? gfx::PATTERNS_4BIT : gfx::PATTERNS_8BIT;
    const int rows = (n + PATTERN_COLS - 1) / PATTERN_COLS;
    const std::vector<uint16_t> pal = dbg_->palette(sprite_bank(dbg_, pat_palette_->currentIndex()));
    const jnext::dbg::ConstBytes ram = dbg_->pattern_ram();
    const uint8_t transp = dbg_->nextreg_peek(0x4B);
    const uint8_t off = static_cast<uint8_t>(pat_offset_->value());

    QImage img(PATTERN_COLS * gfx::PATTERN_SIDE, rows * gfx::PATTERN_SIDE, QImage::Format_ARGB32);
    img.fill(0);
    std::array<gfx::Pixel, 256> px{};
    for (int p = 0; p < n; ++p) {
        if (!gfx::decode_pattern(ram.data, ram.size, four, static_cast<uint8_t>(p), off, transp, px)) continue;
        const int cx = (p % PATTERN_COLS) * gfx::PATTERN_SIDE, cy = (p / PATTERN_COLS) * gfx::PATTERN_SIDE;
        for (int i = 0; i < 256; ++i) {
            if (px[static_cast<size_t>(i)].transparent) continue;
            img.setPixel(cx + i % 16, cy + i / 16, argb_of(pal, px[static_cast<size_t>(i)].index));
        }
    }
    pat_atlas_->set_image(img, gfx::PATTERN_SIDE, n);
    update_pattern_info();
}

void GraphicsPanel::update_pattern_info() {
    const int sel = pat_atlas_->selected();
    if (sel < 0) { pat_info_->setText(tr("Click a pattern.")); return; }
    const unsigned size = fmt4_->isChecked() ? 128 : 256;
    pat_info_->setText(QString::asprintf("Pattern %02X: pattern RAM 0x%04X-0x%04X", sel, sel * size,
                                         sel * size + size - 1));
}

// ---------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------

void GraphicsPanel::render_tiles() {
    if (!dbg_ || !isVisibleTo(window()) || sub_tabs_->currentIndex() != 1) return;
    ++render_count_;
    // Format and source follow the live NR 0x6B / NR 0x6F.
    const uint8_t nr6b = dbg_->nextreg_peek(0x6B);
    const uint8_t nr6f = dbg_->nextreg_peek(0x6F);
    tile_textmode_ = nr6b & 0x08;
    tile_bank7_ = nr6f & 0x80;
    tile_base_ = nr6f & 0x3F;
    const int n = gfx::tile_count(nr6b);
    tile_offset_->setMaximum(tile_textmode_ ? 127 : 15);

    // Tile bytes come from the BRAM the tilemap fetches: pages 0x0A/0x0B (bank 5) or 0x0E (bank 7).
    std::vector<uint8_t> bank(tile_bank7_ ? gfx::BANK7_SIZE : gfx::BANK5_SIZE, 0);
    if (tile_bank7_) {
        dbg_->peek(jnext::dbg::MemSpace::page(0x0E), 0, 0x2000, bank.data());
    } else {
        dbg_->peek(jnext::dbg::MemSpace::page(0x0A), 0, 0x2000, bank.data());
        dbg_->peek(jnext::dbg::MemSpace::page(0x0B), 0, 0x2000, bank.data() + 0x2000);
    }

    const std::vector<uint16_t> pal = dbg_->palette(tilemap_bank(dbg_, tile_palette_->currentIndex()));
    std::vector<uint16_t> pal256(pal);
    pal256.resize(256, 0);
    const uint8_t nr4c = dbg_->nextreg_peek(0x4C);
    const uint8_t nr14 = dbg_->nextreg_peek(0x14);
    const uint8_t off = static_cast<uint8_t>(tile_offset_->value());

    const int rows = (n + TILE_COLS - 1) / TILE_COLS;
    QImage img(TILE_COLS * gfx::TILE_SIDE, rows * gfx::TILE_SIDE, QImage::Format_ARGB32);
    img.fill(0);
    std::array<gfx::Pixel, 64> px{};
    for (int t = 0; t < n; ++t) {
        if (!gfx::decode_tile(bank.data(), bank.size(), tile_bank7_, tile_base_, tile_textmode_,
                              static_cast<uint16_t>(t), off, nr4c, nr14, pal256.data(), px)) continue;
        const int cx = (t % TILE_COLS) * gfx::TILE_SIDE, cy = (t / TILE_COLS) * gfx::TILE_SIDE;
        for (int i = 0; i < 64; ++i) {
            if (px[static_cast<size_t>(i)].transparent) continue;
            img.setPixel(cx + i % 8, cy + i / 8, argb_of(pal256, px[static_cast<size_t>(i)].index));
        }
    }
    tile_atlas_->set_image(img, gfx::TILE_SIDE, n);
    tile_source_->setText(QString::asprintf("Tile definitions: bank %d + 0x%04X (NR 0x6F = %02X), %s, %d tiles",
                                            tile_bank7_ ? 7 : 5, tile_base_ * 256, nr6f,
                                            tile_textmode_ ? "text mode" : "4-bit", n));
    update_tile_info();
}

void GraphicsPanel::update_tile_info() {
    const int sel = tile_atlas_->selected();
    if (sel < 0) { tile_info_->setText(tr("Click a tile.")); return; }
    const uint16_t t = static_cast<uint16_t>(sel);
    const unsigned first = gfx::tile_byte_addr(tile_bank7_, tile_base_, tile_textmode_, t, 0, 0);
    const unsigned last = tile_textmode_ ? gfx::tile_byte_addr(tile_bank7_, tile_base_, true, t, 0, 7)
                                         : gfx::tile_byte_addr(tile_bank7_, tile_base_, false, t, 7, 7);
    tile_info_->setText(QString::asprintf("Tile %03X: bank %d 0x%04X-0x%04X", sel, tile_bank7_ ? 7 : 5, first, last));
}

// ---------------------------------------------------------------------------
// Palettes
// ---------------------------------------------------------------------------

void GraphicsPanel::render_palettes() {
    if (!dbg_ || !isVisibleTo(window()) || sub_tabs_->currentIndex() != 2) return;
    ++render_count_;

    // "(active)" marks: the ULA bank from the backend, the others from the live select bits.
    const bool ula2 = dbg_->active_ula_palette_bank() != 0;
    const bool active[8] = {!ula2, ula2, !layer2_second(dbg_), layer2_second(dbg_),
                            !sprite_second(dbg_), sprite_second(dbg_),
                            !tilemap_second(dbg_), tilemap_second(dbg_)};
    static const char* const names[8] = {"ULA first", "ULA second", "Layer 2 first", "Layer 2 second",
                                         "Sprites first", "Sprites second", "Tilemap first", "Tilemap second"};
    for (int i = 0; i < pal_bank_->count(); ++i)
        pal_bank_->setItemText(i, QString::fromLatin1(names[i]) + (active[i] ? " (active)" : ""));

    const PaletteId bank = static_cast<PaletteId>(pal_bank_->currentData().toInt());
    const std::vector<uint16_t> pal = dbg_->palette(bank);
    const std::vector<uint8_t> pri = dbg_->palette_priority(bank);
    for (int i = 0; i < 256; ++i) {
        const uint16_t v = static_cast<size_t>(i) < pal.size() ? pal[static_cast<size_t>(i)] : 0;
        pal_table_->item(i, 1)->setBackground(QColor::fromRgba(jnext::dbg::rgb333_to_argb(v)));
        pal_table_->item(i, 2)->setText(QString::asprintf("%d %d %d", (v >> 6) & 7, (v >> 3) & 7, v & 7));
        pal_table_->item(i, 3)->setText(QString::asprintf("%03X", v & 0x1FF));
        // Bit 1 of the stored field is bit 15 of the palette word, the priority bit.
        const bool p = static_cast<size_t>(i) < pri.size() && (pri[static_cast<size_t>(i)] & 0x02);
        pal_table_->item(i, 4)->setText(p ? "P" : "-");
    }
}
