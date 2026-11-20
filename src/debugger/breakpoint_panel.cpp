#include "debugger/breakpoint_panel.h"
#include "debug/debugger.h"
#include "debug/ram_page.h"
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
static constexpr int COL_HITS    = 4;   // GH #372: appended, columns 0-3 do not move
static constexpr int COL_COND    = 5;

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

    // GH #372 - zero every own row's hit count.
    auto* reset_btn = new QPushButton(tr("Reset Hits"), this);
    reset_btn->setToolTip(tr("Zero the Hits count of every breakpoint of this window."));
    connect(reset_btn, &QPushButton::clicked, this, [this] {
        if (model_) model_->reset_hits();
    });
    btn_row->addWidget(reset_btn);

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
    table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels(
        {tr("On"), tr("Type"), tr("Address"), tr("Symbol / Source"), tr("Hits"), tr("Condition")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);   // the Condition column
    table_->verticalHeader()->setVisible(false);
    table_->setColumnWidth(0, 32);
    table_->setColumnWidth(1, 90);
    table_->setColumnWidth(2, 95);   // room for a page qualifier: "$8000 @12"
    table_->setColumnWidth(COL_SYMBOL, 140);
    table_->setColumnWidth(COL_HITS, 64);

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
    if (model_) {
        connect(model_, &BreakpointModel::changed, this, [this](uint32_t) { refresh(); });
        // GH #372 - a count moved: the Hits cells only, not a rebuild.
        connect(model_, &BreakpointModel::hits_changed, this, [this] { refresh_hits(); });
    }
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

        // The breakpoint's own space, else the one mapped there now: the memory
        // the source line below is looked up in.
        const jnext::dbg::MemSpace space =
            e.space != jnext::dbg::MemSpace::cpu() ? e.space
            : dbg_                                 ? dbg_->source_page(e.addr)
                                                   : jnext::dbg::MemSpace::cpu();
        QString sym;
        if (symbol_table_) {
            auto s = symbol_table_->lookup(space, e.addr);
            if (s) sym = QString::fromStdString(*s);
        }
        // CAP-SRC — with no symbol, the source line an Execute breakpoint
        // sits on: on its own page, or on the page mapped there now.
        if (sym.isEmpty() && dbg_ && e.type == BreakpointModel::Execute &&
            !dbg_->source_map().empty()) {
            if (const SourceLocation* src = dbg_->source_map().lookup(space, e.addr))
                sym = QString::fromStdString(src->file) + ":" + QString::number(src->line);
        }
        // A source breakpoint armed at several places is one row (E23-09).
        if (e.locations > 1) sym += tr(" (%n locations)", nullptr, e.locations);
        auto* sym_item = new QTableWidgetItem(sym);
        table_->setItem(i, COL_SYMBOL, sym_item);

        table_->setItem(i, COL_HITS, new QTableWidgetItem());

        // GH #372 - this GUI's condition, with the last run-time error as its tooltip; another client's
        // conditional breakpoint is only marked (its text is not ours to show).
        auto* cond_item = new QTableWidgetItem(e.own ? e.condition : e.conditional ? tr("(condition)") : QString());
        table_->setItem(i, COL_COND, cond_item);
    }

    updating_ = false;
    refresh_hits();
}

void BreakpointPanel::refresh_hits()
{
    if (!model_) return;
    const bool was = updating_;
    updating_ = true;
    for (int i = 0; i < static_cast<int>(entries_.size()) && i < table_->rowCount(); ++i) {
        QTableWidgetItem* it = table_->item(i, COL_HITS);
        if (!it) continue;
        it->setText(entries_[i].own ? QString::number(model_->hits(entries_[i])) : QString());
        it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // The condition's last run-time error rides on the same signal.
        if (QTableWidgetItem* c = table_->item(i, COL_COND)) {
            const QString err = model_->condition_error(entries_[i]);
            c->setToolTip(err.isEmpty() ? QString() : tr("Last run-time error: %1").arg(err));
        }
    }
    updating_ = was;
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
    model_->set_enabled(e, enabled);
    updating_ = false;

    // ... and because there was no rebuild, entries_ is kept truthful here.
    // on_edit() reads this flag to carry it across an address change.
    entries_[row].enabled = enabled;
}

