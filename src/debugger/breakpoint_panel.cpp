#include "debugger/breakpoint_panel.h"
#include "core/emulator.h"
#include "debug/debug_state.h"
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

    // GH #225 — the MASTER SWITCH. It deletes nothing and clears no
    // per-breakpoint flag; it decides whether the set's live cache is built
    // from those flags at all, so unchecking and re-checking it restores
    // exactly the set that was there. Right-aligned, away from the three
    // destructive buttons it must not be mistaken for.
    master_check_ = new QCheckBox(tr("Breakpoints enabled"), this);
    master_check_->setChecked(
        emulator_->debug_state().breakpoints().master_enabled());
    master_check_->setToolTip(
        tr("Master switch. Unchecking suspends every breakpoint and watchpoint "
           "without deleting any; re-checking restores each one's own Enabled "
           "state. Step Over, Step Out and Run to Here keep working."));
    connect(master_check_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_) return;
        emulator_->debug_state().breakpoints().set_master_enabled(on);
    });
    btn_row->addWidget(master_check_);

    layout->addLayout(btn_row);

    // Table. Enabled first: it is a control, not data, and every debugger that
    // has one puts it in the leading column.
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels(
        {tr("On"), tr("Type"), tr("Address"), tr("Symbol")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setColumnWidth(0, 32);
    table_->setColumnWidth(1, 90);
    table_->setColumnWidth(2, 70);

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

    // GH #220 — the list is driven by the set, not by whoever mutated it. Both
    // change kinds matter here: this table holds Execute AND data breakpoints.
    observer_ = emulator_->debug_state().breakpoints().add_observer(
        [this](BreakpointChange) { refresh(); });
}

BreakpointPanel::~BreakpointPanel()
{
    emulator_->debug_state().breakpoints().remove_observer(observer_);
}

QString BreakpointPanel::type_name(int type_index)
{
    switch (type_index) {
        case 0: return "Execute";
        case 1: return "Read";
        case 2: return "Write";
        case 3: return "Read/Write";
        case 4: return "IO Read";
        case 5: return "IO Write";
        default: return "?";
    }
}

// The combo index -> WatchType map, in ONE place (GH #222).
//
// It used to be spelled out inline at each of the four mutation sites below
// as `wt = READ; if (idx==2) WRITE; if (idx==3) READ_WRITE;`. Adding the two
// I/O types would have meant getting the same edit right four times, and the
// two halves of on_edit() must agree exactly or an edited breakpoint is
// removed as one type and re-added as another. Index 0 is Execute, which is
// add_pc() and not a watchpoint at all — every caller tests for it first.
static WatchType watch_type_for(int type_index)
{
    switch (type_index) {
        case 2:  return WatchType::WRITE;
        case 3:  return WatchType::READ_WRITE;
        case 4:  return WatchType::IO_READ;
        case 5:  return WatchType::IO_WRITE;
        default: return WatchType::READ;
    }
}

void BreakpointPanel::rebuild_entries()
{
    entries_.clear();
    const auto& bps = emulator_->debug_state().breakpoints();

    // Execute (PC) breakpoints. pc_breakpoints() is THE MODEL (GH #225): it
    // carries the disabled ones too, each with its own flag, which is the
    // whole point — a disabled breakpoint stays in this list.
    for (const auto& entry : bps.pc_breakpoints()) {
        entries_.push_back({entry.first, 0, entry.second});
    }

    // Data (watchpoint) breakpoints.
    //
    // NOTHING MAKES THIS SWITCH EXHAUSTIVE AT COMPILE TIME, and dropping the
    // `default:` arm does not change that (GH #222 review). The project's one
    // -Werror=switch is `target_compile_options(jnext PRIVATE ...)` in
    // CMakeLists.txt:236 — scoped to the `jnext` executable, i.e. main.cpp's
    // CLI dispatch — and this file is in jnext_debugger; no -Wall is set
    // anywhere, and -Wswitch needs it. Measured, not assumed: a sixth
    // WatchType builds jnext_debugger with zero diagnostics.
    //
    // So A SIXTH WatchType MUST BE ADDED HERE BY HAND, together with
    // watch_type_for(), type_name() and the Add dialog's combo.
    //
    // The -1 initialiser is the one concession to that: an unhandled type
    // shows as "?" in the Type column instead of impersonating an Execute
    // breakpoint (index 0), which on_edit()/on_remove() would then route to
    // remove_pc(). Wrong and visible beats wrong and destructive.
    for (const auto& wp : bps.watchpoints()) {
        int ti = -1;
        switch (wp.type) {
            case WatchType::READ:       ti = 1; break;
            case WatchType::WRITE:      ti = 2; break;
            case WatchType::READ_WRITE: ti = 3; break;
            case WatchType::IO_READ:    ti = 4; break;
            case WatchType::IO_WRITE:   ti = 5; break;
        }
        entries_.push_back({wp.addr, ti, wp.enabled});
    }

    // Sort by address
    std::sort(entries_.begin(), entries_.end(),
        [](const BpEntry& a, const BpEntry& b) { return a.addr < b.addr; });
}

void BreakpointPanel::refresh()
{
    // GH #225 — RE-ENTRANCY GUARD, and it is not merely tidiness.
    //
    // apply_enabled_cell() raises `updating_` across its write to the set,
    // because that write notifies and the notification lands back here — while
    // Qt is still emitting itemChanged for the very QTableWidgetItem that the
    // rebuild below would delete. Suppressing the rebuild removes that
    // lifetime hazard, and costs nothing: the widget is already showing the
    // state the user just clicked. The OTHER subscriber (the disassembly
    // gutter) is unaffected — the suppression is this panel's, not the set's.
    if (updating_) return;

    rebuild_entries();

    // Everything below writes widgets FROM the model. The same flag is what
    // stops itemChanged() and the master checkbox's toggled() from writing
    // those values straight back into it.
    updating_ = true;

    if (master_check_)
        master_check_->setChecked(
            emulator_->debug_state().breakpoints().master_enabled());

    table_->setRowCount(static_cast<int>(entries_.size()));

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];

        // GH #225 — this breakpoint's OWN flag, not whether it can currently
        // fire. The master switch has its own control; echoing it into every
        // row would erase the state a user has to get back when they flip it.
        auto* en_item = new QTableWidgetItem();
        en_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                          Qt::ItemIsUserCheckable);
        en_item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
        table_->setItem(i, COL_ENABLED, en_item);

        auto* type_item = new QTableWidgetItem(type_name(e.type_index));
        table_->setItem(i, COL_TYPE, type_item);

        auto* addr_item = new QTableWidgetItem(QString::asprintf("$%04X", e.addr));
        table_->setItem(i, COL_ADDR, addr_item);

        QString sym;
        if (symbol_table_) {
            auto s = symbol_table_->lookup(e.addr);
            if (s) sym = QString::fromStdString(*s);
        }
        auto* sym_item = new QTableWidgetItem(sym);
        table_->setItem(i, COL_SYMBOL, sym_item);
    }

    updating_ = false;
}

