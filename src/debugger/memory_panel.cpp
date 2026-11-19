#include "debugger/memory_panel.h"
#include "qt/palette_tint_qt.h"   // GH #363
#include "debugger/coverage_colors.h"   // GH #388
#include "debug/debugger.h"
#include "debug/symbol_table.h"
#include "debugger/memory_text.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MemoryPanel::MemoryPanel(jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    create_ui();
    setFocusPolicy(Qt::StrongFocus);
}

void MemoryPanel::create_ui() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(2);

    // Top bar: address input + page selector
    auto* top_bar = new QHBoxLayout();
    top_bar->setSpacing(6);

    auto* addr_label = new QLabel("Addr:", this);
    top_bar->addWidget(addr_label);

    addr_input_ = new QLineEdit(this);
    addr_input_->setPlaceholderText("$0000");
    addr_input_->setMaximumWidth(80);
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    addr_input_->setFont(mono);
    top_bar->addWidget(addr_input_);

    connect(addr_input_, &QLineEdit::returnPressed, this, [this]() {
        // An address (`4000`, `$4000`, `0x4000`) or a symbol, as every other
        // address field takes it (SymbolTable::resolve, OD-12).
        const std::string text = addr_input_->text().trimmed().toStdString();
        const auto addr = dbg_ ? dbg_->symbols().resolve(text) : SymbolTable().resolve(text);
        if (addr) navigate_to_address(*addr);
    });

    page_selector_ = new QComboBox(this);
    page_selector_->addItem("CPU View");
    for (int i = 0; i < 8; ++i) {
        page_selector_->addItem(QString("Slot %1 (page --)").arg(i));
    }
    // GH #278 WP8 — any physical page, mapped in a slot or not.
    page_selector_->addItem(tr("Page..."));
    top_bar->addWidget(page_selector_);

    connect(page_selector_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx == PAGE_ITEM && !choose_page()) {
            // Cancelled or not a page: back to where the user was.
            const QSignalBlocker block(page_selector_);
            page_selector_->setCurrentIndex(prev_mode_);
            return;
        }
        prev_mode_ = idx;
        scroll_offset_ = 0;
        selected_addr_ = -1;
        edit_nibble_ = 0;
        update_scroll_bar();
        // The new view starts at its top: the scroll bar still held the old
        // view's position, and its valueChanged put scroll_offset_ back there —
        // a Slot view opened from CPU View at $2010 showed its last rows.
        scroll_bar_->setValue(0);
        update();
    });

    // GH #388 - the memory-coverage legend, shown only while coverage is on.
    coverage_legend_ = new QLabel(this);
    coverage_legend_->setTextFormat(Qt::RichText);
    coverage_legend_->setVisible(false);
    top_bar->addWidget(coverage_legend_);

    top_bar->addStretch();
    outer->addLayout(top_bar);

    // Hex editor area + scroll bar side by side.
    // The hex editor is drawn via paintEvent on `this` widget.
    // We just add the scroll bar in an HBox at the bottom stretch.
    auto* hex_row = new QHBoxLayout();
    hex_row->setContentsMargins(0, 0, 0, 0);
    hex_row->setSpacing(0);

    // Spacer for the painted hex area
    hex_row->addStretch();

    scroll_bar_ = new QScrollBar(Qt::Vertical, this);
    scroll_bar_->setMinimum(0);
    scroll_bar_->setMaximum(0);
    scroll_bar_->setSingleStep(1);
    scroll_bar_->setPageStep(20);
    connect(scroll_bar_, &QScrollBar::valueChanged, this, [this](int val) {
        scroll_offset_ = val;
        update();
    });
    hex_row->addWidget(scroll_bar_);

    outer->addLayout(hex_row, 1);

    update_page_selector();
    update_scroll_bar();
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void MemoryPanel::refresh() {
    update_page_selector();
    update_coverage_legend();
    update();
}

// GH #388 - "Coverage: X R W" in the three colours while coverage is on. Rebuilt
// on every refresh, so a light/dark switch recolours it.
void MemoryPanel::update_coverage_legend() {
    if (!coverage_legend_) return;
    const bool on = dbg_ && dbg_->mem_coverage_enabled();
    coverage_legend_->setVisible(on);
    if (!on) return;
    const QPalette pal = palette();
    auto span = [&](const QColor& c, const char* t) {
        return QString("<b style=\"color:%1\">%2</b>").arg(palette_tint::css(c), t);
    };
    coverage_legend_->setText(tr("Coverage:") + " " + span(coverage_colors::exec(pal), "X") + " " +
                              span(coverage_colors::read(pal), "R") + " " +
                              span(coverage_colors::write(pal), "W"));
}

QSize MemoryPanel::sizeHint() const {
    return QSize(760, 550);   // GH #360: the memory tag after the ASCII column
}

// ---------------------------------------------------------------------------
// Memory access helpers
// ---------------------------------------------------------------------------

// GH #360: the space a Slot view shows, as SlotInfo names it; a RAM-page index at or
// above 0xE0 (a slot paged to nothing) is `floating()`, never a page the tag grammar
// refuses (design 4.6).
static jnext::dbg::MemSpace own_space(const jnext::dbg::SlotInfo& si) {
    if (si.space.kind == jnext::dbg::MemSpace::Kind::Page && si.space.index >= 0xE0)
        return jnext::dbg::MemSpace::floating();
    return si.space;
}

