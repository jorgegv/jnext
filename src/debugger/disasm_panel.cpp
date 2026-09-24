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
#include <QClipboard>
#include <QGuiApplication>
#include <QKeySequence>

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

    // GH #21 — the clipboard commands are real QActions ON THIS WIDGET, not
    // keys handled in keyPressEvent. Three things follow, and all three are
    // the reason:
    //   * the chord is ONE object, instead of a handler and a menu label that
    //     can drift apart;
    //   * the context menu renders the shortcut next to the entry without
    //     being told what it is;
    //   * the binding is enumerable — `findChildren<QAction*>()` reaches it —
    //     which is what lets host_hotkey_test check that a chord this project
    //     advertises in FEATURES.md is one the product actually binds. A
    //     keyPressEvent branch is invisible to that gate.
    //
    // Ctrl, not Alt. The main window puts host hotkeys on Alt because Ctrl
    // there is the guest's Symbol Shift; the debugger is a SEPARATE top-level
    // window (DebuggerWindow is its own QMainWindow) with no key handler and
    // no event filter feeding Keyboard::set_key(), so nothing typed here can
    // reach the guest and the standard clipboard chords are free.
    //
    // WidgetShortcut: they fire only while this panel has focus. Ctrl+C in
    // the memory or watch panel is not this panel's to take.
    copy_action_ = new QAction(tr("Copy"), this);
    copy_action_->setShortcut(QKeySequence::Copy);
    copy_action_->setShortcutContext(Qt::WidgetShortcut);
    copy_action_->setToolTip(tr("Copy the selected lines as assembly"));
    connect(copy_action_, &QAction::triggered, this, [this]() {
        copy_selection(disasm_text::CopyFormat::AsmOnly);
    });
    addAction(copy_action_);

    copy_addresses_action_ = new QAction(tr("Copy with Addresses"), this);
    copy_addresses_action_->setToolTip(
        tr("Copy the selected lines with their addresses and opcode bytes"));
    connect(copy_addresses_action_, &QAction::triggered, this, [this]() {
        copy_selection(disasm_text::CopyFormat::WithAddresses);
    });
    addAction(copy_addresses_action_);

    select_all_action_ = new QAction(tr("Select All"), this);
    select_all_action_->setShortcut(QKeySequence::SelectAll);
    select_all_action_->setShortcutContext(Qt::WidgetShortcut);
    connect(select_all_action_, &QAction::triggered, this, [this]() {
        select_all_visible();
    });
    addAction(select_all_action_);

    // GH #220 — the gutter is driven by the set, not by whoever mutated it.
    // PcBreakpoints ONLY: the gutter paints bps.has_pc() (see paintEvent), so
    // re-disassembling on a watchpoint change would be pure waste.
    observer_ = emulator_->debug_state().breakpoints().add_observer(
        [this](BreakpointChange what) {
            if (what == BreakpointChange::PcBreakpoints) refresh();
        });
}

