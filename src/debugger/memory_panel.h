#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QScrollBar>

class Emulator;

/// Memory hex editor panel (shown as QDockWidget content).
/// Custom-painted hex editor with address/hex/ASCII columns,
/// region highlighting, and inline byte editing.
class MemoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit MemoryPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current memory and MMU state.
    void refresh();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void create_ui();
    void navigate_to_address(uint16_t addr);
    void update_page_selector();
    void update_scroll_bar();
    int visible_rows() const;
    int hex_area_left() const;
    int ascii_area_left() const;
    int row_height() const;
    int header_height() const;

    /// Read a byte depending on current view mode.
    uint8_t read_byte(uint16_t addr) const;

    /// Write a byte depending on current view mode.
    void write_byte(uint16_t addr, uint8_t val);

    /// Get the total address range for current view.
    int total_rows() const;

    /// Convert widget coordinates to (row, byte_index).
    /// Returns false if click is outside the hex area.
    bool hit_test(int x, int y, int& row, int& col) const;

    /// Scroll to make the selected_addr_ visible if needed.
    void ensure_visible();

    Emulator* emulator_;

    // Top bar widgets
    QLineEdit* addr_input_ = nullptr;
    QComboBox* page_selector_ = nullptr;

    // Scroll bar
    QScrollBar* scroll_bar_ = nullptr;

    // View state
    int scroll_offset_ = 0;        // first visible row index
    int selected_addr_ = -1;       // currently selected byte address (-1 = none)
    int edit_nibble_ = 0;          // 0 = high nibble next, 1 = low nibble entered
    uint8_t edit_value_ = 0;       // partial edit value

    // Layout constants
    static constexpr int BYTES_PER_ROW = 16;
    static constexpr int HEX_CHAR_WIDTH = 8;        // approximate; recalculated from font metrics
    static constexpr int TOP_BAR_HEIGHT = 36;

    // Cached font metrics (set in paintEvent)
    mutable int char_w_ = 8;
    mutable int char_h_ = 14;
};
