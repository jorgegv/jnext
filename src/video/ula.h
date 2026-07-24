#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

class Mmu;
class Ram;
class PaletteManager;

/// Timex ULA screen mode, selected via port 0xFF.
///
/// Port 0xFF bit layout:
///   bits 2:0  = screen bank (0 = primary 0x4000, 1 = alternate 0x6000)
///   bits 5:3  = video mode
///
/// Only the mode bits (5:3) are decoded here; the screen bank field is used by
/// STANDARD_1 mode and is implicit in HI_COLOUR / HI_RES.
enum class TimexScreenMode : uint8_t {
    STANDARD   = 0,  ///< Normal ULA 256×192; pixel+attr from 0x4000/0x5800
    STANDARD_1 = 1,  ///< Alternate screen: pixel+attr from 0x6000/0x7800
    HI_COLOUR  = 2,  ///< 256×192; pixels from 0x4000, per-row-column attr from 0x6000
    HI_RES     = 6,  ///< 512-wide monochrome rendered natively (s0/s1 byte-interleaved per VHDL)
};

/// ULA pixel renderer.
///
/// Renders the full 640×256 output frame into a uint32_t (ARGB8888) framebuffer
/// by reading pixel data and attributes from VRAM via the MMU.
///
/// Layout of the 640×256 output framebuffer (G104 canonical 640px):
///
///   ┌──────────────────────────────────────┐
///   │         32px top border              │  rows 0–31
///   ├────┬─────────────────────────┬───────┤
///   │ 64 │  512×192 display area   │  64   │  rows 32–223
///   │ px │                         │  px   │
///   ├────┴─────────────────────────┴───────┤
///   │         32px bottom border           │  rows 224–255
///   └──────────────────────────────────────┘
///
/// Left/right borders are each 64 pixels wide (64 + 512 + 64 = 640).
/// Top and bottom borders are each 32 rows (32 + 192 + 32 = 256), symmetric.
/// In STANDARD/STANDARD_1/HI_COLOUR each source bit emits two adjacent
/// framebuffer cells (VHDL zxula.vhd:390-393 — lo-res shift register doubles
/// every shift_pbyte bit).  In HI_RES the renderer emits a true 512-pixel
/// display with bytes from screens 0 and 1 byte-interleaved
/// (VHDL zxula.vhd:389 — shift_reg_32 = pbyte_hi & abyte_hi & pbyte_lo & abyte_lo).
class Ula {
public:
    // Output framebuffer dimensions (G104: canonical 640).
    static constexpr int FB_WIDTH   = 640;
    static constexpr int FB_HEIGHT  = 256;

    // Active display area within the framebuffer
    static constexpr int DISP_X     = 64;   // left border width in output pixels
    static constexpr int DISP_Y     = 32;   // top border height in output pixels
    static constexpr int DISP_W     = 512;
    static constexpr int DISP_H     = 192;

    /// Reset ULA state to power-on defaults (preserves palette/RAM pointers).
    void reset() {
        ula_enabled_ = true;
        clip_x1_ = 0; clip_x2_ = 255; clip_y1_ = 0; clip_y2_ = 191;
        border_colour_ = 7;
        border_per_line_.fill(7);
        flash_counter_ = 0;
        flash_phase_ = false;
        screen_mode_reg_ = 0;
        mode_ = TimexScreenMode::STANDARD;

        // Phase-1 scaffold state (VHDL zxnext.vhd:4987-5029 reset defaults).
        ula_scroll_x_coarse_ = 0;   // nr_26_ula_scrollx reset X"00"
        ula_scroll_y_        = 0;   // nr_27_ula_scrolly reset X"00"
        ula_fine_scroll_x_   = false; // nr_68_ula_fine_scroll_x reset '0'
        // VHDL zxnext.vhd:5002 — nr_42_ulanext_format reset = X"07".
        ulanext_format_      = 0x07;
        ulanext_en_          = false; // nr_43_ulanext_en reset '0'
        select_bgnd_argb_    = 0xFFFF00FFu; // NR $4A reset X"E3" expanded (zxnext.vhd:5014)
        ulap_en_             = false; // port_ff3b_ulap_en reset '0' (zxnext.vhd:4547)
        ulap_en_per_line_.fill(false);  // gap G11 — mirror reset default
        ulap_mode_           = 0;     // port_bf3b_ulap_mode reset "00" (zxnext.vhd:4529)
        ulap_index_          = 0;     // port_bf3b_ulap_index reset "000000" (zxnext.vhd:4530)
        alt_file_            = false; // port 0xFF bit 0 (screen bank) default 0
        shadow_screen_en_    = false; // i_ula_shadow_en default '0'
        border_clr_tmx_src_  = false; // hi-res/tmx border route selector (Wave D)

        // Per-scanline port-0xFF change-log (G07).
        port_ff_count_           = 0;
        current_line_            = 0;
        port_ff_render_cursor_   = 0;
        port_ff_overflow_warned_ = false;
        baseline_port_ff_        = screen_mode_reg_;
        // Per-scanline scroll change-log (G08).
        scroll_change_count_       = 0;
        current_scroll_line_       = 0;
        scroll_render_cursor_      = 0;
        scroll_overflow_warned_    = false;
        baseline_scroll_x_coarse_  = 0;
        baseline_scroll_y_         = 0;
        baseline_fine_scroll_x_    = 0;
        // Per-scanline active-palette selectors (G10).
        active_ula_palette_  = false;
        active_l2_palette_   = false;
        active_spr_palette_  = false;
        active_tm_palette_   = false;
        palsel43_change_count_     = 0;
        palsel43_render_cursor_    = 0;
        palsel43_overflow_warned_  = false;
        baseline_active_ula_pal_   = false;
        baseline_active_l2_pal_    = false;
        baseline_active_spr_pal_   = false;
        palsel6b_change_count_     = 0;
        palsel6b_render_cursor_    = 0;
        palsel6b_overflow_warned_  = false;
        baseline_active_tm_pal_    = false;
        palsel_current_line_       = 0;
    }

