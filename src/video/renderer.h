#pragma once
#include <cstdint>
#include <array>
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
    static constexpr int FB_WIDTH     = 320;
    static constexpr int FB_WIDTH_HI  = 640;
    static constexpr int FB_HEIGHT    = 256;

    // Display area within the framebuffer (matches ULA constants)
    static constexpr int DISP_X = 32;   // left border width
    static constexpr int DISP_Y = 32;   // top border height
    static constexpr int DISP_W = 256;  // active display width
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
        fallback_per_line_.fill(0xE3);
        ula_enabled_per_line_.fill(true);   // Ula::reset() leaves ula_enabled_ = true
        ula_.reset();
    }

    /// Access the underlying ULA (e.g. to set border colour from port 0xFE).
    Ula& ula() { return ula_; }
    const Ula& ula() const { return ula_; }

    /// Set layer priority from NextREG 0x15 bits 4:2.
    void set_layer_priority(uint8_t val) { layer_priority_ = val & 0x07; }
    uint8_t layer_priority() const { return layer_priority_; }

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
    /// @param framebuffer  Pointer to FB_WIDTH_HI x FB_HEIGHT pixels (max).
    /// @param mmu          MMU for VRAM access (ULA).
    /// @param ram          Physical RAM for Layer 2 and Tilemap.
    /// @param palette      Palette manager for colour lookups.
    /// @param sprites      Sprite engine (may be null if not yet wired).
    /// @param tilemap      Tilemap renderer (may be null if not yet wired).
    /// @return             Actual composite width (320 or 640).
    int render_frame(uint32_t* framebuffer, Mmu& mmu, Ram& ram,
                     PaletteManager& palette,
                     class Layer2& layer2,
                     SpriteEngine* sprites,
                     Tilemap* tilemap);

private:
    Ula ula_;
    uint8_t layer_priority_ = 0;    // NextREG 0x15 bits 4:2 (default SLU)
    uint8_t fallback_colour_ = 0xE3; // NextREG 0x4A (default transparent index)
    uint8_t transparent_rgb_ = 0xE3; // NextREG 0x14 (default transparent colour)
    bool sprite_en_ = false;         // NextREG 0x15 bit 0 (VHDL default: disabled)
    bool stencil_mode_ = false;      // NextREG 0x68 bit 0 (VHDL: stencil AND mode)
    uint8_t blend_mode_ = 0;         // NextREG 0x68 bits 6:5 (VHDL: ula_blend_mode_2)
    bool tm_enabled_ = false;        // NR 0x6B bit 7 (VHDL: tilemap enable)

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

    // Transparent pixel marker — alpha channel = 0.
    static constexpr uint32_t TRANSPARENT = 0x00000000;

    // Per-scanline layer buffers (640 pixels max to support hi-res modes)
    std::array<uint32_t, FB_WIDTH_HI> ula_line_{};
    std::array<uint32_t, FB_WIDTH_HI> layer2_line_{};
    std::array<uint32_t, FB_WIDTH_HI> sprite_line_{};
    std::array<uint32_t, FB_WIDTH_HI> tilemap_line_{};
    std::array<bool, FB_WIDTH_HI>     tm_pixel_below_{};  // VHDL tm_pixel_below_2: per-pixel tilemap-below-ULA flag
    std::array<bool, FB_WIDTH_HI>     layer2_priority_{}; // palette bit 15 (L2 promotion)
    std::array<bool, FB_WIDTH_HI>     ula_border_{};      // true when pixel is border region

    /// True when any 640px layer is active this frame.
    bool hi_res_active_ = false;

    /// Active composite width (320 or 640) for the current frame.
    int composite_width_ = FB_WIDTH;

    /// Composite one scanline from the layer buffers into the output.
    /// @param fallback_argb  ARGB8888 fallback colour for when all layers are transparent.
    /// @param width          Composite width (320 or 640).
    void composite_scanline(uint32_t* dst, uint32_t fallback_argb, int width);

    /// Check if a pixel is transparent (alpha channel = 0).
    static bool is_transparent(uint32_t argb) { return (argb & 0xFF000000) == 0; }
};
