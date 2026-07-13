#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <array>
#include <string>
#include "video/ula.h"

class Mmu;
class Ram;
class PaletteManager;
class SpriteEngine;
class Tilemap;
class Copper;
class NextReg;

/// Layer priority modes from NextREG 0x15 bits 4:2.
enum class LayerPriority : uint8_t {
    SLU = 0,   // Sprites, Layer2, ULA       (000)
    LSU = 1,   // Layer2, Sprites, ULA        (001)
    SUL = 2,   // Sprites, ULA, Layer2        (010)
    LUS = 3,   // Layer2, ULA, Sprites        (011)
    USL = 4,   // ULA, Sprites, Layer2        (100)
    ULS = 5,   // ULA, Layer2, Sprites        (101)
    // 6 and 7 are blending modes (deferred to Phase 8)
};

/// Video compositor — combines ULA, Layer 2, Tilemap, and Sprites
/// per scanline in the priority order controlled by NextREG 0x15.
///
/// VHDL reference: zxnext.vhd lines 7193-7354 (compositor process).
class Renderer {
public:
    // -----------------------------------------------------------------
    // Host-side layer mask (--delayed-screenshot-layers, Task 22b)
    // -----------------------------------------------------------------
    //
    // Selects which layers the compositor is allowed to see. A masked-out
    // layer is forced TRANSPARENT at the compositor input, which is
    // exactly what the hardware does when that layer is disabled:
    //   ULA      NR 0x68 b7 = 1  → ula_transparent      (zxnext.vhd:7103)
    //   Layer 2  NR 0x69 b7 = 0  → l2_transparent       (zxnext.vhd:7106)
    //   Sprites  NR 0x15 b0 = 0  → sprite_transparent   (zxnext.vhd:7118)
    //   Tilemap  NR 0x6B b7 = 0  → tm_transparent       (zxnext.vhd:7109)
    // Everything downstream — the NR 0x15 priority order, the ULA/TM
    // merge, the blend modes, and the NR 0x4A fallback colour — then
    // behaves per the VHDL with no special-casing.
    //
    // The one place that needs the enables and not just the transparency
    // flags is STENCIL: zxnext.vhd:7130 selects the AND-branch only when
    //     ula_stencil_mode_2 = '1' AND ula_en_2 = '1' AND tm_en_2 = '1'
    // — BOTH enables. stencil_rgb is transparent whenever either input is
    // (7112), so masking either `ula` or `tiles` must take the AND-branch
    // down with it and fall through to the ordinary ulatm merge (7134-7135);
    // otherwise the surviving layer would be erased rather than shown.
    //
    // Note on the BORDER: the border is emitted by the ULA path, so
    // masking out `ula` removes the border too, and those pixels fall
    // through to the NR 0x4A fallback colour — bit-identical to what the
    // hardware shows with ula_en = 0 (the `ula_border_2` raster flag stays
    // asserted, as in the VHDL, so the border exception in priority modes
    // 3/4/5 keeps working).
    //
    // This is a HOST-side debug knob, not machine state: reset(),
    // save_state() and load_state() deliberately leave it alone. The
    // frontends arm it for the single frame that --delayed-screenshot
    // captures and disarm it immediately afterwards.
    enum LayerMask : uint8_t {
        LAYER_ULA     = 0x01,
        LAYER_LAYER2  = 0x02,
        LAYER_SPRITES = 0x04,
        LAYER_TILES   = 0x08,
        LAYER_ALL     = 0x0F,
    };

    void set_layer_mask(uint8_t m) { layer_mask_ = m & LAYER_ALL; }
    uint8_t layer_mask() const { return layer_mask_; }

    /// Parse a --delayed-screenshot-layers value: a comma-separated list of
    /// the lowercase names `ula`, `layer2`, `sprites`, `tiles`, `all`.
    /// On success writes the mask, clears `error` and returns true. On an
    /// empty list, an empty element, an unknown name, or a layer named twice
    /// (including `all` combined with anything), returns false, leaves `mask`
    /// untouched and fills `error` with a user-facing message. Never silently
    /// drops a name.
    static bool parse_layer_mask(const std::string& spec, uint8_t& mask,
                                 std::string& error);

