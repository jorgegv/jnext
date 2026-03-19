#include "video/renderer.h"
#include "memory/mmu.h"

void Renderer::render_frame(uint32_t* framebuffer, Mmu& mmu)
{
    // Phase 2: ULA only.
    // Phase 3 will add: Layer 2, Tilemap, Sprites in NextREG 0x15 priority order.
    ula_.render_frame(framebuffer, mmu);
}
