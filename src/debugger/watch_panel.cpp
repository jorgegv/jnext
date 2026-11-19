#include "debugger/watch_panel.h"
#include "debug/debugger.h"
#include "debugger/memory_text.h"
#include "script/expr_compiler.h"
#include "script/diagnostic.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QString>

WatchPanel::WatchPanel(QWidget* parent)
    : QWidget(parent)
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

    auto* edit_btn = new QPushButton(tr("Edit"), this);
    connect(edit_btn, &QPushButton::clicked, this, &WatchPanel::on_edit_watch);
    btn_row->addWidget(edit_btn);

    auto* remove_btn = new QPushButton(tr("Remove"), this);
    connect(remove_btn, &QPushButton::clicked, this, &WatchPanel::remove_selected);
    btn_row->addWidget(remove_btn);

    btn_row->addStretch();
    layout->addLayout(btn_row);

    // Table
    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({tr("Address"), tr("Label"), tr("Type"), tr("Value"), tr("Memory")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    // GH #360: Memory is the last column and takes the stretch, so Value is sized to its text.
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);

    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);

    layout->addWidget(table_, 1);
}

// GH #373: "$<hex> (<signed decimal>d)", 2, 4 or 8 hex digits by magnitude (the language is signed int32; a negative is its two's complement).
static QString expr_value_text(int32_t v) {
    const unsigned u = static_cast<unsigned>(v);
    const int digits = u <= 0xFF ? 2 : u <= 0xFFFF ? 4 : 8;
    return QString::asprintf("$%0*X (%dd)", digits, u, static_cast<int>(v));
}

void WatchPanel::refresh() {
    if (!dbg_) return;

    // CAP-INS-02 — peek(Cpu) wraps at 0xFFFF exactly as the 16-bit address
    // arithmetic of the CPU does, so a Word or Long at the top of memory reads
    // on from 0x0000, as it always did.
    auto peek = [this](uint16_t a) -> uint8_t {
        uint8_t b = 0;
        dbg_->peek(jnext::dbg::MemSpace::cpu(), a, 1, &b);
        return b;
    };

    // One formatter for all three sizes: "$<hex> (<unsigned decimal>d)".
    auto hexdec = [](int digits, uint32_t v) {
        return QString::asprintf("$%0*X (%ud)", digits, static_cast<unsigned>(v),
                                 static_cast<unsigned>(v));
    };

    // GH #360: the memory a read of each watch's address reaches NOW (live).
    const auto sinfo = dbg_->mmu_slots();

    for (int i = 0; i < static_cast<int>(watches_.size()); ++i) {
        const auto& w = watches_[i];
        if (w.kind == WatchEntry::Kind::CVariable) {
            if (paused_) refresh_c_row(i, w);   // while running a C row keeps the text of the last stop
            continue;
        }
        if (w.kind == WatchEntry::Kind::Expression) {
            // GH #373: every refresh, paused or running; no cached compile, so a reloaded map is followed
            const auto r = jnext::script::eval_expr(w.expr, *dbg_);
            if (auto* item = i < table_->rowCount() ? table_->item(i, 3) : nullptr) {
                item->setText(r.ok ? expr_value_text(r.value)
                                   : QString::fromStdString(r.errors.empty() ? std::string("error") : r.errors.front().to_string()));
                item->setData(Qt::ForegroundRole,
                              r.ok ? QVariant() : QVariant(palette().color(QPalette::PlaceholderText)));
            }
            continue;
        }
        QString value_str;

        switch (w.type) {
            case BYTE: {
                uint8_t val = peek(w.addr);
                value_str = hexdec(2, val);
                break;
            }
            case WORD: {
                uint8_t lo = peek(w.addr);
                uint8_t hi = peek(static_cast<uint16_t>(w.addr + 1));
                uint16_t val = static_cast<uint16_t>(lo | (hi << 8));
                value_str = hexdec(4, val);
                break;
            }
            case LONG: {
                uint32_t val = 0;
                for (int b = 0; b < 4; ++b) {
                    uint8_t byte = peek(static_cast<uint16_t>(w.addr + b));
                    val |= static_cast<uint32_t>(byte) << (b * 8);
                }
                value_str = hexdec(8, val);
                break;
            }
            case EXPR:   // handled above
                break;
        }

        if (i < table_->rowCount()) {
            auto* item = table_->item(i, 3);
            if (item) item->setText(value_str);
            if (auto* mem = table_->item(i, 4))
                mem->setText(memtext::text(dbg_, style_, memtext::now_of(sinfo, w.addr), w.addr));
        }
    }
}

