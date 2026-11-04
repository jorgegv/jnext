#pragma once

#include <QMetaType>
#include <QStringList>
#include <QWidget>
#include <QTableWidget>

#include "debug/long_address.h"
#include <vector>
#include <cstdint>
#include <string>

namespace jnext { namespace dbg { class Debugger; } }

/// GH #435 - a C variable a watch follows: the facade path (it round-trips through `read_c_value`), and for a parameter or local the
/// function it lives in (name and CDB module, so two static functions of one name stay apart). `function` empty: a global, a file static
/// or a static local, read in any frame by a path no local can shadow.
struct CVarWatch {
    std::string path;
    std::string function;
    std::string module;
};
Q_DECLARE_METATYPE(CVarWatch)

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

    /// GH #360 - Friendly or Physical in the Memory column.
    void set_address_style(jnext::dbg::AddressStyle st) { style_ = st; }

    /// GH #435 - watch a C variable. Its row is read only while paused (`set_paused`): a running machine leaves the text of the last stop.
    void add_c_watch(const CVarWatch& w);
    void set_paused(bool paused) { paused_ = paused; }

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
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;

    enum WatchType { BYTE = 0, WORD = 1, LONG = 2 };

    struct WatchEntry {
        uint16_t addr = 0;
        std::string label;
        WatchType type = BYTE;
        enum class Kind { Memory, CVariable } kind = Kind::Memory;
        CVarWatch cvar;   // CVariable
        QStringList shown = QStringList({QString(), QString(), QString(), QString(), QString()});   // CVariable: the columns as last read
        bool refused = false;   // CVariable: the Value is text, not a value
    };
    void refresh_c_row(int row, const WatchEntry& w);
    bool paused_ = false;

    std::vector<WatchEntry> watches_;
};
