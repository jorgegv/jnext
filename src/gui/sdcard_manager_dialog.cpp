#include "gui/sdcard_manager_dialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QSignalBlocker>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "core/emulator.h"
#include "core/sdcard_card_ops.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_file_add.h"
#include "gui/sdcard_card_model.h"

// QFile::moveToTrash is Qt 5.15's: the floor of every Qt5 leg that exists (the Qt5 guard and the MinGW Qt5 builds are
// 5.15.18; every Qt6 leg is newer). There is no fallback, and a trash that fails is REPORTED: a host delete never
// becomes a permanent one.
static_assert(QT_VERSION >= QT_VERSION_CHECK(5, 15, 0), "the SD card file manager needs QFile::moveToTrash (Qt 5.15)");

namespace {

namespace sm = sdmanager;

/// A QListView that takes drops: the owner decides what it accepts and what a drop does.
class DropView : public QListView {
public:
    std::function<bool(const QMimeData&)> accepts;
    std::function<void(const QMimeData&, bool shift)> dropped;

public:
    /// What a test drives: the viewport's event path, which is how a real drop reaches dropEvent (a scroll area
    /// ignores drag events delivered to the view itself).
    bool deliver(QEvent* e) { return viewportEvent(e); }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override { take(e); }
    void dragMoveEvent(QDragMoveEvent* e) override { take(e); }
    void dropEvent(QDropEvent* e) override {
        if (!accepts || !e->mimeData() || !accepts(*e->mimeData())) { e->ignore(); return; }
        e->setDropAction(Qt::CopyAction);
        e->accept();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const bool shift = (e->modifiers() & Qt::ShiftModifier) != 0;
#else
        const bool shift = (e->keyboardModifiers() & Qt::ShiftModifier) != 0;
#endif
        if (dropped) dropped(*e->mimeData(), shift);
    }

private:
    template <class E> void take(E* e) {
        if (accepts && e->mimeData() && accepts(*e->mimeData())) { e->setDropAction(Qt::CopyAction); e->accept(); }
        else e->ignore();
    }
};

/// A wait cursor for the length of an engine call (the engines have no progress callback).
struct WaitCursor {
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
};

QString slot_title(int slot) { return slot == 0 ? QObject::tr("SD card") : QObject::tr("Second SD card (D:)"); }

}  // namespace

