#pragma once

#include <QWidget>
#include <QTableWidget>

namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing the Z80 stack contents as 16-bit values.
/// Displays from SP upward: address, 16-bit word, hi byte, lo byte.
class StackPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4a);
    ///             null shows nothing.
    explicit StackPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    void refresh();
    void set_paused(bool paused);

    QSize sizeHint() const override { return QSize(400, 300); }

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;
    bool paused_ = false;
    QTableWidget* table_ = nullptr;

    static constexpr int STACK_ROWS = 24;  // show 24 stack entries
};
