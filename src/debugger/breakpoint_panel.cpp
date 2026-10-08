#include "debugger/breakpoint_panel.h"
#include "debug/debugger.h"
#include "debug/symbol_table.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>

// The table's columns, named once (GH #225 inserted one at the front).
static constexpr int COL_ENABLED = 0;
static constexpr int COL_TYPE    = 1;
static constexpr int COL_ADDR    = 2;
static constexpr int COL_SYMBOL  = 3;

BreakpointPanel::BreakpointPanel(QWidget* parent)
    : QWidget(parent)
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

    // GH #225 — the MASTER SWITCH. It deletes nothing and clears no
    // per-breakpoint flag; it decides whether any breakpoint can fire at all
    // (the backend's master switch, one for every client), so unchecking and
    // re-checking it restores exactly the set that was there. Right-aligned,
    // away from the three
    // destructive buttons it must not be mistaken for.
    master_check_ = new QCheckBox(tr("Breakpoints enabled"), this);
    master_check_->setChecked(true);   // the backend's default; set_model() reads it
    master_check_->setToolTip(
        tr("Master switch. Unchecking suspends every breakpoint and watchpoint "
           "without deleting any; re-checking restores each one's own Enabled "
           "state. Step Over, Step Out and Run to Here keep working."));
    connect(master_check_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_ || !model_) return;
        model_->set_master_enabled(on);
    });
    btn_row->addWidget(master_check_);

    layout->addLayout(btn_row);

    // Table. Enabled first: it is a control, not data, and every debugger that
    // has one puts it in the leading column.
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels(
        {tr("On"), tr("Type"), tr("Address"), tr("Symbol / Source")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setColumnWidth(0, 32);
    table_->setColumnWidth(1, 90);
    table_->setColumnWidth(2, 95);   // room for a page qualifier: "$8000 @12"

    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);

    // Double-click to edit — but NOT on the Enabled column, where a
    // double-click is two toggles and must not also pop the Edit dialog.
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int, int col) {
        if (col == COL_ENABLED) return;
        on_edit();
    });

    // GH #225 — the per-breakpoint checkbox. NoEditTriggers does not disable a
    // user-checkable item's indicator, so this fires on a plain click.
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_ || !item || item->column() != COL_ENABLED) return;
        apply_enabled_cell(item->row(), item->checkState() == Qt::Checked);
    });

    layout->addWidget(table_, 1);
}

void BreakpointPanel::set_model(BreakpointModel* model)
{
    if (model_) disconnect(model_, nullptr, this, nullptr);
    model_ = model;
    // GH #220 — the list is driven by the model, not by whoever mutated it.
    // Every kind matters here: this table holds Execute AND data breakpoints.
    // Qt drops the connection when either side is destroyed, which is what
    // BreakpointSet's remove_observer() in the destructor used to do by hand.
    if (model_)
        connect(model_, &BreakpointModel::changed, this, [this](uint32_t) { refresh(); });
    refresh();
}

void BreakpointPanel::rebuild_entries()
{
    // THE MODEL (GH #225): it carries the disabled breakpoints too, each with
    // its own flag, which is the whole point — a disabled breakpoint stays in
    // this list. Already sorted by address (BreakpointModel::rows()).
    entries_.clear();
    if (model_) entries_ = model_->rows();
}

void BreakpointPanel::refresh()
{
    // GH #225 — RE-ENTRANCY GUARD, and it is not merely tidiness.
    //
    // apply_enabled_cell() raises `updating_` across its write to the model,
    // because that write notifies and the notification lands back here — while
    // Qt is still emitting itemChanged for the very QTableWidgetItem that the
    // rebuild below would delete. Suppressing the rebuild removes that
    // lifetime hazard, and costs nothing: the widget is already showing the
    // state the user just clicked. The OTHER subscriber (the disassembly
    // gutter) is unaffected — the suppression is this panel's, not the model's.
    if (updating_) return;

    rebuild_entries();

    // Everything below writes widgets FROM the model. The same flag is what
    // stops itemChanged() and the master checkbox's toggled() from writing
    // those values straight back into it.
    updating_ = true;

    if (master_check_)
        master_check_->setChecked(model_ ? model_->master_enabled() : true);

    table_->setRowCount(static_cast<int>(entries_.size()));

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];

        // GH #225 — this breakpoint's OWN flag, not whether it can currently
        // fire. The master switch has its own control; echoing it into every
        // row would erase the state a user has to get back when they flip it.
        //
        // GH #278 WP4c (REQ-qt-13d) — another client's row shows its flag but
        // cannot be ticked: it is not this GUI's to change.
        auto* en_item = new QTableWidgetItem();
        en_item->setFlags(e.own ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                   Qt::ItemIsUserCheckable)
                                : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
        en_item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
        table_->setItem(i, COL_ENABLED, en_item);

        auto* type_item = new QTableWidgetItem(e.type_text);
        table_->setItem(i, COL_TYPE, type_item);

        auto* addr_item = new QTableWidgetItem(e.addr_text);
        table_->setItem(i, COL_ADDR, addr_item);

        QString sym;
        if (symbol_table_) {
            auto s = symbol_table_->lookup(e.addr);
            if (s) sym = QString::fromStdString(*s);
        }
        // CAP-SRC — with no symbol, the source line an Execute breakpoint
        // sits on: on its own page, or on the page mapped there now.
        if (sym.isEmpty() && dbg_ && e.type == BreakpointModel::Execute &&
            !dbg_->source_map().empty()) {
            const uint8_t page = e.page != jnext::dbg::PAGE_ANY
                                     ? static_cast<uint8_t>(e.page)
                                     : dbg_->source_page(e.addr);
            if (const auto src = dbg_->source_map().lookup(page, e.addr))
                sym = QString::fromStdString(src->file) + ":" + QString::number(src->line);
        }
        auto* sym_item = new QTableWidgetItem(sym);
        table_->setItem(i, COL_SYMBOL, sym_item);
    }

    updating_ = false;
}