SdManagerDialog::SdManagerDialog(Emulator& emu, sm::SdManagerSession& session, const sm::Remembered& remembered,
                                 const QString& host_fallback, QWidget* parent)
    : QDialog(parent), emu_(emu), session_(session), host_fallback_(host_fallback) {
    setWindowTitle(tr("SD Card File Manager"));
    setObjectName(QStringLiteral("sdcard_manager_dialog"));
    resize(900, 520);

    // ---- the card pane ----
    slot_combo_ = new QComboBox;
    slot_combo_->setObjectName(QStringLiteral("slot_combo"));
    read_only_box_ = new QCheckBox(tr("Read-only"));
    read_only_box_->setObjectName(QStringLiteral("read_only_box"));
    card_path_ = new QLabel;
    card_path_->setObjectName(QStringLiteral("card_path"));
    card_path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    card_note_ = new QLabel;
    card_note_->setObjectName(QStringLiteral("card_note"));
    card_note_->setWordWrap(true);
    auto* card_up = new QToolButton;
    card_up->setObjectName(QStringLiteral("card_up"));
    card_up->setText(tr("Up"));
    auto* card_mode = new QToolButton;
    card_mode->setObjectName(QStringLiteral("card_mode_button"));
    card_mode->setText(tr("Icons"));
    card_mode->setCheckable(true);

    card_model_ = new SdCardModel(this);
    auto* card_view = new DropView;
    card_view_ = card_view;
    card_view_->setObjectName(QStringLiteral("card_view"));
    card_view_->setModel(card_model_);
    card_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    card_view_->setDragEnabled(true);
    card_view_->setDragDropMode(QAbstractItemView::DragDrop);
    card_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    card_view_->setAcceptDrops(true);
    card_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    card_view->accepts = [](const QMimeData& m) { return m.hasUrls(); };
    card_view->dropped = [this](const QMimeData& m, bool shift) { drop_on_card(m, shift); };

    auto* left = new QVBoxLayout;
    auto* left_top = new QHBoxLayout;
    left_top->addWidget(slot_combo_, 1);
    left_top->addWidget(read_only_box_);
    auto* left_nav = new QHBoxLayout;
    left_nav->addWidget(card_up);
    left_nav->addWidget(card_path_, 1);
    left_nav->addWidget(card_mode);
    left->addLayout(left_top);
    left->addLayout(left_nav);
    left->addWidget(card_note_);
    left->addWidget(card_view_, 1);

    // ---- the host pane ----
    host_model_ = new QFileSystemModel(this);
    host_model_->setReadOnly(true);   // Qt's own drop and rename never act behind the manager's back
    host_path_ = new QLabel;
    host_path_->setObjectName(QStringLiteral("host_path"));
    host_path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* host_up = new QToolButton;
    host_up->setObjectName(QStringLiteral("host_up"));
    host_up->setText(tr("Up"));
    auto* host_mode = new QToolButton;
    host_mode->setObjectName(QStringLiteral("host_mode_button"));
    host_mode->setText(tr("Icons"));
    host_mode->setCheckable(true);
    auto* host_view = new DropView;
    host_view_ = host_view;
    host_view_->setObjectName(QStringLiteral("host_view"));
    host_view_->setModel(host_model_);
    host_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    host_view_->setDragEnabled(true);
    host_view_->setAcceptDrops(true);
    host_view_->setDragDropMode(QAbstractItemView::DragDrop);
    host_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);   // never Qt's inline rename: it would bypass the held-file check
    host_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    host_view->accepts = [](const QMimeData& m) { return m.hasFormat(QString::fromLatin1(SdCardModel::kMime)); };
    host_view->dropped = [this](const QMimeData& m, bool shift) { drop_on_host(m, shift); };

    auto* right = new QVBoxLayout;
    auto* right_nav = new QHBoxLayout;
    right_nav->addWidget(host_up);
    right_nav->addWidget(host_path_, 1);
    right_nav->addWidget(host_mode);
    right->addLayout(right_nav);
    right->addWidget(host_view_, 1);

    // ---- between them ----
    to_host_button_ = new QPushButton(tr("Copy >"));
    to_host_button_->setObjectName(QStringLiteral("copy_to_host"));
    to_card_button_ = new QPushButton(tr("< Copy"));
    to_card_button_->setObjectName(QStringLiteral("copy_to_card"));
    auto* middle = new QVBoxLayout;
    middle->addStretch(1);
    middle->addWidget(to_host_button_);
    middle->addWidget(to_card_button_);
    middle->addStretch(1);

    auto* panes = new QHBoxLayout;
    panes->addLayout(left, 1);
    panes->addLayout(middle);
    panes->addLayout(right, 1);
    auto* close = new QPushButton(tr("Close"));
    close->setObjectName(QStringLiteral("close_button"));
    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    bottom->addWidget(close);
    auto* outer = new QVBoxLayout(this);
    outer->addLayout(panes, 1);
    outer->addLayout(bottom);

    // ---- actions: one set per pane, in its context menu and on its keys ----
    auto add_actions = [this](QListView* view, Side side, const QString& prefix) {
        auto make = [&](const char* name, const QString& text, const QKeySequence& key, std::function<void()> fn) {
            auto* a = new QAction(text, view);
            a->setObjectName(prefix + QLatin1String(name));
            if (!key.isEmpty()) {
                a->setShortcut(key);
                a->setShortcutContext(Qt::WidgetShortcut);
            }
            view->addAction(a);
            connect(a, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
            return a;
        };
        make("copy", side == Side::Card ? tr("Copy to the host folder") : tr("Copy to the card"), QKeySequence(),
             [this, side] { if (side == Side::Card) copy_to_host(); else copy_to_card(); });
        make("rename", tr("Rename"), QKeySequence(Qt::Key_F2), [this, side] { rename_selected(side); });
        make("delete", tr("Delete"), QKeySequence(Qt::Key_Delete), [this, side] { delete_selected(side); });
        make("new_folder", tr("New Folder"), QKeySequence(), [this, side] { new_folder(side); });
    };
    add_actions(card_view_, Side::Card, QStringLiteral("card_"));
    add_actions(host_view_, Side::Host, QStringLiteral("host_"));
    for (QListView* v : {card_view_, host_view_})
        connect(v, &QWidget::customContextMenuRequested, this, [this, v](const QPoint& pos) {
            QMenu menu(v);
            for (QAction* a : v->actions()) menu.addAction(a);
            update_actions();
            menu.exec(v->viewport()->mapToGlobal(pos));
        });

    // ---- signals ----
    connect(slot_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (resetting_ || i < 0) return;
        show_slot(slot_combo_->itemData(i).toInt(), QStringLiteral("/"));
    });
    connect(read_only_box_, &QCheckBox::toggled, this, [this] { update_actions(); });
    connect(to_host_button_, &QPushButton::clicked, this, [this] { copy_to_host(); });
    connect(to_card_button_, &QPushButton::clicked, this, [this] { copy_to_card(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(card_up, &QToolButton::clicked, this, [this] { navigate_card(SdCardModel::parent_path(card_dir_)); });
    connect(host_up, &QToolButton::clicked, this, [this] {
        const QString parent = QFileInfo(host_dir_).dir().absolutePath();
        if (!parent.isEmpty() && parent != host_dir_) navigate_host(parent);
    });
    connect(card_mode, &QToolButton::toggled, this, [this](bool on) { set_view_mode(card_view_, on); });
    connect(host_mode, &QToolButton::toggled, this, [this](bool on) { set_view_mode(host_view_, on); });
    connect(card_view_, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& i) {
        const QString p = card_model_->path_of(i.row());
        if (!p.isEmpty() && card_model_->is_dir(i.row())) navigate_card(p);
    });
    connect(host_view_, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& i) {
        if (host_model_->isDir(i)) navigate_host(host_model_->filePath(i));
    });
    connect(card_view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { update_actions(); });
    connect(host_view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { update_actions(); });

    // ---- what to open on: THE EMULATOR WINS ----
    const sm::Resolved r = sm::resolve_remembered(remembered, sm::card_slots(emu_), host_fallback.toStdString(),
                                                  QDir::homePath().toStdString());
    shown_slots_ = sm::card_slots(emu_);
    apply_slots();
    show_slot(r.slot, SdCardModel::card_text(r.card_dir));
    navigate_host(QString::fromStdString(r.host_dir));

    poll_ = new QTimer(this);
    poll_->setInterval(500);
    connect(poll_, &QTimer::timeout, this, [this] { check_card_change(); });
    poll_->start();
}

