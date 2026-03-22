#pragma once

#include <QWidget>
#include <QTableWidget>

class Emulator;

/// Debugger panel showing all 128 hardware sprites in a table.
class SpritePanel : public QWidget {
    Q_OBJECT
public:
    explicit SpritePanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current sprite state.
    void refresh();

    QSize sizeHint() const override { return QSize(400, 500); }

private:
    void create_ui();

    Emulator* emulator_;
    QTableWidget* table_ = nullptr;
};