// GH #225 — a click on a row's Enabled checkbox.
void BreakpointPanel::apply_enabled_cell(int row, bool enabled)
{
    if (row < 0 || row >= static_cast<int>(entries_.size()) || !model_) return;

    // Only this GUI's own rows are editable (REQ-qt-13d); another client's is
    // left alone, and its cell is not user-checkable in the first place.
    const auto e = entries_[row];
    if (!e.own) return;

    // Raised across the write: see refresh()'s head for why the rebuild this
    // notification would otherwise trigger must not happen from inside the
    // itemChanged signal that got us here.
    updating_ = true;
    model_->set_enabled(e.type, e.addr, enabled, e.page);
    updating_ = false;

    // ... and because there was no rebuild, entries_ is kept truthful here.
    // on_edit() reads this flag to carry it across an address change.
    entries_[row].enabled = enabled;
}

bool BreakpointPanel::show_bp_dialog(const QString& title, uint16_t& addr, uint16_t& page,
                                     int& type_index)
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
    type_combo->addItem(tr("IO Read"));
    type_combo->addItem(tr("IO Write"));
    type_combo->setCurrentIndex(type_index);
    form->addRow(tr("Type:"), type_combo);

    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText("e.g. 4000, $4000, symbol or file.bas:12");
    // A breakpoint on a source line edits as that line.
    QString initial = QString::asprintf("%04X", addr);
    if (page != jnext::dbg::PAGE_ANY && dbg_) {
        if (const auto src = dbg_->source_map().lookup(static_cast<uint8_t>(page), addr))
            initial = QString::fromStdString(src->file) + ":" + QString::number(src->line);
    }
    addr_edit->setText(initial);
    form->addRow(tr("Address, symbol or file:line:"), addr_edit);

    // The one thing a user cannot guess about the two IO types: what the
    // address means. Ports are decoded by address-line masking, so 00-FF is a
    // low-byte match and anything above is an exact 16-bit port (GH #222).
    form->addRow(QString(), new QLabel(
        tr("IO Read/Write: 00-FF matches any port with that low byte\n"
           "(FE = the ULA); 0100 and up matches that exact port (243B)."), &dlg));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    const std::string text = addr_edit->text().trimmed().toStdString();
    type_index = type_combo->currentIndex();
    // A symbol or a number first; then, for an Execute breakpoint, a source
    // line — which keeps the page its record names, so a line of banked code
    // breaks only while that bank is mapped.
    if (const auto resolved = symbol_table_ ? symbol_table_->resolve(text)
                                            : SymbolTable().resolve(text)) {
        addr = *resolved;
        page = jnext::dbg::PAGE_ANY;
        return true;
    }
    if (type_index == BreakpointModel::Execute && dbg_) {
        if (const auto src = dbg_->source_map().resolve(text)) {
            addr = src->address;
            page = src->page ? *src->page : jnext::dbg::PAGE_ANY;
            return true;
        }
    }
    return false;
}

void BreakpointPanel::on_add()
{
    if (!model_) return;
    uint16_t addr = 0;
    uint16_t page = jnext::dbg::PAGE_ANY;
    int type_index = 0;
    if (!show_bp_dialog(tr("Add Breakpoint"), addr, page, type_index))
        return;

    // No repaint call here: the mutation notified, and it notified the RIGHT
    // views — the gutter only for an Execute breakpoint, because a data
    // breakpoint changes nothing the gutter draws.
    model_->add(type_index, addr, page);
}

void BreakpointPanel::on_edit()
{
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entries_.size()) || !model_) return;

    auto old = entries_[row];
    if (!old.own) return;           // another client's: read-only (REQ-qt-13d)
    uint16_t addr = old.addr;
    uint16_t page = old.page;
    int type_index = old.type;

    if (!show_bp_dialog(tr("Edit Breakpoint"), addr, page, type_index))
        return;

    model_->remove(old.type, old.addr, old.page);

    // Add new, carrying the old one's Enabled state across (GH #225). An edit
    // moves a breakpoint; it does not create one, so a disabled breakpoint
    // whose address the user corrects must come back still disabled. add()
    // always creates enabled, hence the explicit re-apply.
    model_->add(type_index, addr, page);
    model_->set_enabled(type_index, addr, old.enabled, page);
    // Each of the mutations above notified; the last one left the table
    // showing the edited breakpoint. Nothing to repaint by hand.
}

void BreakpointPanel::on_remove()
{
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entries_.size()) || !model_) return;

    // A COPY, not a reference: the removal below notifies, refresh() rebuilds
    // entries_ from under us, and a reference into it would dangle (GH #220).
    const auto e = entries_[row];
    if (!e.own) return;             // another client's: read-only (REQ-qt-13d)
    model_->remove(e.type, e.addr, e.page);
    // As in on_add(): the mutation notified the views its kind concerns.
}