    /// Render a mask back into its canonical comma-separated name list
    /// ("all" when every layer is present). For log lines and tests.
    static std::string layer_mask_to_string(uint8_t mask);

    // Canonical 640-wide framebuffer (G104). Layout:
    //   64 left border + 512 display + 64 right border = 640 columns.
    //   32 top border + 192 display + 32 bottom border = 256 rows.
    static constexpr int FB_WIDTH     = 640;
    static constexpr int FB_HEIGHT    = 256;

    // Display area within the framebuffer (matches ULA-side widened constants
    // post-Phase-2; Phase 1 keeps the per-layer renderers emitting 320-grid
    // and the compositor doubles in-place — see renderer.cpp scaffold).
    static constexpr int DISP_X = 64;   // left border width
    static constexpr int DISP_Y = 32;   // top border height
    static constexpr int DISP_W = 512;  // active display width
    static constexpr int DISP_H = 192;  // active display height

    /// Reset renderer state to power-on defaults.
    void reset() {
        layer_priority_ = 0;        // SLU
        fallback_colour_ = 0xE3;    // default transparent index
        transparent_rgb_ = 0xE3;    // NR 0x14 default
        sprite_en_ = false;         // NR 0x15 bit 0 default (VHDL: 0)
        stencil_mode_ = false;      // NR 0x68 bit 0 default (VHDL: 0)
        blend_mode_ = 0;            // NR 0x68 bits 6:5 default (VHDL: 00)
        tm_enabled_ = false;        // NR 0x6B bit 7 default (VHDL: 0)
        nr15_raw_ = 0;
        fallback_per_line_.fill(0xE3);
        ula_enabled_per_line_.fill(true);   // Ula::reset() leaves ula_enabled_ = true
        // G04 / G11 — per-scanline arrays mirror the scalar reset values.
        transparent_rgb_per_line_.fill(0xE3);
        stencil_mode_per_line_.fill(false);
        blend_mode_per_line_.fill(0);
        ula_.reset();
        // Per-scanline NR 0x1A snapshot mirrors the Ula reset clip window
        // (0, 255, 0, 191) — must run after ula_.reset().
        init_ula_clip_per_line();
        // Reset NR 0x15 per-scanline change log.
        nr15_change_count_      = 0;
        nr15_render_cursor_     = 0;
        nr15_current_line_      = 0;
        nr15_overflow_warned_   = false;
        baseline_nr15_raw_      = 0;
        baseline_layer_priority_= 0;
        baseline_sprite_en_     = false;
    }

    /// Access the underlying ULA (e.g. to set border colour from port 0xFE).
    Ula& ula() { return ula_; }
    const Ula& ula() const { return ula_; }

    /// Apply the ULA clip window (NextREG 0x1A) to one rendered ULA scanline.
    ///
    /// Unlike Layer 2 / Tilemap / Sprites — which each apply their own clip
    /// window inside their render_scanline — the ULA's clip lives in the
    /// compositor stage (VHDL zxnext.vhd:7104, `ula_clipped` feeds
    /// `ula_transparent`), so `Ula::render_scanline` emits an UNCLIPPED line
    /// and this pass masks it.  Every clipped-away cell becomes TRANSPARENT.
    ///
    /// Factored out of render_frame so the debugger's ULA views can apply the
    /// identical mask — otherwise the ULA panel would be the only layer view
    /// showing content the compositor suppresses.
    ///
    /// @param line  FB_WIDTH (640) ARGB cells, as emitted by Ula::render_scanline.
    /// @param row   Framebuffer row 0..FB_HEIGHT-1.
    void apply_ula_clip(uint32_t* line, int row) const;

    /// Set layer priority from NextREG 0x15 bits 4:2.
    void set_layer_priority(uint8_t val) { layer_priority_ = val & 0x07; }
    uint8_t layer_priority() const { return layer_priority_; }
    bool sprite_en() const { return sprite_en_; }

