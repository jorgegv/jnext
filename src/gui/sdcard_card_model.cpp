#include "gui/sdcard_card_model.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QPalette>
#include <QIcon>
#include <QLocale>
#include <QMimeData>

#include "core/sdcard_carry.h"
#include "platform/sdcard_manager.h"
#include "toolbar_icons/toolbar_icons.h"

namespace ops = sdcard::cardops;

SdCardModel::SdCardModel(QObject* parent) : QAbstractListModel(parent) {}

bool SdCardModel::set_location(const QString& image, const QString& dir) {
    image_ = image;
    dir_ = dir.isEmpty() ? QStringLiteral("/") : dir;
    return reload();
}

bool SdCardModel::reload() {
    ++reloads_;
    beginResetModel();
    entries_.clear();
    error_.clear();
    has_up_ = !image_.isEmpty() && dir_ != QLatin1String("/");
    bool ok = true;
    if (!image_.isEmpty()) {
        std::string err;
        const sdcard::FileAddStatus st =
            ops::list_dir(image_.toStdString(), card_bytes(dir_), entries_, err);
        if (st != sdcard::FileAddStatus::Ok) {
            entries_.clear();
            error_ = QString::fromStdString(err.empty() ? "the card cannot be read" : err);
            ok = false;
        }
    }
    endResetModel();
    return ok;
}

QString SdCardModel::card_shown(const QString& path) {
    return QString::fromStdString(sdcard::carry::escape_name(card_bytes(path), false));
}

QString SdCardModel::child_path(const QString& dir, const QString& name) {
    return (dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/')) + name;
}

QString SdCardModel::parent_path(const QString& dir) {
    QString d = dir;
    while (d.size() > 1 && d.endsWith(QLatin1Char('/'))) d.chop(1);
    const int slash = d.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : d.left(slash);
}

QString SdCardModel::path_of(int row) const {
    if (is_up_row(row)) return {};   // the up row is not an entry
    const int n = row - (has_up_ ? 1 : 0);
    if (n < 0 || n >= static_cast<int>(entries_.size())) return {};
    const ops::CardEntry& e = entries_[static_cast<size_t>(n)];
    if (e.open_name.empty()) return {};   // shown, never acted on
    return child_path(dir_, card_text(e.open_name));
}

QString SdCardModel::stamp_of(int row) const {
    if (is_up_row(row) || row < 0 || row >= rowCount()) return {};
    return card_text(sdmanager::entry_stamp(entry(row)));
}

int SdCardModel::row_of_path(const QString& path) const {
    for (int r = 0; r < rowCount(); ++r)
        if (path_of(r) == path) return r;
    return -1;
}

QByteArray SdCardModel::encode_items(const QString& image, const QStringList& paths, const QStringList& stamps) {
    QByteArray out = QByteArray::fromStdString(sdcard::carry::escape_name(image.toStdString(), false));
    for (int i = 0; i < paths.size(); ++i) {
        out += '\n';
        out += QByteArray::fromStdString(sdcard::carry::escape_name(card_bytes(paths[i]), false));
        if (i < stamps.size() && !stamps[i].isEmpty()) {
            out += '\t';   // escape_name writes a tab as %09, so it cannot occur inside either field
            out += QByteArray::fromStdString(sdcard::carry::escape_name(card_bytes(stamps[i]), false));
        }
    }
    return out;
}

bool SdCardModel::decode_items(const QByteArray& data, QString& image, QStringList& paths, QStringList* stamps) {
    paths.clear();
    if (stamps) stamps->clear();
    const QList<QByteArray> lines = data.split('\n');
    if (lines.size() < 2) return false;
    for (int i = 0; i < lines.size(); ++i) {
        const int tab = lines[i].indexOf('\t');
        std::string raw, raw_stamp;
        if (!sdcard::carry::unescape_name((tab < 0 ? lines[i] : lines[i].left(tab)).toStdString(), raw)) return false;
        if (tab >= 0 && i > 0 && !sdcard::carry::unescape_name(lines[i].mid(tab + 1).toStdString(), raw_stamp)) return false;
        if (i == 0) image = QString::fromStdString(raw);
        else {
            paths << card_text(raw);
            if (stamps) *stamps << card_text(raw_stamp);
        }
    }
    return !image.isEmpty();
}

QIcon SdCardModel::up_icon() {
    static QIcon icon;
    static QPalette seen;
    if (icon.isNull() || seen != QGuiApplication::palette()) {   // follows a light/dark switch
        seen = QGuiApplication::palette();
        icon = toolbar_icons::icon(seen, QStringLiteral("up-dir"));
    }
    return icon;
}

int SdCardModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size()) + (has_up_ ? 1 : 0);
}

