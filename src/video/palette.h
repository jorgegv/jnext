#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

/// Convert a ZX RGB333 colour (3 bits each, as used internally on the ZX Next)
/// to ARGB8888.
uint32_t rgb333_to_argb8888(uint8_t r3, uint8_t g3, uint8_t b3);

/// Canonical 16 ZX Spectrum colour values (ARGB8888).  These are the
/// hardware-defined RGB triplets a power-on Spectrum produces: indices
/// 0-7 are the standard colours, 8-15 the bright variants.  They are
/// used as the default contents of the ULA palette and as ground-truth
/// expected values in tests.  This is NOT a "palette" data structure —
/// it is the constant table of the 16 ZX colour ARGB values.
extern const uint32_t kZxStandardColours[16];

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
    // NR 0xFF — ULA+ palette poke side-channel (zxnext.vhd:6957-6958)
    // -----------------------------------------------------------------
    //
    // VHDL zxnext.vhd:6957 — `nr_ulatm_we <= … or nr_ff_we;`
    // VHDL zxnext.vhd:6958 — when nr_ff_we, the dpram address becomes
    //   ('0' & nr_43_palette_write_select(2) & "11" & port_bf3b_ulap_index)
    // i.e. bit 9 = 0 (ULA region of palette_utm), bit 8 = bank from
    // NR 0x43 b6, bits 7:6 = "11" (upper-quadrant ULA palette indices
    // 0xC0..0xFF), bits 5:0 = the 6-bit index latched from port 0xBF3B.
    //
    // VHDL zxnext.vhd:4919 — value byte:
    //   nr_palette_value <= (nr_wr_dat & (nr_wr_dat(1) or nr_wr_dat(0)))
    // i.e. RRRGGGBB expands to RRRGGGBBB with B0 = (B1 or B0).  This is
    // identical to the NR 0x41 8-bit expansion (rrrgggbb_to_rgb333).
    //
    // VHDL zxnext.vhd:4920 — priority is captured only on NR 0x44 writes;
    // for NR 0xFF, nr_palette_priority is "00".
    //
    // Storage is a separate 64-entry × 2-bank "ULA+ palette" buffer that
    // mirrors the upper quadrant of palette_utm in the VHDL.  Rendering
    // wiring (ULA+ runtime, G103) is deferred; this side-channel only
    // commits the poke and exposes a read-back accessor for tests and
    // future runtime consumers.
    void nr_ff_poke(bool bank_second, uint8_t bf3b_index, uint8_t byte);

    /// Read the 9-bit RRRGGGBBB value last poked into the ULA+ palette
    /// at (bank, index).  index is the 6-bit port 0xBF3B latch value
    /// (0..63), NOT the 8-bit ULA-region index.  bank: false = first
    /// ULA palette (NR 0x43 b6 = 0), true = second (b6 = 1).
    uint16_t ulap_poke_rgb333(bool bank_second, uint8_t bf3b_index) const {
        return ulap_poke_rgb333_[bank_second ? 1 : 0][bf3b_index & 0x3F];
    }

    /// Look up ULA+ colour as ARGB8888 (G103 runtime path).
    ///
    /// VHDL zxnext.vhd:6981 — the runtime read into palette_utm uses
    /// `('0' & ula_palette_select_1 & ulalores_pixel_1)` as the dpram
    /// address.  When ULA+ is enabled, `ulalores_pixel_1 = ula_pixel`
    /// has top 2 bits = "11" (zxula.vhd:535), so the low 6 bits of the
    /// 8-bit `ula_pixel` directly index the 64-entry ULA+ region of
    /// each bank.  Bank is selected by `ula_palette_select_1` =
    /// `nr_43_active_ula_palette` (NR 0x43 bit 1; zxnext.vhd:5393,6825).
    /// This is a SEPARATE bit from the NR 0x43 bit 6 used by NR 0xFF
    /// pokes for write addressing.
    ///
    /// @param bank_second  false = first ULA palette (NR 0x43 b1=0),
    ///                     true  = second ULA palette (NR 0x43 b1=1).
    /// @param bf3b_index   low 6 bits of the 8-bit ULA+ encoder pixel
    ///                     (i.e. `ula_pixel & 0x3F`).
    uint32_t ulap_colour(bool bank_second, uint8_t bf3b_index) const {
        const uint16_t rgb333 =
            ulap_poke_rgb333_[bank_second ? 1 : 0][bf3b_index & 0x3F];
        const uint8_t r3 = static_cast<uint8_t>((rgb333 >> 6) & 0x07);
        const uint8_t g3 = static_cast<uint8_t>((rgb333 >> 3) & 0x07);
        const uint8_t b3 = static_cast<uint8_t>( rgb333       & 0x07);
        return rgb333_to_argb8888(r3, g3, b3);
    }

    // -----------------------------------------------------------------
    // Colour lookups (return ARGB8888, used by renderers)
    // -----------------------------------------------------------------

    /// Look up ULA colour by index (0-15). Uses the active ULA palette.
    uint32_t ula_colour(uint8_t idx) const {
        return ula_argb_[active_ula_second_][idx & 0x0F];
    }

    /// Look up ULAnext colour as ARGB8888 by 8-bit pixel index.
    ///
    /// G102 — VHDL `zxnext.vhd:6981` runtime read uses
    /// `('0' & ula_palette_select_1 & ulalores_pixel_1)` as the 10-bit
    /// dpram address into `palette_utm`.  When `i_ulanext_en='1'`,
    /// `ula_pixel` is the full 8-bit value emitted by `compute_ulanext_pixel`
    /// (zxula.vhd:485-528), so the runtime reads from a 256-entry × 2-bank
    /// store.  Bank is selected by `nr_43_active_ula_palette` (NR 0x43
    /// bit 1; zxnext.vhd:5393, :6825) — the SAME bank as the standard
    /// ULA / ULA+ paths use at runtime.
    ///
    /// Storage shadow: writes to `PaletteId::ULA_FIRST/SECOND` populate
    /// both the legacy 16-entry array (for backward-compat `ula_colour`)
    /// and this wider 256-entry array.  Indices 0..15 therefore agree
    /// between the two stores; indices 16..255 are RRRGGGBB-identity by
    /// default and overridable via NR 0x40/0x41/0x44.
    ///
    /// @param bank_second  false = first ULA palette (NR 0x43 b1=0),
    ///                     true  = second ULA palette (NR 0x43 b1=1).
    /// @param idx          full 8-bit ULAnext-encoded pixel value.
    uint32_t ulanext_colour(bool bank_second, uint8_t idx) const {
        return ulanext_argb_[bank_second ? 1 : 0][idx];
    }

    /// Read the stored RGB333 value at (bank, idx) in the ULAnext 256-entry
    /// palette (G102).  Used for tests / save-state diagnostics; runtime
    /// readers should use `ulanext_colour` for ARGB8888.
    uint16_t ulanext_rgb333(bool bank_second, uint8_t idx) const {
        return ulanext_rgb333_[bank_second ? 1 : 0][idx];
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

    /// Return the 2-bit Layer 2 palette priority field for a palette
    /// entry on the active L2 palette.  Captured from `nr_wr_dat(7:6)`
    /// on the second NR 0x44 write (VHDL zxnext.vhd:4920) and stored
    /// in the dpram word's bits 15:14 (zxnext.vhd:7025, 7039).
    /// Bit 1 (nr_palette_priority(1) = NR 0x44 b7) is the bit that
    /// drives `layer2_priority_2` at zxnext.vhd:7050.
    uint8_t layer2_priority(uint8_t idx) const {
        return layer2_priority_[active_l2_second_][idx];
    }

    /// Convenience: true iff the high priority bit (NR 0x44 b7,
    /// = nr_palette_priority(1)) is set for the entry.  This is the
    /// bit the compositor uses to promote Layer 2 over sprites
    /// (VHDL zxnext.vhd:7050, 7220).
    bool layer2_priority_high(uint8_t idx) const {
        return (layer2_priority_[active_l2_second_][idx] & 0x02) != 0;
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

    // -----------------------------------------------------------------
    // Per-scanline palette snapshot — TASK-PER-SCANLINE-PALETTE-PLAN.md
    // -----------------------------------------------------------------
    //
    // Mirrors the existing per-scanline pattern (border, fallback,
    // ula_enabled, tilemap_scroll). Required for demos that change a
    // palette entry mid-frame via Copper to produce gradients
    // (e.g. beast.nex sky). Renderer flow:
    //
    //   start_frame();            // emulator at frame start
    //   …                         // emulation runs; palette writes
    //                             // append to change_log_ tagged with
    //                             // current_line_ (set per scanline)
    //   rewind_to_baseline();     // before render_frame
    //   for row in 0..H:
    //       apply_changes_for_line(row);
    //       render_scanline(row);

    /// Snapshot the live palette as the frame baseline and reset the
    /// per-frame change log. Called at the start of every frame.
    void start_frame();

    /// Update the scanline tag attached to subsequent palette writes.
    /// Called from Emulator::on_scanline. Default 0 at frame start.
    void set_current_line(int line) {
        current_line_ = static_cast<uint16_t>(line);
    }

    /// Restore the live palette to the frame baseline and reset the
    /// render cursor. Called once before per-scanline render.
    void rewind_to_baseline();

    /// Apply all logged changes whose line tag equals `line`. Cursor
    /// is monotonically advanced; the log is in scanline order so
    /// total work across a frame is O(change_count_).
    void apply_changes_for_line(int line);

    /// Apply every remaining log entry, regardless of its line tag.
    /// Called after the per-line render loop so palette writes that
    /// landed in vblank (line >= FB_HEIGHT, where the renderer never
    /// invokes apply_changes_for_line) still update the live state.
    /// Without this, rewind_to_baseline at the next frame's start
    /// snapshots a baseline that lacks those writes — workloads that
    /// finish their palette setup during vblank (e.g. tilemap_demo at
    /// CPU >= 14 MHz) end up rendering against an all-zero baseline
    /// every frame. Mirrors Tilemap::flush_remaining_nr6b_changes.
    void flush_remaining_changes();

    /// Number of palette changes recorded this frame (diagnostic).
    size_t change_log_size() const { return change_count_; }

    /// Static cap; further writes after this many in a frame are
    /// silently dropped (with a once-per-frame warn). Sized for the
    /// worst-case "Copper writes a palette entry on every scanline of
    /// every frame, several times" scenario the plan calls out.
    static constexpr size_t MAX_CHANGES_PER_FRAME = 4096;

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    // Internal RGB333 storage (uint16_t, bits 8:0 = RRRGGGBBB).
    // [0] = first palette, [1] = second palette.
    std::array<uint16_t, ULA_SIZE>  ula_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> layer2_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> sprite_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> tilemap_rgb333_[2];

    // G102 — ULAnext 256-entry × 2-bank ULA palette store (parallel mirror
    // of the legacy 16-entry `ula_rgb333_`).  VHDL `palette_utm` dpram
    // (zxnext.vhd:6960) is 1024×16 logically split into ULA bank0/bank1
    // (256 entries each) at addresses 0x000-0x0FF / 0x100-0x1FF, and
    // tilemap bank0/bank1 at 0x200-0x2FF / 0x300-0x3FF.  jnext stores ULA
    // in this mirror and tilemap in `tilemap_rgb333_`; runtime ULA reads
    // (`ulanext_colour`) consult this mirror when ULAnext is enabled.
    // Indices 0..15 mirror `ula_rgb333_` (always written together by
    // `apply_change` for ULA targets); indices 16..255 default to
    // RRRGGGBB-identity (matches layer2 / sprite default) and can be
    // overridden via NR 0x40/0x41/0x44 with `nr_palette_idx >= 16`.
    std::array<uint16_t, FULL_SIZE> ulanext_rgb333_[2];

    // 2-bit palette priority slots (VHDL zxnext.vhd:4920, 7025, 7050).
    // L2 palette only — sprite/tilemap rgb333 storage doesn't expose a
    // priority slot in the dpram word the renderer reads.
    std::array<uint8_t,  FULL_SIZE> layer2_priority_[2]{};

    // NR 0xFF / ULA+ palette poke storage — VHDL zxnext.vhd:6957-6958.
    // 64 entries × 2 banks; each entry is the 9-bit RGB333 value latched
    // by the most recent NR 0xFF write at (NR 0x43 b6, bf3b_index).
    // ULA+ runtime rendering (G103) reads from this in the future; for
    // now it is write-only-from-NR-0xFF / read-only-from-tests.
    std::array<uint16_t, 64> ulap_poke_rgb333_[2]{};

    // Cached ARGB8888 lookup tables (rebuilt on palette write).
    std::array<uint32_t, ULA_SIZE>  ula_argb_[2];
    std::array<uint32_t, FULL_SIZE> layer2_argb_[2];
    std::array<uint32_t, FULL_SIZE> sprite_argb_[2];
    std::array<uint32_t, FULL_SIZE> tilemap_argb_[2];

    // G102 — Cached ARGB8888 mirror of `ulanext_rgb333_` (rebuilt on write).
    std::array<uint32_t, FULL_SIZE> ulanext_argb_[2];

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
    void write_entry(uint16_t rgb333, uint8_t priority = 0);
    void advance_index();
    void rebuild_argb(PaletteId id, uint8_t idx);

    /// Convert 8-bit RRRGGGBB to 9-bit RGB333.
    /// Blue LSB is derived as (B1 | B0) per hardware spec.
    static uint16_t rrrgggbb_to_rgb333(uint8_t val);

    // ── Per-scanline change log (TASK-PER-SCANLINE-PALETTE-PLAN.md) ──
    //
    // Recorded by write_entry; replayed by apply_changes_for_line.
    // Static allocation by design (no per-frame heap churn).
    struct PaletteChange {
        uint16_t  line;        ///< 0..lines_per_frame-1
        PaletteId target;      ///< 4 palettes × 2 banks
        uint8_t   index;       ///< 0..255 (ULA wraps to 0..15)
        uint16_t  rgb333;      ///< 9-bit packed RRRGGGBBB
        uint8_t   priority;    ///< 2-bit nr_palette_priority (L2 only;
                               ///< 0 for ULA / sprite / tilemap targets
                               ///< and for NR 0x41 8-bit writes per
                               ///< zxnext.vhd:4920 else-branch)
    };

    std::array<PaletteChange, MAX_CHANGES_PER_FRAME> change_log_{};
    size_t   change_count_     = 0;
    uint16_t current_line_     = 0;
    size_t   render_cursor_    = 0;
    bool     overflow_warned_  = false;

    // Frame-start baseline of all palette state. Same shape as the live
    // arrays so rewind is a memcpy per palette.
    std::array<uint16_t, ULA_SIZE>  baseline_ula_rgb333_[2]{};
    std::array<uint16_t, FULL_SIZE> baseline_layer2_rgb333_[2]{};
    std::array<uint16_t, FULL_SIZE> baseline_sprite_rgb333_[2]{};
    std::array<uint16_t, FULL_SIZE> baseline_tilemap_rgb333_[2]{};
    std::array<uint32_t, ULA_SIZE>  baseline_ula_argb_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_layer2_argb_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_sprite_argb_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_tilemap_argb_[2]{};
    std::array<uint8_t,  FULL_SIZE> baseline_layer2_priority_[2]{};

    // G102 — Per-scanline replay baselines for the wider ULAnext store.
    // Mirrors the existing per-target baseline pattern.  Snapshotted at
    // start_frame and restored at rewind_to_baseline so per-scanline
    // ULAnext palette writes (Copper-driven gradient bands, etc.) replay
    // correctly per scanline.
    std::array<uint16_t, FULL_SIZE> baseline_ulanext_rgb333_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_ulanext_argb_[2]{};

    /// Apply a single change to the live palette state (no logging).
    /// Used by both the replay path and the rewind-then-replay loop.
    void apply_change(const PaletteChange& c);
};