    // -----------------------------------------------------------------
    // Per-scanline NR 0x15 snapshot (G02 — driver-side infrastructure)
    // -----------------------------------------------------------------
    //
    // Mirrors PaletteManager / Layer2 per-scanline change-log idiom.
    // VHDL zxnext.vhd:5232 (write capture), 6799 (per-line latch oracle).
    // Required by demos that toggle layer-priority / sprite-en mid-frame
    // via Copper to multiplex L2/sprite z-orders per band.
    //
    // Writes to NR 0x15 land via `write_nr15(byte)` (single-call form);
    // the byte is also decomposed into `layer_priority_` / `sprite_en_`
    // for the legacy live-state callers (compositor priority lookup).
    //
    //   renderer.start_frame_nr15();              // emulator at frame start
    //   …                                         // emulation runs; NR 0x15
    //                                             // writes append to log
    //                                             // tagged with current_line
    //   renderer.rewind_to_baseline_nr15();       // before render_frame
    //   for row in 0..H:
    //       renderer.apply_changes_for_line_nr15(row);
    //       …                                     // composite pixel sees
    //                                             // per-line layer_priority

    /// NR 0x15 raw write — VHDL zxnext.vhd:5229-5234.
    /// Captures bit 0 (sprite_en) + bits 4:2 (layer_priority) into the
    /// per-scanline log; updates the legacy live setters as well.  The
    /// remaining bits (5, 6, 7) are not stored in this class — they
    /// live on SpriteEngine and the LoRes path; the emulator's NR 0x15
    /// dispatcher handles those alongside this call.
    void write_nr15(uint8_t v);

    /// Latest raw NR 0x15 byte snapshot (sprite_en in bit 0, priority
    /// in bits 4:2).  Reflects the most recent per-scanline replay
    /// when called between apply_changes_for_line_nr15 invocations.
    uint8_t nr15_raw() const { return nr15_raw_; }

    /// Snapshot the live NR 0x15 state as the frame baseline and reset
    /// the change-log.  Called at the start of every frame.
    void start_frame_nr15();

