#include "video/palette.h"
#include "core/log.h"
#include "core/saveable.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// RGB333 → ARGB8888 helper
// ---------------------------------------------------------------------------

uint32_t rgb333_to_argb8888(uint8_t r3, uint8_t g3, uint8_t b3)
{
    // Expand 3-bit component to 8-bit: xxx → xxx_xx_x (full range 0–255).
    uint8_t r8 = static_cast<uint8_t>((r3 << 5) | (r3 << 2) | (r3 >> 1));
    uint8_t g8 = static_cast<uint8_t>((g3 << 5) | (g3 << 2) | (g3 >> 1));
    uint8_t b8 = static_cast<uint8_t>((b3 << 5) | (b3 << 2) | (b3 >> 1));
    return 0xFF000000u
         | (static_cast<uint32_t>(r8) << 16)
         | (static_cast<uint32_t>(g8) <<  8)
         |  static_cast<uint32_t>(b8);
}

// ---------------------------------------------------------------------------
// Default ULA palette as RGB333 values — the 16 hardware-defined boot
// defaults of the 256-entry palette.  Used ONLY by reset() to seed the
// std-ULA encoder slots (0x00..0x0F ink, 0x10..0x1F paper).  Not exposed
// outside this translation unit: the hardware has a single 256-entry
// palette and no narrower colour table.
// ---------------------------------------------------------------------------

static const uint16_t kDefaultUlaRgb333[16] = {
    // RGB333 packed: (R << 6) | (G << 3) | B
    // Normal (indices 0-7)
    (0 << 6) | (0 << 3) | 0,   // 0 Black
    (0 << 6) | (0 << 3) | 6,   // 1 Blue
    (6 << 6) | (0 << 3) | 0,   // 2 Red
    (6 << 6) | (0 << 3) | 6,   // 3 Magenta
    (0 << 6) | (6 << 3) | 0,   // 4 Green
    (0 << 6) | (6 << 3) | 6,   // 5 Cyan
    (6 << 6) | (6 << 3) | 0,   // 6 Yellow
    (6 << 6) | (6 << 3) | 6,   // 7 White
    // Bright (indices 8-15)
    (0 << 6) | (0 << 3) | 0,   // 8  Bright Black
    (0 << 6) | (0 << 3) | 7,   // 9  Bright Blue
    (7 << 6) | (0 << 3) | 0,   // 10 Bright Red
    (7 << 6) | (0 << 3) | 7,   // 11 Bright Magenta
    (0 << 6) | (7 << 3) | 0,   // 12 Bright Green
    (0 << 6) | (7 << 3) | 7,   // 13 Bright Cyan
    (7 << 6) | (7 << 3) | 0,   // 14 Bright Yellow
    (7 << 6) | (7 << 3) | 7,   // 15 Bright White
};

// ---------------------------------------------------------------------------
// Helper: convert uint16_t RGB333 to ARGB8888
// ---------------------------------------------------------------------------

static uint32_t rgb333_to_argb(uint16_t rgb333)
{
    uint8_t r = (rgb333 >> 6) & 0x07;
    uint8_t g = (rgb333 >> 3) & 0x07;
    uint8_t b =  rgb333       & 0x07;
    return rgb333_to_argb8888(r, g, b);
}

// ---------------------------------------------------------------------------
// PaletteManager
// ---------------------------------------------------------------------------

PaletteManager::PaletteManager()
{
    reset();
}