DisasmPanel::~DisasmPanel()
{
    emulator_->debug_state().breakpoints().remove_observer(observer_);
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
        // GH #225 — two questions, not one: does a breakpoint EXIST here,
        // and can it fire? paintEvent draws a filled dot for the second and a
        // hollow ring for a breakpoint that is only the first.
        entry.has_breakpoint  = bps.pc_exists(cur);
        entry.breakpoint_live = bps.has_pc(cur);
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

void DisasmPanel::run_to_selected()
{
    // GH #1. Deliberately routed through the SIGNAL rather than calling
    // DebugState::run_to() here: DebuggerManager's handler carries the Task 60e
    // corruption gate and the four set_paused(false) calls, and a second
    // implementation of that would drift.
    emit run_to_requested(selected_address());
}

int DisasmPanel::line_at_y(int y) const
{
    int adjusted = y - paint_y_offset_;
    if (adjusted < 0) return -1;
    int line = adjusted / LINE_HEIGHT;
    if (line < 0 || line >= static_cast<int>(entries_.size())) return -1;
    return line;
}

int DisasmPanel::line_at_y_clamped(int y) const
{
    if (entries_.empty()) return -1;
    const int adjusted = y - paint_y_offset_;
    if (adjusted < 0) return 0;                       // dragged off the top
    const int line = adjusted / LINE_HEIGHT;
    const int last = static_cast<int>(entries_.size()) - 1;
    return (line > last) ? last : line;               // dragged off the bottom
}

// ---------------------------------------------------------------------------
// GH #21 — selection and copy
// ---------------------------------------------------------------------------

void DisasmPanel::set_selection(uint16_t addr, bool extend)
{
    if (!extend || !has_selection_) sel_anchor_ = addr;
    sel_cursor_    = addr;
    has_selection_ = true;
    update();
}

void DisasmPanel::clear_selection()
{
    if (!has_selection_) return;
    has_selection_ = false;
    update();
}

bool DisasmPanel::selection_range(uint16_t& low, uint16_t& high) const
{
    if (!has_selection_) return false;
    low  = std::min(sel_anchor_, sel_cursor_);
    high = std::max(sel_anchor_, sel_cursor_);
    return true;
}

bool DisasmPanel::line_selected(uint16_t addr) const
{
    uint16_t low = 0, high = 0;
    if (!selection_range(low, high)) return false;
    return addr >= low && addr <= high;
}

void DisasmPanel::select_all_visible()
{
    if (entries_.empty()) { clear_selection(); return; }
    sel_anchor_    = entries_.front().line.addr;
    sel_cursor_    = entries_.back().line.addr;
    has_selection_ = true;
    // The caret moves with the cursor, exactly as it does for a drag or a
    // Shift-arrow. Leaving it behind would make selected_address() report a
    // line that is no longer where the selection ends — a desync with no
    // symptom today, because nothing in the product calls that method, and a
    // trap for whoever wires it to something.
    selected_line_ = static_cast<int>(entries_.size()) - 1;
    update();
}

QString DisasmPanel::selection_text(disasm_text::CopyFormat fmt) const
{
    uint16_t low = 0, high = 0;
    if (!selection_range(low, high)) return QString();

    // Live memory, not the painted lines — see the header comment. This is
    // also why a selection made before the view scrolled away still copies in
    // full: nothing about the copy depends on what is currently on screen.
    auto read_fn = [this](uint16_t a) -> uint8_t {
        return emulator_->mmu().read(a);
    };

    const auto lines = disasm_text::collect_range(low, high, read_fn, symbol_table_);
    return QString::fromStdString(disasm_text::format_lines(lines, fmt));
}

void DisasmPanel::copy_selection(disasm_text::CopyFormat fmt)
{
    const QString text = selection_text(fmt);
    // Nothing selected: leave whatever is on the clipboard alone. A copy that
    // silently wipes the clipboard is worse than a copy that does nothing.
    if (text.isEmpty()) return;
    if (QClipboard* clip = QGuiApplication::clipboard()) clip->setText(text);
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
        // GH #21 — the selection, as a range of addresses. Two things the
        // fill must not destroy: it starts at GUTTER_WIDTH, so the breakpoint
        // dot stays fully opaque, and it is semi-transparent, so the PC row's
        // yellow tint still shows through under it and its bold black text
        // stays readable. Same colour and alpha the single selected line has
        // always used — this is the same highlight, over more lines.
        if (line_selected(entry.line.addr)) {
            painter.fillRect(GUTTER_WIDTH, y, w - GUTTER_WIDTH, LINE_HEIGHT,
                             QColor(204, 221, 255, 128)); // semi-transparent light blue
        }

        // Breakpoint indicator in the gutter.
        //
        // GH #225 — a SUSPENDED breakpoint (individually disabled, or any
        // breakpoint while the master switch is off) is drawn as a HOLLOW RING
        // in the same place, at the same size, in the same red.
        //
        // Not hidden, and that is the decision the issue asks for. Hiding it
        // makes the breakpoint invisible exactly where the user set it: the
        // gutter click that would bring it back has nothing to aim at, the
        // list and the gutter disagree about what exists, and the obvious
        // reading is "it was deleted" — which is the one thing disabling
        // must not look like. An outline keeps the address marked and reads
        // as clearly different from an armed one.
        //
        // Measured rather than asserted (GH #225 review): at the gutter's real
        // size the marker is about 8x8 px, where disc-versus-ring is legible
        // but not generous. If it ever has to be plainer, the honest lever is
        // a SECOND visual channel — a paler red for suspended — not a bigger
        // ring: the gutter is 20 px wide and the dot already fills it.
        if (entry.has_breakpoint) {
            const int cx = GUTTER_WIDTH / 2;
            const int cy = y + LINE_HEIGHT / 2;
            if (entry.breakpoint_live) {
                painter.setBrush(QColor(255, 0, 0));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(QPoint(cx, cy), 5, 5);
            } else {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(255, 0, 0), 2));
                painter.drawEllipse(QPoint(cx, cy), 4, 4);
            }
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

        // Symbol substitution lives in disasm_text::apply_symbols(), which is
        // also what the clipboard path calls — GH #21 requires the copied text
        // to carry the symbolic form, and one shared rule is what guarantees
        // the two can never drift apart.
        const QString mnemonic_str = QString::fromStdString(
            disasm_text::apply_symbols(entry.line.mnemonic, symbol_table_));
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
    // Qt5/Qt6 (GH #108 Phase A): QMouseEvent::position() is Qt6 QSinglePointEvent
    // API; Qt 5.15 spells the same widget-local QPointF localPos().
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF evpos = event->position();
#else
    const QPointF evpos = event->localPos();
#endif
    int line = line_at_y(static_cast<int>(evpos.y()));
    if (line < 0) return;

    if (static_cast<int>(evpos.x()) < GUTTER_WIDTH) {
        // Toggle breakpoint
        uint16_t addr = entries_[line].line.addr;
        auto& bps = emulator_->debug_state().breakpoints();
        // GH #225 — pc_exists(), not has_pc(): the gutter click toggles
        // whether a breakpoint IS THERE, which is what it has always done.
        // has_pc() is now "can it fire", so a click on a suspended breakpoint
        // would have read "none here" and added a second one on top of it.
        // Enabling and disabling is the Breakpoints panel's checkbox.
        if (bps.pc_exists(addr)) {
            bps.remove_pc(addr);
        } else {
            bps.add_pc(addr);
        }
        // The set's observer has already run. While PAUSED it re-disassembled,
        // so entries_ was rebuilt underneath us — hence the bounds check. While
        // RUNNING refresh() is a no-op by design, and this patch plus update()
        // is what still shows the dot the instant it is clicked.
        if (line < static_cast<int>(entries_.size())) {
            entries_[line].has_breakpoint  = bps.pc_exists(addr);
            entries_[line].breakpoint_live = bps.has_pc(addr);
        }
        update();
    } else {
        // Select line. GH #21: a plain click is also a one-line SELECTION —
        // anchor and cursor on the same address — so Ctrl+C right after a
        // click copies that line, and a drag from here extends it.
        selected_line_ = line;
        set_selection(entries_[line].line.addr,
                      (event->modifiers() & Qt::ShiftModifier) != 0);
        dragging_ = true;
        update();
    }
}

void DisasmPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) { QWidget::mouseMoveEvent(event); return; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF evpos = event->position();
#else
    const QPointF evpos = event->localPos();
#endif
    // Clamped, not line_at_y(): dragging past the top or bottom edge should
    // keep extending to that end, not stop dead. There is deliberately no
    // auto-scroll — this panel scrolls by re-disassembling from a heuristic
    // address, which would move the lines under the pointer mid-drag.
    const int line = line_at_y_clamped(static_cast<int>(evpos.y()));
    if (line < 0) return;
    selected_line_ = line;
    set_selection(entries_[line].line.addr, /*extend=*/true);
}

void DisasmPanel::mouseReleaseEvent(QMouseEvent* event)
{
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
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
    // Ctrl+C and Ctrl+A are deliberately NOT handled here — they are
    // shortcuts on copy_action_ / select_all_action_, created in the
    // constructor. Qt's shortcut map consumes them before a key event is
    // delivered, so a branch here would be a second, silent implementation of
    // the same binding.
    //
    // Whether this key moved the selected line, and whether it should extend
    // the selection rather than collapse it. See the sync after the switch.
    bool       nav    = false;
    const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0;

    switch (event->key()) {
    case Qt::Key_Up:
        nav = true;
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
        nav = true;
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
        nav = true;
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
        nav = true;
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
        nav = true;
        view_addr_ = 0;
        disassemble_from(view_addr_, visible_lines());
        selected_line_ = 0;
        update();
        event->accept();
        break;

    case Qt::Key_End:
        nav = true;
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

    // GH #21 — keep the selection on the line the keyboard moved to. This is
    // forced, not decorative: the blue highlight is painted from the
    // SELECTION now, so without this the arrow keys would move
    // selected_line_ and leave the highlight behind. Shift extends, exactly
    // as Shift-click does.
    if (nav && selected_line_ >= 0 &&
        selected_line_ < static_cast<int>(entries_.size()))
        set_selection(entries_[selected_line_].line.addr, extend);
}

void DisasmPanel::contextMenuEvent(QContextMenuEvent* event)
{
    int line = line_at_y(event->y());
    if (line < 0 || line >= static_cast<int>(entries_.size())) return;

    selected_line_ = line;

    // GH #21 — right-clicking INSIDE the selection keeps it (that is how you
    // copy a range you just dragged out); right-clicking outside it collapses
    // the selection onto that one line, which is what every list widget does.
    const uint16_t clicked_addr = entries_[line].line.addr;
    if (!line_selected(clicked_addr))
        set_selection(clicked_addr, /*extend=*/false);

    update();

    uint16_t addr = entries_[line].line.addr;

    QMenu menu(this);

    // Copy first: it is the entry a reader of this panel reaches for most, and
    // both forms are offered because they answer different questions — one
    // pastes into a source file, the other into a bug report. These are the
    // SAME QAction objects that carry the Ctrl+C / Ctrl+A bindings, not menu
    // twins of them, so the menu and the keyboard cannot disagree.
    menu.addAction(copy_action_);
    menu.addAction(copy_addresses_action_);
    menu.addAction(select_all_action_);

    menu.addSeparator();

    auto* toggle_bp = menu.addAction("Toggle Breakpoint");
    connect(toggle_bp, &QAction::triggered, this, [this, addr, line]() {
        auto& bps = emulator_->debug_state().breakpoints();
        if (bps.pc_exists(addr)) {
            bps.remove_pc(addr);
        } else {
            bps.add_pc(addr);
        }
        // Same as the gutter click above — see mousePressEvent().
        if (line < static_cast<int>(entries_.size())) {
            entries_[line].has_breakpoint  = bps.pc_exists(addr);
            entries_[line].breakpoint_live = bps.has_pc(addr);
        }
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
    //
    // GH #218/#220 — every one of these adds a WATCHPOINT, which the
    // Breakpoints panel lists but this gutter does not draw (it paints has_pc()
    // only). #218 gave them an explicit signal to repaint that list; #220
    // deleted it, because add_watchpoint() now notifies the list itself. Note
    // what is NOT here as a result: no repaint of our own, and no wire to a
    // panel we do not own.
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
    // One implementation, in debug/disasm_text.cpp. The context menu below
    // still reaches it through this name; the copy path and the painter reach
    // apply_symbols(), which is built on the same scan.
    return disasm_text::extract_immediate16(mnemonic);
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
