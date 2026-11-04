#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QSet>
#include <QTreeWidget>

#include <cstdint>
#include <vector>

#include "debug/debugger.h"
#include "debug/long_address.h"
#include "debugger/watch_panel.h"

/// GH #435 (epic #353) - the Variables tab: the C variables of one call frame, and the globals and statics, as a tree.
///
/// Every value, type, refusal and note is the debugger facade's text (`c_locals`, `c_globals`, `c_children`): this panel
/// formats no number. It reads only while paused (like the Call Stack), and a tree item's children are read when it is
/// first expanded. A refresh re-reads the expanded items and updates their texts in place, so the selection, the scroll
/// position and the expansion stay. A value is edited in place (GH #437): double-click its Value cell while paused.
class VariablesPanel : public QWidget {
    Q_OBJECT
public:
    explicit VariablesPanel(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// GH #437 - who an edit is logged as (the MUTATE line's `by`), as the Memory panel has it. Set by `DebuggerManager::set_panels_client()`.
    void set_client(jnext::dbg::ClientId by) { client_ = by; }


    /// Redraw from the backend. Returns at once unless paused. A refresh at a new master cycle (a new stop) selects frame 0;
    /// one at the same cycle (an idle tick, a style switch) keeps the chosen frame.
    void refresh();
    void set_paused(bool paused) { paused_ = paused; }

    /// GH #360 - Friendly or Physical in the Address column. The next refresh re-spells it.
    void set_address_style(jnext::dbg::AddressStyle st) { style_ = st; }

    /// Show frame `k` (an index into `c_frames()`, newest first). Reads its locals at once; emits `frame_changed` when it changes.
    void select_frame(int k);
    int selected_frame() const { return frame_; }

    /// The Call Stack panel's row for the selected frame: row i is frame i + 1 (the Call Stack lists the tracked CALLs newest
    /// first, and has no row for frame 0 or for a frame found through the saved-frame-pointer chain), else -1. The one place that
    /// mapping is written, with `frame_of_call_row`.
    int call_stack_row() const;
    static int frame_of_call_row(int row) { return row + 1; }

    QSize sizeHint() const override { return QSize(400, 300); }

    /// GH #435 - pin `item` (a variable, member, element or pointee; not a group, range or message) as a C watch: emits `watch_requested`.
    /// A Locals item is bound to the frame's function; a global, file static or static local is not (it is read by a path no local can shadow,
    /// spelled as `c_globals()` spells it). Refused (false, nothing added) while the machine runs: the tab shows the last stop, and a binding
    /// taken from the live machine would not be what the user pointed at.
    /// This is the right-click menu's one action.
    bool watch_item(QTreeWidgetItem* item);

    /// What a tree item carries (the tests read it): its kind, the facade path, a range's first element.
    enum { KindGroup = 0, KindVar = 1, KindRange = 2, KindMessage = 3 };
    enum { RoleKind = Qt::UserRole, RolePath, RoleFirst, RoleKey, RoleExpandable, RoleRowKind, RoleFile, RoleEditText };

    /// GH #437 - open the Value editor of `item` (as a double-click does). False, and no editor, unless the machine is paused and the item
    /// is editable (a variable, member, element or pointee whose facade row is ok and not an aggregate).
    bool edit_item(QTreeWidgetItem* item);
    /// GH #437 - what the editor of `item` commits: write `text` to the item's C object in the selected frame. An unchanged text writes nothing.
    /// A refusal shows `<path>: <why>` in the message label; the cell keeps its value (the editor never writes the model). Public for the delegate.
    void commit_edit(QTreeWidgetItem* item, const QString& text);

signals:
    /// The user (or `select_frame`) chose another frame.
    void frame_changed(int k);
    void watch_requested(const CVarWatch& w);

private:
    // What one tree row is, before it becomes an item.
    struct Node {
        QString key;                 ///< unique among its siblings, stable across refreshes: a path, a range, a file
        int kind = 0;                ///< KindVar / KindRange / KindMessage / KindGroup
        QString path;                ///< KindVar, KindRange: the facade path (a range: its parent's)
        int first = 0;               ///< KindRange: the first element
        QStringList cols;            ///< Name, Type, Value, Address
        QString tip;
        QString file;                ///< KindVar: the source file of the variable (`CValueRow::file`)
        int row_kind = 0;            ///< KindVar: the facade row's `CValueRow::Kind` (a child has its root's)
        bool placeholder = false;    ///< the Value is a refusal: drawn in the placeholder colour
        bool expandable = false;     ///< an aggregate or a pointer: gets the indicator, children on expand
        bool editable = false;       ///< GH #437: an ok row that is not an aggregate
        QString edit_text;           ///< what the editor holds: the first word of the facade's value (`120 'x'` is `120`)
        std::vector<Node> kids;      ///< KindGroup: its rows, built eagerly
    };
    void on_context_menu(const QPoint& pos);
    Node node_of(const jnext::dbg::Debugger::CValueRow& row, const QString& key_prefix, const QString& parent_name) const;
    Node message(const QString& text) const;
    std::vector<Node> locals_nodes() const;
    std::vector<Node> globals_nodes() const;
    std::vector<Node> child_nodes(QTreeWidgetItem* item) const;
    QString address_text(const jnext::dbg::Debugger::CWhere& w) const;
    QString key_of(QTreeWidgetItem* item) const { return item->data(0, RoleKey).toString(); }

    void sync(QTreeWidgetItem* parent, const std::vector<Node>& nodes);
    void apply(QTreeWidgetItem* item, const Node& n);
    void rebuild_frames();
    void on_expanded(QTreeWidgetItem* item);
    void on_collapsed(QTreeWidgetItem* item);

    jnext::dbg::Debugger* dbg_;
    jnext::dbg::ClientId client_ = jnext::dbg::CLIENT_NONE;
    bool paused_ = false;
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;
    int frame_ = 0;
    bool have_cycle_ = false;
    uint64_t last_cycle_ = 0;
    bool syncing_ = false;                    ///< the panel itself is expanding items: not the user's doing
    QSet<QString> expanded_;                  ///< keys of the items the user has open
    QString locals_scope_;                    ///< the function whose locals are shown ("" for none): part of every Locals key
    std::array<jnext::dbg::SlotInfo, 8> slots_{};

    QLabel* status_ = nullptr;
    QLabel* message_ = nullptr;               ///< GH #437: the reason of the last refused edit, above the tree
    QComboBox* frames_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QTreeWidgetItem* locals_ = nullptr;
    QTreeWidgetItem* globals_ = nullptr;
};