void PaletteManager::reset()
{
    control_ = 0;
    index_ = 0;
    target_palette_ = PaletteId::ULA_FIRST;
    auto_inc_disabled_ = false;
    active_ula_second_ = false;
    active_l2_second_ = false;
    active_spr_second_ = false;
    active_tm_second_ = false;
    ulanext_mode_ = false;
    nine_bit_first_written_ = false;
    nine_bit_first_byte_ = 0;
    global_transparency_ = 0xE3;
    sprite_transparency_ = 0xE3;
    tilemap_transparency_ = 0x0F;

    // G102 — Initialize the single 256-entry × 2-bank ULA palette store.
    //
    // VHDL `palette_utm` dpram (zxnext.vhd:6960) holds 256 entries per
    // ULA bank.  The std-ULA encoder (zxula.vhd:543-553) emits an 8-bit
    // `ula_pixel` in 0x00..0x1F (4 sub-cycles × 8 colours):
    //   0x00..0x07: pixel_en=1, attr(6)=0 → ink colour 0..7
    //   0x08..0x0F: pixel_en=1, attr(6)=1 → ink bright colour 8..15
    //   0x10..0x17: pixel_en=0, attr(6)=0 → paper colour 0..7 (= ink 0..7)
    //   0x18..0x1F: pixel_en=0, attr(6)=1 → paper bright 8..15 (= ink 8..15)
    // So a firmware-untouched palette must hold the 16 ZX colours at both
    // 0x00..0x0F AND 0x10..0x1F so that the 32 standard-ULA encodings all
    // resolve to the right colour.  Indices 0x20..0xFF default to RRRGGGBB-
    // identity (matches Layer2/Sprite defaults; ULAnext / ULA+ programs
    // override these before use).
    for (int p = 0; p < 2; ++p) {
        for (int i = 0; i < 16; ++i) {
            ula_rgb333_[p][i]      = kDefaultUlaRgb333[i];
            ula_argb_[p][i]        = rgb333_to_argb(kDefaultUlaRgb333[i]);
            // Mirror at 0x10..0x1F (paper variants of the 0x00..0x0F ink
            // values; same colour, different encoder branch).
            ula_rgb333_[p][i + 16] = kDefaultUlaRgb333[i];
            ula_argb_[p][i + 16]   = rgb333_to_argb(kDefaultUlaRgb333[i]);
        }
        for (int i = 32; i < FULL_SIZE; ++i) {
            uint16_t rgb333 = rrrgggbb_to_rgb333(static_cast<uint8_t>(i));
            ula_rgb333_[p][i] = rgb333;
            ula_argb_[p][i]   = rgb333_to_argb(rgb333);
        }
    }

    // Initialize Layer 2 and Sprite palettes to the default RRRGGGBB mapping.
    // On real hardware (VHDL), the default palette maps each index 0-255
    // to its own RRRGGGBB colour value.
    for (int p = 0; p < 2; ++p) {
        for (int i = 0; i < 256; ++i) {
            uint16_t rgb333 = rrrgggbb_to_rgb333(static_cast<uint8_t>(i));
            layer2_rgb333_[p][i] = rgb333;
            layer2_argb_[p][i] = rgb333_to_argb(rgb333);
            sprite_rgb333_[p][i] = rgb333;
            sprite_argb_[p][i] = rgb333_to_argb(rgb333);
        }
        tilemap_rgb333_[p].fill(0);
        tilemap_argb_[p].fill(0xFF000000u);
        // Priority defaults to 00 at power-on (VHDL zxnext.vhd:4920
        // else-branch yields 00 for any non-NR-0x44 write; the dpram
        // initial state is undefined in HW but the firmware writes it
        // before use, and 00 is the "not promoted" expectation).
        // VERIFY4 / pass-4 — extended priority storage for all four palette
        // types so NR 0x44 readback returns the stored bits 15:14 of the
        // dpram word for every palette target.
        ula_priority_[p].fill(0);
        layer2_priority_[p].fill(0);
        sprite_priority_[p].fill(0);
        tilemap_priority_[p].fill(0);

        // NR 0xFF / ULA+ palette poke storage — VHDL palette_utm dpram
        // initial state is undefined in hardware; we zero it so tests
        // and future ULA+ render consumers see a deterministic baseline.
        ulap_poke_rgb333_[p].fill(0);
    }
}

