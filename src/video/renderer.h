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
    static constexpr int FB_WIDTH  = 320;
    static constexpr int FB_HEIGHT = 256;

    // Display area within the framebuffer (matches ULA constants)
    static constexpr int DISP_X = 32;   // left border width
    static constexpr int DISP_Y = 32;   // top border height
    static constexpr int DISP_W = 256;  // active display width
    static constexpr int DISP_H = 192;  // active display height

    /// Access the underlying ULA (e.g. to set border colour from port 0xFE).
    Ula& ula() { return ula_; }

    /// Set layer priority from NextREG 0x15 bits 4:2.
    void set_layer_priority(uint8_t val) { layer_priority_ = val & 0x07; }
    uint8_t layer_priority() const { return layer_priority_; }

    /// Set the fallback colour index (NextREG 0x4A).
    /// Used when all layers are transparent at a given pixel.
    void set_fallback_colour(uint8_t val) { fallback_colour_ = val; }
    uint8_t fallback_colour() const { return fallback_colour_; }

    /// Render one complete frame into the ARGB8888 framebuffer.
    ///
    /// @param framebuffer  Pointer to FB_WIDTH x FB_HEIGHT pixels.
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

private:
    Ula ula_;
    uint8_t layer_priority_ = 0;    // NextREG 0x15 bits 4:2 (default SLU)
    uint8_t fallback_colour_ = 0xE3; // NextREG 0x4A (default transparent index)

    // Transparent pixel marker — alpha channel = 0.
    static constexpr uint32_t TRANSPARENT = 0x00000000;

    // Per-scanline layer buffers (320 pixels each)
    std::array<uint32_t, FB_WIDTH> ula_line_{};
    std::array<uint32_t, FB_WIDTH> layer2_line_{};
    std::array<uint32_t, FB_WIDTH> sprite_line_{};
    std::array<uint32_t, FB_WIDTH> tilemap_line_{};
    std::array<bool, FB_WIDTH>     ula_over_flags_{};  // tilemap per-tile ULA priority

    /// Composite one scanline from the layer buffers into the output.
    /// Applies transparency: a pixel with alpha=0 is transparent.
    void composite_scanline(uint32_t* dst);

    /// Check if a pixel is transparent (alpha channel = 0).
    static bool is_transparent(uint32_t argb) { return (argb & 0xFF000000) == 0; }
};
