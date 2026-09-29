#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QScrollBar>

#include "debug/events.h"   // jnext::dbg::ClientId

namespace jnext { namespace dbg { class Debugger; } }

/// Memory hex editor panel (shown as QDockWidget content).
/// Custom-painted hex editor with address/hex/ASCII columns,
/// region highlighting, and inline byte editing.
class MemoryPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend (GH #278 WP5): the bytes are its
    ///             `peek(Cpu)`, an edit its `poke(Cpu)`, the slot labels its
    ///             `mmu_slots()`, the SP row its `registers()`. Null shows
    ///             nothing and writes nothing.
    explicit MemoryPanel(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// The client an edit is attributed to (the backend's MUTATE line). The
    /// manager sets the debugger window's client while the window is open;
    /// CLIENT_NONE otherwise.
    void set_client(jnext::dbg::ClientId by) { client_ = by; }

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

    /// The CPU address a view offset reads and writes: the offset itself in
    /// the CPU view; `(slot << 13) | (addr & 0x1FFF)` in a slot view — still
    /// through the CPU map, as it always was (the physical-page view is WP8).
    uint16_t cpu_address(uint16_t addr) const;

    /// Read `n` bytes of the current view from `addr` in ONE backend peek (the
    /// paint reads a row at a time). A peek moves nothing: no watch, no +3
    /// floating-bus latch.
    void read_bytes(uint16_t addr, uint8_t* out, size_t n) const;

    /// Write a byte depending on current view mode.
    void write_byte(uint16_t addr, uint8_t val);

    /// Get the total address range for current view.
    int total_rows() const;

    /// Convert widget coordinates to (row, byte_index).
    /// Returns false if click is outside the hex area.
    bool hit_test(int x, int y, int& row, int& col) const;

    /// Scroll to make the selected_addr_ visible if needed.
    void ensure_visible();

    jnext::dbg::Debugger* dbg_;
    jnext::dbg::ClientId  client_ = jnext::dbg::CLIENT_NONE;

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
