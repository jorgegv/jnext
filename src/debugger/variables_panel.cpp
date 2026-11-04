#include "debugger/variables_panel.h"
#include "debug/debugger.h"
#include "debugger/memory_text.h"

#include <QFont>
#include <QHeaderView>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QMenu>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

using jnext::dbg::Debugger;

namespace {
constexpr size_t kChunk = 100;   // children read per expansion; more are offered as ranges of this many

// The Value column's editor (GH #437): a line edit holding the item's RoleEditText, not the cell's text (which carries notes and
// the char of a char). It never writes the model: what is typed goes to the panel, which asks the debugger, and the cell shows
// whatever the next read says.
class ValueDelegate : public QStyledItemDelegate {
public:
    ValueDelegate(QTreeWidget* tree, VariablesPanel* panel) : QStyledItemDelegate(tree), tree_(tree), panel_(panel) {}
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override {
        auto* e = new QLineEdit(parent);
        e->setFrame(false);
        return e;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        static_cast<QLineEdit*>(editor)->setText(index.data(VariablesPanel::RoleEditText).toString());
    }
    void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override {
        QTreeWidgetItem* item = tree_->itemFromIndex(index);
        if (item) panel_->commit_edit(item, static_cast<QLineEdit*>(editor)->text());
    }
private:
    QTreeWidget* tree_;
    VariablesPanel* panel_;
};
}

VariablesPanel::VariablesPanel(Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    status_ = new QLabel(this);
    status_->setObjectName("variables_status");
    status_->setWordWrap(true);
    status_->hide();
    layout->addWidget(status_);

    message_ = new QLabel(this);
    message_->setObjectName("variables_message");
    message_->setWordWrap(true);
    message_->hide();
    layout->addWidget(message_);

    frames_ = new QComboBox(this);
    frames_->setObjectName("variables_frame");
    layout->addWidget(frames_);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName("variables_tree");
    tree_->setFont(mono);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({tr("Name"), tr("Type"), tr("Value"), tr("Address")});
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);   // an editor opens only through edit_item() (a double-click on a Value, paused)
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setUniformRowHeights(true);
    tree_->header()->setFont(mono);
    tree_->setColumnWidth(0, 120);
    tree_->setColumnWidth(1, 110);
    tree_->setColumnWidth(2, 140);
    tree_->header()->setStretchLastSection(true);
    layout->addWidget(tree_, 1);

    locals_ = new QTreeWidgetItem(tree_, {tr("Locals")});
    locals_->setData(0, RoleKind, KindGroup);
    locals_->setData(0, RoleKey, "S:L");
    globals_ = new QTreeWidgetItem(tree_, {tr("Globals and statics")});
    globals_->setData(0, RoleKind, KindGroup);
    globals_->setData(0, RoleKey, "S:G");
    locals_->setExpanded(true);
    globals_->setExpanded(true);

    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &VariablesPanel::on_context_menu);
    connect(frames_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { select_frame(i); });
    tree_->setItemDelegateForColumn(2, new ValueDelegate(tree_, this));
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int col) {
        if (col == 2) edit_item(item);
    });
    connect(tree_, &QTreeWidget::itemExpanded, this, &VariablesPanel::on_expanded);
    connect(tree_, &QTreeWidget::itemCollapsed, this, &VariablesPanel::on_collapsed);
}

int VariablesPanel::call_stack_row() const
{
    if (!dbg_ || frame_ < 1) return -1;
    return static_cast<size_t>(frame_) <= dbg_->call_stack().size() ? frame_ - 1 : -1;
}

QString VariablesPanel::address_text(const Debugger::CWhere& w) const
{
    using K = Debugger::CWhere::Kind;
    const bool next = memtext::next_layout(dbg_);
    switch (w.kind) {
    case K::Page:
        return QString::fromStdString(jnext::dbg::long_address(w.source.address, w.source.space, style_, next));
    case K::Cpu:
        // what a read reaches now: the memory the slot maps at that address
        return QString::fromStdString(jnext::dbg::long_address(w.cpu, memtext::now_of(slots_, w.cpu), style_, next));
    case K::Register:
        return QString::fromStdString(w.regs);
    default:
        return QString();
    }
}

