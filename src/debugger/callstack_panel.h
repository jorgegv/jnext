#pragma once

#include <QWidget>
#include <QTableWidget>

#include "debug/long_address.h"

class SymbolTable;
namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing the call stack (CALL/RST/INT tracking).
/// Displays most recent call at the top.
class CallStackPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4a);
    ///             null shows nothing.
    explicit CallStackPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    void refresh();
    void set_paused(bool paused);

    /// GH #360 - Friendly or Physical in the two memory columns.
    void set_address_style(jnext::dbg::AddressStyle st) { style_ = st; }
    void set_symbol_table(const SymbolTable* st) { symbol_table_ = st; }

    QSize sizeHint() const override { return QSize(400, 300); }

    /// GH #435 - select row `row` (0 = the newest call) without telling anyone; -1 clears the selection.
    void select_row(int row);

signals:
    /// The USER chose row `row` (0 = the newest call). A refresh or `select_row` does not emit it.
    void call_selected(int row);

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;
    const SymbolTable* symbol_table_ = nullptr;
    bool paused_ = false;
    bool programmatic_ = false;   ///< a refresh or select_row() is changing the selection: not the user's choice
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;
    QTableWidget* table_ = nullptr;
};
