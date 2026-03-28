#include "debugger/breakpoint_panel.h"
#include "debugger/disasm_panel.h"
#include "core/emulator.h"
#include "debug/debug_state.h"
#include "debug/symbol_table.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>

BreakpointPanel::BreakpointPanel(Emulator* emulator, QWidget* parent)
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
    connect(add_btn, &QPushButton::clicked, this, &BreakpointPanel::on_add);
    btn_row->addWidget(add_btn);

    auto* edit_btn = new QPushButton(tr("Edit"), this);
    connect(edit_btn, &QPushButton::clicked, this, &BreakpointPanel::on_edit);
    btn_row->addWidget(edit_btn);

    auto* remove_btn = new QPushButton(tr("Remove"), this);
    connect(remove_btn, &QPushButton::clicked, this, &BreakpointPanel::on_remove);
    btn_row->addWidget(remove_btn);

    btn_row->addStretch();
    layout->addLayout(btn_row);

    // Table
    table_ = new QTableWidget(0, 3, this);
    table_->setHorizontalHeaderLabels({tr("Type"), tr("Address"), tr("Symbol")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setColumnWidth(0, 90);
    table_->setColumnWidth(1, 70);

    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);

    // Double-click to edit
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        on_edit();
    });

    layout->addWidget(table_, 1);
}

QString BreakpointPanel::type_name(int type_index)
{
    switch (type_index) {
        case 0: return "Execute";
        case 1: return "Read";
        case 2: return "Write";
        case 3: return "Read/Write";
        default: return "?";
    }
}

void BreakpointPanel::rebuild_entries()
{
    entries_.clear();
    const auto& bps = emulator_->debug_state().breakpoints();

    // Execute (PC) breakpoints
    for (uint16_t addr : bps.pc_breakpoints()) {
        entries_.push_back({addr, 0});
    }

    // Data (watchpoint) breakpoints
    for (const auto& wp : bps.watchpoints()) {
        int ti = 0;
        switch (wp.type) {
            case WatchType::READ:       ti = 1; break;
            case WatchType::WRITE:      ti = 2; break;
            case WatchType::READ_WRITE: ti = 3; break;
            default: ti = 1; break;
        }
        entries_.push_back({wp.addr, ti});
    }

    // Sort by address
    std::sort(entries_.begin(), entries_.end(),
        [](const BpEntry& a, const BpEntry& b) { return a.addr < b.addr; });
}

void BreakpointPanel::refresh()
{
    rebuild_entries();

    table_->setRowCount(static_cast<int>(entries_.size()));

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];

        auto* type_item = new QTableWidgetItem(type_name(e.type_index));
        table_->setItem(i, 0, type_item);

        auto* addr_item = new QTableWidgetItem(QString::asprintf("$%04X", e.addr));
        table_->setItem(i, 1, addr_item);

        QString sym;
        if (symbol_table_) {
            auto s = symbol_table_->lookup(e.addr);
            if (s) sym = QString::fromStdString(*s);
        }
        auto* sym_item = new QTableWidgetItem(sym);
        table_->setItem(i, 2, sym_item);
    }
}

bool BreakpointPanel::show_bp_dialog(const QString& title, uint16_t& addr, int& type_index)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(400);

    auto* form = new QFormLayout(&dlg);

    auto* type_combo = new QComboBox(&dlg);
    type_combo->addItem(tr("Execute"));
    type_combo->addItem(tr("Read"));
    type_combo->addItem(tr("Write"));
    type_combo->addItem(tr("Read/Write"));
    type_combo->setCurrentIndex(type_index);
    form->addRow(tr("Type:"), type_combo);

    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText("e.g. 4000 or $4000");
    addr_edit->setText(QString::asprintf("%04X", addr));
    form->addRow(tr("Address (hex):"), addr_edit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    QString addr_text = addr_edit->text().trimmed();
    if (addr_text.startsWith('$')) addr_text = addr_text.mid(1);
    if (addr_text.startsWith("0x", Qt::CaseInsensitive)) addr_text = addr_text.mid(2);

    bool ok = false;
    addr = static_cast<uint16_t>(addr_text.toUInt(&ok, 16));
    if (!ok) return false;

    type_index = type_combo->currentIndex();
    return true;
}

void BreakpointPanel::on_add()
{
    uint16_t addr = 0;
    int type_index = 0;
    if (!show_bp_dialog(tr("Add Breakpoint"), addr, type_index))
        return;

    auto& bps = emulator_->debug_state().breakpoints();
    if (type_index == 0) {
        bps.add_pc(addr);
    } else {
        WatchType wt = WatchType::READ;
        if (type_index == 2) wt = WatchType::WRITE;
        if (type_index == 3) wt = WatchType::READ_WRITE;
        bps.add_watchpoint(addr, wt);
    }

    refresh();
    if (disasm_panel_) disasm_panel_->refresh();
}

void BreakpointPanel::on_edit()
{
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;

    auto old = entries_[row];
    uint16_t addr = old.addr;
    int type_index = old.type_index;

    if (!show_bp_dialog(tr("Edit Breakpoint"), addr, type_index))
        return;

    // Remove old
    auto& bps = emulator_->debug_state().breakpoints();
    if (old.type_index == 0) {
        bps.remove_pc(old.addr);
    } else {
        WatchType wt = WatchType::READ;
        if (old.type_index == 2) wt = WatchType::WRITE;
        if (old.type_index == 3) wt = WatchType::READ_WRITE;
        bps.remove_watchpoint(old.addr, wt);
    }

    // Add new
    if (type_index == 0) {
        bps.add_pc(addr);
    } else {
        WatchType wt = WatchType::READ;
        if (type_index == 2) wt = WatchType::WRITE;
        if (type_index == 3) wt = WatchType::READ_WRITE;
        bps.add_watchpoint(addr, wt);
    }

    refresh();
    if (disasm_panel_) disasm_panel_->refresh();
}

void BreakpointPanel::on_remove()
{
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;

    auto& e = entries_[row];
    auto& bps = emulator_->debug_state().breakpoints();

    if (e.type_index == 0) {
        bps.remove_pc(e.addr);
    } else {
        WatchType wt = WatchType::READ;
        if (e.type_index == 2) wt = WatchType::WRITE;
        if (e.type_index == 3) wt = WatchType::READ_WRITE;
        bps.remove_watchpoint(e.addr, wt);
    }

    refresh();
    if (disasm_panel_) disasm_panel_->refresh();
}
