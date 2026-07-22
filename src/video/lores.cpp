#include "video/lores.h"
#include "core/saveable.h"
#include "memory/ram.h"

// ---------------------------------------------------------------------------
// read_bank5 — LoRes port-A fetch from the dedicated bank-5 VRAM
// ---------------------------------------------------------------------------
//
// zxnext.vhd:6631
//     vram_bank5_a0 <= cpu_bank5_addr when cpu_bank5_sched = '1' else lores_addr;
// with `lores_addr` a raw 14-bit bank-5 offset (lores.vhd:56).  The CPU wins
// the shared port and the LoRes fetch is deferred to the next 28 MHz slot; it
// still completes inside the pixel period, so there is no visible effect and
// no added CPU contention (which is what plan rows LR-163/164 assert).
//
// Bank 5 is a dedicated 16K dual-port BRAM (zxnext.vhd:6558-6578), NOT a slice
// of external SRAM and never bank 7 — the shadow-screen mux at
// zxnext.vhd:6651-6655 sits on the ULA's port B.  The page-10 fallback below
// mirrors Ula::vram_read's own fallback so a standalone Ram fixture (unit
// tests, non-Next machines) still resolves to physical bank 5.

uint8_t Lores::read_bank5(uint16_t addr, Ram& ram) const
{
    const uint16_t off = static_cast<uint16_t>(addr & 0x3FFF);
    if (bank5_vram_) return bank5_vram_[off];
    return ram.read(10u * 8192u + off);
}

// ---------------------------------------------------------------------------
// save_state / load_state
// ---------------------------------------------------------------------------
//
// Only the live register cell is machine state.  `per_line_` is rebuilt every
// frame (init_per_line at frame start + snapshot_for_line per scanline),
// exactly like Renderer's stencil/blend/NR-0x14 snapshots, so it is
// deliberately absent — same rationale as those.

void Lores::save_state(StateWriter& w) const
{
    w.write_u8(live_.enabled ? 1 : 0);
    w.write_u8(live_.scroll_x);
    w.write_u8(live_.scroll_y);
    w.write_u8(live_.nr6a);
}

void Lores::load_state(StateReader& r)
{
    live_.enabled  = r.read_u8() != 0;
    live_.scroll_x = r.read_u8();
    live_.scroll_y = r.read_u8();
    live_.nr6a     = static_cast<uint8_t>(r.read_u8() & 0x3F);
}