MemoryPanel::Where MemoryPanel::locate(uint16_t addr) const {
    int mode = page_selector_ ? page_selector_->currentIndex() : 0;
    if (mode == 0)                                   // CPU view: as the CPU sees it
        return {jnext::dbg::MemSpace::cpu(), addr};
    const uint32_t off = addr & 0x1FFF;              // within the 8K view
    if (mode == PAGE_ITEM)                           // the NR page the user picked
        return {jnext::dbg::MemSpace::page(static_cast<uint16_t>(page_)), off};
    // A slot: its physical backing store, exactly as the backend names it.
    const jnext::dbg::SlotInfo slot = dbg_->mmu_slots()[mode - 1];
    return {slot.space, slot.space_offset + off};
}

void MemoryPanel::read_bytes(uint16_t addr, uint8_t* out, size_t n) const {
    if (!dbg_) return;
    // INS-02 — a peek: the live CPU map in the CPU view, the physical space in
    // a slot or Page view. A row never crosses an 8K view (16 bytes,
    // 16-aligned), so one peek per row holds in every view.
    const Where w = locate(addr);
    dbg_->peek(w.space, w.addr, n, out);
}

void MemoryPanel::write_byte(uint16_t addr, uint8_t val) {
    if (!dbg_) return;
    // INS-02 / §4.2a — the CPU view writes through the live map (`Mmu::write`:
    // ROM ignored, overlays honoured, no watch); a slot or Page view writes the
    // physical page, which no overlay sees. A ROM slot is `RefusedReadOnly`.
    // Logged as this client's MUTATE; refused while an RZX records or plays.
    const Where w = locate(addr);
    dbg_->poke(client_, w.space, w.addr, 1, &val);
}

bool MemoryPanel::choose_page() {
    bool ok = false;
    QString text = QInputDialog::getText(
        this, tr("Physical Page"), tr("NR page number (hex, 00-DF):"), QLineEdit::Normal,
        page_ >= 0 ? QString::asprintf("%02X", page_) : QString(), &ok).trimmed();
    if (!ok) return false;
    if (text.startsWith('$')) text = text.mid(1);
    // No "0x" strip: QString::toUInt(base 16) accepts the prefix itself
    // (QMP-15 types 0x47), so a strip line here was dead (review R11).
    bool num = false;
    const unsigned v = text.toUInt(&num, 16);
    if (!num || v > 0xDF) return false;          // 0xE0.. has no backing store
    page_ = static_cast<int>(v);
    page_selector_->setItemText(PAGE_ITEM, QString::asprintf("Page %02X", page_));
    return true;
}

