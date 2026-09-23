#pragma once

#include <algorithm>
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <vector>
#include "debug/breakpoints.h"
#include "debug/disasm.h"
#include "debug/disasm_text.h"

class QAction;
class Emulator;
class SymbolTable;
class WatchPanel;

/// Scrollable disassembly view with breakpoint gutter.
/// Uses custom painting for precise control over the display.
///
/// GH #220 — it OBSERVES the BreakpointSet, but only the PC half: the gutter
/// paints has_pc() and nothing else, so a watchpoint change is correctly none
/// of its business and must not cost it a re-disassembly.
class DisasmPanel : public QWidget {
    Q_OBJECT
public:
    explicit DisasmPanel(Emulator* emulator, QWidget* parent = nullptr);
    ~DisasmPanel() override;

    /// Re-disassemble around current PC and repaint.
    void refresh();

    /// The address of the CARET line — the line last clicked, right-clicked or
    /// moved to with the arrow keys — or the CPU's PC when there is none.
    ///
    /// This is the caret, not the selection: after a drag or a Shift-click the
    /// caret sits at the end the cursor moved to, and the selection spans back
    /// to its anchor (`selection_range()`). Nothing in the product calls this
    /// today — Enter and the context menu's "Run to Here" both read the line
    /// they act on directly — so read it as "where the caret is", not as a
    /// promise about what Run to Cursor is wired to.
    uint16_t selected_address() const;

    /// Activate "Follow PC" mode (called when Break or Step is used).
    void activate_follow_pc();

    /// Set paused state — when not paused, the panel grays out and stops updating.
    void set_paused(bool paused);

    /// Set symbol table for address resolution in disassembly display.
    void set_symbol_table(SymbolTable* st) { symbol_table_ = st; }

    /// Set watch panel for "Add Watch" context menu actions.
    void set_watch_panel(WatchPanel* wp) { watch_panel_ = wp; }

    // --- GH #21: selecting and copying the listing -----------------------
    //
    // The selection is an ADDRESS RANGE, not a pair of line indices.
    // entries_ is rebuilt from scratch by every scroll, every refresh() and
    // every activate_follow_pc(), so an index into it survives none of those
    // while an address survives all of them. That single choice is what makes
    // the selection hold still while the view moves under it.

    /// The exact text `copy_selection(fmt)` would put on the clipboard.
    /// Empty when nothing is selected.
    ///
    /// Re-disassembles the selected range from LIVE memory rather than reusing
    /// the painted lines, so a selection whose lines have since scrolled out of
    /// the view still copies in full. The flip side, stated rather than hidden:
    /// the text is memory as it is NOW, so copying while the machine runs
    /// freely can catch self-modifying code mid-write.
    QString selection_text(disasm_text::CopyFormat fmt) const;

    /// Put the current selection on the clipboard. A copy with nothing
    /// selected is a no-op — it never clears what is already there.
    void copy_selection(disasm_text::CopyFormat fmt);

    /// Select every line currently in the view. This panel has no buffer
    /// behind the view — entries_ IS the view — so "select all" is the
    /// visible window, which is what Ctrl+A does.
    void select_all_visible();

    /// True when something is selected; then `low`/`high` are the inclusive
    /// address bounds, normalised so low <= high whichever way the drag went.
    bool selection_range(uint16_t& low, uint16_t& high) const;

    bool has_selection() const { return has_selection_; }

    /// Drop the selection (nothing is highlighted, a copy is a no-op).
    void clear_selection();

signals:
    void run_to_requested(uint16_t addr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;

private:
    void disassemble_from(uint16_t addr, int count);
    uint16_t clamp_view_addr(uint16_t addr) const;
    int line_at_y(int y) const;
    /// line_at_y(), but a y above the first line or below the last clamps to
    /// that end instead of returning -1 — what a drag off the top or bottom
    /// edge of the panel should mean.
    int line_at_y_clamped(int y) const;
    /// Move the selection cursor to `addr`. `extend` keeps the anchor
    /// (Shift-click, Shift-arrow, and every drag step); otherwise the
    /// selection collapses to that one line.
    void set_selection(uint16_t addr, bool extend);
    bool line_selected(uint16_t addr) const;
    void navigate_to_address(const QString& text);
    static uint16_t extract_immediate16(const char* mnemonic);

    Emulator* emulator_;

    // Navigation
    QLineEdit* addr_input_ = nullptr;
    QPushButton* goto_pc_btn_ = nullptr;
    QScrollBar* scrollbar_ = nullptr;
    bool scrollbar_updating_ = false; // guard against feedback loops

    // Display state
    struct DisasmEntry {
        DisasmLine line;
        bool is_current_pc;
        /// A PC breakpoint EXISTS at this address, enabled or not (GH #225).
        bool has_breakpoint;
        /// ... and it can actually fire: individually enabled, master switch
        /// on. The gutter draws a filled dot for live and a hollow ring for
        /// suspended, so the two are never confused for one another.
        bool breakpoint_live;
    };
    std::vector<DisasmEntry> entries_;
    int selected_line_ = -1;

    // GH #21 — the clipboard commands, as real QActions on this widget.
    // They carry the Ctrl+C / Ctrl+A bindings and are what the context menu
    // shows; see the constructor for why they are actions and not keys
    // handled in keyPressEvent.
    QAction* copy_action_           = nullptr;
    QAction* copy_addresses_action_ = nullptr;
    QAction* select_all_action_     = nullptr;

    // GH #21 selection — addresses, see the public block above.
    bool     has_selection_ = false;
    bool     dragging_      = false;
    uint16_t sel_anchor_    = 0;   // where the drag started
    uint16_t sel_cursor_    = 0;   // where it is now
    uint16_t view_addr_ = 0; // top address in view

    // Vertical offset for the painting area (below the top bar)
    int paint_y_offset_ = 0;

    // Layout constants
    static constexpr int GUTTER_WIDTH = 20;   // breakpoint indicator column
    static constexpr int LINE_HEIGHT = 18;    // pixels per line
    int visible_lines() const { return std::max(4, (height() - paint_y_offset_) / LINE_HEIGHT); }
    static constexpr int ADDR_WIDTH = 48;     // address column width
    static constexpr int BYTES_WIDTH = 100;   // hex bytes column width

    QFont mono_font_;

    // Paused state — when false, panel is grayed out and not updated
    bool paused_ = true;

    // Optional: symbol table and watch panel for enhanced features
    SymbolTable* symbol_table_ = nullptr;
    WatchPanel* watch_panel_ = nullptr;

    BreakpointSet::ObserverId observer_ = 0;
};
