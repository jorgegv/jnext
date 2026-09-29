#include "debugger/stack_panel.h"
#include "debug/debugger.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>

StackPanel::StackPanel(const jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    create_ui();
}

void StackPanel::create_ui() {
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(STACK_ROWS, 4, this);
    table_->setFont(mono);
    table_->setHorizontalHeaderLabels({"Address", "Word", "High Byte", "Low Byte"});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setFont(mono);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    // Column widths
    table_->setColumnWidth(0, 75);   // Address
    table_->setColumnWidth(1, 145);  // Word hex+dec
    table_->setColumnWidth(2, 95);   // High Byte
    table_->setColumnWidth(3, 95);   // Low Byte

    // Pre-populate cells
    for (int r = 0; r < STACK_ROWS; ++r) {
        for (int c = 0; c < 4; ++c) {
            auto* item = new QTableWidgetItem("--");
            item->setTextAlignment(Qt::AlignCenter);
            table_->setItem(r, c, item);
        }
        table_->setRowHeight(r, 20);
    }

    layout->addWidget(table_);
}

void StackPanel::set_paused(bool paused) {
    paused_ = paused;
}

void StackPanel::refresh() {
    if (!dbg_ || !paused_) return;

    uint16_t sp = dbg_->registers().SP;   // INS-01

    // Display in ascending address order: SP at top (row 0), higher addresses below.
    for (int r = 0; r < STACK_ROWS; ++r) {
        int offset = r * 2;
        uint16_t addr = static_cast<uint16_t>(sp + offset);

        // Guard against wrap-around at top of memory
        if (addr < sp && offset > 0) {
            table_->item(r, 0)->setText("----");
            table_->item(r, 1)->setText("----");
            table_->item(r, 2)->setText("--");
            table_->item(r, 3)->setText("--");
            for (int c = 0; c < 4; ++c)
                table_->item(r, c)->setBackground(QColor(Qt::white));
            continue;
        }

        // INS-02 — a PEEK, not the CPU's read: an observation moves nothing
        // (the +3 floating-bus latch, which Mmu::read() latches on a contended
        // address) and fires no watch. The high byte is addr + 1 modulo 64K, as
        // it was: an odd SP's last row, $FFFF, pairs with $0000.
        uint8_t b[2] = {0, 0};
        dbg_->peek(jnext::dbg::MemSpace::cpu(), addr, 2, b);
        uint8_t lo = b[0];
        uint8_t hi = b[1];
        uint16_t word = lo | (hi << 8);

        table_->item(r, 0)->setText(QString::asprintf("%04X", addr));
        table_->item(r, 1)->setText(QString::asprintf("%04X (%5d)", word, word));
        table_->item(r, 2)->setText(QString::asprintf("%02X (%3d)", hi, hi));
        table_->item(r, 3)->setText(QString::asprintf("%02X (%3d)", lo, lo));

        // Highlight SP row (top row = top of stack)
        bool is_sp = (offset == 0);
        QColor bg = is_sp ? QColor(0xE0, 0xFF, 0xE0) : QColor(Qt::white);
        for (int c = 0; c < 4; ++c) {
            table_->item(r, c)->setBackground(bg);
        }
    }
}
