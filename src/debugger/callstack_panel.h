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

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;
    const SymbolTable* symbol_table_ = nullptr;
    bool paused_ = false;
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;
    QTableWidget* table_ = nullptr;
};
