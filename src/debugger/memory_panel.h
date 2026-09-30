#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QScrollBar>

#include "debug/events.h"   // jnext::dbg::ClientId
#include "debug/inspect.h"  // jnext::dbg::MemSpace

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

    /// WHERE a view offset lives (GH #278 WP8, owner decision Q7): the CPU
    /// address itself in the CPU view; in a slot view the slot's PHYSICAL
    /// backing store — `SlotInfo::space` + `space_offset` + `addr & 0x1FFF`
    /// (CAP-INS-03, REQ-qt-31: `Page{nr_page}` for a RAM slot, `Rom{…}` for a
    /// ROM slot; the panel never composes the space itself), so an overlay
    /// over the slot (DivMMC, Multiface, Layer 2) no longer shows in it; and in
    /// the Page view the NR page the user picked.
    struct Where {
        jnext::dbg::MemSpace space;
        uint32_t             addr = 0;
    };
    Where locate(uint16_t addr) const;

    /// Read `n` bytes of the current view from `addr` in ONE backend peek (the
    /// paint reads a row at a time). A peek moves nothing: no watch, no +3
    /// floating-bus latch.
    void read_bytes(uint16_t addr, uint8_t* out, size_t n) const;

    /// Write a byte where locate() says it lives. A ROM slot's poke is
    /// `RefusedReadOnly`: nothing is written and the byte paints unchanged.
    void write_byte(uint16_t addr, uint8_t val);

    /// GH #278 WP8 — the selector's last item, "Page...": asks for an NR page
    /// number and shows that physical page, mapped in a slot or not. Returns
    /// false (and changes nothing) on cancel or a number outside 00-DF.
    bool choose_page();

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
    static constexpr int PAGE_ITEM = 9;  // selector index of "Page..."
    int page_ = -1;                // the NR page the Page view shows
    int prev_mode_ = 0;            // selector index before the last change
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
