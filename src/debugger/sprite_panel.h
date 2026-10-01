#pragma once

#include <QWidget>
#include <QTableWidget>

namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing all 128 hardware sprites in a table.
class SpritePanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4b);
    ///             null shows nothing.
    explicit SpritePanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Update display with current sprite state.
    void refresh();

    QSize sizeHint() const override { return QSize(400, 500); }

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;
    QTableWidget* table_ = nullptr;
};
