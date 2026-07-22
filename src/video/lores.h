#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

class Ram;
class StateWriter;
class StateReader;

/// LoRes 128x96 chunky display generator.
///
/// A 1:1 model of the FPGA entity `lores`
/// (`cores/zxnext/src/video/lores.vhd`, 117 lines) plus the small amount of
/// surrounding `zxnext.vhd` wiring that owns the registers feeding it.
///
/// **LoRes is NOT a compositor layer.**  It substitutes its 8-bit pixel for
/// the ULA's pixel *inside the ULA slot* (zxnext.vhd:6980-6981):
///
///     ulalores_pixel_1 <= lores_pixel_1 when lores_pixel_en_1 = '1'
///                                       else ula_pixel_1;
///     ulatm_pixel_1    <= ('0' & ula_palette_select_1 & ulalores_pixel_1) ...
///
/// so the LoRes byte is an index into the SAME 256-entry ULA palette, in the
/// bank NR $43 bit 1 selects, and everything downstream (NR $14 global
/// transparency, NR $15 layer priority, the ULA/tilemap stencil and blend
/// modes, NR $68 bit 7) treats the result as if the ULA had produced it.
/// `Renderer::apply_lores` performs that substitution into the already
/// rendered ULA scanline; there is deliberately no fifth layer buffer.
///
/// The pixel generator itself is a pure function of its inputs, exactly as the
/// VHDL entity is: `coord_x` / `coord_y` / `address` / `pixel` / `pixel_en`
/// are static and take a `Params` bundle mirroring the entity's port list
/// (lores.vhd:37-61).  The non-static half of this class holds only the
/// register state (`nr_15_lores_en`, NR $32/$33/$6A) and the per-scanline
/// snapshot of it.
///
/// Test plan: doc/testing/LORES-TEST-PLAN-DESIGN.md
class Lores {
public:
    // -----------------------------------------------------------------
    // Pixel generator (lores.vhd) — pure functions
    // -----------------------------------------------------------------

    /// The `lores` entity's input port bundle (lores.vhd:37-61).
    ///
    /// `clip_*` are the EFFECTIVE ULA clip values (`ula_clip_*_0`,
    /// zxnext.vhd:6771-6783 — including the y2 clamp to 0xBF), because the
    /// LoRes module is wired to the ULA's clip window and has none of its
    /// own (zxnext.vhd:4258-4261).  NR $1D is not decoded at all; the
    /// `nr_1d_lores_clip_*` signals exist only as commented-out
    /// declarations (zxnext.vhd:1167-1171, 6785-6793).
    struct Params {
        bool    mode           = false;  ///< lores.vhd:39 — 0 = 8-bit LoRes, 1 = Radastan
        bool    dfile          = false;  ///< lores.vhd:40 — Timex display file (Radastan only)
        bool    ulap_en        = false;  ///< lores.vhd:41 — ULA+ enabled (Radastan high nibble)
        uint8_t palette_offset = 0;      ///< lores.vhd:43 — NR $6A bits 3:0
        uint8_t scroll_x       = 0;      ///< lores.vhd:53 — NR $32
        uint8_t scroll_y       = 0;      ///< lores.vhd:54 — NR $33
        uint8_t clip_x1        = 0;      ///< lores.vhd:48-51 — ULA clip window
        uint8_t clip_x2        = 255;
        uint8_t clip_y1        = 0;
        uint8_t clip_y2        = 191;
    };

    /// lores.vhd:82 — `x <= hc_i(7 downto 0) + scroll_x_i`.
    /// 8-bit, wraps mod 256.  `hc_i` is the `phc` counter (zxnext.vhd:4250),
    /// i.e. the "practical" 0..255 display-column counter, not `hc`.
    static uint8_t coord_x(uint16_t phc, uint8_t scroll_x) {
        return static_cast<uint8_t>((phc & 0xFF) + scroll_x);
    }

    /// lores.vhd:84-87 — the scrolled row, with the hardware's mod-192 wrap:
    ///
    ///     y_pre         <= vc_i + ('0' & scroll_y_i);            -- 9 bit
    ///     y(7 downto 6) <= (y_pre(7 downto 6) + 1) when y_pre >= 192
    ///                                             else y_pre(7 downto 6);
    ///     y(5 downto 0) <= y_pre(5 downto 0);
    ///
    /// For `y_pre < 384` this is exactly `(vc + scroll_y) mod 192`.  For
    /// `y_pre >= 384` it is NOT — the 2-bit add wraps and the result lands
    /// outside the 96-row image.  That quirk is real hardware behaviour and
    /// is asserted by plan rows LR-109 / LR-110, not smoothed over here.
    static uint8_t coord_y(uint16_t vc, uint8_t scroll_y) {
        const uint16_t y_pre = static_cast<uint16_t>((vc & 0x1FF) + scroll_y) & 0x1FF;
        uint8_t hi = static_cast<uint8_t>((y_pre >> 6) & 0x03);
        if (y_pre >= 192)
            hi = static_cast<uint8_t>((hi + 1) & 0x03);
        return static_cast<uint8_t>((hi << 6) | (y_pre & 0x3F));
    }

