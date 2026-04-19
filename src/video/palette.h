#pragma once
#include <array>
#include <cstdint>

/// Convert a ZX RGB333 colour (3 bits each, as used internally on the ZX Next)
/// to ARGB8888.
uint32_t rgb333_to_argb8888(uint8_t r3, uint8_t g3, uint8_t b3);

/// Standard ZX Spectrum ULA palette (16 entries, ARGB8888).
/// Indices 0-7 are normal colours, 8-15 are bright versions.
extern const uint32_t kUlaPalette[16];

// ---------------------------------------------------------------------------
// Palette identifiers used by NextREG 0x43 bits 6:4
// ---------------------------------------------------------------------------

enum class PaletteId : uint8_t {
    ULA_FIRST       = 0,  // 000
    LAYER2_FIRST    = 1,  // 001
    SPRITE_FIRST    = 2,  // 010
    TILEMAP_FIRST   = 3,  // 011
    ULA_SECOND      = 4,  // 100
    LAYER2_SECOND   = 5,  // 101
    SPRITE_SECOND   = 6,  // 110
    TILEMAP_SECOND  = 7,  // 111
};

// ---------------------------------------------------------------------------
// PaletteManager — manages all ZX Next palette banks
// ---------------------------------------------------------------------------
//
// Internal storage is RGB333 (9-bit, packed into uint16_t).
// ARGB8888 lookup tables are rebuilt on write for zero-cost reads.
//
// NextREG registers:
//   0x40 — Palette Index (selects entry for read/write)
//   0x41 — Palette Value 8-bit (RRRGGGBB format, auto-increment optional)
//   0x43 — Palette Control (selects target palette, auto-inc disable, ULANext)
//   0x44 — Palette Value 9-bit (two consecutive writes per entry)
//   0x14 — Global transparency colour (Layer2/ULA/LoRes)
//   0x4B — Sprite transparency index
//   0x4C — Tilemap transparency index

class PaletteManager {
public:
    static constexpr int ULA_SIZE     = 16;   // indices 0-15 (standard mode)
    static constexpr int FULL_SIZE    = 256;  // Layer2 / Sprite / Tilemap

    PaletteManager();

    /// Reset all palettes to power-on defaults.
    void reset();

    // -----------------------------------------------------------------
    // NextREG 0x43 — Palette Control
    // -----------------------------------------------------------------

    /// Write to NextREG 0x43.
    ///   bits 6:4 = target palette for read/write (PaletteId)
    ///   bit 7    = disable auto-increment on write
    ///   bit 3    = active sprite palette (0=first, 1=second)
    ///   bit 2    = active layer2 palette (0=first, 1=second)
    ///   bit 1    = active ULA palette (0=first, 1=second)
    ///   bit 0    = enable ULANext mode
    void write_control(uint8_t val);
    uint8_t read_control() const { return control_; }

    // -----------------------------------------------------------------
    // NextREG 0x40 — Palette Index
    // -----------------------------------------------------------------

    void set_index(uint8_t idx);
    uint8_t get_index() const { return index_; }

    // -----------------------------------------------------------------
    // NextREG 0x41 — Palette Value (8-bit write)
    // -----------------------------------------------------------------
    // Format: RRRGGGBB — blue LSB is derived as (B1 | B0).
    void write_8bit(uint8_t val);

    // -----------------------------------------------------------------
    // NextREG 0x41 — Palette Value (8-bit read)
    // -----------------------------------------------------------------
    // VHDL zxnext.vhd:6038-6039 — reads nr_palette_dat(8 downto 1), i.e. the
    // upper 8 bits of the 9-bit RGB333 value at the currently selected
    // target palette + index. Pure read, does not mutate palette state.
    uint8_t read_8bit() const;

    // -----------------------------------------------------------------
    // NextREG 0x44 — Palette Value (9-bit write, two consecutive writes)
    // -----------------------------------------------------------------
    // First write:  RRRGGGBB (8 bits)
    // Second write: bit 0 = blue LSB (9th bit); bit 7 = L2 priority flag
    void write_9bit(uint8_t val);

    // -----------------------------------------------------------------
    // Colour lookups (return ARGB8888, used by renderers)
    // -----------------------------------------------------------------

    /// Look up ULA colour by index (0-15). Uses the active ULA palette.
    uint32_t ula_colour(uint8_t idx) const {
        return ula_argb_[active_ula_second_][idx & 0x0F];
    }

    /// Look up Layer 2 colour by 8-bit pixel value. Uses the active L2 palette.
    uint32_t layer2_colour(uint8_t idx) const {
        return layer2_argb_[active_l2_second_][idx];
    }

