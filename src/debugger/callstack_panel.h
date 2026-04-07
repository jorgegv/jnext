#pragma once

#include <QWidget>
#include <QTableWidget>

class Emulator;
class SymbolTable;

/// Debugger panel showing the call stack (CALL/RST/INT tracking).
/// Displays most recent call at the top.
class CallStackPanel : public QWidget {
    Q_OBJECT
public:
    explicit CallStackPanel(Emulator* emulator, QWidget* parent = nullptr);

    void refresh();
    void set_paused(bool paused);
    void set_symbol_table(SymbolTable* st) { symbol_table_ = st; }

    QSize sizeHint() const override { return QSize(400, 300); }

private:
    void create_ui();

    Emulator* emulator_;
    SymbolTable* symbol_table_ = nullptr;
    bool paused_ = false;
    QTableWidget* table_ = nullptr;
};
