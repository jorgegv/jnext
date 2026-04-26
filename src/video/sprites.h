#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

class Ram;
class PaletteManager;

/// ZX Spectrum Next sprite engine — 128 hardware sprites, 16x16 pixels each.
///
/// Implements the sprite subsystem as defined in the FPGA VHDL (sprites.vhd).
///
/// Sprite attribute format (5 bytes per sprite, written via port 0x57):
///   Byte 0: X position bits 7:0
///   Byte 1: Y position bits 7:0
///   Byte 2: bits 7:4 = palette offset, bit 3 = X mirror, bit 2 = Y mirror,
///           bit 1 = rotate, bit 0 = X MSB (bit 8)
///   Byte 3: bit 7 = visible, bit 6 = extended (5th byte present),
///           bits 5:0 = pattern index N5:N0
///   Byte 4: bit 7 = 4-bit colour (H flag), bit 6 = N6 (7th pattern bit),
///           bit 5 = reserved, bits 4:3 = X scale, bits 2:1 = Y scale,
///           bit 0 = Y MSB (bit 8)
///
/// When byte 3 bit 6 is clear (no extended attributes), only 4 bytes are
/// written per sprite and byte 4 retains its previous value.  The Y MSB is
/// forced to 0 in that case (hardware: spr_y8 = 0 when attr3(6)=0).
///
/// Pattern memory is 16 KB (16384 bytes), addressed as:
///   8-bit mode: pattern[6:0] & row[3:0] & col[3:0]  (256 bytes per pattern)
///   4-bit mode: pattern[6:1] & N6 & row[3:0] & col[3:1] (128 bytes per pattern)
///
/// Per-sprite X/Y scaling (x1/x2/x4/x8) is implemented via extended byte 4.
/// Sprite anchoring (composite/relative sprites) is implemented.
class SpriteEngine {
public:
    static constexpr int NUM_SPRITES    = 128;
    static constexpr int PATTERN_RAM_SZ = 16384;  // 16 KB, 14-bit address
    static constexpr int SPRITE_SIZE    = 16;
    static constexpr int DISPLAY_WIDTH  = 320;     // full pixel width including border

    SpriteEngine() { reset(); }

    void reset();

    // -----------------------------------------------------------------
    // Port handlers
    // -----------------------------------------------------------------

    /// Port 0x303B write: select sprite slot for attribute/pattern writes.
    ///   bits 6:0 = sprite index (0-127)
    ///   bit 7    = pattern slot high bit (pattern_index bit 7)
    void write_slot_select(uint8_t val);

    /// Port 0x303B read: sprite status register.
    ///   bit 1 = max sprites per line exceeded (sticky, cleared on read)
    ///   bit 0 = collision detected (sticky, cleared on read)
    uint8_t read_status();

    /// Port 0x57 write: auto-incrementing sprite attribute upload.
    /// Writes 4 or 5 bytes per sprite depending on byte 3 bit 6 (extended).
    void write_attribute(uint8_t val);

    /// Port 0x5B write: auto-incrementing pattern data upload.
    void write_pattern(uint8_t val);

    // -----------------------------------------------------------------
    // NextREG handlers
    // -----------------------------------------------------------------

    /// NextREG 0x15 bit 0: global sprite visibility.
    void set_sprites_visible(bool vis) { sprites_visible_ = vis; }
    bool sprites_visible() const { return sprites_visible_; }

    /// NextREG 0x19: sprite clip window X1.
    void set_clip_x1(uint8_t val) { clip_x1_ = val; }

    /// NextREG 0x1A: sprite clip window X2.
    void set_clip_x2(uint8_t val) { clip_x2_ = val; }

    /// NextREG 0x1B: sprite clip window Y1.
    void set_clip_y1(uint8_t val) { clip_y1_ = val; }

    /// NextREG 0x1C: sprite clip window Y2.
    void set_clip_y2(uint8_t val) { clip_y2_ = val; }

    /// Observability getters for sprite clip window (NR 0x19 write state).
    /// Mirror the Layer2/Tilemap/Ula convention so the NextREG integration
    /// tests can verify the 4-write cycle + NR 0x1C bit 1 reset.
    uint8_t clip_x1() const { return clip_x1_; }
    uint8_t clip_x2() const { return clip_x2_; }
    uint8_t clip_y1() const { return clip_y1_; }
    uint8_t clip_y2() const { return clip_y2_; }