    /// Return the 8-bit RRRGGGBB value for a Layer 2 palette entry.
    /// Used for VHDL-accurate transparency comparison (zxnext.vhd:7121).
    uint8_t layer2_rgb8(uint8_t idx) const {
        uint16_t c = layer2_rgb333_[active_l2_second_][idx];
        uint8_t r3 = (c >> 6) & 0x07;
        uint8_t g3 = (c >> 3) & 0x07;
        uint8_t b3 = c & 0x07;
        return static_cast<uint8_t>((r3 << 5) | (g3 << 2) | (b3 >> 1));
    }

    /// Look up sprite colour by 8-bit pixel value. Uses the active sprite palette.
    uint32_t sprite_colour(uint8_t idx) const {
        return sprite_argb_[active_spr_second_][idx];
    }

    /// Look up tilemap colour by 4-bit pixel value. Uses the active tilemap palette.
    uint32_t tilemap_colour(uint8_t idx) const {
        return tilemap_argb_[active_tm_second_][idx];
    }

    // -----------------------------------------------------------------
    // Transparency
    // -----------------------------------------------------------------

    /// Global transparency colour index (NextREG 0x14, default 0xE3).
    /// Applies to Layer 2, ULA, and LoRes.
    void set_global_transparency(uint8_t val) { global_transparency_ = val; }
    uint8_t global_transparency() const { return global_transparency_; }

    /// Sprite transparency index (NextREG 0x4B, default 0xE3).
    void set_sprite_transparency(uint8_t val) { sprite_transparency_ = val; }
    uint8_t sprite_transparency() const { return sprite_transparency_; }

    /// Tilemap transparency index (NextREG 0x4C, default 0x0F, 4-bit).
    void set_tilemap_transparency(uint8_t val) { tilemap_transparency_ = val & 0x0F; }
    uint8_t tilemap_transparency() const { return tilemap_transparency_; }

    // -----------------------------------------------------------------
    // Active tilemap palette select (NR 0x6B bit 4)
    // -----------------------------------------------------------------
    //
    // VHDL authority: nr_6b_tm_palette_select drives the tilemap palette
    // lookup at render time.  It is a SEPARATE bit from the NR 0x43
    // palette-I/O target field; writes to NR 0x43 must not affect it.
    // The emulator routes NR 0x6B writes to this setter from the NR 0x6B
    // write handler in core/emulator.cpp.

    /// Set the active tilemap palette (false = palette 0, true = palette 1).
    void set_active_tilemap_palette(bool second) { active_tm_second_ = second; }

    /// Query the active tilemap palette select (mostly for tests / debug).
    bool active_tilemap_palette() const { return active_tm_second_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    // Internal RGB333 storage (uint16_t, bits 8:0 = RRRGGGBBB).
    // [0] = first palette, [1] = second palette.
    std::array<uint16_t, ULA_SIZE>  ula_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> layer2_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> sprite_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> tilemap_rgb333_[2];

    // Cached ARGB8888 lookup tables (rebuilt on palette write).
    std::array<uint32_t, ULA_SIZE>  ula_argb_[2];
    std::array<uint32_t, FULL_SIZE> layer2_argb_[2];
    std::array<uint32_t, FULL_SIZE> sprite_argb_[2];
    std::array<uint32_t, FULL_SIZE> tilemap_argb_[2];

    // Control state
    uint8_t control_ = 0;         // NextREG 0x43 raw value
    uint8_t index_   = 0;         // NextREG 0x40 palette entry index

    // Decoded from control_ for fast access
    PaletteId target_palette_ = PaletteId::ULA_FIRST;
    bool auto_inc_disabled_   = false;
    bool active_ula_second_   = false;   // bit 1 of 0x43
    bool active_l2_second_    = false;   // bit 2 of 0x43
    bool active_spr_second_   = false;   // bit 3 of 0x43
    bool active_tm_second_    = false;   // driven by NR 0x6B bit 4 (VHDL nr_6b_tm_palette_select)
    bool ulanext_mode_        = false;   // bit 0 of 0x43

    // 9-bit write state machine
    bool     nine_bit_first_written_ = false;
    uint8_t  nine_bit_first_byte_    = 0;

    // Transparency
    uint8_t global_transparency_ = 0xE3;
    uint8_t sprite_transparency_ = 0xE3;
    uint8_t tilemap_transparency_ = 0x0F;

    // Helpers
    void write_entry(uint16_t rgb333);
    void advance_index();
    void rebuild_argb(PaletteId id, uint8_t idx);

    /// Convert 8-bit RRRGGGBB to 9-bit RGB333.
    /// Blue LSB is derived as (B1 | B0) per hardware spec.
    static uint16_t rrrgggbb_to_rgb333(uint8_t val);
};
