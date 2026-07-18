#pragma once

#include <QWidget>
#include <QTableWidget>

class Emulator;
class SymbolTable;
class SourceMap;

/// Debugger panel showing the call stack (CALL/RST/INT tracking).
/// Displays most recent call at the top.
class CallStackPanel : public QWidget {
    Q_OBJECT
public:
    explicit CallStackPanel(Emulator* emulator, QWidget* parent = nullptr);

    void refresh();
    void set_paused(bool paused);
    void set_symbol_table(SymbolTable* st) { symbol_table_ = st; }
    void set_source_map(SourceMap* source_map) { source_map_ = source_map; }

    QSize sizeHint() const override { return QSize(400, 300); }

private:
    void create_ui();

    Emulator* emulator_;
    SymbolTable* symbol_table_ = nullptr;
    SourceMap* source_map_ = nullptr;
    bool paused_ = false;
    QTableWidget* table_ = nullptr;
};