SdManagerDialog::~SdManagerDialog() = default;

bool SdManagerDialog::deliver_event_for_test(Side side, QEvent& ev) {
    return static_cast<DropView*>(side == Side::Card ? card_view_ : host_view_)->deliver(&ev);
}

void SdManagerDialog::stop_polling_for_test() {
    if (poll_) poll_->stop();
}

sm::Remembered SdManagerDialog::remembered() const {
    sm::Remembered r;
    if (slot_ >= 0) r.card_image = shown_slots_[static_cast<size_t>(slot_)].image;
    r.card_dir = sm::encode_card_dir(SdCardModel::card_bytes(card_dir_));
    r.host_dir = host_dir_.toStdString();
    return r;
}

// ---------------------------------------------------------------------------
// What the panes show
// ---------------------------------------------------------------------------

void SdManagerDialog::apply_slots() {
    resetting_ = true;
    slot_combo_->clear();
    for (int s = 0; s < 2; ++s) {
        const sm::SlotInfo& si = shown_slots_[static_cast<size_t>(s)];
        QString text = slot_title(s);
        if (si.mounted) text += QStringLiteral(" - ") + QFileInfo(QString::fromStdString(si.image)).fileName();
        else text += QStringLiteral(" - ") + tr("empty");
        slot_combo_->addItem(text, s);
        if (auto* m = qobject_cast<QStandardItemModel*>(slot_combo_->model()))
            if (QStandardItem* item = m->item(s)) item->setEnabled(si.mounted);
    }
    if (slot_ >= 0) slot_combo_->setCurrentIndex(slot_);
    resetting_ = false;
}