// A C row, read at a stop. A parameter or local follows its function: it is read in the NEWEST frame of it (by name and module), and the row
// says so when there is none. A global, file static or static local is read through its unshadowable path, in frame 0.
void WatchPanel::refresh_c_row(int row, const WatchEntry& w) {
    // what is shown is also kept in the entry, so a table rebuild (an Add or Remove while the machine runs) shows the last stop again
    auto set = [&](int col, const QString& text) {
        watches_[static_cast<size_t>(row)].shown[col] = text;
        if (auto* it = table_->item(row, col)) it->setText(text);
    };
    const auto placeholder = [&](bool on) {
        watches_[static_cast<size_t>(row)].refused = on;
        if (auto* it = table_->item(row, 3))
            it->setData(Qt::ForegroundRole, on ? QVariant(palette().color(QPalette::PlaceholderText)) : QVariant());
    };
    set(0, QString());
    set(4, QString());
    const auto status = dbg_->c_variables_status();
    if (!status.available) {
        set(3, QString::fromStdString(status.why));
        placeholder(true);
        return;
    }
    size_t frame = 0;
    if (!w.cvar.function.empty()) {
        bool found = false;
        const auto store = dbg_->variables();
        const auto frames = dbg_->c_frames();
        for (size_t k = 0; store && !found && k < frames.size(); ++k) {
            const int f = frames[k].function;
            if (f < 0 || static_cast<size_t>(f) >= store->functions().size()) continue;
            const auto& fn = store->functions()[static_cast<size_t>(f)];
            if (fn.name == w.cvar.function && fn.module == w.cvar.module) {
                frame = k;
                found = true;
            }
        }
        if (!found) {
            set(3, tr("out of scope: %1 is not on the call stack").arg(QString::fromStdString(w.cvar.function)));
            placeholder(true);
            return;
        }
    }
    const auto r = dbg_->read_c_value(frame, w.cvar.path);
    if (!r.type.empty()) set(2, QString::fromStdString(r.type));
    if (!r.ok) {
        set(3, QString::fromStdString(r.why));   // a refusal is text, never a number
        placeholder(true);
        return;
    }
    QString value = QString::fromStdString(r.value);
    QStringList notes;
    for (const std::string& n : r.notes) notes << QString::fromStdString(n);
    if (!notes.isEmpty()) value += "  (" + notes.join("; ") + ")";
    set(3, value);
    placeholder(false);
    using K = jnext::dbg::Debugger::CWhere::Kind;
    const auto mmu = dbg_->mmu_slots();
    switch (r.where.kind) {
    case K::Page:
        set(0, QString::asprintf("$%04X", r.where.source.address));
        set(4, memtext::text(dbg_, style_, r.where.source.space, r.where.source.address));
        break;
    case K::Cpu:
        set(0, QString::asprintf("$%04X", r.where.cpu));
        set(4, memtext::text(dbg_, style_, memtext::now_of(mmu, r.where.cpu), r.where.cpu));
        break;
    case K::Register:
        set(0, QString::fromStdString(r.where.regs));
        break;
    default:
        break;
    }
}

void WatchPanel::add_c_watch(const CVarWatch& cv) {
    WatchEntry entry;
    entry.kind = WatchEntry::Kind::CVariable;
    entry.cvar = cv;
    watches_.push_back(entry);
    update_table();
}

