#pragma once

#include <algorithm>
#include <QPointer>
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "debug/disasm.h"
#include "debug/disasm_text.h"
#include "debug/events.h"
#include "debug/long_address.h"
#include "debugger/breakpoint_glyph.h"

class QAction;
class BreakpointModel;
class SymbolTable;
namespace jnext { namespace dbg { class Debugger; } }
class WatchPanel;

/// Scrollable disassembly view: Mem, Addr, breakpoint column, [Label], mnemonic, bytes (GH #448).
/// Uses custom painting for precise control over the display.
///
/// GH #220 — it follows the GUI's BreakpointModel, but only its Execute kind:
/// the gutter paints this GUI's Execute breakpoints and nothing else, so a
/// data breakpoint change is correctly none of its business and must not cost
/// it a re-disassembly (GH #278 WP4c: the model's changed(kinds) says which).
class DisasmPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP5):
    ///             the bytes are its memory_reader() — a peek (the PC
    ///             instruction as the CPU will fetch it, GH #360), so
    ///             disassembling moves nothing — and the registers its registers().
    explicit DisasmPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Re-disassemble around current PC and repaint.
    void refresh();

    /// The address of the CARET line — the line last clicked, right-clicked or
    /// moved to with the arrow keys — or the CPU's PC when there is none.
    ///
    /// This is the caret, not the selection: after a drag or a Shift-click the
    /// caret sits at the end the cursor moved to, and the selection spans back
    /// to its anchor (`selection_range()`).
    ///
    /// GH #1 made this load-bearing: the bindable "Run to Cursor" action reads
    /// it (via run_to_selected()). Enter and the context menu's "Run to Here"
    /// still read the line they act on directly, because they already know it.
    uint16_t selected_address() const;

    /// GH #1 — "Run to Cursor" as a WINDOW command, for the bindable action.
    /// Emits run_to_requested() for selected_address(), so it goes through the
    /// exact same path as Enter and the context menu, corruption gate included.
    /// With no caret, selected_address() is the PC, so this runs a full lap —
    /// the same thing "Run to Here" on the current line has always done.
    void run_to_selected();

    /// GH #423 - centre the view on `addr` and select that line (the caret, so
    /// Run to Cursor and a copy act on it): the Source tab's "Show in Disassembly".
    void show_address(uint16_t addr);

    /// Activate "Follow PC" mode (called when Break or Step is used).
    void activate_follow_pc();

    /// Set paused state — when not paused, the panel grays out and stops updating.
    void set_paused(bool paused);

    /// Set symbol table for address resolution in disassembly display.
    void set_symbol_table(const SymbolTable* st) { symbol_table_ = st; }

    /// GH #387 - the backend the annotation menu entries write to (the panel reads through its
    /// const pointer). Without one the menu offers no annotation entries.
    void set_annotation_backend(jnext::dbg::Debugger* dbg) { annot_ = dbg; }

    /// GH #387 - what a line shows in the Label column (the name the one reader gives its
    /// address in its memory, elided to LABEL_CHARS) and after its mnemonic (`; first line`,
    /// ` ...` when the comment has more); "" for none or a line out of range.
    QString line_label(int line) const;
    QString line_comment(int line) const;
    /// GH #387 - whether the Label column is shown: while the symbol table holds any label.
    bool label_column_shown() const;
    /// GH #387 - x of the mnemonic text of every line (for the layout rows).
    int mnemonic_x() const;
    /// GH #448 - the layout, left to right: Mem, Addr, BP column, [Label], mnemonic, bytes,
    /// [; comment]. x of the BP column's left edge (its glyph is centred GUTTER_WIDTH/2 in).
    int breakpoint_x() const;
    /// GH #448 - width of the mnemonic column: a longer mnemonic still paints whole.
    int mnemonic_width() const;
    /// GH #448 - x of the opcode bytes of `line`: after the mnemonic column, or after the
    /// mnemonic itself (plus a gap) when that line's mnemonic is wider than the column.
    int bytes_x(int line) const;

    /// Set watch panel for "Add Watch" context menu actions.
    void set_watch_panel(WatchPanel* wp) { watch_panel_ = wp; }

    /// GH #278 WP4c — the GUI's breakpoints, which the gutter draws and the
    /// gutter click, "Toggle Breakpoint" and the "Break on Read/Write" items
    /// edit. Without one the gutter is bare and those items do nothing. Held
    /// weakly: the window may outlive the manager that owns the model.
    void set_breakpoint_model(BreakpointModel* model);

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

    /// GH #360 - Friendly (`P04`, `DIVROM`) or Physical (`$048000`, `BRAM7+$0000`) in
    /// the Mem column.
    void set_address_style(jnext::dbg::AddressStyle st) { style_ = st; }

    /// Names an address in the memory that supplies it now (GH #375).
    disasm_text::SymbolNamer namer(bool assembler_names = false) const;

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

    // --- GH #347: the Source tab's selection --------------------------------

    /// The Source tab's selected lines, `first..last` of `file` (an empty file: none).
    /// Every instruction with a source location on one of them (any location, not only
    /// the primary) is tinted. With `reveal` (a move the user made in the Source tab)
    /// and none of them in view, the view centres on the first of those addresses that
    /// is mapped now; without it the view stays where it is. The selection is untouched.
    void set_source_lines(const std::string& file, int first, int last, bool reveal);
    /// Whether the instruction at `addr` is tinted by the Source tab's lines now.
    bool source_linked(uint16_t addr) const;

