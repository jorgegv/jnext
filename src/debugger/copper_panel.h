#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QTableWidget>

class Emulator;

/// Debugger panel showing decoded copper instructions and current PC.
class CopperPanel : public QWidget {
    Q_OBJECT
public:
    explicit CopperPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current copper state.
    void refresh();

    QSize sizeHint() const override { return QSize(350, 400); }

private:
    void create_ui();

    Emulator* emulator_;
    QCheckBox* enable_check_ = nullptr;
    QLabel* pc_label_ = nullptr;
    QTableWidget* table_ = nullptr;
};
