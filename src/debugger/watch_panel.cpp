#include "debugger/watch_panel.h"
#include "core/emulator.h"
#include "memory/mmu.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QString>

WatchPanel::WatchPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // Button row
    auto* btn_row = new QHBoxLayout();
    btn_row->setSpacing(4);

    auto* add_btn = new QPushButton(tr("Add"), this);
    connect(add_btn, &QPushButton::clicked, this, &WatchPanel::on_add_watch);
    btn_row->addWidget(add_btn);

    auto* remove_btn = new QPushButton(tr("Remove"), this);
    connect(remove_btn, &QPushButton::clicked, this, &WatchPanel::remove_selected);
    btn_row->addWidget(remove_btn);

    btn_row->addStretch();
    layout->addLayout(btn_row);

    // Table
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({tr("Address"), tr("Label"), tr("Type"), tr("Value")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);

    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);

    layout->addWidget(table_, 1);
}

void WatchPanel::refresh() {
    if (!emulator_) return;

    for (int i = 0; i < static_cast<int>(watches_.size()); ++i) {
        const auto& w = watches_[i];
        QString value_str;

        switch (w.type) {
            case BYTE: {
                uint8_t val = emulator_->mmu().read(w.addr);
                value_str = QString::asprintf("$%02X", val);
                break;
            }
            case WORD: {
                uint8_t lo = emulator_->mmu().read(w.addr);
                uint8_t hi = emulator_->mmu().read(static_cast<uint16_t>(w.addr + 1));
                uint16_t val = static_cast<uint16_t>(lo | (hi << 8));
                value_str = QString::asprintf("$%04X", val);
                break;
            }
            case LONG: {
                uint32_t val = 0;
                for (int b = 0; b < 4; ++b) {
                    uint8_t byte = emulator_->mmu().read(static_cast<uint16_t>(w.addr + b));
                    val |= static_cast<uint32_t>(byte) << (b * 8);
                }
                value_str = QString::asprintf("$%08X", val);
                break;
            }
        }

        if (i < table_->rowCount()) {
            auto* item = table_->item(i, 3);
            if (item) item->setText(value_str);
        }
    }
}

void WatchPanel::add_watch(uint16_t addr, const std::string& label, int type) {
    WatchEntry entry;
    entry.addr = addr;
    entry.label = label;
    entry.type = static_cast<WatchType>(type);
    watches_.push_back(entry);
    update_table();
}

void WatchPanel::remove_selected() {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(watches_.size())) return;
    watches_.erase(watches_.begin() + row);
    update_table();
}

void WatchPanel::on_add_watch() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Watch"));

    auto* form = new QFormLayout(&dlg);

    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText("e.g. 4000 or $4000");
    form->addRow(tr("Address (hex):"), addr_edit);

    auto* label_edit = new QLineEdit(&dlg);
    label_edit->setPlaceholderText("(optional)");
    form->addRow(tr("Label:"), label_edit);

    auto* type_combo = new QComboBox(&dlg);
    type_combo->addItem(tr("Byte"));
    type_combo->addItem(tr("Word"));
    type_combo->addItem(tr("Long"));
    form->addRow(tr("Type:"), type_combo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QString addr_text = addr_edit->text().trimmed();
    if (addr_text.startsWith('$')) addr_text = addr_text.mid(1);
    if (addr_text.startsWith("0x", Qt::CaseInsensitive)) addr_text = addr_text.mid(2);

    bool ok = false;
    uint16_t addr = static_cast<uint16_t>(addr_text.toUInt(&ok, 16));
    if (!ok) return;

    std::string label = label_edit->text().trimmed().toStdString();
    int type = type_combo->currentIndex();

    add_watch(addr, label, type);
}

void WatchPanel::update_table() {
    table_->setRowCount(static_cast<int>(watches_.size()));

    static const char* type_names[] = {"Byte", "Word", "Long"};

    for (int i = 0; i < static_cast<int>(watches_.size()); ++i) {
        const auto& w = watches_[i];

        auto* addr_item = new QTableWidgetItem(QString::asprintf("$%04X", w.addr));
        table_->setItem(i, 0, addr_item);

        auto* label_item = new QTableWidgetItem(QString::fromStdString(w.label));
        table_->setItem(i, 1, label_item);

        auto* type_item = new QTableWidgetItem(type_names[w.type]);
        table_->setItem(i, 2, type_item);

        auto* value_item = new QTableWidgetItem("--");
        table_->setItem(i, 3, value_item);
    }

    refresh();
}