VariablesPanel::Node VariablesPanel::node_of(const Debugger::CValueRow& row, const QString& key_prefix,
                                             const QString& parent_name) const
{
    Node n;
    n.kind = KindVar;
    n.row_kind = static_cast<int>(row.kind);
    n.file = QString::fromStdString(row.file);
    n.path = QString::fromStdString(row.path);
    n.key = key_prefix + n.path;
    QString name = QString::fromStdString(row.name);
    if (name == "*") name += parent_name;   // the dereference of a pointer: `*p`
    if (row.kind == Debugger::CValueRow::Kind::StaticLocal) {
        // `sum::calls`, so two statics of one name stay apart: the path without a leading `<file>::` (which only a repeated function name has)
        name = n.path;
        const QString file = QString::fromStdString(row.file) + "::";
        if (name.startsWith(file)) name = name.mid(file.size());
    }
    QString value;
    QString tip;
    if (row.ok) {
        value = QString::fromStdString(row.value);
        QStringList notes;
        for (const std::string& s : row.notes) notes << QString::fromStdString(s);
        if (!notes.isEmpty()) {
            value += "  (" + notes.join("; ") + ")";
            tip = notes.join("\n");
        }
    } else {
        value = QString::fromStdString(row.why);   // a refusal is text, never a number
        n.placeholder = true;
        tip = value;
    }
    n.cols = {name, QString::fromStdString(row.type), value, address_text(row.where)};
    n.tip = tip;
    // a pointer to something (not `void *`, whose type text ends the same way but has nothing to look at) opens onto its pointee
    n.expandable = row.aggregate || (row.ok && !row.type.empty() && row.type.back() == '*' && row.type != "void *");
    // GH #437: a scalar or a pointer that was read can be written; its editor holds the first word of the facade's value (string slicing, not number formatting)
    n.editable = row.ok && !row.aggregate;
    if (n.editable) n.edit_text = QString::fromStdString(row.value).section(' ', 0, 0);
    return n;
}

VariablesPanel::Node VariablesPanel::message(const QString& text) const
{
    Node n;
    n.kind = KindMessage;
    n.key = "#msg";
    n.cols = {QString(), QString(), text, QString()};
    n.tip = text;
    n.placeholder = true;
    return n;
}

std::vector<VariablesPanel::Node> VariablesPanel::locals_nodes() const
{
    std::vector<Node> out;
    const Debugger::CLocalsResult r = dbg_->c_locals(static_cast<size_t>(frame_));
    if (!r.ok) {
        out.push_back(message(QString::fromStdString(r.why)));
        return out;
    }
    const QString prefix = "L:" + locals_scope_ + "|";
    for (const Debugger::CValueRow& row : r.rows) out.push_back(node_of(row, prefix, QString()));
    return out;
}

std::vector<VariablesPanel::Node> VariablesPanel::globals_nodes() const
{
    std::vector<Node> out;
    std::map<std::string, size_t> at;   // file -> index in `out`, in the facade's order (by file)
    for (const Debugger::CValueRow& row : dbg_->c_globals()) {
        const auto it = at.find(row.file);
        size_t g;
        if (it == at.end()) {
            Node grp;
            grp.kind = KindGroup;
            grp.key = "G:" + QString::fromStdString(row.file);
            grp.cols = {QString::fromStdString(row.file), QString(), QString(), QString()};
            out.push_back(grp);
            g = out.size() - 1;
            at[row.file] = g;
        } else {
            g = it->second;
        }
        out[g].kids.push_back(node_of(row, "G:", QString()));
    }
    return out;
}

// The children of an expandable item: what the facade lists under its path, or ranges of kChunk when there are more.
std::vector<VariablesPanel::Node> VariablesPanel::child_nodes(QTreeWidgetItem* item) const
{
    std::vector<Node> out;
    const int kind = item->data(0, RoleKind).toInt();
    const QString path = item->data(0, RolePath).toString();
    const QString key = key_of(item);
    const size_t first = kind == KindRange ? static_cast<size_t>(item->data(0, RoleFirst).toInt()) : 0;
    const Debugger::CChildRows r = dbg_->c_children(static_cast<size_t>(frame_), path.toStdString(), first, kChunk);
    if (!r.ok) {
        out.push_back(message(QString::fromStdString(r.why)));
        return out;
    }
    // the key prefix of the section the item is in ("L:<function>|" or "G:"), so keys stay unique across Locals and Globals
    const QString prefix = key.startsWith("L:") ? key.left(key.indexOf('|') + 1) : QString("G:");
    if (kind == KindVar && r.total > kChunk) {
        for (size_t f = 0; f < r.total; f += kChunk) {
            const size_t last = std::min(f + kChunk, r.total) - 1;
            Node n;
            n.kind = KindRange;
            n.path = path;
            n.first = static_cast<int>(f);
            n.key = key + "#" + QString::number(f);
            n.cols = {"[" + QString::number(f) + ".." + QString::number(last) + "]", QString(), QString(), QString()};
            n.expandable = true;
            out.push_back(n);
        }
        return out;
    }
    const QString parent_name = item->text(0);
    for (const Debugger::CValueRow& row : r.rows) out.push_back(node_of(row, prefix, parent_name));
    return out;
}

