#include "debugger/callstack_panel.h"
#include "core/emulator.h"
#include "debug/call_stack.h"
#include "debug/symbol_table.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>

CallStackPanel::CallStackPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

void CallStackPanel::create_ui() {
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(0, 4, this);
    table_->setFont(mono);
    table_->setHorizontalHeaderLabels({"#", "Type", "Caller", "Target"});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setFont(mono);

    table_->setColumnWidth(0, 30);   // depth number
    table_->setColumnWidth(1, 50);   // type
    table_->setColumnWidth(2, 70);   // caller PC
    table_->setColumnWidth(3, 70);   // target PC
    table_->horizontalHeader()->setStretchLastSection(true);

    layout->addWidget(table_);
}

void CallStackPanel::set_paused(bool paused) {
    paused_ = paused;
}

void CallStackPanel::refresh() {
    if (!emulator_ || !paused_) return;

    const auto& frames = emulator_->call_stack().frames();
    int count = static_cast<int>(frames.size());

    table_->setRowCount(count);

    // Display most recent first (top of table = most recent call)
    for (int i = 0; i < count; ++i) {
        int frame_idx = count - 1 - i;  // reverse order
        const auto& f = frames[frame_idx];

        auto set_cell = [&](int col, const QString& text) {
            auto* item = table_->item(i, col);
            if (!item) {
                item = new QTableWidgetItem();
                item->setTextAlignment(Qt::AlignCenter);
                table_->setItem(i, col, item);
            }
            item->setText(text);
        };

        set_cell(0, QString::number(frame_idx));

        const char* type_str = "?";
        switch (f.type) {
            case CallType::CALL: type_str = "CALL"; break;
            case CallType::RST:  type_str = "RST";  break;
            case CallType::INT:  type_str = "INT";  break;
            case CallType::NMI:  type_str = "NMI";  break;
        }
        set_cell(1, type_str);
        set_cell(2, QString::asprintf("%04X", f.caller_pc));

        // Show symbol name for target if available
        QString target_text = QString::asprintf("%04X", f.target_pc);
        if (symbol_table_) {
            auto sym = symbol_table_->lookup(f.target_pc);
            if (sym)
                target_text = QString::fromStdString(*sym);
        }
        set_cell(3, target_text);

        table_->setRowHeight(i, 20);
    }
}