void SdManagerDialog::show_slot(int slot, const QString& dir) {
    shown_slots_ = sm::card_slots(emu_);
    slot_ = slot;
    card_dir_ = dir.isEmpty() ? QStringLiteral("/") : dir;
    apply_slots();
    if (slot_ < 0 || !shown_slots_[static_cast<size_t>(slot_)].mounted) {
        slot_ = -1;
        card_model_->set_location(QString(), QStringLiteral("/"));
        card_path_->setText(QString());
        card_note_->setText(tr("No SD card in this slot."));
    } else {
        const sm::SlotInfo& si = shown_slots_[static_cast<size_t>(slot_)];
        bool ok = card_model_->set_location(QString::fromStdString(si.image), card_dir_);
        if (!ok && card_dir_ != QLatin1String("/")) {   // the folder is gone (a guest or a remote client removed it): the root
            card_dir_ = QStringLiteral("/");
            ok = card_model_->set_location(QString::fromStdString(si.image), card_dir_);
        }
        card_path_->setText(SdCardModel::card_shown(card_dir_));
        card_note_->setText(ok ? (si.writable ? QString() : QString::fromStdString(si.read_only_reason))
                               : tr("The card cannot be read: %1").arg(card_model_->error()));
    }
    update_actions();
}

void SdManagerDialog::navigate_card(const QString& dir) {
    if (slot_ < 0) return;
    show_slot(slot_, dir);
}

void SdManagerDialog::navigate_host(const QString& dir) {
    host_dir_ = dir;
    if (dir.isEmpty()) {
        host_path_->setText(QString());
        return;
    }
    host_model_->setRootPath(dir);
    host_view_->setRootIndex(host_model_->index(dir));
    host_path_->setText(QDir::toNativeSeparators(dir));
    update_actions();
}

void SdManagerDialog::refresh() {
    show_slot(slot_, card_dir_);
    if (!host_dir_.isEmpty()) navigate_host(host_dir_);
}

bool SdManagerDialog::check_card_change() {
    const std::array<sm::SlotInfo, 2> now = sm::card_slots(emu_);
    bool changed = false;
    if (slot_ >= 0) {
        const sm::SlotInfo& a = shown_slots_[static_cast<size_t>(slot_)];
        const sm::SlotInfo& b = now[static_cast<size_t>(slot_)];
        changed = a.mounted != b.mounted || (b.mounted && !sdcard::same_image_file(a.image, b.image));
    } else {
        changed = std::any_of(now.begin(), now.end(), [](const sm::SlotInfo& s) { return s.mounted; });
    }
    if (!changed) {
        // The OTHER slot may have gained or lost a card, and writability can change (an RZX recording started): keep the
        // selector, the labels and the actions true to the machine.
        if (std::any_of(now.begin(), now.end(), [this](const sm::SlotInfo& s) {
                const sm::SlotInfo& o = shown_slots_[static_cast<size_t>(s.slot)];
                return s.mounted != o.mounted || (s.mounted && !sdcard::same_image_file(s.image, o.image)) ||
                       s.writable != o.writable || s.read_only_reason != o.read_only_reason;
            }))
            show_slot(slot_, card_dir_);   // re-reads the slots and relists
        return false;
    }
    // The emulator wins: the card pane resets to what the machine has now, at its ROOT.
    int next = -1;
    if (slot_ >= 0 && now[static_cast<size_t>(slot_)].mounted) next = slot_;
    else for (const sm::SlotInfo& s : now) if (s.mounted) { next = s.slot; break; }
    show_slot(next, QStringLiteral("/"));
    return true;
}