void VariablesPanel::apply(QTreeWidgetItem* item, const Node& n)
{
    for (int c = 0; c < 4; ++c) item->setText(c, n.cols.value(c));
    item->setToolTip(2, n.tip);
    item->setToolTip(3, n.tip);
    item->setData(0, RoleKind, n.kind);
    item->setData(0, RolePath, n.path);
    item->setData(0, RoleFirst, n.first);
    item->setData(0, RoleKey, n.key);
    item->setData(0, RoleExpandable, n.expandable);
    item->setData(0, RoleRowKind, n.row_kind);
    item->setData(0, RoleFile, n.file);
    item->setData(0, RoleEditText, n.edit_text);
    item->setData(2, RoleEditText, n.edit_text);
    item->setFlags(n.editable ? item->flags() | Qt::ItemIsEditable : item->flags() & ~Qt::ItemFlags(Qt::ItemIsEditable));
    if (n.placeholder)
        item->setForeground(2, palette().color(QPalette::PlaceholderText));
    else
        item->setData(2, Qt::ForegroundRole, QVariant());
    item->setChildIndicatorPolicy(n.expandable || !n.kids.empty() ? QTreeWidgetItem::ShowIndicator
                                                                  : QTreeWidgetItem::DontShowIndicator);
}

// Make `parent`'s children be `nodes`. The same keys in the same order: update in place, and re-read only the open ones.
// Otherwise the children are rebuilt, and the ones the user had open (by key) are opened again.
void VariablesPanel::sync(QTreeWidgetItem* parent, const std::vector<Node>& nodes)
{
    bool same = parent->childCount() == static_cast<int>(nodes.size());
    for (int i = 0; same && i < parent->childCount(); ++i) same = key_of(parent->child(i)) == nodes[static_cast<size_t>(i)].key;
    if (!same) {
        qDeleteAll(parent->takeChildren());
        for (const Node& n : nodes) parent->addChild(new QTreeWidgetItem());
    }
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* item = parent->child(i);
        const Node& n = nodes[static_cast<size_t>(i)];
        apply(item, n);
        if (n.kind == KindGroup) {
            sync(item, n.kids);
            if (!same && expanded_.contains(n.key)) item->setExpanded(true);
        } else if (n.expandable) {
            const bool open = same ? item->isExpanded() : expanded_.contains(n.key);
            if (open) {
                sync(item, child_nodes(item));
                item->setExpanded(true);
            } else if (item->childCount() > 0) {
                qDeleteAll(item->takeChildren());   // closed: stale, read again when it is opened
            }
        } else if (item->childCount() > 0) {
            qDeleteAll(item->takeChildren());
        }
    }
}

void VariablesPanel::on_context_menu(const QPoint& pos)
{
    QTreeWidgetItem* item = tree_->itemAt(pos);
    if (!item || item->data(0, RoleKind).toInt() != KindVar || !paused_) return;
    QMenu menu;
    QAction* watch = menu.addAction(tr("Watch"));
    QAction* edit = (item->flags() & Qt::ItemIsEditable) ? menu.addAction(tr("Edit Value")) : nullptr;   // GH #437: only where a value can be written
    QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
    if (chosen == watch) watch_item(item);
    else if (edit && chosen == edit) edit_item(item);
}

bool VariablesPanel::watch_item(QTreeWidgetItem* item)
{
    if (!dbg_ || !paused_ || !item || item->data(0, RoleKind).toInt() != KindVar) return false;
    CVarWatch w;
    w.path = item->data(0, RolePath).toString().toStdString();
    if (key_of(item).startsWith("L:")) {
        const auto store = dbg_->variables();
        const auto frames = dbg_->c_frames();
        const int f = static_cast<size_t>(frame_) < frames.size() ? frames[static_cast<size_t>(frame_)].function : -1;
        if (!store || f < 0 || static_cast<size_t>(f) >= store->functions().size()) return false;
        const auto& fn = store->functions()[static_cast<size_t>(f)];
        if (item->data(0, RoleRowKind).toInt() == static_cast<int>(Debugger::CValueRow::Kind::StaticLocal)) {
            // a static local lives in its function, not in a frame: unbound, spelled as c_globals() spells it, with the file when the function
            // name is not unique (find_var prefers the CURRENT frame's function of a bare `fn::`, which could be another module's)
            int same = 0;
            for (const auto& other : store->functions()) same += other.name == fn.name;
            const std::string file = item->data(0, RoleFile).toString().toStdString();
            w.path = (same > 1 ? file + "::" : std::string()) + fn.name + "::" + w.path;
        } else {
            w.function = fn.name;
            w.module = fn.module;
        }
    }
    emit watch_requested(w);
    return true;
}

