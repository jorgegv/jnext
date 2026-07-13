#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

class Ram;
class PaletteManager;

/// Layer 2 bitmap renderer (256×192 @ 8-bit colour).
///
/// Layer 2 reads pixel data directly from physical RAM banks, bypassing
/// the MMU.  The active bank (NextREG 0x12) selects the starting 16K bank;
/// three consecutive banks hold the 48K bitmap (3 thirds × 16K each).
///
/// Pixel layout (256×192, 8-bit):
///   Third 0 (rows   0– 63): bank N,   offset = row * 256 + x
///   Third 1 (rows  64–127): bank N+1, offset = (row-64) * 256 + x
///   Third 2 (rows 128–191): bank N+2, offset = (row-128) * 256 + x
///
/// Resolution modes (NextREG 0x70 bits 5:4):
///   00 = 256×192 @ 8-bit  (row-major:    addr = y * 256 + x)
///   01 = 320×256 @ 8-bit  (column-major: addr = x * 256 + y)
///   1x = 640×256 @ 4-bit  (column-major: addr = x * 256 + y, 2 pixels/byte)
///
/// VHDL reference: layer2.vhd (address generation, pixel output).
class Layer2 {
public:
    Layer2() = default;

    void reset();

    // -----------------------------------------------------------------
    // NextREG configuration
    // -----------------------------------------------------------------

    /// NextREG 0x12: active 16K bank (default 8).
    void set_active_bank(uint8_t bank) {
        active_bank_ = bank & 0x7F;
        log_bank_change();
    }
    uint8_t active_bank() const { return active_bank_; }

    /// NextREG 0x13: shadow 16K bank (default 11).
    void set_shadow_bank(uint8_t bank) {
        shadow_bank_ = bank & 0x7F;
        log_bank_change();
    }
    uint8_t shadow_bank() const { return shadow_bank_; }

    /// NextREG 0x16: X scroll LSB.
    void set_scroll_x_lsb(uint8_t val) {
        scroll_x_ = (scroll_x_ & 0x100) | val;
        log_scroll_change();
    }

    /// NextREG 0x71: X scroll MSB (bit 0 only).
    void set_scroll_x_msb(uint8_t val) {
        scroll_x_ = (scroll_x_ & 0xFF) | ((val & 1) << 8);
        log_scroll_change();
    }

    /// NextREG 0x17: Y scroll.
    void set_scroll_y(uint8_t val) {
        scroll_y_ = val;
        log_scroll_change();
    }

    /// Live X scroll (9-bit). Reflects the current per-line snapshot
    /// during render_frame after `apply_changes_for_line` advances it.
    /// Primarily for tests + debugger; production rendering reads the
    /// member directly.
    uint16_t scroll_x() const { return scroll_x_; }
    /// Live Y scroll (8-bit). Same caveat as `scroll_x()`.
    uint8_t  scroll_y() const { return scroll_y_; }

    /// NextREG 0x70: Layer 2 control.
    ///   bits 5:4 = resolution (00=256×192, 01=320×256, 1x=640×256)
    ///   bits 3:0 = palette offset
    void set_control(uint8_t val);

    /// Current resolution mode: 0=256×192, 1=320×256, 2/3=640×256.
    uint8_t resolution() const { return resolution_; }

    /// Live palette offset (bits 3:0 of NR 0x70). Reflects the current
    /// per-line snapshot during render_frame after `apply_changes_for_line`
    /// advances it. Primarily for tests + debugger.
    uint8_t palette_offset() const { return palette_offset_; }

    /// True when in a wide mode (320×256 or 640×256).
    bool is_wide() const { return resolution_ != 0; }

    /// Enable/disable Layer 2 rendering (from NextREG 0x69 bit 7 or port 0x123B).
    void set_enabled(bool en) {
        enabled_ = en;
        log_enable_change();
    }
    bool enabled() const { return enabled_; }