void SdManagerDialog::set_view_mode(QListView* v, bool icons) {
    v->setViewMode(icons ? QListView::IconMode : QListView::ListMode);
    if (icons) {
        v->setResizeMode(QListView::Adjust);
        v->setMovement(QListView::Static);
    }
}

QStringList SdManagerDialog::selected_card_paths() const {
    QStringList out;
    for (const QModelIndex& i : card_view_->selectionModel()->selectedIndexes()) {
        const QString p = card_model_->path_of(i.row());   // "" = shown, never acted on
        if (!p.isEmpty() && !out.contains(p)) out << p;
    }
    return out;
}

QStringList SdManagerDialog::selected_host_paths() const {
    QStringList out;
    for (const QModelIndex& i : host_view_->selectionModel()->selectedIndexes()) {
        const QString p = host_model_->filePath(i);
        if (!p.isEmpty() && !out.contains(p)) out << p;
    }
    return out;
}

void SdManagerDialog::update_actions() {
    const bool card_there = slot_ >= 0;
    const bool writable = card_there && shown_slots_[static_cast<size_t>(slot_)].writable;
    if (writable) {
        read_only_box_->setEnabled(true);
        read_only_box_->setToolTip(QString());
    } else {
        // Forced on and greyed, with the reason as its tooltip (and the note under the selector).
        QSignalBlocker block(read_only_box_);
        read_only_box_->setChecked(true);
        read_only_box_->setEnabled(false);
        read_only_box_->setToolTip(card_there ? QString::fromStdString(shown_slots_[static_cast<size_t>(slot_)].read_only_reason)
                                              : tr("No SD card in this slot."));
    }
    const bool card_write = writable && !read_only_box_->isChecked();
    const bool card_sel = !card_view_->selectionModel()->selectedIndexes().isEmpty();
    const bool host_sel = !host_view_->selectionModel()->selectedIndexes().isEmpty();
    to_host_button_->setEnabled(card_there && card_sel);
    to_card_button_->setEnabled(card_write && host_sel);
    auto en = [this](const char* n, bool on) { if (QAction* a = findChild<QAction*>(QString::fromLatin1(n))) a->setEnabled(on); };
    en("card_copy", card_there && card_sel);
    en("card_rename", card_write && card_sel);
    en("card_delete", card_write && card_sel);
    en("card_new_folder", card_write);
    en("host_copy", card_write && host_sel);
    en("host_rename", host_sel);
    en("host_delete", host_sel);
    en("host_new_folder", !host_dir_.isEmpty());
}

// ---------------------------------------------------------------------------
// Questions
// ---------------------------------------------------------------------------

QMessageBox::StandardButton SdManagerDialog::box(const QString& title, const QString& text,
                                                 QMessageBox::StandardButtons buttons, QMessageBox::Icon icon) {
    ++boxes_shown_;
    if (box_hook_) return box_hook_(title, text, buttons);
    QMessageBox mb(icon, title, text, buttons, this);
    return static_cast<QMessageBox::StandardButton>(mb.exec());
}

bool SdManagerDialog::ask_text(const QString& title, const QString& label, const QString& initial, QString& out) {
    ++boxes_shown_;
    if (text_hook_) {
        const std::optional<QString> r = text_hook_(title, label, initial);
        if (!r) return false;
        out = *r;
        return true;
    }
    bool ok = false;
    out = QInputDialog::getText(this, title, label, QLineEdit::Normal, initial, &ok);
    return ok;
}