bool VariablesPanel::edit_item(QTreeWidgetItem* item)
{
    if (!dbg_ || !paused_ || !item || item->data(0, RoleKind).toInt() != KindVar || !(item->flags() & Qt::ItemIsEditable)) return false;
    tree_->editItem(item, 2);
    return true;
}

void VariablesPanel::commit_edit(QTreeWidgetItem* item, const QString& text)
{
    if (!dbg_ || !paused_ || !item || item->data(0, RoleKind).toInt() != KindVar) return;
    if (text == item->data(2, RoleEditText).toString()) return;   // unchanged: no write, no MUTATE line, no revision bump
    jnext::dbg::CNewValue v;
    v.text = text.toStdString();
    const QString path = item->data(0, RolePath).toString();
    const Debugger::CWriteResult w = dbg_->write_c_value(client_, static_cast<size_t>(frame_), path.toStdString(), v);
    if (w.ok()) {
        // the write bumped the backend's revision: the manager's next paused tick redraws every panel, this one with the value read back
        message_->hide();
        return;
    }
    message_->setText(path + ": " + QString::fromStdString(w.why));
    message_->show();
    refresh();   // nothing changed, so the manager will not redraw: the cell must get its value back
}

void VariablesPanel::on_expanded(QTreeWidgetItem* item)
{
    if (syncing_) return;
    expanded_.insert(key_of(item));
    if (!paused_ || item->childCount() > 0) return;
    const int kind = item->data(0, RoleKind).toInt();
    if (kind != KindVar && kind != KindRange) return;
    slots_ = dbg_->mmu_slots();
    syncing_ = true;
    sync(item, child_nodes(item));
    syncing_ = false;
}

void VariablesPanel::on_collapsed(QTreeWidgetItem* item)
{
    if (!syncing_) expanded_.remove(key_of(item));
}

void VariablesPanel::rebuild_frames()
{
    const std::vector<Debugger::CFrame> frames = dbg_->c_frames();
    const auto store = dbg_->variables();
    frames_->blockSignals(true);
    frames_->clear();
    for (size_t i = 0; i < frames.size(); ++i) {
        const Debugger::CFrame& f = frames[i];
        QString text;
        QString fn;
        if (f.function >= 0 && store && static_cast<size_t>(f.function) < store->functions().size()) {
            fn = QString::fromStdString(store->functions()[static_cast<size_t>(f.function)].name);
            text = fn;
            if (f.line) text += " at " + QString::fromStdString(f.line->file) + ":" + QString::number(f.line->line);
        } else {
            text = tr("<not C code>") + QString::asprintf(" $%04X", f.pc.address);
        }
        if (i == 0) text += tr(" (current)");
        frames_->addItem(text, fn);
    }
    if (frame_ >= frames_->count()) frame_ = 0;
    frames_->setCurrentIndex(frames_->count() ? frame_ : -1);
    frames_->blockSignals(false);
    locals_scope_ = frames_->count() ? frames_->itemData(frame_).toString() : QString();
}

void VariablesPanel::select_frame(int k)
{
    // not while running: the frame list is the last stop's, and a read now would show values that belong to neither (the next paused
    // refresh decides)
    if (!dbg_ || k < 0 || k >= frames_->count() || k == frame_) return;
    if (!paused_) {
        // ignored: the selector shows the frame the tree shows
        frames_->blockSignals(true);
        frames_->setCurrentIndex(frame_);
        frames_->blockSignals(false);
        return;
    }
    frame_ = k;
    frames_->blockSignals(true);
    frames_->setCurrentIndex(k);
    frames_->blockSignals(false);
    locals_scope_ = frames_->itemData(k).toString();
    slots_ = dbg_->mmu_slots();
    syncing_ = true;
    sync(locals_, locals_nodes());
    syncing_ = false;
    emit frame_changed(k);
}

void VariablesPanel::refresh()
{
    if (!dbg_ || !paused_) return;
    const Debugger::CVariablesStatus st = dbg_->c_variables_status();
    status_->setText(QString::fromStdString(st.why));
    status_->setHidden(st.available);
    if (!st.available) {
        syncing_ = true;
        qDeleteAll(locals_->takeChildren());
        qDeleteAll(globals_->takeChildren());
        syncing_ = false;
        frames_->blockSignals(true);
        frames_->clear();
        frames_->blockSignals(false);
        frame_ = 0;
        return;
    }
    const uint64_t cycle = dbg_->time().master_cycle;
    if (!have_cycle_ || cycle != last_cycle_) {
        frame_ = 0;   // a new stop: back to where the CPU is
        message_->hide();
    }
    have_cycle_ = true;
    last_cycle_ = cycle;
    slots_ = dbg_->mmu_slots();
    rebuild_frames();
    syncing_ = true;
    sync(locals_, locals_nodes());
    sync(globals_, globals_nodes());
    syncing_ = false;
}
