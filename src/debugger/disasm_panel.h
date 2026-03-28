#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <vector>
#include "debug/disasm.h"

class Emulator;

/// Scrollable disassembly view with breakpoint gutter.
/// Uses custom painting for precise control over the display.
class DisasmPanel : public QWidget {
    Q_OBJECT
public:
    explicit DisasmPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Re-disassemble around current PC and repaint.
    void refresh();

    /// Get the address of the currently selected line (for Run to Cursor).
    uint16_t selected_address() const;

    /// Activate "Follow PC" mode (called when Break or Step is used).
    void activate_follow_pc();

    /// Set paused state — when not paused, the panel grays out and stops updating.
    void set_paused(bool paused);

signals:
    void breakpoint_toggled(uint16_t addr);
    void run_to_requested(uint16_t addr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    QSize sizeHint() const override;

private:
    void disassemble_from(uint16_t addr, int count);
    int line_at_y(int y) const;
    void navigate_to_address(const QString& text);

    Emulator* emulator_;

    // Navigation
    QLineEdit* addr_input_ = nullptr;
    QCheckBox* follow_pc_ = nullptr;

    // Display state
    struct DisasmEntry {
        DisasmLine line;
        bool is_current_pc;
        bool has_breakpoint;
    };
    std::vector<DisasmEntry> entries_;
    int selected_line_ = -1;
    uint16_t view_addr_ = 0; // top address in view

    // Vertical offset for the painting area (below the top bar)
    int paint_y_offset_ = 0;

    // Layout constants
    static constexpr int GUTTER_WIDTH = 20;   // breakpoint indicator column
    static constexpr int LINE_HEIGHT = 18;    // pixels per line
    static constexpr int VISIBLE_LINES = 22;  // number of visible lines
    static constexpr int ADDR_WIDTH = 48;     // address column width
    static constexpr int BYTES_WIDTH = 100;   // hex bytes column width

    QFont mono_font_;

    // Paused state — when false, panel is grayed out and not updated
    bool paused_ = true;
};
