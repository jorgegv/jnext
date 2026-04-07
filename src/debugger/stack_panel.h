#pragma once

#include <QWidget>
#include <QTableWidget>

class Emulator;

/// Debugger panel showing the Z80 stack contents as 16-bit values.
/// Displays from SP upward: address, 16-bit word, hi byte, lo byte.
class StackPanel : public QWidget {
    Q_OBJECT
public:
    explicit StackPanel(Emulator* emulator, QWidget* parent = nullptr);

    void refresh();
    void set_paused(bool paused);

    QSize sizeHint() const override { return QSize(400, 300); }

private:
    void create_ui();

    Emulator* emulator_;
    bool paused_ = false;
    QTableWidget* table_ = nullptr;

    static constexpr int STACK_ROWS = 24;  // show 24 stack entries
};
