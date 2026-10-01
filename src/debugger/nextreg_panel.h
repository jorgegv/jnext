#pragma once

#include <QWidget>
#include <QTableWidget>

#include "debug/events.h"   // jnext::dbg::ClientId

namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing all 256 NextREG registers with editable values.
class NextRegPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend (GH #278 WP4b): reads are its
    ///             `nextreg_peek()`, an edit its `nextreg_write()`. Null shows
    ///             nothing and writes nothing.
    explicit NextRegPanel(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// The client an edit is attributed to (the backend's MUTATE line). The
    /// manager sets the debugger window's client while the window is open;
    /// CLIENT_NONE otherwise.
    void set_client(jnext::dbg::ClientId by) { client_ = by; }

    /// Update display with current NextREG state.
    void refresh();

    QSize sizeHint() const override { return QSize(400, 500); }

private:
    void create_ui();
    void populate_names();

    jnext::dbg::Debugger* dbg_;
    jnext::dbg::ClientId  client_ = jnext::dbg::CLIENT_NONE;
    QTableWidget* table_ = nullptr;

    /// Static register name lookup (populated once at construction).
    static const char* reg_name(uint8_t reg);
};