void SdManagerDialog::show_report(const QString& what, const sm::TransferReport& r) {
    if (r.failed.empty() && r.error.empty() && !r.cancelled && r.not_attempted.empty()) return;
    auto list = [](const std::vector<std::string>& v) {
        QStringList l;
        for (const std::string& s : v) l << QString::fromStdString(s);
        return l.join(QStringLiteral("\n  "));
    };
    QString text = what + QStringLiteral("\n\n");
    if (!r.done.empty()) text += tr("Done (%1):\n  %2\n").arg(r.done.size()).arg(list(r.done));
    if (!r.failed.empty()) text += tr("Failed: %1\n  %2\n").arg(QString::fromStdString(r.failed), QString::fromStdString(r.error));
    else if (!r.error.empty()) text += QString::fromStdString(r.error) + QLatin1Char('\n');
    if (r.cancelled) text += tr("Stopped.\n");
    if (!r.skipped.empty()) text += tr("Skipped (%1):\n  %2\n").arg(r.skipped.size()).arg(list(r.skipped));
    if (!r.not_attempted.empty()) text += tr("Not attempted (%1):\n  %2\n").arg(r.not_attempted.size()).arg(list(r.not_attempted));
    box(tr("SD Card File Manager"), text.trimmed(), QMessageBox::Ok, QMessageBox::Warning);
}

