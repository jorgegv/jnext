#include "debugger/stack_panel.h"
#include "core/emulator.h"
#include "cpu/z80_cpu.h"
#include "memory/mmu.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>

StackPanel::StackPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
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
    table_->setHorizontalHeaderLabels({"Address", "Word", "Hi", "Lo"});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setFont(mono);

    // Column widths
    table_->setColumnWidth(0, 60);   // Address
    table_->setColumnWidth(1, 110);  // Word hex+dec
    table_->setColumnWidth(2, 80);   // Hi byte
    table_->setColumnWidth(3, 80);   // Lo byte
    table_->horizontalHeader()->setStretchLastSection(true);

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
    if (!emulator_ || !paused_) return;

    auto regs = emulator_->cpu().get_registers();
    auto& mmu = emulator_->mmu();
    uint16_t sp = regs.SP;

    for (int r = 0; r < STACK_ROWS; ++r) {
        uint16_t addr = sp + r * 2;
        uint8_t lo = mmu.read(addr);
        uint8_t hi = mmu.read(addr + 1);
        uint16_t word = lo | (hi << 8);

        table_->item(r, 0)->setText(QString::asprintf("%04X", addr));
        table_->item(r, 1)->setText(QString::asprintf("%04X (%d)", word, word));
        table_->item(r, 2)->setText(QString::asprintf("%02X (%d)", hi, hi));
        table_->item(r, 3)->setText(QString::asprintf("%02X (%d)", lo, lo));

        // Highlight SP row
        QColor bg = (r == 0) ? QColor(0xE0, 0xFF, 0xE0) : QColor(Qt::white);
        for (int c = 0; c < 4; ++c) {
            table_->item(r, c)->setBackground(bg);
        }
    }
}
