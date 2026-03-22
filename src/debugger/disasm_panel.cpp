#include "disasm_panel.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFontDatabase>

#include "core/emulator.h"

DisasmPanel::DisasmPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    // Set up monospace font
    mono_font_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono_font_.setPointSize(10);

    // Top bar with address input and follow-PC checkbox
    auto* top_bar = new QHBoxLayout();
    top_bar->setContentsMargins(2, 2, 2, 2);

    auto* addr_label = new QLabel("Addr:");
    addr_input_ = new QLineEdit();
    addr_input_->setPlaceholderText("Address...");
    addr_input_->setFont(mono_font_);
    addr_input_->setMaximumWidth(80);
    addr_input_->setToolTip("Enter hex address (e.g. 4000) and press Enter");

    follow_pc_ = new QCheckBox("Follow PC");
    follow_pc_->setChecked(true);

    top_bar->addWidget(addr_label);
    top_bar->addWidget(addr_input_);
    top_bar->addWidget(follow_pc_);
    top_bar->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* top_widget = new QWidget();
    top_widget->setLayout(top_bar);
    layout->addWidget(top_widget);
    layout->addStretch();

    // Calculate paint offset (top bar height)
    paint_y_offset_ = 28; // approximate top bar height

    setMinimumSize(GUTTER_WIDTH + ADDR_WIDTH + BYTES_WIDTH + 300,
                   VISIBLE_LINES * LINE_HEIGHT + paint_y_offset_);
    setFocusPolicy(Qt::StrongFocus);

    // Connect address input
    connect(addr_input_, &QLineEdit::returnPressed, this, [this]() {
        navigate_to_address(addr_input_->text());
    });

    // When follow-PC is re-checked, refresh to jump to PC
    connect(follow_pc_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) refresh();
    });
}

void DisasmPanel::navigate_to_address(const QString& text)
{
    bool ok = false;
    uint16_t addr = static_cast<uint16_t>(text.toUInt(&ok, 16));
    if (ok) {
        follow_pc_->setChecked(false);
        view_addr_ = addr;
        disassemble_from(view_addr_, VISIBLE_LINES);
        update();
    }
}

void DisasmPanel::disassemble_from(uint16_t addr, int count)
{
    entries_.clear();
    entries_.reserve(count);

    // Memory read function via emulator MMU
    auto read_fn = [this](uint16_t a) -> uint8_t {
        return emulator_->mmu().read(a);
    };

    uint16_t current_pc = emulator_->cpu().get_registers().PC;
    const auto& bps = emulator_->debug_state().breakpoints();

    uint16_t cur = addr;
    for (int i = 0; i < count; ++i) {
        DisasmEntry entry;
        entry.line = disasm_one(cur, read_fn);
        entry.is_current_pc = (cur == current_pc);
        entry.has_breakpoint = bps.has_pc(cur);
        entries_.push_back(entry);

        cur = static_cast<uint16_t>(cur + entry.line.byte_count);
        // Handle wrap-around at 0xFFFF
        if (cur < addr && i > 0 && entry.line.byte_count > 0) {
            // We've wrapped around the address space; stop
            break;
        }
    }
}

void DisasmPanel::refresh()
{
    uint16_t pc = emulator_->cpu().get_registers().PC;

    if (follow_pc_->isChecked()) {
        view_addr_ = pc;
    }

    disassemble_from(view_addr_, VISIBLE_LINES);
    update();
}

uint16_t DisasmPanel::selected_address() const
{
    if (selected_line_ >= 0 && selected_line_ < static_cast<int>(entries_.size())) {
        return entries_[selected_line_].line.addr;
    }
    // Fallback: return current PC
    return emulator_->cpu().get_registers().PC;
}

int DisasmPanel::line_at_y(int y) const
{
    int adjusted = y - paint_y_offset_;
    if (adjusted < 0) return -1;
    int line = adjusted / LINE_HEIGHT;
    if (line < 0 || line >= static_cast<int>(entries_.size())) return -1;
    return line;
}

void DisasmPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setFont(mono_font_);

    int w = width();

    // Fill background
    painter.fillRect(0, paint_y_offset_, w, VISIBLE_LINES * LINE_HEIGHT,
                     QColor(255, 255, 255));

    // Draw gutter separator
    painter.setPen(QColor(200, 200, 200));
    painter.drawLine(GUTTER_WIDTH, paint_y_offset_,
                     GUTTER_WIDTH, paint_y_offset_ + VISIBLE_LINES * LINE_HEIGHT);

    QFontMetrics fm(mono_font_);

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& entry = entries_[i];
        int y = paint_y_offset_ + i * LINE_HEIGHT;
        int text_y = y + fm.ascent() + (LINE_HEIGHT - fm.height()) / 2;

        // Row background highlights
        if (entry.is_current_pc) {
            painter.fillRect(GUTTER_WIDTH, y, w - GUTTER_WIDTH, LINE_HEIGHT,
                             QColor(255, 255, 204)); // light yellow
        }
        if (i == selected_line_) {
            painter.fillRect(GUTTER_WIDTH, y, w - GUTTER_WIDTH, LINE_HEIGHT,
                             QColor(204, 221, 255, 128)); // semi-transparent light blue
        }

        // Breakpoint indicator (red circle in gutter)
        if (entry.has_breakpoint) {
            painter.setBrush(QColor(220, 40, 40));
            painter.setPen(Qt::NoPen);
            int cx = GUTTER_WIDTH / 2;
            int cy = y + LINE_HEIGHT / 2;
            painter.drawEllipse(QPoint(cx, cy), 5, 5);
        }

        // Address column
        int x = GUTTER_WIDTH + 4;
        painter.setPen(QColor(100, 100, 100));
        QString addr_str = QString::asprintf("$%04X", entry.line.addr);
        painter.drawText(x, text_y, addr_str);

        // Hex bytes column
        x += ADDR_WIDTH;
        painter.setPen(QColor(150, 150, 150));
        QString bytes_str;
        for (int b = 0; b < entry.line.byte_count; ++b) {
            if (b > 0) bytes_str += ' ';
            bytes_str += QString::asprintf("%02X", entry.line.bytes[b]);
        }
        painter.drawText(x, text_y, bytes_str);

        // Mnemonic column
        x += BYTES_WIDTH;
        if (entry.is_current_pc) {
            QFont bold = mono_font_;
            bold.setBold(true);
            painter.setFont(bold);
        }
        painter.setPen(QColor(0, 0, 0));
        painter.drawText(x, text_y, QString(entry.line.mnemonic));

        if (entry.is_current_pc) {
            painter.setFont(mono_font_);
        }
    }
}

void DisasmPanel::mousePressEvent(QMouseEvent* event)
{
    int line = line_at_y(event->y());
    if (line < 0) return;

    if (event->x() < GUTTER_WIDTH) {
        // Toggle breakpoint
        uint16_t addr = entries_[line].line.addr;
        auto& bps = emulator_->debug_state().breakpoints();
        if (bps.has_pc(addr)) {
            bps.remove_pc(addr);
        } else {
            bps.add_pc(addr);
        }
        entries_[line].has_breakpoint = bps.has_pc(addr);
        emit breakpoint_toggled(addr);
        update();
    } else {
        // Select line
        selected_line_ = line;
        update();
    }
}

void DisasmPanel::wheelEvent(QWheelEvent* event)
{
    int delta = event->angleDelta().y();
    int lines_to_scroll = (delta > 0) ? -3 : 3;

    if (follow_pc_->isChecked()) {
        follow_pc_->setChecked(false);
    }

    if (lines_to_scroll > 0) {
        // Scroll down: advance view_addr_ by skipping some instructions
        auto read_fn = [this](uint16_t a) -> uint8_t {
            return emulator_->mmu().read(a);
        };
        uint16_t addr = view_addr_;
        for (int i = 0; i < lines_to_scroll; ++i) {
            int len = instruction_length(addr, read_fn);
            addr = static_cast<uint16_t>(addr + len);
        }
        view_addr_ = addr;
    } else {
        // Scroll up: heuristic - go back ~3 bytes per line (average Z80 instruction)
        int bytes_back = (-lines_to_scroll) * 3;
        if (view_addr_ >= bytes_back) {
            view_addr_ -= bytes_back;
        } else {
            view_addr_ = 0;
        }
        // Re-disassemble from the estimated address to try to align
        // This is inherently imprecise for variable-length ISAs
    }

    disassemble_from(view_addr_, VISIBLE_LINES);
    update();
    event->accept();
}

