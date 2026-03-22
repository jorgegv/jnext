#include "debugger/copper_panel.h"
#include "core/emulator.h"
#include "peripheral/copper.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QString>

static constexpr int DISPLAY_ROWS = 64;

CopperPanel::CopperPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

void CopperPanel::create_ui() {
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    QFont mono_bold = mono;
    mono_bold.setBold(true);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(4);
    main_layout->setContentsMargins(4, 4, 4, 4);

    // Top row: enable checkbox + PC label
    auto* top_layout = new QHBoxLayout();

    enable_check_ = new QCheckBox(tr("Copper Running"), this);
    enable_check_->setEnabled(false); // read-only for now
    top_layout->addWidget(enable_check_);

    top_layout->addStretch();

    pc_label_ = new QLabel("PC: 0000", this);
    pc_label_->setFont(mono_bold);
    top_layout->addWidget(pc_label_);

    main_layout->addLayout(top_layout);

    // Instruction table
    table_ = new QTableWidget(DISPLAY_ROWS, 4, this);
    table_->setHorizontalHeaderLabels({"Addr", "Raw", "Type", "Details"});
    table_->setFont(mono);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(20);

    table_->setColumnWidth(0, 50);   // Addr
    table_->setColumnWidth(1, 50);   // Raw
    table_->setColumnWidth(2, 50);   // Type
    table_->setColumnWidth(3, 120);  // Details

    // Pre-create cells
    for (int r = 0; r < DISPLAY_ROWS; ++r) {
        for (int c = 0; c < 4; ++c) {
            auto* item = new QTableWidgetItem("");
            item->setTextAlignment(Qt::AlignCenter);
            table_->setItem(r, c, item);
        }
    }

    main_layout->addWidget(table_);
}

/// Decode a 16-bit copper instruction.
static void decode_copper_instr(uint16_t instr, QString& type, QString& details) {
    if (instr == 0x0000) {
        type = "NOP";
        details = "";
    } else if (instr == 0xFFFF) {
        type = "HALT";
        details = "";
    } else if (instr & 0x8000) {
        // WAIT: bit 15 = 1, bits 14:9 = hpos (6 bits), bits 8:0 = vpos (9 bits)
        int hpos = (instr >> 9) & 0x3F;
        int vpos = instr & 0x01FF;
        type = "WAIT";
        details = QString::asprintf("v=%d, h=%d", vpos, hpos);
    } else {
        // MOVE: bit 15 = 0, bits 14:8 = nextreg (7 bits), bits 7:0 = value
        int reg = (instr >> 8) & 0x7F;
        int val = instr & 0xFF;
        type = "MOVE";
        details = QString::asprintf("NR %02X = %02X", reg, val);
    }
}

void CopperPanel::refresh() {
    if (!emulator_ || !table_) return;

    auto& copper = emulator_->copper();

    // Update running state
    enable_check_->setChecked(copper.is_running());

    // Update PC display
    uint16_t pc = copper.pc();
    pc_label_->setText(QString::asprintf("PC: %03X  Mode: %d", pc, copper.mode()));

    // Show DISPLAY_ROWS instructions centered around PC
    int start = static_cast<int>(pc) - DISPLAY_ROWS / 2;
    if (start < 0) start = 0;
    if (start + DISPLAY_ROWS > 1024) start = 1024 - DISPLAY_ROWS;

    for (int r = 0; r < DISPLAY_ROWS; ++r) {
        int addr = start + r;
        uint16_t instr = copper.instruction(static_cast<uint16_t>(addr));

        QString type, details;
        decode_copper_instr(instr, type, details);

        table_->item(r, 0)->setText(QString::asprintf("%03X", addr));
        table_->item(r, 1)->setText(QString::asprintf("%04X", instr));
        table_->item(r, 2)->setText(type);
        table_->item(r, 3)->setText(details);

        // Highlight current PC row
        QColor bg = (static_cast<uint16_t>(addr) == pc)
            ? QColor(255, 255, 160)   // yellow highlight
            : (r % 2 == 0 ? QColor(255, 255, 255) : QColor(245, 245, 245));

        for (int c = 0; c < 4; ++c) {
            table_->item(r, c)->setBackground(bg);
        }
    }
}