void WatchPanel::add_expr_watch(const std::string& expr, const std::string& label) {
    WatchEntry entry;
    entry.kind = WatchEntry::Kind::Expression;
    entry.expr = expr;
    entry.label = label;
    entry.type = EXPR;
    watches_.push_back(entry);
    update_table();
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

bool WatchPanel::show_watch_dialog(const QString& title, uint16_t& addr,
                                    std::string& label, int& type, std::string& expr)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(400);

    auto* form = new QFormLayout(&dlg);

    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText("e.g. 4000, $4000 or a symbol (an expression with Type Expression)");
    addr_edit->setText(type == EXPR ? QString::fromStdString(expr) : QString::asprintf("%04X", addr));
    form->addRow(tr("Address, symbol or expression:"), addr_edit);

    auto* label_edit = new QLineEdit(&dlg);
    label_edit->setPlaceholderText("(optional)");
    label_edit->setText(QString::fromStdString(label));
    form->addRow(tr("Label:"), label_edit);

    auto* type_combo = new QComboBox(&dlg);
    type_combo->addItem(tr("Byte"));
    type_combo->addItem(tr("Word"));
    type_combo->addItem(tr("Long"));
    type_combo->addItem(tr("Expression"));   // last: indices 0..2 are unchanged
    type_combo->setCurrentIndex(type);
    form->addRow(tr("Type:"), type_combo);

    // Why OK did not close the dialog. Hidden until there is a reason.
    auto* error = new QLabel(&dlg);
    error->setObjectName(QStringLiteral("watch_dialog_error"));
    error->setWordWrap(true);
    error->setStyleSheet(QStringLiteral("color: #c00000;"));
    error->hide();
    form->addRow(error);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    // CAP-SYM: hex ($ / 0x prefixes optional), else a loaded symbol name;
    // `@name` forces the symbol (OD-12).
    // OK on text that names neither shows the dialog again with the reason; it
    // never returns as if cancelled, so an entry is never silently dropped.
    // Only Cancel gives up.
    std::string entered;
    std::optional<uint16_t> resolved;
    bool is_expr = false;
    while (dlg.exec() == QDialog::Accepted) {
        entered = addr_edit->text().trimmed().toStdString();
        if (type_combo->currentIndex() == EXPR) {
            // GH #373: an expression is compiled in no event, so an event payload name or a script-only form is refused here
            using namespace jnext::script;
            const auto c = compile_expr(entered, PayloadScope::none(),
                                        {dbg_ ? symbols_of(*dbg_) : SymbolResolver{}, {}});
            if (c) { is_expr = true; break; }
            error->setText(tr("Not an expression: %1").arg(QString::fromStdString(c.errors.front().to_string())));
            error->show();
            continue;
        }
        resolved = dbg_ ? dbg_->symbols().resolve(entered) : SymbolTable().resolve(entered);
        if (resolved) break;
        error->setText(tr("Not an address or symbol: %1").arg(addr_edit->text().trimmed()));
        error->show();
    }
    if (is_expr) {
        expr = entered;
        label = label_edit->text().trimmed().toStdString();
        type = EXPR;
        return true;
    }
    if (!resolved) return false;
    addr = *resolved;

    label = label_edit->text().trimmed().toStdString();
    // Labelled only when the text named the symbol it resolved to: `BEEF`
    // typed as a number is not the variable `BEEF`.
    if (label.empty() && dbg_) {
        std::string name = entered;
        if (!name.empty() && name.front() == '@')
            name = QString::fromStdString(name.substr(1)).trimmed().toStdString();
        if (dbg_->lookup_name(name) == resolved) label = dbg_->symbols().shown_name(name);
    }
    type = type_combo->currentIndex();
    return true;
}

void WatchPanel::on_add_watch() {
    uint16_t addr = 0;
    std::string label;
    int type = 0;
    std::string expr;
    if (show_watch_dialog(tr("Add Watch"), addr, label, type, expr)) {
        if (type == EXPR) add_expr_watch(expr, label);
        else add_watch(addr, label, type);
    }
}

void WatchPanel::on_edit_watch() {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(watches_.size())) return;

    auto& w = watches_[row];
    if (w.kind == WatchEntry::Kind::CVariable) return;   // nothing an address dialog could edit
    uint16_t addr = w.addr;
    std::string label = w.label;
    int type = static_cast<int>(w.type);
    std::string expr = w.expr;

    if (show_watch_dialog(tr("Edit Watch"), addr, label, type, expr)) {
        w.addr = addr;
        w.label = label;
        w.type = static_cast<WatchType>(type);
        w.kind = type == EXPR ? WatchEntry::Kind::Expression : WatchEntry::Kind::Memory;
        w.expr = type == EXPR ? expr : std::string();
        update_table();
    }
}

void WatchPanel::update_table() {
    table_->setRowCount(static_cast<int>(watches_.size()));

    static const char* type_names[] = {"Byte", "Word", "Long", "Expression"};

    for (int i = 0; i < static_cast<int>(watches_.size()); ++i) {
        const auto& w = watches_[i];

        const bool c = w.kind == WatchEntry::Kind::CVariable;
        const bool x = w.kind == WatchEntry::Kind::Expression;
        auto* addr_item = new QTableWidgetItem(c ? w.shown[0] : x ? QString() : QString::asprintf("$%04X", w.addr));
        table_->setItem(i, 0, addr_item);

        QString label = QString::fromStdString(w.label);
        if (c) {
            label = QString::fromStdString(w.cvar.path);
            if (!w.cvar.function.empty()) label += " (in " + QString::fromStdString(w.cvar.function) + ")";
        }
        if (x && label.isEmpty()) label = QString::fromStdString(w.expr);
        auto* label_item = new QTableWidgetItem(label);
        if (x) label_item->setToolTip(QString::fromStdString(w.expr));
        table_->setItem(i, 1, label_item);

        auto* type_item = new QTableWidgetItem(c ? w.shown[2] : QString(type_names[w.type]));
        table_->setItem(i, 2, type_item);

        auto* value_item = new QTableWidgetItem(c && !w.shown[3].isEmpty() ? w.shown[3] : QString("--"));
        if (c && w.refused) value_item->setData(Qt::ForegroundRole, palette().color(QPalette::PlaceholderText));
        table_->setItem(i, 3, value_item);

        table_->setItem(i, 4, new QTableWidgetItem(c ? w.shown[4] : QString()));
    }

    refresh();
}
