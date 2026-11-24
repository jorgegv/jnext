#pragma once

#include <QAbstractListModel>
#include <QString>

#include <string>
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
/// Dragging out: the model exports `kMime`, one line per item (the image, then each card path), every line
/// %XX-escaped so a name with a newline or a high byte survives. The host pane accepts only that type.
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

    /// The entry of `row`, and its card path ("" for an entry with no open_name).
    const sdcard::cardops::CardEntry& entry(int row) const { return entries_[static_cast<size_t>(row)]; }
    QString path_of(int row) const;
    bool is_dir(int row) const { return entries_[static_cast<size_t>(row)].dir; }

    /// `dir` + "/" + `name` as a card path.
    static QString child_path(const QString& dir, const QString& name);
    /// The parent of a card path ("/" for a top-level entry or the root).
    static QString parent_path(const QString& dir);

    /// Encode / decode the drag payload (image first, then the paths). Decode refuses a malformed one.
    static QByteArray encode_items(const QString& image, const QStringList& paths);
    static bool decode_items(const QByteArray& data, QString& image, QStringList& paths);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }

private:
    QString image_, dir_ = QStringLiteral("/"), error_;
    std::vector<sdcard::cardops::CardEntry> entries_;
};
