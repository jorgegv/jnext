#include "debugger/callstack_panel.h"
#include "debug/debugger.h"
#include "debug/ram_page.h"
#include "debugger/memory_text.h"
#include "debug/symbol_table.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>

CallStackPanel::CallStackPanel(const jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    create_ui();
}

void CallStackPanel::create_ui() {
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(0, 7, this);
    table_->setFont(mono);
    // CAP-SRC — "Called from": the caller's source line, looked up on the page
    // the caller ran from, when a source map is loaded.
    table_->setHorizontalHeaderLabels({"#", "Type", "Caller", "Target", "Called from",
                                         "Caller memory", "Target memory"});   // GH #360
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setFont(mono);

    table_->setColumnWidth(0, 30);   // depth number
    table_->setColumnWidth(1, 50);   // type
    table_->setColumnWidth(2, 70);   // caller PC
    table_->setColumnWidth(3, 70);   // target PC
    table_->setColumnWidth(4, 130);  // called from
    table_->setColumnWidth(5, 120);  // caller memory (GH #360)
    table_->horizontalHeader()->setStretchLastSection(true);

    layout->addWidget(table_);

    // GH #435 - the user's choice of a row picks the C frame the Variables tab shows
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (!programmatic_ && row >= 0) emit call_selected(row);
    });
}

void CallStackPanel::select_row(int row) {
    programmatic_ = true;
    if (row >= 0 && row < table_->rowCount()) {
        table_->setCurrentCell(row, 0);
        table_->selectRow(row);
    } else {
        table_->clearSelection();
        table_->setCurrentCell(-1, -1);
    }
    programmatic_ = false;
}

void CallStackPanel::set_paused(bool paused) {
    paused_ = paused;
}

void CallStackPanel::refresh() {
    if (!dbg_ || !paused_) return;

    const auto& frames = dbg_->call_stack();   // INS-12
    int count = static_cast<int>(frames.size());

    // setRowCount() can move the current cell: not the user's choice (GH #435)
    struct Guard { bool& f; ~Guard() { f = false; } } guard{programmatic_};
    programmatic_ = true;
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
            // The memory the target was entered in when recorded; else the
            // one that supplies it now.
            const jnext::dbg::MemSpace space =
                f.target_space != jnext::dbg::MemSpace::cpu() ? f.target_space
                                                              : dbg_->source_page(f.target_pc);
            auto sym = symbol_table_->lookup(space, f.target_pc);
            if (sym)
                target_text = QString::fromStdString(*sym);
        }
        set_cell(3, target_text);

        // GH #360 - the memory each end ran in, as recorded when the frame was
        // pushed (a live page change since does not move it). An unrecorded
        // target shows the prediction, the rule its symbol lookup above uses.
        const jnext::dbg::MemSpace target_space =
            f.target_space != jnext::dbg::MemSpace::cpu() ? f.target_space
                                                          : dbg_->source_page(f.target_pc);

        QString source_text;
        if (const SourceLocation* src =
                f.caller_space == jnext::dbg::MemSpace::cpu()
                    ? nullptr
                    : dbg_->source_map().lookup(f.caller_space, f.caller_pc))
            source_text = QString::fromStdString(src->file) + ":" + QString::number(src->line);
        set_cell(4, source_text);
        set_cell(5, memtext::text(dbg_, style_, f.caller_space, f.caller_pc));
        set_cell(6, memtext::text(dbg_, style_, target_space, f.target_pc));

        table_->setRowHeight(i, 20);
    }
}
