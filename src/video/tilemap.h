#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

class Ram;
class PaletteManager;

/// Tilemap renderer for 40x32 and 80x32 tile modes.
///
/// The tilemap displays a grid of 8x8 pixel tiles fetched from RAM.
/// In 40-column mode: 40 tiles x 32 rows, each tile 8px wide = 320px.
/// In 80-column mode: 80 tiles x 32 rows, each tile 4px wide = 320px.
///
/// Tile map memory holds either 2 bytes per tile (index + attribute) or
/// 1 byte per tile when flags are stripped (force_attr mode).
///
/// Tile definition memory holds 4-bit-per-pixel patterns: 8x8 = 32 bytes
/// per tile (4 bytes per row, 2 pixels per byte, high nibble first).
///
/// Derived from VHDL: cores/zxnext/src/video/tilemap.vhd
class Tilemap {
public:
    Tilemap() = default;

    void reset();

    // -----------------------------------------------------------------
    // NextREG configuration
    // -----------------------------------------------------------------

    /// NextREG 0x6B — Tilemap control.
    ///   bit 7 = enabled
    ///   bit 6 = 80-column mode
    ///   bit 5 = force attribute / strip flags (use default_attr for all tiles)
    ///   bit 4 = tilemap palette select (VHDL nr_6b_tm_palette_select)
    ///   bit 3 = text mode (extend palette offset, no rotate/mirror)
    ///   bit 1 = 512 tile mode (attr bit 0 becomes tile index bit 8)
    ///   bit 0 = ULA-over-tilemap (per-tile priority bit)
    void set_control(uint8_t val);
    uint8_t get_control() const { return control_raw_; }

    /// NextREG 0x6C — Default tilemap attribute.
    void set_default_attr(uint8_t val) { default_attr_ = val; }
    uint8_t get_default_attr() const { return default_attr_; }

    /// NextREG 0x6E — Tilemap base address.
    ///   bit 7 = bank select (0 = bank 5, 1 = bank 7).
    ///   bits 5:0 = 256-byte offset within the selected 16K bank.
    ///   bit 6 is unused.
    void set_map_base(uint8_t val);
    uint8_t get_map_base_raw() const { return map_base_raw_; }

    /// NR 0x6E read-back — VHDL zxnext.vhd:6108 forces bit 6 to '0'
    /// (`nr_6e_tilemap_base_7 & '0' & nr_6e_tilemap_base`). The raw
    /// stored byte is therefore masked when read back through NR 0x6E.
    uint8_t get_map_base_read() const { return map_base_raw_ & 0xBF; }

    /// NextREG 0x6F — Tile definitions base address (same encoding as 0x6E).
    void set_def_base(uint8_t val);
    uint8_t get_def_base_raw() const { return def_base_raw_; }

    /// NR 0x6F read-back — VHDL zxnext.vhd:6111 forces bit 6 to '0'
    /// (`nr_6f_tilemap_tiles_7 & '0' & nr_6f_tilemap_tiles`).
    uint8_t get_def_base_read() const { return def_base_raw_ & 0xBF; }

    /// NextREG 0x2F — Tilemap X scroll MSB (bits 1:0).
    void set_scroll_x_msb(uint8_t val) { scroll_x_ = (scroll_x_ & 0xFF) | ((val & 0x03) << 8); }

    /// NextREG 0x30 — Tilemap X scroll LSB.
    void set_scroll_x_lsb(uint8_t val) { scroll_x_ = (scroll_x_ & 0x300) | val; }

    /// NextREG 0x31 — Tilemap Y scroll.
    void set_scroll_y(uint8_t val) { scroll_y_ = val; }

    /// Enable/disable tilemap rendering (bit 7 of NextREG 0x6B).
    void set_enabled(bool en) {
        enabled_ = en;
        log_nr6b_change();
    }
    bool enabled() const { return enabled_; }

    /// True when in 80-column mode (bit 6 of NextREG 0x6B).
    /// Live value — reflects current per-line snapshot during render_frame
    /// after `apply_nr6b_changes_for_line` advances it.
    bool mode_80col() const { return mode_80col_; }

