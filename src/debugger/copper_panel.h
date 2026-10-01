#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QTableWidget>

namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing decoded copper instructions and current PC.
class CopperPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4b);
    ///             null shows nothing.
    explicit CopperPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Update display with current copper state.
    void refresh();

    QSize sizeHint() const override { return QSize(350, 400); }

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;
    QCheckBox* enable_check_ = nullptr;
    QLabel* pc_label_ = nullptr;
    QTableWidget* table_ = nullptr;
};
