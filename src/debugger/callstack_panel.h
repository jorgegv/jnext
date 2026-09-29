#pragma once

#include <QWidget>
#include <QTableWidget>

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
    void set_symbol_table(SymbolTable* st) { symbol_table_ = st; }

    QSize sizeHint() const override { return QSize(400, 300); }

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;
    SymbolTable* symbol_table_ = nullptr;
    bool paused_ = false;
    QTableWidget* table_ = nullptr;
};
