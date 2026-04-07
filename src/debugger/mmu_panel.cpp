#include "debugger/mmu_panel.h"
#include "core/emulator.h"
#include "memory/mmu.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>

MmuPanel::MmuPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

void MmuPanel::create_ui() {
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    QFont mono_bold = mono;
    mono_bold.setBold(true);
    QFont mono_small("Monospace", 9);
    mono_small.setStyleHint(QFont::Monospace);

    auto* top_layout = new QVBoxLayout(this);
    top_layout->setSpacing(2);
    top_layout->setContentsMargins(4, 4, 4, 4);

    auto make_hsep = [&]() -> QFrame* {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color: #D0D0D0;");
        return line;
    };

    // --- Next MMU Mappings ---
    auto* mmu_title = new QLabel("Next MMU Mappings", this);
    mmu_title->setFont(mono_bold);
    mmu_title->setAlignment(Qt::AlignCenter);
    top_layout->addWidget(mmu_title);

    auto* mmu_grid = new QGridLayout();
    mmu_grid->setSpacing(1);
    mmu_grid->setContentsMargins(0, 0, 0, 0);

    const char* mmu_hdrs[] = {"Slot", "Page", "Type"};
    for (int c = 0; c < 3; ++c) {
        auto* lbl = new QLabel(mmu_hdrs[c], this);
        lbl->setFont(mono_bold);
        lbl->setAlignment(Qt::AlignCenter);
        mmu_grid->addWidget(lbl, 0, c);
    }

    for (int s = 0; s < 8; ++s) {
        auto* slot_lbl = new QLabel(QString::number(s), this);
        slot_lbl->setFont(mono_small);
        slot_lbl->setAlignment(Qt::AlignCenter);
        mmu_grid->addWidget(slot_lbl, s + 1, 0);

        slot_page_[s] = new QLabel("--", this);
        slot_page_[s]->setFont(mono_small);
        slot_page_[s]->setAlignment(Qt::AlignCenter);
        mmu_grid->addWidget(slot_page_[s], s + 1, 1);

        slot_type_[s] = new QLabel("RAM", this);
        slot_type_[s]->setFont(mono_small);
        slot_type_[s]->setAlignment(Qt::AlignCenter);
        mmu_grid->addWidget(slot_type_[s], s + 1, 2);
    }

    top_layout->addLayout(mmu_grid);
    top_layout->addWidget(make_hsep());

    // --- 128K Bank Mappings ---
    auto* bank_title = new QLabel("128K Bank Mappings", this);
    bank_title->setFont(mono_bold);
    bank_title->setAlignment(Qt::AlignCenter);
    top_layout->addWidget(bank_title);

    auto* bank_grid = new QGridLayout();
    bank_grid->setSpacing(1);

    auto add_bank_row = [&](int row, const char* label, QLabel*& value) {
        auto* name = new QLabel(label, this);
        name->setFont(mono_small);
        name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bank_grid->addWidget(name, row, 0);

        value = new QLabel("--", this);
        value->setFont(mono_small);
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        bank_grid->addWidget(value, row, 1);
    };

    add_bank_row(0, "Bank:", bank_128k_);
    add_bank_row(1, "ROM:", rom_select_);
    add_bank_row(2, "Lock:", paging_locked_);

    top_layout->addLayout(bank_grid);
    top_layout->addStretch();

    setMinimumWidth(200);
    setMaximumWidth(260);
}

void MmuPanel::refresh() {
    if (!emulator_) return;

    auto& mmu = emulator_->mmu();

    for (int s = 0; s < 8; ++s) {
        uint8_t page = mmu.get_page(s);
        bool is_rom = mmu.is_slot_rom(s);

        slot_page_[s]->setText(QString::asprintf("%02X", page));

        if (is_rom) {
            slot_type_[s]->setText("ROM");
            slot_type_[s]->setStyleSheet("QLabel { color: #CC6600; font-weight: bold; }");
        } else {
            slot_type_[s]->setText(QString("B%1").arg(page / 2));
            slot_type_[s]->setStyleSheet("");
        }
    }

    uint8_t p7ffd = mmu.port_7ffd();
    bank_128k_->setText(QString::number(p7ffd & 0x07));
    rom_select_->setText(QString::number((p7ffd >> 4) & 1));
    bool locked = (p7ffd >> 5) & 1;
    paging_locked_->setText(locked ? "Yes" : "No");
    paging_locked_->setStyleSheet(locked
        ? "QLabel { color: #CC0000; font-weight: bold; }" : "");
}