    /// lores.vhd:91, 93-94 — the 8-bit-mode bank-5 offset.
    ///
    ///     lores_addr_pre           <= y(7 downto 1) & x(7 downto 1);
    ///     lores_addr(13 downto 11) <= (pre(13 downto 11) + 1) when y >= 96
    ///                                                        else pre(13:11);
    ///     lores_addr(10 downto 0)  <= pre(10 downto 0);
    ///
    /// Net effect: rows 0..47 land in `0x0000-0x17FF` and rows 48..95 in
    /// `0x2000-0x37FF`, skipping the `0x1800-0x1FFF` attribute area
    /// (lores.vhd:22-25).  The 3-bit `+1` field wraps, which is the address
    /// consequence of the y-wrap quirk above (LR-110).
    static uint16_t addr_lores(uint8_t x, uint8_t y) {
        const uint16_t pre = static_cast<uint16_t>(((y >> 1) << 7) | (x >> 1));
        uint16_t hi = static_cast<uint16_t>((pre >> 11) & 0x07);
        if (y >= 96)
            hi = static_cast<uint16_t>((hi + 1) & 0x07);
        return static_cast<uint16_t>((hi << 11) | (pre & 0x07FF));
    }

    /// lores.vhd:96 — the Radastan bank-5 offset.
    ///
    ///     lores_addr_rad <= dfile_i & y(7 downto 1) & x(7 downto 2);
    ///
    /// 64 bytes per row, NO `+0x800` correction: 96 rows x 64 bytes = 0x1800,
    /// contiguous from the selected half's base (lores.vhd:27-30).
    static uint16_t addr_radastan(uint8_t x, uint8_t y, bool dfile) {
        return static_cast<uint16_t>((dfile ? 0x2000u : 0u)
                                     | (static_cast<uint16_t>(y >> 1) << 6)
                                     | (x >> 2));
    }

    /// lores.vhd:98 — mode mux over the two address generators.
    static uint16_t address(const Params& p, uint16_t phc, uint16_t vc) {
        const uint8_t x = coord_x(phc, p.scroll_x);
        const uint8_t y = coord_y(vc, p.scroll_y);
        return p.mode ? addr_radastan(x, y, p.dfile) : addr_lores(x, y);
    }

    /// lores.vhd:102, 106-107, 111 — the emitted 8-bit palette index.
    ///
    /// 8-bit mode:  `(data(7:4) + palette_offset) & data(3:0)` — the 4-bit
    ///              add wraps with no carry out; the low nibble is untouched.
    /// Radastan:    low nibble = `data(7:4)` when `x(1) = '0'` else
    ///              `data(3:0)`; high nibble = `palette_offset`, or
    ///              `"11" & palette_offset(1:0)` when ULA+ is enabled.
    ///
    /// @param x  the SCROLLED coordinate from coord_x (Radastan needs x(1)).
    static uint8_t pixel(const Params& p, uint8_t x, uint8_t data) {
        if (p.mode) {
            const uint8_t lo = (x & 0x02) ? static_cast<uint8_t>(data & 0x0F)
                                          : static_cast<uint8_t>(data >> 4);
            const uint8_t hi = p.ulap_en
                ? static_cast<uint8_t>(0x0C | (p.palette_offset & 0x03))
                : static_cast<uint8_t>(p.palette_offset & 0x0F);
            return static_cast<uint8_t>((hi << 4) | lo);
        }
        const uint8_t hi = static_cast<uint8_t>(
            ((data >> 4) + p.palette_offset) & 0x0F);
        return static_cast<uint8_t>((hi << 4) | (data & 0x0F));
    }

    /// lores.vhd:115 — the clip comparison, all four bounds INCLUSIVE:
    ///
    ///     lores_pixel_en_o <= '1' when (hc_i >= '0' & clip_x1_i)
    ///                              and (hc_i <= '0' & clip_x2_i)
    ///                              and (vc_i >= '0' & clip_y1_i)
    ///                              and (vc_i <= '0' & clip_y2_i) else '0';
    ///
    /// There is no separate display-area test: it falls out of the compare,
    /// because `phc(8)` is set outside the 256-pixel window and the effective
    /// `clip_y2` can never exceed 0xBF (the clamp at zxnext.vhd:6779-6783).
    ///
    /// This is the module output only.  The NR $15 bit 7 enable is a separate
    /// AND term one stage later (`lores_pixel_en_1 <= lores_pixel_en_1a and
    /// lores_en_1`, zxnext.vhd:6933) — the module is never gated and fetches
    /// from bank 5 every pixel regardless.
    static bool pixel_en(const Params& p, uint16_t phc, uint16_t vc) {
        const uint16_t h = static_cast<uint16_t>(phc & 0x1FF);
        const uint16_t v = static_cast<uint16_t>(vc  & 0x1FF);
        return h >= p.clip_x1 && h <= p.clip_x2 && v >= p.clip_y1 && v <= p.clip_y2;
    }