signals:
    void run_to_requested(uint16_t addr);
    /// GH #347 - the USER changed the selection: its bounds and the caret line. Not
    /// emitted by show_address() (the Source tab asked for that one), nor by a refresh.
    void selection_changed(uint16_t lo, uint16_t hi, uint16_t caret);
    /// GH #347 - the menu's Show in Source on `addr`.
    void show_source_requested(uint16_t addr);
    /// GH #387 - a menu entry changed a label or a comment: the manager refreshes the panels.
    void annotations_changed();

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
    /// Centre the view on `addr`, leaving the selection alone (the address box).
    void center_on(uint16_t addr);
    /// The gutter click and the context menu's Toggle Breakpoint: remove the
    /// Execute breakpoint drawn at `addr`, or add a logical one.
    void toggle_breakpoint(uint16_t addr, int line);
    static uint16_t extract_immediate16(const char* mnemonic);
    /// Emit selection_changed() when the (lo, hi, caret) differs from the last one emitted.
    void notify_selection_();
    bool linked_at_(jnext::dbg::MemSpace space, uint16_t addr) const;

    const jnext::dbg::Debugger* dbg_;

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
        /// The memory that supplied the line when it was drawn (`source_page`).
        /// While running the panel is not redrawn, so a click must act on what
        /// it shows, not on what is mapped by then.
        jnext::dbg::MemSpace space = jnext::dbg::MemSpace::cpu();
        /// GH #360 - the memory the line's BYTES came from, for the Mem column:
        /// the PC line's is `space` (the fetch prediction), any other line's is
        /// the live data route of its slot (`SlotInfo::now`, design 10.3).
        jnext::dbg::MemSpace mem = jnext::dbg::MemSpace::cpu();
    };
    std::vector<DisasmEntry> entries_;
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;
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

    // GH #347 - the Source tab's lines, and the last selection announced to it.
    std::string link_file_;
    int link_first_ = 0, link_last_ = 0;
    std::optional<std::array<uint16_t, 3>> notified_;
    bool notifying_ = false;

    // Vertical offset for the painting area (below the top bar)
    int paint_y_offset_ = 0;

    // Layout constants
    static constexpr int GUTTER_WIDTH = kBreakpointColumnWidth;   // breakpoint indicator column
    static constexpr int LINE_HEIGHT = 18;    // pixels per line
    int visible_lines() const { return std::max(4, (height() - paint_y_offset_) / LINE_HEIGHT); }
    static constexpr int ADDR_WIDTH = 48;     // address column width
    static constexpr int MEM_WIDTH = 96;      // GH #360: memory column; holds "BRAM7+$1FFF"
    static constexpr int BYTES_WIDTH = 100;   // hex bytes column width
    static constexpr int LEFT_MARGIN = 4;     // GH #448: Mem starts here
    static constexpr int MNEMONIC_CHARS = 20; // GH #448: the mnemonic column holds this many characters
    static constexpr int LABEL_CHARS = 16;    // GH #387: the Label column holds this many characters
    int label_width() const;
    void fill_row(QPainter& painter, int y, int w, const QColor& c) const;
    jnext::dbg::Debugger* annot_ = nullptr;

    QFont mono_font_;

    // Paused state — when false, panel is grayed out and not updated
    bool paused_ = true;

    // Optional: symbol table and watch panel for enhanced features
    const SymbolTable* symbol_table_ = nullptr;
    WatchPanel* watch_panel_ = nullptr;

    QPointer<BreakpointModel> bp_model_;
};
