#include "disasm_panel.h"
#include "debug/breakpoints.h"

#include <cstring>
#include <cstdlib>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFontDatabase>

#include "core/emulator.h"
#include "debug/symbol_table.h"
#include "debugger/watch_panel.h"

#include <cstring>
#include <cstdlib>

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

    auto* addr_label = new QLabel("Address:");
    addr_input_ = new QLineEdit();
    addr_input_->setPlaceholderText("Address...");
    addr_input_->setFont(mono_font_);
    addr_input_->setMaximumWidth(80);
    addr_input_->setToolTip("Enter hex address (e.g. 4000) and press Enter");

    goto_pc_btn_ = new QPushButton("Go to PC");
    goto_pc_btn_->setMaximumWidth(80);

    top_bar->addWidget(addr_label);
    top_bar->addWidget(addr_input_);
    top_bar->addWidget(goto_pc_btn_);
    top_bar->addStretch();

    scrollbar_ = new QScrollBar(Qt::Vertical, this);
    scrollbar_->setRange(0, 0xFFFF);
    scrollbar_->setSingleStep(3);  // ~1 instruction
    scrollbar_->setPageStep(48);   // ~one page of instructions

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* top_widget = new QWidget();
    top_widget->setLayout(top_bar);
    layout->addWidget(top_widget);
    layout->addStretch();

    // Calculate paint offset (top bar height + column headers + spacing)
    paint_y_offset_ = 52;

    setMinimumSize(GUTTER_WIDTH + ADDR_WIDTH + BYTES_WIDTH + 200,
                   10 * LINE_HEIGHT + paint_y_offset_);
    setFocusPolicy(Qt::StrongFocus);

    // Connect address input
    connect(addr_input_, &QLineEdit::returnPressed, this, [this]() {
        navigate_to_address(addr_input_->text());
    });

    // Goto PC button — centers the view around current PC
    connect(goto_pc_btn_, &QPushButton::clicked, this, [this]() {
        activate_follow_pc();
    });

    // Scrollbar — navigate address space
    connect(scrollbar_, &QScrollBar::valueChanged, this, [this](int value) {
        if (scrollbar_updating_) return;
        view_addr_ = clamp_view_addr(static_cast<uint16_t>(value));
        disassemble_from(view_addr_, visible_lines());
        update();
    });
}

void DisasmPanel::navigate_to_address(const QString& text)
{
    bool ok = false;
    uint16_t addr = static_cast<uint16_t>(text.toUInt(&ok, 16));
    if (!ok) return;

    // Center the target address in the view
    int half = visible_lines() / 2;
    int bytes_back = half * 3; // heuristic: avg Z80 instruction ~3 bytes
    uint16_t start = (addr >= bytes_back) ? (addr - bytes_back) : 0;

    auto read_fn = [this](uint16_t a) -> uint8_t {
        return emulator_->mmu().read(a);
    };

    int extra_lines = visible_lines() + half + 10;
    std::vector<uint16_t> addrs;
    addrs.reserve(extra_lines);
    uint16_t cur = start;
    for (int i = 0; i < extra_lines; ++i) {
        addrs.push_back(cur);
        int len = instruction_length(cur, read_fn);
        cur = static_cast<uint16_t>(cur + len);
    }

    int target_idx = -1;
    for (int i = 0; i < static_cast<int>(addrs.size()); ++i) {
        if (addrs[i] == addr) { target_idx = i; break; }
    }

    if (target_idx >= 0) {
        int start_idx = target_idx - half;
        if (start_idx < 0) start_idx = 0;
        view_addr_ = addrs[start_idx];
    } else {
        view_addr_ = addr;
    }

    disassemble_from(view_addr_, visible_lines());
    update();
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

    // Sync scrollbar
    if (scrollbar_) {
        scrollbar_updating_ = true;
        scrollbar_->setValue(addr);
        scrollbar_updating_ = false;
    }
}

void DisasmPanel::set_paused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    update(); // trigger repaint to show/hide gray overlay
}