    /// Update the scanline tag attached to subsequent NR 0x15 writes.
    /// Default 0 at frame start (set by start_frame_nr15).
    void set_current_line_nr15(int line) {
        nr15_current_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live NR 0x15 state to baseline; reset render cursor.
    /// Called once before per-scanline render.
    void rewind_to_baseline_nr15();

    /// Apply all logged NR 0x15 changes whose line tag equals `line`.
    /// Cursor monotonically advanced.
    void apply_changes_for_line_nr15(int line);

    /// Number of NR 0x15 changes recorded this frame (diagnostic).
    size_t nr15_change_log_size() const { return nr15_change_count_; }

    /// Cap; further writes after this many in a frame are dropped.
    static constexpr size_t MAX_NR15_CHANGES_PER_FRAME = 1024;

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// Set the fallback colour index (NextREG 0x4A).
    /// Used when all layers are transparent at a given pixel.
    /// In the VHDL, this also replaces the ULA border colour.
    void set_fallback_colour(uint8_t val) { fallback_colour_ = val; }
    uint8_t fallback_colour() const { return fallback_colour_; }

    /// Set the transparent colour index (NextREG 0x14).
    /// VHDL zxnext.vhd:7100 — palette RGB[8:1] is compared against this
    /// value for ULA and Layer 2 transparency determination.
    void set_transparent_rgb(uint8_t val) { transparent_rgb_ = val; }
    uint8_t transparent_rgb() const { return transparent_rgb_; }

    /// Global sprite enable (NextREG 0x15 bit 0).
    /// VHDL zxnext.vhd:6934/7118 — when false, all sprite pixels are
    /// forced transparent at the compositor stage.
    void set_sprite_en(bool v) { sprite_en_ = v; }

    /// Stencil mode (NextREG 0x68 bit 0, ula_blend_mode bits 1:0).
    /// VHDL zxnext.vhd:7112-7113 — when active AND tm_en=1, ULA/TM merge
    /// uses bitwise AND instead of priority-based selection.
    void set_stencil_mode(bool v) { stencil_mode_ = v; }

    /// ULA blend mode (NextREG 0x68 bits 6:5 → `ula_blend_mode_2`).
    /// VHDL zxnext.vhd:7141-7178 — selects `mix_rgb` / `mix_top` / `mix_bot`
    /// sources for priority modes 6 (additive) and 7 (subtractive).
    ///   00 = default (mix_rgb = ULA mix, top/bot = TM split by tm_pixel_below).
    ///   10 = mix_rgb = ula_final (post-stencil), top/bot both transparent.
    ///   11 = mix_rgb = TM, top/bot both ULA split by tm_pixel_below.
    ///   01 (others) = mix_rgb transparent; top/bot swap TM/ULA by tm_pixel_below.
    void set_blend_mode(uint8_t v) { blend_mode_ = v & 0x03; }
    uint8_t blend_mode() const { return blend_mode_; }

    /// Tilemap enabled flag (NR 0x6B bit 7). Stencil mode (VHDL 7130)
    /// requires tm_en to be active.
    void set_tm_enabled(bool v) { tm_enabled_ = v; }

    /// Snapshot the current fallback colour for a given scanline.
    /// Called during the frame loop so per-line copper changes are preserved.
    void snapshot_fallback_for_line(int line) {
        if (line >= 0 && line < 320)
            fallback_per_line_[line] = fallback_colour_;
    }

    /// Initialize the entire per-line fallback array to the current value.
    /// Called at the start of each frame.
    void init_fallback_per_line() {
        fallback_per_line_.fill(fallback_colour_);
    }

    /// The NR 0x4A fallback colour the compositor used on framebuffer row
    /// `line` — the colour it emits where EVERY layer is transparent (VHDL
    /// zxnext.vhd:7218-7352: each priority mux defaults to `fallback_rgb_2`
    /// and is only overwritten by an opaque layer).
    ///
    /// This is the exact byte `render_row` feeds `rrrgggbb_to_argb` for that
    /// row, so the debugger's "Background" view reads the compositor's own
    /// per-scanline snapshot instead of re-deriving anything.  It is per-LINE
    /// because a Copper program can MOVE NR 0x4A mid-frame to paint a gradient
    /// across the raster; the live `fallback_colour()` is only the last value
    /// of the frame and would flatten that to a lie.
    ///
    /// Out-of-range lines fall back to the live register (matches
    /// transparent_rgb_for_line / stencil_mode_for_line).
    uint8_t fallback_for_line(int line) const {
        return (line >= 0 && line < 320) ? fallback_per_line_[line]
                                         : fallback_colour_;
    }

    /// Snapshot the current ULA-enable state (NR 0x68 bit 7 inverted) for
    /// a given scanline. Parallels snapshot_fallback_for_line so a Copper
    /// MOVE to NR 0x68 mid-frame is reflected in only the rows that follow
    /// the toggle, matching VHDL zxnext.vhd:7103 where ula_en_2 is sampled
    /// per pixel and flips ula_transparent at the scanline boundary.
    void snapshot_ula_enabled_for_line(int line) {
        if (line >= 0 && line < 320)
            ula_enabled_per_line_[line] = ula_.ula_enabled();
    }

    /// Initialize the entire per-line ULA-enable array to the current value.
    /// Called at the start of each frame, parallel to init_fallback_per_line.
    void init_ula_enabled_per_line() {
        ula_enabled_per_line_.fill(ula_.ula_enabled());
    }

    /// Per-scanline NR 0x14 (transparent RGB) snapshot — gap G04 closure.
    ///
    /// VHDL zxnext.vhd:1137,5226 — `transparent_rgb_2` is captured at
    /// stage 0 of each line; mid-frame Copper MOVEs to NR 0x14 (sky vs.
    /// foreground transparency-key swaps) take effect from the next
    /// scanline onward. The compositor reads `transparent_rgb_per_line_`
    /// at row N when present so flat-frame replay collapses are
    /// avoided. Snapshot/init follow the same idiom as
    /// fallback_per_line_/ula_enabled_per_line_.
    void snapshot_transparent_rgb_for_line(int line) {
        if (line >= 0 && line < 320)
            transparent_rgb_per_line_[line] = transparent_rgb_;
    }
    void init_transparent_rgb_per_line() {
        transparent_rgb_per_line_.fill(transparent_rgb_);
    }
    uint8_t transparent_rgb_for_line(int line) const {
        return (line >= 0 && line < 320) ? transparent_rgb_per_line_[line]
                                         : transparent_rgb_;
    }

    /// Per-scanline NR 0x68 bit 0 (stencil_mode) snapshot — gap G11.
    ///
    /// VHDL zxnext.vhd:5445, 7142-7176 — `ula_stencil_mode_2` is captured
    /// at stage 0 of each scanline; a Copper MOVE that flips the bit
    /// mid-frame takes effect from the next line.
    void snapshot_stencil_mode_for_line(int line) {
        if (line >= 0 && line < 320)
            stencil_mode_per_line_[line] = stencil_mode_;
    }
    void init_stencil_mode_per_line() {
        stencil_mode_per_line_.fill(stencil_mode_);
    }
    bool stencil_mode_for_line(int line) const {
        return (line >= 0 && line < 320) ? stencil_mode_per_line_[line]
                                         : stencil_mode_;
    }

    /// Per-scanline NR 0x68 bits 6:5 (ula_blend_mode) snapshot — gap G11.
    void snapshot_blend_mode_for_line(int line) {
        if (line >= 0 && line < 320)
            blend_mode_per_line_[line] = blend_mode_;
    }
    void init_blend_mode_per_line() {
        blend_mode_per_line_.fill(blend_mode_);
    }
    uint8_t blend_mode_for_line(int line) const {
        return (line >= 0 && line < 320) ? blend_mode_per_line_[line]
                                         : blend_mode_;
    }

    /// Per-scanline ULA clip window (NR 0x1A) snapshot.
    ///
    /// VHDL zxnext.vhd:988-991 — the ULA clip comparators consume the live
    /// `nr_1a_ula_clip_*` registers per pixel (values written via NR 0x1A's
    /// rotating 4-write cycle, zxnext.vhd:6779-6783, where the y2 clamp to
    /// 0xBF is also applied), so a mid-frame write takes effect at the raster
    /// position where it lands.  apply_ula_clip(row) reads this snapshot
    /// instead of the live Ula::clip_*() getters so rows the beam already
    /// passed keep the window that was in force when they were scanned.
    ///
    /// The snapshot stores the four EFFECTIVE values (clip_y2() is the
    /// clamped consumer-facing value, not the raw NR 0x1A byte), never the
    /// rotating write-cycle index — that lives in the NR dispatch.
    struct UlaClipWindow {
        uint8_t x1, x2, y1, y2;
    };
    void snapshot_ula_clip_for_line(int line) {
        if (line >= 0 && line < 320)
            ula_clip_per_line_[line] = live_ula_clip();
    }
    void init_ula_clip_per_line() {
        ula_clip_per_line_.fill(live_ula_clip());
    }
    UlaClipWindow ula_clip_for_line(int line) const {
        return (line >= 0 && line < 320) ? ula_clip_per_line_[line]
                                         : live_ula_clip();
    }

    /// Convert an 8-bit RRRGGGBB colour to ARGB8888.
    static uint32_t rrrgggbb_to_argb(uint8_t c) {
        uint8_t r3 = (c >> 5) & 0x07;
        uint8_t g3 = (c >> 2) & 0x07;
        uint8_t b2 = c & 0x03;
        uint8_t r8 = (r3 << 5) | (r3 << 2) | (r3 >> 1);
        uint8_t g8 = (g3 << 5) | (g3 << 2) | (g3 >> 1);
        uint8_t b8 = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;
        return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
    }

    /// Render one complete frame into the ARGB8888 framebuffer.
    ///
    /// @param framebuffer  Pointer to FB_WIDTH x FB_HEIGHT pixels (always 640×256).
    /// @param mmu          MMU for VRAM access (ULA).
    /// @param ram          Physical RAM for Layer 2 and Tilemap.
    /// @param palette      Palette manager for colour lookups.
    /// @param sprites      Sprite engine (may be null if not yet wired).
    /// @param tilemap      Tilemap renderer (may be null if not yet wired).
    void render_frame(uint32_t* framebuffer, Mmu& mmu, Ram& ram,
                      PaletteManager& palette,
                      class Layer2& layer2,
                      SpriteEngine* sprites,
                      Tilemap* tilemap);

    /// Render every layer for ONE framebuffer row and composite it.
    ///
    /// This is render_frame's row body, lifted out verbatim (Task 36) so the
    /// debugger's "All layers" view can composite through the very same code
    /// the live output does — NR 0x15 priority, per-layer transparency, the
    /// ULA/tilemap merge, Layer 2 priority promotion, the blend modes, the
    /// stencil, the border, and the NR 0x4A fallback colour. A second,
    /// hand-rolled compositor in the debugger would inevitably drift from
    /// this one; the fallback colour alone (emitted where EVERY layer is
    /// transparent, so it belongs to no layer view) is why the per-layer
    /// panels cannot be made to add up to the picture on screen.
    ///
    /// The CALLER owns the per-scanline change-log replay: rewind every log
    /// to the frame baseline, apply the entries for `row`, call this, and
    /// flush the remaining entries when done (render_frame does exactly that
    /// inline; VideoLayerView::render_to_image uses the same helpers). This
    /// function reads only the state the logs leave live, advances no cursor,
    /// and does not touch the once-per-frame ULA flash counter — so it is
    /// safe to call from a debug view on a paused machine.
    ///
    /// @param out  FB_WIDTH (640) ARGB8888 cells for this row.
    /// @param row  Framebuffer row 0..FB_HEIGHT-1.
    void render_row(uint32_t* out, int row, Mmu& mmu, Ram& ram,
                    PaletteManager& palette,
                    class Layer2& layer2,
                    SpriteEngine* sprites,
                    Tilemap* tilemap);

    /// Configure the per-pixel compositor trace (debug). When path is
    /// non-empty, the renderer dumps one CSV row per pixel of the target
    /// frame to that file, then closes it. Empty path disables tracing.
    /// Used for compositor-fidelity investigations (parallax.nex etc.).
    void set_compositor_trace(const std::string& path, int target_frame);

private:
    // Per-pixel compositor trace (set via set_compositor_trace; see CSV
    // dump in composite_scanline). Empty path = disabled, zero overhead.
    std::string trace_path_;
    int         trace_target_frame_   = 250;
    int         trace_frame_counter_  = 0;
    std::FILE*  trace_fp_             = nullptr;
    bool        trace_active_         = false;
    int         trace_current_row_    = 0;

    Ula ula_;
    uint8_t layer_priority_ = 0;    // NextREG 0x15 bits 4:2 (default SLU)
    uint8_t fallback_colour_ = 0xE3; // NextREG 0x4A (default transparent index)
    uint8_t transparent_rgb_ = 0xE3; // NextREG 0x14 (default transparent colour)
    bool sprite_en_ = false;         // NextREG 0x15 bit 0 (VHDL default: disabled)
    bool stencil_mode_ = false;      // NextREG 0x68 bit 0 (VHDL: stencil AND mode)
    uint8_t blend_mode_ = 0;         // NextREG 0x68 bits 6:5 (VHDL: ula_blend_mode_2)
    bool tm_enabled_ = false;        // NR 0x6B bit 7 (VHDL: tilemap enable)
    uint8_t nr15_raw_   = 0;         // last full byte written via write_nr15

    /// Host-side layer selection (see LayerMask above). Not machine state:
    /// deliberately absent from reset() / save_state() / load_state().
    uint8_t layer_mask_ = LAYER_ALL;

    // ── NR 0x15 per-scanline change log (G02) ────────────────────────
    struct Nr15Change {
        uint16_t line;
        uint8_t  raw;             ///< full 8-bit NR 0x15 byte
    };
    std::array<Nr15Change, MAX_NR15_CHANGES_PER_FRAME> nr15_change_log_{};
    size_t   nr15_change_count_     = 0;
    uint16_t nr15_current_line_     = 0;
    size_t   nr15_render_cursor_    = 0;
    bool     nr15_overflow_warned_  = false;

    uint8_t  baseline_nr15_raw_      = 0;
    uint8_t  baseline_layer_priority_= 0;
    bool     baseline_sprite_en_     = false;

    /// Per-scanline fallback colour snapshot.
    /// Populated during the frame loop by snapshot_fallback_for_line().
    /// Used during batch rendering so copper per-line changes are visible.
    std::array<uint8_t, 320> fallback_per_line_{};

    /// Per-scanline ULA-enable snapshot (NR 0x68 bit 7 inverted).
    /// Populated during the frame loop by snapshot_ula_enabled_for_line().
    /// VHDL zxnext.vhd:7103 samples ula_en_2 per pixel, so a Copper MOVE
    /// to NR 0x68 that lands on line N flips transparency from line N
    /// onward; the render loop consumes this array instead of the live
    /// Ula::ula_enabled() flag to preserve that per-line visibility.
    std::array<bool, 320>    ula_enabled_per_line_{};

    /// Per-scanline NR 0x14 transparent RGB snapshot — gap G04 closure.
    /// Populated by snapshot_transparent_rgb_for_line() during the
    /// frame loop. VHDL zxnext.vhd:1137,5226: transparent_rgb_2 is
    /// captured at stage 0 of every line.
    std::array<uint8_t, 320>  transparent_rgb_per_line_{};

    /// Per-scanline NR 0x68 b0 stencil snapshot — gap G11 closure.
    std::array<bool, 320>     stencil_mode_per_line_{};

    /// Per-scanline NR 0x68 b6:5 blend-mode snapshot — gap G11 closure.
    std::array<uint8_t, 320>  blend_mode_per_line_{};

    /// Per-scanline NR 0x1A ULA clip window snapshot (see UlaClipWindow).
    /// Like its stencil/blend/NR-0x14 siblings this is re-populated every
    /// frame (init at frame start + snapshot per scanline), so it is
    /// deliberately absent from save_state()/load_state().
    std::array<UlaClipWindow, 320> ula_clip_per_line_{};

    /// The four EFFECTIVE ULA clip values currently live on the Ula
    /// (clip_y2() already clamps per VHDL zxnext.vhd:6779-6783).
    UlaClipWindow live_ula_clip() const {
        return UlaClipWindow{ula_.clip_x1(), ula_.clip_x2(),
                             ula_.clip_y1(), ula_.clip_y2()};
    }

    // Transparent pixel marker — alpha channel = 0.
    static constexpr uint32_t TRANSPARENT = 0x00000000;

    // Per-scanline layer buffers (640 columns — canonical width post-G104).
    std::array<uint32_t, FB_WIDTH> ula_line_{};
    std::array<uint32_t, FB_WIDTH> layer2_line_{};
    std::array<uint32_t, FB_WIDTH> sprite_line_{};
    std::array<uint32_t, FB_WIDTH> tilemap_line_{};
    std::array<bool, FB_WIDTH>     tm_pixel_below_{};  // VHDL tm_pixel_below_2: per-pixel tilemap-below-ULA flag
    std::array<bool, FB_WIDTH>     tm_pixel_textmode_{}; // VHDL tm_pixel_textmode_2: per-pixel textmode flag (G101)
    std::array<bool, FB_WIDTH>     layer2_priority_{}; // palette bit 15 (L2 promotion)
    std::array<bool, FB_WIDTH>     ula_border_{};      // true when pixel is border region

    /// Composite one scanline from the layer buffers into the output.
    /// Always operates at FB_WIDTH (640) columns post-G104 Phase 1.
    /// @param fallback_argb  ARGB8888 fallback colour for when all layers are transparent.
    /// @param row  Framebuffer row, used to read the per-line ULA-enable
    ///             snapshot (ula_enabled_per_line_) for the stencil gate
    ///             (VHDL zxnext.vhd:7130 requires ula_en_2 = '1').
    void composite_scanline(uint32_t* dst, uint32_t fallback_argb, int row);

    /// Check if a pixel is transparent (alpha channel = 0).
    static bool is_transparent(uint32_t argb) { return (argb & 0xFF000000) == 0; }
};
