#pragma once

#include <QWidget>
#include <QTableWidget>
#include "debug/breakpoints.h"

class Emulator;
class SymbolTable;
class DisasmPanel;

/// Panel showing all active breakpoints (execute + data) with add/edit/remove.
class BreakpointPanel : public QWidget {
    Q_OBJECT
public:
    explicit BreakpointPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Rebuild the table from current breakpoint state.
    void refresh();

    /// Set symbol table for address-to-name resolution.
    void set_symbol_table(SymbolTable* st) { symbol_table_ = st; }

    /// Set disasm panel to trigger repaint on breakpoint changes.
    void set_disasm_panel(DisasmPanel* dp) { disasm_panel_ = dp; }

public slots:
    void on_add();
    void on_edit();
    void on_remove();

private:
    bool show_bp_dialog(const QString& title, uint16_t& addr, int& type_index);
    static QString type_name(int type_index);

    Emulator* emulator_;
    SymbolTable* symbol_table_ = nullptr;
    DisasmPanel* disasm_panel_ = nullptr;
    QTableWidget* table_ = nullptr;

    // Unified list: type_index 0=Execute, 1=Read, 2=Write, 3=Read/Write
    struct BpEntry {
        uint16_t addr;
        int type_index; // 0=Exec, 1=Read, 2=Write, 3=R+W
    };
    std::vector<BpEntry> entries_;

    void rebuild_entries();
};
