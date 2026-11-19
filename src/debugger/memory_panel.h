#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QScrollBar>

#include <optional>
#include <vector>

#include "debug/events.h"   // jnext::dbg::ClientId
#include "debug/inspect.h"  // jnext::dbg::MemSpace
#include "debug/long_address.h"
#include "debug/mem_tools.h"
#include "debug/result.h"

namespace jnext { namespace dbg { class Debugger; } }

class QMenu;
class QContextMenuEvent;

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

    /// GH #360 - Friendly (`P0E`) or Physical (`$060000`) in the tag after the ASCII column.
    void set_address_style(jnext::dbg::AddressStyle st) { style_ = st; }

    /// Update display with current memory and MMU state.
    void refresh();

    /// GH #366 - show CPU address `addr`: switches the view to CPU View when it is in a slot or
    /// page view (a CPU address means nothing there), then centres and selects the byte.
    void show_address(uint16_t addr);

    QSize sizeHint() const override;

    // ---- GH #370: find, and save / load / fill a block -------------------------------------
    // Reached from the panel's context menu (right-click, Menu key, Shift+F10); the dialogs
    // call the same functions. A block is in the SELECTED VIEW's numbering: the CPU address in
    // CPU View, the offset within the 8K page in a Slot or Page view. Each answer goes out as
    // `message()` (the debugger window holds it in its status bar).

    /// Fill `menu` with Find..., Find Next, a separator, Save Block..., Load Block...,
    /// Fill Block.... The actions belong to the menu, never to the panel.
    void fill_context_menu(QMenu& menu);

    /// The selected byte as a view offset, -1 = none.
    int selected_address() const { return selected_addr_; }

    /// Search the selected view for `p`, from the selected byte (inclusive) or the view start.
    void find(const jnext::dbg::memtools::Pattern& p);
    /// The last pattern again, from the byte after the selection (or the view start).
    void find_next();
    /// Write `len` bytes from `start` to a raw binary file.
    void save_block(uint32_t start, uint32_t len, const QString& path);
    /// Read a raw binary file into memory at `start`; no `len` = the whole file.
    void load_block(uint32_t start, std::optional<uint32_t> len, const QString& path);
    /// Repeat `value` over `len` bytes from `start`.
    void fill_block(uint32_t start, uint32_t len, const std::vector<uint8_t>& value);

signals:
    /// What a find / save / load / fill did, or why it refused.
    void message(const QString& text);
    /// A find hit has a CPU address: the Disassembly should show it.
    void show_in_disassembly_requested(uint16_t addr);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
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

    /// GH #388 - show / recolour the "Coverage: X R W" legend.
    void update_coverage_legend();

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

    // GH #370 helpers (memory_panel.cpp).
    struct Block {
        Where                                 w;       // where the start lives
        std::vector<jnext::dbg::memtools::Chunk> chunks;
    };
    bool plan_block(const QString& verb, uint32_t start, uint32_t len, Block& b);
    bool locate_start(const QString& verb, uint32_t start, Where& w);
    QString loc_text(const Where& w, uint32_t space_addr) const;
    size_t write_chunks(const Block& b, const uint8_t* data, jnext::dbg::Result* stop);
    bool report_write_refusal(const QString& verb, size_t landed, jnext::dbg::Result stop);
    void run_find(const jnext::dbg::memtools::Pattern& p, size_t from);
    uint32_t start_default() const;
    bool parse_start(const QString& verb, const QString& text, uint32_t& start);
    void show_find_dialog();
    void show_save_dialog();
    void show_load_dialog();
    void show_fill_dialog();

    /// Get the total address range for current view.
    int total_rows() const;

    /// Convert widget coordinates to (row, byte_index).
    /// Returns false if click is outside the hex area.
    bool hit_test(int x, int y, int& row, int& col) const;

    /// Scroll to make the selected_addr_ visible if needed.
    void ensure_visible();

    jnext::dbg::Debugger* dbg_;
    jnext::dbg::ClientId  client_ = jnext::dbg::CLIENT_NONE;
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;

    // Top bar widgets
    QLineEdit* addr_input_ = nullptr;
    QComboBox* page_selector_ = nullptr;
    QLabel* coverage_legend_ = nullptr;   // GH #388

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

    // GH #370: the last find, and the last answers of the Find dialog.
    std::optional<jnext::dbg::memtools::Pattern> last_pattern_;
    QString find_text_, find_mask_;
    bool    find_text_mode_ = false, find_match_case_ = true;

    // Layout constants
    static constexpr int BYTES_PER_ROW = 16;
    static constexpr int HEX_CHAR_WIDTH = 8;        // approximate; recalculated from font metrics
    static constexpr int TOP_BAR_HEIGHT = 36;

    // Cached font metrics (set in paintEvent)
    mutable int char_w_ = 8;
    mutable int char_h_ = 14;
};