uint16_t DisasmPanel::clamp_view_addr(uint16_t addr) const
{
    // Prevent view_addr_ from going so far that the view extends past 0xFFFF.
    // Last visible line should reach ~0xFFFF. Longest Z80 instruction is 4 bytes,
    // so use 1 byte per line (minimum) to compute the tightest bound.
    int max_addr = 0x10000 - visible_lines();
    if (max_addr < 0) max_addr = 0;
    if (addr > static_cast<uint16_t>(max_addr))
        return static_cast<uint16_t>(max_addr);
    return addr;
}

void DisasmPanel::refresh()
{
    if (!paused_) return; // don't update while running freely

    disassemble_from(view_addr_, visible_lines());
    update();
}

void DisasmPanel::activate_follow_pc()
{
    if (!emulator_) return;
    uint16_t pc = emulator_->cpu().get_registers().PC;

    // Center PC in the middle of the visible area
    int half = visible_lines() / 2;
    int bytes_back = half * 3; // heuristic: avg Z80 instruction ~3 bytes
    uint16_t start = (pc >= bytes_back) ? (pc - bytes_back) : 0;

    auto read_fn = [this](uint16_t a) -> uint8_t {
        return emulator_->mmu().read(a);
    };

    int extra_lines = visible_lines() + half + 10;
    std::vector<uint16_t> addrs;
    addrs.reserve(extra_lines);
    uint16_t cur = start;
    for (int i = 0; i < extra_lines; ++i) {
        addrs.push_back(cur);
        int len = instruction_length(cur, read_fn);
        cur = static_cast<uint16_t>(cur + len);
    }

    // Find PC in the address list
    int pc_idx = -1;
    for (int i = 0; i < static_cast<int>(addrs.size()); ++i) {
        if (addrs[i] == pc) { pc_idx = i; break; }
    }

    if (pc_idx >= 0) {
        int start_idx = pc_idx - half;
        if (start_idx < 0) start_idx = 0;
        view_addr_ = addrs[start_idx];
    } else {
        view_addr_ = pc;
    }

    disassemble_from(view_addr_, visible_lines());
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
    painter.fillRect(0, paint_y_offset_, w, visible_lines() * LINE_HEIGHT,
                     QColor(255, 255, 255));

    // Draw column headers just above the disasm lines
    QFont header_font = mono_font_;
    header_font.setBold(true);
    header_font.setPointSize(8);
    QFontMetrics hfm(header_font);
    painter.setFont(header_font);
    painter.setPen(QColor(80, 80, 80));
    int header_y = paint_y_offset_ - 4;
    painter.drawText(2, header_y, "BP");
    painter.drawText(GUTTER_WIDTH + 4, header_y, "Addr");
    painter.setFont(mono_font_);

    // Draw gutter separator
    painter.setPen(QColor(200, 200, 200));
    painter.drawLine(GUTTER_WIDTH, paint_y_offset_,
                     GUTTER_WIDTH, paint_y_offset_ + visible_lines() * LINE_HEIGHT);

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
            painter.setBrush(QColor(255, 0, 0));
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

        // Mnemonic column — with optional symbol resolution
        x += BYTES_WIDTH;
        if (entry.is_current_pc) {
            QFont bold = mono_font_;
            bold.setBold(true);
            painter.setFont(bold);
        }
        painter.setPen(QColor(0, 0, 0));

        QString mnemonic_str(entry.line.mnemonic);
        // Try to resolve 16-bit immediates to symbol names
        if (symbol_table_) {
            uint16_t imm = extract_immediate16(entry.line.mnemonic);
            if (imm != 0 || std::strstr(entry.line.mnemonic, "$0000")) {
                auto sym = symbol_table_->lookup(imm);
                if (sym) {
                    QString target = QString::asprintf("$%04X", imm);
                    mnemonic_str.replace(target, QString::fromStdString(*sym));
                }
            }
        }
        painter.drawText(x, text_y, mnemonic_str);

        if (entry.is_current_pc) {
            painter.setFont(mono_font_);
        }
    }

    // Gray overlay when running (not paused)
    if (!paused_) {
        painter.fillRect(0, paint_y_offset_, w, visible_lines() * LINE_HEIGHT,
                         QColor(192, 192, 192, 80)); // light semi-transparent gray
    }
}