// ---------------------------------------------------------------------------
// NR 0xFF — ULA+ palette poke side-channel
// ---------------------------------------------------------------------------
//
// VHDL zxnext.vhd:6957-6958 (write enable + address override),
// :4919 (value byte expansion), :4920 (priority forced to "00" since
// nr_44_we is not asserted on an NR 0xFF write).
//
// We delegate the RRRGGGBB → RRRGGGBBB byte expansion to the same
// helper that NR 0x41 uses (rrrgggbb_to_rgb333), preserving the
// `B0 = B1 or B0` rule from the VHDL.
//
// Storage layout follows the VHDL palette_utm dpram address derivation:
// the bf3b_index already corresponds to bits 5:0 of the dpram address
// (with bits 7:6 = "11" implicit), and bank_second corresponds to bit 8
// (palette_write_select(2)).  The "11" upper-quadrant placement is
// implicit in the storage shape: 64 entries per bank, addressed by the
// 6-bit bf3b_index directly.
void PaletteManager::nr_ff_poke(bool bank_second, uint8_t bf3b_index,
                                uint8_t byte)
{
    const uint16_t rgb333 = rrrgggbb_to_rgb333(byte);
    ulap_poke_rgb333_[bank_second ? 1 : 0][bf3b_index & 0x3F] = rgb333;
    // NOTE: priority is "00" per VHDL zxnext.vhd:4920 (NR 0x44 is the
    // only path that captures priority bits); we don't store a separate
    // priority slot for NR 0xFF pokes because there is nothing to
    // capture.  The future ULA+ render path will see priority = 0.
}

// ---------------------------------------------------------------------------
// NextREG 0x43 — Palette Control
// ---------------------------------------------------------------------------

void PaletteManager::write_control(uint8_t val)
{
    control_ = val;

    target_palette_    = static_cast<PaletteId>((val >> 4) & 0x07);
    auto_inc_disabled_ = (val & 0x80) != 0;
    active_spr_second_ = (val & 0x08) != 0;
    active_l2_second_  = (val & 0x04) != 0;
    active_ula_second_ = (val & 0x02) != 0;
    ulanext_mode_      = (val & 0x01) != 0;

    // NOTE: active_tm_second_ is NOT derived from NR 0x43 here.  Per VHDL,
    // the authoritative tilemap palette select for rendering is
    // nr_6b_tm_palette_select (NR 0x6B bit 4).  NR 0x43 only selects the
    // read/write target palette for palette-I/O; it must not change which
    // tilemap palette the compositor uses at render time.  The correct
    // update path is core/emulator.cpp -> PaletteManager::set_active_tilemap_palette()
    // from the NR 0x6B write handler.
    // (Earlier code derived active_tm_second_ from target_palette_ here as a
    // workaround; that was removed to match VHDL.)
    //
    // Reset 9-bit write state on control change.
    nine_bit_first_written_ = false;
}

// ---------------------------------------------------------------------------
// NextREG 0x40 — Palette Index
// ---------------------------------------------------------------------------

void PaletteManager::set_index(uint8_t idx)
{
    index_ = idx;
    // Reset 9-bit write state on index change.
    nine_bit_first_written_ = false;
}

// ---------------------------------------------------------------------------
// NextREG 0x41 — 8-bit palette write (RRRGGGBB)
// ---------------------------------------------------------------------------

void PaletteManager::write_8bit(uint8_t val)
{
    // Reset 9-bit state since we're doing an 8-bit write.
    nine_bit_first_written_ = false;

    uint16_t rgb333 = rrrgggbb_to_rgb333(val);
    write_entry(rgb333);
}

// ---------------------------------------------------------------------------
// NextREG 0x41 — 8-bit palette read (RRRGGGBB)
// ---------------------------------------------------------------------------
//
// VHDL zxnext.vhd:6038-6039 — reading NR 0x41 returns nr_palette_dat(8:1),
// the upper 8 bits of the 9-bit RGB333 value at the target + index selected
// by NR 0x43 / NR 0x40. Pure read: does not mutate nine_bit latch or index.