    /// NextREG 0x34 write: set sprite attribute slot index (alternative to
    /// port 0x303B).  bits 6:0 = sprite index, bit 7 = pattern MSB.
    void set_attr_slot(uint8_t val);

    /// NextREG 0x75-0x79: direct sprite attribute byte writes for the
    /// currently selected sprite slot.
    void write_attr_byte(uint8_t byte_idx, uint8_t val);

    /// NextREG 0x09 bit 3: sprites rendered over border (1) or clipped to
    /// display area (0).
    void set_over_border(bool val) { over_border_ = val; }

    /// NextREG 0x15 bit 5: border-clip enable.  When 0 and over_border=1,
    /// the sprite clip window is ignored and the full 320x256 area is drawn
    /// (VHDL sprites.vhd lines 1043-1048).  Default is 0 at power-on.
    void set_border_clip_en(bool val) { border_clip_en_ = val; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// Render sprites for one scanline into a 320-pixel ARGB8888 buffer.
    ///
    /// Sprites are rendered in order 0..127.  Later (higher-index) sprites
    /// appear on top of earlier ones by default.  The zero_on_top flag
    /// (NextREG 0x15 bit 5) reverses this so sprite 0 is on top.
    ///
    /// @param dst       Output buffer, 320 pixels wide (same layout as ULA
    ///                  framebuffer: 32px border + 256px display + 32px border).
    /// @param y         Scanline number in display coordinates (0-255 visible).
    /// @param palette   Palette manager for sprite colour lookups.
    void render_scanline(uint32_t* dst, int y,
                         const PaletteManager& palette) const;

    /// Render one scanline regardless of global sprites_visible_ flag.
    /// Used by the debugger video panel.
    void render_scanline_debug(uint32_t* dst, int y,
                               const PaletteManager& palette);

    /// Set whether sprite 0 is drawn on top (true) or behind (false, default).
    void set_zero_on_top(bool val) { zero_on_top_ = val; }

    /// Debug: log sprite 0 state and internal counters.
    void debug_log_sprite0() const;

    // -----------------------------------------------------------------
    // Per-scanline attribute and pattern change log
    // -----------------------------------------------------------------
    //
    // VHDL sprites.vhd lines 327-470: sprite attributes live in 5 dual-port
    // RAMs (attr0..attr4) — sync-write from CPU, async-read by the per-line
    // sprite FSM. Mid-frame writes therefore take effect on the very next
    // scanline rendered, while writes that arrive *during* a scanline's
    // qualify-and-process pass take effect for that line too (the FSM reads
    // the latest committed value at qualify time).
    //
    // VHDL sprites.vhd lines 561-572: the 16 KB sprite pattern RAM is a
    // single simple-dual-port BRAM (sdpbram_16k_8) with the same sync-write
    // / async-read shape — port A driven by CPU port 0x5B (pattern_we),
    // port B driven by the sprite FSM's spr_pat_addr at render time. Like
    // the attribute side, mid-frame port 0x5B writes alter the bytes
    // returned for any subsequent scanline's sprite render. parallax.nex
    // exploits this by Z80N-DMA-streaming ~92 pattern bytes per frame
    // across 311 distinct scanlines (peak 256 writes in a single 1 ms
    // burst, ~21 distinct pattern slots cycled per frame) to multiplex
    // platform / lava-column / crystal sprites across vertical bands.
    //
    // The C++ engine renders an entire scanline in one synchronous call from
    // Renderer::render_frame, which itself fires once per frame from
    // Emulator::run_frame after the CPU has executed the whole frame's
    // worth of instructions and DMA. Without per-scanline replay, the
    // sprite-attribute / sprite-pattern snapshots used for *every*
    // scanline are the end-of-frame state — so DMA-driven sprite-
    // multiplexing demos (parallax.nex) collapse to the very last
    // upload's positions and pixel data.
    //
    // Mirrors the exact pattern in PaletteManager (palette.{h,cpp}) and
    // Layer2 (layer2.{h,cpp}, commit f448b4f for beast.nex parallax). The
    // attribute and pattern logs share the same start_frame /
    // set_current_line / rewind_to_baseline / apply_changes_for_line
    // wiring (single set of public methods).
    //
    //   sprites_.start_frame();              // emulator at frame start
    //   …                                    // emulation runs; attribute
    //                                        //   AND pattern writes
    //                                        //   append to their change
    //                                        //   logs tagged with
    //                                        //   current_line_
    //   sprites_.rewind_to_baseline();       // before render_frame
    //   for row in 0..H:
    //       sprites_.apply_changes_for_line(row);
    //       render_scanline(row);

    /// Snapshot the live attribute table and pattern RAM as the frame
    /// baselines and reset both per-frame change logs. Called at the
    /// start of every frame. Also flushes any pattern-log entries the
    /// previous render didn't replay (writes tagged at lines >=
    /// FB_HEIGHT) into live RAM before snapshotting, so the baseline
    /// reflects the true end-of-prev-frame hardware state.
    void start_frame();

    /// Update the scanline tag attached to subsequent attribute writes.
    /// Called from Emulator::on_scanline. Default 0 at frame start.
    void set_current_line(int line) {
        current_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live attribute table and pattern RAM to the frame
    /// baselines and reset the render cursors. Called once before
    /// per-scanline render.
    void rewind_to_baseline();

    /// Apply all logged attribute and pattern changes whose line tag
    /// equals `line`. Both cursors are monotonically advanced; logs are
    /// in scanline order so total work across a frame is
    /// O(change_count_ + pattern_change_count_).
    void apply_changes_for_line(int line);

    /// Number of attribute changes recorded this frame (diagnostic).
    size_t change_log_size() const { return change_count_; }

    /// Number of pattern-byte changes recorded this frame (diagnostic).
    size_t pattern_change_log_size() const { return pattern_change_count_; }

    /// Static cap; further writes after this many in a frame are silently
    /// dropped (with a once-per-frame warn). Sized for the worst-case
    /// "Copper / Z80N-DMA writes hundreds of attribute bytes per scanline"
    /// scenario. parallax.nex measured ~270 attribute writes per frame at
    /// 50 Hz; 8192 leaves a 30× headroom. KNOWN LIMIT: extreme per-scanline
    /// DMA streaming demos (>8192 attribute byte-writes/frame, e.g. >32 byte
    /// re-uploads/scanline at 256 lines) will overflow — beyond that point
    /// the engine warns once and drops further log entries this frame.
    static constexpr size_t MAX_CHANGES_PER_FRAME = 8192;

    /// Static cap for the per-scanline pattern change log. Sized for the
    /// worst-case "Copper / Z80N-DMA writes hundreds of pattern bytes per
    /// scanline" scenario. parallax.nex measured ~92 pattern writes per
    /// frame at 50 Hz with a 256-write peak inside a single 1 ms DMA
    /// burst; 8192 gives ~90× headroom over typical and ~32× over peak.
    /// KNOWN LIMIT: a demo that re-streams the entire 16 KB pattern RAM
    /// per frame (16384 bytes) would overflow — beyond that point the
    /// engine warns once and drops further entries this frame.
    static constexpr size_t MAX_PATTERN_CHANGES_PER_FRAME = 8192;

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    // -----------------------------------------------------------------
    // Debug / introspection accessors
    // -----------------------------------------------------------------

    /// Read a raw attribute byte (0-4) for the given sprite index.
    /// Returns 0 if idx >= 128 or byte_idx >= 5.
    uint8_t read_attr_byte(uint8_t sprite_idx, uint8_t byte_idx) const;

    /// Decoded sprite info for debugger display.
    struct SpriteInfo {
        int      x;
        int      y;
        uint8_t  pattern;
        uint8_t  palette_offset;
        bool     visible;
        bool     x_mirror;
        bool     y_mirror;
        bool     rotate;
        bool     is_4bit;
        uint8_t  x_scale;       // 0=1x, 1=2x, 2=4x, 3=8x
        uint8_t  y_scale;
    };

    /// Get decoded sprite info. Returns zeroed struct if idx >= 128.
    SpriteInfo get_sprite_info(uint8_t idx) const;

private:
    /// Internal representation of one sprite's 5-byte attribute set.
    struct SpriteAttr {
        uint8_t byte0 = 0;  // X LSB
        uint8_t byte1 = 0;  // Y LSB
        uint8_t byte2 = 0;  // palette_offset(7:4), xmirror(3), ymirror(2), rotate(1), x_msb(0)
        uint8_t byte3 = 0;  // visible(7), extended(6), pattern(5:0)
        uint8_t byte4 = 0;  // 4bit(7), N6(6), resv(5), xscale(4:3), yscale(2:1), y_msb(0)

        // Decoded accessors
        int      x()             const { return ((byte2 & 0x01) << 8) | byte0; }
        int      y()             const;
        bool     visible()       const { return (byte3 & 0x80) != 0; }
        bool     extended()      const { return (byte3 & 0x40) != 0; }
        uint8_t  pattern_base()  const { return byte3 & 0x3F; }  // N5:N0
        uint8_t  pattern_n6()    const { return (extended() && is_4bit()) ? ((byte4 >> 6) & 1) : 0; }
        uint8_t  pattern_7bit()  const { return (pattern_base() << 1) | pattern_n6(); }
        uint8_t  palette_offset()const { return (byte2 >> 4) & 0x0F; }
        bool     x_mirror()      const { return (byte2 & 0x08) != 0; }
        bool     y_mirror()      const { return (byte2 & 0x04) != 0; }
        bool     rotate()        const { return (byte2 & 0x02) != 0; }
        bool     is_4bit()       const { return extended() && ((byte4 & 0x80) != 0); }

        // Scale factors: 0=1x, 1=2x, 2=4x, 3=8x (only valid when extended)
        uint8_t  x_scale()       const { return extended() ? ((byte4 >> 3) & 0x03) : 0; }
        uint8_t  y_scale()       const { return extended() ? ((byte4 >> 1) & 0x03) : 0; }

        // Scaled sprite dimensions
        int      width()         const { return SPRITE_SIZE << x_scale(); }
        int      height()        const { return SPRITE_SIZE << y_scale(); }

        // Anchor/relative sprite detection (VHDL sprites.vhd)
        // A sprite is relative when: extended AND byte4 bits 7:6 = "01"
        bool     is_relative()   const { return extended() && ((byte4 & 0xC0) == 0x40); }
        // Anchor type: byte4 bit 5 (only meaningful for non-relative extended sprites)
        // Type 0: relatives inherit position/pattern/palette/h4bit only
        // Type 1: additionally inherit mirror/rotate/scale with XOR
        bool     is_anchor_type1() const { return extended() && ((byte4 & 0x20) != 0); }
    };

    SpriteAttr sprites_[NUM_SPRITES];
    uint8_t    pattern_ram_[PATTERN_RAM_SZ];

    // Port 0x57 attribute upload state
    uint8_t    attr_slot_  = 0;    // current sprite index (0-127)
    uint8_t    attr_byte_  = 0;    // which attribute byte (0-4) we're writing next

    // Port 0x5B pattern upload state
    uint16_t   pattern_offset_ = 0;  // auto-incrementing, 14-bit

    // Port 0x303B pattern slot MSB (bit 7 of write to 0x303B)
    uint8_t    pattern_slot_msb_ = 0;

    // Configuration
    bool       sprites_visible_ = false;
    bool       over_border_     = false;
    bool       zero_on_top_     = false;
    bool       border_clip_en_  = false;  // NR 0x15 bit 5; default 0 (no border clip)

    // Clip window (VHDL defaults: 0x00,0xFF,0x00,0xBF)
    uint8_t    clip_x1_ = 0;
    uint8_t    clip_x2_ = 255;
    uint8_t    clip_y1_ = 0;
    uint8_t    clip_y2_ = 0xBF;   // 191 — maps to Y=223 (display bottom) in non-over-border

    // Status (sticky flags, cleared on read of port 0x303B)
    mutable bool collision_     = false;
    mutable bool max_sprites_   = false;

    // Per-scanline cycle budget for the sprite engine's overtime model.
    //
    // Behavioural approximation of the VHDL sprite FSM
    // (sprites.vhd:285 state_t, :830-864 transitions, :977 sprites_overtime).
    // The FSM runs on the master clock and must QUALIFY all 128 slots plus
    // PROCESS (render) each visible in-range sprite before the next
    // line_reset pulse — if it is still busy when the next line starts,
    // bit 1 of the status register is latched.
    //
    // We model this with a per-sprite cycle cost:
    //   - 1 QUALIFY cycle per slot (always charged, all 128 slots)
    //   - width() PROCESS cycles per visible in-range sprite (16/32/64/128)
    //
    // Budget rationale (not an exact hardware timing — a calibration point
    // matched to documented ZX Next informal limits):
    //   - 128 slots × QUALIFY = 128 cycles (always)
    //   - 100 visible 16-px anchors on one line = 1600 PROCESS cycles
    //     -> total 1728 <= 1792 (no overtime) — matches the commonly cited
    //     "~100 sprites per line" figure.
    //   - 128 visible 16-px anchors on one line = 2176 cycles -> overtime
    //     fires (G13.OT-02 expectation).
    // This is not an exact VHDL cycle count; the FSM actually runs at the
    // master clock with per-state latencies, but the emulator renders a
    // full line in a single call and has no per-cycle stepping. Use the
    // above budget as a single calibrated threshold.
    static constexpr int SPR_LINE_BUDGET_CYCLES = 1792;

    // Anchor state for composite sprite chains
    struct AnchorState {
        bool     type1      = false;  // type 0 vs type 1
        bool     h4bit      = false;  // anchor's 4-bit pattern flag
        bool     visible    = false;
        int      x          = 0;      // 9-bit position
        int      y          = 0;
        uint8_t  pattern    = 0;      // 7-bit pattern (N5:N0 << 1 | N6)
        uint8_t  pal_offset = 0;      // 4-bit palette offset
        bool     rotate     = false;
        bool     x_mirror   = false;
        bool     y_mirror   = false;
        uint8_t  x_scale    = 0;      // 2-bit scale
        uint8_t  y_scale    = 0;
    };

    // Update anchor state from a non-relative sprite
    static void update_anchor(AnchorState& anchor, const SpriteAttr& spr);

    // Compute effective SpriteAttr for a relative sprite
    static SpriteAttr resolve_relative(const SpriteAttr& rel, const AnchorState& anchor);

    // Helpers
    void render_sprite_scanline(uint32_t* dst, const SpriteAttr& spr, int y,
                                const PaletteManager& palette,
                                bool* line_occupied) const;

    uint8_t read_pattern(uint16_t addr) const {
        return pattern_ram_[addr & (PATTERN_RAM_SZ - 1)];
    }

    // ── Per-scanline change log ──────────────────────────────────────
    //
    // Each attribute entry is a single attribute-byte write. Per-byte
    // granularity mirrors the VHDL CPU-side write port (one byte
    // committed per cycle through `attr_data` to one of attr0..attr4)
    // and avoids the cost of snapshotting the full 640-byte table on
    // every change.
    struct AttrChange {
        uint16_t line;       ///< 0..lines_per_frame-1
        uint8_t  slot;       ///< 0..127
        uint8_t  byte_index; ///< 0..4
        uint8_t  value;
    };

    // Pattern entries mirror VHDL sprites.vhd:561-572 (sdpbram_16k_8,
    // pattern_we / pattern_a). One entry per byte committed via port
    // 0x5B; pattern_offset is the 14-bit address that received the
    // byte, captured AFTER the port-0x5B-driven auto-increment is
    // applied to pattern_offset_ (so it points at the byte that was
    // written, not the next byte). Layout chosen to keep struct size
    // at 6 bytes for cache-friendly streaming.
    struct PatternChange {
        uint16_t line;            ///< 0..lines_per_frame-1
        uint16_t pattern_offset;  ///< 0..PATTERN_RAM_SZ-1 (14-bit)
        uint8_t  value;
        uint8_t  pad;             ///< explicit 6-byte alignment
    };

    // Baseline + logs are large enough that allocating on the heap once
    // is reasonable — but to mirror the Layer2/Palette pattern (no heap
    // allocation in the hot path, no save-state) we keep them inline.
    // Total size: 5×128 (attr base) + 8192×5 (attr log) + 16384 (pattern
    // base) + 8192×6 (pattern log) ≈ 106 KB per SpriteEngine.
    SpriteAttr baseline_sprites_[NUM_SPRITES];
    std::array<AttrChange, MAX_CHANGES_PER_FRAME> change_log_{};
    size_t   change_count_    = 0;
    uint16_t current_line_    = 0;
    size_t   render_cursor_   = 0;
    bool     overflow_warned_ = false;

    // Pattern-side change log + frame baseline (mirrors attribute side).
    uint8_t  baseline_pattern_ram_[PATTERN_RAM_SZ];
    std::array<PatternChange, MAX_PATTERN_CHANGES_PER_FRAME> pattern_change_log_{};
    size_t   pattern_change_count_      = 0;
    size_t   pattern_render_cursor_     = 0;
    bool     pattern_overflow_warned_   = false;

    /// Append a snapshot of (slot, byte_index, value) to the attribute
    /// change log, tagged with current_line_. Called from every
    /// attribute setter.
    void log_attr_change(uint8_t slot, uint8_t byte_index, uint8_t value);

    /// Append a snapshot of (pattern_offset, value) to the pattern
    /// change log, tagged with current_line_. Called from write_pattern
    /// (port 0x5B) for every CPU-side pattern byte commit.
    void log_pattern_change(uint16_t pattern_offset, uint8_t value);
};