QVariant SdCardModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    if (is_up_row(index.row())) {
        switch (role) {
        case Qt::DisplayRole: return up_text();
        case Qt::DecorationRole: return up_icon();
        case Qt::ToolTipRole: return tr("Up to the parent folder");
        default: return {};
        }
    }
    const ops::CardEntry& e = entry(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return QString::fromStdString(ops::display_name(e));
    case Qt::DecorationRole: {
        static const QFileIconProvider icons;
        return icons.icon(e.dir ? QFileIconProvider::Folder : QFileIconProvider::File);
    }
    case Qt::ToolTipRole: {
        QString tip = QString::fromStdString(ops::display_name(e));
        if (!e.dir) tip += QStringLiteral("\n") + QLocale().formattedDataSize(e.size);
        if (e.open_name.empty()) tip += QStringLiteral("\n") + tr("this entry cannot be addressed: it is not acted on");
        return tip;
    }
    default:
        return {};
    }
}

Qt::ItemFlags SdCardModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (is_up_row(index.row())) return Qt::ItemIsEnabled;   // never selected, so never the target of an action
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!path_of(index.row()).isEmpty()) f |= Qt::ItemIsDragEnabled;
    return f;
}

QStringList SdCardModel::mimeTypes() const { return {QString::fromLatin1(kMime)}; }

QMimeData* SdCardModel::mimeData(const QModelIndexList& indexes) const {
    QStringList paths, stamps;
    for (const QModelIndex& i : indexes) {
        const QString p = path_of(i.row());
        if (!p.isEmpty() && !paths.contains(p)) {
            paths << p;
            stamps << stamp_of(i.row());
        }
    }
    if (paths.isEmpty()) return nullptr;
    auto* m = new QMimeData;
    m->setData(QString::fromLatin1(kMime), encode_items(image_, paths, stamps));
    return m;
}

// ---------------------------------------------------------------------------
// SdHostModel
// ---------------------------------------------------------------------------

SdHostModel::SdHostModel(QFileSystemModel* source, QObject* parent) : QAbstractListModel(parent), source_(source) {
    // The listing changes under the view (a file made by a copy, the sort after the folder was read): forward each change
    // as the same change of this model, so the selection survives it.
    connect(source_, &QAbstractItemModel::rowsAboutToBeInserted, this, [this](const QModelIndex& p, int first, int last) {
        if (p == QModelIndex(root_)) beginInsertRows(QModelIndex(), first + (has_up_ ? 1 : 0), last + (has_up_ ? 1 : 0));
    });
    connect(source_, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex& p) {
        if (p == QModelIndex(root_)) endInsertRows();
    });
    connect(source_, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this](const QModelIndex& p, int first, int last) {
        if (p == QModelIndex(root_)) beginRemoveRows(QModelIndex(), first + (has_up_ ? 1 : 0), last + (has_up_ ? 1 : 0));
    });
    connect(source_, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex& p) {
        if (p == QModelIndex(root_)) endRemoveRows();
    });
    connect(source_, &QAbstractItemModel::layoutAboutToBeChanged, this, [this] {
        emit layoutAboutToBeChanged();
        kept_.clear();
        for (const QModelIndex& i : persistentIndexList()) kept_.push_back({QPersistentModelIndex(i), file_path(i)});
    });
    connect(source_, &QAbstractItemModel::layoutChanged, this, [this] {
        QModelIndexList from, to;
        for (const auto& k : kept_) {
            from << QModelIndex(k.first);
            to << (k.second.isEmpty() ? (has_up_ && k.first.row() == 0 ? index(0, 0) : QModelIndex()) : index_of_path(k.second));
        }
        changePersistentIndexList(from, to);
        kept_.clear();
        emit layoutChanged();
    });
    connect(source_, &QAbstractItemModel::modelReset, this, [this] { beginResetModel(); endResetModel(); });
    connect(source_, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& a, const QModelIndex& b) {
        if (a.parent() != QModelIndex(root_) || a.column() != 0) return;
        emit dataChanged(index(a.row() + (has_up_ ? 1 : 0), 0), index(b.row() + (has_up_ ? 1 : 0), 0));
    });
}

