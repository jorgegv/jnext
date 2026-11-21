#include "debugger/nextreg_panel.h"
#include "debug/debugger.h"
#include "debug/nextreg_names.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QString>

// ---------------------------------------------------------------------------
// Static register name table: debug/nextreg_names.h
// ---------------------------------------------------------------------------

const char* NextRegPanel::reg_name(uint8_t reg) {
    return jnext::dbg::nextreg_name(reg);
}

// ---------------------------------------------------------------------------
// NextRegPanel
// ---------------------------------------------------------------------------

NextRegPanel::NextRegPanel(jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    create_ui();
}

void NextRegPanel::create_ui() {
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(256, 4, this);
    table_->setHorizontalHeaderLabels({"Reg", "Name", "Hex", "Binary"});
    table_->setFont(mono);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(18);

    table_->setColumnWidth(0, 40);   // Reg
    table_->setColumnWidth(1, 130);  // Name
    table_->setColumnWidth(2, 40);   // Hex
    table_->setColumnWidth(3, 75);   // Binary

    // Populate static columns (Reg, Name) and create value cells
    for (int i = 0; i < 256; ++i) {
        // Reg column (read-only)
        auto* reg_item = new QTableWidgetItem(QString::asprintf("%02X", i));
        reg_item->setTextAlignment(Qt::AlignCenter);
        reg_item->setFlags(reg_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(i, 0, reg_item);

        // Name column (read-only)
        auto* name_item = new QTableWidgetItem(reg_name(static_cast<uint8_t>(i)));
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(i, 1, name_item);

        // Hex value column (editable)
        auto* hex_item = new QTableWidgetItem("00");
        hex_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, 2, hex_item);

        // Binary column (read-only display)
        auto* bin_item = new QTableWidgetItem("00000000");
        bin_item->setTextAlignment(Qt::AlignCenter);
        bin_item->setFlags(bin_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(i, 3, bin_item);
    }

    // Connect cell edit to write NextREG
    connect(table_, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (!dbg_ || column != 2) return;

        auto* item = table_->item(row, column);
        if (!item) return;

        bool ok = false;
        uint8_t val = static_cast<uint8_t>(item->text().toUInt(&ok, 16));
        if (!ok) return;

        // INS-04 — THE Z80's write path: the register's handler runs, and the
        // backend logs the MUTATE line naming this client. Refused only while
        // an RZX records or plays (an edit would diverge the recording); then,
        // as for a non-hex edit, nothing is written and the next refresh puts
        // the register's own value back in the cell.
        dbg_->nextreg_write(client_, static_cast<uint8_t>(row), val);
    });

    layout->addWidget(table_);
}

void NextRegPanel::refresh() {
    if (!dbg_ || !table_) return;

    // Block signals during bulk update to avoid triggering cellChanged
    table_->blockSignals(true);

    for (int i = 0; i < 256; ++i) {
        // peek(), not cached() and NOT read().
        //
        // Not cached(): that is regs_[i] — the last byte written to that NextREG number
        // and nothing more. Many registers are not owned by their cache; their live
        // value is composed by a read handler from the subsystem that actually holds
        // the state (NR 0x69 bit 7 is the Layer 2 enable FF, which port 0x123B also
        // latches; NR 0x15 recomposes priority and sprite_en from the renderer; NR 0x6B
        // is the live tilemap control...). This panel showed the stale cache for every
        // one of them — with beast.nex it displayed NR 0x69 = 0x00 while the true value
        // was 0xC0 (Task 40).
        //
        // But NOT read() either, which is the *Z80's* read: for NR 0x2C / 0x2E the VHDL
        // read latches the Pi-I2S low bits into the NR 0x2D shadow (zxnext.vhd:6594-6603),
        // and this loop sweeps all 256 registers several times a second while the guest
        // is running — it would latch L, then latch R over it, and hand the Z80 a sample
        // the debugger invented. read() also emits a trace line per register, i.e. ~1000
        // phantom NextREG reads a second into the log used to diagnose NextREG traffic.
        //
        // peek() is the same value with neither consequence — the backend's
        // nextreg_peek() (INS-04) is exactly that NextReg::peek().
        uint8_t val = dbg_->nextreg_peek(static_cast<uint8_t>(i));

        // Hex column
        table_->item(i, 2)->setText(QString::asprintf("%02X", val));

        // Binary column
        QString bin;
        for (int b = 7; b >= 0; --b)
            bin += (val & (1 << b)) ? '1' : '0';
        table_->item(i, 3)->setText(bin);
    }

    table_->blockSignals(false);
}