void DisasmPanel::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        if (selected_line_ > 0) {
            --selected_line_;
            update();
        } else {
            // Scroll up by one line
            if (follow_pc_->isChecked()) follow_pc_->setChecked(false);
            if (view_addr_ >= 3) {
                view_addr_ -= 3;
            } else {
                view_addr_ = 0;
            }
            disassemble_from(view_addr_, VISIBLE_LINES);
            selected_line_ = 0;
            update();
        }
        event->accept();
        break;

    case Qt::Key_Down:
        if (selected_line_ < static_cast<int>(entries_.size()) - 1) {
            ++selected_line_;
            update();
        } else if (!entries_.empty()) {
            // Scroll down by one line
            if (follow_pc_->isChecked()) follow_pc_->setChecked(false);
            auto read_fn = [this](uint16_t a) -> uint8_t {
                return emulator_->mmu().read(a);
            };
            int len = instruction_length(view_addr_, read_fn);
            view_addr_ = static_cast<uint16_t>(view_addr_ + len);
            disassemble_from(view_addr_, VISIBLE_LINES);
            selected_line_ = static_cast<int>(entries_.size()) - 1;
            update();
        }
        event->accept();
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (selected_line_ >= 0 && selected_line_ < static_cast<int>(entries_.size())) {
            emit run_to_requested(entries_[selected_line_].line.addr);
        }
        event->accept();
        break;

    case Qt::Key_PageDown: {
        if (follow_pc_->isChecked()) follow_pc_->setChecked(false);
        auto read_fn = [this](uint16_t a) -> uint8_t {
            return emulator_->mmu().read(a);
        };
        uint16_t addr = view_addr_;
        for (int i = 0; i < VISIBLE_LINES; ++i) {
            int len = instruction_length(addr, read_fn);
            addr = static_cast<uint16_t>(addr + len);
        }
        view_addr_ = addr;
        disassemble_from(view_addr_, VISIBLE_LINES);
        selected_line_ = 0;
        update();
        event->accept();
        break;
    }

    case Qt::Key_PageUp: {
        if (follow_pc_->isChecked()) follow_pc_->setChecked(false);
        int bytes_back = VISIBLE_LINES * 3; // heuristic
        if (view_addr_ >= bytes_back) {
            view_addr_ -= bytes_back;
        } else {
            view_addr_ = 0;
        }
        disassemble_from(view_addr_, VISIBLE_LINES);
        selected_line_ = 0;
        update();
        event->accept();
        break;
    }

    case Qt::Key_Home:
        if (follow_pc_->isChecked()) follow_pc_->setChecked(false);
        view_addr_ = 0;
        disassemble_from(view_addr_, VISIBLE_LINES);
        selected_line_ = 0;
        update();
        event->accept();
        break;

    case Qt::Key_End:
        if (follow_pc_->isChecked()) follow_pc_->setChecked(false);
        view_addr_ = 0xFF00; // near end of address space
        disassemble_from(view_addr_, VISIBLE_LINES);
        selected_line_ = 0;
        update();
        event->accept();
        break;

    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

void DisasmPanel::contextMenuEvent(QContextMenuEvent* event)
{
    int line = line_at_y(event->y());
    if (line < 0 || line >= static_cast<int>(entries_.size())) return;

    selected_line_ = line;
    update();

    uint16_t addr = entries_[line].line.addr;

    QMenu menu(this);

    auto* toggle_bp = menu.addAction("Toggle Breakpoint");
    connect(toggle_bp, &QAction::triggered, this, [this, addr, line]() {
        auto& bps = emulator_->debug_state().breakpoints();
        if (bps.has_pc(addr)) {
            bps.remove_pc(addr);
        } else {
            bps.add_pc(addr);
        }
        entries_[line].has_breakpoint = bps.has_pc(addr);
        emit breakpoint_toggled(addr);
        update();
    });

    auto* run_to = menu.addAction("Run to Here");
    connect(run_to, &QAction::triggered, this, [this, addr]() {
        emit run_to_requested(addr);
    });

    menu.addSeparator();

    auto* go_to = menu.addAction("Go to Address...");
    connect(go_to, &QAction::triggered, this, [this]() {
        addr_input_->setFocus();
        addr_input_->selectAll();
    });

    menu.exec(event->globalPos());
}

QSize DisasmPanel::sizeHint() const
{
    return QSize(GUTTER_WIDTH + ADDR_WIDTH + BYTES_WIDTH + 300,
                 VISIBLE_LINES * LINE_HEIGHT + paint_y_offset_);
}