    /// Set the palette manager reference (must be called before rendering).
    void set_palette(PaletteManager* pal) { palette_ = pal; }

    /// Set the RAM reference for direct VRAM access (bypasses MMU, like real hardware).
    void set_ram(Ram* ram) { ram_ = ram; }
    /// Dedicated bank-7 lower-half BRAM (VHDL bank7_ram dpram2); wired by
    /// Emulator::init from Mmu::bank7_bram(). Null = legacy page-14 fallback.
    void set_bank7_bram(const uint8_t* p) { bank7_bram_ = p; }
    /// Dedicated bank-5 16K VRAM (VHDL bank5_ram dpram2, zxnext.vhd:6558;
    /// ULA port-B fetch at :6633-6661). Wired by Emulator::init from
    /// Mmu::bank5_vram() on Next machines; null = legacy physical-page-10
    /// fallback (standalone machines, unit tests). Task 25.
    void set_bank5_vram(const uint8_t* p) { bank5_vram_ = p; }

    /// Set the border colour (bits 2:0 from ULA port 0xFE write).
    void set_border(uint8_t colour) { border_colour_ = colour & 0x07; }
    uint8_t get_border() const { return border_colour_; }

    /// Snapshot the current border colour for a given scanline.
    /// Called during the frame loop so per-line border changes are preserved.
    void snapshot_border_for_line(int line) {
        if (line >= 0 && line < FB_HEIGHT)
            border_per_line_[line] = border_colour_;
    }

    /// Initialize all per-line border colours to current value (called at frame start).
    void init_border_per_line() {
        border_per_line_.fill(border_colour_);
    }

    /// Get the snapshotted border colour for a given line.
    uint8_t border_for_line(int line) const {
        if (line >= 0 && line < FB_HEIGHT) return border_per_line_[line];
        return border_colour_;
    }

    /// Enable/disable ULA rendering (NextREG 0x68 bit 7).
    void set_ula_enabled(bool enabled) { ula_enabled_ = enabled; }
    bool ula_enabled() const { return ula_enabled_; }