uint8_t PaletteManager::read_8bit() const
{
    int bank = (static_cast<int>(target_palette_) >= 4) ? 1 : 0;
    uint16_t rgb333 = 0;

    switch (target_palette_) {
        case PaletteId::ULA_FIRST:
        case PaletteId::ULA_SECOND:
            // G102 — single 256-entry × 2-bank ULA store; full 8-bit
            // `nr_palette_idx` is the dpram address (zxnext.vhd:6953
            // routed through nr_palette_dat).
            rgb333 = ula_rgb333_[bank][index_];
            break;
        case PaletteId::LAYER2_FIRST:
        case PaletteId::LAYER2_SECOND:
            rgb333 = layer2_rgb333_[bank][index_];
            break;
        case PaletteId::SPRITE_FIRST:
        case PaletteId::SPRITE_SECOND:
            rgb333 = sprite_rgb333_[bank][index_];
            break;
        case PaletteId::TILEMAP_FIRST:
        case PaletteId::TILEMAP_SECOND:
            rgb333 = tilemap_rgb333_[bank][index_];
            break;
    }

    // RGB333 → RRRGGGBB (discard blue LSB: bit 0 of the 9-bit value).
    uint8_t r3 = (rgb333 >> 6) & 0x07;
    uint8_t g3 = (rgb333 >> 3) & 0x07;
    uint8_t b3 = rgb333        & 0x07;
    return static_cast<uint8_t>((r3 << 5) | (g3 << 2) | (b3 >> 1));
}

// ---------------------------------------------------------------------------
// NextREG 0x44 — 9-bit palette read (RRR.GGG.BBB.priority).
// VHDL zxnext.vhd:6047-6048:
//     port_253b_dat <= nr_palette_dat(10:9) & "00000" & nr_palette_dat(0);
// bits 7:6 = priority (2 bits, stored only for Layer 2 entries via NR 0x44
//            second write; ULA/sprite/tilemap entries have priority "00"),
// bits 5:1 = constant zero, bit 0 = blue LSB at (target, index).
//
// VERIFY4 / pass-4 — pre-fix the NR 0x44 readback fell through to the bare
// regs_[0x44] last-write-wins shadow, which leaks the SECOND-byte raw value
// instead of the priority/blue-LSB composition. Pure read — never mutates
// index, sub_idx, or priority state.
// ---------------------------------------------------------------------------
uint8_t PaletteManager::read_9bit() const
{
    int bank = (static_cast<int>(target_palette_) >= 4) ? 1 : 0;
    uint16_t rgb333 = 0;
    uint8_t  priority = 0;

    switch (target_palette_) {
        case PaletteId::ULA_FIRST:
        case PaletteId::ULA_SECOND:
            rgb333   = ula_rgb333_[bank][index_];
            priority = ula_priority_[bank][index_] & 0x03;
            break;
        case PaletteId::LAYER2_FIRST:
        case PaletteId::LAYER2_SECOND:
            rgb333   = layer2_rgb333_[bank][index_];
            priority = layer2_priority_[bank][index_] & 0x03;
            break;
        case PaletteId::SPRITE_FIRST:
        case PaletteId::SPRITE_SECOND:
            rgb333   = sprite_rgb333_[bank][index_];
            priority = sprite_priority_[bank][index_] & 0x03;
            break;
        case PaletteId::TILEMAP_FIRST:
        case PaletteId::TILEMAP_SECOND:
            rgb333   = tilemap_rgb333_[bank][index_];
            priority = tilemap_priority_[bank][index_] & 0x03;
            break;
    }

    // bits 7:6 = priority, bits 5:1 = 0, bit 0 = blue LSB (rgb333 bit 0).
    return static_cast<uint8_t>(((priority & 0x03) << 6) | (rgb333 & 0x01));
}

// ---------------------------------------------------------------------------
// NextREG 0x44 — 9-bit palette write (two consecutive writes)
// ---------------------------------------------------------------------------

void PaletteManager::write_9bit(uint8_t val)
{
    if (!nine_bit_first_written_) {
        // First write: store RRRGGGBB byte.
        nine_bit_first_byte_ = val;
        nine_bit_first_written_ = true;
    } else {
        // Second write: bit 0 = blue LSB (9th bit), bits 7:6 = palette
        // priority slot for Layer 2 (VHDL zxnext.vhd:4920 captures
        // nr_wr_dat(7 downto 6) into nr_palette_priority on the second
        // NR 0x44 write, then routes those two bits into bits 15:14 of
        // the L2/sprite dpram word at zxnext.vhd:7025).
        uint8_t first = nine_bit_first_byte_;
        uint8_t r3 = (first >> 5) & 0x07;
        uint8_t g3 = (first >> 2) & 0x07;
        // Blue: high 2 bits from first byte bits 1:0, LSB from second byte bit 0.
        uint8_t b3 = static_cast<uint8_t>(((first & 0x03) << 1) | (val & 0x01));

        uint16_t rgb333 = static_cast<uint16_t>((r3 << 6) | (g3 << 3) | b3);
        uint8_t priority = static_cast<uint8_t>((val >> 6) & 0x03);
        write_entry(rgb333, priority);

        nine_bit_first_written_ = false;
    }
}