bool BreakpointPanel::show_bp_dialog(const QString& title, uint16_t addr,
                                     jnext::dbg::MemSpace space, int& type_index,
                                     BpTarget& target)
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
    type_combo->addItem(tr("Interrupt"));
    type_combo->addItem(tr("NMI"));
    type_combo->setCurrentIndex(type_index);
    form->addRow(tr("Type:"), type_combo);

    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setObjectName(QStringLiteral("bp_dialog_address"));
    addr_edit->setPlaceholderText("e.g. 4000, $4000, symbol or file.bas:12");
    // A breakpoint on a source line edits as that line.
    QString initial = QString::asprintf("%04X", addr);
    if (space != jnext::dbg::MemSpace::cpu() && dbg_) {
        if (const SourceLocation* src = dbg_->source_map().lookup(space, addr))
            initial = QString::fromStdString(src->file) + ":" + QString::number(src->line);
    }
    addr_edit->setText(initial);
    form->addRow(tr("Address, symbol or file:line:"), addr_edit);

    // GH #372 - a range end, an IO port mask and a condition, each optional.
    auto* to_edit = new QLineEdit(&dlg);
    to_edit->setObjectName(QStringLiteral("bp_dialog_to"));
    to_edit->setPlaceholderText(tr("optional: end of the range"));
    if (target.hi) to_edit->setText(QString::asprintf("%04X", *target.hi));
    form->addRow(tr("To (optional):"), to_edit);

    auto* mask_edit = new QLineEdit(&dlg);
    mask_edit->setObjectName(QStringLiteral("bp_dialog_mask"));
    mask_edit->setPlaceholderText(tr("optional, hex: empty = the address rule"));
    if (target.mask) mask_edit->setText(QString::asprintf("%04X", *target.mask));
    form->addRow(tr("Mask (optional):"), mask_edit);

    // The one thing a user cannot guess about the two IO types: what the
    // address means. Ports are decoded by address-line masking, so 00-FF is a
    // low-byte match and anything above is an exact 16-bit port (GH #222).
    form->addRow(QString(), new QLabel(
        tr("IO Read/Write: 00-FF matches any port with that low byte\n"
           "(FE = the ULA); 0100 and up matches that exact port (243B)."), &dlg));

    // Interrupt and NMI have no address: the field is off for them. A range is for memory types, a mask
    // for the two IO types.
    const auto sync_fields = [type_combo, addr_edit, to_edit, mask_edit] {
        const int t = type_combo->currentIndex();
        addr_edit->setEnabled(t != BreakpointModel::Interrupt && t != BreakpointModel::Nmi);
        to_edit->setEnabled(t <= BreakpointModel::ReadWrite);
        mask_edit->setEnabled(t == BreakpointModel::IoRead || t == BreakpointModel::IoWrite);
    };
    connect(type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, sync_fields);
    sync_fields();

    auto* cond_edit = new QLineEdit(&dlg);
    cond_edit->setObjectName(QStringLiteral("bp_dialog_condition"));
    cond_edit->setPlaceholderText(tr("optional, e.g. A == 5 and mem[0x9000] != 0"));
    cond_edit->setText(target.condition);
    form->addRow(tr("Condition (optional):"), cond_edit);

    // GH #372 - count the hits, never stop.
    auto* count_only = new QCheckBox(tr("Count hits only (don't stop)"), &dlg);
    count_only->setObjectName(QStringLiteral("bp_dialog_count_only"));
    count_only->setChecked(target.count_only);
    form->addRow(QString(), count_only);

    // Why OK did not close the dialog. Hidden until there is a reason.
    auto* error = new QLabel(&dlg);
    error->setObjectName(QStringLiteral("bp_dialog_error"));
    error->setWordWrap(true);
    error->setStyleSheet(QStringLiteral("color: #c00000;"));
    error->hide();
    form->addRow(error);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    // OK on text that does not resolve for the chosen type shows the dialog
    // again with the reason; it never returns as if cancelled, so an edit is
    // never silently dropped. Only Cancel gives up.
    while (dlg.exec() == QDialog::Accepted) {
        const std::string text = addr_edit->text().trimmed().toStdString();
        const int type = type_combo->currentIndex();
        // The optional fields, once the address is known. Returns why not ("" = fine) and fills `t`.
        const auto finish = [&](BpTarget& t) -> QString {
            t.count_only = count_only->isChecked();
            t.condition  = cond_edit->text().trimmed();
            const QString to_text   = to_edit->text().trimmed();
            const QString mask_text = mask_edit->text().trimmed();
            if (to_edit->isEnabled() && !to_text.isEmpty()) {
                if (!t.source_line.isEmpty())
                    return tr("A source line cannot have a range end: clear To.");
                const auto hi = symbol_table_ ? symbol_table_->resolve_key(to_text.toStdString())
                                              : SymbolTable().resolve_key(to_text.toStdString());
                if (!hi) return tr("To is not an address or a symbol: %1").arg(to_text);
                if (hi->addr < t.addr) return tr("To is below the address.");
                if (hi->addr != t.addr) {
                    t.hi    = hi->addr;
                    t.space = jnext::dbg::MemSpace::cpu();   // a range is never qualified with a memory
                }
            }
            if (mask_edit->isEnabled() && !mask_text.isEmpty()) {
                QString digits = mask_text;
                if (digits.startsWith(QLatin1Char('$'))) digits.remove(0, 1);
                else if (digits.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) digits.remove(0, 2);
                bool ok = false;
                const uint mask = digits.toUInt(&ok, 16);
                if (!ok || mask > 0xFFFF) return tr("The mask is not a hex number from 0000 to FFFF: %1").arg(mask_text);
                if (t.addr & ~mask)
                    return tr("The port value has bits outside the mask: it can never match.");
                if (mask == 0xFFFF && t.addr == 0)
                    return tr("A mask of FFFF with port 0000 is refused by the debugger.");
                t.mask = static_cast<uint16_t>(mask);
            }
            if (!t.condition.isEmpty() && dbg_) {
                const std::string why = jnext::script::bp_condition_problem(
                    static_cast<jnext::script::BpSpec::Kind>(type), t.condition.toStdString(), *dbg_);
                if (!why.empty()) return QString::fromStdString(why);
            }
            return QString();
        };
        BpTarget t;
        if (type == BreakpointModel::Interrupt || type == BreakpointModel::Nmi) {
            const QString why = finish(t);
            if (why.isEmpty()) {
                target = t;
                type_index = type;
                return true;
            }
            error->setText(why);
            error->show();
            continue;
        }
        // A number or a symbol first; then, for an Execute breakpoint, a source
        // line, which arms every place it compiled to, each tied to the memory
        // that supplies it.
        SourceResolution src;
        QString why;
        if (const auto resolved = symbol_table_ ? symbol_table_->resolve_key(text)
                                                : SymbolTable().resolve_key(text)) {
            // A name keeps the memory it was assembled for on an Execute
            // breakpoint (as a source line does): it breaks only while that
            // memory supplies the code.
            t.addr  = resolved->addr;
            t.space = resolved->space && type == BreakpointModel::Execute ? *resolved->space
                                                                          : jnext::dbg::MemSpace::cpu();
            why = finish(t);
        } else {
            if (dbg_) src = dbg_->source_map().resolve(text);
            if (!src.starts.empty() && type == BreakpointModel::Execute) {
                t.source_line = QString::fromStdString(text);
                why = finish(t);
            } else {
                // A source line names an instruction, so only an Execute breakpoint
                // can be set on one.
                why = !src.starts.empty()
                          ? tr("A source line can only be an Execute breakpoint. "
                               "Use an address or a symbol for this type.")
                          : !src.error.empty()
                                ? QString::fromStdString(src.error)
                                : tr("Not an address, symbol or source line: %1")
                                      .arg(addr_edit->text().trimmed());
            }
        }
        if (why.isEmpty()) {
            target = t;
            type_index = type;
            return true;
        }
        error->setText(why);
        error->show();
    }
    return false;
}

