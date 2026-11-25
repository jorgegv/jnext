#pragma once

#include <QAbstractListModel>
#include <QIcon>
#include <QString>

class QFileSystemModel;

#include <string>
#include <utility>
#include <vector>

#include "core/sdcard_card_ops.h"

/// GH #558 - the card pane's model: ONE directory of the card in a slot, as sdcard::cardops::list_dir gives it
/// (directories first, then by name), shown with the platform's generic folder and file icons.
///
/// The model lists; it decides nothing. Every card path it hands out is built from the `open_name` of a listed
/// entry (the contract of cardops: never a typed or displayed long name), and an entry whose `open_name` is empty
/// is SHOWN but has no path, so no action can be aimed at it. Names are shown through
/// sdcard::cardops::display_name (printable ASCII, every other byte and '%' as %XX), so a hostile name cannot
/// carry a control character into a widget.
///
/// Dragging out: the model exports `kMime`, one line per item (the image as UTF-8, then each card path as its bytes),
/// every line %XX-escaped so a name with a newline or a high byte survives. The host pane accepts only that type.
class SdCardModel : public QAbstractListModel {
    Q_OBJECT
public:
    static constexpr const char* kMime = "application/x-jnext-sdcard-items";

    explicit SdCardModel(QObject* parent = nullptr);

    /// Show `dir` (a card path of open_names, "/" = root) of `image`; "" = no card. Reloads. False with error()
    /// set when the card or the directory cannot be read (the model is then empty).
    bool set_location(const QString& image, const QString& dir);
    bool reload();

    const QString& image() const { return image_; }
    const QString& dir() const { return dir_; }
    const QString& error() const { return error_; }
    /// How many times the folder has been listed (set_location and reload): a test observable.
    int reloads() const { return reloads_; }

    /// Every folder but the root starts with an "up" row (the arrow icon, text ".."): it is shown and activated like a
    /// folder, but it is not an entry of the card: no path, not selectable, not draggable, never a target.
    bool has_up_row() const { return has_up_; }
    bool is_up_row(int row) const { return has_up_ && row == 0; }
    /// The shared look of the up row of both panes (the up-dir icon in the application palette, the text, the tip).
    static QIcon up_icon();
    static QString up_text() { return QStringLiteral(".."); }

    /// The entry of `row` (not the up row), and its card path ("" for the up row and for an entry with no open_name).
    const sdcard::cardops::CardEntry& entry(int row) const { return entries_[static_cast<size_t>(row - (has_up_ ? 1 : 0))]; }
    QString path_of(int row) const;
    /// The listed identity of a row (sdmanager::entry_stamp), carried as Latin-1 like a card path; "" for the up row.
    QString stamp_of(int row) const;
    /// The row of a card path in the current listing, or -1.
    int row_of_path(const QString& path) const;
    bool is_dir(int row) const { return is_up_row(row) || entry(row).dir; }

    /// CARD PATHS ARE BYTES (code page 437, FatFs's OEM encoding), and a QString is Unicode: UTF-8 would turn a high byte
    /// into U+FFFD and no open_name would match it again. So every card path or name that crosses into Qt is carried as
    /// LATIN-1 (one code point per byte, lossless in both directions), and every card path that goes back to the engines
    /// goes through card_bytes(). Host paths are UTF-8 as everywhere else; display strings are escaped ASCII.
    static QString card_text(const std::string& bytes) { return QString::fromLatin1(bytes.data(), static_cast<int>(bytes.size())); }
    static std::string card_bytes(const QString& s) { return s.toLatin1().toStdString(); }
    /// What to SHOW of a card path (printable ASCII, every other byte %XX).
    static QString card_shown(const QString& path);

    /// `dir` + "/" + `name` as a card path.
    static QString child_path(const QString& dir, const QString& name);
    /// The parent of a card path ("/" for a top-level entry or the root).
    static QString parent_path(const QString& dir);

    /// Encode / decode the drag payload (image first, then the paths). Decode refuses a malformed one.
    /// `stamps` (parallel to `paths`, or empty) ride in the payload after each path (a tab, then the escaped stamp), so a
    /// drop can tell an entry that changed since the drag began. A payload without them decodes as before.
    static QByteArray encode_items(const QString& image, const QStringList& paths, const QStringList& stamps = {});
    static bool decode_items(const QByteArray& data, QString& image, QStringList& paths, QStringList* stamps = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }

private:
    QString image_, dir_ = QStringLiteral("/"), error_;
    std::vector<sdcard::cardops::CardEntry> entries_;
    bool has_up_ = false;
    int reloads_ = 0;
};

/// GH #558 - the host pane's model: ONE folder of a QFileSystemModel as a flat list, with the same "up" row first in
/// every folder but the filesystem root. The QFileSystemModel keeps the listing, the sort, the icons and the watching;
/// this model only adds the row and maps the others (any change of the folder resets it: the selection is not kept
/// across a change on disk, as the listing itself is rebuilt).
class SdHostModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit SdHostModel(QFileSystemModel* source, QObject* parent = nullptr);

    /// Show `dir` ("" = nothing).
    void set_dir(const QString& dir);
    const QString& dir() const { return dir_; }
    bool has_up_row() const { return has_up_; }
    bool is_up_row(int row) const { return has_up_ && row == 0; }
    QFileSystemModel* source() const { return source_; }
    /// The path and kind of a row ("" / false for the up row), and the proxy index of a source path.
    QString file_path(const QModelIndex& i) const;
    bool is_dir(const QModelIndex& i) const;
    QModelIndex index_of_path(const QString& path) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override;

private:
    QModelIndex to_source(const QModelIndex& i) const;
    QFileSystemModel* source_;
    QString dir_;
    QPersistentModelIndex root_;
    bool has_up_ = false;
    std::vector<std::pair<QPersistentModelIndex, QString>> kept_;   // the rows kept across a re-sort, by path
};