// ---------------------------------------------------------------------------
// Internal: write a colour entry to the currently targeted palette
// ---------------------------------------------------------------------------

void PaletteManager::write_entry(uint16_t rgb333, uint8_t priority)
{
    // Per-scanline log: record BEFORE applying so that apply_change can
    // be reused for replay without re-logging.
    //
    // G102 — log the FULL 8-bit `index_` for ULA targets.  Matches VHDL
    // `nr_palette_idx[7:0]` flowing into the palette_utm address at
    // zxnext.vhd:6952, 6957 (8-bit nr_palette_idx drives the dpram
    // address with no folding; the single 256-entry × 2-bank ULA
    // store covers the full encoder output range 0x00..0xFF).
    if (change_count_ < MAX_CHANGES_PER_FRAME) {
        change_log_[change_count_++] = PaletteChange{
            current_line_,
            target_palette_,
            index_,
            rgb333,
            priority,
        };
    } else if (!overflow_warned_) {
        Log::video()->warn(
            "PaletteManager: change-log full at line {} (cap {} per frame); "
            "further palette writes this frame will not be per-scanline. "
            "TASK-PER-SCANLINE-PALETTE-PLAN.md §Q1.",
            current_line_, MAX_CHANGES_PER_FRAME);
        overflow_warned_ = true;
    }

    apply_change(PaletteChange{current_line_, target_palette_,
                               static_cast<uint8_t>(index_), rgb333,
                               priority});

    advance_index();
}

void PaletteManager::apply_change(const PaletteChange& c)
{
    const int bank = (static_cast<int>(c.target) >= 4) ? 1 : 0;
    const uint32_t argb = rgb333_to_argb(c.rgb333);

    switch (c.target) {
        case PaletteId::ULA_FIRST:
        case PaletteId::ULA_SECOND:
            // G102 — Single 256-entry × 2-bank ULA store.  The full 8-bit
            // `nr_palette_idx` drives the dpram address with no folding,
            // matching VHDL palette_utm at `'0' & write_select(2) &
            // nr_palette_idx[7:0]` (zxnext.vhd:6952, 6957).
            ula_rgb333_[bank][c.index] = c.rgb333;
            ula_argb_[bank][c.index]   = argb;
            // VERIFY4 / pass-4 — priority is written to bits 15:14 of the
            // palette_utm dpram word for ULA writes too (zxnext.vhd:6972).
            // Renderer ignores the bit, but NR 0x44 readback exposes it.
            ula_priority_[bank][c.index] = c.priority & 0x03;
            break;
        case PaletteId::LAYER2_FIRST:
        case PaletteId::LAYER2_SECOND:
            layer2_rgb333_[bank][c.index]   = c.rgb333;
            layer2_argb_[bank][c.index]     = argb;
            // Per VHDL zxnext.vhd:4920, only NR 0x44 (9-bit) writes
            // populate nr_palette_priority; NR 0x41 (8-bit) writes
            // store '00'. write_9bit / write_8bit pass the right value
            // through PaletteChange::priority.
            layer2_priority_[bank][c.index] = c.priority & 0x03;
            break;
        case PaletteId::SPRITE_FIRST:
        case PaletteId::SPRITE_SECOND:
            sprite_rgb333_[bank][c.index] = c.rgb333;
            sprite_argb_[bank][c.index] = argb;
            // VERIFY4 / pass-4 — priority bits stored in palette_l2s dpram
            // for sprite writes (zxnext.vhd:7025) but NOT consumed by
            // renderer (l2s_prgb(15) only routes for layer2 at :7039).
            // Surface them for NR 0x44 readback.
            sprite_priority_[bank][c.index] = c.priority & 0x03;
            break;
        case PaletteId::TILEMAP_FIRST:
        case PaletteId::TILEMAP_SECOND:
            tilemap_rgb333_[bank][c.index] = c.rgb333;
            tilemap_argb_[bank][c.index] = argb;
            // VERIFY4 / pass-4 — tilemap entries land in palette_utm dpram
            // alongside ULA entries (zxnext.vhd:6972), which captures the
            // full 11-bit nr_palette_priority & nr_palette_value word.
            // Renderer ignores priority for tilemap; NR 0x44 readback needs it.
            tilemap_priority_[bank][c.index] = c.priority & 0x03;
            break;
    }
}