    /// True when in text mode (bit 3 of NextREG 0x6B).
    /// VHDL tilemap.vhd:191 textmode_i <= control_i(3).
    bool text_mode() const { return text_mode_; }

    /// True when in 512-tile mode (bit 1 of NextREG 0x6B).
    /// VHDL tilemap.vhd:194 mode_512_i <= control_i(1).
    bool mode_512() const { return mode_512_; }

    /// True when tm_on_top is set (bit 0 of NextREG 0x6B).
    /// VHDL tilemap.vhd:195 tm_on_top_i <= control_i(0).
    bool tm_on_top() const { return ula_on_top_; }

    /// Tilemap palette select (bit 4 of NextREG 0x6B).
    /// VHDL: nr_6b_tm_palette_select — selects tilemap palette 0 vs 1 at
    /// render time.  Independent from the NR 0x43 palette-I/O target field.
    bool palette_sel() const { return palette_sel_; }

    /// Maximum number of scanlines we track per-line scroll for.  Sized to
    /// cover every machine timing the emulator supports — Pentagon /
    /// +3 reach lines_per_frame=320 (vc_max_=319), and we add headroom so
    /// that mid-vblank NR 0x30/0x31 writes at the very last scanline (and
    /// any future timing variant) are stored, not silently dropped.
    /// Mirrors the SpriteEngine catch-up rationale at sprites.cpp:119-144.
    static constexpr int kSnapshotLines = 528;

    /// Snapshot current scroll values for a given scanline (called per-line).
    void snapshot_scroll_for_line(int line) {
        if (line >= 0 && line < kSnapshotLines) {
            scroll_x_per_line_[line] = scroll_x_;
            scroll_y_per_line_[line] = scroll_y_;
        }
    }

    /// Initialize per-line scroll arrays to current values (called at frame start).
    void init_scroll_per_line() {
        scroll_x_per_line_.fill(scroll_x_);
        scroll_y_per_line_.fill(scroll_y_);
    }

    /// Test/debug accessor: per-line snapshotted X scroll for a scanline.
    /// Returns 0 if the line index is out of range.
    uint16_t scroll_x_for_line(int line) const {
        return (line >= 0 && line < kSnapshotLines) ? scroll_x_per_line_[line] : 0;
    }

    /// Test/debug accessor: per-line snapshotted Y scroll for a scanline.
    uint8_t scroll_y_for_line(int line) const {
        return (line >= 0 && line < kSnapshotLines) ? scroll_y_per_line_[line] : 0;
    }

