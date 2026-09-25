#include "video/lores.h"
#include "core/saveable.h"
#include "memory/ram.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

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
// deliberately absent from the stream — load_state refills it from the
// loaded register cell instead (GH #261), same as those.

// GH #27 S4 — the ONE field list (design §9.2). Four bytes at the END of
// block 10, after `Renderer`'s own scalars, because that is where
// `Renderer::save_state` has always nested this call. The keys are prefixed
// `lores_` because block 10 is ONE flat object holding three subsystems'
// fields, and `enabled` / `scroll_x` / `scroll_y` would not say whose they
// are there. It is legibility, not a live collision — `S4-KEYS-UNIQUE` is
// what proves the flattening is actually unambiguous.
//
// `enabled` is declared `boolean`, not `u8`. That is byte-identical, not
// merely equivalent: `StateWriter::write_bool` IS `write_u8(v ? 1 : 0)` and
// `StateReader::read_bool` IS `read_u8() != 0` (`saveable.h:45,94`), which is
// exactly what the two hand-written lines spelled out.
//
// NOT DECLARED: `per_line_`, rebuilt every frame by `init_per_line()` +
// `snapshot_for_line()` — §9.5(8). `load_state` refills it from the register
// cell after the walk (GH #261).
void Lores::describe_state(jnext::save::StateDesc& d)
{
    d.boolean("lores_enabled", live_.enabled);
    d.u8("lores_scroll_x", live_.scroll_x);
    d.u8("lores_scroll_y", live_.scroll_y);
    d.u8("lores_nr6a", live_.nr6a);
}

void Lores::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Lores::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
    after_load_state();
}

// GH #27 S4 — everything `load_state` does BESIDES the walk, for the reason
// `Ula::after_load_state` gives: `Renderer::describe_state` nests this
// declaration, so `Renderer::load_state` performs the walk and calls this.
void Lores::after_load_state()
{
    // NR $6A is 6 bits wide (zxnext.vhd:5032-5034), and `set_nr6a` masks
    // every live write the same way. The mask is applied HERE and not in the
    // declaration: it is a property of the RESTORE, not of the field, and
    // putting it in `describe_state` would change what the WRITE direction
    // emits — the one thing the byte-identity gate forbids. Same placement
    // S3 gave `Mmu::nr_8f_mode_` and `NextReg`'s two NR 0x03 masks.
    live_.nr6a = static_cast<uint8_t>(live_.nr6a & 0x3F);
    // GH #261 — refill from the state just loaded: a render before the next
    // init_per_line() (Emulator::rewind_to_frame) must not use the
    // pre-restore frame's rows.
    init_per_line();
}
