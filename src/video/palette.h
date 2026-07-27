#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/// Convert a ZX RGB333 colour (3 bits each, as used internally on the ZX Next)
/// to ARGB8888.
uint32_t rgb333_to_argb8888(uint8_t r3, uint8_t g3, uint8_t b3);

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
    static constexpr int FULL_SIZE    = 256;  // ULA / Layer2 / Sprite / Tilemap

    /// First ULA palette index of the 64-entry ULA+ region.
    ///
    /// VHDL zxnext.vhd:6958 — every ULA+ access (NR 0xFF poke write and
    /// port 0xFF3B palette read) drives the `palette_utm` dpram address
    /// as `'0' & nr_43_palette_write_select(2) & "11" & port_bf3b_ulap_index`.
    /// The hardwired `"11"` in bits 7:6 places the 64 ULA+ slots at ULA
    /// palette indices 0xC0..0xFF of the selected bank — the SAME dpram
    /// words the NR 0x41 / NR 0x44 palette stream writes.  There is one
    /// memory, so the two paths alias; this constant is where that
    /// aliasing is expressed.
    static constexpr int ULAP_BASE    = 0xC0;

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
    // NextREG 0x44 — Palette Value (9-bit read)
    // -----------------------------------------------------------------
    // VHDL zxnext.vhd:6047-6048:
    //   port_253b_dat <= nr_palette_dat(10 downto 9) & "00000" & nr_palette_dat(0);
    // i.e. bits 7:6 = stored 2-bit priority (only set for Layer 2 by NR 0x44
    // 2nd write per :4920; "00" elsewhere), bits 5:1 = constant zero, bit 0 =
    // stored blue LSB (the 9th colour bit at the currently selected target +
    // index). Pure read — does NOT mutate index, sub_idx, or priority state.
    // VERIFY4 / pass-4 — added because VHDL composes a non-trivial readback
    // (priority bits in bits 7:6, blue-LSB in bit 0) that the bare regs_[] echo
    // could not reproduce.
    uint8_t read_9bit() const;

    // -----------------------------------------------------------------
    // VHDL `nr_stored_palette_value` accessor — exposed via NR 0x28 read.
    // -----------------------------------------------------------------
    // VHDL zxnext.vhd:1190 declares the FF, :5398-5399 latches it from
    // `nr_wr_dat` on the FIRST half of an NR 0x44 9-bit write
    // (`nr_palette_sub_idx = '0'`), and :6004 surfaces it on the NR 0x28
    // read mux: `port_253b_dat <= nr_stored_palette_value;`. The
    // `nine_bit_first_byte_` shadow inside PaletteManager mirrors the
    // same FF (write_9bit at palette.cpp:332). Read-only; does not mutate
    // any palette state. V14-NMP-02 (Pass-14 verify-audit) — added so the
    // NR 0x28 read handler in Emulator can return the live VHDL signal
    // instead of the stale cached NR 0x28 last-write byte.
    uint8_t nine_bit_first_byte() const { return nine_bit_first_byte_; }

    // V21-NMP-01 (Pass-21 verify-audit): `nr_palette_sub_idx` exposure.
    // VHDL zxnext.vhd:1182 declares the single-bit FF. Reset / NR 0x40 /
    // NR 0x41 / NR 0x43 / NR 0x28 writes drive it to '0' (zxnext.vhd:5000,
    // 5376, 5382, 5395); NR 0x44 writes toggle it (zxnext.vhd:5403).
    // The same FF is surfaced as bit 7 of the NR 0x03 read mux
    // (zxnext.vhd:5894): `port_253b_dat <= nr_palette_sub_idx & ...`.
    // PaletteManager's `nine_bit_first_written_` shadow follows the
    // same lifecycle (palette.cpp:330-351 toggles, palette.cpp:220 etc.
    // reset paths) so it is the natural source for the NR 0x03 readback.
    bool nine_bit_first_written() const { return nine_bit_first_written_; }

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
    // Storage is the ULA palette itself, at indices ULAP_BASE + index of
    // the selected bank — see ULAP_BASE.  The VHDL has ONE `palette_utm`
    // dpram, so an NR 0xFF poke and an NR 0x41 / NR 0x44 write to ULA
    // index 0xC0..0xFF address the same word and each is visible to the
    // other.  jnext modelled these as two separate arrays until #64; that
    // split also silently dropped the ULA+ palette from save-states,
    // because only ula_rgb333_ is serialised.
    void nr_ff_poke(bool bank_second, uint8_t bf3b_index, uint8_t byte);

    /// Read the 9-bit RRRGGGBBB value held in the ULA+ palette region at
    /// (bank, index).  index is the 6-bit port 0xBF3B latch value (0..63),
    /// NOT the 8-bit ULA-region index — it is offset by ULAP_BASE here.
    /// bank: false = first ULA palette, true = second.  Which NR 0x43 bit
    /// selects the bank depends on the access: b6 for NR 0xFF pokes and
    /// port 0xFF3B reads (zxnext.vhd:6958), b1 for the render-time read
    /// (zxnext.vhd:6981) — see ulap_colour().
    uint16_t ulap_rgb333(bool bank_second, uint8_t bf3b_index) const {
        return ula_rgb333_[bank_second ? 1 : 0][ULAP_BASE | (bf3b_index & 0x3F)];
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
        return ula_argb_[bank_second ? 1 : 0][ULAP_BASE | (bf3b_index & 0x3F)];
    }

    // -----------------------------------------------------------------
    // Colour lookups (return ARGB8888, used by renderers)
    // -----------------------------------------------------------------

    /// Look up ULA colour as ARGB8888 by (bank, 8-bit ula_pixel).
    ///
    /// G102 — VHDL `zxnext.vhd:6960` declares ONE `palette_utm` dpram
    /// that is 256 entries × 2 ULA banks (and a separate 256×2 tilemap
    /// region not covered by this accessor).  VHDL `zxnext.vhd:6981`
    /// runtime read uses `('0' & ula_palette_select_1 & ulalores_pixel_1)`
    /// as the 10-bit dpram address; `ulalores_pixel_1 = ula_pixel` is the
    /// full 8-bit value emitted by the std-ULA encoder (zxula.vhd:543-553)
    /// or the ULAnext / ULA+ encoders.  All three encoder paths therefore
    /// index the same 256-entry × 2-bank store — the hardware has no
    /// narrower colour table.
    ///
    /// Bank is selected by `nr_43_active_ula_palette` (NR 0x43 bit 1;
    /// zxnext.vhd:5393, :6825).  This is a SEPARATE bit from NR 0x43 bit 6
    /// (`palette_write_select(2)`) which only routes write addressing.
    ///
    /// Power-on default: indices 0..0x1F hold the canonical 16 ZX colours
    /// mirrored across the std-ULA encoder's 32-state range (8 ink × 2
    /// BRIGHT × 2 ink/paper sub-cycles); indices 0x20..0xFF default to
    /// RRRGGGBB-identity.  All entries are overridable via NR 0x40/0x41/
    /// 0x44 with the FULL 8-bit `nr_palette_idx` as the address.
    ///
    /// @param bank_second  false = first ULA palette (NR 0x43 b1=0),
    ///                     true  = second ULA palette (NR 0x43 b1=1).
    /// @param ula_pixel    full 8-bit `ula_pixel` value emitted by any of
    ///                     the std/ULAnext/ULA+ encoders.
    uint32_t ula_colour(bool bank_second, uint8_t ula_pixel) const {
        return ula_argb_[bank_second ? 1 : 0][ula_pixel];
    }

    /// Read the stored RGB333 value at (bank, ula_pixel) in the 256-entry
    /// ULA palette.  Used for tests / save-state diagnostics; runtime
    /// readers should use `ula_colour` for ARGB8888.
    uint16_t ula_rgb333(bool bank_second, uint8_t ula_pixel) const {
        return ula_rgb333_[bank_second ? 1 : 0][ula_pixel];
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
    /// total work across a frame is O(change_log_.size()).
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
    size_t change_log_size() const { return change_log_.size(); }

    /// Initial change-log capacity, reserved at construction. GH #110:
    /// the log GROWS past this (amortized vector growth; clear() at
    /// frame start retains capacity, so steady state never allocates).
    /// A fixed cap of 4096 was measured 3.4× undersized: TX-1696's
    /// per-frame bulk palette DMA peaks at 14 111 writes/frame.
    static constexpr size_t CHANGE_LOG_RESERVE = 4096;

    /// Sanity bound (canary, GH #110): writes past this many in a frame
    /// are dropped from the LOG with a once-per-frame warn — they still
    /// apply to the live palette via apply_change, so only per-scanline
    /// replay fidelity degrades. No real workload approaches this
    /// (TX-1696, the heaviest known, peaks at ~14k/frame); hitting it
    /// points at a runaway-write bug, not at content.
    static constexpr size_t MAX_CHANGES_PER_FRAME = 1000000;

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    // Internal RGB333 storage (uint16_t, bits 8:0 = RRRGGGBBB).
    // [0] = first palette, [1] = second palette.
    //
    // G102 — ULA palette is a single 256-entry × 2-bank store, matching
    // VHDL `palette_utm` dpram (zxnext.vhd:6960).  All three ULA encoder
    // paths (std at zxula.vhd:543-553, ULAnext at :485-528, ULA+ at
    // :531-541) emit an 8-bit `ula_pixel` that indexes this store
    // directly.  Power-on init mirrors the canonical 16 ZX colours across
    // 0..0x1F (the std-ULA encoder's 32-state range) and uses
    // RRRGGGBB-identity for 0x20..0xFF (matches layer2/sprite defaults).
    std::array<uint16_t, FULL_SIZE> ula_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> layer2_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> sprite_rgb333_[2];
    std::array<uint16_t, FULL_SIZE> tilemap_rgb333_[2];

    // 2-bit palette priority slots (VHDL zxnext.vhd:4920, 7025, 7050).
    // L2 palette is the only one whose priority bits are CONSUMED by the
    // renderer (zxnext.vhd:7039 routes l2s_prgb(15) into layer2_priority_2),
    // but per VHDL the dpram word stores priority in bits 15:14 for ALL
    // palette types (palette_utm at :6972, palette_l2s at :7025), so a
    // read of NR 0x44 (zxnext.vhd:6048 returns nr_palette_dat(10:9)) sees
    // the priority field on every target — including ULA / sprite / tilemap
    // entries whose priority is otherwise unused. VERIFY4 / pass-4 added
    // ula_priority_ / sprite_priority_ / tilemap_priority_ to surface the
    // stored priority field on read_9bit().
    std::array<uint8_t,  FULL_SIZE> ula_priority_[2]{};
    std::array<uint8_t,  FULL_SIZE> layer2_priority_[2]{};
    std::array<uint8_t,  FULL_SIZE> sprite_priority_[2]{};
    std::array<uint8_t,  FULL_SIZE> tilemap_priority_[2]{};

    // NOTE: there is no separate ULA+ palette store — the 64 ULA+ slots
    // ARE ula_rgb333_[bank][ULAP_BASE .. ULAP_BASE+63], per the single
    // `palette_utm` dpram of zxnext.vhd:6958/6960.  See ULAP_BASE.

    // Cached ARGB8888 lookup tables (rebuilt on palette write).
    std::array<uint32_t, FULL_SIZE> ula_argb_[2];
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
    void write_entry(uint16_t rgb333, uint8_t priority = 0);
    void advance_index();

    /// Convert 8-bit RRRGGGBB to 9-bit RGB333.
    /// Blue LSB is derived as (B1 | B0) per hardware spec.
    static uint16_t rrrgggbb_to_rgb333(uint8_t val);

    // ── Per-scanline change log (TASK-PER-SCANLINE-PALETTE-PLAN.md) ──
    //
    // Recorded by write_entry; replayed by apply_changes_for_line.
    // Growable vector (GH #110), reserved to CHANGE_LOG_RESERVE at
    // construction; start_frame() clears it but keeps capacity, so
    // heap churn is amortized to zero once a run's peak is reached.
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

    std::vector<PaletteChange> change_log_;
    uint16_t current_line_     = 0;
    size_t   render_cursor_    = 0;
    bool     overflow_warned_  = false;

    // Frame-start baseline of all palette state. Same shape as the live
    // arrays so rewind is a memcpy per palette.
    std::array<uint16_t, FULL_SIZE> baseline_ula_rgb333_[2]{};
    std::array<uint16_t, FULL_SIZE> baseline_layer2_rgb333_[2]{};
    std::array<uint16_t, FULL_SIZE> baseline_sprite_rgb333_[2]{};
    std::array<uint16_t, FULL_SIZE> baseline_tilemap_rgb333_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_ula_argb_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_layer2_argb_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_sprite_argb_[2]{};
    std::array<uint32_t, FULL_SIZE> baseline_tilemap_argb_[2]{};
    std::array<uint8_t,  FULL_SIZE> baseline_layer2_priority_[2]{};

    /// Apply a single change to the live palette state (no logging).
    /// Used by both the replay path and the rewind-then-replay loop.
    void apply_change(const PaletteChange& c);
};