    // Clip window (NextREG 0x1B, 4-write cycle)
    void set_clip_x1(uint8_t v) { clip_x1_ = v; }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; }

    // Clip-window getters (VHDL zxnext.vhd:4977-4980 reset defaults:
    // x1=0x00, x2=0x9F, y1=0x00, y2=0xFF).  X coords are internally
    // doubled by the clip comparator (tilemap.vhd:416-417).
    uint8_t clip_x1() const { return clip_x1_; }
    uint8_t clip_x2() const { return clip_x2_; }
    uint8_t clip_y1() const { return clip_y1_; }
    uint8_t clip_y2() const { return clip_y2_; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// True when in 80-column (640-pixel) mode.
    bool is_80col() const { return mode_80col_; }

    /// Render one scanline of tilemap into an ARGB8888 buffer.
    ///
    /// Always renders 640 pixels into `dst`.
    ///   - 80-col mode: native 1:1 (each tilemap pixel maps to one output cell).
    ///   - 40-col mode: each source column is pixel-doubled — every tilemap
    ///     pixel is emitted into two adjacent output cells (`dst[2k]` and
    ///     `dst[2k+1]`).
    /// VHDL tilemap.vhd:228 confirms the rate: tm_mode=1 selects the 14 MHz
    /// tap (hcount(10:1)) → 640 pixels per scanline, tm_mode=0 selects the
    /// 7 MHz tap (hcount(10:2)) → 320 source pixels each emitted as two
    /// 14 MHz cells.  G104 (HI_RES 512px canonical framebuffer).
    ///
    /// @param dst            Output buffer (640 pixels wide).
    /// @param ula_over_flags Per-pixel ULA priority flags (640 entries).
    /// @param y              Display row (0-255 within the active display area).
    /// @param ram            Physical RAM for direct access.
    /// @param palette        Palette manager for tilemap colour lookup.
    /// @param textmode_flags Optional per-pixel textmode flag buffer (640
    ///                       entries). When non-null, set to true for any
    ///                       pixel emitted while the tilemap is in text-mode
    ///                       (NR 0x6B bit 3 / VHDL tilemap.vhd:62,443
    ///                       pixel_textmode_o); false otherwise. Required by
    ///                       the compositor for VHDL zxnext.vhd:7109
    ///                       text-mode RGB transparency check (G98/G101).
    void render_scanline(uint32_t* dst, bool* ula_over_flags, int y,
                         const Ram& ram,
                         const PaletteManager& palette,
                         bool* textmode_flags = nullptr) const;

    /// Render one scanline regardless of enabled_ state. Used by the debugger.
    /// Always emits 640 pixels — same emit semantics as render_scanline.
    void render_scanline_debug(uint32_t* dst, bool* ula_over_flags, int y,
                               const Ram& ram,
                               const PaletteManager& palette,
                               bool* textmode_flags = nullptr);

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    // -----------------------------------------------------------------
    // Per-scanline NR 0x6B change log (G06)
    // -----------------------------------------------------------------
    //
    // VHDL zxnext.vhd:5461-5462 latches NR 0x6B on every write
    // (nr_6b_tm_en <= bit 7, nr_6b_tm_control <= bits 6:0); the downstream
    // tilemap pipeline reads those latches every clock cycle, so a Copper
    // MOVE to NR 0x6B mid-frame must take effect on the very next scanline
    // (mode flip, textmode toggle, 256<->512 tile mode, tm_on_top, enable).
    // Without this log the renderer would see only the end-of-frame value
    // (last-write-wins) and collapse all scanlines onto the same path.
    //
    // The change-log captures the 5 bits owned by Tilemap (b0/b1/b3/b6/b7).
    // Bit 4 (palette select) is owned by Ula (UL-C, palsel6b log) — out of
    // scope here. Bit 5 (force_attr / strip_flags) is currently logged
    // alongside the others for completeness but is not exercised by
    // TM-160..164.
    //
    // Lifecycle (mirrors layer2 / palette / ula):
    //   tilemap_.start_frame_nr6b();         // emulator at frame start
    //   …                                    // emulation runs; set_control
    //                                        // / set_enabled writes append
    //                                        // tagged with current_line_
    //   tilemap_.rewind_nr6b_to_baseline();  // before render_frame
    //   for row in 0..H:
    //       tilemap_.apply_nr6b_changes_for_line(row);
    //       render_scanline(row);

    /// Snapshot the live NR 0x6B state as the frame baseline and reset
    /// the change log. Called at the start of every frame.
    void start_frame_nr6b();

    /// Update the scanline tag attached to subsequent NR 0x6B writes.
    /// Called from Emulator::on_scanline. Default 0 at frame start.
    void set_current_nr6b_line(int line) {
        nr6b_current_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live NR 0x6B state to the frame baseline and reset
    /// the render cursor. Called once before per-scanline render.
    void rewind_nr6b_to_baseline();

    /// Apply all logged NR 0x6B changes whose line tag equals `line`.
    /// Cursor is monotonically advanced; the log is in scanline order
    /// so total work across a frame is O(change_count_).
    void apply_nr6b_changes_for_line(int line);

    /// Apply every remaining NR 0x6B change, regardless of its line tag.
    /// Called after the per-line render loop so that NR 0x6B writes
    /// landing in vblank (line >= FB_HEIGHT, where the renderer never
    /// invokes apply_nr6b_changes_for_line) still update the live state
    /// — otherwise the next frame's start_frame_nr6b() would snapshot a
    /// stale baseline equal to the LAST visible-line replay value, and
    /// firmware that writes NR 0x6B during vblank (waitForScanline(255)
    /// + sc_update style main loops) would never see the write reflected
    /// in the rendered output.
    void flush_remaining_nr6b_changes();

    /// Number of NR 0x6B changes recorded this frame (diagnostic).
    size_t nr6b_change_log_size() const { return nr6b_change_count_; }

    /// Static cap; further writes after this many in a frame are
    /// silently dropped (with a once-per-frame warn). Sized for the
    /// worst-case "Copper writes NR 0x6B on every scanline" scenario.
    static constexpr size_t MAX_NR6B_CHANGES_PER_FRAME = 1024;

private:
    // --- Control register state ---
    uint8_t  control_raw_    = 0;
    bool     enabled_        = false;
    bool     mode_80col_     = false;
    bool     text_mode_      = false;
    bool     force_attr_     = false;
    bool     mode_512_       = false;
    bool     ula_on_top_     = false;   // global ULA-over-tilemap flag
    bool     palette_sel_    = false;   // NR 0x6B bit 4 — tilemap palette select

    uint8_t  default_attr_   = 0;       // NextREG 0x6C

    // Base addresses (physical RAM byte offsets)
    uint8_t  map_base_raw_   = 0;       // raw register value for 0x6E
    uint8_t  def_base_raw_   = 0;       // raw register value for 0x6F
    uint32_t map_base_addr_  = 0;       // decoded physical RAM address
    uint32_t def_base_addr_  = 0;       // decoded physical RAM address

    // Scroll
    uint16_t scroll_x_       = 0;       // 10-bit X scroll
    uint8_t  scroll_y_       = 0;       // 8-bit Y scroll

    // Per-scanline scroll snapshots (for mid-frame scroll changes).
    // Sized via kSnapshotLines (>= max lines_per_frame across all machines).
    std::array<uint16_t, kSnapshotLines> scroll_x_per_line_{};
    std::array<uint8_t,  kSnapshotLines> scroll_y_per_line_{};

    // Clip window — VHDL reset defaults from zxnext.vhd:4977-4980
    // (0x00 / 0x9F / 0x00 / 0xFF cover the full 320x256 visible area;
    // X coords are internally doubled at render, giving pixel-x 0..319).
    uint8_t  clip_x1_        = 0;       // = 0x00
    uint8_t  clip_x2_        = 159;     // = 0x9F
    uint8_t  clip_y1_        = 0;       // = 0x00
    uint8_t  clip_y2_        = 255;     // = 0xFF

    // Helpers
    static uint32_t decode_base_addr(uint8_t reg_val);

    // ── Per-scanline NR 0x6B change log (G06) ────────────────────────
    // VHDL zxnext.vhd:5461-5462 — NR 0x6B latches every write.
    // Snapshot captures the 5 Tilemap-owned bits (b7/b6/b5/b3/b1/b0)
    // packed back into a NR 0x6B byte (bit 4 left as 0 — Ula's territory
    // via palsel6b log). The reconstructed byte is the input to
    // set_control during apply.
    struct Nr6bChange {
        uint16_t line;
        uint8_t  control_byte;   ///< Reconstructed NR 0x6B (b4 cleared)
    };
    std::array<Nr6bChange, MAX_NR6B_CHANGES_PER_FRAME> nr6b_change_log_{};
    size_t   nr6b_change_count_    = 0;
    size_t   nr6b_render_cursor_   = 0;
    uint16_t nr6b_current_line_    = 0;
    bool     nr6b_overflow_warned_ = false;

    // Baseline snapshot (taken at start_frame_nr6b).
    uint8_t  baseline_control_raw_ = 0;
    bool     baseline_enabled_     = false;
    bool     baseline_mode_80col_  = false;
    bool     baseline_text_mode_   = false;
    bool     baseline_force_attr_  = false;
    bool     baseline_mode_512_    = false;
    bool     baseline_ula_on_top_  = false;

    /// Append a snapshot of the live NR 0x6B state (5 owned bits
    /// reconstructed into a control byte). Called from set_control and
    /// set_enabled. Bit 4 (palette select) is excluded — Ula's palsel6b
    /// log owns it.
    void log_nr6b_change();

    /// Apply a control_byte from the change-log to the live state,
    /// updating the 5 owned fields without re-appending to the log.
    /// Bit 4 is intentionally ignored (Ula's territory).
    void replay_control_byte(uint8_t val);
};
