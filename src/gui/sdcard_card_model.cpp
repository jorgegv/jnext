#include "gui/sdcard_card_model.h"

#include <QFileIconProvider>
#include <QIcon>
#include <QLocale>
#include <QMimeData>

#include "core/sdcard_carry.h"

namespace ops = sdcard::cardops;

SdCardModel::SdCardModel(QObject* parent) : QAbstractListModel(parent) {}

bool SdCardModel::set_location(const QString& image, const QString& dir) {
    image_ = image;
    dir_ = dir.isEmpty() ? QStringLiteral("/") : dir;
    return reload();
}

bool SdCardModel::reload() {
    beginResetModel();
    entries_.clear();
    error_.clear();
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
    if (row < 0 || row >= static_cast<int>(entries_.size())) return {};
    const ops::CardEntry& e = entries_[static_cast<size_t>(row)];
    if (e.open_name.empty()) return {};   // shown, never acted on
    return child_path(dir_, card_text(e.open_name));
}

QByteArray SdCardModel::encode_items(const QString& image, const QStringList& paths) {
    QByteArray out = QByteArray::fromStdString(sdcard::carry::escape_name(image.toStdString(), false));
    for (const QString& p : paths) {
        out += '\n';
        out += QByteArray::fromStdString(sdcard::carry::escape_name(card_bytes(p), false));
    }
    return out;
}

bool SdCardModel::decode_items(const QByteArray& data, QString& image, QStringList& paths) {
    paths.clear();
    const QList<QByteArray> lines = data.split('\n');
    if (lines.size() < 2) return false;
    for (int i = 0; i < lines.size(); ++i) {
        std::string raw;
        if (!sdcard::carry::unescape_name(lines[i].toStdString(), raw)) return false;
        if (i == 0) image = QString::fromStdString(raw);
        else paths << card_text(raw);
    }
    return !image.isEmpty();
}

int SdCardModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant SdCardModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(entries_.size())) return {};
    const ops::CardEntry& e = entries_[static_cast<size_t>(index.row())];
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
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!path_of(index.row()).isEmpty()) f |= Qt::ItemIsDragEnabled;
    return f;
}

QStringList SdCardModel::mimeTypes() const { return {QString::fromLatin1(kMime)}; }

QMimeData* SdCardModel::mimeData(const QModelIndexList& indexes) const {
    QStringList paths;
    for (const QModelIndex& i : indexes) {
        const QString p = path_of(i.row());
        if (!p.isEmpty() && !paths.contains(p)) paths << p;
    }
    if (paths.isEmpty()) return nullptr;
    auto* m = new QMimeData;
    m->setData(QString::fromLatin1(kMime), encode_items(image_, paths));
    return m;
}