int MemoryPanel::total_rows() const {
    int mode = page_selector_ ? page_selector_->currentIndex() : 0;
    if (mode == 0) {
        // CPU view: 64K / 16 bytes per row = 4096 rows
        return 0x10000 / BYTES_PER_ROW;
    } else {
        // Slot view: 8K / 16 = 512 rows
        return 0x2000 / BYTES_PER_ROW;
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void MemoryPanel::navigate_to_address(uint16_t addr) {
    int row = addr / BYTES_PER_ROW;
    int vis = visible_rows();
    if (vis <= 0) vis = 24;

    // Center the row if possible.
    int target = row - vis / 2;
    if (target < 0) target = 0;
    int max_scroll = total_rows() - vis;
    if (max_scroll < 0) max_scroll = 0;
    if (target > max_scroll) target = max_scroll;

    scroll_offset_ = target;
    selected_addr_ = addr;
    edit_nibble_ = 0;

    scroll_bar_->setValue(scroll_offset_);
    update();
}

void MemoryPanel::update_page_selector() {
    if (!dbg_ || !page_selector_) return;

    const auto slot_info = dbg_->mmu_slots();   // INS-03
    for (int i = 0; i < 8; ++i) {
        // GH #360: the slot's own space through the formatter ("ROM0", "P0A"). The
        // old "page %02X" printed effective_page, which for a ROM slot is the
        // un-shifted ROM page and read as RAM page 00. A slot paged to nothing
        // (NR >= 0xE0) is FLOAT. The words are not upper-cased (GH #278 WP0).
        page_selector_->setItemText(i + 1,
            QString("Slot %1 (%2)").arg(i).arg(QString::fromStdString(
                jnext::dbg::space_tag(own_space(slot_info[i])))));
    }
}

void MemoryPanel::update_scroll_bar() {
    int vis = visible_rows();
    if (vis <= 0) vis = 24;
    int max_val = total_rows() - vis;
    if (max_val < 0) max_val = 0;
    scroll_bar_->setMaximum(max_val);
    scroll_bar_->setPageStep(vis);
}

// ---------------------------------------------------------------------------
// Layout calculations
// ---------------------------------------------------------------------------

int MemoryPanel::visible_rows() const {
    int avail = height() - header_height();
    if (avail <= 0) return 0;
    return avail / row_height();
}

int MemoryPanel::row_height() const {
    return char_h_ + 2;
}

int MemoryPanel::header_height() const {
    return TOP_BAR_HEIGHT + 4;
}

int MemoryPanel::hex_area_left() const {
    // "$XXXX  " = 7 chars
    return 7 * char_w_;
}

int MemoryPanel::ascii_area_left() const {
    // hex area: 7 (addr) + 16*3 (hex bytes) + 1 (gap between groups) + 2 (spacing before ASCII)
    // = 7 + 48 + 1 + 2 = 58
    return 58 * char_w_;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

bool MemoryPanel::hit_test(int x, int y, int& row, int& col) const {
    int y_off = y - header_height();
    if (y_off < 0) return false;

    row = y_off / row_height();
    if (row >= visible_rows()) return false;

    // Check if click is in the hex area.
    int hex_left = hex_area_left();
    int rel_x = x - hex_left;
    if (rel_x < 0) return false;

    // Each byte occupies 3 chars (2 hex digits + space), except there's
    // an extra space between byte 7 and 8.
    // Bytes 0-7: positions 0..23 (each 3 chars)
    // Extra space at position 24
    // Bytes 8-15: positions 25..48 (each 3 chars)

    // Convert pixel x to character column within hex area.
    int char_col = rel_x / char_w_;

    if (char_col < 24) {
        // First group (bytes 0-7)
        col = char_col / 3;
        if (col > 7) return false;
    } else if (char_col >= 25 && char_col < 49) {
        // Second group (bytes 8-15)
        col = 8 + (char_col - 25) / 3;
        if (col > 15) return false;
    } else {
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void MemoryPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);

    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    p.setFont(mono);

    QFontMetrics fm(mono);
    char_w_ = fm.horizontalAdvance('0');
    char_h_ = fm.height();

    int rh = row_height();
    int y0 = header_height();
    int vis = visible_rows();

    if (!dbg_ || vis <= 0) return;

    const int  mode_sel  = page_selector_ ? page_selector_->currentIndex() : 0;
    const bool cpu_view  = mode_sel == 0;
    const int  mode_slot = (mode_sel >= 1 && mode_sel < PAGE_ITEM) ? mode_sel : 0;   // GH #360
    const auto sinfo     = dbg_->mmu_slots();                                        // GH #360
    uint16_t sp = dbg_->registers().SP;
    const bool cov_on = dbg_->mem_coverage_enabled();   // GH #388

    for (int vrow = 0; vrow < vis; ++vrow) {
        int abs_row = scroll_offset_ + vrow;
        if (abs_row >= total_rows()) break;

        uint32_t base_addr = static_cast<uint32_t>(abs_row) * BYTES_PER_ROW;
        int y = y0 + vrow * rh;

        // Determine row background colour.
        QColor bg = Qt::white;   // a light panel's colours; surface() below adapts them
        if (cpu_view) {
            uint16_t row_start = static_cast<uint16_t>(base_addr);
            uint16_t row_end   = static_cast<uint16_t>(base_addr + BYTES_PER_ROW - 1);

            // SP row highlight
            if (sp >= row_start && sp <= row_end) {
                bg = QColor(0xFF, 0xE0, 0xC0);  // light orange
            }
            // VRAM area (0x4000-0x57FF pixel data)
            else if (row_start >= 0x4000 && row_end <= 0x57FF) {
                bg = QColor(0xE0, 0xFF, 0xFF);  // light cyan
            }
            // Attribute area (0x5800-0x5AFF)
            else if (row_start >= 0x5800 && row_end <= 0x5AFF) {
                bg = QColor(0xFF, 0xFF, 0xE0);  // light yellow
            }
            // Overlap: row straddles VRAM/attr boundary
            else if (row_start < 0x5800 && row_end >= 0x5800 && row_start >= 0x4000) {
                bg = QColor(0xE0, 0xFF, 0xFF);
            }
        }

        p.fillRect(0, y, width() - scroll_bar_->width(), rh, palette_tint::surface(palette(), bg));

        // Address column
        p.setPen(palette_tint::ink(palette(), QColor(Qt::darkGray)));
        QString addr_str = QString("$%1").arg(base_addr & 0xFFFF, 4, 16, QChar('0')).toUpper();
        p.drawText(2, y + fm.ascent(), addr_str);

        // Hex bytes — the row's sixteen in one bulk read.
        int hex_x = hex_area_left();
        char ascii_buf[BYTES_PER_ROW + 1];
        uint8_t row_bytes[BYTES_PER_ROW] = {};
        read_bytes(static_cast<uint16_t>(base_addr), row_bytes, BYTES_PER_ROW);
        // GH #388 - the row's coverage flags, through the SAME locate() as the
        // bytes (the CPU view is the live data route, a Slot view the slot's
        // space, a Page view Page{n}).
        uint8_t row_cov[BYTES_PER_ROW] = {};
        if (cov_on) {
            const Where cw = locate(static_cast<uint16_t>(base_addr));
            dbg_->mem_coverage(cw.space, cw.addr, BYTES_PER_ROW, row_cov);
        }

        for (int col = 0; col < BYTES_PER_ROW; ++col) {
            uint16_t addr = static_cast<uint16_t>(base_addr + col);
            uint8_t byte = row_bytes[col];

            // Calculate x position with group separator
            int x_pos;
            if (col < 8) {
                x_pos = hex_x + col * 3 * char_w_;
            } else {
                x_pos = hex_x + (col * 3 + 1) * char_w_;  // extra space after byte 7
            }

            // Check if this byte is selected.
            bool selected = (selected_addr_ >= 0 &&
                             static_cast<uint16_t>(selected_addr_) == addr);

            if (selected) {
                // Blue highlight with white text
                p.fillRect(x_pos - 1, y, char_w_ * 2 + 2, rh, QColor(0x33, 0x66, 0xFF));
                p.setPen(Qt::white);
            } else {
                p.setPen(palette_tint::ink(palette(), QColor(Qt::black)));
            }

            QString hex_str = QString("%1").arg(byte, 2, 16, QChar('0')).toUpper();
            p.drawText(x_pos, y + fm.ascent(), hex_str);

            // GH #388 - a three-segment underline under the byte, X | R | W in
            // thirds of the cell: every combination is shown with no precedence.
            if (row_cov[col]) {
                const int cell = char_w_ * 2;
                const int seg = cell / 3;
                // On the selected cell (a fixed blue fill) the blue R, and in the dark
                // palette every mark, would sit on a like colour: lift the marks one pixel
                // and give them a white backing, one pixel wider all round.
                const int uy = y + rh - (selected ? 3 : 2);
                if (selected) p.fillRect(x_pos - 1, uy - 1, cell + 2, 4, Qt::white);
                if (row_cov[col] & jnext::dbg::COVERAGE_EXEC)
                    p.fillRect(x_pos, uy, seg, 2, coverage_colors::exec(palette()));
                if (row_cov[col] & jnext::dbg::COVERAGE_READ)
                    p.fillRect(x_pos + seg, uy, seg, 2, coverage_colors::read(palette()));
                if (row_cov[col] & jnext::dbg::COVERAGE_WRITE)
                    p.fillRect(x_pos + 2 * seg, uy, cell - 2 * seg, 2, coverage_colors::write(palette()));
            }

            // ASCII representation
            ascii_buf[col] = (byte >= 0x20 && byte <= 0x7E)
                             ? static_cast<char>(byte) : '.';
        }
        ascii_buf[BYTES_PER_ROW] = '\0';

        // ASCII column
        p.setPen(palette_tint::ink(palette(), QColor(Qt::darkBlue)));
        int asc_x = ascii_area_left();
        p.drawText(asc_x, y + fm.ascent(), QString("|%1|").arg(ascii_buf));

        // GH #360 - which memory the row's bytes came from: the slot's live data route
        // in the CPU view, the space shown in a Slot or Page view; its own address in the
        // physical style. After the ASCII item, so nothing before it moves.
        const Where tw = locate(static_cast<uint16_t>(base_addr));
        const jnext::dbg::MemSpace tag_space =
            cpu_view ? memtext::now_of(sinfo, static_cast<uint16_t>(base_addr))
                     : (mode_slot > 0 ? own_space(sinfo[mode_slot - 1]) : tw.space);
        p.setPen(palette_tint::ink(palette(), QColor(Qt::darkMagenta)));
        p.drawText(asc_x + 19 * char_w_, y + fm.ascent(),
                   memtext::text(dbg_, style_, tag_space, static_cast<uint16_t>(tw.addr)));
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void MemoryPanel::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    int row, col;
    if (hit_test(event->pos().x(), event->pos().y(), row, col)) {
        int abs_row = scroll_offset_ + row;
        selected_addr_ = abs_row * BYTES_PER_ROW + col;

        // Clamp to address space.
        int max_addr = total_rows() * BYTES_PER_ROW - 1;
        if (selected_addr_ > max_addr) selected_addr_ = max_addr;

        edit_nibble_ = 0;
        edit_value_ = 0;
        setFocus();
        update();
    } else {
        selected_addr_ = -1;
        edit_nibble_ = 0;
        update();
    }
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void MemoryPanel::keyPressEvent(QKeyEvent* event) {
    int key = event->key();

    // Navigation keys
    if (key == Qt::Key_PageUp) {
        scroll_offset_ -= visible_rows();
        if (scroll_offset_ < 0) scroll_offset_ = 0;
        scroll_bar_->setValue(scroll_offset_);
        update();
        return;
    }
    if (key == Qt::Key_PageDown) {
        scroll_offset_ += visible_rows();
        int max_scroll = total_rows() - visible_rows();
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_offset_ > max_scroll) scroll_offset_ = max_scroll;
        scroll_bar_->setValue(scroll_offset_);
        update();
        return;
    }
    if (key == Qt::Key_Home) {
        scroll_offset_ = 0;
        scroll_bar_->setValue(0);
        update();
        return;
    }
    if (key == Qt::Key_End) {
        int max_scroll = total_rows() - visible_rows();
        if (max_scroll < 0) max_scroll = 0;
        scroll_offset_ = max_scroll;
        scroll_bar_->setValue(scroll_offset_);
        update();
        return;
    }

    // Arrow keys move selection
    if (selected_addr_ >= 0) {
        int max_addr = total_rows() * BYTES_PER_ROW - 1;
        if (key == Qt::Key_Left && selected_addr_ > 0) {
            --selected_addr_;
            edit_nibble_ = 0;
            ensure_visible();
            return;
        }
        if (key == Qt::Key_Right && selected_addr_ < max_addr) {
            ++selected_addr_;
            edit_nibble_ = 0;
            ensure_visible();
            return;
        }
        if (key == Qt::Key_Up && selected_addr_ >= BYTES_PER_ROW) {
            selected_addr_ -= BYTES_PER_ROW;
            edit_nibble_ = 0;
            ensure_visible();
            return;
        }
        if (key == Qt::Key_Down && selected_addr_ + BYTES_PER_ROW <= max_addr) {
            selected_addr_ += BYTES_PER_ROW;
            edit_nibble_ = 0;
            ensure_visible();
            return;
        }
    }

    // Hex digit input for editing
    if (selected_addr_ >= 0) {
        int nibble = -1;
        if (key >= Qt::Key_0 && key <= Qt::Key_9) nibble = key - Qt::Key_0;
        else if (key >= Qt::Key_A && key <= Qt::Key_F) nibble = 10 + (key - Qt::Key_A);

        if (nibble >= 0) {
            if (edit_nibble_ == 0) {
                // High nibble: store and wait for low nibble.
                edit_value_ = static_cast<uint8_t>(nibble << 4);
                edit_nibble_ = 1;
            } else {
                // Low nibble: combine and write.
                edit_value_ |= static_cast<uint8_t>(nibble);
                write_byte(static_cast<uint16_t>(selected_addr_), edit_value_);
                edit_nibble_ = 0;

                // Auto-advance to next byte.
                int max_addr = total_rows() * BYTES_PER_ROW - 1;
                if (selected_addr_ < max_addr) {
                    ++selected_addr_;
                }
            }
            update();
            return;
        }
    }

    // Escape deselects
    if (key == Qt::Key_Escape) {
        selected_addr_ = -1;
        edit_nibble_ = 0;
        update();
        return;
    }

    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Ensure selected address is visible
// ---------------------------------------------------------------------------

void MemoryPanel::ensure_visible() {
    if (selected_addr_ < 0) return;
    int row = selected_addr_ / BYTES_PER_ROW;
    int vis = visible_rows();
    if (vis <= 0) vis = 1;

    if (row < scroll_offset_) {
        scroll_offset_ = row;
    } else if (row >= scroll_offset_ + vis) {
        scroll_offset_ = row - vis + 1;
    }

    int max_scroll = total_rows() - vis;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset_ > max_scroll) scroll_offset_ = max_scroll;
    if (scroll_offset_ < 0) scroll_offset_ = 0;

    scroll_bar_->setValue(scroll_offset_);
    update();
}

// ---------------------------------------------------------------------------
// Scroll wheel
// ---------------------------------------------------------------------------

void MemoryPanel::wheelEvent(QWheelEvent* event) {
    int delta = event->angleDelta().y();
    int lines = delta / 40;  // ~3 lines per standard scroll notch
    if (lines == 0) lines = (delta > 0) ? -1 : 1;
    else lines = -lines;

    scroll_offset_ += lines;
    if (scroll_offset_ < 0) scroll_offset_ = 0;
    int max_scroll = total_rows() - visible_rows();
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset_ > max_scroll) scroll_offset_ = max_scroll;

    scroll_bar_->setValue(scroll_offset_);
    update();
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void MemoryPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update_scroll_bar();
}

// ---------------------------------------------------------------------------
// GH #370 - find, and save / load / fill a block
// ---------------------------------------------------------------------------

namespace mt = jnext::dbg::memtools;

namespace {

QString hex_bytes_text(const std::vector<uint8_t>& v) {
    QStringList l;
    for (uint8_t b : v) l << QString::asprintf("%02X", b);
    return l.join(' ');
}

}  // namespace

void MemoryPanel::fill_context_menu(QMenu& menu) {
    // The actions are the MENU's children: nothing here may stay a child of the panel, because
    // callers find the Addr box and the view selector with findChild<>().
    menu.addAction(tr("Find..."), this, [this]() { show_find_dialog(); });
    QAction* next = menu.addAction(tr("Find Next"), this, [this]() { find_next(); });
    next->setEnabled(last_pattern_.has_value());
    menu.addSeparator();
    menu.addAction(tr("Save Block..."), this, [this]() { show_save_dialog(); });
    menu.addAction(tr("Load Block..."), this, [this]() { show_load_dialog(); });
    menu.addAction(tr("Fill Block..."), this, [this]() { show_fill_dialog(); });
}

void MemoryPanel::contextMenuEvent(QContextMenuEvent* event) {
    if (event->reason() == QContextMenuEvent::Mouse) {
        int row, col;
        if (hit_test(event->pos().x(), event->pos().y(), row, col)) {
            selected_addr_ = (scroll_offset_ + row) * BYTES_PER_ROW + col;
            const int max_addr = total_rows() * BYTES_PER_ROW - 1;
            if (selected_addr_ > max_addr) selected_addr_ = max_addr;
            edit_nibble_ = 0;
            edit_value_ = 0;
            update();
        }
    }
    QMenu menu(this);
    fill_context_menu(menu);
    menu.exec(event->globalPos());
    event->accept();
}

// The Start every dialog offers: the selected byte, else the first visible row.
uint32_t MemoryPanel::start_default() const {
    return selected_addr_ >= 0 ? static_cast<uint32_t>(selected_addr_)
                               : static_cast<uint32_t>(scroll_offset_) * BYTES_PER_ROW;
}

// A start the way the Addr box takes it: a number or a symbol.
bool MemoryPanel::parse_start(const QString& verb, const QString& text, uint32_t& start) {
    const std::string t = text.trimmed().toStdString();
    const auto a = dbg_ ? dbg_->symbols().resolve(t) : SymbolTable().resolve(t);
    if (!a) {
        emit message(verb + tr(" refused: not an address: '%1'").arg(text.trimmed()));
        return false;
    }
    start = *a;
    return true;
}

// WHERE a block's first byte lives. The view bound is checked BEFORE locate(), which masks a
// Slot or Page offset to 13 bits and would turn $2000 into $0000.
bool MemoryPanel::locate_start(const QString& verb, uint32_t start, Where& w) {
    const bool cpu = !page_selector_ || page_selector_->currentIndex() == 0;
    if (!cpu && start >= 0x2000) {
        emit message(verb + tr(" refused: the start is outside the 8K page"));
        return false;
    }
    w = cpu ? Where{jnext::dbg::MemSpace::cpu(), start} : locate(static_cast<uint16_t>(start));
    return true;
}

bool MemoryPanel::plan_block(const QString& verb, uint32_t start, uint32_t len, Block& b) {
    if (!dbg_) {
        emit message(verb + tr(" refused: no debugger"));
        return false;
    }
    if (!locate_start(verb, start, b.w)) return false;
    std::string why;
    auto chunks = mt::split_block(b.w.space, b.w.addr, len, &why);
    if (!chunks) {
        emit message(verb + tr(" refused: ") + QString::fromStdString(why));
        return false;
    }
    b.chunks = std::move(*chunks);
    return true;
}

// `offset $0100 of ROM0` for a space, `$4000` for the CPU view; the address is the SPACE's.
QString MemoryPanel::loc_text(const Where& w, uint32_t space_addr) const {
    if (w.space.kind == jnext::dbg::MemSpace::Kind::Cpu)
        return QString::asprintf("$%04X", space_addr);
    return QString::asprintf("offset $%04X of ", space_addr) +
           QString::fromStdString(jnext::dbg::space_tag(w.space));
}

// One poke per chunk (one MUTATE line, one RZX check, one rewind mark each); stops at the first
// chunk that does not land whole. Returns the bytes that landed.
size_t MemoryPanel::write_chunks(const Block& b, const uint8_t* data, jnext::dbg::Result* stop) {
    size_t landed = 0, off = 0;
    *stop = jnext::dbg::Result::Ok;
    for (const mt::Chunk& c : b.chunks) {
        const auto r = dbg_->poke(client_, c.space, c.addr, c.len, data + off);
        landed += r.value;
        off += c.len;
        if (r.status != jnext::dbg::Result::Ok) {
            *stop = r.status;
            break;
        }
    }
    return landed;
}

// The refusals Load and Fill share. True when it said something (nothing landed).
bool MemoryPanel::report_write_refusal(const QString& verb, size_t landed, jnext::dbg::Result stop) {
    using jnext::dbg::Result;
    if (stop == Result::RefusedRzx) {
        emit message(verb + tr(" refused: memory cannot be changed while an RZX recording or "
                               "playback is running (stop it first)"));
        return true;
    }
    if (landed == 0 && stop != Result::Ok) {
        emit message(verb + tr(" refused: ") +
                     (stop == Result::RefusedReadOnly ? tr("ROM is read-only")
                                                      : QString::fromLatin1(jnext::dbg::result_name(stop))));
        return true;
    }
    return false;
}

void MemoryPanel::run_find(const mt::Pattern& p, size_t from) {
    if (!dbg_) return;
    const bool cpu = !page_selector_ || page_selector_->currentIndex() == 0;
    const size_t size = cpu ? 0x10000 : 0x2000;
    const Where base = locate(0);
    std::vector<uint8_t> buf(size, 0);
    const auto r = dbg_->peek(base.space, base.addr, size, buf.data());
    if (r.value != size) {
        emit message(tr("Find: could not read %1").arg(page_selector_->currentText()));
        return;
    }
    const auto hit = mt::find(buf.data(), size, p, from);
    if (!hit) {
        emit message(tr("Not found in %1").arg(page_selector_->currentText()));
        return;
    }
    const uint32_t off = static_cast<uint32_t>(hit->offset);
    navigate_to_address(static_cast<uint16_t>(off));

    // The CPU address the Disassembly can show: the view's own in CPU View, the slot's in a Slot
    // view, the lowest slot that MAPS the page in a Page view (SlotInfo::space), else none.
    int cpu_addr = -1;
    const int mode = page_selector_->currentIndex();
    if (mode == 0) {
        cpu_addr = static_cast<int>(off);
    } else if (mode == PAGE_ITEM) {
        const auto slot_info = dbg_->mmu_slots();
        for (int k = 0; k < 8; ++k)
            if (slot_info[k].space == jnext::dbg::MemSpace::page(static_cast<uint16_t>(page_))) {
                cpu_addr = k * 0x2000 + static_cast<int>(off);
                break;
            }
    } else {
        cpu_addr = (mode - 1) * 0x2000 + static_cast<int>(off);
    }
    QString text = tr("Found at %1").arg(loc_text(base, base.addr + off));
    if (hit->wrapped) text += tr(" (wrapped)");
    if (cpu_addr < 0) text += tr("; not mapped in any slot, so the Disassembly does not move");
    if (cpu_addr >= 0) emit show_in_disassembly_requested(static_cast<uint16_t>(cpu_addr));
    emit message(text);
}

void MemoryPanel::find(const mt::Pattern& p) {
    last_pattern_ = p;
    run_find(p, selected_addr_ >= 0 ? static_cast<size_t>(selected_addr_) : 0);
}

void MemoryPanel::find_next() {
    if (!last_pattern_) return;
    run_find(*last_pattern_, selected_addr_ >= 0 ? static_cast<size_t>(selected_addr_) + 1 : 0);
}

void MemoryPanel::save_block(uint32_t start, uint32_t len, const QString& path) {
    Block b;
    if (!plan_block(tr("Save"), start, len, b)) return;
    const QString name = QFileInfo(path).fileName();
    QByteArray data(static_cast<qsizetype>(len), 0);
    size_t off = 0;
    for (const mt::Chunk& c : b.chunks) {
        const auto r = dbg_->peek(c.space, c.addr, c.len, reinterpret_cast<uint8_t*>(data.data()) + off);
        if (r.value != c.len) {
            emit message(tr("Save failed: could not read the memory"));
            return;
        }
        off += c.len;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
        emit message(tr("Save failed: %1: %2").arg(name, f.errorString()));
        return;
    }
    emit message(tr("Saved %1 bytes from %2 to %3").arg(len).arg(loc_text(b.w, b.w.addr), name));
}

void MemoryPanel::load_block(uint32_t start, std::optional<uint32_t> len, const QString& path) {
    const QString verb = tr("Load");
    if (!dbg_) {
        emit message(verb + tr(" refused: no debugger"));
        return;
    }
    Where w;
    if (!locate_start(verb, start, w)) return;
    const QString name = QFileInfo(path).fileName();
    if (!QFileInfo(path).isFile()) {
        emit message(tr("Load failed: %1: not a file").arg(name));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit message(tr("Load failed: %1: %2").arg(name, f.errorString()));
        return;
    }
    const QByteArray data = f.readAll();
    const uint32_t F = static_cast<uint32_t>(data.size());
    if (F == 0) {
        emit message(tr("Load refused: %1 is empty").arg(name));
        return;
    }
    const uint32_t M = len ? *len : F;
    const uint32_t room = mt::block_room(w.space, w.addr);
    const uint32_t T = std::min(std::min(M, F), room);
    std::string why;
    if (T == 0) {
        mt::split_block(w.space, w.addr, M ? 1 : 0, &why);   // names what is wrong with the start
        emit message(verb + tr(" refused: ") + QString::fromStdString(why));
        return;
    }
    Block b;
    b.w = w;
    auto chunks = mt::split_block(w.space, w.addr, T, &why);
    if (!chunks) {
        emit message(verb + tr(" refused: ") + QString::fromStdString(why));
        return;
    }
    b.chunks = std::move(*chunks);
    jnext::dbg::Result stop;
    const size_t L = write_chunks(b, reinterpret_cast<const uint8_t*>(data.constData()), &stop);
    update();
    if (report_write_refusal(verb, L, stop)) return;
    if (L == M) {
        emit message(tr("Loaded %1 bytes at %2 from %3").arg(M).arg(loc_text(w, w.addr), name));
        return;
    }
    QStringList why_not;
    if (F < M) why_not << tr("the file holds only %1 bytes").arg(F);
    if (room < std::min(M, F)) {
        // Only Cpu and Page blocks get here: a ROM space refuses the poke with nothing landed.
        why_not << (w.space.kind == jnext::dbg::MemSpace::Kind::Cpu
                        ? tr("the rest would run past $FFFF")
                        : tr("the rest would run past page DF"));
    }
    if (L < T) why_not << tr("the rest is ROM, which is read-only");
    emit message(tr("Loaded %1 of %2 bytes at %3 from %4: %5")
                     .arg(L).arg(M).arg(loc_text(w, w.addr), name, why_not.join("; ")));
}

void MemoryPanel::fill_block(uint32_t start, uint32_t len, const std::vector<uint8_t>& value) {
    const QString verb = tr("Fill");
    Block b;
    if (!plan_block(verb, start, len, b)) return;
    const std::vector<uint8_t> buf = mt::fill_buffer(value, len);
    jnext::dbg::Result stop;
    const size_t L = write_chunks(b, buf.data(), &stop);
    update();
    if (report_write_refusal(verb, L, stop)) return;
    const QString hex = hex_bytes_text(value);
    if (L == len)
        emit message(tr("Filled %1 bytes at %2 with %3").arg(len).arg(loc_text(b.w, b.w.addr), hex));
    else
        emit message(tr("Filled %1 of %2 bytes at %3 with %4: the rest is ROM, which is read-only")
                         .arg(L).arg(len).arg(loc_text(b.w, b.w.addr), hex));
}

// ---- the dialogs: stack objects parented to the panel, gone when they return ------------------

namespace {

QLineEdit* add_row(QFormLayout* form, QDialog* dlg, const QString& label, const QString& text) {
    auto* e = new QLineEdit(text, dlg);
    form->addRow(label, e);
    return e;
}

QDialogButtonBox* add_buttons(QFormLayout* form, QDialog* dlg) {
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    form->addRow(box);
    return box;
}

}  // namespace

void MemoryPanel::show_find_dialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Find"));
    auto* form = new QFormLayout(&dlg);
    QLineEdit* pat = add_row(form, &dlg, tr("Pattern:"), find_text_);
    QLineEdit* mask = add_row(form, &dlg, tr("Mask (optional):"), find_mask_);
    auto* text = new QCheckBox(tr("Text"), &dlg);
    text->setChecked(find_text_mode_);
    form->addRow(text);
    auto* match_case = new QCheckBox(tr("Match case"), &dlg);
    match_case->setChecked(find_match_case_);
    form->addRow(match_case);
    auto sync = [=]() {
        mask->setEnabled(!text->isChecked());
        match_case->setEnabled(text->isChecked());
    };
    connect(text, &QCheckBox::toggled, &dlg, sync);
    sync();
    add_buttons(form, &dlg);
    if (dlg.exec() != QDialog::Accepted) return;

    find_text_ = pat->text();
    find_mask_ = mask->text();
    find_text_mode_ = text->isChecked();
    find_match_case_ = match_case->isChecked();
    std::string why;
    const auto p = find_text_mode_
        ? mt::text_pattern(find_text_.toStdString(), find_match_case_, &why)
        : mt::parse_hex_pattern(find_text_.toStdString(), find_mask_.toStdString(), &why);
    if (!p) {
        emit message(tr("Find: ") + QString::fromStdString(why));
        return;
    }
    find(*p);
}

void MemoryPanel::show_save_dialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save Block"));
    auto* form = new QFormLayout(&dlg);
    QLineEdit* start = add_row(form, &dlg, tr("Start:"), QString::asprintf("$%04X", start_default()));
    QLineEdit* len = add_row(form, &dlg, tr("Length (hex):"), QString());
    QLineEdit* file = add_row(form, &dlg, tr("File:"), QString());
    auto* browse = new QPushButton(tr("Browse..."), &dlg);
    connect(browse, &QPushButton::clicked, &dlg, [&]() {
        const QString f = QFileDialog::getSaveFileName(&dlg, tr("Save Block"), file->text());
        if (!f.isEmpty()) file->setText(f);
    });
    form->addRow(browse);
    add_buttons(form, &dlg);
    if (dlg.exec() != QDialog::Accepted) return;

    uint32_t s;
    std::string why;
    const auto l = mt::parse_length(len->text().toStdString(), &why);
    if (!l) { emit message(tr("Save refused: ") + QString::fromStdString(why)); return; }
    if (!parse_start(tr("Save"), start->text(), s)) return;
    save_block(s, *l, file->text());
}

void MemoryPanel::show_load_dialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Load Block"));
    auto* form = new QFormLayout(&dlg);
    QLineEdit* file = add_row(form, &dlg, tr("File:"), QString());
    auto* browse = new QPushButton(tr("Browse..."), &dlg);
    connect(browse, &QPushButton::clicked, &dlg, [&]() {
        const QString f = QFileDialog::getOpenFileName(&dlg, tr("Load Block"), file->text());
        if (!f.isEmpty()) file->setText(f);
    });
    form->addRow(browse);
    QLineEdit* start = add_row(form, &dlg, tr("Start:"), QString::asprintf("$%04X", start_default()));
    QLineEdit* len = add_row(form, &dlg, tr("Length (hex, blank = whole file):"), QString());
    add_buttons(form, &dlg);
    if (dlg.exec() != QDialog::Accepted) return;

    uint32_t s;
    std::optional<uint32_t> l;
    if (!len->text().trimmed().isEmpty()) {
        std::string why;
        l = mt::parse_length(len->text().toStdString(), &why);
        if (!l) { emit message(tr("Load refused: ") + QString::fromStdString(why)); return; }
    }
    if (!parse_start(tr("Load"), start->text(), s)) return;
    load_block(s, l, file->text());
}

void MemoryPanel::show_fill_dialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Fill Block"));
    auto* form = new QFormLayout(&dlg);
    QLineEdit* start = add_row(form, &dlg, tr("Start:"), QString::asprintf("$%04X", start_default()));
    QLineEdit* len = add_row(form, &dlg, tr("Length (hex):"), QString());
    QLineEdit* value = add_row(form, &dlg, tr("Value (hex bytes):"), QString());
    add_buttons(form, &dlg);
    if (dlg.exec() != QDialog::Accepted) return;

    std::string why;
    const auto l = mt::parse_length(len->text().toStdString(), &why);
    if (!l) { emit message(tr("Fill refused: ") + QString::fromStdString(why)); return; }
    const auto v = mt::parse_fill(value->text().toStdString(), &why);
    if (!v) { emit message(tr("Fill refused: ") + QString::fromStdString(why)); return; }
    uint32_t s;
    if (!parse_start(tr("Fill"), start->text(), s)) return;
    fill_block(s, *l, *v);
}