    // Clip window (NextREG 0x18, 4-write cycle).  Each setter logs a
    // 4-coord snapshot in the per-scanline clip change-log so that
    // mid-frame Copper writes to the rotating index produce the correct
    // per-line clip without relying on last-write-wins.
    void set_clip_x1(uint8_t v) { clip_x1_ = v; log_clip_change(); }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; log_clip_change(); }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; log_clip_change(); }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; log_clip_change(); }
    uint8_t clip_x1() const { return clip_x1_; }
    uint8_t clip_x2() const { return clip_x2_; }
    uint8_t clip_y1() const { return clip_y1_; }
    uint8_t clip_y2() const { return clip_y2_; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// Render one scanline of Layer 2 into an ARGB8888 buffer.
    ///
    /// Always renders 640 pixels into `dst`. The function pixel-doubles the
    /// 256×192 source (writes 512 cells centred at DISP_X=64) and the
    /// 320×256 source (writes the full 640) modes, and emits 640-native for
    /// resolution ≥ 2 (640×256 4bpp). Cells outside the painted range are
    /// left untouched (caller is expected to pre-fill with TRANSPARENT).
    ///
    /// @param dst              Output buffer — must hold at least 640 pixels.
    /// @param row              Framebuffer row (0–255).
    /// @param ram              Physical RAM for direct bank access.
    /// @param palette          Palette manager for Layer 2 colour lookup.
    /// @param transparent_rgb  NextREG 0x14 global transparency reference
    ///                         (8-bit RRRGGGBB), compared against the
    ///                         RRRGGGBB portion of each pixel's palette
    ///                         output (VHDL zxnext.vhd:7121: `layer2_rgb_2
    ///                         (8 downto 1) = transparent_rgb_2`). MUST be
    ///                         the caller's per-line-deferred snapshot, not
    ///                         a live NextREG value — Task 46: this used to
    ///                         be read internally via
    ///                         `palette.global_transparency()` (a *separate*
    ///                         live member from `Renderer::transparent_rgb_`,
    ///                         both written from the same NR 0x14 handler),
    ///                         so a mid-frame Copper MOVE to NR 0x14 could
    ///                         flip a pixel's transparency for the CURRENT
    ///                         row instead of only rows from the next
    ///                         scanline onward — and because this function
    ///                         *skip-writes* transparent pixels (leaves
    ///                         `dst[]` untouched at the caller's TRANSPARENT
    ///                         sentinel), a wrongly-transparent pixel was
    ///                         unrecoverably destroyed before it ever
    ///                         reached the compositor. `Renderer::render_row`
    ///                         passes `transparent_rgb_for_line(row)` (the
    ///                         SAME stage0/1a/1/2-pipelined snapshot
    ///                         `composite_scanline` reads, VHDL
    ///                         zxnext.vhd:1137,5226,6822,6912-6913,7078,
    ///                         wired by Task 45). `render_scanline_debug`
    ///                         deliberately does NOT do this — see its own
    ///                         doc comment.
    /// @param rom_in_sram  Next-mode flag — when true, apply VHDL zxnext.vhd:
    ///                     2964 +0x20 shift (in 8K-page units = +16 in 16K-
    ///                     bank units) to the active bank so the Layer 2
    ///                     fetch hits the same SRAM region that the MMU
    ///                     writes to via Mmu::to_sram_page.
    /// @param priority_dst Optional per-pixel priority output (size matches
    ///                     `dst`). When non-null, every emitted opaque pixel
    ///                     also writes the L2 palette priority bit (NR 0x44
    ///                     b7 = nr_palette_priority(1), VHDL zxnext.vhd:
    ///                     7050) to the corresponding cell. Transparent
    ///                     pixels (skipped via the `continue` paths) leave
    ///                     the buffer untouched, so the renderer's frame-
    ///                     start zero-fill is preserved. Used by the
    ///                     compositor to drive Layer 2 priority promotion
    ///                     above sprites (VHDL zxnext.vhd:7220). Pass
    ///                     nullptr from contexts (debugger view) that do
    ///                     not need per-pixel priority info.
    void render_scanline(uint32_t* dst, int row, const Ram& ram,
                         const PaletteManager& palette,
                         uint8_t transparent_rgb,
                         bool rom_in_sram = false,
                         bool* priority_dst = nullptr) const;

    /// Render one scanline using a specific bank, regardless of enabled_ state.
    /// Used by the debugger video panel to show active and shadow Layer 2 content.
    /// Always renders 640 pixels — see render_scanline doc.
    ///
    /// Deliberately reads `palette.global_transparency()` (the LIVE NR 0x14
    /// value) internally rather than taking a `transparent_rgb` parameter:
    /// this is the debugger's isolated-bank inspection view (a synthetic
    /// render of a chosen bank at the CURRENT paused instant, not a replay
    /// of a specific historical scanline within an in-progress frame — see
    /// `src/debugger/video_panel.cpp:393,399`), so there is no meaningful
    /// per-line snapshot to defer to. The debugger must OBSERVE the machine
    /// state, never change what it observes, and must not be forced through
    /// the render-time deferral path that exists solely to match the
    /// hardware's mid-frame Copper-write pipeline timing.
    void render_scanline_debug(uint32_t* dst, int row, const Ram& ram,
                               const PaletteManager& palette, uint8_t bank,
                               bool rom_in_sram = false);

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    // -----------------------------------------------------------------
    // Per-scanline scroll snapshot
    // -----------------------------------------------------------------
    //
    // Mirrors PaletteManager's per-scanline change-log (see palette.h
    // §Per-scanline palette snapshot). Required for demos that change
    // L2 scroll mid-frame via Copper to produce parallax / perspective
    // bands (e.g. beast.nex bottom-grass area, parallax.nex). Renderer
    // flow:
    //
    //   layer2_.start_frame();           // emulator at frame start
    //   …                                // emulation runs; scroll
    //                                    // writes append to change_log_
    //                                    // tagged with current_line_
    //   layer2_.rewind_to_baseline();    // before render_frame
    //   for row in 0..H:
    //       layer2_.apply_changes_for_line(row);
    //       render_scanline(row);

    /// Snapshot the live scroll state as the frame baseline and reset
    /// the per-frame change log. Called at the start of every frame.
    void start_frame();

    /// Update the scanline tag attached to subsequent scroll writes.
    /// Called from Emulator::on_scanline. Default 0 at frame start.
    void set_current_line(int line) {
        current_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live scroll state to the frame baseline and reset
    /// the render cursor. Called once before per-scanline render.
    void rewind_to_baseline();

    /// Apply all logged scroll changes whose line tag equals `line`.
    /// Cursor is monotonically advanced; the log is in scanline order
    /// so total work across a frame is O(change_count_).
    void apply_changes_for_line(int line);

    /// Apply every remaining log entry across all five Layer 2 logs
    /// (scroll, clip, bank, enable, NR 0x70), regardless of line tag.
    /// Called after the per-line render loop so writes that landed in
    /// vblank still update the live state — without this they are lost
    /// (rewind has undone the live mutation, and apply_changes_for_line
    /// (0..H-1) never matches a vblank line). Mirrors
    /// Tilemap::flush_remaining_nr6b_changes.
    void flush_remaining_changes();

    /// Number of scroll changes recorded this frame (diagnostic).
    size_t change_log_size() const { return change_count_; }

    /// Number of clip-window snapshots recorded this frame (diagnostic).
    size_t clip_change_log_size() const { return clip_change_count_; }

    /// Number of bank (NR 0x12 / NR 0x13) changes recorded (diagnostic).
    size_t bank_change_log_size() const { return bank_change_count_; }

    /// Number of enable changes recorded this frame (diagnostic).
    size_t enable_change_log_size() const { return enable_change_count_; }

    /// Number of NR 0x70 (resolution + palette offset) changes recorded
    /// this frame (diagnostic).
    size_t nr70_change_log_size() const { return nr70_change_count_; }

    /// Static cap; further writes after this many in a frame are
    /// silently dropped (with a once-per-frame warn). Sized for the
    /// worst-case "Copper writes scroll on every scanline" scenario.
    static constexpr size_t MAX_CHANGES_PER_FRAME = 1024;

private:
    uint8_t  active_bank_    = 8;     // NextREG 0x12 default
    uint8_t  shadow_bank_    = 11;    // NextREG 0x13 default
    uint16_t scroll_x_       = 0;     // 9-bit X scroll (0x16 + 0x71)
    uint8_t  scroll_y_       = 0;     // NextREG 0x17
    uint8_t  palette_offset_ = 0;     // bits 3:0 of NextREG 0x70
    uint8_t  resolution_     = 0;     // bits 5:4 of NextREG 0x70 (0=256×192)
    bool     enabled_        = false;
    // VHDL zxnext.vhd:4959-4962 NR 0x18 (Layer 2 clip) defaults:
    //   clip_x1=0x00, clip_x2=0xFF, clip_y1=0x00, clip_y2=0xBF (191).
    uint8_t  clip_x1_        = 0x00;
    uint8_t  clip_x2_        = 0xFF;
    uint8_t  clip_y1_        = 0x00;
    uint8_t  clip_y2_        = 0xBF;

    // ── Per-scanline change log ──────────────────────────────────────
    struct ScrollChange {
        uint16_t line;             ///< 0..lines_per_frame-1
        uint16_t scroll_x;         ///< 9-bit X scroll snapshot after change
        uint8_t  scroll_y;         ///< Y scroll snapshot after change
    };

    std::array<ScrollChange, MAX_CHANGES_PER_FRAME> change_log_{};
    size_t   change_count_    = 0;
    uint16_t current_line_    = 0;
    size_t   render_cursor_   = 0;
    bool     overflow_warned_ = false;

    uint16_t baseline_scroll_x_ = 0;
    uint8_t  baseline_scroll_y_ = 0;

    // ── Clip-window per-scanline change log (G05) ────────────────────
    // VHDL zxnext.vhd:5243, 5278 — NR 0x18 rotating index writes.
    // Snapshot of all 4 coordinates is captured per write.
    struct ClipChange {
        uint16_t line;
        uint8_t  x1, x2, y1, y2;
    };
    std::array<ClipChange, MAX_CHANGES_PER_FRAME> clip_change_log_{};
    size_t   clip_change_count_   = 0;
    size_t   clip_render_cursor_  = 0;
    bool     clip_overflow_warned_= false;
    uint8_t  baseline_clip_x1_    = 0x00;
    uint8_t  baseline_clip_x2_    = 0xFF;
    uint8_t  baseline_clip_y1_    = 0x00;
    uint8_t  baseline_clip_y2_    = 0xBF;

    // ── Bank per-scanline change log (G09) ───────────────────────────
    // VHDL zxnext.vhd:5220, 1135 — NR 0x12 / NR 0x13 active-bank writes.
    struct BankChange {
        uint16_t line;
        uint8_t  active_bank;
        uint8_t  shadow_bank;
    };
    std::array<BankChange, MAX_CHANGES_PER_FRAME> bank_change_log_{};
    size_t   bank_change_count_    = 0;
    size_t   bank_render_cursor_   = 0;
    bool     bank_overflow_warned_ = false;
    uint8_t  baseline_active_bank_ = 8;
    uint8_t  baseline_shadow_bank_ = 11;

    // ── Enable per-scanline change log (G14) ─────────────────────────
    // VHDL zxnext.vhd:3916, 3924-3925 — port 0x123B b1 / NR 0x69 b7.
    struct EnableChange {
        uint16_t line;
        bool     enabled;
    };
    std::array<EnableChange, MAX_CHANGES_PER_FRAME> enable_change_log_{};
    size_t   enable_change_count_    = 0;
    size_t   enable_render_cursor_   = 0;
    bool     enable_overflow_warned_ = false;
    bool     baseline_enabled_       = false;

    // ── NR 0x70 per-scanline change log (G06.NR70-01) ────────────────
    // VHDL zxnext.vhd:5475-5477 — NR 0x70 captures bits 5:4 (resolution)
    // and bits 3:0 (palette offset). VHDL layer2.vhd:113 samples
    // i_resolution every CLK_7 cycle into layer2_resolution_q, and
    // layer2.vhd:147,150,160 use the live value to choose the row/column-
    // major address path. A mid-frame Copper write to NR 0x70 must
    // reroute the renderer width/path on the very next scanline. Without
    // this log, the renderer would see only the end-of-frame value
    // (last-write-wins) and collapse all scanlines onto the same path.
    struct Nr70Change {
        uint16_t line;
        uint8_t  resolution;       ///< bits 5:4 of NR 0x70
        uint8_t  palette_offset;   ///< bits 3:0 of NR 0x70
    };
    std::array<Nr70Change, MAX_CHANGES_PER_FRAME> nr70_change_log_{};
    size_t   nr70_change_count_    = 0;
    size_t   nr70_render_cursor_   = 0;
    bool     nr70_overflow_warned_ = false;
    uint8_t  baseline_resolution_     = 0;
    uint8_t  baseline_palette_offset_ = 0;

    /// Append a snapshot of the live scroll_x_/scroll_y_ to the change
    /// log, tagged with current_line_. Called from the three public
    /// scroll setters above.
    void log_scroll_change();
    /// Append a 4-coord snapshot of the live clip-window state.
    void log_clip_change();
    /// Append a snapshot of active_bank_ / shadow_bank_.
    void log_bank_change();
    /// Append a snapshot of enabled_.
    void log_enable_change();
    /// Append a snapshot of resolution_ / palette_offset_ (NR 0x70 split).
    void log_nr70_change();
};