    // -----------------------------------------------------------------
    // Register state
    // -----------------------------------------------------------------

    /// Per-scanline snapshot of every LoRes register.
    ///
    /// zxnext.vhd:6768-6802 re-latches `lores_scroll_x_0`, `lores_scroll_y_0`,
    /// `lores_mode_0`, `lores_dfile_0` and `lores_palette_offset_0` on every
    /// rising CLK_7 (once per display pixel); `lores_en_0` is latched on
    /// CLK_14 with `sc(0)='1'` (6817).  jnext's model is line-accurate, so
    /// the finest grain available is one snapshot per scanline — the same
    /// limitation already accepted for every other layer.  Without it a
    /// mid-frame Copper/CPU write collapses the whole frame onto the
    /// end-of-frame value (beast.nex writes NR $15 mid-frame today).
    struct LineState {
        bool    enabled  = false;   ///< NR $15 bit 7 (zxnext.vhd:4948 reset '0')
        uint8_t scroll_x = 0;       ///< NR $32 (zxnext.vhd:4995 reset 0x00)
        uint8_t scroll_y = 0;       ///< NR $33 (zxnext.vhd:4997 reset 0x00)
        uint8_t nr6a     = 0;       ///< NR $6A (zxnext.vhd:5032-5034 reset 0x00)
    };

    void reset() {
        live_ = LineState{};
        per_line_.fill(LineState{});
    }

    /// NR $15 bit 7 — `nr_15_lores_en` (zxnext.vhd:5229).
    void set_enabled(bool v)  { live_.enabled = v; }
    bool enabled() const      { return live_.enabled; }

    /// NR $32 — `nr_32_lores_scrollx` (zxnext.vhd:5340).
    void    set_scroll_x(uint8_t v) { live_.scroll_x = v; }
    uint8_t scroll_x() const        { return live_.scroll_x; }

    /// NR $33 — `nr_33_lores_scrolly` (zxnext.vhd:5343).
    void    set_scroll_y(uint8_t v) { live_.scroll_y = v; }
    uint8_t scroll_y() const        { return live_.scroll_y; }

    /// NR $6A — bit 5 radastan, bit 4 dfile XOR, bits 3:0 palette offset
    /// (zxnext.vhd:5456-5458).  Bits 7:6 are not stored; the read-back mux
    /// hard-wires them to "00" (zxnext.vhd:6099), so they are masked away
    /// here too.
    void    set_nr6a(uint8_t v) { live_.nr6a = static_cast<uint8_t>(v & 0x3F); }
    uint8_t nr6a() const        { return live_.nr6a; }

    bool    radastan() const       { return (live_.nr6a & 0x20) != 0; }
    bool    dfile_xor() const      { return (live_.nr6a & 0x10) != 0; }
    uint8_t palette_offset() const { return static_cast<uint8_t>(live_.nr6a & 0x0F); }

    // -----------------------------------------------------------------
    // Per-scanline snapshot (same idiom as Renderer::snapshot_*_for_line)
    // -----------------------------------------------------------------

    void snapshot_for_line(int line) {
        if (line >= 0 && line < MAX_LINES)
            per_line_[static_cast<size_t>(line)] = live_;
    }
    void init_per_line() { per_line_.fill(live_); }
    LineState state_for_line(int line) const {
        return (line >= 0 && line < MAX_LINES)
                   ? per_line_[static_cast<size_t>(line)] : live_;
    }

    // -----------------------------------------------------------------
    // Bank-5 fetch
    // -----------------------------------------------------------------

    /// Dedicated bank-5 dual-port VRAM (zxnext.vhd:6558-6578).  LoRes reads
    /// it through PORT A, shared with the CPU (`vram_bank5_a0 <= ... else
    /// lores_addr`, zxnext.vhd:6631) — a raw 14-bit bank-5 offset
    /// (lores.vhd:56).  It is NOT an MMU translation and NEVER bank 7: the
    /// shadow-screen mux at zxnext.vhd:6651-6655 is on the ULA's port B only.
    void set_bank5_vram(const uint8_t* p) { bank5_vram_ = p; }

    /// Read one byte of bank 5.  Uses the dedicated BRAM when wired (Next
    /// machines); the physical-page-10 fallback mirrors `Ula::vram_read` and
    /// keeps standalone unit-test fixtures working.
    uint8_t read_bank5(uint16_t addr, Ram& ram) const;

    void save_state(StateWriter& w) const;
    void load_state(StateReader& r);

private:
    static constexpr int MAX_LINES = 320;

    LineState                            live_{};
    std::array<LineState, MAX_LINES>     per_line_{};
    const uint8_t*                       bank5_vram_ = nullptr;
};
