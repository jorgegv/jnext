#include "debugger/symbols_panel.h"

#include "debug/debugger.h"
#include "debug/symbol_table.h"
#include "debugger/memory_text.h"
#include "qt/palette_tint_qt.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int kRoleMapped = Qt::UserRole + 1;   // 1 mapped now, 0 not
constexpr int kColName = 0, kColAddr = 1, kColMem = 2;
}  // namespace

SymbolsPanel::SymbolsPanel(const jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent), dbg_(dbg)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(2);

    auto* top = new QHBoxLayout();
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter symbols"));
    filter_->setClearButtonEnabled(true);
    top->addWidget(filter_, 1);
    count_ = new QLabel(QStringLiteral("0 of 0"), this);
    top->addWidget(count_);
    lay->addLayout(top);

    table_ = new QTableWidget(0, 3, this);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Address"), tr("Memory")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setShowGrid(false);
    table_->viewport()->installEventFilter(this);
    lay->addWidget(table_, 1);

    message_ = new QLabel(this);
    message_->setWordWrap(true);
    lay->addWidget(message_);

    connect(filter_, &QLineEdit::textChanged, this, [this]() { apply_filter_(); });
    connect(table_, &QTableWidget::cellActivated, this, [this](int row, int) { activate_row_(row); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { activate_row_(row); });
}

void SymbolsPanel::set_address_style(jnext::dbg::AddressStyle st)
{
    if (style_ == st) return;
    style_ = st;
    rev_ = ~0ull;   // the Memory column's text changes
}

void SymbolsPanel::focus_filter()
{
    filter_->setFocus(Qt::OtherFocusReason);
    filter_->selectAll();
}

bool SymbolsPanel::mapped_(const Row& r) const
{
    // An unpaged label names its address in every memory; a paged one only while that memory supplies it.
    return !r.space || dbg_->source_page(r.addr) == *r.space;
}

QString SymbolsPanel::unmapped_text_(const Row& r) const
{
    const QString what = r.space ? memtext::text(dbg_, style_, *r.space, r.addr) : QString();
    return tr("not mapped at $%1 now: %2 is not paged in").arg(QString::asprintf("%04X", r.addr)).arg(what);
}

void SymbolsPanel::rebuild_()
{
    rows_.clear();
    table_->setRowCount(0);
    if (!dbg_) return;
    const SymbolTable& st = dbg_->symbols();
    rev_ = dbg_->state_revision();
    table_->setUpdatesEnabled(false);
    for (const auto& [addr, label] : st.symbols()) {
        Row r;
        r.addr = addr;
        r.space = label.space;
        rows_.push_back(r);
    }
    table_->setRowCount(static_cast<int>(rows_.size()));
    int i = 0;
    for (const auto& [addr, label] : st.symbols()) {
        const Row& r = rows_[static_cast<size_t>(i)];
        table_->setItem(i, kColName, new QTableWidgetItem(QString::fromStdString(st.shown_name(label.name))));
        table_->setItem(i, kColAddr, new QTableWidgetItem(QString::asprintf("$%04X", addr)));
        table_->setItem(i, kColMem, new QTableWidgetItem(
            r.space ? memtext::text(dbg_, style_, *r.space, addr) : tr("any")));
        for (int c = 0; c < 3; ++c) table_->item(i, c)->setData(kRoleMapped, -1);
        ++i;
    }
    table_->setUpdatesEnabled(true);
    regrey_();
    apply_filter_();
}

void SymbolsPanel::regrey_()
{
    const QColor grey = palette_tint::ink(QApplication::palette(), QColor(0x88, 0x88, 0x88));
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const Row& r = rows_[static_cast<size_t>(i)];
        const int now = mapped_(r) ? 1 : 0;
        if (table_->item(i, kColName)->data(kRoleMapped).toInt() == now) continue;   // unchanged: touch nothing
        for (int c = 0; c < 3; ++c) {
            QTableWidgetItem* it = table_->item(i, c);
            it->setData(kRoleMapped, now);
            it->setForeground(now ? QBrush() : QBrush(grey));
            it->setToolTip(now ? QString() : unmapped_text_(r));
        }
    }
}

void SymbolsPanel::apply_filter_()
{
    const QString f = filter_->text().trimmed();
    int shown = 0;
    for (int i = 0; i < table_->rowCount(); ++i) {
        const bool hit = f.isEmpty() || table_->item(i, kColName)->text().contains(f, Qt::CaseInsensitive);
        table_->setRowHidden(i, !hit);
        if (hit) ++shown;
    }
    count_->setText(tr("%1 of %2").arg(shown).arg(table_->rowCount()));
}

void SymbolsPanel::refresh()
{
    if (!dbg_) return;
    if (dbg_->state_revision() != rev_) rebuild_();
    else regrey_();
}

void SymbolsPanel::activate_row_(int row)
{
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const Row& r = rows_[static_cast<size_t>(row)];
    if (!mapped_(r)) {
        message_->setText(unmapped_text_(r));
        return;
    }
    message_->clear();
    emit show_in_disassembly_requested(r.addr);
}

bool SymbolsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == table_->viewport() && event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        const int row = table_->rowAt(ce->pos().y());
        if (row >= 0) show_menu_(row, ce->globalPos());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void SymbolsPanel::show_menu_(int row, const QPoint& global)
{
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const Row r = rows_[static_cast<size_t>(row)];
    const bool mapped = mapped_(r);
    const uint16_t addr = r.addr;

    QMenu menu(this);
    menu.setToolTipsVisible(true);
    QAction* dis = menu.addAction(tr("Show in Disassembly"));
    QAction* src = nullptr;
    if (dbg_ && !dbg_->source_map().empty()) {
        src = menu.addAction(tr("Show in Source"));
        if (!dbg_->source_map().lookup(r.space ? *r.space : dbg_->source_page(addr), addr)) {
            src->setEnabled(false);
            src->setToolTip(tr("no source line at $%1").arg(QString::asprintf("%04X", addr)));
        }
    }
    QAction* mem = menu.addAction(tr("Show in Memory"));
    if (!mapped) {
        dis->setEnabled(false);
        mem->setEnabled(false);
        dis->setToolTip(unmapped_text_(r));
        mem->setToolTip(unmapped_text_(r));
    }
    QAction* chosen = nullptr;
    for (QAction* a : {dis, src, mem})
        if (a) connect(a, &QAction::triggered, &menu, [&chosen, a]() { chosen = a; });
    menu.exec(global);
    if (chosen == dis) emit show_in_disassembly_requested(addr);
    else if (chosen == mem) emit show_in_memory_requested(addr);
    else if (chosen && chosen == src) emit show_in_source_requested(addr);
}
