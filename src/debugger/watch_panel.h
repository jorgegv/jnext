#pragma once

#include <QWidget>
#include <QTableWidget>
#include <vector>
#include <cstdint>
#include <string>

namespace jnext { namespace dbg { class Debugger; } }

/// Watch panel showing memory values at watched addresses.
///
/// A watch is a PEEK, not an event (qt-frontend.md §3.5): the list is this
/// panel's own, and its values are read through the debugger backend's
/// `peek(MemSpace::Cpu)` (GH #278 WP4c) — the address as the CPU sees it,
/// overlays included, and non-perturbing: no watchpoint fires on it and the
/// +3 floating-bus latch does not move (F1).
class WatchPanel : public QWidget {
    Q_OBJECT
public:
    explicit WatchPanel(QWidget* parent = nullptr);

    /// The backend the values are read through. Until one is set the Value
    /// column stays as it is. Set by DebuggerManager when it builds the window.
    void set_backend(const jnext::dbg::Debugger* dbg) { dbg_ = dbg; }

    /// Refresh all watch values from current emulator memory state.
    void refresh();

    /// Add a watch entry.
    void add_watch(uint16_t addr, const std::string& label, int type = 0);

    /// Remove the currently selected watch.
    void remove_selected();

    /// Get number of watches.
    int watch_count() const { return static_cast<int>(watches_.size()); }

public slots:
    /// Show dialog to add a new watch.
    void on_add_watch();

    /// Show dialog to edit the currently selected watch.
    void on_edit_watch();

private:
    void update_table();
    bool show_watch_dialog(const QString& title, uint16_t& addr,
                           std::string& label, int& type);

    const jnext::dbg::Debugger* dbg_ = nullptr;
    QTableWidget* table_ = nullptr;

    enum WatchType { BYTE = 0, WORD = 1, LONG = 2 };

    struct WatchEntry {
        uint16_t addr;
        std::string label;
        WatchType type;
    };

    std::vector<WatchEntry> watches_;
};