// ---------------------------------------------------------------------------
// Per-scanline snapshot API — TASK-PER-SCANLINE-PALETTE-PLAN.md
// ---------------------------------------------------------------------------

void PaletteManager::start_frame()
{
    // Memcpy the live state into the baseline. Same shape on both sides.
    for (int p = 0; p < 2; ++p) {
        baseline_ula_rgb333_[p]      = ula_rgb333_[p];
        baseline_ula_argb_[p]        = ula_argb_[p];
        baseline_layer2_rgb333_[p]   = layer2_rgb333_[p];
        baseline_layer2_argb_[p]     = layer2_argb_[p];
        baseline_sprite_rgb333_[p]   = sprite_rgb333_[p];
        baseline_sprite_argb_[p]     = sprite_argb_[p];
        baseline_tilemap_rgb333_[p]  = tilemap_rgb333_[p];
        baseline_tilemap_argb_[p]    = tilemap_argb_[p];
        baseline_layer2_priority_[p] = layer2_priority_[p];
    }
    change_count_     = 0;
    render_cursor_    = 0;
    current_line_     = 0;
    overflow_warned_  = false;
}

void PaletteManager::rewind_to_baseline()
{
    for (int p = 0; p < 2; ++p) {
        ula_rgb333_[p]      = baseline_ula_rgb333_[p];
        ula_argb_[p]        = baseline_ula_argb_[p];
        layer2_rgb333_[p]   = baseline_layer2_rgb333_[p];
        layer2_argb_[p]     = baseline_layer2_argb_[p];
        sprite_rgb333_[p]   = baseline_sprite_rgb333_[p];
        sprite_argb_[p]     = baseline_sprite_argb_[p];
        tilemap_rgb333_[p]  = baseline_tilemap_rgb333_[p];
        tilemap_argb_[p]    = baseline_tilemap_argb_[p];
        layer2_priority_[p] = baseline_layer2_priority_[p];
    }
    render_cursor_ = 0;
}

void PaletteManager::apply_changes_for_line(int line)
{
    // Log is in scanline order (writes append with monotonic
    // current_line_). Advance the cursor while entries match the
    // requested line. Total cost across a full render is O(change_count_).
    while (render_cursor_ < change_count_
        && change_log_[render_cursor_].line == line) {
        apply_change(change_log_[render_cursor_]);
        ++render_cursor_;
    }
}

void PaletteManager::flush_remaining_changes()
{
    // Drain log entries that the per-line render loop did not reach.
    // Mirrors Tilemap::flush_remaining_nr6b_changes. Without this, palette
    // writes tagged at line >= FB_HEIGHT (vblank) get dropped: rewind has
    // already undone their direct mutation, and apply_changes_for_line(0..H)
    // never matches a vblank line. The next frame's start_frame() then
    // snapshots a stale baseline. tilemap_demo at NR 0x07 >= 0x02 (CPU
    // 14/28 MHz) finishes its NR 0x40/0x41 setup during vblank, so without
    // this flush the active tilemap palette stays at its (zero) baseline
    // every frame and every tilemap pixel renders as palette[0] = black.
    while (render_cursor_ < change_count_) {
        apply_change(change_log_[render_cursor_]);
        ++render_cursor_;
    }
}

// ---------------------------------------------------------------------------
// Internal: advance palette index (if auto-increment enabled)
// ---------------------------------------------------------------------------

