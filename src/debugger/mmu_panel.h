#pragma once

#include <QWidget>
#include <QLabel>

class Emulator;

/// Debugger panel showing MMU slot-to-page mapping and 128K banking info.
/// Compact vertical layout for use alongside the CPU registers panel.
class MmuPanel : public QWidget {
    Q_OBJECT
public:
    explicit MmuPanel(Emulator* emulator, QWidget* parent = nullptr);

    void refresh();

private:
    void create_ui();

    Emulator* emulator_;

    QLabel* slot_page_[8] = {};
    QLabel* slot_type_[8] = {};
    QLabel* bank_128k_ = nullptr;
    QLabel* rom_select_ = nullptr;
    QLabel* paging_locked_ = nullptr;
};
