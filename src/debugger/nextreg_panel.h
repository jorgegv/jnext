#pragma once

#include <QWidget>
#include <QTableWidget>

class Emulator;

/// Debugger panel showing all 256 NextREG registers with editable values.
class NextRegPanel : public QWidget {
    Q_OBJECT
public:
    explicit NextRegPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current NextREG state.
    void refresh();

    QSize sizeHint() const override { return QSize(400, 500); }

private:
    void create_ui();
    void populate_names();

    Emulator* emulator_;
    QTableWidget* table_ = nullptr;

    /// Static register name lookup (populated once at construction).
    static const char* reg_name(uint8_t reg);
};