void SdHostModel::set_dir(const QString& dir) {
    if (dir == dir_ && root_.isValid()) return;   // the same folder: the listing follows itself, and the selection stays
    beginResetModel();
    dir_ = dir;
    if (dir.isEmpty()) {
        root_ = QModelIndex();
        has_up_ = false;
    } else {
        source_->setRootPath(dir);
        root_ = source_->index(dir);
        has_up_ = !QFileInfo(dir).isRoot();
    }
    endResetModel();
}

QModelIndex SdHostModel::to_source(const QModelIndex& i) const {
    if (!i.isValid() || is_up_row(i.row()) || !root_.isValid()) return {};
    return source_->index(i.row() - (has_up_ ? 1 : 0), 0, QModelIndex(root_));
}

QString SdHostModel::file_path(const QModelIndex& i) const {
    const QModelIndex s = to_source(i);
    return s.isValid() ? source_->filePath(s) : QString();
}

bool SdHostModel::is_dir(const QModelIndex& i) const {
    const QModelIndex s = to_source(i);
    return s.isValid() && source_->isDir(s);
}

QModelIndex SdHostModel::index_of_path(const QString& path) const {
    const QModelIndex s = source_->index(path);
    if (!s.isValid() || s.parent() != QModelIndex(root_)) return {};
    return index(s.row() + (has_up_ ? 1 : 0), 0);
}

int SdHostModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !root_.isValid()) return 0;
    return source_->rowCount(QModelIndex(root_)) + (has_up_ ? 1 : 0);
}

QVariant SdHostModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    if (is_up_row(index.row())) {
        switch (role) {
        case Qt::DisplayRole: return SdCardModel::up_text();
        case Qt::DecorationRole: return SdCardModel::up_icon();
        case Qt::ToolTipRole: return tr("Up to the parent folder");
        default: return {};
        }
    }
    const QModelIndex s = to_source(index);
    if (!s.isValid()) return {};
    // The same generic file and folder icons as the card pane (never the platform's per-file icons, which an offscreen
    // or minimal desktop does not have): the two panes look alike.
    if (role == Qt::DecorationRole) {
        static const QFileIconProvider icons;
        return icons.icon(source_->isDir(s) ? QFileIconProvider::Folder : QFileIconProvider::File);
    }
    return source_->data(s, role);
}

Qt::ItemFlags SdHostModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (is_up_row(index.row())) return Qt::ItemIsEnabled;
    const QModelIndex s = to_source(index);
    return s.isValid() ? (source_->flags(s) & ~Qt::ItemIsEditable) : Qt::NoItemFlags;
}

QStringList SdHostModel::mimeTypes() const { return source_->mimeTypes(); }

QMimeData* SdHostModel::mimeData(const QModelIndexList& indexes) const {
    QModelIndexList mapped;
    for (const QModelIndex& i : indexes) {
        const QModelIndex s = to_source(i);
        if (s.isValid()) mapped << s;
    }
    return mapped.isEmpty() ? nullptr : source_->mimeData(mapped);
}

Qt::DropActions SdHostModel::supportedDragActions() const { return source_->supportedDragActions(); }
