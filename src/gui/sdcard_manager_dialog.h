#pragma once

#include <QDialog>
#include <QMessageBox>
#include <QString>

#include <array>
#include <functional>
#include <optional>

#include "platform/sdcard_manager.h"

class Emulator;
class QCheckBox;
class QComboBox;
class QFileSystemModel;
class QLabel;
class QListView;
class QMimeData;
class QModelIndex;
class QPushButton;
class QTimer;
class QToolButton;
class SdCardModel;

/// GH #558 - the SD card file manager window: two panes, the card in a slot on the left and a host folder on the
/// right, with Copy, Rename, Delete and New Folder between and inside them, and drag and drop (Shift = move).
///
/// A VIEW. Every decision is in src/platform/sdcard_manager.* (the pause and reset session, what a copy or a move
/// does, what is held and must not be touched, what the window opens on), and every card operation is
/// sdcard::cardops, add_to_image or extract_from_image. This class only draws, asks the questions and refreshes.
/// What it adds to the contract:
///   * the machine is paused and a card change ends in a hard reset by the SdManagerSession it is given (opened and
///     closed by the owner, MainWindow; this window calls before_card_write(), which asks the first-change warning);
///   * EVERY operation re-reads sdmanager::card_slots() and sdmanager::held_host_files() at the moment it runs: the card in
///     the slot may have changed under the window (a remote debugger), a recording may have started;
///   * the emulator wins: the card pane shows what the machine has mounted. When the card in the shown slot changes
///     while the window is open the pane resets to the new card's ROOT (check_card_change(), polled by a timer).
///
/// Native widgets only: QListView on SdCardModel and QFileSystemModel, QFileIconProvider icons, QMessageBox and
/// QInputDialog. Read-only cards (card_slots(): a card jnext cannot write, --sdcard-readonly, an RZX recording, ...) and the
/// "Read-only" box disable every card-writing action.
class SdManagerDialog : public QDialog {
    Q_OBJECT
public:
    /// `host_fallback` is the Load directory (the host pane's second choice); `session` must be open for any card write.
    SdManagerDialog(Emulator& emu, sdmanager::SdManagerSession& session, const sdmanager::Remembered& remembered,
                    const QString& host_fallback, QWidget* parent = nullptr);
    ~SdManagerDialog() override;

    /// What to remember: the shown card, its folder (stored form) and the host folder.
    sdmanager::Remembered remembered() const;

    /// The shown slot (-1 = none) and folders.
    int slot() const { return slot_; }
    const QString& card_dir() const { return card_dir_; }
    const QString& host_dir() const { return host_dir_; }

    /// Reload both panes.
    void refresh();

    /// Has the card in the shown slot changed under the window? If so, show the first mounted card's root. Called by a
    /// timer while the window is open, and by every operation before it acts. True when the view was reset.
    bool check_card_change();

    enum class Side { Card, Host };

    // ---- the actions (also behind the buttons, menus and keys) ----
    void copy_to_host();                     ///< the card pane's selection to the host folder
    void copy_to_card();                     ///< the host pane's selection to the card folder
    void rename_selected(Side side);
    void delete_selected(Side side);
    void new_folder(Side side);
    /// A drop: the card MIME on the host pane (a copy off the card), local files on the card pane (a copy onto it);
    /// `move` = Shift was held. False = refused (the reason was shown).
    bool drop_on_host(const QMimeData& data, bool move);
    bool drop_on_card(const QMimeData& data, bool move);

    // ---- seams for the tests (nothing else uses them) ----
    using BoxHook = std::function<QMessageBox::StandardButton(const QString& title, const QString& text,
                                                              QMessageBox::StandardButtons buttons)>;
    using TextHook = std::function<std::optional<QString>(const QString& title, const QString& label, const QString& initial)>;
    using TrashHook = std::function<bool(const QString& path, QString& err)>;
    /// Every message box goes through `h` (and is counted) instead of a real box.
    void set_box_hook_for_test(BoxHook h) { box_hook_ = std::move(h); }
    void set_text_hook_for_test(TextHook h) { text_hook_ = std::move(h); }
    /// Replaces QFile::moveToTrash (a headless session has no trash).
    void set_trash_hook_for_test(TrashHook h) { trash_hook_ = std::move(h); }
    int boxes_shown_for_test() const { return boxes_shown_; }
    /// Hand a real QDropEvent to a pane's own event handler (the drop path, Shift detection included).
    bool deliver_event_for_test(Side side, class QEvent& ev);
    /// Stop the polling timer (the tests call check_card_change() themselves).
    void stop_polling_for_test();

private:
    bool ask_text(const QString& title, const QString& label, const QString& initial, QString& out);
    QMessageBox::StandardButton box(const QString& title, const QString& text, QMessageBox::StandardButtons buttons,
                                    QMessageBox::Icon icon = QMessageBox::Question);
    void show_report(const QString& what, const sdmanager::TransferReport& r);
    bool card_gate();                         ///< before_card_write with the first-change warning
    sdmanager::TransferHooks hooks();
    /// The current state of the shown slot, re-read; false (and a box) when it cannot be used. `write`: the
    /// operation changes the card, so a read-only card refuses.
    struct CardNow { std::string image; std::string read_only_reason; };
    bool card_now(bool write, CardNow& out);
    /// The same for an operation on the HOST pane: it needs no card, only the image to keep out of the way.
    void host_op_context(CardNow& out);
    void apply_slots();                       ///< combo entries, read-only box and action states
    void show_slot(int slot, const QString& dir);
    void navigate_card(const QString& dir);
    void navigate_host(const QString& dir);
    void update_actions();
    void set_view_mode(QListView* v, bool icons);
    QStringList selected_card_paths() const;
    QStringList selected_host_paths() const;
    bool run_and_report(const sdmanager::TransferRequest& req);
    bool trash(const QString& path, QString& err);

    Emulator& emu_;
    sdmanager::SdManagerSession& session_;
    QString host_fallback_;
    int slot_ = -1;
    QString card_dir_ = QStringLiteral("/");
    QString host_dir_;
    std::array<sdmanager::SlotInfo, 2> shown_slots_{};

    QComboBox* slot_combo_ = nullptr;
    QCheckBox* read_only_box_ = nullptr;
    QLabel* card_path_ = nullptr;
    QLabel* card_note_ = nullptr;
    QLabel* host_path_ = nullptr;
    QListView* card_view_ = nullptr;
    QListView* host_view_ = nullptr;
    SdCardModel* card_model_ = nullptr;
    QFileSystemModel* host_model_ = nullptr;
    QPushButton* to_host_button_ = nullptr;
    QPushButton* to_card_button_ = nullptr;
    QTimer* poll_ = nullptr;

    BoxHook box_hook_;
    TextHook text_hook_;
    TrashHook trash_hook_;
    int boxes_shown_ = 0;
    bool resetting_ = false;
};