void DisasmPanel::mousePressEvent(QMouseEvent* event)
{
    int line = line_at_y(static_cast<int>(event->position().y()));
    if (line < 0) return;

    if (static_cast<int>(event->position().x()) < GUTTER_WIDTH) {
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

    if (lines_to_scroll > 0) {
        // Scroll down: advance view_addr_ by skipping some instructions
        auto read_fn = [this](uint16_t a) -> uint8_t {
            return emulator_->mmu().read(a);
        };
        uint16_t addr = view_addr_;
        for (int i = 0; i < lines_to_scroll; ++i) {
            int len = instruction_length(addr, read_fn);
            uint16_t next = static_cast<uint16_t>(addr + len);
            if (next < addr) break; // wrap guard
            addr = next;
        }
        view_addr_ = clamp_view_addr(addr);
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

    disassemble_from(view_addr_, visible_lines());
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
            if (view_addr_ >= 3) {
                view_addr_ -= 3;
            } else {
                view_addr_ = 0;
            }
            disassemble_from(view_addr_, visible_lines());
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
            auto read_fn = [this](uint16_t a) -> uint8_t {
                return emulator_->mmu().read(a);
            };
            int len = instruction_length(view_addr_, read_fn);
            view_addr_ = clamp_view_addr(static_cast<uint16_t>(view_addr_ + len));
            disassemble_from(view_addr_, visible_lines());
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
        auto read_fn = [this](uint16_t a) -> uint8_t {
            return emulator_->mmu().read(a);
        };
        uint16_t addr = view_addr_;
        for (int i = 0; i < visible_lines(); ++i) {
            int len = instruction_length(addr, read_fn);
            uint16_t next = static_cast<uint16_t>(addr + len);
            if (next < addr) break;
            addr = next;
        }
        view_addr_ = clamp_view_addr(addr);
        disassemble_from(view_addr_, visible_lines());
        selected_line_ = 0;
        update();
        event->accept();
        break;
    }

    case Qt::Key_PageUp: {
        int bytes_back = visible_lines() * 3; // heuristic
        if (view_addr_ >= bytes_back) {
            view_addr_ -= bytes_back;
        } else {
            view_addr_ = 0;
        }
        disassemble_from(view_addr_, visible_lines());
        selected_line_ = 0;
        update();
        event->accept();
        break;
    }

    case Qt::Key_Home:
        view_addr_ = 0;
        disassemble_from(view_addr_, visible_lines());
        selected_line_ = 0;
        update();
        event->accept();
        break;

    case Qt::Key_End:
        view_addr_ = 0xFF00; // near end of address space
        disassemble_from(view_addr_, visible_lines());
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

    // --- Shared: extract immediate and register info for watch/breakpoint actions ---
    const auto& entry = entries_[line];
    const char* mnem = entry.line.mnemonic;
    uint16_t imm = extract_immediate16(mnem);
    bool has_imm = (imm != 0 || std::strstr(mnem, "$0000"));

    auto regs = emulator_->cpu().get_registers();
    struct { const char* name; const char* pattern; uint16_t val; } reg_pairs[] = {
        {"HL", "(HL)", regs.HL}, {"DE", "(DE)", regs.DE}, {"BC", "(BC)", regs.BC},
        {"IX", "(IX", regs.IX}, {"IY", "(IY", regs.IY}, {"SP", "(SP)", regs.SP}
    };

    // --- Watch actions ---
    if (watch_panel_) {
        menu.addSeparator();

        if (symbol_table_) {
            auto sym = symbol_table_->lookup(addr);
            if (sym) {
                auto* watch_sym = menu.addAction(
                    QString("Watch '%1' ($%2)").arg(QString::fromStdString(*sym))
                        .arg(addr, 4, 16, QChar('0')));
                connect(watch_sym, &QAction::triggered, this, [this, addr, sym]() {
                    watch_panel_->add_watch(addr, *sym);
                });
            }
        }

        if (has_imm) {
            QString label;
            if (symbol_table_) {
                auto sym = symbol_table_->lookup(imm);
                if (sym) label = QString::fromStdString(*sym);
            }
            auto* watch_imm = menu.addAction(
                QString("Watch $%1").arg(imm, 4, 16, QChar('0')));
            connect(watch_imm, &QAction::triggered, this, [this, imm, label]() {
                watch_panel_->add_watch(imm, label.toStdString());
            });
        }

        for (const auto& rp : reg_pairs) {
            if (std::strstr(mnem, rp.pattern)) {
                auto* watch_reg = menu.addAction(
                    QString("Watch (%1) = $%2").arg(rp.name)
                        .arg(rp.val, 4, 16, QChar('0')));
                connect(watch_reg, &QAction::triggered, this, [this, rp]() {
                    watch_panel_->add_watch(rp.val,
                        std::string("(") + rp.name + ")");
                });
            }
        }
    }

    // --- Data breakpoint actions ---
    {
        menu.addSeparator();

        if (has_imm) {
            auto* bp_read = menu.addAction(
                QString("Break on Read $%1").arg(imm, 4, 16, QChar('0')));
            connect(bp_read, &QAction::triggered, this, [this, imm]() {
                emulator_->debug_state().breakpoints().add_watchpoint(imm, WatchType::READ);
            });
            auto* bp_write = menu.addAction(
                QString("Break on Write $%1").arg(imm, 4, 16, QChar('0')));
            connect(bp_write, &QAction::triggered, this, [this, imm]() {
                emulator_->debug_state().breakpoints().add_watchpoint(imm, WatchType::WRITE);
            });
        }

        for (const auto& rp : reg_pairs) {
            if (std::strstr(mnem, rp.pattern)) {
                auto* bp_read = menu.addAction(
                    QString("Break on Read (%1) = $%2").arg(rp.name)
                        .arg(rp.val, 4, 16, QChar('0')));
                connect(bp_read, &QAction::triggered, this, [this, rp]() {
                    emulator_->debug_state().breakpoints().add_watchpoint(rp.val, WatchType::READ);
                });
                auto* bp_write = menu.addAction(
                    QString("Break on Write (%1) = $%2").arg(rp.name)
                        .arg(rp.val, 4, 16, QChar('0')));
                connect(bp_write, &QAction::triggered, this, [this, rp]() {
                    emulator_->debug_state().breakpoints().add_watchpoint(rp.val, WatchType::WRITE);
                });
            }
        }
    }

    menu.exec(event->globalPos());
}

uint16_t DisasmPanel::extract_immediate16(const char* mnemonic)
{
    // Scan mnemonic for a $XXXX pattern (4 hex digits after $)
    const char* p = mnemonic;
    while (*p) {
        if (*p == '$') {
            const char* start = p + 1;
            int digits = 0;
            while (start[digits] && std::isxdigit(static_cast<unsigned char>(start[digits])))
                ++digits;
            if (digits == 4) {
                char buf[5] = {};
                std::memcpy(buf, start, 4);
                return static_cast<uint16_t>(std::strtoul(buf, nullptr, 16));
            }
        }
        ++p;
    }
    return 0; // not found — 0 is ambiguous but acceptable
}

void DisasmPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Position scrollbar on the right edge, from paint_y_offset_ to bottom
    if (scrollbar_) {
        int sb_w = scrollbar_->sizeHint().width();
        scrollbar_->setGeometry(width() - sb_w, paint_y_offset_,
                                sb_w, height() - paint_y_offset_);
        // Adjust max to match clamp_view_addr logic.
        int max_addr = 0x10000 - visible_lines();
        if (max_addr < 0) max_addr = 0;
        scrollbar_updating_ = true;
        scrollbar_->setRange(0, max_addr);
        scrollbar_->setPageStep(visible_lines());
        scrollbar_updating_ = false;
    }

    // Re-disassemble to fill the new height
    disassemble_from(view_addr_, visible_lines());
    update();
}

QSize DisasmPanel::sizeHint() const
{
    return QSize(GUTTER_WIDTH + ADDR_WIDTH + BYTES_WIDTH + 200,
                 20 * LINE_HEIGHT + paint_y_offset_);
}
