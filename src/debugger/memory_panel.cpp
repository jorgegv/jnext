#include "debugger/memory_panel.h"
#include "debug/debugger.h"

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
    addr_input_->setMaxLength(6);
    addr_input_->setMaximumWidth(80);
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    addr_input_->setFont(mono);
    top_bar->addWidget(addr_input_);

    connect(addr_input_, &QLineEdit::returnPressed, this, [this]() {
        QString text = addr_input_->text().trimmed();
        if (text.startsWith('$')) text = text.mid(1);
        if (text.startsWith("0x", Qt::CaseInsensitive)) text = text.mid(2);
        bool ok = false;
        uint16_t addr = text.toUInt(&ok, 16);
        if (ok) navigate_to_address(addr);
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
    update();
}

QSize MemoryPanel::sizeHint() const {
    return QSize(620, 550);
}

// ---------------------------------------------------------------------------
// Memory access helpers
// ---------------------------------------------------------------------------

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
    if (text.startsWith("0x", Qt::CaseInsensitive)) text = text.mid(2);
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
        // effective_page: physical page in use (explicit NR 0x50-0x57 or
        // derived legacy page). nr_page would show 0xFF for legacy ROM slots.
        uint8_t page = slot_info[i].effective_page;
        // The HEX is upper case, the words are not (GH #278 WP0): this used to
        // upper-case the whole label, "SLOT 3 (PAGE 0B)" beside "CPU View".
        page_selector_->setItemText(i + 1,
            QString("Slot %1 (page %2)").arg(i).arg(QString::asprintf("%02X", page)));
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

    bool cpu_view = (page_selector_ ? page_selector_->currentIndex() : 0) == 0;
    uint16_t sp = dbg_->registers().SP;

    for (int vrow = 0; vrow < vis; ++vrow) {
        int abs_row = scroll_offset_ + vrow;
        if (abs_row >= total_rows()) break;

        uint32_t base_addr = static_cast<uint32_t>(abs_row) * BYTES_PER_ROW;
        int y = y0 + vrow * rh;

        // Determine row background colour.
        QColor bg = Qt::white;
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

        p.fillRect(0, y, width() - scroll_bar_->width(), rh, bg);

        // Address column
        p.setPen(Qt::darkGray);
        QString addr_str = QString("$%1").arg(base_addr & 0xFFFF, 4, 16, QChar('0')).toUpper();
        p.drawText(2, y + fm.ascent(), addr_str);

        // Hex bytes — the row's sixteen in one bulk read.
        int hex_x = hex_area_left();
        char ascii_buf[BYTES_PER_ROW + 1];
        uint8_t row_bytes[BYTES_PER_ROW] = {};
        read_bytes(static_cast<uint16_t>(base_addr), row_bytes, BYTES_PER_ROW);

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
                p.setPen(Qt::black);
            }

            QString hex_str = QString("%1").arg(byte, 2, 16, QChar('0')).toUpper();
            p.drawText(x_pos, y + fm.ascent(), hex_str);

            // ASCII representation
            ascii_buf[col] = (byte >= 0x20 && byte <= 0x7E)
                             ? static_cast<char>(byte) : '.';
        }
        ascii_buf[BYTES_PER_ROW] = '\0';

        // ASCII column
        p.setPen(Qt::darkBlue);
        int asc_x = ascii_area_left();
        p.drawText(asc_x, y + fm.ascent(), QString("|%1|").arg(ascii_buf));
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