bool SdManagerDialog::card_gate() {
    return session_.before_card_write([this] {
        return box(tr("Change the SD card?"),
                   tr("Changing the card restarts the machine (a hard reset) when this window closes, so that it sees the "
                      "new contents. Make the change?"),
                   QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
    });
}

bool SdManagerDialog::trash(const QString& path, QString& err) {
    if (trash_hook_) return trash_hook_(path, err);
    if (QFile::moveToTrash(path)) return true;
    err = tr("the system trash did not take it");
    return false;
}

sm::TransferHooks SdManagerDialog::hooks() {
    sm::TransferHooks h;
    h.ask_overwrite = [this](const std::string& dest, bool dest_is_dir) {
        const QString d = QString::fromStdString(dest);
        const QString text = dest_is_dir
            ? tr("'%1' already exists. Merge into it, replacing the files with the same names?").arg(d)
            : tr("'%1' already exists. Replace it?").arg(d);
        const QMessageBox::StandardButton b = box(tr("Already exists"), text,
            QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll | QMessageBox::Cancel);
        switch (b) {
        case QMessageBox::Yes:      return sm::OverwriteAnswer::Replace;
        case QMessageBox::YesToAll: return sm::OverwriteAnswer::ReplaceAll;
        case QMessageBox::No:       return sm::OverwriteAnswer::Skip;
        case QMessageBox::NoToAll:  return sm::OverwriteAnswer::SkipAll;
        default:                    return sm::OverwriteAnswer::Cancel;
        }
    };
    h.trash_host = [this](const std::string& path, std::string& err) {
        QString e;
        const bool ok = trash(QString::fromStdString(path), e);
        if (!ok) err = e.toStdString();
        return ok;
    };
    h.before_card_write = [this] { return card_gate(); };
    return h;
}

bool SdManagerDialog::card_now(bool write, CardNow& out) {
    // The card may have changed under the window: show the truth before acting on a stale view.
    if (check_card_change()) {
        box(tr("SD Card File Manager"), tr("The SD card in this slot has changed; the view was reset to the card the machine has now."),
            QMessageBox::Ok, QMessageBox::Information);
        return false;
    }
    if (slot_ < 0) return false;
    const sm::SlotInfo& si = shown_slots_[static_cast<size_t>(slot_)];
    out.image = si.image;
    out.read_only_reason.clear();
    if (write) {
        if (!si.writable) out.read_only_reason = si.read_only_reason;
        else if (read_only_box_->isChecked()) out.read_only_reason = tr("the Read-only box is ticked").toStdString();
        if (!out.read_only_reason.empty()) {
            box(tr("SD Card File Manager"), tr("The card is read-only: %1").arg(QString::fromStdString(out.read_only_reason)),
                QMessageBox::Ok, QMessageBox::Information);
            return false;
        }
    }
    return true;
}

void SdManagerDialog::host_op_context(CardNow& out) {
    check_card_change();   // keep the view true; a host operation needs no card
    out.image = slot_ >= 0 ? shown_slots_[static_cast<size_t>(slot_)].image : std::string();
    out.read_only_reason.clear();
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

bool SdManagerDialog::run_and_report(const sm::TransferRequest& req) {
    sm::TransferHooks h = hooks();
    sm::TransferReport rep;
    {
        WaitCursor wait;
        rep = sm::run_transfer(req, h);
    }
    show_report(tr("Copy"), rep);
    refresh();
    return rep.failed.empty() && rep.error.empty() && !rep.cancelled;
}

void SdManagerDialog::copy_to_host() {
    CardNow c;
    if (!card_now(false, c) || host_dir_.isEmpty()) return;
    const QStringList paths = selected_card_paths();
    if (paths.isEmpty()) return;
    sm::TransferRequest req;
    req.from = sm::Side::Card;
    req.image = c.image;
    req.dest_dir = host_dir_.toStdString();
    for (const QString& p : paths) req.items.push_back({SdCardModel::card_bytes(p), SdCardModel::card_shown(p).toStdString(), false});
    req.held_files = sm::held_host_files(emu_);
    run_and_report(req);
}

void SdManagerDialog::copy_to_card() {
    CardNow c;
    if (!card_now(true, c)) return;
    const QStringList paths = selected_host_paths();
    if (paths.isEmpty()) return;
    sm::TransferRequest req;
    req.from = sm::Side::Host;
    req.image = c.image;
    req.dest_dir = SdCardModel::card_bytes(card_dir_);
    for (const QString& p : paths) req.items.push_back({p.toStdString(), QFileInfo(p).fileName().toStdString(), false});
    req.held_files = sm::held_host_files(emu_);
    run_and_report(req);
}

bool SdManagerDialog::drop_on_host(const QMimeData& data, bool move) {
    CardNow c;
    if (!card_now(move, c) || host_dir_.isEmpty()) return false;
    QString image;
    QStringList paths;
    if (!SdCardModel::decode_items(data.data(QString::fromLatin1(SdCardModel::kMime)), image, paths) ||
        !sdcard::same_image_file(image.toStdString(), c.image)) {
        box(tr("SD Card File Manager"), tr("That drop does not come from the card shown here."), QMessageBox::Ok, QMessageBox::Information);
        return false;
    }
    sm::TransferRequest req;
    req.from = sm::Side::Card;
    req.image = c.image;
    req.dest_dir = host_dir_.toStdString();
    req.move = move;
    for (const QString& p : paths) req.items.push_back({SdCardModel::card_bytes(p), SdCardModel::card_shown(p).toStdString(), false});
    req.held_files = sm::held_host_files(emu_);
    return run_and_report(req);
}

bool SdManagerDialog::drop_on_card(const QMimeData& data, bool move) {
    CardNow c;
    if (!card_now(true, c)) return false;
    QStringList paths;
    for (const QUrl& u : data.urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (paths.isEmpty()) return false;
    sm::TransferRequest req;
    req.from = sm::Side::Host;
    req.image = c.image;
    req.dest_dir = SdCardModel::card_bytes(card_dir_);
    req.move = move;
    for (const QString& p : paths) req.items.push_back({p.toStdString(), QFileInfo(p).fileName().toStdString(), false});
    req.held_files = sm::held_host_files(emu_);
    return run_and_report(req);
}

void SdManagerDialog::rename_selected(Side side) {
    CardNow c;
    if (side == Side::Card ? !card_now(true, c) : (host_op_context(c), false)) return;
    const QStringList sel = side == Side::Card ? selected_card_paths() : selected_host_paths();
    if (sel.size() != 1) {
        box(tr("SD Card File Manager"), tr("Select one item to rename."), QMessageBox::Ok, QMessageBox::Information);
        return;
    }
    const QString old_name = side == Side::Card ? sel.first().section(QLatin1Char('/'), -1) : QFileInfo(sel.first()).fileName();
    QString shown = old_name;
    if (side == Side::Card)
        for (int r = 0; r < card_model_->rowCount(); ++r)
            if (card_model_->path_of(r) == sel.first())
                shown = QString::fromStdString(sdcard::cardops::display_name(card_model_->entry(r)));
    QString name;
    if (!ask_text(tr("Rename"), tr("New name:"), shown, name) || name.isEmpty() || name == shown) return;
    if (side == Side::Card) {
        if (!card_gate()) return;
        std::string err;
        const sdcard::FileAddStatus st = sdcard::cardops::rename(c.image, SdCardModel::card_bytes(sel.first()), name.toStdString(), err);
        if (st != sdcard::FileAddStatus::Ok)
            box(tr("SD Card File Manager"), tr("Cannot rename: %1").arg(QString::fromStdString(err)), QMessageBox::Ok, QMessageBox::Warning);
    } else {
        const QFileInfo fi(sel.first());
        const QString target = fi.dir().filePath(name);
        QString why;
        if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name == QLatin1String(".") || name == QLatin1String(".."))
            why = tr("'%1' is not a usable name").arg(name);
        else if ((QFileInfo::exists(target) || QFileInfo(target).isSymLink()) &&
                 QFileInfo(target).canonicalFilePath() != fi.canonicalFilePath())   // a case-only rename is the same file
            why = tr("'%1' already exists").arg(name);
        else if (sm::is_held(sel.first().toStdString(), sm::held_host_files(emu_)))
            why = tr("it is a file the emulator is using (a card image, an SD-writes file, a recording...)");
        else if (!QFile::rename(sel.first(), target))
            why = tr("the system refused the new name");
        if (!why.isEmpty())
            box(tr("SD Card File Manager"), tr("Cannot rename: %1").arg(why), QMessageBox::Ok, QMessageBox::Warning);
    }
    refresh();
}

void SdManagerDialog::delete_selected(Side side) {
    CardNow c;
    if (side == Side::Card ? !card_now(true, c) : (host_op_context(c), false)) return;
    const QStringList sel = side == Side::Card ? selected_card_paths() : selected_host_paths();
    if (sel.isEmpty()) return;
    const QString what = sel.size() == 1 ? tr("'%1'").arg(side == Side::Card ? SdCardModel::card_shown(sel.first()) : sel.first())
                                         : tr("%1 items").arg(sel.size());
    const QString text = side == Side::Card
        ? tr("Delete %1 from the card? This cannot be undone.").arg(what)
        : tr("Move %1 to the trash?").arg(what);
    if (box(tr("Delete"), text, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    std::vector<std::string> paths;
    for (const QString& p : sel) paths.push_back(side == Side::Card ? SdCardModel::card_bytes(p) : p.toStdString());
    sm::TransferHooks h = hooks();
    sm::TransferReport rep;
    {
        WaitCursor wait;
        rep = sm::delete_items(side == Side::Card ? sm::Side::Card : sm::Side::Host, c.image, paths,
                               sm::held_host_files(emu_), h, c.read_only_reason);
    }
    show_report(tr("Delete"), rep);
    refresh();
}

void SdManagerDialog::new_folder(Side side) {
    CardNow c;
    if (side == Side::Card ? !card_now(true, c) : (host_op_context(c), false)) return;
    QString name;
    if (!ask_text(tr("New Folder"), tr("Name of the new folder:"), tr("New Folder"), name) || name.isEmpty()) return;
    sm::TransferHooks h = hooks();
    const sm::TransferReport rep = side == Side::Card
        ? sm::make_folder(sm::Side::Card, c.image, SdCardModel::card_bytes(card_dir_), name.toStdString(), h, c.read_only_reason)
        : sm::make_folder(sm::Side::Host, c.image, host_dir_.toStdString(), name.toStdString(), h);
    show_report(tr("New Folder"), rep);
    refresh();
}
