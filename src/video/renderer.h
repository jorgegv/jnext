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
        fallback_per_line_.fill(0xE3);
        ula_.reset();
    }

    /// Access the underlying ULA (e.g. to set border colour from port 0xFE).
    Ula& ula() { return ula_; }

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

    /// Per-scanline fallback colour snapshot.
    /// Populated during the frame loop by snapshot_fallback_for_line().
    /// Used during batch rendering so copper per-line changes are visible.
    std::array<uint8_t, 320> fallback_per_line_{};

    // Transparent pixel marker — alpha channel = 0.
    static constexpr uint32_t TRANSPARENT = 0x00000000;

    // Per-scanline layer buffers (640 pixels max to support hi-res modes)
    std::array<uint32_t, FB_WIDTH_HI> ula_line_{};
    std::array<uint32_t, FB_WIDTH_HI> layer2_line_{};
    std::array<uint32_t, FB_WIDTH_HI> sprite_line_{};
    std::array<uint32_t, FB_WIDTH_HI> tilemap_line_{};
    std::array<bool, FB_WIDTH_HI>     ula_over_flags_{};  // tilemap per-tile ULA priority

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
