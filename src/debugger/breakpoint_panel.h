#pragma once

#include <QPointer>
#include <QWidget>
#include <QTableWidget>

#include <vector>

#include "debugger/breakpoint_model.h"

class QCheckBox;
class SymbolTable;

/// Panel showing all active breakpoints (execute + data) with add/edit/remove.
///
/// GH #278 WP4c — the breakpoints are the debugger BACKEND's subscriptions,
/// reached through the GUI's BreakpointModel. The panel lists EVERY owner's
/// (REQ-qt-13d): this GUI's own rows are editable, another client's (a script,
/// a remote debugger) are listed read-only, their Type naming the client.
///
/// GH #220 — it follows the model's `changed()` notification, so no mutation
/// route has to remember to repaint it.
///
/// GH #225 — it is also where a breakpoint is enabled and disabled: a per-row
/// Enabled checkbox in column 0, and a master switch next to the buttons. Both
/// write to the model and then do nothing else; the model notifies, this
/// panel and the disassembly gutter redraw themselves. The panel holds NO
/// enable state of its own, which is what makes the two controls compose —
/// there is only ever one answer to "is this breakpoint enabled", and it lives
/// in the backend.
class BreakpointPanel : public QWidget {
    Q_OBJECT
public:
    explicit BreakpointPanel(QWidget* parent = nullptr);

    /// GH #278 WP4c — the model this panel lists and edits. Until one is set the
    /// panel is empty and its buttons do nothing. Set by DebuggerManager when
    /// it builds the window; the panel follows the model's changed() from then
    /// on. Held weakly: the window may outlive the manager that owns the model.
    void set_model(BreakpointModel* model);

    /// Rebuild the table from current breakpoint state.
    void refresh();

    /// Set symbol table for address-to-name resolution.
    void set_symbol_table(SymbolTable* st) { symbol_table_ = st; }

public slots:
    void on_add();
    void on_edit();
    void on_remove();

private:
    bool show_bp_dialog(const QString& title, uint16_t& addr, int& type_index);

    /// GH #225 — apply the check state of the Enabled cell `row` to the model.
    void apply_enabled_cell(int row, bool enabled);

    QPointer<BreakpointModel> model_;
    SymbolTable* symbol_table_ = nullptr;
    QTableWidget* table_ = nullptr;
    QCheckBox* master_check_ = nullptr;

    /// True while refresh() is writing the widgets FROM the model, so the
    /// itemChanged / toggled handlers do not write the same values back into
    /// the model and re-enter refresh() through the observer.
    bool updating_ = false;

    // The rows as last listed (BreakpointModel::Row: type is the Add dialog's
    // index 0..5 — Execute, Read, Write, Read/Write, IO Read, IO Write — or -1
    // for a subscription it cannot express; `enabled` is the row's own flag,
    // not the master; `own` marks this GUI's editable rows).
    std::vector<BreakpointModel::Row> entries_;

    void rebuild_entries();
};
