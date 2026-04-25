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
    void set_active_bank(uint8_t bank) { active_bank_ = bank & 0x7F; }
    uint8_t active_bank() const { return active_bank_; }

    /// NextREG 0x13: shadow 16K bank (default 11).
    void set_shadow_bank(uint8_t bank) { shadow_bank_ = bank & 0x7F; }
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

    /// True when in a wide mode (320×256 or 640×256).
    bool is_wide() const { return resolution_ != 0; }

    /// Enable/disable Layer 2 rendering (from NextREG 0x69 bit 7 or port 0x123B).
    void set_enabled(bool en) { enabled_ = en; }
    bool enabled() const { return enabled_; }

    // Clip window (NextREG 0x18, 4-write cycle)
    void set_clip_x1(uint8_t v) { clip_x1_ = v; }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; }
    uint8_t clip_x1() const { return clip_x1_; }
    uint8_t clip_x2() const { return clip_x2_; }
    uint8_t clip_y1() const { return clip_y1_; }
    uint8_t clip_y2() const { return clip_y2_; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// Render one scanline of Layer 2 into an ARGB8888 buffer.
    ///
    /// @param dst          Output buffer (render_width pixels wide).
    /// @param row          Framebuffer row (0–255).
    /// @param ram          Physical RAM for direct bank access.
    /// @param palette      Palette manager for Layer 2 colour lookup.
    /// @param render_width Output width: 320 or 640. When 640 and resolution
    ///                     is 640×256, renders both nibbles per byte.
    /// @param rom_in_sram  Next-mode flag — when true, apply VHDL zxnext.vhd:
    ///                     2964 +0x20 shift (in 8K-page units = +16 in 16K-
    ///                     bank units) to the active bank so the Layer 2
    ///                     fetch hits the same SRAM region that the MMU
    ///                     writes to via Mmu::to_sram_page.
    void render_scanline(uint32_t* dst, int row, const Ram& ram,
                         const PaletteManager& palette,
                         int render_width = 320,
                         bool rom_in_sram = false) const;

    /// Render one scanline using a specific bank, regardless of enabled_ state.
    /// Used by the debugger video panel to show active and shadow Layer 2 content.
    void render_scanline_debug(uint32_t* dst, int row, const Ram& ram,
                               const PaletteManager& palette, uint8_t bank,
                               int render_width = 320,
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

    /// Number of scroll changes recorded this frame (diagnostic).
    size_t change_log_size() const { return change_count_; }

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
    uint8_t  clip_x1_        = 0;
    uint8_t  clip_x2_        = 255;
    uint8_t  clip_y1_        = 0;
    uint8_t  clip_y2_        = 255;

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

    /// Append a snapshot of the live scroll_x_/scroll_y_ to the change
    /// log, tagged with current_line_. Called from the three public
    /// scroll setters above.
    void log_scroll_change();
};
