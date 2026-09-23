#pragma once

#include <QWidget>
#include <QTableWidget>
#include "debug/breakpoints.h"

class Emulator;
class QCheckBox;
class SymbolTable;

/// Panel showing all active breakpoints (execute + data) with add/edit/remove.
///
/// GH #220 — it OBSERVES the BreakpointSet (both halves: it lists execute and
/// data breakpoints alike), so no mutation route has to remember to repaint it.
///
/// GH #225 — it is also where a breakpoint is enabled and disabled: a per-row
/// Enabled checkbox in column 0, and a master switch next to the buttons. Both
/// write to the BreakpointSet and then do nothing else; the set notifies, this
/// panel and the disassembly gutter redraw themselves. The panel holds NO
/// enable state of its own, which is what makes the two controls compose —
/// there is only ever one answer to "is this breakpoint enabled", and it lives
/// in src/debug/.
class BreakpointPanel : public QWidget {
    Q_OBJECT
public:
    explicit BreakpointPanel(Emulator* emulator, QWidget* parent = nullptr);
    ~BreakpointPanel() override;

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
    static QString type_name(int type_index);

    /// GH #225 — apply the check state of the Enabled cell `row` to the set.
    void apply_enabled_cell(int row, bool enabled);

    Emulator* emulator_;
    SymbolTable* symbol_table_ = nullptr;
    QTableWidget* table_ = nullptr;
    QCheckBox* master_check_ = nullptr;
    BreakpointSet::ObserverId observer_ = 0;

    /// True while refresh() is writing the widgets FROM the model, so the
    /// itemChanged / toggled handlers do not write the same values back into
    /// the model and re-enter refresh() through the observer.
    bool updating_ = false;

    // Unified list: type_index 0=Execute, 1=Read, 2=Write, 3=Read/Write
    struct BpEntry {
        uint16_t addr;
        int type_index; // 0=Exec, 1=Read, 2=Write, 3=R+W
        bool enabled;   // GH #225 — this breakpoint's own flag, not the master
    };
    std::vector<BpEntry> entries_;

    void rebuild_entries();
};