// GH #225 — a click on a row's Enabled checkbox.
void BreakpointPanel::apply_enabled_cell(int row, bool enabled)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;

    // type_index == -1 is the "unknown WatchType" row rebuild_entries()
    // deliberately shows as "?": there is no type to address the breakpoint
    // by, so it is left alone rather than guessed at. Same rule as
    // on_edit()/on_remove().
    const auto e = entries_[row];
    if (e.type_index < 0) return;

    auto& bps = emulator_->debug_state().breakpoints();

    // Raised across the write: see refresh()'s head for why the rebuild this
    // notification would otherwise trigger must not happen from inside the
    // itemChanged signal that got us here.
    updating_ = true;
    if (e.type_index == 0) {
        bps.set_pc_enabled(e.addr, enabled);
    } else {
        bps.set_watchpoint_enabled(e.addr, watch_type_for(e.type_index), enabled);
    }
    updating_ = false;

    // ... and because there was no rebuild, entries_ is kept truthful here.
    // on_edit() reads this flag to carry it across an address change.
    entries_[row].enabled = enabled;
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
    type_combo->addItem(tr("IO Read"));
    type_combo->addItem(tr("IO Write"));
    type_combo->setCurrentIndex(type_index);
    form->addRow(tr("Type:"), type_combo);

    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText("e.g. 4000 or $4000");
    addr_edit->setText(QString::asprintf("%04X", addr));
    form->addRow(tr("Address (hex):"), addr_edit);

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
        bps.add_watchpoint(addr, watch_type_for(type_index));
    }
    // No repaint call here: the mutation notified, and it notified the RIGHT
    // views. This site used to refresh the disassembly whichever type was
    // added; now only add_pc() reaches it, because a watchpoint changes nothing
    // the gutter draws. Same pixels, less work.
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
        bps.remove_watchpoint(old.addr, watch_type_for(old.type_index));
    }

    // Add new, carrying the old one's Enabled state across (GH #225). An edit
    // moves a breakpoint; it does not create one, so a disabled breakpoint
    // whose address the user corrects must come back still disabled. add_*()
    // always creates enabled, hence the explicit re-apply.
    if (type_index == 0) {
        bps.add_pc(addr);
        bps.set_pc_enabled(addr, old.enabled);
    } else {
        const WatchType wt = watch_type_for(type_index);
        bps.add_watchpoint(addr, wt);
        bps.set_watchpoint_enabled(addr, wt, old.enabled);
    }
    // Each of the mutations above notified; the last one left the table
    // showing the edited breakpoint. Nothing to repaint by hand.
}

void BreakpointPanel::on_remove()
{
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;

    // A COPY, not a reference: the removal below notifies, refresh() rebuilds
    // entries_ from under us, and a reference into it would dangle (GH #220).
    const auto e = entries_[row];
    auto& bps = emulator_->debug_state().breakpoints();

    if (e.type_index == 0) {
        bps.remove_pc(e.addr);
    } else {
        bps.remove_watchpoint(e.addr, watch_type_for(e.type_index));
    }
    // As in on_add(): the mutation notified the views its kind concerns.
}