// The breakpoint a dialog result stands for (a source line goes through add_source_line instead).
jnext::script::BpSpec BreakpointPanel::spec_of(int type_index, const BpTarget& t)
{
    jnext::script::BpSpec s;
    s.kind       = static_cast<jnext::script::BpSpec::Kind>(type_index);
    s.addr       = t.addr;
    s.space      = t.space;
    s.hi         = t.hi;
    s.mask       = t.mask;
    s.condition  = t.condition.toStdString();
    s.count_only = t.count_only;
    return s;
}

void BreakpointPanel::on_add()
{
    if (!model_) return;
    BpTarget target;
    int type_index = 0;
    if (!show_bp_dialog(tr("Add Breakpoint"), 0, jnext::dbg::MemSpace::cpu(), type_index, target))
        return;

    // No repaint call here: the mutation notified, and it notified the RIGHT
    // views — the gutter only for an Execute breakpoint, because a data
    // breakpoint changes nothing the gutter draws.
    if (!target.source_line.isEmpty()) {
        model_->add_source_line(target.source_line, target.count_only, target.condition.toStdString());
    } else {
        model_->add(spec_of(type_index, target));
    }
}

void BreakpointPanel::on_edit()
{
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entries_.size()) || !model_) return;

    auto old = entries_[row];
    if (!old.own) return;           // another client's: read-only (REQ-qt-13d)
    int type_index = old.type;
    BpTarget target;
    target.count_only = old.count_only;
    if (old.hi != old.addr) target.hi = old.hi;
    if (old.mask != jnext::script::bp_port_mask_for(old.addr)) target.mask = old.mask;
    target.condition = old.condition;

    if (!show_bp_dialog(tr("Edit Breakpoint"), old.addr, old.space, type_index, target))
        return;

    model_->remove(old);

    // Add new, carrying the old one's Enabled state across (GH #225). An edit
    // moves a breakpoint; it does not create one, so a disabled breakpoint
    // whose address the user corrects must come back still disabled. add()
    // always creates enabled, hence the explicit re-apply.
    if (!target.source_line.isEmpty()) {
        const jnext::dbg::GroupId g = model_->add_source_line(target.source_line, target.count_only,
                                                              target.condition.toStdString());
        if (g == jnext::dbg::GROUP_NONE) return;
        if (!old.enabled)
            for (const auto& r : model_->rows())
                if (r.group == g) { model_->set_enabled(r, false); break; }
    } else {
        const jnext::script::BpSpec s = spec_of(type_index, target);
        model_->add(s);
        model_->set_enabled(s, old.enabled);
    }
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
    model_->remove(e);
    // As in on_add(): the mutation notified the views its kind concerns.
}