    // Clip window (NextREG 0x1A, 4-write cycle)
    void set_clip_x1(uint8_t v) { clip_x1_ = v; }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; }
    uint8_t clip_x1() const { return clip_x1_; }
    uint8_t clip_x2() const { return clip_x2_; }
    uint8_t clip_y1() const { return clip_y1_; }
    // Wave F (S8.08) — render-time y2 clamp per VHDL zxnext.vhd:6779-6783:
    // when the raw NR 0x1A y2 field has its top two bits set (y2 >= 0xC0),
    // the consumer-facing signal is clamped to 0xBF.  The VHDL applies the
    // clamp combinationally at the assignment of `ula_clip_y2_0`, not at the
    // NR 0x1A storage register (`nr_1a_ula_clip_y2`), so jnext mirrors that
    // by storing the raw byte and clamping inside the getter.  All callers
    // (renderer, debugger UI, save-state round-trip of consumers) see the
    // clamped value — raw storage preserves NR 0x1A write semantics and
    // keeps save/load of legacy snapshots byte-identical.
    uint8_t clip_y2() const {
        return (clip_y2_ & 0xC0) == 0xC0 ? 0xBF : clip_y2_;
    }
    // Raw stored byte of NR 0x1A y2 (no clamp). Mirrors VHDL
    // `nr_1a_ula_clip_y2` exactly — needed by the NR 0x1A read mux at
    // zxnext.vhd:5963-5969 which reads the raw register, not the
    // consumer-facing clamped `ula_clip_y2_0` (zxnext.vhd:6779-6783).
    uint8_t clip_y2_raw() const { return clip_y2_; }

    /// Set the Timex screen mode from a port 0xFF write.
    ///
    /// The full byte is stored so callers can read it back (e.g. for
    /// floating-bus emulation).  The mode is decoded from bits 2:0 per
    /// VHDL zxula.vhd:191 (`screen_mode_s <= i_port_ff_reg(2 downto 0)`).
    /// Bits 5:3 carry the HI_RES paper colour (zxula.vhd:419) and are
    /// consumed by the renderer / border path, NOT by the mode decoder.
    void set_screen_mode(uint8_t port_val);

    /// Return the raw byte last written to port 0xFF.
    uint8_t get_screen_mode_reg() const { return screen_mode_reg_; }

    // -----------------------------------------------------------------
    // Per-scanline port-0xFF Timex screen-mode replay (G07)
    // -----------------------------------------------------------------
    //
    // Mirrors the layer2 / palette / sprites change-log pattern.  Required
    // because port 0xFF writes performed mid-frame (typically by Copper or
    // the Z80 racing the beam) change the per-scanline screen-mode path
    // (STANDARD vs HI_COLOUR vs HI_RES vs ALT).  Without this log,
    // mid-frame writes collapse to last-write-wins and every scanline
    // renders with the same mode.
    //
    // VHDL oracle:
    //   zxnext.vhd:3615-3616 — port_ff_wr latches port_ff_reg <= cpu_do.
    //   zxnext.vhd:3630      — port_ff_dat_tmx <= port_ff_reg (CPU-clk).
    //   zxula.vhd:191        — screen_mode_s <= i_port_ff_reg(2:0).
    //   zxula.vhd:209        — screen_mode <= screen_mode_s (latched at
    //                          character-cell boundary i_hc(3:0)=X"3"/"B").
    //
    // The change log is tagged with the scanline at the write moment;
    // apply_changes_for_line replays at the start of rendering each
    // line, so writes during line N take effect at the start of line
    // N+1 — matching VHDL's per-character-cell sample.

    /// Snapshot the live screen-mode register as the frame baseline and
    /// reset the per-frame change log. Called from Emulator::run_frame.
    void start_frame();

    /// Update the scanline tag attached to subsequent port-0xFF writes.
    /// Called from Emulator::on_scanline. Default 0 at frame start.
    void set_current_line(int line) {
        current_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live screen-mode register to the frame baseline and
    /// reset the render cursor. Called once before per-scanline render.
    void rewind_to_baseline();

    /// Apply all logged port-0xFF writes whose line tag equals `line`.
    /// Cursor is monotonically advanced; the log is in scanline order so
    /// total work across a frame is O(port_ff_count_).
    void apply_changes_for_line(int line);

    /// Apply every remaining port-0xFF entry, regardless of line tag.
    /// Called after the per-line render loop so writes that landed in
    /// vblank still update the live state. Mirrors
    /// Tilemap::flush_remaining_nr6b_changes.
    void flush_remaining_changes();

    /// Number of port-0xFF changes recorded this frame (diagnostic).
    size_t port_ff_change_log_size() const { return port_ff_count_; }

    /// Static cap; further writes after this many in a frame are
    /// silently dropped (with a once-per-frame warn).
    static constexpr size_t MAX_CHANGES_PER_FRAME = 1024;

    // -------------------------------------------------------------------
    // Phase-1 scaffold: scroll / ULAnext / ULA+ / shadow / alt-file
    // API surface for Phase-2 Waves A-F. VHDL refs:
    //   zxula.vhd:193-207  scroll_y (nr_27)          → ula_scroll_y_
    //   zxula.vhd:199      scroll_x (nr_26, fine)    → ula_scroll_x_coarse_, ula_fine_scroll_x_
    //   zxula.vhd:191      i_ula_shadow_en gate       → shadow_screen_en_
    //   zxula.vhd:218      alt-file bit (port 0xFF b0)→ alt_file_
    //   zxula.vhd:470,492-515 ULAnext format (nr_42) → ulanext_format_
    //   zxula.vhd:470,531  ULA+ enable (port 0xFF3B) → ulap_en_
    // Semantics are VHDL-faithful: setters store raw bytes (no masking) unless
    // noted; any render-time transform (mod-192 wrap, fine-bit merge, etc.)
    // stays in the renderer so Phase-2 agents see the untransformed byte.
    // -------------------------------------------------------------------

    // NR 0x26 coarse X-scroll (8-bit raw). VHDL zxnext.vhd:5304, zxula.vhd:199.
    void    set_ula_scroll_x_coarse(uint8_t v) {
        ula_scroll_x_coarse_ = v;
        log_scroll_change();
    }
    uint8_t get_ula_scroll_x_coarse() const    { return ula_scroll_x_coarse_; }

    // NR 0x27 Y-scroll (8-bit raw; cross-third wrap applied by renderer).
    // VHDL zxnext.vhd:5307, zxula.vhd:193-207.
    void    set_ula_scroll_y(uint8_t v) {
        ula_scroll_y_ = v;
        log_scroll_change();
    }
    uint8_t get_ula_scroll_y() const    { return ula_scroll_y_; }

    // NR 0x68 bit 2 — fine X-scroll enable. VHDL zxnext.vhd:5449, zxula.vhd:199.
    void set_ula_fine_scroll_x(bool b)  {
        ula_fine_scroll_x_ = b;
        log_scroll_change();
    }
    bool get_ula_fine_scroll_x() const  { return ula_fine_scroll_x_; }

    // -------------------------------------------------------------------
    // Per-scanline NR 0x26 / NR 0x27 / NR 0x68 b2 scroll change-log (G08).
    //
    // Mirrors the Layer2 / PaletteManager / SpriteEngine pattern. Required
    // for Copper-driven mid-frame scroll splits (e.g. parallax effects via
    // NR 0x26 / NR 0x27 mid-frame writes). VHDL zxula.vhd:193-207, :199 —
    // px / py are latched per character cell (i_hc(3:0) = "3" or "B"); a
    // mid-frame write to NR 0x26 / NR 0x27 takes effect on the next cell
    // boundary, so per-scanline replay (8-pixel-cell granularity coarsened
    // to a full scanline) is the natural emulator approximation.
    //
    // Lifecycle (called by Emulator::run_frame + Renderer::render_frame):
    //   ula.start_frame_scroll();              // emulator at frame start
    //   ...                                    // emulation runs; scroll
    //                                          // setters append entries
    //                                          // tagged with current_line_
    //   ula.rewind_scroll_to_baseline();       // before render_frame
    //   for row in 0..H:
    //       ula.apply_scroll_changes_for_line(row);
    //       render_scanline(row);
    // -------------------------------------------------------------------

    /// Snapshot the live scroll state as the frame baseline and reset the
    /// per-frame change log. Called at the start of every frame.
    void start_frame_scroll();

    /// Update the scanline tag attached to subsequent scroll writes.
    /// Called from Emulator::on_scanline. Default 0 at frame start.
    void set_current_scroll_line(int line) {
        current_scroll_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live scroll state to the frame baseline and reset the
    /// render cursor. Called once before per-scanline render.
    void rewind_scroll_to_baseline();

    /// Apply all logged scroll changes whose line tag equals `line`.
    /// Cursor is monotonically advanced; the log is in scanline order so
    /// total work across a frame is O(scroll_change_count_).
    void apply_scroll_changes_for_line(int line);

    /// Apply every remaining scroll log entry, regardless of line tag.
    /// Called after the per-line render loop so vblank writes still
    /// update the live state. Mirrors flush_remaining_changes.
    void flush_remaining_scroll_changes();

    /// Number of scroll changes recorded this frame (diagnostic / tests).
    size_t scroll_change_log_size() const { return scroll_change_count_; }

    /// Static cap for scroll log; overflow drops further writes for the
    /// remainder of the frame and emits a once-per-frame warning.
    static constexpr size_t MAX_SCROLL_CHANGES_PER_FRAME = 1024;

    // NR 0x42 ULAnext format byte (raw). VHDL default X"07" at reset (zxnext.vhd:5002).
    // Semantics in zxula.vhd:503-515 (case X"01"/X"03"/…/X"7F" ink/paper split).
    void    set_ulanext_format(uint8_t v) { ulanext_format_ = v; }
    uint8_t get_ulanext_format() const    { return ulanext_format_; }

    // NR 0x43 bit 0 — ULAnext enable. Phase-2 Wave B/C writes ONLY through
    // this narrow setter; aggregate NR 0x43 palette-group bits stay owned by
    // the Compositor/PaletteManager. VHDL zxnext.vhd:5394.
    void set_ulanext_en(bool b) { ulanext_en_ = b; }
    bool get_ulanext_en() const { return ulanext_en_; }

    /// ULAnext pixel encoder output (VHDL zxula.vhd:492-529).
    ///
    /// @param pixel_en   VHDL `pixel_en` signal (true => ink cycle, false => paper).
    /// @param border     Border segment (border_active_d=1).
    /// @param attr       Active attribute byte (attr_active).
    /// @return  `pixel`       = 8-bit `ula_pixel` (palette index 0..255).
    ///          `select_bgnd` = `ula_select_bgnd` (true => transparent paper).
    ///
    /// Caller must gate on `get_ulanext_en()`; the function assumes the VHDL
    /// `if i_ulanext_en = '1'` branch is already taken.  Uses the current
    /// `ulanext_format_` as `i_ulanext_format`.
    ///
    /// VHDL semantics (zxula.vhd:492-529):
    ///   border           => pixel = paper_base_index(7:3) & attr(5:3)
    ///                     = 0x80 | (attr & 0x38) >> 3,
    ///                       and format=0xFF additionally asserts select_bgnd.
    ///   pixel_en (ink)  => pixel = attr AND format.
    ///   paper cycle     => case format: 0x01/03/07/0F/1F/3F/7F each pack
    ///                       paper_base_index(7:k) & attr(7:k-1) into 8 bits,
    ///                       with paper_base_index(7)=1 => top bit always 1.
    ///                       All other formats (incl. 0xFF) assert select_bgnd.
    struct UlaNextPixel { uint8_t pixel; bool select_bgnd; };
    UlaNextPixel compute_ulanext_pixel(bool pixel_en, bool border, uint8_t attr) const;

    /// zxnext.vhd:6986-6991 — the `ula_select_bgnd` consumer:
    ///
    ///   if lores_pixel_en_1 = '1' or ula_select_bgnd_1 = '0' then
    ///      ula_rgb_1 <= ulatm_rgb_1(8 downto 0);              -- palette
    ///   else
    ///      ula_rgb_1 <= fallback_rgb_1 &
    ///                   (fallback_rgb_1(1) or fallback_rgb_1(0));  -- NR $4A
    ///
    /// A pixel whose ULAnext encoding asserted `ula_select_bgnd` takes the
    /// NR $4A fallback colour instead of a palette lookup.  The Renderer
    /// owns the NR $4A per-line replay (`fallback_per_line_`, modelling the
    /// fallback_rgb_0/1a/1 latch chain at zxnext.vhd:6823/6915-6916) and
    /// pushes the row's expanded ARGB here before each render_scanline
    /// call.  The `lores_pixel_en_1 = '1'` disjunct is modelled by
    /// Renderer::apply_lores, whose unconditional overwrite runs after this
    /// substitution.  Not serialized: per-row scratch input, re-pushed by
    /// Renderer::render_row from its own (serialized) fallback state.
    void set_select_bgnd_argb(uint32_t argb) { select_bgnd_argb_ = argb; }

    // Port 0xFF3B — ULA+ enable. Narrow setter; ULA+ mode/index live in the
    // Compositor-side palette state. VHDL zxnext.vhd:4547-4551.
    void set_ulap_en(bool b) { ulap_en_ = b; }
    bool get_ulap_en() const { return ulap_en_; }

    /// Per-scanline ULA+ enable snapshot (NR 0x68 b3 / port 0xFF3B path) —
    /// gap G11 closure. VHDL zxnext.vhd:5445 captures the bit at stage 0
    /// of every line; mid-frame Copper writes take effect on the next
    /// line. Mirrors the snapshot/init/getter idiom used by Renderer's
    /// fallback_per_line_ and ula_enabled_per_line_.
    void snapshot_ulap_en_for_line(int line) {
        if (line >= 0 && line < 320)
            ulap_en_per_line_[line] = ulap_en_;
    }
    void init_ulap_en_per_line() {
        ulap_en_per_line_.fill(ulap_en_);
    }
    bool ulap_en_for_line(int line) const {
        return (line >= 0 && line < 320) ? ulap_en_per_line_[line]
                                         : ulap_en_;
    }

    // Port 0xBF3B — ULA+ mode/index register (write-only latch).
    // VHDL zxnext.vhd:4525-4538: `port_bf3b_ulap_mode <= cpu_do(7 downto 6)`.
    // Only the top 2 bits are the mode-group; the low 6 bits (index) are
    // owned by the Compositor/PaletteManager palette-index state and are not
    // tracked here. We need `ulap_mode_` on Ula because the 0xFF3B write
    // at zxnext.vhd:4548 is gated on `port_bf3b_ulap_mode = "01"`, so the
    // Emulator port-dispatch path has to consult Ula to decide whether the
    // 0xFF3B enable latch fires. Setter stores raw 2-bit value (0..3).
    void    set_ulap_mode(uint8_t m) { ulap_mode_ = static_cast<uint8_t>(m & 0x03); }
    uint8_t get_ulap_mode() const    { return ulap_mode_; }

    // Port 0xBF3B — ULA+ palette-entry index latch (low 6 bits).
    // VHDL zxnext.vhd:4533-4535: when port_bf3b write has cpu_do(7:6) = "00",
    // `port_bf3b_ulap_index <= cpu_do(5 downto 0)`.  This latch is the slot
    // selector for the NR 0xFF palette poke side-channel (zxnext.vhd:6957-6958)
    // and for ULA+ palette readback (zxnext.vhd:6958 ulap_palette_rd path).
    // Stored on Ula because that is where the bf3b mode latch lives; future
    // G103 (ULA+ runtime) work will consume it from here without re-routing.
    void    set_ulap_index(uint8_t i) { ulap_index_ = static_cast<uint8_t>(i & 0x3F); }
    uint8_t get_ulap_index() const    { return ulap_index_; }

    // ULA+ pixel encoder — VHDL zxula.vhd:531-541.
    //
    // Produces the 8-bit ula_pixel palette index when ULA+ is enabled.
    // Inputs:
    //   pixel_en     — combined shift-register/flash/border gate (zxula.vhd:470).
    //                  In ULA+ mode the flash XOR is suppressed and attr(7)
    //                  is reinterpreted as a palette-group bit, so callers
    //                  pass the RAW shift-reg bit (zxula.vhd:470 with the
    //                  `and not i_ulap_en` term zeroing the flash XOR).
    //   attr         — attribute byte from the attr plane.
    //   screen_mode_2 — zxula.vhd:191-209 `screen_mode(2)` latched signal,
    //                  which is port_ff_reg(2) on the Next (hi-res bit in
    //                  the VHDL's 3-bit screen_mode encoding). Forces the
    //                  low bit of the upper nibble high per :535.
    //
    // Output (8 bits):
    //   bits(7:3) = "11" & attr(7:6) & (screen_mode_2 or not pixel_en)
    //   bits(2:0) = attr(2:0)  if pixel_en else attr(5:3)
    //
    // Note: this is a pure function of its inputs; it does not consult any
    // Ula state. It is exposed publicly so the ULA renderer AND subsystem
    // tests can call it with identical semantics (VHDL-oracle symmetry with
    // `vhdl_standard_ula_pixel` in the test file).
    static uint8_t encode_ulap_pixel(bool pixel_en, uint8_t attr, bool screen_mode_2);

    // Port 0xFF bit 0 — alt-file (selects 0x4000 vs 0x6000 for primary screen).
    // Orthogonal to shadow_screen_en_ (bank-5 vs bank-7). VHDL zxula.vhd:218.
    void set_alt_file(bool b) { alt_file_ = b; }
    bool get_alt_file() const { return alt_file_; }

    // Port 0x7FFD bit 3 — shadow-screen enable (bank 7 instead of bank 5).
    // VHDL zxula.vhd:191: when '1', the ULA is limited to the standard mode
    // (screen_mode forced to "000") because bank 7 is only 8 KB BRAM.
    // When the flag deasserts we do NOT restore any previous screen_mode:
    // VHDL is a combinational OR-gate; restoration is the caller's job.
    void set_shadow_screen_en(bool b) {
        shadow_screen_en_ = b;
        // VHDL ula_bank_do <= vram_bank7_do when port_7ffd_shadow='1':
        // i_ula_shadow_en is the SAME signal that switches vram_read from
        // bank 5 (page 10) to bank 7 (page 14). Keeping the two flags in
        // sync — without this, vram_read stays on bank 5 even when
        // shadow_screen_en_ is asserted, and ULA reads game data instead of
        // the shadow screen contents. Beast.nex 2026-04-25.
        vram_use_bank7_ = b;
        if (b) {
            // Force screen_mode_reg_ bits 2:0 (the VHDL mode field per
            // zxula.vhd:191) to "000" (STANDARD, alt_file=0).  Use the
            // existing setter so side-effects propagate consistently —
            // in particular, Wave-D's set_screen_mode update re-derives
            // `alt_file_` from mode_bits(0), which is cleared by this
            // mask.  That matches VHDL exactly: when `i_ula_shadow_en='1'`,
            // screen_mode_s collapses to "000" for the entire 3-bit mode
            // field (including the alt-file bit).  Bits 5:3 of port 0xFF
            // (HI_RES paper colour, zxula.vhd:419) and bits 7:6 are
            // preserved — the VHDL gate at :191 only touches the 3-bit
            // mode field.  G179 — mask flipped from 0x07 to 0xF8 when the
            // mode field migrated from bits 5:3 (jnext drift) to VHDL
            // bits 2:0.
            const uint8_t masked = static_cast<uint8_t>(screen_mode_reg_ & 0xF8);
            set_screen_mode(masked);
        }
    }
    bool get_shadow_screen_en() const { return shadow_screen_en_; }

    /// Which VRAM bank vram_read() is currently fetching from: true = bank 7
    /// (shadow), false = bank 5.  Kept in sync with shadow_screen_en_ by the
    /// setter above.  Exposed so tests can assert that the debugger's
    /// render_scanline_bank() override is properly restored.
    bool vram_bank7() const { return vram_use_bank7_; }

    // Wave-D hook: select `border_clr_tmx` (VHDL zxula.vhd:419) route for
    // border rendering instead of the standard `border_clr`. Default false
    // preserves existing behaviour; Phase-2 Wave D may flip this based on
    // shift_screen_mode(2) state.
    void set_border_clr_tmx_src(bool hi_res) { border_clr_tmx_src_ = hi_res; }
    bool get_border_clr_tmx_src() const       { return border_clr_tmx_src_; }

    // -------------------------------------------------------------------
    // Per-scanline active-palette select (G10 — Tier 3 W1 closure)
    // -------------------------------------------------------------------
    //
    // Distinct from per-scanline palette CONTENT (PaletteManager change-
    // log).  Tracks the SELECTOR bit-lanes that pick which palette bank
    // is active per scanline:
    //
    //   NR 0x43 b1 — active_ula_palette     (zxnext.vhd:5393, :6825)
    //   NR 0x43 b2 — active_layer2_palette  (zxnext.vhd:5392, :6827)
    //   NR 0x43 b3 — active_sprite_palette  (zxnext.vhd:5391, :6828)
    //   NR 0x6B b4 — tm_palette_select      (zxnext.vhd:5462, :6826)
    //
    // VHDL structure: NR 0x43 latches (b1, b2, b3) and NR 0x6B latch
    // (control(4)) are independent storage cells; the active-palette mux
    // at zxnext.vhd:6825-6828 reads them per pixel cycle.  Mid-frame
    // Copper writes that flip a selector must take effect on the next
    // scanline; without per-scanline replay, last-write-wins collapses
    // every line onto the end-of-frame value.
    //
    // Two separate change-logs by VHDL semantics: NR 0x43 selects (one
    // 3-bit field captured atomically per write) and NR 0x6B b4
    // (1-bit field) flow through different latches and can change
    // independently.  S17.03 verifies that independence.
    //
    // Mirrors layer2.h pattern: setter → log; start_frame() snapshots
    // baseline + clears log; rewind_to_baseline() restores live to
    // baseline; apply_changes_for_line(line) replays in scanline order.
    //
    // Note: these are MIRROR copies of the selectors that PaletteManager
    // also stores.  Ula tracks them so per-scanline replay is on a
    // single subsystem; the renderer/compositor consults the live Ula
    // state for the appropriate scanline.

    /// NR 0x43 b1 — active ULA palette select (false = palette 0).
    void set_active_ula_palette(bool second) {
        active_ula_palette_ = second;
        log_palsel43_change();
    }
    bool get_active_ula_palette() const { return active_ula_palette_; }

    /// NR 0x43 b2 — active Layer 2 palette select.
    void set_active_layer2_palette(bool second) {
        active_l2_palette_ = second;
        log_palsel43_change();
    }
    bool get_active_layer2_palette() const { return active_l2_palette_; }

    /// NR 0x43 b3 — active sprite palette select.
    void set_active_sprite_palette(bool second) {
        active_spr_palette_ = second;
        log_palsel43_change();
    }
    bool get_active_sprite_palette() const { return active_spr_palette_; }

    /// NR 0x6B b4 — tilemap palette select (separate latch from NR 0x43).
    void set_active_tilemap_palette(bool second) {
        active_tm_palette_ = second;
        log_palsel6b_change();
    }
    bool get_active_tilemap_palette() const { return active_tm_palette_; }

    /// Snapshot the live selector state as the frame baseline and reset
    /// per-frame change logs. Called at the start of every frame.
    void palsel_start_frame();

    /// Update the scanline tag attached to subsequent selector writes.
    /// Default 0 at frame start.
    void set_palsel_current_line(int line) {
        palsel_current_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live selector state to the frame baseline and reset
    /// the render cursor. Called once before per-scanline render.
    void palsel_rewind_to_baseline();

    /// Apply all logged selector changes whose line tag equals `line`.
    /// Cursor is monotonically advanced; the log is in scanline order
    /// so total work across a frame is O(change_count_).
    void palsel_apply_changes_for_line(int line);

    /// Apply every remaining selector entry across both palsel logs
    /// (NR 0x43 + NR 0x6B b4), regardless of line tag. Called after the
    /// per-line render loop so vblank writes still update the live
    /// state. Mirrors flush_remaining_changes.
    void palsel_flush_remaining_changes();

    /// Number of NR 0x43 selector changes recorded this frame.
    size_t palsel43_change_log_size() const { return palsel43_change_count_; }

    /// Number of NR 0x6B b4 selector changes recorded this frame.
    size_t palsel6b_change_log_size() const { return palsel6b_change_count_; }

    /// Static cap; further writes after this many in a frame are
    /// silently dropped (with a once-per-frame warn).
    static constexpr size_t MAX_PALSEL_CHANGES_PER_FRAME = 1024;

    /// Single entry of the NR 0x43 selector log — captures all 3 active-
    /// palette bits atomically (they share a register write).
    struct PalSel43Change {
        uint16_t line;
        bool     active_ula;
        bool     active_l2;
        bool     active_spr;
    };

    /// Single entry of the NR 0x6B b4 selector log — captures the 1-bit
    /// tilemap palette select.
    struct PalSel6bChange {
        uint16_t line;
        bool     active_tm;
    };

    /// Read-only access to NR 0x43 selector log entry (for tests/debug).
    const PalSel43Change& palsel43_change_at(size_t i) const {
        return palsel43_change_log_[i];
    }
    /// Read-only access to NR 0x6B b4 selector log entry (for tests/debug).
    const PalSel6bChange& palsel6b_change_at(size_t i) const {
        return palsel6b_change_log_[i];
    }

    /// Render one complete frame.
    ///
    /// @param framebuffer  Pointer to FB_WIDTH × FB_HEIGHT ARGB8888 pixels,
    ///                     row-major (row 0 = top).
    /// @param mmu          MMU for reading VRAM (screen at 0x4000, attrs at 0x5800).
    void render_frame(uint32_t* framebuffer, Mmu& mmu);

    /// Render a single scanline (row 0..FB_HEIGHT-1) into a FB_WIDTH-pixel
    /// buffer (640 cells under G104).  Used by the compositor for per-scanline
    /// rendering.
    ///
    /// @param dst         Output ARGB buffer of FB_WIDTH (640) pixels.
    /// @param row         Scanline index 0..FB_HEIGHT-1.
    /// @param mmu         MMU for VRAM access.
    /// @param border_dst  Optional per-pixel border flag buffer (FB_WIDTH
    ///                    bools). When non-null, every cell painted with
    ///                    the border colour gets `true`; display-area cells
    ///                    are left untouched (caller must pre-fill `false`).
    ///                    Mirrors VHDL `ula_border_2` (zxnext.vhd:7256/7266
    ///                    /7278) sourced from `border_active` (zxula.vhd:415,
    ///                    exposed as `o_ula_border` at zxula.vhd:567).
    ///                    Default nullptr — non-compositor callers (debugger
    ///                    video panel, subsystem tests) don't need it.
    void render_scanline(uint32_t* dst, int row, Mmu& mmu,
                         bool* border_dst = nullptr);

    /// Render a single scanline forcing a specific VRAM bank, regardless of
    /// the live port-0x7FFD bit-3 shadow-screen selector.
    ///
    /// The live `render_scanline` above follows `vram_use_bank7_`, which is
    /// exactly right for the compositor but wrong for the debugger's two
    /// separate ULA views: "Primary (bank 5)" must always show bank 5 and
    /// "Shadow (bank 7)" must always show bank 7, whichever one the running
    /// program happens to have selected.
    ///
    /// Bank 7 uses the same ZX pixel/attribute layout as bank 5; on real
    /// hardware it is a dedicated 8K BRAM (VHDL bank7_ram dpram2,
    /// zxnext.vhd:6670) and bank 5 a dedicated 16K dual-port VRAM
    /// (bank5_ram dpram2, zxnext.vhd:6558) — `vram_read` serves both from
    /// the wired buffers.
    ///
    /// Delegates to `render_scanline`, so the Timex screen mode (port 0xFF),
    /// ULA scroll, palette selector and per-line border snapshot all apply
    /// exactly as in the live path.  Saves/restores the live bank selector,
    /// so it does NOT disturb emulation state.  Border-flag plumbing is
    /// unused for the debugger view (always nullptr).
    void render_scanline_bank(uint32_t* dst, int row, Mmu& mmu, bool use_bank7);

    /// Shadow-screen (bank 7) convenience wrapper for render_scanline_bank.
    void render_scanline_screen1(uint32_t* dst, int row, Mmu& mmu) {
        render_scanline_bank(dst, row, mmu, /*use_bank7=*/true);
    }

    /// Advance flash state (call once per frame after all scanlines rendered).
    void advance_flash();

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    PaletteManager*  palette_         = nullptr; ///< 256-entry × 2-bank ULA palette (G102)
    Ram*             ram_             = nullptr; ///< Physical RAM for direct VRAM reads
    const uint8_t*   bank7_bram_      = nullptr; ///< Dedicated bank-7 BRAM (see set_bank7_bram)
    const uint8_t*   bank5_vram_      = nullptr; ///< Dedicated bank-5 16K VRAM (see set_bank5_vram)
    bool             ula_enabled_     = true;  ///< ULA rendering enabled (NextREG 0x68 bit 7)
    bool             vram_use_bank7_  = false; ///< When true, vram_read() reads from bank 7 (page 14) not bank 5 (page 10)
    uint8_t          clip_x1_        = 0;
    uint8_t          clip_x2_        = 255;
    uint8_t          clip_y1_        = 0;
    uint8_t          clip_y2_        = 191;
    uint8_t          border_colour_   = 7;     ///< ZX colour index 0–7 (white = 7)
    std::array<uint8_t, FB_HEIGHT> border_per_line_; ///< Per-scanline border colour snapshots
    int              flash_counter_   = 0;     ///< Incremented once per frame
    bool             flash_phase_     = false; ///< Toggles every 16 frames
    uint8_t          screen_mode_reg_ = 0;     ///< Raw value last written to port 0xFF
    TimexScreenMode  mode_            = TimexScreenMode::STANDARD;

    // --- Phase-1 scaffold (VHDL refs on setter declarations) --------------
    uint8_t ula_scroll_x_coarse_ = 0;     ///< NR 0x26 (zxula.vhd:199)
    uint8_t ula_scroll_y_        = 0;     ///< NR 0x27 (zxula.vhd:193-207)
    bool    ula_fine_scroll_x_   = false; ///< NR 0x68 bit 2 (zxula.vhd:199)
    uint8_t ulanext_format_      = 0x07;  ///< NR 0x42, VHDL reset X"07" (zxnext.vhd:5002)
    bool    ulanext_en_          = false; ///< NR 0x43 bit 0 (zxnext.vhd:5394)
    /// NR $4A fallback expanded RRRGGGBB→ARGB (Renderer::rrrgggbb_to_argb
    /// convention), consumed when `ula_select_bgnd` is asserted
    /// (zxnext.vhd:6986-6991). Default = expansion of the NR $4A reset
    /// value X"E3" (zxnext.vhd:5014); refreshed per row by render_row.
    uint32_t select_bgnd_argb_   = 0xFFFF00FFu;
    bool    ulap_en_             = false; ///< Port 0xFF3B enable (zxnext.vhd:4547)
    /// Per-scanline ULA+ enable snapshot — gap G11 closure.
    std::array<bool, 320> ulap_en_per_line_{};
    uint8_t ulap_mode_           = 0;     ///< Port 0xBF3B top-2 bits (zxnext.vhd:4529/4532)
    uint8_t ulap_index_          = 0;     ///< Port 0xBF3B low 6 bits (zxnext.vhd:4530/4534)
    bool    alt_file_            = false; ///< Port 0xFF bit 0 (zxula.vhd:218)
    bool    shadow_screen_en_    = false; ///< i_ula_shadow_en (zxula.vhd:191)
    bool    border_clr_tmx_src_  = false; ///< Wave-D border route select (zxula.vhd:419)

    // ── Per-scanline port-0xFF change log (G07) ──────────────────────
    struct PortFFChange {
        uint16_t line;
        uint8_t  value;
    };
    std::array<PortFFChange, MAX_CHANGES_PER_FRAME> port_ff_log_{};
    size_t   port_ff_count_         = 0;
    uint16_t current_line_          = 0;
    size_t   port_ff_render_cursor_ = 0;
    bool     port_ff_overflow_warned_ = false;
    uint8_t  baseline_port_ff_      = 0;
    void log_port_ff_change();

    // ── Per-scanline scroll change log (G08) ─────────────────────────────
    struct ScrollChange {
        uint16_t line;
        uint8_t  scroll_x_coarse;
        uint8_t  scroll_y;
        uint8_t  fine_scroll_x;
    };
    std::array<ScrollChange, MAX_SCROLL_CHANGES_PER_FRAME> scroll_change_log_{};
    size_t   scroll_change_count_    = 0;
    uint16_t current_scroll_line_    = 0;
    size_t   scroll_render_cursor_   = 0;
    bool     scroll_overflow_warned_ = false;
    uint8_t  baseline_scroll_x_coarse_ = 0;
    uint8_t  baseline_scroll_y_        = 0;
    uint8_t  baseline_fine_scroll_x_   = 0;
    void log_scroll_change();

    // -- Per-scanline active-palette select (G10) --------------------------
    bool    active_ula_palette_  = false;
    bool    active_l2_palette_   = false;
    bool    active_spr_palette_  = false;
    bool    active_tm_palette_   = false;

    std::array<PalSel43Change, MAX_PALSEL_CHANGES_PER_FRAME> palsel43_change_log_{};
    size_t   palsel43_change_count_     = 0;
    size_t   palsel43_render_cursor_    = 0;
    bool     palsel43_overflow_warned_  = false;
    bool     baseline_active_ula_pal_   = false;
    bool     baseline_active_l2_pal_    = false;
    bool     baseline_active_spr_pal_   = false;

    std::array<PalSel6bChange, MAX_PALSEL_CHANGES_PER_FRAME> palsel6b_change_log_{};
    size_t   palsel6b_change_count_     = 0;
    size_t   palsel6b_render_cursor_    = 0;
    bool     palsel6b_overflow_warned_  = false;
    bool     baseline_active_tm_pal_    = false;

    uint16_t palsel_current_line_       = 0;

    void log_palsel43_change();
    void log_palsel6b_change();

    /// Render a display row in STANDARD or STANDARD_1 mode.
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param screen_row  ZX screen row [0, 191].
    /// @param pixel_base  Base address of pixel data (0x4000 or 0x6000).
    /// @param attr_base_row  Base address of attribute row: 0x5800+(row/8)*32
    ///                       or 0x7800+(row/8)*32 for alternate screen.
    /// @param mmu         MMU for VRAM access.
    /// @param border_dst  Optional per-pixel border-flag buffer (FB_WIDTH
    ///                    bools). When non-null, the left/right border
    ///                    strips set the corresponding cells to `true`;
    ///                    display-area cells are left untouched.
    void render_display_line(uint32_t* row, int screen_row,
                             uint16_t pixel_base_offset,
                             uint16_t attr_row_base,
                             Mmu& mmu,
                             bool* border_dst = nullptr);

    /// Render a display row in HI_COLOUR mode.
    /// Pixel data from primary screen (0x4000), per-row-column attribute from
    /// alternate screen (0x6000).  Attribute layout mirrors the standard pixel
    /// layout: attr(row, col) = mmu.read(0x6000 | pixel_addr_offset(row, col)).
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param screen_row  ZX screen row [0, 191].
    /// @param mmu         MMU for VRAM access.
    /// @param border_dst  Optional border-flag buffer; see render_display_line.
    void render_display_line_hicolour(uint32_t* row, int screen_row, Mmu& mmu,
                                      bool* border_dst = nullptr);

    /// Render a display row in HI_RES mode.
    ///
    /// Native 512-pixel rendering per VHDL zxula.vhd:389: in HI_RES mode
    /// (`shift_screen_mode(2) = '1'`) the shift register is loaded with
    /// `shift_pbyte(15:8) & shift_abyte(15:8) & shift_pbyte(7:0) &
    /// shift_abyte(7:0)` and emitted MSB-first at the 14 MHz pixel clock.
    /// So per source-column pair the 32 emitted hi-res pixels are: 8 px from
    /// screen-0 col N (bits 7..0), then 8 px from screen-1 col N (bits 7..0),
    /// then 8 px from screen-0 col N+1, then 8 px from screen-1 col N+1.
    /// Border fills 64 cells either side.
    ///
    /// Ink / paper derivation per VHDL zxula.vhd:419, 426-427: in HI_RES
    /// (shift_screen_mode(2)='1') the WHOLE attr_reg is loaded with
    /// `border_clr_tmx <= "01" & (not port_ff(5:3)) & port_ff(5:3)`.  So
    /// BRIGHT=1 (implicit), ink colour = port_ff(5:3), paper colour =
    /// ~port_ff(5:3) & 7.  port_ff(5:3) is the user-controllable "screen
    /// colour"; there is no independent ink/paper field.
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param screen_row  ZX screen row [0, 191].
    /// @param mmu         MMU for VRAM access.
    /// @param border_dst  Optional border-flag buffer; see render_display_line.
    void render_display_line_hires(uint32_t* row, int screen_row, Mmu& mmu,
                                   bool* border_dst = nullptr);

    /// Fill an entire output row with the border colour (FB_WIDTH = 640 cells).
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param border_dst  Optional border-flag buffer (FB_WIDTH bools); when
    ///                    non-null, every cell is marked `true` (top/bottom
    ///                    border rows are entirely border).
    void render_border_line(uint32_t* row, bool* border_dst = nullptr);

    /// Look up a ULA colour by 8-bit `ula_pixel` (the encoder output)
    /// in the active ULA palette bank (NR 0x43 b1).  Uses PaletteManager
    /// when wired; else returns opaque black (production paths always
    /// have the palette set).  G102 — VHDL zxnext.vhd:6981.
    uint32_t lookup_colour(uint8_t ula_pixel) const;

    /// Read from ULA VRAM directly from physical bank 5 (or bank 7 for shadow).
    /// On real hardware, the ULA has a dedicated port to bank 5 RAM, bypassing
    /// the MMU entirely. The addr parameter is a CPU-space address (0x4000-0x7FFF);
    /// we convert it to a physical RAM offset in bank 5.
    /// Falls back to MMU reads if RAM is not wired (backward compat).
    uint8_t vram_read(uint16_t addr, Mmu& mmu) const;

    /// G12 — attribute-byte read for the STANDARD/STANDARD_1 renderer.
    /// Always-on (no arm/gate — removed round 3): returns the value
    /// AttributeMux reconstructed for the current render row instead of
    /// the raw byte currently sitting in RAM (which only ever reflects
    /// the LAST write of the frame — see attribute_mux.h), for the
    /// address range AttributeMux actually tracks. Falls back to the
    /// plain vram_read() path otherwise (STANDARD_1's genuine Timex
    /// alt-file attribute range, which AttributeMux does not track — see
    /// .cpp for why), byte-for-byte identical to pre-G12 behaviour.
    /// `addr` is the full CPU-space attribute address (0x5800-0x5AFF /
    /// 0x7800-0x7AFF + row/col offset); `alt` is true iff `addr` is in
    /// the STANDARD_1 (Timex alt-file) 0x7800 range, matching the `alt`
    /// local already computed in render_display_line from attr_row_base.
    /// Physical BANK selection (5 vs 7) is NOT taken from `alt` — see
    /// .cpp for the VHDL citation on why that would be wrong.
    uint8_t attr_vram_read(uint16_t addr, bool alt, Mmu& mmu) const;

    /// Compute the interleaved pixel address offset for (screen_row, col).
    /// Returns the offset from 0x4000 (or 0x6000) for the given position.
    static uint16_t pixel_addr_offset(int screen_row, int col);
};
