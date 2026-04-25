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
// Standard ZX Spectrum ULA palette (kept for backward compatibility)
// ---------------------------------------------------------------------------

const uint32_t kUlaPalette[16] = {
    rgb333_to_argb8888(0, 0, 0),   // 0 Black
    rgb333_to_argb8888(0, 0, 6),   // 1 Blue
    rgb333_to_argb8888(6, 0, 0),   // 2 Red
    rgb333_to_argb8888(6, 0, 6),   // 3 Magenta
    rgb333_to_argb8888(0, 6, 0),   // 4 Green
    rgb333_to_argb8888(0, 6, 6),   // 5 Cyan
    rgb333_to_argb8888(6, 6, 0),   // 6 Yellow
    rgb333_to_argb8888(6, 6, 6),   // 7 White
    rgb333_to_argb8888(0, 0, 0),   // 8  Bright Black
    rgb333_to_argb8888(0, 0, 7),   // 9  Bright Blue
    rgb333_to_argb8888(7, 0, 0),   // 10 Bright Red
    rgb333_to_argb8888(7, 0, 7),   // 11 Bright Magenta
    rgb333_to_argb8888(0, 7, 0),   // 12 Bright Green
    rgb333_to_argb8888(0, 7, 7),   // 13 Bright Cyan
    rgb333_to_argb8888(7, 7, 0),   // 14 Bright Yellow
    rgb333_to_argb8888(7, 7, 7),   // 15 Bright White
};

// ---------------------------------------------------------------------------
// Default ULA palette as RGB333 values
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

    // Initialize ULA palettes with default colours.
    for (int p = 0; p < 2; ++p) {
        for (int i = 0; i < ULA_SIZE; ++i) {
            ula_rgb333_[p][i] = kDefaultUlaRgb333[i];
            ula_argb_[p][i] = rgb333_to_argb(kDefaultUlaRgb333[i]);
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
    }
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
        case PaletteId::ULA_SECOND: {
            uint8_t idx = index_ & 0x0F;  // matches write_entry masking
            rgb333 = ula_rgb333_[bank][idx];
            break;
        }
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
// NextREG 0x44 — 9-bit palette write (two consecutive writes)
// ---------------------------------------------------------------------------

void PaletteManager::write_9bit(uint8_t val)
{
    if (!nine_bit_first_written_) {
        // First write: store RRRGGGBB byte.
        nine_bit_first_byte_ = val;
        nine_bit_first_written_ = true;
    } else {
        // Second write: bit 0 = blue LSB (9th bit).
        // Reconstruct full 9-bit RGB333 from the two writes.
        uint8_t first = nine_bit_first_byte_;
        uint8_t r3 = (first >> 5) & 0x07;
        uint8_t g3 = (first >> 2) & 0x07;
        // Blue: high 2 bits from first byte bits 1:0, LSB from second byte bit 0.
        uint8_t b3 = static_cast<uint8_t>(((first & 0x03) << 1) | (val & 0x01));

        uint16_t rgb333 = static_cast<uint16_t>((r3 << 6) | (g3 << 3) | b3);
        write_entry(rgb333);

        nine_bit_first_written_ = false;
    }
}

// ---------------------------------------------------------------------------
// Internal: write a colour entry to the currently targeted palette
// ---------------------------------------------------------------------------

void PaletteManager::write_entry(uint16_t rgb333)
{
    // Per-scanline log: record BEFORE applying so that apply_change can
    // be reused for replay without re-logging. ULA gets the masked
    // index because that is what the live mutation uses below.
    if (change_count_ < MAX_CHANGES_PER_FRAME) {
        const bool is_ula = (target_palette_ == PaletteId::ULA_FIRST
                          || target_palette_ == PaletteId::ULA_SECOND);
        change_log_[change_count_++] = PaletteChange{
            current_line_,
            target_palette_,
            static_cast<uint8_t>(is_ula ? (index_ & 0x0F) : index_),
            rgb333,
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
                               static_cast<uint8_t>(index_), rgb333});

    advance_index();
}

void PaletteManager::apply_change(const PaletteChange& c)
{
    const int bank = (static_cast<int>(c.target) >= 4) ? 1 : 0;
    const uint32_t argb = rgb333_to_argb(c.rgb333);

    switch (c.target) {
        case PaletteId::ULA_FIRST:
        case PaletteId::ULA_SECOND: {
            const uint8_t idx = c.index & 0x0F;
            ula_rgb333_[bank][idx] = c.rgb333;
            ula_argb_[bank][idx] = argb;
            break;
        }
        case PaletteId::LAYER2_FIRST:
        case PaletteId::LAYER2_SECOND:
            layer2_rgb333_[bank][c.index] = c.rgb333;
            layer2_argb_[bank][c.index] = argb;
            break;
        case PaletteId::SPRITE_FIRST:
        case PaletteId::SPRITE_SECOND:
            sprite_rgb333_[bank][c.index] = c.rgb333;
            sprite_argb_[bank][c.index] = argb;
            break;
        case PaletteId::TILEMAP_FIRST:
        case PaletteId::TILEMAP_SECOND:
            tilemap_rgb333_[bank][c.index] = c.rgb333;
            tilemap_argb_[bank][c.index] = argb;
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
        baseline_ula_rgb333_[p]     = ula_rgb333_[p];
        baseline_ula_argb_[p]       = ula_argb_[p];
        baseline_layer2_rgb333_[p]  = layer2_rgb333_[p];
        baseline_layer2_argb_[p]    = layer2_argb_[p];
        baseline_sprite_rgb333_[p]  = sprite_rgb333_[p];
        baseline_sprite_argb_[p]    = sprite_argb_[p];
        baseline_tilemap_rgb333_[p] = tilemap_rgb333_[p];
        baseline_tilemap_argb_[p]   = tilemap_argb_[p];
    }
    change_count_     = 0;
    render_cursor_    = 0;
    current_line_     = 0;
    overflow_warned_  = false;
}

void PaletteManager::rewind_to_baseline()
{
    for (int p = 0; p < 2; ++p) {
        ula_rgb333_[p]     = baseline_ula_rgb333_[p];
        ula_argb_[p]       = baseline_ula_argb_[p];
        layer2_rgb333_[p]  = baseline_layer2_rgb333_[p];
        layer2_argb_[p]    = baseline_layer2_argb_[p];
        sprite_rgb333_[p]  = baseline_sprite_rgb333_[p];
        sprite_argb_[p]    = baseline_sprite_argb_[p];
        tilemap_rgb333_[p] = baseline_tilemap_rgb333_[p];
        tilemap_argb_[p]   = baseline_tilemap_argb_[p];
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

// ---------------------------------------------------------------------------
// Unused rebuild helper (reserved for future bulk updates)
// ---------------------------------------------------------------------------

void PaletteManager::rebuild_argb(PaletteId /*id*/, uint8_t /*idx*/)
{
    // Individual entries are updated inline in write_entry().
    // This is reserved for potential bulk rebuild operations.
}

void PaletteManager::save_state(StateWriter& w) const
{
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < ULA_SIZE; ++i) w.write_u16(ula_rgb333_[p][i]);
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
}

void PaletteManager::load_state(StateReader& r)
{
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < ULA_SIZE; ++i) {
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
}
