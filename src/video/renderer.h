#pragma once
#include <cstdint>
#include "video/ula.h"

class Mmu;

/// Video compositor.
///
/// Currently a thin wrapper around the ULA renderer.
/// Later phases will add Layer 2, sprites, and the tilemap here, composited
/// in the priority order controlled by NextREG 0x15.
class Renderer {
public:
    /// Access the underlying ULA (e.g. to set border colour from port 0xFE).
    Ula& ula() { return ula_; }

    /// Render one complete frame into the ARGB8888 framebuffer.
    ///
    /// @param framebuffer  Pointer to Ula::FB_WIDTH × Ula::FB_HEIGHT pixels.
    /// @param mmu          MMU for VRAM access.
    void render_frame(uint32_t* framebuffer, Mmu& mmu);

private:
    Ula ula_;
};