void PaletteManager::advance_index()
{
    if (!auto_inc_disabled_) {
        ++index_;  // wraps naturally at 256 (uint8_t)
    }
}

// ---------------------------------------------------------------------------
// Internal: convert 8-bit RRRGGGBB to 9-bit RGB333
// ---------------------------------------------------------------------------

uint16_t PaletteManager::rrrgggbb_to_rgb333(uint8_t val)
{
    uint8_t r3 = (val >> 5) & 0x07;
    uint8_t g3 = (val >> 2) & 0x07;
    // Blue: 2 bits from val, LSB derived as (B1 | B0) per hardware.
    uint8_t b2 = val & 0x03;
    uint8_t b3 = static_cast<uint8_t>((b2 << 1) | ((b2 >> 1) | (b2 & 1)));
    return static_cast<uint16_t>((r3 << 6) | (g3 << 3) | b3);
}

void PaletteManager::save_state(StateWriter& w) const
{
    // G102 — single 256-entry × 2-bank ULA palette store.
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) w.write_u16(ula_rgb333_[p][i]);
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) w.write_u16(layer2_rgb333_[p][i]);
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) w.write_u16(sprite_rgb333_[p][i]);
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) w.write_u16(tilemap_rgb333_[p][i]);
    w.write_u8(control_);
    w.write_u8(index_);
    w.write_u8(static_cast<uint8_t>(target_palette_));
    w.write_bool(auto_inc_disabled_);
    w.write_bool(active_ula_second_);
    w.write_bool(active_l2_second_);
    w.write_bool(active_spr_second_);
    w.write_bool(active_tm_second_);
    w.write_bool(ulanext_mode_);
    w.write_bool(nine_bit_first_written_);
    w.write_u8(nine_bit_first_byte_);
    w.write_u8(global_transparency_);
    w.write_u8(sprite_transparency_);
    w.write_u8(tilemap_transparency_);
    // Layer 2 palette priority (NR 0x44 b7:6 capture). Save AFTER the
    // existing fields so older save-states keep loading via the
    // backward-compat read path below (priority defaults to 0).
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) w.write_u8(layer2_priority_[p][i]);
}

void PaletteManager::load_state(StateReader& r)
{
    // G102 — single 256-entry × 2-bank ULA palette store (paired with
    // matching save_state writer above).
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) {
            ula_rgb333_[p][i] = r.read_u16();
            ula_argb_[p][i] = rgb333_to_argb(ula_rgb333_[p][i]);
        }
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) {
            layer2_rgb333_[p][i] = r.read_u16();
            layer2_argb_[p][i] = rgb333_to_argb(layer2_rgb333_[p][i]);
        }
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) {
            sprite_rgb333_[p][i] = r.read_u16();
            sprite_argb_[p][i] = rgb333_to_argb(sprite_rgb333_[p][i]);
        }
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i) {
            tilemap_rgb333_[p][i] = r.read_u16();
            tilemap_argb_[p][i] = rgb333_to_argb(tilemap_rgb333_[p][i]);
        }
    control_ = r.read_u8();
    index_ = r.read_u8();
    target_palette_ = static_cast<PaletteId>(r.read_u8());
    auto_inc_disabled_ = r.read_bool();
    active_ula_second_ = r.read_bool();
    active_l2_second_ = r.read_bool();
    active_spr_second_ = r.read_bool();
    active_tm_second_ = r.read_bool();
    ulanext_mode_ = r.read_bool();
    nine_bit_first_written_ = r.read_bool();
    nine_bit_first_byte_ = r.read_u8();
    global_transparency_ = r.read_u8();
    sprite_transparency_ = r.read_u8();
    tilemap_transparency_ = r.read_u8();
    // Layer 2 palette priority slots — must be paired with the matching
    // save_state writer above. The L2/sprite dpram word in VHDL holds
    // priority (bits 15:14) alongside the colour, so the priority is
    // re-derivable from rgb333_ alone for entries written via NR 0x44;
    // we still serialise it explicitly so 8-bit-write entries stay 0.
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < FULL_SIZE; ++i)
            layer2_priority_[p][i] = r.read_u8();
}
